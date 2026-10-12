// Head accessories: hair (a static scalp cap for volume + thick rounded locks,
// each on its own physics chain), facial hair, horns and ears.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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

void Builder::emitClump(Part& p, float th, float s, float off, float len, float width, float thick, float lift,
                        const std::function<Vec3(Vec3)>& flow, Vec3 color, int bone) const {
    const float h = m.h;
    constexpr int kSteps = 3;
    const float seg = len / kSteps;
    std::vector<Vec3> pts{headSurface(th, std::min(s, 0.985f), off)};
    Vec3 prevDir = normalize(flow(pts[0]), Vec3{0, -1, 0});
    bool free = s < 0.03f;
    // On the scalp: step along the tangent plane, then project back onto the offset
    // head surface along the ray from the head centre (smooth across the crown).
    const Vec3 hcen = headCenter();
    auto project = [&](Vec3 q) {
        for (int it = 0; it < 2; ++it) {
            float sq = clamp((q.y - m.headOrigin.y) / h, 0.0f, 0.985f);
            float tq = std::atan2(q.z - m.headOrigin.z - hZc_(sq) * h, q.x - m.headOrigin.x);
            Vec3 sp = headSurface(tq, sq, off);
            q = hcen + normalize(q - hcen, Vec3{0, 1, 0}) * length(sp - hcen);
        }
        return q;
    };
    for (int k = 0; k < kSteps; ++k) {
        const Vec3 cur = pts.back();
        Vec3 D = normalize(flow(cur), prevDir);
        Vec3 q;
        if (!free) {
            const Vec3 N = headNormalDir(cur);
            Vec3 Dt = normalize(D - N * dot(D, N), prevDir);
            q = project(cur + Dt * seg);
            if ((q.y - m.headOrigin.y) / h < 0.03f) free = true;
        } else {
            q = collide(cur + normalize(D + prevDir) * seg, 0.004f * h);
        }
        prevDir = normalize(q - cur, prevDir);
        pts.push_back(q);
    }
    if (pts.size() < 3) return;
    // Cross-sections: two side edges sunk slightly into the surface and a raised
    // ridge, so each lock shows two planes catching the light differently.
    const size_t n = pts.size();
    std::vector<uint32_t> Ls, Ms, Rs;
    const SkinW w = one(bone);
    auto add = [&](Vec3 v) {
        p.pos.push_back(v);
        p.w.push_back(w);
        p.uv.push_back({});
        return uint32_t(p.pos.size() - 1);
    };
    for (size_t k = 0; k + 1 < n; ++k) {
        const float t = float(k) / float(n - 1);
        const Vec3 c = pts[k];
        const Vec3 T = normalize(pts[k + 1] - pts[k > 0 ? k - 1 : 0], prevDir);
        const Vec3 N0 = headNormalDir(c);
        const Vec3 N = normalize(N0 - T * dot(N0, T), N0);
        const Vec3 B = normalize(cross(N, T), Vec3{1, 0, 0});
        const float hw = 0.5f * width * std::pow(1.0f - 0.85f * t, 0.8f);
        const float ridge = thick * (k == 0 ? 0.12f : 1.0f - 0.45f * t) + lift * t * t;
        Ls.push_back(add(c - B * hw - N * (0.30f * thick)));
        Ms.push_back(add(c + N * ridge));
        Rs.push_back(add(c + B * hw - N * (0.30f * thick)));
    }
    const uint32_t tip = add(pts.back() + headNormalDir(pts.back()) * (lift * 0.8f));
    const Mat base{color, MaterialClass::Hair};
    const Mat tipM{color * 1.06f, MaterialClass::Hair};
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, const Mat& mt) {
        p.tris.push_back({a, b, c, mt});
        p.tris.push_back({a, c, d, mt});
    };
    for (size_t k = 0; k + 1 < Ls.size(); ++k) {
        quad(Ls[k], Ls[k + 1], Ms[k + 1], Ms[k], base);
        quad(Ms[k], Ms[k + 1], Rs[k + 1], Rs[k], base);
    }
    const size_t e = Ls.size() - 1;
    p.tris.push_back({Ls[e], tip, Ms[e], tipM});
    p.tris.push_back({Ms[e], tip, Rs[e], tipM});
    p.tris.push_back({Ls[0], Ms[0], Rs[0], base});  // root cap
    // Winding: make every face point away from the head.
    for (size_t i = p.tris.size() - (Ls.size() - 1) * 4 - 3; i < p.tris.size(); ++i) {
        auto& tr = p.tris[i];
        const Vec3 A = p.pos[tr.a], Bv = p.pos[tr.b], C = p.pos[tr.c];
        const Vec3 nrm = cross(Bv - A, C - A), ctr = (A + Bv + C) / 3.0f;
        bool rootCap = i == p.tris.size() - 1;
        Vec3 out = rootCap ? (pts[0] - pts[1]) : (ctr - headCenter());
        if (dot(nrm, out) < 0) std::swap(tr.b, tr.c);
    }
}

void Builder::buildHair() {
    const HairStyle style = g.hair;
    const float h = m.h, vol = g.hairVolume, len = g.hairLength, deg = kPi / 180.0f;
    const RigInfo& rig = ch.rig;
    const int head = rig.head;
    const Vec3 hc = headCenter();
    Rng r = Rng(g.seed).fork(55);
    const Vec3 c0 = g.palette.hair, c1 = g.palette.hairTip;
    const Mat hairMat{c0 * 0.80f, MaterialClass::Hair};  // under-layer, a touch darker than the locks

    const bool tied = isTiedHair(style);
    const bool elderBald = style == HairStyle::Bald && g.age > 45;
    if (style == HairStyle::Bald && !elderBald && g.facialHair != FacialHair::BraidedBeard) return;

    // ---- scalp cap ----
    struct CapSpec {
        float front, temple, side, back, nape;  // hairline height per direction
        float thick, top;                       // thickness, highest covered s (1 = crown)
        float tips = 1.0f;                      // lock tips along the rim (0 = clean edge)
    };
    auto cap = [&](CapSpec cs, float crownVolume, Mat capMat) {
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
                float off = cs.thick * ((cs.thick < 0.02f * h ? 0.75f : 0.20f) + (cs.thick < 0.02f * h ? 0.25f : 0.80f) * smoothstep(0.0f, 0.3f, f)) + crownVolume * h * bump((s - 0.86f) / 0.22f) +
                            0.0f;
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
                // A partial cap (horseshoe) pinches to nothing where its hairline meets its top.
                const float th0 = kTau * j / cols, th1 = kTau * (j + 1) / cols;
                const float l0 = line(std::fabs(std::remainder(th0 - kPi / 2, kTau))), l1 = line(std::fabs(std::remainder(th1 - kPi / 2, kTau)));
                if (!closed && std::min(cs.top - l0, cs.top - l1) < 0.015f) continue;
                p.tris.push_back({a, b, c, capMat});
                p.tris.push_back({a, c, d, capMat});
            }
        if (closed) {
            uint32_t pole = uint32_t(p.pos.size());
            p.pos.push_back(m.headOrigin + Vec3{0, h + cs.thick + crownVolume * h, hZc_(1.0f) * h});
            p.w.push_back(one(head));
            p.uv.push_back({});
            for (int j = 0; j < cols; ++j) p.tris.push_back({pole, idx(rows, j + 1), idx(rows, j), capMat});
        }
        int flipped = 0;
        for (auto& tr : p.tris) {  // outward winding
            const Vec3 A = p.pos[tr.a], B = p.pos[tr.b], C = p.pos[tr.c];
            if (dot(cross(B - A, C - A), (A + B + C) / 3.0f - hc) < 0) std::swap(tr.b, tr.c), ++flipped;
        }
        if (std::getenv("CHARGEN_CHECK")) std::fprintf(stderr, "cap flipped %d/%zu\n", flipped, p.tris.size());
    };
    const float capT = 0.055f * vol * h;
    const CapSpec shortCap{0.80f, 0.66f, 0.47f, 0.36f, 0.28f, capT * 0.9f, 1.0f};
    // Long cuts: the cap only covers the top; the hair mass wraps the back of the skull from the crown.
    const CapSpec longCap{0.80f, 0.68f, 0.66f, 0.79f, 0.80f, capT, 1.0f, 0.0f};
    const CapSpec tightCap{0.80f, 0.66f, 0.48f, 0.34f, 0.26f, 0.032f * h, 1.0f, 0.5f};
    // Shaved and faded sides read as stubble over skin, not as a hair helmet.
    const Mat shaved{lerp(c0 * 0.80f, g.palette.skin, 0.72f), MaterialClass::Skin};
    const Mat faded{lerp(c0 * 0.80f, g.palette.skin, 0.45f), MaterialClass::Skin};
    switch (style) {
        case HairStyle::Bald:
            if (elderBald) cap({0.60f, 0.60f, 0.44f, 0.30f, 0.26f, 0.025f * h, 0.60f}, 0, hairMat);  // horseshoe (sides/back only)
            break;  // horseshoe (sides/back only)
        case HairStyle::Buzz: cap({0.80f, 0.66f, 0.47f, 0.36f, 0.28f, 0.016f * h, 1.0f}, 0, hairMat); break;
        case HairStyle::Undercut:
        case HairStyle::CrewCut: {
            const bool shave = style == HairStyle::Undercut;
            cap({0.80f, 0.66f, 0.47f, 0.36f, 0.28f, 0.013f * h, 1.0f, 0.0f}, 0, shave ? shaved : faded);  // shaved / faded sides
            if (style == HairStyle::Undercut) cap({0.80f, 0.74f, 0.74f, 0.70f, 0.66f, capT * 1.1f, 1.0f}, 0.0f, hairMat);  // full top
            break;
        }
        case HairStyle::Tonsure: {  // monk's ring: the crown is shaved
            CapSpec c = shortCap;
            c.thick = 0.018f * h;
            c.tips = 0.0f;
            c.top = 0.86f;
            cap(c, 0.0f, hairMat);
            break;
        }
        case HairStyle::Afro: {  // a big rounded volume of tight curls
            CapSpec c{0.80f, 0.66f, 0.47f, 0.36f, 0.30f, 0.26f * h * vol, 1.0f, 0.0f};
            cap(c, 0.08f * vol, hairMat);
            break;
        }
        default: {
            // A thin under-layer only: the volume comes from the faceted locks laid over it.
            CapSpec c = tied ? tightCap : isShortHair(style) ? shortCap : longCap;
            c.thick = 0.018f * h;
            c.tips = 0.0f;
            cap(c, 0.0f, hairMat);
            break;
        }
    }

    // ---- faceted locks over the scalp (short cuts, tops of long and tied cuts) ----
    {
        Part& cp = newPart();
        const Vec3 crown = m.headOrigin + Vec3{0, h * 1.02f, hZc_(1.0f) * h};
        // Where tied hair is gathered (the flow combs every lock towards the nearest tie).
        std::vector<Vec3> ties;
        switch (style) {
            case HairStyle::HighPonytail: ties = {headSurface(270 * deg, 0.86f, 0.0f)}; break;
            case HairStyle::Bun: ties = {headSurface(270 * deg, 0.72f, 0.0f)}; break;
            case HairStyle::ManBun: case HairStyle::MessyBun: ties = {headSurface(270 * deg, 0.88f, 0.0f)}; break;
            case HairStyle::Topknot: ties = {headSurface(270 * deg, 0.97f, 0.0f)}; break;
            case HairStyle::LowPonytail: ties = {headSurface(270 * deg, 0.40f, 0.0f)}; break;
            case HairStyle::CrownBraid:  // combed up and back into a braid wound round the head
                for (int k = 0; k < 16; ++k) {
                    const float th = kTau * float(k) / 16.0f;
                    ties.push_back(headSurface(th, 0.80f + 0.08f * std::sin(th), 0.0f));
                }
                break;
            case HairStyle::TwinBraids: ties = {headSurface(190 * deg, 0.45f, 0.0f), headSurface(350 * deg, 0.45f, 0.0f)}; break;
            case HairStyle::SideBraid: ties = {headSurface(g.partSide > 0 ? 200 * deg : 340 * deg, 0.45f, 0.0f)}; break;
            default: ties = {headSurface(270 * deg, 0.52f, 0.0f)}; break;
        }
        auto nearestTie = [&](Vec3 q) {
            Vec3 best = ties[0];
            for (const Vec3& t : ties) if (length(t - q) < length(best - q)) best = t;
            return best;
        };
        auto away = [&](Vec3 q) { Vec3 d = q - crown; d.y = std::min(d.y, 0.0f); return normalize(d, Vec3{0, -1, 0}); };
        const float part = g.partSide;
        const float px = part * 0.10f * h;  // part line offset on top
        std::function<Vec3(Vec3)> flow;
        float L = 0.30f, W = 0.20f, Tk = 0.045f, lift = 0.0f, sTop = 0.97f, jit = 0.07f;
        Curve hairline{{0.0f, 0.80f}, {0.8f, 0.68f}, {1.57f, 0.49f}, {2.3f, 0.38f}, {kPi, 0.30f}};
        bool use = true;
        switch (style) {
            case HairStyle::ShortCrop:
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.5f, 0}; };
                L = 0.24f; W = 0.18f; Tk = 0.040f;
                break;
            case HairStyle::SidePart:
            case HairStyle::Pixie:
                flow = [&, px](Vec3 q) {
                    float top = smoothstep(0.62f * h, 0.85f * h, q.y - m.headOrigin.y);
                    float sideSign = (q.x - px) * part >= 0 ? 1.0f : -1.0f;   // +1 on the part's side
                    Vec3 sweep{sideSign > 0 ? part * 0.8f : -part * 1.0f, -0.25f, -0.15f};
                    return lerp(away(q) + Vec3{0, -0.6f, -0.2f}, sweep, top);
                };
                L = style == HairStyle::Pixie ? 0.40f : 0.36f; W = 0.21f; Tk = 0.050f;
                break;
            case HairStyle::SlickedBack:
                flow = [&](Vec3 q) { return Vec3{0, -0.35f, -1.0f} + away(q) * 0.3f; };
                L = 0.40f; W = 0.22f; Tk = 0.036f; jit = 0.04f;
                break;
            case HairStyle::Messy:
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.3f, 0}; };
                L = 0.30f; W = 0.19f; Tk = 0.050f; lift = 0.015f; jit = 0.30f;
                break;
            case HairStyle::Undercut:
                flow = [&, part](Vec3 q) { return Vec3{-part * 0.55f, -0.15f, -0.8f} + away(q) * 0.25f; };
                L = 0.40f; W = 0.22f; Tk = 0.055f;
                hairline = Curve{{0.0f, 0.80f}, {0.8f, 0.76f}, {1.57f, 0.76f}, {2.3f, 0.74f}, {kPi, 0.70f}};
                break;
            case HairStyle::CrewCut:  // short on top, faded sides
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.2f, -0.4f}; };
                L = 0.20f; W = 0.17f; Tk = 0.032f; jit = 0.05f;
                hairline = Curve{{0.0f, 0.80f}, {0.8f, 0.74f}, {1.57f, 0.70f}, {2.3f, 0.66f}, {kPi, 0.62f}};
                break;
            case HairStyle::Tonsure:  // a ring of short hair combed down around a shaved crown
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.6f, 0}; };
                L = 0.22f; W = 0.18f; Tk = 0.040f; jit = 0.10f;
                break;
            case HairStyle::WarriorBraids:  // pulled straight back off the face (the braids and mass hang below)
                flow = [&](Vec3 q) { return Vec3{0, -0.30f, -1.0f} + away(q) * 0.25f; };
                L = 0.42f; W = 0.22f; Tk = 0.034f; jit = 0.04f;
                hairline = Curve{{0.0f, 0.80f}, {0.8f, 0.70f}, {1.57f, 0.66f}, {2.3f, 0.82f}, {kPi, 0.86f}};
                break;
            case HairStyle::BowlCut:  // combed down from the crown all round
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.4f, 0}; };
                L = 0.34f; W = 0.22f; Tk = 0.034f; jit = 0.04f;
                break;
            case HairStyle::Curly:  // short, dense, tight curls
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.2f, 0}; };
                L = 0.16f; W = 0.15f; Tk = 0.06f; lift = 0.03f; jit = 0.65f;
                break;
            case HairStyle::Afro:  // curls over the big volume
                flow = [&](Vec3 q) { return away(q) + Vec3{0, 0.2f, 0}; };
                L = 0.20f; W = 0.24f; Tk = 0.07f; lift = 0.03f; jit = 0.7f;
                break;
            case HairStyle::Mullet:  // short top and sides, long at the back (sheet below)
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.5f, -0.2f}; };
                L = 0.26f; W = 0.19f; Tk = 0.045f; jit = 0.15f;
                break;
            case HairStyle::Wolf:  // layered and choppy, lifted crown
                flow = [&, px](Vec3 q) {
                    float sideSign = (q.x - px) >= 0 ? 1.0f : -1.0f;
                    return Vec3{sideSign * 0.5f, -0.7f, -0.1f} + away(q) * 0.5f;
                };
                L = 0.36f; W = 0.21f; Tk = 0.05f; lift = 0.02f; jit = 0.30f;
                break;
            case HairStyle::Locs:  // roots combed down; the locs themselves hang below
                flow = [&](Vec3 q) { return away(q) + Vec3{0, -0.6f, 0}; };
                L = 0.30f; W = 0.20f; Tk = 0.035f; jit = 0.06f;
                break;
            case HairStyle::Buzz:
            case HairStyle::Bald:
                use = false;
                break;
            default:
                if (tied) {
                    flow = [&](Vec3 q) { return normalize(nearestTie(q) - q) + Vec3{0, -0.1f, 0}; };
                    L = 0.40f; W = 0.24f; Tk = 0.026f; jit = 0.03f;
                } else {  // long cuts: the top is combed away from the part, the mass takes over below
                    flow = [&, px](Vec3 q) {
                        float sideSign = (q.x - px) >= 0 ? 1.0f : -1.0f;
                        return Vec3{sideSign * 0.7f, -0.6f, -0.25f} + away(q) * 0.3f;
                    };
                    L = 0.40f; W = 0.24f; Tk = 0.030f; jit = 0.04f;
                    hairline = Curve{{0.0f, 0.80f}, {0.8f, 0.72f}, {1.57f, 0.72f}, {2.3f, 0.86f}, {kPi, 0.88f}};
                }
                break;
        }
        if (use) {
            L *= h; W *= h * vol; Tk *= h * vol; lift *= h;
            const float headR = 0.40f * h;
            const float dsRow = 0.40f * L / h;
            int row = 0;
            for (float sr = sTop; sr > 0.2f; sr -= dsRow, ++row) {
                const float ringR = headR * std::max(0.25f, std::sin(std::acos(clamp((sr - 0.55f) / 0.48f, -1.0f, 1.0f))));
                const int nAround = std::max(4, int(kTau * ringR / (0.70f * W)));
                for (int j = 0; j < nAround; ++j) {
                    float th = kTau * (float(j) + 0.5f * float(row % 2) + r.range(-0.12f, 0.12f)) / float(nAround);
                    float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                    if (sr < hairline(phi) + 0.02f) continue;
                    if (style == HairStyle::Tonsure && sr > 0.83f) continue;
                    Vec3 col = c0 * r.range(0.90f, 1.08f);
                    Vec3 jdir{r.range(-jit, jit), r.range(-0.3f * jit, 0.3f * jit), r.range(-jit, jit)};
                    auto fl = [&, jdir](Vec3 q) { return normalize(flow(q), Vec3{0, -1, 0}) + jdir; };
                    const float off = style == HairStyle::Afro ? (0.06f + 0.17f * smoothstep(0.36f, 0.56f, sr)) * h * vol : 0.012f * h + Tk * 0.5f;
                    emitClump(cp, th, sr, off, L * r.range(0.85f, 1.12f), W * r.range(0.9f, 1.1f), Tk, lift, fl, col, head);
                }
            }
        }
    }

    const Vec3 down{0, -1, 0}, fwd{0, 0, 1}, back{0, 0, -1};
    // ---- locks ----
    std::vector<LockSpec> locks;
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

    // A braid wound round the head (crown braid): a twisted, beaded tube on the scalp.
    auto braidLoop = [&](float s0, float sAmp, float radius) {
        Part& bp = newPart();
        const int K = lod.sparseRings ? 20 : 32, S = 6;
        const float R = radius * h;
        std::vector<Vec3> path;
        for (int k = 0; k < K; ++k) {
            const float th = kTau * float(k) / float(K);
            path.push_back(headSurface(th, s0 + sAmp * std::sin(th), 0.55f * R + 0.012f * h));
        }
        const SkinW w = one(head);
        for (int k = 0; k < K; ++k) {
            const Vec3 c = path[size_t(k)], T = normalize(path[size_t((k + 1) % K)] - path[size_t((k + K - 1) % K)]);
            const Vec3 N0 = headNormalDir(c), N = normalize(N0 - T * dot(N0, T)), B = cross(T, N);
            const float bead = 0.80f + 0.30f * std::fabs(std::sin(kPi * float(k) / 2.0f + 0.5f));  // a plait every two steps
            for (int j = 0; j < S; ++j) {
                const float a = kTau * float(j) / float(S) + 0.9f * float(k);  // twist
                bp.pos.push_back(c + (N * std::cos(a) * 0.75f + B * std::sin(a)) * (R * bead));
                bp.w.push_back(w);
                bp.uv.push_back({});
            }
        }
        for (int k = 0; k < K; ++k)
            for (int j = 0; j < S; ++j) {
                const uint32_t a = uint32_t(k * S + j), b = uint32_t(k * S + (j + 1) % S), c = uint32_t(((k + 1) % K) * S + (j + 1) % S),
                               d = uint32_t(((k + 1) % K) * S + j);
                const Mat mt{c0 * (k % 2 ? 0.92f : 1.04f), MaterialClass::Hair};
                const Vec3 ctr = (bp.pos[a] + bp.pos[c]) * 0.5f, axis = (path[size_t(k)] + path[size_t((k + 1) % K)]) * 0.5f;
                const bool flip = dot(cross(bp.pos[b] - bp.pos[a], bp.pos[c] - bp.pos[a]), ctr - axis) < 0;
                bp.tris.push_back(flip ? Part::Tri{a, c, b, mt} : Part::Tri{a, b, c, mt});
                bp.tris.push_back(flip ? Part::Tri{a, d, c, mt} : Part::Tri{a, c, d, mt});
            }
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
        float curly = 0.0f;      // ringlets: locks twist in and out of the mass along their length
    };
    auto sheet = [&](const SheetSpec& S) {
        // Odd column count: ridge columns (even) alternate with groove columns (odd),
        // so every lock is a wedge whose two faces catch the light differently.
        int M0 = S.cols > 0 ? S.cols : (lod.sparseRings ? 9 : 13);
        const int M = M0 % 2 ? M0 : M0 + 1, steps = lod.lockSteps + (lod.sparseRings ? 0 : 1) + (S.curly > 0 ? 2 : 0);
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
                if (S.curly > 0) {  // ringlets: each lock bulges out and tucks in, neighbours out of phase
                    const float ph = float(k) * 2.2f + float(c) * 1.7f;
                    groove += S.curly * thk * 0.9f * std::sin(ph) * smoothstep(0.08f, 0.3f, t);
                    base += normalize(cross(tan, out), Vec3{1, 0, 0}) * (S.curly * 0.035f * h * std::cos(ph) * smoothstep(0.1f, 0.4f, t));
                }
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
            sideFrame(0.55f, 0.15f);
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
        case HairStyle::Topknot:  // samurai knot on the crown with a short tail
            bunAt(270 * deg, 0.97f, 0.11f * vol);
            tail(270 * deg, 0.99f, normalize(back + Vec3{0, 0.5f, 0}), 0.45f, 0.06f, false);
            break;
        case HairStyle::Mullet: {
            SheetSpec m2{185 * deg, 355 * deg, 1.05f * len, 0.06f * h * vol, 0.55f, 0.62f, 0.9f, 0.04f, 0.10f, 6, 1.4f, 0.45f, 0.05f};
            m2.fwdBias = -0.2f;
            sheet(m2);
            break;
        }
        case HairStyle::Wolf: {  // shaggy, layered, shoulder length with a curtain fringe
            SheetSpec w2{125 * deg, 415 * deg, 1.10f * len, 0.068f * h * vol, 0.70f, 0.88f, 0.70f, 0.08f, 0.22f, 10, 1.5f, 0.45f, 0.05f};
            w2.tipLen = 0.16f;
            sheet(w2);
            fringe(0.95f);
            break;
        }
        case HairStyle::Locs:
            for (int i = 0; i < 14; ++i) {
                float th = (150.0f + 240.0f * (float(i) + 0.5f) / 14.0f) * deg;
                float phi = std::fabs(std::remainder(th - kPi / 2, kTau));
                float sRoot = lerp(0.62f, 0.80f, smoothstep(1.0f, kPi, phi)) + r.range(-0.03f, 0.03f);
                Vec3 dir = normalize(down + thetaDir(th) * 0.25f + (phi < 1.4f ? fwd * 0.1f : back * 0.2f));
                LockSpec L{th, sRoot, dir, (1.3f + 0.4f * r.uniform()) * len * h, 0.075f * h * vol, 0.075f * h * vol};
                L.gravity = 1.0f; L.stiffness = 0.5f; L.curl = 0.05f; L.tipWidth = 0.7f; L.sides = 6; L.tube = true; L.bones = 3;
                L.pStiff = 1.0f; L.pDrag = 0.3f; L.pGrav = 0.15f;
                add(L);
            }
            break;
        case HairStyle::Hime: {  // straight curtain, blunt fringe and blunt cheek-length side locks
            SheetSpec backMass{168 * deg, 372 * deg, 2.4f * len, 0.074f * h * vol, 0.74f, 0.92f, 0.85f, 0.02f, 0.16f, 8, 1.0f, 0.4f, 0.05f};
            backMass.fwdBias = -0.25f;
            backMass.tipLen = 0.0f;
            backMass.lenOf = [](float) { return 1.0f; };
            sheet(backMass);
            for (float sd : {1.0f, -1.0f}) {
                float a0 = sd > 0 ? 128 * deg : 362 * deg, a1 = sd > 0 ? 178 * deg : 412 * deg;
                SheetSpec side{a0, a1, 0.62f, 0.06f * h * vol, 0.72f, 0.72f, 1.0f, 0.02f, 0.04f, 2, 2.0f, 0.5f, 0.04f};
                side.fwdBias = 0.10f;
                side.cols = 4;
                side.tipLen = 0.0f;
                side.lenOf = [](float) { return 1.0f; };
                sheet(side);
            }
            fringe(1.0f);
            break;
        }
        case HairStyle::CrownBraid:  // milkmaid braid wound round the head
            braidLoop(0.79f, 0.10f, 0.088f * vol);
            break;
        case HairStyle::WarriorBraids: {  // northern warrior: hair pulled back, two braids from the temples
            const bool fm = g.femininity > 0.5f;
            SheetSpec backMass{172 * deg, 368 * deg, (fm ? 2.2f : 1.45f) * len, 0.070f * h * vol, 0.74f, 0.90f, 0.85f, 0.04f, 0.16f, 7,
                               1.0f, 0.4f, 0.05f};
            backMass.fwdBias = -0.30f;
            sheet(backMass);
            for (float sd : {1.0f, -1.0f})
                tail((sd > 0 ? 34 : 146) * deg, 0.66f, normalize(down + fwd * 0.25f + Vec3{sd * 0.08f, 0, 0}), (fm ? 1.7f : 1.4f) * len, 0.062f,
                     true);
            break;
        }
        case HairStyle::TwinBraids:
            for (float sd : {1.0f, -1.0f})
                tail((sd > 0 ? 190 : 350) * deg, 0.45f, normalize(down + fwd * 0.35f + Vec3{-sd * 0.2f, 0, 0} * -1.0f), 1.7f * len, 0.085f, true);
            fringe(0.9f);
            break;
        case HairStyle::SideBraid: {
            const float sd = g.partSide;
            tail((sd > 0 ? 200 : 340) * deg, 0.45f, normalize(down + fwd * 0.45f + Vec3{sd * 0.15f, 0, 0}), 1.9f * len, 0.10f, true);
            fringe(1.0f);
            break;
        }
        case HairStyle::LowPonytail:
            tail(270 * deg, 0.40f, normalize(back * 0.3f + down), 2.0f * len, 0.11f, false);
            if (g.fringe != Fringe::None) fringe(1.0f);
            break;
        case HairStyle::BowlCut: {  // the medieval page-boy: level all round, straight fringe
            SheetSpec bowl{120 * deg, 420 * deg, 0.50f, 0.060f * h * vol, 0.76f, 0.88f, 1.0f, 0.30f, 0.14f, 8, 2.4f, 0.5f, 0.04f};
            bowl.lenOf = [](float x) { return lerp(0.62f, 1.0f, 1.0f - std::fabs(2.0f * x - 1.0f)); };
            bowl.tipLen = 0.015f;
            sheet(bowl);
            fringe(0.72f);
            break;
        }
        case HairStyle::MessyBun:
            bunAt(270 * deg, 0.88f, 0.19f * vol);
            sideFrame(0.60f, 0.14f);
            fringe(0.9f);
            break;
        case HairStyle::LongCurly: {
            SheetSpec backMass{160 * deg, 380 * deg, 1.9f * len, 0.12f * h * vol, 0.72f, 0.90f, 0.85f, 0.10f, 0.45f, 9, 0.9f, 0.4f, 0.05f};
            backMass.fwdBias = -0.15f;
            backMass.tipLen = 0.16f;
            backMass.curly = 1.0f;
            backMass.cols = lod.sparseRings ? 11 : 17;
            sheet(backMass);
            for (float sd : {1.0f, -1.0f}) {
                float a0 = sd > 0 ? 126 * deg : 360 * deg, a1 = sd > 0 ? 180 * deg : 414 * deg;
                SheetSpec front{a0, a1, 1.5f * len, 0.08f * h * vol, 0.70f, 0.70f, 1.0f, 0.04f, 0.20f, 2, 1.1f, 0.45f, 0.05f};
                front.fwdBias = 0.12f;
                front.cols = 4;
                front.tipLen = 0.12f;
                front.curly = 0.8f;
                sheet(front);
            }
            fringe(1.0f);
            break;
        }
        case HairStyle::VeryLong: {
            SheetSpec backMass{168 * deg, 372 * deg, 3.4f * len, 0.080f * h * vol, 0.74f, 0.92f, 0.85f, 0.03f, 0.18f, 8, 0.8f, 0.4f, 0.06f};
            backMass.fwdBias = -0.25f;
            sheet(backMass);
            for (float sd : {1.0f, -1.0f}) {
                float a0 = sd > 0 ? 126 * deg : 360 * deg, a1 = sd > 0 ? 180 * deg : 414 * deg;
                SheetSpec front{a0, a1, 2.2f * len, 0.064f * h * vol, 0.70f, 0.70f, 1.0f, 0.02f, 0.06f, 2, 1.0f, 0.45f, 0.05f};
                front.fwdBias = 0.16f;
                front.cols = 4;
                front.tipLen = 0.08f;
                sheet(front);
            }
            fringe(1.0f);
            break;
        }
        case HairStyle::AsymBob: {  // one side longer, swept fringe
            const float sd = g.partSide;
            SheetSpec b2{125 * deg, 415 * deg, 1.05f, 0.062f * h * vol, 0.72f, 0.92f, 0.78f, 0.25f, 0.12f, 8, 2.0f, 0.5f, 0.04f};
            b2.lenOf = [sd](float x) { float k = sd > 0 ? x : 1.0f - x; return lerp(0.62f, 1.35f, smoothstep(0.0f, 1.0f, k)); };
            sheet(b2);
            fringe(1.0f);
            break;
        }
        default: break;
    }

    // Braided beards: one or two plaits hanging from the chin (with physics).
    if (g.facialHair == FacialHair::BraidedBeard) {
        const bool two = g.seed % 2 == 0;
        for (float off : two ? std::vector<float>{-0.16f, 0.16f} : std::vector<float>{0.0f})
            tail(kPi / 2 + off, 0.035f, normalize(down + fwd * 0.12f), 0.60f * std::max(0.8f, g.hairLength), two ? 0.055f : 0.068f, true);
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

// Facial hair grows like the scalp hair: columns of locks rooted on the skin along
// the beard line, combed down the face and, past the jaw, hanging with gravity.
// The upper edge lies flush on the skin (the painted beard shadow carries it on),
// the mass thickens towards the jaw and ends in a soft, rounded hem of lock tips.
// The moustache is the same thing in small: locks from under the nose, combed
// down and out over the lip.
void Builder::buildFacialHair() {
    const FacialHair fh = g.facialHair;
    if (fh == FacialHair::None || fh == FacialHair::Stubble) return;
    const float h = m.h;
    const int head = ch.rig.head;
    Rng r = Rng(g.seed).fork(58);
    const Vec3 col = lerp(g.palette.hair, g.palette.brow, 0.25f);
    const Vec3 tipCol = lerp(col, g.palette.hairTip * 1.1f, 0.5f);
    const Vec3 hcen = headCenter();
    const SkinW w = one(head);
    const float mw = g.face.mouthWidth, len = std::max(0.75f, g.hairLength);
    auto onFace = [&](float th, float sv, float off) {
        Vec3 q = headPoint(th, std::max(sv, 0.0f));
        if (sv < 0.0f) q.y += sv * h;
        return q + headNormalDir(q) * off;
    };

    // A sheet of locks: column c has points pts[c][k]; out[c][k] its outward direction.
    struct Sheet {
        std::vector<std::vector<Vec3>> pts, nrm;
        std::vector<std::vector<float>> thick;
        size_t onSkin = 0;  // rows lying on the face (their inner side is sunk into the head)
    };
    auto emitSheet = [&](const Sheet& S, bool closeHem) {
        Part& p = newPart();
        const size_t C = S.pts.size(), K = S.pts[0].size();
        std::vector<std::vector<uint32_t>> O(C), I(C);
        auto addV = [&](Vec3 v) {
            p.pos.push_back(v);
            p.w.push_back(w);
            p.uv.push_back({});
            return uint32_t(p.pos.size() - 1);
        };
        for (size_t c = 0; c < C; ++c)
            for (size_t k = 0; k < K; ++k) {
                const float ridge = (c % 2 == 0 ? 0.30f : -0.15f) * smoothstep(0.0f, 0.3f, float(k) / float(K - 1));
                Vec3 o = S.pts[c][k] + S.nrm[c][k] * (S.thick[c][k] * (1.0f + ridge));
                o = outsideHeadMesh(o, (k == 0 ? 0.005f : k < S.onSkin ? 0.010f : 0.006f) * h);  // never under a facet of the head
                O[c].push_back(addV(o));
                // On the face the inner side hides inside the faceted head; hanging rows get real volume.
                const float in = k < S.onSkin ? 0.012f * h : 0.30f * S.thick[c][k];
                I[c].push_back(addV(S.pts[c][k] - S.nrm[c][k] * in));
            }
        auto tri = [&](uint32_t a, uint32_t b, uint32_t c2, Vec3 out, Vec3 shade) {
            const Vec3 A = p.pos[a], B = p.pos[b], Cc = p.pos[c2];
            if (dot(cross(B - A, Cc - A), out) < 0) std::swap(b, c2);
            p.tris.push_back({a, b, c2, Mat{shade, MaterialClass::Hair}});
        };
        for (size_t c = 0; c + 1 < C; ++c)
            for (size_t k = 0; k + 1 < K; ++k) {
                const float t = float(k) / float(K - 1);
                const Vec3 sh = lerp(col, tipCol, smoothstep(0.5f, 1.0f, t)) * (c % 2 == 0 ? 1.03f : 0.95f);
                const Vec3 n = S.nrm[c][k] + S.nrm[c + 1][k + 1];
                tri(O[c][k], O[c + 1][k], O[c + 1][k + 1], n, sh);
                tri(O[c][k], O[c + 1][k + 1], O[c][k + 1], n, sh);
                // Underside of the hanging part (seen from below and from the sides).
                tri(I[c][k], I[c + 1][k + 1], I[c + 1][k], n * -1.0f, col * 0.75f);
                tri(I[c][k], I[c][k + 1], I[c + 1][k + 1], n * -1.0f, col * 0.75f);
            }
        if (closeHem)
            for (size_t c = 0; c + 1 < C; ++c) {
                const Vec3 dn = S.pts[c][K - 1] - S.pts[c][K - 2];
                tri(O[c][K - 1], I[c][K - 1], I[c + 1][K - 1], dn, tipCol * 0.9f);
                tri(O[c][K - 1], I[c + 1][K - 1], O[c + 1][K - 1], dn, tipCol * 0.9f);
            }
        for (size_t c : {size_t(0), C - 1})  // side walls
            for (size_t k = 0; k + 1 < K; ++k) {
                const Vec3 side = S.pts[c][k] - S.pts[c == 0 ? 1 : C - 2][k];
                tri(O[c][k], I[c][k], I[c][k + 1], side, col * 0.85f);
                tri(O[c][k], I[c][k + 1], O[c][k + 1], side, col * 0.85f);
            }
    };

    const bool full = fh == FacialHair::FullBeard || fh == FacialHair::LongBeard || fh == FacialHair::BraidedBeard;
    const bool chops = fh == FacialHair::MuttonChops, goatee = fh == FacialHair::Goatee, shortB = fh == FacialHair::ShortBeard;
    if (full || chops || goatee || shortB) {
        // Beard line: under the lower lip at the front, up past the mouth corners, along the cheek to the sideburn.
        auto sTop = [&](float ax) {
            return 0.185f + 0.075f * smoothstep(0.10f, 0.25f, ax) + 0.21f * smoothstep(0.30f, 0.95f, ax);
        };
        auto sJaw = [&](float ax) { return 0.03f + 0.09f * smoothstep(0.45f, 0.95f, ax); };
        const float thick = (fh == FacialHair::LongBeard ? 0.034f : full ? 0.030f : chops ? 0.024f : shortB ? 0.013f : 0.022f) * h;
        const float drop = (fh == FacialHair::LongBeard ? 0.42f * len : full ? 0.13f * len : goatee ? 0.10f : shortB ? 0.025f : 0.02f) * h;
        float th0 = -0.10f, th1 = kPi + 0.10f;
        if (goatee) th0 = kPi / 2 - 0.42f, th1 = kPi / 2 + 0.42f;
        const int NC = goatee ? 7 : lod.sparseRings ? 11 : 17;
        const int KF = 4, KD = drop > 0.03f * h ? 4 : 2;
        // One sheet, or for mutton chops two (the chin stays bare).
        std::vector<std::pair<float, float>> spans;
        if (chops) spans = {{th0, kPi / 2 - 0.40f}, {kPi / 2 + 0.40f, th1}};
        else spans = {{th0, th1}};
        for (auto [a0, a1] : spans) {
            Sheet S;
            const int nc = chops ? 7 : NC;
            for (int c = 0; c < nc; ++c) {
                const float th = lerp(a0, a1, float(c) / float(nc - 1));
                const float ax = std::fabs(std::cos(th)), front = 1.0f - ax;
                const float lockVar = r.range(0.9f, 1.1f) * (c % 2 == 0 ? 1.08f : 0.94f);
                std::vector<Vec3> pts, nrm;
                std::vector<float> tk;
                float top = goatee ? 0.185f + 0.02f * smoothstep(0.1f, 0.4f, ax) : sTop(ax);
                const float bot = goatee ? 0.03f : sJaw(ax);
                // Goatee / mutton chops: taper the thickness at the open ends.
                const float endT = goatee || chops ? std::min(1.0f, std::min(float(c), float(nc - 1 - c)) / 1.5f) : 1.0f;
                for (int k = 0; k < KF; ++k) {
                    const float t = float(k) / float(KF - 1);
                    const float sv = lerp(top, bot, t);
                    const Vec3 q = onFace(th, sv, 0.0f);
                    pts.push_back(q);
                    nrm.push_back(headNormalDir(q));
                    tk.push_back(std::max(0.003f * h, thick * endT * smoothstep(0.0f, 0.6f, t) * (0.75f + 0.25f * front)));
                }
                // Below the jaw: hang down (and a little forward at the chin), kept off the neck and chest.
                const float dl = drop * lockVar * (chops ? 0.6f : 1.0f) * (1.0f - 0.70f * std::pow(ax, 1.4f)) / float(KD);
                Vec3 q = pts.back(), d = normalize(Vec3{0, -1, 0} + Vec3{0, 0, 0.30f * front} + thetaDir(th) * 0.10f);
                for (int k = 0; k < KD; ++k) {
                    q = collide(q + d * dl, 0.010f * h + 0.5f * thick);
                    d = normalize(d * 0.6f + Vec3{0, -1, 0} * 0.4f);
                    const float t = float(k + 1) / float(KD);
                    Vec3 n = q - Vec3{hcen.x, q.y, hcen.z - 0.06f * h};
                    n = normalize(n, Vec3{0, 0, 1});
                    pts.push_back(q);
                    nrm.push_back(n);
                    tk.push_back(thick * endT * (1.0f - 0.55f * t) * (0.75f + 0.25f * front));
                }
                S.pts.push_back(pts);
                S.nrm.push_back(nrm);
                S.thick.push_back(tk);
            }
            S.onSkin = KF;
            emitSheet(S, true);
        }
    }
    if (fh == FacialHair::Mustache || fh == FacialHair::Goatee || full || chops) {
        // Moustache: locks from under the nose combed down and out; long ends
        // droop past the mouth corners (and join the chops / the beard).
        const int NC = lod.sparseRings ? 7 : 11, K = 4;
        const float xMax = (chops ? 0.135f : 0.105f) * mw, thick = (full ? 0.018f : 0.016f) * h;
        const float droop = fh == FacialHair::Mustache ? r.range(0.02f, 0.06f) : chops ? 0.07f : 0.03f;
        Sheet S;
        for (int c = 0; c < NC; ++c) {
            const float x = lerp(-xMax, xMax, float(c) / float(NC - 1)), u = std::fabs(x) / xMax;
            std::vector<Vec3> pts, nrm;
            std::vector<float> tk;
            for (int k = 0; k < K; ++k) {
                const float t = float(k) / float(K - 1);
                const float xs = x * (1.0f + 0.10f * t);
                const float th = std::acos(clamp(xs / 0.36f, -1.0f, 1.0f));
                const float sv = lerp(0.306f - 0.022f * u * u, 0.258f - 0.010f * u - droop * u * u * u, t);
                const Vec3 q = onFace(th, sv, 0.002f * h);
                pts.push_back(q);
                nrm.push_back(headNormalDir(q));
                const float prof = (0.55f + 0.45f * (1.0f - u * u)) * (1.0f - 0.35f * bump(x / (0.12f * xMax)));  // parted at the philtrum
                tk.push_back(thick * prof * (0.25f + 0.75f * std::sin(kPi * std::min(1.0f, 0.2f + 0.9f * t))) * (u > 0.85f ? 0.6f : 1.0f));
            }
            S.pts.push_back(pts);
            S.nrm.push_back(nrm);
            S.thick.push_back(tk);
        }
        S.onSkin = K;
        emitSheet(S, true);
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
        float len = (elf ? 0.40f * h : 0.11f * h) * g.face.earSize;
        Vec3 v = normalize(Vec3{0, 1, 0} - dir * dir.y), u = cross(dir, v);
        Loft L;
        float rr = (elf ? 0.075f * h : 0.085f * h) * std::sqrt(g.face.earSize);
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
