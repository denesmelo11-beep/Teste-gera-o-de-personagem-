#include "chargen/genome.hpp"

#include <array>

#include "chargen/rng.hpp"

namespace cg {

namespace {

struct R {
    float lo, hi;
};

// Ranges per archetype (head units unless noted).
struct ArchetypeSpec {
    std::string_view name;
    R height, heads, legs, neckLen, neckThick;
    R shoulder, chest, waist, hip, depth;
    R bust, butt, belly, muscle;
    R thigh, calf, arm, hand, foot;
    float femininity;
    float fantasyChance;  // fantasy skin / beast parts
    std::array<float, size_t(HairStyle::Count)> hairWeights;
};

// clang-format off
constexpr ArchetypeSpec kSpecs[] = {
    //            height        heads        legs           neckLen        neckThick
    //            shoulder      chest        waist          hip            depth
    //            bust          butt         belly          muscle
    //            thigh         calf         arm            hand           foot
    {"heroine",   {1.58f,1.74f},{6.2f,6.8f},{0.48f,0.51f},{0.24f,0.32f},{0.15f,0.17f},
                  {0.80f,0.90f},{0.62f,0.68f},{0.44f,0.49f},{0.74f,0.82f},{0.38f,0.42f},
                  {0.40f,0.75f},{0.45f,0.75f},{0,0},{0.15f,0.35f},
                  {0.33f,0.37f},{0.19f,0.21f},{0.135f,0.150f},{1.10f,1.20f},{1.05f,1.15f},
                  1.0f, 0.15f, {1,4,4,4,3,3,0,0}},
    {"bombshell", {1.66f,1.84f},{6.5f,7.1f},{0.49f,0.52f},{0.24f,0.32f},{0.15f,0.17f},
                  {0.84f,0.94f},{0.66f,0.72f},{0.42f,0.47f},{0.88f,0.98f},{0.40f,0.44f},
                  {0.85f,1.10f},{0.85f,1.10f},{0,0},{0.20f,0.40f},
                  {0.37f,0.42f},{0.20f,0.23f},{0.140f,0.155f},{1.10f,1.20f},{1.05f,1.15f},
                  1.0f, 0.2f, {0,3,5,4,2,3,0,0}},
    {"dorito",    {1.78f,1.95f},{6.8f,7.4f},{0.47f,0.50f},{0.20f,0.28f},{0.21f,0.25f},
                  {1.15f,1.32f},{0.88f,1.00f},{0.56f,0.64f},{0.68f,0.76f},{0.48f,0.54f},
                  {0,0},{0.25f,0.45f},{0,0},{0.65f,0.95f},
                  {0.36f,0.40f},{0.22f,0.25f},{0.18f,0.21f},{1.20f,1.32f},{1.15f,1.25f},
                  0.0f, 0.15f, {6,1,2,2,0,0,2,2}},
    {"brute",     {1.95f,2.30f},{6.8f,7.6f},{0.40f,0.45f},{0.10f,0.20f},{0.28f,0.34f},
                  {1.45f,1.80f},{1.15f,1.40f},{0.85f,1.05f},{0.82f,0.95f},{0.60f,0.70f},
                  {0,0},{0.25f,0.45f},{0.3f,0.7f},{0.85f,1.00f},
                  {0.44f,0.52f},{0.27f,0.32f},{0.24f,0.29f},{1.35f,1.55f},{1.25f,1.40f},
                  0.0f, 0.5f, {2,0,1,1,0,0,4,4}},
    {"slender",   {1.70f,1.88f},{6.8f,7.4f},{0.50f,0.53f},{0.28f,0.36f},{0.14f,0.155f},
                  {0.80f,0.90f},{0.60f,0.68f},{0.44f,0.50f},{0.66f,0.76f},{0.36f,0.40f},
                  {0.10f,0.45f},{0.30f,0.55f},{0,0},{0.05f,0.25f},
                  {0.30f,0.33f},{0.17f,0.19f},{0.12f,0.135f},{1.05f,1.15f},{1.0f,1.1f},
                  0.6f, 0.35f, {1,2,5,3,1,4,0,0}},
    {"chibi",     {0.95f,1.20f},{3.0f,3.7f},{0.33f,0.37f},{0.06f,0.10f},{0.11f,0.13f},
                  {0.56f,0.64f},{0.48f,0.54f},{0.44f,0.50f},{0.52f,0.58f},{0.30f,0.34f},
                  {0.0f,0.15f},{0.20f,0.35f},{0.0f,0.15f},{0.0f,0.1f},
                  {0.21f,0.24f},{0.13f,0.15f},{0.095f,0.11f},{1.0f,1.1f},{1.0f,1.1f},
                  0.6f, 0.3f, {3,3,2,3,4,2,0,0}},
    {"amazon",    {1.84f,2.02f},{6.9f,7.5f},{0.50f,0.53f},{0.22f,0.30f},{0.18f,0.20f},
                  {0.98f,1.10f},{0.74f,0.82f},{0.48f,0.54f},{0.88f,0.98f},{0.42f,0.46f},
                  {0.60f,0.95f},{0.70f,1.00f},{0,0},{0.55f,0.85f},
                  {0.39f,0.44f},{0.22f,0.25f},{0.16f,0.18f},{1.15f,1.25f},{1.1f,1.2f},
                  0.85f, 0.3f, {1,2,3,5,1,1,2,0}},
    {"monster",   {1.90f,2.50f},{6.4f,7.8f},{0.38f,0.48f},{0.08f,0.22f},{0.24f,0.36f},
                  {1.25f,1.95f},{0.95f,1.45f},{0.60f,1.05f},{0.70f,1.00f},{0.50f,0.72f},
                  {0.0f,0.6f},{0.3f,0.8f},{0.0f,0.9f},{0.70f,1.00f},
                  {0.38f,0.54f},{0.24f,0.34f},{0.20f,0.30f},{1.35f,1.70f},{1.25f,1.50f},
                  0.2f, 1.0f, {3,0,2,1,0,0,4,4}},
};
// clang-format on

float pick(Rng& r, R range) { return r.range(range.lo, range.hi); }

// Push a value away from its "canon" centre.
float exaggerate(float v, float canon, float e) { return canon + (v - canon) * (1.0f + 1.4f * e); }

Vec3 jitter(Rng& r, Vec3 c, float amount) {
    return {saturate(c.x + r.range(-amount, amount)), saturate(c.y + r.range(-amount, amount)),
            saturate(c.z + r.range(-amount, amount))};
}

Vec3 pickSkin(Rng& r, bool fantasy) {
    static constexpr Vec3 human[] = {{1.00f, 0.90f, 0.84f}, {1.00f, 0.85f, 0.74f}, {0.96f, 0.78f, 0.64f},
                                     {0.86f, 0.64f, 0.48f}, {0.66f, 0.45f, 0.32f}, {0.45f, 0.30f, 0.22f}};
    static constexpr Vec3 fantasySkins[] = {{0.55f, 0.72f, 0.45f}, {0.50f, 0.62f, 0.85f}, {0.80f, 0.36f, 0.34f},
                                            {0.62f, 0.55f, 0.80f}, {0.62f, 0.64f, 0.70f}, {0.35f, 0.32f, 0.45f}};
    if (fantasy) return jitter(r, fantasySkins[r.rangeInt(0, 5)], 0.04f);
    int i = r.weighted({4, 4, 3, 2, 1.5f, 1});
    return jitter(r, human[i], 0.02f);
}

Vec3 pickHair(Rng& r) {
    // Anime hair: naturals plus vivid colours.
    switch (r.weighted({2, 1.2f, 1, 1, 1, 1, 1, 1, 0.8f, 0.8f})) {
        case 0: return hsv(r.range(0.70f, 0.75f), r.range(0.2f, 0.4f), r.range(0.12f, 0.22f));  // near black
        case 1: return hsv(r.range(0.06f, 0.09f), r.range(0.5f, 0.7f), r.range(0.35f, 0.55f));  // brown
        case 2: return hsv(r.range(0.11f, 0.14f), r.range(0.45f, 0.65f), r.range(0.85f, 0.98f));  // blonde
        case 3: return hsv(r.range(0.0f, 0.02f), r.range(0.7f, 0.85f), r.range(0.65f, 0.85f));  // red
        case 4: return hsv(r.range(0.92f, 0.97f), r.range(0.35f, 0.55f), r.range(0.9f, 1.0f));  // pink
        case 5: return hsv(r.range(0.55f, 0.62f), r.range(0.5f, 0.75f), r.range(0.6f, 0.9f));  // blue
        case 6: return hsv(r.range(0.6f, 0.65f), r.range(0.03f, 0.1f), r.range(0.82f, 0.95f));  // silver
        case 7: return hsv(r.range(0.74f, 0.80f), r.range(0.4f, 0.6f), r.range(0.55f, 0.8f));  // purple
        case 8: return hsv(r.range(0.38f, 0.47f), r.range(0.45f, 0.65f), r.range(0.55f, 0.8f));  // green/teal
        default: return hsv(r.range(0.07f, 0.10f), r.range(0.75f, 0.9f), r.range(0.9f, 1.0f));  // orange
    }
}

void makePalette(Rng& r, Genome& g, bool fantasy) {
    Palette& p = g.palette;
    p.skin = pickSkin(r, fantasy);
    p.hair = pickHair(r);
    // Subtle gradient towards the tips (sometimes a dyed tip).
    p.hairTip = r.chance(0.2f) ? pickHair(r) : p.hair * 0.85f + Vec3{0.05f, 0.03f, 0.08f};
    float ih = r.uniform();
    p.iris = hsv(ih, r.range(0.6f, 0.9f), r.range(0.55f, 0.85f));
    p.iris2 = hsv(ih + r.range(-0.06f, 0.06f), r.range(0.3f, 0.6f), 1.0f);

    // Cloth: colour-scheme based palettes so outfits always look designed.
    int scheme = r.weighted({3, 3, 2, 1.2f});
    float h = r.uniform();
    float s = r.range(0.35f, 0.8f);
    if (scheme == 0) {  // complementary
        p.cloth[0] = hsv(h, s, r.range(0.3f, 0.6f));
        p.cloth[1] = hsv(h, s * 0.15f, r.range(0.85f, 0.97f));
        p.cloth[2] = hsv(h + 0.5f, r.range(0.6f, 0.9f), r.range(0.7f, 0.95f));
    } else if (scheme == 1) {  // analogous
        p.cloth[0] = hsv(h, s, r.range(0.25f, 0.5f));
        p.cloth[1] = hsv(h + 0.08f, s * 0.8f, r.range(0.6f, 0.85f));
        p.cloth[2] = hsv(h - 0.1f, r.range(0.6f, 0.9f), r.range(0.8f, 0.97f));
    } else if (scheme == 2) {  // triad
        p.cloth[0] = hsv(h, s, r.range(0.35f, 0.65f));
        p.cloth[1] = hsv(h + 0.333f, s * 0.6f, r.range(0.7f, 0.9f));
        p.cloth[2] = hsv(h + 0.666f, r.range(0.6f, 0.9f), r.range(0.75f, 0.95f));
    } else {  // chess: black & white with one accent (Hyper Chess homage)
        bool dark = r.chance(0.5f);
        p.cloth[0] = dark ? Vec3{0.12f, 0.12f, 0.14f} : Vec3{0.94f, 0.94f, 0.96f};
        p.cloth[1] = dark ? Vec3{0.94f, 0.94f, 0.96f} : Vec3{0.12f, 0.12f, 0.14f};
        p.cloth[2] = hsv(h, 0.8f, 0.9f);
        g.outfit.pattern = r.chance(0.6f) ? Pattern::Checker : Pattern::Plain;
    }
    p.leather = hsv(r.range(0.04f, 0.09f), r.range(0.4f, 0.6f), r.range(0.18f, 0.4f));
    p.metal = r.chance(0.5f) ? hsv(r.range(0.1f, 0.13f), r.range(0.45f, 0.6f), r.range(0.8f, 0.95f))  // gold
                             : hsv(r.range(0.55f, 0.65f), r.range(0.05f, 0.15f), r.range(0.6f, 0.85f));  // steel
    p.horn = fantasy && r.chance(0.5f) ? hsv(r.uniform(), 0.4f, 0.25f) : Vec3{0.92f, 0.88f, 0.78f};
}

template <class E>
E pickEnum(Rng& r, std::initializer_list<float> w) {
    return static_cast<E>(r.weighted(w));
}

}  // namespace

std::string_view toString(Archetype a) { return a < Archetype::Count ? kSpecs[size_t(a)].name : "?"; }

std::string_view toString(HairStyle a) {
    static constexpr std::string_view n[] = {"spiky", "bob", "long", "ponytail", "twintails", "hime", "mohawk", "buzz"};
    return a < HairStyle::Count ? n[size_t(a)] : "?";
}

std::optional<Archetype> archetypeFromString(std::string_view s) {
    for (size_t i = 0; i < size_t(Archetype::Count); ++i)
        if (kSpecs[i].name == s) return Archetype(i);
    return std::nullopt;
}

Genome randomGenome(uint64_t seed, const GenomeOptions& opt) {
    Genome g;
    g.seed = seed;
    Rng root(seed);
    Rng ra = root.fork(1);
    g.archetype = opt.archetype ? *opt.archetype : Archetype(ra.weighted({4, 3, 3, 1.5f, 2, 1, 2, 1}));
    const ArchetypeSpec& sp = kSpecs[size_t(g.archetype)];

    // ---- body ----
    Rng rb = root.fork(2);
    BodyShape& b = g.body;
    float e = opt.exaggeration;
    b.height = pick(rb, sp.height);
    b.headsTall = pick(rb, sp.heads);
    b.legRatio = pick(rb, sp.legs);
    b.neckLength = pick(rb, sp.neckLen);
    b.neckThick = pick(rb, sp.neckThick);
    b.shoulderWidth = exaggerate(pick(rb, sp.shoulder), 0.95f, e);
    b.chestWidth = exaggerate(pick(rb, sp.chest), 0.72f, e);
    b.waistWidth = std::max(0.30f, exaggerate(pick(rb, sp.waist), 0.52f, e));
    b.hipWidth = exaggerate(pick(rb, sp.hip), 0.78f, e);
    b.chestDepth = pick(rb, sp.depth);
    b.bust = std::max(0.0f, pick(rb, sp.bust) * (1.0f + 0.6f * e));
    b.butt = std::max(0.0f, pick(rb, sp.butt) * (1.0f + 0.5f * e));
    b.belly = pick(rb, sp.belly);
    b.muscle = pick(rb, sp.muscle);
    b.thigh = exaggerate(pick(rb, sp.thigh), 0.37f, e * 0.7f);
    b.calf = exaggerate(pick(rb, sp.calf), 0.21f, e * 0.7f);
    b.arm = exaggerate(pick(rb, sp.arm), 0.15f, e * 0.8f);
    b.handSize = exaggerate(pick(rb, sp.hand), 1.0f, e * 0.6f);
    b.footSize = pick(rb, sp.foot);
    b.armLength = rb.range(0.96f, 1.04f);
    b.headWidth = rb.range(0.97f, 1.05f) * (sp.femininity > 0.5f ? 1.0f : 1.06f);
    b.posture = rb.range(0.3f, 0.8f) * (0.5f + 0.6f * sp.femininity);

    // ---- beast / anatomy extensions ----
    Rng rx = root.fork(3);
    bool fantasy = rx.chance(sp.fantasyChance);
    bool monster = g.archetype == Archetype::Monster;
    b.armPairs = monster ? (rx.chance(0.6f) ? 2 : 1) : (fantasy && rx.chance(0.08f) ? 2 : 1);
    b.tail = monster ? rx.chance(0.7f) : (fantasy && rx.chance(0.35f));
    b.tailLength = rx.range(0.7f, 1.3f);
    b.horns = (monster ? rx.chance(0.75f) : (fantasy && rx.chance(0.3f))) ? 2 : 0;
    b.hornSize = rx.range(0.7f, 1.6f) * (monster ? 1.3f : 1.0f);
    b.ears = fantasy ? pickEnum<EarStyle>(rx, {1, 3, 2}) : EarStyle::Human;

    // ---- face ----
    Rng rf = root.fork(4);
    float fem = std::clamp(sp.femininity + rf.range(-0.15f, 0.15f), 0.0f, 1.0f);
    g.femininity = fem;
    FaceStyle& f = g.face;
    f.eyeSize = lerp(0.8f, 1.12f, fem) * rf.range(0.92f, 1.08f) * (g.archetype == Archetype::Chibi ? 1.15f : 1.0f);
    f.eyeSpacing = rf.range(0.94f, 1.06f);
    f.eyeTilt = rf.range(-0.12f, 0.18f) + (1.0f - fem) * 0.06f;
    f.eyeRound = saturate(lerp(0.25f, 0.75f, fem) + rf.range(-0.15f, 0.15f));
    f.lashWeight = saturate(lerp(0.25f, 0.95f, fem) + rf.range(-0.1f, 0.1f));
    f.browThick = saturate(lerp(0.85f, 0.4f, fem) + rf.range(-0.1f, 0.1f));
    f.mouthWidth = rf.range(0.85f, 1.15f) * (fem < 0.5f ? 1.15f : 1.0f);
    f.pupil = fantasy ? pickEnum<PupilStyle>(rf, {1, 3, 1, 0.6f}) : (rf.chance(0.06f) ? PupilStyle::Star : PupilStyle::Round);
    f.fang = rf.chance(fantasy ? 0.6f : 0.12f);
    f.blush = rf.chance(fem * 0.5f);

    // ---- hair ----
    Rng rh = root.fork(5);
    g.hair = HairStyle(rh.weighted(std::span<const float>(sp.hairWeights)));
    g.hairVolume = rh.range(0.85f, 1.2f);
    g.hairLength = rh.range(0.8f, 1.25f);

    // ---- outfit ----
    Rng ro = root.fork(6);
    Outfit& o = g.outfit;
    o.pattern = pickEnum<Pattern>(ro, {6, 1.2f, 0.8f});
    if (fem > 0.5f) {
        o.top = pickEnum<TopStyle>(ro, {0, 2, 2, 2, 3, 2, 1.5f});
        o.bottom = pickEnum<BottomStyle>(ro, {2, 3, 1, 3, 0.6f});
        o.boots = pickEnum<BootStyle>(ro, {1, 2, 3, 2.5f});
        o.gloves = pickEnum<GloveStyle>(ro, {3, 1.5f, 1.5f, 1.5f});
    } else {
        o.top = pickEnum<TopStyle>(ro, {g.archetype == Archetype::Brute || monster ? 4.0f : 1.0f, 2, 2, 2, 0, 0, 2.5f});
        o.bottom = pickEnum<BottomStyle>(ro, {5, 2, 0, 0, monster ? 3.0f : 0.8f});
        o.boots = pickEnum<BootStyle>(ro, {1, 3, 3, 0});
        o.gloves = pickEnum<GloveStyle>(ro, {2, 2, 2.5f, 0.5f});
    }
    o.shoulderPads = ro.weighted({5, 1.5f, g.archetype == Archetype::Brute || g.archetype == Archetype::Dorito ? 4.0f : 1.5f});
    o.padSize = ro.range(0.8f, 1.3f) * (1.0f + 0.4f * e);
    o.belt = ro.chance(0.6f);

    // Palette last: the "chess" scheme may override the pattern.
    Rng rp = root.fork(7);
    makePalette(rp, g, fantasy);
    return g;
}

}  // namespace cg
