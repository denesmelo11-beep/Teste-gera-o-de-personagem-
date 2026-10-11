// Head accessories: anime hair (cap + chunky locks that collide with the
// head/body), horns, ears, beards.
#include <algorithm>

#include "generator_impl.hpp"

namespace cg::detail {

namespace {

// A chunk of hair grown from the scalp: a flat slab (bangs, side locks,
// spikes) or a round tube (ponytails), collided against head and body.
struct LockSpec {
    float theta, s;    // root on the scalp
    Vec3 dir;          // initial growth direction (character space)
    float length, width, thick;
    float gravity = 1.0f;   // pull towards -Y
    float stiffness = 0.6f;
    float curl = 0.0f;      // bend towards the head (+) or away (-) along the length
    float tipWidth = 0.35f; // width fraction kept at the tip (blunt chunks, not needles)
    int sides = 6;          // 6 = flat slab, 8 = round tube
    bool tube = false;
    bool faceFront = false;  // flat side faces forward (face-framing locks)
    int chain = -1;         // hair bone chain index, -1 = head
};

Vec3 thetaDir(float theta) { return {std::cos(theta), 0, std::sin(theta)}; }

}  // namespace

void Builder::buildHair() {
    const float h = m.h, vol = g.hairVolume, len = g.hairLength;
    const RigInfo& rig = ch.rig;
    Rng r = Rng(g.seed).fork(55);
    const HairStyle style = g.hair;
    const bool shellStyle = style == HairStyle::Bob || style == HairStyle::Long || style == HairStyle::Hime;
    const float thick = (style == HairStyle::Buzz ? 0.022f : 0.075f * vol) * h;
    const Vec3 hc = headCenter();
    const float deg = kPi / 180.0f;
    const int head = rig.head, chest = rig.chest;
    const float neckY = m.neckBase.y;
    const Vec3 c0 = g.palette.hair, c1 = g.palette.hairTip;
    // Hair below the neck rides the chest so it stays on the back.
    auto hairWeight = [=](Vec3 p) { return blend(head, chest, saturate((neckY - p.y) / (0.8f * h))); };

    // ---- tight cap for styles without a full shell ----
    if (!shellStyle && style != HairStyle::Mohawk) {
        Curve line{{0.0f, style == HairStyle::Buzz ? 0.84f : 0.80f}, {0.8f, 0.64f}, {1.57f, 0.47f},
                   {2.3f, 0.33f}, {kPi, style == HairStyle::Buzz ? 0.30f : 0.22f}};
        const int cols = lod.capCols, rows = lod.capRows;
        Part& p = newPart();
        for (int i = 0; i <= rows; ++i)
            for (int j = 0; j < cols; ++j) {
                float th = kTau * j / cols;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));  // 0 front .. pi back
                float f = float(i) / (rows + 1);
                float s = lerp(line(phi), 1.0f, std::pow(f, 0.85f));
                float off = thick * (0.45f + 0.55f * smoothstep(0.0f, 0.35f, f));
                p.pos.push_back(headSurface(th, s, off));
                p.w.push_back(one(head));
                p.uv.push_back({});
            }
        uint32_t pole = uint32_t(p.pos.size());
        p.pos.push_back(m.headOrigin + Vec3{0, h + thick, hZc_(1.0f) * h});
        p.w.push_back(one(head));
        p.uv.push_back({});
        Mat hm{c0, MaterialClass::Hair};
        auto idx = [&](int i, int j) { return uint32_t(i * cols + (j % cols)); };
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                uint32_t a = idx(i, j), b = idx(i + 1, j), c = idx(i + 1, j + 1), d = idx(i, j + 1);
                p.tris.push_back({a, b, c, hm});
                p.tris.push_back({a, c, d, hm});
            }
        for (int j = 0; j < cols; ++j) p.tris.push_back({pole, idx(rows, j + 1), idx(rows, j), hm});
    }

    // ---- shell: one mass wrapping the skull that falls as a slab behind the back ----
    if (shellStyle) {
        const int N = lod.headSeg <= 12 ? 12 : 16;
        float endY;
        if (style == HairStyle::Bob) endY = m.headOrigin.y + 0.08f * h;
        else endY = lerp(m.waistY, m.chestY, 0.35f) + (1.0f - len) * 0.8f * h;
        const bool jagged = style != HairStyle::Hime;
        std::vector<float> ys;
        for (float s : {0.93f, 0.84f, 0.74f, 0.64f, 0.54f, 0.44f, 0.34f, 0.24f, 0.14f}) {
            float y = m.headOrigin.y + s * h;
            if (y > endY + 0.05f * h) ys.push_back(y);
        }
        for (float y = ys.back() - 0.32f * h; y > endY + 0.1f * h; y -= 0.32f * h) ys.push_back(y);
        ys.push_back(endY);
        Loft L;
        const float slabW = 0.60f * h * g.body.headWidth * vol;
        for (size_t k = 0; k < ys.size(); ++k) {
            const float y = ys[k], s = (y - m.headOrigin.y) / h;
            const float u = float(k) / float(ys.size() - 1);  // 0 crown .. 1 tips
            const float hang = smoothstep(0.42f, -0.25f, s);  // 0 on the skull, 1 hanging free
            // Face opening (forehead, eyes, cheeks) as an angular half-width.
            const float faceHalf = s > 0.86f ? 0.0f : lerp(64.0f, 76.0f, smoothstep(0.5f, 0.2f, s)) * deg * smoothstep(0.86f, 0.76f, s);
            std::vector<Vec3> ring(static_cast<size_t>(N));
            for (int j = 0; j < N; ++j) {
                float th = kTau * j / N;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                float sh = clamp(s, phi > 1.2f ? 0.34f : 0.18f, 0.98f);  // below the skull, follow the nape not the jaw
                Vec3 wrap;
                if (phi < faceHalf) wrap = headSurface(th, sh, -0.03f * h);  // tucked inside the head
                else {
                    float edge = smoothstep(faceHalf, faceHalf + 0.6f, phi);
                    wrap = headSurface(th, sh, thick * (0.55f + 0.45f * edge) + 0.04f * h * (1 - s) * edge);
                }
                // Free-hanging slab behind neck / back.
                float t = torsoT(y);
                float backZ = t < 1.0f ? torsoRing(t).backZ(0) - gluteDepth(t) * 0.0f : m.neckBase.z - g.body.neckThick * h;
                backZ = std::min(backZ, m.neckBase.z - g.body.neckThick * h);
                float w = slabW * (1.0f - 0.3f * u * u);
                float c = std::cos(th), sn = std::sin(th);
                float zc = backZ - 0.05f * h - 0.06f * h * vol;
                Vec3 slab{w * spow(c, 0.8f), y, zc + (sn < 0 ? 0.15f * h * vol * spow(sn, 0.8f) : 0.04f * h * sn)};
                Vec3 p = lerp(wrap, slab, hang);
                if (phi >= faceHalf) p = collide(p, 0.025f * h);
                if (k + 1 == ys.size() && jagged) {
                    float v = 0.18f * h * (1 - std::fabs(c));        // V-shaped tips, longest at the back
                    p.y -= (j % 2 ? 0.10f * h : 0.0f) + v * (style == HairStyle::Bob ? 0.3f : 1.0f);
                }
                if (style == HairStyle::Bob && k + 1 == ys.size()) p = p + normalize(Vec3{p.x, 0, p.z - hc.z}) * (0.04f * h);
                ring[size_t(j)] = p;
            }
            L.rings.push_back(ring);
            L.t.push_back(u);
        }
        // Rings run downward: reverse for outward winding.
        for (auto& ring : L.rings) std::reverse(ring.begin() + 1, ring.end());
        L.poleStart = true;
        L.startPole = m.headOrigin + Vec3{0, h + thick, hZc_(1.0f) * h};
        L.tStart = 0;
        L.poleEnd = true;
        L.endPole = Vec3{0, endY + 0.1f * h, L.rings.back()[0].z * 0.0f + (m.neckBase.z - 0.1f * h)};
        L.tEnd = 1;
        Part& sp = newPart();
        emitLoft(sp, L, [=](float, float, Vec3 p) { return hairWeight(p); },
                 [=](float t, float) { return Mat{lerp(c0, c1, smoothstep(0.4f, 1.0f, t)), MaterialClass::Hair}; });
    }

    // ---- chunks per style ----
    std::vector<LockSpec> locks;
    const Vec3 down{0, -1, 0}, fwd{0, 0, 1};
    auto bangs = [&](int n, float length, bool blunt) {
        for (int i = 0; i < n; ++i) {
            float f = n == 1 ? 0.5f : float(i) / (n - 1);
            float th = lerp(52.0f, 128.0f, f) * deg;
            float l = length * (blunt ? 1.0f : r.range(0.85f, 1.12f));
            Vec3 dir = normalize(fwd * 0.8f + down * 0.3f + thetaDir(th) * 0.3f);
            LockSpec L{th, 0.97f, dir, l, 0.30f * h * vol, 0.08f * h, blunt ? 0.6f : 1.0f, 0.5f, 0.15f};
            L.tipWidth = blunt ? 0.9f : 0.4f;
            locks.push_back(L);
        }
    };
    auto sideLocks = [&](float length, float width) {
        for (float sd : {1.0f, -1.0f}) {
            float th = (sd > 0 ? 28.0f : 152.0f) * deg;
            LockSpec L{th, 0.84f, normalize(down + thetaDir(th) * 0.3f + fwd * 0.3f), length, width * h * vol, 0.08f * h, 1.0f, 0.4f, 0.22f};
            L.tipWidth = 0.45f;
            L.faceFront = true;
            locks.push_back(L);
        }
    };
    auto spikes = [&](int n, float length) {
        for (int i = 0; i < n; ++i) {
            float th = kTau * (i + r.range(-0.2f, 0.2f)) / n + kPi / 2 + kPi / n;
            float s = r.range(0.74f, 0.92f);
            Vec3 out = normalize(headSurface(th, s, 0) - hc);
            Vec3 dir = normalize(out + Vec3{0, 0.5f, 0} + Vec3{0, 0, -0.45f});
            float l = length * r.range(0.8f, 1.15f) * (std::sin(th) > 0.5f ? 0.7f : 1.0f);
            LockSpec L{th, s, dir, l, 0.30f * h * vol, 0.12f * h, 0.15f, 0.85f, -0.1f};
            L.tipWidth = 0.12f;
            L.sides = 4;
            locks.push_back(L);
        }
    };
    auto tail = [&](float th, float s, Vec3 dir, float length, int chain) {
        LockSpec L{th, s, dir, length, 0.30f * h * vol, 0.30f * h * vol, 1.0f, 0.45f, 0.0f};
        L.tipWidth = 0.25f;
        L.sides = 8;
        L.tube = true;
        L.chain = chain;
        locks.push_back(L);
    };

    switch (style) {
        case HairStyle::Spiky:
            spikes(8, 0.42f * h * vol);
            bangs(3, 0.40f * h, false);
            break;
        case HairStyle::Bob:
            bangs(4, 0.46f * h, false);
            sideLocks(0.62f * h, 0.24f);
            break;
        case HairStyle::Long:
            bangs(4, 0.46f * h, false);
            sideLocks(1.35f * h * len, 0.26f);
            break;
        case HairStyle::Ponytail:
            bangs(4, 0.44f * h, false);
            sideLocks(0.62f * h, 0.20f);
            ch.rig.hairChains.push_back({});
            tail(270 * deg, 0.86f, normalize(Vec3{0, 0.25f, -1}), 1.9f * h * len, 0);
            break;
        case HairStyle::TwinTails:
            bangs(4, 0.44f * h, false);
            ch.rig.hairChains.push_back({});
            ch.rig.hairChains.push_back({});
            tail(325 * deg, 0.86f, normalize(Vec3{0.8f, 0.1f, -0.5f}), 1.7f * h * len, 0);
            tail(215 * deg, 0.86f, normalize(Vec3{-0.8f, 0.1f, -0.5f}), 1.7f * h * len, 1);
            break;
        case HairStyle::Hime:
            bangs(5, 0.44f * h, true);
            sideLocks(0.80f * h, 0.24f);
            break;
        case HairStyle::Mohawk: {
            const int n = 6;
            for (int i = 0; i < n; ++i) {
                float f = float(i) / (n - 1);
                float th = f < 0.4f ? 90 * deg : 270 * deg;
                float s = f < 0.4f ? lerp(0.86f, 0.99f, f / 0.4f) : lerp(0.99f, 0.62f, (f - 0.4f) / 0.6f);
                Vec3 out = normalize(headSurface(th, s, 0) - hc);
                float l = 0.38f * h * vol * (0.7f + 0.45f * bump((f - 0.4f) / 0.7f));
                LockSpec L{th, s, normalize(out + Vec3{0, 0.1f, -0.35f}), l, 0.32f * h, 0.12f * h, 0.1f, 0.9f, -0.1f};
                L.tipWidth = 0.15f;
                L.sides = 4;
                locks.push_back(L);
            }
            break;
        }
        case HairStyle::Buzz:
        default: break;
    }

    // ---- grow ----
    const int steps = lod.lockSteps;
    std::vector<std::vector<Vec3>> grown(locks.size());
    for (size_t li = 0; li < locks.size(); ++li) {
        const LockSpec& L = locks[li];
        float seg = L.length / steps;
        Vec3 p = headSurface(L.theta, L.s, thick * 0.4f);
        Vec3 dir = L.dir;
        auto& pts = grown[li];
        pts.push_back(p);
        for (int k = 0; k < steps; ++k) {
            float f = float(k + 1) / steps;
            Vec3 toHead = normalize(hc - p);
            dir = normalize(dir * L.stiffness + Vec3{0, -1, 0} * (L.gravity * (1 - L.stiffness)) + toHead * (L.curl * f));
            Vec3 q = collide(p + dir * seg, L.thick * 0.55f + thick * 0.6f);
            dir = normalize(q - p, dir);
            p = q;
            pts.push_back(p);
        }
    }

    // Ponytail bone chains along the tube centre lines.
    const int chainBones = 3;
    for (size_t li = 0; li < locks.size(); ++li) {
        if (locks[li].chain < 0) continue;
        auto& chain = ch.rig.hairChains[size_t(locks[li].chain)];
        int parent = head;
        for (int b = 0; b < chainBones; ++b) {
            float f = float(b) / chainBones * steps;
            int k = int(f);
            Vec3 pos = lerp(grown[li][size_t(k)], grown[li][size_t(std::min(k + 1, steps))], f - k);
            parent = ch.skeleton.add("hair" + std::to_string(locks[li].chain) + "_" + std::to_string(b), parent, pos);
            chain.push_back(parent);
        }
    }

    // ---- emit ----
    const Vec3 tie = accent;
    for (size_t li = 0; li < locks.size(); ++li) {
        const LockSpec& L = locks[li];
        const auto& pts = grown[li];
        Loft lf;
        const int n = int(pts.size());
        for (int k = 0; k < n; ++k) {
            float t = float(k) / (n - 1);
            Vec3 tan = normalize(pts[size_t(std::min(k + 1, n - 1))] - pts[size_t(k ? k - 1 : 0)]);
            Vec3 out = L.faceFront ? normalize(Vec3{0.25f * (pts[size_t(k)].x - hc.x) / h, 0, 1}) : normalize(pts[size_t(k)] - hc);
            Vec3 v = normalize(out - tan * dot(out, tan), {0, 0, 1});
            Vec3 u = cross(tan, v);
            float taper = lerp(1.0f, L.tipWidth, std::pow(t, 1.3f));
            if (L.tube) taper *= 1.0f + 0.25f * std::sin(kPi * std::min(1.0f, t * 1.8f)) - (k == 1 ? 0.0f : 0.0f);
            float w = L.width * 0.5f * taper, th = L.thick * 0.5f * (L.tube ? taper : (0.7f + 0.3f * taper));
            SuperRing sr = L.tube ? SuperRing{w, 0, th, th, 2, 2} : SuperRing{w, 0, th, th * 0.5f, 3.0f, 3.0f};
            if (L.tube && k == 1) {  // hair tie
                sr.w *= 0.72f;
                sr.dF *= 0.72f;
                sr.dB *= 0.72f;
            }
            lf.rings.push_back(makeRing(pts[size_t(k)], u, v, sr, L.sides));
            lf.t.push_back(t);
        }
        lf.poleStart = true;
        lf.startPole = pts[0] - normalize(pts[1] - pts[0]) * (0.4f * L.thick);
        lf.tStart = -0.05f;
        lf.poleEnd = true;
        lf.endPole = pts.back() + normalize(pts.back() - pts[size_t(n - 2)]) * (L.width * 0.25f * L.tipWidth + 0.02f * h);
        lf.tEnd = 1.02f;
        std::vector<int> bones = L.chain >= 0 ? ch.rig.hairChains[size_t(L.chain)] : std::vector<int>{};
        WeightFn wf = [=](float t, float, Vec3 p) {
            if (!bones.empty()) {
                float f = saturate(t) * (bones.size() - 1) * 1.15f;
                int i0 = std::clamp(int(f), 0, int(bones.size()) - 1);
                if (i0 >= int(bones.size()) - 1) return one(bones.back());
                return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], f - i0);
            }
            return hairWeight(p);
        };
        const bool tube = L.tube;
        const float tieT = 1.0f / (n - 1);
        emitLoft(newPart(), lf, wf, [=](float t, float) {
            if (tube && t > 0.0f && t < tieT * 1.5f) return Mat{tie, MaterialClass::Cloth};
            return Mat{lerp(c0, c1, smoothstep(0.35f, 1.0f, t)), MaterialClass::Hair};
        });
    }

    // ---- beard (masculine low-poly heroes) ----
    Rng rb = Rng(g.seed).fork(56);
    if (g.femininity < 0.3f && rb.chance(g.archetype == Archetype::Brute ? 0.6f : 0.3f)) {
        float bl = rb.range(0.15f, 0.55f) * h;  // length below the chin
        Part& p = newPart(true);
        Mat bm{g.palette.hair * 0.92f, MaterialClass::Hair};
        // A chunky wedge hugging the jaw: rings from the sideburns down to the tip.
        Loft L;
        const float ss[] = {0.40f, 0.28f, 0.15f, 0.04f};
        const int N = 10;
        for (float s : ss) {
            SuperRing hr = headRing(s);
            std::vector<Vec3> ring(static_cast<size_t>(N));
            for (int j = 0; j < N; ++j) {
                // Outer arc over the jaw (theta 0..pi), inner arc tucked inside the head.
                bool outer = j <= N / 2;
                float a = kPi * float(outer ? j : N - j) / float(N / 2);
                Vec2 q = hr.point(std::clamp(a, 0.05f, kPi - 0.05f));
                // Keep the mouth free: the front of the upper rings dives into the face.
                float mouthGap = s > 0.12f ? bump((a - kPi / 2) / 0.75f) : 0.0f;
                float k = outer ? 1.12f - 0.2f * mouthGap : 0.85f;
                ring[size_t(j)] = m.headOrigin + Vec3{q.x * k, s * h, hr.zc + (q.y - hr.zc) * k};
            }
            L.rings.push_back(ring);
            L.t.push_back(1 - s / 0.40f);
        }
        // Rings go downward: reverse for outward winding.
        for (auto& ring : L.rings) std::reverse(ring.begin() + 1, ring.end());
        L.poleEnd = true;
        L.endPole = m.headOrigin + Vec3{0, -bl, hZc_(0) * h + 0.02f * h};
        L.tEnd = 1.2f;
        emitLoft(p, L, [=](float, float, Vec3) { return one(head); }, [=](float, float) { return bm; });
    }
}

void Builder::buildHorns() {
    if (g.body.horns == 0) return;
    const float h = m.h, size = g.body.hornSize;
    const int head = ch.rig.head;
    Rng r = Rng(g.seed).fork(57);
    const int kind = r.rangeInt(0, 2);  // 0 swept back, 1 curled ram, 2 straight up
    Mat hm{g.palette.horn, MaterialClass::Horn};
    for (float sd : {1.0f, -1.0f}) {
        float th = sd > 0 ? 62 * kPi / 180 : 118 * kPi / 180;
        Vec3 p = headSurface(th, 0.86f, -0.01f * h);
        Vec3 dir = kind == 2 ? normalize(Vec3{sd * 0.3f, 1, 0.1f}) : normalize(Vec3{sd * 0.5f, 0.7f, -0.3f});
        const int n = 6;
        float seg = 0.55f * h * size / n;
        std::vector<Vec3> pts{p};
        for (int k = 0; k < n; ++k) {
            Quat bend = kind == 1 ? Quat::axisAngle({sd, 0, 0}, -0.55f) : Quat::axisAngle({1, 0, 0}, kind == 0 ? -0.18f : 0.05f);
            dir = normalize(rotate(bend, dir));
            p = p + dir * seg;
            pts.push_back(p);
        }
        Loft L;
        float r0 = 0.09f * h * std::sqrt(size);
        for (int k = 0; k < n; ++k) {
            float t = float(k) / n;
            Vec3 d = normalize(pts[size_t(k + 1)] - pts[size_t(k)]);
            Vec3 v = normalize(cross(d, {1, 0, 0}) * sd, {0, 0, 1});
            Vec3 u = cross(d, v);
            float rr = r0 * (1 - 0.85f * t);
            L.rings.push_back(makeRing(pts[size_t(k)], u, v, SuperRing{rr, 0, rr, rr}, 6));
            L.t.push_back(t);
        }
        L.poleEnd = true;
        L.endPole = pts.back();
        emitLoft(newPart(true), L, [=](float, float, Vec3) { return one(head); },
                 [=](float t, float) { return Mat{hm.color * (1.0f - 0.25f * t), MaterialClass::Horn}; });
    }
}

void Builder::buildEars() {
    const float h = m.h;
    const int head = ch.rig.head;
    const EarStyle es = g.body.ears;
    for (float sd : {1.0f, -1.0f}) {
        if (es == EarStyle::Cat) {
            // Pointed animal ears on top of the head, hair coloured.
            float th = (sd > 0 ? 55.0f : 125.0f) * kPi / 180;
            Vec3 base = headSurface(th, 0.90f, 0.02f * h);
            Vec3 dir = normalize(Vec3{sd * 0.35f, 1.0f, -0.05f});
            Vec3 v = normalize(Vec3{0, 0, 1} - dir * dir.z), u = cross(dir, v);
            Loft L;
            L.rings.push_back(makeRing(base, u, v, SuperRing{0.15f * h, 0, 0.07f * h, 0.03f * h, 1.4f, 1.4f}, 6));
            L.t.push_back(0);
            L.poleEnd = true;
            L.endPole = base + dir * (0.36f * h);
            L.tEnd = 1;
            Vec3 c = g.palette.hair, inner = lerp(g.palette.skin, Vec3{1.0f, 0.6f, 0.7f}, 0.5f);
            emitLoft(newPart(true), L, [=](float, float, Vec3) { return one(head); },
                     [=](float, float th2) { return std::sin(th2) > 0.3f ? Mat{inner, MaterialClass::Skin} : Mat{c, MaterialClass::Hair}; });
            continue;
        }
        // Human / elf ear on the side of the head.
        float th = sd > 0 ? 0.0f : kPi;
        Vec3 base = headSurface(th, 0.44f, -0.02f * h) + Vec3{0, 0, -0.06f * h};
        bool elf = es == EarStyle::Elf;
        Vec3 dir = elf ? normalize(Vec3{sd * 1.0f, 0.45f, -0.45f}) : normalize(Vec3{sd * 1.0f, 0.1f, -0.25f});
        float len = elf ? 0.42f * h : 0.10f * h;
        Vec3 v = normalize(Vec3{0, 1, 0} - dir * dir.y), u = cross(dir, v);
        Loft L;
        float rr = elf ? 0.07f * h : 0.08f * h;
        L.rings.push_back(makeRing(base, u, v, SuperRing{0.025f * h, 0, rr, rr, 2, 2}, 6));
        L.t.push_back(0);
        L.rings.push_back(makeRing(base + dir * (len * 0.45f), u, v, SuperRing{0.02f * h, 0, rr * 0.9f, rr * 0.8f, 2, 2}, 6));
        L.t.push_back(0.5f);
        L.poleEnd = true;
        L.endPole = base + dir * len;
        L.tEnd = 1;
        emitLoft(newPart(), L, [=](float, float, Vec3) { return one(head); }, [&](float, float) { return skin(); });
    }
}

}  // namespace cg::detail
