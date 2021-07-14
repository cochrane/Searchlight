#include "dccdecode.h"

#include <avr/interrupt.h>

// Times for DCC, assuming a 1 MHz timer (which we approximately get with f_cpu = 8Mhz and a prescaler of 8)
const uint8_t DCC_TIME_ONE = 58;
const uint8_t DCC_TIME_ZERO = 100;
const uint8_t DCC_WAIT_TIME = (uint16_t(DCC_TIME_ONE) + uint16_t(DCC_TIME_ZERO)) / 2;

void setupDccInt0PB2() {
  // PB2: DCC Input  
  PORTB &= ~(1 << PB2);
  DDRB &= ~(1 << PB2);

  // DCC Input interrupt
  MCUCR |= (1 << ISC01); // INT0 fires on falling edge
  GIMSK |= (1 << INT0);// Int0 is enabled
}

void setupDccTimer0() {
  OCR0A = DCC_WAIT_TIME;
  TCCR0A = 0;// Normal mode
  TCCR0B = 0; // Timer stopped (for now, turned on in ISR(INT0_vect).)
  TIMSK = (1 << OCIE0A) | (1 << OCIE1A); // Interrupts on
}

enum DccReceiveState {
   // We are waiting for >= 10 bits that are all 1.
   // Any 0 bit before that resets the count
   // After ten 1s, a 0 is the first separator and indicates that the actual message bytes are following
  DCC_RECEIVE_STATE_PREAMBLE = 0,
  // Waiting for bits for the current byte to come in, always exactly 8.
  DCC_RECEIVE_STATE_BYTE_READING,
  // Done with a byte, waiting for the separator bit.
  // If it is 0, another byte follows; if it is 1, the message is over.
  DCC_RECEIVE_STATE_AWAIT_SEPARATOR
};
DccReceiveState receiveState;
uint8_t currentBit;

volatile DccMessage dccMessage;
volatile uint8_t currentMessageNumber = 0;

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
  bool bitValue = (PINB & (1 << PINB2)) != 0;
  switch (receiveState) {
    case DCC_RECEIVE_STATE_PREAMBLE:
      if (bitValue) {
        currentBit++;
      } else {
        if (currentBit >= 10) {
          receiveState = DCC_RECEIVE_STATE_BYTE_READING;
          dccMessage.length = 0;
          dccMessage.data[0] = 0;
        }
        currentBit = 0;
      }
      break;
    case DCC_RECEIVE_STATE_BYTE_READING:
      dccMessage.data[dccMessage.length] = (dccMessage.data[dccMessage.length] << 1) | bitValue;
      currentBit += 1;
      if (currentBit == 8) {
        receiveState = DCC_RECEIVE_STATE_AWAIT_SEPARATOR;
      }
      break;
    case DCC_RECEIVE_STATE_AWAIT_SEPARATOR:
      dccMessage.length += 1;
      currentBit = 0;
      if (bitValue) {
        // End of packet
        receiveState = DCC_RECEIVE_STATE_PREAMBLE;
        currentMessageNumber += 1;
      } else {
        // Another byte follows
        if (dccMessage.length >= sizeof(dccMessage.data)) {
          // We can't store (nor process) the byte; ignore this message and wait for next preamble
          receiveState = DCC_RECEIVE_STATE_PREAMBLE;
        } else {
          dccMessage.data[dccMessage.length] = 0;
          receiveState = DCC_RECEIVE_STATE_BYTE_READING;
        }
      }
      break;

  }
}