#include "colors.h"

#include <avr/eeprom.h>
#include <string.h>

namespace Colors {
    ColorRGB colorValues[ sizeof(defaultColorValues)/sizeof(ColorRGB) ];
    ColorRGB colorValuesStored[ sizeof(defaultColorValues)/sizeof(ColorRGB) ] EEMEM;

    void loadColorsFromEeprom() {
        eeprom_read_block(colorValues, colorValuesStored, sizeof(defaultColorValues));
    }

    void restoreDefaultColorsToEeprom() {
        eeprom_update_block(defaultColorValues, colorValuesStored, sizeof(defaultColorValues));
        memcpy(colorValues, defaultColorValues, sizeof(defaultColorValues));
    }

    uint8_t getColorValue(uint8_t index) {
        return ((uint8_t *) colorValues)[index];
    }

    void writeColorValueToEeprom(uint8_t index, uint8_t value) {
        eeprom_update_byte(&(((uint8_t *) colorValuesStored)[index]), value);
        ((uint8_t *) colorValues)[index] = value;
    }
}


