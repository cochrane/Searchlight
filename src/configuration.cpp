#include "configuration.h"

#include <avr/eeprom.h>

namespace config {

Configuration valuesEeprom EEMEM;
Configuration values;

// CV31 and 32 for access to extended data
// Not really used at the moment
uint8_t extendedRangeHighEeprom EEMEM;
uint8_t extendedRangeLowEeprom EEMEM;

void loadConfiguration() {
    eeprom_read_block(&values, &valuesEeprom, sizeof(Configuration));
    if (values.activeSignalHeads > MAX_NUM_SIGNAL_HEADS) {
        values.activeSignalHeads = 1;
    }
}

void resetConfigurationToDefault() {
    const Configuration defaultConfiguration = {
        /*.address =*/ 1,
        /*.brightness =*/ 100,
        /*.colorOrder =*/ Configuration::COLOR_ORDER_GRB,
        /*.activeSignalHeads =*/ 1,
        /* .workarounds =*/ 0
    };

    eeprom_update_block(&defaultConfiguration, &valuesEeprom, sizeof(Configuration));

    setValueForCv(31, 0); // Extended area pointer (high)
    setValueForCv(32, 0); // Extended area pointer (low)

    loadConfiguration();
}

uint16_t getValueForCv(uint16_t cvIndex) {
    switch(cvIndex) {
        case 1:
        case 18:
            return uint8_t(values.address & 0xFF);
        case 9:
        case 17:
            return uint8_t((values.address >> 8) & 0xFF);
        
        case 29: return DEFAULT_CONFIGURATION;
        case 31: return eeprom_read_byte(&extendedRangeHighEeprom);
        case 32: return eeprom_read_byte(&extendedRangeLowEeprom);
        case CV_INDEX_BRIGHTNESS: return values.brightness;
        case CV_INDEX_COLOR_ORDER: return uint8_t(values.colorOrder);
        case CV_INDEX_NUM_SIGNAL_HEADS: return values.activeSignalHeads;
        case CV_INDEX_WORKAROUNDS: return values.workarounds;
        default: return 0xFFFF;
    }
}

bool setValueForCv(uint16_t cvIndex, uint8_t value) {
    switch(cvIndex) {
        case 1:
        case 18:
            values.address = (values.address & 0xFF00) | value;
            eeprom_update_word(&valuesEeprom.address, values.address);
            return true;
        case 9:
        case 17:
            values.address = (values.address & 0x00FF) | (value << 8);
            eeprom_update_word(&valuesEeprom.address, values.address);
            return true;
        case 29:
            return value == DEFAULT_CONFIGURATION; // pretend we can write it, but only to what it already was.
        case 31:
            eeprom_update_byte(&extendedRangeHighEeprom, value);
            return true;
        case 32:
            eeprom_update_byte(&extendedRangeLowEeprom, value);
            return true;
        case CV_INDEX_BRIGHTNESS:
            values.brightness = value;
            eeprom_update_byte(&valuesEeprom.brightness, values.brightness);
            return true;
        case CV_INDEX_COLOR_ORDER:
            values.colorOrder = Configuration::ColorOrder(value);
            eeprom_update_byte(&valuesEeprom.colorOrder, values.colorOrder);
            return true;
        case CV_INDEX_NUM_SIGNAL_HEADS:
            values.activeSignalHeads = value <= MAX_NUM_SIGNAL_HEADS ? value : MAX_NUM_SIGNAL_HEADS;
            eeprom_update_byte(&valuesEeprom.activeSignalHeads, values.activeSignalHeads);
            return true;
        case CV_INDEX_WORKAROUNDS:
            values.workarounds = value & WORKAROUND_VALID_BITS;
            eeprom_update_byte(&valuesEeprom.workarounds, values.workarounds);
            return true;
        default:
            return false;
    }
}

uint8_t writeMaskForCv(uint16_t cvIndex) {
    switch (cvIndex) {
        case CV_INDEX_WORKAROUNDS: return WORKAROUND_VALID_BITS;
        default: return 0xFF;
    }
}

}
