// Head: a superellipse-ring skull with sculpted facial relief (nose, brow
// ridge, eye sockets, cheekbones, lips, chin). At low resolution the painted
// face is only a few pixels, so the planes of the head carry the character;
// the painted decals add eyes, brows and mouth on top.
#include <algorithm>

#include "generator_impl.hpp"

namespace cg::detail {

void Builder::initHead() {
    // Normalised to head height: s = 0 chin, 1 crown. Eye line ~0.47.
    // Stylised: the lower face narrows to a defined chin, the cranium stays full.
    hW_ = Curve{{0, 0.085f},    {0.06f, 0.16f}, {0.15f, 0.245f}, {0.25f, 0.305f}, {0.36f, 0.352f}, {0.47f, 0.375f},
                {0.60f, 0.392f}, {0.74f, 0.39f}, {0.86f, 0.345f}, {0.95f, 0.235f}, {1.0f, 0.05f}};
    hZc_ = Curve{{0, 0.20f}, {0.06f, 0.16f}, {0.15f, 0.10f}, {0.28f, 0.03f}, {0.42f, -0.01f},
                 {0.58f, -0.035f}, {0.74f, -0.05f}, {0.88f, -0.06f}, {1.0f, -0.06f}};
    hDf_ = Curve{{0, 0.07f},     {0.06f, 0.14f}, {0.15f, 0.24f}, {0.28f, 0.35f}, {0.42f, 0.41f},
                 {0.58f, 0.425f}, {0.74f, 0.41f}, {0.88f, 0.33f}, {0.96f, 0.22f}, {1.0f, 0.04f}};
    hDb_ = Curve{{0, 0.05f},     {0.06f, 0.10f}, {0.15f, 0.18f}, {0.28f, 0.30f}, {0.42f, 0.40f},
                 {0.58f, 0.44f}, {0.74f, 0.45f}, {0.88f, 0.38f}, {0.96f, 0.26f}, {1.0f, 0.04f}};
    hNf_ = Curve{{0, 2.1f}, {0.15f, 2.0f}, {0.30f, 2.2f}, {0.47f, 2.5f}, {0.60f, 2.5f}, {0.90f, 2.2f}, {1.0f, 2.0f}};
    hNb_ = Curve{{0, 2.0f}, {0.4f, 2.2f}, {1.0f, 2.1f}};
}

SuperRing Builder::headRing(float s) const {
    const float h = m.h;
    const FaceStyle& f = g.face;
    // Individual structure. Each term sits on its landmark so the planes of the head
    // change with it: jaw angle, chin, cheekbones and the hollows beneath them, jowls,
    // overall face width and the cranium.
    float wMul = 1.0f + f.jaw * 0.15f * bump((s - 0.15f) / 0.16f) + f.chinWidth * 0.30f * bump(s / 0.09f) +
                 f.cheek * 0.08f * bump((s - 0.38f) / 0.11f) - f.cheekHollow * 0.07f * bump((s - 0.25f) / 0.08f) +
                 f.jowl * 0.11f * bump((s - 0.13f) / 0.13f) + f.roundness * 0.07f * bump((s - 0.25f) / 0.16f) +
                 f.faceWidth * 0.10f * bump((s - 0.30f) / 0.32f) + f.crown * 0.06f * bump((s - 0.78f) / 0.22f);
    // Masculine jaw: a defined angle and a broad, flat-bottomed chin (no long V).
    const float masc = 1.0f - g.femininity;
    wMul += masc * (0.10f * bump((s - 0.11f) / 0.09f) + 0.14f * bump(s / 0.06f));
    // Species character: a bulbous dome, a pointed chin, a jutting underbite.
    wMul += f.headTop * 0.13f * bump((s - 0.82f) / 0.24f);
    wMul *= 1.0f - f.chinPoint * 0.55f * smoothstep(0.34f, 0.0f, s);
    wMul += f.underbite * 0.12f * bump((s - 0.10f) / 0.12f);
    const float crownBack = f.crown * 0.05f * bump((s - 0.75f) / 0.25f) + f.headTop * 0.07f * bump((s - 0.80f) / 0.25f);
    const float chinFwd = f.chin * 0.03f * bump(s / 0.14f) + f.chinPoint * 0.035f * bump(s / 0.12f) +
                          f.underbite * 0.10f * smoothstep(0.24f, 0.06f, s);
    const float chinDrop = f.chin * 0.02f * bump(s / 0.1f);
    const float full = (0.02f * f.roundness + 0.015f * f.jowl) * bump((s - 0.22f) / 0.15f);
    // Muzzle (bovine/beast peoples): the lower face pushed forward below the eyes.
    // Wolf: a long, narrow snout from a stop just under the eyes; the lower jaw shorter.
    // Reptile: a long, flat snout from just under the eyes, the jaw nearly as long as it.
    const bool wolf = g.body.snout == 1, reptile = g.body.snout == 2;
    const float muz = wolf      ? g.body.muzzle * 0.72f * smoothstep(0.445f, 0.355f, s) * (0.35f + 0.65f * smoothstep(0.02f, 0.24f, s))
                      : reptile ? g.body.muzzle * 0.62f * smoothstep(0.46f, 0.36f, s) * (0.45f + 0.55f * smoothstep(0.0f, 0.22f, s))
                                : g.body.muzzle * 0.46f * smoothstep(0.53f, 0.40f, s) * (0.50f + 0.50f * smoothstep(0.0f, 0.18f, s));
    const float narrow = wolf || reptile ? 0.30f : 0.22f;
    return {std::max(hW_(s), 0.01f) * h * g.body.headWidth * wMul * (1.0f - narrow * g.body.muzzle * (wolf ? smoothstep(0.44f, 0.32f, s) : smoothstep(0.5f, 0.3f, s))),
            (hZc_(s) + chinFwd + 0.5f * muz) * h,
            std::max(hDf_(s) + chinDrop * 0.5f + full + 0.5f * muz, 0.01f) * h, std::max(hDb_(s) + crownBack - 0.5f * muz, 0.01f) * h,
            hNf_(s), hNb_(s)};
}

// Forward relief of the face in head units; xn is the horizontal position in
// head units before headWidth scaling, s the height above the chin. The nose is
// a separate faceted piece (buildNose); only a soft base for it is sculpted here.
float Builder::faceRelief(float xn, float s) const {
    const FaceStyle& f = g.face;
    const float ax = std::fabs(xn);
    float r = 0;
    r += (0.006f + 0.004f * f.noseBridge) * bump((s - 0.43f) / 0.13f) * bump(xn / 0.07f);
    // Brow ridge (with the glabella) overhanging the sockets. Four-eyed heads stack two
    // sockets: the main pair higher under a higher brow, a smaller pair below it set a
    // little wider, a bony ridge between them and the cheekbones pushed down.
    const bool four = f.eyePairs >= 2;
    const float browS = four ? 0.600f : 0.56f, eyeS = four ? 0.506f : 0.468f, cheekS = four ? 0.330f : 0.375f;
    r += (0.006f + 0.026f * f.browRidge + 0.008f * f.eyeDepth) * bump((s - browS) / 0.045f) * bump(xn / 0.36f);
    r -= (0.008f + 0.020f * f.eyeDepth) * bump((s - eyeS) / (four ? 0.045f : 0.055f)) * bump((ax - 0.165f * f.eyeSpacing) / 0.115f);
    if (four) {
        // The lower pair sits in a socket as deep as the upper one, under a lid-and-brow
        // ridge of its own, with a lower-lid pad beneath: each eye is framed in bone.
        const float ex2 = 0.200f * f.eyeSpacing;
        r -= (0.012f + 0.020f * f.eyeDepth) * bump((s - 0.392f) / 0.034f) * bump((ax - ex2) / 0.080f);
        r += 0.013f * bump((s - 0.448f) / 0.024f) * bump((ax - ex2) / 0.12f);
        r += 0.006f * bump((s - 0.350f) / 0.022f) * bump((ax - ex2) / 0.085f);
    }
    // Cheekbones and the hollow beneath them.
    r += (0.010f + 0.016f * std::max(0.0f, f.cheek)) * bump((s - cheekS) / 0.05f) * bump((ax - 0.235f) / 0.09f);
    r -= 0.016f * f.cheekHollow * bump((s - 0.27f) / 0.06f) * bump((ax - 0.22f) / 0.08f);
    // Lips, the dip below them, and the chin.
    const float mw = f.mouthWidth;
    r += (0.003f + 0.004f * f.lipFullness) * bump((s - 0.239f) / 0.024f) * bump(xn / (0.075f * mw));
    r += (0.004f + 0.005f * f.lipFullness) * bump((s - 0.205f) / 0.024f) * bump(xn / (0.068f * mw));
    r -= 0.007f * bump((s - 0.176f) / 0.018f) * bump(xn / 0.07f);
    r += (0.010f + 0.014f * f.chin) * bump((s - 0.09f) / 0.06f) * bump(xn / (0.11f * (1.0f + 0.4f * f.chinWidth)));
    return r;
}

// Uniform phi -> angle, compressing samples around the face centre (theta = pi/2).
float Builder::headWarp(float phi) const {
    float d = std::remainder(phi - kPi / 2, kTau);  // -pi..pi
    float a = std::fabs(d) / kPi;
    return kPi / 2 + std::copysign(kPi * std::pow(a, 1.12f), d);  // mild: the face stays a few large planes
}

Vec3 Builder::headPoint(float theta, float s) const {
    SuperRing r = headRing(s);
    Vec2 p = r.point(theta);
    float sn = std::sin(theta);
    if (sn > 0) p.y += faceRelief(p.x / (m.h * g.body.headWidth), s) * m.h * smoothstep(0.0f, 0.6f, sn);
    return m.headOrigin + Vec3{p.x, s * m.h, p.y};
}

Vec3 Builder::headCenter() const { return m.headOrigin + Vec3{0, 0.55f * m.h, -0.03f * m.h}; }

Vec3 Builder::headNormalDir(Vec3 p) const { return normalize(p - headCenter()); }

Vec3 Builder::headSurface(float theta, float s, float offset) const {
    Vec3 q = headPoint(theta, s);
    return q + headNormalDir(q) * offset;
}

Vec3 Builder::collide(Vec3 p, float margin) const {
    const float h = m.h;
    const auto& bones = ch.skeleton.bones;
    for (int iter = 0; iter < 3; ++iter) {
        // Head: superellipse rings, with a sphere for the crown.
        Vec3 l = p - m.headOrigin;
        float s = l.y / h;
        if (s > 0.0f && s < 0.82f) {
            SuperRing r = headRing(s);
            r.dF += 0.05f * h * bump((s - 0.4f) / 0.2f);  // keep clear of the nose
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
        // Torso (sculpted surface).
        float t = torsoT(p.y);
        if (t > -0.05f && t < 1.03f) {
            float zc = cZc_(t);
            Vec2 d{p.x, p.z - zc};
            float ang = std::atan2(d.y, d.x), th = ang;
            Vec3 sp;
            for (int k = 0; k < 3; ++k) {
                sp = torsoPoint(t, th);
                th += std::remainder(ang - std::atan2(sp.z - zc, sp.x), kTau);
            }
            float rs = length(Vec2{sp.x, sp.z - zc}) + margin, rp = length(d);
            if (rp < rs) {
                Vec2 n = rp > 1e-5f ? d / rp : Vec2{0, -1};
                p.x = n.x * rs;
                p.z = zc + n.y * rs;
            }
        }
        // Remaining capsules (neck, arms, thighs) from the physics rig.
        for (const PhysCollider& c : ch.physics.colliders) {
            Vec3 o = bones[size_t(c.bone)].restWorld;
            Vec3 a = o + c.a, b = o + c.b, ab = b - a;
            float tt = dot(ab, ab) > 1e-9f ? saturate(dot(p - a, ab) / dot(ab, ab)) : 0.0f;
            Vec3 q = a + ab * tt, dq = p - q;
            float R = c.radius + margin, dl = length(dq);
            if (dl < R && dl > 1e-6f) p = q + dq * (R / dl);
        }
    }
    p.y = std::max(p.y, margin + 0.06f * h);  // very long hair on short people rests on the ground
    return p;
}

void Builder::buildHead() {
    const int head = ch.rig.head;
    const float h = m.h;
    Loft L;
    // Few rings, each on a landmark (chin, jaw, mouth, nose tip, cheekbone, eyes, brow, cranium).
    const std::vector<float> ss = g.face.eyePairs >= 2  // landmarks of the stacked sockets
        ? std::vector<float>{0.0f, 0.05f, 0.10f, 0.16f, 0.22f, 0.28f, 0.35f, 0.392f, 0.448f, 0.506f, 0.56f, 0.60f, 0.68f, 0.78f, 0.88f}
        : std::vector<float>{0.0f, 0.05f, 0.10f, 0.16f, 0.22f, 0.28f, 0.34f, 0.40f, 0.47f, 0.56f, 0.64f, 0.76f, 0.88f};
    const int N = lod.headSeg;
    int i = 0;
    for (float s : ss) {
        if (lod.sparseRings && (i++ % 2) && s < 0.85f) continue;
        std::vector<Vec3> ring(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) ring[size_t(j)] = headPoint(headWarp(kTau * j / N), s);
        L.rings.push_back(ring);
        L.t.push_back(s);
    }
    L.poleStart = true;
    L.startPole = m.headOrigin + Vec3{0, -0.01f * h, hZc_(0) * h};
    L.tStart = 0;
    L.poleEnd = true;
    L.endPole = m.headOrigin + Vec3{0, h, hZc_(1.0f) * h};
    L.tEnd = 1;
    Part& hp = newPart();
    hp.smoothAlways = true;  // smooth in the smooth style; the faceted style flattens it like the body
    headPartId_ = hp.id;
    headPart_ = parts.size() - 1;
    // Mild forward bend of the face normals: keeps hard shadows off the eyes
    // while the nose and brow still read.
    const Vec3 o = m.headOrigin;
    hp.normalFn = [=](Vec3 p, Vec3 n) {
        float s = (p.y - o.y) / h;
        float front = smoothstep(0.0f, 0.3f * h, p.z - o.z) * bump((s - 0.42f) / 0.5f);
        return normalize(lerp(n, normalize(Vec3{n.x * 0.5f, n.y * 0.6f + 0.1f, 1.0f}), 0.5f * front));
    };
    emitLoft(hp, L, [=](float, float, Vec3) { return one(head); }, [&](float, float) { return skin(); });
}

// The nose: its own small faceted solid (ridge, side planes, tip, wings and the
// underside), set on the built face. It is the strongest plane break of the
// face, so length, projection, width, hump and tip angle all read clearly.
void Builder::buildNose() {
    if (g.face.beak) return;  // the beak takes the nose's place
    const FaceStyle& f = g.face;
    const float h = m.h;
    const Vec3 o = m.headOrigin;
    auto faceZ = [&](float x, float s) {  // front of the built head, metres relative to the head origin
        float z;
        if (headMeshZ(x * h, s * h, z)) return z - o.z;
        SuperRing r = headRing(s);
        return r.frontZ(std::min(std::fabs(x * h), r.w * 0.995f)) + faceRelief(x / g.body.headWidth, s) * h;
    };
    const float L = f.noseLength, P = f.noseProj, Wd = f.noseWidth, tip = f.noseTip;
    const float sRoot = lerp(0.505f, 0.485f, g.femininity);
    const float sTip = clamp(0.345f - 0.06f * (L - 1.0f) + 0.010f * tip, 0.285f, 0.385f);
    const float proj = 0.050f * P, dR = 0.004f + 0.008f * f.noseBridge;
    const float sM = lerp(sRoot, sTip, 0.55f), dM = lerp(dR, proj, 0.55f) + 0.016f * f.noseHump;
    const float droop = std::max(0.0f, -tip);
    const float sU = sTip - 0.016f - 0.008f * droop, sBase = sTip - 0.030f - 0.006f * droop;
    Part& p = newPart();
    p.id = headPartId_;
    nosePart_ = parts.size() - 1;
    const SkinW w = one(ch.rig.head);
    auto V = [&](float x, float sv, float d) {  // d: standoff from the face in head units (negative sinks in)
        p.pos.push_back(o + Vec3{x * h, sv * h, faceZ(x, sv) + (d > 0 ? d : d - 0.003f) * h});
        p.w.push_back(w);
        p.uv.push_back({});
        return uint32_t(p.pos.size() - 1);
    };
    const uint32_t R = V(0, sRoot, dR), M = V(0, sM, dM), T = V(0, sTip + 0.006f * tip, proj),
                   U = V(0, sU, proj * (0.70f + 0.10f * tip)), S = V(0, sBase, 0.0f);
    const Vec3 inner = o + Vec3{0, 0.40f * h, faceZ(0, 0.40f) - 0.04f * h};
    const Mat sk = skin(), wet{{0.07f, 0.06f, 0.06f}, MaterialClass::Dark};
    const float tipY = o.y + (sTip + 0.012f) * h;
    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        const Vec3 A = p.pos[a], B = p.pos[b], C = p.pos[c];
        if (dot(cross(B - A, C - A), (A + B + C) / 3.0f - inner) < 0) std::swap(b, c);
        p.tris.push_back({a, b, c, f.darkNose && (A.y + B.y + C.y) / 3.0f < tipY ? wet : sk});  // animal nose: dark leather tip
    };
    for (float sd : {1.0f, -1.0f}) {
        const uint32_t Rs = V(sd * 0.017f * Wd, sRoot + 0.006f, 0), Bs = V(sd * 0.027f * Wd, sM, 0),
                       Ts = V(sd * 0.024f * Wd, sTip + 0.004f, proj * 0.62f), Ws = V(sd * 0.040f * Wd, sTip - 0.010f, proj * 0.32f),
                       As = V(sd * 0.045f * Wd, sTip - 0.022f, 0), Ns = V(sd * 0.016f * Wd, sBase + 0.005f, proj * 0.26f);
        tri(R, Rs, Bs);
        tri(R, Bs, M);
        tri(M, Bs, Ts);
        tri(M, Ts, T);
        tri(Bs, Ws, Ts);
        tri(Bs, As, Ws);
        tri(T, Ts, U);
        tri(Ts, Ws, Ns);
        tri(Ts, Ns, U);
        tri(Ws, As, Ns);
        tri(U, Ns, S);
        tri(Ns, As, S);
    }
}

// Pushes p out along the ray from the head centre so it is never inside the built
// (faceted) head: the chords of the low-poly surface can sit outside the sculpted one.
Vec3 Builder::outsideHeadMesh(Vec3 p, float margin) const {
    const Part& hp = parts[headPart_];
    const Vec3 c = headCenter(), d = normalize(p - c, Vec3{0, 0, 1});
    float best = -1.0f;
    for (const auto& tr : hp.tris) {
        const Vec3 a = hp.pos[tr.a], e1 = hp.pos[tr.b] - a, e2 = hp.pos[tr.c] - a;
        const Vec3 pv = cross(d, e2);
        const float det = dot(e1, pv);
        if (std::fabs(det) < 1e-12f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = c - a;
        const float u = dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 qv = cross(tv, e1);
        const float v = dot(d, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        best = std::max(best, dot(e2, qv) * inv);
    }
    const float r = length(p - c);
    return best > 0.0f && best + margin > r ? c + d * (best + margin) : p;
}

// Front-most intersection of a ray along -z (at x, y relative to the head origin)
// with the built head mesh.
// Four-eyed heads: each eye gets a modelled upper lid (a faceted crescent that
// overhangs the painted opening) and a thinner lower lid, so the stacked eyes sit in
// flesh rather than reading as stickers. Rows match paintEyes.
void Builder::buildEyelids() {
    const FaceStyle& f = g.face;
    if (f.eyePairs < 2 || bonesOnly_) return;
    const float h = m.h, hw = g.body.headWidth;
    struct Row {
        float ey, ex, scale;
    };
    const Row rows[] = {{0.506f, 0.180f * f.eyeSpacing, 0.86f}, {0.392f, 0.200f * f.eyeSpacing, 0.78f}};
    Part& p = newPart();
    const int head = ch.rig.head;
    const Mat lid{g.palette.skin * 0.92f, MaterialClass::Skin};
    for (const Row& r : rows)
        for (float side : {1.0f, -1.0f}) {
            const float W = 0.094f * f.eyeSize * r.scale, Hh = 0.050f * f.eyeSize * r.scale;
            for (int lower = 0; lower < 2; ++lower) {
                Loft L;
                const int n = 7;
                std::vector<Vec3> pts;
                for (int k = 0; k < n; ++k) {
                    const float u = -1.08f + 2.16f * float(k) / float(n - 1);
                    const float xf = side * (r.ex + u * W);
                    const float s = lower ? r.ey - Hh * (0.72f + 0.12f * (1.0f - u * u)) : r.ey + Hh * (0.92f + 0.30f * (1.0f - u * u)) - 0.10f * Hh * u;
                    float z;
                    const float x = xf * h * hw;
                    if (!headMeshZ(x, s * h, z)) z = headPoint(std::acos(clamp(xf / 0.36f, -1.0f, 1.0f)), s).z;
                    pts.push_back(Vec3{m.headOrigin.x + x, m.headOrigin.y + s * h, z + 0.004f * h});
                }
                for (int k = 0; k < n; ++k) {
                    const float u = float(k) / float(n - 1);
                    const Vec3 d = normalize(pts[size_t(std::min(k + 1, n - 1))] - pts[size_t(std::max(k - 1, 0))], Vec3{side, 0, 0});
                    const Vec3 v = normalize(Vec3{0, 0, 1} - d * d.z, Vec3{0, 0, 1}), uu = cross(d, v);
                    const float taper = std::sin(kPi * (0.08f + 0.84f * u));
                    const float th = (lower ? 0.0055f : 0.0120f) * h * r.scale * taper;   // up-down
                    const float dp = (lower ? 0.0050f : 0.0100f) * h * r.scale * taper;   // standing off the face
                    L.rings.push_back(makeRing(pts[size_t(k)] + v * (0.5f * dp), uu, v, SuperRing{th, 0, dp, 0.5f * dp, 1.6f, 2.0f}, 4));
                    L.t.push_back(u);
                }
                L.poleStart = L.poleEnd = true;
                L.startPole = pts.front();
                L.endPole = pts.back();
                emitLoft(p, L, [=](float, float, Vec3) { return one(head); }, [=](float, float) { return lid; });
            }
        }
}

bool Builder::headMeshZ(float x, float y, float& z) const {
    const Part& hp = parts[headPart_];
    const Vec3 o = m.headOrigin + Vec3{x, y, 0};
    bool hit = false;
    for (const auto& tr : hp.tris) {
        const Vec3 a = hp.pos[tr.a], b = hp.pos[tr.b], c = hp.pos[tr.c];
        // 2D barycentric test in the xy plane, then interpolate z.
        float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::fabs(d) < 1e-12f) continue;
        float l1 = ((b.y - c.y) * (o.x - c.x) + (c.x - b.x) * (o.y - c.y)) / d;
        float l2 = ((c.y - a.y) * (o.x - c.x) + (a.x - c.x) * (o.y - c.y)) / d;
        float l3 = 1 - l1 - l2;
        if (l1 < -1e-5f || l2 < -1e-5f || l3 < -1e-5f) continue;
        float zz = l1 * a.z + l2 * b.z + l3 * c.z;
        if (!hit || zz > z) z = zz, hit = true;
    }
    return hit;
}

// The 2D face lives on two decal patches that hug the sculpted front of the
// head. Their UVs point at the Neutral cells; expressions are UV offsets.
void Builder::buildFaceDecals() {
    const int head = ch.rig.head;
    const float h = m.h, eps = 0.0050f * h;
    auto patch = [&](SubmeshKind kind, float x0, float x1, float s0, float s1, int nx, int ny, int cellX, int cellY,
                     int cellW, int cellH) {
        Part& p = newPart(false, kind);
        p.smoothAlways = true;
        std::vector<bool> onHead;
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                float fu = float(i) / nx, fv = float(j) / ny;
                float xf = lerp(x0, x1, fu), s = lerp(s0, s1, fv);
                SuperRing r = headRing(s);
                float x = xf * h * g.body.headWidth;
                float z = r.frontZ(std::min(std::fabs(x), r.w * 0.995f)) + faceRelief(xf, s) * h + eps;
                // Sit on the actual (possibly faceted, low-poly) head surface: cast along -z.
                const bool hit = std::fabs(x) < r.w * 0.96f;
                if (float hz; headMeshZ(x, s * h, hz)) z = hz - m.headOrigin.z + eps;
                onHead.push_back(hit);
                p.pos.push_back(m.headOrigin + Vec3{x, s * h, z});
                p.w.push_back(one(head));
                float u = (cellX + 0.5f + fu * (cellW - 1)) / FaceLayout::kAtlasW;
                float v = (cellY + 0.5f + (1 - fv) * (cellH - 1)) / FaceLayout::kAtlasH;
                p.uv.push_back({u, v});
            }
        Mat white{{1, 1, 1}, MaterialClass::Skin};
        // Only the front of the face: quads off the silhouette or turned sideways would
        // stretch the cell across the temples.
        auto ok = [&](uint32_t a, uint32_t b, uint32_t c) {
            const Vec3 A = p.pos[a], B = p.pos[b], C = p.pos[c];
            const Vec3 n = normalize(cross(B - A, C - A), {0, 0, 1});
            return std::fabs(n.z) > 0.45f && onHead[a] && onHead[b] && onHead[c];
        };
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                uint32_t a = uint32_t(j * (nx + 1) + i), b = a + 1, c = a + uint32_t(nx + 1) + 1, d = a + uint32_t(nx + 1);
                if (ok(a, b, c)) p.tris.push_back({a, b, c, white});
                if (ok(a, c, d)) p.tris.push_back({a, c, d, white});
            }
        return parts.size() - 1;
    };
    auto ec = FaceLayout::eyeCell(EyeExpr::Neutral);
    auto mc = FaceLayout::mouthCell(MouthExpr::Neutral);
    // Dense enough to follow the nose bridge and the sockets.
    size_t eyes = patch(SubmeshKind::FaceEyes, FaceLayout::kEyeX0, FaceLayout::kEyeX1, FaceLayout::kEyeS0, FaceLayout::kEyeS1,
                        lod.headSeg, std::max(4, lod.headSeg / 3), ec[0], ec[1], FaceLayout::kEyeCellW, FaceLayout::kEyeCellH);
    size_t mouth = patch(SubmeshKind::FaceMouth, FaceLayout::kMouthX0, FaceLayout::kMouthX1, FaceLayout::kMouthS0,
                         FaceLayout::kMouthS1, std::max(6, lod.headSeg / 2), 8, mc[0], mc[1], FaceLayout::kMouthCellW,
                         FaceLayout::kMouthCellH);
    parts[eyes].id = parts[mouth].id = headPartId_;
    if (g.face.beak) parts[mouth].tris.clear();         // the beak covers the mouth
    if (g.face.eyeKind == 1) parts[eyes].tris.clear();  // compound eyes are modelled

    // Skin layer: a copy of the front of the faceted head (and the nose) with UVs
    // into the skin cell, so stubble, sockets, weathering, scars and ink follow
    // every facet exactly.
    Part& sk = newPart(false, SubmeshKind::FaceSkin);
    sk.smoothAlways = true;
    sk.id = headPartId_;
    const Vec3 o = m.headOrigin;
    auto copyFront = [&](size_t src) {
        if (src >= parts.size()) return;
        const Part& sp = parts[src];
        std::vector<int> map(sp.pos.size(), -1);
        for (const auto& tr : sp.tris) {
            const Vec3 c = (sp.pos[tr.a] + sp.pos[tr.b] + sp.pos[tr.c]) / 3.0f;
            const float sc = (c.y - o.y) / h, xc = (c.x - o.x) / (h * g.body.headWidth);
            if (c.z - o.z < hZc_(clamp(sc, 0.0f, 1.0f)) * h - 0.02f * h) continue;  // front half only
            if (sc < FaceLayout::kSkinS0 + 0.01f || sc > FaceLayout::kSkinS1 - 0.01f || std::fabs(xc) > FaceLayout::kSkinX1 - 0.01f) continue;
            bool torn = false;  // no skin paint over decay holes
            for (const Hole& ho : holes_) torn |= ho.head && length(c - ho.c) < ho.R + 0.01f * h;
            if (torn) continue;
            uint32_t ids[3];
            const uint32_t vs[3] = {tr.a, tr.b, tr.c};
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = vs[k];
                if (map[v] < 0) {
                    const Vec3 q = sp.pos[v];
                    const float x = clamp((q.x - o.x) / (h * g.body.headWidth), FaceLayout::kSkinX0, FaceLayout::kSkinX1);
                    const float sv = clamp((q.y - o.y) / h, FaceLayout::kSkinS0, FaceLayout::kSkinS1);
                    const float fu = (x - FaceLayout::kSkinX0) / (FaceLayout::kSkinX1 - FaceLayout::kSkinX0);
                    const float fv = (FaceLayout::kSkinS1 - sv) / (FaceLayout::kSkinS1 - FaceLayout::kSkinS0);
                    map[v] = int(sk.pos.size());
                    sk.pos.push_back(q);
                    sk.w.push_back(sp.w[v]);
                    sk.uv.push_back({(FaceLayout::kSkinCellX + 0.5f + fu * (FaceLayout::kSkinCellW - 1)) / FaceLayout::kAtlasW,
                                     (FaceLayout::kSkinCellY + 0.5f + fv * (FaceLayout::kSkinCellH - 1)) / FaceLayout::kAtlasH});
                }
                ids[k] = uint32_t(map[v]);
            }
            sk.tris.push_back({ids[0], ids[1], ids[2], Mat{{1, 1, 1}, MaterialClass::Skin}});
        }
    };
    copyFront(headPart_);
    copyFront(nosePart_);
}

}  // namespace cg::detail
