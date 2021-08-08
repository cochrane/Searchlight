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
 * PB2: DCC Input
 * PB3: LEDs
 * PB4: ACK
 * Timer 0: Handles DCC
 * Timer 1: Handles animation in normal mode, ack pulse in programming (same settings)
 */

// The pin to use for acknowledgements
#define ACK_PIN_MASK  _BV(PB4)

enum DecoderMode: uint8_t {
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

// Color values
const uint8_t CV_INDEX_COLOR_BASE = 48;
const uint8_t CV_INDEX_COLOR_LENGTH = 3 * Colors::COUNT;

SignalHead signalHeads[config::MAX_NUM_SIGNAL_HEADS];
uint8_t signalHeadColors[3*config::MAX_NUM_SIGNAL_HEADS];

void turnLedsOff() {
    memset(signalHeadColors, 0, sizeof(signalHeadColors));
    ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
}

// Timer1 has fired.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    TCCR1 = 0; // Stop timer
#ifdef ACK_VIA_LEDS
    turnLedsOff();
#else
    PORTB &= ~ACK_PIN_MASK;
#endif
    decoderMode = DECODER_MODE_PROGRAMMING;
  } else if (decoderMode == DECODER_MODE_OPERATION) {
    // Runs as animation timer
    animationTimestep += 1;
  }
}

void setup() {
  turnLedsOff();

  // Load address from EEPROM
  config::loadConfiguration();
  Colors::loadColorsFromEeprom();

  // Timer 0: Measures DCC signal
  setupDccTimer0();

  // DCC Input
  setupDccInt0PB2();

#ifndef ACK_VIA_LEDS
  DDRB |= ACK_PIN_MASK;
  PORTB &= ~ACK_PIN_MASK;
#endif

  // Prepare timer 1 for animation purposes
  SignalHead::setupTimer1();
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
        Colors::restoreDefaultColorsToEeprom();
        config::resetConfigurationToDefault();
        return true;
      }
      return false;
    default:
      return config::setValueForCv(cvIndex, newValue);
  }
}

void sendProgrammingAck() {
  if (decoderMode == DECODER_MODE_OPERATION) {
    return;
  }
#ifdef ACK_VIA_LEDS
  // Increase power consumption (and hope this is enough…)
  memset(signalHeadColors, 255, config::values.activeSignalHeads*3);
  ws2812_sendarray_mask(signalHeadColors, config::values.activeSignalHeads*3, PIN_LED);
#else
  PORTB |= ACK_PIN_MASK;
#endif

  decoderMode = DECODER_MODE_SENDING_ACK;
  
  // Timer 1: Turn off increased power after 5-7 ms
  OCR1A = WAIT_TIME_ACK;
  TCNT1 = 0;
  TCCR1 = (1 << CTC1) | (1 << CS13) | (1 << CS11) | (1 << CS10); // Normal mode, clear on OCR1A match, run immediately with CLK/1024
  TIMSK |= (1 << OCIE1A); // Interrupts on

  sei();
}

// Message stored by the decoder in programming mode; length = 0 if not used.
DccMessage lastProgrammingMessage;

// Page for paged mode addressing. We support this mainly because implementing it was fun.
// Note: Our internal page is 0-based even though the protocol transmits 1 based, because
// that makes the maths easier. Note also that the protocol requires that "0" in the message
// maps to page 256 (1 based), which happens automatically here.
uint8_t pagedModePage = 0;

void processRegisterModeMessage() {
    uint8_t programmingRegister = (lastProgrammingMessage.data[0] & 0x7) + 1;
    
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
}

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
    processRegisterModeMessage();
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
        uint8_t setBit = 1 << bitIndex;
        uint8_t bitValue = (lastProgrammingMessage.data[2] & 0x8) >> 3;
        if ((lastProgrammingMessage.data[2] & 0x10) == 0) {
          // Verify bit
          // Recommendation in RCN214: Confirm any bit value for CVs we don't have
          uint16_t value = getCvValue(cv);
          if (value > 0xFF || ((uint8_t(value) & setBit) == uint8_t(bitValue << bitIndex))) {
            sendProgrammingAck();
          }
        } else {
          // Write bit
          uint16_t newValue = getCvValue(cv);
          if (newValue <= 0xFF && (setBit & config::writeMaskForCv(cv))) {
            uint8_t newValueByte = uint8_t(newValue & 0xFF);
            if (bitValue) {
              newValueByte |= setBit;
            } else {
              newValueByte &= ~setBit;
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

inline void parseNewMessage() {
  if (decoderMode == DECODER_MODE_SENDING_ACK) {
    // There's an ACK currently going out so ignore all messages (which are just other "Programming" messages anyway)
    return;
  }

  if (dccMessage.isGeneralReset()) {
    // General reset command
    if (decoderMode == DECODER_MODE_OPERATION) {
      TCCR1 = 0; // Stop timer
      turnLedsOff();
      lastProgrammingMessage.length = 0;
      decoderMode = DECODER_MODE_RESET_RECEIVED;
    }
    return;
  }

  if (decoderMode != DECODER_MODE_OPERATION && dccMessage.isPossiblyProgramming()) {
    decoderMode = DECODER_MODE_PROGRAMMING;
    processProgrammingMessage(dccMessage.data, dccMessage.length);
    return;
  }

  if (decoderMode != DECODER_MODE_EMERGENCY_STOP) {
    decoderMode = DECODER_MODE_OPERATION;
  }

  if (dccMessage.isBasicAccessoryMessage()) {
    // Basic accessory decoder: 10AA-AAAA 1AAA-DAAR
    // Address format is weird. See RCN213.
    uint16_t decoderAddress = (dccMessage.data[0] & 0x3F) | (0x7 & ~((dccMessage.data[1] & 0x70) >> 4));
    uint8_t port = (dccMessage.data[1] & 0x6) >> 1;
    uint16_t outputAddress = (decoderAddress << 2 | port) - 3;

    /*
     * Weirdness with ESU command stations:
     * If you switch address DCC 10 to left, it interprets and transmits that as
     * decoder address = 2, port = 2
     * However, if you do a POM to set some CV for DCC 10, it interprets that as
     * decoder addres = 10, port = 0
     * Not sure why, it's very annoying.
     */
    bool direction = dccMessage.data[1] & 0x1;
    bool bitC = dccMessage.data[1] & 0x8; // For normal mode: "turn on/off". For PoM: "whole decoder/single output"
    // Note that RCN 214 deprecates this use of bitC for PoM, but my ESU command station still uses it, so it stays.
    if (outputAddress == 2047 && !direction && !bitC) {
      // Emergency turn off. Not sure it helps if the signal goes dark but why not.
      turnLedsOff();
      decoderMode = DECODER_MODE_EMERGENCY_STOP;
      return;
    }

    if (dccMessage.length == 6 && (dccMessage.data[2] & 0xF0) == 0xE0) {
      // POM, but is it our address?
      if ((config::values.workarounds & config::WORKAROUND_BIT_POM_ADDRESSING) && !bitC) {
        // Workaround: When switching "10", ESU command stations send "decoder 2 port 2" or whatever,
        // but when doing "POM set CV for address 10", they send "decoder 10 port 0". Maddening.
        // This workaround interprets that as meant for this decoder, which makes life a little
        // easier. Not sure it's a good idea though.
        // Note that it clears the "C" bit in that case.
        if (decoderAddress != config::values.address) {
          return;
        }
      } else {
        if (outputAddress < config::values.address ||
          outputAddress >= config::values.address + config::values.activeSignalHeads * 3) {
          return;
        }
      }
      processProgrammingMessage(&dccMessage.data[2], dccMessage.length - 2);
      return;
    }

    // Every signal head gets three addresses: red/green, lunar/yellow, flashing on/off
    if (outputAddress < config::values.address ||
      outputAddress >= config::values.address + config::values.activeSignalHeads * 3) {
      return;
    }
    decoderMode = DECODER_MODE_OPERATION;
    
    if (!bitC) {
      // Message with flag C=0/turnOff gets sent whenever the command station thinks we've sent power
      // through the attached solenoid coils for long enough. For anything not controlling solenoids,
      // this message is completely irrelevant.

      // TODO But if we were to add Railcom then this would be a place where we'd need to ack.
      return;
    }

    uint8_t relativeAddress = uint8_t(outputAddress - config::values.address);
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

uint8_t lastAnimationTimestep = 1;
inline bool updateAnimation() {
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

inline void loop() {
  bool didSomething = false;
  if (hasNewDccMessage()) {
    parseNewMessage();
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
