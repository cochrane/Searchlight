#pragma once

#include <stdint.h>
#include <animation.h>
#include <colors.h>

class SignalHead {
public:
    void setColor(Colors::ColorName color);
    void setFlashing(bool flashing);

    void updateColor(uint8_t *color);

    SignalHead();

    static void setupTimer1();

private:
    Colors::ColorName switchingFrom = Colors::RED;
    Colors::ColorName switchingTo = Colors::RED;
    Colors::ColorName nextAfter = Colors::UNDEFINED;
    bool isFlashing = false;

    AnimationPlayer colorSwitching;
    AnimationPlayer flashing;
};

inline void SignalHead::setFlashing(bool flashing) {
    isFlashing = flashing;
}
