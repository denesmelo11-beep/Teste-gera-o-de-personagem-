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
    Bob, Shoulder, LongStraight, LongWavy, Ponytail, HighPonytail, Braid, Bun, HalfUp, ManBun,
    CrewCut, Tonsure, WarriorBraids, Curly, Afro, Mullet, Topknot, Locs, Wolf,
    Hime, CrownBraid, TwinBraids, SideBraid, LowPonytail, BowlCut, MessyBun, LongCurly, VeryLong, AsymBob, Count
};
bool isShortHair(HairStyle h);  // cut close to the head (cap + locks only)
bool isTiedHair(HairStyle h);   // hair gathered into tails, braids or buns
enum class Fringe : uint8_t { None, Curtain, SideSwept, Straight, Count };
enum class FacialHair : uint8_t { None, Stubble, Mustache, Goatee, ShortBeard, FullBeard, LongBeard, BraidedBeard, MuttonChops, Tendrils, Count };
enum class EarStyle : uint8_t { Human, Elf, Fin, None, Count };  // Fin: a ribbed membrane fan (fishfolk); None: ear holes only (avians, reptiles)
enum class PupilStyle : uint8_t { Round, Slit, Count };
// Peoples of the world. Each one reshapes the sampled body and face (proportions,
// skin, ears, tusks, horns, muzzle, hooves, glowing eyes) and biases hair and beards.
// The second block are trait peoples, built from the composable traits in BodyShape and
// FaceStyle (wings, beaks, serpent bodies, shells, mandibles...): each covers a family of
// fantasy races (dragonborn and kobolds, aarakocra and arakkoa, naga and yuan-ti...).
enum class Species : uint8_t {
    Human, Dwarf, Gnome, Orc, Troll, Undead, NightElf, HighElf, Draenei, Tauren, Goblin, Worgen, Ogre, Centaur, Skeleton,
    Dragonborn, Saurian, Avian, Naga, Tortle, Insectoid, Fishfolk, Demon, Cephalid, Fae, Crystalborn, Emberkin, Wraith,
    Cyclops, Talpid, Oculite, Slime, Rimemaw, Gloomhound, Gravitant,
    Count
};
// First trait people (the classic peoples come before it).
inline constexpr Species kFirstTraitSpecies = Species::Dragonborn;

std::string_view toString(Sex s);
std::string_view toString(AgeGroup a);
std::string_view toString(Persona p);
std::string_view toString(Archetype a);
std::string_view toString(HairStyle h);
std::string_view toString(FacialHair f);
std::string_view toString(Species s);
std::optional<Species> speciesFromString(std::string_view s);
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

// Ear blade: length and width in head units, angles in radians.
struct EarShape {
    float length = 0.11f, width = 0.085f;
    float up = 0.15f;     // tilt above horizontal
    float back = 0.30f;   // sweep towards the back
    float droop = 0.0f;   // the tip bends down (bovine, long troll ears)
    float baseS = 0.45f;  // root height on the head (0 chin .. 1 crown): wolves wear them on top
    float baseIn = 0.08f; // root angle in from the side of the head (radians)
};

struct BodyShape {
    int build = 0;              // 0 average, 1 lean, 2 heavy, 3 stocky, 4 lanky, 5 strong
    float boneMass = 1.0f;      // skeleton robustness (frame, muscle, people): bone and joint thickness
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
    EarShape ear;
    float muzzle = 0.0f;        // 0 flat face .. 1 bovine snout
    int tusks = 0;              // 0 none, 1 short lower tusks, 2 long upswept tusks
    float tuskSize = 1.0f;
    float tuskCurve = 0.0f;     // -1 splay outwards .. +1 hook inwards towards each other (more imposing)
    int tailType = 0;           // 0 plain, 1 heavy reptile, 2 whip with spade tip, 3 bushy fur, 4 spiked club, 5 thin rat-like
    int spines = 0;             // dorsal spines along the spine (and the tail): count, 0 none
    float spineSize = 1.0f;
    int spineKind = 0;          // 0 swept-back spikes, 1 bony plates, 2 short knobs
    int skinKind = 0;           // 0 flesh, 1 metallic (constructs, androids), 2 armoured carapace (hard faceted plates),
                                // 3 scales (fine cells, pale belly), 4 crystal (faceted shards), 5 magma (black crust, glowing seams)
    float carapace = 1.0f;      // carapace coverage 0..1 (plates spread from the limbs and back; >0.95 masks the face too)
    Vec3 carapaceColor{0.10f, 0.10f, 0.12f};
    int core = 0;               // a glowing orb set into the body: 0 none, 1 abdomen, 2 chest
    Vec3 coreColor{0.45f, 0.80f, 1.0f};
    bool hooves = false;
    int hornKind = -1;          // -1 any, 0 swept back, 1 ram, 2 upright, 3 bovine (out, then up)
    bool centaur = false;       // human torso on a horse body (four legs)
    bool skeletal = false;      // nothing but bones (risen skeletons): always built at full decay
    bool digitigrade = false;   // walks on the toes: high hock behind, long metatarsal (draenei, worgen)
    int snout = 0;              // muzzle shape: 0 bovine (broad), 1 wolf (long, narrow), 2 reptile (long, flat, wide jaw)
    bool tailFur = false;       // bushy furred tail (hair material)
    bool claws = false;         // dark claws on fingers and toes
    int furPattern = 0;         // 0 none, 1 countershading (pale front, dark back), 2 also dark lower limbs, 3 also stripes (beasts)
    int bones = 0;              // exposed bones (undead): 1 ribs, 2 forearm, 4 shin, 8 jaw
    int boneSide = 1;           // +1 left, -1 right
    // ---- composable species traits (any people can wear any of them) ----
    int wings = 0;              // 0 none, 1 membrane (bat, dragon, demon), 2 feathered, 3 insect (two pairs of panes)
    float wingSize = 1.0f;      // span scale
    bool wingsFolded = false;   // rest pose folded along the back instead of half spread
    int lowerBody = 0;          // 0 legs, 1 serpent (naga: a long scaled coil instead of legs), 2 wraith (a trailing wisp),
                                // 3 ooze (the body sags into a gelatinous mound)
    float serpentLength = 1.0f; // coil length scale
    int hands = 0;              // 0 five fingers, 1 three clawed talons, 2 crab pincer, 3 tendrils (boneless fingers),
                                // 4 digging spades (broad palms, five flat shovel claws)
    int feet = 0;               // 0 by the people, 1 bird talons (three toes forward, one back)
    int backTentacles = 0;      // tentacles from the shoulder blades (0..6), with spring physics
    float tentacleLength = 1.0f;
    bool shell = false;         // a turtle shell on the back with a plastron over the belly
    int crest = 0;              // 0 none, 1 fin along the head midline, 2 feather crest, 3 bony neck frill, 4 cobra hood
    float crestSize = 1.0f;
    int shards = 0;             // crystal shards growing from shoulders and back (count)
    Vec3 crystalColor{0.55f, 0.80f, 0.95f};
    int bodyEyes = 0;           // extra eyes open over the torso, arms and legs (count)
    bool orb = false;           // a living world: the trunk swells into a great sphere the face looks out of, small limbs
    int stance = 0;             // 0 upright, 1 beast: on all fours, the arms become forelegs and the head is held forward
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
    // Identity extras.
    float crown = 0.0f;        // -1 small, flat cranium .. +1 tall, round cranium
    float faceWidth = 0.0f;    // -1 narrow, long face .. +1 broad face (cheeks and jaw)
    float browLength = 1.0f;
    float irisSize = 1.0f;
    int noseShape = 1;         // 0 button, 1 straight, 2 aquiline, 3 broad
    float earSize = 1.0f;
    int marking = 0;           // 0 none, 1 mole, 2 cheek warpaint, 3 under-eye bars, 4 chin stripe, 5 brow slit, 6 forehead mark
    Vec3 markColor{0.45f, 0.12f, 0.12f};
    // Sculpted nose (a faceted piece of its own, the face's main plane break).
    float noseLength = 1.0f;   // root-to-tip length
    float noseProj = 1.0f;     // how far the tip stands off the face
    float noseWidth = 1.0f;    // bridge and nostril-wing width
    float noseHump = 0.0f;     // dorsal hump (aquiline/broken) 0..1
    float noseTip = 0.0f;      // -1 hooked/drooping .. +1 upturned
    // Skull and soft tissue.
    float chinWidth = 0.0f;    // -1 pointed .. +1 broad, square chin
    float cheekHollow = 0.0f;  // gaunt hollows under the cheekbones 0..1
    float jowl = 0.0f;         // heavy lower face (age, fat) 0..1
    float eyeDepth = 0.3f;     // deep-set eyes: socket shading and brow overhang 0..1
    float eyeBags = 0.0f;      // tired, puffy under-eyes 0..1
    // Skin.
    float stubble = 0.0f;      // beard shadow on the jaw and upper lip 0..1
    float ruddy = 0.0f;        // weathered red on nose and cheeks 0..1
    float grime = 0.0f;        // dirt smudges (survivors, labourers) 0..1
    int scarType = 0;          // 0 none, 1 brow-to-cheek, 2 across the nose, 3 lip, 4 cheek gash, 5 blinded eye
    int tattoo = 0;            // 0 none, 1 runic lines, 2 tribal cheek marks, 3 forehead sigil
    bool glowEyes = false;     // luminous eyes without pupils (elves, draenei, undead)
    bool darkNose = false;     // a dark, wet animal nose (worgen)
    float milky = 0.0f;        // clouded, dead eyes 0..1
    // Trait features (beaks and mouthparts replace the nose and mouth they cover).
    int beak = 0;              // 0 none, 1 hooked raptor, 2 long straight (heron, arakkoa), 3 short and stout (owl, turtle)
    bool mandibles = false;    // insect mouthparts: two curved pincers at the mouth corners
    int antennae = 0;          // 0 none, 1 segmented whips, 2 feathery (moth)
    float antennaLength = 1.0f;
    int eyeKind = 0;           // 0 normal, 1 compound (faceted glossy insect eyes), 2 single cyclopean eye, 3 eyeless (skin over the sockets)
    bool maw = false;          // a lipless mouth split from cheek to cheek, rows of teeth
    // Species head and face character.
    float headTop = 0.0f;      // -1 small, low cranium .. +1 big bulbous dome
    float chinPoint = 0.0f;    // 0 .. 1 the lower face narrows to a pointed chin
    float underbite = 0.0f;    // 0 .. 1 the lower jaw juts forward past the upper lip
    float wrinkles = 0.0f;     // 0 .. 1 creased skin (brow, nose bridge, crow's feet, cheek folds)
    int mouthStyle = 0;        // 0 lips, 1 wide sharp-toothed grin line, 2 underbite with lower teeth, 3 shark grin (two rows of pointed teeth)
    Vec3 sclera{0.97f, 0.97f, 0.97f};
    bool hollowEyes = false;   // eyes rotted away: dark sockets
    int eyePairs = 1;          // 2: a second, smaller pair of eyes below the first
    Vec3 glow{0.8f, 0.9f, 1.0f};
    float rot = 0.0f;          // undead decay: dark patches and stitches 0..1
    Vec3 tattooColor{0.16f, 0.22f, 0.32f};
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
    Vec3 wing{0.30f, 0.22f, 0.22f};    // membrane, feathers or insect panes
    Vec3 beak{0.85f, 0.70f, 0.30f};    // beak, mandibles, talons
    Vec3 belly{0.80f, 0.72f, 0.55f};   // scaled bellies and plastrons, sucker sides of tentacles
};

struct Genome {
    uint64_t seed = 0;
    Archetype archetype = Archetype::Natural;
    Species species = Species::Human;
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
    std::optional<Species> species;      // Human when empty (unless mixedSpecies)
    std::optional<int> armPairs;         // 1 or 2 (four arms) for any people
    bool mixedSpecies = false;           // draw the people from the whole world
    float exaggeration = 0.0f;           // pushes proportions towards stylised extremes
};

Genome randomGenome(uint64_t seed, const GenomeOptions& opt = {});

}  // namespace cg
