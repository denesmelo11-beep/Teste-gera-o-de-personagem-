#include "chargen/genome.hpp"

#include <array>
#include <vector>
#include <iterator>

#include "chargen/rng.hpp"

namespace cg {

namespace {

constexpr std::string_view kArchNames[] = {"natural", "bombshell", "dorito", "brute", "amazon", "slender", "chibi", "monster"};
constexpr std::string_view kPersonaNames[] = {"heroic", "villainous", "cute", "sexy", "stern", "gentle", "rogue", "noble"};
constexpr std::string_view kAgeNames[] = {"young", "adult", "mature", "elder"};
constexpr std::string_view kHairNames[] = {"bald", "buzz", "shortcrop", "sidepart", "slickedback", "messy", "undercut", "pixie", "bob",
                                           "shoulder", "longstraight", "longwavy", "ponytail", "highponytail", "braid", "bun", "halfup", "manbun",
                                           "crewcut", "tonsure", "warriorbraids", "curly", "afro", "mullet", "topknot", "locs", "wolf",
                                           "hime", "crownbraid", "twinbraids", "sidebraid", "lowponytail", "bowlcut", "messybun",
                                           "longcurly", "verylong", "asymbob"};
static_assert(std::size(kHairNames) == size_t(cg::HairStyle::Count));
constexpr std::string_view kBeardNames[] = {"none", "stubble", "mustache", "goatee", "shortbeard", "fullbeard", "longbeard", "braidedbeard", "muttonchops", "tendrils"};
constexpr std::string_view kSpeciesNames[] = {"human", "dwarf", "gnome", "orc", "troll", "undead", "nightelf", "highelf", "draenei", "tauren", "goblin", "worgen", "ogre", "centaur", "skeleton",
                                              "dragonborn", "saurian", "avian", "naga", "tortle", "insectoid", "fishfolk", "demon", "cephalid", "fae",
                                              "crystalborn", "emberkin", "wraith", "cyclops", "talpid", "oculite", "slime", "rimemaw"};
static_assert(std::size(kSpeciesNames) == size_t(cg::Species::Count));
static_assert(std::size(kBeardNames) == size_t(cg::FacialHair::Count));

template <size_t N>
int indexOf(const std::string_view (&names)[N], std::string_view s) {
    for (size_t i = 0; i < N; ++i)
        if (names[i] == s) return int(i);
    return -1;
}

float z(Rng& r) { return clamp(r.gauss(0, 1), -2.5f, 2.5f); }

Vec3 jitter(Rng& r, Vec3 c, float a) {
    return {saturate(c.x + r.range(-a, a)), saturate(c.y + r.range(-a, a)), saturate(c.z + r.range(-a, a))};
}

// Persona-indexed tables (Heroic, Villainous, Cute, Sexy, Stern, Gentle, Rogue, Noble).
constexpr float kBrowTilt[] = {-0.10f, -0.30f, 0.18f, -0.05f, -0.22f, 0.15f, -0.08f, 0.0f};
constexpr float kRestLid[] = {0.02f, 0.18f, 0.0f, 0.14f, 0.10f, 0.03f, 0.08f, 0.06f};
constexpr float kMouthRest[] = {0.0f, 0.0f, 0.004f, 0.002f, -0.005f, 0.006f, 0.0f, -0.002f};
constexpr float kMouthAsym[] = {0.0f, 0.006f, 0.0f, 0.002f, 0.0f, 0.0f, 0.005f, 0.0f};
constexpr std::array<std::array<float, 3>, 8> kBrowShapeW = {{{2, 1, 1.5f},
                                                               {0.5f, 0.5f, 3},
                                                               {1, 3, 0.2f},
                                                               {1, 2.5f, 1.5f},
                                                               {3, 0.3f, 1.5f},
                                                               {1.5f, 2.5f, 0.2f},
                                                               {1.5f, 1, 2},
                                                               {1.5f, 2, 1}}};

Vec3 pickSkin(Rng& r, bool fantasy) {
    // Weathered, sun- and cold-touched tones rather than porcelain.
    static constexpr Vec3 human[] = {{0.90f, 0.77f, 0.69f}, {0.87f, 0.71f, 0.59f}, {0.79f, 0.61f, 0.48f},
                                     {0.67f, 0.49f, 0.36f}, {0.51f, 0.36f, 0.26f}, {0.35f, 0.25f, 0.19f}};
    static constexpr Vec3 fantasySkins[] = {{0.52f, 0.62f, 0.44f}, {0.62f, 0.68f, 0.76f}, {0.66f, 0.30f, 0.27f},
                                            {0.58f, 0.52f, 0.66f}, {0.58f, 0.58f, 0.60f}, {0.36f, 0.33f, 0.42f}};
    if (fantasy) return jitter(r, fantasySkins[r.weighted({2, 1, 1.5f, 0.6f, 1.5f, 1.5f})], 0.03f);
    return jitter(r, human[r.weighted({3, 4, 3, 2.2f, 1.6f, 1.2f})], 0.02f);
}

Vec3 pickHair(Rng& r, Persona p) {
    const bool dark = p == Persona::Villainous || p == Persona::Noble;
    switch (r.weighted({3, 3, 2.5f, 1, 0.7f, 2, 0.6f, dark ? 0.9f : 0.3f, 0.2f, 0.3f})) {
        case 0: return hsv(r.range(0.05f, 0.08f), r.range(0.2f, 0.4f), r.range(0.22f, 0.27f));  // black (lifted so its facets still read)
        case 1: return hsv(r.range(0.06f, 0.08f), r.range(0.45f, 0.6f), r.range(0.26f, 0.34f)); // dark brown
        case 2: return hsv(r.range(0.07f, 0.09f), r.range(0.45f, 0.6f), r.range(0.35f, 0.50f)); // brown
        case 3: return hsv(r.range(0.03f, 0.05f), r.range(0.6f, 0.75f), r.range(0.38f, 0.52f)); // auburn
        case 4: return hsv(r.range(0.02f, 0.05f), r.range(0.65f, 0.8f), r.range(0.55f, 0.72f)); // red
        case 5: return hsv(r.range(0.10f, 0.12f), r.range(0.40f, 0.55f), r.range(0.72f, 0.88f)); // blonde
        case 6: return hsv(r.range(0.11f, 0.13f), r.range(0.15f, 0.25f), r.range(0.86f, 0.95f)); // platinum
        case 7: return hsv(r.range(0.55f, 0.65f), r.range(0.02f, 0.06f), r.range(0.84f, 0.94f)); // white / silver
        case 8: return hsv(r.range(0.97f, 1.0f), r.range(0.6f, 0.75f), r.range(0.30f, 0.42f));  // deep crimson
        default: return hsv(r.range(0.62f, 0.68f), r.range(0.25f, 0.4f), r.range(0.12f, 0.2f)); // blue-black
    }
}

Vec3 pickIris(Rng& r, bool fantasy) {
    if (fantasy && r.chance(0.6f)) {
        switch (r.rangeInt(0, 2)) {
            case 0: return hsv(r.range(0.98f, 1.02f), 0.75f, 0.75f);  // red
            case 1: return hsv(r.range(0.74f, 0.80f), 0.55f, 0.70f);  // violet
            default: return hsv(r.range(0.11f, 0.13f), 0.80f, 0.85f); // gold
        }
    }
    switch (r.weighted({4, 1.5f, 0.6f, 1, 2, 1})) {
        case 0: return hsv(r.range(0.06f, 0.08f), r.range(0.6f, 0.75f), r.range(0.25f, 0.40f));  // brown
        case 1: return hsv(r.range(0.10f, 0.14f), r.range(0.5f, 0.65f), r.range(0.40f, 0.55f));  // hazel
        case 2: return hsv(r.range(0.09f, 0.11f), r.range(0.75f, 0.9f), r.range(0.55f, 0.70f));  // amber
        case 3: return hsv(r.range(0.28f, 0.38f), r.range(0.40f, 0.6f), r.range(0.40f, 0.55f));  // green
        case 4: return hsv(r.range(0.55f, 0.60f), r.range(0.45f, 0.65f), r.range(0.55f, 0.75f)); // blue
        default: return hsv(r.range(0.55f, 0.60f), r.range(0.08f, 0.18f), r.range(0.50f, 0.65f)); // grey
    }
}

AgeGroup groupOf(float age) {
    return age < 23 ? AgeGroup::Young : age < 40 ? AgeGroup::Adult : age < 60 ? AgeGroup::Mature : AgeGroup::Elder;
}

}  // namespace

bool isShortHair(HairStyle h) {
    using H = HairStyle;
    switch (h) {
        case H::Bald: case H::Buzz: case H::ShortCrop: case H::SidePart: case H::SlickedBack: case H::Messy: case H::Undercut:
        case H::Pixie: case H::CrewCut: case H::Tonsure: case H::Curly: case H::Afro: case H::BowlCut: return true;
        default: return false;
    }
}
bool isTiedHair(HairStyle h) {
    using H = HairStyle;
    switch (h) {
        case H::Ponytail: case H::HighPonytail: case H::Braid: case H::Bun: case H::ManBun: case H::Topknot: case H::CrownBraid:
        case H::TwinBraids: case H::SideBraid: case H::LowPonytail: case H::MessyBun: return true;
        default: return false;
    }
}

std::string_view toString(Sex s) { return s == Sex::Female ? "female" : "male"; }
std::string_view toString(AgeGroup a) { return a < AgeGroup::Count ? kAgeNames[size_t(a)] : "?"; }
std::string_view toString(Persona p) { return p < Persona::Count ? kPersonaNames[size_t(p)] : "?"; }
std::string_view toString(Archetype a) { return a < Archetype::Count ? kArchNames[size_t(a)] : "?"; }
std::string_view toString(HairStyle h) { return h < HairStyle::Count ? kHairNames[size_t(h)] : "?"; }
std::string_view toString(FacialHair f) { return f < FacialHair::Count ? kBeardNames[size_t(f)] : "?"; }
std::string_view toString(Species s) { return s < Species::Count ? kSpeciesNames[size_t(s)] : "?"; }
std::optional<Species> speciesFromString(std::string_view s) {
    int i = indexOf(kSpeciesNames, s);
    return i < 0 ? std::nullopt : std::optional<Species>(Species(i));
}
std::optional<Archetype> archetypeFromString(std::string_view s) {
    int i = indexOf(kArchNames, s);
    return i < 0 ? std::nullopt : std::optional<Archetype>(Archetype(i));
}
std::optional<Persona> personaFromString(std::string_view s) {
    int i = indexOf(kPersonaNames, s);
    return i < 0 ? std::nullopt : std::optional<Persona>(Persona(i));
}
std::optional<AgeGroup> ageFromString(std::string_view s) {
    int i = indexOf(kAgeNames, s);
    return i < 0 ? std::nullopt : std::optional<AgeGroup>(AgeGroup(i));
}

namespace {
// Each people keeps the individual's sampled variation (height, build, face) and
// rescales it around its own canon. Widths are in head units, so a short canon
// (more heads per height) automatically reads as stocky.
void applySpecies(Genome& g, Species sp, Rng r, const GenomeOptions& opt) {
    g.species = sp;
    const bool fem = g.sex == Sex::Female;
    BodyShape& b = g.body;
    FaceStyle& f = g.face;
    Palette& pal = g.palette;
    const float hRel = b.height / (fem ? 1.65f : 1.78f);  // keeps the individual's relative height
    auto height = [&](float lo, float hi) { b.height = lerp(lo, hi, r.uniform()) * clamp(hRel, 0.94f, 1.06f); };
    // Species factors were authored against a slighter human canon: rebase so peoples
    // with their own head count keep their intended absolute bulk.
    auto widths = [&](float sh, float ch, float wa, float hp, float limbs) {
        const float rb2 = 0.88f;
        b.shoulderWidth *= sh * rb2; b.chestWidth *= ch * rb2; b.waistWidth *= wa * rb2; b.hipWidth *= hp * rb2;
        b.thigh *= limbs * rb2; b.calf *= limbs * rb2; b.arm *= limbs * rb2; b.neckThick *= 0.9f;
    };
    auto pickC = [&](std::initializer_list<Vec3> cs) {
        std::vector<Vec3> v(cs);
        return jitter(r, v[size_t(r.rangeInt(0, int(v.size()) - 1))], 0.03f);
    };
    auto hair = [&](std::initializer_list<std::pair<HairStyle, float>> ws) {
        if (opt.hair) return;
        std::vector<float> w;
        std::vector<HairStyle> hs;
        for (auto [h2, wt] : ws) hs.push_back(h2), w.push_back(wt);
        g.hair = hs[size_t(r.weighted(std::span<const float>(w)))];
        if (isShortHair(g.hair) || isTiedHair(g.hair)) g.fringe = r.chance(0.5f) ? Fringe::None : g.fringe;
    };
    auto beard = [&](std::initializer_list<std::pair<FacialHair, float>> ws) {
        if (fem || g.age < 18) return;
        std::vector<float> w;
        std::vector<FacialHair> fh;
        for (auto [b2, wt] : ws) fh.push_back(b2), w.push_back(wt);
        g.facialHair = fh[size_t(r.weighted(std::span<const float>(w)))];
        f.stubble = g.facialHair == FacialHair::None ? f.stubble : std::max(f.stubble, 0.4f);
    };
    auto hairColor = [&](Vec3 c) {
        const float gray = std::pow(saturate((g.age - 42.0f) / 30.0f), 1.2f) * 0.8f;
        pal.hair = lerp(c, Vec3{1, 1, 1} * 0.8f, gray);
        pal.hairTip = pal.hair * 0.88f;
        pal.brow = luminance(pal.hair) > 0.55f ? lerp(pal.hair * 0.6f, Vec3{0.30f, 0.24f, 0.20f}, 0.5f) : pal.hair * 0.8f;
    };
    auto skinTo = [&](Vec3 c) {
        pal.skin = c;
        pal.lips = lerp(pal.skin * Vec3{0.90f, 0.62f, 0.60f}, Vec3{0.45f, 0.20f, 0.25f}, fem ? 0.3f : 0.1f);
    };
    auto glow = [&](Vec3 c) { f.glowEyes = true; f.glow = c; f.pupil = PupilStyle::Round; };
    // Peoples define their own fantasy traits: drop the random ones of the base draw.
    b.ears = EarStyle::Human;
    b.ear = EarShape{};
    b.horns = 0;
    b.tail = false;
    b.tailType = 0;
    b.spines = 0;
    b.armPairs = 1;
    f.pupil = PupilStyle::Round;
    f.fang = false;
    if (!(pal.skin.x >= pal.skin.y && pal.skin.y >= pal.skin.z)) skinTo(pickSkin(r, false));  // a fantasy hue from the base draw
    switch (sp) {
        case Species::Dwarf:
            b.boneMass *= 1.3f;
            height(fem ? 1.22f : 1.30f, fem ? 1.36f : 1.45f);
            b.headsTall = r.range(4.7f, 5.0f);
            b.legRatio = 0.40f;
            widths(1.30f, 1.35f, fem ? 1.30f : 1.45f, 1.15f, 1.28f);
            b.chestDepth *= 1.3f; b.handSize *= 1.25f; b.footSize *= 1.25f;
            b.neckLength *= 0.45f; b.neckThick *= 1.35f; b.muscle = saturate(b.muscle + 0.2f);
            if (!fem) b.belly += 0.12f;
            // A strong, straight or hawkish nose: long and prominent, never a round bulb.
            f.noseLength = r.range(1.05f, 1.25f); f.noseProj = r.range(1.05f, 1.30f) * (fem ? 0.8f : 1.0f);
            f.noseWidth = r.range(1.00f, 1.20f) * (fem ? 0.9f : 1.0f); f.noseTip = r.range(-0.3f, 0.1f); f.noseHump = r.range(0.0f, 0.6f);
            f.browRidge = saturate(f.browRidge + 0.3f); f.eyeDepth = saturate(f.eyeDepth + 0.3f);
            f.ruddy = saturate(f.ruddy + 0.4f); f.jaw = std::max(f.jaw, fem ? 0.2f : 0.8f);
            f.wrinkles = saturate((g.age - 25.0f) / 60.0f + 0.25f); f.eyeSize *= 0.9f; f.headTop = 0.2f;
            b.ear.length = 0.13f;
            hairColor(pickC({{0.55f, 0.20f, 0.10f}, {0.30f, 0.20f, 0.14f}, {0.16f, 0.13f, 0.12f}, {0.75f, 0.60f, 0.35f}, {0.45f, 0.30f, 0.18f}}));
            hair({{HairStyle::Braid, 2}, {HairStyle::TwinBraids, fem ? 3.0f : 0.8f}, {HairStyle::Bald, fem ? 0.0f : 2.0f},
                  {HairStyle::WarriorBraids, 2}, {HairStyle::LongStraight, 1}, {HairStyle::CrownBraid, fem ? 2.0f : 0.0f},
                  {HairStyle::Messy, 1}, {HairStyle::Topknot, fem ? 0.0f : 1.0f}});
            beard({{FacialHair::LongBeard, 3}, {FacialHair::BraidedBeard, 3}, {FacialHair::FullBeard, 2}, {FacialHair::MuttonChops, 0.6f}});
            break;
        case Species::Gnome:
            b.boneMass *= 0.85f;
            height(fem ? 0.92f : 0.96f, fem ? 1.05f : 1.12f);
            b.headsTall = r.range(3.2f, 3.45f);
            b.legRatio = 0.41f;
            widths(0.88f, 0.88f, 0.92f, 0.92f, 0.80f);
            b.neckLength *= 0.6f; b.handSize *= 1.1f; b.bust *= 0.6f; b.butt *= 0.8f;
            // Gnomes: wide-set, sharp, clever eyes (not doll eyes), a long pointed nose, crow's feet.
            f.eyeSize = 1.0f; f.eyeShape = r.chance(0.5f) ? 0 : 3; f.irisSize = 0.92f; f.eyeSpacing = 1.08f; f.eyeTilt = 0.06f;
            f.noseLength = 1.15f; f.noseProj = r.range(1.05f, 1.3f); f.noseWidth = 0.95f; f.noseTip = r.range(-0.1f, 0.3f);
            f.chinPoint = 0.3f; f.browRidge = 0.5f; f.mouthWidth = 1.2f; f.lipFullness = 0.2f;
            f.roundness = saturate(f.roundness + 0.15f); f.chin = 0.0f; f.jaw = fem ? -0.3f : 0.0f; f.cheekHollow = 0.2f; f.cheek = 0.5f;
            f.headTop = 0.6f; f.wrinkles = g.age > 40 ? 0.55f : 0.25f;
            b.ear = {0.30f, 0.11f, 0.25f, 0.35f, 0.0f};
            hairColor(pickC({{0.62f, 0.36f, 0.44f}, {0.30f, 0.50f, 0.50f}, {0.42f, 0.32f, 0.52f}, {0.42f, 0.50f, 0.30f}, {0.80f, 0.80f, 0.80f},
                             {0.58f, 0.30f, 0.18f}, {0.70f, 0.62f, 0.40f}}));  // dyed, but faded
            hair({{HairStyle::TwinBraids, fem ? 2.0f : 0.0f}, {HairStyle::HighPonytail, fem ? 2.0f : 0.3f}, {HairStyle::Messy, 2},
                  {HairStyle::Bun, fem ? 1.0f : 0.0f}, {HairStyle::BowlCut, 1}, {HairStyle::Curly, 1}, {HairStyle::Topknot, 1}});
            beard({{FacialHair::None, 1}, {FacialHair::Mustache, 2}, {FacialHair::Goatee, 1}, {FacialHair::FullBeard, 1.5f},
                   {FacialHair::MuttonChops, 1}});
            break;
        case Species::Orc:
            b.boneMass *= 1.25f;
            height(fem ? 1.75f : 1.88f, fem ? 1.88f : 2.05f);
            b.headsTall = r.range(6.4f, 6.7f);
            widths(fem ? 1.12f : 1.48f, fem ? 1.10f : 1.40f, fem ? 1.10f : 1.30f, 1.05f, fem ? 1.12f : 1.38f);
            b.handSize *= 1.25f; b.neckLength *= 0.55f; b.neckThick *= 1.5f; b.muscle = saturate(b.muscle + 0.35f);
            b.hunch = std::max(b.hunch, fem ? 0.3f : 0.9f);
            f.browRidge = 1.1f; f.eyeDepth = 0.9f; f.eyeShape = r.chance(0.5f) ? 2 : 3; f.eyeSize = 0.88f;
            f.noseLength = 0.85f; f.noseProj = 0.75f; f.noseWidth = 1.6f; f.noseTip = 0.35f; f.noseHump = 0.0f;
            f.jaw = fem ? 0.5f : 1.0f; f.chinWidth = fem ? 0.4f : 1.0f; f.cheek = 0.3f; f.mouthWidth = 1.25f; f.lipFullness = 0.5f;
            f.underbite = fem ? 0.55f : 0.95f; f.mouthStyle = 2; f.headTop = -0.3f; f.wrinkles = fem ? 0.1f : 0.35f;
            f.sclera = Vec3{0.88f, 0.80f, 0.62f}; f.browTilt = -0.3f; f.restLid = 0.2f; f.irisSize = 0.85f; f.eyeShape = 6;
            f.browThick = 1.0f; f.lashWeight = 0.15f;
            b.tusks = 1; b.tuskSize = fem ? 0.55f : 1.0f; b.tuskCurve = r.range(-0.3f, 1.0f);
            if (!fem && r.chance(0.15f)) { b.spines = 6; b.spineKind = 2; b.spineSize = 0.7f; }  // a ridge of bony knobs
            b.ear = {0.20f, 0.07f, 0.35f, 0.40f, 0.0f};
            skinTo(fem ? pickC({{0.48f, 0.60f, 0.32f}, {0.40f, 0.52f, 0.28f}, {0.55f, 0.60f, 0.34f}})
                       : pickC({{0.45f, 0.58f, 0.30f}, {0.38f, 0.48f, 0.26f}, {0.52f, 0.52f, 0.30f}, {0.40f, 0.36f, 0.26f}, {0.30f, 0.42f, 0.24f}}));
            pal.iris = pickC({{0.75f, 0.15f, 0.10f}, {0.85f, 0.55f, 0.10f}, {0.35f, 0.22f, 0.12f}});
            hairColor(pickC({{0.12f, 0.11f, 0.11f}, {0.22f, 0.15f, 0.11f}, {0.30f, 0.20f, 0.12f}}));
            hair({{HairStyle::Topknot, 2}, {HairStyle::Ponytail, 2}, {HairStyle::Bald, fem ? 0.2f : 2.0f}, {HairStyle::Braid, 1.5f},
                  {HairStyle::WarriorBraids, 2}, {HairStyle::LowPonytail, 1}, {HairStyle::LongStraight, 1}, {HairStyle::Undercut, 1}});
            beard({{FacialHair::None, 2}, {FacialHair::Goatee, 2}, {FacialHair::BraidedBeard, 2}, {FacialHair::ShortBeard, 1}});
            break;
        case Species::Troll:
            b.boneMass *= 0.95f;
            height(fem ? 1.95f : 2.05f, fem ? 2.10f : 2.22f);
            b.headsTall = r.range(7.0f, 7.3f);
            b.legRatio = 0.52f;
            widths(1.05f, 1.0f, 0.88f, 0.95f, 0.88f);
            b.armLength *= 1.15f; b.handSize *= 1.35f; b.footSize *= 1.25f; b.hunch = std::max(b.hunch, fem ? 0.9f : 1.6f);
            f.noseLength = 1.5f; f.noseProj = r.range(1.5f, 1.8f); f.noseWidth = 1.2f; f.noseTip = -0.2f;
            f.jaw = -0.2f; f.chin = 0.4f; f.eyeShape = 3; f.eyeDepth = 0.6f; f.browRidge = 0.8f; f.mouthWidth = 1.4f;
            f.underbite = 0.6f; f.mouthStyle = 2; f.chinPoint = 0.35f; f.sclera = Vec3{0.90f, 0.84f, 0.55f}; f.faceWidth = -0.4f;
            f.eyeTilt = 0.12f; f.cheekHollow = 0.6f; f.wrinkles = 0.25f;
            b.tusks = 2; b.tuskSize = fem ? 0.75f : 1.3f; b.tuskCurve = r.range(-0.2f, 1.0f);
            if (r.chance(0.25f)) { b.spines = r.rangeInt(5, 8); b.spineKind = 0; b.spineSize = r.range(0.6f, 0.9f); }
            b.digitigrade = false;
            b.ear = {0.55f, 0.09f, 0.05f, 0.30f, 0.15f};
            skinTo(pickC({{0.35f, 0.55f, 0.70f}, {0.30f, 0.60f, 0.58f}, {0.42f, 0.62f, 0.45f}, {0.55f, 0.45f, 0.70f}}));
            pal.iris = pickC({{0.95f, 0.75f, 0.15f}, {0.85f, 0.20f, 0.12f}});
            hairColor(pickC({{0.85f, 0.25f, 0.15f}, {0.25f, 0.70f, 0.65f}, {0.92f, 0.92f, 0.95f}, {0.55f, 0.30f, 0.75f}, {0.40f, 0.75f, 0.35f},
                             {0.95f, 0.55f, 0.15f}}));
            hair({{HairStyle::Topknot, 3}, {HairStyle::Locs, 2}, {HairStyle::HighPonytail, 1.5f}, {HairStyle::Messy, 1},
                  {HairStyle::Undercut, 1}, {HairStyle::Ponytail, 1}});
            beard({{FacialHair::None, 3}, {FacialHair::Goatee, 1}});
            break;
        case Species::Undead:
            b.boneMass *= 0.95f;
            widths(0.95f, 0.88f, 0.78f, 0.90f, 0.72f);
            b.muscle *= 0.5f; b.belly = 0.0f; b.bust *= 0.6f; b.butt *= 0.6f; b.hunch = std::max(b.hunch, 0.6f);
            b.anat.definition = 0.9f; b.anat.ribcage = 1.0f; b.anat.clavicles = 1.0f; b.anat.scapulae = 1.0f; b.anat.pelvis = 1.0f;
            b.anat.knees = 1.0f; b.anat.elbows = 1.0f;
            // Exposed bones: ribs through the flank, a bared forearm or shin, the jaw.
            // Their flesh is torn open on the real skeleton (see Builder: holes over the bones).
            b.bones = 0;
            b.boneSide = r.chance(0.5f) ? 1 : -1;
            f.cheekHollow = 1.0f; f.eyeDepth = 1.0f; f.eyeBags = 0.7f; f.jowl = 0.0f; f.roundness = 0.0f; f.ruddy = 0.0f;
            f.lipFullness = 0.1f; f.rot = r.range(0.4f, 0.9f);
            glow({1.0f, 0.85f, 0.35f});
            skinTo(pickC({{0.55f, 0.62f, 0.52f}, {0.58f, 0.55f, 0.66f}, {0.62f, 0.66f, 0.72f}, {0.60f, 0.58f, 0.55f}}));
            pal.lips = pal.skin * 0.55f;
            hairColor(pickC({{0.14f, 0.13f, 0.14f}, {0.55f, 0.55f, 0.55f}, {0.85f, 0.85f, 0.82f}, {0.35f, 0.28f, 0.24f}}));
            hair({{HairStyle::LongStraight, 2}, {HairStyle::Messy, 2}, {HairStyle::Bald, 1.5f}, {HairStyle::Ponytail, 1},
                  {HairStyle::Shoulder, 1.5f}, {HairStyle::Tonsure, 0.5f}});
            beard({{FacialHair::None, 3}, {FacialHair::Stubble, 1}, {FacialHair::Goatee, 0.5f}});
            break;
        case Species::NightElf:
            b.boneMass *= 0.95f;
            height(fem ? 1.90f : 2.05f, fem ? 2.02f : 2.20f);
            b.headsTall = r.range(7.1f, 7.4f);
            b.legRatio = 0.52f;
            widths(fem ? 1.0f : 1.10f, fem ? 1.0f : 1.08f, 0.95f, 1.0f, 1.0f);
            b.muscle = saturate(b.muscle + 0.15f);
            f.eyeShape = 3; f.cheek = std::max(f.cheek, 0.4f); f.noseLength = 1.1f; f.chinPoint = 0.25f; f.eyeTilt = 0.12f;
            f.browRidge = saturate(f.browRidge + 0.3f);
            glow({0.82f, 0.90f, 1.0f});
            b.ear = {0.80f, 0.085f, 0.55f, 0.50f, 0.0f};
            b.ears = EarStyle::Elf;
            skinTo(pickC({{0.62f, 0.52f, 0.78f}, {0.72f, 0.62f, 0.85f}, {0.48f, 0.52f, 0.80f}, {0.80f, 0.72f, 0.88f}, {0.42f, 0.34f, 0.58f}}));
            hairColor(pickC({{0.30f, 0.70f, 0.65f}, {0.30f, 0.45f, 0.85f}, {0.90f, 0.92f, 0.96f}, {0.55f, 0.35f, 0.75f}, {0.40f, 0.70f, 0.40f},
                             {0.18f, 0.20f, 0.45f}}));
            if (fem && r.chance(0.65f)) { f.tattoo = 1 + r.rangeInt(0, 2); f.tattooColor = pickC({{0.25f, 0.15f, 0.40f}, {0.10f, 0.35f, 0.35f}, {0.20f, 0.35f, 0.15f}}); }
            hair({{HairStyle::LongStraight, 2}, {HairStyle::Ponytail, 2}, {HairStyle::HighPonytail, 1.5f}, {HairStyle::Braid, 1.5f},
                  {HairStyle::HalfUp, 1}, {HairStyle::VeryLong, 1}, {HairStyle::WarriorBraids, 1}, {HairStyle::SidePart, fem ? 0.0f : 1.0f}});
            beard({{FacialHair::None, 3}, {FacialHair::LongBeard, 1.2f}, {FacialHair::FullBeard, 0.8f}, {FacialHair::Goatee, 0.5f}});
            break;
        case Species::HighElf:
            b.boneMass *= 0.85f;
            height(fem ? 1.72f : 1.82f, fem ? 1.82f : 1.92f);
            b.headsTall = r.range(7.0f, 7.2f);
            b.legRatio = 0.52f;
            widths(0.95f, 0.95f, 0.92f, 0.98f, 0.92f);
            b.muscle = saturate(b.muscle - 0.1f);
            f.cheek = std::max(f.cheek, 0.5f); f.jaw = fem ? -0.3f : 0.2f; f.chin = 0.2f; f.noseProj = fem ? 0.6f : 0.9f;
            f.chinPoint = 0.35f; f.eyeTilt = 0.10f;
            f.noseWidth = 0.85f; f.eyeShape = 3; f.cheekHollow = 0.2f; f.jowl = 0.0f;
            glow(r.chance(0.7f) ? Vec3{0.55f, 1.0f, 0.40f} : Vec3{0.55f, 0.80f, 1.0f});
            b.ear = {0.62f, 0.075f, 0.45f, 0.45f, 0.0f};
            b.ears = EarStyle::Elf;
            skinTo(pickC({{0.98f, 0.88f, 0.80f}, {0.96f, 0.84f, 0.74f}, {0.90f, 0.76f, 0.64f}}));
            hairColor(pickC({{0.92f, 0.82f, 0.50f}, {0.95f, 0.93f, 0.88f}, {0.70f, 0.22f, 0.12f}, {0.14f, 0.12f, 0.12f}, {0.50f, 0.25f, 0.14f}}));
            hair({{HairStyle::LongStraight, 3}, {HairStyle::Ponytail, 2}, {HairStyle::HalfUp, 2}, {HairStyle::SidePart, fem ? 0.3f : 2.0f},
                  {HairStyle::SlickedBack, fem ? 0.0f : 1.5f}, {HairStyle::VeryLong, fem ? 1.5f : 0.3f}, {HairStyle::Bun, fem ? 1.0f : 0.0f}});
            beard({{FacialHair::None, 6}, {FacialHair::Goatee, 0.5f}});
            break;
        case Species::Draenei:
            b.boneMass *= 1.1f;
            height(fem ? 2.00f : 2.15f, fem ? 2.12f : 2.30f);
            b.headsTall = r.range(7.2f, 7.4f);
            b.legRatio = 0.52f;
            if (fem) widths(1.0f, 1.0f, 0.98f, 1.06f, 1.0f);
            else widths(1.30f, 1.30f, 1.15f, 1.05f, 1.28f), b.muscle = saturate(b.muscle + 0.3f);
            b.horns = 2; b.hornKind = 0; b.hornSize = fem ? 0.8f : 1.1f;
            b.tail = true; b.tailLength = 0.8f; b.hooves = true; b.digitigrade = true; b.tailType = 0;
            glow({0.70f, 0.85f, 1.0f});
            b.ear = {0.30f, 0.085f, 0.30f, 0.45f, 0.0f};
            skinTo(pickC({{0.45f, 0.55f, 0.85f}, {0.68f, 0.60f, 0.88f}, {0.52f, 0.42f, 0.72f}, {0.70f, 0.78f, 0.95f}}));
            pal.horn = Vec3{0.22f, 0.20f, 0.24f};
            hairColor(pickC({{0.14f, 0.13f, 0.16f}, {0.90f, 0.92f, 0.96f}, {0.25f, 0.30f, 0.60f}, {0.45f, 0.30f, 0.60f}}));
            hair({{HairStyle::LongStraight, 2}, {HairStyle::Ponytail, 2}, {HairStyle::Braid, 1.5f}, {HairStyle::HighPonytail, 1},
                  {HairStyle::Bald, fem ? 0.0f : 2.0f}, {HairStyle::Topknot, fem ? 0.0f : 1.0f}});
            beard({{FacialHair::Tendrils, 6}, {FacialHair::None, 1}});
            break;
        case Species::Tauren:
            b.boneMass *= 1.45f;
            height(fem ? 2.05f : 2.25f, fem ? 2.20f : 2.50f);
            b.headsTall = r.range(6.0f, 6.3f);
            b.legRatio = 0.46f;
            if (fem) widths(1.38f, 1.36f, 1.32f, 1.22f, 1.40f);
            else widths(1.55f, 1.50f, 1.45f, 1.20f, 1.50f);
            b.chestDepth *= 1.4f; b.handSize *= 1.4f; b.neckLength *= 0.3f; b.neckThick *= 1.7f;
            b.muscle = saturate(b.muscle + 0.4f); b.hunch = std::max(b.hunch, fem ? 0.6f : 1.3f); b.belly += fem ? 0.0f : 0.1f;
            b.bust *= 0.8f;
            b.muzzle = fem ? 0.85f : 1.0f;
            b.horns = 2; b.hornKind = 3; b.hornSize = fem ? 0.8f : 1.2f;
            b.hooves = true; b.tail = true; b.tailLength = 0.75f; b.furPattern = 1;
            b.ear = {0.36f, 0.13f, -0.25f, 0.10f, 0.25f};
            f.eyeShape = 1; f.eyeSize = 0.95f; f.noseWidth = 2.4f; f.noseProj = 0.45f; f.noseLength = 0.7f; f.noseTip = 0.3f;
            f.mouthWidth = 1.4f; f.stubble = 0.0f; f.lipFullness = 0.3f;
            skinTo(pickC({{0.48f, 0.34f, 0.24f}, {0.32f, 0.23f, 0.17f}, {0.20f, 0.18f, 0.17f}, {0.70f, 0.55f, 0.38f}, {0.85f, 0.82f, 0.76f},
                          {0.55f, 0.53f, 0.50f}}));
            pal.horn = Vec3{0.88f, 0.84f, 0.72f};
            pal.iris = pickC({{0.30f, 0.20f, 0.12f}, {0.15f, 0.12f, 0.10f}});
            hairColor(pickC({{0.12f, 0.11f, 0.10f}, {0.30f, 0.20f, 0.14f}, {0.88f, 0.86f, 0.80f}, {0.45f, 0.32f, 0.20f}}));
            hair({{HairStyle::Braid, 2}, {HairStyle::TwinBraids, 2}, {HairStyle::LongStraight, 1.5f}, {HairStyle::WarriorBraids, 2},
                  {HairStyle::Shoulder, 1}});
            beard({{FacialHair::BraidedBeard, 3}, {FacialHair::None, 2}, {FacialHair::Goatee, 1}});
            if (g.facialHair == FacialHair::Goatee || g.facialHair == FacialHair::None) f.stubble = 0.0f;
            break;
        case Species::Goblin:
            b.boneMass *= 0.85f;
            height(fem ? 0.95f : 0.98f, fem ? 1.06f : 1.12f);
            b.headsTall = r.range(3.6f, 3.9f);
            b.legRatio = 0.42f;
            widths(0.90f, 0.92f, 0.95f, 0.92f, 0.80f);
            b.handSize *= 1.2f; b.footSize *= 1.0f; b.neckLength *= 0.7f; b.bust *= 0.7f;
            f.noseLength = 1.7f; f.noseProj = r.range(1.9f, 2.3f); f.noseWidth = 0.9f; f.noseTip = -0.5f; f.noseHump = 0.5f;
            f.eyeShape = 3; f.eyeSize = 0.84f; f.irisSize = 0.72f; f.eyeTilt = 0.16f; f.restLid = 0.16f; f.mouthWidth = 1.75f;
            f.chin = 0.5f; f.cheek = 0.7f; f.jaw = -0.3f; f.browRidge = 0.9f; f.browTilt = -0.35f; f.lipFullness = 0.05f;
            f.headTop = 0.85f; f.chinPoint = 0.9f; f.wrinkles = r.range(0.45f, 0.8f); f.mouthStyle = 1; f.mouthRest = 0.006f;
            f.sclera = Vec3{0.92f, 0.86f, 0.52f}; f.cheekHollow = 0.4f;
            b.ear = {0.60f, 0.17f, 0.15f, 0.25f, 0.05f};
            skinTo(pickC({{0.50f, 0.66f, 0.32f}, {0.62f, 0.70f, 0.36f}, {0.36f, 0.50f, 0.28f}}));
            pal.iris = pickC({{0.85f, 0.65f, 0.10f}, {0.75f, 0.15f, 0.10f}, {0.30f, 0.55f, 0.20f}});
            hairColor(pickC({{0.12f, 0.11f, 0.11f}, {0.30f, 0.20f, 0.14f}, {0.60f, 0.20f, 0.12f}, {0.55f, 0.55f, 0.55f}}));
            hair({{HairStyle::SlickedBack, 2}, {HairStyle::Topknot, 1.5f}, {HairStyle::Undercut, 1}, {HairStyle::Bald, fem ? 0.0f : 1.5f},
                  {HairStyle::Messy, 1}, {HairStyle::Ponytail, fem ? 2.0f : 0.5f}, {HairStyle::Bun, fem ? 1.5f : 0.0f}});
            beard({{FacialHair::None, 3}, {FacialHair::Mustache, 1.5f}, {FacialHair::Goatee, 1}, {FacialHair::MuttonChops, 1}});
            break;
        case Species::Worgen: {
            b.boneMass *= 1.2f;
            height(fem ? 2.00f : 2.15f, fem ? 2.15f : 2.35f);
            b.headsTall = r.range(6.6f, 6.9f);
            b.legRatio = 0.50f;
            if (fem) widths(1.22f, 1.20f, 1.28f, 0.84f, 1.18f);
            else widths(1.45f, 1.40f, 1.15f, 1.0f, 1.40f);
            b.handSize *= 1.35f; b.footSize *= 1.15f; b.neckThick *= 1.4f; b.neckLength *= 0.6f;
            b.muscle = saturate(b.muscle + 0.35f); b.hunch = std::max(b.hunch, fem ? 0.9f : 1.4f); b.belly = 0.0f;
            b.bust *= 0.55f; b.butt *= 0.7f;
            b.digitigrade = true; b.claws = true; b.tail = true; b.tailFur = true; b.tailType = 3; b.tailLength = 0.65f; b.furPattern = 2;
            b.muzzle = fem ? 0.85f : 1.0f; b.snout = 1;
            b.ear = {0.36f, 0.15f, 1.15f, 0.30f, 0.0f, 0.80f, 0.70f};
            glow({1.0f, 0.78f, 0.25f});
            f.restLid = 0.05f; f.browTilt = -0.25f;
            f.darkNose = true; f.eyeShape = 3; f.eyeSize = 0.92f; f.browRidge = 1.0f; f.eyeDepth = 0.8f; f.noseWidth = 1.7f; f.noseProj = 1.0f;
            f.noseLength = 0.8f; f.noseTip = 0.0f; f.noseHump = 0.0f; f.mouthWidth = 1.5f; f.lipFullness = 0.1f; f.stubble = 0.0f;
            const Vec3 fur = pickC({{0.40f, 0.39f, 0.38f}, {0.28f, 0.23f, 0.19f}, {0.15f, 0.14f, 0.14f}, {0.46f, 0.34f, 0.24f},
                                    {0.56f, 0.55f, 0.53f}, {0.33f, 0.31f, 0.30f}});
            skinTo(fur);
            pal.lips = fur * 0.35f;
            pal.iris = pickC({{0.95f, 0.70f, 0.15f}, {0.85f, 0.45f, 0.10f}, {0.70f, 0.75f, 0.30f}});
            hairColor(fur * 0.78f);
            pal.horn = Vec3{0.20f, 0.18f, 0.17f};  // claws
            if (!opt.hair) g.hair = r.chance(0.5f) ? HairStyle::Wolf : r.chance(0.5f) ? HairStyle::Shoulder : HairStyle::Messy;
            g.fringe = Fringe::None;
            g.facialHair = FacialHair::MuttonChops;  // the cheek ruff
            break;
        }
        case Species::Ogre: {  // colossus: a towering, top-heavy brute with a small head
            b.boneMass *= 1.55f;
            height(fem ? 2.45f : 2.65f, fem ? 2.70f : 3.05f);
            b.headsTall = r.range(7.6f, 8.0f);
            b.legRatio = 0.44f;
            if (fem) widths(1.70f, 1.70f, 1.85f, 1.35f, 1.70f);
            else widths(1.85f, 1.80f, 1.75f, 1.30f, 1.80f);
            b.chestDepth *= 1.45f; b.handSize *= 1.55f; b.footSize *= 1.3f; b.armLength *= 1.10f;
            b.neckLength *= 0.25f; b.neckThick *= 1.9f; b.muscle = saturate(b.muscle + 0.45f);
            b.hunch = std::max(b.hunch, 1.2f); b.belly += fem ? 0.10f : 0.30f; b.bust *= 0.8f;
            b.anat.traps = 1.4f; b.anat.deltoids = 1.3f; b.anat.forearms = 1.4f;
            f.browRidge = 1.2f; f.eyeDepth = 1.0f; f.eyeSize = 0.80f; f.eyeShape = 6; f.jaw = 1.0f; f.chinWidth = 1.0f; f.chin = 0.6f;
            f.noseWidth = 1.7f; f.noseProj = 0.9f; f.noseLength = 0.9f; f.noseTip = 0.3f; f.mouthWidth = 1.3f; f.cheekHollow = 0.0f;
            f.jowl = 0.6f; f.crown = -0.6f; f.headTop = -0.7f; f.underbite = 0.85f; f.mouthStyle = 2; f.wrinkles = 0.5f;
            f.sclera = Vec3{0.86f, 0.80f, 0.66f}; f.irisSize = 0.75f;
            b.tusks = 1; b.tuskSize = fem ? 0.6f : 1.1f; b.tuskCurve = r.range(0.0f, 1.0f);
            if (r.chance(0.3f)) { b.spines = 7; b.spineKind = 1; b.spineSize = 1.1f; }
            b.ear = {0.13f, 0.09f, 0.10f, 0.25f, 0.0f};
            skinTo(pickC({{0.55f, 0.50f, 0.42f}, {0.50f, 0.42f, 0.38f}, {0.58f, 0.36f, 0.30f}, {0.46f, 0.50f, 0.44f}, {0.44f, 0.44f, 0.50f}}));
            pal.iris = pickC({{0.85f, 0.55f, 0.10f}, {0.30f, 0.22f, 0.15f}, {0.75f, 0.20f, 0.10f}});
            hairColor(pickC({{0.12f, 0.11f, 0.11f}, {0.30f, 0.20f, 0.13f}, {0.45f, 0.18f, 0.10f}}));
            hair({{HairStyle::Bald, fem ? 0.3f : 3.0f}, {HairStyle::Topknot, 2}, {HairStyle::Ponytail, 1}, {HairStyle::Messy, 1}, {HairStyle::Buzz, 1}});
            beard({{FacialHair::None, 2}, {FacialHair::Stubble, 2}, {FacialHair::ShortBeard, 1}, {FacialHair::BraidedBeard, 1}});
            break;
        }
        case Species::Centaur: {  // a human torso rising from a horse's chest
            b.boneMass *= 1.15f;
            height(fem ? 2.15f : 2.30f, fem ? 2.30f : 2.50f);
            b.headsTall = r.range(7.8f, 8.1f);
            b.legRatio = 0.56f;  // the human hip line rides at the horse's withers
            widths(fem ? 1.05f : 1.15f, fem ? 1.05f : 1.12f, 1.05f, 1.0f, 1.10f);
            b.centaur = true; b.hooves = true; b.tail = true; b.tailFur = true; b.tailType = 3; b.tailLength = 0.9f; b.belly = 0.0f;
            b.muscle = saturate(b.muscle + 0.2f);
            f.cheek = std::max(f.cheek, 0.3f);
            b.ear = {0.20f, 0.08f, 0.35f, 0.40f, 0.0f};
            hairColor(pickC({{0.16f, 0.13f, 0.11f}, {0.36f, 0.24f, 0.15f}, {0.62f, 0.55f, 0.42f}, {0.75f, 0.74f, 0.70f}, {0.45f, 0.20f, 0.12f}}));
            hair({{HairStyle::LongStraight, 2}, {HairStyle::Braid, 2}, {HairStyle::WarriorBraids, 2}, {HairStyle::Ponytail, 1.5f},
                  {HairStyle::LongWavy, 1}, {HairStyle::Topknot, fem ? 0.0f : 1.0f}});
            beard({{FacialHair::None, 2}, {FacialHair::FullBeard, 2}, {FacialHair::LongBeard, 1.5f}, {FacialHair::BraidedBeard, 1}});
            break;
        }
        case Species::Skeleton:  // risen bones: any human frame, eyes burning in the sockets
            b.skeletal = true; b.belly = 0.0f;
            glow(r.chance(0.6f) ? Vec3{0.45f, 0.85f, 1.0f} : r.chance(0.5f) ? Vec3{1.0f, 0.45f, 0.15f} : Vec3{0.55f, 1.0f, 0.45f});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        // ---- trait peoples: composed from the species traits ----
        case Species::Dragonborn: {  // draconic folk: scaled, horned, a reptile snout, sometimes wings and a tail
            b.boneMass *= 1.25f;
            height(fem ? 1.80f : 1.92f, fem ? 1.95f : 2.10f);
            b.headsTall = r.range(6.9f, 7.2f);
            if (fem) widths(1.12f, 1.10f, 1.05f, 0.98f, 1.10f);
            else widths(1.35f, 1.32f, 1.18f, 1.02f, 1.28f);
            b.muscle = saturate(b.muscle + 0.3f); b.belly = 0.0f; b.bust *= 0.55f; b.butt *= 0.8f; b.neckThick *= 1.25f;
            b.muzzle = fem ? 0.80f : 0.92f; b.snout = 2; b.skinKind = 3; b.claws = true; b.ears = EarStyle::None;
            b.horns = 2; b.hornKind = r.chance(0.6f) ? 0 : 2; b.hornSize = fem ? 0.9f : 1.15f;
            if (r.chance(0.45f)) { b.spines = r.rangeInt(6, 9); b.spineKind = 1; b.spineSize = r.range(0.7f, 1.0f); }
            if (r.chance(0.40f)) { b.tail = true; b.tailType = 1; b.tailLength = r.range(0.8f, 1.1f); }
            if (r.chance(0.25f)) { b.wings = 1; b.wingsFolded = r.chance(0.5f); b.wingSize = r.range(0.85f, 1.05f); }
            f.pupil = PupilStyle::Slit; f.eyeShape = 3; f.browRidge = 1.0f; f.eyeDepth = 0.7f; f.noseProj = 0.5f; f.noseWidth = 1.3f;
            f.mouthStyle = 1; f.mouthWidth = 1.5f; f.lipFullness = 0.05f; f.stubble = 0.0f; f.cheek = 0.6f;
            const Vec3 scale = pickC({{0.55f, 0.16f, 0.12f}, {0.62f, 0.42f, 0.18f}, {0.70f, 0.58f, 0.32f}, {0.20f, 0.38f, 0.24f},
                                      {0.18f, 0.28f, 0.48f}, {0.16f, 0.15f, 0.17f}, {0.72f, 0.74f, 0.76f}, {0.48f, 0.34f, 0.22f}});
            skinTo(scale);
            pal.belly = lerp(scale, Vec3{0.88f, 0.78f, 0.58f}, 0.55f);
            pal.wing = scale * 0.70f;
            pal.horn = lerp(scale * 0.5f, Vec3{0.85f, 0.80f, 0.68f}, 0.5f);
            pal.beak = pal.horn;
            pal.iris = pickC({{0.95f, 0.70f, 0.15f}, {0.90f, 0.35f, 0.10f}, {0.55f, 0.85f, 0.35f}});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Saurian: {  // lizardfolk and their small kin (kobolds): a long snout, a heavy tail, digitigrade legs
            const bool small = r.chance(0.3f);
            b.boneMass *= small ? 0.85f : 1.1f;
            if (small) height(0.95f, 1.15f), b.headsTall = r.range(4.4f, 4.8f), b.legRatio = 0.44f;
            else height(fem ? 1.68f : 1.78f, fem ? 1.85f : 1.98f), b.headsTall = r.range(6.8f, 7.1f);
            widths(1.05f, 1.02f, 0.98f, 0.95f, 1.0f);
            b.belly = 0.0f; b.bust *= 0.3f; b.butt *= 0.7f; b.hunch = std::max(b.hunch, 0.7f);
            b.muzzle = 1.0f; b.snout = 2; b.skinKind = 3; b.claws = true; b.digitigrade = true; b.ears = EarStyle::None;
            b.tail = true; b.tailType = 1; b.tailLength = r.range(1.0f, 1.3f);
            if (r.chance(0.55f)) { b.crest = 1; b.crestSize = r.range(0.6f, 1.0f); }
            else if (r.chance(0.5f)) { b.spines = r.rangeInt(7, 10); b.spineKind = 2; b.spineSize = 0.7f; }
            if (small && r.chance(0.5f)) { b.horns = 2; b.hornKind = 0; b.hornSize = 0.6f; }
            f.pupil = PupilStyle::Slit; f.eyeShape = 1; f.eyeSize = 1.1f; f.noseProj = 0.4f; f.mouthStyle = 1; f.mouthWidth = 1.6f;
            f.lipFullness = 0.0f; f.browRidge = 0.9f; f.stubble = 0.0f;
            const Vec3 scale = pickC({{0.30f, 0.42f, 0.22f}, {0.38f, 0.36f, 0.22f}, {0.22f, 0.38f, 0.36f}, {0.46f, 0.30f, 0.20f}, {0.28f, 0.30f, 0.26f}});
            skinTo(scale);
            pal.belly = lerp(scale, Vec3{0.82f, 0.76f, 0.50f}, 0.55f);
            pal.wing = pickC({{0.70f, 0.25f, 0.15f}, {0.85f, 0.65f, 0.20f}, {0.20f, 0.45f, 0.55f}});
            pal.horn = Vec3{0.30f, 0.28f, 0.24f};
            pal.iris = pickC({{0.95f, 0.75f, 0.15f}, {0.85f, 0.45f, 0.10f}});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Avian: {  // bird folk: a beak, feathered wings, talons; raptors, long-billed scholars, owls and crows
            b.boneMass *= 0.75f;
            height(fem ? 1.55f : 1.62f, fem ? 1.80f : 1.92f);
            b.headsTall = r.range(6.4f, 6.8f);
            widths(0.92f, 0.95f, 0.85f, 0.85f, 0.82f);
            b.belly = 0.0f; b.bust *= 0.3f; b.butt *= 0.6f; b.neckLength *= 1.25f; b.neckThick *= 0.9f;
            b.ears = EarStyle::None; b.digitigrade = true; b.feet = 1; b.hands = r.chance(0.6f) ? 1 : 0; b.furPattern = 2;
            b.wings = 2; b.wingsFolded = r.chance(0.35f); b.wingSize = r.range(0.85f, 1.05f);
            if (r.chance(0.7f)) { b.crest = 2; b.crestSize = r.range(0.6f, 1.1f); }
            f.beak = r.weighted({1.2f, 1.0f, 1.0f}) + 1;
            f.eyeShape = 1; f.eyeSize = 1.2f; f.irisSize = 1.25f; f.browThick = 0.15f; f.lashWeight = 0.05f; f.stubble = 0.0f;
            f.chinPoint = 0.4f; f.headTop = 0.3f; f.cheek = 0.0f;
            const Vec3 feather = pickC({{0.35f, 0.24f, 0.16f}, {0.18f, 0.17f, 0.20f}, {0.55f, 0.48f, 0.38f}, {0.62f, 0.60f, 0.56f},
                                        {0.28f, 0.32f, 0.40f}, {0.45f, 0.20f, 0.14f}});
            skinTo(feather);
            pal.wing = feather * 0.85f;
            pal.belly = lerp(feather, Vec3{0.90f, 0.85f, 0.75f}, 0.5f);
            pal.beak = pickC({{0.85f, 0.70f, 0.30f}, {0.20f, 0.19f, 0.20f}, {0.75f, 0.72f, 0.62f}});
            pal.iris = pickC({{0.95f, 0.75f, 0.15f}, {0.95f, 0.50f, 0.10f}, {0.10f, 0.08f, 0.08f}});
            hairColor(feather * 0.8f);
            hair({{HairStyle::Messy, 2}, {HairStyle::ShortCrop, 1}, {HairStyle::Wolf, 1.5f}, {HairStyle::Bald, 1}});
            g.fringe = Fringe::None;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Naga: {  // serpent folk: a human torso on a long scaled coil; hoods, fins and sea-witch hair
            b.boneMass *= 1.05f;
            height(fem ? 2.05f : 2.20f, fem ? 2.25f : 2.45f);
            b.headsTall = r.range(7.6f, 8.0f);
            b.legRatio = 0.50f;
            widths(fem ? 1.0f : 1.15f, fem ? 1.0f : 1.12f, 0.95f, 1.05f, 1.05f);
            b.lowerBody = 1; b.serpentLength = r.range(0.9f, 1.2f); b.skinKind = 3; b.claws = r.chance(0.5f);
            if (r.chance(0.4f)) b.crest = 4, b.crestSize = r.range(0.8f, 1.1f);
            else if (r.chance(0.5f)) b.ears = EarStyle::Fin;
            if (r.chance(0.15f)) b.armPairs = 2;
            f.pupil = PupilStyle::Slit; f.eyeShape = 3; f.eyeTilt = 0.12f; f.noseProj = 0.6f; f.noseWidth = 0.85f; f.cheek = 0.6f;
            f.chinPoint = 0.3f; f.stubble = 0.0f; f.fang = true;
            const Vec3 scale = pickC({{0.20f, 0.42f, 0.42f}, {0.28f, 0.40f, 0.22f}, {0.36f, 0.26f, 0.44f}, {0.20f, 0.24f, 0.36f}, {0.46f, 0.38f, 0.22f}});
            skinTo(scale);
            pal.belly = lerp(scale, Vec3{0.80f, 0.74f, 0.56f}, 0.32f);
            pal.wing = lerp(scale, Vec3{0.75f, 0.30f, 0.25f}, 0.35f);
            pal.iris = pickC({{0.95f, 0.75f, 0.15f}, {0.55f, 0.90f, 0.40f}, {0.90f, 0.30f, 0.20f}});
            hairColor(pickC({{0.12f, 0.12f, 0.14f}, {0.18f, 0.40f, 0.42f}, {0.85f, 0.85f, 0.88f}, {0.40f, 0.20f, 0.45f}}));
            hair({{HairStyle::VeryLong, fem ? 2.0f : 0.3f}, {HairStyle::LongWavy, fem ? 2.0f : 0.5f}, {HairStyle::Bald, fem ? 0.2f : 2.5f},
                  {HairStyle::HighPonytail, 1}, {HairStyle::Locs, 1}});
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Tortle: {  // turtle folk: a domed shell, a beak, a stout slow frame
            b.boneMass *= 1.3f;
            height(fem ? 1.55f : 1.62f, fem ? 1.75f : 1.85f);
            b.headsTall = r.range(5.8f, 6.2f);
            b.legRatio = 0.44f;
            widths(1.25f, 1.30f, 1.40f, 1.18f, 1.30f);
            b.chestDepth *= 1.25f; b.neckThick *= 1.35f; b.neckLength *= 0.8f; b.handSize *= 1.15f; b.footSize *= 1.2f;
            b.bust *= 0.3f; b.belly = 0.2f; b.hunch = std::max(b.hunch, 0.8f);
            b.shell = true; b.skinKind = 3; b.claws = true; b.ears = EarStyle::None;
            f.beak = 3; f.eyeShape = 1; f.eyeSize = 0.95f; f.browThick = 0.1f; f.lashWeight = 0.05f; f.wrinkles = 0.6f; f.headTop = -0.2f;
            f.stubble = 0.0f;
            const Vec3 hide = pickC({{0.38f, 0.44f, 0.30f}, {0.44f, 0.42f, 0.32f}, {0.32f, 0.38f, 0.36f}});
            skinTo(hide);
            pal.horn = pickC({{0.30f, 0.26f, 0.18f}, {0.24f, 0.26f, 0.20f}, {0.36f, 0.30f, 0.20f}});
            pal.belly = Vec3{0.72f, 0.64f, 0.44f};
            pal.beak = hide * 0.55f + Vec3{0.08f, 0.07f, 0.05f};
            pal.iris = pickC({{0.30f, 0.20f, 0.12f}, {0.75f, 0.55f, 0.15f}});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Insectoid: {  // mantis and spider kin: four arms, chitin, mandibles, antennae, compound eyes
            b.boneMass *= 0.8f;
            height(fem ? 1.95f : 2.05f, fem ? 2.15f : 2.30f);
            b.headsTall = r.range(7.4f, 7.8f);
            b.legRatio = 0.53f;
            widths(0.95f, 0.92f, 0.70f, 0.80f, 0.80f);
            b.armPairs = 2; b.belly = 0.0f; b.bust = 0.0f; b.butt *= 0.5f; b.armLength *= 1.1f;
            b.skinKind = 2; b.carapace = 0.92f; b.digitigrade = true; b.claws = true; b.ears = EarStyle::None;
            b.hands = r.chance(0.6f) ? 1 : 2;
            if (r.chance(0.35f)) b.wings = 3, b.wingSize = r.range(0.8f, 1.0f);
            f.mandibles = true; f.antennae = 1; f.antennaLength = r.range(0.8f, 1.2f); f.eyeKind = 1; f.eyeSize = 1.1f;
            f.noseProj = 0.25f; f.noseWidth = 0.6f; f.mouthStyle = 0; f.lipFullness = 0.0f; f.chinPoint = 0.8f; f.headTop = 0.2f;
            f.browThick = 0.0f; f.stubble = 0.0f;
            const Vec3 chitin = pickC({{0.22f, 0.30f, 0.16f}, {0.30f, 0.22f, 0.14f}, {0.12f, 0.11f, 0.13f}, {0.26f, 0.18f, 0.30f}, {0.40f, 0.36f, 0.20f}});
            b.carapaceColor = chitin;
            skinTo(lerp(chitin, Vec3{0.55f, 0.50f, 0.40f}, 0.45f));
            pal.beak = chitin * 1.3f;
            pal.wing = lerp(chitin, Vec3{0.75f, 0.80f, 0.70f}, 0.5f);
            pal.horn = chitin * 0.8f;
            pal.iris = pickC({{0.10f, 0.12f, 0.10f}, {0.35f, 0.08f, 0.06f}, {0.10f, 0.20f, 0.30f}});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Fishfolk: {  // deep ones: finned heads and ears, a shark's grin, scaled hides
            b.boneMass *= 1.05f;
            height(fem ? 1.68f : 1.78f, fem ? 1.88f : 2.00f);
            b.headsTall = r.range(6.7f, 7.0f);
            widths(fem ? 1.0f : 1.15f, fem ? 1.0f : 1.10f, 1.0f, 0.95f, 1.05f);
            b.belly = 0.0f; b.hunch = std::max(b.hunch, 0.6f); b.handSize *= 1.15f; b.footSize *= 1.2f;
            b.skinKind = 3; b.claws = true; b.ears = EarStyle::Fin; b.crest = 1; b.crestSize = r.range(0.7f, 1.1f);
            if (r.chance(0.3f)) { b.tail = true; b.tailType = 0; b.tailLength = 0.7f; }
            f.mouthStyle = 3; f.mouthWidth = 1.6f; f.lipFullness = 0.0f; f.eyeShape = 1; f.eyeSize = 1.15f; f.irisSize = 1.2f;
            f.noseProj = 0.25f; f.noseWidth = 1.2f; f.noseLength = 0.6f; f.browThick = 0.0f; f.chinWidth = 0.5f; f.jaw = 0.6f;
            f.underbite = 0.3f; f.stubble = 0.0f; f.lashWeight = 0.0f;
            if (r.chance(0.5f)) glow(pickC({{0.45f, 0.95f, 0.85f}, {0.95f, 0.85f, 0.40f}}));
            const Vec3 hide = pickC({{0.22f, 0.36f, 0.40f}, {0.28f, 0.40f, 0.30f}, {0.32f, 0.34f, 0.40f}, {0.20f, 0.26f, 0.30f}});
            skinTo(hide);
            pal.belly = lerp(hide, Vec3{0.85f, 0.85f, 0.78f}, 0.5f);
            pal.wing = lerp(hide, Vec3{0.75f, 0.35f, 0.25f}, 0.4f);
            pal.iris = pickC({{0.85f, 0.75f, 0.20f}, {0.15f, 0.15f, 0.15f}});
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Demon: {  // fiends: bat wings, horns, a spade tail, hooves; burning eyes
            b.boneMass *= 1.1f;
            height(fem ? 1.85f : 2.00f, fem ? 2.05f : 2.25f);
            b.headsTall = r.range(7.2f, 7.5f);
            b.legRatio = 0.52f;
            if (fem) widths(1.0f, 1.0f, 0.95f, 1.05f, 1.0f);
            else widths(1.28f, 1.25f, 1.05f, 1.0f, 1.20f), b.muscle = saturate(b.muscle + 0.3f);
            b.horns = 2; b.hornKind = r.rangeInt(0, 2); b.hornSize = fem ? 0.9f : 1.2f;
            b.tail = true; b.tailType = 2; b.tailLength = r.range(0.9f, 1.2f);
            if (r.chance(0.6f)) b.digitigrade = true, b.hooves = true;
            if (r.chance(0.7f)) b.wings = 1, b.wingsFolded = r.chance(0.3f), b.wingSize = r.range(0.85f, 1.1f);
            b.claws = true;
            if (!fem && r.chance(0.3f)) { b.spines = 6; b.spineKind = 0; b.spineSize = 0.7f; }
            b.ear = {0.30f, 0.07f, 0.35f, 0.45f, 0.0f};
            b.ears = EarStyle::Elf;
            f.fang = true; f.eyeShape = 3; f.eyeTilt = 0.14f; f.browTilt = -0.35f; f.cheek = 0.7f; f.chinPoint = 0.3f;
            glow(pickC({{0.55f, 1.0f, 0.30f}, {1.0f, 0.55f, 0.15f}, {1.0f, 0.25f, 0.20f}}));
            skinTo(pickC({{0.50f, 0.14f, 0.12f}, {0.36f, 0.20f, 0.38f}, {0.30f, 0.30f, 0.34f}, {0.16f, 0.14f, 0.20f}, {0.58f, 0.32f, 0.40f}}));
            pal.wing = pal.skin * 0.80f;
            pal.horn = pickC({{0.12f, 0.11f, 0.11f}, {0.60f, 0.52f, 0.42f}});
            pal.beak = pal.horn;
            hairColor(pickC({{0.08f, 0.07f, 0.08f}, {0.60f, 0.10f, 0.10f}, {0.85f, 0.85f, 0.88f}, {0.30f, 0.15f, 0.35f}}));
            hair({{HairStyle::LongStraight, 2}, {HairStyle::VeryLong, fem ? 1.5f : 0.3f}, {HairStyle::HighPonytail, 1}, {HairStyle::Bald, fem ? 0.0f : 1.5f},
                  {HairStyle::SlickedBack, 1}, {HairStyle::Wolf, 1}});
            beard({{FacialHair::None, 3}, {FacialHair::Goatee, 2}});
            break;
        }
        case Species::Cephalid: {  // mind-eaters: a swollen domed skull, a mouth hidden by four face tentacles
            b.boneMass *= 0.85f;
            height(fem ? 1.75f : 1.82f, fem ? 1.90f : 1.98f);
            b.headsTall = r.range(6.8f, 7.1f);
            widths(0.90f, 0.90f, 0.85f, 0.90f, 0.80f);
            b.muscle *= 0.5f; b.belly = 0.0f; b.bust *= 0.4f; b.armLength *= 1.08f; b.handSize *= 1.1f; b.ears = EarStyle::None;
            b.hands = r.chance(0.5f) ? 3 : 0;
            if (r.chance(0.3f)) b.backTentacles = 2 * r.rangeInt(1, 2), b.tentacleLength = r.range(0.8f, 1.1f);
            f.headTop = 1.0f; f.crown = 0.8f; f.noseProj = 0.15f; f.noseWidth = 0.7f; f.noseLength = 0.6f; f.browThick = 0.0f;
            f.browRidge = 0.5f; f.cheekHollow = 0.6f; f.chinPoint = 0.2f; f.lashWeight = 0.0f; f.stubble = 0.0f; f.lipFullness = 0.0f;
            glow(pickC({{0.92f, 0.92f, 0.85f}, {0.70f, 0.95f, 0.85f}}));
            const Vec3 hide = pickC({{0.55f, 0.42f, 0.55f}, {0.42f, 0.44f, 0.52f}, {0.48f, 0.52f, 0.44f}, {0.62f, 0.50f, 0.50f}});
            skinTo(hide);
            pal.lips = hide * 0.6f;
            pal.belly = lerp(hide, Vec3{0.85f, 0.70f, 0.70f}, 0.4f);
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::Tendrils;
            break;
        }
        case Species::Fae: {  // dusk fae: gauzy insect wings, feathered antennae, small and slight
            b.boneMass *= 0.7f;
            height(fem ? 1.30f : 1.36f, fem ? 1.52f : 1.60f);
            b.headsTall = r.range(6.0f, 6.4f);
            b.legRatio = 0.53f;
            widths(0.88f, 0.88f, 0.85f, 0.92f, 0.82f);
            b.muscle *= 0.6f; b.belly = 0.0f;
            b.wings = 3; b.wingSize = r.range(0.9f, 1.15f);
            b.ear = {0.55f, 0.08f, 0.50f, 0.45f, 0.0f};
            b.ears = EarStyle::Elf;
            if (r.chance(0.55f)) f.antennae = 2, f.antennaLength = r.range(0.8f, 1.1f);
            f.eyeShape = 3; f.eyeSize = 1.12f; f.eyeTilt = 0.14f; f.chinPoint = 0.5f; f.cheek = 0.5f; f.noseProj = 0.7f;
            glow(pickC({{0.70f, 0.95f, 0.75f}, {0.85f, 0.75f, 1.0f}, {1.0f, 0.85f, 0.45f}}));
            skinTo(pickC({{0.78f, 0.80f, 0.82f}, {0.62f, 0.66f, 0.58f}, {0.72f, 0.64f, 0.78f}, {0.50f, 0.56f, 0.62f}}));
            pal.wing = pickC({{0.55f, 0.50f, 0.42f}, {0.40f, 0.48f, 0.52f}, {0.50f, 0.40f, 0.52f}, {0.62f, 0.60f, 0.50f}});
            hairColor(pickC({{0.85f, 0.85f, 0.88f}, {0.25f, 0.30f, 0.22f}, {0.45f, 0.30f, 0.55f}, {0.70f, 0.55f, 0.30f}}));
            hair({{HairStyle::LongWavy, 2}, {HairStyle::Messy, 1.5f}, {HairStyle::Pixie, 1.5f}, {HairStyle::VeryLong, 1}, {HairStyle::Wolf, 1}});
            beard({{FacialHair::None, 6}});
            break;
        }
        case Species::Crystalborn: {  // living stone: a hide of crystal facets, shards breaking from the shoulders
            b.boneMass *= 1.5f;
            height(fem ? 2.00f : 2.15f, fem ? 2.25f : 2.45f);
            b.headsTall = r.range(7.2f, 7.6f);
            if (fem) widths(1.25f, 1.25f, 1.20f, 1.10f, 1.25f);
            else widths(1.50f, 1.45f, 1.30f, 1.10f, 1.45f);
            b.muscle = saturate(b.muscle + 0.3f); b.belly = 0.0f; b.bust *= 0.5f; b.neckThick *= 1.3f;
            b.skinKind = 4; b.shards = r.rangeInt(8, 14); b.ears = EarStyle::None;
            b.crystalColor = pickC({{0.55f, 0.80f, 0.95f}, {0.70f, 0.45f, 0.90f}, {0.40f, 0.90f, 0.60f}, {0.95f, 0.55f, 0.65f}});
            glow(b.crystalColor);
            f.browRidge = 1.0f; f.jaw = 0.8f; f.chinWidth = 0.7f; f.noseProj = 0.6f; f.browThick = 0.0f; f.lipFullness = 0.1f; f.stubble = 0.0f;
            skinTo(pickC({{0.40f, 0.40f, 0.44f}, {0.34f, 0.32f, 0.36f}, {0.46f, 0.44f, 0.40f}}));
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Emberkin: {  // fire-born: a black basalt crust split by molten seams
            b.boneMass *= 1.3f;
            height(fem ? 1.90f : 2.05f, fem ? 2.10f : 2.30f);
            b.headsTall = r.range(7.0f, 7.4f);
            if (fem) widths(1.12f, 1.12f, 1.05f, 1.02f, 1.12f);
            else widths(1.40f, 1.36f, 1.15f, 1.02f, 1.32f);
            b.muscle = saturate(b.muscle + 0.35f); b.belly = 0.0f; b.bust *= 0.6f;
            b.skinKind = 5; b.ears = EarStyle::None;
            b.crystalColor = pickC({{1.0f, 0.50f, 0.12f}, {1.0f, 0.32f, 0.10f}, {0.95f, 0.75f, 0.25f}});
            if (r.chance(0.6f)) b.horns = 2, b.hornKind = r.rangeInt(0, 2), b.hornSize = r.range(0.8f, 1.2f);
            if (r.chance(0.3f)) b.core = 2, b.coreColor = b.crystalColor;
            if (r.chance(0.3f)) { b.spines = r.rangeInt(5, 8); b.spineKind = 2; b.spineSize = 0.8f; }
            glow(b.crystalColor);
            f.browRidge = 1.1f; f.jaw = 0.7f; f.browThick = 0.0f; f.lipFullness = 0.05f; f.stubble = 0.0f; f.mouthStyle = 1;
            skinTo(pickC({{0.16f, 0.14f, 0.13f}, {0.22f, 0.17f, 0.15f}, {0.13f, 0.12f, 0.14f}}));
            pal.horn = Vec3{0.10f, 0.09f, 0.09f};
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Wraith: {  // the restless dead: a gaunt upper body trailing into a tattered wisp of shadow
            b.boneMass *= 0.85f;
            height(fem ? 1.95f : 2.05f, fem ? 2.15f : 2.25f);
            b.headsTall = r.range(7.4f, 7.8f);
            widths(0.95f, 0.88f, 0.75f, 0.85f, 0.75f);
            b.lowerBody = 2; b.serpentLength = r.range(0.9f, 1.2f); b.muscle *= 0.4f; b.belly = 0.0f; b.bust *= 0.5f;
            b.armLength *= 1.12f; b.handSize *= 1.15f; b.claws = true; b.hunch = std::max(b.hunch, 0.7f);
            b.anat.definition = 0.9f; b.anat.ribcage = 1.0f; b.anat.clavicles = 1.0f;
            f.cheekHollow = 1.0f; f.eyeDepth = 1.0f; f.jowl = 0.0f; f.roundness = 0.0f; f.lipFullness = 0.05f; f.ruddy = 0.0f;
            f.stubble = 0.0f;
            glow(pickC({{0.55f, 0.85f, 1.0f}, {0.75f, 1.0f, 0.80f}, {0.90f, 0.90f, 0.95f}}));
            skinTo(pickC({{0.55f, 0.60f, 0.66f}, {0.48f, 0.52f, 0.50f}, {0.42f, 0.42f, 0.50f}}));
            pal.lips = pal.skin * 0.5f;
            pal.underwear = Vec3{0.12f, 0.12f, 0.14f};
            hairColor(pickC({{0.78f, 0.80f, 0.82f}, {0.12f, 0.12f, 0.14f}, {0.40f, 0.42f, 0.46f}}));
            hair({{HairStyle::VeryLong, 2}, {HairStyle::LongStraight, 2}, {HairStyle::Messy, 1}, {HairStyle::Bald, 1}});
            beard({{FacialHair::None, 3}, {FacialHair::LongBeard, 1}});
            break;
        }
        case Species::Cyclops: {  // one-eyed giants: a single great eye under a heavy brow, hulking and stooped
            b.boneMass *= 1.45f;
            height(fem ? 2.35f : 2.55f, fem ? 2.65f : 2.95f);
            b.headsTall = r.range(7.2f, 7.6f);
            b.legRatio = 0.46f;
            if (fem) widths(1.55f, 1.55f, 1.55f, 1.25f, 1.55f);
            else widths(1.75f, 1.70f, 1.55f, 1.20f, 1.70f);
            b.chestDepth *= 1.3f; b.handSize *= 1.4f; b.footSize *= 1.25f; b.neckThick *= 1.6f; b.neckLength *= 0.5f;
            b.muscle = saturate(b.muscle + 0.4f); b.hunch = std::max(b.hunch, 1.0f); b.belly += fem ? 0.05f : 0.2f; b.bust *= 0.8f;
            if (r.chance(0.35f)) b.horns = 2, b.hornKind = r.chance(0.5f) ? 1 : 3, b.hornSize = r.range(0.7f, 1.0f);
            b.ear = {0.14f, 0.09f, 0.12f, 0.25f, 0.0f};
            f.eyeKind = 2; f.eyeSize = r.range(0.95f, 1.15f); f.browRidge = 1.3f; f.browThick = 1.0f; f.jaw = 1.0f; f.chinWidth = 0.9f;
            f.noseWidth = 1.5f; f.noseProj = 0.8f; f.noseLength = 0.7f; f.mouthWidth = 1.3f; f.underbite = 0.5f; f.wrinkles = 0.4f;
            f.sclera = Vec3{0.88f, 0.84f, 0.72f};
            skinTo(pickC({{0.62f, 0.50f, 0.42f}, {0.52f, 0.46f, 0.40f}, {0.48f, 0.50f, 0.52f}, {0.58f, 0.44f, 0.36f}}));
            pal.iris = pickC({{0.55f, 0.35f, 0.12f}, {0.30f, 0.45f, 0.55f}, {0.70f, 0.20f, 0.10f}, {0.40f, 0.45f, 0.20f}});
            hairColor(pickC({{0.12f, 0.11f, 0.11f}, {0.35f, 0.24f, 0.15f}, {0.55f, 0.52f, 0.50f}}));
            hair({{HairStyle::Bald, fem ? 0.3f : 3.0f}, {HairStyle::Messy, 2}, {HairStyle::VeryLong, fem ? 1.5f : 0.5f}, {HairStyle::Topknot, 1}});
            beard({{FacialHair::None, 1}, {FacialHair::FullBeard, 2}, {FacialHair::LongBeard, 2}, {FacialHair::BraidedBeard, 1}});
            break;
        }
        case Species::Talpid: {  // burrowers: squat mole folk with spade hands, a long probing snout and buried eyes
            b.boneMass *= 1.1f;
            height(fem ? 1.20f : 1.28f, fem ? 1.40f : 1.50f);
            b.headsTall = r.range(5.4f, 5.8f);
            b.legRatio = 0.44f;
            widths(1.35f, 1.35f, 1.30f, 1.10f, 1.30f);
            b.chestDepth *= 1.2f; b.neckLength *= 0.4f; b.neckThick *= 1.5f; b.handSize *= 1.35f; b.footSize *= 1.1f;
            b.muscle = saturate(b.muscle + 0.3f); b.hunch = std::max(b.hunch, 1.1f); b.belly += 0.15f; b.bust *= 0.5f;
            b.hands = 4; b.claws = true; b.furPattern = 2; b.ears = EarStyle::None;
            b.muzzle = r.range(0.75f, 0.95f); b.snout = 1;
            if (r.chance(0.3f)) { b.tail = true; b.tailType = 5; b.tailLength = 0.45f; }
            f.darkNose = true; f.eyeSize = 0.55f; f.eyeDepth = 1.0f; f.browRidge = 0.6f; f.noseWidth = 1.2f; f.noseProj = 1.0f;
            f.mouthWidth = 0.9f; f.lipFullness = 0.05f; f.stubble = 0.0f; f.lashWeight = 0.0f;
            if (r.chance(0.4f)) f.eyeKind = 3;  // blind diggers: skin grown over the eyes
            const Vec3 fur = pickC({{0.20f, 0.18f, 0.18f}, {0.30f, 0.25f, 0.21f}, {0.36f, 0.34f, 0.33f}, {0.24f, 0.20f, 0.24f}});
            skinTo(fur);
            pal.lips = fur * 0.5f;
            pal.beak = Vec3{0.80f, 0.74f, 0.62f};  // pale digging claws
            pal.horn = pal.beak;
            pal.iris = Vec3{0.08f, 0.08f, 0.08f};
            hairColor(fur * 0.85f);
            if (!opt.hair) g.hair = HairStyle::Bald;
            beard({{FacialHair::None, 4}, {FacialHair::MuttonChops, 1}});
            break;
        }
        case Species::Oculite: {  // the watchers: gaunt folk with eyes opening all over the skin
            b.boneMass *= 0.9f;
            height(fem ? 1.70f : 1.78f, fem ? 1.90f : 2.00f);
            b.headsTall = r.range(7.0f, 7.4f);
            widths(0.92f, 0.92f, 0.85f, 0.92f, 0.85f);
            b.muscle *= 0.6f; b.belly = 0.0f; b.bust *= 0.6f; b.hunch = std::max(b.hunch, 0.5f);
            b.bodyEyes = r.rangeInt(14, 24); b.ears = EarStyle::None;
            if (r.chance(0.4f)) f.eyePairs = 2;
            f.eyeSize = 1.15f; f.irisSize = 1.1f; f.browThick = 0.0f; f.lashWeight = 0.0f; f.stubble = 0.0f; f.cheekHollow = 0.6f;
            f.noseProj = 0.3f; f.noseLength = 0.6f; f.lipFullness = 0.05f;
            f.sclera = pickC({{0.85f, 0.80f, 0.55f}, {0.80f, 0.74f, 0.60f}, {0.82f, 0.82f, 0.78f}});
            skinTo(pickC({{0.62f, 0.56f, 0.58f}, {0.50f, 0.52f, 0.56f}, {0.58f, 0.54f, 0.46f}, {0.42f, 0.36f, 0.42f}}));
            pal.iris = pickC({{0.75f, 0.45f, 0.10f}, {0.35f, 0.55f, 0.20f}, {0.55f, 0.15f, 0.12f}, {0.30f, 0.30f, 0.55f}});
            pal.lips = pal.skin * 0.6f;
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Slime: {  // living ooze: a soft, featureless upper body rising out of a gelatinous mound
            b.boneMass *= 0.6f;
            height(fem ? 1.60f : 1.68f, fem ? 1.85f : 1.95f);
            b.headsTall = r.range(6.4f, 6.8f);
            widths(1.05f, 1.10f, 1.15f, 1.10f, 1.05f);
            b.legRatio = 0.36f;  // the body has sunk into its own mass
            g.underwear = false;
            b.lowerBody = 3; b.serpentLength = r.range(0.9f, 1.2f); b.muscle *= 0.2f; b.belly += 0.2f; b.bust *= 0.4f;
            b.anat.definition = 0.0f; b.ears = EarStyle::None;
            b.hands = r.chance(0.5f) ? 3 : 0;
            f.noseProj = 0.15f; f.noseWidth = 0.8f; f.noseLength = 0.5f; f.browThick = 0.0f; f.browRidge = 0.0f; f.lipFullness = 0.0f;
            f.roundness = 1.0f; f.stubble = 0.0f; f.lashWeight = 0.0f; f.chinPoint = 0.0f; f.jaw = 0.2f;
            glow(pickC({{0.75f, 1.0f, 0.55f}, {0.95f, 0.80f, 0.35f}, {0.85f, 0.65f, 1.0f}}));
            skinTo(pickC({{0.38f, 0.52f, 0.30f}, {0.46f, 0.36f, 0.52f}, {0.30f, 0.44f, 0.46f}, {0.55f, 0.48f, 0.25f}}));
            pal.belly = pal.skin * 0.7f;
            pal.lips = pal.skin * 0.5f;
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        case Species::Rimemaw: {  // ice-shore hunters: broad, hunched, a jaw split from ear to ear, small cold eyes
            b.boneMass *= 1.25f;
            height(fem ? 1.85f : 1.95f, fem ? 2.10f : 2.25f);
            b.headsTall = r.range(6.6f, 7.0f);
            if (fem) widths(1.25f, 1.25f, 1.25f, 1.05f, 1.20f);
            else widths(1.45f, 1.40f, 1.30f, 1.05f, 1.35f);
            b.chestDepth *= 1.2f; b.neckThick *= 1.5f; b.neckLength *= 0.5f; b.handSize *= 1.2f; b.footSize *= 1.2f;
            b.muscle = saturate(b.muscle + 0.25f); b.hunch = std::max(b.hunch, 1.1f); b.belly += 0.2f; b.bust *= 0.6f;
            b.skinKind = 3; b.claws = true; b.ears = r.chance(0.5f) ? EarStyle::Fin : EarStyle::None; b.digitigrade = r.chance(0.4f);
            if (r.chance(0.4f)) { b.spines = r.rangeInt(5, 8); b.spineKind = 2; b.spineSize = 0.7f; }
            f.maw = true; f.mouthWidth = 1.8f; f.eyeSize = 0.7f; f.eyeShape = 1; f.browRidge = 1.1f; f.jaw = 1.0f; f.chinWidth = 1.0f;
            f.noseProj = 0.25f; f.noseWidth = 1.3f; f.noseLength = 0.6f; f.lipFullness = 0.0f; f.stubble = 0.0f; f.lashWeight = 0.0f;
            f.headTop = -0.4f;
            glow(pickC({{0.65f, 0.90f, 1.0f}, {0.85f, 0.95f, 1.0f}}));
            const Vec3 hide = pickC({{0.62f, 0.68f, 0.72f}, {0.48f, 0.56f, 0.62f}, {0.70f, 0.70f, 0.66f}, {0.36f, 0.42f, 0.50f}});
            skinTo(hide);
            pal.belly = lerp(hide, Vec3{0.92f, 0.92f, 0.90f}, 0.5f);
            pal.lips = hide * 0.4f;
            if (!opt.hair) g.hair = HairStyle::Bald;
            g.facialHair = FacialHair::None;
            break;
        }
        default: break;
    }
    if (f.eyeKind != 0) f.eyePairs = 1;  // modelled eyes replace the painted pairs
    if (g.hair == HairStyle::SlickedBack || g.hair == HairStyle::Bald || g.hair == HairStyle::Topknot || g.hair == HairStyle::WarriorBraids)
        g.fringe = Fringe::None;
    pal.underwear = sp == Species::Undead ? Vec3{0.30f, 0.28f, 0.27f} : pal.underwear;
    // Dark-fantasy grade: fantastical hues are muted and a touch darker, never candy-bright.
    auto grade = [](Vec3 c, float sat, float val) {
        const float l = luminance(c);
        return lerp(Vec3{l, l, l}, c, sat) * val;
    };
    if (sp == Species::Orc || sp == Species::Troll || sp == Species::NightElf || sp == Species::Draenei || sp == Species::Goblin ||
        sp == Species::Undead || sp == Species::Dragonborn || sp == Species::Saurian || sp == Species::Naga || sp == Species::Fishfolk ||
        sp == Species::Demon || sp == Species::Cephalid || sp == Species::Fae || sp == Species::Wraith || sp == Species::Oculite ||
        sp == Species::Slime || sp == Species::Rimemaw) {
        pal.skin = grade(pal.skin, 0.62f, 0.86f);
        pal.lips = grade(pal.lips, 0.62f, 0.86f);
    }
    if (sp == Species::Gnome || sp == Species::Troll || sp == Species::NightElf) {
        pal.hair = grade(pal.hair, 0.62f, 0.88f);
        pal.hairTip = pal.hair * 0.88f;
    }
}

}  // namespace

Genome randomGenome(uint64_t seed, const GenomeOptions& opt) {
    Genome g;
    g.seed = seed;
    Rng root(seed);
    const Archetype arch = opt.archetype.value_or(Archetype::Natural);
    g.archetype = arch;

    // ---- identity: sex, age, persona ----
    Rng ri = root.fork(1);
    bool fem = ri.chance(0.5f);
    if (arch == Archetype::Bombshell || arch == Archetype::Amazon) fem = true;
    if (arch == Archetype::Dorito || arch == Archetype::Brute) fem = false;
    if (opt.sex) fem = *opt.sex == Sex::Female;
    g.sex = fem ? Sex::Female : Sex::Male;
    g.femininity = fem ? ri.range(0.82f, 0.97f) : ri.range(0.03f, 0.18f);

    Rng ra = root.fork(2);
    AgeGroup ag = opt.age ? *opt.age : AgeGroup(ra.weighted({0.26f, 0.40f, 0.22f, 0.12f}));
    if (arch == Archetype::Chibi) ag = AgeGroup::Young;
    static constexpr float kAgeLo[] = {16, 23, 40, 60}, kAgeHi[] = {22.9f, 39.9f, 59.9f, 82};
    g.age = ra.range(kAgeLo[size_t(ag)], kAgeHi[size_t(ag)]);
    g.ageGroup = groupOf(g.age);
    const bool young = ag == AgeGroup::Young, mature = ag == AgeGroup::Mature, elder = ag == AgeGroup::Elder;

    Rng rp = root.fork(3);
    const Persona per = opt.persona ? *opt.persona
                                    : Persona(rp.weighted({fem ? 1.2f : 1.6f, 1.0f, fem ? 1.2f : 0.4f, fem ? 1.1f : 0.7f,
                                                           1.0f, 1.0f, 1.0f, 0.8f}));
    g.persona = per;
    const size_t P = size_t(per);

    // ---- body: ordinary population, three independent axes ----
    Rng rb = root.fork(4);
    float frame = z(rb), fat = z(rb), mus = z(rb), tz = z(rb);
    fat += young ? -0.2f : mature ? 0.35f : elder ? 0.45f : 0.0f;
    mus += elder ? -0.6f : 0.0f;
    switch (per) {
        case Persona::Heroic: mus += 0.7f; fat -= 0.2f; break;
        case Persona::Rogue: mus += 0.3f; fat -= 0.3f; break;
        case Persona::Cute: mus -= 0.5f; break;
        case Persona::Sexy: mus += 0.2f; fat -= 0.1f; break;
        case Persona::Stern: mus += 0.2f; break;
        case Persona::Gentle: fat += 0.2f; break;
        default: break;
    }
    // Build: a broad body type biasing the axes (most people are average), so a crowd
    // mixes wiry, heavy, stocky, lanky and labour-strong bodies around the base.
    const int build = opt.archetype && arch != Archetype::Natural ? 0
                    : rb.weighted({4.0f, 1.2f, fem ? 0.9f : 1.2f, 0.8f, 0.7f, fem ? 0.4f : 1.0f});
    g.body.build = build;
    switch (build) {
        case 1: fat -= 1.0f; mus += 0.2f; break;                                             // lean, wiry
        case 2: fat += 1.6f + 0.5f * std::fabs(z(rb)); mus -= 0.2f; break;                    // heavy
        case 3: frame += 1.4f; tz -= 1.3f; fat += 0.4f; mus += 0.5f; break;                  // stocky
        case 4: tz += 1.3f; frame -= 0.8f; fat -= 0.6f; break;                                // lanky
        case 5: mus += 1.2f; frame += 0.8f; fat += 0.5f; break;                               // strong (labour, soldiers)
        default: break;
    }
    fat = clamp(fat, -2.0f, 3.4f);
    mus = clamp(mus, -2.2f, 2.6f);

    BodyShape& b = g.body;
    const float ageH = young ? 0.99f : elder ? 0.975f : 1.0f;
    b.height = (fem ? 1.65f + 0.065f * tz : 1.78f + 0.07f * tz) * ageH;
    // Stylised-heroic canon: a slightly large head (6.4-6.6 heads) keeps faces readable at
    // low resolution; limbs are full, torsos taper cleanly (shoulders > chest > waist).
    // Heroic-realistic canon (~7 heads): a grounded, adult read; faces stay legible via the planes.
    b.headsTall = (fem ? 6.85f : 7.05f) + 0.10f * tz - (young ? 0.15f : 0.0f);
    b.legRatio = clamp((fem ? 0.505f : 0.50f) + 0.007f * z(rb), 0.48f, 0.53f);
    b.neckLength = clamp((fem ? 0.31f : 0.27f) + 0.022f * z(rb) - 0.02f * std::max(0.0f, mus), 0.14f, 0.38f);
    b.neckThick = (fem ? 0.140f : 0.205f) + 0.008f * frame + 0.016f * std::max(0.0f, mus) + 0.008f * fat;
    if (fem) {
        // Hourglass: waist ~half the hip width, full thighs, arms that read at low res.
        b.shoulderWidth = 0.70f + 0.028f * frame + 0.024f * mus;
        b.chestWidth = 0.53f + 0.022f * frame + 0.024f * fat;
        b.waistWidth = 0.375f + (fat > 0 ? 0.068f : 0.042f) * fat - 0.004f * mus;
        b.hipWidth = 0.77f + 0.032f * frame + 0.045f * fat;
        b.chestDepth = 0.39f + 0.02f * fat;
        b.bust = clamp(0.6f + 0.2f * z(rb) + 0.12f * fat, 0.15f, 1.25f);
        b.butt = clamp(0.62f + 0.15f * z(rb) + 0.10f * fat, 0.25f, 1.25f);
        b.muscle = saturate(0.22f + 0.13f * mus);
        b.thigh = 0.39f + 0.024f * fat + 0.010f * mus;
        b.calf = 0.225f + 0.009f * fat + 0.006f * mus;
        b.arm = std::max(0.160f, 0.172f + 0.009f * fat + 0.008f * mus);  // never noodle arms
        b.handSize = 1.0f + 0.02f * frame;
        b.posture = 0.6f + 0.1f * z(rb) * 0.5f;
    } else {
        b.shoulderWidth = 0.96f + 0.035f * frame + 0.05f * mus;
        b.chestWidth = 0.66f + 0.026f * frame + 0.035f * mus + 0.035f * fat;
        b.waistWidth = 0.50f + (fat > 0 ? 0.090f : 0.055f) * fat - 0.006f * mus;
        b.hipWidth = 0.66f + 0.024f * frame + 0.04f * fat;
        b.chestDepth = 0.51f + 0.022f * mus + 0.022f * fat;
        b.bust = std::max(0.0f, 0.12f * (fat - 1.0f));  // only heavier men get chest softness
        b.butt = clamp(0.32f + 0.08f * z(rb) + 0.05f * mus, 0.12f, 0.65f);
        b.muscle = saturate(0.50f + 0.17f * mus);
        b.thigh = 0.42f + 0.016f * fat + 0.020f * mus;
        b.calf = 0.285f + 0.008f * fat + 0.012f * mus;
        b.arm = std::max(0.205f, 0.225f + 0.009f * fat + 0.018f * mus);
        b.handSize = 1.10f + 0.03f * frame;
        b.posture = 0.45f + 0.05f * z(rb);
    }
    b.belly = std::max(0.0f, 0.24f * (fat - 0.5f)) + (elder ? 0.05f : 0.0f);
    // Bones follow the frame and the muscle they carry (thick shafts and knuckly joints on the strong).
    b.boneMass = clamp(1.0f + 0.10f * frame + 0.07f * mus, 0.80f, 1.35f) * (fem ? 0.88f : 1.0f);
    b.footSize = b.handSize;
    b.headWidth = 1.0f + 0.045f * z(rb) + 0.02f * std::max(0.0f, fat);
    if (build == 3) { b.legRatio -= 0.02f; b.headsTall -= 0.25f; b.neckLength *= 0.8f; b.handSize *= 1.06f; }
    if (build == 4) { b.legRatio += 0.012f; b.headsTall += 0.25f; b.neckLength *= 1.1f; }
    b.armLength = 1.0f + 0.015f * z(rb);
    b.sag = elder ? rb.range(0.6f, 0.9f) : mature ? rb.range(0.2f, 0.4f) : rb.range(0.0f, 0.1f);
    b.hunch = elder ? rb.range(0.4f, 0.8f) : mature ? rb.range(0.0f, 0.15f) : 0.0f;
    if (per == Persona::Sexy && fem) {
        b.bust += 0.2f;
        b.butt += 0.2f;
        b.waistWidth -= 0.03f;
        b.hipWidth += 0.03f;
    }
    if (per == Persona::Cute) {
        b.headsTall -= 0.25f;
        b.height *= 0.97f;
    }
    if (per == Persona::Noble) {
        b.posture += 0.1f;
        b.headsTall += 0.1f;
    }

    // ---- stylised presets (opt-in) ----
    switch (arch) {
        case Archetype::Bombshell:
            b.bust = rb.range(0.95f, 1.2f); b.butt = rb.range(0.9f, 1.15f);
            b.waistWidth = rb.range(0.39f, 0.43f); b.hipWidth = rb.range(0.74f, 0.80f);
            b.headsTall = rb.range(6.2f, 6.5f); b.thigh = rb.range(0.36f, 0.39f); b.height = rb.range(1.64f, 1.76f);
            break;
        case Archetype::Dorito:
            b.shoulderWidth = rb.range(0.98f, 1.08f); b.chestWidth = rb.range(0.74f, 0.80f);
            b.waistWidth = rb.range(0.48f, 0.52f); b.hipWidth = rb.range(0.56f, 0.60f);
            b.muscle = rb.range(0.75f, 0.95f); b.arm = rb.range(0.155f, 0.17f); b.belly = 0;
            b.headsTall = rb.range(6.6f, 7.0f); b.height = rb.range(1.82f, 1.95f);
            break;
        case Archetype::Brute:
            b.height = rb.range(2.0f, 2.3f); b.headsTall = rb.range(6.9f, 7.4f); b.legRatio = rb.range(0.44f, 0.47f);
            b.shoulderWidth = rb.range(1.25f, 1.45f); b.chestWidth = rb.range(0.98f, 1.12f);
            b.waistWidth = rb.range(0.72f, 0.82f); b.hipWidth = rb.range(0.70f, 0.78f); b.chestDepth = rb.range(0.52f, 0.58f);
            b.muscle = rb.range(0.9f, 1.0f); b.thigh = rb.range(0.38f, 0.42f); b.calf = rb.range(0.28f, 0.32f);
            b.arm = rb.range(0.21f, 0.24f); b.neckThick = rb.range(0.25f, 0.29f); b.neckLength = rb.range(0.12f, 0.18f);
            b.handSize = b.footSize = rb.range(1.1f, 1.2f); b.belly = rb.range(0.0f, 0.25f);
            break;
        case Archetype::Amazon:
            b.height = rb.range(1.84f, 2.0f); b.headsTall = rb.range(6.5f, 6.9f); b.muscle = rb.range(0.6f, 0.85f);
            b.shoulderWidth = rb.range(0.82f, 0.90f); b.chestWidth = rb.range(0.62f, 0.67f); b.waistWidth = rb.range(0.44f, 0.48f);
            b.hipWidth = rb.range(0.72f, 0.78f); b.bust = rb.range(0.6f, 0.9f); b.butt = rb.range(0.75f, 1.0f);
            b.thigh = rb.range(0.33f, 0.36f); b.calf = rb.range(0.23f, 0.26f); b.arm = rb.range(0.13f, 0.145f);
            break;
        case Archetype::Slender:
            b.height *= 1.04f; b.headsTall = rb.range(6.6f, 7.0f); b.waistWidth -= 0.03f; b.hipWidth -= 0.04f;
            b.thigh = rb.range(0.26f, 0.28f); b.calf = rb.range(0.18f, 0.20f); b.arm = rb.range(0.10f, 0.11f);
            b.muscle = rb.range(0.05f, 0.2f); b.bust = std::min(b.bust, 0.4f); b.legRatio = 0.52f;
            break;
        case Archetype::Chibi:
            b.height = rb.range(0.95f, 1.2f); b.headsTall = rb.range(3.0f, 3.7f); b.legRatio = rb.range(0.33f, 0.37f);
            b.neckLength = 0.08f; b.neckThick = 0.12f; b.shoulderWidth = 0.6f; b.chestWidth = 0.5f; b.waistWidth = 0.47f;
            b.hipWidth = 0.55f; b.chestDepth = 0.32f; b.bust = 0.1f; b.butt = 0.25f; b.muscle = 0.05f;
            b.thigh = 0.22f; b.calf = 0.15f; b.arm = 0.1f; b.sag = b.hunch = 0;
            break;
        case Archetype::Monster:
            b.height = rb.range(1.95f, 2.4f); b.headsTall = rb.range(6.6f, 7.4f); b.legRatio = rb.range(0.42f, 0.48f);
            b.shoulderWidth = rb.range(1.15f, 1.5f); b.chestWidth = rb.range(0.88f, 1.12f); b.waistWidth = rb.range(0.62f, 0.82f);
            b.hipWidth = rb.range(0.66f, 0.82f); b.chestDepth = rb.range(0.46f, 0.60f); b.muscle = rb.range(0.7f, 1.0f);
            b.thigh = rb.range(0.36f, 0.44f); b.calf = rb.range(0.27f, 0.33f); b.arm = rb.range(0.18f, 0.24f);
            b.neckThick = rb.range(0.22f, 0.30f); b.neckLength = rb.range(0.1f, 0.2f);
            b.handSize = b.footSize = rb.range(1.1f, 1.25f);
            break;
        default: break;
    }
    // ---- anatomy: muscle groups follow the muscle axis with individual variation;
    //      definition follows leanness; bones show more on lean, bony and older bodies.
    {
        Anatomy& an = b.anat;
        const float m = b.muscle, lean = saturate(0.55f - 0.22f * fat + 0.12f * mus);
        auto grp = [&](float bias) { return clamp(m * (1.0f + 0.22f * z(rb)) + bias, 0.0f, 1.4f); };
        an.definition = saturate(lean + 0.25f * (m - 0.4f) + 0.06f * z(rb) - (elder ? 0.1f : 0.0f));
        an.neck = grp(fem ? -0.1f : 0.1f);
        an.traps = grp(fem ? -0.15f : 0.05f);
        an.deltoids = grp(0.05f);
        an.pecs = grp(fem ? -0.2f : 0.05f);
        an.lats = grp(fem ? -0.15f : 0.0f);
        an.abs = grp(0.0f);
        an.obliques = grp(0.0f);
        an.serratus = grp(-0.05f);
        an.biceps = grp(0.0f);
        an.triceps = grp(0.0f);
        an.forearms = grp(0.0f);
        an.glutes = clamp(0.4f + 0.5f * m + 0.12f * z(rb) + (fem ? 0.15f : 0.0f), 0.0f, 1.4f);
        an.quads = grp(fem ? 0.05f : 0.05f);
        an.adductors = clamp(0.35f + 0.25f * m + 0.25f * std::max(0.0f, fat) + (fem ? 0.15f : 0.0f), 0.0f, 1.3f);
        an.hamstrings = grp(0.0f);
        an.calves = grp(0.05f);
        const float bony = saturate(0.5f + 0.15f * frame - 0.22f * fat + 0.1f * z(rb) + (elder ? 0.2f : 0.0f));
        an.clavicles = saturate(bony + (fem ? 0.1f : 0.0f));
        an.ribcage = saturate(bony - 0.1f);
        an.scapulae = saturate(bony);
        an.pelvis = saturate(bony + (fem ? 0.12f : -0.05f));
        an.knees = saturate(bony + 0.1f);
        an.elbows = bony;
        an.wrists = bony;
        if (per == Persona::Heroic || per == Persona::Rogue) an.definition = saturate(an.definition + 0.1f);
    }

    const float e = opt.exaggeration;
    // Push away from the population mean of this sex (not a fixed canon, which made
    // e = 2 explode hips into skirts), and keep the result inside what the torso and
    // thigh junction can build.
    auto ex = [e](float v, float canon) { return canon + (v - canon) * (1.0f + 1.0f * e); };
    const float fm = g.femininity;
    b.shoulderWidth = clamp(ex(b.shoulderWidth, lerp(0.98f, 0.71f, fm)), 0.55f, 1.45f);
    b.waistWidth = clamp(ex(b.waistWidth, lerp(0.53f, 0.39f, fm)), 0.30f, 0.95f);
    b.hipWidth = clamp(ex(b.hipWidth, lerp(0.67f, 0.78f, fm)), 0.50f, 1.0f);
    // Exaggeration grows soft volumes only up to what the surface builds cleanly
    // (glutes past ~1 flare into a skirt at the hip line).
    b.bust = std::min(b.bust * (1.0f + 0.5f * e), std::max(b.bust, 1.4f));
    b.butt = std::min(b.butt * (1.0f + 0.4f * e), std::max(b.butt, 0.95f));

    // ---- fantasy extensions ----
    Rng rx = root.fork(5);
    const bool monster = arch == Archetype::Monster;
    const bool fantasy = monster || rx.chance(per == Persona::Villainous ? 0.12f : 0.05f);
    b.armPairs = monster && rx.chance(0.5f) ? 2 : 1;
    b.tail = monster ? rx.chance(0.6f) : (fantasy && rx.chance(0.2f));
    b.tailLength = rx.range(0.7f, 1.2f);
    if (b.tail) b.tailType = rx.weighted({1.0f, monster ? 2.0f : 0.8f, 1.2f, 0.6f, monster ? 1.2f : 0.2f, 0.6f});
    // Dorsal spines and plates: monsters often, other fantasy blood sometimes.
    if (monster ? rx.chance(0.55f) : (fantasy && rx.chance(0.18f))) {
        b.spines = rx.rangeInt(5, 9);
        b.spineKind = rx.weighted({1.5f, 1.0f, 0.8f});
        b.spineSize = rx.range(0.8f, 1.3f) * (monster ? 1.2f : 1.0f);
    }
    b.horns = (monster ? rx.chance(0.7f) : (fantasy && rx.chance(0.35f))) ? 2 : 0;
    b.hornSize = rx.range(0.7f, 1.3f) * (monster ? 1.3f : 1.0f);
    b.ears = (fantasy ? rx.chance(0.6f) : rx.chance(0.10f)) ? EarStyle::Elf : EarStyle::Human;
    if (b.ears == EarStyle::Elf) b.ear = {0.40f, 0.075f, 0.40f, 0.40f, 0.0f};

    // ---- face: persona + age + sex ----
    // Identity lives in the planes of the head (nose, brow, cheekbones, jaw, chin),
    // the eye family and the skin's history (scars, stubble, weather, dirt), so all
    // of them vary widely; persona, age and sex only bias the draws.
    Rng rf = root.fork(6);
    FaceStyle& f = g.face;
    const bool villain = per == Persona::Villainous, cute = per == Persona::Cute, stern = per == Persona::Stern;
    const bool rogue = per == Persona::Rogue, noble = per == Persona::Noble, gentle = per == Persona::Gentle;
    {   // Eye families: 0 standard (reference), 1 round, 2 narrow, 3 sharp, 4 droopy, 5 hooded, 6 small.
        std::array<float, 7> ew{3.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.7f, 0.6f};
        if (!fem) { ew[1] *= 0.35f; ew[2] += 1.4f; ew[5] += 1.0f; ew[6] += 1.2f; }
        if (young) { ew[1] += 1.0f; ew[6] *= 0.5f; }
        if (mature) { ew[5] += 0.6f; ew[6] += 0.6f; }
        if (elder) { ew[1] *= 0.2f; ew[4] += 1.5f; ew[5] += 1.2f; ew[6] += 1.6f; }
        if (cute) ew[1] += 2.5f;
        if (villain) { ew[3] += 2.0f; ew[2] += 1.0f; ew[1] *= 0.2f; }
        if (noble) ew[3] += 1.0f;
        if (gentle) { ew[4] += 1.2f; ew[1] += 0.5f; }
        if (stern) { ew[2] += 1.5f; ew[5] += 1.0f; }
        if (rogue) { ew[3] += 1.0f; ew[2] += 1.0f; }
        if (per == Persona::Heroic) ew[0] += 1.0f;
        f.eyeShape = rf.weighted(std::span<const float>(ew));
    }
    f.eyeSize = 1.0f + 0.09f * z(rf) + (cute ? 0.10f : 0.0f) - (villain ? 0.06f : 0.0f) + (young ? 0.05f : 0.0f) -
                (elder ? 0.05f : 0.0f) - (fem ? 0.0f : 0.05f);
    f.eyeSpacing = 1.0f + 0.04f * z(rf);
    f.eyeTilt = 0.07f * z(rf) + (villain ? 0.06f : 0.0f) - (gentle ? 0.05f : 0.0f);
    f.restLid = clamp(kRestLid[P] + (elder ? 0.08f : 0.0f) + 0.04f * z(rf), 0.0f, 0.34f);
    f.lashWeight = saturate(fem ? 0.60f + 0.15f * z(rf) + (per == Persona::Sexy ? 0.2f : 0.0f) : 0.32f + 0.12f * z(rf));
    f.pupil = fantasy && villain && rf.chance(0.5f) ? PupilStyle::Slit : PupilStyle::Round;
    f.browShape = rf.weighted(std::span<const float>(kBrowShapeW[P]));
    f.browThick = saturate((fem ? 0.35f : 0.66f) + 0.24f * z(rf) + (elder && !fem ? 0.18f : 0.0f));
    f.browTilt = kBrowTilt[P] + 0.10f * z(rf);
    f.browHeight = (noble ? 0.005f : stern ? -0.006f : cute ? 0.006f : 0.0f) + 0.004f * z(rf);
    f.browLength = 1.0f + 0.12f * z(rf);
    f.irisSize = 1.0f + 0.06f * z(rf) + (cute ? 0.06f : 0.0f) - (villain ? 0.06f : 0.0f);
    f.mouthWidth = 1.0f + 0.10f * z(rf) + (fem ? 0.0f : 0.06f);
    f.lipFullness = saturate((fem ? 0.55f : 0.28f) + (fem ? 0.17f : 0.12f) * z(rf) + (per == Persona::Sexy ? 0.2f : 0.0f) -
                             (elder ? 0.15f : 0.0f));
    f.mouthRest = kMouthRest[P] + 0.002f * z(rf);
    f.mouthAsym = kMouthAsym[P] * (rf.chance(0.5f) ? 1.0f : -1.0f);
    // Nose: a preset family, then continuous variation around it.
    f.noseShape = rf.weighted({fem ? 2.0f : 0.6f, 3.0f, noble || villain ? 2.2f : 1.0f, fem ? 0.6f : 1.6f});
    {
        float L = 1.0f, Pj = 1.0f, Wd = 1.0f, hump = 0.0f, tip = 0.0f;
        switch (f.noseShape) {
            case 0: L = 0.80f; Pj = 0.78f; Wd = 0.90f; tip = 0.6f; break;  // button
            case 2: L = 1.14f; Pj = 1.15f; Wd = 0.95f; hump = 0.65f; tip = -0.35f; break;  // aquiline
            case 3: L = 1.00f; Pj = 0.92f; Wd = 1.38f; tip = 0.1f; break;  // broad
            default: break;
        }
        f.noseLength = clamp(L + 0.10f * z(rf) + (fem ? -0.07f : 0.05f) + (elder ? 0.07f : 0.0f), 0.65f, 1.45f);
        f.noseProj = clamp(Pj + 0.14f * z(rf) + (fem ? -0.32f : 0.10f) - (cute ? 0.15f : 0.0f), 0.45f, 1.6f);
        f.noseWidth = clamp(Wd + 0.12f * z(rf) + (fem ? -0.18f : 0.08f) + 0.05f * std::max(0.0f, fat), 0.65f, 1.7f);
        f.noseHump = saturate(hump + 0.25f * z(rf) + ((rogue || per == Persona::Heroic) && !fem && rf.chance(0.3f) ? 0.5f : 0.0f));
        f.noseTip = clamp(tip + 0.3f * z(rf) - (elder ? 0.25f : 0.0f), -1.0f, 1.0f);
    }
    f.noseSize = f.noseProj;
    f.noseBridge = saturate(0.5f + 0.2f * z(rf) + (fem ? 0.0f : 0.15f) + (noble || villain ? 0.15f : 0.0f) - (cute ? 0.25f : 0.0f));
    // Skull.
    f.jaw = clamp(fem ? -0.10f + 0.32f * z(rf) - (villain ? 0.1f : 0.0f)
                      : 0.62f + 0.32f * z(rf) + (per == Persona::Heroic ? 0.2f : 0.0f) - (cute ? 0.3f : 0.0f),
                  -1.0f, 1.0f);
    f.chinWidth = clamp((fem ? -0.25f : 0.35f) + 0.38f * z(rf) + (per == Persona::Heroic ? 0.2f : 0.0f) - (cute ? 0.2f : 0.0f), -1.0f, 1.0f);
    f.chin = clamp(0.30f * z(rf) + (villain ? 0.35f : 0.0f) + (per == Persona::Heroic ? 0.2f : 0.0f) - (cute ? 0.3f : 0.0f), -1.0f, 1.0f);
    f.cheek = clamp(0.45f * z(rf) + (villain ? 0.45f : 0.0f) + (noble ? 0.25f : 0.0f) + (per == Persona::Sexy ? 0.2f : 0.0f) -
                        (cute ? 0.2f : 0.0f),
                    -1.0f, 1.0f);
    f.browRidge = saturate((fem ? 0.18f + 0.12f * z(rf) : 0.62f + 0.22f * z(rf)) + (villain ? 0.15f : 0.0f) + (stern ? 0.1f : 0.0f));
    f.roundness = saturate((young ? 0.35f : 0.0f) + (cute ? 0.4f : 0.0f) + 0.18f * fat);
    f.crown = clamp(0.45f * z(rf), -1.0f, 1.0f);
    f.faceWidth = clamp(0.42f * z(rf) + 0.25f * std::max(0.0f, fat) - (noble ? 0.15f : 0.0f), -1.0f, 1.0f);
    f.cheekHollow = saturate(0.30f * z(rf) + 0.20f - 0.25f * fat + (elder ? 0.35f : mature ? 0.12f : 0.0f) + (villain ? 0.2f : 0.0f) -
                             (young ? 0.15f : 0.0f));
    f.jowl = saturate(0.35f * fat - 0.05f + (elder ? 0.35f : mature ? 0.15f : 0.0f) + 0.12f * z(rf));
    f.eyeDepth = saturate((fem ? 0.22f : 0.45f) + 0.18f * z(rf) + (stern ? 0.15f : 0.0f) + (elder ? 0.15f : 0.0f) +
                          (villain ? 0.2f : 0.0f) - (cute ? 0.2f : 0.0f));
    f.eyeBags = saturate((g.age - 34.0f) / 45.0f + 0.15f * z(rf) + (rogue ? 0.1f : 0.0f));
    f.earSize = 1.0f + 0.12f * z(rf) + (elder ? 0.08f : 0.0f);
    f.ageLines = saturate((g.age - 34.0f) / 34.0f);
    // Skin history.
    f.freckles = rf.chance(young ? 0.18f : 0.10f);
    f.ruddy = saturate(0.18f * z(rf) + (fem ? 0.05f : 0.15f) + (elder ? 0.15f : 0.0f));
    f.grime = rf.chance(rogue ? 0.45f : per == Persona::Heroic || stern ? 0.25f : noble ? 0.03f : 0.15f) ? rf.range(0.35f, 0.9f) : 0.0f;
    {
        const float pScar = rogue ? 0.40f : per == Persona::Heroic ? 0.22f : villain ? 0.30f : stern ? 0.18f : fem ? 0.05f : 0.10f;
        if (rf.chance(pScar)) f.scarType = 1 + rf.weighted({1.5f, 1.0f, 0.8f, 1.0f, villain || rogue ? 0.6f : 0.25f});
        f.scar = f.scarType != 0;
        const float pTat = (fantasy ? 0.25f : 0.06f) + (villain || rogue ? 0.06f : 0.0f);
        if (rf.chance(pTat)) f.tattoo = 1 + rf.weighted({1.0f, 1.0f, 0.8f});
        static constexpr Vec3 kInk[] = {{0.16f, 0.22f, 0.34f}, {0.12f, 0.12f, 0.14f}, {0.18f, 0.28f, 0.20f}, {0.45f, 0.14f, 0.10f}};
        f.tattooColor = kInk[rf.rangeInt(0, 3)];
    }
    f.fang = fantasy && villain && rf.chance(0.3f);
    f.blush = cute && rf.chance(0.3f);
    {
        const float pm = rogue || villain ? 0.22f : per == Persona::Heroic ? 0.12f : 0.06f;
        if (rf.chance(pm)) f.marking = rf.weighted({1.5f, fantasy ? 1.5f : 0.5f, 1.0f, 0.4f, fem ? 0.3f : 0.8f, 0.4f});
        if (f.marking == 0 && rf.chance(0.12f)) f.marking = 1;  // moles are common
        static constexpr Vec3 kPaint[] = {{0.55f, 0.10f, 0.10f}, {0.12f, 0.14f, 0.18f}, {0.85f, 0.85f, 0.80f}, {0.20f, 0.30f, 0.55f}, {0.70f, 0.50f, 0.15f}};
        f.markColor = kPaint[rf.rangeInt(0, 4)];
    }

    // ---- hair: what people wear in a grim medieval world ----
    Rng rh = root.fork(7);
    std::array<float, size_t(HairStyle::Count)> w{};
    using H = HairStyle;
    auto W = [&](H s) -> float& { return w[size_t(s)]; };
    if (fem) {
        W(H::ShortCrop) = 0.2f; W(H::SidePart) = 0.2f; W(H::Messy) = 0.5f; W(H::Pixie) = 0.5f; W(H::Bob) = 1.0f;
        W(H::Shoulder) = 2.2f; W(H::LongStraight) = 2.5f; W(H::LongWavy) = 2.5f; W(H::Ponytail) = 1.6f; W(H::HighPonytail) = 0.6f;
        W(H::Braid) = 2.2f; W(H::Bun) = 1.6f; W(H::HalfUp) = 1.6f; W(H::Curly) = 0.4f; W(H::Afro) = 0.4f; W(H::Wolf) = 0.8f;
        W(H::Hime) = 0.3f; W(H::CrownBraid) = 1.6f; W(H::TwinBraids) = 1.5f; W(H::SideBraid) = 1.6f; W(H::LowPonytail) = 1.6f;
        W(H::MessyBun) = 1.4f; W(H::LongCurly) = 1.3f; W(H::VeryLong) = 0.9f; W(H::AsymBob) = 0.3f; W(H::Locs) = 0.4f;
        W(H::WarriorBraids) = 1.0f; W(H::Undercut) = 0.15f; W(H::BowlCut) = 0.2f;
        if (elder) { W(H::Bun) += 2.5f; W(H::CrownBraid) += 1.0f; W(H::HighPonytail) *= 0.2f; W(H::Pixie) *= 0.5f;
                     W(H::LongWavy) *= 0.5f; W(H::VeryLong) *= 0.3f; W(H::Wolf) *= 0.3f; }
    } else {
        W(H::Bald) = elder ? 2.5f : mature ? 0.8f : 0.2f; W(H::Buzz) = 1.2f; W(H::ShortCrop) = 3.0f; W(H::SidePart) = 1.2f;
        W(H::SlickedBack) = 1.2f; W(H::Messy) = 2.5f; W(H::Undercut) = 0.7f; W(H::Shoulder) = 1.4f; W(H::LongStraight) = 0.8f;
        W(H::LongWavy) = 0.6f; W(H::Ponytail) = 1.0f; W(H::Braid) = 0.4f; W(H::HalfUp) = 0.5f; W(H::ManBun) = 0.6f;
        W(H::CrewCut) = 1.5f; W(H::Tonsure) = elder || mature ? 0.8f : 0.3f; W(H::WarriorBraids) = 1.4f; W(H::Curly) = 1.0f;
        W(H::Afro) = 0.4f; W(H::Mullet) = 0.4f; W(H::Topknot) = 0.6f; W(H::Locs) = 0.5f; W(H::Wolf) = 1.0f;
        W(H::LowPonytail) = 1.0f; W(H::SideBraid) = 0.2f; W(H::BowlCut) = young ? 1.0f : 0.5f; W(H::TwinBraids) = 0.3f;
        if (elder) { W(H::Undercut) *= 0.2f; W(H::ManBun) *= 0.3f; W(H::Wolf) *= 0.3f; W(H::BowlCut) *= 0.3f; }
    }
    switch (per) {
        case Persona::Rogue: W(H::Messy) += 2; W(H::Wolf) += 1; W(H::Undercut) += 0.6f; W(H::Locs) += 0.5f; W(H::WarriorBraids) += 0.6f; break;
        case Persona::Noble: (fem ? W(H::LongStraight) : W(H::SlickedBack)) += 1.5f; W(H::Bun) += fem ? 1.0f : 0.0f;
                             W(H::CrownBraid) += fem ? 1.5f : 0.0f; W(H::VeryLong) += fem ? 0.6f : 0.0f; W(H::Topknot) += fem ? 0.0f : 0.4f; break;
        case Persona::Cute: W(H::Bob) += fem ? 0.8f : 0.0f; W(H::TwinBraids) += fem ? 1.2f : 0.0f; W(H::BowlCut) += 0.6f; W(H::Curly) += 0.5f; break;
        case Persona::Villainous: W(H::LongStraight) += 1; W(H::SlickedBack) += 1.5f; W(H::Bald) += fem ? 0.0f : 0.6f; break;
        case Persona::Heroic: W(H::ShortCrop) += fem ? 0.0f : 1.0f; W(H::WarriorBraids) += 1.0f; W(H::Ponytail) += 1; W(H::Braid) += 0.6f; break;
        case Persona::Sexy: W(H::LongWavy) += fem ? 1.5f : 0.3f; W(H::HalfUp) += 1; break;
        case Persona::Stern: W(H::Tonsure) += fem ? 0.0f : 0.6f; W(H::CrewCut) += 0.6f; W(H::Bun) += fem ? 0.8f : 0.0f; break;
        case Persona::Gentle: W(H::Braid) += 0.6f; W(H::BowlCut) += 0.3f; break;
        default: break;
    }
    if (arch == Archetype::Brute || arch == Archetype::Monster) { W(H::Bald) += 1.5f; W(H::Buzz) += 1; }
    g.hair = opt.hair ? *opt.hair : HairStyle(rh.weighted(std::span<const float>(w)));
    const bool shortHair = isShortHair(g.hair) && g.hair != HairStyle::Pixie;
    g.fringe = Fringe(rh.weighted({shortHair ? 3.0f : 1.0f, shortHair ? 0.3f : 2.0f, 2.0f, shortHair ? 0.4f : 1.0f}));
    if (g.hair == HairStyle::SlickedBack || g.hair == HairStyle::Bald || g.hair == HairStyle::Buzz || g.hair == HairStyle::CrewCut ||
        g.hair == HairStyle::Tonsure || g.hair == HairStyle::WarriorBraids || g.hair == HairStyle::Afro || g.hair == HairStyle::Topknot ||
        g.hair == HairStyle::Locs || g.hair == HairStyle::CrownBraid)
        g.fringe = Fringe::None;
    if (g.hair == HairStyle::Hime || g.hair == HairStyle::BowlCut) g.fringe = Fringe::Straight;
    g.hairVolume = clamp(1.0f + 0.1f * z(rh), 0.8f, 1.25f);
    g.hairLength = clamp(1.0f + 0.12f * z(rh), 0.75f, 1.3f);
    g.hairWave = g.hair == HairStyle::LongWavy ? rh.range(0.6f, 1.0f) : g.hair == HairStyle::LongCurly ? 1.0f
               : g.hair == HairStyle::Hime ? 0.0f : rh.range(0.0f, 0.2f);
    g.partSide = rh.chance(0.5f) ? 1.0f : -1.0f;
    if (!fem && g.age >= 18) {
        // None, Stubble, Mustache, Goatee, ShortBeard, FullBeard, LongBeard, BraidedBeard, MuttonChops
        std::array<float, size_t(FacialHair::Count)> bw{young ? 6.0f : 2.2f, young ? 2.5f : 2.0f, 0.5f, 0.8f, 1.6f, 1.6f, 0.4f, 0.35f, 0.4f};
        if (elder) { bw[5] += 1.0f; bw[6] += 1.8f; bw[1] *= 0.5f; }
        if (mature) { bw[5] += 0.6f; bw[6] += 0.5f; }
        if (per == Persona::Heroic || rogue) { bw[1] += 1.0f; bw[4] += 0.8f; bw[7] += 0.4f; }
        if (stern) { bw[5] += 1.0f; bw[6] += 0.6f; }
        if (noble) { bw[3] += 0.8f; bw[2] += 0.4f; bw[0] += 1.0f; }
        if (villain) { bw[3] += 1.0f; bw[2] += 0.5f; }
        if (arch == Archetype::Brute) { bw[7] += 1.0f; bw[5] += 1.0f; }
        if (young) { bw[6] = 0.0f; bw[7] *= 0.3f; }
        g.facialHair = FacialHair(rh.weighted(std::span<const float>(bw)));
    }
    // Beard shadow: shaved men show it, bearded ones carry it around the beard's edge.
    if (!fem && g.age >= 18)
        f.stubble = g.facialHair == FacialHair::Stubble ? rh.range(0.6f, 1.0f)
                  : g.facialHair == FacialHair::None ? (rh.chance(0.5f) ? rh.range(0.15f, 0.5f) : 0.0f)
                                                    : rh.range(0.35f, 0.7f);

    // ---- palette ----
    Rng rc = root.fork(8);
    Palette& pal = g.palette;
    pal.skin = pickSkin(rc, fantasy);
    pal.hair = pickHair(rc, per);
    const float gray = std::pow(saturate((g.age - 42.0f) / 30.0f), 1.2f) * rc.range(0.6f, 1.0f);
    pal.hair = lerp(pal.hair, Vec3{1, 1, 1} * rc.range(0.62f, 0.86f), gray);
    pal.hairTip = pal.hair * 0.88f;
    pal.brow = luminance(pal.hair) > 0.55f ? lerp(pal.hair * 0.6f, Vec3{0.30f, 0.24f, 0.20f}, 0.5f) : pal.hair * 0.8f;
    pal.iris = pickIris(rc, fantasy);
    pal.lips = lerp(pal.skin * Vec3{0.90f, 0.62f, 0.60f}, Vec3{0.60f, 0.25f, 0.28f},
                    (fem ? 0.35f : 0.15f) + (per == Persona::Sexy && fem ? 0.25f : 0.0f));
    // Muted mid tones: never near-black, which would swallow the body's form.
    static constexpr Vec3 kUnder[] = {{0.80f, 0.76f, 0.68f}, {0.52f, 0.52f, 0.54f}, {0.36f, 0.36f, 0.39f}, {0.47f, 0.38f, 0.30f}};
    pal.underwear = kUnder[rc.weighted({1.0f, 1.6f, 1.2f, 1.0f})];
    pal.horn = fantasy && rc.chance(0.5f) ? hsv(rc.uniform(), 0.3f, 0.22f) : Vec3{0.88f, 0.84f, 0.74f};

    // ---- species: reshape the sampled person into one of the world's peoples ----
    Species sp = opt.species.value_or(Species::Human);
    if (!opt.species && opt.mixedSpecies)
        sp = Species(root.fork(9).weighted({5.0f, 1.2f, 0.8f, 1.2f, 0.8f, 0.8f, 0.9f, 0.9f, 0.7f, 0.8f, 0.7f, 0.7f, 0.5f, 0.5f, 0.6f}));
    if (sp != Species::Human) applySpecies(g, sp, root.fork(10), opt);
    if (opt.armPairs) g.body.armPairs = std::clamp(*opt.armPairs, 1, 2);
    // Exaggerated proportions, once the people has rescaled them: keep the hips within
    // what the shoulders carry and the waist below both, or short peoples turn into
    // blocks with a flared skirt.
    if (opt.exaggeration > 0.0f) {
        BodyShape& eb = g.body;
        eb.hipWidth = std::min(eb.hipWidth, 1.30f * eb.shoulderWidth);
        eb.waistWidth = std::min(eb.waistWidth, 0.90f * std::max(eb.shoulderWidth, eb.hipWidth));
    }
    return g;
}

}  // namespace cg
