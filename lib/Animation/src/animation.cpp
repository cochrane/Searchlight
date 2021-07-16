#include "animation.h"

#include <Arduino.h>

const uint8_t COLOR_RED[3] = { 255, 0, 0 };
const uint8_t COLOR_BLACK[3] = { 0, 0, 0 };

AnimationPlayer::AnimationPlayer() {
    phaseTimestep = 0;
    phaseIndex = 0;
}

void AnimationPlayer::setAnimation(uint8_t index) {
    phaseIndex = index;
}

const uint8_t *AnimationPlayer::select(const uint8_t *a, const uint8_t *b, uint8_t index) {
    switch (index) {
        case 0: return a;
        case 1: return b;
        case 2: return COLOR_RED;
        default:
        case 3: return COLOR_BLACK;
    }
}

bool AnimationPlayer::isComplete() {
    AnimationPhase *currentPhase = &animations[phaseIndex];
    while (currentPhase->length < 0) {
        phaseIndex += currentPhase->length;
        currentPhase = &animations[phaseIndex];
    }

    return (currentPhase->flags & 0x80) != 0;
}

uint8_t blend(uint8_t start, uint8_t end, uint8_t alpha, uint8_t alphaScale) {
    int16_t result = int16_t(alpha) * int16_t(end - start) / int16_t(alphaScale) + int16_t(start);
    return uint8_t(result);
}

void AnimationPlayer::updateColor(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    AnimationPhase *currentPhase = &animations[phaseIndex];
    while (currentPhase->length < 0) {
        phaseIndex += currentPhase->length;
        currentPhase = &animations[phaseIndex];
    }

    const uint8_t *inputStart = select(a, b, (currentPhase->flags >> 4) & 0x3);
    const uint8_t *inputEnd = select(a, b, currentPhase->flags & 0x3);

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