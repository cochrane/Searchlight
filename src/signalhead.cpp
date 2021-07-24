#include "signalhead.h"

#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <string.h>

const uint8_t TIMESTEPS_FULLY_ON = 2;
const uint8_t TIMESTEPS_TURNING_OFF = 20;
const uint8_t TIMESTEPS_FULLY_OFF = 4;
const uint8_t TIMESTEPS_TURNING_ON = 20;

const uint8_t COLOR_SWITCHING_TIME = 20;
const uint8_t COLOR_SWITCHING_INTERMEDIATE_RED_TIME = 1;

const ColorRGB defaultColorValues[] = {
    ColorRGB(255, 0, 0), // RED - 2 - cv 48,49,50
    ColorRGB(0, 255, 0), // GREEN - 3 - cv 51,52,53
    ColorRGB(127, 127, 0), // YELLOW - 4 - cv 54,55,56
    ColorRGB(96, 96, 96), // LUNAR - 5 - cv 57,58,59
    ColorRGB(0, 0, 0), // UNDEFINED/BLACK - 6
};

ColorRGB colorValues[ sizeof(defaultColorValues)/sizeof(ColorRGB) ];
ColorRGB colorValuesStored[ sizeof(defaultColorValues)/sizeof(ColorRGB) ] EEMEM;

const uint8_t ANIMATION_START_FLASHING = 0;
const uint8_t ANIMATION_START_SWITCH_DIRECT = 5;
const uint8_t ANIMATION_SWITCH_DONE = 7;
const uint8_t ANIMATION_START_SWITCH_INTERMEDIATE_RED = 8;

const AnimationPhase animations[] = {
    // Flashing: A is signal color
    { TIMESTEPS_FULLY_ON, 0x80 | 0x00 },
    { TIMESTEPS_TURNING_OFF, 0x06 },
    { TIMESTEPS_FULLY_OFF, 0x66 },
    { TIMESTEPS_TURNING_ON, 0x60 },
    { -4, 0x00 },

    // Color change directly. A is start color, B is end color
    { COLOR_SWITCHING_TIME/2, 0x06 },
    { COLOR_SWITCHING_TIME/2, 0x61 },
    { 127, 0x80 | 0x11 },

    // Color change with intermediate red. A is start, B is end
    { COLOR_SWITCHING_TIME/4, 0x06 },
    { COLOR_SWITCHING_TIME/4, 0x62 },
    { COLOR_SWITCHING_INTERMEDIATE_RED_TIME, 0x22 },
    { COLOR_SWITCHING_TIME/4, 0x26 },
    { COLOR_SWITCHING_TIME/4, 0x61 },
    { 127, 0x80 | 0x11 },
};

void SignalHead::setupTimer1() {
    // The ISR is not here but in main because it needs to do different things depending on stuff

    // Run roughly every twenty milliseconds
    OCR1A = 156;
    TCNT1 = 0;
    TCCR1 = (1 << CTC1) | (1 << CS13) | (1 << CS11) | (1 << CS10); // Normal mode, clear on OCR1A match, run immediately with CLK/1024
    TIMSK |= (1 << OCIE1A); // Interrupts on
}

void SignalHead::loadColorsFromEeprom() {
    eeprom_read_block(colorValues, colorValuesStored, sizeof(defaultColorValues));
}

void SignalHead::restoreDefaultColorsToEeprom() {
    eeprom_update_block(defaultColorValues, colorValuesStored, sizeof(defaultColorValues));
    memcpy(colorValues, defaultColorValues, sizeof(defaultColorValues));
}

uint8_t SignalHead::getColorValue(uint8_t index) {
    uint8_t adjusted = ColorRGB::adjustArrayIndex(index);
    return ((uint8_t *) colorValues)[adjusted];
}

void SignalHead::writeColorValueToEeprom(uint8_t index, uint8_t value) {
    uint8_t adjusted = ColorRGB::adjustArrayIndex(index);
    eeprom_update_byte(&(((uint8_t *) colorValuesStored)[adjusted]), value);
    ((uint8_t *) colorValues)[adjusted] = value;
}

SignalHead::SignalHead()
: switchingFrom(RED),
switchingTo(RED),
nextAfter(UNDEFINED),
isFlashing(false),
colorSwitching(ANIMATION_SWITCH_DONE),
flashing(ANIMATION_START_FLASHING)
{
}

void SignalHead::setColor(Color color) {
    if (switchingTo != color) {
        nextAfter = color;
    }
}

void SignalHead::updateColor(uint8_t *colors) {
    colorSwitching.updateColor((const uint8_t *) &colorValues[switchingFrom], (const uint8_t *) &colorValues[switchingTo], colors);

    if (colorSwitching.isComplete() && nextAfter != UNDEFINED) {
        switchingFrom = switchingTo;
        switchingTo = nextAfter;
        nextAfter = UNDEFINED;

        if (switchingFrom == RED || switchingTo == RED) {
            colorSwitching.setAnimation(ANIMATION_START_SWITCH_DIRECT);
        } else {
            colorSwitching.setAnimation(ANIMATION_START_SWITCH_INTERMEDIATE_RED);
        }
    }

    if (isFlashing || !flashing.isComplete()) {
        flashing.updateColor(colors, (const uint8_t *) &colorValues[UNDEFINED], colors);
    }
}
