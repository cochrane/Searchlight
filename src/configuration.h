#pragma once

#include <stdint.h>

namespace config {

const uint8_t CV_INDEX_BRIGHTNESS = 47;
const uint8_t BRIGHTNESS_MAX = 100;

const uint8_t CV_INDEX_COLOR_ORDER = 64;
const uint8_t CV_INDEX_NUM_SIGNAL_HEADS = 65;
const uint8_t MAX_NUM_SIGNAL_HEADS = 3;
const uint8_t CV_INDEX_WORKAROUNDS = 66;

// CV29: base configuration
// In this decoder, CV29 isn't writable.
const uint8_t CONFIGURATION_BIT_28_SPEED_STEPS = (1 << 1); // Only relevant in locomotive mode
const uint8_t CONFIGURATION_BIT_LOCO_USE_EXTENDED_ADDRESS = (1 << 5); // Only relevant in locomotive mode
const uint8_t CONFIGURATION_BIT_EXTENDED_DECODER = (1 << 5); // Only relevant in accessory mode
const uint8_t CONFIGURATION_BIT_ADDRESSING_OUTPUT = (1 << 6); // Only relevant in accessory mode
const uint8_t CONFIGURATION_BIT_IS_ACCESSORY_DECODER = (1 << 7);
const uint8_t DEFAULT_CONFIGURATION = CONFIGURATION_BIT_IS_ACCESSORY_DECODER | CONFIGURATION_BIT_ADDRESSING_OUTPUT;

const uint8_t WORKAROUND_BIT_POM_ADDRESSING = (1 << 0);
const uint8_t WORKAROUND_VALID_BITS = WORKAROUND_BIT_POM_ADDRESSING;

struct Configuration {
    uint16_t address;
    uint8_t brightness;

    enum ColorOrder: uint8_t {
    COLOR_ORDER_RGB = 0,
    COLOR_ORDER_GRB
    };
    uint8_t colorOrder;

    uint8_t activeSignalHeads;

    uint8_t workarounds;
};

extern Configuration values;

void loadConfiguration();
void resetConfigurationToDefault();

bool setValueForCv(uint16_t cvIndex, uint8_t value);
uint16_t getValueForCv(uint16_t cvIndex);

uint8_t writeMaskForCv(uint16_t cvIndex);

}
