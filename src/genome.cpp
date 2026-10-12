#include "chargen/genome.hpp"

#include <array>

#include "chargen/rng.hpp"

namespace cg {

namespace {

constexpr std::string_view kArchNames[] = {"natural", "bombshell", "dorito", "brute", "amazon", "slender", "chibi", "monster"};
constexpr std::string_view kPersonaNames[] = {"heroic", "villainous", "cute", "sexy", "stern", "gentle", "rogue", "noble"};
constexpr std::string_view kAgeNames[] = {"young", "adult", "mature", "elder"};
constexpr std::string_view kHairNames[] = {"bald", "buzz", "shortcrop", "sidepart", "slickedback", "messy", "undercut", "pixie", "bob",
                                           "shoulder", "longstraight", "longwavy", "ponytail", "highponytail", "braid", "bun", "halfup", "manbun"};
constexpr std::string_view kBeardNames[] = {"none", "mustache", "goatee", "shortbeard", "fullbeard"};

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
constexpr std::array<std::array<float, 5>, 8> kEyeShapeW = {{{3, 1, 1, 1, 0.5f},
                                                              {1, 0.2f, 3, 2.5f, 0.3f},
                                                              {1, 3, 0.2f, 0.6f, 1.5f},
                                                              {2.5f, 0.5f, 1.5f, 2.5f, 0.5f},
                                                              {2, 0.3f, 3, 1, 0.5f},
                                                              {2, 1.5f, 0.5f, 0.5f, 2.5f},
                                                              {2.5f, 0.5f, 1, 2, 0.5f},
                                                              {3, 0.6f, 1.5f, 1.2f, 0.6f}}};
constexpr std::array<std::array<float, 3>, 8> kBrowShapeW = {{{2, 1, 1.5f},
                                                               {0.5f, 0.5f, 3},
                                                               {1, 3, 0.2f},
                                                               {1, 2.5f, 1.5f},
                                                               {3, 0.3f, 1.5f},
                                                               {1.5f, 2.5f, 0.2f},
                                                               {1.5f, 1, 2},
                                                               {1.5f, 2, 1}}};

Vec3 pickSkin(Rng& r, bool fantasy) {
    static constexpr Vec3 human[] = {{0.99f, 0.88f, 0.82f}, {0.97f, 0.82f, 0.71f}, {0.91f, 0.74f, 0.60f},
                                     {0.80f, 0.60f, 0.45f}, {0.62f, 0.43f, 0.31f}, {0.42f, 0.29f, 0.22f}};
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

std::string_view toString(Sex s) { return s == Sex::Female ? "female" : "male"; }
std::string_view toString(AgeGroup a) { return a < AgeGroup::Count ? kAgeNames[size_t(a)] : "?"; }
std::string_view toString(Persona p) { return p < Persona::Count ? kPersonaNames[size_t(p)] : "?"; }
std::string_view toString(Archetype a) { return a < Archetype::Count ? kArchNames[size_t(a)] : "?"; }
std::string_view toString(HairStyle h) { return h < HairStyle::Count ? kHairNames[size_t(h)] : "?"; }
std::string_view toString(FacialHair f) { return f < FacialHair::Count ? kBeardNames[size_t(f)] : "?"; }
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
    fat = clamp(fat, -2.0f, 2.6f);
    mus = clamp(mus, -2.2f, 2.5f);

    BodyShape& b = g.body;
    const float ageH = young ? 0.99f : elder ? 0.975f : 1.0f;
    b.height = (fem ? 1.65f + 0.065f * tz : 1.78f + 0.07f * tz) * ageH;
    // Stylised-heroic canon: a slightly large head (6.4-6.6 heads) keeps faces readable at
    // low resolution; limbs are full, torsos taper cleanly (shoulders > chest > waist).
    b.headsTall = (fem ? 6.35f : 6.5f) + 0.10f * tz - (young ? 0.12f : 0.0f);
    b.legRatio = clamp((fem ? 0.505f : 0.50f) + 0.007f * z(rb), 0.48f, 0.53f);
    b.neckLength = clamp((fem ? 0.31f : 0.27f) + 0.022f * z(rb) - 0.02f * std::max(0.0f, mus), 0.14f, 0.38f);
    b.neckThick = (fem ? 0.125f : 0.172f) + 0.007f * frame + 0.014f * std::max(0.0f, mus) + 0.007f * fat;
    if (fem) {
        // Hourglass: waist ~half the hip width, full thighs, arms that read at low res.
        b.shoulderWidth = 0.61f + 0.025f * frame + 0.02f * mus;
        b.chestWidth = 0.46f + 0.02f * frame + 0.02f * fat;
        b.waistWidth = 0.300f + 0.045f * fat - 0.004f * mus;
        b.hipWidth = 0.70f + 0.03f * frame + 0.04f * fat;
        b.chestDepth = 0.34f + 0.018f * fat;
        b.bust = clamp(0.6f + 0.2f * z(rb) + 0.12f * fat, 0.15f, 1.25f);
        b.butt = clamp(0.62f + 0.15f * z(rb) + 0.10f * fat, 0.25f, 1.25f);
        b.muscle = saturate(0.22f + 0.13f * mus);
        b.thigh = 0.34f + 0.022f * fat + 0.008f * mus;
        b.calf = 0.19f + 0.008f * fat + 0.005f * mus;
        b.arm = std::max(0.115f, 0.127f + 0.007f * fat + 0.005f * mus);  // never noodle arms
        b.handSize = 0.94f + 0.02f * frame;
        b.posture = 0.6f + 0.1f * z(rb) * 0.5f;
    } else {
        b.shoulderWidth = 0.80f + 0.03f * frame + 0.04f * mus;
        b.chestWidth = 0.53f + 0.022f * frame + 0.03f * mus + 0.02f * fat;
        b.waistWidth = 0.395f + 0.055f * fat - 0.006f * mus;
        b.hipWidth = 0.56f + 0.02f * frame + 0.025f * fat;
        b.chestDepth = 0.42f + 0.018f * mus + 0.018f * fat;
        b.bust = std::max(0.0f, 0.12f * (fat - 1.0f));  // only heavier men get chest softness
        b.butt = clamp(0.32f + 0.08f * z(rb) + 0.05f * mus, 0.12f, 0.65f);
        b.muscle = saturate(0.50f + 0.17f * mus);
        b.thigh = 0.34f + 0.014f * fat + 0.016f * mus;
        b.calf = 0.215f + 0.007f * fat + 0.008f * mus;
        b.arm = std::max(0.145f, 0.160f + 0.007f * fat + 0.012f * mus);
        b.handSize = 1.0f + 0.03f * frame;
        b.posture = 0.45f + 0.05f * z(rb);
    }
    b.belly = std::max(0.0f, 0.18f * (fat - 0.6f)) + (elder ? 0.05f : 0.0f);
    b.footSize = b.handSize;
    b.headWidth = 1.0f + 0.025f * z(rb);
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
    auto ex = [e](float v, float canon) { return canon + (v - canon) * (1.0f + 1.4f * e); };
    b.shoulderWidth = ex(b.shoulderWidth, 0.75f);
    b.waistWidth = std::max(0.30f, ex(b.waistWidth, 0.43f));
    b.hipWidth = ex(b.hipWidth, 0.62f);
    b.bust *= 1.0f + 0.5f * e;
    b.butt *= 1.0f + 0.4f * e;

    // ---- fantasy extensions ----
    Rng rx = root.fork(5);
    const bool monster = arch == Archetype::Monster;
    const bool fantasy = monster || rx.chance(per == Persona::Villainous ? 0.12f : 0.05f);
    b.armPairs = monster && rx.chance(0.5f) ? 2 : 1;
    b.tail = monster ? rx.chance(0.6f) : (fantasy && rx.chance(0.2f));
    b.tailLength = rx.range(0.7f, 1.2f);
    b.horns = (monster ? rx.chance(0.7f) : (fantasy && rx.chance(0.35f))) ? 2 : 0;
    b.hornSize = rx.range(0.7f, 1.3f) * (monster ? 1.3f : 1.0f);
    b.ears = (fantasy ? rx.chance(0.6f) : rx.chance(0.10f)) ? EarStyle::Elf : EarStyle::Human;

    // ---- face: persona + age + sex ----
    Rng rf = root.fork(6);
    FaceStyle& f = g.face;
    f.eyeShape = rf.weighted(std::span<const float>(kEyeShapeW[P]));
    f.eyeSize = 1.0f + 0.06f * z(rf) + (per == Persona::Cute ? 0.12f : 0.0f) - (per == Persona::Villainous ? 0.08f : 0.0f) +
                (young ? 0.06f : 0.0f) - (elder ? 0.06f : 0.0f) - (fem ? 0.0f : 0.05f);
    f.eyeSpacing = 1.0f + 0.04f * z(rf);
    f.eyeTilt = 0.05f * z(rf) + (per == Persona::Villainous ? 0.06f : 0.0f) - (per == Persona::Gentle ? 0.05f : 0.0f);
    f.restLid = clamp(kRestLid[P] + (elder ? 0.08f : 0.0f) + 0.03f * z(rf), 0.0f, 0.32f);
    f.lashWeight = saturate(fem ? 0.55f + 0.15f * z(rf) + (per == Persona::Sexy ? 0.2f : 0.0f) : 0.25f + 0.1f * z(rf));
    f.pupil = fantasy && per == Persona::Villainous && rf.chance(0.5f) ? PupilStyle::Slit : PupilStyle::Round;
    f.browShape = rf.weighted(std::span<const float>(kBrowShapeW[P]));
    f.browThick = saturate((fem ? 0.40f : 0.65f) + 0.12f * z(rf) + (elder && !fem ? 0.1f : 0.0f));
    f.browTilt = kBrowTilt[P] + 0.05f * z(rf);
    f.browHeight = per == Persona::Noble ? 0.005f : per == Persona::Stern ? -0.006f : per == Persona::Cute ? 0.006f : 0.0f;
    f.mouthWidth = 1.0f + 0.06f * z(rf) + (fem ? 0.0f : 0.05f);
    f.lipFullness = saturate((fem ? 0.55f : 0.30f) + (fem ? 0.15f : 0.10f) * z(rf) + (per == Persona::Sexy ? 0.2f : 0.0f) -
                             (elder ? 0.15f : 0.0f));
    f.mouthRest = kMouthRest[P];
    f.mouthAsym = kMouthAsym[P] * (rf.chance(0.5f) ? 1.0f : -1.0f);
    f.noseSize = (fem ? 0.92f : 1.10f) + 0.08f * z(rf) - (per == Persona::Cute ? 0.1f : 0.0f) + (elder ? 0.06f : 0.0f);
    f.noseBridge = saturate(0.5f + 0.2f * z(rf) + (fem ? 0.0f : 0.15f) + (per == Persona::Noble || per == Persona::Villainous ? 0.15f : 0.0f) -
                            (per == Persona::Cute ? 0.25f : 0.0f));
    f.jaw = clamp(fem ? -0.10f + 0.2f * z(rf) - (per == Persona::Villainous ? 0.1f : 0.0f)
                      : 0.75f + 0.18f * z(rf) + (per == Persona::Heroic ? 0.2f : 0.0f) - (per == Persona::Cute ? 0.3f : 0.0f),
                  -1.0f, 1.0f);
    f.chin = clamp(0.2f * z(rf) + (per == Persona::Villainous ? 0.35f : 0.0f) + (per == Persona::Heroic ? 0.2f : 0.0f) -
                       (per == Persona::Cute ? 0.3f : 0.0f),
                   -1.0f, 1.0f);
    f.cheek = clamp(0.25f * z(rf) + (per == Persona::Villainous ? 0.45f : 0.0f) + (per == Persona::Noble ? 0.25f : 0.0f) +
                        (per == Persona::Sexy ? 0.2f : 0.0f) - (per == Persona::Cute ? 0.2f : 0.0f),
                    -1.0f, 1.0f);
    f.browRidge = saturate(fem ? 0.2f + 0.1f * z(rf) : 0.7f + 0.15f * z(rf)) + (per == Persona::Villainous ? 0.15f : 0.0f);
    f.roundness = saturate((young ? 0.35f : 0.0f) + (per == Persona::Cute ? 0.4f : 0.0f) + 0.15f * fat);
    f.ageLines = saturate((g.age - 36.0f) / 36.0f);
    f.freckles = rf.chance(young ? 0.18f : 0.10f);
    f.scar = rf.chance(per == Persona::Rogue ? 0.30f : per == Persona::Heroic ? 0.10f : fem ? 0.03f : 0.06f);
    f.fang = fantasy && per == Persona::Villainous && rf.chance(0.3f);
    f.blush = per == Persona::Cute && rf.chance(0.3f);

    // ---- hair ----
    Rng rh = root.fork(7);
    std::array<float, size_t(HairStyle::Count)> w{};
    using H = HairStyle;
    auto W = [&](H s) -> float& { return w[size_t(s)]; };
    if (fem) {
        W(H::Buzz) = 0.1f; W(H::ShortCrop) = 0.2f; W(H::SidePart) = 0.3f; W(H::SlickedBack) = 0.2f; W(H::Messy) = 0.5f;
        W(H::Undercut) = 0.4f; W(H::Pixie) = 1.2f; W(H::Bob) = 2.0f; W(H::Shoulder) = 2.5f; W(H::LongStraight) = 3.0f;
        W(H::LongWavy) = 2.5f; W(H::Ponytail) = 2.0f; W(H::HighPonytail) = 1.2f; W(H::Braid) = 1.5f; W(H::Bun) = 1.2f;
        W(H::HalfUp) = 1.5f;
        if (elder) { W(H::Bun) += 2; W(H::HighPonytail) *= 0.2f; W(H::Pixie) *= 0.5f; W(H::LongWavy) *= 0.5f; }
    } else {
        W(H::Bald) = elder ? 2.3f : mature ? 0.6f : 0.15f; W(H::Buzz) = 1.2f; W(H::ShortCrop) = 3.0f; W(H::SidePart) = 2.5f;
        W(H::SlickedBack) = 1.5f; W(H::Messy) = 2.5f; W(H::Undercut) = 1.5f; W(H::Shoulder) = 1.0f; W(H::LongStraight) = 0.6f;
        W(H::LongWavy) = 0.5f; W(H::Ponytail) = 0.8f; W(H::Braid) = 0.3f; W(H::HalfUp) = 0.3f; W(H::ManBun) = 0.8f;
        if (elder) { W(H::Undercut) *= 0.2f; W(H::ManBun) *= 0.3f; }
    }
    switch (per) {
        case Persona::Rogue: W(H::Messy) += 2; W(H::Undercut) += 1; break;
        case Persona::Noble: (fem ? W(H::LongStraight) : W(H::SlickedBack)) += 1.5f; W(H::Bun) += fem ? 1.0f : 0.0f; break;
        case Persona::Cute: W(H::Bob) += fem ? 1.0f : 0.0f; W(H::HighPonytail) += fem ? 1.0f : 0.0f; W(H::Pixie) += fem ? 1.0f : 0.0f; break;
        case Persona::Villainous: W(H::LongStraight) += 1; W(H::SlickedBack) += 1.5f; W(H::Undercut) += 1; break;
        case Persona::Heroic: W(H::ShortCrop) += fem ? 0.0f : 1.0f; W(H::SidePart) += 1; W(H::Ponytail) += 1; break;
        case Persona::Sexy: W(H::LongWavy) += fem ? 1.5f : 0.3f; W(H::HalfUp) += 1; break;
        default: break;
    }
    if (arch == Archetype::Brute || arch == Archetype::Monster) { W(H::Bald) += 1.5f; W(H::Buzz) += 1; }
    g.hair = opt.hair ? *opt.hair : HairStyle(rh.weighted(std::span<const float>(w)));
    const bool shortHair = g.hair <= HairStyle::Undercut;
    g.fringe = Fringe(rh.weighted({shortHair ? 3.0f : 1.0f, shortHair ? 0.3f : 2.0f, 2.0f, shortHair ? 0.4f : 1.2f}));
    if (g.hair == HairStyle::SlickedBack || g.hair == HairStyle::Bald || g.hair == HairStyle::Buzz) g.fringe = Fringe::None;
    g.hairVolume = clamp(1.0f + 0.1f * z(rh), 0.8f, 1.25f);
    g.hairLength = clamp(1.0f + 0.12f * z(rh), 0.75f, 1.3f);
    g.hairWave = g.hair == HairStyle::LongWavy ? rh.range(0.6f, 1.0f) : rh.range(0.0f, 0.2f);
    g.partSide = rh.chance(0.5f) ? 1.0f : -1.0f;
    if (!fem && g.age >= 19)
        g.facialHair = FacialHair(rh.weighted({young ? 10.0f : 4.0f, 0.25f, 0.9f,
                                               1.5f + (per == Persona::Heroic || per == Persona::Rogue ? 1.0f : 0.0f),
                                               1.0f + (elder ? 1.5f : 0.0f) + (per == Persona::Stern ? 1.0f : 0.0f)}));

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
    return g;
}

}  // namespace cg
