#include "dccdecode.h"

#include <avr/interrupt.h>

namespace dccdecode {

// Times for DCC, assuming a 1 MHz timer (which we approximately get with f_cpu = 8Mhz and a prescaler of 8)
const uint8_t DCC_TIME_ONE = 58;
const uint8_t DCC_TIME_ZERO = 100;
const uint8_t DCC_WAIT_TIME = (uint16_t(DCC_TIME_ONE) + uint16_t(DCC_TIME_ZERO)) / 2;

volatile Message message;
volatile uint8_t currentMessageNumber = 0;
uint8_t lastReadMessageNumber = 0;

const uint8_t DCC_PIN_MASK = (1 << PB2);

void setupInt0PB2() {
  // PB2: DCC Input  
  PORTB &= ~DCC_PIN_MASK;
  DDRB &= ~DCC_PIN_MASK;

  // DCC Input interrupt
  MCUCR |= (1 << ISC01); // INT0 fires on falling edge
  GIMSK |= (1 << INT0);// Int0 is enabled
}

void setupTimer0() {
  OCR0A = DCC_WAIT_TIME;
  TCCR0A = 0;// Normal mode
  TCCR0B = 0; // Timer stopped (for now, turned on in ISR(INT0_vect).)
  TIMSK |= (1 << OCIE0A); // Interrupts on
}

// Low on DCC in received.
ISR(INT0_vect) {
  // Start a timer
  TCNT0 = 0; // Reset
  TCCR0B = (1 << CS01); // Start the timer, Clock/8
}

// The timer started by ISR(INT0_vect) has fired.
ISR(TIMER0_COMPA_vect) {
  TCCR0B = 0; // Stop the timer

  // Read bit value: If it's still low, then it was a long 0 wave; if it has changed to 1, it was a short 1 wave
  bool bitValue = (PINB & DCC_PIN_MASK);
  receivedBit(bitValue);
}

enum DccReceiveState: uint8_t {
   // We are waiting for >= 10 bits that are all 1.
   // Any 0 bit before that resets the count
   // After ten 1s, a 0 is the first separator and indicates that the actual message bytes are following
  DCC_RECEIVE_STATE_PREAMBLE0 = 0,
  DCC_RECEIVE_STATE_PREAMBLE1,
  DCC_RECEIVE_STATE_PREAMBLE2,
  DCC_RECEIVE_STATE_PREAMBLE3,
  DCC_RECEIVE_STATE_PREAMBLE4,
  DCC_RECEIVE_STATE_PREAMBLE5,
  DCC_RECEIVE_STATE_PREAMBLE6,
  DCC_RECEIVE_STATE_PREAMBLE7,
  DCC_RECEIVE_STATE_PREAMBLE8,
  DCC_RECEIVE_STATE_PREAMBLE9,
  DCC_RECEIVE_STATE_PREAMBLE10,
  // Waiting for bits for the current byte to come in, always exactly 8.
  DCC_RECEIVE_STATE_BYTE_READING_BIT0,
  DCC_RECEIVE_STATE_BYTE_READING_BIT1,
  DCC_RECEIVE_STATE_BYTE_READING_BIT2,
  DCC_RECEIVE_STATE_BYTE_READING_BIT3,
  DCC_RECEIVE_STATE_BYTE_READING_BIT4,
  DCC_RECEIVE_STATE_BYTE_READING_BIT5,
  DCC_RECEIVE_STATE_BYTE_READING_BIT6,
  DCC_RECEIVE_STATE_BYTE_READING_BIT7,
  // Done with a byte, waiting for the separator bit.
  // If it is 0, another byte follows; if it is 1, the message is over.
  DCC_RECEIVE_STATE_AWAIT_SEPARATOR
};

static inline void receivedBit(bool bitValue) {
  static DccReceiveState receiveState;
  static uint8_t runningXor = 0;

  switch (receiveState) {
    case DCC_RECEIVE_STATE_PREAMBLE0:
    case DCC_RECEIVE_STATE_PREAMBLE1:
    case DCC_RECEIVE_STATE_PREAMBLE2:
    case DCC_RECEIVE_STATE_PREAMBLE3:
    case DCC_RECEIVE_STATE_PREAMBLE4:
    case DCC_RECEIVE_STATE_PREAMBLE5:
    case DCC_RECEIVE_STATE_PREAMBLE6:
    case DCC_RECEIVE_STATE_PREAMBLE7:
    case DCC_RECEIVE_STATE_PREAMBLE8:
    case DCC_RECEIVE_STATE_PREAMBLE9:
      if (bitValue) {
        receiveState = DccReceiveState(receiveState + 1);
      } else {
        receiveState = DCC_RECEIVE_STATE_PREAMBLE0;
      }
      break;
    case DCC_RECEIVE_STATE_PREAMBLE10:
      // Wait for first 0 bit indicating start of message
      if (!bitValue) {
        receiveState = DCC_RECEIVE_STATE_BYTE_READING_BIT0;
        message.length = 0;
        message.data[0] = 0;
        runningXor = 0;
      }
      break;
    case DCC_RECEIVE_STATE_BYTE_READING_BIT0:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT1:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT2:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT3:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT4:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT5:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT6:
    case DCC_RECEIVE_STATE_BYTE_READING_BIT7:
      message.data[message.length] = (message.data[message.length] << 1) | bitValue;
      receiveState = DccReceiveState(receiveState + 1);
      break;
    case DCC_RECEIVE_STATE_AWAIT_SEPARATOR:
      runningXor ^= message.data[message.length];
      message.length += 1;
      if (bitValue) {
        // End of packet
        receiveState = DCC_RECEIVE_STATE_PREAMBLE0;
        if (runningXor == 0) {
          currentMessageNumber += 1;
        }
      } else {
        // Another byte follows
        if (message.length >= sizeof(message.data)) {
          // We can't store (nor process) the byte; ignore this message and wait for next preamble
          receiveState = DCC_RECEIVE_STATE_PREAMBLE0;
        } else {
          message.data[message.length] = 0;
          receiveState = DCC_RECEIVE_STATE_BYTE_READING_BIT0;
        }
      }
      break;
  }
}

bool hasNewMessage() {
  uint8_t newMessageNumber = currentMessageNumber;
  bool changed = (lastReadMessageNumber != newMessageNumber);
  if (changed) {
    lastReadMessageNumber = newMessageNumber;
  }
  return changed;
}

}