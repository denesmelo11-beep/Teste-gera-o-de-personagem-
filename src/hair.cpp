// Head accessories: hair (a static scalp cap for volume + thick rounded locks,
// each on its own physics chain), facial hair, horns and ears.
#include <algorithm>

#include "generator_impl.hpp"

namespace cg::detail {

namespace {

struct LockSpec {
    float theta, s;          // root on the scalp
    Vec3 dir;                // initial growth direction
    float length, width, thick;
    float gravity = 1.0f;    // styling: pull towards -Y while growing
    float stiffness = 0.55f; // styling: how much the lock keeps its direction
    float curl = 0.1f;       // bend towards the head along the length (+) or away (-)
    float tipWidth = 0.55f;  // width kept at the tip: blunt, rounded ends
    float wave = 0.0f;       // sideways waves
    int sides = 8;
    bool tube = false;       // round tube (ponytail / braid) instead of a lens-shaped slab
    bool braid = false;
    bool faceFront = false;  // flat side faces forward (face-framing locks)
    int bones = 3;
    // Runtime physics.
    float pStiff = 1.0f, pDrag = 0.4f, pGrav = 0.05f;
};

Vec3 thetaDir(float theta) { return {std::cos(theta), 0, std::sin(theta)}; }

}  // namespace

void Builder::buildHair() {
    const HairStyle style = g.hair;
    const float h = m.h, vol = g.hairVolume, len = g.hairLength, deg = kPi / 180.0f;
    const RigInfo& rig = ch.rig;
    const int head = rig.head;
    const Vec3 hc = headCenter();
    Rng r = Rng(g.seed).fork(55);
    const Vec3 c0 = g.palette.hair, c1 = g.palette.hairTip;
    const Mat hairMat{c0, MaterialClass::Hair};

    const bool tied = style == HairStyle::Ponytail || style == HairStyle::HighPonytail || style == HairStyle::Braid ||
                      style == HairStyle::Bun || style == HairStyle::ManBun;
    const bool elderBald = style == HairStyle::Bald && g.age > 45;
    if (style == HairStyle::Bald && !elderBald) return;

    // ---- scalp cap ----
    struct CapSpec {
        float front, temple, side, back, nape;  // hairline height per direction
        float thick, top;                       // thickness, highest covered s (1 = crown)
    };
    // Short hair is silhouette: the cap itself carries the cut's volume.
    const float part0 = g.partSide;
    auto volumeField = [&](float th, float s) -> float {
        const float dTop = bump((s - 0.88f) / 0.28f);
        const float phi = std::remainder(th - kPi / 2, kTau);  // signed angle from the front
        switch (style) {
            case HairStyle::SidePart: {
                float away = smoothstep(0.0f, 1.2f, -phi * part0);  // side opposite the part gets the sweep
                float groove = bump((phi - part0 * 0.45f) / 0.12f) * bump((s - 0.85f) / 0.15f);
                return h * (0.035f * vol * dTop * (0.5f + 0.8f * away) + 0.02f * bump((s - 0.80f) / 0.1f) * bump(phi / 0.9f) -
                            0.015f * groove);
            }
            case HairStyle::ShortCrop: return h * 0.016f * dTop;
            case HairStyle::SlickedBack:
                return h * (0.022f * bump((s - 0.83f) / 0.08f) * bump(phi / 1.0f) + 0.022f * bump((s - 0.80f) / 0.25f) * bump((std::fabs(phi) - kPi) / 1.4f));
            case HairStyle::Messy:
                return h * vol * (0.02f * dTop + 0.022f * dTop * (0.5f + 0.5f * std::sin(th * 5.0f + s * 11.0f)));
            case HairStyle::Pixie: return h * vol * 0.03f * dTop * (0.6f + 0.6f * smoothstep(0.0f, 1.0f, -phi * part0));
            case HairStyle::Undercut: return h * vol * 0.05f * bump((s - 0.92f) / 0.22f) * (0.6f + 0.6f * smoothstep(-0.5f, 1.0f, -phi * part0));
            default: return 0.0f;
        }
    };
    auto cap = [&](CapSpec cs, float crownVolume) {
        Curve line{{0.0f, cs.front}, {0.8f, cs.temple}, {1.57f, cs.side}, {2.3f, cs.back}, {kPi, cs.nape}};
        const int cols = lod.capCols, rows = lod.capRows;
        const bool closed = cs.top >= 0.999f;
        Part& p = newPart();
        p.smoothAlways = true;
        for (int i = 0; i <= rows; ++i)
            for (int j = 0; j < cols; ++j) {
                float th = kTau * j / cols;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                float f = float(i) / (closed ? rows + 1 : rows);
                float s = lerp(line(phi), cs.top, std::pow(f, 0.85f));
                float off = cs.thick * (0.35f + 0.65f * smoothstep(0.0f, 0.3f, f)) + crownVolume * h * bump((s - 0.86f) / 0.22f) +
                            (cs.thick > 0.03f * h ? volumeField(th, s) * smoothstep(0.0f, 0.35f, f) : 0.0f);
                p.pos.push_back(headSurface(th, std::min(s, 0.985f), off));
                p.w.push_back(one(head));
                p.uv.push_back({});
            }
        auto idx = [&](int i, int j) { return uint32_t(i * cols + (j % cols)); };
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                uint32_t a = idx(i, j), b = idx(i + 1, j), c = idx(i + 1, j + 1), d = idx(i, j + 1);
                p.tris.push_back({a, b, c, hairMat});
                p.tris.push_back({a, c, d, hairMat});
            }
        if (closed) {
            uint32_t pole = uint32_t(p.pos.size());
            p.pos.push_back(m.headOrigin + Vec3{0, h + cs.thick + crownVolume * h, hZc_(1.0f) * h});
            p.w.push_back(one(head));
            p.uv.push_back({});
            for (int j = 0; j < cols; ++j) p.tris.push_back({pole, idx(rows, j + 1), idx(rows, j), hairMat});
        }
    };
    const float capT = 0.055f * vol * h;
    const CapSpec shortCap{0.80f, 0.66f, 0.47f, 0.36f, 0.28f, capT * 0.9f, 1.0f};
    const CapSpec longCap{0.80f, 0.66f, 0.50f, 0.32f, 0.24f, capT, 1.0f};
    const CapSpec tightCap{0.80f, 0.66f, 0.48f, 0.34f, 0.26f, 0.032f * h, 1.0f};
    switch (style) {
        case HairStyle::Bald: cap({0.62f, 0.60f, 0.42f, 0.30f, 0.26f, 0.025f * h, 0.60f}, 0); break;  // horseshoe (sides/back only)
        case HairStyle::Buzz: cap({0.80f, 0.66f, 0.47f, 0.36f, 0.28f, 0.016f * h, 1.0f}, 0); break;
        case HairStyle::Undercut:
            cap({0.80f, 0.66f, 0.47f, 0.36f, 0.28f, 0.013f * h, 1.0f}, 0);       // shaved sides
            cap({0.80f, 0.74f, 0.74f, 0.70f, 0.66f, capT * 1.1f, 1.0f}, 0.0f);  // full top
            break;
        default:
            if (tied) cap(tightCap, 0.01f);
            else if (style <= HairStyle::Pixie) cap(shortCap, 0.025f * vol);
            else cap(longCap, 0.03f * vol);
            break;
    }

    // ---- locks ----
    std::vector<LockSpec> locks;
    const Vec3 down{0, -1, 0}, fwd{0, 0, 1}, back{0, 0, -1};
    const float part = g.partSide;
    auto add = [&](LockSpec L) { locks.push_back(L); };
    auto fringe = [&](float lenMul) {
        switch (g.fringe) {
            case Fringe::Curtain:
                for (float sd : {1.0f, -1.0f})
                    for (int i = 0; i < 2; ++i) {
                        LockSpec L{(90.0f - sd * (7.0f + 20.0f * i)) * deg, 0.94f,
                                   normalize(fwd * 0.5f + Vec3{sd * 0.8f, 0, 0} + down * 0.35f), (0.44f + 0.12f * i) * h * lenMul,
                                   0.28f * h * vol, 0.05f * h};
                        L.gravity = 0.9f; L.curl = 0.2f; L.tipWidth = 0.5f; L.bones = 2; L.pStiff = 2.0f; L.pDrag = 0.55f;
                        add(L);
                    }
                break;
            case Fringe::SideSwept:
                for (int i = 0; i < 3; ++i) {
                    LockSpec L{(90.0f + part * (28.0f - 14.0f * i)) * deg, 0.95f,
                               normalize(fwd * 0.45f + Vec3{-part * 0.85f, 0, 0} + down * 0.25f), (0.46f + 0.07f * i) * h * lenMul,
                               0.32f * h * vol, 0.05f * h};
                    L.gravity = 0.8f; L.curl = 0.2f; L.tipWidth = 0.45f; L.bones = 2; L.pStiff = 2.0f; L.pDrag = 0.55f;
                    add(L);
                }
                break;
            case Fringe::Straight:
                for (int i = 0; i < 5; ++i) {
                    float th = lerp(62.0f, 118.0f, i / 4.0f) * deg;
                    LockSpec L{th, 0.95f, normalize(fwd * 0.8f + down * 0.4f + thetaDir(th) * 0.2f), 0.40f * h * lenMul,
                               0.24f * h * vol, 0.05f * h};
                    L.gravity = 0.9f; L.curl = 0.25f; L.tipWidth = 0.75f; L.bones = 2; L.pStiff = 2.5f; L.pDrag = 0.6f;
                    add(L);
                }
                break;
            default: break;
        }
    };
    // Locks hanging all around the head (bob, shoulder length, messy).
    auto around = [&](int n, float length, float sRoot, float width, float curl, bool frontGap) {
        for (int i = 0; i < n; ++i) {
            float th = kPi / 2 + kTau * (i + 0.5f) / n;
            float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
            if (frontGap && phi < 0.75f) continue;  // keep the face clear
            Vec3 dir = normalize(down + thetaDir(th) * 0.22f);
            LockSpec L{th, sRoot + 0.06f * bump((phi - kPi) / 1.2f), dir, length * h * r.range(0.92f, 1.08f), width * h * vol, 0.045f * h * vol};
            L.faceFront = phi < 1.2f;
            L.gravity = 1.0f; L.curl = curl; L.tipWidth = 0.55f; L.wave = g.hairWave; L.bones = 3; L.pStiff = 1.6f; L.pDrag = 0.45f;
            add(L);
        }
    };
    // Long back curtain in two staggered layers.
    auto curtain = [&](int n, float length, float width) {
        for (int layer = 0; layer < 2; ++layer)
            for (int i = 0; i < n - layer; ++i) {
                float f = (i + 0.5f + 0.5f * layer) / n;
                // Outer layer wraps from temple to temple: its front locks frame the face.
                float th = layer == 0 ? lerp(148.0f, 392.0f, f) * deg : lerp(195.0f, 345.0f, f) * deg;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                bool front = phi < 1.25f;
                float sRoot = layer == 0 ? (front ? 0.78f : 0.82f) : 0.58f;
                Vec3 dir = normalize(down + thetaDir(th) * (front ? 0.15f : 0.30f) + (front ? fwd * 0.25f : back * 0.25f));
                float lenK = front ? 0.72f : (layer ? 0.92f : 1.0f);
                LockSpec L{th, sRoot, dir, length * h * lenK * r.range(0.95f, 1.05f), width * h * vol * (front ? 0.8f : 1.0f), 0.045f * h * vol};
                L.faceFront = front;
                L.gravity = 1.0f; L.curl = 0.05f; L.tipWidth = 0.55f; L.wave = g.hairWave; L.bones = 4;
                L.pStiff = 0.9f - 0.15f * g.hairWave; L.pDrag = 0.38f; L.pGrav = 0.05f;
                add(L);
            }
    };
    auto sideFrame = [&](float length, float width) {
        for (float sd : {1.0f, -1.0f}) {
            float th = (90.0f - sd * 62.0f) * deg;
            LockSpec L{th, 0.74f, normalize(down + fwd * 0.35f + Vec3{sd * 0.15f, 0, 0}), length * h, width * h * vol, 0.05f * h * vol};
            L.gravity = 1.0f; L.curl = 0.15f; L.tipWidth = 0.5f; L.wave = g.hairWave; L.faceFront = true; L.bones = 3;
            L.pStiff = 1.1f; L.pDrag = 0.4f;
            add(L);
        }
    };
    auto tail = [&](float th, float s, Vec3 dir, float length, float radius, bool braid) {
        LockSpec L{th, s, dir, length * h, radius * 2 * h * vol, radius * 2 * h * vol};
        L.gravity = 1.0f; L.stiffness = 0.45f; L.curl = 0.0f; L.tipWidth = braid ? 0.5f : 0.3f; L.sides = 8; L.tube = true;
        L.braid = braid; L.bones = 4; L.pStiff = braid ? 1.4f : 1.2f; L.pDrag = braid ? 0.25f : 0.3f; L.pGrav = braid ? 0.15f : 0.1f;
        add(L);
    };
    // Static knot of hair (bun) on the head.
    auto bunAt = [&](float th, float s, float radius) {
        Vec3 base = headSurface(th, s, 0.02f * h);
        Vec3 n = headNormalDir(base);
        Ellipsoid e;
        e.center = base + n * (radius * h * 0.65f);
        e.axis = n;
        e.up = {0, 1, 0};
        e.rAxis = radius * h * 0.8f;
        e.rU = e.rV = radius * h;
        e.lat = 6;
        e.lon = lod.lockSeg + 2;
        Part& bp = newPart();
        bp.smoothAlways = true;
        emitEllipsoid(bp, e, [=](float, float, Vec3) { return one(head); }, [=](float, float) { return hairMat; });
    };

    switch (style) {
        case HairStyle::ShortCrop: break;
        case HairStyle::SidePart: fringe(0.75f); break;
        case HairStyle::SlickedBack: break;
        case HairStyle::Messy:
            around(9, 0.40f, 0.78f, 0.36f, 0.1f, true);
            fringe(0.85f);
            break;
        case HairStyle::Undercut: fringe(0.9f); break;
        case HairStyle::Pixie:
            around(7, 0.24f, 0.62f, 0.34f, 0.3f, true);
            fringe(0.85f);
            break;
        case HairStyle::Bob: around(12, 0.66f, 0.82f, 0.46f, 0.40f, true); fringe(1.0f); break;
        case HairStyle::Shoulder: around(12, 1.15f * len, 0.82f, 0.50f, 0.12f, true); fringe(1.0f); break;
        case HairStyle::LongStraight:
        case HairStyle::LongWavy:
            curtain(10, 2.1f * len, 0.50f);
            fringe(1.0f);
            break;
        case HairStyle::Ponytail:
            tail(270 * deg, 0.52f, normalize(back * 0.6f + down), 1.6f * len, 0.13f, false);
            if (g.fringe != Fringe::None) fringe(0.9f);
            break;
        case HairStyle::HighPonytail:
            tail(270 * deg, 0.86f, normalize(back + Vec3{0, 0.4f, 0}), 1.7f * len, 0.14f, false);
            fringe(0.9f);
            break;
        case HairStyle::Braid:
            tail(270 * deg, 0.46f, normalize(back * 0.4f + down), 2.2f * len, 0.11f, true);
            fringe(0.9f);
            break;
        case HairStyle::Bun:
            bunAt(270 * deg, 0.72f, 0.20f * vol);
            sideFrame(0.62f, 0.26f);
            break;
        case HairStyle::HalfUp:
            curtain(9, 1.8f * len, 0.50f);
            tail(270 * deg, 0.84f, normalize(back + Vec3{0, 0.1f, 0}), 0.7f, 0.09f, false);
            fringe(1.0f);
            break;
        case HairStyle::ManBun:
            bunAt(270 * deg, 0.88f, 0.15f * vol);
            break;
        default: break;
    }

    // ---- grow (styling pass with head/body collision), rig and mesh ----
    const int baseSteps = lod.lockSteps;
    for (LockSpec& L : locks) {
        const int steps = L.braid ? baseSteps * 2 : baseSteps;
        float seg = L.length / steps;
        Vec3 p = headSurface(L.theta, L.s, capT * 0.3f);
        Vec3 dir = L.dir;
        std::vector<Vec3> pts{p};
        for (int k = 0; k < steps; ++k) {
            float f = float(k + 1) / steps;
            Vec3 toHead = normalize(hc - p);
            dir = normalize(dir * L.stiffness + Vec3{0, -1, 0} * (L.gravity * (1 - L.stiffness)) + toHead * (L.curl * f));
            Vec3 q = p + dir * seg;
            if (L.wave > 0) {
                Vec3 side = normalize(cross(dir, toHead), {1, 0, 0});
                q += side * (L.wave * 0.035f * h * std::sin(f * kTau * 1.6f + L.theta * 3.0f));
            }
            q = collide(q, L.thick * 0.55f + capT * 0.5f);
            dir = normalize(q - p, dir);
            p = q;
            pts.push_back(p);
        }

        // Bone chain along the lock (equal arc length), parented to the head.
        std::vector<float> arc{0};
        for (size_t k = 1; k < pts.size(); ++k) arc.push_back(arc.back() + length(pts[k] - pts[k - 1]));
        auto at = [&](float a) {
            for (size_t k = 1; k < pts.size(); ++k)
                if (arc[k] >= a) return lerp(pts[k - 1], pts[k], (a - arc[k - 1]) / std::max(1e-6f, arc[k] - arc[k - 1]));
            return pts.back();
        };
        std::vector<int> bones;
        int parent = head;
        const int nb = std::max(1, std::min(L.bones, lod.lockSteps - 1));
        const int chainId = int(ch.rig.hairChains.size());
        for (int b = 0; b < nb; ++b) {
            parent = ch.skeleton.add("hair" + std::to_string(chainId) + "_" + std::to_string(b), parent, at(arc.back() * b / nb));
            bones.push_back(parent);
        }
        ch.rig.hairChains.push_back(bones);
        SpringChain sc;
        sc.name = "hair" + std::to_string(chainId);
        sc.bones = bones;
        sc.tail = pts.back() - ch.skeleton.bones[size_t(bones.back())].restWorld;
        sc.stiffness = L.pStiff;
        sc.drag = L.pDrag;
        sc.gravity = L.pGrav;
        sc.hitRadius = std::min(L.thick, L.width) * 0.45f;
        sc.maxAngle = L.tube ? 2.0f : 1.4f;
        ch.physics.chains.push_back(sc);

        // Mesh: lens-shaped slab (or round tube), smooth, with a rounded tip.
        Loft lf;
        const int n = int(pts.size());
        for (int k = 0; k < n; ++k) {
            float t = float(k) / (n - 1);
            Vec3 tan = normalize(pts[size_t(std::min(k + 1, n - 1))] - pts[size_t(k ? k - 1 : 0)]);
            Vec3 out = L.faceFront ? normalize(Vec3{0.3f * (pts[size_t(k)].x - hc.x) / h, 0, 1}) : normalize(pts[size_t(k)] - hc);
            Vec3 v = normalize(out - tan * dot(out, tan), {0, 0, 1});
            Vec3 u = cross(tan, v);
            float taper = lerp(1.0f, L.tipWidth, smoothstep(0.35f, 1.0f, t));
            float w = L.width * 0.5f * taper * (0.85f + 0.15f * smoothstep(0.0f, 0.15f, t));
            float th = L.thick * 0.5f * (0.6f + 0.4f * taper);
            SuperRing sr = L.tube ? SuperRing{w, 0, th, th, 2, 2} : SuperRing{w, 0, th, th * 0.45f, 2.2f, 2.2f};
            if (L.tube && k == 1) {  // hair tie
                sr.w *= 0.70f;
                sr.dF *= 0.70f;
                sr.dB *= 0.70f;
            }
            if (L.braid && k > 1) {  // braid knots
                float kk = (k % 2) ? 1.12f : 0.84f;
                sr.w *= kk;
                sr.dF *= kk;
                sr.dB *= kk;
            }
            lf.rings.push_back(makeRing(pts[size_t(k)], u, v, sr, lod.lockSeg));
            lf.t.push_back(t);
        }
        lf.poleStart = true;
        lf.startPole = pts[0] - normalize(pts[1] - pts[0]) * (0.4f * L.thick);
        lf.tStart = -0.05f;
        lf.poleEnd = true;
        lf.endPole = pts.back() + normalize(pts.back() - pts[size_t(n - 2)]) * (L.width * 0.22f * L.tipWidth);
        lf.tEnd = 1.02f;
        const bool tube = L.tube;
        const float tieT = 1.0f / (n - 1);
        const Vec3 tieCol = lerp(c0 * 0.3f, Vec3{0.12f, 0.08f, 0.07f}, 0.5f);
        Part& lp = newPart();
        lp.smoothAlways = true;  // hair keeps smooth shading and its sheen in either style
        emitLoft(lp, lf, [=](float t, float, Vec3) {
            float f = saturate(t) * nb;
            int i0 = std::clamp(int(f), 0, nb - 1);
            if (i0 >= nb - 1) return one(bones[size_t(nb - 1)]);
            return blend(bones[size_t(i0)], bones[size_t(i0 + 1)], smoothstep(0.0f, 1.0f, f - i0));
        }, [=](float t, float) {
            if (tube && t > 0.0f && t < tieT * 1.5f) return Mat{tieCol, MaterialClass::Leather};
            return Mat{lerp(c0, c1, smoothstep(0.4f, 1.0f, t)), MaterialClass::Hair};
        });
    }
}

// Beards and moustaches: shells over the jaw following the sculpted face.
void Builder::buildFacialHair() {
    const FacialHair fh = g.facialHair;
    if (fh == FacialHair::None) return;
    const float h = m.h;
    const int head = ch.rig.head;
    const Mat mat{g.palette.hair * 0.92f, MaterialClass::Hair};
    auto mask = [&](float th, float s) -> float {
        float ax = std::fabs(std::cos(th)) * 0.38f;  // approximate horizontal distance from the centre line
        bool mouth = ax < 0.075f * g.face.mouthWidth && s > 0.19f && s < 0.268f;
        bool stache = ax < 0.09f && s >= 0.255f && s < 0.305f && std::sin(th) > 0;
        switch (fh) {
            case FacialHair::Mustache: return stache ? 1.0f : 0.0f;
            case FacialHair::Goatee: return ((!mouth && ax < 0.13f && s < 0.215f && std::sin(th) > 0.2f) || stache) ? 1.0f : 0.0f;
            case FacialHair::ShortBeard:
            case FacialHair::FullBeard: {
                bool jaw = s < 0.29f - 0.08f * std::max(0.0f, std::sin(th)) && std::sin(th) > -0.15f;
                bool cheekClear = s > 0.27f && ax < 0.20f && !stache;
                return ((jaw && !mouth && !cheekClear) || stache) ? 1.0f : 0.0f;
            }
            default: return 0.0f;
        }
    };
    const float thick = fh == FacialHair::FullBeard ? 0.035f * h : fh == FacialHair::ShortBeard ? 0.016f * h : 0.020f * h;
    const float drop = fh == FacialHair::FullBeard ? 0.12f * h * g.hairLength : 0.0f;
    const int cols = 22, rows = 14;
    Part& p = newPart();
    p.smoothAlways = true;
    std::vector<int> vid(size_t((cols + 1) * (rows + 1)), -1);
    auto vert = [&](int i, int j) {
        int& id = vid[size_t(j * (cols + 1) + i)];
        if (id >= 0) return uint32_t(id);
        float th = lerp(-0.25f, kPi + 0.25f, float(i) / cols), s = lerp(-0.02f, 0.34f, float(j) / rows);
        Vec3 q = headPoint(th, std::max(s, 0.0f));
        if (s < 0.0f) q.y += s * h;
        float chinK = bump((s - 0.04f) / 0.2f) * std::max(0.0f, std::sin(th));
        Vec3 n = normalize(headNormalDir(q) + Vec3{0, -0.6f * chinK, 0});
        q += n * (thick * (0.6f + 0.4f * chinK)) + Vec3{0, -drop * chinK, 0.3f * drop * chinK};
        id = int(p.pos.size());
        p.pos.push_back(q);
        p.w.push_back(one(head));
        p.uv.push_back({});
        return uint32_t(id);
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            float th = lerp(-0.25f, kPi + 0.25f, (i + 0.5f) / cols), s = lerp(-0.02f, 0.34f, (j + 0.5f) / rows);
            if (mask(th, std::max(s, 0.0f)) < 0.5f) continue;
            uint32_t a = vert(i, j), b = vert(i, j + 1), c = vert(i + 1, j + 1), d = vert(i + 1, j);
            p.tris.push_back({a, b, c, mat});
            p.tris.push_back({a, c, d, mat});
        }
}

void Builder::buildHorns() {
    if (g.body.horns == 0) return;
    const float h = m.h, size = g.body.hornSize;
    const int head = ch.rig.head;
    Rng r = Rng(g.seed).fork(57);
    const int kind = r.rangeInt(0, 2);  // 0 swept back, 1 curled ram, 2 straight up
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
            L.rings.push_back(makeRing(pts[size_t(k)], u, v, SuperRing{rr, 0, rr, rr}, 8));
            L.t.push_back(t);
        }
        L.poleEnd = true;
        L.endPole = pts.back();
        Vec3 col = g.palette.horn;
        emitLoft(newPart(), L, [=](float, float, Vec3) { return one(head); },
                 [=](float t, float) { return Mat{col * (1.0f - 0.25f * t), MaterialClass::Horn}; });
    }
}

void Builder::buildEars() {
    const float h = m.h;
    const int head = ch.rig.head;
    const bool elf = g.body.ears == EarStyle::Elf;
    for (float sd : {1.0f, -1.0f}) {
        float th = sd > 0 ? 0.08f : kPi - 0.08f;
        Vec3 base = headSurface(th, 0.45f, -0.03f * h) + Vec3{0, 0, -0.05f * h};
        Vec3 dir = elf ? normalize(Vec3{sd * 1.0f, 0.40f, -0.40f}) : normalize(Vec3{sd * 1.0f, 0.15f, -0.30f});
        float len = elf ? 0.40f * h : 0.11f * h;
        Vec3 v = normalize(Vec3{0, 1, 0} - dir * dir.y), u = cross(dir, v);
        Loft L;
        float rr = elf ? 0.075f * h : 0.085f * h;
        L.rings.push_back(makeRing(base, u, v, SuperRing{0.03f * h, 0, rr, rr, 2, 2}, 8));
        L.t.push_back(0);
        L.rings.push_back(makeRing(base + dir * (len * 0.45f), u, v, SuperRing{0.022f * h, 0, rr * 0.9f, rr * 0.75f, 2, 2}, 8));
        L.t.push_back(0.5f);
        L.poleEnd = true;
        L.endPole = base + dir * len;
        L.tEnd = 1;
        Part& p = newPart();
        p.smoothAlways = true;
        emitLoft(p, L, [=](float, float, Vec3) { return one(head); }, [&](float, float) { return skin(); });
    }
}

}  // namespace cg::detail
