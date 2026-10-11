// Character "DNA": every knob the generator understands. All lengths of the
// body are expressed in head units (h = height / headsTall) so proportions stay
// coherent from chibi to towering brute.
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

#include "math.hpp"

namespace cg {

enum class Archetype : uint8_t { Heroine, Bombshell, Dorito, Brute, Slender, Chibi, Amazon, Monster, Count };
enum class HairStyle : uint8_t { Spiky, Bob, Long, Ponytail, TwinTails, Hime, Mohawk, Buzz, Count };
enum class TopStyle : uint8_t { None, Tank, Tee, LongSleeve, Crop, Bandeau, Armor, Count };
enum class BottomStyle : uint8_t { Pants, Shorts, Briefs, Skirt, Loincloth, Count };
enum class GloveStyle : uint8_t { None, Short, Gauntlet, Long, Count };
enum class BootStyle : uint8_t { Shoe, Ankle, Knee, Thigh, Count };
enum class Pattern : uint8_t { Plain, Stripes, Checker, Count };
enum class EarStyle : uint8_t { Human, Elf, Cat, Count };
enum class PupilStyle : uint8_t { Round, Slit, Ring, Star, Count };

std::string_view toString(Archetype a);
std::string_view toString(HairStyle a);
std::optional<Archetype> archetypeFromString(std::string_view s);

struct BodyShape {
    float height = 1.68f;      // metres, crown to floor
    float headsTall = 6.8f;    // anime ~6.5-7.5, heroic 8, chibi 3-4
    float legRatio = 0.50f;    // crotch height / height
    float neckLength = 0.32f;  // head units
    float neckThick = 0.14f;   // radius, head units
    // Half widths in head units.
    float shoulderWidth = 0.80f;
    float chestWidth = 0.58f;
    float waistWidth = 0.39f;
    float hipWidth = 0.72f;
    float chestDepth = 0.36f;
    // Soft-tissue / mass sliders, 0..1 (values > 1 are "absurd" and allowed).
    float bust = 0.5f;
    float butt = 0.5f;
    float belly = 0.0f;
    float muscle = 0.2f;
    // Limb radii in head units.
    float thigh = 0.30f;
    float calf = 0.16f;
    float arm = 0.11f;
    float armLength = 1.0f;  // multiplier
    float handSize = 1.0f;
    float footSize = 1.0f;
    float headWidth = 1.0f;
    float posture = 0.5f;    // spine S-curve: chest out / butt back
    // Anatomy extensions.
    int armPairs = 1;        // 1 or 2 (four-armed)
    bool tail = false;
    float tailLength = 1.0f; // in multiples of leg length
    int horns = 0;           // 0 or 2
    float hornSize = 1.0f;
    EarStyle ears = EarStyle::Human;
};

struct FaceStyle {
    float eyeSize = 1.0f;     // multiplier
    float eyeSpacing = 1.0f;
    float eyeTilt = 0.05f;    // radians, + = tsurime (outer corner up)
    float eyeRound = 0.6f;    // 0 sharp/narrow, 1 big round
    float lashWeight = 0.8f;  // 0 masculine thin line, 1 heavy winged lashes
    float browThick = 0.5f;
    float mouthWidth = 1.0f;
    PupilStyle pupil = PupilStyle::Round;
    bool fang = false;
    bool blush = false;
};

struct Palette {
    Vec3 skin{1.0f, 0.86f, 0.77f};
    Vec3 hair{0.25f, 0.2f, 0.35f};
    Vec3 hairTip{0.25f, 0.2f, 0.35f};
    Vec3 iris{0.2f, 0.5f, 0.9f};
    Vec3 iris2{0.6f, 0.9f, 1.0f};
    Vec3 cloth[3] = {{0.2f, 0.25f, 0.6f}, {0.95f, 0.95f, 0.95f}, {0.85f, 0.2f, 0.25f}};
    Vec3 leather{0.3f, 0.2f, 0.15f};
    Vec3 metal{0.75f, 0.72f, 0.65f};
    Vec3 horn{0.9f, 0.85f, 0.75f};
};

struct Outfit {
    TopStyle top = TopStyle::Tee;
    BottomStyle bottom = BottomStyle::Pants;
    GloveStyle gloves = GloveStyle::None;
    BootStyle boots = BootStyle::Ankle;
    int shoulderPads = 0;  // 0 none, 1 left only, 2 both
    float padSize = 1.0f;
    bool belt = true;
    Pattern pattern = Pattern::Plain;
    bool tabard = false;        // front/back cloth panels with trim and V tips
    bool puffySleeves = false;  // bouffant upper sleeves
    bool bracers = false;       // leather forearm wraps
    bool thighStraps = false;   // straps on bare thighs
    bool corset = false;        // leather corset over the waist
    bool bandolier = false;     // diagonal chest strap with buckle
    int pouches = 0;            // belt pouches
    bool kneePads = false;      // metal knee caps
};

struct Genome {
    uint64_t seed = 0;
    Archetype archetype = Archetype::Heroine;
    float femininity = 1.0f;  // drives face defaults, not geometry directly
    BodyShape body;
    FaceStyle face;
    HairStyle hair = HairStyle::Bob;
    float hairVolume = 1.0f;
    float hairLength = 1.0f;
    Outfit outfit;
    Palette palette;
};

struct GenomeOptions {
    std::optional<Archetype> archetype;  // random archetype when empty
    float exaggeration = 0.0f;           // 0 = canon, 1 = cartoonishly pushed, 2 = absurd
};

Genome randomGenome(uint64_t seed, const GenomeOptions& opt = {});

}  // namespace cg
