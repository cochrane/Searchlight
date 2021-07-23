#pragma once

#include <stdint.h>
#include <animation.h>

class SignalHead {
public:
    enum Color: int8_t {
        RED = 0,
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

    AnimationPlayer colorSwitching;
    AnimationPlayer flashing;
};

inline void SignalHead::setFlashing(bool flashing) {
    isFlashing = flashing;
}
