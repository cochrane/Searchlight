#include <stdint.h>

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

    static uint8_t adjustArrayIndex(uint8_t index) {
#ifdef COLOR_GRB
        uint8_t field = index % 3; // 0 becomes 1, 1 becomes 0, 2 stays 2
        if (field == 2) return index;
        return (index/3)*3 + (field ^ 0x1);
#else
        return index;
#endif
    }
};

// Ensure the sizes fit so we can work properly with the eeprom
static_assert(sizeof(ColorRGB) == 3);
static_assert(sizeof(ColorRGB[2]) == 6);

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
extern ColorRGB colorValues[];

class AnimationPlayer {
    uint8_t phaseTimestep;
    uint8_t phaseIndex;

    const uint8_t *select(const uint8_t *a, const uint8_t *b, uint8_t index);

    const AnimationPhase *getCurrentPhase();
public:
    AnimationPlayer(uint8_t initialAnimation);

    void setAnimation(uint8_t index);
    bool isComplete();
    void updateColor(const uint8_t *a, const uint8_t *b, uint8_t *out);
};
