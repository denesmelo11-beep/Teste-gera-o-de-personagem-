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
    static constexpr float kEyeX0 = -0.37f, kEyeX1 = 0.37f, kEyeS0 = 0.245f, kEyeS1 = 0.615f;
    static constexpr float kMouthX0 = -0.16f, kMouthX1 = 0.16f, kMouthS0 = 0.07f, kMouthS1 = 0.31f;

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
    Mesh mesh;
    Image faceAtlas;  // RGBA, FaceLayout::kAtlasW x kAtlasH
    float height = 0;
};

struct GenOptions {
    int lod = 0;           // 0 full, 1 reduced, 2 low
    bool faceted = true;   // flat-shaded low-poly facets (false = smooth normals for anime cel shading)
};

Character generateCharacter(const Genome& g, const GenOptions& opt = {});

// Paints the expression atlas for a genome (called by generateCharacter).
Image paintFaceAtlas(const Genome& g);

}  // namespace cg
