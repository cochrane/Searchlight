#include "configuration.h"

#include <avr/eeprom.h>

namespace config {

Configuration valuesEeprom EEMEM;
Configuration values;

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
        /*.activeSignalHeads =*/ 1
    };

    eeprom_update_block(&defaultConfiguration, &valuesEeprom, sizeof(Configuration));
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
        case CV_INDEX_BRIGHTNESS: return values.brightness;
        case CV_INDEX_COLOR_ORDER: return uint8_t(values.colorOrder);
        case CV_INDEX_NUM_SIGNAL_HEADS: return values.activeSignalHeads;
        default: return 0xFFFF;
    }
}

bool setValueForCv(uint16_t cvIndex, uint8_t value) {
    switch(cvIndex) {
        case 1:
        case 18:
            values.address = (values.address & 0xFF00) | value;
            eeprom_update_word(&valuesEeprom.address, values.address);
            break;
        case 9:
        case 17:
            values.address = (values.address & 0x00FF) | (value << 8);
            eeprom_update_word(&valuesEeprom.address, values.address);
            break;
        case 29: return value == DEFAULT_CONFIGURATION; // pretend we can write it, but only to what it already was.
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
        default:
            return false;
    }
}

}
