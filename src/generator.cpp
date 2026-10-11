// Body generation. The body is sculpted from lofts of superellipse rings along
// the bones (torso, limbs, neck), welded into one smooth low-poly mesh. Every
// loft carries skin weights derived from its parameter along the bone chain.
// Bodies are base meshes: only a modesty layer (underwear), equipment is
// layered on top by the game.
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
        case 0: return {24, 24, 12, 10, 8, 8, 24, 7, false};
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
    const float D = b.chestDepth * h, post = b.posture, lat = b.muscle * b.muscle * 0.17f * h, nR = b.neckThick * h;
    const float hunch = b.hunch * h;
    cHw_ = Curve{{-0.06f, 0.92f * P}, {0.0f, 1.01f * P},  {0.06f, 1.02f * P}, {0.13f, P},
                 {0.23f, lerp(P, W, 0.38f)}, {0.32f, lerp(P, W, 0.75f)}, {0.42f, W}, {0.55f, lerp(W, C, 0.6f)}, {0.66f, 0.95f * C},
                 {0.76f, C + 0.4f * lat}, {0.85f, lerp(C, 0.9f * S, 0.55f) + lat}, {0.905f, std::max(0.92f * S, 0.95f * C)},
                 {0.945f, 0.88f * S}, {0.975f, 0.76f * S}, {1.0f, (0.58f - 0.06f * b.muscle) * S + 0.05f * b.muscle * h}, {1.05f, nR * 1.55f + 0.05f * b.muscle * h}, {1.10f, nR * 1.05f}};
    cDf_ = Curve{{-0.06f, 0.74f * D}, {0.0f, 0.86f * D}, {0.10f, 0.86f * D}, {0.20f, 0.84f * D}, {0.30f, 0.76f * D},
                 {0.42f, 0.72f * D},  {0.55f, 0.80f * D}, {0.66f, 0.88f * D}, {0.76f, 0.95f * D}, {0.86f, 0.92f * D - 0.02f * hunch},
                 {0.905f, 0.82f * D - 0.03f * hunch}, {0.945f, 0.74f * D}, {0.975f, 0.64f * D}, {1.0f, 0.55f * D + 0.03f * b.muscle * h}, {1.05f, nR * 1.25f}, {1.10f, nR * 0.95f}};
    // Age rounds the upper back (kyphosis).
    cDb_ = Curve{{-0.06f, 0.70f * D}, {0.0f, 0.80f * D}, {0.10f, 0.82f * D}, {0.20f, 0.80f * D}, {0.30f, 0.68f * D},
                 {0.42f, 0.62f * D},  {0.55f, 0.70f * D}, {0.66f, 0.80f * D}, {0.76f, 0.86f * D + 0.03f * hunch},
                 {0.86f, 0.88f * D + 0.08f * hunch}, {0.905f, 0.86f * D + 0.08f * hunch}, {0.945f, 0.80f * D + 0.06f * hunch}, {0.975f, 0.70f * D + 0.05f * hunch},
                 {1.0f, 0.62f * D + 0.06f * b.muscle * h + 0.04f * hunch}, {1.05f, nR * 1.35f + 0.04f * b.muscle * h}, {1.10f, nR * 1.0f}};
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
    const float kneeR = std::max(0.55f * thighR, 0.85f * calfR), ankleR = 0.52f * calfR;
    m.footLen = 0.92f * h * b.footSize;
    m.footHalfW = std::max(0.21f * m.footLen, ankleR * 1.25f);
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
            a.handTip = a.wrist + a.dir * (std::min(0.50f * h, 0.25f * L) * b.handSize);
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

Vec3 Builder::torsoPoint(float t, float th) const {
    const float h = m.h, mu = g.body.muscle, B = g.body.bust;
    SuperRing sr = torsoRing(t);
    Vec2 q = sr.point(th);
    const float sn = std::sin(th), u = std::fabs(q.x) / std::max(sr.w, 1e-4f);
    if (sn > 0) {
        float front = std::pow(sn, 0.6f);
        float bf = bustField(t, th);
        if (bf > 0) {
            float depth = (0.06f + 0.31f * std::min(B, 1.5f)) * h;
            q.y += depth * bf * front;
            q.x += std::copysign(depth * 0.18f * bf, q.x);
        }
        float pecK = mu * std::sqrt(mu) * 0.11f * h * (1.0f - saturate(B * 1.5f));
        if (pecK > 0) {
            const float tp = 0.79f;
            float du = (u - 0.43f) / 0.50f, dt = (t - tp) / (t < tp ? 0.08f : 0.17f);
            float e = 1.0f - du * du - dt * dt;
            if (e > 0) q.y += pecK * (t < tp ? std::pow(e, 0.6f) : e) * front;
        }
        float absK = 0.028f * h * mu * (1.0f - std::min(1.0f, 2.0f * g.body.belly));
        if (absK > 0 && t > 0.38f && t < 0.68f && u < 0.42f) {
            float rows = 0.5f + 0.5f * std::cos(kTau * (t - 0.425f) / 0.08f);
            float fade = bump((t - 0.53f) / 0.16f) * bump(u / 0.42f);
            q.y += absK * (rows - 0.6f) * fade * front;
            q.y -= absK * 0.8f * bump(u / 0.08f) * fade;
        }
        // Navel.
        q.y -= 0.012f * h * bump(u / 0.05f) * bump((t - 0.40f) / 0.025f);
    } else {
        float back = std::pow(-sn, 0.7f);
        float lobe = 0.62f + 0.38f * bump((u - 0.42f) / 0.58f);
        q.y -= gluteDepth(t) * lobe * back;
        q.y += (0.012f + mu * 0.04f) * h * bump(u / 0.10f) * bump((t - 0.66f) / 0.34f);  // spine groove
        q.y -= mu * 0.045f * h * bump((u - 0.45f) / 0.28f) * bump((t - 0.84f) / 0.10f) * back;
        q.y -= mu * 0.03f * h * bump((u - 0.18f) / 0.12f) * bump((t - 0.50f) / 0.14f) * back;
    }
    return {q.x, m.crotchY + t * m.torsoLen, q.y};
}

Vec3 Builder::insideTorso(Vec3 p, float k) const {
    float t = torsoT(p.y);
    if (t < -0.06f || t > 1.05f) return p;
    float zc = cZc_(t);
    Vec2 d{p.x, p.z - zc};
    float ang = std::atan2(d.y, d.x), th = ang;
    Vec3 sp;
    for (int i = 0; i < 3; ++i) {
        sp = torsoPoint(t, th);
        th += std::remainder(ang - std::atan2(sp.z - zc, sp.x), kTau);
    }
    float rs = length(Vec2{sp.x, sp.z - zc}) * k, rp = length(d);
    if (rp <= rs || rp < 1e-6f) return p;
    return {d.x / rp * rs, p.y, zc + d.y / rp * rs};
}

// Modesty layer only: simple briefs and a chest band.
Mat Builder::underwearAt(float t, float th) const {
    if (!g.underwear) return skin();
    const float s = std::sin(th), c = std::cos(th);
    const Mat uw{g.palette.underwear, MaterialClass::Cloth};
    const bool fem = g.femininity > 0.5f;
    (void)s;
    (void)c;
    if (t < (fem ? 0.10f : 0.12f)) return uw;
    if (fem && g.body.bust > 0.05f) {
        const float B = std::min(g.body.bust, 1.4f);
        const float tb = 0.735f - 0.035f * B - 0.05f * g.body.sag;
        if (t > tb - 0.085f - 0.035f * B && t < tb + 0.06f) return uw;
    }
    return skin();
}

Mat Builder::paintTorsoAt(Vec3 p) const {
    float t = torsoT(p.y);
    float zc = cZc_(clamp(t, -0.06f, 1.07f));
    float th = std::atan2(p.z - zc, p.x);
    if (th < 0) th += kTau;
    return underwearAt(t, th);
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
    const float h = m.h, S = g.body.shoulderWidth * h;
    std::vector<float> bounds;
    if (g.underwear && g.femininity > 0.5f && g.body.bust > 0.05f) {
        const float B = std::min(g.body.bust, 1.4f), tb = 0.735f - 0.035f * B - 0.05f * g.body.sag;
        bounds = {tb - 0.085f - 0.035f * B, tb + 0.06f};
    }
    if (g.underwear) bounds.push_back(g.femininity > 0.5f ? 0.10f : 0.12f);
    std::vector<float> base{-0.06f, 0.0f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.36f, 0.42f, 0.48f, 0.54f,
                            0.59f, 0.63f, 0.67f, 0.71f, 0.75f, 0.79f, 0.83f, 0.87f, 0.91f, 0.95f, 0.98f, 1.01f, 1.04f, 1.07f};
    auto rings = mergeRings(base, bounds, lod.sparseRings);
    const int N = lod.torsoSeg;
    Loft L;
    for (float t : rings) {
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) ring[size_t(j)] = torsoPoint(t, warpTheta(kTau * j / N));
        L.rings.push_back(ring);
        L.t.push_back(t);
    }
    L.poleStart = true;
    L.startPole = {0, m.crotchY - 0.05f * m.torsoLen, cZc_(-0.06f)};
    L.tStart = -0.09f;
    L.poleEnd = true;
    L.endPole = {0, m.shoulderY + 0.12f * m.torsoLen, cZc_(1.10f)};
    L.tEnd = 1.12f;

    auto wf = [&, S](float t, float th, Vec3 p) {
        SkinW w = chainWeights({{0.12f, r.hips}, {0.40f, r.spine}, {0.66f, r.chest}, {1.0f, r.chest}, {1.10f, r.neck}}, t);
        if (r.bustL >= 0) {  // soft tissue follows the bust bones (secondary motion)
            float bf = bustField(t, warpTheta(th));
            if (bf > 0) {
                for (float& x : w.w) x *= (1 - 0.85f * bf);
                w.add(p.x > 0 ? r.bustL : r.bustR, 0.85f * bf);
            }
        }
        float cl = 0.5f * smoothstep(0.86f, 1.0f, t) * smoothstep(0.30f * S, 0.75f * S, std::fabs(p.x));
        if (cl > 0 && !r.arms.empty()) {
            for (float& x : w.w) x *= (1 - cl);
            w.add(r.arms[p.x > 0 ? 0 : 1].root, cl);
        }
        return w;
    };
    Part& tp = newPart();
    bodyGroup_ = tp.id;
    emitLoft(tp, L, wf, [&](float t, float th) { return underwearAt(t, warpTheta(th)); });
}

void Builder::buildNeck() {
    const RigInfo& r = ch.rig;
    const float h = m.h, nR = g.body.neckThick * h;
    Vec3 a{0, m.shoulderY - 0.15f * h, -0.02f * h + 0.04f * g.body.hunch * h}, b = m.headOrigin + Vec3{0, 0.24f * h, -0.12f * h};
    Vec3 d = normalize(b - a), v = orthoFront(d), u = cross(d, v);
    Loft L;
    for (float t : {0.0f, 0.3f, 0.6f, 0.85f, 1.0f}) {
        float rr = nR * (1.35f - 0.35f * smoothstep(0.0f, 0.45f, t));
        // Throat a touch flatter than the nape; a hint of the Adam's apple on men.
        float adam = (1.0f - g.femininity) * 0.10f * rr * bump((t - 0.62f) / 0.15f);
        L.rings.push_back(makeRing(lerp(a, b, t), u, v, SuperRing{rr, 0, rr * 0.92f + adam, rr * 0.98f}, lod.limbSeg));
        L.t.push_back(t);
    }
    emitLoft(newPart(), L, [&](float t, float, Vec3) {
        return chainWeights({{0.0f, r.chest}, {0.25f, r.neck}, {0.7f, r.neck}, {1.0f, r.head}}, t);
    }, [&](float, float) { return skin(); });
}

// ---------------- arms ----------------
void Builder::buildArms() {
    const float mu = g.body.muscle;
    for (size_t k = 0; k < m.arms.size(); ++k) {
        const ArmMarks& a = m.arms[k];
        const LimbRig& rig = ch.rig.arms[k];
        const float upperLen = length(a.elbow - a.shoulder), foreLen = length(a.wrist - a.elbow);
        const float Ltot = upperLen + foreLen, te = upperLen / Ltot;
        const Vec3 d = a.dir;
        const float r = a.radius;
        // Shoulder cap (deltoid) is part of the arm loft: it rounds up into the trapezius.
        Curve rc{{-0.20f, 0.50f}, {-0.13f, 0.78f + 0.20f * mu}, {-0.06f, 0.98f + 0.30f * mu}, {0.02f, 1.05f + 0.30f * mu},
                 {0.10f, 0.97f + 0.15f * mu}, {0.18f, 0.95f + 0.20f * mu}, {0.26f, 1.0f + 0.28f * mu}, {0.40f, 0.90f},
                 {te - 0.04f, 0.78f}, {te, 0.74f}, {te + 0.12f, 0.92f + 0.22f * mu}, {0.80f, 0.70f},
                 {1.0f, 0.56f}, {1.05f, 0.55f}};
        std::vector<float> ts{-0.20f, -0.13f, -0.06f, 0.02f, 0.10f, 0.18f, 0.26f, 0.34f, 0.42f,
                              te - 0.06f, te, te + 0.06f, te + 0.15f, 0.78f, 0.9f, 1.0f, 1.05f};
        if (lod.sparseRings) ts = mergeRings(ts, {}, true);
        Loft L;
        const Vec3 dirIn = normalize(Vec3{-a.side, 0.12f, 0});
        for (float t : ts) {
            float rr = r * rc(t);
            float flat = t > te ? 0.86f : 1.0f;
            float bic = 0.20f * rr * mu * bump((t - 0.27f) / 0.15f), tri = 0.18f * rr * mu * bump((t - 0.20f) / 0.18f);
            SuperRing sr{rr, 0.0f, rr * flat + bic, rr * flat + tri, 2.0f, 2.0f};
            // Above the joint the loft bends towards the neck so the shoulder cap
            // sits on the trapezius instead of rising above it.
            float kk = smoothstep(0.06f, -0.10f, t);
            Vec3 dd = normalize(lerp(d, -dirIn, kk));
            Vec3 vv = orthoFront(dd), uu = cross(dd, vv);
            Vec3 c = t >= 0 ? a.shoulder + d * (t * Ltot) : a.shoulder + dirIn * (-t * Ltot * 0.85f);
            c += Vec3{a.side, 0.04f * rr, 0} * (0.16f * rr * (0.6f + mu) * bump((t - 0.0f) / 0.13f));
            auto ring = makeRing(c, uu, vv, sr, lod.limbSeg);
            if (t < -0.08f && a.pair == 0)
                for (Vec3& p : ring) p = lerp(p, insideTorso(p, 0.99f), smoothstep(-0.08f, -0.16f, t));
            L.rings.push_back(ring);
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = a.shoulder + dirIn * (0.24f * Ltot * 0.85f);
        L.tStart = -0.26f;
        int clav = rig.root, up = rig.upper, lo = rig.lower, hand = rig.end;
        Part& ap = newPart();
        armGroup_ = ap.id;
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
void Builder::buildLegs() {
    const float h = m.h, mu = g.body.muscle;
    const float T = g.body.thigh * h, Cf = g.body.calf * h * (1 + 0.2f * mu);
    const float K = std::max(0.55f * T, 0.85f * Cf), A = 0.52f * Cf;
    for (int s = 0; s < 2; ++s) {
        const LimbRig& rig = ch.rig.legs[size_t(s)];
        const Vec3 Hj = m.hipJoint[s], Kn = m.knee[s], An = m.ankle[s];
        const float l1 = length(Kn - Hj), l2 = length(An - Kn), Lt = l1 + l2, tk = l1 / Lt;
        const Vec3 d1 = (Kn - Hj) / l1, d2 = (An - Kn) / l2;
        auto path = [&](float t) { return t < tk ? Hj + d1 * (t * Lt) : Kn + d2 * ((t - tk) * Lt); };
        const float Ttop = std::max(T * 1.02f, 0.50f * g.body.hipWidth * h);
        Curve rc{{-0.16f, 0.62f * Ttop}, {-0.06f, 0.88f * Ttop}, {0.0f, Ttop}, {0.14f, lerp(Ttop, T, 0.6f)}, {0.28f, 0.92f * T},
                 {tk - 0.12f, lerp(T, K, 0.7f)}, {tk, K}, {tk + 0.06f, 0.95f * Cf}, {tk + 0.17f, Cf},
                 {tk + 0.32f * (1 - tk), 0.92f * Cf}, {0.86f, lerp(Cf, A, 0.7f)}, {0.95f, A}, {1.06f, A}};
        std::vector<float> ts{-0.16f, -0.08f, 0.0f, 0.08f, 0.16f, 0.24f, 0.32f, 0.40f, tk - 0.06f, tk, tk + 0.06f,
                              tk + 0.14f, tk + 0.24f, 0.84f, 0.92f, 1.0f, 1.06f};
        if (lod.sparseRings) ts = mergeRings(ts, {}, true);
        Loft L;
        for (float t : ts) {
            Vec3 dd = normalize(lerp(d1, d2, smoothstep(tk - 0.06f, tk + 0.06f, t)));
            Vec3 v = orthoFront(dd), u = cross(dd, v);
            float rr = rc(t);
            float calf = bump((t - (tk + 0.17f)) / 0.22f);
            float quad = 0.10f * rr * mu * bump((t - 0.30f) / 0.20f);  // quadriceps
            float gl = std::min(g.body.butt, 1.6f) * 0.05f * h * bump((t + 0.10f) / 0.14f);
            float topFlat = 1.0f - 0.22f * smoothstep(0.18f, -0.02f, t);  // upper thigh: wide, not deep
            float kneeCap = 0.08f * rr * bump((t - tk) / 0.05f);
            SuperRing sr{rr, -0.10f * Cf * calf, rr * 0.96f * topFlat + quad + kneeCap,
                         rr * (1.0f + (0.12f + 0.12f * mu) * calf) * topFlat + gl, 2.0f, 2.0f};
            auto ring = makeRing(path(t), u, v, sr, lod.limbSeg);
            // Above the crotch the thigh lives inside the pelvis: no ledge where they meet.
            const float blendLo = m.crotchY - 0.26f * h, blendTop = m.crotchY - 0.03f * h;
            for (Vec3& p : ring)
                if (p.y > blendLo) p = lerp(p, insideTorso(p, 0.93f), smoothstep(blendLo, blendTop, p.y));
            L.rings.push_back(ring);
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = Hj + Vec3{0, 0.20f * Lt, 0};
        L.tStart = -0.20f;
        int hips = ch.rig.hips, th = rig.upper, sh = rig.lower, ft = rig.end;
        Part& lp = newPart();
        lp.id = bodyGroup_;
        // Faces tucked into the pelvis take the torso's paint so seams stay clean.
        lp.paintAt = [&](Vec3 c) { return c.y > m.crotchY - 0.06f * h ? paintTorsoAt(c) : skin(); };
        emitLoft(lp, L, [=](float t, float, Vec3) {
            return chainWeights({{-0.10f, hips}, {0.08f, th}, {tk - 0.07f, th}, {tk + 0.07f, sh}, {0.95f, sh}, {1.05f, ft}}, t);
        }, [&](float, float) { return skin(); });
        lp.paintAt = nullptr;
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
    buildNeck();
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
