#pragma once

#include <stdint.h>

namespace colors {
    enum ColorName: int8_t {
        RED = 0,
        GREEN,
        YELLOW,
        LUNAR,

        UNDEFINED,

        COUNT
    };

    struct ColorRGB {
        uint8_t r;
        uint8_t g;
        uint8_t b;

        ColorRGB() = default;
        constexpr ColorRGB(uint8_t red, uint8_t green, uint8_t blue): r(red), g(green), b(blue) {} 
    };

    // The actually used values for the colors given by the color names.
    extern colors::ColorRGB colorValues[];

    // Ensure the sizes fit so we can work properly with the eeprom
    static_assert(sizeof(ColorRGB) == 3);
    static_assert(sizeof(ColorRGB[2]) == 6);

    /*!
     * Called during setup, loads the color values stored in the EEPROM into RAM.
     */
    void loadColorsFromEeprom();
    /*!
     * For decoder reset: Put the default colors back into the EEPROM.
     */
    void restoreDefaultColorsToEeprom();
    /*!
     * For programming mode, value reading: The color at the index.
     * The index is i*3 + field, where i is the ColorName value, and field is 0 for r, 1 for g, 2 for b
     */
    uint8_t getColorValue(uint8_t index);
    /*!
     * For programming mode, value writing: Change the color at the index.
     * The index is i*3 + field, where i is the ColorName value, and field is 0 for r, 1 for g, 2 for b
     * Also updates the internal color used.
     */
    void writeColorValueToEeprom(uint8_t index, uint8_t color);
}