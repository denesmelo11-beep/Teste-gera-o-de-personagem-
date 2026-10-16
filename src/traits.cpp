// Composable species traits: wings, serpent and wraith lower bodies, back tentacles,
// turtle shells, crystal shards, trait hands and feet, beaks, mandibles, antennae,
// compound eyes, crests and fins. Any people can wear any of them; the trait
// peoples (genome.cpp) are presets built from them. Every piece is skinned to bones
// of its own, parented into the humanoid rig, so the animator can drive them (wings,
// pincers, the coil) and the spring physics moves the soft ones (tentacles, antennae).
#include <algorithm>
#include <array>

#include "generator_impl.hpp"

namespace cg::detail {

namespace {

// A thin sheet (wing membrane, feather, fin): built single-sided, emitted as two
// faces a hair apart so it reads from both sides with back-face culling.
struct Sheet {
    std::vector<Vec3> pos;
    std::vector<SkinW> w;
    struct Tri {
        uint32_t a, b, c;
        Mat m;
    };
    std::vector<Tri> tris;
    uint32_t add(Vec3 p, const SkinW& sw) {
        pos.push_back(p);
        w.push_back(sw);
        return uint32_t(pos.size() - 1);
    }
    void tri(uint32_t a, uint32_t b, uint32_t c, const Mat& m) { tris.push_back({a, b, c, m}); }
};

void emitSheet(Part& part, const Sheet& s, float thick, float backShade = 0.8f) {
    std::vector<Vec3> n(s.pos.size());
    for (const auto& t : s.tris) {
        const Vec3 fn = cross(s.pos[t.b] - s.pos[t.a], s.pos[t.c] - s.pos[t.a]);
        n[t.a] += fn, n[t.b] += fn, n[t.c] += fn;
    }
    const uint32_t front = uint32_t(part.pos.size());
    for (size_t i = 0; i < s.pos.size(); ++i) {
        part.pos.push_back(s.pos[i] + normalize(n[i], {0, 0, 1}) * (0.5f * thick));
        part.w.push_back(s.w[i]);
        part.uv.push_back({});
    }
    const uint32_t back = uint32_t(part.pos.size());
    for (size_t i = 0; i < s.pos.size(); ++i) {
        part.pos.push_back(s.pos[i] - normalize(n[i], {0, 0, 1}) * (0.5f * thick));
        part.w.push_back(s.w[i]);
        part.uv.push_back({});
    }
    for (const auto& t : s.tris) {
        part.tris.push_back({front + t.a, front + t.b, front + t.c, t.m});
        part.tris.push_back({back + t.a, back + t.c, back + t.b, Mat{t.m.color * backShade, t.m.cls}});
    }
}

// A fan from `center` over an open edge polyline, in `rings` radial steps. paint gets
// (r: 0 centre .. 1 edge, s: 0 .. 1 along the edge).
void fanSheet(Sheet& sh, Vec3 center, const std::vector<Vec3>& edge, int rings, const std::function<SkinW(Vec3)>& wf,
              const std::function<Mat(float r, float s)>& paint) {
    const uint32_t c = sh.add(center, wf(center));
    const size_t E = edge.size();
    std::vector<std::vector<uint32_t>> ids(static_cast<size_t>(rings));
    for (int k = 1; k <= rings; ++k)
        for (size_t e = 0; e < E; ++e) {
            const Vec3 p = lerp(center, edge[e], float(k) / float(rings));
            ids[size_t(k - 1)].push_back(sh.add(p, wf(p)));
        }
    for (size_t e = 0; e + 1 < E; ++e) {
        const float s = (float(e) + 0.5f) / float(E - 1);
        sh.tri(c, ids[0][e], ids[0][e + 1], paint(0.5f / float(rings), s));
        for (int k = 1; k < rings; ++k) {
            const float r = (float(k) + 0.5f) / float(rings);
            const uint32_t a = ids[size_t(k - 1)][e], b = ids[size_t(k)][e], cc = ids[size_t(k)][e + 1], d = ids[size_t(k - 1)][e + 1];
            const Mat m = paint(r, s);
            sh.tri(a, b, cc, m);
            sh.tri(a, cc, d, m);
        }
    }
}

// A tapered tube along a polyline (spars, claws, antennae, mandibles). rad(t) with t
// 0..1 along it; closed with a pointed tip.
void emitTube(Part& p, const std::vector<Vec3>& pts, const std::function<float(float)>& rad, int sides, const WeightFn& wf,
              const PaintFn& pf, float tipExtra = 0.0f) {
    if (pts.size() < 2) return;
    Loft L;
    const size_t n = pts.size();
    Vec3 vPrev = orthoFront(normalize(pts[1] - pts[0]));
    for (size_t k = 0; k < n; ++k) {
        const Vec3 d = normalize(pts[std::min(k + 1, n - 1)] - pts[k > 0 ? k - 1 : 0]);
        Vec3 v = normalize(vPrev - d * dot(vPrev, d), orthoFront(d));  // transported frame: no twist
        vPrev = v;
        const Vec3 u = cross(d, v);
        const float t = float(k) / float(n - 1), r = std::max(rad(t), 1e-4f);
        L.rings.push_back(makeRing(pts[k], u, v, SuperRing{r, 0, r, r}, sides));
        L.t.push_back(t);
    }
    L.poleEnd = true;
    L.endPole = pts.back() + normalize(pts[n - 1] - pts[n - 2]) * (rad(1.0f) + tipExtra);
    L.tEnd = 1;
    emitLoft(p, L, wf, pf);
}

// Catmull-Rom through control points, resampled at equal arc length.
std::vector<Vec3> smoothPath(const std::vector<Vec3>& c, int samples) {
    std::vector<Vec3> dense;
    const size_t n = c.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const Vec3 p0 = c[i > 0 ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, n - 1)];
        for (int k = 0; k < 16; ++k) {
            const float t = float(k) / 16.0f, t2 = t * t, t3 = t2 * t;
            dense.push_back((p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f);
        }
    }
    dense.push_back(c.back());
    std::vector<float> arc{0};
    for (size_t k = 1; k < dense.size(); ++k) arc.push_back(arc.back() + length(dense[k] - dense[k - 1]));
    std::vector<Vec3> out;
    size_t j = 1;
    for (int s = 0; s < samples; ++s) {
        const float a = arc.back() * float(s) / float(samples - 1);
        while (j + 1 < dense.size() && arc[j] < a) ++j;
        out.push_back(lerp(dense[j - 1], dense[j], saturate((a - arc[j - 1]) / std::max(1e-6f, arc[j] - arc[j - 1]))));
    }
    return out;
}

Vec3 pathAt(const std::vector<Vec3>& p, float u) {
    const float f = saturate(u) * float(p.size() - 1);
    const size_t i = std::min(size_t(f), p.size() - 2);
    return lerp(p[i], p[i + 1], f - float(i));
}
Vec3 pathDir(const std::vector<Vec3>& p, float u) {
    const float f = saturate(u) * float(p.size() - 1);
    const size_t i = std::min(size_t(f), p.size() - 2);
    return normalize(p[i + 1] - p[i]);
}

float hash01(uint64_t seed, uint64_t k) { return float(splitmix64(seed * 0x9E3779B97F4A7C15ull ^ (k * 0xBF58476D1CE4E5B9ull)) >> 40) / float(1ull << 24); }

// Weights along a bone chain from a parameter: f = u * bones, blending into the parent at the root.
SkinW chainAt(const std::vector<int>& bones, int parent, float u) {
    const int nb = int(bones.size());
    const float f = saturate(u) * float(nb);
    const int i0 = std::clamp(int(f), 0, nb - 1);
    if (u < 0.04f) return blend(parent, bones[0], u / 0.04f);
    if (i0 == nb - 1) return one(bones[size_t(i0)]);
    return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], smoothstep(0.35f, 1.0f, f - float(i0)));
}

const Mat kBoneM{{0.80f, 0.76f, 0.63f}, MaterialClass::Horn};

}  // namespace

// ---------------------------------------------------------------- wings

// Laid out in a 2D wing plane (a outwards, b up, in units of the span scale) behind
// the shoulder blades. Membrane: arm and finger spars with the skin stretched between
// them down to the small of the back, scalloped between the fingertips. Feathered:
// primaries on the hand, secondaries along the forearm, tertials at the elbow, with
// coverts over their roots. Insect: a fore and a hind pane, veined, glassy.
void Builder::buildWings() {
    const int kind = std::clamp(g.body.wings, 0, 3);
    if (!kind) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h, sz0 = 1.05f * g.body.wingSize * h * clamp(std::sqrt(g.body.headsTall / 7.0f), 0.75f, 1.15f);
    const bool folded = g.body.wingsFolded && kind != 3;
    const Vec3 wingC = bonesOnly_ ? kBoneM.color : g.palette.wing;
    const Mat clawM{g.palette.beak * 0.45f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    for (float side : {1.0f, -1.0f}) {
        const Vec3 root = torsoPoint(0.84f, 1.5f * kPi + side * 0.42f) + Vec3{0, 0, 0.02f * h};
        const Vec3 S = root + Vec3{side * 0.10f * h, 0.10f * h, -0.14f * h};
        // Short bodies get smaller wings: the lowest feather or fingertip (about 3 spans
        // below the shoulder) stays off the ground.
        const float sz = std::min(sz0, (S.y - 0.02f) / 3.8f);
        const Vec3 O = folded ? normalize(Vec3{side, 0.0f, -0.60f}) : normalize(Vec3{side, 0.0f, -0.30f});  // folded: flat over the back, compact
        const Vec3 U{0, 1, 0}, nrm = normalize(cross(O, U) * side);  // the wing plane's normal (towards the front)
        // Spread wings curve back as they reach out; folded ones lie along the back.
        auto P = [&](Vec2 q) { return S + O * (q.x * sz) + U * (q.y * sz) - Vec3{0, 0, 1} * (folded ? 0.0f : 0.07f * sz * q.x * q.x); };
        Vec2 E2, W2;
        std::vector<Vec2> tips;  // finger tips / pane outline anchors
        if (kind == 3) {
            E2 = {0.95f, 0.42f};
            W2 = {1.9f, 0.85f};
        } else if (folded) {
            E2 = {0.45f, 1.05f};
            W2 = {0.75f, -0.55f};
            tips = {{0.95f, -2.75f}, {0.55f, -2.95f}, {0.25f, -2.60f}, {0.05f, -2.05f}};
        } else {
            E2 = {1.05f, 0.95f};
            W2 = {2.25f, 1.45f};
            tips = {{4.55f, 1.15f}, {4.35f, -0.45f}, {3.40f, -1.85f}, {1.95f, -2.50f}};
        }
        const std::string suf = side > 0 ? ".L" : ".R";
        LimbRig w;
        w.side = side;
        w.root = sk.add("wing" + suf, r.chest, root);
        w.upper = sk.add("wingarm" + suf, w.root, S);
        w.lower = sk.add("wingfore" + suf, w.upper, P(E2));
        w.end = sk.add("winghand" + suf, w.lower, P(W2));
        w.restDir = normalize(P(E2) - S);
        r.wings.push_back(w);
        r.limits.push_back({w.root, false, {1, 0, 0}, 0.0f, 0.3f});
        r.limits.push_back({w.upper, false, {1, 0, 0}, 0.0f, 1.7f});
        r.limits.push_back({w.lower, false, {1, 0, 0}, 0.0f, 2.2f});
        r.limits.push_back({w.end, false, {1, 0, 0}, 0.0f, 1.4f});
        // Chain parameter of a point in the plane: 0..1 along the humerus, 1..2 along the
        // forearm, 2 at and beyond the wrist (the hand carries the fingers).
        auto param = [&](Vec2 q) {
            auto seg = [](Vec2 a, Vec2 b, Vec2 p, float& t) {
                const Vec2 ab = b - a;
                t = saturate(dot(p - a, ab) / std::max(1e-6f, dot(ab, ab)));
                return length(p - (a + ab * t));
            };
            float t1, t2;
            const float d1 = seg({0, 0}, E2, q, t1), d2 = seg(E2, W2, q, t2);
            return d1 < d2 ? t1 : 1.0f + t2;
        };
        const int R = w.root, Up = w.upper, Lo = w.lower, En = w.end;
        auto wf2 = [=](float pr) { return chainWeights({{0.04f, R}, {0.30f, Up}, {0.90f, Up}, {1.12f, Lo}, {1.80f, Lo}, {2.0f, En}}, pr); };
        // Inverse of P for weights of 3D sheet points: project back onto the plane axes.
        auto planeOf = [&](Vec3 p) {
            const Vec3 d = p - S;
            return Vec2{dot(d, O) / sz, dot(d, U) / sz};
        };
        auto wf3 = [&, wf2](Vec3 p) { return wf2(param(planeOf(p))); };
        Part& part = newPart(true);
        if (kind == 3) {  // ---- insect: two veined panes
            struct Pane {
                float ang, len, wid, drop;
            };
            const Pane panes[] = {{0.42f, 3.0f, 0.62f, 0.0f}, {-0.38f, 2.3f, 0.50f, -0.22f}};
            for (const Pane& pn : panes) {
                const Vec2 ax{std::cos(pn.ang), std::sin(pn.ang)}, nx{-ax.y, ax.x};
                const Vec2 base{0.05f, pn.drop};
                std::vector<Vec3> edge;
                const int n = 14;
                for (int k = 0; k <= n; ++k) {  // around the leaf: leading edge out, trailing edge back
                    const float s = float(k) / float(n), u = s < 0.5f ? 2.0f * s : 2.0f * (1.0f - s);
                    const float wv = pn.wid * std::pow(std::sin(kPi * std::pow(u, 0.75f)), 0.8f) * (s < 0.5f ? 0.55f : 1.0f);
                    const Vec2 q = base + ax * (u * pn.len) + nx * (s < 0.5f ? wv : -wv);
                    edge.push_back(P(q));
                }
                Sheet sh;
                const Vec3 pale = lerp(wingC, Vec3{0.85f, 0.88f, 0.90f}, 0.35f);
                fanSheet(sh, P(base), edge, 3, [&](Vec3 p) {
                    // Panes follow the chain by distance along their own axis.
                    const Vec2 q = planeOf(p) - base;
                    return wf2(2.0f * saturate(dot(q, ax) / pn.len) + 0.15f);
                }, [&](float rr, float s) {
                    // Glassy pane darkening to a smoky edge, with faint veins.
                    const int sector = int(s * 14.0f);
                    const float vein = sector % 4 == 1 ? 0.82f : 1.0f;
                    return Mat{lerp(pale, wingC * 0.55f, smoothstep(0.55f, 1.0f, rr)) * vein, MaterialClass::Metal};
                });
                emitSheet(part, sh, 0.004f * h, 0.85f);
            }
            continue;
        }
        // Arm spar: shoulder, elbow, wrist; then the hand.
        const float sparR = (kind == 2 ? 0.075f : 0.055f) * sz;
        const Mat sparM{kind == 2 ? wingC * 0.9f : lerp(wingC, g.palette.skin, 0.4f) * 0.85f, kind == 2 ? MaterialClass::Hair : MaterialClass::Skin};
        {
            std::vector<Vec3> pts;
            for (int k = 0; k <= 8; ++k) {
                const float f = float(k) / 8.0f;
                const Vec2 q = f < 0.5f ? lerp(Vec2{0, 0}, E2, 2.0f * f) : lerp(E2, W2, 2.0f * f - 1.0f);
                pts.push_back(P(q));
            }
            pts.insert(pts.begin(), root);
            emitTube(part, pts, [&](float t) { return sparR * (1.0f - 0.45f * t) * (1.0f + 0.35f * bump((t - 0.55f) / 0.12f)); }, 6,
                     [&](float t, float, Vec3 p) { return t < 0.08f ? blend(R, Up, t / 0.08f) : wf3(p); },
                     [&](float, float) { return bonesOnly_ ? kBoneM : sparM; }, 0.3f * sparR);
        }
        // Thumb claw at the wrist's leading edge.
        if (!bonesOnly_) emitSpike(part, P(W2), normalize(U + O * 0.4f + nrm * 0.2f), O, 0.30f * sz, 0.05f * sz, 0, one(En), clawM);
        if (kind == 1) {  // ---- membrane
            for (const Vec2& t2 : tips) {
                std::vector<Vec3> pts;
                for (int k = 0; k <= 5; ++k) pts.push_back(P(lerp(W2, t2, float(k) / 5.0f)));
                emitTube(part, pts, [&](float t) { return sparR * 0.55f * (1.0f - 0.8f * t); }, 5, [&](float, float, Vec3) { return one(En); },
                         [&](float, float) { return bonesOnly_ ? kBoneM : sparM; });
            }
            if (bonesOnly_) continue;
            // Edge: fingertips with scallops between them, down to the small of the back,
            // then along the flank back up to the shoulder and the elbow.
            const Vec2 B = folded ? Vec2{0.05f, -1.45f} : Vec2{0.18f, -1.75f};
            std::vector<Vec2> poly{tips[0]};
            for (size_t k = 1; k < tips.size(); ++k) {
                const Vec2 a = tips[k - 1], b = tips[k];
                for (float f : {0.33f, 0.66f}) poly.push_back(lerp(lerp(a, b, f), W2, 0.20f * std::sin(kPi * f) / 0.87f));
                poly.push_back(b);
            }
            for (float f : {0.5f}) poly.push_back(lerp(lerp(tips.back(), B, f), W2, 0.15f));
            poly.push_back(B);
            poly.push_back(lerp(lerp(B, Vec2{0, 0}, 0.5f), W2, -0.05f));
            poly.push_back({0.02f, 0.0f});
            poly.push_back(lerp(Vec2{0, 0}, E2, 0.5f));
            poly.push_back(E2);
            std::vector<Vec3> edge;
            for (Vec2 q : poly) edge.push_back(P(q));
            Sheet sh;
            const Vec3 mem = wingC;
            fanSheet(sh, P(W2), edge, 4, wf3, [&](float rr, float s) {
                // Darker towards the edge, faint bands between the fingers.
                const float band = 0.06f * float(int(s * 18.0f) % 2);
                return Mat{mem * (1.05f - 0.30f * rr - band), MaterialClass::Skin};
            });
            emitSheet(part, sh, 0.006f * h, 0.78f);
        } else {  // ---- feathered
            const Vec3 fc = wingC, tipC = wingC * 0.45f;
            struct Feather {
                Vec2 root;
                float ang, len, wid;
            };
            std::vector<Feather> fs;
            const Vec2 H2 = W2 + (folded ? Vec2{0.15f, -0.55f} : Vec2{0.85f, 0.05f});  // the hand's reach past the wrist
            const float down = folded ? -1.25f : 0.0f;  // folded: everything rotated to hang down the back
            for (int k = 0; k < 9; ++k) {  // primaries: long, fanning from along the hand down
                const float f = float(k) / 8.0f;
                fs.push_back({lerp(H2, W2, f), lerp(0.10f, -0.95f, f) + down, lerp(2.45f, 1.75f, f), 0.22f});
            }
            for (int k = 0; k < 9; ++k) {  // secondaries along the forearm
                const float f = float(k) / 8.0f;
                fs.push_back({lerp(W2, E2, f), lerp(-1.10f, -1.55f, f) + down * 0.6f, lerp(1.70f, 1.55f, f), 0.24f});
            }
            for (int k = 0; k < 4; ++k) {  // tertials towards the body
                const float f = float(k) / 3.0f;
                fs.push_back({lerp(E2, Vec2{0.15f, 0.05f}, f), lerp(-1.6f, -1.75f, f) + down * 0.4f, lerp(1.45f, 1.05f, f), 0.24f});
            }
            Sheet cov;
            {  // coverts over the feather roots: a fan from the wrist over the feathers' mid-points
                std::vector<Vec3> edge;
                for (const Feather& fe : fs) {
                    const Vec2 d{std::cos(fe.ang), std::sin(fe.ang)};
                    edge.push_back(P(fe.root + d * (0.42f * fe.len)));
                }
                edge.push_back(P({0.02f, 0.0f}));
                edge.push_back(P(lerp(Vec2{0, 0}, E2, 0.5f)));
                edge.push_back(P(E2));
                fanSheet(cov, P(W2), edge, 3, wf3, [&](float rr, float s) {
                    return Mat{fc * (1.02f - 0.10f * rr) * (0.94f + 0.06f * float(int(s * 30.0f) % 2)), MaterialClass::Hair};
                });
            }
            emitSheet(part, cov, 0.008f * h, 0.85f);
            for (size_t i = 0; i < fs.size(); ++i) {
                const Feather& fe = fs[i];
                const Vec2 d{std::cos(fe.ang), std::sin(fe.ang)}, nx{-d.y, d.x};
                const SkinW sw = wf2(param(fe.root));
                const float layer = (float(i % 9) * 0.006f + (i < 9 ? 0.0f : 0.012f)) * sz;  // shingled: each overlaps the next
                Sheet sh;
                const int segs = 4;
                std::vector<uint32_t> L, C, Rr;
                for (int k = 0; k <= segs; ++k) {
                    const float u = float(k) / float(segs);
                    const float wv = fe.wid * sz * std::pow(std::sin(kPi * std::min(0.5f + 0.5f * u, 0.98f)), 0.7f) * (u < 0.15f ? 0.6f : 1.0f);
                    const Vec3 c = P(fe.root + d * (u * fe.len)) + nrm * layer;
                    const Vec3 nn = P(fe.root + nx) - P(fe.root);
                    const Vec3 sideV = normalize(nn) * wv;
                    L.push_back(sh.add(c + sideV * 0.55f, sw));
                    C.push_back(sh.add(c + nrm * (0.01f * sz), sw));
                    Rr.push_back(sh.add(c - sideV, sw));
                }
                const uint32_t tip = sh.add(P(fe.root + d * (fe.len * 1.08f)) + nrm * layer, sw);
                const float shade = 0.92f + 0.08f * float(i % 2);
                for (int k = 0; k < segs; ++k) {
                    const float u = (float(k) + 0.5f) / float(segs);
                    const Mat mm{lerp(fc, tipC, smoothstep(0.55f, 0.95f, u)) * shade, MaterialClass::Hair};
                    sh.tri(L[size_t(k)], C[size_t(k)], C[size_t(k + 1)], mm), sh.tri(L[size_t(k)], C[size_t(k + 1)], L[size_t(k + 1)], mm);
                    sh.tri(C[size_t(k)], Rr[size_t(k)], Rr[size_t(k + 1)], mm), sh.tri(C[size_t(k)], Rr[size_t(k + 1)], C[size_t(k + 1)], mm);
                }
                const Mat tm{tipC * shade, MaterialClass::Hair};
                sh.tri(L.back(), C.back(), tip, tm), sh.tri(C.back(), Rr.back(), tip, tm);
                emitSheet(part, sh, 0.005f * h, 0.85f);
            }
        }
    }
}

// ---------------------------------------------------------------- serpent coil / wraith wisp

// Replaces the legs: the torso's hip ring flows into a long scaled coil that drops to
// the ground and trails behind in an S (naga), or into a tapering, ragged wisp that
// fades to shadow (wraith). The leg bones stay in the skeleton (unskinned) so every
// clip and engine query keeps working.
void Builder::buildSerpent() {
    const int kind = std::clamp(g.body.lowerBody, 0, 3);
    if (!kind) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    const int N = lod.torsoSeg;
    const float t0 = kTorsoBase;
    // Shape of the torso's bottom ring (built flat for these bodies).
    Vec3 c0{};
    std::vector<Vec3> ring0(static_cast<size_t>(N));
    for (int j = 0; j < N; ++j) ring0[size_t(j)] = torsoPoint(t0, warpTheta(kTau * float(j) / float(N))), c0 += ring0[size_t(j)];
    c0 = c0 / float(N);
    float rT = 0;
    for (const Vec3& p : ring0) rT += length(Vec3{p.x - c0.x, 0, p.z - c0.z});
    rT /= float(N);
    const float L = g.body.serpentLength;
    const uint64_t sd = g.seed;
    const float flip = hash01(sd, 5) < 0.5f ? 1.0f : -1.0f;
    std::vector<Vec3> ctrl;
    float R0;
    if (kind == 1) {
        R0 = clamp(0.95f * rT, 0.30f * h, 0.75f * h);
        ctrl = {c0, Vec3{0, lerp(c0.y, R0, 0.55f), c0.z + 0.22f * h}, Vec3{0, R0 * 1.02f, c0.z + 0.02f * h}};
        for (int k = 1; k <= 5; ++k)
            ctrl.push_back({flip * std::sin(float(k) * 1.25f + 0.4f) * 0.95f * h * L, R0 * (1.0f - 0.05f * float(k)), c0.z - float(k) * 1.30f * h * L});
        const Vec3 last = ctrl.back();
        ctrl.push_back(last + Vec3{-flip * 0.45f * h, 0.30f * h, -0.85f * h * L});
    } else if (kind == 3) {  // ooze: straight down into a spreading mound
        R0 = rT;
        ctrl = {c0, Vec3{0, c0.y * 0.62f, c0.z + 0.03f * h}, Vec3{0, c0.y * 0.28f, c0.z + 0.06f * h}, Vec3{0, 0.0015f, c0.z + 0.05f * h}};
    } else {
        R0 = 0.92f * rT;
        ctrl = {c0, Vec3{0, c0.y * 0.58f, c0.z - 0.12f * h}, Vec3{flip * 0.12f * h, c0.y * 0.26f, c0.z - 0.50f * h * L},
                Vec3{-flip * 0.08f * h, 0.06f * h, c0.z - 1.05f * h * L}, Vec3{flip * 0.05f * h, 0.012f, c0.z - 1.45f * h * L}};
    }
    const std::vector<Vec3> path = smoothPath(ctrl, 96);
    auto radius = [&](float u) {
        if (kind == 1) return R0 * (1.0f - 0.86f * smoothstep(0.30f, 1.0f, u)) * (1.0f + 0.05f * bump((u - 0.15f) / 0.12f));
        if (kind == 3)  // a sagging belly of goo, then a wide pool
            return R0 * (1.0f + 0.60f * std::pow(std::max(0.0f, std::sin(kPi * std::min(u / 0.75f, 1.0f))), 1.5f) + 1.6f * g.body.serpentLength * std::pow(smoothstep(0.50f, 1.0f, u), 1.3f));
        return R0 * (1.0f - 0.92f * std::pow(u, 0.75f));
    };
    // Bones along the path.
    const int nb = kind == 1 ? 8 : kind == 3 ? 3 : 4;
    int parent = r.hips;
    for (int b = 0; b < nb; ++b) {
        parent = sk.add((kind == 1 ? "coil" : kind == 3 ? "ooze" : "wisp") + std::to_string(b), parent, pathAt(path, 0.03f + 0.95f * float(b) / float(nb)));
        r.serpent.push_back(parent);
        r.limits.push_back({parent, false, {1, 0, 0}, 0.0f, kind == 1 ? 0.7f : 0.9f});
    }
    const std::vector<int> bones = r.serpent;
    const int hips = r.hips;
    auto tw = [bones, hips](float u) { return chainAt(bones, hips, (u - 0.03f) / 0.95f + 0.0f); };
    if (bonesOnly_ && kind == 1) {  // the coil's vertebrae, lying on the ground
        Part& vp = newPart();
        for (int i = 0; i < 40; ++i) {
            const float u = (float(i) + 0.5f) / 40.0f;
            Ellipsoid e;
            e.center = pathAt(path, u) - Vec3{0, 0.68f * radius(u) * smoothstep(0.10f, 0.22f, u), 0};
            e.axis = pathDir(path, u);
            e.up = {0, 1, 0};
            e.rAxis = 0.42f * length(path.back() - path.front()) / 40.0f + 0.02f * h;
            e.rU = e.rV = std::max(0.30f * radius(u), 0.02f * h);
            e.center.y = std::max(e.center.y, e.rV + 0.003f);
            e.lat = 3;
            e.lon = 5;
            emitEllipsoid(vp, e, [=](float, float, Vec3) { return tw(u); }, [=](float, float) { return kBoneM; });
        }
        return;
    }
    // Rings from the tip up to the hips (the loft runs towards the torso, like the torso does).
    std::vector<float> us;
    const int nr = lod.torsoSeg >= 16 ? 24 : lod.torsoSeg >= 12 ? 16 : 11;
    for (int i = nr; i >= 1; --i) us.push_back(std::pow(float(i) / float(nr), 1.15f) * 0.995f);
    us.back() = std::min(us.back(), 0.035f);
    Loft Lf;
    for (float u : us) {
        const Vec3 c = pathAt(path, u), d = -pathDir(path, u);
        const Vec3 ux = normalize(Vec3{1, 0, 0} - d * d.x, {1, 0, 0}), v = cross(ux, d);
        const float rr = radius(u), k = smoothstep(0.0f, 0.16f, u);
        const float ground = kind == 1 ? smoothstep(0.10f, 0.22f, u) : 0.0f;
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) {
            const Vec3 o = ring0[size_t(j)] - c0;
            const float ang = std::atan2(o.z, o.x);
            float ox = lerp(o.x, rr * std::cos(ang), k), oz = lerp(o.z, rr * std::sin(ang), k);
            if (oz > 0) oz *= 1.0f - 0.22f * ground;  // a flatter belly on the ground
            if (kind == 2) {  // ragged, uneven folds
                const float rag = 1.0f + 0.30f * u * std::sin(5.0f * ang + 7.0f * u + float(sd % 7));
                ox *= rag, oz *= rag;
            } else if (kind == 3) {  // lumpy, sagging lobes
                const float lump = 1.0f + 0.16f * smoothstep(0.15f, 0.8f, u) * std::sin(3.0f * ang + 4.0f * u + float(sd % 11)) +
                                   0.07f * smoothstep(0.35f, 0.9f, u) * std::sin(9.0f * ang + float(sd % 5));  // drip ridges
                ox *= lump, oz *= lump;
            }
            Vec3 p = c + ux * ox + v * oz;
            p.y = std::max(p.y, 0.0015f);
            ring[size_t(j)] = p;
        }
        Lf.rings.push_back(ring);
        Lf.t.push_back(u);
    }
    Lf.poleStart = true;
    Lf.startPole = path.back() + pathDir(path, 1.0f) * (kind == 1 ? 0.6f * radius(1.0f) : kind == 3 ? 0.0f : 0.05f * h);
    Lf.startPole.y = std::max(Lf.startPole.y, 0.0015f);
    Lf.tStart = 1.0f;
    const Vec3 skinC = g.palette.skin, bellyC = g.palette.belly, shadow = Vec3{0.05f, 0.05f, 0.06f};
    PaintFn pf;
    if (kind == 1)
        pf = [=, this](float u, float th) {
            const float s = std::sin(warpTheta(th));
            if (s > 0.62f) return Mat{bellyC * (0.90f + 0.10f * float(int(u * 70.0f) % 2)), MaterialClass::Skin};  // belly scutes
            const float stripe = s < -0.75f ? 0.78f : 1.0f;  // a darker dorsal stripe
            return Mat{skinC * stripe, MaterialClass::Skin};
        };
    else if (kind == 3)
        pf = [=](float u, float th) {  // darker and denser towards the ground, a glossy sheen in streaks
            const float streak = 0.92f + 0.10f * std::sin(6.0f * th + 9.0f * u);
            return Mat{lerp(skinC, bellyC, smoothstep(0.3f, 0.95f, u)) * streak, MaterialClass::Skin};
        };
    else
        pf = [=](float u, float) {
            const float f = smoothstep(0.05f, 0.75f, u);
            return Mat{lerp(skinC * 0.8f, shadow, f), f > 0.5f ? MaterialClass::Dark : MaterialClass::Cloth};
        };
    if (bonesOnly_) {  // a wraith is no flesh: its wisp outlasts the bones, closed under the pelvis
        Lf.poleEnd = true;
        Lf.endPole = c0 + Vec3{0, 0.05f * h, 0};
        Lf.tEnd = 0.0f;
        emitLoft(newPart(), Lf, [=](float u, float, Vec3) { return tw(u); }, pf);
        return;
    }
    Part& tp = parts[torsoPart_];
    const uint32_t base = uint32_t(tp.pos.size());
    emitLoft(tp, Lf, [=](float u, float, Vec3) { return tw(u); }, pf);
    std::vector<uint32_t> top;
    for (int j = 0; j < N; ++j) top.push_back(base + uint32_t((us.size() - 1) * size_t(N) + size_t(j)));
    emitStrip(tp, top, torsoRing0_, pf(0.01f, 1.5f * kPi));
    if (kind == 3) {  // a few droplets shed around the mound
        Part& dp = newPart();
        for (int k = 0; k < 6; ++k) {
            const float a = kTau * (float(k) + hash01(sd, 40 + uint64_t(k))) / 6.0f;
            const float rr = radius(0.97f) * (1.15f + 0.35f * hash01(sd, 50 + uint64_t(k)));
            const float sz = (0.04f + 0.05f * hash01(sd, 60 + uint64_t(k))) * h;
            Ellipsoid e;
            e.center = path.back() + Vec3{rr * std::cos(a), 0.45f * sz, rr * std::sin(a)};
            e.axis = {0, 1, 0};
            e.up = {0, 0, 1};
            e.rAxis = 0.45f * sz;
            e.rU = e.rV = sz;
            e.lat = 3;
            e.lon = 6;
            emitEllipsoid(dp, e, [=](float, float, Vec3) { return one(bones.back()); }, [=](float, float) { return Mat{bellyC, MaterialClass::Skin}; });
        }
        return;
    }
    if (kind == 1) {  // the tip sways
        SpringChain sc;
        sc.name = "coiltip";
        sc.bones = {bones[size_t(nb - 3)], bones[size_t(nb - 2)], bones[size_t(nb - 1)]};
        sc.tail = Lf.startPole - sk.bones[size_t(bones.back())].restWorld;
        sc.stiffness = 1.6f;
        sc.drag = 0.4f;
        sc.gravity = 0.1f;
        sc.hitRadius = 0.3f * R0;
        sc.maxAngle = 0.6f;
        ch.physics.chains.push_back(sc);
    } else {
        SpringChain sc;
        sc.name = "wisp";
        sc.bones = {bones[size_t(nb - 2)], bones[size_t(nb - 1)]};
        sc.tail = Lf.startPole - sk.bones[size_t(bones.back())].restWorld;
        sc.stiffness = 0.8f;
        sc.drag = 0.3f;
        sc.gravity = 0.05f;
        sc.hitRadius = 0.2f * R0;
        sc.maxAngle = 0.8f;
        ch.physics.chains.push_back(sc);
    }
}

// ---------------------------------------------------------------- back tentacles

void Builder::buildBackTentacles() {
    const int n = std::clamp(g.body.backTentacles, 0, 6);
    if (!n) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    const Vec3 skinC = bonesOnly_ ? kBoneM.color : g.palette.skin, sucker = g.palette.belly;
    for (int k = 0; k < n; ++k) {
        const float side = k % 2 ? -1.0f : 1.0f, row = float(k / 2);
        const float t = 0.88f - 0.13f * row;
        const Vec3 root = torsoPoint(t, 1.5f * kPi + side * (0.28f + 0.10f * row)) + Vec3{0, 0, 0.03f * h};
        const float len = (2.6f - 0.3f * row) * h * g.body.tentacleLength;
        const float r0 = (0.15f - 0.015f * row) * h * (0.85f + 0.25f * g.body.muscle);
        // Rest shape: out and back from the shoulder blades, then drooping in a soft S.
        std::vector<Vec3> ctrl{root};
        Vec3 p = root, dir = normalize(Vec3{side * 1.0f, 0.70f - 0.40f * row, -0.8f});
        const float seg = len / 6.0f;
        for (int i = 0; i < 6; ++i) {
            const float u = float(i + 1) / 6.0f;
            dir = normalize(dir + Vec3{side * (0.12f + 0.10f * std::sin(u * 5.0f + row)), -0.30f, -0.05f});
            p += dir * seg;
            p.y = std::max(p.y, 0.05f * h);
            ctrl.push_back(p);
        }
        const std::vector<Vec3> pts = smoothPath(ctrl, 14);
        std::vector<int> bones;
        int parent = r.chest;
        for (int b = 0; b < 5; ++b) {
            parent = sk.add("tentacle" + std::to_string(k) + "_" + std::to_string(b), parent, pathAt(pts, 0.92f * float(b) / 5.0f));
            bones.push_back(parent);
        }
        r.tentacles.push_back(bones);
        const int chest = r.chest;
        Part& tp = newPart();
        emitTube(tp, pts, [=](float u) { return r0 * (1.0f - 0.88f * std::pow(u, 0.9f)) * (1.0f + 0.12f * bump(u / 0.12f)); },
                 std::max(5, lod.limbSeg - 1), [=](float u, float, Vec3) { return chainAt(bones, chest, u); },
                 [=, this](float u, float th) {
                     // Suckers on the underside: pale dots in pairs down the length.
                     if (std::sin(th) > 0.45f && int(u * 22.0f) % 2 == 0 && u > 0.08f) return Mat{sucker, MaterialClass::Skin};
                     return Mat{skinC * (1.0f - 0.25f * u), bonesOnly_ ? MaterialClass::Horn : MaterialClass::Skin};
                 });
        SpringChain sc;
        sc.name = "tentacle" + std::to_string(k);
        sc.bones = bones;
        sc.tail = pts.back() - sk.bones[size_t(bones.back())].restWorld;
        sc.stiffness = 0.7f;
        sc.drag = 0.35f;
        sc.gravity = 0.25f;
        sc.hitRadius = 0.6f * r0;
        sc.maxAngle = 1.5f;
        ch.physics.chains.push_back(sc);
    }
}

// ---------------------------------------------------------------- shell & shards

// Turtle shell: a domed carapace on the back, its scutes in a vertebral row, costal
// rows and a pale marginal rim; a flat plastron over the belly.
void Builder::buildShell() {
    if (!g.body.shell || beast_) return;
    const RigInfo& r = ch.rig;
    const float h = m.h;
    const SuperRing mid = torsoRing(0.62f);
    const float halfW = std::max(cHw_(0.70f), cHw_(0.30f)) * 1.12f;
    const float y0 = m.crotchY + 0.16f * m.torsoLen, y1 = m.crotchY + 0.98f * m.torsoLen;
    const Vec3 sc = bonesOnly_ ? kBoneM.color : g.palette.horn;
    const int chest = r.chest, spine = r.spine, hips = r.hips;
    const float cy = 0.5f * (y0 + y1);
    auto wf = [=, this](float, float, Vec3 p) {
        return chainWeights({{0.20f, hips}, {0.42f, spine}, {0.66f, chest}}, torsoT(p.y));
    };
    {
        // A dome standing proud of the back, wrapping the flanks; its buried front half
        // stays inside the torso.
        const float zb = mid.zc - mid.dB - (0.30f * h + 0.10f * mid.dB), zf = mid.zc + 0.30f * mid.dF;
        Ellipsoid e;
        e.center = {0, cy, 0.5f * (zb + zf)};
        e.axis = {0, 0, 1};  // the start pole (t = 0) is the top of the dome
        e.up = {0, 1, 0};
        e.rAxis = 0.5f * (zf - zb);
        e.rU = halfW;
        e.rV = 0.5f * (y1 - y0);
        e.lat = 7;
        e.lon = 14;
        const Vec3 bridge = g.palette.belly * 0.8f;
        Part& p = newPart();
        emitEllipsoid(p, e, wf, [=](float t, float th) {
            const float s = std::cos(th);
            const bool vertebral = std::fabs(s) < 0.30f && t < 0.40f;
            const int row = int(t * 7.0f), col = int((th / kTau) * 7.0f);
            if (t > 0.58f) return Mat{bridge, MaterialClass::Horn};  // the underside, where it meets the plastron
            if (t > 0.40f) return Mat{lerp(sc, Vec3{0.85f, 0.78f, 0.60f}, 0.35f) * (0.92f + 0.08f * float(col % 2)), MaterialClass::Horn};  // marginal rim
            const float shade = vertebral ? 0.80f : 0.92f + 0.10f * float((row + col) % 2);
            return Mat{sc * shade, MaterialClass::Horn};
        });
    }
    if (bonesOnly_) return;
    {  // plastron
        const SuperRing fr = torsoRing(0.40f);
        Ellipsoid e;
        e.center = {0, m.crotchY + 0.45f * m.torsoLen, fr.zc + fr.dF * 0.55f};
        e.axis = {0, 0, 1};
        e.up = {0, 1, 0};
        e.rAxis = fr.dF * 0.55f + 0.06f * h;
        e.rU = 0.80f * cHw_(0.42f);
        e.rV = 0.34f * m.torsoLen;
        e.lat = 4;
        e.lon = 12;
        const Vec3 bc = g.palette.belly;
        emitEllipsoid(newPart(), e, wf, [=](float t, float th) {
            const int band = int((0.5f + 0.5f * std::sin(th)) * 5.0f);
            return Mat{bc * (t < 0.3f ? 0.95f - 0.06f * float(band % 2) : 0.9f), MaterialClass::Horn};
        });
    }
}

// Crystal shards: clusters of hexagonal crystals breaking out of the shoulders, the
// upper back and the forearms.
void Builder::buildShards() {
    const int n = std::clamp(g.body.shards, 0, 24);
    if (!n || bonesOnly_) return;
    const RigInfo& r = ch.rig;
    const float h = m.h;
    const Vec3 cc = g.body.crystalColor;
    Part& p = newPart(true);
    auto crystal = [&](Vec3 base, Vec3 dir, float len, float rad, const SkinW& w, int k) {
        dir = normalize(dir);
        const Vec3 v = orthoFront(dir), u = cross(dir, v);
        Loft L;
        for (float t : {-0.15f, 0.62f}) {
            L.rings.push_back(makeRing(base + dir * (t * len), u, v, SuperRing{rad, 0, rad, rad}, 6));
            L.t.push_back(t);
        }
        L.poleEnd = true;
        L.endPole = base + dir * len;
        L.tEnd = 1;
        const float tone = 0.85f + 0.3f * hash01(g.seed, uint64_t(k) * 7 + 3);
        const Vec3 col{std::min(cc.x * tone, 1.0f), std::min(cc.y * tone, 1.0f), std::min(cc.z * tone, 1.0f)};
        emitLoft(p, L, [=](float, float, Vec3) { return w; }, [=](float t, float th) {
            return Mat{t > 0.65f ? lerp(col, Vec3{1, 1, 1}, 0.35f) : col * (0.82f + 0.18f * float(int(th * 3.0f / kPi) % 2)),
                       t > 0.65f ? MaterialClass::Glow : MaterialClass::Metal};
        });
    };
    for (int k = 0; k < n; ++k) {
        const float a = hash01(g.seed, uint64_t(k) * 3), b = hash01(g.seed, uint64_t(k) * 3 + 1), c = hash01(g.seed, uint64_t(k) * 3 + 2);
        const int site = k % 4;
        if (site < 2 && !m.arms.empty() && !beast_) {  // shoulders (site 0) and forearms (site 1)
            const ArmMarks& am = m.arms[size_t((k / 4) % 2)];
            const LimbRig& lr = r.arms[size_t((k / 4) % 2)];
            const Vec3 at = site == 0 ? lerp(am.shoulder, am.elbow, 0.10f + 0.20f * a) : lerp(am.elbow, am.wrist, 0.25f + 0.5f * a);
            const Vec3 out = normalize(Vec3{am.side, site == 0 ? 0.9f : 0.1f, -0.4f} + Vec3{0, 0, 0.6f * (b - 0.5f)});
            const float rad = am.radius * (site == 0 ? 1.0f : 0.85f);
            crystal(at + out * (0.6f * rad), out, (site == 0 ? 0.55f : 0.38f) * h * (0.7f + 0.6f * c), (0.07f + 0.04f * c) * h,
                    one(site == 0 ? lr.upper : lr.lower), k);
        } else {  // upper back
            const float t = lerp(0.62f, 0.95f, a), th = 1.5f * kPi + (b - 0.5f) * 1.4f;
            const Vec3 q = torsoPoint(t, th);
            const Vec3 out = beast_ ? normalize(Vec3{(b - 0.5f) * 1.6f, 1.0f, -0.45f - 0.3f * c}) : normalize(Vec3{(b - 0.5f) * 1.2f, 0.45f + 0.3f * c, -1.0f});
            crystal(q - out * (0.02f * h), out, (0.45f + 0.45f * c) * h, (0.08f + 0.05f * c) * h,
                    chainWeights({{0.40f, r.spine}, {0.66f, r.chest}}, t), k);
        }
    }
}

// ---------------------------------------------------------------- trait hands & feet

// 1 talons: three heavy clawed fingers and an opposed clawed thumb. 2 crab pincer: a
// swollen claw (propodus) with a fixed finger and a movable one on its own bone.
// 3 tendrils: a small palm splitting into five boneless, curling tendrils.
void Builder::buildTraitHand(const ArmMarks& a, const LimbRig& rig) {
    const int kind = g.body.hands;
    const float Lh = length(a.handTip - a.wrist);
    const Vec3 d = a.dir, v = orthoFront(d), u = cross(d, v);
    const Vec3 palm = u * a.side;
    const float hw = std::max(0.25f * Lh, a.radius * 0.78f) * (1 + 0.12f * g.body.muscle);
    const float th = std::max(0.13f * Lh, a.radius * 0.55f);
    const int hand = rig.end;
    const Mat clawM{g.palette.beak * 0.42f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    const size_t armIndex = size_t(&a - m.arms.data());
    auto handW = [=](float, float, Vec3) { return one(hand); };
    auto palmBlock = [&](float tEnd, float wMul) {
        Loft L;
        const float ts[] = {-0.05f, 0.18f, 0.40f, tEnd};
        const float ws[] = {0.62f, 0.92f, 1.0f, 0.95f};
        for (int k = 0; k < 4; ++k) {
            const Vec3 c = a.wrist + d * (ts[k] * Lh);
            L.rings.push_back(makeRing(c, u, v, SuperRing{th * (k ? 1.0f : 0.85f), 0, hw * ws[k] * wMul, hw * ws[k] * wMul, 2.4f, 2.4f}, lod.handSeg));
            L.t.push_back(ts[k]);
        }
        L.poleEnd = true;
        L.endPole = a.wrist + d * ((tEnd + 0.04f) * Lh);
        L.tEnd = 1;
        Part& hp = newPart();
        hp.id = armGroup_;
        emitLoft(hp, L, handW, [&](float, float) { return skin(); });
    };
    if (kind == 2) {  // ---- crab pincer
        while (ch.rig.pincers.size() < m.arms.size()) ch.rig.pincers.push_back(-1);
        const float big = 1.25f;  // the claw outweighs a hand
        const Vec3 shellC = g.body.skinKind == 2 ? g.body.carapaceColor * 1.4f : lerp(g.palette.skin, g.palette.horn, 0.5f);
        const Mat shellM{shellC, MaterialClass::Horn};
        Part& cp = newPart();
        cp.id = armGroup_;
        Ellipsoid e;  // the palm of the claw
        e.center = a.wrist + d * (0.42f * Lh * big);
        e.axis = d;
        e.up = v;
        e.rAxis = 0.55f * Lh * big;
        e.rU = 1.05f * hw * big;
        e.rV = 0.95f * hw * big;
        e.lat = 5;
        e.lon = 9;
        emitEllipsoid(cp, e, handW, [=](float t, float) { return Mat{shellC * (t > 0.7f ? 0.85f : 1.0f), MaterialClass::Horn}; });
        // Fixed finger (pollex) from the lower edge; the movable dactyl on its own bone from the upper.
        const Vec3 tipBase = a.wrist + d * (0.88f * Lh * big);
        const Vec3 lo = tipBase - u * a.side * (0.45f * hw * big), hi = tipBase + u * a.side * (0.50f * hw * big);
        const int dactyl = ch.skeleton.add(std::string("pincer") + (a.pair ? "2" : "") + (a.side > 0 ? ".L" : ".R"), hand, hi);
        ch.rig.pincers[armIndex] = dactyl;
        ch.rig.limits.push_back({dactyl, true, normalize(v), -0.05f, 0.9f});
        auto finger = [&](Vec3 base, float curlSign, int bone, float len) {
            std::vector<Vec3> pts{base};
            Vec3 dd = normalize(d + palm * (0.25f * curlSign));
            Vec3 p = base;
            for (int k = 0; k < 4; ++k) {
                dd = normalize(dd - palm * (0.22f * curlSign));
                p += dd * (len / 4.0f);
                pts.push_back(p);
            }
            emitTube(cp, pts, [&](float t) { return 0.36f * hw * big * (1.0f - 0.85f * t); }, 5, [=](float, float, Vec3) { return one(bone); },
                     [=](float t, float) { return t > 0.75f ? clawM : shellM; });
            for (int k = 1; k <= 2; ++k)  // blunt teeth on the cutting edge
                emitSpike(cp, lerp(pts[1], pts[3], 0.5f * float(k)), -palm * curlSign, d, 0.12f * hw * big, 0.06f * hw * big, 2, one(bone), clawM);
        };
        finger(lo, -1.0f, hand, 0.85f * Lh * big);
        finger(hi, 1.0f, dactyl, 0.95f * Lh * big);
        return;
    }
    if (kind == 3) {  // ---- tendrils
        palmBlock(0.42f, 0.9f);
        Part& tp = newPart();
        tp.id = armGroup_;
        for (int f = 0; f < 5; ++f) {
            const float off = (2.0f - float(f)) / 2.0f;
            Vec3 p = a.wrist + d * (0.40f * Lh) + v * (off * 0.65f * hw) + palm * (0.1f * th);
            Vec3 fd = normalize(d + v * (off * 0.25f));
            std::vector<Vec3> pts{p};
            const float len = (1.25f - 0.15f * std::fabs(off)) * Lh;
            for (int k = 0; k < 6; ++k) {
                const float c = float(k) / 6.0f;
                fd = normalize(fd + palm * (0.14f + 0.25f * c) + v * (0.10f * off * std::sin(c * 4.0f)));
                p += fd * (len / 6.0f);
                pts.push_back(p);
            }
            const float fr = std::min(0.23f * hw, 0.45f * th);
            emitTube(tp, pts, [=](float t) { return fr * (1.0f - 0.85f * t); }, 5, handW, [&](float t, float) {
                return Mat{lerp(g.palette.skin, g.palette.belly, 0.5f * t), MaterialClass::Skin};
            });
        }
        return;
    }
    if (kind == 4) {  // ---- digging spades: a broad, flat palm and five shovel claws fanned out
        palmBlock(0.50f, 1.30f);
        Part& sp = newPart();
        sp.id = armGroup_;
        const Mat spadeM{g.palette.beak * 0.9f, MaterialClass::Horn};
        for (int f = 0; f < 5; ++f) {
            const float off = (2.0f - float(f)) / 2.0f;  // -1 .. 1 across the palm
            Vec3 fd = normalize(d + v * (0.22f * off) + palm * 0.10f), p = a.wrist + d * (0.46f * Lh) + v * (off * 0.90f * hw);
            const float len = (0.62f - 0.12f * std::fabs(off)) * Lh, wid = 0.46f * hw, thick = 0.30f * th;
            Loft L;
            const int n = 5;
            for (int k = 0; k <= n; ++k) {
                const float t = float(k) / float(n);
                const Vec3 vv = normalize(v - fd * dot(v, fd), orthoFront(fd)), uu = cross(fd, vv);
                const float taper = 1.0f - 0.55f * t * t;
                L.rings.push_back(makeRing(p, uu, vv, SuperRing{thick * (1.0f - 0.6f * t), 0, wid * taper, wid * taper, 2.6f, 2.6f}, 6));
                L.t.push_back(t);
                fd = normalize(fd + palm * 0.10f);
                p += fd * (len / float(n));
            }
            L.poleEnd = true;
            L.endPole = p + fd * (0.15f * wid);
            L.tEnd = 1;
            emitLoft(sp, L, handW, [=, this](float t, float) { return t < 0.18f ? skin() : Mat{spadeM.color * (t > 0.8f ? 0.75f : 1.0f), MaterialClass::Horn}; });
        }
        return;
    }
    // ---- talons
    palmBlock(0.50f, 1.0f);
    Part& fp = newPart();
    fp.id = armGroup_;
    auto clawFinger = [&](Vec3 base, Vec3 dir0, float len, float fr) {
        std::vector<Vec3> pts{base};
        Vec3 fd = normalize(dir0), p = base;
        for (int k = 0; k < 3; ++k) {
            fd = normalize(fd + palm * (0.24f + 0.08f * float(k)));
            p += fd * (len / 3.0f);
            pts.push_back(p);
        }
        emitTube(fp, pts, [=](float t) { return fr * (1.0f - 0.25f * t); }, 6, handW, [&](float, float) { return skin(); });
        // A long hooked claw.
        std::vector<Vec3> cpts{p - fd * (0.4f * fr)};
        Vec3 cd = fd, q = cpts[0];
        for (int k = 0; k < 3; ++k) {
            cd = normalize(cd + palm * 0.45f);
            q += cd * (0.9f * fr);
            cpts.push_back(q);
        }
        emitTube(fp, cpts, [=](float t) { return 0.62f * fr * (1.0f - 0.9f * t); }, 5, handW, [&](float, float) { return clawM; });
    };
    for (int f = 0; f < 3; ++f) {
        const float off = (1.0f - float(f));
        clawFinger(a.wrist + d * (0.47f * Lh) + v * (off * 0.62f * hw), d + v * (off * 0.12f), 0.52f * Lh, std::min(0.30f * hw, 0.55f * th));
    }
    clawFinger(a.wrist + d * (0.22f * Lh) + v * (0.62f * hw) + palm * (0.3f * th), d * 0.55f + v * 0.45f + palm * 0.55f, 0.40f * Lh,
               std::min(0.30f * hw, 0.55f * th));
}

// Bird talons: a scaled tarsus to the ball of the foot, three toes forward and one
// back, each ending in a hooked talon. Works on plantigrade and digitigrade legs.
void Builder::buildTalonFeet() {
    const float h = m.h, fs = g.body.footSize, Cf = g.body.calf * h * (1 + 0.2f * g.body.muscle);
    const Vec3 scaleC = g.palette.beak * 0.85f;
    const Mat legM{scaleC, MaterialClass::Leather};
    const Mat clawM{g.palette.beak * 0.30f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const int foot = rig.end, toe = rig.tip;
        const Vec3 an = m.ankle[s];
        const Vec3 bl{m.ball[s].x, 0.075f * h * fs, m.ball[s].z};
        Part& p = newPart();
        {  // tarsus, from inside the end of the shin
            const std::vector<Vec3> pts{lerp(an, bl, -0.18f), an, lerp(an, bl, 0.55f), bl};
            const float rs = (g.body.digitigrade ? 0.62f : 0.50f) * Cf;
            emitTube(p, pts, [=](float t) { return rs * (1.0f - 0.35f * t); }, lod.limbSeg,
                     [=](float t, float, Vec3) { return blend(foot, toe, smoothstep(0.6f, 0.95f, t)); },
                     [=, this](float t, float) { return t < 0.2f ? Mat{lerp(g.palette.skin, scaleC, 0.5f), MaterialClass::Skin} : legM; });
        }
        const float tl = 0.36f * h * fs, tr = 0.055f * h * fs;
        auto digit = [&](Vec3 dir, float len) {
            std::vector<Vec3> pts{bl};
            Vec3 q = bl, dd = normalize(dir);
            for (int k = 0; k < 3; ++k) {
                dd = normalize(dd + Vec3{0, -0.35f, 0});
                q += dd * (len / 3.0f);
                q.y = std::max(q.y, 0.6f * tr);
                pts.push_back(q);
            }
            emitTube(p, pts, [=](float t) { return tr * (1.0f - 0.35f * t); }, 5, [=](float, float, Vec3) { return one(toe); },
                     [=](float, float) { return legM; });
            std::vector<Vec3> cp{q};
            Vec3 cd = normalize(Vec3{dd.x, 0.2f, dd.z});
            for (int k = 0; k < 3; ++k) {
                cd = normalize(cd + Vec3{0, -0.55f, 0});
                Vec3 nq = cp.back() + cd * (0.9f * tr);
                nq.y = std::max(nq.y, 0.004f);
                cp.push_back(nq);
            }
            emitTube(p, cp, [=](float t) { return 0.75f * tr * (1.0f - 0.9f * t); }, 5, [=](float, float, Vec3) { return one(toe); },
                     [=](float, float) { return clawM; });
        };
        const float sd = s == 0 ? 1.0f : -1.0f;
        digit({0.55f * sd, -0.2f, 1.0f}, 0.85f * tl);
        digit({0.0f, -0.2f, 1.0f}, tl);
        digit({-0.50f * sd, -0.2f, 1.0f}, 0.85f * tl);
        digit({0.1f * sd, -0.2f, -1.0f}, 0.55f * tl);  // hallux, behind
    }
}

// ---------------------------------------------------------------- head traits

// Beak over the nose and mouth. It grows out of a low dome of bare skin (the cere of a
// raptor, the facial skin of a crow) that sinks into the face, so the keratin never sits
// on the face like a mask. The upper mandible has a rounded culmen and closes over a
// shorter lower one; small nostrils sit near the base.
//   1 hooked raptor  2 long and straight (heron, arakkoa)  3 short and stout (owl, turtle)
//   4 parrot (deep, strongly hooked)  5 broad and flat (duck)  6 hornbill (a casque on top)
//   7 crow (strong, straight, slightly curved)
// Each character varies length, depth, width and hook a little.
void Builder::buildBeak() {
    const int kind = std::clamp(g.face.beak, 0, 7);
    if (!kind) return;
    const float h = m.h;
    const int head = ch.rig.head;
    Rng r = Rng(g.seed).fork(61);
    const Vec3 bc = bonesOnly_ ? kBoneM.color : g.palette.beak;
    //                       -    raptor long  stout parrot duck  hornbill crow
    const float len[] =   {0, 0.48f, 0.95f, 0.30f, 0.38f, 0.58f, 0.80f, 0.58f};
    const float hook[] =  {0, 0.55f, 0.05f, 0.30f, 1.05f, -0.05f, 0.25f, 0.12f};
    const float wid[] =   {0, 0.15f, 0.11f, 0.19f, 0.16f, 0.21f, 0.14f, 0.13f};
    const float dep[] =   {0, 0.09f, 0.09f, 0.13f, 0.17f, 0.045f, 0.12f, 0.11f};
    const float droopK[] = {0, 0.12f, 0.18f, 0.12f, 0.28f, 0.02f, 0.2f, 0.12f};
    const float lowK[] =  {0, 0.72f, 0.88f, 0.88f, 0.55f, 0.95f, 0.88f, 0.9f};
    // A skull with a rostrum already carries the beak's root forward, so the keratin is shorter.
    const float L = len[kind] * h * r.range(0.88f, 1.15f) * (1.0f - 0.15f * g.head.rostrum), W = wid[kind] * h * r.range(0.9f, 1.1f);
    const float D = dep[kind] * h * r.range(0.92f, 1.15f), hk = hook[kind] * r.range(0.8f, 1.2f), droop = droopK[kind] * r.range(0.8f, 1.25f);
    const float taperE = kind == 2 ? 0.7f : kind == 5 ? 0.25f : 0.85f;
    const bool twoTone = r.chance(0.45f);
    const Vec3 tipC = twoTone ? (r.chance(0.5f) ? bc * 0.45f : lerp(bc, Vec3{0.92f, 0.86f, 0.62f}, 0.6f)) : bc * 0.65f;
    const Vec3 cereC = bonesOnly_ ? kBoneM.color
                     : kind == 1 || kind == 4 ? lerp(bc, Vec3{0.9f, 0.75f, 0.3f}, 0.4f)
                     : lerp(g.palette.skin, bc, 0.3f) * 0.85f;
    // Face centre line, where the beak meets it.
    // Set low on the face, well below the eyes, so the eyes read above it.
    const float sBeak = 0.25f;
    const float zf = headPoint(0.5f * kPi, sBeak).z;
    const Vec3 c0 = Vec3{0, headPoint(0.5f * kPi, sBeak).y, zf - 0.08f * h};
    Part& p = newPart();
    p.id = headPartId_;
    auto weight = [=](float, float, Vec3) { return one(head); };
    // The skin dome: deep inside the face, flaring at the surface, closing just in front.
    {
        Loft Lf;
        const float yc = c0.y + 0.01f * h, top = 0.10f * h + 0.3f * D, bot = 0.10f * h;
        const float zs[] = {zf - 0.06f * h, zf + 0.004f * h, zf + 0.03f * h};
        const float ws[] = {1.15f, 1.45f, 1.15f}, hs[] = {0.95f, 1.1f, 0.85f};
        for (int k = 0; k < 3; ++k) {
            Lf.rings.push_back(makeRing({0, yc, zs[k]}, {-1, 0, 0}, {0, 1, 0}, SuperRing{W * ws[k], 0, top * hs[k], bot * hs[k], 2.4f, 2.4f}, 10));
            Lf.t.push_back(float(k) / 3.0f);
        }
        Lf.poleEnd = true;
        Lf.endPole = {0, yc, zf + 0.045f * h};
        Lf.tEnd = 1;
        emitLoft(p, Lf, weight, [=](float, float) { return Mat{cereC, bonesOnly_ ? MaterialClass::Horn : MaterialClass::Skin}; });
    }
    auto mandible = [&](Vec3 base, float lenM, float widM, float depth, float down, float hkM, bool upper, const Vec3& col) {
        Loft Lf;
        const int n = 8;
        auto centre = [=](float t) { return base + Vec3{0, -down * t * lenM - hkM * lenM * std::pow(t, 3.0f) * 0.6f, t * lenM}; };
        for (int k = 0; k < n; ++k) {
            const float t = float(k) / float(n);
            const float taper = std::pow(1.0f - t, taperE);
            // The root swells into the face so the beak grows out of the skull, not off it.
            const float root = 1.0f - smoothstep(0.0f, 0.45f, t);
            float w = widM * taper * (1.0f + 0.45f * root), dd = depth * (0.35f + 0.65f * taper) * (1.0f + (upper ? 0.6f : 0.3f) * root);
            if (kind == 5) w = widM * (1.0f - 0.25f * t) * (1.0f + 0.3f * root);  // a broad, round-ended bill
            // Upper: a rounded ridge on top, flat underneath; lower: the reverse.
            SuperRing sr = upper ? SuperRing{w, 0, dd, 0.25f * dd, 1.6f, 3.0f} : SuperRing{w, 0, 0.25f * dd, dd, 3.0f, 1.6f};
            if (kind == 5) sr.nF = sr.nB = 3.0f;
            Lf.rings.push_back(makeRing(centre(t), {-1, 0, 0}, {0, 1, 0}, sr, 8));  // loft along +z: u = cross(+z, +y) = -x
            Lf.t.push_back(t);
        }
        Lf.poleStart = true;
        Lf.startPole = base - Vec3{0, 0, 0.05f * h};
        Lf.tStart = 0;
        Lf.poleEnd = true;
        Lf.endPole = centre(1.0f) + Vec3{0, -(upper ? 0.35f : 0.0f) * std::max(0.0f, hkM) * 0.15f * lenM, -0.04f * std::max(0.0f, hkM) * lenM};
        Lf.tEnd = 1;
        emitLoft(p, Lf, weight, [=](float t, float) { return Mat{t > 0.72f ? tipC : col, MaterialClass::Horn}; });
        return centre;
    };
    // The lower mandible closes against the upper one along its whole length.
    const float Dl = D * (kind == 4 ? 0.75f : 0.6f);
    const Vec3 ub = c0 + Vec3{0, 0.04f * h, 0}, lb = ub - Vec3{0, 0.25f * (D + Dl) + 0.004f * h, 0};
    auto up = mandible(ub, L, W, D, droop, hk, true, bc);
    mandible(lb, L * lowK[kind], W * (kind == 5 ? 0.95f : 0.86f), Dl, droop, kind == 4 ? 0.2f : 0.0f, false, bc * 0.82f);
    if (bonesOnly_) return;
    // Nostrils, on the sides of the culmen near the base.
    for (float sd : {1.0f, -1.0f}) {
        const float tn = kind == 2 ? 0.12f : 0.2f;
        const float taper = std::pow(1.0f - tn, taperE);
        Ellipsoid e;
        e.center = up(tn) + Vec3{sd * 0.55f * W * taper, 0.55f * D * (0.35f + 0.65f * taper), 0};
        e.axis = {0, 0, 1};
        e.up = {0, 1, 0};
        e.rAxis = 0.035f * h;
        e.rU = 0.012f * h;
        e.rV = 0.016f * h;
        e.lat = 2;
        e.lon = 5;
        emitEllipsoid(p, e, weight, [=](float, float) { return Mat{bc * 0.25f, MaterialClass::Horn}; });
    }
    if (kind == 6) {  // the hornbill's casque, riding the culmen
        const float t = 0.3f, taper = std::pow(1.0f - t, taperE), dd = D * (0.35f + 0.65f * taper);
        Ellipsoid e;
        e.center = up(t) + Vec3{0, 0.8f * dd + 0.35f * D, -0.02f * L};
        e.axis = normalize(up(0.45f) - up(0.15f));
        e.up = {0, 1, 0};
        e.rAxis = 0.32f * L;
        e.rU = 0.55f * W;
        e.rV = 0.75f * D;
        e.lat = 4;
        e.lon = 8;
        const Vec3 cc = twoTone ? tipC : lerp(bc, Vec3{0.85f, 0.25f, 0.1f}, 0.35f);
        emitEllipsoid(p, e, weight, [=](float, float) { return Mat{cc, MaterialClass::Horn}; });
    }
}

// Insect mouthparts: a pair of curved, toothed pincers closing in front of the mouth.
void Builder::buildMandibles() {
    if (!g.face.mandibles) return;
    const float h = m.h;
    const int head = ch.rig.head;
    const Mat chitin{bonesOnly_ ? kBoneM.color : g.palette.beak * 0.55f, MaterialClass::Horn};
    Part& p = newPart();
    p.id = headPartId_;
    for (float sd : {1.0f, -1.0f}) {
        const float x = sd * 0.12f * g.face.mouthWidth;
        const float th = std::acos(clamp(x / 0.30f, -1.0f, 1.0f));
        Vec3 q = headPoint(th, 0.17f) - Vec3{0, 0, 0.03f * h};
        Vec3 dir = normalize(Vec3{sd * 0.55f, -0.20f, 0.80f});
        std::vector<Vec3> pts{q};
        for (int k = 0; k < 5; ++k) {
            dir = normalize(rotate(Quat::axisAngle({0, 1, 0}, -sd * 0.42f), dir));
            q += dir * (0.085f * h);
            pts.push_back(q);
        }
        emitTube(p, pts, [=](float t) { return 0.050f * h * (1.0f - 0.85f * t); }, 5, [=](float, float, Vec3) { return one(head); },
                 [=](float t, float) { return Mat{chitin.color * (t > 0.7f ? 0.6f : 1.0f), MaterialClass::Horn}; });
        for (int k = 2; k <= 3; ++k)  // inner teeth
            emitSpike(p, pts[size_t(k)], normalize(Vec3{-sd, 0, 0.2f}), {0, 0, 1}, 0.05f * h, 0.018f * h, 0, one(head), chitin);
    }
}

// Antennae from the top of the brow: 1 segmented whips with a small club, 2 feathery
// plumes (moths). Spring chains: they bob with the head.
void Builder::buildAntennae() {
    const int kind = std::clamp(g.face.antennae, 0, 2);
    if (!kind) return;
    Skeleton& sk = ch.skeleton;
    const float h = m.h;
    const int head = ch.rig.head;
    const float len = (kind == 2 ? 1.0f : 1.35f) * h * g.face.antennaLength;
    const Vec3 col = bonesOnly_ ? kBoneM.color : g.palette.beak * 0.5f + g.palette.hair * 0.3f;
    for (float sd : {1.0f, -1.0f}) {
        const Vec3 base = headSurface(0.5f * kPi - sd * 0.42f, 0.86f, -0.01f * h);
        Vec3 dir = normalize(Vec3{sd * 0.45f, 1.0f, 0.45f}), q = base;
        std::vector<Vec3> ctrl{base};
        for (int k = 0; k < 5; ++k) {
            dir = normalize(dir + Vec3{sd * 0.06f, -0.16f, -0.28f});
            q += dir * (len / 5.0f);
            ctrl.push_back(q);
        }
        const std::vector<Vec3> pts = smoothPath(ctrl, kind == 1 ? 13 : 8);
        std::vector<int> bones;
        int parent = head;
        for (int b = 0; b < 3; ++b) {
            parent = sk.add(std::string("antenna") + (sd > 0 ? ".L" : ".R") + std::to_string(b), parent, pathAt(pts, 0.9f * float(b) / 3.0f));
            bones.push_back(parent);
        }
        ch.rig.antennae.push_back(bones);
        Part& p = newPart(kind == 2);
        p.id = headPartId_;
        auto wf = [=](float u, float, Vec3) { return chainAt(bones, head, u); };
        if (kind == 1) {
            emitTube(p, pts, [=](float u) { return 0.020f * h * (1.0f - 0.45f * u) * (1.0f + 0.30f * std::fabs(std::sin(u * kPi * 6.0f))); }, 5, wf,
                     [=](float u, float) { return Mat{col * (0.85f + 0.15f * float(int(u * 12.0f) % 2)), MaterialClass::Horn}; });
            Ellipsoid e;  // club
            e.center = pts.back();
            e.axis = pathDir(pts, 1.0f);
            e.up = {0, 1, 0};
            e.rAxis = 0.05f * h;
            e.rU = e.rV = 0.032f * h;
            e.lat = 3;
            e.lon = 5;
            const SkinW tipW = one(bones.back());
            emitEllipsoid(p, e, [=](float, float, Vec3) { return tipW; }, [=](float, float) { return Mat{col * 0.8f, MaterialClass::Horn}; });
        } else {
            emitTube(p, pts, [=](float u) { return 0.014f * h * (1.0f - 0.5f * u); }, 4, wf, [=](float, float) { return Mat{col, MaterialClass::Horn}; });
            // The plume: a serrated leaf in the plane facing forwards.
            Sheet sh;
            std::vector<uint32_t> L, R;
            const int n = int(pts.size());
            for (int k = 0; k < n; ++k) {
                const float u = float(k) / float(n - 1);
                const Vec3 d = pathDir(pts, u);
                const Vec3 sideV = normalize(cross(d, Vec3{0, 0, 1}), {1, 0, 0});
                const float w = 0.12f * h * std::pow(std::sin(kPi * std::min(0.12f + u, 1.0f)), 0.8f) * (k % 2 ? 0.75f : 1.0f);
                const SkinW sw = chainAt(bones, head, u);
                L.push_back(sh.add(pts[size_t(k)] + sideV * w, sw));
                R.push_back(sh.add(pts[size_t(k)] - sideV * w, sw));
            }
            std::vector<uint32_t> C;
            for (int k = 0; k < n; ++k) C.push_back(sh.add(pts[size_t(k)], chainAt(bones, head, float(k) / float(n - 1))));
            const Mat pm{lerp(col, g.palette.wing, 0.5f), MaterialClass::Hair};
            for (int k = 0; k + 1 < n; ++k) {
                sh.tri(C[size_t(k)], L[size_t(k)], L[size_t(k + 1)], pm), sh.tri(C[size_t(k)], L[size_t(k + 1)], C[size_t(k + 1)], pm);
                sh.tri(C[size_t(k)], C[size_t(k + 1)], R[size_t(k + 1)], pm), sh.tri(C[size_t(k)], R[size_t(k + 1)], R[size_t(k)], pm);
            }
            emitSheet(p, sh, 0.004f * h, 0.85f);
        }
        SpringChain sc;
        sc.name = std::string("antenna") + (sd > 0 ? ".L" : ".R");
        sc.bones = bones;
        sc.tail = pts.back() - sk.bones[size_t(bones.back())].restWorld;
        sc.stiffness = 2.6f;
        sc.drag = 0.3f;
        sc.gravity = 0.04f;
        sc.hitRadius = 0.02f * h;
        sc.maxAngle = 0.8f;
        ch.physics.chains.push_back(sc);
    }
}

// Compound eyes: two faceted, glossy domes over the eye sockets (they replace the
// painted eyes).
void Builder::buildCompoundEyes() {
    if (g.face.eyeKind != 1 || bonesOnly_) return;
    const float h = m.h;
    const int head = ch.rig.head;
    const Vec3 base = g.face.glowEyes ? g.face.glow * 0.45f : g.palette.iris * 0.35f;
    Part& p = newPart(true);
    p.id = headPartId_;
    for (float sd : {1.0f, -1.0f}) {
        const float x = sd * 0.20f * g.face.eyeSpacing * g.body.headWidth;
        const float th = std::acos(clamp(x / 0.36f, -1.0f, 1.0f));
        const Vec3 q = headPoint(th, 0.49f);
        const Vec3 n = normalize(headNormalDir(q) + Vec3{0, 0, 0.35f});
        Ellipsoid e;
        e.center = q - n * (0.03f * h);
        e.axis = n;
        e.up = {0, 1, 0};
        e.rAxis = 0.10f * h * g.face.eyeSize;
        e.rU = 0.12f * h * g.face.eyeSize;
        e.rV = 0.15f * h * g.face.eyeSize;
        e.lat = 5;
        e.lon = 9;
        emitEllipsoid(p, e, [=](float, float, Vec3) { return one(head); }, [=](float t, float th2) {
            const float k = 0.75f + 0.25f * float((int(t * 5.0f) + int(th2 * 9.0f / kTau)) % 2);
            return Mat{base * k + Vec3{0.02f, 0.02f, 0.02f}, MaterialClass::Metal};
        });
    }
}

// Crests: 1 a ribbed fin along the head's midline (fishfolk), 2 a crest of feathers
// sweeping back from the crown, 3 a bony frill fanned behind the head, 4 a cobra hood
// spread from the neck.
void Builder::buildCrest() {
    const int kind = std::clamp(g.body.crest, 0, 4);
    if (!kind || bonesOnly_) return;
    const float h = m.h, cs = g.body.crestSize;
    const int head = ch.rig.head, neck = ch.rig.neck;
    const Vec3 fin = g.palette.wing, skinC = g.palette.skin;
    Part& p = newPart(true);
    p.id = headPartId_;
    if (kind == 1 || kind == 2) {
        // Points up the brow, over the crown and down the back of the skull.
        std::vector<Vec3> base;
        std::vector<Vec3> up;
        const int n = kind == 1 ? 11 : 6;
        for (int k = 0; k < n; ++k) {
            const float f = float(k) / float(n - 1);
            const float ang = lerp(0.20f, 2.75f, f);  // 0 forehead .. pi back of the head, around the crown
            const float s = kind == 1 ? 0.60f + 0.40f * std::sin(ang) : 0.80f + 0.20f * std::sin(ang);
            const float theta = ang < 0.5f * kPi ? 0.5f * kPi : 1.5f * kPi;
            const Vec3 q = headSurface(theta, std::min(s, 0.985f), -0.01f * h);
            const Vec3 c = headCenter();
            const Vec3 out = normalize(q - c);
            base.push_back(Vec3{0, q.y, q.z});
            up.push_back(normalize(out + Vec3{0, 0.2f, -0.4f}));
        }
        if (kind == 1) {
            Sheet sh;
            std::vector<uint32_t> B, T;
            for (int k = 0; k < n; ++k) {
                const float f = float(k) / float(n - 1);
                const float H = 0.42f * h * cs * std::pow(std::sin(kPi * (0.15f + 0.8f * f)), 0.8f) * (k % 2 ? 0.80f : 1.0f);  // scalloped between rays
                B.push_back(sh.add(base[size_t(k)], one(head)));
                T.push_back(sh.add(base[size_t(k)] + up[size_t(k)] * H, one(head)));
            }
            for (int k = 0; k + 1 < n; ++k) {
                const Mat mm{fin * (k % 2 ? 1.0f : 0.72f), MaterialClass::Skin};
                sh.tri(B[size_t(k)], T[size_t(k)], T[size_t(k + 1)], mm), sh.tri(B[size_t(k)], T[size_t(k + 1)], B[size_t(k + 1)], mm);
            }
            emitSheet(p, sh, 0.006f * h, 0.85f);
        } else {
            for (int k = 0; k < n; ++k) {
                const float f = float(k) / float(n - 1);
                const float len = (0.75f - 0.30f * std::fabs(f - 0.35f)) * h * cs;
                Sheet sh;
                const Vec3 b0 = base[size_t(k)], dir = normalize(up[size_t(k)] + Vec3{0, 0.3f, -0.9f});
                const Vec3 sideV{0.07f * h * cs, 0, 0};
                const SkinW sw = one(head);
                const uint32_t a0 = sh.add(b0 - sideV * 0.4f, sw), a1 = sh.add(b0 + sideV * 0.4f, sw);
                const uint32_t m0 = sh.add(b0 + dir * (0.55f * len) - sideV, sw), m1 = sh.add(b0 + dir * (0.55f * len) + sideV, sw);
                const uint32_t tp = sh.add(b0 + dir * len + Vec3{0, -0.05f * len, 0}, sw);
                const Mat mm{lerp(fin, g.palette.hair, 0.3f) * (0.9f + 0.1f * float(k % 2)), MaterialClass::Hair};
                const Mat tm{fin * 0.5f, MaterialClass::Hair};
                sh.tri(a0, m0, m1, mm), sh.tri(a0, m1, a1, mm), sh.tri(m0, tp, m1, tm);
                emitSheet(p, sh, 0.008f * h, 0.85f);
            }
        }
        return;
    }
    // Frill (3) or hood (4): a fan behind the head.
    const Vec3 c = kind == 3 ? m.headOrigin + Vec3{0, 0.42f * h, -0.30f * h} : m.neckBase + Vec3{0, 0.15f * h, -0.10f * h};
    const float R = (kind == 3 ? 0.95f : 0.80f) * h * cs;
    std::vector<Vec3> edge;
    const int n = 13;
    for (int k = 0; k <= n; ++k) {
        const float a = lerp(kind == 3 ? -0.15f : -0.55f, kind == 3 ? kPi + 0.15f : kPi + 0.55f, float(k) / float(n));
        const float spike = kind == 3 ? (k % 2 ? 0.80f : 1.08f) : 1.0f;
        const float rx = R * (kind == 4 ? 0.95f : 1.0f), ry = kind == 4 ? 1.35f * R : R;
        edge.push_back(c + Vec3{std::cos(a) * rx * spike, std::sin(a) * ry * spike * (kind == 4 ? 1.0f : 0.85f), -0.25f * R});
    }
    if (kind == 4) {  // the hood hangs from the neck, its lower edge sweeping in towards the shoulders
        for (Vec3& e : edge)
            if (e.y < c.y) e.y = lerp(e.y, c.y - 0.25f * R, 0.6f);
    }
    Sheet sh;
    auto wf = [=](Vec3 q) { return kind == 4 ? blend(neck, head, smoothstep(c.y - 0.2f * R, c.y + 0.6f * R, q.y)) : one(head); };
    fanSheet(sh, c + Vec3{0, 0, 0.10f * R}, edge, 3, wf, [=, this](float rr, float s) {
        if (kind == 3) return Mat{(rr > 0.72f ? lerp(skinC, g.palette.horn, 0.6f) : skinC * (0.85f + 0.1f * float(int(s * 13.0f) % 2))), MaterialClass::Skin};
        // Hood: pale edge band, dark marking.
        const bool mark = rr > 0.45f && rr < 0.65f && std::fabs(s - 0.5f) > 0.12f && std::fabs(s - 0.5f) < 0.32f;
        return Mat{mark ? skinC * 0.35f : rr > 0.85f ? g.palette.belly : skinC, MaterialClass::Skin};
    });
    emitSheet(p, sh, 0.01f * h, 0.85f);
}

// ---------------------------------------------------------------- eyes and maws

// 2 cyclopean: one great eye over the nose bridge, framed by a heavy lid ring; the paired
// sockets heal over. 3 eyeless: skin grown smooth over both sockets.
void Builder::buildTraitEyes() {
    const int kind = g.face.eyeKind;
    if ((kind != 2 && kind != 3) || bonesOnly_) return;
    const float h = m.h;
    const int head = ch.rig.head;
    auto headW = [=](float, float, Vec3) { return one(head); };
    Part& p = newPart(true);
    p.id = headPartId_;
    const Mat flesh{g.palette.skin * 0.97f, MaterialClass::Skin};
    for (float sd : {1.0f, -1.0f}) {  // the healed sockets
        const float x = sd * 0.20f * g.face.eyeSpacing * g.body.headWidth;
        const float th = std::acos(clamp(x / 0.36f, -1.0f, 1.0f));
        const Vec3 q = headPoint(th, 0.49f);
        const Vec3 n = normalize(headNormalDir(q) + Vec3{0, 0, 0.35f});
        Ellipsoid e;
        e.center = q - n * (0.035f * h);
        e.axis = n;
        e.up = {0, 1, 0};
        e.rAxis = 0.06f * h;
        e.rU = (kind == 2 ? 0.11f : 0.12f) * h * g.face.eyeSize;
        e.rV = 0.09f * h * g.face.eyeSize;
        e.lat = 4;
        e.lon = 8;
        emitEllipsoid(p, e, headW, [=](float, float) { return flesh; });
    }
    if (kind != 2) return;
    const float R = 0.15f * h * g.face.eyeSize;
    const Vec3 q = headPoint(0.5f * kPi, 0.52f);
    const Vec3 n = normalize(Vec3{0, 0.05f, 1});
    const Vec3 iris = g.face.glowEyes ? g.face.glow : g.palette.iris, sclera = g.face.sclera;
    const MaterialClass irisCls = g.face.glowEyes ? MaterialClass::Glow : MaterialClass::Skin;
    Ellipsoid e;
    e.center = q - n * (0.40f * R);
    e.axis = n;
    e.up = {0, 1, 0};
    e.rAxis = e.rU = R;
    e.rV = 0.92f * R;
    e.lat = 8;
    e.lon = 12;
    emitEllipsoid(p, e, headW, [=, this](float t, float) {
        if (t > 0.86f) return Mat{g.face.glowEyes ? iris : Vec3{0.03f, 0.03f, 0.03f}, irisCls};  // pupil
        if (t > 0.70f) return Mat{iris, irisCls};
        return Mat{sclera, MaterialClass::Skin};
    });
    // Lids: a thick ring of flesh round the eye, heavier on top, and a brow shelf above.
    std::vector<Vec3> rim;
    for (int k = 0; k <= 18; ++k) {
        const float a = kTau * float(k) / 18.0f;
        const float top = std::sin(a) > 0 ? 1.0f : 0.0f;
        rim.push_back(e.center + n * (0.42f * R) + Vec3{std::cos(a) * 0.98f * R, std::sin(a) * (0.86f + 0.10f * top) * R, 0});
    }
    emitTube(p, rim, [=](float t) { return (0.14f + 0.08f * std::max(0.0f, std::sin(kTau * t))) * R; }, 6, headW,
             [=, this](float, float) { return Mat{g.palette.skin * 0.92f, MaterialClass::Skin}; });
    Ellipsoid brow;
    brow.center = e.center + Vec3{0, 0.95f * R, 0.30f * R};
    brow.axis = {1, 0, 0};
    brow.up = {0, 1, 0};
    brow.rAxis = 1.25f * R;
    brow.rU = 0.38f * R;
    brow.rV = 0.22f * R * (1.0f + 0.5f * g.face.browRidge);
    brow.lat = 5;
    brow.lon = 7;
    emitEllipsoid(p, brow, headW, [=, this](float, float) { return Mat{g.palette.skin * 0.95f, MaterialClass::Skin}; });
}

// A maw: the mouth split wide, past the cheeks, a dark gape lined with rows of teeth.
void Builder::buildMaw() {
    if (!g.face.maw || g.face.beak || bonesOnly_) return;
    const float h = m.h;
    const int head = ch.rig.head;
    const float sM = 0.215f, half = 0.70f + 0.16f * g.face.mouthWidth;  // angular half-width from the front
    const int n = 20;
    Sheet sh;
    std::vector<Vec3> top, bot, nrm;
    for (int i = 0; i <= n; ++i) {
        const float a = float(i) / float(n);  // 0 .. 1 across
        const float th = 0.5f * kPi + half * (2.0f * a - 1.0f);
        const float gape = 0.075f * h * std::pow(std::sin(kPi * a), 0.6f) * (0.8f + 0.25f * g.face.jaw);
        const Vec3 c = headPoint(th, sM);
        const Vec3 nn = headNormalDir(c);
        nrm.push_back(nn);
        top.push_back(headPoint(th, sM + gape / h * 0.55f) + nn * (0.006f * h));
        bot.push_back(headPoint(th, sM - gape / h * 0.45f) + nn * (0.006f * h));
    }
    const Mat gapeM{Vec3{0.10f, 0.03f, 0.04f}, MaterialClass::Dark};
    std::vector<uint32_t> ti, bi;
    for (int i = 0; i <= n; ++i) ti.push_back(sh.add(top[size_t(i)], one(head))), bi.push_back(sh.add(bot[size_t(i)], one(head)));
    for (int i = 0; i < n; ++i) {
        sh.tri(ti[size_t(i)], bi[size_t(i)], bi[size_t(i + 1)], gapeM);
        sh.tri(ti[size_t(i)], bi[size_t(i + 1)], ti[size_t(i + 1)], gapeM);
    }
    Part& p = newPart();
    p.id = headPartId_;
    emitSheet(p, sh, 0.004f * h, 1.0f);
    // Lip ridges: thin rolls of flesh along both edges, so the gape reads as cut into the face.
    auto ridge = [&](const std::vector<Vec3>& edge, float lift) {
        std::vector<Vec3> pts;
        for (size_t i = 0; i < edge.size(); ++i) pts.push_back(edge[i] + nrm[i] * (0.004f * h) + Vec3{0, lift, 0});
        emitTube(p, pts, [=](float t) { return 0.016f * h * (0.4f + 0.6f * std::sin(kPi * t)); }, 5, [=](float, float, Vec3) { return one(head); },
                 [=, this](float, float) { return Mat{g.palette.lips, MaterialClass::Skin}; });
    };
    ridge(top, 0.010f * h);
    ridge(bot, -0.010f * h);
    // Teeth: needle rows in the upper jaw, a few longer fangs; shorter teeth below.
    const Mat toothM{Vec3{0.86f, 0.82f, 0.70f}, MaterialClass::Horn};
    for (int i = 1; i < n; ++i) {
        const float a = float(i) / float(n);
        const float gape = length(top[size_t(i)] - bot[size_t(i)]);
        if (gape < 0.02f * h) continue;
        const bool fang = i == n / 2 - 3 || i == n / 2 + 3;
        const Vec3 in = -nrm[size_t(i)] * 0.3f;
        emitSpike(p, top[size_t(i)], Vec3{0, -1, 0} + in, nrm[size_t(i)], (fang ? 0.75f : 0.42f) * gape, 0.012f * h * (fang ? 1.4f : 1.0f), 0,
                  one(head), toothM);
        if (i % 2 == 0)
            emitSpike(p, bot[size_t(i)], Vec3{0, 1, 0} + in, nrm[size_t(i)], 0.32f * gape, 0.010f * h, 0, one(head), toothM);
        (void)a;
    }
}

// Eyes opening over the skin of the body (the watchers): sampled by area over the torso
// and limbs, each a glossy eyeball half sunk in the flesh with a ring of lid.
void Builder::buildBodyEyes() {
    if (g.body.bodyEyes <= 0 || bonesOnly_ || decay_ >= 0.3f) return;
    const float h = m.h;
    Rng r = Rng(g.seed).fork(83);
    struct Cand {
        size_t part, tri;
        float area;
    };
    std::vector<Cand> cands;
    float total = 0;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const Part& p = parts[pi];
        if (p.kind != SubmeshKind::Body || p.flat || pi == headPart_ || pi == nosePart_ || p.tris.size() < 60) continue;
        for (size_t ti = 0; ti < p.tris.size(); ++ti) {
            const auto& t = p.tris[ti];
            if (t.m.cls != MaterialClass::Skin) continue;
            const float ar = 0.5f * length(cross(p.pos[t.b] - p.pos[t.a], p.pos[t.c] - p.pos[t.a]));
            const Vec3 c = (p.pos[t.a] + p.pos[t.b] + p.pos[t.c]) / 3.0f;
            if (c.y < 0.12f * m.H) continue;  // not on the soles and ankles
            cands.push_back({pi, ti, ar});
            total += ar;
        }
    }
    if (cands.empty() || total <= 0) return;
    const Vec3 iris = g.face.glowEyes ? g.face.glow : g.palette.iris, sclera = g.face.sclera;
    const MaterialClass irisCls = g.face.glowEyes ? MaterialClass::Glow : MaterialClass::Skin;
    struct Eye {
        Vec3 c, n;
        float R;
        SkinW w;
        Vec3 skinC, look;
    };
    // Laid out like a body plan rather than scattered: one great eye in the middle of the
    // chest, then mirrored pairs (shoulders, flanks, arms, thighs), bigger on the trunk.
    std::vector<Eye> eyes;
    auto at = [&](size_t k) {
        const Part& p = parts[cands[k].part];
        const auto& t = p.tris[cands[k].tri];
        const Vec3 c = (p.pos[t.a] + p.pos[t.b] + p.pos[t.c]) / 3.0f;
        const Vec3 nn = normalize(cross(p.pos[t.b] - p.pos[t.a], p.pos[t.c] - p.pos[t.a]), {0, 0, 1});
        return Eye{c, nn, 0, p.w[t.a], t.m.color, nn};
    };
    auto nearest = [&](Vec3 q) {
        size_t best = 0;
        float bd = 1e9f;
        for (size_t k = 0; k < cands.size(); ++k) {
            const float d = length(at(k).c - q);
            if (d < bd) bd = d, best = k;
        }
        return best;
    };
    auto add = [&](Eye e) {
        for (const Eye& o : eyes)
            if (length(o.c - e.c) < 1.25f * (o.R + e.R)) return false;
        eyes.push_back(e);
        return true;
    };
    if (g.body.bodyEyes >= 5) {
        Eye e = at(nearest(torsoPoint(0.66f, 0.5f * kPi) + Vec3{0, 0, 0.2f * h}));
        e.R = 0.34f * h;
        add(e);
    }
    for (int tries = 0; tries < g.body.bodyEyes * 16 && int(eyes.size()) + 1 < g.body.bodyEyes; ++tries) {
        float pick = r.uniform() * total;
        size_t k = 0;
        while (k + 1 < cands.size() && pick > cands[k].area) pick -= cands[k].area, ++k;
        Eye e = at(k);
        if (e.c.x < 0.04f * h) continue;  // left side; the right mirrors it
        e.R = (cands[k].part == torsoPart_ && e.c.y > m.hipJointY ? r.range(0.17f, 0.24f) : r.range(0.10f, 0.16f)) * h;
        e.look = normalize(e.n + Vec3{r.range(-0.2f, 0.2f), r.range(-0.12f, 0.12f), r.range(0.0f, 0.25f)});
        Eye m2 = at(nearest({-e.c.x, e.c.y, e.c.z}));
        m2.R = e.R;
        m2.look = normalize(Vec3{-e.look.x, e.look.y, e.look.z});
        if (!add(e)) continue;
        if (!add(m2)) eyes.pop_back();
    }
    Part& p = newPart(true);
    for (const Eye& e : eyes) {
        auto wf = [w = e.w](float, float, Vec3) { return w; };
        const Vec3 look = e.look;
        Ellipsoid b;
        b.center = e.c - e.n * (0.45f * e.R);  // sunk: only the front of the ball shows
        b.axis = look;
        b.up = std::fabs(look.y) > 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        b.rAxis = b.rU = b.rV = e.R;
        b.lat = 6;
        b.lon = 8;
        emitEllipsoid(p, b, wf, [=, this](float t, float) {
            if (t > 0.86f) return Mat{g.face.glowEyes ? iris : Vec3{0.03f, 0.03f, 0.03f}, irisCls};
            if (t > 0.70f) return Mat{iris, irisCls};
            return Mat{sclera, MaterialClass::Skin};
        });
        // A lid ring where the eyeball meets the skin, a little slit-shaped.
        const Vec3 u = normalize(cross(e.n, b.up), {1, 0, 0}), v = cross(u, e.n);
        std::vector<Vec3> rim;
        for (int k = 0; k <= 12; ++k) {
            const float a = kTau * float(k) / 12.0f;
            rim.push_back(e.c + e.n * (0.12f * e.R) + u * (std::cos(a) * 0.86f * e.R) + v * (std::sin(a) * 0.62f * e.R));
        }
        const Mat lidM{e.skinC * 0.62f, MaterialClass::Skin};  // a dark, swollen lid ring
        emitTube(p, rim, [=](float) { return 0.22f * e.R; }, 5, wf, [=](float, float) { return lidM; });
    }
}

// ---------------------------------------------------------------- beast body

// On all fours (computeBeastMarks): one loft from the round rump along the trunk to the
// deep chest and on through a short, thick neck into the back of the skull; massive
// forelegs from high shoulders down to broad clawed paws, crouched digitigrade hind legs.
void Builder::buildBeastBody() {
    const RigInfo& r = ch.rig;
    const float H = m.H, h = m.h, mu = g.body.muscle;
    const int N = lod.torsoSeg;
    const int hips = r.hips, spine = r.spine, chest = r.chest, neck = r.neck, head = r.head;
    // Trunk and neck.
    {
        Loft L;
        const std::vector<float> ts = lod.sparseRings ? std::vector<float>{0.0f, 0.12f, 0.30f, 0.50f, 0.68f, 0.82f, 0.94f, 1.0f}
                                                      : std::vector<float>{0.0f, 0.05f, 0.12f, 0.22f, 0.34f, 0.46f, 0.58f, 0.68f, 0.77f, 0.85f, 0.92f, 0.97f, 1.0f};
        for (float t : ts) {
            const BeastRing br = beastRing(t);
            L.rings.push_back(makeRing({0, br.y, beastZ(t)}, {1, 0, 0}, {0, -1, 0}, SuperRing{br.w, 0, br.bot, br.top, 2.3f, 2.3f}, N));
            L.t.push_back(t);
        }
        const BeastRing f = beastRing(1.0f);
        const Vec3 n0{0, f.y, beastZ(1.0f)}, n1 = m.headOrigin + Vec3{0, 0.42f * h, -0.24f * h};  // the nape, deep in the skull
        const Vec3 nd = normalize(n1 - n0, {0, 0, 1}), nv = normalize(Vec3{0, -1, 0} - nd * (-nd.y), {0, -1, 0}), nu = cross(nd, nv);
        const float nR = std::max(0.30f * h, std::min(g.body.neckThick * h * 0.95f, 0.70f * f.w));
        const std::vector<float> vs = lod.sparseRings ? std::vector<float>{0.5f, 1.0f} : std::vector<float>{0.35f, 0.7f, 1.0f};
        for (float v : vs) {
            const float k = smoothstep(0.0f, 0.55f, v);
            const float w = lerp(f.w, nR, k), up = lerp(f.top, 0.95f * nR, k), dn = lerp(f.bot, 1.05f * nR, k);
            L.rings.push_back(makeRing(n0 + nd * (v * length(n1 - n0)), nu, nv, SuperRing{w * (1.0f - 0.10f * v * v), 0, dn, up, 2.2f, 2.2f}, N));
            L.t.push_back(1.0f + 0.4f * v);
        }
        const BeastRing b0 = beastRing(0.0f);
        L.poleStart = true;
        L.startPole = {0, b0.y + 0.1f * b0.top, m.beastZR - 0.035f * H};
        L.tStart = -0.04f;
        L.poleEnd = true;
        L.endPole = n1 + nd * (0.4f * nR);
        L.tEnd = 1.55f;
        Part& tp = newPart();
        bodyGroup_ = tp.id;
        torsoPart_ = parts.size() - 1;
        if (g.body.furPattern >= 3) {  // tiger stripes: wide over the back, tapering down the flanks
            const float zR = m.beastZR, zF = m.beastZF, ph0 = 6.0f * hash01(g.seed, 51);
            const Mat stripeM{g.palette.skin * 0.40f, MaterialClass::Hair};
            tp.clips.push_back({[=, this](Vec3 q) {
                const float t = (q.z - zR) / (zF - zR);
                if (t < 0.04f || t > 0.86f) return 1.0f;
                const BeastRing br = beastRing(t);
                const float up = (q.y - br.y) / std::max(br.top, 1e-4f);  // 1 on the spine, -1 under the belly
                if (up < -0.35f) return 1.0f;
                const float ph = (q.z + 0.25f * (q.y - br.y)) * (kTau / (0.10f * H)) + ph0;
                const float thr = lerp(0.85f, 0.15f, smoothstep(-0.35f, 0.9f, up));
                return (thr - std::sin(ph)) * 0.02f * H;
            }, stripeM, 1});
        }
        emitLoft(tp, L, [=](float t, float, Vec3) {
            return chainWeights({{0.10f, hips}, {0.22f, hips}, {0.48f, spine}, {0.76f, chest}, {1.0f, chest}, {1.16f, neck}, {1.36f, neck}, {1.50f, head}}, t);
        }, [this](float, float) { return skin(); });
    }
    // Legs: lofts through knots (point, radius, t), sunk into the trunk at the top.
    struct K { Vec3 p; float r, t; };
    auto leg = [&](const std::vector<K>& ks, float tb, float tc, int parent, const LimbRig& rig) {
        Loft L;
        for (size_t k = 0; k < ks.size(); ++k) {
            const Vec3 dd = normalize(ks[std::min(k + 1, ks.size() - 1)].p - ks[k > 0 ? k - 1 : 0].p, {0, -1, 0});
            const Vec3 v = orthoFront(dd), u = cross(dd, v);
            const float rr = ks[k].r;
            L.rings.push_back(makeRing(ks[k].p, u, v, SuperRing{rr * 0.86f, 0, rr * 1.0f, rr * 1.08f, 2.2f, 2.2f}, lod.limbSeg));
            L.t.push_back(ks[k].t);
        }
        L.poleStart = true;
        L.startPole = ks[0].p + normalize(ks[0].p - ks[1].p) * (0.5f * ks[0].r);
        L.tStart = -0.02f;
        L.poleEnd = true;  // closed inside the paw
        L.endPole = ks.back().p + normalize(ks.back().p - ks[ks.size() - 2].p) * (0.6f * ks.back().r);
        L.tEnd = 1.0f;
        const int up = rig.upper, lo = rig.lower, ft = rig.end, tip = rig.tip;
        Part& lp = newPart();
        lp.id = bodyGroup_;
        emitLoft(lp, L, [=](float t, float, Vec3) {
            return chainWeights({{0.0f, parent}, {0.14f, up}, {tb - 0.07f, up}, {tb + 0.05f, lo}, {tc - 0.07f, lo}, {tc + 0.04f, ft}, {1.0f, tip}}, t);
        }, [this](float, float) { return skin(); });
    };
    // Paws: a broad pad resting on the ground and four toes in an arc, each with a hooked
    // claw; a heavy hoof instead for hoofed peoples.
    const Mat clawM{g.palette.horn * 0.55f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    const Mat hoofM{g.palette.horn * 0.35f + Vec3{0.05f, 0.04f, 0.04f}, MaterialClass::Horn};
    auto paw = [&](Vec3 ball, float size, float sd, int toe) {
        const Vec3 fwd = normalize(Vec3{sd * 0.10f, 0, 1}), up{0, 1, 0}, side = normalize(cross(up, fwd));
        const float pw = 0.060f * H * size, pl = 0.085f * H * size, ph = 0.032f * H * size;
        Part& p = newPart();
        p.id = bodyGroup_;
        const SkinW w = one(toe);
        auto blob = [&](Vec3 c, float ra, float ru, float rv, const Mat& mt, int lat, int lon) {
            Ellipsoid e;
            e.center = c;
            e.axis = fwd;
            e.up = up;
            e.rAxis = ra;
            e.rU = ru;
            e.rV = rv;
            e.lat = lat;
            e.lon = lon;
            emitEllipsoid(p, e, [=](float, float, Vec3) { return w; }, [=](float, float) { return mt; });
        };
        const Vec3 base{ball.x, 0.0f, ball.z};
        const size_t v0 = p.pos.size();
        auto ground = [&] {  // the sampled pad (or the claw tips) rests exactly on the floor
            float lowY = 1e9f;
            for (size_t i = v0; i < p.pos.size(); ++i) lowY = std::min(lowY, p.pos[i].y);
            for (size_t i = v0; i < p.pos.size(); ++i) p.pos[i].y -= lowY - 0.002f;
        };
        if (g.body.hooves) {
            blob(base + Vec3{0, 0.85f * ph, 0.2f * pl}, 0.75f * pl, 0.85f * pw, 0.85f * ph, hoofM, 4, lod.limbSeg);
            ground();
            return;
        }
        blob(base + Vec3{0, ph, -0.05f * pl}, pl, pw, ph, skin(), lod.sparseRings ? 4 : 5, lod.limbSeg);
        const float clawK = g.body.claws ? 1.0f : 0.6f;
        for (int k = 0; k < 4; ++k) {
            const float off = (float(k) - 1.5f) / 1.5f;
            const Vec3 c = base + fwd * ((0.78f - 0.22f * off * off) * pl) + side * (off * 0.74f * pw) + Vec3{0, 0.78f * ph, 0};
            const float tl = 0.36f * pl * (1.0f - 0.15f * std::fabs(off));
            blob(c, tl, 0.27f * pw, 0.78f * ph, skin(), 3, 6);
            const Vec3 tipP = c + fwd * (0.75f * tl) + Vec3{0, 0.1f * ph, 0};
            const Vec3 cd = normalize(fwd * 1.0f + Vec3{0, -0.42f, 0} + side * (0.15f * off));
            emitSpike(p, tipP, cd, Vec3{0, -1, 0}, 0.050f * H * size * clawK, 0.020f * H * size, 0, w, clawM);
        }
        ground();
    };
    const float wk = clamp(0.5f * (g.body.shoulderWidth + g.body.chestWidth), 0.8f, 1.4f);
    const float rf = 0.082f * H * (1.0f + 0.3f * mu) * wk, rh = 0.074f * H * (1.0f + 0.3f * mu) * wk;
    const float fs = clamp(g.body.footSize, 0.8f, 1.3f), hs = clamp(g.body.handSize, 0.8f, 1.4f);
    for (int s = 0; s < 2; ++s) {
        const float sd = s == 0 ? 1.0f : -1.0f;
        {  // foreleg: shoulder blade, heavy upper arm and forearm, a thick wrist and pastern
            const Vec3 a = m.hipJoint[s], b = m.knee[s], c = m.ankle[s], d = m.ball[s];
            leg({{a + Vec3{-sd * 0.02f * H, 0.11f * H, -0.07f * H}, rf * 1.10f, 0.0f}, {a, rf * 1.12f, 0.10f}, {lerp(a, b, 0.5f), rf * 1.0f, 0.24f},
                 {b + Vec3{0, 0.01f * H, -0.012f * H}, rf * 0.80f, 0.38f}, {lerp(b, c, 0.30f), rf * 0.84f, 0.50f}, {lerp(b, c, 0.72f), rf * 0.60f, 0.64f},
                 {c, rf * 0.52f, 0.76f}, {lerp(c, d, 0.55f) + Vec3{0, 0.03f * H, 0}, rf * 0.50f, 0.90f}},
                0.38f, 0.76f, chest, r.legs[size_t(s)]);
            paw(d, 1.15f * fs * std::sqrt(hs), sd, r.legs[size_t(s)].tip);
        }
        {  // hind leg: big thigh, the stifle forward, a lean shank to the hock, long metatarsal
            const Vec3 a = m.hindHip[s], b = m.hindKnee[s], c = m.hindAnkle[s], d = m.hindBall[s];
            leg({{a + Vec3{-sd * 0.02f * H, 0.08f * H, 0.03f * H}, rh * 1.15f, 0.0f}, {a, rh * 1.15f, 0.10f}, {lerp(a, b, 0.5f), rh * 1.02f, 0.22f},
                 {b, rh * 0.74f, 0.34f}, {lerp(b, c, 0.32f), rh * 0.72f, 0.44f}, {lerp(b, c, 0.78f), rh * 0.46f, 0.60f}, {c, rh * 0.42f, 0.72f},
                 {lerp(c, d, 0.5f), rh * 0.38f, 0.84f}, {d + Vec3{0, 0.035f * H, -0.01f * H}, rh * 0.42f, 0.96f}},
                0.34f, 0.72f, hips, r.legs[size_t(s + 2)]);
            paw(d, 0.95f * fs, sd, r.legs[size_t(s + 2)].tip);
        }
    }
}

// ---------------------------------------------------------------- living world (orb body)

// A colossus of stone: a broad boulder from the hips to the crown swallows torso and
// skull, the face set in its upper front; boulders crowd its shoulders and back, clusters
// of rock armour its forearms, fists and shins, and a few stones circle it. Skinned up the
// spine so it leans and turns with the body.
void Builder::buildOrbBody() {
    if (!g.body.orb || bonesOnly_ || g.body.centaur || g.body.lowerBody || beast_) return;
    const float h = m.h;
    const RigInfo& r = ch.rig;
    const Vec3 R = m.orbR;
    Vec3 c = m.orbC;
    {  // the face (down to the jaw) stands just proud of the boulder's front
        const Vec3 face = headPoint(0.5f * kPi, 0.30f);
        const float dy = clamp((face.y - c.y) / R.y, -0.9f, 0.9f);
        c.z = std::min(c.z, face.z - 0.06f * h - R.z * std::sqrt(1.0f - dy * dy));
    }
    const int hips = r.hips, spine = r.spine, chest = r.chest, neck = r.neck;
    const float y0 = c.y - R.y, y1 = c.y + R.y;
    auto wf = [=](float, float, Vec3 p) {
        const float t = saturate((p.y - y0) / (y1 - y0));
        return chainWeights({{0.10f, hips}, {0.40f, spine}, {0.70f, chest}, {0.95f, neck}}, t);
    };
    ch.height = std::max(ch.height, y1);
    const uint64_t sd = g.seed;
    const Vec3 rock = skin().color;
    auto rockM = [=](float k) { return Mat{rock * k, MaterialClass::Skin}; };
    Part& p = newPart();
    p.id = bodyGroup_;
    auto blob = [&](Part& pp, Vec3 cc, Vec3 axis, float ra, float ru, float rv, const std::function<SkinW(float, float, Vec3)>& w, float shade, int lat,
                    int lon) {
        Ellipsoid e;
        e.center = cc;
        e.axis = axis;
        e.up = std::fabs(axis.y) > 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        e.rAxis = ra;
        e.rU = ru;
        e.rV = rv;
        e.lat = lat;
        e.lon = lon;
        emitEllipsoid(pp, e, w, [=](float, float) { return rockM(shade); });
    };
    // The boulder trunk (exact skin colour: magma or crystal skin cracks it like the rest).
    {
        Ellipsoid e;
        e.center = c;
        e.axis = {0, 1, 0};
        e.up = {0, 0, 1};
        e.rAxis = R.y;
        e.rU = R.x;
        e.rV = R.z;
        e.lat = lod.torsoSeg >= 16 ? 12 : 9;
        e.lon = lod.torsoSeg >= 16 ? 20 : 14;
        emitEllipsoid(p, e, wf, [this](float, float) { return skin(); });
    }
    auto surf = [&](float az, float el) {  // a point on the boulder: az 0 front, el from the equator
        return c + Vec3{R.x * std::sin(az) * std::cos(el), R.y * std::sin(el), R.z * std::cos(az) * std::cos(el)};
    };
    // Boulders: a heavy brow over the face, then the shoulders, crown and back.
    const Vec3 brow = surf(0.0f, std::asin(clamp((m.headOrigin.y + 0.78f * h - c.y) / R.y, -0.95f, 0.95f)));
    blob(p, brow + Vec3{0, 0, -0.08f * h}, {1, 0, 0}, 0.62f * h, 0.24f * h, 0.20f * h, wf, 0.92f, 3, 7);
    const int nb = lod.sparseRings ? 9 : 14;
    for (int k = 0; k < nb; ++k) {
        const float u = hash01(sd, 200 + uint64_t(k)), v = hash01(sd, 300 + uint64_t(k)), w = hash01(sd, 400 + uint64_t(k));
        float az = (float(k) + 0.5f * u) / float(nb) * kTau;
        float el = lerp(-0.15f, 1.15f, v);
        if (std::fabs(std::remainder(az, kTau)) < 0.75f && el < 0.95f) az += 0.9f * (az < kPi ? 1.0f : -1.0f);  // keep clear of the face
        const Vec3 q = surf(az, el), n = normalize(Vec3{(q.x - c.x) / (R.x * R.x), (q.y - c.y) / (R.y * R.y), (q.z - c.z) / (R.z * R.z)});
        const float rr = R.y * (0.22f + 0.16f * w);
        blob(p, q - n * (0.35f * rr), n, 0.75f * rr, rr, 0.9f * rr, wf, 0.82f + 0.2f * u, 3, 6);
    }
    // Rock armour on the arms (upper arm, forearm, a boulder fist) and the shins.
    Part& ap = newPart();
    ap.id = bodyGroup_;
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& am = m.arms[i];
        const LimbRig& lr = r.arms[i];
        const Vec3 out = normalize(Vec3{am.side, 0.25f, -0.15f});
        const float ar = am.radius;
        for (int k = 0; k < 4; ++k) {
            const float f = 0.18f + 0.22f * float(k), hk = hash01(sd, 500 + uint64_t(i * 8 + size_t(k)));
            const Vec3 q = lerp(am.elbow, am.wrist, f) + normalize(out + Vec3{0, 0, hk - 0.5f}) * (0.75f * ar);
            const int bone = lr.lower;
            blob(ap, q, normalize(am.wrist - am.elbow), (0.85f + 0.3f * hk) * ar, (1.0f + 0.3f * hk) * ar, 0.85f * ar,
                 [=](float, float, Vec3) { return one(bone); }, 0.8f + 0.25f * hk, 3, 6);
        }
        {
            const Vec3 q = lerp(am.shoulder, am.elbow, 0.65f) + out * (0.7f * ar);
            const int bone = lr.upper;
            blob(ap, q, normalize(am.elbow - am.shoulder), 1.1f * ar, 1.15f * ar, 0.9f * ar, [=](float, float, Vec3) { return one(bone); }, 0.9f, 3, 6);
        }
        {  // the fist: a knuckled boulder over the hand
            const float Lh = length(am.handTip - am.wrist);
            const int bone = lr.end;
            blob(ap, lerp(am.wrist, am.handTip, 0.5f), am.dir, 0.62f * Lh, 0.55f * Lh, 0.48f * Lh, [=](float, float, Vec3) { return one(bone); }, 0.85f, 4,
                 7);
        }
    }
    for (int s = 0; s < 2; ++s) {
        const LimbRig& lg = r.legs[size_t(s)];
        const int bone = lg.lower;
        const Vec3 q = lerp(m.knee[s], m.ankle[s], 0.35f) + Vec3{0, 0, 0.75f * g.body.calf * h};
        blob(ap, q, normalize(m.ankle[s] - m.knee[s]), 0.9f * g.body.calf * h, 0.95f * g.body.calf * h, 0.6f * g.body.calf * h,
             [=](float, float, Vec3) { return one(bone); }, 0.85f, 3, 6);
    }
    // Moons: stones circling the equator.
    Part& mp = newPart(true);
    mp.id = bodyGroup_;
    for (int k = 0; k < 3; ++k) {
        const float a = kTau * (float(k) + 0.3f * hash01(sd, 70 + uint64_t(k))) / 3.0f + 0.6f;
        const float rr = R.x * (1.45f + 0.2f * hash01(sd, 80 + uint64_t(k)));
        Ellipsoid s;
        s.center = c + Vec3{rr * std::cos(a), R.y * (0.15f + 0.35f * std::sin(2.0f * a + 1.0f)), rr * 0.8f * std::sin(a)};
        s.axis = normalize(Vec3{hash01(sd, 90 + uint64_t(k)) - 0.5f, 1, 0.3f});
        s.up = {0, 0, 1};
        s.rAxis = (0.14f + 0.12f * hash01(sd, 100 + uint64_t(k))) * h;
        s.rU = s.rAxis * 1.2f;
        s.rV = s.rAxis * 0.9f;
        s.lat = 3;
        s.lon = 5;
        emitEllipsoid(mp, s, [=](float, float, Vec3) { return one(chest); },
                      [=](float, float) { return Mat{Vec3{0.30f, 0.28f, 0.27f}, MaterialClass::Leather}; });
    }
}

// Body art (painted, inked, scarred or glowing marks on bare skin) and markings in a
// coat (spots, a dorsal streak, banded limbs). All are clip regions on the body
// surface, so their edges stay clean at any triangle size.
void Builder::buildBodyArt() {
    // Paint and ink belong on bare flesh: none over scales, crystal, metal or bone.
    const int art = g.body.skinKind == 0 && !g.body.skeletal ? g.body.bodyArt : 0, marks = g.body.furMarks;
    if ((!art && !marks) || bonesOnly_ || torsoPart_ >= parts.size()) return;
    const float h = m.h;
    const Vec3 sk = g.palette.skin;
    std::vector<ClipRegion> clips;
    // A band round a limb segment a..b: at tc (0..1 along it), half width hw, zig-zagging k times by amp.
    auto band = [&](Vec3 a, Vec3 b, float r, float tc, float hw, float amp, int k) {
        const Vec3 d = normalize(b - a), e1 = normalize(cross(d, Vec3{0, 0, 1}), Vec3{1, 0, 0}), e2 = cross(d, e1);
        const float L = length(b - a);
        return [=](Vec3 q) {
            const float tt = dot(q - a, d) / L;
            const Vec3 rad = q - (a + d * (tt * L));
            if (length(rad) > 1.5f * r || tt < -0.1f || tt > 1.1f) return 1.0f;
            const float ang = std::atan2(dot(rad, e2), dot(rad, e1));
            return std::fabs((tt - tc) * L - amp * std::sin(float(k) * ang)) - hw;
        };
    };
    // Chevrons repeating along a limb segment between t0 and t1 (a sleeve).
    auto sleeve = [&](Vec3 a, Vec3 b, float r, float t0, float t1, float period, float hw) {
        const Vec3 d = normalize(b - a), e1 = normalize(cross(d, Vec3{0, 0, 1}), Vec3{1, 0, 0}), e2 = cross(d, e1);
        const float L = length(b - a);
        return [=](Vec3 q) {
            const float tt = dot(q - a, d) / L;
            const Vec3 rad = q - (a + d * (tt * L));
            if (length(rad) > 1.5f * r) return 1.0f;
            const float ang = std::atan2(dot(rad, e2), dot(rad, e1));
            const float u = tt * L + 0.45f * r * std::fabs(std::sin(ang));
            const float f = std::fabs(std::fmod(u + 100.0f * period, period) - 0.5f * period) - hw;
            return std::max(f, std::max(t0 - tt, tt - t1) * L);
        };
    };
    // A stroke over the body through points, tapering at both ends.
    auto stroke = [&](std::vector<Vec3> pts, float hw) {
        return [=](Vec3 q) {
            float best = 1e9f;
            const size_t n = pts.size();
            for (size_t i = 0; i + 1 < n; ++i) {
                const Vec3 ab = pts[i + 1] - pts[i];
                const float u = clamp(dot(q - pts[i], ab) / std::max(dot(ab, ab), 1e-9f), 0.0f, 1.0f);
                const float gu = (float(i) + u) / float(n - 1);
                const float w = hw * std::sqrt(std::max(0.05f, std::sin(kPi * gu)));
                best = std::min(best, length(q - (pts[i] + ab * u)) - w);
            }
            return best;
        };
    };
    // A stroke over the torso through (t, th) points (th = pi/2 the front), measured on the
    // torso's own parameters so it follows the sculpted surface whatever stands over it.
    auto tstroke = [&](std::vector<Vec2> pts, float hw) {
        const float tl = m.torsoLen;
        return [=, this](Vec3 q) {
            const float t = torsoT(q.y), tc = clamp(t, -0.06f, 1.1f);
            if (std::fabs(q.x) > 1.2f * cHw_(tc) + 0.03f * h) return 1.0f;  // not the arms
            const float rw = cHw_(tc);
            const Vec2 Q{std::atan2(q.z - cZc_(tc), q.x) * rw, t * tl};
            float best = 1e9f;
            const size_t n = pts.size();
            for (size_t i = 0; i + 1 < n; ++i) {
                const Vec2 A{pts[i].y * rw, pts[i].x * tl}, B{pts[i + 1].y * rw, pts[i + 1].x * tl}, ab = B - A;
                const float u = clamp(dot(Q - A, ab) / std::max(dot(ab, ab), 1e-9f), 0.0f, 1.0f);
                const float gu = (float(i) + u) / float(n - 1);
                best = std::min(best, length(Q - (A + ab * u)) - hw * std::sqrt(std::max(0.08f, std::sin(kPi * gu))));
            }
            return best;
        };
    };
    const float sx = (g.seed % 2 == 0) ? 1.0f : -1.0f;
    Rng r = Rng(g.seed).fork(73);
    const Vec3 col = g.body.bodyArtColor;
    const Mat paint{col, MaterialClass::Skin};
    if (!beast_) {
        switch (art) {
            case 1:  // tribal bands: a zig-zag and a thin band on the upper arms, one on the forearms
                for (const ArmMarks& a : m.arms) {
                    if (a.pair != 0 || (a.side != sx && g.seed % 3 == 0)) continue;
                    clips.push_back({band(a.shoulder, a.elbow, a.radius, 0.50f, 0.032f * h, 0.030f * h, 4), paint, 2});
                    clips.push_back({band(a.shoulder, a.elbow, a.radius, 0.72f, 0.018f * h, 0.0f, 0), paint, 2});
                    clips.push_back({band(a.elbow, a.wrist, a.radius * 0.85f, 0.70f, 0.026f * h, 0.020f * h, 4), paint, 2});
                }
                break;
            case 2: {  // war paint: three claw strokes across the chest and a broad band round each upper arm
                for (int k = 0; k < 3; ++k) {
                    const float o = float(k - 1);
                    std::vector<Vec2> pts;
                    for (int i = 0; i <= 3; ++i) {
                        const float u = float(i) / 3.0f;
                        pts.push_back({lerp(0.94f, 0.34f, u) - 0.07f * o, kPi / 2 + sx * (lerp(0.85f, -0.30f, u) - 0.22f * o)});
                    }
                    clips.push_back({tstroke(pts, 0.040f * h), paint, 2});
                }
                for (const ArmMarks& a : m.arms)
                    if (a.pair == 0) clips.push_back({band(a.shoulder, a.elbow, a.radius, 0.45f, 0.045f * h, 0.0f, 0), paint, 1});
                break;
            }
            case 3:  // rune sleeve: chevrons from shoulder to wrist down one arm
            case 6:  // glowing runes: both arms and two lines down the chest
                for (const ArmMarks& a : m.arms) {
                    if (a.pair != 0 || (art == 3 && a.side != sx)) continue;
                    const Mat mt = art == 6 ? Mat{col, MaterialClass::Glow} : paint;
                    clips.push_back({sleeve(a.shoulder, a.elbow, a.radius, 0.20f, 0.92f, 0.16f * h, 0.022f * h), mt, 2});
                    clips.push_back({sleeve(a.elbow, a.wrist, a.radius * 0.85f, 0.08f, 0.82f, 0.15f * h, 0.020f * h), mt, 2});
                }
                if (art == 6)
                    for (float sd : {1.0f, -1.0f}) {
                        const std::vector<Vec2> pts{{0.97f, kPi / 2 + sd * 0.62f}, {0.82f, kPi / 2 + sd * 0.32f}, {0.72f, kPi / 2 + sd * 0.48f},
                                                    {0.56f, kPi / 2 + sd * 0.24f}, {0.40f, kPi / 2 + sd * 0.34f}};
                        clips.push_back({tstroke(pts, 0.020f * h), Mat{col, MaterialClass::Glow}, 2});
                    }
                break;
            case 4: {  // battle scars: pale seams over the chest, belly and arms
                const Mat scarM{lerp(sk, Vec3{0.70f, 0.34f, 0.32f}, 0.55f), MaterialClass::Skin};
                for (int k = 0; k < 4; ++k) {
                    const float t = r.range(0.40f, 0.88f), th = kPi / 2 + r.range(-0.9f, 0.9f), dt = r.range(-0.14f, 0.14f), dth = r.range(0.25f, 0.5f);
                    clips.push_back({tstroke({{t, th - dth}, {t + 0.5f * dt, th}, {t + dt, th + dth}}, 0.028f * h), scarM, 2});
                }
                for (const ArmMarks& a : m.arms) {
                    if (a.pair != 0 || !r.chance(0.6f)) continue;
                    const Vec3 d = normalize(a.elbow - a.shoulder), out = normalize(Vec3{a.side, 0, 0.6f} - d * dot(Vec3{a.side, 0, 0.6f}, d));
                    const float t0 = r.range(0.3f, 0.6f);
                    const Vec3 p0 = lerp(a.shoulder, a.elbow, t0) + out * a.radius, p1 = lerp(a.shoulder, a.elbow, t0 + 0.2f) + out * a.radius + Vec3{0, 0, 0.3f * a.radius};
                    clips.push_back({stroke({p0, (p0 + p1) * 0.5f + out * (0.1f * a.radius), p1}, 0.024f * h), scarM, 2});
                }
                break;
            }
            case 5: {  // bone paint: ribs and a breastbone over the chest
                const float tl = m.torsoLen;
                clips.push_back({[=, this](Vec3 q) {
                    const float t = torsoT(q.y);
                    if (t < 0.45f || t > 0.95f) return 1.0f;
                    const float tc = clamp(t, -0.06f, 1.1f);
                    if (std::fabs(q.x) > 1.15f * cHw_(tc) + 0.02f * h || q.z < cZc_(tc)) return 1.0f;  // the chest front only, not the arms
                    const float phi = std::atan2(std::fabs(q.x), q.z - cZc_(tc));                    // 0 centre .. pi/2 side
                    const float sternum = std::max(std::fabs(q.x) - 0.026f * h, std::max(0.58f - t, t - 0.92f) * tl);
                    float ribs = 1e9f;
                    for (int i = 0; i < 5; ++i) {
                        const float ti = 0.86f - 0.065f * float(i) - 0.07f * smoothstep(0.2f, 1.3f, phi);
                        ribs = std::min(ribs, std::fabs(t - ti) * tl - 0.022f * h * (1.0f - 0.35f * phi));
                    }
                    ribs = std::max(ribs, std::max(0.14f - phi, phi - 1.30f) * 0.1f * h);
                    return std::min(sternum, ribs);
                }, paint, 2});
                break;
            }
            default: break;
        }
    }
    if (marks) {
        const Mat darkM{sk * 0.52f, MaterialClass::Hair};
        if (marks == 1 || (marks == 3 && beast_)) {  // spots: a scatter of rounded blotches
            const float cell = (beast_ ? 0.055f * m.H : 0.20f * h), rad = 0.30f * cell;
            const uint64_t sd = g.seed;
            clips.push_back({[=](Vec3 q) {
                const float fx = std::floor(q.x / cell), fy = std::floor(q.y / cell), fz = std::floor(q.z / cell);
                float best = 1e9f;
                for (int k = 0; k < 27; ++k) {
                    const float cx = fx + float(k % 3 - 1), cy = fy + float(k / 3 % 3 - 1), cz = fz + float(k / 9 - 1);
                    const uint64_t id = uint64_t(int64_t(cx) * 73856093) ^ uint64_t(int64_t(cy) * 19349663) ^ uint64_t(int64_t(cz) * 83492791);
                    const Vec3 fp{(cx + 0.2f + 0.6f * hash01(sd, id)) * cell, (cy + 0.2f + 0.6f * hash01(sd, id + 1)) * cell,
                                  (cz + 0.2f + 0.6f * hash01(sd, id + 2)) * cell};
                    best = std::min(best, length(q - fp) - rad * (0.7f + 0.6f * hash01(sd, id + 3)));
                }
                return best;
            }, darkM, 1});
        } else if (marks == 2) {  // a dark streak down the spine
            const Mat darkM{sk * 0.34f, MaterialClass::Hair};
            if (beast_) {
                clips.push_back({[=, this](Vec3 q) {
                    const float t = (q.z - m.beastZR) / (m.beastZF - m.beastZR);
                    if (t < 0.0f || t > 1.0f) return 1.0f;
                    const BeastRing br = beastRing(t);
                    return std::fabs(q.x) - 0.35f * br.w * smoothstep(0.0f, 0.15f, t) * smoothstep(1.0f, 0.8f, t) - (q.y < br.y ? 1.0f : 0.0f);
                }, darkM, 1});
            } else {
                clips.push_back({[=, this](Vec3 q) {
                    const float t = torsoT(q.y);
                    if (t < 0.15f || t > 1.05f) return 1.0f;
                    const float tc = clamp(t, -0.06f, 1.1f);
                    if (q.z > cZc_(tc)) return 1.0f;
                    return std::fabs(q.x) - 0.42f * cHw_(tc) * (0.6f + 0.4f * smoothstep(0.2f, 0.9f, t));
                }, darkM, 1});
            }
        } else if (marks == 3) {  // banded limbs: dark rings round the arms and legs
            for (const ArmMarks& a : m.arms)
                for (float tc : {0.30f, 0.55f, 0.80f}) {
                    clips.push_back({band(a.shoulder, a.elbow, a.radius, tc, 0.025f * h, 0.012f * h, 3), darkM, 1});
                    clips.push_back({band(a.elbow, a.wrist, a.radius * 0.85f, tc, 0.022f * h, 0.010f * h, 3), darkM, 1});
                }
            for (int s = 0; s < 2; ++s)
                for (float tc : {0.35f, 0.65f}) {
                    clips.push_back({band(m.hipJoint[s], m.knee[s], g.body.thigh * h, tc, 0.03f * h, 0.015f * h, 3), darkM, 1});
                    clips.push_back({band(m.knee[s], m.ankle[s], g.body.calf * h, tc, 0.026f * h, 0.012f * h, 3), darkM, 1});
                }
        }
    }
    // Triangle budget: the clips refine where their edges cross the body; dense bodies cut without refining.
    size_t tris = 0;
    for (const Part& pp : parts) tris += pp.tris.size();
    for (ClipRegion& c : clips) {
        if (tris > 16000) c.refine = 0;
        else if (tris > 12000) c.refine = std::min(c.refine, 1);
        parts[torsoPart_].clips.push_back(std::move(c));
    }
}

void Builder::buildPiercings() {
    const FaceStyle& f = g.face;
    if ((!f.piercings && !f.whiskers) || bonesOnly_) return;
    const float h = m.h;
    const int head = ch.rig.head;
    const Mat metal{(g.seed % 3 == 0) ? Vec3{0.66f, 0.66f, 0.70f} : Vec3{0.74f, 0.57f, 0.26f}, MaterialClass::Metal};
    Part& p = newPart();
    p.id = headPartId_;
    p.smoothAlways = true;
    const SkinW w = one(head);
    auto vtx = [&](Vec3 q) {
        p.pos.push_back(q);
        p.w.push_back(w);
        p.uv.push_back({});
        return uint32_t(p.pos.size() - 1);
    };
    // A hoop: a torus of major radius R and wire radius r round `axis`.
    auto hoop = [&](Vec3 c, Vec3 axis, float R, float r) {
        axis = normalize(axis, Vec3{1, 0, 0});
        const Vec3 e1 = normalize(cross(axis, Vec3{0, 1, 0.3f}), Vec3{0, 0, 1}), e2 = cross(axis, e1);
        const int N = 10, S = 4;
        const uint32_t i0 = uint32_t(p.pos.size());
        for (int i = 0; i < N; ++i) {
            const float a = kTau * float(i) / float(N);
            const Vec3 rd = e1 * std::cos(a) + e2 * std::sin(a), ctr = c + rd * R;
            for (int j = 0; j < S; ++j) {
                const float b = kTau * float(j) / float(S);
                vtx(ctr + (rd * std::cos(b) + axis * std::sin(b)) * r);
            }
        }
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < S; ++j) {
                const uint32_t A = i0 + uint32_t(i * S + j), B = i0 + uint32_t(((i + 1) % N) * S + j), C = i0 + uint32_t(((i + 1) % N) * S + (j + 1) % S),
                               D = i0 + uint32_t(i * S + (j + 1) % S);
                const Vec3 mid = (p.pos[A] + p.pos[C]) * 0.5f;
                const float a = kTau * (float(i) + 0.5f) / float(N);
                const Vec3 wire = c + (e1 * std::cos(a) + e2 * std::sin(a)) * R;
                const bool flip = dot(cross(p.pos[B] - p.pos[A], p.pos[C] - p.pos[A]), mid - wire) < 0;
                p.tris.push_back(flip ? Part::Tri{A, C, B, metal} : Part::Tri{A, B, C, metal});
                p.tris.push_back(flip ? Part::Tri{A, D, C, metal} : Part::Tri{A, C, D, metal});
            }
    };
    auto stud = [&](Vec3 c, Vec3 n, float r) {
        Ellipsoid e;
        e.center = c;
        e.axis = normalize(n, Vec3{0, 0, 1});
        e.up = std::fabs(e.axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        e.rAxis = 0.8f * r;
        e.rU = e.rV = r;
        e.lat = 3;
        e.lon = 6;
        emitEllipsoid(p, e, [=](float, float, Vec3) { return w; }, [=](float, float) { return metal; });
    };
    const float sx = (g.seed % 2 == 0) ? 1.0f : -1.0f;
    if ((f.piercings & 1) && g.body.ears != EarStyle::None && g.body.ears != EarStyle::Fin) {
        // Hoops through the lower edge of the ear blade (as built in buildEars): one at the lobe,
        // a row climbing a long elven ear.
        const EarShape& E = g.body.ear;
        const float es = f.earSize, len = E.length * h * es, wid = E.width * h * std::sqrt(es);
        const bool longEar = E.length > 0.45f;
        for (float sd : {1.0f, -1.0f}) {
            if (!longEar && sd != sx && g.seed % 4 == 0) continue;
            const float th = sd > 0 ? E.baseIn : kPi - E.baseIn;
            const Vec3 base = headSurface(th, E.baseS, -0.03f * h) + Vec3{0, 0, E.baseS > 0.6f ? -0.02f * h : -0.05f * h};
            const Vec3 dir = normalize(Vec3{sd * std::cos(E.up) * std::cos(E.back), std::sin(E.up), -std::cos(E.up) * std::sin(E.back)});
            const Vec3 bladeUp = normalize(Vec3{0, 1, 0} - dir * dir.y, Vec3{0, 0, 1});
            auto at = [&](float t) { return base + dir * (len * t) + Vec3{0, -1, 0} * (E.droop * len * t * t); };
            const std::vector<float> ts = longEar ? std::vector<float>{0.30f, 0.46f, 0.62f} : std::vector<float>{0.30f};
            for (float t : ts) {
                const float wsAt = t < 0.28f ? 0.97f : lerp(1.0f, 0.72f, (t - 0.28f) / 0.30f);
                const Vec3 edge = at(t) - bladeUp * (0.85f * wid * wsAt);
                const float R = (longEar ? 0.030f : 0.040f) * h;
                hoop(edge - bladeUp * (0.8f * R), normalize(cross(dir, bladeUp)), R, 0.0065f * h);
            }
        }
    }
    if (f.piercings & 2) {  // a bar through one eyebrow: two studs
        const float th = std::acos(clamp(sx * 0.17f / 0.30f, -1.0f, 1.0f));
        for (float s : {0.505f, 0.565f}) {
            const Vec3 q = headSurface(th, s, 0.0f);
            stud(q + headNormalDir(q) * (0.008f * h), headNormalDir(q), 0.016f * h);
        }
    }
    if (f.piercings & 4) {  // a ring through the lower lip, at one corner
        const float th = std::acos(clamp(sx * 0.040f / 0.30f, -1.0f, 1.0f));
        const Vec3 q = headSurface(th, 0.20f, 0.0f);
        hoop(q + headNormalDir(q) * (0.010f * h) + Vec3{0, -0.012f * h, 0}, Vec3{1, 0, 0}, 0.020f * h, 0.0045f * h);
    }
    if (f.whiskers) {  // long whiskers fanning from the sides of the muzzle
        Part& wp = newPart();
        wp.id = headPartId_;
        const Mat wm{lerp(g.palette.skin, Vec3{0.9f, 0.88f, 0.82f}, 0.6f), MaterialClass::Hair};
        for (float sd : {1.0f, -1.0f})
            for (int k = 0; k < 3; ++k) {
                const Vec3 b0 = headSurface(kPi / 2 - sd * 0.55f, 0.27f + 0.035f * float(k), -0.004f * h);
                const Vec3 d = normalize(Vec3{sd * 1.0f, -0.20f + 0.14f * float(k), -0.15f});
                std::vector<Vec3> pts;
                for (int i = 0; i <= 4; ++i) {
                    const float u = float(i) / 4.0f;
                    pts.push_back(b0 + d * (0.36f * h * u) + Vec3{0, -0.05f * h * u * u, 0});
                }
                emitTube(wp, pts, [=](float t) { return 0.0055f * h * (1.0f - 0.8f * t); }, 4, [=](float, float, Vec3) { return w; },
                         [=](float, float) { return wm; });
            }
    }
}

}  // namespace cg::detail
