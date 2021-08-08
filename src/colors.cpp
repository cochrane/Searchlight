#include "colors.h"

#include <avr/eeprom.h>
#include <string.h>

namespace Colors {
    const ColorRGB defaultColorValues[] = {
        ColorRGB(255, 0, 0), // RED - 2 - cv 48,49,50
        ColorRGB(0, 255, 0), // GREEN - 3 - cv 51,52,53
        ColorRGB(127, 127, 0), // YELLOW - 4 - cv 54,55,56
        ColorRGB(96, 96, 96), // LUNAR - 5 - cv 57,58,59
        ColorRGB(0, 0, 0), // UNDEFINED/BLACK - 6
    };

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


