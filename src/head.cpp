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
    hW_ = Curve{{0, 0.075f},    {0.06f, 0.15f}, {0.15f, 0.235f}, {0.25f, 0.30f}, {0.36f, 0.35f}, {0.47f, 0.375f},
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
    // Individual structure: jaw width, chin, cheekbones, fullness.
    float wMul = 1.0f + f.jaw * 0.12f * bump((s - 0.17f) / 0.17f) + f.jaw * 0.22f * bump(s / 0.10f) +
                 f.cheek * 0.05f * bump((s - 0.37f) / 0.12f) + f.roundness * 0.07f * bump((s - 0.25f) / 0.16f);
    float chinFwd = f.chin * 0.03f * bump(s / 0.14f);
    float chinDrop = f.chin * 0.03f * bump(s / 0.1f);
    float full = f.roundness * 0.02f * bump((s - 0.25f) / 0.15f);
    return {std::max(hW_(s), 0.01f) * h * g.body.headWidth * wMul, (hZc_(s) + chinFwd) * h,
            std::max(hDf_(s) + chinDrop * 0.5f + full, 0.01f) * h, std::max(hDb_(s), 0.01f) * h, hNf_(s), hNb_(s)};
}

// Forward relief of the face in head units; xn is the horizontal position in
// head units before headWidth scaling, s the height above the chin.
float Builder::faceRelief(float xn, float s) const {
    const FaceStyle& f = g.face;
    const float ns = f.noseSize, ax = std::fabs(xn);
    float r = 0;
    // Nose: bridge between the eyes down to the tip, then under to the base.
    if (s > 0.28f && s < 0.55f) {
        const float tip = 0.335f;
        float depth, width;
        if (s > tip) {
            float k = smoothstep(tip, 0.53f, s);
            float bridge = 0.010f + 0.016f * f.noseBridge;
            depth = lerp(0.050f * ns, bridge, k) + 0.006f * f.noseBridge * bump((k - 0.5f) / 0.5f);  // aquiline hump
            width = lerp(0.040f, 0.022f, k) * std::sqrt(ns);
        } else {
            depth = 0.050f * ns * smoothstep(0.295f, tip, s);
            width = 0.044f * std::sqrt(ns);
        }
        r += depth * bump(xn / (width * 1.7f));
        r += 0.014f * ns * bump((s - 0.315f) / 0.022f) * bump((ax - 0.045f * ns) / 0.028f);  // nostril wings
    }
    // Brow ridge and the eye sockets beneath it.
    r += (0.006f + 0.020f * f.browRidge) * bump((s - 0.555f) / 0.045f) * bump(xn / 0.34f);
    r -= 0.015f * bump((s - 0.468f) / 0.055f) * bump((ax - 0.165f * f.eyeSpacing) / 0.115f);
    // Cheekbones.
    r += (0.010f + 0.012f * f.cheek) * bump((s - 0.375f) / 0.05f) * bump((ax - 0.235f) / 0.09f);
    // Lips, the dip below them, and the chin.
    const float mw = f.mouthWidth;
    r += (0.003f + 0.004f * f.lipFullness) * bump((s - 0.252f) / 0.024f) * bump(xn / (0.075f * mw));
    r += (0.004f + 0.005f * f.lipFullness) * bump((s - 0.218f) / 0.024f) * bump(xn / (0.068f * mw));
    r -= 0.007f * bump((s - 0.188f) / 0.018f) * bump(xn / 0.07f);
    r += (0.010f + 0.012f * f.chin) * bump((s - 0.09f) / 0.06f) * bump(xn / 0.11f);
    return r;
}

// Uniform phi -> angle, compressing samples around the face centre (theta = pi/2).
float Builder::headWarp(float phi) const {
    float d = std::remainder(phi - kPi / 2, kTau);  // -pi..pi
    float a = std::fabs(d) / kPi;
    return kPi / 2 + std::copysign(kPi * std::pow(a, 1.45f), d);
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
    return p;
}

void Builder::buildHead() {
    const int head = ch.rig.head;
    const float h = m.h;
    Loft L;
    const float ss[] = {0.0f, 0.05f, 0.10f, 0.15f, 0.19f, 0.225f, 0.26f, 0.295f, 0.32f, 0.345f, 0.37f,
                        0.40f, 0.44f, 0.48f, 0.52f, 0.56f, 0.61f, 0.68f, 0.76f, 0.84f, 0.91f, 0.96f};
    const int N = lod.headSeg;
    int i = 0;
    for (float s : ss) {
        if (lod.sparseRings && (i++ % 2) && s < 0.9f) continue;
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
    hp.smoothAlways = true;  // painted features need clean shading
    headPartId_ = hp.id;
    headPart_ = parts.size() - 1;
    // Mild forward bend of the face normals: keeps hard shadows off the eyes
    // while the nose and brow still read.
    const Vec3 o = m.headOrigin;
    hp.normalFn = [=](Vec3 p, Vec3 n) {
        float s = (p.y - o.y) / h;
        float front = smoothstep(0.0f, 0.3f * h, p.z - o.z) * bump((s - 0.45f) / 0.4f);
        return normalize(lerp(n, normalize(Vec3{n.x * 0.5f, n.y * 0.6f + 0.1f, 1.0f}), 0.3f * front));
    };
    emitLoft(hp, L, [=](float, float, Vec3) { return one(head); }, [&](float, float) { return skin(); });
}

// The 2D face lives on two decal patches that hug the sculpted front of the
// head. Their UVs point at the Neutral cells; expressions are UV offsets.
void Builder::buildFaceDecals() {
    const int head = ch.rig.head;
    const float h = m.h, eps = 0.0050f * h;
    auto patch = [&](SubmeshKind kind, float x0, float x1, float s0, float s1, int nx, int ny, int cellX, int cellY,
                     int cellW, int cellH) {
        Part& p = newPart(false, kind);
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                float fu = float(i) / nx, fv = float(j) / ny;
                float xf = lerp(x0, x1, fu), s = lerp(s0, s1, fv);
                SuperRing r = headRing(s);
                float x = xf * h * g.body.headWidth;
                float z = r.frontZ(std::min(std::fabs(x), r.w * 0.995f)) + faceRelief(xf, s) * h + eps;
                p.pos.push_back(m.headOrigin + Vec3{x, s * h, z});
                p.w.push_back(one(head));
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
    // Dense enough to follow the nose bridge and the sockets.
    size_t eyes = patch(SubmeshKind::FaceEyes, FaceLayout::kEyeX0, FaceLayout::kEyeX1, FaceLayout::kEyeS0, FaceLayout::kEyeS1,
                        lod.headSeg, std::max(4, lod.headSeg / 3), ec[0], ec[1], FaceLayout::kEyeCellW, FaceLayout::kEyeCellH);
    size_t mouth = patch(SubmeshKind::FaceMouth, FaceLayout::kMouthX0, FaceLayout::kMouthX1, FaceLayout::kMouthS0,
                         FaceLayout::kMouthS1, std::max(6, lod.headSeg / 2), 8, mc[0], mc[1], FaceLayout::kMouthCellW,
                         FaceLayout::kMouthCellH);
    parts[eyes].id = parts[mouth].id = headPartId_;
}

}  // namespace cg::detail
