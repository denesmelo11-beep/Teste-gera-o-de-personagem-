// Output of the generator: one skinned mesh, a simple humanoid skeleton (with
// optional extra arms / tail / hair chains / bust bones) and a 2D face atlas.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "genome.hpp"
#include "image.hpp"
#include "math.hpp"

namespace cg {

// Stored in COLOR_0.a so a toon shader can pick shadow tint / specular per
// material without extra textures: alpha = (class * 32 + 16) / 255.
enum class MaterialClass : uint8_t { Skin, Cloth, Leather, Metal, Hair, Dark, Horn, Count };

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;                 // only meaningful for face decals
    uint8_t color[4];        // sRGB rgb + material class in alpha
    uint8_t joints[4];
    float weights[4];
};

enum class SubmeshKind : uint8_t { Body, FaceEyes, FaceMouth };

struct Submesh {
    SubmeshKind kind;
    uint32_t firstIndex = 0, indexCount = 0;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;  // Body first, then the two face decals
    std::vector<uint16_t> partIds;   // per vertex: which primitive it came from (outline hint)
    size_t triangleCount() const { return indices.size() / 3; }
};

struct Bone {
    std::string name;
    int parent = -1;
    Vec3 restLocal;  // translation relative to parent (rest rotations are identity)
    Vec3 restWorld;
};

struct Skeleton {
    std::vector<Bone> bones;
    int add(std::string name, int parent, Vec3 world);
    int find(std::string_view name) const;
};

// Bone ids grouped by role, consumed by the procedural animator.
struct LimbRig {
    int root = -1, upper = -1, lower = -1, end = -1, tip = -1;
    float side = 1;   // +1 left (+X), -1 right
    Vec3 restDir;     // rest direction of the upper bone
};

struct RigInfo {
    int root = -1, hips = -1, spine = -1, chest = -1, neck = -1, head = -1;
    std::vector<LimbRig> arms;  // L, R, (L2, R2)
    std::vector<LimbRig> legs;  // L, R
    int bustL = -1, bustR = -1;
    std::vector<int> tail;
    std::vector<std::vector<int>> hairChains;
    // Equipment sockets (bone ids): weapons, shield, back, belt.
    int socketHandR = -1, socketHandL = -1, socketBack = -1, socketHip = -1;
};

// ---- physics description (consumed by cg::SecondaryMotion or an engine) ----
// Capsule collider attached to a bone; a == b gives a sphere. Offsets are in
// the bone's rest frame (rest rotations are identity, so world-aligned).
struct PhysCollider {
    int bone = -1;
    Vec3 a, b;
    float radius = 0.05f;
};

// VRM-style spring chain: every bone points at the next one (the last at
// `tail`), and is simulated as a damped pendulum with collisions.
struct SpringChain {
    std::vector<int> bones;
    Vec3 tail;                 // tip offset from the last bone, rest frame
    float stiffness = 0.4f;    // pull back to the animated rest direction (1/s)
    float drag = 0.4f;         // 0..1 velocity loss per step
    float gravity = 0.3f;      // m/s^2 scale
    float hitRadius = 0.02f;   // particle radius for collisions
    float maxAngle = 1.6f;     // max deviation from rest direction (radians)
    std::string name;
};

// Soft-tissue spring (bust): point mass in the parent frame with per-axis limits.
struct JiggleBone {
    int bone = -1;
    float frequency = 2.2f;    // Hz
    float damping = 0.25f;     // ratio of critical
    Vec3 limit{0.03f, 0.04f, 0.03f};  // max offset per axis (metres, parent frame)
    float gravity = 0.15f;     // sag under acceleration
};

struct PhysicsRig {
    std::vector<PhysCollider> colliders;
    std::vector<SpringChain> chains;
    std::vector<JiggleBone> jiggles;
};

// ---- 2D reactive face ----
enum class EyeExpr : uint8_t { Neutral, Blink, Happy, Angry, Sad, Surprised, Hurt, Smug, Count };
enum class MouthExpr : uint8_t { Neutral, Smile, Open, Grin, Frown, Shout, Oh, Smirk, Count };
enum class Emotion : uint8_t { Calm, Joy, Rage, Sorrow, Shock, Pain, Confident, Talk, Count };

struct FaceState {
    EyeExpr eyes = EyeExpr::Neutral;
    MouthExpr mouth = MouthExpr::Neutral;
};
FaceState emotionFace(Emotion e);
std::string_view toString(EyeExpr e);
std::string_view toString(MouthExpr e);

// Atlas layout. Face rectangles are in head units: x across the face,
// s = height above the chin, both scaled by the head height.
struct FaceLayout {
    static constexpr int kAtlasW = 1024, kAtlasH = 512;
    static constexpr int kEyeCellW = 256, kEyeCellH = 128, kEyeCols = 4;
    static constexpr int kMouthCellW = 128, kMouthCellH = 96, kMouthY = 256;
    static constexpr float kEyeX0 = -0.37f, kEyeX1 = 0.37f, kEyeS0 = 0.31f, kEyeS1 = 0.68f;
    static constexpr float kMouthX0 = -0.16f, kMouthX1 = 0.16f, kMouthS0 = 0.115f, kMouthS1 = 0.355f;

    // Pixel origin (top-left) of a cell.
    static std::array<int, 2> eyeCell(EyeExpr e) {
        int i = int(e);
        return {(i % kEyeCols) * kEyeCellW, (i / kEyeCols) * kEyeCellH};
    }
    static std::array<int, 2> mouthCell(MouthExpr m) { return {int(m) * kMouthCellW, kMouthY}; }
    // UV offset to add to the decal's base UVs (which point at the Neutral cell).
    static Vec2 eyeUvOffset(EyeExpr e) {
        auto c = eyeCell(e);
        return {float(c[0]) / kAtlasW, float(c[1]) / kAtlasH};
    }
    static Vec2 mouthUvOffset(MouthExpr m) {
        auto c = mouthCell(m);
        return {float(c[0]) / kAtlasW, float(c[1] - kMouthY) / kAtlasH};
    }
};

struct Character {
    Genome genome;
    Skeleton skeleton;
    RigInfo rig;
    PhysicsRig physics;
    Mesh mesh;
    Image faceAtlas;  // RGBA, FaceLayout::kAtlasW x kAtlasH
    float height = 0;
};

struct GenOptions {
    int lod = 0;            // 0 full, 1 reduced, 2 low
    bool faceted = true;        // flat facets on every surface (planes carry the anatomy); decals stay smooth
    float creaseAngle = 34.0f;  // when not faceted: smooth shading split at sharper edges; 180 = fully smooth
};

Character generateCharacter(const Genome& g, const GenOptions& opt = {});

// Paints the expression atlas for a genome (called by generateCharacter).
Image paintFaceAtlas(const Genome& g);

}  // namespace cg
