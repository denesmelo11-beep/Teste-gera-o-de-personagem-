// Character "DNA": every knob the generator understands.
//
// Design rules:
//  * Most people are ordinary: height, fat, muscle and bone frame are drawn from
//    normal distributions, extremes live in the tails or in opt-in presets.
//  * Identity reads first from silhouette, hair, colour and the resting face,
//    so those carry most of the variety (age and persona drive them).
//  * Bodies are base meshes (underwear only): equipment is layered on top later.
//  * Body lengths are in head units (h = height / headsTall).
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

#include "math.hpp"

namespace cg {

enum class Sex : uint8_t { Female, Male };
enum class AgeGroup : uint8_t { Young, Adult, Mature, Elder, Count };
enum class Persona : uint8_t { Heroic, Villainous, Cute, Sexy, Stern, Gentle, Rogue, Noble, Count };
// Optional body presets for stylised extremes. Natural = sampled population.
enum class Archetype : uint8_t { Natural, Bombshell, Dorito, Brute, Amazon, Slender, Chibi, Monster, Count };

enum class HairStyle : uint8_t {
    Bald, Buzz, ShortCrop, SidePart, SlickedBack, Messy, Undercut, Pixie,
    Bob, Shoulder, LongStraight, LongWavy, Ponytail, HighPonytail, Braid, Bun, HalfUp, ManBun, Count
};
enum class Fringe : uint8_t { None, Curtain, SideSwept, Straight, Count };
enum class FacialHair : uint8_t { None, Mustache, Goatee, ShortBeard, FullBeard, Count };
enum class EarStyle : uint8_t { Human, Elf, Count };
enum class PupilStyle : uint8_t { Round, Slit, Count };

std::string_view toString(Sex s);
std::string_view toString(AgeGroup a);
std::string_view toString(Persona p);
std::string_view toString(Archetype a);
std::string_view toString(HairStyle h);
std::string_view toString(FacialHair f);
std::optional<Archetype> archetypeFromString(std::string_view s);
std::optional<Persona> personaFromString(std::string_view s);
std::optional<AgeGroup> ageFromString(std::string_view s);

// Muscles and bony landmarks, each about 0..1 (values above 1 for heroic builds).
// Overall volume comes from the widths and radii in BodyShape; these fields shape
// and reveal the anatomical forms, so they map one-to-one onto future sliders.
struct Anatomy {
    float definition = 0.4f;  // how visible forms are (low body fat): 0 soft .. 1 shredded
    // Muscles
    float neck = 0.5f;        // sternocleidomastoid, neck thickness
    float traps = 0.5f;       // trapezius slope and upper-back diamond
    float deltoids = 0.5f;
    float pecs = 0.5f;
    float lats = 0.5f;        // back width (V taper)
    float abs = 0.5f;         // rectus abdominis blocks
    float obliques = 0.5f;    // flanks, inguinal "V"
    float serratus = 0.5f;
    float biceps = 0.5f;
    float triceps = 0.5f;
    float forearms = 0.5f;
    float glutes = 0.5f;
    float quads = 0.5f;
    float adductors = 0.5f;   // inner thigh fullness
    float hamstrings = 0.5f;
    float calves = 0.5f;
    // Bones
    float clavicles = 0.5f;
    float ribcage = 0.5f;     // costal arch, rib flare
    float scapulae = 0.5f;
    float pelvis = 0.5f;      // iliac crest / hip bones
    float knees = 0.5f;
    float elbows = 0.5f;
    float wrists = 0.5f;      // and ankles
};

struct BodyShape {
    float height = 1.68f;      // metres, crown to floor
    float headsTall = 6.9f;    // stylised-realistic 6.6-7.4, chibi 3-4
    float legRatio = 0.50f;    // crotch height / height
    float neckLength = 0.32f;  // head units
    float neckThick = 0.14f;   // radius, head units
    // Half widths in head units.
    float shoulderWidth = 0.82f;
    float chestWidth = 0.62f;
    float waistWidth = 0.55f;
    float hipWidth = 0.78f;
    float chestDepth = 0.38f;
    // Soft tissue / mass, 0..1 (values > 1 allowed for stylised presets).
    float bust = 0.5f;
    float butt = 0.5f;
    float belly = 0.0f;
    float muscle = 0.25f;
    float sag = 0.0f;          // age: bust/glute drop, softer contours
    float hunch = 0.0f;        // age: rounded upper back, head forward
    // Limb radii in head units.
    float thigh = 0.32f;
    float calf = 0.19f;
    float arm = 0.13f;
    float armLength = 1.0f;
    float handSize = 1.0f;
    float footSize = 1.0f;
    float headWidth = 1.0f;
    float posture = 0.5f;      // spine S-curve
    // Anatomy extensions (fantasy).
    int armPairs = 1;
    bool tail = false;
    float tailLength = 1.0f;
    int horns = 0;
    float hornSize = 1.0f;
    EarStyle ears = EarStyle::Human;
    Anatomy anat;
};

struct FaceStyle {
    // Eyes
    int eyeShape = 0;          // 0 almond, 1 round, 2 hooded, 3 upturned, 4 downturned
    float eyeSize = 1.0f;
    float eyeSpacing = 1.0f;
    float eyeTilt = 0.0f;      // radians, + = outer corner up
    float restLid = 0.0f;      // resting upper-lid drop (heavy-lidded / sly)
    float lashWeight = 0.5f;   // lash line thickness
    PupilStyle pupil = PupilStyle::Round;
    // Brows
    int browShape = 0;         // 0 straight, 1 soft arch, 2 angled
    float browThick = 0.5f;
    float browTilt = 0.0f;     // resting angle: + inner end up (worried/kind), - inner end down (stern/menacing)
    float browHeight = 0.0f;   // resting offset
    // Mouth & nose
    float mouthWidth = 1.0f;
    float lipFullness = 0.5f;
    float mouthRest = 0.0f;    // resting curve: + slight smile, - slight frown
    float mouthAsym = 0.0f;    // resting smirk
    float noseSize = 1.0f;     // sculpted in the mesh
    float noseBridge = 0.5f;   // 0 soft/button .. 1 strong/aquiline
    // Head structure
    float jaw = 0.0f;          // -1 narrow .. +1 square
    float chin = 0.0f;         // -1 soft .. +1 long/prominent
    float cheek = 0.0f;        // cheekbones
    float browRidge = 0.3f;    // sculpted brow ridge
    float roundness = 0.0f;    // fuller cheeks (youth / softness)
    // Marks
    float ageLines = 0.0f;     // 0..1 crow's feet, under-eye, nasolabial folds
    bool freckles = false;
    bool scar = false;
    bool fang = false;
    bool blush = false;
};

struct Palette {
    Vec3 skin{1.0f, 0.86f, 0.77f};
    Vec3 hair{0.25f, 0.2f, 0.18f};
    Vec3 hairTip{0.25f, 0.2f, 0.18f};
    Vec3 brow{0.2f, 0.15f, 0.12f};
    Vec3 iris{0.35f, 0.25f, 0.15f};
    Vec3 lips{0.75f, 0.45f, 0.42f};
    Vec3 underwear{0.85f, 0.82f, 0.76f};
    Vec3 horn{0.9f, 0.85f, 0.75f};
};

struct Genome {
    uint64_t seed = 0;
    Archetype archetype = Archetype::Natural;
    Sex sex = Sex::Female;
    float femininity = 1.0f;   // continuous dimorphism used by face/body details
    float age = 28.0f;
    AgeGroup ageGroup = AgeGroup::Adult;
    Persona persona = Persona::Heroic;
    BodyShape body;
    FaceStyle face;
    HairStyle hair = HairStyle::Shoulder;
    Fringe fringe = Fringe::SideSwept;
    FacialHair facialHair = FacialHair::None;
    float hairVolume = 1.0f;
    float hairLength = 1.0f;
    float hairWave = 0.0f;     // 0 straight .. 1 wavy
    float partSide = 1.0f;     // +1 / -1
    bool underwear = true;     // base mesh modesty layer (Skyrim-like)
    Palette palette;
};

struct GenomeOptions {
    std::optional<Archetype> archetype;  // Natural when empty
    std::optional<Sex> sex;
    std::optional<AgeGroup> age;
    std::optional<Persona> persona;
    std::optional<HairStyle> hair;
    float exaggeration = 0.0f;           // pushes proportions towards stylised extremes
};

Genome randomGenome(uint64_t seed, const GenomeOptions& opt = {});

}  // namespace cg
