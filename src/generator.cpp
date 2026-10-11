// Body generation. The body is sculpted from lofts of superellipse rings along
// the bones (torso, limbs, neck), welded into one smooth low-poly mesh. Every
// loft carries skin weights derived from its parameter along the bone chain.
// Bodies are base meshes: only a modesty layer (underwear), equipment is
// layered on top by the game.
#include <algorithm>
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
    switch (lod) {
        case 0: return {28, 24, 14, 10, 8, 8, 24, 7, false};
        case 1: return {16, 16, 8, 8, 6, 5, 16, 5, true};
        default: return {12, 12, 6, 6, 4, 4, 12, 4, true};
    }
}

namespace {

// Piecewise weights along a chain: keys {t, bone}.
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

}  // namespace

Builder::Builder(const Genome& genome, const GenOptions& o) : g(genome), opt(o), lod(lodSpec(o.lod)) {
    computeMarks();
    const BodyShape& b = g.body;
    const float h = m.h, S = b.shoulderWidth * h, C = b.chestWidth * h, W = b.waistWidth * h, P = b.hipWidth * h;
    const float D = b.chestDepth * h, post = b.posture, lat = 0.10f * h * b.anat.lats * b.anat.lats * (1.0f - 0.6f * g.femininity), nR = b.neckThick * h;
    const float hunch = b.hunch * h;
    // Below the hip line (t < kTorsoBase) these curves only guide the hip and glute
    // volume of the thigh junction; the thighs carry the surface there.
    // Ribcage: men keep the chest wide down to the lats and taper in a straight V;
    // women narrow under the bust into a high, defined waist. Above the shoulder line
    // the trapezius runs as a straight slope from the acromion to the side of the neck.
    const Anatomy& an = b.anat;
    const float fem = g.femininity, trap = 0.035f * h * an.traps;
    const float rib55 = lerp(0.48f, 0.62f, fem), rib66 = lerp(0.80f, 0.95f, fem);
    cHw_ = Curve{{-0.06f, 0.92f * P}, {0.0f, 1.01f * P},  {0.06f, 1.02f * P}, {0.13f, P},
                 {0.23f, lerp(P, W, 0.38f)}, {0.32f, lerp(P, W, 0.75f)}, {0.42f, W}, {0.55f, lerp(W, C, rib55)}, {0.66f, lerp(W, C, rib66)},
                 {0.76f, C + 0.6f * lat}, {0.85f, lerp(C, 0.9f * S, 0.55f) + lat}, {0.905f, std::max(0.92f * S, 0.95f * C)},
                 {0.945f, 0.90f * S}, {0.975f, 0.84f * S}, {1.0f, lerp(0.84f * S, nR, 0.30f) + 0.4f * trap},
                 {1.03f, lerp(0.84f * S, nR, 0.62f) + trap}, {1.06f, lerp(0.84f * S, nR, 0.88f) + 0.5f * trap}, {1.085f, 1.06f * nR}, {1.10f, nR}};
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
                 {0.42f, 0.0f}, {0.66f, 0.02f * h * post}, {0.80f, 0.025f * h * post}, {1.0f, -0.01f * h + 0.06f * hunch},
                 {1.10f, -0.02f * h + 0.10f * hunch}};
    initHead();
}

void Builder::computeMarks() {
    const BodyShape& b = g.body;
    m.H = b.height;
    m.h = m.H / b.headsTall;
    const float h = m.h, mu = b.muscle;
    m.chinY = m.H - h - 0.04f * b.hunch * h;
    m.shoulderY = m.chinY - b.neckLength * h;
    m.crotchY = m.H * b.legRatio;
    if (m.shoulderY - m.crotchY < 0.9f * h) m.crotchY = m.shoulderY - 0.9f * h;
    m.torsoLen = m.shoulderY - m.crotchY;
    m.hipJointY = m.crotchY + 0.11f * m.torsoLen + 0.06f * h;
    m.waistY = m.crotchY + 0.42f * m.torsoLen;
    m.chestY = m.crotchY + 0.62f * m.torsoLen;
    m.ankleY = 0.24f * h * b.footSize;
    m.kneeY = m.ankleY + (m.hipJointY - m.ankleY) * 0.48f;

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
    }

    const float S = b.shoulderWidth * h, C = b.chestWidth * h, armR = b.arm * h;
    const float del = armR * (1.12f + 0.45f * mu);
    for (int pair = 0; pair < std::clamp(b.armPairs, 1, 2); ++pair)
        for (int s = 0; s < 2; ++s) {
            ArmMarks a;
            a.side = s == 0 ? 1.0f : -1.0f;
            a.pair = pair;
            a.radius = armR * (pair ? 0.85f : 1.0f);
            a.deltoid = del * (pair ? 0.85f : 1.0f);
            float angle, fwd;
            if (pair == 0) {
                a.shoulder = {a.side * std::max(S - 0.55f * del, 0.5f * S), m.shoulderY - 0.05f * h - 0.9f * del - 0.10f * mu * h,
                              -0.02f * h + 0.05f * b.hunch * h};
                a.clav = {a.side * 0.10f * h, m.shoulderY - 0.10f * h, -0.02f * h + 0.03f * b.hunch * h};
                angle = 46.0f;
                fwd = 0.0f;
            } else {
                float y = m.chestY - 0.12f * h;
                a.shoulder = {a.side * 0.92f * C, y, 0.02f * h};
                a.clav = {a.side * 0.35f * C, y + 0.04f * h, 0.0f};
                angle = 64.0f;
                fwd = 0.3f;
            }
            float ar = angle * kPi / 180.0f;
            a.dir = normalize(Vec3{a.side * std::sin(ar), -std::cos(ar), fwd});
            float L = std::max(a.shoulder.y - (m.crotchY - 0.05f * h), 0.9f * m.torsoLen) * b.armLength;
            a.elbow = a.shoulder + a.dir * (0.53f * L);
            a.wrist = a.elbow + a.dir * (0.47f * L);
            a.handTip = a.wrist + a.dir * (std::min(0.66f * h, 0.33f * L) * b.handSize);
            m.arms.push_back(a);
        }

    m.neckBase = {0, m.shoulderY - 0.06f * h, -0.02f * h + 0.06f * b.hunch * h};
    m.headOrigin = {0, m.chinY, 0.05f * h + 0.12f * b.hunch * h};
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
        const float pecK = 0.08f * h * an.pecs * (1.0f - saturate(B * 1.6f));
        if (pecK > 0) {
            float tlow = 0.715f + 0.075f * u * u;
            float ft = smoothstep(tlow - 0.012f, tlow + 0.03f, t) * smoothstep(0.995f, 0.86f, t);
            float fu = smoothstep(0.02f, 0.10f, u) * smoothstep(0.98f, 0.72f, u);
            n += pecK * ft * fu * (0.55f + 0.45f * bump((u - 0.45f) / 0.55f));
            n -= pecK * 0.35f * def * bump((t - (tlow - 0.012f)) / 0.02f) * fu;
        }
        // Ribcage: the costal arch, an inverted V from the base of the sternum.
        n += 0.016f * h * (0.4f + an.ribcage) * def * smoothstep(-0.02f, 0.03f, t - (0.67f - 0.17f * u)) *
             bump((t - 0.62f) / 0.12f) * smoothstep(0.85f, 0.5f, u);
        // Serratus on the flank under the armpit.
        n += 0.020f * h * an.serratus * def * smoothstep(0.62f, 0.85f, u) * (bump((t - 0.70f) / 0.035f) + 0.7f * bump((t - 0.635f) / 0.03f));
        // Abdominals: rectus slab, linea alba, three tendinous intersections above the navel.
        const float absK = 0.075f * h * an.abs * def * (1.0f - std::min(1.0f, 2.5f * g.body.belly)) * (1.0f - 0.5f * fem);
        if (absK > 0) {
            float hw = lerp(0.24f, 0.36f, smoothstep(0.15f, 0.60f, t));
            float ft = smoothstep(0.12f, 0.20f, t) * smoothstep(0.69f, 0.64f, t);
            float slab = smoothstep(hw + 0.02f, hw - 0.08f, u) * ft;
            float grooves = (bump((t - 0.445f) / 0.022f) + bump((t - 0.525f) / 0.022f) + bump((t - 0.605f) / 0.022f)) * smoothstep(0.40f, 0.43f, t);
            n += absK * slab * (0.55f - 0.75f * grooves) - absK * 0.9f * bump(u / 0.065f) * ft;
        }
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
            float phi = std::atan2(std::fabs(q.x), q.z - cZc_(clamp(torsoT(q.y), -0.06f, 1.1f)));
            float top = cy + tl * (0.15f + 0.05f * std::sin(phi) + 0.02f * smoothstep(1.6f, 3.0f, phi));
            return std::max(q.y - top, cy + hole(phi) - q.y);
        }, uw});
    } else {
        p.clips.push_back({[=, this](Vec3 q) {
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
    std::vector<float> base{kTorsoBase, 0.17f, 0.23f, 0.29f, 0.35f, 0.40f, 0.445f, 0.485f, 0.525f, 0.565f, 0.605f, 0.645f,
                            0.685f, 0.72f, 0.755f, 0.79f, 0.825f, 0.86f, 0.89f, 0.92f, 0.945f, 0.97f, 0.99f, 1.01f, 1.035f,
                            1.06f, 1.085f, 1.10f};
    auto rings = mergeRings(base, {}, lod.sparseRings);
    const int N = lod.torsoSeg;
    Loft L;
    for (float t : rings) {
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) ring[size_t(j)] = torsoPoint(t, warpTheta(kTau * j / N));
        L.rings.push_back(ring);
        L.t.push_back(t);
    }
    // The neck continues the same surface up into the head (no seam at its base):
    // sternocleidomastoid ridges run from behind the ears down to the sternal notch,
    // with an Adam's apple on men.
    const Anatomy& an = g.body.anat;
    const Vec3 n0{0, m.crotchY + 1.10f * m.torsoLen, cZc_(1.10f)};
    const Vec3 n1 = m.headOrigin + Vec3{0, 0.40f * h, -0.15f * h};  // deep in the skull: the nape enters the occiput
    const Vec3 nd = normalize(n1 - n0), nv = orthoFront(nd), nu = cross(nd, nv);
    const float def = 0.3f + 0.7f * an.definition;
    std::vector<float> neckT = lod.sparseRings ? std::vector<float>{0.5f, 1.0f} : std::vector<float>{0.3f, 0.6f, 0.85f, 1.0f};
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
    tp.cavity = (0.40f + 0.70f * g.body.anat.definition) * (1.0f - 0.35f * g.femininity);
    addUnderwear(tp, true);
    if (g.body.bust > 0.05f)  // the bust is one soft form: no creases across it
        tp.smoothAt = [this](Vec3 p) {
            float t = torsoT(p.y);
            if (t < 0.45f || t > 0.98f || p.z < cZc_(t)) return false;
            float th = std::atan2(p.z - cZc_(t), p.x);
            return bustField(t, th) > 0.0f || bustField(t + 0.03f, th) > 0.0f || bustField(t - 0.03f, th) > 0.0f;
        };
    const uint32_t first = uint32_t(tp.pos.size());
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
        Curve rc{{-0.20f, 0.55f}, {-0.12f, 0.86f}, {-0.05f, 1.0f}, {0.05f, 1.0f}, {0.20f, 0.95f}, {0.35f, 0.88f},
                 {te - 0.05f, 0.79f}, {te, 0.75f}, {te + 0.10f, 0.84f}, {te + 0.25f, 0.74f}, {0.85f, 0.60f}, {1.0f, 0.53f}, {1.05f, 0.53f}};
        auto relief = [&](float beta, float t, float rr) {
            auto win = [&](float c, float w) { return bump(std::remainder(beta - c, kTau) / w); };
            float o = 0;
            // Deltoid: wide over the shoulder, its outline closing to a V at the insertion.
            float dw = lerp(1.7f, 0.45f, smoothstep(0.02f, 0.30f, t));
            float dt = smoothstep(-0.17f, -0.07f, t) * smoothstep(0.33f, 0.12f, t);
            o += rr * 0.24f * (0.4f + an.deltoids) * win(kPi, dw) * dt;
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
            o += rr * 0.20f * (0.35f + an.forearms) * win(2.25f, 0.8f) * bump((t - (te + 0.08f)) / 0.11f);
            o += rr * 0.14f * (0.35f + an.forearms) * win(0.75f, 0.8f) * bump((t - (te + 0.12f)) / 0.12f);
            // Wrist: flat (wider than deep) with the head of the ulna on the back.
            float wr = smoothstep(0.80f, 0.97f, t);
            o -= rr * 0.20f * wr * (win(1.57f, 0.8f) + win(4.71f, 0.8f));
            o += rr * 0.10f * (0.4f + an.wrists) * win(5.5f, 0.45f) * bump((t - 0.96f) / 0.04f);
            // Inner upper arm lies flatter against the body.
            o -= rr * 0.06f * win(0.0f, 0.8f) * bump((t - 0.10f) / 0.18f);
            return o;
        };
        std::vector<float> ts{-0.20f, -0.13f, -0.07f, -0.01f, 0.06f, 0.14f, 0.22f, 0.30f, 0.37f, te - 0.07f, te - 0.025f, te + 0.02f,
                              te + 0.08f, te + 0.16f, te + 0.26f, 0.85f, 0.93f, 1.0f, 1.05f};
        if (lod.sparseRings) ts = mergeRings(ts, {}, true);
        Loft L;
        const Vec3 dirIn = normalize(Vec3{-a.side, 0.12f, 0});
        const int n = lod.limbSeg;
        for (float t : ts) {
            float rr = r * rc(t);
            // Above the joint the loft bends towards the neck so the shoulder cap
            // sits on the trapezius instead of rising above it.
            float kk = smoothstep(0.06f, -0.10f, t);
            Vec3 dd = normalize(lerp(d, -dirIn, kk));
            Vec3 vv = orthoFront(dd), uu = cross(dd, vv);
            Vec3 c = t >= 0 ? a.shoulder + d * (t * Ltot) : a.shoulder + dirIn * (-t * Ltot * 0.85f);
            std::vector<Vec3> ring(static_cast<size_t>(n));
            for (int j = 0; j < n; ++j) {
                float al = kTau * float(j) / float(n);
                Vec3 radial = uu * (rr * std::cos(al)) + vv * (rr * std::sin(al));
                float beta = a.side > 0 ? al : kPi - al;
                ring[size_t(j)] = c + radial + normalize(radial, vv) * relief(beta, t, rr);
            }
            // The top of the cap sinks smoothly into the torso.
            if (t < -0.04f && a.pair == 0)
                for (Vec3& p : ring) p = lerp(p, insideTorso(p, 0.95f), smoothstep(-0.04f, -0.15f, t));
            L.rings.push_back(ring);
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = a.shoulder + dirIn * (0.24f * Ltot * 0.85f);
        L.tStart = -0.26f;
        int clav = rig.root, up = rig.upper, lo = rig.lower, hand = rig.end;
        Part& ap = newPart();
        armGroup_ = ap.id;
        ap.cavity = (0.40f + 0.70f * an.definition) * (1.0f - 0.35f * g.femininity);
        // Normal transfer: where the shoulder cap runs into the torso it takes the
        // torso's normal, so the join reads as one surface (only the silhouette remains).
        if (a.pair == 0)
            ap.normalFn = [this](Vec3 p, Vec3 n) {
                float t = torsoT(p.y);
                if (t < 0.6f || t > 1.08f) return n;
                float th;
                Vec3 sp = torsoSurfaceToward(p, &th);
                float zc = cZc_(t), rp = length(Vec2{p.x, p.z - zc}), rs = length(Vec2{sp.x, sp.z - zc});
                float w = smoothstep(1.10f, 1.0f, rp / std::max(rs, 1e-5f));
                if (w <= 0) return n;
                Vec3 pt = torsoPoint(t + 0.005f, th) - torsoPoint(t - 0.005f, th), pth = torsoPoint(t, th + 0.01f) - torsoPoint(t, th - 0.01f);
                Vec3 tn = normalize(cross(pt, pth), n);
                return normalize(lerp(n, tn, w), n);
            };
        emitLoft(ap, L, [=](float t, float, Vec3) {
            return chainWeights({{-0.18f, clav}, {0.0f, up}, {te - 0.07f, up}, {te + 0.07f, lo}, {0.94f, lo}, {1.04f, hand}}, t);
        }, [&](float, float) { return skin(); });
        buildHand(a, rig);
    }
}

void Builder::buildHand(const ArmMarks& a, const LimbRig& rig) {
    const float Lh = length(a.handTip - a.wrist);
    const Vec3 d = a.dir, v = orthoFront(d), u = cross(d, v);
    const Vec3 palm = u * a.side;  // towards the body in the A-pose
    const float hw = std::max(0.25f * Lh, a.radius * 0.78f) * (1 + 0.12f * g.body.muscle);
    const float th = std::max(0.13f * Lh, a.radius * 0.55f);
    const int hand = rig.end;
    struct K {
        float t, w, th, curl;
    };
    // Palm, knuckle line, then the fingers as one softly curled block.
    const K keys[] = {{-0.05f, 0.62f, 0.85f, 0}, {0.18f, 0.92f, 1.0f, 0}, {0.46f, 1.0f, 1.0f, 0.04f},
                      {0.56f, 0.97f, 0.88f, 0.10f}, {0.74f, 0.92f, 0.80f, 0.30f}, {0.88f, 0.84f, 0.68f, 0.55f}};
    Loft L;
    for (const K& k : keys) {
        Vec3 c = a.wrist + d * (k.t * Lh) + palm * (k.curl * th);
        L.rings.push_back(makeRing(c, u, v, SuperRing{th * k.th, 0, hw * k.w, hw * k.w, 2.6f, 2.6f}, lod.handSeg));
        L.t.push_back(k.t);
    }
    L.poleEnd = true;
    L.endPole = a.wrist + d * (0.95f * Lh) + palm * (0.80f * th);
    L.tEnd = 1;
    Part& hp = newPart();
    hp.id = armGroup_;
    emitLoft(hp, L, [=](float, float, Vec3) { return one(hand); }, [&](float, float) { return skin(); });

    // Thumb.
    Vec3 tb = a.wrist + d * (0.2f * Lh) + v * (0.65f * hw) + palm * (0.3f * th);
    Vec3 td = normalize(d * 0.55f + v * 0.45f + palm * 0.55f);
    Vec3 tv = orthoFront(td), tu = cross(td, tv);
    float tl = 0.42f * Lh, tr = 0.34f * hw;
    Loft T;
    for (float t : {0.0f, 0.5f, 0.85f}) {
        float rr = tr * (1.0f - 0.22f * t);
        T.rings.push_back(makeRing(tb + td * (t * tl), tu, tv, SuperRing{rr, 0, rr, rr}, 6));
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
    const float h = m.h, mu = g.body.muscle;
    const float T = g.body.thigh * h, Cf = g.body.calf * h * (1 + 0.2f * mu);
    // Knee narrower than the calf, slim ankle: the diamond-shaped lower leg.
    const float K = std::max(0.52f * T, 0.80f * Cf), A = 0.44f * Cf;
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
        // Men get a soft rounded bulge at the front of the crotch instead of a notch.
        const float bulge = (1.0f - g.femininity) * h;
        Vec3 crotch = (torsoPoint(0.0f, 0.5f * kPi) + torsoPoint(0.0f, 1.5f * kPi)) * 0.5f;
        crotch.y = m.crotchY - 0.01f * h - 0.03f * bulge;
        crotch.z += 0.05f * bulge;
        const Vec3 front = torsoPoint(0.2f * kTorsoBase, 0.5f * kPi) + Vec3{0, -0.03f * bulge, 0.06f * bulge};
        std::vector<Vec3> pts;
        if (lod.sparseRings) pts = {torsoPoint(0.5f * kTorsoBase, 0.5f * kPi), crotch, torsoPoint(0.5f * kTorsoBase, 1.5f * kPi)};
        else pts = {torsoPoint(0.6f * kTorsoBase, 0.5f * kPi) + Vec3{0, 0, 0.02f * bulge}, front, crotch,
                    torsoPoint(0.2f * kTorsoBase, 1.5f * kPi), torsoPoint(0.6f * kTorsoBase, 1.5f * kPi)};
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
        Curve rc{{-0.16f, 0.62f * Ttop}, {-0.06f, 0.88f * Ttop}, {0.0f, Ttop}, {0.14f, lerp(Ttop, T, 0.55f)}, {0.26f, 0.90f * T},
                 {tk - 0.13f, lerp(T, K, 0.62f)}, {tk - 0.03f, 1.02f * K}, {tk + 0.04f, K}, {tk + 0.10f, 0.93f * Cf},
                 {tk + 0.18f, Cf}, {tk + 0.30f, 0.90f * Cf}, {0.86f, lerp(Cf, A, 0.72f)}, {0.95f, A}, {1.06f, A}};
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
            o += rr * 0.10f * an.quads * def * win(2.15f, 1.0f) * bump((t - 0.26f) / 0.20f);
            o += rr * 0.06f * an.quads * def * win(1.57f, 0.7f) * bump((t - 0.24f) / 0.18f);
            o += rr * 0.16f * an.quads * def * win(0.75f, 0.8f) * bump((t - (tk - 0.075f)) / 0.075f);
            o += rr * 0.10f * an.adductors * win(0.1f, 1.1f) * bump((t - (tc + 0.10f)) / 0.16f);
            o += rr * 0.07f * an.hamstrings * def * win(4.71f, 0.9f) * bump((t - 0.28f) / 0.18f);
            o -= rr * 0.05f * def * win(3.14f, 0.45f) * bump((t - 0.30f) / 0.16f);
            // Knee: patella block, narrow sides, popliteal hollow, patellar tendon.
            o += rr * 0.15f * bone * win(1.57f, 0.6f) * bump((t - (tk - 0.008f)) / 0.042f);
            o -= rr * 0.07f * bone * (win(0.0f, 0.55f) + win(3.14f, 0.55f)) * bump((t - tk) / 0.05f);
            o -= rr * 0.06f * win(4.71f, 0.6f) * bump((t - tk) / 0.05f);
            o += rr * 0.04f * bone * win(1.57f, 0.35f) * bump((t - (tk + 0.04f)) / 0.025f);
            // Calf: medial gastrocnemius head lower and larger, lateral head higher; flat
            // tibia in front; Achilles hollows; medial malleolus higher than the lateral.
            o += rr * 0.12f * an.calves * win(5.35f, 0.8f) * bump((t - (tk + 0.165f)) / 0.11f);
            o += rr * 0.09f * an.calves * win(4.05f, 0.75f) * bump((t - (tk + 0.12f)) / 0.10f);
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

        // Junction rings: interpolate loop -> first thigh ring, keeping the hip/glute volume.
        std::vector<uint32_t> prev = loop;
        const int K = lod.sparseRings ? 1 : 2;
        for (int k = 1; k <= K; ++k) {
            float f = float(k) / float(K + 1);
            std::vector<uint32_t> cur(M);
            for (size_t j = 0; j < M; ++j) {
                Vec3 p = lerp(bp.pos[loop[j]], ring1[j], f);
                if (outer[j] > 0) {
                    Vec3 sp = torsoSurfaceToward(p);
                    float zc = cZc_(clamp(torsoT(p.y), -0.06f, 1.07f));
                    float rp = length(Vec2{p.x, p.z - zc}), rs = length(Vec2{sp.x, sp.z - zc});
                    if (rs > rp) p = lerp(p, sp, outer[j] * smoothstep(-0.05f, 0.04f, torsoT(p.y)));
                }
                float te = (Hj.y - p.y) / std::max(1e-4f, -d1.y * Lt);
                cur[j] = addV(p, weights(te));
            }
            emitStrip(bp, prev, cur, skin());
            prev = cur;
        }
        // Thigh, knee, calf, ankle.
        std::vector<float> ts{t1};
        // Rings on the landmarks only, so flat facets form large, deliberate planes.
        for (float t : {0.19f, 0.28f})
            if (t > t1 + 0.04f && t < tk - 0.17f) ts.push_back(t);
        for (float t : {tk - 0.12f, tk - 0.055f, tk - 0.015f, tk + 0.03f, tk + 0.08f, tk + 0.15f, tk + 0.24f, 0.85f, 0.93f, 0.99f, 1.06f})
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
    const float A = m.ankleY, Lf = m.footLen, Wf = m.footHalfW;
    Curve cw{{0, 0.62f}, {0.10f, 0.76f}, {0.35f, 0.78f}, {0.60f, 0.95f}, {0.80f, 1.05f}, {0.93f, 0.98f}, {1.0f, 0.82f}};
    Curve chh{{0, 0.60f}, {0.08f, 0.90f}, {0.30f, 1.10f}, {0.50f, 0.80f}, {0.72f, 0.46f}, {0.88f, 0.34f}, {1.0f, 0.28f}};
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
                 [&](float, float) { return skin(); });
    }
}

void Builder::buildTail() {
    if (!g.body.tail) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    const float y0 = m.crotchY + 0.22f * m.torsoLen;
    const float z0 = torsoRing(torsoT(y0)).backZ(0) - gluteDepth(0.22f) + 0.02f * h;
    const float len = g.body.tailLength * m.hipJointY * 0.95f;
    const float r0 = 0.11f * h * (1 + 0.3f * g.body.muscle);
    constexpr int kPts = 9;
    std::vector<Vec3> pts;
    Vec3 p{0, y0, z0};
    float step = len / (kPts - 1);
    for (int i = 0; i < kPts; ++i) {
        pts.push_back(p);
        float u = float(i) / (kPts - 1);
        p += normalize(Vec3{0, lerp(-0.8f, 0.3f, u), -1.0f}) * step;
    }
    const int nb = 4;
    int parent = r.hips;
    for (int b = 0; b < nb; ++b) {
        parent = sk.add("tail" + std::to_string(b), parent, pts[size_t(b * (kPts - 1) / nb)]);
        r.tail.push_back(parent);
    }
    Loft L;
    for (int i = 0; i < kPts; ++i) {
        float u = float(i) / (kPts - 1);
        Vec3 d = normalize(i + 1 < kPts ? pts[size_t(i + 1)] - pts[size_t(i)] : pts[size_t(i)] - pts[size_t(i - 1)]);
        Vec3 v = normalize(cross(d, {1, 0, 0}), {0, 1, 0});
        Vec3 uu = cross(d, v);
        float rad = r0 * (1.0f - 0.8f * u);
        L.rings.push_back(makeRing(pts[size_t(i)], uu, v, SuperRing{rad, 0, rad, rad}, lod.limbSeg));
        L.t.push_back(u);
    }
    L.poleStart = true;
    L.startPole = pts[0] + Vec3{0, 0, 0.05f * h};
    L.poleEnd = true;
    L.endPole = pts.back() + normalize(pts.back() - pts[pts.size() - 2]) * (0.6f * r0);
    L.tEnd = 1;
    std::vector<int> bones = r.tail;
    int hips = r.hips;
    Vec3 base = g.palette.skin;
    emitLoft(newPart(), L, [=](float t, float, Vec3) {
        float f = t * nb;
        int i0 = std::clamp(int(f), 0, nb - 1);
        if (i0 == nb - 1) return one(bones[size_t(i0)]);
        if (t < 0.05f) return blend(hips, bones[0], t / 0.05f);
        return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], smoothstep(0.3f, 1.0f, f - i0));
    }, [=](float t, float) { return Mat{base * (1.0f - 0.15f * t), MaterialClass::Skin}; });
    SpringChain sc;
    sc.name = "tail";
    sc.bones = bones;
    sc.tail = pts.back() - sk.bones[size_t(bones.back())].restWorld;
    sc.stiffness = 0.9f;
    sc.drag = 0.35f;
    sc.gravity = 0.2f;
    sc.hitRadius = r0 * 0.6f;
    ch.physics.chains.push_back(sc);
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
}

Character Builder::build() {
    ch.genome = g;
    ch.height = m.H;
    buildSkeleton();
    buildTorso();
    buildHead();
    buildArms();
    buildLegs();
    buildFeet();
    buildTail();
    buildEars();
    buildHorns();
    buildColliders();
    buildHair();
    buildFacialHair();
    buildFaceDecals();
    finalizeParts(parts, ch.mesh, opt.faceted, opt.creaseAngle);
    ch.faceAtlas = paintFaceAtlas(g);
    return std::move(ch);
}

}  // namespace detail

Character generateCharacter(const Genome& g, const GenOptions& opt) {
    detail::Builder b(g, opt);
    return b.build();
}

}  // namespace cg
