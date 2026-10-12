// Head accessories: hair (a static scalp cap for volume + thick rounded locks,
// each on its own physics chain), facial hair, horns and ears.
#include <algorithm>
#include <functional>

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
        float tips = 1.0f;                      // lock tips along the rim (0 = clean edge)
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
        const int cols = lod.capCols + (lod.capCols % 2), rows = lod.capRows;
        const bool closed = cs.top >= 0.999f;
        Part& p = newPart();
        p.smoothAlways = true;
        for (int i = 0; i <= rows; ++i)
            for (int j = 0; j < cols; ++j) {
                float th = kTau * j / cols;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                float f = float(i) / (closed ? rows + 1 : rows);
                float s = lerp(line(phi), cs.top, std::pow(f, 0.85f));
                // Hairline and nape end in lock tips: alternate rim columns hang lower.
                if (i == 0 && cs.thick > 0.03f * h && j % 2 == 0)
                    s -= lerp(0.06f, 0.035f, bump((phi - 1.57f) / 0.7f)) * (cs.top >= 0.999f ? 1.0f : 0.0f) * cs.tips;
                float off = cs.thick * (0.20f + 0.80f * smoothstep(0.0f, 0.3f, f)) + crownVolume * h * bump((s - 0.86f) / 0.22f) +
                            (cs.thick > 0.03f * h ? volumeField(th, s) * smoothstep(0.0f, 0.35f, f) : 0.0f);
                // Combed strands: alternate columns sit lower, so the cap reads as locks
                // radiating from the crown (fades out near the crown and on buzz cuts).
                if (cs.thick > 0.03f * h && j % 2 == 1) off -= 0.30f * cs.thick * smoothstep(0.98f, 0.70f, s) * smoothstep(0.0f, 0.15f, f);
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
    // Long cuts: the cap only covers the top; the hair mass wraps the back of the skull from the crown.
    const CapSpec longCap{0.80f, 0.68f, 0.66f, 0.79f, 0.80f, capT, 1.0f, 0.0f};
    const CapSpec tightCap{0.80f, 0.66f, 0.48f, 0.34f, 0.26f, 0.032f * h, 1.0f, 0.5f};
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
            else cap(longCap, 0.012f * vol);
            break;
    }

    // ---- locks ----
    std::vector<LockSpec> locks;
    const Vec3 down{0, -1, 0}, fwd{0, 0, 1}, back{0, 0, -1};
    const float part = g.partSide;
    auto add = [&](LockSpec L) { locks.push_back(L); };
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
        L.braid = braid; L.bones = 4; L.pStiff = braid ? 1.1f : 0.85f; L.pDrag = braid ? 0.25f : 0.28f; L.pGrav = braid ? 0.2f : 0.18f;
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

    // Hair mass for longer cuts: one thick, continuous sheet around the back and
    // sides (volume and a clean silhouette instead of loose strips). Locks are
    // sculpted into it as grooves and soft pointed tips along the hem; a row of
    // spring chains spread across it carries the physics.
    struct SheetSpec {
        float th0, th1;          // angular range, increasing through the back
        float length, thick;
        float sSide, sBack;      // root heights (under the cap)
        float frontLen;          // length factor for the face-framing ends
        float curl, outward;     // hem curls under (bob) / mass stands off the head
        int locks;
        float pStiff, pDrag, pGrav;
        float fwdBias = 0.0f;    // + hangs in front of the shoulders, - falls down the back
        int cols = 0;            // 0: automatic
        std::function<float(float)> lenOf;    // optional length factor across the sheet (0..1)
        std::function<float(float)> sweepOf;  // optional sideways push across the sheet (+x)
        float tipLen = 0.13f;    // pointed tips at the hem (head units)
    };
    auto sheet = [&](const SheetSpec& S) {
        // Odd column count: ridge columns (even) alternate with groove columns (odd),
        // so every lock is a wedge whose two faces catch the light differently.
        int M0 = S.cols > 0 ? S.cols : (lod.sparseRings ? 9 : 13);
        const int M = M0 % 2 ? M0 : M0 + 1, steps = lod.lockSteps + (lod.sparseRings ? 0 : 1);
        std::vector<float> lockLen(static_cast<size_t>(M), 1.0f);
        for (int c = 0; c < M; c += 2) lockLen[size_t(c)] = r.range(0.86f, 1.06f);
        for (int c = 1; c < M; c += 2) lockLen[size_t(c)] = 0.95f * std::min(lockLen[size_t(c - 1)], lockLen[size_t(std::min(c + 1, M - 1))]);
        std::vector<std::vector<Vec3>> cl(static_cast<size_t>(M));
        std::vector<float> colLen(static_cast<size_t>(M));
        for (int c = 0; c < M; ++c) {
            float f = float(c) / float(M - 1), th = lerp(S.th0, S.th1, f);
            float phi = std::fabs(std::remainder(th - kPi / 2, kTau));          // 0 front .. pi back
            float sRoot = lerp(S.sSide, S.sBack, smoothstep(1.2f, kPi, phi));
            float lk = (S.lenOf ? S.lenOf(f) : lerp(S.frontLen, 1.0f, smoothstep(0.9f, 2.0f, phi))) * lockLen[size_t(c)];
            const float sweep = S.sweepOf ? S.sweepOf(f) : 0.0f;
            // Roots sit just under the cap surface so the mass grows out of it seamlessly.
            Vec3 p = headSurface(th, sRoot, std::max(0.004f * h, capT * 0.95f - 0.28f * S.thick));
            Vec3 dir = normalize(Vec3{0, -1, 0} + thetaDir(th) * S.outward + fwd * S.fwdBias + Vec3{sweep, 0, 0} +
                                 (phi < 1.3f ? fwd * 0.12f : back * 0.12f));
            float seg = S.length * h * lk / float(steps);
            std::vector<Vec3> pts{p};
            for (int k = 0; k < steps; ++k) {
                float t = float(k + 1) / float(steps);
                Vec3 toHead = normalize(hc - p);
                dir = normalize(dir * 0.62f + Vec3{0, -1, 0} * 0.38f + toHead * (S.curl * t * t) + fwd * (S.fwdBias * 0.25f) +
                                Vec3{sweep * 0.3f, 0, 0});
                Vec3 q = p + dir * seg;
                if (g.hairWave > 0) {
                    Vec3 side = normalize(cross(dir, toHead), {1, 0, 0});
                    q += side * (g.hairWave * (S.fwdBias > 0 ? 0.012f : 0.035f) * h * std::sin(t * kTau * 1.4f + float(c) * 0.22f));  // coherent waves
                }
                q = collide(q, S.thick * 0.6f + capT * 0.4f);
                // Face-framing locks drape forward over the chest; the body must not
                // push them out sideways into wings.
                if (S.fwdBias > 0 && std::fabs(q.x) > std::fabs(pts[0].x) + 0.04f * h)
                    q.x = std::copysign(std::fabs(pts[0].x) + 0.04f * h, q.x);
                dir = normalize(q - p, dir);
                p = q;
                pts.push_back(p);
            }
            cl[size_t(c)] = pts;
            colLen[size_t(c)] = lk;
        }
        // Physics chains on a subset of columns.
        const int nCh = std::clamp(S.locks / 2 + 1, 3, 7);
        std::vector<std::vector<int>> chains;
        std::vector<int> chainCol;
        const int nb = std::max(2, std::min(4, lod.lockSteps - 1));
        for (int j = 0; j < nCh; ++j) {
            int col = int(std::lround(float(j) * float(M - 1) / float(nCh - 1)));
            const auto& pts = cl[size_t(col)];
            std::vector<float> arc{0};
            for (size_t k = 1; k < pts.size(); ++k) arc.push_back(arc.back() + length(pts[k] - pts[k - 1]));
            auto at = [&](float a) {
                for (size_t k = 1; k < pts.size(); ++k)
                    if (arc[k] >= a) return lerp(pts[k - 1], pts[k], (a - arc[k - 1]) / std::max(1e-6f, arc[k] - arc[k - 1]));
                return pts.back();
            };
            const int chainId = int(ch.rig.hairChains.size());
            std::vector<int> bones;
            int parent = head;
            for (int b = 0; b < nb; ++b) {
                parent = ch.skeleton.add("hair" + std::to_string(chainId) + "_" + std::to_string(b), parent, at(arc.back() * b / nb));
                bones.push_back(parent);
            }
            ch.rig.hairChains.push_back(bones);
            SpringChain sc;
            sc.name = "hair" + std::to_string(chainId);
            sc.bones = bones;
            sc.tail = pts.back() - ch.skeleton.bones[size_t(bones.back())].restWorld;
            sc.stiffness = S.pStiff;
            sc.drag = S.pDrag;
            sc.gravity = S.pGrav;
            sc.hitRadius = S.thick * 0.6f;
            sc.maxAngle = 1.3f;
            ch.physics.chains.push_back(sc);
            chains.push_back(bones);
            chainCol.push_back(col);
        }
        auto weightAt = [&](int c, float t) {
            int j = 0;
            while (j + 2 < nCh && chainCol[size_t(j + 1)] <= c) ++j;
            float fc = saturate(float(c - chainCol[size_t(j)]) / float(std::max(1, chainCol[size_t(j + 1)] - chainCol[size_t(j)])));
            fc = smoothstep(0.0f, 1.0f, fc);
            float fb = saturate(t) * float(nb);
            int i0 = std::clamp(int(fb), 0, nb - 1), i1 = std::min(i0 + 1, nb - 1);
            float fi = smoothstep(0.0f, 1.0f, fb - float(i0));
            SkinW w;
            w.add(chains[size_t(j)][size_t(i0)], (1 - fc) * (1 - fi));
            w.add(chains[size_t(j)][size_t(i1)], (1 - fc) * fi);
            w.add(chains[size_t(j + 1)][size_t(i0)], fc * (1 - fi));
            w.add(chains[size_t(j + 1)][size_t(i1)], fc * fi);
            w.normalize();
            return w;
        };
        // Surfaces: outer (sculpted locks), inner, hem and the two side edges.
        Part& sp = newPart();
        sp.smoothAlways = true;
        sp.cavity = 0.0f;  // locks read from their facets, no dark divider lines
        const int K = steps + 1;
        std::vector<uint32_t> outer(static_cast<size_t>(M * K)), inner(static_cast<size_t>(M * K));
        for (int c = 0; c < M; ++c) {
            const auto& pts = cl[size_t(c)];
            const float lockPhase = c % 2 == 0 ? 1.0f : -1.0f;                    // ridge / groove
            const float lockLift = 0.5f + 0.5f * std::sin(float(c / 2) * 2.7f + float(g.seed % 7));  // some locks sit further out
            for (int k = 0; k < K; ++k) {
                float t = float(k) / float(K - 1);
                Vec3 tan = normalize(pts[size_t(std::min(k + 1, K - 1))] - pts[size_t(std::max(k - 1, 0))]);
                Vec3 out = normalize(pts[size_t(k)] - hc);
                out = normalize(out - tan * dot(out, tan), {0, 0, 1});
                float thk = S.thick * (1.0f - 0.55f * smoothstep(0.5f, 1.0f, t)) * (0.55f + 0.45f * smoothstep(0.0f, 0.12f, t));
                float groove = (lockPhase > 0 ? 0.55f + 0.25f * lockLift : -0.30f) * thk * smoothstep(0.04f, 0.25f, t);
                Vec3 base = pts[size_t(k)];
                // Pointed tips hang with gravity even where the hem rests on the body.
                const Vec3 tipDir = normalize(tan + Vec3{0, -1.2f, 0}, Vec3{0, -1, 0});
                if (k == K - 1) base += tipDir * (S.tipLen * h * colLen[size_t(c)] * (0.5f + 0.5f * lockPhase));
                if (k == K - 2) base += tipDir * (0.3f * S.tipLen * h * colLen[size_t(c)] * (0.5f + 0.5f * lockPhase));
                sp.pos.push_back(base + out * (0.5f * thk + groove));
                sp.w.push_back(weightAt(c, t));
                sp.uv.push_back({});
                outer[size_t(c * K + k)] = uint32_t(sp.pos.size() - 1);
                sp.pos.push_back(base - out * (0.5f * thk));
                sp.w.push_back(weightAt(c, t));
                sp.uv.push_back({});
                inner[size_t(c * K + k)] = uint32_t(sp.pos.size() - 1);
            }
        }
        auto colAt = [&](int k) { return lerp(c0, c1, smoothstep(0.4f, 1.0f, float(k) / float(K - 1))); };
        auto quad = [&](uint32_t a, uint32_t b, uint32_t cc, uint32_t d, Vec3 outDir, Vec3 col) {
            Vec3 n = cross(sp.pos[b] - sp.pos[a], sp.pos[cc] - sp.pos[a]);
            Mat mt{col, MaterialClass::Hair};
            if (dot(n, outDir) < 0) {
                sp.tris.push_back({a, cc, b, mt});
                sp.tris.push_back({a, d, cc, mt});
            } else {
                sp.tris.push_back({a, b, cc, mt});
                sp.tris.push_back({a, cc, d, mt});
            }
        };
        for (int c = 0; c + 1 < M; ++c)
            for (int k = 0; k + 1 < K; ++k) {
                Vec3 mid = (sp.pos[outer[size_t(c * K + k)]] + sp.pos[outer[size_t((c + 1) * K + k + 1)]]) * 0.5f;
                Vec3 od = normalize(mid - hc);
                quad(outer[size_t(c * K + k)], outer[size_t(c * K + k + 1)], outer[size_t((c + 1) * K + k + 1)], outer[size_t((c + 1) * K + k)], od, colAt(k));
                quad(inner[size_t(c * K + k)], inner[size_t(c * K + k + 1)], inner[size_t((c + 1) * K + k + 1)], inner[size_t((c + 1) * K + k)], od * -1.0f, colAt(k) * 0.8f);
            }
        for (int c = 0; c + 1 < M; ++c) {  // hem
            const Vec3 a = sp.pos[outer[size_t(c * K + K - 1)]], b = sp.pos[outer[size_t(c * K + K - 2)]];
            quad(outer[size_t(c * K + K - 1)], inner[size_t(c * K + K - 1)], inner[size_t((c + 1) * K + K - 1)], outer[size_t((c + 1) * K + K - 1)],
                 normalize(a - b), colAt(K - 1));
        }
        for (int e = 0; e < 2; ++e) {  // side edges
            int c = e ? M - 1 : 0, cn = e ? M - 2 : 1;
            for (int k = 0; k + 1 < K; ++k) {
                Vec3 sideDir = normalize(sp.pos[outer[size_t(c * K + k)]] - sp.pos[outer[size_t(cn * K + k)]]);
                quad(outer[size_t(c * K + k)], outer[size_t(c * K + k + 1)], inner[size_t(c * K + k + 1)], inner[size_t(c * K + k)], sideDir, colAt(k));
            }
        }
    };

    // Fringe: a short sheet over the forehead, its locks and tips like the long mass.
    auto fringe = [&](float lenMul) {
        const float L = 0.50f * lenMul;
        auto mk = [&](float a0, float a1, std::function<float(float)> lenOf, std::function<float(float)> sweepOf, int locks, int cols) {
            SheetSpec f{a0, a1, L, 0.042f * h * vol, 0.90f, 0.90f, 1.0f, 0.70f, 0.10f, locks, 2.2f, 0.55f, 0.03f};
            f.fwdBias = 0.45f;
            f.cols = cols;
            f.lenOf = std::move(lenOf);
            f.sweepOf = std::move(sweepOf);
            f.tipLen = 0.035f;  // soft points, never spikes
            sheet(f);
        };
        switch (g.fringe) {
            case Fringe::Straight:
                mk(60 * deg, 120 * deg, [](float x) { return 1.0f - 0.15f * std::fabs(x - 0.5f); }, nullptr, 4, 7);
                break;
            case Fringe::SideSwept: {
                const float sd = part;
                mk(56 * deg, 124 * deg, [sd](float x) { float k = sd > 0 ? 1.0f - x : x; return 0.72f + 0.55f * k; },
                   [sd](float) { return -0.55f * sd; }, 4, 7);
                break;
            }
            case Fringe::Curtain:
                mk(90 * deg, 126 * deg, [](float x) { return 0.85f + 0.40f * x; }, [](float x) { return -0.15f - 0.30f * x; }, 2, 5);
                mk(54 * deg, 90 * deg, [](float x) { return 1.25f - 0.40f * x; }, [](float x) { return 0.45f - 0.30f * x; }, 2, 5);
                break;
            default: break;
        }
    };

    switch (style) {
        case HairStyle::ShortCrop: break;
        case HairStyle::SidePart: break;  // the cap's swept volume and its tipped hairline are the fringe
        case HairStyle::SlickedBack: break;
        case HairStyle::Messy:
            around(9, 0.40f, 0.78f, 0.36f, 0.1f, true);
            fringe(0.85f);
            break;
        case HairStyle::Undercut: break;
        case HairStyle::Pixie:
            around(7, 0.24f, 0.62f, 0.34f, 0.3f, true);
            fringe(0.85f);
            break;
        case HairStyle::Bob:
            sheet({125 * deg, 415 * deg, 0.95f, 0.062f * h * vol, 0.72f, 0.92f, 0.78f, 0.30f, 0.14f, 8, 2.2f, 0.5f, 0.04f});
            fringe(1.0f);
            break;
        case HairStyle::Shoulder:
            sheet({122 * deg, 418 * deg, 1.55f * len, 0.066f * h * vol, 0.72f, 0.92f, 0.72f, 0.12f, 0.10f, 9, 1.4f, 0.45f, 0.05f});
            fringe(1.0f);
            break;
        case HairStyle::LongStraight:
        case HairStyle::LongWavy: {
            const float ps = 0.95f - 0.15f * g.hairWave;
            SheetSpec backMass{168 * deg, 372 * deg, 2.45f * len, 0.078f * h * vol, 0.74f, 0.92f, 0.85f, 0.04f, 0.20f, 8, ps, 0.4f, 0.05f};
            backMass.fwdBias = -0.25f;
            sheet(backMass);
            for (float sd : {1.0f, -1.0f}) {  // face-framing locks over the front of the shoulders
                float a0 = sd > 0 ? 126 * deg : 360 * deg, a1 = sd > 0 ? 180 * deg : 414 * deg;
                SheetSpec front{a0, a1, 1.55f * len, 0.062f * h * vol, 0.70f, 0.70f, 1.0f, 0.02f, 0.06f, 2, ps * 1.2f, 0.45f, 0.05f};
                front.fwdBias = 0.16f;
                front.cols = 4;
                front.tipLen = 0.07f;
                sheet(front);
            }
            fringe(1.0f);
            break;
        }
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
        case HairStyle::HalfUp: {
            SheetSpec backMass{168 * deg, 372 * deg, 2.1f * len, 0.072f * h * vol, 0.74f, 0.92f, 0.85f, 0.04f, 0.20f, 7, 1.0f, 0.4f, 0.05f};
            backMass.fwdBias = -0.25f;
            sheet(backMass);
            for (float sd : {1.0f, -1.0f}) {
                float a0 = sd > 0 ? 128 * deg : 362 * deg, a1 = sd > 0 ? 178 * deg : 412 * deg;
                SheetSpec front{a0, a1, 1.1f * len, 0.058f * h * vol, 0.70f, 0.70f, 1.0f, 0.02f, 0.06f, 2, 1.2f, 0.45f, 0.05f};
                front.fwdBias = 0.14f;
                front.cols = 4;
                front.tipLen = 0.07f;
                sheet(front);
            }
            tail(270 * deg, 0.84f, normalize(back + Vec3{0, 0.1f, 0}), 0.7f, 0.09f, false);
            fringe(1.0f);
            break;
        }
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
    const Mat mat{lerp(g.palette.hair, g.palette.skin * 0.45f, 0.18f), MaterialClass::Hair};  // beards read a touch lighter than scalp hair
    // Signed region in face coordinates (theta around the head, s above the chin),
    // negative inside: jaw below the cheek line, minus the mouth, plus the moustache.
    const float mw = g.face.mouthWidth;
    auto field = [fh, mw](float th, float s) -> float {
        const float sn = std::sin(th), ax = std::fabs(std::cos(th)) * 0.38f;
        auto box = [](float a, float b, float c) { return std::max(a, std::max(b, c)); };
        const float mouth = box(ax - 0.075f * mw, 0.19f - s, s - 0.268f);
        const float stache = box(ax - 0.09f, 0.255f - s, s - 0.305f) + std::max(0.0f, -sn);
        switch (fh) {
            case FacialHair::Mustache: return stache;
            case FacialHair::Goatee: return std::min(std::max(box(ax - 0.13f, s - 0.215f, 0.2f - sn), -mouth), stache);
            case FacialHair::ShortBeard:
            case FacialHair::FullBeard: {
                float jaw = std::max(s - (0.30f - 0.11f * std::max(0.0f, sn)), -0.10f - sn);
                float cheekClear = std::max(0.24f - s, ax - 0.22f);
                return std::min(std::max(std::max(jaw, -cheekClear), -mouth), stache);
            }
            default: return 1.0f;
        }
    };
    auto faceCoords = [this](Vec3 p, float& th, float& s) {
        s = (p.y - m.headOrigin.y) / m.h;
        float zc = hZc_(clamp(s, 0.0f, 1.0f)) * m.h;
        th = std::atan2(p.z - m.headOrigin.z - zc, p.x - m.headOrigin.x);
    };
    // 1) Painted on the head itself with an exact edge (stubble for short beards).
    parts[headPart_].clips.push_back({[=](Vec3 p) {
        float th, s;
        faceCoords(p, th, s);
        return field(th, std::max(s, 0.0f));
    }, mat});
    // 2) Volume for moustache, goatee and beards: a shell whose thickness falls to
    //    zero at the edge, so it grows out of the painted region without a step.
    const float thick = fh == FacialHair::FullBeard ? 0.030f * h : fh == FacialHair::ShortBeard ? 0.008f * h : 0.014f * h;
    const float drop = fh == FacialHair::FullBeard ? 0.10f * h * g.hairLength : 0.0f;
    const int cols = 30, rows = 18;
    Part& p = newPart();
    p.smoothAlways = true;
    p.cavity = 0.8f;
    std::vector<int> vid(size_t((cols + 1) * (rows + 1)), -1);
    auto vert = [&](int i, int j) {
        int& id = vid[size_t(j * (cols + 1) + i)];
        if (id >= 0) return uint32_t(id);
        float th = lerp(-0.25f, kPi + 0.25f, float(i) / cols), s = lerp(-0.02f, 0.34f, float(j) / rows);
        Vec3 q = headPoint(th, std::max(s, 0.0f));
        if (s < 0.0f) q.y += s * h;
        float cover = smoothstep(0.0f, 0.035f, -field(th, std::max(s, 0.0f)));
        float chinK = bump((s - 0.04f) / 0.2f) * std::max(0.0f, std::sin(th)) * cover;
        Vec3 n = normalize(headNormalDir(q) + Vec3{0, -0.6f * chinK, 0});
        q += n * (0.0015f * h + thick * cover * (0.6f + 0.4f * chinK)) + Vec3{0, -drop * chinK, 0.3f * drop * chinK};
        id = int(p.pos.size());
        p.pos.push_back(q);
        p.w.push_back(one(head));
        p.uv.push_back({});
        return uint32_t(id);
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            float th = lerp(-0.25f, kPi + 0.25f, (i + 0.5f) / cols), s = lerp(-0.02f, 0.34f, (j + 0.5f) / rows);
            if (field(th, std::max(s, 0.0f)) > -0.004f) continue;
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
