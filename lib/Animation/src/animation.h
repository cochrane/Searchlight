#include <stdint.h>

struct AnimationPhase {
    int8_t length; // 0: Infinite, do not update colors; negative: Offset to jump back to, also does not update colors
    uint8_t flags; // Bit 7: "Is complete here", Bits 4-6: select first color (at start), Bits 0-3: select second color (at end)
};

// Must be defined elsewhere
extern AnimationPhase animations[];

class AnimationPlayer {
    uint8_t phaseTimestep;
    uint8_t phaseIndex;

    const uint8_t *select(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t index);

    const AnimationPhase *getCurrentPhase();
public:
    AnimationPlayer();

    void setAnimation(uint8_t index);
    bool isComplete();
    void updateColor(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t *out);
};
