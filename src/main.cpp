#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <string.h> // For memset

#include "dccdecode.h"
#include "signalhead.h"
#include "configuration.h"

// Skip the reset; we pinky promise not to send updates too often.
#define ws2812_resettime 0
#include <light_ws2812.h>
#include <light_ws2812.c>

// Which pin on the controller is connected to the NeoPixels?
#define PIN_LED        _BV(PB3)

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
  DECODER_MODE_EMERGENCY_STOP,
  DECODER_MODE_RESET_RECEIVED,
  DECODER_MODE_PROGRAMMING,
  DECODER_MODE_SENDING_ACK
};
volatile DecoderMode decoderMode = DECODER_MODE_OPERATION;

// WAIT_TIME_ACK: (8 Mhz / 1024) * 6 ms
// 1024 is from prescaler
#define WAIT_TIME_ACK 47

volatile uint8_t animationTimestep = 0;

// CV31 and 32 for access to extended data
// Not really used at the moment
uint8_t extendedRangeHighEeprom EEMEM;
uint8_t extendedRangeLowEeprom EEMEM;

// Color values
const uint8_t CV_INDEX_COLOR_BASE = 48;
const uint8_t CV_INDEX_COLOR_LENGTH = 3 * Colors::COUNT;

SignalHead signalHeads[config::MAX_NUM_SIGNAL_HEADS];
uint8_t signalHeadColors[3*config::MAX_NUM_SIGNAL_HEADS] = { 0 };

// Timer1 has fired.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    TCCR1 = 0; // Stop timer
    memset(signalHeadColors, 0, sizeof(signalHeadColors));
    ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
    decoderMode = DECODER_MODE_PROGRAMMING;
  } else if (decoderMode == DECODER_MODE_OPERATION) {
    // Runs as animation timer
    animationTimestep += 1;
  }
}

void setup() {
  // Load address from EEPROM
  config::loadConfiguration();
  Colors::loadColorsFromEeprom();

  // Timer 0: Measures DCC signal
  setupDccTimer0();

  // DCC Input
  setupDccInt0PB2();

  // Prepare timer 1 for animation purposes
  SignalHead::setupTimer1();
  
  ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
  sei();
}

// Values <= 255 are actual values, anything else means "CV not supported"
uint16_t getCvValue(uint16_t cvIndex) {
  if (cvIndex >= CV_INDEX_COLOR_BASE && cvIndex < CV_INDEX_COLOR_BASE + CV_INDEX_COLOR_LENGTH) {
    return Colors::getColorValue(cvIndex - CV_INDEX_COLOR_BASE);
  }

  switch (cvIndex) {
    case 7: return 1; // Decoder version number
    case 8: return 0x0D; // Manufacturer ID for home-made and public domain decoders
    case 31: return eeprom_read_byte(&extendedRangeHighEeprom);
    case 32: return eeprom_read_byte(&extendedRangeLowEeprom);
    default: return config::getValueForCv(cvIndex);
  }
}

bool writeCvValue(uint16_t cvIndex, uint8_t newValue) {
  if (cvIndex >= CV_INDEX_COLOR_BASE && cvIndex < CV_INDEX_COLOR_BASE + CV_INDEX_COLOR_LENGTH) {
    Colors::writeColorValueToEeprom(cvIndex - CV_INDEX_COLOR_BASE, newValue);
    return true;
  }

  switch (cvIndex) {
    case 8:
      if (newValue == 8) {
        // Total reset of everything
        // There is special logic in the standard for when the reset takes longer, but we don't need that here.
        writeCvValue(31, 0); // Extended area pointer (high)
        writeCvValue(32, 0); // Extended area pointer (low)
        Colors::restoreDefaultColorsToEeprom();
        config::resetConfigurationToDefault();
        return true;
      }
      return false;
    case 31:
      eeprom_update_byte(&extendedRangeHighEeprom, newValue);
      return true;
    case 32:
      eeprom_update_byte(&extendedRangeLowEeprom, newValue);
      return true;
    default:
      return config::setValueForCv(cvIndex, newValue);
  }
}

void sendProgrammingAck() {
  if (decoderMode == DECODER_MODE_OPERATION) {
    return;
  }
  // Increase power consumption (and hope this is enough…)
  memset(signalHeadColors, 255, config::values.activeSignalHeads*3);
  ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);

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
void processProgrammingMessage(const volatile uint8_t *relevantMessage, uint8_t messageLength) {
  // Compare and copy
  bool matchesLastMessage = messageLength == lastProgrammingMessage.length;
  lastProgrammingMessage.length = messageLength;
  for (int i = 0; i < messageLength; i++) {
    matchesLastMessage = matchesLastMessage && relevantMessage[i] == lastProgrammingMessage.data[i];
    lastProgrammingMessage.data[i] = relevantMessage[i];
  }

  if (!matchesLastMessage) {
    return;
  }

  if (lastProgrammingMessage.length == 3 && decoderMode == DECODER_MODE_PROGRAMMING) {
    // Old register mode access
    uint8_t programmingRegister = (relevantMessage[0] & 0x7) + 1;
    
    if (programmingRegister == 6) {
      if (lastProgrammingMessage.data[1] == 1) {
        // Set page mode page. Supporting full page mode costs very little when we already support register mode.
        // This will map 0 to 255 - that is by design and required by the spec.
        pagedModePage = lastProgrammingMessage.data[2] - 1;
        sendProgrammingAck();
      } else {
        // Read page mode page
        if ((lastProgrammingMessage.data[2] - 1) == pagedModePage) {
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
      if (lastProgrammingMessage.data[0] & 0x8) {
        // Write byte
        if (writeCvValue(cv, lastProgrammingMessage.data[1])) {
          sendProgrammingAck();
        }
      } else {
        // Verify byte
        if (getCvValue(cv) == lastProgrammingMessage.data[1]) {
          sendProgrammingAck();
        }
      }
    }
    return;
  }
  
  if (lastProgrammingMessage.length != 4) {
    return;
  }

  uint16_t cv = ((lastProgrammingMessage.data[0] & 0x3) << 8 | lastProgrammingMessage.data[1]) + 1;
  
  switch (lastProgrammingMessage.data[0] & 0xC) {
    case 0x4:
      // Verify byte
      // Recommendation in RCN214: Never confirm for CVs we don't have
      if (getCvValue(cv) == lastProgrammingMessage.data[2]) {
        sendProgrammingAck();
      }
      break;
    case 0xC:
      // Write byte
      if (writeCvValue(cv, lastProgrammingMessage.data[2])) {
        sendProgrammingAck();
      }
      break;
    case 0x8:
      if ((lastProgrammingMessage.data[2] & 0xE0) == 0xE0) {
        // Bit manipulation
        uint8_t bitIndex = lastProgrammingMessage.data[2] & 0x7;
        uint8_t bitValue = (lastProgrammingMessage.data[2] & 0x8) >> 3;
        if ((lastProgrammingMessage.data[2] & 0x10) == 0) {
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
      ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
      lastProgrammingMessage.length = 0;
      decoderMode = DECODER_MODE_RESET_RECEIVED;
    }
    return true;
  }

  if (decoderMode != DECODER_MODE_OPERATION && (dccMessage.data[0] & 0xF0) == 0x70) {
    decoderMode = DECODER_MODE_PROGRAMMING;
    processProgrammingMessage(dccMessage.data, dccMessage.length);
    return true;
  }

  if (decoderMode != DECODER_MODE_EMERGENCY_STOP) {
    decoderMode = DECODER_MODE_OPERATION;
  }

  if (dccMessage.length >= 3 && (dccMessage.data[0] & 0xC0) == 0x80) {
      // Accessory decoder
      if ((dccMessage.data[1] & 0x80) == 0x80) {
        // Basic accessory decoder: 10AA-AAAA 1AAA-DAAR

        // Address format is weird. See RCN213.
        uint16_t address = (dccMessage.data[0] & 0x3F) | (0x7 & ~((dccMessage.data[1] & 0x70) >> 4));
        uint8_t port = (dccMessage.data[1] & 0x6) >> 1;
        uint16_t addressRcn213 = (address << 2 | port) - 3;

        bool direction = dccMessage.data[1] & 0x1;
        bool turnOn = dccMessage.data[1] & 0x8;
        if (addressRcn213 == 2047 && !direction && !turnOn) {
          // Emergency turn off. Not sure it helps if the signal goes dark but why not.
          memset(signalHeadColors, 0, sizeof(signalHeadColors));
          ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
          decoderMode = DECODER_MODE_EMERGENCY_STOP;
        }

        // Every signal head gets three addresses: red/green, lunar/yellow, flashing on/off
        if (addressRcn213 < config::values.address ||
          addressRcn213 >= config::values.address + config::values.activeSignalHeads * 3) {
          return false;
        }
        decoderMode = DECODER_MODE_OPERATION;
        
        if (!turnOn) {
          // Message with flag turnOff gets sent whenever the command station thinks we've sent power
          // through the attached solenoid coils for long enough. For anything not controlling solenoids,
          // this message is completely irrelevant.

          // TODO If we were to add Railcom then this would be a place where we'd need to ack.
          return false;
        }

        // For POM:
        if (direction == false && turnOn == true && dccMessage.length > 3 && (dccMessage.data[2] & 0xF0) == 0xE0) {
          // POM commands
          processProgrammingMessage(&dccMessage.data[2], dccMessage.length - 2);
          return true;
        }

        uint8_t relativeAddress = uint8_t(addressRcn213 - config::values.address);
        uint8_t signalHead = relativeAddress/3;
        uint8_t relativeField = relativeAddress - signalHead*3;
        // Invert number so signal head 0 is the top one
        uint8_t invertedSignalHead = config::values.activeSignalHeads - 1 - signalHead;
        if (relativeField == 0) {
          // dir=0: red, dir=1: green
          signalHeads[invertedSignalHead].setColor(direction ? Colors::GREEN : Colors::RED);
        } else if (relativeField == 1) {
          // dir=0: lunar, dir=1: yellow
          signalHeads[invertedSignalHead].setColor(direction ? Colors::YELLOW : Colors::LUNAR);
        } else if (relativeField == 2) {
          // dir=0: flashing off, dir=1: flashing on
          signalHeads[invertedSignalHead].setFlashing(direction);
        }
      }
  }

  /*volatile const uint8_t *commandStart;
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

  if (commandLength == 4 && (commandStart[0] & 0xF0) == 0xE0) {
    // POM commands
    processProgrammingMessage(commandStart, commandLength);
    return true;
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
    signalHeads[0].setColor(Colors::RED);
  } else if (currentSpeed < 100) {
    signalHeads[0].setColor(Colors::YELLOW);
  } else {
    signalHeads[0].setColor(Colors::GREEN);
  }
  signalHeads[0].setFlashing(lightOn);

  if (!f1 && !f2) {
    signalHeads[1].setColor(Colors::RED);
  } else if (f1 && !f2) {
    signalHeads[1].setColor(Colors::GREEN);
  } else if (!f1 && f2) {
    signalHeads[1].setColor(Colors::YELLOW);
  } else if (f1 && f2) {
    signalHeads[1].setColor(Colors::LUNAR);
  }
  signalHeads[1].setFlashing(f3);*/

  return true;
}

uint8_t lastAnimationTimestep = 1;
bool updateAnimation() {
  if (animationTimestep == lastAnimationTimestep) {
    return false;
  }

  lastAnimationTimestep = animationTimestep;

  for (int i = 0; i < config::values.activeSignalHeads; i++) {
    signalHeads[i].updateColor(&signalHeadColors[i*3]);

    if (config::values.colorOrder == config::Configuration::COLOR_ORDER_GRB) {
      // Swap colors for WS2812
      uint8_t red = signalHeadColors[i*3 + 0];
      uint8_t green = signalHeadColors[i*3 + 1];
      signalHeadColors[i*3 + 0] = green;
      signalHeadColors[i*3 + 1] = red;
    }
  }
  if (config::values.brightness < config::BRIGHTNESS_MAX) {
    for (int i = 0; i < config::values.activeSignalHeads*3; i++) {
      signalHeadColors[i] = (uint16_t(signalHeadColors[i]) * config::values.brightness) / config::BRIGHTNESS_MAX;
    }
  }
  ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);

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
