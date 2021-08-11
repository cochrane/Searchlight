#pragma once

#include <stdint.h>
#include <animation.h>
#include <colors.h>

class SignalHead {
public:
    void setColor(colors::ColorName color);
    void setFlashing(bool flashing);

    void updateColor(uint8_t *color);

    SignalHead();

    static void setupTimer1();

private:
    colors::ColorName switchingFrom = colors::RED;
    colors::ColorName switchingTo = colors::RED;
    colors::ColorName nextAfter = colors::UNDEFINED;
    bool isFlashing = false;

    AnimationPlayer colorSwitching;
    AnimationPlayer flashing;
};

inline void SignalHead::setFlashing(bool flashing) {
    isFlashing = flashing;
}
