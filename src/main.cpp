#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <string.h> // For memset

#include "dccdecode.h"
#include "signalhead.h"

// Skip the reset; we pinky promise not to send updates too often.
#define ws2812_resettime 0
#include <light_ws2812.h>
#include <light_ws2812.c>

// Which pin on the controller is connected to the NeoPixels?
#define PIN_LED        _BV(PB3)

// How many NeoPixels are attached to the controller?
#define NUMPIXELS 2

/*
   PB2: DCC Input
   PB3: LED Output
   Timer 0: Handles DCC
   Timer 1: Handles animation, ack pulse
*/

// Message stored by the decoder in programming mode; length = 0 if not used.
DccMessage lastProgrammingMessage;

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

volatile uint8_t animationTimestep = 0;

SignalHead signalHeads[NUMPIXELS];
uint8_t signalHeadColors[3*NUMPIXELS] = { 0 };

// Timer1 has fired.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    TCCR1 = 0; // Stop timer
    memset(signalHeadColors, 0, sizeof(signalHeadColors));
    ws2812_sendarray_mask(signalHeadColors, sizeof(signalHeadColors), PIN_LED);
    decoderMode = DECODER_MODE_PROGRAMMING;
  } else if (decoderMode == DECODER_MODE_OPERATION) {
    // Runs as animation timer
    animationTimestep += 1;
  }
}

uint8_t addressEeprom EEMEM = 3;
uint8_t address = 0;

// CV31 and 32 for access to extended data
// Not really used at the moment
uint8_t extendedRangeHighEeprom EEMEM;
uint8_t extendedRangeLowEeprom EEMEM;

// Brightness
const uint8_t CV_INDEX_BRIGHTNESS = 47;
uint8_t brightnessEeprom EEMEM;
const uint8_t BRIGHTNESS_MAX = 100;
uint8_t brightness = BRIGHTNESS_MAX;

void setup() {
  // Load address from EEPROM
  address = eeprom_read_byte(&addressEeprom);
  brightness = eeprom_read_byte(&brightnessEeprom);

  // Timer 0: Measures DCC signal
  setupDccTimer0();

  // DCC Input
  setupDccInt0PB2();

  // Prepare timer 1 for animation purposes
  SignalHead::setupTimer1();
  
  ws2812_sendarray_mask(signalHeadColors, sizeof(signalHeadColors), PIN_LED);
  sei();
}

// Values <= 255 are actual values, anything else means "CV not supported"
uint16_t getCvValue(uint16_t cvIndex) {
  switch (cvIndex) {
    case 1: return address;
    case 7: return 1; // Decoder version number
    case 8: return 0x0D; // Manufacturer ID for home-made and public domain decoders
    case 31: return eeprom_read_byte(&extendedRangeHighEeprom);
    case 32: return eeprom_read_byte(&extendedRangeLowEeprom);
    case CV_INDEX_BRIGHTNESS: return brightness;
    default: return 0x100;
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
    writeCvValue(31, 0); // Extended area pointer (high)
    writeCvValue(32, 0); // Extended area pointer (low)
    writeCvValue(CV_INDEX_BRIGHTNESS, BRIGHTNESS_MAX); // Maximum brightness
    return true;
  } else if (cvIndex == 31) {
    eeprom_update_byte(&extendedRangeHighEeprom, newValue);
  } else if (cvIndex == 32) {
    eeprom_update_byte(&extendedRangeLowEeprom, newValue);
  } else if (cvIndex == CV_INDEX_BRIGHTNESS) {
    brightness = newValue;
    eeprom_update_byte(&brightnessEeprom, newValue);
  }
  return false;
}

void sendProgrammingAck() {
  // Increase power consumption (and hope this is enough…)
  memset(signalHeadColors, 255, sizeof(signalHeadColors));
  ws2812_sendarray_mask(signalHeadColors, sizeof(signalHeadColors), PIN_LED);

  decoderMode = DECODER_MODE_SENDING_ACK;
  
  // Timer 1: Turn off increased power after 5-7 ms
  OCR1A = WAIT_TIME_ACK;
  TCNT1 = 0;
  TCCR1 = (1 << CTC1) | (1 << CS13) | (1 << CS11) | (1 << CS10); // Normal mode, clear on OCR1A match, run immediately with CLK/1024
  TIMSK |= (1 << OCIE1A); // Interrupts on

  sei();
}

// Page for paged mode addressing. We support this mainly because implementing it was fun.
// Note: Our internal page is 0-based even though the protocol transmits 1 based, because
// that makes the maths easier. Note also that the protocol requires that "0" in the message
// maps to page 256 (1 based), which happens automatically here.
uint8_t pagedModePage = 0;

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
        // This will map 0 to 255 - that is by design and required by the spec.
        pagedModePage = dccMessage.data[2] - 1;
        sendProgrammingAck();
      } else {
        // Read page mode page
        if ((dccMessage.data[2] - 1) == pagedModePage) {
          sendProgrammingAck();
        }
      }
    } else {
      uint16_t cv = programmingRegister;
      if (programmingRegister < 5) {
        cv = pagedModePage * 4 + programmingRegister;
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
        if (getCvValue(cv) == dccMessage.data[1]) {
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
      if (getCvValue(cv) == dccMessage.data[2]) {
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
          // Recommendation in RCN214: Confirm any bit value for CVs we don't have
          uint16_t value = getCvValue(cv);
          if (value > 0xFF || ((value >> bitIndex) & 0x1) == bitValue) {
            sendProgrammingAck();
          }
        } else {
          // Write bit
          uint16_t newValue = getCvValue(cv);
          if (newValue <= 0xFF) {
            uint8_t newValueByte = uint8_t(newValue & 0xFF);
            if (bitValue) {
              newValueByte |= 1 << bitIndex;
            } else {
              newValueByte = newValue & ~(1 << bitIndex);
            }
            if (writeCvValue(cv, newValueByte)) {
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

bool parseNewMessage() {
  if (lastReadMessageNumber == currentMessageNumber) {
    return false;
  }

    // Process message
  lastReadMessageNumber = currentMessageNumber;
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < dccMessage.length; i++) {
    checksum ^= dccMessage.data[i];
  }
  if (checksum != 0 || dccMessage.length < 2) {
    return true;
  }

  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    // There's an ACK currently going out so ignore all messages (which are just other "Programming" messages anyway)
    return true;
  }

  if (dccMessage.length == 3 && dccMessage.data[0] == 0 && dccMessage.data[1] == 0) {
    // General reset command
    if (decoderMode == DECODER_MODE_OPERATION) {
      TCCR1 = 0; // Stop timer
      memset(signalHeadColors, 0, sizeof(signalHeadColors));
      ws2812_sendarray_mask(signalHeadColors, sizeof(signalHeadColors), PIN_LED);
      lastProgrammingMessage.length = 0;
      decoderMode = DECODER_MODE_RESET_RECEIVED;
    }
    return true;
  }

  if (decoderMode != DECODER_MODE_OPERATION && (dccMessage.data[0] & 0xF0) == 0x70) {
    decoderMode = DECODER_MODE_PROGRAMMING;
    processProgrammingMessage();
    return true;
  }

  decoderMode = DECODER_MODE_OPERATION;
  uint16_t messageAddress = 0;
  volatile const uint8_t *commandStart;
  uint8_t commandLength;
  bool isLocomotive = false;
  if ((dccMessage.data[0] & 0x80) == 0) {
    // Short address
    commandStart = dccMessage.data + 1;
    commandLength = dccMessage.length - 1;
    messageAddress = dccMessage.data[0];
    isLocomotive = true;
  } else if (dccMessage.length >= 3 && (dccMessage.data[0] & 0xC0) == 0xC0) {
    // Long address
    commandStart = dccMessage.data + 2;
    commandLength = dccMessage.length - 2;
    messageAddress = dccMessage.data[1] | (uint16_t(dccMessage.data[0] & 0x3F) << 8);
    isLocomotive = true;
  } else if (dccMessage.length >= 3 && (dccMessage.data[0] & 0xC0) == 0x80) {
    // Accessory decoder
    commandStart = dccMessage.data + 2;
    commandLength = dccMessage.length - 2;

    // Address format is weird. See RCN213.
    uint16_t address = (dccMessage.data[0] & 0x3F) | (0x7 & ~((dccMessage.data[1] & 0x70) >> 4));
    uint8_t port = (dccMessage.data[1] & 0x6) >> 1;
    uint16_t addressRcn213 = (address << 2 | port) - 3;
    if ((dccMessage.data[1] & 0x80) == 0x80) {
      bool direction = dccMessage.data[1] & 0x1;
      bool turnOn = dccMessage.data[1] & 0x8;
      // Basic accessory decoder: 10AA-AAAA 1AAA-DAAR
      // For POM: 
    } else if ((dccMessage.data[1] & 0x89) == 0x01) {
      // Extended accessory decoder: 10AA-AAAA 0AAA-0AA1 DDDD-DDDD
    }
  } else {
    return true;
  }

  if (messageAddress != address) {
    return true; // Not our locomotive
  }

  if (commandLength == 2 && (commandStart[0] & 0xC0) == 0x40) {
    // Basic speed and direction: 01RG-GGGG
    // But! The first G is the last G
    // (they later used the free bit after R to give one bit more precision for G, and the bit was freed because they rethought how functions work).
    currentSpeed = ((commandStart[0] & 0xF) << 1) | ((commandStart[0] & 0x10) >> 4);
    currentSpeed = currentSpeed << 2; // Super, super simple mapping to 127 steps, yes it won't reach 127, I don't care this is debug only
    currentDirection = (commandStart[0] & 0x20) == 0x20;
  } else if (commandLength == 3 && commandStart[0] == 0x3F) {
    // 127 step speed packet 0011-1111 RGGG-GGGG
    currentSpeed = commandStart[1] & 0x7F;
    currentDirection = (commandStart[1] & 0x80) == 0x80;
  } else if (commandLength >= 4 && commandStart[0] == 0x3C) {
    // Speed, direction and up to 32 functions 0011-1100 RGGG-GGGG
    currentSpeed = commandStart[1] & 0x7F;
    currentDirection = (commandStart[1] & 0x80) == 0x80;
    lightOn = (commandStart[2] & 0x01) == 0x01;
    f1 = (commandStart[2] & 0x02) == 0x02;
    f2 = (commandStart[2] & 0x04) == 0x04;
    f3 = (commandStart[2] & 0x08) == 0x08;
  } else if (commandLength == 2 && (commandStart[0] & 0xE0) == 0x80) {
    // Functions F0-F4 100D-DDDD, with bit0 = F1, bit3 = F4, bit4 = F0 for some reason
    uint8_t command = commandStart[0];
    lightOn = (command & 0x10);
    f1 = (command & 0x01) == 0x01;
    f2 = (command & 0x02) == 0x02;
    f3 = (command & 0x04) == 0x04;
  } else {
    return true;
  }
  
  if (currentSpeed < 50) {
    signalHeads[0].setColor(SignalHead::RED);
  } else if (currentSpeed < 100) {
    signalHeads[0].setColor(SignalHead::YELLOW);
  } else {
    signalHeads[0].setColor(SignalHead::GREEN);
  }
  signalHeads[0].setFlashing(lightOn);

  if (!f1 && !f2) {
    signalHeads[1].setColor(SignalHead::RED);
  } else if (f1 && !f2) {
    signalHeads[1].setColor(SignalHead::GREEN);
  } else if (!f1 && f2) {
    signalHeads[1].setColor(SignalHead::YELLOW);
  } else if (f1 && f2) {
    signalHeads[1].setColor(SignalHead::LUNAR);
  }
  signalHeads[1].setFlashing(f3);

  return true;
}

uint8_t lastAnimationTimestep = 1;
bool updateAnimation() {
  if (animationTimestep == lastAnimationTimestep) {
    return false;
  }

  lastAnimationTimestep = animationTimestep;

  for (int i = 0; i < NUMPIXELS; i++) {
    signalHeads[i].updateColor(&signalHeadColors[i*3]);
  }
  if (brightness < BRIGHTNESS_MAX) {
    for (int i = 0; i < sizeof(signalHeadColors); i++) {
      signalHeadColors[i] = (uint16_t(signalHeadColors[i]) * brightness) / BRIGHTNESS_MAX;
    }
  }
  ws2812_sendarray_mask(signalHeadColors, sizeof(signalHeadColors), PIN_LED);

  return true;
}

void loop() {
  bool didSomething = false;
  if (parseNewMessage()) {
    didSomething = true;
  }
  if (decoderMode == DECODER_MODE_OPERATION && updateAnimation()) {
    didSomething = true;
  }

  if (!didSomething) {
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
