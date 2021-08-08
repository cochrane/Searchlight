#include <stdint.h>
#include <colors.h>

struct AnimationPhase {
    /*
     * Color selection here: The low three bits of the first nibble of "flags" is the start,
     * the low three bits of the second nibble is the end. Sounds weird but the idea is that
     * you can just tell what's going on by looking at the hex value for the byte, and then
     * maybe add 0x80 to set the top bit for "animation is complete".
     * Values:
     * 0: Color a (of the ones passed to updateColor)
     * 1: Color b (of the ones passed to updateColor)
     * anything higher: colorValues[index-2]
     */

    int8_t length; // 0: Infinite, do not update colors; negative: Offset to jump back to, also does not update colors
    uint8_t flags; // Bit 7: "Is complete here", Bits 4-6: select first color (at start), Bits 0-3: select second color (at end)
};

// Must be defined elsewhere
const extern AnimationPhase animations[];
extern Colors::ColorRGB Colors::colorValues[];

class AnimationPlayer {
    uint8_t phaseTimestep;
    uint8_t phaseIndex;

    const AnimationPhase *getCurrentPhase();
public:
    AnimationPlayer(uint8_t initialAnimation);

    void setAnimation(uint8_t index);
    bool isComplete();
    void updateColor(const uint8_t *a, const uint8_t *b, uint8_t *out);
};
