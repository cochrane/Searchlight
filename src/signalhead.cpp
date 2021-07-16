#include "signalhead.h"

#include <Adafruit_NeoPixel.h>
#include <avr/interrupt.h>

const uint8_t TIMESTEPS_FULLY_ON = 2;
const uint8_t TIMESTEPS_TURNING_OFF = 20;
const uint8_t TIMESTEPS_FULLY_OFF = 4;
const uint8_t TIMESTEPS_TURNING_ON = 20;

const uint8_t COLOR_SWITCHING_TIME = 20;
const uint8_t COLOR_SWITCHING_INTERMEDIATE_RED_TIME = 1;

const uint8_t COLOR_RED[3] = { 255, 0, 0 };
const uint8_t COLOR_GREEN[3] = { 0, 255, 0 };
const uint8_t COLOR_YELLOW[3] = { 255, 255, 0 };
const uint8_t COLOR_LUNAR[3] = { 20, 20, 255 };
const uint8_t COLOR_BLACK[3] = { 0, 0, 0 };

const uint8_t ANIMATION_START_FLASHING = 0;
const uint8_t ANIMATION_START_SWITCH_DIRECT = 5;
const uint8_t ANIMATION_START_SWITCH_INTERMEDIATE_RED = 8;

AnimationPhase animations[] = {
    // Flashing: A is signal color
    { TIMESTEPS_FULLY_ON, 0x80 | 0x00 },
    { TIMESTEPS_TURNING_OFF, 0x03 },
    { TIMESTEPS_FULLY_OFF, 0x33 },
    { TIMESTEPS_TURNING_ON, 0x30 },
    { -4, 0x00 },

    // Color change directly. A is start color, B is end color
    { COLOR_SWITCHING_TIME/2, 0x03 },
    { COLOR_SWITCHING_TIME/2, 0x31 },
    { 127, 0x80 | 0x11 },

    // Color change with intermediate red. A is start, B is end, C is red
    { COLOR_SWITCHING_TIME/4, 0x03 },
    { COLOR_SWITCHING_TIME/4, 0x32 },
    { COLOR_SWITCHING_INTERMEDIATE_RED_TIME, 0x22 },
    { COLOR_SWITCHING_TIME/4, 0x23 },
    { COLOR_SWITCHING_TIME/4, 0x31 },
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

SignalHead::SignalHead()
: switchingFrom(RED),
switchingTo(RED),
nextAfter(UNDEFINED),
isFlashing(false)
 {
    colorSwitching.setAnimation(7);
    flashing.setAnimation(ANIMATION_START_FLASHING);
}

void SignalHead::setColor(Color color) {
    if (switchingTo != color) {
        nextAfter = color;
    }
}

const uint8_t *SignalHead::colorPointer(SignalHead::Color a) {
    switch(a) {
        case RED: return COLOR_RED;
        case GREEN: return COLOR_GREEN;
        case YELLOW: return COLOR_YELLOW;
        case LUNAR: return COLOR_LUNAR;
        default: return COLOR_BLACK;
    }
}

uint32_t SignalHead::updateColor() {
    
    uint8_t colors[3];
    colorSwitching.updateColor(colorPointer(switchingFrom), colorPointer(switchingTo), colors);

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
        flashing.updateColor(colors, COLOR_BLACK, colors);
    }

    return Adafruit_NeoPixel::Color(colors[0], colors[1], colors[2]);
}
