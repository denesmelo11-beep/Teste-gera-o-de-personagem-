// Body generation. The body is sculpted from lofts of superellipse rings along
// the bones (torso, limbs, neck), welded into one smooth low-poly mesh. Every
// loft carries skin weights derived from its parameter along the bone chain.
// Bodies are base meshes: only a modesty layer (underwear), equipment is
// layered on top by the game.
#include <algorithm>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>

#include "generator_impl.hpp"

namespace cg {

int Skeleton::add(std::string name, int parent, Vec3 world) {
    Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.restWorld = world;
    b.restLocal = parent >= 0 ? world - bones[size_t(parent)].restWorld : world;
    bones.push_back(std::move(b));
    return int(bones.size()) - 1;
}

int Skeleton::find(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == name) return int(i);
    return -1;
}

namespace detail {

LodSpec lodSpec(int lod) {
    // Lean budgets: flat facets carry the forms, so rings sit on landmarks only.
    switch (lod) {
        case 0: return {16, 20, 8, 8, 6, 6, 16, 5, false};
        case 1: return {12, 12, 6, 6, 5, 5, 12, 4, true};
        default: return {8, 12, 5, 5, 4, 4, 10, 3, true};
    }
}

namespace {

// Piecewise weights along a chain: keys {t, bone}.
}  // namespace
SkinW chainWeights(std::initializer_list<std::pair<float, int>> keys, float t) {
    const auto* k = keys.begin();
    size_t n = keys.size();
    if (t <= k[0].first) return one(k[0].second);
    for (size_t i = 0; i + 1 < n; ++i)
        if (t <= k[i + 1].first) {
            float f = (t - k[i].first) / std::max(1e-6f, k[i + 1].first - k[i].first);
            return blend(k[i].second, k[i + 1].second, smoothstep(0.0f, 1.0f, f));
        }
    return one(k[n - 1].second);
}

// Ring inserted exactly at a colour boundary so edges stay crisp.
std::vector<float> mergeRings(std::vector<float> base, const std::vector<float>& bounds, bool sparse) {
    if (sparse) {
        std::vector<float> keep;
        for (size_t i = 0; i < base.size(); ++i)
            if (i % 2 == 0 || i + 1 == base.size()) keep.push_back(base[i]);
        base = keep;
    }
    std::vector<float> out;
    for (float t : base) {
        bool near = false;
        for (float b : bounds) near |= std::fabs(t - b) < 0.015f;
        if (!near) out.push_back(t);
    }
    out.insert(out.end(), bounds.begin(), bounds.end());
    std::sort(out.begin(), out.end());
    return out;
}

Vec3 orthoFront(Vec3 d) { return normalize(Vec3{0, 0, 1} - d * d.z, {1, 0, 0}); }

// A horn-like growth rooted at `base` (sunk a little into the surface) pointing
// along `dir`: kind 0 a faceted spike curving towards `sweep`, 1 a thin bony plate
// (a diamond, flat across `fore`), 2 a short blunt knob.
void emitSpike(Part& p, Vec3 base, Vec3 dir, Vec3 sweep, float len, float wid, int kind, const SkinW& w, const Mat& m) {
    dir = normalize(dir, {0, 0, -1});
    const Vec3 fore = normalize(sweep - dir * dot(sweep, dir), orthoFront(dir));
    const Vec3 side = normalize(cross(dir, fore));
    const Mat tipM{m.color * 0.72f, m.cls};
    auto paint = [=](float t, float) { return t > 0.55f ? tipM : m; };
    auto weight = [=](float, float, Vec3) { return w; };
    if (kind == 2) {  // knob
        Ellipsoid e;
        e.center = base + dir * (0.35f * len);
        e.axis = dir;
        e.up = fore;
        e.rAxis = 0.55f * len;
        e.rU = 0.85f * wid;
        e.rV = wid;
        e.lat = 3;
        e.lon = 6;
        emitEllipsoid(p, e, weight, paint);
        return;
    }
    Loft L;
    const int n = kind == 1 ? 4 : 3;
    const float ts[] = {0.0f, 0.38f, 0.72f};
    for (int k = 0; k < 3; ++k) {
        const float t = ts[k];
        // The spike bends towards `sweep` as it rises; the plate stays straight.
        const Vec3 c = base + dir * (len * (t - 0.18f)) + fore * (kind == 0 ? 0.35f * len * t * t : 0.0f);
        const float rr = wid * (1.0f - 0.78f * t);
        SuperRing sr = kind == 1 ? SuperRing{0.22f * wid * (1.0f - 0.5f * t), 0, rr * 1.15f, rr * 1.15f, 1.3f, 1.3f}
                                 : SuperRing{rr, 0, rr, rr};
        L.rings.push_back(makeRing(c, side, fore, sr, kind == 1 ? n : 5));
        L.t.push_back(t);
    }
    L.poleEnd = true;
    L.endPole = base + dir * (len * 0.82f) + fore * (kind == 0 ? 0.32f * len : 0.06f * len);
    L.tEnd = 1;
    emitLoft(p, L, weight, paint);
}

Builder::Builder(const Genome& genome, const GenOptions& o) : g(genome), opt(o), lod(lodSpec(o.lod)) {
    decay_ = std::max(o.decay, genome.body.skeletal ? 1.0f : 0.0f);
    bonesOnly_ = decay_ >= 0.97f;
    computeMarks();
    const BodyShape& b = g.body;
    const float h = m.h, S = b.shoulderWidth * h, C = b.chestWidth * h, W = b.waistWidth * h, P = b.hipWidth * h;
    const float D = b.chestDepth * h, post = b.posture, lat = 0.10f * h * b.anat.lats * b.anat.lats * (1.0f - 0.6f * g.femininity), nR = b.neckThick * h;
    const float hunch = b.hunch * h;
    // Strong hunch (trolls, tauren, orcs): a real stoop, the upper torso leaning forward.
    const float stoop = std::max(0.0f, b.hunch - 0.4f) * h;
    // Below the hip line (t < kTorsoBase) these curves only guide the hip and glute
    // volume of the thigh junction; the thighs carry the surface there.
    // Ribcage: men keep the chest wide down to the lats and taper in a straight V;
    // women narrow under the bust into a high, defined waist. Above the shoulder line
    // the trapezius runs as a straight slope from the acromion to the side of the neck.
    const Anatomy& an = b.anat;
    const float fem = g.femininity, trap = 0.035f * h * an.traps;
    const float rib55 = lerp(0.48f, 0.62f, fem), rib66 = lerp(0.80f, 0.95f, fem);
    cHw_ = Curve{{-0.06f, 0.82f * P}, {0.0f, 0.92f * P},  {0.06f, 0.98f * P}, {0.13f, P},
                 {0.23f, lerp(P, W, 0.38f)}, {0.32f, lerp(P, W, 0.75f)}, {0.42f, W}, {0.55f, lerp(W, C, rib55)}, {0.66f, lerp(W, C, rib66)},
                 {0.76f, C + 0.6f * lat}, {0.85f, lerp(C, 0.78f * S, 0.45f) + lat}, {0.90f, std::max(0.74f * S, 0.95f * C)},
                 // Trapezius: a straight slope from the top of the shoulder joint to the side of
                 // the neck (slightly convex with the muscle); the deltoid cap of the arm,
                 // not the torso, forms the shoulder's outline.
                 {0.94f, lerp(std::max(0.74f * S, 0.95f * C), 1.1f * nR, 0.24f) + 0.5f * trap},
                 {0.98f, lerp(std::max(0.74f * S, 0.95f * C), 1.1f * nR, 0.48f) + 0.9f * trap},
                 {1.02f, lerp(std::max(0.74f * S, 0.95f * C), 1.1f * nR, 0.72f) + trap},
                 {1.06f, lerp(std::max(0.74f * S, 0.95f * C), 1.1f * nR, 0.94f) + 0.4f * trap}, {1.085f, 1.04f * nR}, {1.10f, nR}};
    cDf_ = Curve{{-0.06f, 0.74f * D}, {0.0f, 0.86f * D}, {0.10f, 0.86f * D}, {0.20f, 0.84f * D}, {0.30f, 0.76f * D},
                 {0.42f, 0.72f * D},  {0.55f, 0.80f * D}, {0.66f, 0.88f * D}, {0.76f, 0.95f * D}, {0.86f, 0.92f * D - 0.02f * hunch},
                 {0.905f, 0.84f * D - 0.03f * hunch}, {0.945f, 0.76f * D}, {0.975f, 0.66f * D}, {1.0f, 0.56f * D},
                 {1.03f, lerp(0.56f * D, 0.92f * nR, 0.55f)}, {1.06f, 0.95f * nR}, {1.10f, 0.92f * nR}};
    // Age rounds the upper back (kyphosis).
    cDb_ = Curve{{-0.06f, 0.70f * D}, {0.0f, 0.80f * D}, {0.10f, 0.82f * D}, {0.20f, 0.80f * D}, {0.30f, 0.68f * D},
                 {0.42f, 0.62f * D},  {0.55f, 0.70f * D}, {0.66f, 0.80f * D}, {0.76f, 0.86f * D + 0.03f * hunch},
                 {0.86f, 0.88f * D + 0.08f * hunch}, {0.905f, 0.86f * D + 0.08f * hunch}, {0.945f, 0.80f * D + 0.06f * hunch},
                 {0.975f, 0.72f * D + 0.05f * hunch}, {1.0f, 0.64f * D + 0.05f * hunch + 0.3f * trap},
                 {1.03f, lerp(0.64f * D, nR, 0.45f) + 0.04f * hunch + 0.6f * trap}, {1.06f, 1.08f * nR + 0.3f * trap}, {1.10f, nR}};
    cZc_ = Curve{{-0.06f, -0.01f * h * post}, {0.10f, -0.035f * h * post}, {0.20f, -0.035f * h * post},
                 {0.42f, 0.0f}, {0.66f, 0.02f * h * post + 0.05f * stoop}, {0.80f, 0.025f * h * post + 0.12f * stoop},
                 {1.0f, -0.01f * h + 0.06f * hunch + 0.20f * stoop}, {1.10f, -0.02f * h + 0.10f * hunch + 0.28f * stoop}};
    initHead();
}

void Builder::computeMarks() {
    const BodyShape& b = g.body;
    m.H = b.height;
    m.h = m.H / b.headsTall;
    const float h = m.h, mu = b.muscle;
    const float stoop = std::max(0.0f, b.hunch - 0.4f);
    m.chinY = m.H - h - 0.04f * b.hunch * h - 0.30f * stoop * h;
    m.shoulderY = m.chinY - b.neckLength * h;
    m.crotchY = m.H * b.legRatio;
    if (m.shoulderY - m.crotchY < 0.9f * h) m.crotchY = m.shoulderY - 0.9f * h;
    m.torsoLen = m.shoulderY - m.crotchY;
    m.hipJointY = m.crotchY + 0.11f * m.torsoLen + 0.06f * h;
    m.waistY = m.crotchY + 0.42f * m.torsoLen;
    m.chestY = m.crotchY + 0.62f * m.torsoLen;
    m.ankleY = 0.24f * h * b.footSize;
    m.kneeY = m.ankleY + (m.hipJointY - m.ankleY) * 0.57f;  // stylised: long shins, high knees

    const float P = b.hipWidth * h, thighR = b.thigh * h, calfR = b.calf * h * (1 + 0.2f * mu);
    const float kneeR = std::max(0.50f * thighR, 0.75f * calfR), ankleR = 0.44f * calfR;
    m.footLen = 0.92f * h * b.footSize;
    m.footHalfW = std::max(0.21f * m.footLen, ankleR * 1.25f);
    for (int s = 0; s < 2; ++s) {
        float sd = s == 0 ? 1.0f : -1.0f;
        // Thighs side by side under the pelvis; never overlapping at the crotch.
        const float thighTop = std::max(1.02f * thighR, 0.5f * P);
        m.hipJoint[s] = {sd * std::max(0.50f * P, 0.98f * lerp(thighTop, thighR, 0.4f) + 0.012f * h), m.hipJointY, -0.01f * h};
        m.knee[s] = {sd * std::max(0.45f * P, kneeR * 1.15f), m.kneeY, 0.02f * h};
        m.ankle[s] = {sd * std::max(0.44f * P, ankleR * 1.6f), m.ankleY, -0.01f * h};
        m.ball[s] = m.ankle[s] + Vec3{sd * 0.03f * m.footLen, -0.7f * m.ankleY, 0.53f * m.footLen};
        if (b.digitigrade) {
            // Walking on the toes: the knee rides high and forward, the shin runs down and
            // back to a raised hock, and a long metatarsal angles forward to the toes.
            const float L = m.hipJointY;
            m.knee[s] = {m.knee[s].x, 0.60f * L, 0.30f * h};
            m.ankle[s] = {m.ankle[s].x * 0.95f, 0.30f * L, -0.32f * h};
            m.ball[s] = {m.ankle[s].x + sd * 0.01f * h, 0.07f * h, 0.12f * h};
        }
    }
    if (b.digitigrade) m.ankleY = m.ankle[0].y;
    if (b.centaur) {
        // Horse body: the back runs level just under the human hip line; legs straight
        // in front, angled (stifle forward, hock back) behind.
        const float Yb = m.crotchY + 0.08f * h;
        m.barrelHalfH = 0.82f * h;
        m.barrelY = Yb - 0.70f * h;
        m.barrelHalfW = 0.80f * h;
        m.barrelFront = 0.85f * h;
        m.barrelBack = -4.1f * h;
        for (int s = 0; s < 2; ++s) {
            const float sd = s == 0 ? 1.0f : -1.0f, x = sd * 0.42f * h;
            m.hipJoint[s] = {x, m.barrelY - 0.35f * h, 0.20f * h};
            m.knee[s] = {x, 0.36f * Yb, 0.32f * h};
            m.ankle[s] = {x, 0.11f * Yb, 0.30f * h};
            m.ball[s] = {x, 0.015f * Yb, 0.42f * h};
            m.hindHip[s] = {sd * 0.46f * h, m.barrelY - 0.1f * h, -3.35f * h};
            m.hindKnee[s] = {sd * 0.48f * h, 0.62f * Yb, -2.95f * h};
            m.hindAnkle[s] = {sd * 0.44f * h, 0.30f * Yb, -3.78f * h};
            m.hindBall[s] = {sd * 0.42f * h, 0.015f * Yb, -3.50f * h};
        }
        m.ankleY = 0.11f * Yb;
    }

    const float S = b.shoulderWidth * h, C = b.chestWidth * h, armR = b.arm * h;
    const float del = armR * (1.12f + 0.45f * mu);
    for (int pair = 0; pair < std::clamp(b.armPairs, 1, 2); ++pair)
        for (int s = 0; s < 2; ++s) {
            ArmMarks a;
            a.side = s == 0 ? 1.0f : -1.0f;
            a.pair = pair;
            a.radius = armR * (pair ? 0.88f : 1.0f);
            a.deltoid = del * (pair ? 0.80f : 1.0f);
            float angle, fwd;
            if (pair == 0) {
                a.shoulder = {a.side * std::max(S - 0.55f * del, 0.5f * S), m.shoulderY - 0.05f * h - 0.9f * del - 0.10f * mu * h,
                              -0.02f * h + 0.05f * b.hunch * h + 0.20f * stoop * h};
                a.clav = {a.side * 0.10f * h, m.shoulderY - 0.10f * h, -0.02f * h + 0.03f * b.hunch * h + 0.20f * stoop * h};
                angle = 46.0f;
                fwd = 0.0f;
            } else {
                // A second shoulder girdle on the lower ribcage: a little smaller, angled
                // forward and out, so both pairs can work without colliding.
                float y = m.crotchY + 0.50f * m.torsoLen;
                a.shoulder = {a.side * 0.80f * C, y, 0.03f * h};
                a.clav = {a.side * 0.30f * C, y + 0.06f * h, 0.06f * h};
                angle = 40.0f;
                fwd = 0.55f;
            }
            float ar = angle * kPi / 180.0f;
            a.dir = normalize(Vec3{a.side * std::sin(ar), -std::cos(ar), fwd});
            float L = std::max(a.shoulder.y - (m.crotchY - 0.05f * h), 0.9f * m.torsoLen) * b.armLength;
            a.elbow = a.shoulder + a.dir * (0.53f * L);
            a.wrist = a.elbow + a.dir * (0.47f * L);
            a.handTip = a.wrist + a.dir * (std::min(0.66f * h, 0.33f * L) * b.handSize);
            m.arms.push_back(a);
        }

    m.neckBase = {0, m.shoulderY - 0.06f * h, -0.02f * h + 0.06f * b.hunch * h + 0.24f * stoop * h};
    m.headOrigin = {0, m.chinY, 0.05f * h + 0.12f * b.hunch * h + 0.45f * stoop * h};
    m.headPivot = m.headOrigin + Vec3{0, 0.14f * h, -0.10f * h};
}

Part& Builder::newPart(bool flat, SubmeshKind kind) {
    parts.emplace_back();
    Part& p = parts.back();
    p.flat = flat;
    p.kind = kind;
    p.id = uint16_t(parts.size());
    return p;
}

// ---------------- torso surface ----------------
SuperRing Builder::torsoRing(float t) const {
    float belly = g.body.belly * 0.30f * m.h * bump((t - 0.34f) / 0.26f);
    return {cHw_(t), cZc_(t), cDf_(t) + belly, cDb_(t), 2.2f, 2.5f};
}

// Angular warp: more samples on the front/back (bust, abs, glutes), fewer on the flat sides.
float Builder::warpTheta(float phi) const { return phi + 0.32f * 0.5f * std::sin(2.0f * phi); }

// Bust lobe intensity in [0,1]: an elliptic cap, round underneath (crease),
// blending softly into the upper chest. Age lowers it and flattens the top.
float Builder::bustField(float t, float th) const {
    const float B = g.body.bust, sag = g.body.sag;
    if (B < 0.03f || std::sin(th) <= 0) return 0;
    SuperRing sr = torsoRing(t);
    float u = std::fabs(sr.point(th).x) / sr.w;
    const float tb = 0.735f - 0.035f * std::min(B, 1.4f) - 0.05f * sag;
    const float ru = 0.36f + 0.10f * std::min(B, 1.4f);
    const float rt = t < tb ? 0.075f + 0.035f * std::min(B, 1.4f) : 0.17f + 0.08f * sag;
    float du = (u - 0.47f - 0.03f * sag) / ru, dt = (t - tb) / rt;
    float e = 1.0f - du * du - dt * dt;
    if (e <= 0) return 0;
    return t < tb ? std::pow(e, 0.7f) : std::pow(e, 1.1f + 0.8f * sag);
}

float Builder::gluteDepth(float t) const {
    const float sag = g.body.sag;
    return (std::min(g.body.butt, 1.6f) * 0.26f + 0.03f) * (1.0f - 0.25f * sag) * m.h * bump((t - 0.06f + 0.03f * sag) / 0.20f);
}

// Torso surface: the superellipse ring plus anatomical relief. Each landmark is a
// smooth field in (t, u) where u is the lateral position |x| / half width; its
// strength comes from the Anatomy fields (muscle size, bone prominence) and from
// `definition` (how much body fat softens the forms).
Vec3 Builder::torsoPoint(float t, float th) const {
    const float h = m.h, B = g.body.bust, fem = g.femininity;
    const Anatomy& an = g.body.anat;
    const float def = 0.3f + 0.7f * an.definition;
    SuperRing sr = torsoRing(t);
    Vec2 q = sr.point(th);
    const float sn = std::sin(th), u = std::fabs(q.x) / std::max(sr.w, 1e-4f);
    if (sn > 0) {
        const float front = std::pow(sn, 0.6f);
        float bf = bustField(t, th);
        if (bf > 0) {
            float depth = (0.06f + 0.31f * std::min(B, 1.5f)) * h;
            q.y += depth * bf * front;
            q.x += std::copysign(depth * 0.18f * bf, q.x);
        }
        float n = 0;
        // Clavicles: a ridge from the sternal notch rising towards the shoulder, the
        // hollow under its outer half, and the notch itself.
        const float cl = (0.4f + an.clavicles) * (1.0f - 0.35f * fem);
        n += 0.020f * h * cl * bump((t - (0.985f + 0.025f * u)) / 0.022f) * smoothstep(0.06f, 0.16f, u) * smoothstep(0.86f, 0.66f, u);
        n -= 0.012f * h * cl * def * bump((t - 0.945f) / 0.03f) * bump((u - 0.68f) / 0.16f);
        n -= 0.022f * h * cl * bump(u / 0.12f) * bump((t - 1.02f) / 0.035f);
        // Pectorals: a slab with a crisp lower edge rising towards the armpit, a sternal
        // groove and a softer top fading under the clavicle.
        const float pecK = 0.075f * h * std::min(an.pecs, 1.1f) * (1.0f - saturate(B * 1.6f));
        if (pecK > 0) {
            float tlow = 0.715f + 0.075f * u * u;
            // A rounded lower edge (a mass, not a ledge).
            float ft = smoothstep(tlow - 0.025f, tlow + 0.055f, t) * smoothstep(0.995f, 0.86f, t);
            float fu = smoothstep(0.02f, 0.10f, u) * smoothstep(0.86f, 0.62f, u);  // fades before the armpit
            n += pecK * ft * fu * (0.55f + 0.45f * bump((u - 0.45f) / 0.55f));
            n -= pecK * 0.12f * def * bump((t - (tlow - 0.02f)) / 0.03f) * fu;
        }
        // Ribcage: the costal arch, an inverted V from the base of the sternum.
        n += 0.016f * h * (0.4f + an.ribcage) * def * smoothstep(-0.02f, 0.03f, t - (0.67f - 0.17f * u)) *
             bump((t - 0.62f) / 0.12f) * smoothstep(0.85f, 0.5f, u);
        // Serratus on the flank under the armpit.
        n += 0.020f * h * an.serratus * def * smoothstep(0.62f, 0.85f, u) * (bump((t - 0.70f) / 0.035f) + 0.7f * bump((t - 0.635f) / 0.03f));
        // Abdominals: rectus slab, linea alba, three tendinous intersections above the navel.
        // Abdominals as planes: a raised rectus slab split by the linea alba, with a
        // single transverse line at the navel (simple, readable at low poly).
        const float absK = 0.060f * h * std::min(an.abs, 1.0f) * def * (1.0f - std::min(1.0f, 2.5f * g.body.belly)) * (1.0f - 0.5f * fem);
        if (absK > 0) {
            float hw = lerp(0.26f, 0.38f, smoothstep(0.15f, 0.60f, t));
            float ft = smoothstep(0.14f, 0.26f, t) * smoothstep(0.70f, 0.63f, t);
            float slab = smoothstep(hw + 0.04f, hw - 0.10f, u) * ft;
            // Two transverse lines (navel, mid) and the linea alba: four readable blocks above the navel.
            float lines = bump((t - 0.405f) / 0.04f) + 0.8f * bump((t - 0.525f) / 0.035f);
            n += absK * slab * (0.65f - 0.55f * std::min(lines, 1.0f)) - absK * 0.8f * bump(u / 0.10f) * ft;
        }
        // Sternum: a shallow groove between the pectorals.
        n -= 0.012f * h * (1.0f - fem) * bump(u / 0.10f) * bump((t - 0.80f) / 0.12f);
        // Obliques, the inguinal "V" from the hip bone to the pubis, and the hip bone.
        n += 0.022f * h * an.obliques * smoothstep(0.45f, 0.75f, u) * bump((t - 0.33f) / 0.15f);
        {
            const Vec2 a{0.88f, 0.25f}, ab = Vec2{0.22f, 0.10f} - Vec2{0.88f, 0.25f}, p{u, t};
            float k = saturate(dot(p - a, ab) / dot(ab, ab));
            n -= 0.026f * h * (0.3f + an.obliques) * def * bump(length(p - (a + ab * k)) / 0.04f);
        }
        n += 0.012f * h * (0.3f + an.pelvis) * def * bump((u - 0.86f) / 0.12f) * bump((t - 0.23f) / 0.05f);
        // Navel.
        n -= 0.012f * h * bump(u / 0.05f) * bump((t - 0.40f) / 0.025f);
        q.y += n * front;
    } else {
        const float back = std::pow(-sn, 0.7f);
        float lobe = 0.62f + 0.38f * bump((u - 0.42f) / 0.58f);
        q.y -= gluteDepth(t) * lobe * back;
        float o = 0;  // outwards (backwards)
        // Erector columns either side of the spine groove.
        o += 0.032f * h * (0.4f + 0.6f * an.lats) * def * bump((u - 0.15f) / 0.10f) * bump((t - 0.33f) / 0.22f);
        o -= (0.008f + 0.024f * def) * h * bump(u / 0.07f) * bump((t - 0.60f) / 0.45f);
        // Shoulder blades: raised plates with a medial border and a pointed lower angle.
        const float sc = 0.4f + an.scapulae;
        o += 0.032f * h * sc * smoothstep(0.20f, 0.30f, u) * smoothstep(0.80f, 0.62f, u) * smoothstep(0.70f, 0.77f, t) * smoothstep(0.96f, 0.88f, t);
        o += 0.016f * h * sc * def * bump((u - 0.26f) / 0.05f) * bump((t - 0.83f) / 0.10f);
        o += 0.016f * h * sc * def * bump((u - 0.42f) / 0.10f) * bump((t - 0.715f) / 0.03f);
        // Lower trapezius diamond and the lats flaring under the armpits.
        if (t > 0.6f) o += 0.022f * h * an.traps * def * smoothstep(-0.02f, 0.08f, 0.85f * (t - 0.60f) / 0.40f - u) * smoothstep(1.02f, 0.94f, t);
        o += 0.028f * h * an.lats * smoothstep(0.55f, 0.85f, u) * bump((t - 0.66f) / 0.16f);
        // Lower-back dimples above the glutes.
        o -= 0.012f * h * (0.3f + an.pelvis) * fem * bump((u - 0.20f) / 0.07f) * bump((t - 0.20f) / 0.04f);
        q.y -= o * back;
    }
    return {q.x, m.crotchY + t * m.torsoLen, q.y};
}

// Point of the analytic torso surface on the horizontal ray from the torso axis through p.
Vec3 Builder::torsoSurfaceToward(Vec3 p, float* theta) const {
    float t = clamp(torsoT(p.y), -0.06f, 1.07f);
    float zc = cZc_(t);
    float ang = std::atan2(p.z - zc, p.x), th = ang;
    Vec3 sp;
    for (int i = 0; i < 3; ++i) {
        sp = torsoPoint(t, th);
        th += std::remainder(ang - std::atan2(sp.z - zc, sp.x), kTau);
    }
    if (theta) *theta = th;
    sp.y = p.y;
    return sp;
}

Vec3 Builder::outsideTorso(Vec3 p) const {
    float t = torsoT(p.y);
    if (t < -0.06f || t > 1.05f) return p;
    float zc = cZc_(t);
    Vec3 sp = torsoSurfaceToward(p);
    Vec2 d{p.x, p.z - zc};
    float rs = length(Vec2{sp.x, sp.z - zc}), rp = length(d);
    if (rp >= rs || rp < 1e-6f) return p;
    return {d.x / rp * rs, p.y, zc + d.y / rp * rs};
}

Vec3 Builder::insideTorso(Vec3 p, float k) const {
    float t = torsoT(p.y);
    if (t < -0.06f || t > 1.05f) return p;
    float zc = cZc_(t);
    Vec3 sp = torsoSurfaceToward(p);
    Vec2 d{p.x, p.z - zc};
    float rs = length(Vec2{sp.x, sp.z - zc}) * k, rp = length(d);
    if (rp <= rs || rp < 1e-6f) return p;
    return {d.x / rp * rs, p.y, zc + d.y / rp * rs};
}

// Modesty layer only (briefs, chest band), cut as clean regions across the mesh.
// Women: high-cut briefs (leg opening rises from the crotch to the hip bone) and
// a bandeau with a soft sweetheart dip. Men: short boxer-briefs.
void Builder::addUnderwear(Part& p, bool chestBand) const {
    if (!g.underwear) return;
    const Mat uw{g.palette.underwear, MaterialClass::Cloth};
    const float h = m.h, cy = m.crotchY, tl = m.torsoLen;
    const bool fem = g.femininity > 0.5f;
    if (fem) {
        // Defined around the torso axis (phi: 0 front .. pi back), so the pelvis and the
        // thigh tops tucked just inside it get the exact same edge.
        const Curve hole{{0.0f, -0.12f * h}, {0.30f, -0.01f * h}, {0.75f, 0.07f * tl}, {1.5708f, 0.15f * tl}, {2.05f, 0.10f * tl},
                         {2.45f, 0.01f * tl}, {2.80f, -0.04f * h}, {3.1416f, -0.06f * h}};
        p.clips.push_back({[=, this](Vec3 q) {
            if (std::fabs(q.x) > 1.15f * cHw_(0.13f) + 0.02f * h) return 1.0f;  // not the hands/forearms
            float phi = std::atan2(std::fabs(q.x), q.z - cZc_(clamp(torsoT(q.y), -0.06f, 1.1f)));
            float top = cy + tl * (0.15f + 0.05f * std::sin(phi) + 0.02f * smoothstep(1.6f, 3.0f, phi));
            return std::max(q.y - top, cy + hole(phi) - q.y);
        }, uw});
    } else {
        p.clips.push_back({[=, this](Vec3 q) {
            if (std::fabs(q.x) > 1.15f * cHw_(0.13f) + 0.02f * h) return 1.0f;  // not the hands/forearms
            float z = q.z - cZc_(clamp(torsoT(q.y), -0.06f, 1.1f));
            float top = cy + tl * 0.13f - 0.01f * h * smoothstep(0.0f, 0.3f * h, z);
            float hole = cy - 0.15f * h + 0.02f * h * smoothstep(0.0f, 0.2f * h, z);
            return std::max(q.y - top, hole - q.y);
        }, uw});
    }
    if (chestBand && fem && g.body.bust > 0.05f) {
        const float B = std::min(g.body.bust, 1.4f);
        const float tb = 0.735f - 0.035f * B - 0.05f * g.body.sag;
        const float y0 = cy + tl * (tb - 0.085f - 0.035f * B), y1 = cy + tl * (tb + 0.065f);
        p.clips.push_back({[=, this](Vec3 q) {
            // Torso only: the arms share this surface.
            if (std::fabs(q.x) > 1.30f * cHw_(clamp(torsoT(q.y), -0.06f, 1.1f)) + 0.03f * h) return 1.0f;
            float front = smoothstep(0.0f, 0.2f * h, q.z - cZc_(clamp(torsoT(q.y), -0.06f, 1.1f)));
            float top = y1 + 0.015f * h * smoothstep(0.0f, 0.3f * h, std::fabs(q.x)) - 0.035f * h * front * bump(q.x / (0.10f * h));
            return std::max(y0 - q.y, q.y - top);
        }, uw});
    }
}

// ---------------- skeleton ----------------
void Builder::buildSkeleton() {
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    r.root = sk.add("root", -1, {0, 0, 0});
    r.hips = sk.add("hips", r.root, {0, m.hipJointY, 0});
    r.spine = sk.add("spine", r.hips, {0, m.waistY, 0});
    r.chest = sk.add("chest", r.spine, {0, m.chestY, 0});
    r.neck = sk.add("neck", r.chest, m.neckBase);
    r.head = sk.add("head", r.neck, m.headPivot);
    for (const ArmMarks& a : m.arms) {
        std::string suf = std::string(a.pair ? "2" : "") + (a.side > 0 ? ".L" : ".R");
        LimbRig l;
        l.side = a.side;
        l.restDir = a.dir;
        l.root = sk.add("clavicle" + suf, r.chest, a.clav);
        l.upper = sk.add("upperarm" + suf, l.root, a.shoulder);
        l.lower = sk.add("forearm" + suf, l.upper, a.elbow);
        l.end = sk.add("hand" + suf, l.lower, a.wrist);
        r.arms.push_back(l);
    }
    for (int s = 0; s < 2; ++s) {
        std::string suf = s == 0 ? ".L" : ".R";
        LimbRig l;
        l.side = s == 0 ? 1.0f : -1.0f;
        l.restDir = normalize(m.knee[s] - m.hipJoint[s]);
        l.upper = sk.add("thigh" + suf, r.hips, m.hipJoint[s]);
        l.lower = sk.add("shin" + suf, l.upper, m.knee[s]);
        l.end = sk.add("foot" + suf, l.lower, m.ankle[s]);
        l.tip = sk.add("toe" + suf, l.end, m.ball[s]);
        r.legs.push_back(l);
    }
    // Range of motion. Elbows and knees are hinges that only fold one way; the rest are
    // cones (bigger at the shoulders, small along the spine and neck).
    auto cone = [&](int bone, float maxA) { if (bone >= 0) r.limits.push_back({bone, false, {1, 0, 0}, 0.0f, maxA}); };
    cone(r.hips, 0.9f);
    cone(r.spine, 0.55f);
    cone(r.chest, 0.45f);
    cone(r.neck, 0.6f);
    cone(r.head, 0.85f);
    for (const LimbRig& a : r.arms) {
        cone(a.root, 0.35f);
        cone(a.upper, 2.7f);
        r.limits.push_back({a.lower, true, normalize(cross(a.restDir, {0, 0, 1})), -0.05f, 2.55f});
        cone(a.end, 1.25f);
    }
    if (g.body.centaur)
        for (int s = 0; s < 2; ++s) {
            std::string suf = s == 0 ? ".L" : ".R";
            LimbRig l;
            l.side = s == 0 ? 1.0f : -1.0f;
            l.restDir = normalize(m.hindKnee[s] - m.hindHip[s]);
            l.root = r.hips;
            l.upper = sk.add("hindthigh" + suf, r.hips, m.hindHip[s]);
            l.lower = sk.add("hindshin" + suf, l.upper, m.hindKnee[s]);
            l.end = sk.add("hindfoot" + suf, l.lower, m.hindAnkle[s]);
            l.tip = sk.add("hindtoe" + suf, l.end, m.hindBall[s]);
            r.legs.push_back(l);
        }
    for (const LimbRig& l : r.legs) {
        cone(l.upper, 1.9f);
        r.limits.push_back({l.lower, true, {1, 0, 0}, -0.05f, 2.6f});  // the knee only folds backwards
        cone(l.end, 0.9f);
        cone(l.tip, 0.7f);
    }
    if (g.body.bust > 0.05f) {
        const float bust = std::min(g.body.bust, 1.5f);
        const float tb = 0.735f - 0.035f * bust - 0.05f * g.body.sag, depth = (0.06f + 0.31f * bust) * h;
        for (int s = 0; s < 2; ++s) {
            float th = s == 0 ? 1.12f : kPi - 1.12f;  // lobe centre (u ~ 0.47)
            Vec3 c = torsoPoint(tb, th) - Vec3{0, 0, 0.5f * depth};
            int id = sk.add(s == 0 ? "bust.L" : "bust.R", r.chest, c);
            (s == 0 ? r.bustL : r.bustR) = id;
        }
    }
    // Equipment sockets: palms, between the shoulder blades, left hip.
    const ArmMarks& aR = m.arms[1];
    const ArmMarks& aL = m.arms[0];
    auto palm = [&](const ArmMarks& a) {
        float Lh = length(a.handTip - a.wrist);
        Vec3 u = cross(a.dir, orthoFront(a.dir));
        return a.wrist + a.dir * (0.45f * Lh) + u * a.side * (0.12f * Lh);
    };
    r.socketHandR = sk.add("socket.hand.R", r.arms[1].end, palm(aR));
    r.socketHandL = sk.add("socket.hand.L", r.arms[0].end, palm(aL));
    r.socketBack = sk.add("socket.back", r.chest, torsoPoint(0.80f, -kPi / 2) - Vec3{0, 0, 0.04f * h});
    r.socketHip = sk.add("socket.hip", r.hips, torsoPoint(0.20f, 0.0f) + Vec3{0.04f * h, 0, 0});
}

// ---------------- torso ----------------
void Builder::buildTorso() {
    const RigInfo& r = ch.rig;
    const float h = m.h, S = g.body.shoulderWidth * h, nR = g.body.neckThick * h;
    // Rings sit on the anatomy: abdominal grooves at 0.445 / 0.525 / 0.605, pectoral
    // shelf around 0.72, clavicles near 0.99, then the trapezius slope.
    // Faceted style: few, large planes. Rings on the hip line, iliac crest, lower
    // abdomen, navel, upper abdomen, rib arch, the two edges of the pectoral shelf,
    // mid chest, upper chest, clavicles and the trapezius slope.
    std::vector<float> base{kTorsoBase, 0.24f, 0.33f, 0.41f, 0.50f, 0.58f, 0.655f, 0.715f, 0.75f, 0.83f, 0.91f,
                            0.965f, 0.995f, 1.035f, 1.07f, 1.10f};
    auto rings = mergeRings(base, {}, lod.sparseRings);
    const int N = lod.torsoSeg;
    Loft L;
    for (size_t i = 0; i < rings.size(); ++i) {
        // The lowest rings are saddles: high over the hips, dipping at the front and
        // back midline down to the crotch, so the legs attach without twisted quads.
        const float drop = i == 0 ? kTorsoBase : i == 1 ? 0.07f : i == 2 ? 0.025f : 0.0f;
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) {
            float th = warpTheta(kTau * j / N), sn = std::sin(th);
            ring[size_t(j)] = torsoPoint(rings[i] - drop * std::pow(sn * sn, 1.5f), th);
        }
        L.rings.push_back(ring);
        L.t.push_back(rings[i]);
    }
    // The neck continues the same surface up into the head (no seam at its base):
    // sternocleidomastoid ridges run from behind the ears down to the sternal notch,
    // with an Adam's apple on men.
    const Anatomy& an = g.body.anat;
    const Vec3 n0{0, m.crotchY + 1.10f * m.torsoLen, cZc_(1.10f)};
    const Vec3 n1 = m.headOrigin + Vec3{0, 0.40f * h, -0.15f * h};  // deep in the skull: the nape enters the occiput
    const Vec3 nd = normalize(n1 - n0), nv = orthoFront(nd), nu = cross(nd, nv);
    const float def = 0.3f + 0.7f * an.definition;
    std::vector<float> neckT = lod.sparseRings ? std::vector<float>{0.5f, 1.0f} : std::vector<float>{0.4f, 0.8f, 1.0f};
    for (float v : neckT) {
        const float rr = nR * (1.0f - 0.12f * v * v);
        SuperRing sr{rr, 0, rr * 0.94f, rr * 1.0f, 2.0f, 2.0f};
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) {
            float th = warpTheta(kTau * j / N);
            Vec2 p = sr.point(th);
            float side = std::fabs(std::remainder(th - 0.5f * kPi, kTau));  // 0 at the throat .. pi at the nape
            float scm = lerp(0.35f, 1.95f, v);                              // ridge angle from the throat
            float o = rr * 0.10f * (0.4f + an.neck) * def * bump((side - scm) / 0.45f);
            o += rr * 0.14f * (1.0f - g.femininity) * bump(side / 0.35f) * bump((v - 0.45f) / 0.18f);
            o -= rr * 0.07f * bump(side / 0.5f) * bump(v / 0.25f);
            Vec3 radial = nu * p.x + nv * p.y;
            ring[size_t(j)] = n0 + nd * (v * length(n1 - n0)) + radial + normalize(radial, nv) * o;
        }
        L.rings.push_back(ring);
        L.t.push_back(1.10f + 0.4f * v);
    }
    L.poleEnd = true;
    L.endPole = n1 + nd * (0.4f * nR);
    L.tEnd = 1.55f;

    auto wf = [&, S](float t, float th, Vec3 p) {
        if (t <= 1.0f) t = torsoT(p.y);  // rings near the hips are not horizontal
        SkinW w = chainWeights({{0.12f, r.hips}, {0.40f, r.spine}, {0.66f, r.chest}, {1.0f, r.chest}, {1.16f, r.neck}, {1.38f, r.neck},
                                {1.50f, r.head}}, t);
        if (r.bustL >= 0 && t < 1.0f) {  // soft tissue follows the bust bones (secondary motion)
            float bf = bustField(t, warpTheta(th));
            if (bf > 0) {
                for (float& x : w.w) x *= (1 - 0.85f * bf);
                w.add(p.x > 0 ? r.bustL : r.bustR, 0.85f * bf);
            }
        }
        float cl = 0.5f * smoothstep(0.86f, 1.0f, t) * smoothstep(1.08f, 1.0f, t) * smoothstep(0.30f * S, 0.75f * S, std::fabs(p.x));
        if (cl > 0 && !r.arms.empty()) {
            for (float& x : w.w) x *= (1 - cl);
            w.add(r.arms[p.x > 0 ? 0 : 1].root, cl);
        }
        return w;
    };
    Part& tp = newPart();
    bodyGroup_ = tp.id;
    torsoPart_ = parts.size() - 1;
    tp.cavity = (0.18f + 0.30f * g.body.anat.definition) * (1.0f - 0.35f * g.femininity);  // facets carry most of the form
    addUnderwear(tp, true);
    if (g.body.bust > 0.05f)  // the bust is one soft form: no creases across it
        tp.smoothAt = [this](Vec3 p) {
            float t = torsoT(p.y);
            if (t < 0.45f || t > 0.98f || p.z < cZc_(t)) return false;
            float th = std::atan2(p.z - cZc_(t), p.x);
            return bustField(t, th) > 0.0f || bustField(t + 0.03f, th) > 0.0f || bustField(t - 0.03f, th) > 0.0f;
        };
    const uint32_t first = uint32_t(tp.pos.size());
    torsoTriStart_ = tp.tris.size();
    torsoFirst_ = first;
    torsoN_ = N;
    torsoRingT_ = L.t;
    emitLoft(tp, L, wf, [&](float, float) { return skin(); });
    torsoRing0_.clear();
    for (int j = 0; j < N; ++j) torsoRing0_.push_back(first + uint32_t(j));
}

// ---------------- arms ----------------
// Arms: a loft from inside the shoulder to the wrist. The cross-section is a base
// profile plus anatomical relief around the limb (beta: 0 towards the body, pi/2
// front, pi outside, 3pi/2 back): deltoid with its three heads narrowing into a V
// on the outer arm, biceps, horseshoe triceps, brachialis, elbow bones, the
// brachioradialis/extensor mass high on the forearm, flexors, and a flat wrist.
void Builder::buildArms() {
    const Anatomy& an = g.body.anat;
    const float def = 0.35f + 0.65f * an.definition;
    for (size_t k = 0; k < m.arms.size(); ++k) {
        const ArmMarks& a = m.arms[k];
        const LimbRig& rig = ch.rig.arms[k];
        const float upperLen = length(a.elbow - a.shoulder), foreLen = length(a.wrist - a.elbow);
        const float Ltot = upperLen + foreLen, te = upperLen / Ltot;
        const Vec3 d = a.dir;
        const float r = a.radius;
        // Above the joint (t < 0) the radius closes like a dome: the deltoid cap.
        const float dk = 1.0f + 0.28f * an.deltoids;  // deltoid cap size
        Curve rc{{-0.21f, 0.62f * dk}, {-0.15f, 0.88f * dk}, {-0.08f, 1.02f * dk}, {0.0f, 1.06f * dk}, {0.07f, 1.04f * dk}, {0.18f, 0.94f + 0.04f * dk},
                 {0.27f, 0.90f}, {0.35f, 0.88f},
                 {te - 0.05f, 0.76f}, {te, 0.72f}, {te + 0.10f, 0.84f}, {te + 0.25f, 0.70f}, {0.85f, 0.54f}, {1.0f, 0.48f}, {1.05f, 0.48f}};
        auto relief = [&](float beta, float t, float rr) {
            auto win = [&](float c, float w) { return bump(std::remainder(beta - c, kTau) / w); };
            float o = 0;
            // Deltoid: wide over the shoulder, its outline closing to a V at the insertion.
            float dw = lerp(1.7f, 0.45f, smoothstep(0.02f, 0.30f, t));
            float dt = smoothstep(-0.08f, 0.02f, t) * smoothstep(0.33f, 0.12f, t);
            o += rr * 0.32f * (0.4f + an.deltoids) * win(kPi, dw) * dt;
            o += rr * 0.10f * (0.4f + an.deltoids) * (win(2.1f, 0.6f) + win(4.2f, 0.6f)) * smoothstep(-0.10f, 0.0f, t) * smoothstep(0.22f, 0.08f, t);
            // The groove under the deltoid, between biceps and triceps.
            o -= rr * 0.06f * def * win(kPi, 0.5f) * bump((t - 0.36f) / 0.06f);
            // Biceps (front), triceps long and lateral heads (back), brachialis (outside, low).
            o += rr * 0.20f * an.biceps * win(1.57f, 0.95f) * bump((t - 0.29f) / 0.15f);
            o += rr * 0.14f * an.triceps * win(5.15f, 0.8f) * bump((t - 0.22f) / 0.17f);
            o += rr * 0.12f * an.triceps * win(3.95f, 0.7f) * bump((t - 0.17f) / 0.13f);
            o -= rr * 0.05f * def * win(4.71f, 0.5f) * bump((t - (te - 0.07f)) / 0.06f);  // flat triceps tendon
            o += rr * 0.07f * an.biceps * def * win(2.5f, 0.5f) * bump((t - 0.40f) / 0.07f);
            // Elbow: olecranon behind, epicondyles either side, soft hollow in front.
            const float eb = 0.4f + an.elbows;
            o += rr * 0.12f * eb * win(4.71f, 0.5f) * bump((t - te) / 0.035f);
            o += rr * 0.07f * eb * (win(0.0f, 0.45f) + win(kPi, 0.45f)) * bump((t - te) / 0.03f);
            o -= rr * 0.06f * win(1.57f, 0.55f) * bump((t - te) / 0.04f);
            // Forearm: brachioradialis / extensors high on the outer front, flexors inside.
            o += rr * 0.30f * (0.35f + an.forearms) * win(2.25f, 0.9f) * bump((t - (te + 0.09f)) / 0.12f);
            o += rr * 0.18f * (0.35f + an.forearms) * win(0.75f, 0.8f) * bump((t - (te + 0.12f)) / 0.12f);
            // Wrist: flat (wider than deep) with the head of the ulna on the back.
            float wr = smoothstep(0.80f, 0.97f, t);
            o -= rr * 0.20f * wr * (win(1.57f, 0.8f) + win(4.71f, 0.8f));
            o += rr * 0.10f * (0.4f + an.wrists) * win(5.5f, 0.45f) * bump((t - 0.96f) / 0.04f);
            // Inner upper arm lies flatter against the body.
            o -= rr * 0.06f * win(0.0f, 0.8f) * bump((t - 0.10f) / 0.18f);
            return o;
        };
        int clav = rig.root, up = rig.upper, lo = rig.lower, hand = rig.end;
        auto armWeights = [=](float t) {
            return chainWeights({{-0.18f, clav}, {0.0f, up}, {te - 0.07f, up}, {te + 0.07f, lo}, {0.94f, lo}, {1.04f, hand}}, t);
        };
        const Vec3 vv = orthoFront(d), uu = cross(d, vv);
        auto ringPoint = [&](float t, float al) {
            const float rr = r * rc(t);
            Vec3 radial = uu * (rr * std::cos(al)) + vv * (rr * std::sin(al));
            float beta = a.side > 0 ? al : kPi - al;
            return a.shoulder + d * (t * Ltot) + radial + normalize(radial, vv) * relief(beta, t, rr);
        };
        {
            // The arm grows out of the torso surface: a patch on the side of the chest
            // (armpit to shoulder top, front to back) is opened and its boundary becomes
            // the first ring of the arm. Junction rings in between form the deltoid as a
            // soft union of a ball around the shoulder joint and the torso, so torso and
            // arm are one continuous surface.
            Part& bp = parts[torsoPart_];
            const int N = torsoN_;
            auto nearest = [&](float t) {
                int best = 0;
                for (size_t i = 0; i < torsoRingT_.size(); ++i)
                    if (std::fabs(torsoRingT_[i] - t) < std::fabs(torsoRingT_[size_t(best)] - t)) best = int(i);
                return best;
            };
            // The opening scales with the arm: from below the armpit to the top of the
            // deltoid, and wider front-to-back for heavy shoulders.
            const float Ra = r * rc(0.02f) * 1.1f;
            // Four arms: the upper opening keeps above the lower pair's.
            const bool four = m.arms.size() > 2;
            int i0, i1;
            if (a.pair == 0) {
                i0 = nearest(clamp(torsoT(a.shoulder.y - 1.55f * Ra), four ? 0.63f : 0.52f, 0.80f));
                i1 = nearest(clamp(torsoT(a.shoulder.y + 0.85f * Ra), 0.92f, 1.0f));
                upperArmI0_ = i0;
            } else {
                i1 = std::min(nearest(clamp(torsoT(a.shoulder.y + 0.95f * Ra), 0.52f, 0.60f)), upperArmI0_ - 1);
                i0 = std::min(nearest(clamp(torsoT(a.shoulder.y - 1.2f * Ra), 0.32f, 0.46f)), i1 - 2);
            }
            if (lod.sparseRings) { if (i1 <= i0) i1 = i0 + 1; }
            else i0 = std::max(1, std::min(i0, i1 - 3));  // at least three rings tall: no pinched armpit
            const float dF = torsoRing(torsoT(a.shoulder.y)).dF;
            const int jw = Ra > 0.62f * dF && N >= 12 ? 2 : 1;
            const int jc = a.side > 0 ? 0 : N / 2, jL = jc - jw, jR = jc + jw;
            auto vid = [&](int i, int j) { return torsoFirst_ + uint32_t(i * N + ((j % N) + N) % N); };
            for (int i = i0; i < i1; ++i)
                for (int j = jL; j < jR; ++j) {
                    size_t q = torsoTriStart_ + 2 * size_t(i * N + ((j % N) + N) % N);
                    torsoHoles_.push_back(q);
                    torsoHoles_.push_back(q + 1);
                }
            std::vector<uint32_t> loop;
            for (int j = jL; j <= jR; ++j) loop.push_back(vid(i0, j));
            for (int i = i0 + 1; i <= i1; ++i) loop.push_back(vid(i, jR));
            for (int j = jR - 1; j >= jL; --j) loop.push_back(vid(i1, j));
            for (int i = i1 - 1; i > i0; --i) loop.push_back(vid(i, jL));
            const size_t M = loop.size();
            // Orient the loop like the arm rings: its Newell normal must point the way the
            // rings turn (cross(uu, vv)). Robust even when the joint lies outside the loop.
            Vec3 cen{};
            for (uint32_t v : loop) cen += bp.pos[v];
            cen = cen / float(M);
            Vec3 newell{};
            for (size_t j = 0; j < M; ++j) newell += cross(bp.pos[loop[j]] - cen, bp.pos[loop[(j + 1) % M]] - cen);
            if (dot(newell, cross(uu, vv)) < 0) std::reverse(loop.begin(), loop.end());
            auto angleOf = [&](Vec3 p) { Vec3 rel = p - cen; return std::atan2(dot(rel, vv), dot(rel, uu)); };
            std::vector<float> alpha(M);
            {
                float total = 0;
                std::vector<float> acc(M);
                for (size_t j = 0; j < M; ++j) {
                    acc[j] = total;
                    total += length(bp.pos[loop[(j + 1) % M]] - bp.pos[loop[j]]);
                }
                const float a0 = angleOf(bp.pos[loop[0]]);
                for (size_t j = 0; j < M; ++j) alpha[j] = a0 + kTau * acc[j] / std::max(total, 1e-6f);
            }
            auto addV = [&](Vec3 pnt, const SkinW& w) {
                bp.pos.push_back(pnt);
                bp.w.push_back(w);
                bp.uv.push_back({});
                return uint32_t(bp.pos.size() - 1);
            };
            const float t1 = 0.10f;
            auto armRing = [&](float t) {
                const float w = smoothstep(t1, t1 + 0.30f, t);
                std::vector<Vec3> rg(M);
                for (size_t j = 0; j < M; ++j) rg[j] = ringPoint(t, lerp(alpha[j], alpha[0] + kTau * float(j) / float(M), w));
                return rg;
            };
            const std::vector<Vec3> ring1 = armRing(t1);
            // Junction: a Hermite fillet per column, leaving the torso tangent to its
            // surface and arriving along the arm, plus the deltoid's mass on the outer and
            // upper side (none in the armpit). No creases, no shelf.
            const int K = lod.sparseRings ? 2 : 3;
            std::vector<Vec3> m0(M), m1(M), P0(M);
            std::vector<float> delt(M);
            for (size_t j = 0; j < M; ++j) {
                const Vec3 p0 = bp.pos[loop[j]], p1 = ring1[j];
                const float Lj = length(p1 - p0);
                float th;
                Vec3 sp = torsoSurfaceToward(p0, &th);
                (void)sp;
                const float tt = clamp(torsoT(p0.y), -0.05f, 1.05f), e = 0.01f;
                Vec3 n0 = normalize(cross(torsoPoint(tt + e, th) - torsoPoint(tt - e, th), torsoPoint(tt, th + e) - torsoPoint(tt, th - e)),
                                    Vec3{a.side, 0, 0});
                if (dot(n0, Vec3{p0.x, 0, p0.z - cZc_(tt)}) < 0) n0 = n0 * -1.0f;
                Vec3 tan0 = (p1 - p0) - n0 * dot(p1 - p0, n0);
                m0[j] = normalize(tan0 + n0 * 0.25f * length(tan0), d) * (0.9f * Lj);
                m1[j] = d * (0.9f * Lj);
                P0[j] = p0;
                // Deltoid mass: lateral and top of the arm (relative to the arm axis).
                Vec3 rad = p1 - (a.shoulder + d * dot(p1 - a.shoulder, d));
                Vec3 radN = normalize(rad, Vec3{a.side, 0, 0});
                float lateral = saturate(dot(radN, normalize(Vec3{a.side, 0.6f, 0})) * 1.2f + 0.2f);
                delt[j] = lateral * r * 0.30f * (0.5f + 0.5f * an.deltoids);
            }
            std::vector<uint32_t> prev = loop;
            for (int kq = 1; kq <= K; ++kq) {
                const float f = float(kq) / float(K + 1);
                const float h00 = 2 * f * f * f - 3 * f * f + 1, h10 = f * f * f - 2 * f * f + f, h01 = -2 * f * f * f + 3 * f * f,
                            h11 = f * f * f - f * f;
                std::vector<uint32_t> cur(M);
                for (size_t j = 0; j < M; ++j) {
                    Vec3 pnt = P0[j] * h00 + m0[j] * h10 + ring1[j] * h01 + m1[j] * h11;
                    Vec3 rad = pnt - (a.shoulder + d * dot(pnt - a.shoulder, d));
                    pnt += normalize(rad, Vec3{a.side, 0, 0}) * (delt[j] * std::sin(kPi * std::min(1.0f, f * 1.15f)));
                    pnt = lerp(pnt, outsideTorso(pnt), 1.0f - f);
                    SkinW w;
                    const SkinW& wl = bp.w[loop[j]];
                    for (int qq = 0; qq < 4; ++qq) w.add(wl.j[qq], wl.w[qq] * (1.0f - f));
                    w.add(up, f);
                    w.normalize();
                    cur[j] = addV(pnt, w);
                }
                emitStrip(bp, prev, cur, skin());
                prev = cur;
            }
            std::vector<float> ts{t1, 0.18f, 0.27f, 0.36f, te - 0.06f, te, te + 0.07f, te + 0.18f, 0.85f, 0.95f, 1.05f};
            if (lod.sparseRings) ts = mergeRings(ts, {}, true);
            for (float t : ts) {
                std::vector<Vec3> rg = armRing(t);
                std::vector<uint32_t> cur(M);
                for (size_t j = 0; j < M; ++j) cur[j] = addV(rg[j], armWeights(t));
                emitStrip(bp, prev, cur, skin());
                prev = cur;
            }
            armGroup_ = bodyGroup_;
        }
        buildHand(a, rig);
    }
    if (!torsoHoles_.empty()) {  // remove the torso faces replaced by the arms
        Part& bp = parts[torsoPart_];
        std::sort(torsoHoles_.begin(), torsoHoles_.end());
        std::vector<Part::Tri> kept;
        kept.reserve(bp.tris.size());
        size_t h = 0;
        for (size_t t = 0; t < bp.tris.size(); ++t) {
            while (h < torsoHoles_.size() && torsoHoles_[h] < t) ++h;
            if (h < torsoHoles_.size() && torsoHoles_[h] == t) continue;
            kept.push_back(bp.tris[t]);
        }
        bp.tris.swap(kept);
        torsoHoles_.clear();
        if (std::getenv("CHARGEN_CHECK")) {  // debug: open edges and folds (adjacent faces nearly opposite) near the shoulders
            std::unordered_map<uint64_t, std::vector<size_t>> edges;
            for (size_t t = 0; t < bp.tris.size(); ++t) {
                const auto& tr = bp.tris[t];
                const uint32_t v[3] = {tr.a, tr.b, tr.c};
                for (int e = 0; e < 3; ++e) {
                    uint32_t x = v[e], y = v[(e + 1) % 3];
                    edges[x < y ? (uint64_t(x) << 32 | y) : (uint64_t(y) << 32 | x)].push_back(t);
                }
            }
            auto nrm = [&](size_t t) {
                const auto& tr = bp.tris[t];
                return normalize(cross(bp.pos[tr.b] - bp.pos[tr.a], bp.pos[tr.c] - bp.pos[tr.a]), Vec3{0, 0, 0});
            };
            int open = 0, folds = 0;
            for (auto& e : edges) {
                if (e.second.size() == 1) ++open;
                if (e.second.size() != 2) continue;
                const auto& tr = bp.tris[e.second[0]];
                Vec3 ctr = (bp.pos[tr.a] + bp.pos[tr.b] + bp.pos[tr.c]) / 3.0f;
                bool nearShoulder = false;
                for (const ArmMarks& am : m.arms) nearShoulder |= am.pair == 0 && length(ctr - am.shoulder) < 0.6f * m.h;
                if (nearShoulder && dot(nrm(e.second[0]), nrm(e.second[1])) < -0.2f) {
                    ++folds;
                    if (std::getenv("CHARGEN_CHECK")[0] == '2')
                        std::fprintf(stderr, "  fold t=%.3f x/h=%.2f z/h=%.2f tris %zu %zu verts %u\n", torsoT(ctr.y), ctr.x / m.h, ctr.z / m.h, e.second[0], e.second[1], tr.a);
                }
            }
            std::fprintf(stderr, "CHECK open=%d folds=%d\n", open, folds);
        }
    }
}

void Builder::buildHand(const ArmMarks& a, const LimbRig& rig) {
    if (decay_ >= 0.85f) return;  // extremities skeletonize first: the bone hand replaces it
    const float Lh = length(a.handTip - a.wrist);
    const Vec3 d = a.dir, v = orthoFront(d), u = cross(d, v);
    const Vec3 palm = u * a.side;  // towards the body in the A-pose
    const float hw = std::max(0.25f * Lh, a.radius * 0.78f) * (1 + 0.12f * g.body.muscle);
    const float th = std::max(0.13f * Lh, a.radius * 0.55f);
    const int hand = rig.end;
    struct K {
        float t, w, th, curl;
    };
    // Palm, knuckle line, then the fingers as one softly curled block whose
    // separations are grooves on both faces (the cavity shading draws them).
    const bool fingers = lod.handSeg >= 6;  // LOD0/1: separate fingers; LOD2: one mitten block
    const Mat clawM{g.palette.horn * 0.6f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    if (fingers) {
        // Palm block up to the knuckles.
        const K palmKeys[] = {{-0.05f, 0.62f, 0.85f, 0}, {0.18f, 0.92f, 1.0f, 0}, {0.40f, 1.0f, 1.0f, 0.02f}, {0.50f, 0.98f, 0.92f, 0.05f}};
        const int n = lod.handSeg;
        Loft L;
        for (const K& k : palmKeys) {
            Vec3 c = a.wrist + d * (k.t * Lh) + palm * (k.curl * th);
            const float wu = th * k.th, wv = hw * k.w, knuckle = bump((k.t - 0.48f) / 0.06f);
            std::vector<Vec3> ring(static_cast<size_t>(n));
            for (int j = 0; j < n; ++j) {
                float al = kTau * float(j) / float(n);
                float x = wu * spow(std::cos(al), 2.0f / 2.6f), z = wv * spow(std::sin(al), 2.0f / 2.6f);
                float back = (x * a.side < 0) ? 1.0f : 0.0f;
                x += std::copysign(0.08f * wu * knuckle * back, x);
                ring[size_t(j)] = c + u * x + v * z;
            }
            L.rings.push_back(ring);
            L.t.push_back(k.t);
        }
        L.poleEnd = true;
        L.endPole = a.wrist + d * (0.53f * Lh) + palm * (0.05f * th);
        L.tEnd = 0.53f;
        Part& hp = newPart();
        hp.id = armGroup_;
        emitLoft(hp, L, [=](float, float, Vec3) { return one(hand); }, [&](float, float) { return skin(); });
        // Four fingers, each a tapered, slightly curled three-joint tube.
        const float lens[] = {0.46f, 0.52f, 0.49f, 0.38f};
        for (int f = 0; f < 4; ++f) {
            const float off = (1.5f - float(f)) / 1.5f;  // +1 index (thumb side) .. -1 little finger
            const float fr = std::min(0.24f * hw, 0.45f * th) * (f == 3 ? 0.85f : 1.0f);
            Vec3 p = a.wrist + d * (0.46f * Lh) + v * (off * 0.72f * hw) + palm * (0.04f * th);
            Vec3 fd = normalize(d + v * (off * 0.06f));
            const float seg = lens[f] * Lh * (g.body.claws ? 1.05f : 1.0f) / 3.0f;
            std::vector<Vec3> pts{p};
            for (int k = 0; k < 3; ++k) {
                fd = normalize(fd + palm * (0.22f + 0.08f * float(k)));  // relaxed curl
                p = p + fd * seg;
                pts.push_back(p);
            }
            Loft F;
            for (int k = 0; k < 3; ++k) {
                const Vec3 dd = normalize(pts[size_t(k + 1)] - pts[size_t(k)]);
                const Vec3 fv = normalize(v - dd * dot(v, dd)), fu = cross(dd, fv);
                const float rr = fr * (1.0f - 0.16f * float(k));
                F.rings.push_back(makeRing(pts[size_t(k)], fu, fv, SuperRing{rr * 0.9f, 0, rr, rr, 2.2f, 2.2f}, 5));
                F.t.push_back(float(k) / 3.0f);
            }
            F.poleEnd = true;
            F.endPole = pts.back();
            F.tEnd = 1;
            Part& fp = newPart();
            fp.id = armGroup_;
            emitLoft(fp, F, [=](float, float, Vec3) { return one(hand); },
                     [&](float t, float) { return g.body.claws && t > 0.75f ? clawM : skin(); });
            if (g.body.claws) {  // a hooked claw past the fingertip
                const Vec3 tip = pts.back(), dd = normalize(pts[3] - pts[2]);
                const Vec3 fv = normalize(v - dd * dot(v, dd)), fu = cross(dd, fv);
                Loft C;
                C.rings.push_back(makeRing(tip - dd * (0.3f * fr), fu, fv, SuperRing{0.55f * fr, 0, 0.55f * fr, 0.45f * fr}, 5));
                C.t.push_back(0);
                C.poleEnd = true;
                C.endPole = tip + normalize(dd + palm * 0.6f) * (1.6f * fr);
                C.tEnd = 1;
                Part& cp = newPart();
                cp.id = armGroup_;
                emitLoft(cp, C, [=](float, float, Vec3) { return one(hand); }, [&](float, float) { return clawM; });
            }
        }
    } else {
    const K keys[] = {{-0.05f, 0.62f, 0.85f, 0}, {0.18f, 0.92f, 1.0f, 0}, {0.40f, 1.0f, 1.0f, 0.02f}, {0.48f, 1.0f, 1.0f, 0.05f},
                      {0.58f, 0.97f, 0.86f, 0.10f}, {0.74f, 0.93f, 0.78f, 0.30f}, {0.88f, 0.86f, 0.66f, 0.55f}};
    const int n = lod.handSeg;
    Loft L;
    for (const K& k : keys) {
        Vec3 c = a.wrist + d * (k.t * Lh) + palm * (k.curl * th);
        const float wu = th * k.th, wv = hw * k.w;
        const float fingers = smoothstep(0.50f, 0.60f, k.t), knuckle = bump((k.t - 0.44f) / 0.07f);
        std::vector<Vec3> ring(static_cast<size_t>(n));
        for (int j = 0; j < n; ++j) {
            float al = kTau * float(j) / float(n);
            float x = wu * spow(std::cos(al), 2.0f / 2.6f), z = wv * spow(std::sin(al), 2.0f / 2.6f);
            float zz = z / wv, back = (x * a.side < 0) ? 1.0f : 0.0f;
            float groove = bump((zz + 0.5f) / 0.2f) + bump(zz / 0.2f) + bump((zz - 0.5f) / 0.2f);
            x -= std::copysign(0.20f * wu * fingers * groove, x);
            x += std::copysign(0.10f * wu * knuckle * back * (1.0f - groove), x);
            ring[size_t(j)] = c + u * x + v * z;
        }
        L.rings.push_back(ring);
        L.t.push_back(k.t);
    }
    L.poleEnd = true;
    L.endPole = a.wrist + d * (0.95f * Lh) + palm * (0.80f * th);
    L.tEnd = 1;
    Part& hp = newPart();
    hp.id = armGroup_;
    hp.cavity = 0.5f;
    emitLoft(hp, L, [=](float, float, Vec3) { return one(hand); }, [&](float, float) { return skin(); });

    }

    // Thumb.
    Vec3 tb = a.wrist + d * (0.2f * Lh) + v * (0.65f * hw) + palm * (0.3f * th);
    Vec3 td = normalize(d * 0.55f + v * 0.45f + palm * 0.55f);
    Vec3 tv = orthoFront(td), tu = cross(td, tv);
    float tl = 0.46f * Lh, tr = 0.38f * hw;
    Loft T;
    for (float t : {0.0f, 0.35f, 0.65f, 0.88f}) {
        float rr = tr * (1.0f - 0.25f * t) * (1.0f + 0.25f * bump(t / 0.4f));  // thenar mass at the base
        T.rings.push_back(makeRing(tb + td * (t * tl), tu, tv, SuperRing{rr, 0, rr, rr}, 8));
        T.t.push_back(t);
    }
    T.poleEnd = true;
    T.endPole = tb + td * tl;
    Part& tp = newPart();
    tp.id = armGroup_;
    emitLoft(tp, T, [=](float, float, Vec3) { return one(hand); }, [&](float, float) { return skin(); });
}

// ---------------- legs & feet ----------------
// The legs continue the torso surface: the bottom ring of the torso splits into
// two halves which, closed by a bridge through the crotch, become the top loops of
// the thighs ("pants" topology). A few junction rings carry the hip and glute volume
// down into the thigh. No surfaces intersect, so the hips stay clean in any light.
void Builder::buildLegs() {
    if (g.body.centaur) {
        buildHorseBody();
        return;
    }
    const float h = m.h, mu = g.body.muscle;
    const float T = g.body.thigh * h, Cf = g.body.calf * h * (1 + 0.2f * mu);
    // Knee narrower than the calf, slim ankle: the diamond-shaped lower leg.
    const float K = std::max(0.50f * T, 0.70f * Cf), A = (g.body.digitigrade ? 0.62f : 0.44f) * Cf;
    const int N = lod.torsoSeg, q = N / 4;
    const int hips = ch.rig.hips;
    Part& bp = parts[torsoPart_];
    auto addV = [&](Vec3 p, const SkinW& w) {
        bp.pos.push_back(p);
        bp.w.push_back(w);
        bp.uv.push_back({});
        return uint32_t(bp.pos.size() - 1);
    };
    // Crotch bridge shared by both legs: down the front midline, under, up the back.
    std::vector<uint32_t> bridge;
    {
        // The torso's bottom ring already reaches the crotch at its front and back
        // midline; the bridge closes the gap between them under the crotch. Men get a
        // soft rounded bulge at the front instead of a notch.
        const float bulge = (1.0f - g.femininity) * h;
        const Vec3 rf = bp.pos[torsoRing0_[size_t(q)]], rb = bp.pos[torsoRing0_[size_t(3 * q)]];
        Vec3 crotch{0, m.crotchY - 0.012f * h - 0.02f * bulge, lerp(rb.z, rf.z, 0.55f) + 0.04f * bulge};
        std::vector<Vec3> pts;
        if (lod.sparseRings) pts = {crotch};
        else pts = {lerp(rf, crotch, 0.5f) + Vec3{0, -0.012f * h - 0.01f * bulge, 0.03f * bulge}, crotch, lerp(rb, crotch, 0.5f) + Vec3{0, -0.012f * h, 0}};
        for (Vec3 p : pts) bridge.push_back(addV(p, one(hips)));
    }
    const int nb = int(bridge.size()), mid = nb / 2;
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const Vec3 Hj = m.hipJoint[s], Kn = m.knee[s], An = m.ankle[s];
        const float l1 = length(Kn - Hj), l2 = length(An - Kn), Lt = l1 + l2, tk = l1 / Lt;
        const Vec3 d1 = (Kn - Hj) / l1, d2 = (An - Kn) / l2;
        auto path = [&](float t) { return t < tk ? Hj + d1 * (t * Lt) : Kn + d2 * ((t - tk) * Lt); };
        const float tc = (Hj.y - m.crotchY) / std::max(1e-4f, -d1.y * Lt);  // leg parameter at crotch height
        const float t1 = tc + 0.05f;                                         // first full thigh ring
        const float Ttop = std::max(T * 1.02f, 0.50f * g.body.hipWidth * h);
        // Thigh: full at the top, tapering hard into a narrow knee. Lower leg: a
        // diamond whose widest point sits high (a quarter down the shin), then a long
        // clean taper to a slim ankle that stays slim for the last third.
        auto shin = [&](float f) { return tk + f * (1.0f - tk); };
        // Thigh: one steady taper from the hip to the knee (no waist on the thigh).
        auto thigh = [&](float f) { return f * tk; };
        Curve rc{{-0.16f, 0.62f * Ttop}, {-0.06f, 0.88f * Ttop}, {0.0f, Ttop}, {thigh(0.22f), lerp(Ttop, T, 0.7f)}, {thigh(0.45f), 0.93f * T},
                 {thigh(0.68f), lerp(T, K, 0.40f)}, {thigh(0.86f), lerp(T, K, 0.78f)}, {tk - 0.02f, 1.02f * K}, {tk + 0.015f, K}, {shin(0.10f), 0.86f * Cf},
                 {shin(0.27f), Cf}, {shin(0.42f), 0.84f * Cf}, {shin(0.56f), 0.60f * Cf}, {shin(0.70f), A}, {1.0f, 0.96f * A}, {1.06f, 0.96f * A}};
        int th = rig.upper, sh = rig.lower, ft = rig.end;
        auto weights = [=](float t) {
            return chainWeights({{tc - 0.03f, hips}, {t1 + 0.07f, th}, {tk - 0.07f, th}, {tk + 0.07f, sh}, {0.95f, sh}, {1.05f, ft}}, t);
        };
        auto section = [&](float t, Vec3& c, Vec3& u, Vec3& v) {
            Vec3 dd = normalize(lerp(d1, d2, smoothstep(tk - 0.06f, tk + 0.06f, t)));
            v = orthoFront(dd);
            u = cross(dd, v);
            c = path(t);
            float rr = rc(t);
            float calf = bump((t - (tk + 0.17f)) / 0.22f);
            float topFlat = 1.0f - 0.18f * smoothstep(0.18f, -0.02f, t);  // upper thigh: wide, not deep
            return SuperRing{rr, -0.06f * Cf * calf, rr * 0.97f * topFlat, rr * (1.0f + 0.06f * calf) * topFlat, 2.0f, 2.0f};
        };
        // Anatomical relief around the leg. beta: 0 medial, pi/2 front, pi lateral, 3pi/2 back.
        const Anatomy& an = g.body.anat;
        const float def = 0.35f + 0.65f * an.definition, bone = 0.4f + an.knees;
        auto relief = [&](float beta, float t, float rr) {
            auto win = [&](float c, float w) { return bump(std::remainder(beta - c, kTau) / w); };
            float o = 0;
            // Thigh: vastus lateralis sweep, rectus femoris, vastus medialis teardrop above
            // the inner knee, adductor mass high on the inside, hamstrings, flat IT band.
            o += rr * 0.10f * an.quads * def * win(2.15f, 1.0f) * bump((t - 0.55f * tk) / (0.45f * tk));
            o += rr * 0.06f * an.quads * def * win(1.57f, 0.7f) * bump((t - 0.5f * tk) / (0.4f * tk));
            o += rr * 0.09f * an.quads * def * win(0.75f, 0.8f) * bump((t - (tk - 0.08f)) / 0.07f);
            o += rr * 0.10f * an.adductors * win(0.1f, 1.1f) * bump((t - (tc + 0.10f)) / 0.16f);
            o += rr * 0.07f * an.hamstrings * def * win(4.71f, 0.9f) * bump((t - 0.55f * tk) / (0.4f * tk));
            o -= rr * 0.05f * def * win(3.14f, 0.45f) * bump((t - 0.30f) / 0.16f);
            // Knee: patella block, narrow sides, popliteal hollow, patellar tendon.
            o += rr * 0.15f * bone * win(1.57f, 0.6f) * bump((t - (tk - 0.008f)) / 0.042f);
            o -= rr * 0.07f * bone * (win(0.0f, 0.55f) + win(3.14f, 0.55f)) * bump((t - tk) / 0.05f);
            o -= rr * 0.06f * win(4.71f, 0.6f) * bump((t - tk) / 0.05f);
            o += rr * 0.04f * bone * win(1.57f, 0.35f) * bump((t - (tk + 0.04f)) / 0.025f);
            // Calf: medial gastrocnemius head lower and larger, lateral head higher; flat
            // tibia in front; Achilles hollows; medial malleolus higher than the lateral.
            o += rr * 0.10f * an.calves * win(5.35f, 0.8f) * bump((t - (tk + 0.30f * (1.0f - tk))) / 0.10f);  // medial head, lower
            o += rr * 0.07f * an.calves * win(4.05f, 0.75f) * bump((t - (tk + 0.22f * (1.0f - tk))) / 0.09f);  // lateral head, higher
            o -= rr * 0.06f * win(1.57f, 0.45f) * smoothstep(tk + 0.06f, tk + 0.12f, t) * smoothstep(0.98f, 0.85f, t);
            o -= rr * 0.08f * (win(4.0f, 0.5f) + win(5.42f, 0.5f)) * bump((t - 0.93f) / 0.06f);
            o += rr * 0.07f * (0.4f + an.wrists) * win(0.0f, 0.6f) * bump((t - 0.99f) / 0.05f);
            
            return o;
        };

        // Top loop, ordered by angle around the leg (from u towards v, like makeRing).
        // outer: how much a vertex keeps to the pelvis surface in the junction (hip,
        // groin and glute vertices do; the crotch itself does not).
        std::vector<uint32_t> loop;
        std::vector<float> outer;
        const float P = g.body.hipWidth * h;
        auto ringOuter = [&](int j) { return smoothstep(0.05f * P, 0.55f * P, std::fabs(bp.pos[torsoRing0_[size_t(j)]].x)); };
        if (s == 0) {
            for (int k = mid; k >= 0; --k) loop.push_back(bridge[size_t(k)]), outer.push_back(0.0f);
            for (int j = q; j >= -q; --j) loop.push_back(torsoRing0_[size_t((j + N) % N)]), outer.push_back(ringOuter((j + N) % N));
            for (int k = nb - 1; k > mid; --k) loop.push_back(bridge[size_t(k)]), outer.push_back(0.0f);
        } else {
            for (int j = 2 * q; j >= q; --j) loop.push_back(torsoRing0_[size_t(j)]), outer.push_back(ringOuter(j));
            for (int k = 0; k < nb; ++k) loop.push_back(bridge[size_t(k)]), outer.push_back(0.0f);
            for (int j = 3 * q; j > 2 * q; --j) loop.push_back(torsoRing0_[size_t(j)]), outer.push_back(ringOuter(j));
        }
        const size_t M = loop.size();
        // Angles by arc length along the loop (the bridge has vertical extent, so a plain
        // projection would stack several of its vertices on one angle). Index 0 is the
        // crotch (medial) for the left leg and the outer hip (lateral) for the right one,
        // which is where u points in each case.
        std::vector<float> alpha(M, 0.0f);
        {
            float total = 0;
            std::vector<float> acc(M, 0.0f);
            for (size_t j = 0; j < M; ++j) {
                acc[j] = total;
                total += length(bp.pos[loop[(j + 1) % M]] - bp.pos[loop[j]]);
            }
            for (size_t j = 0; j < M; ++j) alpha[j] = kTau * acc[j] / std::max(total, 1e-6f);
        }
        auto ringAt = [&](float t) {
            Vec3 c, u, v;
            SuperRing sr = section(t, c, u, v);
            float w = smoothstep(t1, t1 + 0.35f, t), rr = rc(t);
            std::vector<Vec3> r(M);
            for (size_t j = 0; j < M; ++j) {
                float a = lerp(alpha[j], alpha[0] + kTau * float(j) / float(M), w);
                Vec2 p = sr.point(a);
                Vec3 radial = u * p.x + v * p.y;
                float beta = s == 0 ? a : kPi - a;
                r[j] = c + radial + normalize(radial, v) * relief(beta, t, rr);
            }
            return r;
        };
        const std::vector<Vec3> ring1 = ringAt(t1);

        // Junction rings: interpolate loop -> first thigh ring, keeping the hip/glute volume,
        // then relax the crotch side (Laplacian smoothing with the loop and the first
        // thigh ring held fixed) so the saddle around the bridge never folds.
        const int K = lod.sparseRings ? 1 : 2;
        std::vector<std::vector<Vec3>> J(size_t(K + 2), std::vector<Vec3>(M));
        for (size_t j = 0; j < M; ++j) {
            J[0][j] = bp.pos[loop[j]];
            J[size_t(K + 1)][j] = ring1[j];
        }
        for (int k = 1; k <= K; ++k) {
            float f = float(k) / float(K + 1);
            for (size_t j = 0; j < M; ++j) {
                Vec3 p = lerp(bp.pos[loop[j]], ring1[j], f);
                if (outer[j] > 0) {
                    Vec3 sp = torsoSurfaceToward(p);
                    float zc = cZc_(clamp(torsoT(p.y), -0.06f, 1.07f));
                    float rp = length(Vec2{p.x, p.z - zc}), rs = length(Vec2{sp.x, sp.z - zc});
                    if (rs > rp) p = lerp(p, sp, outer[j] * smoothstep(-0.05f, 0.04f, torsoT(p.y)));
                }
                J[size_t(k)][j] = p;
            }
        }
        for (int it = 0; it < 0; ++it) {
            auto prevJ = J;
            for (int k = 1; k <= K; ++k)
                for (size_t j = 0; j < M; ++j) {
                    float w = 0.55f * (1.0f - outer[j]);
                    if (w <= 0) continue;
                    Vec3 avg = (prevJ[size_t(k)][(j + M - 1) % M] + prevJ[size_t(k)][(j + 1) % M] + prevJ[size_t(k - 1)][j] +
                                prevJ[size_t(k + 1)][j]) * 0.25f;
                    J[size_t(k)][j] = lerp(prevJ[size_t(k)][j], avg, w);
                }
        }
        std::vector<uint32_t> prev = loop;
        for (int k = 1; k <= K; ++k) {
            std::vector<uint32_t> cur(M);
            for (size_t j = 0; j < M; ++j) {
                const Vec3 p = J[size_t(k)][j];
                float te = (Hj.y - p.y) / std::max(1e-4f, -d1.y * Lt);
                cur[j] = addV(p, weights(te));
            }
            emitStrip(bp, prev, cur, skin());
            prev = cur;
        }
        // Thigh, knee, calf, ankle.
        std::vector<float> ts{t1};
        // Rings on the landmarks only, so flat facets form large, deliberate planes.
        for (float f : {0.30f, 0.50f, 0.68f})
            if (f * tk > t1 + 0.04f) ts.push_back(f * tk);
        for (float t : {thigh(0.86f), tk - 0.02f, tk + 0.03f, shin(0.12f), shin(0.27f), shin(0.43f), shin(0.57f), shin(0.72f), 0.97f, 1.06f})
            if (t > t1 + 0.03f) ts.push_back(t);
        if (lod.sparseRings) ts = mergeRings(ts, {}, true);
        for (float t : ts) {
            std::vector<Vec3> r = ringAt(t);
            std::vector<uint32_t> cur(M);
            for (size_t j = 0; j < M; ++j) cur[j] = addV(r[j], weights(t));
            emitStrip(bp, prev, cur, skin());
            prev = cur;
        }
    }
}

// Bare feet: heel, arch, ball and a soft block of toes.
void Builder::buildFeet() {
    if (g.body.centaur) return;  // the horse legs carry their own hooves
    if (decay_ >= 0.85f && !g.body.hooves) return;  // bare foot bones (bones.cpp)
    if (g.body.digitigrade) {
        buildDigitigradeFeet();
        return;
    }
    const bool hoof = g.body.hooves;
    const float A = m.ankleY, Lf = m.footLen * (hoof ? 0.62f : 1.0f), Wf = m.footHalfW * (hoof ? 1.05f : 1.0f);
    // Hooves: a short, blocky, dark keratin foot with a cleft front.
    Curve cw = hoof ? Curve{{0, 0.80f}, {0.3f, 0.95f}, {0.7f, 1.0f}, {1.0f, 0.90f}}
                    : Curve{{0, 0.62f}, {0.10f, 0.76f}, {0.35f, 0.78f}, {0.60f, 0.95f}, {0.80f, 1.05f}, {0.93f, 0.98f}, {1.0f, 0.82f}};
    Curve chh = hoof ? Curve{{0, 0.85f}, {0.3f, 1.05f}, {0.7f, 0.85f}, {1.0f, 0.55f}}
                     : Curve{{0, 0.60f}, {0.08f, 0.90f}, {0.30f, 1.10f}, {0.50f, 0.80f}, {0.72f, 0.46f}, {0.88f, 0.34f}, {1.0f, 0.28f}};
    const Mat hoofM{g.palette.horn * 0.35f + Vec3{0.05f, 0.04f, 0.04f}, MaterialClass::Horn};
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const float sd = s == 0 ? 1.0f : -1.0f;
        const Vec3 an = m.ankle[s];
        const float heelZ = an.z - 0.20f * Lf;
        const Quat turn = Quat::axisAngle({0, 1, 0}, sd * 0.12f);
        auto place = [&](Vec3 p) { return Vec3{an.x, 0, an.z} + rotate(turn, p - Vec3{an.x, 0, an.z}); };
        Loft L;
        for (float t : {0.0f, 0.06f, 0.15f, 0.3f, 0.45f, 0.6f, 0.72f, 0.82f, 0.91f, 0.97f}) {
            float top = chh(t) * A, bot = 0.07f * A;
            // Arch: the inner side of the sole lifts between heel and ball.
            SuperRing sr{cw(t) * Wf, bot, top - bot, bot * 0.9f, 2.2f, 5.0f};
            auto ring = makeRing({an.x, 0.003f, heelZ + t * Lf}, {-1, 0, 0}, {0, 1, 0}, sr, lod.limbSeg);
            for (Vec3& p : ring) {
                float inner = saturate((p.x - an.x) * -sd / Wf);  // 1 on the inner edge
                p.y += 0.25f * A * inner * bump((t - 0.45f) / 0.25f) * saturate(1.0f - p.y / (0.4f * A));
                p = place(p);
            }
            L.rings.push_back(ring);
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = place({an.x, 0.3f * A, heelZ - 0.03f * Lf});
        L.tStart = -0.03f;
        L.poleEnd = true;
        L.endPole = place({an.x, 0.18f * A, heelZ + 1.0f * Lf});
        L.tEnd = 1;
        int foot = rig.end, toe = rig.tip;
        emitLoft(newPart(), L, [=](float t, float, Vec3) { return blend(foot, toe, smoothstep(0.62f, 0.80f, t)); },
                 [&](float, float) { return hoof ? hoofM : skin(); });
    }
}

// Centaur: the horse barrel (chest, belly, croup) swallowing the human hips, and four
// legs: straight forelegs, hind legs with the stifle forward and the hock back, each
// ending in a fetlock, a short pastern and a hoof.
void Builder::buildHorseBody() {
    const float h = m.h;
    const int hips = ch.rig.hips;
    const Mat hoofM{Vec3{0.16f, 0.14f, 0.13f}, MaterialClass::Horn};
    const Vec3 coat = g.palette.skin;
    // The horse coat: an earthy tone drawn from the hair colour family, darker on the legs.
    static constexpr Vec3 kCoats[] = {{0.34f, 0.22f, 0.14f}, {0.46f, 0.26f, 0.15f}, {0.13f, 0.12f, 0.12f}, {0.52f, 0.50f, 0.48f},
                                      {0.62f, 0.50f, 0.32f}, {0.44f, 0.38f, 0.30f}, {0.24f, 0.18f, 0.14f}};  // bay, chestnut, black, grey, palomino, dun, brown
    const Vec3 horse = kCoats[g.seed % 7], legC = horse * 0.58f;
    (void)coat;
    // Barrel.
    {
        Loft L;
        const int N = lod.torsoSeg;
        const float zs[] = {0.0f, 0.12f, 0.28f, 0.46f, 0.64f, 0.80f, 0.92f, 1.0f};
        for (float f : zs) {
            const float z = lerp(m.barrelFront, m.barrelBack, f);
            // Height profile: tall chest that rises to swallow the human hips, a deep girth,
            // a tucked flank, a round croup.
            const float top = m.barrelY + m.barrelHalfH * (1.0f + 0.55f * smoothstep(0.30f, 0.0f, f) - 0.10f * bump((f - 0.6f) / 0.3f) +
                                                           0.06f * bump((f - 0.85f) / 0.15f));
            const float bot = m.barrelY - m.barrelHalfH * (0.95f - 0.25f * smoothstep(0.55f, 0.95f, f) - 0.15f * smoothstep(0.25f, 0.0f, f));
            const float hw = m.barrelHalfW * (0.82f + 0.18f * bump((f - 0.45f) / 0.5f) + 0.08f * bump((f - 0.88f) / 0.15f)) *
                             (f < 0.02f || f > 0.99f ? 0.55f : 1.0f);
            const float cy = 0.5f * (top + bot), hh = 0.5f * (top - bot) * (f < 0.02f || f > 0.99f ? 0.6f : 1.0f);
            L.rings.push_back(makeRing({0, cy, z}, {1, 0, 0}, {0, 1, 0}, SuperRing{hw, 0, hh, hh, 2.3f, 2.3f}, N));
            L.t.push_back(f);
        }
        L.poleStart = true;
        L.startPole = {0, m.barrelY + 0.3f * m.barrelHalfH, m.barrelFront + 0.25f * h};
        L.poleEnd = true;
        L.endPole = {0, m.barrelY + 0.2f * m.barrelHalfH, m.barrelBack - 0.25f * h};
        Part& bp = newPart();
        bp.id = bodyGroup_;
        emitLoft(bp, L, [=](float, float, Vec3) { return one(hips); }, [=](float, float) { return Mat{horse, MaterialClass::Skin}; });
    }
    // Legs.
    auto leg = [&](const LimbRig& rig, Vec3 a, Vec3 b, Vec3 c, Vec3 d, float r0, bool hind) {
        Loft L;
        struct K { Vec3 p; float r, t; int seg; };
        // Muscular forearm / gaskin at the top, then the slender cannon, a knobbly
        // knee/hock, the fetlock and a short pastern.
        std::vector<K> ks = {{a + Vec3{0, 0.35f * h, 0}, r0 * 1.10f, 0.0f, 0}, {a, r0 * 1.05f, 0.08f, 0},
                             {lerp(a, b, 0.45f), r0 * (hind ? 0.95f : 0.80f), 0.25f, 0}, {lerp(a, b, 0.85f), r0 * 0.52f, 0.40f, 0},
                             {b, r0 * 0.56f, 0.45f, 1}, {lerp(b, c, 0.5f), r0 * 0.38f, 0.62f, 1}, {c, r0 * 0.50f, 0.80f, 2},
                             {d + Vec3{0, 0.05f * h, 0}, r0 * 0.40f, 0.92f, 2}};
        for (size_t k = 0; k < ks.size(); ++k) {
            const Vec3 dd = normalize(ks[std::min(k + 1, ks.size() - 1)].p - ks[k > 0 ? k - 1 : 0].p);
            const Vec3 v = orthoFront(dd), u = cross(dd, v);
            L.rings.push_back(makeRing(ks[k].p, u, v, SuperRing{ks[k].r * 0.85f, 0, ks[k].r * 1.05f, ks[k].r, 2.1f, 2.1f}, lod.limbSeg));
            L.t.push_back(ks[k].t);
        }
        const int up = rig.upper, lo = rig.lower, ft = rig.end, tp = rig.tip;
        Part& lp = newPart();
        lp.id = bodyGroup_;
        emitLoft(lp, L, [=](float t, float, Vec3) {
            return chainWeights({{0.02f, hips}, {0.12f, up}, {0.38f, up}, {0.50f, lo}, {0.74f, lo}, {0.86f, ft}, {0.96f, tp}}, t);
        }, [=](float t, float) { return Mat{lerp(horse, legC, smoothstep(0.35f, 0.6f, t)), MaterialClass::Skin}; });
        // Hoof: a short dark cone under the pastern.
        Loft H;
        const float R = r0 * 0.62f;
        const Vec3 base{d.x, 0.0f, d.z + 0.02f * h};
        H.rings.push_back(makeRing(base + Vec3{0, 0.16f * h, -0.03f * h}, {1, 0, 0}, {0, 0, 1}, SuperRing{R * 0.85f, 0, R * 0.9f, R * 0.85f}, lod.limbSeg));
        H.t.push_back(0);
        H.rings.push_back(makeRing(base + Vec3{0, 0.004f, 0}, {1, 0, 0}, {0, 0, 1}, SuperRing{R * 1.08f, 0, R * 1.2f, R * 0.95f}, lod.limbSeg));
        H.t.push_back(1);
        H.poleEnd = true;
        H.endPole = base;
        H.tEnd = 1;
        Part& hp = newPart();
        hp.id = bodyGroup_;
        emitLoft(hp, H, [=](float, float, Vec3) { return one(tp); }, [=](float, float) { return hoofM; });
    };
    const float r0 = 0.34f * h;
    for (int s = 0; s < 2; ++s) {
        leg(ch.rig.legs[size_t(s)], m.hipJoint[s], m.knee[s], m.ankle[s], m.ball[s], r0, false);
        leg(ch.rig.legs[size_t(s + 2)], m.hindHip[s], m.hindKnee[s], m.hindAnkle[s], m.hindBall[s], r0 * 1.12f, true);
    }
}

// Digitigrade feet: a long metatarsal from the raised hock down to the toes, then a
// paw (padded toes with claws) or a cloven hoof on the ground.
void Builder::buildDigitigradeFeet() {
    const float h = m.h, fs = g.body.footSize, Cf = g.body.calf * h * (1 + 0.2f * g.body.muscle);
    const bool hoof = g.body.hooves;
    const Mat hoofM{g.palette.horn * 0.35f + Vec3{0.05f, 0.04f, 0.04f}, MaterialClass::Horn};
    const Mat clawM{g.palette.horn * 0.6f + Vec3{0.03f, 0.03f, 0.03f}, MaterialClass::Horn};
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const float sd = s == 0 ? 1.0f : -1.0f;
        const Vec3 hk = m.ankle[s], bl = m.ball[s];
        const int foot = rig.end, toe = rig.tip;
        // Metatarsal: from inside the end of the shin down to the ball.
        {
            const Vec3 d = normalize(bl - hk);
            const Vec3 v = orthoFront(d), u = cross(d, v);
            Loft L;
            // Starts back inside the end of the shin, at the shin's ankle radius: no step at the hock.
            const float ts[] = {-0.22f, 0.10f, 0.55f, 0.92f};
            const float rs[] = {0.66f, 0.62f, 0.50f, 0.52f};
            for (int k = 0; k < 4; ++k) {
                const float r0 = rs[k] * Cf;
                L.rings.push_back(makeRing(lerp(hk, bl, ts[k]), u, v, SuperRing{r0 * 0.85f, 0, r0 * 1.05f, r0 * 1.15f, 2.2f, 2.2f}, lod.limbSeg));
                L.t.push_back(ts[k]);
            }
            emitLoft(newPart(), L, [=](float t, float, Vec3) { return blend(foot, toe, smoothstep(0.6f, 0.95f, t)); },
                     [&](float, float) { return skin(); });
        }
        if (hoof) {  // a cloven hoof under the ball
            Loft L;
            const float R = 0.62f * Cf * fs;
            const float ys[] = {bl.y + 0.05f * h, 0.45f * (bl.y + 0.05f * h), 0.004f};
            const float rs[] = {0.85f, 1.0f, 1.12f};
            for (int k = 0; k < 3; ++k) {
                auto ring = makeRing({bl.x, ys[k], bl.z + 0.02f * h * float(k)}, {1, 0, 0}, {0, 0, 1}, SuperRing{R * rs[k], 0, R * rs[k] * 1.15f, R * rs[k] * 0.9f, 2.4f, 2.4f},
                                     lod.limbSeg);
                for (Vec3& p : ring) p.z -= 0.25f * R * bump((p.x - bl.x) / (0.12f * R)) * smoothstep(bl.z, bl.z + R, p.z) * float(k) / 2.0f;  // cleft
                L.rings.push_back(ring);
                L.t.push_back(float(k) / 2.0f);
            }
            L.poleEnd = true;
            L.endPole = {bl.x, 0.0f, bl.z + 0.04f * h};
            L.tEnd = 1;
            emitLoft(newPart(), L, [=](float, float, Vec3) { return one(toe); }, [&](float, float) { return hoofM; });
            continue;
        }
        // Paw: a padded base and four toes, each ending in a claw.
        const float pw = 0.19f * h * fs, pl = 0.36f * h * fs;
        {
            Loft L;
            const float ts[] = {0.0f, 0.4f, 0.8f};
            const float ws[] = {0.75f, 1.0f, 0.95f};
            const float hs[] = {1.0f, 0.75f, 0.55f};
            for (int k = 0; k < 3; ++k) {
                const Vec3 c{bl.x, (0.055f * h) * hs[k] + 0.004f, bl.z - 0.05f * h + ts[k] * pl};
                L.rings.push_back(makeRing(c, {1, 0, 0}, {0, 1, 0}, SuperRing{pw * ws[k], 0, 0.055f * h * hs[k], 0.045f * h * hs[k], 2.4f, 3.0f},
                                           lod.limbSeg));
                L.t.push_back(ts[k]);
            }
            L.poleStart = true;
            L.startPole = {bl.x, 0.05f * h, bl.z - 0.09f * h};
            L.poleEnd = true;
            L.endPole = {bl.x, 0.03f * h, bl.z - 0.05f * h + 0.95f * pl};
            emitLoft(newPart(), L, [=](float, float, Vec3) { return one(toe); }, [&](float, float) { return skin(); });
        }
        for (int k = 0; k < 4; ++k) {
            const float off = (float(k) - 1.5f) / 1.5f;  // -1 .. 1 across the paw
            const Vec3 base{bl.x + sd * 0.0f + off * 0.75f * pw, 0.030f * h, bl.z - 0.05f * h + (0.78f - 0.10f * std::fabs(off)) * pl};
            const Vec3 d = normalize(Vec3{off * 0.25f, -0.35f, 1.0f});
            const float tl = 0.10f * h * fs * (1.0f - 0.2f * std::fabs(off)), tr = 0.032f * h * fs;
            const Vec3 v = Vec3{0, 1, 0}, u = normalize(cross(d, v));
            Loft T;
            T.rings.push_back(makeRing(base, u, normalize(cross(u, d)), SuperRing{tr, 0, tr, tr}, 6));
            T.t.push_back(0);
            T.rings.push_back(makeRing(base + d * (0.6f * tl), u, normalize(cross(u, d)), SuperRing{tr * 0.9f, 0, tr * 0.9f, tr * 0.8f}, 6));
            T.t.push_back(0.6f);
            T.poleEnd = true;
            T.endPole = base + d * tl;
            T.tEnd = 1;
            emitLoft(newPart(), T, [=](float, float, Vec3) { return one(toe); },
                     [&](float t, float) { return g.body.claws && t > 0.7f ? clawM : skin(); });
            if (g.body.claws) {  // a hooked claw over the tip
                const Vec3 tip = base + d * tl;
                Loft C;
                C.rings.push_back(makeRing(tip - d * (0.012f * h), u, normalize(cross(u, d)), SuperRing{0.6f * tr, 0, 0.6f * tr, 0.5f * tr}, 5));
                C.t.push_back(0);
                C.poleEnd = true;
                C.endPole = tip + d * (0.05f * h * fs) + Vec3{0, -0.02f * h, 0};
                C.tEnd = 1;
                emitLoft(newPart(), C, [=](float, float, Vec3) { return one(toe); }, [&](float, float) { return clawM; });
            }
        }
    }
}

void Builder::buildTail() {
    if (!g.body.tail) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    // 0 plain, 1 heavy reptile, 2 whip with a spade, 3 bushy fur, 4 spiked club, 5 thin and ratty.
    const int type = g.body.tailFur ? 3 : g.body.hooves ? 0 : std::clamp(g.body.tailType, 0, 5);
    const float lenK[] = {1.0f, 1.05f, 1.25f, 1.0f, 0.95f, 1.15f};
    const float radK[] = {1.25f, 2.7f, 0.6f, 1.0f, 1.6f, 0.5f};
    float y0 = m.crotchY + 0.22f * m.torsoLen;
    float z0 = torsoRing(torsoT(y0)).backZ(0) - gluteDepth(0.22f) + 0.02f * h;
    if (g.body.centaur) y0 = m.barrelY + 0.80f * m.barrelHalfH, z0 = m.barrelBack + 0.05f * h;
    const float len = lenK[type] * g.body.tailLength * (g.body.centaur ? m.barrelY + 0.4f * m.barrelHalfH : m.hipJointY) * 0.95f;
    const float r0 = 0.11f * h * (1 + 0.3f * g.body.muscle) * (g.body.centaur ? 1.6f : 1.0f);
    const float rt = r0 * radK[type];
    constexpr int kPts = 9;
    std::vector<Vec3> pts;
    Vec3 p{0, y0, z0};
    float step = len / (kPts - 1);
    for (int i = 0; i < kPts; ++i) {
        pts.push_back(p);
        float u = float(i) / (kPts - 1);
        Vec3 d;
        switch (type) {
            case 1: d = {0, lerp(-1.5f, 0.05f, u), -1.0f}; break;                            // heavy, carried low
            case 2: d = {0.30f * std::sin(u * 4.0f), lerp(-1.6f, 1.2f, u), -1.0f}; break;     // whip curling up
            case 3: d = {0, lerp(-1.6f, -0.9f, u), -0.55f}; break;                            // bushy, hanging
            case 5: d = {0.22f * std::sin(u * 5.0f), lerp(-1.8f, -0.4f, u), -0.8f}; break;   // limp rat tail
            case 4: d = {0, lerp(-1.3f, 0.2f, u), -1.0f}; break;
            default: d = {0, lerp(-1.2f, 0.3f, u), -1.0f}; break;
        }
        p += normalize(d) * step;
        // Long tails on short bodies reach the ground: they trail along it instead.
        if (p.y < 0.04f * h) p.y = 0.04f * h;
    }
    const int nb = 4;
    int parent = r.hips;
    for (int b = 0; b < nb; ++b) {
        parent = sk.add("tail" + std::to_string(b), parent, pts[size_t(b * (kPts - 1) / nb)]);
        r.tail.push_back(parent);
    }
    std::vector<int> bones = r.tail;
    const int hips = r.hips;
    auto tw = [=](float t) {
        float f = t * nb;
        int i0 = std::clamp(int(f), 0, nb - 1);
        if (i0 == nb - 1) return one(bones[size_t(i0)]);
        if (t < 0.05f) return blend(hips, bones[0], t / 0.05f);
        return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], smoothstep(0.3f, 1.0f, f - i0));
    };
    auto dirAt = [&](int i) {
        return normalize(i + 1 < kPts ? pts[size_t(i + 1)] - pts[size_t(i)] : pts[size_t(i)] - pts[size_t(i - 1)]);
    };
    if (bonesOnly_) {  // a chain of shrinking vertebrae
        Part& vp = newPart();
        const Mat bone{{0.80f, 0.76f, 0.63f}, MaterialClass::Horn};
        for (int i = 0; i + 1 < kPts; ++i) {
            const float u = float(i) / (kPts - 1);
            Ellipsoid e;
            e.center = lerp(pts[size_t(i)], pts[size_t(i + 1)], 0.5f);
            e.axis = normalize(pts[size_t(i + 1)] - pts[size_t(i)]);
            e.up = {0, 1, 0};
            e.rAxis = 0.4f * step;
            e.rU = e.rV = 0.35f * rt * (1.0f - 0.6f * u);
            e.lat = 3;
            e.lon = 5;
            const int bi = r.tail[size_t(std::min(int(u * nb), nb - 1))];
            emitEllipsoid(vp, e, [=](float, float, Vec3) { return one(bi); }, [=](float, float) { return bone; });
        }
    } else {
    Loft L;
    for (int i = 0; i < kPts; ++i) {
        float u = float(i) / (kPts - 1);
        Vec3 d = dirAt(i);
        Vec3 v = normalize(cross(d, {1, 0, 0}), {0, 1, 0});
        Vec3 uu = cross(d, v);
        // Furred tails are bushy (fullest past the middle); bovine tails end in a tuft.
        float rad;
        switch (type) {
            case 1: rad = rt * (1.0f - 0.88f * std::pow(u, 0.75f)); break;
            case 2: rad = rt * (1.0f - 0.55f * u); break;
            case 3: rad = r0 * (0.75f + 1.2f * bump((u - 0.55f) / 0.55f)) * (1.0f - 0.5f * smoothstep(0.85f, 1.0f, u)); break;
            case 4: rad = rt * (1.0f - 0.55f * u); break;
            case 5: rad = rt * (1.0f - 0.78f * u); break;
            default:
                rad = g.body.hooves ? r0 * (1.0f - 0.85f * u) + 1.4f * r0 * bump((u - 0.92f) / 0.10f) : r0 * (1.0f - 0.8f * u);
        }
        L.rings.push_back(makeRing(pts[size_t(i)], uu, v, SuperRing{rad, 0, rad, rad}, type == 2 || type == 5 ? 5 : lod.limbSeg));
        L.t.push_back(u);
    }
    L.poleStart = true;
    L.startPole = pts[0] + Vec3{0, 0, 0.05f * h};
    L.poleEnd = true;
    L.endPole = pts.back() + dirAt(kPts - 1) * (0.6f * rt);
    L.tEnd = 1;
    Vec3 base = g.palette.skin;
    const bool fur = type == 3, tuft = g.body.hooves && !fur;
    const Vec3 hairC = g.palette.hair;
    const float darken = type == 1 ? 0.30f : 0.15f;
    emitLoft(newPart(), L, [=](float t, float, Vec3) { return tw(t); }, [=](float t, float) {
        if (fur) return Mat{lerp(hairC, base, 0.3f) * (1.0f - 0.2f * t), MaterialClass::Hair};
        if (tuft && t > 0.8f) return Mat{hairC, MaterialClass::Hair};
        return Mat{base * (1.0f - darken * t), MaterialClass::Skin};
    });
    }
    // Tips and spines: horn, or bone once the flesh is gone.
    const Vec3 hornC = bonesOnly_ ? Vec3{0.80f, 0.76f, 0.63f} : g.palette.horn;
    const Mat hornM{hornC, MaterialClass::Horn};
    const Vec3 tip = pts.back(), tipD = dirAt(kPts - 1);
    if (type == 2 && !bonesOnly_) {  // spade: a flat diamond blade
        emitSpike(newPart(), tip - tipD * (0.3f * rt), tipD, {0, 1, 0}, 3.4f * rt, 2.6f * rt, 1, one(bones.back()),
                  Mat{g.palette.skin * 0.8f, MaterialClass::Skin});
    }
    if (type == 4) {  // club: a bony mace head ringed with spikes
        Part& cp = newPart();
        const float R = 1.35f * rt;
        const Vec3 c = tip + tipD * (0.4f * R);
        Ellipsoid e;
        e.center = c;
        e.axis = tipD;
        e.up = {0, 1, 0};
        e.rAxis = 1.15f * R;
        e.rU = e.rV = R;
        e.lat = 4;
        e.lon = 7;
        const int bb = bones.back();
        emitEllipsoid(cp, e, [=](float, float, Vec3) { return one(bb); }, [=](float, float) { return hornM; });
        const Vec3 up = normalize(Vec3{0, 1, 0} - tipD * tipD.y, {0, 1, 0}), sd = normalize(cross(tipD, up));
        for (int k = 0; k < 6; ++k) {
            const float a = 2 * kPi * k / 6 + 0.3f;
            const Vec3 n = normalize(up * std::cos(a) + sd * std::sin(a));
            emitSpike(cp, c + n * (0.8f * R), n, tipD, 1.3f * R, 0.42f * R, 0, one(bb), hornM);
        }
        emitSpike(cp, c + tipD * (0.9f * R), tipD, {0, 1, 0}, 1.1f * R, 0.40f * R, 0, one(bb), hornM);
    }
    if (g.body.spines > 0 && type != 3) {  // the dorsal ridge carries on down the tail, shrinking
        Part& sp = newPart();
        const int n = std::max(2, g.body.spines / 2 + 1);
        for (int k = 0; k < n; ++k) {
            const float u = 0.12f + 0.70f * float(k) / std::max(1, n - 1);
            const float f = u * (kPts - 1);
            const int i = std::min(int(f), kPts - 2);
            const Vec3 c = lerp(pts[size_t(i)], pts[size_t(i + 1)], f - i);
            const Vec3 d = dirAt(i);
            const Vec3 up = normalize(Vec3{0, 1, 0} - d * d.y, {0, 1, 0});
            const float rad = std::max(rt * (1.0f - 0.7f * u), 0.25f * r0);
            const float sz = g.body.spineSize * (1.0f - 0.55f * u) * clamp(radK[type] / 1.3f, 0.45f, 1.1f);
            emitSpike(sp, c + up * (0.8f * rad), normalize(up - d * 0.2f), d, 0.40f * h * sz, 0.10f * h * sz, g.body.spineKind, tw(u),
                      hornM);
        }
    }
    SpringChain sc;
    sc.name = "tail";
    sc.bones = bones;
    sc.tail = pts.back() - sk.bones[size_t(bones.back())].restWorld;
    sc.stiffness = type == 1 || type == 4 ? 1.5f : type == 2 || type == 5 ? 0.6f : 0.9f;
    sc.drag = 0.35f;
    sc.gravity = type == 1 || type == 4 ? 0.35f : 0.2f;
    sc.hitRadius = rt * 0.6f;
    ch.physics.chains.push_back(sc);
}

// A glowing orb set into the body (constructs, engines of power): a dark metal
// socket ring with the emissive sphere bulging out of it.
void Builder::buildCore() {
    if (g.body.core <= 0 || bonesOnly_) return;
    const float h = m.h;
    const RigInfo& r = ch.rig;
    const bool chest = g.body.core == 2;
    const float t = chest ? 0.74f : 0.38f;
    const Vec3 q = torsoPoint(t, 0.5f * kPi);
    const Vec3 n{0, 0, 1};
    const float R = (chest ? 0.17f : 0.20f) * h;
    const int bone = chest ? r.chest : r.spine;
    Part& p = newPart();
    Ellipsoid ring;
    ring.center = q - n * (0.10f * R);
    ring.axis = n;
    ring.up = {0, 1, 0};
    ring.rAxis = 0.42f * R;
    ring.rU = ring.rV = 1.40f * R;
    ring.lat = 3;
    ring.lon = 10;
    const Mat socket{lerp(g.palette.skin, Vec3{0.16f, 0.17f, 0.20f}, 0.7f), MaterialClass::Metal};
    emitEllipsoid(p, ring, [=](float, float, Vec3) { return one(bone); }, [=](float, float) { return socket; });
    Ellipsoid orb;
    orb.center = q + n * (0.22f * R);
    orb.axis = n;
    orb.up = {0, 1, 0};
    orb.rAxis = 0.70f * R;
    orb.rU = orb.rV = R;
    orb.lat = 4;
    orb.lon = 10;
    const Vec3 hc = g.body.coreColor * 0.55f + Vec3{0.45f, 0.45f, 0.45f};
    const Vec3 hot{std::min(hc.x, 1.0f), std::min(hc.y, 1.0f), std::min(hc.z, 1.0f)};
    emitEllipsoid(p, orb, [=](float, float, Vec3) { return one(bone); },
                  [=, cc = g.body.coreColor](float tt, float) { return Mat{lerp(hot, cc, smoothstep(0.35f, 0.9f, tt)), MaterialClass::Glow}; });
}

// Skin kinds: metallic flesh (androids, golems) keeps the forms and gains a metal
// sheen; the carapace breaks the surface into hard faceted plates (Voronoi cells)
// with dark flesh in the seams, leaving the face bare unless coverage is total.
void Builder::applySkinKind() {
    const int kind = g.body.skinKind;
    if (kind == 0 || bonesOnly_) return;
    const float h = m.h, S = 0.42f * h;
    const uint64_t seed = g.seed * 0x9E3779B97F4A7C15ull + 77;
    auto hash = [&](int x, int y, int z, uint64_t k) {
        uint64_t v = splitmix64(seed ^ (uint64_t(uint32_t(x)) * 73856093ull) ^ (uint64_t(uint32_t(y)) * 19349663ull) ^
                                (uint64_t(uint32_t(z)) * 83492791ull) ^ (k * 0x632BE59BD9B4E019ull));
        return float(v >> 40) / float(1ull << 24);
    };
    // Smooth value noise (period ~3 plates): partial coverage grows in contiguous
    // patches, and whole plates are kept or dropped so the edge follows the seams.
    auto noise = [&](Vec3 q) {
        const int x0 = int(std::floor(q.x)), y0 = int(std::floor(q.y)), z0 = int(std::floor(q.z));
        const Vec3 f{smoothstep(0, 1, q.x - x0), smoothstep(0, 1, q.y - y0), smoothstep(0, 1, q.z - z0)};
        float acc = 0;
        for (int k = 0; k < 8; ++k) {
            const int dx = k & 1, dy = (k >> 1) & 1, dz = k >> 2;
            acc += hash(x0 + dx, y0 + dy, z0 + dz, 9) * (dx ? f.x : 1 - f.x) * (dy ? f.y : 1 - f.y) * (dz ? f.z : 1 - f.z);
        }
        return acc;
    };
    const float cover = g.body.carapace * 1.25f - 0.12f;
    const Vec3 hc = headCenter();
    const float faceY0 = m.headOrigin.y + 0.05f * h, faceY1 = m.headOrigin.y + 0.80f * h;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        Part& p = parts[pi];
        if (p.kind != SubmeshKind::Body) continue;
        const bool headish = pi == headPart_ || pi == nosePart_;
        for (auto& tr : p.tris) {
            if (tr.m.cls != MaterialClass::Skin) continue;
            if (kind == 1) {
                tr.m.cls = MaterialClass::Metal;
                continue;
            }
            const Vec3 c = (p.pos[tr.a] + p.pos[tr.b] + p.pos[tr.c]) / 3.0f;
            if (headish && g.body.carapace < 0.95f && c.z > hc.z + 0.10f * h && c.y > faceY0 && c.y < faceY1) continue;
            // Nearest and second-nearest feature points of the 3D cell grid.
            const Vec3 u = c / S;
            const int ix = int(std::floor(u.x)), iy = int(std::floor(u.y)), iz = int(std::floor(u.z));
            float d1 = 1e9f, d2 = 1e9f;
            int bx = 0, by = 0, bz = 0;
            Vec3 bf;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = ix + dx, y = iy + dy, z = iz + dz;
                        const Vec3 f{float(x) + hash(x, y, z, 1), float(y) + hash(x, y, z, 2), float(z) + hash(x, y, z, 3)};
                        const float d = length(f - u);
                        if (d < d1) d2 = d1, d1 = d, bx = x, by = y, bz = z, bf = f;
                        else if (d < d2) d2 = d;
                    }
            if (noise(bf * (1.0f / 3.0f)) > cover) continue;  // this plate has not grown (partial coverage)
            if (d2 - d1 < 0.07f) {  // seam: a dark crack between the plates
                tr.m.color = lerp(g.body.carapaceColor, tr.m.color, 0.25f) * 0.45f;
                tr.m.cls = MaterialClass::Dark;
                continue;
            }
            tr.m.color = g.body.carapaceColor * (0.88f + 0.24f * hash(bx, by, bz, 5));
            tr.m.cls = MaterialClass::Metal;
        }
    }
}

// Dorsal ridge: spikes, plates or knobs along the back midline, biggest between the
// shoulder blades, shrinking towards the neck and the small of the back.
void Builder::buildSpines() {
    if (g.body.spines <= 0) return;
    const float h = m.h;
    const RigInfo& r = ch.rig;
    const int n = g.body.spines;
    const Vec3 hornC = bonesOnly_ ? Vec3{0.80f, 0.76f, 0.63f} : g.palette.horn;
    Part& p = newPart();
    for (int k = 0; k < n; ++k) {
        const float a = float(k) / std::max(1, n - 1);
        const float t = lerp(0.30f, 0.97f, a);
        const Vec3 q = torsoPoint(t, 1.5f * kPi);
        const float prof = 0.55f + 0.45f * bump((a - 0.62f) / 0.62f);
        const float sz = g.body.spineSize * prof * (g.body.centaur ? 0.8f : 1.0f);
        const float len = (g.body.spineKind == 2 ? 0.24f : g.body.spineKind == 1 ? 0.42f : 0.55f) * h * sz;
        const float wid = (g.body.spineKind == 1 ? 0.17f : g.body.spineKind == 2 ? 0.12f : 0.11f) * h * sz;
        // Spikes rake back towards the hips; near the neck they lean up and back.
        const Vec3 dir = normalize(Vec3{0, lerp(-0.45f, 0.25f, a), -1.0f});
        const Vec3 sweep = Vec3{0, -1, 0};
        const SkinW w = chainWeights({{0.12f, r.hips}, {0.40f, r.spine}, {0.66f, r.chest}, {1.0f, r.chest}, {1.16f, r.neck}}, t);
        const float shade = 1.0f - 0.12f * float(k % 2);
        // Plates grow from hide (darker, skin-toned at the root) and stagger left and right.
        const bool plate = g.body.spineKind == 1;
        const Vec3 col = plate && !bonesOnly_ ? lerp(g.palette.skin, hornC, 0.55f) * 0.8f : hornC;
        const Vec3 at = q + Vec3{plate ? (k % 2 ? 0.035f : -0.035f) * h : 0.0f, 0, 0.01f * h};
        emitSpike(p, at, dir, sweep, len, wid, g.body.spineKind, w, Mat{col * shade, MaterialClass::Horn});
    }
}

// Capsules/spheres the hair and tails collide with; also exported for engines.
void Builder::buildColliders() {
    const RigInfo& r = ch.rig;
    const Skeleton& sk = ch.skeleton;
    const float h = m.h;
    auto add = [&](int bone, Vec3 a, Vec3 b, float rad) {
        Vec3 o = sk.bones[size_t(bone)].restWorld;
        ch.physics.colliders.push_back({bone, a - o, b - o, rad});
    };
    Vec3 hc = headCenter();
    add(r.head, hc, hc + Vec3{0, 0.08f * h, -0.04f * h}, 0.44f * h * g.body.headWidth);
    add(r.head, m.headOrigin + Vec3{0, 0.22f * h, 0.10f * h}, m.headOrigin + Vec3{0, 0.30f * h, 0.12f * h}, 0.27f * h);
    add(r.neck, m.neckBase, m.headPivot, g.body.neckThick * h * 1.05f);
    auto band = [&](int bone, float t, float extraBack) {
        SuperRing tr = torsoRing(t);
        float rad = 0.5f * (tr.dF + tr.dB) + extraBack * 0.5f;
        float zc = tr.zc + 0.5f * (tr.dF - tr.dB - extraBack);
        float y = m.crotchY + t * m.torsoLen;
        float half = std::max(0.0f, tr.w - rad);
        add(bone, {-half, y, zc}, {half, y, zc}, rad);
    };
    band(r.chest, 0.88f, 0.0f);
    band(r.chest, 0.74f, 0.0f);
    band(r.spine, 0.48f, 0.0f);
    band(r.hips, 0.12f, gluteDepth(0.06f));
    if (r.bustL >= 0) {
        float depth = (0.06f + 0.31f * std::min(g.body.bust, 1.5f)) * h;
        const float B = std::min(g.body.bust, 1.5f), sag = g.body.sag;
        for (int b : {r.bustL, r.bustR}) {
            Vec3 c = sk.bones[size_t(b)].restWorld;
            add(b, c + Vec3{0, 0, 0.25f * depth}, c + Vec3{0, 0, 0.25f * depth}, 0.75f * depth + 0.03f * h);
            // Bigger / older tissue: lower frequency, more travel, less damping.
            JiggleBone jb;
            jb.bone = b;
            jb.frequency = 3.2f - 1.0f * B - 0.5f * sag;
            jb.damping = 0.22f - 0.05f * B + 0.05f * sag;
            jb.limit = Vec3{0.22f, 0.32f + 0.1f * sag, 0.24f} * depth + Vec3{0.006f, 0.008f, 0.006f};
            jb.gravity = 0.04f + 0.04f * sag;
            ch.physics.jiggles.push_back(jb);
        }
    }
    for (size_t i = 0; i < std::min<size_t>(2, r.arms.size()); ++i) {
        const ArmMarks& a = m.arms[i];
        add(r.arms[i].upper, a.shoulder, a.elbow, a.radius * 1.05f);
        add(r.arms[i].lower, a.elbow, a.wrist, a.radius * 0.85f);
    }
    for (int s = 0; s < 2; ++s) add(r.legs[size_t(s)].upper, m.hipJoint[s], m.knee[s], g.body.thigh * h * 0.95f);
    if (g.body.centaur)  // the horse barrel: hair and tails rest on it
        add(r.hips, Vec3{0, m.barrelY + 0.1f * m.barrelHalfH, m.barrelFront - 0.6f * h}, Vec3{0, m.barrelY + 0.1f * m.barrelHalfH, m.barrelBack + 0.6f * h},
            0.92f * m.barrelHalfH);
}

// Exposed bones (undead): torn flesh painted as a dark cavity (a clip on the body
// surface, so the edge is exact) with bone geometry lying across it.
void Builder::buildBones() {
    const int bones = g.body.bones;
    if (!bones) return;
    const float h = m.h, side = float(g.body.boneSide);
    // (newPart() may reallocate `parts`: always index the body surface afresh.)
    auto clipBody = [&](ClipRegion c) { parts[torsoPart_].clips.push_back(std::move(c)); };
    const Mat cavity{{0.17f, 0.09f, 0.09f}, MaterialClass::Dark};
    const Mat bone{{0.84f, 0.80f, 0.68f}, MaterialClass::Horn};
    auto tube = [&](const std::vector<Vec3>& pts, float r0, int boneId) {
        Loft L;
        const size_t n = pts.size();
        for (size_t k = 0; k < n; ++k) {
            const Vec3 d = normalize(pts[std::min(k + 1, n - 1)] - pts[k > 0 ? k - 1 : 0]);
            const Vec3 v = normalize(cross(d, {0, 1, 0.3f}), {1, 0, 0}), u = cross(d, v);
            const float t = float(k) / float(n - 1);
            const float rr = r0 * (0.8f + 0.35f * (bump(t / 0.15f) + bump((t - 1.0f) / 0.15f)));  // knobbly ends
            L.rings.push_back(makeRing(pts[k], u, v, SuperRing{rr, 0, rr, rr}, 5));
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = pts[0] - normalize(pts[1] - pts[0]) * r0;
        L.poleEnd = true;
        L.endPole = pts.back() + normalize(pts.back() - pts[n - 2]) * r0;
        L.tEnd = 1;
        emitLoft(newPart(), L, [=](float, float, Vec3) { return one(boneId); }, [=](float, float) { return bone; });
    };
    if (bones & 1) {  // ribs through the flank
        const float tc = 0.645f, pc = 1.0f;
        clipBody({[=, this](Vec3 q) {
            const float t = torsoT(q.y);
            if (t < 0.4f || t > 0.9f || q.x * side < 0.0f) return 1.0f;
            if (std::fabs(q.x) > 1.25f * cHw_(clamp(t, -0.06f, 1.1f)) + 0.03f * h) return 1.0f;  // not the arms
            const float phi = kPi / 2 - std::atan2(q.z - cZc_(t), q.x * side);  // 0 front .. pi/2 side
            const float et = (t - tc) / 0.125f, ep = (phi - pc) / 0.50f, e = std::sqrt(et * et + ep * ep);
            return (e - 1.0f) * 0.1f * h;
        }, cavity});
        for (int k = 0; k < 5; ++k) {
            std::vector<Vec3> pts;
            for (int i = 0; i <= 6; ++i) {
                const float phi = lerp(0.55f, 1.45f, float(i) / 6.0f);
                const float t = tc - 0.090f + 0.044f * float(k) - 0.035f * (1.45f - phi) / 0.90f;
                const float th = kPi / 2 - side * phi;
                const Vec3 p = torsoPoint(t, th);
                const Vec3 out = normalize(Vec3{p.x, 0, p.z - cZc_(t)}, Vec3{side, 0, 0});
                pts.push_back(p + out * (0.004f * h));
            }
            tube(pts, 0.017f * h, ch.rig.chest);
        }
    }
    auto limb = [&](Vec3 a, Vec3 b, float r, Vec3 sideDir, int boneId, int count) {
        const Vec3 d = normalize(b - a);
        const float L = length(b - a);
        clipBody({[=](Vec3 q) {
            const float tt = dot(q - a, d) / L;
            const Vec3 radial = q - (a + d * (tt * L));
            if (length(radial) > 1.8f * r) return 1.0f;
            const float along = std::max(0.28f - tt, tt - 0.72f) * L;
            const float across = (count == 1 ? 0.05f : 0.20f) * r - dot(radial, sideDir);
            return std::max(along, across);
        }, cavity});
        const Vec3 perp = normalize(cross(d, sideDir));
        for (int k = 0; k < count; ++k) {
            const float o = count == 1 ? 0.0f : (k == 0 ? -0.28f : 0.28f);
            std::vector<Vec3> pts;
            for (int i = 0; i <= 4; ++i) {
                const float tt = lerp(0.24f, 0.76f, float(i) / 4.0f);
                pts.push_back(a + d * (tt * L) + sideDir * (0.82f * r) + perp * (o * r));
            }
            tube(pts, (count == 1 ? 0.30f : 0.19f) * r, boneId);
        }
    };
    if (bones & 2)  // a bared forearm (radius and ulna) on the outer side
        for (size_t i = 0; i < m.arms.size(); ++i) {
            const ArmMarks& am = m.arms[i];
            if (am.pair != 0 || am.side != side) continue;
            const Vec3 d = normalize(am.wrist - am.elbow);
            const Vec3 lat{am.side, 0, 0};
            const Vec3 sd = normalize(normalize(lat - d * dot(lat, d)) + Vec3{0, 0, -0.5f});
            limb(am.elbow, am.wrist, am.radius * 0.80f, sd, ch.rig.arms[i].lower, 2);
        }
    if (bones & 4) {  // the shin bone on the front of the opposite leg
        const int s = side > 0 ? 1 : 0;
        const Vec3 kn = m.knee[s], an = m.ankle[s], d = normalize(an - kn);
        const Vec3 fwd = normalize(Vec3{0, 0, 1} - d * d.z);
        limb(kn, an, g.body.calf * h * 0.85f, fwd, ch.rig.legs[size_t(s)].lower, 1);
    }
}

// Fur colouring for beast peoples: pale throat, chest and belly, a darker back and,
// for some, dark lower limbs. Per face, from the face normal and height.
void Builder::applyFur() {
    if (g.face.rot > 0.0f) {  // undead: mottled, bruised flesh
        auto hash = [](int x, int y, int z) {
            uint32_t n = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
            n = (n ^ (n >> 13)) * 1274126177u;
            return float(n & 0xFFFF) / 65535.0f;
        };
        auto noise = [&](Vec3 q) {  // trilinear value noise
            const float fx = std::floor(q.x), fy = std::floor(q.y), fz = std::floor(q.z);
            const int ix = int(fx), iy = int(fy), iz = int(fz);
            const float tx = smoothstep(0.0f, 1.0f, q.x - fx), ty = smoothstep(0.0f, 1.0f, q.y - fy), tz = smoothstep(0.0f, 1.0f, q.z - fz);
            float v = 0;
            for (int k = 0; k < 8; ++k) {
                const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
                v += hash(ix + dx, iy + dy, iz + dz + int(g.seed % 997)) * (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) * (dz ? tz : 1 - tz);
            }
            return v;
        };
        const Vec3 sk = g.palette.skin, bruise = sk * Vec3{0.62f, 0.62f, 0.70f}, rotC = sk * Vec3{0.70f, 0.78f, 0.58f};
        for (Part& p : parts) {
            if (p.kind != SubmeshKind::Body) continue;
            for (auto& tr : p.tris) {
                if (tr.m.cls != MaterialClass::Skin || length(tr.m.color - sk) > 0.02f) continue;
                const Vec3 c = (p.pos[tr.a] + p.pos[tr.b] + p.pos[tr.c]) / 3.0f;
                const float n1 = noise(c * (1.6f / m.h)), n2 = noise(c * (3.2f / m.h) + Vec3{9, 3, 5});
                Vec3 col = lerp(sk, bruise, g.face.rot * smoothstep(0.42f, 0.85f, n1));
                col = lerp(col, rotC, 0.6f * g.face.rot * smoothstep(0.55f, 0.90f, n2));
                tr.m.color = col;
            }
        }
    }
    const int pat = g.body.furPattern;
    if (!pat) return;
    const Vec3 sk = g.palette.skin;
    const Vec3 pale = lerp(sk, Vec3{0.93f, 0.88f, 0.80f}, 0.45f), dark = sk * 0.62f;
    for (Part& p : parts) {
        if (p.kind != SubmeshKind::Body) continue;
        for (auto& tr : p.tris) {
            if (tr.m.cls != MaterialClass::Skin || length(tr.m.color - sk) > 0.02f) continue;
            const Vec3 A = p.pos[tr.a], B = p.pos[tr.b], C = p.pos[tr.c], c = (A + B + C) / 3.0f;
            const Vec3 n = normalize(cross(B - A, C - A), {0, 0, 1});
            const float front = smoothstep(0.25f, 0.85f, n.z) * smoothstep(m.crotchY - 0.1f * m.h, m.crotchY + 0.2f * m.h, c.y) *
                                smoothstep(m.H, m.H - 0.6f * m.h, c.y + 0.3f * m.h * smoothstep(0.0f, 0.3f, -n.y));
            const float back = smoothstep(-0.2f, -0.8f, n.z) * smoothstep(m.crotchY, m.shoulderY, c.y);
            Vec3 col = lerp(sk, pale, 0.38f * front);
            col = lerp(col, dark, 0.35f * back);
            if (pat >= 2) col = lerp(col, dark, 0.7f * smoothstep(0.16f * m.hipJointY, 0.04f * m.hipJointY, c.y));
            tr.m.color = col;
            tr.m.cls = MaterialClass::Hair;  // fur reads with the hair's cool shadow and sheen, not as skin
        }
    }
}

// Fur tufts (beast peoples): small faceted wedges laid along the limbs and over the
// shoulders, chest and back, combed with the limb, so the body reads as furred.
void Builder::buildFurTufts() {
    if (g.body.furPattern < 2) return;
    const float h = m.h;
    Rng r = Rng(g.seed).fork(61);
    Part& p = newPart();
    const Vec3 c0 = lerp(g.palette.hair, g.palette.skin, 0.35f);
    auto tuft = [&](Vec3 base, Vec3 n, Vec3 flow, float w, float L, int bone) {
        flow = normalize(flow - n * dot(flow, n), Vec3{0, -1, 0});
        const Vec3 side = normalize(cross(n, flow));
        const SkinW sw = one(bone);
        const uint32_t i0 = uint32_t(p.pos.size());
        const Vec3 pts[4] = {base + side * (0.5f * w) - n * (0.01f * h), base - side * (0.5f * w) - n * (0.01f * h),
                             base - flow * (0.35f * w) + n * (0.018f * h), base + flow * L + n * (0.14f * L)};
        for (const Vec3& q : pts) {
            p.pos.push_back(q);
            p.w.push_back(sw);
            p.uv.push_back({});
        }
        const Mat mt{c0 * r.range(0.85f, 1.08f), MaterialClass::Hair};
        const uint32_t tri[3][3] = {{0, 2, 3}, {2, 1, 3}, {1, 0, 3}};
        for (const auto& t : tri) {
            uint32_t a = i0 + t[0], b = i0 + t[1], c = i0 + t[2];
            const Vec3 A = p.pos[a], B = p.pos[b], C = p.pos[c];
            if (dot(cross(B - A, C - A), (A + B + C) / 3.0f - (base - n * (0.05f * h))) < 0) std::swap(b, c);
            p.tris.push_back({a, b, c, mt});
        }
    };
    // Along a limb segment, on the arc of directions around `out` (radians either side).
    auto alongLimb = [&](Vec3 a, Vec3 b, float r0, float r1, Vec3 out, float arc, int rows, int cols, int bone, float size) {
        const Vec3 d = normalize(b - a);
        out = normalize(out - d * dot(out, d), Vec3{1, 0, 0});
        const Vec3 o2 = normalize(cross(d, out));
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                const float t = (float(i) + 0.5f + r.range(-0.2f, 0.2f)) / float(rows);
                const float ang = cols == 1 ? 0.0f : lerp(-arc, arc, float(j) / float(cols - 1)) + r.range(-0.15f, 0.15f);
                const Vec3 n = normalize(out * std::cos(ang) + o2 * std::sin(ang));
                const float rr = lerp(r0, r1, t);
                tuft(lerp(a, b, t) + n * (0.90f * rr), n, d, 3.0f * size * h * r.range(0.8f, 1.2f), 2.4f * size * h * r.range(0.8f, 1.25f), bone);
            }
    };
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& am = m.arms[i];
        if (am.pair != 0) continue;
        const Vec3 back = normalize(Vec3{am.side * 0.6f, 0, -1});
        alongLimb(am.elbow, am.wrist, am.radius * 0.95f, am.radius * 0.70f, back, 0.9f, 4, 3, ch.rig.arms[i].lower, 0.055f);
        alongLimb(am.shoulder, am.elbow, am.radius * 1.1f, am.radius * 0.95f, Vec3{am.side, 0.2f, -0.3f}, 0.7f, 2, 3, ch.rig.arms[i].upper, 0.06f);
    }
    for (int s = 0; s < 2; ++s) {
        const LimbRig& lg = ch.rig.legs[size_t(s)];
        const float sd = s == 0 ? 1.0f : -1.0f;
        const float Cf = g.body.calf * h, T = g.body.thigh * h;
        alongLimb(m.knee[s], m.ankle[s], Cf * 1.0f, Cf * 0.65f, Vec3{0, 0, -1}, 1.1f, 3, 3, lg.lower, 0.06f);
        alongLimb(m.hipJoint[s], m.knee[s], T * 0.95f, T * 0.6f, Vec3{sd, 0, -0.4f}, 0.8f, 3, 2, lg.upper, 0.065f);
    }
    // Shoulders, upper back and the chest ruff under the throat.
    for (int i = 0; i < 26; ++i) {
        const bool chest = i < 9;
        const float t = chest ? r.range(0.84f, 0.98f) : r.range(0.78f, 1.0f);
        const float th = chest ? kPi / 2 + r.range(-0.55f, 0.55f) : 1.5f * kPi + r.range(-1.25f, 1.25f);
        const Vec3 q = torsoPoint(t, th);
        const Vec3 n = normalize(Vec3{q.x, 0.35f, q.z - cZc_(t)}, Vec3{0, 0, 1});
        tuft(q, n, Vec3{0, -1, chest ? 0.2f : -0.2f}, 0.14f * h * r.range(0.8f, 1.2f), 0.26f * h * r.range(0.8f, 1.25f), ch.rig.chest);
    }
}

Character Builder::build() {
    ch.genome = g;
    ch.height = m.H;
    if (bonesOnly_) {  // bare bones: the same rig, nothing but the skeleton (and horns, tusks, hooves' bones)
        buildSkeleton();
        buildTail();
        buildSpines();
        buildHorns();
        buildTusks();
        buildColliders();
        buildBoneSkeleton();
        finalizeParts(parts, ch.mesh, opt.faceted, opt.creaseAngle);
        ch.faceAtlas = paintFaceAtlas(g);
        return std::move(ch);
    }
    buildSkeleton();
    buildTorso();
    buildHead();
    buildNose();
    buildArms();
    buildLegs();
    buildFeet();
    buildTail();
    buildSpines();
    buildCore();
    buildEars();
    buildHorns();
    buildTusks();
    buildColliders();
    buildHair();
    buildFacialHair();
    buildBones();
    applyFur();
    buildFurTufts();
    if (decay_ >= 0.3f || g.species == Species::Undead) {  // torn open onto the real skeleton
        buildBoneSkeleton();
        addDecayHoles();
        buildCavityShells();
    }
    applySkinKind();
    buildFaceDecals();
    finalizeParts(parts, ch.mesh, opt.faceted, opt.creaseAngle);
    ch.faceAtlas = paintFaceAtlas(g);
    return std::move(ch);
}

}  // namespace detail

Character generateCharacter(const Genome& g, const GenOptions& opt) {
    const float decay = std::max(opt.decay, g.body.skeletal ? 1.0f : 0.0f);
    if (decay <= 0.0f) {
        detail::Builder b(g, opt);
        return b.build();
    }
    Genome dead = g;
    detail::applyDecay(dead, decay);
    detail::Builder b(dead, opt);
    return b.build();
}

}  // namespace cg
