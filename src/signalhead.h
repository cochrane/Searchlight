#pragma once

#include <stdint.h>
#include <animation.h>

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

    uint32_t updateColor();

    SignalHead();

    static void setupTimer1();

private:
    Color switchingFrom = RED;
    Color switchingTo = RED;
    Color nextAfter = UNDEFINED;
    bool isFlashing = false;

    const uint8_t *colorPointer(Color a);

    AnimationPlayer colorSwitching;
    AnimationPlayer flashing;
};

inline void SignalHead::setFlashing(bool flashing) {
    isFlashing = flashing;
}
