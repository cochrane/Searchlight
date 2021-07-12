#include <Adafruit_NeoPixel.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN_LED        3

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 2
#define NUM_VERIFY_PIXELS 2

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_LED, NEO_GRB + NEO_KHZ800);

/*
   Pin 1,2,3: Reserved for serial connection
   Pin 4/Arduino 2/PD2/Int0: Handles DCC
   Pin 15/Arduino 9/PB1/OC1A: Servo output
   Pin 24/Arduino A1/PC1/PCINT9: Input, move servo left
   Pin 25/Arduino A2/PC2/PCINT10: Input, move servo right
   Pin 26/Arduino A3/PC3/PCINT11: Input, programming mode
   Timer 0: Handles DCC
   Timer 1: Handles servo output
*/

// INT0 pin is DCC
// That is to say pin 4, Arduino 2, PCINT18, PD2
const uint8_t ARDUINO_PIN_DCC_IN = 2;

const uint8_t DCC_TIME_ONE = 58;
const uint8_t DCC_TIME_ZERO = 100;
const uint8_t WAIT_TIME = (uint16_t(DCC_TIME_ONE) + uint16_t(DCC_TIME_ZERO)) / 2;

const uint16_t DCC_ADDRESS = 10;

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
volatile DccReceiveState receiveState;
volatile uint8_t currentBit;
struct DccMessage {
  uint8_t length = 0;
  uint8_t data[10];
};
volatile DccMessage dccMessage;
volatile uint8_t currentMessageNumber = 0;

// Message stored by the decoder in programming mode; length = 0 if not used.
DccMessage lastProgrammingMessage;

// Low on DCC in received.
ISR(INT0_vect) {
  // Start a timer
  TCNT0 = 0;
  TCCR0B = (1 << CS01); // Clock/8
  OCR0A = WAIT_TIME;
}

// The timer started by ISR(INT0_vect) has fired.
ISR(TIMER0_COMPA_vect) {
  TCNT0 = 0;
  OCR0A = WAIT_TIME;
  TCCR0B = 0;

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
          currentBit = 1;
        } else {
          dccMessage.data[dccMessage.length] = 0;
          receiveState = DCC_RECEIVE_STATE_BYTE_READING;
        }
      }
      break;

  }
}

enum DecoderMode {
  DECODER_MODE_OPERATION = 0,
  DECODER_MODE_RESET_RECEIVED,
  DECODER_MODE_PROGRAMMING
};
DecoderMode decoderMode = DECODER_MODE_OPERATION;

// WAIT_TIME_ACK: (8 Mhz / 1024) * 6 ms
// 1024 is from prescaler
#define WAIT_TIME_ACK 47

// Timer1 has fired.
ISR(TIMER1_COMPA_vect) {
  if (decoderMode == DECODER_MODE_PROGRAMMING) {
    for (int i = 0; i < NUM_VERIFY_PIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 0));
    }
    pixels.show();
    TCCR1 = 0; // Stop timer
  }
}

uint8_t addressEeprom EEMEM = 3;
uint8_t address = 0;

uint8_t lastR = 0xFF, lastG = 0xFF, lastB = 0xFF;
void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  if (r == lastR && g == lastG && b == lastB) {
    return;
  }
  lastR = r;
  lastG = g;
  lastB = b;
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void setup() {
  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)

  // Load address from EEPROM
  address = eeprom_read_byte(&addressEeprom);

  // PB2: DCC Input  
  PORTB &= ~(1 << PB2);
  DDRB &= ~(1 << PB2);

  // Timer 0: Measures DCC signal
  OCR0A = WAIT_TIME;
  TCNT0 = 0;
  TCCR0A = 0;// Normal mode
  TCCR0B = (1 << CS01); // Clock/8
  TIMSK = (1 << OCIE0A); // Interrupts on

  // DCC Input interrupt
  MCUCR |= (1 << ISC01); // INT0 fires on falling edge
  GIMSK |= (1 << INT0);// Int0 is enabled
  
  interrupts();
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 0, 0));
  }
  pixels.show();
  writeRGB(0, 0, 0);
}

bool hasCv(uint16_t cvIndex) {
  return cvIndex == 1 || cvIndex == 8;
}

uint8_t getCvValue(uint16_t cvIndex) {
  switch (cvIndex) {
    case 1: return address;
    case 8: return 0x0D; // Manufacturer ID for home-made and public domain decoders
    default: return 0;
  }
}

bool writeCvValue(uint16_t cvIndex, uint8_t newValue) {
  if (cvIndex == 1) {
    address = newValue;
    eeprom_update_byte(&addressEeprom, address);
    return true;
  } else if (cvIndex == 8 && newValue == 8) {
    // Total reset of everything
    // There is special logic in the standard for when the reset takes longer, but we don't need that here.
    writeCvValue(1, 3); // Reset address to 3
    return true;
  }
  return false;
}

void sendProgrammingAck() {
  // Increase power consumption (and hope this is enough…)
  for (int i = 0; i < NUM_VERIFY_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
  }
  pixels.show();
  
  // Timer 1: Turn off increased power after 5-7 ms
  OCR1A = WAIT_TIME_ACK;
  TCNT1 = 0;
  TCCR1 = (1 << CTC1) | (1 << CS13) | (1 << CS11) | (1 << CS10); // Normal mode, clear on OCR1A match, run immediately with CLK/1024
  TIMSK |= (1 << OCIE1A); // Interrupts on
}

uint16_t pagedModePage = 1;

// Aufgerufen wenn wir im Programmiermodus sind und die Nachricht eine Programmiernachricht ist
void processProgrammingMessage() {
  // Compare and copy
  bool matchesLastMessage = dccMessage.length == lastProgrammingMessage.length;
  lastProgrammingMessage.length = dccMessage.length;
  for (int i = 0; i < dccMessage.length; i++) {
    matchesLastMessage = matchesLastMessage && dccMessage.data[i] == lastProgrammingMessage.data[i];
    lastProgrammingMessage.data[i] = dccMessage.data[i];
  }

  if (dccMessage.length == 3) {
    // Old register mode access
    uint8_t programmingRegister = (dccMessage.data[0] & 0x7) + 1;
    
    if (programmingRegister == 6) {
      if (dccMessage.data[1] == 1) {
        // Set page mode page. Supporting full page mode costs very little when we already support register mode.
        pagedModePage = dccMessage.data[2];
        if (pagedModePage == 0) {
          pagedModePage = 256;
        }
        sendProgrammingAck();
      }
    } else {
      uint16_t cv = programmingRegister;
      if (programmingRegister < 5) {
        cv = (pagedModePage - 1) * 4 + programmingRegister;
      } else if (programmingRegister == 5) {
        cv = 29;
      }
      if ((dccMessage.data[0] & 0x8) != 0) {
        // Write byte
        if (writeCvValue(cv, dccMessage.data[1])) {
          sendProgrammingAck();
        }
      } else {
        // Verify byte
        if (hasCv(cv) && getCvValue(cv) == dccMessage.data[1]) {
          sendProgrammingAck();
        }
      }
    }
    return;
  }
  
  if (dccMessage.length != 4 || !matchesLastMessage) {
    return;
  }

  uint16_t cv = ((dccMessage.data[0] & 0x3) << 8 | dccMessage.data[1]) + 1;
  
  if ((dccMessage.data[0] & 0xC) == 0x4) {
    // Verify byte
    // Recommendation in RCN214: Never confirm for CVs we don't have
    if (hasCv(cv) && getCvValue(cv) == dccMessage.data[2]) {
      sendProgrammingAck();
    }
    
  } else if ((dccMessage.data[0] & 0xC) == 0xC) {
    // Write byte
    if (writeCvValue(cv, dccMessage.data[2])) {
      sendProgrammingAck();
    }
  } else if ((dccMessage.data[0] & 0xC) == 0x8 && (dccMessage.data[2] & 0xE0) == 0xE0) {
    // Bit manipulation
    uint8_t bitIndex = dccMessage.data[2] & 0x7;
    uint8_t bitValue = (dccMessage.data[2] & 0x8) >> 3;
    if ((dccMessage.data[2] & 0x10) == 0) {
      // Verify bit
      if (hasCv(cv)) {
        uint8_t value = getCvValue(cv);
        if ((value & (1 << bitIndex)) == (bitValue << bitIndex)) {
          sendProgrammingAck();
        }
      } else {
        // Recommendation in RCN214: Confirm any bit value for CVs we don't have
        sendProgrammingAck();
      }
    } else {
      // Write bit
      if (hasCv(cv)) {
        uint8_t newValue = getCvValue(cv);
        if (bitValue) {
          newValue |= 1 << bitIndex;
        } else {
          newValue = newValue & ~(1 << bitIndex);
        }
        if (writeCvValue(cv, newValue)) {
          sendProgrammingAck();
        }
      }
    }
  }
}

uint8_t lastReadMessageNumber = 0;

int8_t currentSpeed = 0;
bool currentDirection = false;
bool lightOn = false;

void loop() {
  if (lastReadMessageNumber != currentMessageNumber) {
    // Process message
    lastReadMessageNumber = currentMessageNumber;
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < dccMessage.length; i++) {
      checksum ^= dccMessage.data[i];
    }
    if (checksum != 0 || dccMessage.length < 2) {
      return;
    }

    if (dccMessage.length == 3 && dccMessage.data[0] == 0 && dccMessage.data[1] == 0) {
      // General reset command
      for (int i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
      pixels.show();
      lastProgrammingMessage.length = 0;
      if (decoderMode == DECODER_MODE_OPERATION) {
        decoderMode = DECODER_MODE_RESET_RECEIVED;
      }
      return;
    }

    if (decoderMode != DECODER_MODE_OPERATION && (dccMessage.data[0] & 0xF0) == 0x70) {
      decoderMode == DECODER_MODE_PROGRAMMING;
      processProgrammingMessage();
      return;
    }

    decoderMode = DECODER_MODE_OPERATION;
    uint16_t messageAddress = 0;
    int8_t addressLength = 0;
    if ((dccMessage.data[0] & 0x80) == 0) {
      // Short address
      addressLength = 1;
      messageAddress = dccMessage.data[0];
    } else if (dccMessage.length >= 3 && (dccMessage.data[0] & 0xC0 == 0xC0)) {
      // Long address
      addressLength = 2;
      messageAddress = dccMessage.data[1] | (uint16_t(dccMessage.data[0] & 0x3F) << 8);
    } else {
      return;
    }

    if (messageAddress != address) {
      return; // Not our locomotive
    }

    if (dccMessage.length == addressLength + 2 && (dccMessage.data[addressLength] & 0xC0) == 0x40) {
      // Basic speed and direction: 01RG-GGGG
      // But! The first G is the last G
      // (they later used the free bit after R to give one bit more precision for G, and the bit was freed because they rethought how functions work).
      currentSpeed = ((dccMessage.data[addressLength] & 0xF) << 1) | ((dccMessage.data[addressLength] & 0x10) >> 4);
      currentSpeed << 2; // Super, super simple mapping to 127 steps, yes it won't reach 127, I don't care this is debug only
      currentDirection = (dccMessage.data[addressLength] & 0x20) == 0x20;
    } else if (dccMessage.length == addressLength + 3 && dccMessage.data[addressLength] == 0x3F) {
      // 127 step speed packet 0011-1111 RGGG-GGGG
      currentSpeed = dccMessage.data[addressLength + 1] & 0x7F;
      currentDirection = (dccMessage.data[addressLength + 1] & 0x80) == 0x80;
    } else if (dccMessage.length >= addressLength + 4 && dccMessage.data[addressLength] == 0x3C) {
      // Speed, direction and up to 32 functions 0011-1100 RGGG-GGGG
      currentSpeed = dccMessage.data[addressLength + 1] & 0x7F;
      currentDirection = (dccMessage.data[addressLength + 1] & 0x80) == 0x80;
      lightOn = (dccMessage.data[addressLength + 2] & 0x01) == 0x01;
    } else if (dccMessage.length == addressLength + 2 && (dccMessage.data[addressLength] & 0xE0) == 0x80) {
      // Functions F0-F4 100D-DDDD, with bit0 = F1, bit3 = F4, bit4 = F0 for some reason
      lightOn = (dccMessage.data[addressLength] & 0x10) == 0x10;
    } else {
      return;
    }
    
    if (currentDirection) {
      writeRGB(0, currentSpeed, lightOn ? 20 : 0);
    } else {
      writeRGB(currentSpeed, 0, lightOn ? 20 : 0);
    }
  } else {
    MCUCR |= (1 << SE); // Sleep enable, sleep mode 000 = Idle
    sleep_cpu();
  }
}

int main() {
  setup();
  for(;;) {
    loop();
  }
}