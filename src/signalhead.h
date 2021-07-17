#pragma once

#include <stdint.h>
#include <animation.h>

struct ColorRGB {
#ifdef COLOR_GRB
    uint8_t g;
    uint8_t r;
    uint8_t b;
#else
    uint8_t r;
    uint8_t g;
    uint8_t b;
#endif

    ColorRGB() = default;
    constexpr ColorRGB(uint8_t red, uint8_t green, uint8_t blue): r(red), g(green), b(blue) {} 
};

class SignalHead {
public:
    enum Color: int8_t {
        RED,
        GREEN,
        YELLOW,
        LUNAR,

        UNDEFINED
    };

    void setColor(Color color);
    void setFlashing(bool flashing);

    void updateColor(uint8_t *color);

    SignalHead();

    static void setupTimer1();

private:
    Color switchingFrom = RED;
    Color switchingTo = RED;
    Color nextAfter = UNDEFINED;
    bool isFlashing = false;

    const ColorRGB *colorPointer(Color a);

    AnimationPlayer colorSwitching;
    AnimationPlayer flashing;
};

inline void SignalHead::setFlashing(bool flashing) {
    isFlashing = flashing;
}
