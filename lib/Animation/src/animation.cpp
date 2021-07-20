#include "animation.h"

const uint8_t COLOR_BLACK[3] = { 0, 0, 0 };

AnimationPlayer::AnimationPlayer() {
    phaseTimestep = 0;
    phaseIndex = 0;
}

void AnimationPlayer::setAnimation(uint8_t index) {
    phaseIndex = index;
}

const uint8_t *AnimationPlayer::select(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t index) {
    switch (index) {
        case 0: return a;
        case 1: return b;
        case 2: return c;
        default:
        case 3: return COLOR_BLACK;
    }
}

const AnimationPhase *AnimationPlayer::getCurrentPhase() {
    AnimationPhase *currentPhase = &animations[phaseIndex];
    while (currentPhase->length < 0) {
        phaseIndex += currentPhase->length;
        currentPhase = &animations[phaseIndex];
    }
    return currentPhase;
}

bool AnimationPlayer::isComplete() {
    return (getCurrentPhase()->flags & 0x80) != 0;
}

uint8_t blend(uint8_t start, uint8_t end, uint8_t alpha, uint8_t alphaScale) {
    return uint8_t(int16_t(alpha) * int16_t(end - start) / int16_t(alphaScale)) + start;
}

void AnimationPlayer::updateColor(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t *out) {
    const AnimationPhase *currentPhase = getCurrentPhase();

    const uint8_t *inputStart = select(a, b, c, (currentPhase->flags >> 4) & 0x3);
    const uint8_t *inputEnd = select(a, b, c, currentPhase->flags & 0x3);

    const uint8_t phaseLength = currentPhase->length;

    for (int i = 0; i < 3; i++) {
        out[i] = blend(inputStart[i], inputEnd[i], phaseTimestep, phaseLength);
    }

    phaseTimestep += 1;
    if (currentPhase->length != 127 && phaseTimestep >= currentPhase->length) {
        phaseTimestep = 0;
        phaseIndex++;
    }
}