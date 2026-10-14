// Internal building blocks shared by the generator translation units.
#pragma once
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

#include "chargen/character.hpp"
#include "chargen/rng.hpp"

namespace cg::detail {

// Up to 4 bone influences.
struct SkinW {
    int j[4] = {0, 0, 0, 0};
    float w[4] = {0, 0, 0, 0};
    void add(int bone, float wt);
    void normalize();
};
SkinW one(int bone);
SkinW blend(int a, int b, float t);  // (1-t)*a + t*b

struct Mat {
    Vec3 color{1, 1, 1};
    MaterialClass cls = MaterialClass::Skin;
};

using PaintFn = std::function<Mat(float t, float theta)>;

// Material region given by a signed field (< 0 inside). Triangles crossing the
// boundary are split along it, so painted edges stay clean at any angle.
struct ClipRegion {
    std::function<float(Vec3)> field;
    Mat m;
    int refine = 2;  // subdivision levels near the boundary before cutting (smoother curves, more triangles)
};
using WeightFn = std::function<SkinW(float t, float theta, Vec3 p)>;

// One primitive (a loft, an ellipsoid, a hair lock...). Triangles carry their
// own material so colour boundaries stay crisp after welding.
struct Part {
    std::vector<Vec3> pos;
    std::vector<SkinW> w;
    std::vector<Vec2> uv;
    struct Tri {
        uint32_t a, b, c;
        Mat m;
    };
    std::vector<Tri> tris;
    bool flat = false;
    bool smoothAlways = false;  // keep smooth normals even in faceted mode (head)
    SubmeshKind kind = SubmeshKind::Body;
    uint16_t id = 0;
    std::function<Vec3(Vec3 pos, Vec3 normal)> normalFn;  // optional normal edit (smooth parts)
    std::function<bool(Vec3 pos)> smoothAt;               // optional: never crease around these vertices
    float cavity = 0.0f;  // strength of curvature shading baked into vertex colour (grooves darker, ridges lighter)
    std::function<Mat(Vec3 centroid)> paintAt;            // optional: paint faces by position
    std::vector<ClipRegion> clips;                        // applied in order at finalize
};

// Monotone cubic (Fritsch-Carlson) curve through keys: smooth, no overshoot.
class Curve {
public:
    Curve() = default;
    Curve(std::initializer_list<std::pair<float, float>> keys);
    void key(float t, float v);  // keys must be added in increasing t
    float operator()(float t) const;

private:
    void prepare() const;
    std::vector<float> t_, v_;
    mutable std::vector<float> m_;
};

// Cross-section: superellipse with independent front/back depth & exponent.
// x runs along the ring's u axis, z along its v axis ("front").
struct SuperRing {
    float w = 1, zc = 0, dF = 1, dB = 1, nF = 2, nB = 2;
    Vec2 point(float theta) const;
    float frontZ(float x) const;
    float backZ(float x) const;
    // ~1 on the surface, <1 inside, >1 outside (radial scale factor).
    float field(float x, float z) const;
};

struct Loft {
    std::vector<std::vector<Vec3>> rings;  // equal sized, angles uniform from 0
    std::vector<float> t;                  // parameter per ring
    bool poleStart = false, poleEnd = false;
    Vec3 startPole, endPole;
    float tStart = 0, tEnd = 1;
};

// Ring point j = c + u*x + v*z for theta_j = 2*pi*j/n; requires u = cross(d, v)
// where d is the loft direction, so faces point outwards.
std::vector<Vec3> makeRing(Vec3 c, Vec3 u, Vec3 v, const SuperRing& sr, int n);

void emitLoft(Part& part, const Loft& loft, const WeightFn& wf, const PaintFn& pf);

// Quad strip between two closed loops of vertex indices of equal size; A is the
// loop nearer the start of the surface (same orientation rules as a loft).
void emitStrip(Part& part, const std::vector<uint32_t>& A, const std::vector<uint32_t>& B, const Mat& m);

// Builds an ellipsoid as a loft between two poles along `axis`.
struct Ellipsoid {
    Vec3 center;
    Vec3 axis{0, 0, 1}, up{0, 1, 0};  // pole axis and a reference perpendicular
    float rAxis = 1, rU = 1, rV = 1;  // radius along axis, along cross(axis,up), along up
    int lat = 6, lon = 10;
};
void emitEllipsoid(Part& part, const Ellipsoid& e, const WeightFn& wf, const PaintFn& pf);

// Welds all parts into the final mesh: flat facets, or smooth normals that keep a
// crisp crease wherever adjacent faces meet at more than creaseDeg.
void finalizeParts(std::vector<Part>& parts, Mesh& mesh, bool faceted, float creaseDeg);

}  // namespace cg::detail
