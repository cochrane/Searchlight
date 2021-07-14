#include <Adafruit_NeoPixel.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

#include "dccdecode.h"

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
   PB2: DCC Input
   PB3: LED Output
   Timer 0: Handles DCC
   Timer 1: Handles animation, ack pulse
*/

// INT0 pin is DCC
// That is to say pin 4, Arduino 2, PCINT18, PD2
const uint8_t ARDUINO_PIN_DCC_IN = 2;

// Message stored by the decoder in programming mode; length = 0 if not used.
DccMessage lastProgrammingMessage;

/*
 * Weird work-around.
 * The Adafruit Neopixel library uses micros() to check that enough time has elapsed before switching. However,
 * the normal Arduino/Wiring "micros()" command uses timer1 or timer0 but we're using both for other purposes.
 * Since the rest of the code ensures that the switching on doesn't happen that often anyway, we're delivering
 * dummy values that trick the library into just submitting things.
 * Long-term we need to modify or replace the library
 */
unsigned long micros() {
  static uint8_t counter = 0;
  return 400 * (counter++);
}

enum DecoderMode {
  DECODER_MODE_OPERATION = 0,
  DECODER_MODE_RESET_RECEIVED,
  DECODER_MODE_PROGRAMMING,
  DECODER_MODE_SENDING_ACK
};
volatile DecoderMode decoderMode = DECODER_MODE_OPERATION;

// WAIT_TIME_ACK: (8 Mhz / 1024) * 6 ms
// 1024 is from prescaler
#define WAIT_TIME_ACK 47

// Timer1 has fired.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    TCCR1 = 0; // Stop timer
    pixels.clear();
    pixels.show();
    decoderMode = DECODER_MODE_PROGRAMMING;
  }
}

uint8_t addressEeprom EEMEM = 3;
uint8_t address = 0;

void setup() {
  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)

  // Load address from EEPROM
  address = eeprom_read_byte(&addressEeprom);

  // Timer 0: Measures DCC signal
  setupDccTimer0();

  // DCC Input
  setupDccInt0PB2();
  
  pixels.clear();
  pixels.show();
  sei();
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

  decoderMode = DECODER_MODE_SENDING_ACK;
  
  // Timer 1: Turn off increased power after 5-7 ms
  OCR1A = WAIT_TIME_ACK;
  TCNT1 = 0;
  TCCR1 = (1 << CTC1) | (1 << CS13) | (1 << CS11) | (1 << CS10); // Normal mode, clear on OCR1A match, run immediately with CLK/1024
  TIMSK |= (1 << OCIE1A); // Interrupts on

  interrupts();
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

  if (!matchesLastMessage) {
    return;
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
      } else {
        // Read page mode page
        if (dccMessage.data[2] == pagedModePage || (dccMessage.data[2] == 0 && pagedModePage == 256)) {
          sendProgrammingAck();
        }
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
  
  if (dccMessage.length != 4) {
    return;
  }

  uint16_t cv = ((dccMessage.data[0] & 0x3) << 8 | dccMessage.data[1]) + 1;
  
  switch (dccMessage.data[0] & 0xC) {
    case 0x4:
      // Verify byte
      // Recommendation in RCN214: Never confirm for CVs we don't have
      if (hasCv(cv) && getCvValue(cv) == dccMessage.data[2]) {
        sendProgrammingAck();
      }
      break;
    case 0xC:
      // Write byte
      if (writeCvValue(cv, dccMessage.data[2])) {
        sendProgrammingAck();
      }
      break;
    case 0x8:
      if ((dccMessage.data[2] & 0xE0) == 0xE0) {
        // Bit manipulation
        uint8_t bitIndex = dccMessage.data[2] & 0x7;
        uint8_t bitValue = (dccMessage.data[2] & 0x8) >> 3;
        if ((dccMessage.data[2] & 0x10) == 0) {
          // Verify bit
          if (hasCv(cv)) {
            uint8_t value = getCvValue(cv);
            if (((value >> bitIndex) & 0x1) == bitValue) {
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
      break;
  }
}

uint8_t lastReadMessageNumber = 0;

int8_t currentSpeed = 0;
bool currentDirection = false;
bool lightOn = false;
bool f1 = false;
bool f2 = false;
bool f3 = false;

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

    if (decoderMode == DECODER_MODE_SENDING_ACK) {
      // There's an ACK currently going out so ignore all messages (which are just other "Programming" messages anyway)
      return;
    }

    if (dccMessage.length == 3 && dccMessage.data[0] == 0 && dccMessage.data[1] == 0) {
      // General reset command
      if (decoderMode == DECODER_MODE_OPERATION) {
        //DEBUG should be clear instead
        pixels.clear();
        pixels.show();
        lastProgrammingMessage.length = 0;
        decoderMode = DECODER_MODE_RESET_RECEIVED;
      }
      return;
    }

    if (decoderMode != DECODER_MODE_OPERATION && (dccMessage.data[0] & 0xF0) == 0x70) {
      decoderMode = DECODER_MODE_PROGRAMMING;
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
    } else if (dccMessage.length >= 3 && (dccMessage.data[0] & 0xC0) == 0xC0) {
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
      currentSpeed = currentSpeed << 2; // Super, super simple mapping to 127 steps, yes it won't reach 127, I don't care this is debug only
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
      f1 = (dccMessage.data[addressLength + 2] & 0x02) == 0x02;
      f2 = (dccMessage.data[addressLength + 2] & 0x04) == 0x04;
      f3 = (dccMessage.data[addressLength + 2] & 0x08) == 0x08;
    } else if (dccMessage.length == addressLength + 2 && (dccMessage.data[addressLength] & 0xE0) == 0x80) {
      // Functions F0-F4 100D-DDDD, with bit0 = F1, bit3 = F4, bit4 = F0 for some reason
      lightOn = (dccMessage.data[addressLength] & 0x10) == 0x10;
      f1 = (dccMessage.data[addressLength] & 0x01) == 0x01;ar
      f2 = (dccMessage.data[addressLength] & 0x02) == 0x02;
      f3 = (dccMessage.data[addressLength] & 0x04) == 0x04;
    } else {
      return;
    }
    
    if (currentDirection) {
      pixels.setPixelColor(0, pixels.Color(0, currentSpeed, lightOn ? 20 : 0));
    } else {
      pixels.setPixelColor(0, pixels.Color(currentSpeed, 0, lightOn ? 20 : 0));
    }
    pixels.setPixelColor(1, f1 ? 20 : 0, f2 ? 20 : 0, f3 ? 20 : 0);
    pixels.show();
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
