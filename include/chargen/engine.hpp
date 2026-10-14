// Glue for an RPG engine: everything a game needs around a generated character
// besides the mesh itself.
//
//   * Keys and saves: a compact text key (seed + options) that regenerates the same
//     person, and an exact binary genome blob for edited / inherited / aged genomes.
//   * Body metrics: height, eye height, reach, leg length, mass estimate, movement
//     speeds and a character-controller capsule.
//   * Hit volumes: per-bone capsules tagged with a body zone and a damage scale, with
//     ray and sphere queries against the posed skeleton.
//   * Sockets: world matrices for weapons, shields, back and belt attachments.
//   * Packed GPU mesh: 32-byte vertices and 16-bit indices when they fit.
//   * Death: decay from time since death, decay stages and a rebuild check, so the
//     corpse is regenerated only when it visibly changes.
//   * LOD by screen size, with hysteresis.
//   * Populations and families: weighted peoples for a settlement, children that
//     inherit from both parents (Dwarf Fortress style lineages) and ageing a genome.
//   * Physical traits (0..1) derived from the body, to seed attributes, and a one-line
//     description for tooltips and logs.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "animation.hpp"
#include "character.hpp"
#include "genome.hpp"

namespace cg {

// ---------------------------------------------------------------- keys & saves

// Everything randomGenome reads, plus build options: regenerates the same character
// for the same library version. Edited genomes (childOf, aged, sliders) need saveGenome.
struct CharacterKey {
    uint64_t seed = 0;
    GenomeOptions options;
    float decay = 0.0f;  // 0..1, stored at 1/255 precision
    int lod = 0;
};
std::string encodeKey(const CharacterKey& k);                 // e.g. "CG1-7Z3K..." (Crockford base32)
std::optional<CharacterKey> decodeKey(std::string_view text);  // nullopt if malformed
Genome genomeFromKey(const CharacterKey& k);
Character generateFromKey(const CharacterKey& k, GenOptions base = {});

// Exact genome snapshot (versioned by layout; load fails on a mismatched build).
std::vector<uint8_t> saveGenome(const Genome& g);
std::optional<Genome> loadGenome(const std::vector<uint8_t>& bytes);
std::string toBase32(const std::vector<uint8_t>& bytes);
std::optional<std::vector<uint8_t>> fromBase32(std::string_view text);

// ---------------------------------------------------------------- metrics

struct BodyMetrics {
    float height = 0;          // floor to crown (m)
    float eyeHeight = 0;       // camera height for first person / line of sight
    float shoulderHeight = 0;
    float hipHeight = 0;
    float shoulderSpan = 0;    // shoulder joint to shoulder joint
    float reach = 0;           // shoulder to fingertip
    float legLength = 0;       // hip joint to floor
    float mass = 0;            // kg, from the body's volume
    float walkSpeed = 0;       // m/s, natural pace from leg length (Froude number)
    float runSpeed = 0;
    float capsuleRadius = 0;   // character controller (upright capsule)
    float capsuleHeight = 0;   // total height of the capsule, including the caps
    Vec3 boundsMin, boundsMax; // rest-pose mesh bounds
    int arms = 2;
    bool quadruped = false;    // centaur: four legs, a long body
};
BodyMetrics measure(const Character& c);

// ---------------------------------------------------------------- hit volumes

enum class HitZone : uint8_t { Head, Neck, Chest, Abdomen, Pelvis, UpperArm, Forearm, Hand, Thigh, Shin, Foot, Tail, Count };
std::string_view toString(HitZone z);

struct HitVolume {
    int bone = -1;
    HitZone zone = HitZone::Chest;
    Vec3 a, b;             // capsule ends, offsets in the bone's rest frame
    float radius = 0.05f;
    float damageScale = 1;  // head 2.0, neck 1.6, limbs 0.7, hands and feet 0.5...
    float side = 0;         // +1 left, -1 right, 0 centre
};
// Fitted to the mesh: each bone's capsule wraps the vertices it mostly drives.
std::vector<HitVolume> hitVolumes(const Character& c);

struct RayHit {
    int volume = -1;  // index into the volumes, -1 for a miss
    float t = 0;      // distance along the (normalised) ray
    Vec3 point;
};
// `global` comes from poseMatrices (character space; multiply the ray into it first).
RayHit raycast(const std::vector<HitVolume>& vols, const std::vector<Mat4>& global, Vec3 origin, Vec3 dir,
               float maxDist = 1e9f);
// Volumes overlapping a sphere (melee swings, explosions), nearest first.
std::vector<int> overlapSphere(const std::vector<HitVolume>& vols, const std::vector<Mat4>& global, Vec3 center,
                               float radius);

// ---------------------------------------------------------------- sockets

enum class Socket : uint8_t { HandR, HandL, Back, Hip, Head, Count };
// World (character space) matrix of an attachment point in the current pose.
Mat4 socketMatrix(const Character& c, const std::vector<Mat4>& global, Socket s);

// ---------------------------------------------------------------- packed mesh

struct PackedVertex {          // 32 bytes (vs 56 for cg::Vertex)
    float pos[3];
    int8_t normal[4];          // snorm8 xyz, w = 0
    uint16_t uv[2];            // unorm16
    uint8_t color[4];          // as Vertex::color (material class in alpha)
    uint8_t joints[4];
    uint8_t weights[4];        // unorm8, sum exactly 255
};
static_assert(sizeof(PackedVertex) == 32);

struct PackedMesh {
    std::vector<PackedVertex> vertices;
    std::vector<uint16_t> indices16;  // used when every index fits (most characters)
    std::vector<uint32_t> indices32;  // otherwise
    std::vector<Submesh> submeshes;
    bool wide() const { return !indices32.empty(); }
};
PackedMesh packMesh(const Mesh& m);

// ---------------------------------------------------------------- death & decay

enum class DecayStage : uint8_t { Alive, Fresh, Rotting, Putrefied, Skeletal, Count };
std::string_view toString(DecayStage s);
DecayStage decayStage(float decay);
// Hours since death -> decay (0..1). daysToBones: ~30 temperate, ~12 hot and humid,
// ~120 cold or dry; scale by burial, scavengers, etc.
float decayFromTime(float hoursSinceDeath, float daysToBones = 30.0f);
// Decay snapped to the steps that look different (corpse cache key).
float decayBucket(float decay, int steps = 16);
// True when the corpse built at `builtDecay` should be regenerated at `decay`.
bool needsRebuild(float builtDecay, float decay, int steps = 16);

// ---------------------------------------------------------------- LOD

// Height in pixels of a character of `worldHeight` at `distance`.
float screenHeightPx(float worldHeight, float distance, float fovYRadians, int viewportHeight);
// 0 above ~160 px, 1 above ~70 px, else 2; `current` adds 12% hysteresis.
int pickLod(float screenPx, int current = -1);

// ---------------------------------------------------------------- populations & families

struct PopulationSpec {
    std::vector<std::pair<Species, float>> species;      // weights; empty = humans
    std::vector<std::pair<Archetype, float>> archetypes; // weights; empty = natural people
    float femaleShare = 0.5f;
    std::array<float, 4> ageWeights{0.26f, 0.40f, 0.22f, 0.12f};  // young, adult, mature, elder
    float fourArmShare = 0.0f;
};
// Keys for `count` townsfolk (cheap to store; build genomes / characters on demand).
std::vector<CharacterKey> planPopulation(uint64_t seed, int count, const PopulationSpec& spec);

// A child of two parents: skin, hair and eye colour, nose, eyes, jaw, cranium, ears,
// height and frame are inherited (blended or taken from one side) with a share of
// fresh variation; species follows one parent. Returned as a young adult.
Genome childOf(const Genome& mother, const Genome& father, uint64_t seed, std::optional<Sex> sex = std::nullopt);
// The same person at another age: grey hair, lines, eye bags, jowls, sag, hunch,
// muscle loss and a little height lost in old age (or the reverse, as far as possible).
Genome aged(const Genome& g, float newAge);

// ---------------------------------------------------------------- traits & text

// Physical traits read off the body, 0..1 within the world's population, to seed
// RPG attributes (strength, agility, endurance...) consistently with how people look.
struct PhysicalTraits {
    float strength = 0.5f;   // muscle x frame x size
    float agility = 0.5f;    // light, long-legged, lean
    float endurance = 0.5f;  // frame, mass, age
    float size = 0.5f;       // height and bulk
    float presence = 0.5f;   // intimidation: size, tusks, horns, spines, scars
    float vigor = 0.5f;      // youth / age
};
PhysicalTraits physicalTraits(const Genome& g);

// "Orc man, 34, heavy build, 1.94 m; long tusks hooked inwards; spiked dorsal ridge"
std::string describe(const Genome& g);

// ---------------------------------------------------------------- validation & stress

// Sanity check of a generated character (indices, NaNs, unit normals, skin weights,
// bone ids, submesh ranges, plausible bounds and budget). Cheap enough to run on load.
bool validateCharacter(const Character& c, std::string* problem = nullptr);
// A genome with every knob randomised far beyond the natural population (all peoples,
// presets, four arms, tails, spines, carapace, cores, extra eyes, extreme sliders), for
// fuzzing; `opt` receives a random decay and LOD.
Genome fuzzGenome(uint64_t seed, GenOptions* opt = nullptr);

}  // namespace cg
