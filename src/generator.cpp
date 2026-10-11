// Body generation. The body is "sculpted" from overlapping low-poly primitives:
// lofts of superellipse rings along bones (torso, limbs, neck, head) plus
// ellipsoids (bust, glutes, deltoids, pads). Each loft carries skin weights
// derived from its parameter along the bone chain, so joints bend smoothly.
#include <algorithm>

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
        case 0: return {16, 20, 10, 8, 12, 7, 4, 6, 20, 6, false};
        case 1: return {12, 16, 8, 6, 10, 5, 4, 4, 16, 4, true};
        default: return {8, 12, 6, 4, 8, 4, 3, 3, 12, 3, true};
    }
}

namespace {

// Piecewise-linear weights along a chain: keys {t, bone}.
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

// Cuff / hem: a ring inserted exactly at a colour boundary, optionally flared
// outwards to give clothing a visible lip.
struct Boundary {
    float t;
    float flare;
    int coverSide;  // +1: covered region is t > b, -1: t < b
};

struct RingSample {
    float t;
    float flare;
};

std::vector<RingSample> mergeRings(std::vector<float> base, const std::vector<Boundary>& bounds, bool sparse) {
    if (sparse) {
        std::vector<float> keep;
        for (size_t i = 0; i < base.size(); ++i)
            if (i % 2 == 0 || i + 1 == base.size()) keep.push_back(base[i]);
        base = keep;
    }
    std::vector<RingSample> out;
    for (float t : base) {
        bool near = false;
        for (const Boundary& b : bounds)
            if (std::fabs(t - b.t) < 0.02f || std::fabs(t - (b.t + b.coverSide * 0.018f)) < 0.012f) near = true;
        if (!near) out.push_back({t, 1.0f});
    }
    for (const Boundary& b : bounds) {
        out.push_back({b.t, b.flare});
        if (b.flare > 1.0f) out.push_back({b.t + b.coverSide * 0.018f, b.flare});
    }
    std::sort(out.begin(), out.end(), [](const RingSample& a, const RingSample& b) { return a.t < b.t; });
    return out;
}

Vec3 orthoFront(Vec3 d) { return normalize(Vec3{0, 0, 1} - d * d.z, {1, 0, 0}); }

}  // namespace

Builder::Builder(const Genome& genome, const GenOptions& o) : g(genome), opt(o), lod(lodSpec(o.lod)) {
    computeMarks();
    const BodyShape& b = g.body;
    const float h = m.h, S = b.shoulderWidth * h, C = b.chestWidth * h, W = b.waistWidth * h, P = b.hipWidth * h;
    const float D = b.chestDepth * h, post = b.posture, lat = b.muscle * 0.08f * h, nR = b.neckThick * h;
    cHw_ = Curve{{-0.06f, 0.48f * P}, {0.0f, 0.80f * P},  {0.10f, 0.95f * P}, {0.20f, P},
                 {0.30f, lerp(P, W, 0.55f)}, {0.42f, W}, {0.55f, lerp(W, C, 0.6f)}, {0.66f, 0.95f * C},
                 {0.76f, C}, {0.86f, lerp(C, 0.88f * S, 0.5f) + lat}, {0.94f, std::max(0.78f * S, 0.9f * C)},
                 {1.0f, 0.52f * S}, {1.07f, nR * 1.15f}};
    cDf_ = Curve{{-0.06f, 0.28f * D}, {0.0f, 0.58f * D}, {0.10f, 0.76f * D}, {0.20f, 0.80f * D}, {0.30f, 0.76f * D},
                 {0.42f, 0.72f * D},  {0.55f, 0.80f * D}, {0.66f, 0.88f * D}, {0.76f, 0.95f * D}, {0.86f, 0.92f * D},
                 {0.94f, 0.75f * D},  {1.0f, 0.55f * D},  {1.07f, nR * 1.1f}};
    cDb_ = Curve{{-0.06f, 0.35f * D}, {0.0f, 0.70f * D}, {0.10f, 0.82f * D}, {0.20f, 0.80f * D}, {0.30f, 0.68f * D},
                 {0.42f, 0.62f * D},  {0.55f, 0.70f * D}, {0.66f, 0.80f * D}, {0.76f, 0.86f * D}, {0.86f, 0.88f * D},
                 {0.94f, 0.80f * D},  {1.0f, 0.62f * D},  {1.07f, nR * 1.1f}};
    cZc_ = Curve{{-0.06f, -0.01f * h * post}, {0.10f, -0.035f * h * post}, {0.20f, -0.035f * h * post},
                 {0.42f, 0.0f}, {0.66f, 0.02f * h * post}, {0.80f, 0.025f * h * post}, {1.0f, -0.01f * h},
                 {1.07f, -0.02f * h}};
    // Anime head, normalised to head height (s = 0 chin, 1 crown).
    hW_ = Curve{{0, 0.10f},     {0.06f, 0.18f}, {0.15f, 0.27f}, {0.28f, 0.355f}, {0.42f, 0.39f},
                {0.58f, 0.40f}, {0.74f, 0.39f}, {0.88f, 0.33f}, {0.96f, 0.22f}, {1.0f, 0.04f}};
    hZc_ = Curve{{0, 0.22f},       {0.06f, 0.17f},  {0.15f, 0.10f},  {0.28f, 0.02f}, {0.42f, -0.02f},
                 {0.58f, -0.04f}, {0.74f, -0.05f}, {0.88f, -0.06f}, {1.0f, -0.06f}};
    hDf_ = Curve{{0, 0.05f},     {0.06f, 0.13f}, {0.15f, 0.24f}, {0.28f, 0.36f}, {0.42f, 0.42f},
                 {0.58f, 0.43f}, {0.74f, 0.41f}, {0.88f, 0.33f}, {0.96f, 0.22f}, {1.0f, 0.04f}};
    hDb_ = Curve{{0, 0.05f},     {0.06f, 0.10f}, {0.15f, 0.18f}, {0.28f, 0.30f}, {0.42f, 0.40f},
                 {0.58f, 0.44f}, {0.74f, 0.45f}, {0.88f, 0.38f}, {0.96f, 0.26f}, {1.0f, 0.04f}};
    hNf_ = Curve{{0, 2.2f}, {0.1f, 2.0f}, {0.28f, 2.1f}, {0.42f, 2.4f}, {0.6f, 2.5f}, {0.9f, 2.2f}, {1.0f, 2.0f}};
    hNb_ = Curve{{0, 2.0f}, {0.4f, 2.2f}, {1.0f, 2.1f}};

    const Palette& p = g.palette;
    topColor = p.cloth[0];
    bottomColor = p.cloth[1];
    accent = p.cloth[2];
    bootColor = g.outfit.boots == BootStyle::Thigh ? p.cloth[0] * 0.55f : p.leather;
    gloveColor = g.outfit.gloves == GloveStyle::Long ? p.cloth[0] : p.leather * 0.9f;
}

void Builder::computeMarks() {
    const BodyShape& b = g.body;
    m.H = b.height;
    m.h = m.H / b.headsTall;
    const float h = m.h, mu = b.muscle;
    m.chinY = m.H - h;
    m.shoulderY = m.chinY - b.neckLength * h;
    m.crotchY = m.H * b.legRatio;
    if (m.shoulderY - m.crotchY < 0.9f * h) m.crotchY = m.shoulderY - 0.9f * h;
    m.torsoLen = m.shoulderY - m.crotchY;
    m.hipJointY = m.crotchY + 0.11f * m.torsoLen + 0.06f * h;
    m.waistY = m.crotchY + 0.42f * m.torsoLen;
    m.chestY = m.crotchY + 0.62f * m.torsoLen;
    m.ankleY = 0.26f * h * b.footSize;
    m.kneeY = m.ankleY + (m.hipJointY - m.ankleY) * 0.48f;

    const float P = b.hipWidth * h, thighR = b.thigh * h, calfR = b.calf * h * (1 + 0.2f * mu);
    const float kneeR = std::max(0.55f * thighR, 0.85f * calfR), ankleR = 0.52f * calfR;
    m.footLen = 0.95f * h * b.footSize;
    m.footHalfW = std::max(0.25f * m.footLen, ankleR * 1.55f);
    for (int s = 0; s < 2; ++s) {
        float sd = s == 0 ? 1.0f : -1.0f;
        m.hipJoint[s] = {sd * 0.50f * P, m.hipJointY, -0.01f * h};
        m.knee[s] = {sd * std::max(0.40f * P, kneeR * 1.08f), m.kneeY, 0.02f * h};
        m.ankle[s] = {sd * std::max(0.36f * P, ankleR * 1.5f), m.ankleY, -0.01f * h};
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
                a.shoulder = {a.side * std::max(S - 0.55f * del, 0.5f * S), m.shoulderY - 0.05f * h - 0.9f * del,
                              -0.02f * h};
                a.clav = {a.side * 0.10f * h, m.shoulderY - 0.10f * h, -0.02f * h};
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
            a.handTip = a.wrist + a.dir * (std::min(0.50f * h, 0.25f * L) * b.handSize);
            m.arms.push_back(a);
        }

    m.neckBase = {0, m.shoulderY - 0.06f * h, -0.02f * h};
    m.headOrigin = {0, m.chinY, 0.05f * h};
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

SuperRing Builder::torsoRing(float t) const {
    float belly = g.body.belly * 0.30f * m.h * bump((t - 0.34f) / 0.26f);
    return {cHw_(t), cZc_(t), cDf_(t) + belly, cDb_(t), 2.2f, 2.5f};
}

SuperRing Builder::headRing(float s) const {
    const float h = m.h;
    return {std::max(hW_(s), 0.01f) * h * g.body.headWidth, hZc_(s) * h, std::max(hDf_(s), 0.01f) * h,
            std::max(hDb_(s), 0.01f) * h, hNf_(s), hNb_(s)};
}

Vec3 Builder::headCenter() const { return m.headOrigin + Vec3{0, 0.55f * m.h, -0.03f * m.h}; }

Vec3 Builder::headSurface(float theta, float s, float offset) const {
    SuperRing r = headRing(s);
    Vec2 p = r.point(theta);
    Vec3 q = m.headOrigin + Vec3{p.x, s * m.h, p.y};
    return q + normalize(q - headCenter()) * offset;
}

Vec3 Builder::collide(Vec3 p, float margin) const {
    const float h = m.h;
    for (int iter = 0; iter < 3; ++iter) {
        // Head: superellipse rings, with a sphere for the crown.
        Vec3 l = p - m.headOrigin;
        float s = l.y / h;
        if (s > 0.0f && s < 0.82f) {
            SuperRing r = headRing(s);
            float f = r.field(l.x, l.z);
            float rr = 0.5f * (r.w + 0.5f * (r.dF + r.dB));
            float target = 1.0f + margin / rr;
            if (f < target) {
                float k = target / std::max(f, 1e-4f);
                p.x = m.headOrigin.x + l.x * k;
                p.z = m.headOrigin.z + r.zc + (l.z - r.zc) * k;
            }
        } else if (s >= 0.82f && s < 1.2f) {
            Vec3 dc = p - headCenter();
            float R = 0.455f * h + margin, d = length(dc);
            if (d < R) p = headCenter() + normalize(dc) * R;
        }
        // Neck cylinder.
        if (p.y > m.neckBase.y - 0.1f * h && p.y < m.chinY + 0.2f * h) {
            Vec2 d{p.x, p.z - m.neckBase.z};
            float R = g.body.neckThick * h * 1.1f + margin, dl = length(d);
            if (dl < R) {
                Vec2 n = normalize(d);
                if (dl < 1e-5f) n = {0, -1};
                p.x = n.x * R;
                p.z = m.neckBase.z + n.y * R;
            }
        }
        // Torso rings.
        float t = torsoT(p.y);
        if (t > -0.05f && t < 1.03f) {
            SuperRing r = torsoRing(t);
            r.dB += gluteDepth(t);
            float f = r.field(p.x, p.z);
            float rr = 0.5f * (r.w + 0.5f * (r.dF + r.dB));
            float target = 1.0f + margin / rr;
            if (f < target) {
                float k = target / std::max(f, 1e-4f);
                p.x *= k;
                p.z = r.zc + (p.z - r.zc) * k;
            }
        }
        for (const Collider& c : colliders) {
            Vec3 d = p - c.c;
            Vec3 q{d.x / c.r.x, d.y / c.r.y, d.z / c.r.z};
            float f = length(q), rmin = std::min({c.r.x, c.r.y, c.r.z});
            float target = 1.0f + margin / rmin;
            if (f < target && f > 1e-5f) p = c.c + d * (target / f);
        }
    }
    return p;
}

// ---------------- paint ----------------
Mat Builder::clothAt(Vec3 base, float t, float theta) const {
    const Palette& p = g.palette;
    switch (g.outfit.pattern) {
        case Pattern::Stripes:
            if (int(std::floor(t * 9.0f)) % 2) base = lerp(base, accent, 0.75f);
            break;
        case Pattern::Checker: {
            int a = int(std::floor(t * 5.0f)) + int(std::floor(theta / kTau * 6.0f));
            base = (a & 1) ? p.cloth[0] : p.cloth[1];
            break;
        }
        default: break;
    }
    return {base, MaterialClass::Cloth};
}

Mat Builder::topPaint(float t, float th) const {
    const Outfit& o = g.outfit;
    const float s = std::sin(th), c = std::cos(th);
    switch (o.bottom) {
        case BottomStyle::Pants:
        case BottomStyle::Shorts:
        case BottomStyle::Skirt:
            if (t < 0.40f) return {bottomColor, MaterialClass::Cloth};
            break;
        case BottomStyle::Briefs:
            if (t < (s > 0 ? 0.05f + 0.17f * c * c : 0.13f + 0.08f * c * c)) return {bottomColor, MaterialClass::Cloth};
            break;
        case BottomStyle::Loincloth:
            if (t < 0.10f + 0.06f * c * c) return {g.palette.leather, MaterialClass::Leather};
            break;
        default: break;
    }
    const float front = std::max(0.0f, s);
    switch (o.top) {
        case TopStyle::Tank:
            if (t >= 0.40f && t < 0.95f - 0.08f * front && (t < 0.86f || std::fabs(c) < 0.55f)) return clothAt(topColor, t, th);
            break;
        case TopStyle::Tee:
        case TopStyle::LongSleeve:
            if (t >= 0.40f && t < 0.995f - 0.04f * front) return clothAt(topColor, t, th);
            break;
        case TopStyle::Crop:
            if (t >= 0.62f && t < 0.95f - 0.07f * front) return clothAt(topColor, t, th);
            break;
        case TopStyle::Bandeau:
            if (t >= 0.66f && t < 0.82f) return clothAt(topColor, t, th);
            break;
        case TopStyle::Armor:
            if (t >= 0.40f && t < 0.46f) return {g.palette.leather, MaterialClass::Leather};
            if (t >= 0.46f && t < 0.98f) return {g.palette.metal, MaterialClass::Metal};
            break;
        default: break;
    }
    return skin();
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
        const float bust = g.body.bust, C = g.body.chestWidth * h;
        const float rad = (0.07f + 0.20f * bust) * h;
        for (int s = 0; s < 2; ++s) {
            float sd = s == 0 ? 1.0f : -1.0f;
            float bx = sd * std::max(0.42f * C, rad * 0.98f);
            float by = m.crotchY + 0.73f * m.torsoLen - 0.15f * rad * bust;
            float rz = (0.80f + 0.2f * std::min(bust, 1.2f)) * rad;
            float fz = torsoRing(torsoT(by)).frontZ(bx);
            Vec3 c{bx, by, fz - 0.38f * rz};
            int id = sk.add(s == 0 ? "bust.L" : "bust.R", r.chest, c);
            (s == 0 ? r.bustL : r.bustR) = id;
        }
    }
}

// ---------------- torso ----------------
void Builder::buildTorso() {
    const RigInfo& r = ch.rig;
    const float h = m.h, S = g.body.shoulderWidth * h, C = g.body.chestWidth * h, mu = g.body.muscle;
    std::vector<Boundary> bounds{{0.40f, 1.0f, -1}};
    switch (g.outfit.top) {
        case TopStyle::Crop: bounds.push_back({0.62f, 1.03f, 1}); break;
        case TopStyle::Bandeau: bounds.push_back({0.66f, 1.0f, 1}); bounds.push_back({0.82f, 1.0f, -1}); break;
        case TopStyle::Armor: bounds.push_back({0.46f, 1.0f, 1}); break;
        default: break;
    }
    std::vector<float> base{-0.06f, 0.0f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.45f, 0.50f, 0.55f,
                            0.60f, 0.68f, 0.74f, 0.80f, 0.86f, 0.91f, 0.95f, 0.98f, 1.01f, 1.04f};
    auto rings = mergeRings(base, bounds, lod.sparseRings);
    const int N = lod.torsoSeg;
    Loft L;
    for (const RingSample& rs : rings) {
        float t = rs.t;
        SuperRing sr = torsoRing(t);
        sr.w *= rs.flare;
        sr.dF *= rs.flare;
        sr.dB *= rs.flare;
        float y = m.crotchY + t * m.torsoLen;
        std::vector<Vec3> ring(N);
        for (int j = 0; j < N; ++j) {
            float th = kTau * j / N;
            Vec2 q = sr.point(th);
            if (std::sin(th) > 0) {  // pectorals: two soft lobes
                float lobe = bump((std::fabs(q.x) / C - 0.45f) / 0.5f);
                q.y += mu * 0.06f * h * bump((t - 0.80f) / 0.11f) * lobe * std::sin(th);
            } else {  // glutes: two lobes with a shallow cleft, plus a hint of side mass
                float lobe = 0.62f + 0.38f * bump((std::fabs(q.x) / sr.w - 0.42f) / 0.58f);
                q.y -= gluteDepth(t) * lobe * std::pow(-std::sin(th), 0.7f);
            }
            ring[j] = {q.x, y, q.y};
        }
        L.rings.push_back(ring);
        L.t.push_back(t);
    }
    L.poleStart = true;
    L.startPole = {0, m.crotchY - 0.05f * m.torsoLen, cZc_(-0.06f)};
    L.tStart = -0.09f;
    L.poleEnd = true;
    L.endPole = {0, m.shoulderY + 0.07f * m.torsoLen, cZc_(1.07f)};
    L.tEnd = 1.07f;

    auto wf = [&, S](float t, float, Vec3 p) {
        SkinW w = chainWeights({{0.12f, r.hips}, {0.40f, r.spine}, {0.66f, r.chest}, {1.0f, r.chest}, {1.10f, r.neck}}, t);
        float cl = 0.5f * smoothstep(0.86f, 1.0f, t) * smoothstep(0.30f * S, 0.75f * S, std::fabs(p.x));
        if (cl > 0 && !r.arms.empty()) {
            for (float& x : w.w) x *= (1 - cl);
            w.add(r.arms[p.x > 0 ? 0 : 1].root, cl);
        }
        return w;
    };
    Part& tp = newPart();
    bodyGroup_ = tp.id;
    emitLoft(tp, L, wf, [&](float t, float th) { return topPaint(t, th); });
}

void Builder::buildBust() {
    const RigInfo& r = ch.rig;
    if (r.bustL < 0) return;
    const float h = m.h, bust = g.body.bust;
    const float rad = (0.07f + 0.20f * bust) * h;
    Mat mat = skin();
    switch (g.outfit.top) {
        case TopStyle::None: break;
        case TopStyle::Armor: mat = {g.palette.metal, MaterialClass::Metal}; break;
        default: mat = clothAt(topColor, 0.74f, kPi / 2); break;
    }
    for (int s = 0; s < 2; ++s) {
        float sd = s == 0 ? 1.0f : -1.0f;
        int bone = s == 0 ? r.bustL : r.bustR;
        Ellipsoid e;
        e.center = ch.skeleton.bones[size_t(bone)].restWorld;
        // Poles point up/down so the faceted front stays round, not pointy.
        const Vec3 fwdB = normalize(Vec3{sd * 0.25f, -0.10f, 1.0f});
        e.axis = normalize(Vec3{0, 1, 0.15f});
        e.up = fwdB;
        e.rAxis = rad * 0.95f;
        e.rU = rad;
        e.rV = (0.80f + 0.2f * std::min(bust, 1.2f)) * rad;
        e.lat = lod.ellLat;
        e.lon = lod.ellLon;
        // Front half follows the bust bone; the base stays glued to the chest.
        Vec3 c = e.center, ax = fwdB;
        int chest = r.chest;
        float rz = e.rV;
        Part& bpart = newPart();
        if (g.outfit.top == TopStyle::Bandeau || g.outfit.top == TopStyle::Crop || g.outfit.top == TopStyle::Tank)
            bpart.paintAt = [this, c](Vec3 q) { return paintTorsoAt(Vec3{q.x, std::clamp(q.y, c.y - 0.1f * m.h, c.y + 0.1f * m.h), q.z}); };
        emitEllipsoid(bpart, e, [=](float, float, Vec3 p) {
            float f = dot(p - c, ax) / rz;
            return blend(chest, bone, smoothstep(-0.9f, 0.1f, f));
        }, [=](float, float) { return mat; });
        colliders.push_back({c + ax * (0.2f * rz), {rad * 1.05f, rad, rz * 1.1f}});
    }
}

float Builder::gluteDepth(float t) const {
    return (std::min(g.body.butt, 1.6f) * 0.26f + 0.03f) * m.h * bump((t - 0.06f) / 0.20f);
}

Mat Builder::paintTorsoAt(Vec3 p) const {
    float t = torsoT(p.y);
    float zc = cZc_(clamp(t, -0.06f, 1.07f));
    float th = std::atan2(p.z - zc, p.x);
    if (th < 0) th += kTau;
    return topPaint(t, th);
}

void Builder::buildNeck() {
    const RigInfo& r = ch.rig;
    const float h = m.h, nR = g.body.neckThick * h;
    Vec3 a{0, m.shoulderY - 0.15f * h, -0.02f * h}, b = m.headOrigin + Vec3{0, 0.24f * h, -0.12f * h};
    Vec3 d = normalize(b - a), v = orthoFront(d), u = cross(d, v);
    Loft L;
    for (float t : {0.0f, 0.3f, 0.6f, 0.85f, 1.0f}) {
        float rr = nR * (1.35f - 0.35f * smoothstep(0.0f, 0.45f, t));
        L.rings.push_back(makeRing(lerp(a, b, t), u, v, SuperRing{rr, 0, rr * 0.95f, rr * 0.95f}, lod.limbSeg));
        L.t.push_back(t);
    }
    emitLoft(newPart(), L, [&](float t, float, Vec3) {
        return chainWeights({{0.0f, r.chest}, {0.25f, r.neck}, {0.7f, r.neck}, {1.0f, r.head}}, t);
    }, [&](float, float) { return skin(); });
}

void Builder::buildHead() {
    const int head = ch.rig.head;
    const float h = m.h;
    Loft L;
    const float ss[] = {0.0f, 0.05f, 0.11f, 0.18f, 0.25f, 0.32f, 0.39f, 0.46f, 0.53f, 0.60f, 0.68f, 0.76f, 0.84f, 0.91f, 0.96f};
    int i = 0;
    for (float s : ss) {
        if (lod.sparseRings && (i++ % 2) && s < 0.9f) continue;
        L.rings.push_back(makeRing(m.headOrigin + Vec3{0, s * h, 0}, {1, 0, 0}, {0, 0, 1}, headRing(s), lod.headSeg));
        L.t.push_back(s);
    }
    L.poleStart = true;
    L.startPole = m.headOrigin + Vec3{0, -0.01f * h, hZc_(0) * h};
    L.tStart = 0;
    L.poleEnd = true;
    L.endPole = m.headOrigin + Vec3{0, h, hZc_(1.0f) * h};
    L.tEnd = 1;
    Part& hp = newPart();
    headPartId_ = hp.id;
    // Anime trick: bend the normals of the face towards +Z so the cel shadow
    // never slices across the 2D face; the cranium keeps its real normals.
    const Vec3 o = m.headOrigin;
    hp.normalFn = [=](Vec3 p, Vec3 n) {
        float s = (p.y - o.y) / h;
        float front = smoothstep(-0.05f * h, 0.25f * h, p.z - o.z) * bump((s - 0.4f) / 0.55f);
        return normalize(lerp(n, normalize(Vec3{n.x * 0.35f, n.y * 0.5f + 0.15f, 1.0f}), 0.85f * front));
    };
    emitLoft(hp, L, [=](float, float, Vec3) { return one(head); }, [&](float, float) { return skin(); });
}

// The 2D face lives on two decal patches that hug the front of the head.
// Their UVs point at the Neutral cells; expressions are UV offsets.
void Builder::buildFaceDecals() {
    const int head = ch.rig.head;
    const float h = m.h, eps = 0.004f * h;
    auto patch = [&](SubmeshKind kind, float x0, float x1, float s0, float s1, int nx, int ny, int cellX, int cellY,
                     int cellW, int cellH) {
        Part& p = newPart(false, kind);
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                float fu = float(i) / nx, fv = float(j) / ny;
                float xf = lerp(x0, x1, fu), s = lerp(s0, s1, fv);
                SuperRing r = headRing(s);
                float x = xf * h * g.body.headWidth;
                float z = r.frontZ(std::min(std::fabs(x), r.w * 0.995f)) + eps;
                p.pos.push_back(m.headOrigin + Vec3{x, s * h, z});
                p.w.push_back(one(head));
                // Half-texel inset keeps bilinear filtering inside the cell.
                float u = (cellX + 0.5f + fu * (cellW - 1)) / FaceLayout::kAtlasW;
                float v = (cellY + 0.5f + (1 - fv) * (cellH - 1)) / FaceLayout::kAtlasH;
                p.uv.push_back({u, v});
            }
        Mat white{{1, 1, 1}, MaterialClass::Skin};
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                uint32_t a = uint32_t(j * (nx + 1) + i), b = a + 1, c = a + uint32_t(nx + 1) + 1, d = a + uint32_t(nx + 1);
                p.tris.push_back({a, b, c, white});
                p.tris.push_back({a, c, d, white});
            }
        return parts.size() - 1;
    };
    auto ec = FaceLayout::eyeCell(EyeExpr::Neutral);
    auto mc = FaceLayout::mouthCell(MouthExpr::Neutral);
    int nx = lod.headSeg / 2, ny = std::max(3, lod.headSeg / 4);
    size_t eyes = patch(SubmeshKind::FaceEyes, FaceLayout::kEyeX0, FaceLayout::kEyeX1, FaceLayout::kEyeS0,
                       FaceLayout::kEyeS1, nx, ny, ec[0], ec[1], FaceLayout::kEyeCellW, FaceLayout::kEyeCellH);
    size_t mouth = patch(SubmeshKind::FaceMouth, FaceLayout::kMouthX0, FaceLayout::kMouthX1, FaceLayout::kMouthS0,
                        FaceLayout::kMouthS1, std::max(3, nx / 2), 3, mc[0], mc[1], FaceLayout::kMouthCellW,
                        FaceLayout::kMouthCellH);
    // Decals share the head's outline id so they never get ink lines.
    parts[eyes].id = parts[mouth].id = headPartId_;
}

// ---------------- arms ----------------
void Builder::buildArms() {
    const Outfit& o = g.outfit;
    const float mu = g.body.muscle;
    for (size_t k = 0; k < m.arms.size(); ++k) {
        const ArmMarks& a = m.arms[k];
        const LimbRig& rig = ch.rig.arms[k];
        const float upperLen = length(a.elbow - a.shoulder), foreLen = length(a.wrist - a.elbow);
        const float Ltot = upperLen + foreLen, te = upperLen / Ltot;
        const Vec3 d = a.dir, v = orthoFront(d), u = cross(d, v);
        const float r = a.radius;
        Curve rc{{-0.12f, 0.80f}, {0.0f, 1.0f},  {0.22f, 1.0f + 0.30f * mu}, {0.40f, 0.92f},
                 {te - 0.04f, 0.78f}, {te, 0.74f}, {te + 0.12f, 0.92f + 0.2f * mu}, {0.80f, 0.70f},
                 {1.0f, 0.56f}, {1.05f, 0.55f}};
        std::vector<Boundary> bounds;
        float sleeveEnd = -1, gloveStart = 2;
        if (o.top == TopStyle::Tee) sleeveEnd = 0.32f;
        if (o.top == TopStyle::LongSleeve) sleeveEnd = 0.95f;
        if (o.top == TopStyle::Armor) sleeveEnd = 0.22f;
        if (o.gloves == GloveStyle::Short) gloveStart = 0.88f;
        if (o.gloves == GloveStyle::Gauntlet) gloveStart = 0.58f;
        if (o.gloves == GloveStyle::Long) gloveStart = 0.45f;
        if (sleeveEnd > 0 && sleeveEnd < gloveStart) bounds.push_back({sleeveEnd, 1.08f, -1});
        if (gloveStart < 1.5f) bounds.push_back({gloveStart, o.gloves == GloveStyle::Gauntlet ? 1.16f : 1.05f, 1});
        std::vector<float> base{-0.12f, 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, te - 0.06f, te, te + 0.06f, te + 0.15f, 0.78f, 0.9f, 1.0f, 1.05f};
        auto rings = mergeRings(base, bounds, lod.sparseRings);
        Loft L;
        for (const RingSample& rs : rings) {
            float t = rs.t;
            float rr = r * rc(t) * rs.flare;
            if (o.gloves == GloveStyle::Gauntlet && t > gloveStart) rr *= 1.12f;
            float flat = t > te ? 0.86f : 1.0f;
            float bi = 0.08f * rr * mu * bump((t - 0.22f) / 0.2f);
            SuperRing sr{rr, bi, rr * flat, rr * flat, 2.0f, 2.0f};
            L.rings.push_back(makeRing(a.shoulder + d * (t * Ltot), u, v, sr, lod.limbSeg));
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = a.shoulder - d * (0.16f * Ltot);
        L.tStart = -0.16f;
        auto paint = [&, te, sleeveEnd, gloveStart](float t, float) -> Mat {
            if (t > gloveStart) {
                if (o.gloves == GloveStyle::Gauntlet)
                    return t < gloveStart + 0.018f ? Mat{accent, MaterialClass::Cloth} : Mat{g.palette.metal, MaterialClass::Metal};
                return {gloveColor, o.gloves == GloveStyle::Long ? MaterialClass::Cloth : MaterialClass::Leather};
            }
            if (t < sleeveEnd) {
                if (o.top == TopStyle::Armor) return {g.palette.leather, MaterialClass::Leather};
                if (t > sleeveEnd - 0.018f) return {accent, MaterialClass::Cloth};
                return clothAt(topColor, t, 0);
            }
            (void)te;
            return skin();
        };
        int clav = rig.root, up = rig.upper, lo = rig.lower, hand = rig.end;
        Part& ap = newPart();
        armGroup_ = ap.id;
        emitLoft(ap, L, [=](float t, float, Vec3) {
            return chainWeights({{-0.12f, clav}, {0.04f, up}, {te - 0.07f, up}, {te + 0.07f, lo}, {0.94f, lo}, {1.04f, hand}}, t);
        }, paint);

        // Deltoid cap.
        Ellipsoid e;
        e.center = a.shoulder + Vec3{-a.side * 0.15f * a.deltoid, 0.05f * a.deltoid, 0};
        e.axis = d;
        e.up = {0, 0, 1};
        e.rAxis = a.deltoid * 1.05f;
        e.rU = e.rV = a.deltoid * 0.95f;
        e.lat = std::max(4, lod.ellLat - 1);
        e.lon = lod.limbSeg;
        Mat dm = skin();
        if (o.top == TopStyle::Tee || o.top == TopStyle::LongSleeve) dm = clothAt(topColor, 0.1f, 0);
        if (o.top == TopStyle::Armor) dm = {g.palette.leather, MaterialClass::Leather};
        Vec3 sc = a.shoulder;
        float del = a.deltoid;
        Part& dp = newPart();
        dp.id = armGroup_;
        emitEllipsoid(dp, e, [=](float, float, Vec3 p) {
            float q = dot(p - sc, d) / del;
            return blend(up, clav, 0.35f * smoothstep(0.0f, -1.0f, q));
        }, [=](float, float) { return dm; });
        colliders.push_back({e.center, {a.deltoid, a.deltoid, a.deltoid}});
        buildHand(a, rig);
    }
}

void Builder::buildHand(const ArmMarks& a, const LimbRig& rig) {
    const Outfit& o = g.outfit;
    const float Lh = length(a.handTip - a.wrist);
    const Vec3 d = a.dir, v = orthoFront(d), u = cross(d, v);
    const Vec3 palm = u * a.side;  // towards the body in the A-pose
    const float hw = std::max(0.27f * Lh, a.radius * 0.80f) * (1 + 0.15f * g.body.muscle);
    const float th = std::max(0.15f * Lh, a.radius * 0.60f);
    Mat mat = skin();
    if (o.gloves == GloveStyle::Short) mat = {gloveColor, MaterialClass::Leather};
    if (o.gloves == GloveStyle::Long) mat = {gloveColor, MaterialClass::Cloth};
    if (o.gloves == GloveStyle::Gauntlet) mat = {g.palette.metal, MaterialClass::Metal};
    const int hand = rig.end;
    struct K {
        float t, w, th, curl;
    };
    // Chunky mitten: wide palm, knuckle block, blunt curled fingers.
    const K keys[] = {{-0.05f, 0.62f, 0.85f, 0}, {0.18f, 0.92f, 1.0f, 0}, {0.48f, 1.0f, 1.0f, 0.05f},
                      {0.70f, 0.96f, 0.92f, 0.28f}, {0.86f, 0.86f, 0.78f, 0.55f}};
    Loft L;
    for (const K& k : keys) {
        Vec3 c = a.wrist + d * (k.t * Lh) + palm * (k.curl * th);
        // Ring axes: u carries the thickness (palm normal), v the width.
        L.rings.push_back(makeRing(c, u, v, SuperRing{th * k.th, 0, hw * k.w, hw * k.w, 3.0f, 3.0f}, lod.handSeg));
        L.t.push_back(k.t);
    }
    L.poleEnd = true;
    L.endPole = a.wrist + d * (0.93f * Lh) + palm * (0.75f * th);
    L.tEnd = 1;
    Part& hp = newPart();
    hp.id = armGroup_;
    emitLoft(hp, L, [=](float, float, Vec3) { return one(hand); }, [=](float, float) { return mat; });

    // Thumb.
    Vec3 tb = a.wrist + d * (0.2f * Lh) + v * (0.65f * hw) + palm * (0.3f * th);
    Vec3 td = normalize(d * 0.55f + v * 0.45f + palm * 0.55f);
    Vec3 tv = orthoFront(td), tu = cross(td, tv);
    float tl = 0.40f * Lh, tr = 0.36f * hw;
    Loft T;
    for (float t : {0.0f, 0.5f, 0.85f}) {
        float rr = tr * (1.0f - 0.25f * t);
        T.rings.push_back(makeRing(tb + td * (t * tl), tu, tv, SuperRing{rr, 0, rr, rr}, 5));
        T.t.push_back(t);
    }
    T.poleEnd = true;
    T.endPole = tb + td * tl;
    Part& tp = newPart();
    tp.id = armGroup_;
    emitLoft(tp, T, [=](float, float, Vec3) { return one(hand); }, [=](float, float) { return mat; });
}

// ---------------- legs & feet ----------------
void Builder::buildLegs() {
    const Outfit& o = g.outfit;
    const float h = m.h, mu = g.body.muscle;
    const float T = g.body.thigh * h, Cf = g.body.calf * h * (1 + 0.2f * mu);
    const float K = std::max(0.55f * T, 0.85f * Cf), A = 0.52f * Cf;
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const Vec3 Hj = m.hipJoint[s], Kn = m.knee[s], An = m.ankle[s];
        const float l1 = length(Kn - Hj), l2 = length(An - Kn), Lt = l1 + l2, tk = l1 / Lt;
        const Vec3 d1 = (Kn - Hj) / l1, d2 = (An - Kn) / l2;
        auto path = [&](float t) { return t < tk ? Hj + d1 * (t * Lt) : Kn + d2 * ((t - tk) * Lt); };
        const float Ttop = std::max(T * 1.02f, 0.47f * g.body.hipWidth * h);
        Curve rc{{-0.16f, 0.62f * Ttop}, {-0.06f, 0.88f * Ttop}, {0.0f, Ttop}, {0.14f, lerp(Ttop, T, 0.6f)}, {0.28f, 0.92f * T},
                 {tk - 0.12f, lerp(T, K, 0.7f)}, {tk, K}, {tk + 0.06f, 0.95f * Cf}, {tk + 0.17f, Cf},
                 {tk + 0.32f * (1 - tk), 0.92f * Cf}, {0.86f, lerp(Cf, A, 0.7f)}, {0.95f, A}, {1.06f, A}};
        float bootStart = 2, pantsEnd = -1;
        if (o.boots == BootStyle::Ankle) bootStart = 0.84f;
        if (o.boots == BootStyle::Knee) bootStart = tk - 0.03f;
        if (o.boots == BootStyle::Thigh) bootStart = 0.24f;
        if (o.bottom == BottomStyle::Pants) pantsEnd = 0.97f;
        if (o.bottom == BottomStyle::Shorts) pantsEnd = 0.30f;
        std::vector<Boundary> bounds;
        if (pantsEnd > 0 && pantsEnd < bootStart) bounds.push_back({pantsEnd, 1.06f, -1});
        if (bootStart < 1.5f) bounds.push_back({bootStart, o.boots == BootStyle::Thigh ? 1.06f : 1.28f, 1});
        std::vector<float> base{-0.16f, -0.08f, 0.0f, 0.08f, 0.16f, 0.24f, 0.32f, 0.40f, tk - 0.06f, tk, tk + 0.06f,
                                tk + 0.14f, tk + 0.24f, 0.84f, 0.92f, 1.0f, 1.06f};
        auto rings = mergeRings(base, bounds, lod.sparseRings);
        Loft L;
        for (const RingSample& rs : rings) {
            float t = rs.t;
            Vec3 dd = normalize(lerp(d1, d2, smoothstep(tk - 0.06f, tk + 0.06f, t)));
            Vec3 v = orthoFront(dd), u = cross(dd, v);
            float rr = rc(t) * rs.flare;
            if (t > bootStart && o.boots != BootStyle::Thigh) rr = std::max(rr * 1.16f, A * 1.55f);  // sturdy boot shaft
            float calf = bump((t - (tk + 0.17f)) / 0.22f);
            float gl = std::min(g.body.butt, 1.6f) * 0.05f * h * bump((t + 0.10f) / 0.14f);
            SuperRing sr{rr, -0.10f * Cf * calf, rr * 0.96f, rr * (1.0f + 0.12f * calf) + gl, 2.0f, 2.0f};
            L.rings.push_back(makeRing(path(t), u, v, sr, lod.limbSeg));
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = Hj + Vec3{0, 0.20f * Lt, 0};
        L.tStart = -0.20f;
        auto paint = [&, bootStart, pantsEnd](float t, float) -> Mat {
            if (t > bootStart) {
                if (o.boots != BootStyle::Thigh && t < bootStart + 0.018f) return {lerp(bootColor, Vec3{0.85f, 0.8f, 0.72f}, 0.55f), MaterialClass::Leather};  // folded cuff
                return {bootColor, o.boots == BootStyle::Thigh ? MaterialClass::Cloth : MaterialClass::Leather};
            }
            if (t < 0.02f && o.bottom == BottomStyle::Skirt) return {bottomColor, MaterialClass::Cloth};
            if (t < pantsEnd) {
                if (o.bottom == BottomStyle::Shorts && t > pantsEnd - 0.018f) return {accent, MaterialClass::Cloth};
                return {bottomColor, MaterialClass::Cloth};
            }
            return skin();
        };
        int hips = ch.rig.hips, th = rig.upper, sh = rig.lower, ft = rig.end;
        Part& lp = newPart();
        lp.id = bodyGroup_;
        emitLoft(lp, L, [=](float t, float, Vec3) {
            return chainWeights({{-0.10f, hips}, {0.08f, th}, {tk - 0.07f, th}, {tk + 0.07f, sh}, {0.95f, sh}, {1.05f, ft}}, t);
        }, paint);
    }
}

void Builder::buildFeet() {
    const float A = m.ankleY, Lf = m.footLen, Wf = m.footHalfW;
    const bool barefoot = g.archetype == Archetype::Monster && g.outfit.bottom == BottomStyle::Loincloth;
    Mat upper = barefoot ? skin() : Mat{bootColor, MaterialClass::Leather};
    if (g.outfit.boots == BootStyle::Shoe && !barefoot) upper = {g.palette.leather, MaterialClass::Leather};
    Mat sole = barefoot ? Mat{g.palette.skin * 0.85f, MaterialClass::Skin} : Mat{{0.16f, 0.13f, 0.12f}, MaterialClass::Dark};
    Curve cw{{0, 0.75f}, {0.08f, 0.9f}, {0.3f, 0.95f}, {0.55f, 1.0f}, {0.75f, 1.05f}, {0.92f, 1.0f}, {1.0f, 0.9f}};
    Curve ch_{{0, 0.75f}, {0.08f, 1.05f}, {0.3f, 1.45f}, {0.5f, 1.05f}, {0.75f, 0.70f}, {0.92f, 0.60f}, {1.0f, 0.5f}};
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const float sd = s == 0 ? 1.0f : -1.0f;
        const Vec3 an = m.ankle[s];
        const float heelZ = an.z - 0.22f * Lf;
        const Quat turn = Quat::axisAngle({0, 1, 0}, sd * 0.14f);
        auto place = [&](Vec3 p) { return Vec3{an.x, 0, an.z} + rotate(turn, p - Vec3{an.x, 0, an.z}); };
        Loft L;
        for (float t : {0.0f, 0.06f, 0.15f, 0.3f, 0.45f, 0.6f, 0.72f, 0.84f, 0.94f}) {
            float top = ch_(t) * A, bot = 0.10f * A;
            SuperRing sr{cw(t) * Wf, bot, top - bot, bot * 0.9f, 2.2f, 6.0f};
            auto ring = makeRing({an.x, 0.004f, heelZ + t * Lf}, {-1, 0, 0}, {0, 1, 0}, sr, lod.limbSeg);
            for (Vec3& p : ring) p = place(p);
            L.rings.push_back(ring);
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = place({an.x, 0.3f * A, heelZ - 0.03f * Lf});
        L.tStart = -0.03f;
        L.poleEnd = true;
        L.endPole = place({an.x, 0.28f * A, heelZ + 0.985f * Lf});
        L.tEnd = 1;
        int foot = rig.end, toe = rig.tip;
        emitLoft(newPart(), L, [=](float t, float, Vec3) { return blend(foot, toe, smoothstep(0.6f, 0.78f, t)); },
                 [=](float, float th) { return std::sin(th) < -0.6f ? sole : upper; });
    }
}

// ---------------- clothing extras ----------------
void Builder::buildBelt() {
    const Outfit& o = g.outfit;
    if (!o.belt || o.bottom == BottomStyle::Briefs) return;
    const RigInfo& r = ch.rig;
    const float h = m.h, t0 = 0.38f, t1 = 0.43f;
    const float out = 1.0f + 0.03f, pad = 0.012f * h;
    Loft L;
    auto ring = [&](float t, float k, float extra) {
        SuperRing sr = torsoRing(t);
        sr.w = sr.w * k + extra;
        sr.dF = sr.dF * k + extra;
        sr.dB = sr.dB * k + extra;
        auto rr = makeRing({0, m.crotchY + t * m.torsoLen, 0}, {1, 0, 0}, {0, 0, 1}, sr, lod.torsoSeg);
        L.rings.push_back(rr);
        L.t.push_back(t);
    };
    ring(t0, 1.0f, 0);
    ring(t0, out, pad);
    ring(t1, out, pad);
    ring(t1, 1.0f, 0);
    Mat leather{g.palette.leather * 0.8f, MaterialClass::Leather}, buckle{g.palette.metal, MaterialClass::Metal};
    emitLoft(newPart(), L, [&](float t, float, Vec3) {
        return chainWeights({{0.12f, r.hips}, {0.40f, r.spine}}, t);
    }, [=](float, float th) { return std::fabs(th - kPi / 2) < 0.35f ? buckle : leather; });
}

void Builder::buildSkirt() {
    if (g.outfit.bottom != BottomStyle::Skirt) return;
    const RigInfo& r = ch.rig;
    const float h = m.h;
    const float yTop = m.crotchY + 0.40f * m.torsoLen;
    const float yBot = lerp(m.kneeY, m.crotchY, 0.40f);
    const float legSpan = std::fabs(m.hipJoint[0].x) + std::max(g.body.thigh * h, 0.47f * g.body.hipWidth * h);
    const int N = lod.torsoSeg;
    Loft L;
    const float ts[] = {0.0f, 0.12f, 0.25f, 0.45f, 0.72f, 1.0f};
    const float thighR = std::max(g.body.thigh * h, 0.47f * g.body.hipWidth * h) * 1.32f;
    SuperRing last;
    for (float t : ts) {
        float y = lerp(yTop, yBot, t);
        float tt = std::max(torsoT(y), -0.06f);
        SuperRing tr = torsoRing(tt);
        float flare = 1.0f + 0.25f * t * t;
        float drape = smoothstep(0.0f, 0.6f, t);  // below the hips the skirt hangs, it does not follow the body
        SuperRing sr;
        sr.zc = tr.zc;
        // Thighs start above the crotch: cover them by height, not by skirt parameter.
        float legCover = smoothstep(m.hipJointY + 0.25f * h, m.hipJointY, y);
        sr.w = std::max({tr.w * 1.07f, lerp(tr.w * 1.07f, legSpan * 1.15f, drape), legSpan * 1.12f * legCover}) * flare;
        sr.dF = std::max(tr.dF * 1.08f, thighR * std::max(drape, legCover)) * flare;
        sr.dB = std::max((tr.dB + gluteDepth(std::max(tt, 0.07f) * drape + tt * (1 - drape))) * 1.08f, thighR * std::max(drape, legCover)) * flare;
        if (t > 0.3f) {  // keep the drape monotonic: never narrower than the ring above
            sr.w = std::max(sr.w, last.w);
            sr.dF = std::max(sr.dF, last.dF);
            sr.dB = std::max(sr.dB, last.dB);
        }
        last = sr;
        L.rings.push_back(makeRing({0, y, 0}, {1, 0, 0}, {0, 0, 1}, sr, N));
        L.t.push_back(t);
    }
    // Reverse ring order so the loft runs downward but faces still point out:
    // a downward loft needs u = cross(-Y, v), i.e. mirrored rings.
    for (auto& ring : L.rings) std::reverse(ring.begin() + 1, ring.end());
    const Vec3 base = bottomColor;
    const Vec3 acc = accent;
    int hips = r.hips, tl = r.legs[0].upper, tr = r.legs[1].upper;
    emitLoft(newPart(), L, [=](float t, float, Vec3 p) {
        float legW = 0.55f * t;
        float side = clamp(p.x / std::max(legSpan, 1e-3f), -1.0f, 1.0f);
        float wl = legW * saturate(0.5f + side * 0.8f), wr = legW * saturate(0.5f - side * 0.8f);
        SkinW w;
        w.add(hips, 1 - wl - wr);
        w.add(tl, wl);
        w.add(tr, wr);
        w.normalize();
        return w;
    }, [=](float t, float th) -> Mat {
        if (t > 0.84f) return {acc, MaterialClass::Cloth};
        int pleat = int(std::floor(th / kTau * N));
        return {(pleat & 1) ? base * 0.86f : base, MaterialClass::Cloth};
    });
}

void Builder::buildLoincloth() {
    if (g.outfit.bottom != BottomStyle::Loincloth) return;
    const RigInfo& r = ch.rig;
    const float h = m.h;
    const float yTop = m.crotchY + 0.38f * m.torsoLen, yBot = lerp(m.kneeY, m.crotchY, 0.25f);
    const float wHalf = std::max(0.3f * g.body.hipWidth * h, 0.12f * h);
    Mat cloth{lerp(g.palette.leather, accent, 0.35f), MaterialClass::Cloth};
    for (int side = 0; side < 2; ++side) {  // 0 front, 1 back
        float sz = side == 0 ? 1.0f : -1.0f;
        Loft L;
        for (float t : {0.0f, 0.5f, 1.0f}) {
            float y = lerp(yTop, yBot, t);
            float tt = torsoT(std::max(y, m.crotchY + 0.02f * m.torsoLen));
            SuperRing tr = torsoRing(tt);
            float z = (side == 0 ? tr.frontZ(0) : tr.backZ(0)) + sz * (0.03f * h + 0.08f * h * t);
            if (side == 1) z -= gluteDepth(std::max(tt, 0.07f));
            SuperRing sr{wHalf * (1.0f - 0.15f * t), 0, 0.012f * h, 0.012f * h, 8.0f, 8.0f};
            Vec3 d{0, -1, 0}, v{0, 0, 1};
            L.rings.push_back(makeRing({0, y, z}, cross(d, v), v, sr, 8));
            L.t.push_back(t);
        }
        int hips = r.hips, tl = r.legs[0].upper, tr = r.legs[1].upper;
        emitLoft(newPart(), L, [=](float t, float, Vec3) {
            SkinW w;
            w.add(hips, 1 - 0.4f * t);
            w.add(tl, 0.2f * t);
            w.add(tr, 0.2f * t);
            w.normalize();
            return w;
        }, [=](float, float) { return cloth; });
    }
}

void Builder::buildShoulderPads() {
    const Outfit& o = g.outfit;
    if (o.shoulderPads == 0) return;
    Rng rr = Rng(g.seed).fork(77);
    const bool spikes = rr.chance(g.archetype == Archetype::Brute || g.archetype == Archetype::Monster ? 0.7f : 0.25f);
    for (int s = 0; s < std::min(2, o.shoulderPads); ++s) {
        const ArmMarks& a = m.arms[size_t(s)];
        const LimbRig& rig = ch.rig.arms[size_t(s)];
        const float del = a.deltoid, ps = o.padSize;
        Ellipsoid e;
        e.center = a.shoulder + Vec3{a.side * 0.15f * del, 0.45f * del, 0};
        e.axis = normalize(Vec3{a.side * 0.35f, 1.0f, 0});
        e.up = {0, 0, 1};
        e.rAxis = 0.75f * del * ps;
        e.rU = 1.35f * del * ps;
        e.rV = 1.25f * del * ps;
        e.lat = lod.ellLat;
        e.lon = lod.ellLon;
        Mat metal{g.palette.metal, MaterialClass::Metal}, rim{accent, MaterialClass::Cloth};
        int up = rig.upper, clav = rig.root;
        emitEllipsoid(newPart(), e, [=](float, float, Vec3) { return blend(up, clav, 0.45f); },
                      [=](float t, float) { return (t > 0.36f && t < 0.5f) ? rim : metal; });
        colliders.push_back({e.center, {e.rU, e.rAxis, e.rV}});
        if (!spikes) continue;
        for (int k = 0; k < 2; ++k) {
            Vec3 base = e.center + e.axis * (0.6f * e.rAxis) + Vec3{a.side * (0.25f + 0.35f * k) * e.rU, 0, (k ? -0.2f : 0.25f) * e.rV};
            Vec3 dir = normalize(e.axis + Vec3{a.side * 0.5f * k, 0, 0});
            float len = del * ps * (1.1f - 0.3f * k), rad = 0.28f * del * ps;
            Vec3 v = orthoFront(dir), u = cross(dir, v);
            Loft L;
            L.rings.push_back(makeRing(base, u, v, SuperRing{rad, 0, rad, rad}, 6));
            L.t.push_back(0);
            L.poleEnd = true;
            L.endPole = base + dir * len;
            L.tEnd = 1;
            Mat sm{g.palette.horn, MaterialClass::Horn};
            emitLoft(newPart(true), L, [=](float, float, Vec3) { return blend(up, clav, 0.45f); },
                     [=](float, float) { return sm; });
        }
    }
}

void Builder::buildTail() {
    if (!g.body.tail) return;
    Skeleton& sk = ch.skeleton;
    RigInfo& r = ch.rig;
    const float h = m.h;
    const float y0 = m.crotchY + 0.22f * m.torsoLen;
    const float z0 = torsoRing(torsoT(y0)).backZ(0) + 0.02f * h;
    const float len = g.body.tailLength * m.hipJointY * 0.95f;
    const bool fluffy = g.palette.skin.y > g.palette.skin.x * 0.9f ? false : true;  // reptile on green/blue skins
    const float r0 = (fluffy ? 0.13f : 0.11f) * h * (1 + 0.3f * g.body.muscle);
    // Curve: down & back, then sweeping up at the tip.
    constexpr int kPts = 9;
    std::vector<Vec3> pts;
    Vec3 p{0, y0, z0};
    float step = len / (kPts - 1);
    for (int i = 0; i < kPts; ++i) {
        pts.push_back(p);
        float u = float(i) / (kPts - 1);
        p += normalize(Vec3{0, lerp(-0.7f, 0.6f, u), -1.0f}) * step;
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
        float rad = r0 * (fluffy ? (0.7f + 0.6f * std::sin(kPi * std::min(1.0f, u * 1.15f))) : (1.0f - 0.8f * u));
        L.rings.push_back(makeRing(pts[size_t(i)], uu, v, SuperRing{rad, 0, rad, rad}, lod.limbSeg));
        L.t.push_back(u);
    }
    L.poleStart = true;
    L.startPole = pts[0] + Vec3{0, 0, 0.05f * h};
    L.tStart = 0;
    L.poleEnd = true;
    L.endPole = pts.back() + normalize(pts.back() - pts[pts.size() - 2]) * (0.6f * r0);
    L.tEnd = 1;
    std::vector<int> bones = r.tail;
    int hips = r.hips;
    Vec3 tip = g.palette.hairTip, base = fluffy ? g.palette.hair : g.palette.skin;
    emitLoft(newPart(), L, [=](float t, float, Vec3) {
        float f = t * nb;
        int i0 = std::clamp(int(f), 0, nb - 1);
        float fr = f - i0;
        if (i0 == nb - 1) return one(bones[size_t(i0)]);
        if (t < 0.05f) return blend(hips, bones[0], t / 0.05f);
        return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], smoothstep(0.3f, 1.0f, fr));
    }, [=](float t, float) -> Mat {
        if (fluffy) return {t > 0.8f ? tip : base, MaterialClass::Hair};
        return {base * (1.0f - 0.15f * t), MaterialClass::Skin};
    });
}

Character Builder::build() {
    ch.genome = g;
    ch.height = m.H;
    buildSkeleton();
    buildTorso();
    buildBust();
    buildNeck();
    buildHead();
    buildArms();
    buildLegs();
    buildFeet();
    buildBelt();
    buildSkirt();
    buildLoincloth();
    buildShoulderPads();
    buildTail();
    buildEars();
    buildHorns();
    buildHair();
    buildFaceDecals();
    finalizeParts(parts, ch.mesh, opt.faceted);
    ch.faceAtlas = paintFaceAtlas(g);
    return std::move(ch);
}

}  // namespace detail

Character generateCharacter(const Genome& g, const GenOptions& opt) {
    detail::Builder b(g, opt);
    return b.build();
}

}  // namespace cg
