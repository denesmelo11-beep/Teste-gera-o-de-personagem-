#include "chargen/studies.hpp"

#include <algorithm>

namespace cg {

namespace {

void heroicMuscle(BodyShape& b, float k) {
    Anatomy& a = b.anat;
    b.muscle = k;
    b.belly = 0.0f;
    a.definition = std::min(1.0f, 0.55f + 0.4f * k);
    for (float* f : {&a.traps, &a.deltoids, &a.pecs, &a.lats, &a.abs, &a.obliques, &a.serratus, &a.biceps, &a.triceps,
                     &a.forearms, &a.glutes, &a.quads, &a.hamstrings, &a.calves, &a.neck})
        *f = std::max(*f, 0.45f + 0.5f * k);
}

// Proportions as multipliers on the sampled person (the canon's absolute scale
// changes over time; ratios keep a study's intent).
void shape(BodyShape& b, float sh, float ch, float wa, float hip, float dep, float arm, float thigh, float calf, float neck) {
    b.shoulderWidth *= sh, b.chestWidth *= ch, b.waistWidth *= wa, b.hipWidth *= hip, b.chestDepth *= dep;
    b.arm *= arm, b.thigh *= thigh, b.calf *= calf, b.neckThick *= neck;
}

void hardMaleFace(FaceStyle& f) {
    f.jaw = 0.75f;
    f.chin = 0.1f;
    f.chinWidth = 0.6f;
    f.cheek = 0.6f;
    f.browRidge = 0.7f;
    f.eyeDepth = 0.6f;
    f.roundness = 0.0f;
}

Genome base(uint64_t seed, Species sp, Sex sex) {
    GenomeOptions o;
    o.species = sp;
    o.sex = sex;
    o.age = AgeGroup::Adult;
    o.persona = Persona::Stern;
    return randomGenome(seed, o);
}

}  // namespace

const std::vector<Study>& studies() {
    static const std::vector<Study> kStudies = {
        {"android_monk", "androide monge (nucleo)"},
        {"homunculus", "homunculo sorridente"},
        {"homunculus_shield", "homunculo com escudo"},
        {"four_armed_giant", "gigante de 4 bracos"},
        {"four_armed_giantess", "giganta de 4 bracos"},
        {"centaur_warlord", "centauro de 4 bracos"},
        {"chitin_horror", "horror de quitina"},
        {"iron_golem", "golem de ferro"},
        {"giant_corpse", "gigante apodrecendo"},
        {"giant_bones", "ossada do gigante"},
        {"android_ruin", "androide em ruina"},
        {"tiny_four_arms", "gnomo extremo 4 bracos"},
    };
    return kStudies;
}

std::optional<Genome> studyGenome(std::string_view name, uint64_t seed, GenOptions* opt) {
    GenOptions dummy;
    GenOptions& go = opt ? *opt : dummy;
    Genome g;
    // Bald, metallic blue, heroically muscled, a glowing core in the navel.
    auto androidMonk = [&] {
        g = base(seed, Species::Human, Sex::Male);
        BodyShape& b = g.body;
        b.height = 1.86f;
        b.headsTall = 7.4f;
        b.build = 5;
        heroicMuscle(b, 0.95f);
        b.waistWidth = std::max(b.waistWidth, 0.68f), b.hipWidth = std::max(b.hipWidth, 0.72f);
        shape(b, 1.16f, 1.18f, 0.88f, 0.96f, 1.06f, 1.18f, 1.06f, 1.06f, 1.15f);
        b.skinKind = 1;
        b.core = 1;
        b.coreColor = {0.55f, 0.85f, 1.0f};
        g.palette.skin = {0.36f, 0.46f, 0.60f};
        g.palette.lips = {0.26f, 0.30f, 0.40f};
        g.palette.iris = {0.75f, 0.20f, 0.16f};
        g.hair = HairStyle::Bald;
        g.facialHair = FacialHair::None;
        g.face.stubble = 0.0f;
        g.face.browThick = 0.25f;
        g.face.tattoo = 0, g.face.scarType = 0, g.face.grime = 0.0f, g.face.ruddy = 0.0f;
        hardMaleFace(g.face);
        g.face.eyeShape = 3;
        g.face.eyeSize = std::min(g.face.eyeSize, 0.9f);
    };
    // Lean and hard, spiky black hair, a predatory grin full of sharp teeth.
    auto homunculus = [&](bool shield) {
        g = base(seed, Species::Human, Sex::Male);
        BodyShape& b = g.body;
        b.height = 1.84f;
        b.headsTall = 7.3f;
        b.build = 1;
        heroicMuscle(b, 0.75f);
        b.waistWidth = std::max(b.waistWidth, 0.66f), b.hipWidth = std::max(b.hipWidth, 0.70f);
        shape(b, 1.06f, 1.02f, 0.84f, 0.92f, 0.96f, 1.02f, 0.94f, 0.94f, 1.0f);
        g.hair = HairStyle::ShortCrop;
        g.hairVolume = 1.3f;
        g.hairLength = 0.8f;
        g.facialHair = FacialHair::None;
        g.palette.hair = {0.07f, 0.07f, 0.08f};
        g.palette.hairTip = g.palette.hair;
        g.palette.brow = {0.06f, 0.06f, 0.07f};
        g.palette.skin = {0.86f, 0.70f, 0.60f};
        g.palette.iris = {0.62f, 0.20f, 0.36f};
        FaceStyle& f = g.face;
        hardMaleFace(f);
        f.eyeShape = 3;
        f.eyeTilt = 0.08f;
        f.browTilt = -0.5f;
        f.mouthStyle = 3;
        f.mouthWidth = 1.3f;
        f.mouthRest = 0.012f;
        f.mouthAsym = 0.004f;
        f.eyeSize = std::min(f.eyeSize, 0.88f);
        f.fang = true;
        f.stubble = 0.0f;
        f.scarType = 0;
        if (shield) {  // the body hardens into black plates; the face stays his own
            b.skinKind = 2;
            b.carapace = 0.93f;
            b.carapaceColor = {0.08f, 0.08f, 0.10f};
        }
    };
    // Four arms, four eyes, red hide, a small head on an enormous frame.
    auto giant = [&](Sex sex) {
        g = base(seed, Species::Human, sex);
        const bool fem = sex == Sex::Female;
        BodyShape& b = g.body;
        b.height = fem ? 3.05f : 3.45f;
        b.headsTall = fem ? 7.4f : 7.8f;
        b.build = 5;
        b.armPairs = 2;
        b.boneMass = 1.45f;
        heroicMuscle(b, fem ? 0.85f : 1.15f);
        b.waistWidth = std::max(b.waistWidth, fem ? 0.62f : 0.74f), b.hipWidth = std::max(b.hipWidth, 0.74f);
        if (fem) shape(b, 1.20f, 1.18f, 1.0f, 1.08f, 1.10f, 1.25f, 1.12f, 1.12f, 1.35f);
        else shape(b, 1.38f, 1.40f, 1.08f, 1.06f, 1.25f, 1.45f, 1.28f, 1.28f, 1.65f);
        b.neckLength = 0.18f;
        b.handSize = 1.25f, b.footSize = 1.15f, b.armLength = 1.05f;
        b.horns = 0, b.tail = false, b.spines = 0, b.tusks = 0;
        if (fem) b.bust = std::min(b.bust, 0.7f);
        g.palette.skin = {0.62f, 0.17f, 0.13f};
        g.palette.lips = {0.30f, 0.08f, 0.08f};
        g.palette.underwear = {0.14f, 0.14f, 0.16f};
        FaceStyle& f = g.face;
        f.eyePairs = 2;
        f.glowEyes = true;
        f.glow = {0.95f, 0.82f, 0.25f};
        f.browThick = 0.9f;
        f.stubble = 0.0f;
        f.scarType = 0, f.tattoo = 0, f.marking = 0;
        if (!fem) hardMaleFace(f), f.chinWidth = 0.9f, f.browRidge = 0.95f;
        g.hair = fem ? HairStyle::Braid : HairStyle::Topknot;
        g.facialHair = FacialHair::None;
        g.palette.hair = {0.08f, 0.07f, 0.07f};
        g.palette.hairTip = g.palette.hair;
        g.palette.brow = g.palette.hair;
    };
    if (name == "android_monk") androidMonk();
    else if (name == "homunculus") homunculus(false);
    else if (name == "homunculus_shield") homunculus(true);
    else if (name == "four_armed_giant") giant(Sex::Male);
    else if (name == "four_armed_giantess") giant(Sex::Female);
    else if (name == "giant_corpse") giant(Sex::Male), go.decay = 0.55f;
    else if (name == "giant_bones") giant(Sex::Male), go.decay = 1.0f;
    else if (name == "android_ruin") androidMonk(), go.decay = 0.65f;
    else if (name == "centaur_warlord") {
        g = base(seed, Species::Centaur, Sex::Male);
        g.body.armPairs = 2;
        g.body.spines = 7, g.body.spineKind = 0, g.body.spineSize = 1.0f;
        g.body.horns = 2, g.body.hornKind = 0;
        heroicMuscle(g.body, 0.9f);
    } else if (name == "chitin_horror") {
        GenomeOptions o;
        o.archetype = Archetype::Monster;
        o.sex = Sex::Male;
        g = randomGenome(seed, o);
        BodyShape& b = g.body;
        b.skinKind = 2, b.carapace = 1.0f, b.carapaceColor = {0.20f, 0.17f, 0.12f};
        b.tail = true, b.tailFur = false, b.tailType = 4, b.tailLength = 1.0f;
        b.spines = 8, b.spineKind = 1, b.spineSize = 1.1f;
        b.tusks = 2, b.tuskSize = 0.9f, b.tuskCurve = 1.0f;
        b.horns = 2, b.hornKind = 1;
        b.armPairs = 2;
        g.face.eyePairs = 2;
        g.face.glowEyes = true;
        g.face.glow = {0.9f, 0.3f, 0.15f};
        g.hair = HairStyle::Bald;
        g.facialHair = FacialHair::None;
    } else if (name == "iron_golem") {
        g = base(seed, Species::Ogre, Sex::Male);
        g.body.skinKind = 1;
        g.body.core = 2;
        g.body.coreColor = {1.0f, 0.55f, 0.18f};
        g.body.tusks = 0;
        g.palette.skin = {0.34f, 0.33f, 0.32f};
        g.palette.lips = {0.20f, 0.19f, 0.19f};
        g.face.glowEyes = true;
        g.face.glow = {1.0f, 0.55f, 0.18f};
        g.hair = HairStyle::Bald;
        g.facialHair = FacialHair::None;
        g.face.stubble = 0.0f;
    } else if (name == "tiny_four_arms") {
        GenomeOptions o;
        o.species = Species::Gnome;
        o.armPairs = 2;
        o.exaggeration = 2.0f;
        g = randomGenome(seed, o);
    } else {
        return std::nullopt;
    }
    return g;
}

}  // namespace cg
