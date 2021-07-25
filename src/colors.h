#pragma once

#include <stdint.h>

namespace Colors {
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
    extern Colors::ColorRGB colorValues[];

    const ColorRGB defaultColorValues[] = {
        ColorRGB(255, 0, 0), // RED - 2 - cv 48,49,50
        ColorRGB(0, 255, 0), // GREEN - 3 - cv 51,52,53
        ColorRGB(127, 127, 0), // YELLOW - 4 - cv 54,55,56
        ColorRGB(96, 96, 96), // LUNAR - 5 - cv 57,58,59
        ColorRGB(0, 0, 0), // UNDEFINED/BLACK - 6
    };

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