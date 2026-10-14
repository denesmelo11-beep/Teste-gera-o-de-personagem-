// Death and decay: the real skeleton inside every character (skull, spine, ribcage,
// pelvis, limb bones, hands and feet, tail), the progressive rot of the flesh over it,
// and risen skeletons. Every bone is skinned to the same rig, so a skeleton or a
// half-rotten corpse plays every animation.
#include <algorithm>

#include "generator_impl.hpp"

namespace cg::detail {

namespace {
Vec3 grey(Vec3 c, float sat) {
    const float l = luminance(c);
    return lerp(Vec3{l, l, l}, c, sat);
}
}  // namespace

// The genome of a corpse at decay d: pallor, then mottled rot, then dark leathery
// putrefaction; flesh wastes away over the bones; eyes cloud and then rot out.
void applyDecay(Genome& g, float d) {
    if (d <= 0.0f) return;
    BodyShape& b = g.body;
    FaceStyle& f = g.face;
    Palette& pal = g.palette;
    if (b.skinKind == 1) {  // constructs do not rot: they power down, rust and break open
        const float off = smoothstep(0.02f, 0.25f, d);
        f.glowEyes = false;
        f.hollowEyes = d > 0.08f;  // dead lenses
        f.rot = 0.0f, f.ruddy = 0.0f, f.milky = 0.0f;
        b.coreColor = lerp(b.coreColor, Vec3{0.07f, 0.08f, 0.10f}, off);  // the core goes dark glass
        pal.skin = pal.skin * (1.0f - 0.18f * d);
        pal.lips = pal.skin * 0.7f;
        pal.underwear = lerp(pal.underwear, Vec3{0.24f, 0.21f, 0.17f}, 0.7f * d);
        pal.hair = grey(pal.hair, 1.0f - 0.5f * d) * (1.0f - 0.25f * d);
        pal.hairTip = pal.hair * 0.88f;
        b.bones = 0;
        return;
    }
    const Vec3 c0 = pal.skin;
    const Vec3 pallor = grey(c0, 0.45f) * Vec3{0.86f, 0.88f, 0.95f};
    const Vec3 rot = lerp(grey(c0, 0.3f), Vec3{0.44f, 0.46f, 0.31f}, 0.55f);
    const Vec3 putrid = lerp(grey(c0, 0.2f) * 0.6f, Vec3{0.27f, 0.21f, 0.15f}, 0.7f);
    pal.skin = d < 0.25f ? lerp(c0, pallor, d / 0.25f) : d < 0.6f ? lerp(pallor, rot, (d - 0.25f) / 0.35f) : lerp(rot, putrid, saturate((d - 0.6f) / 0.35f));
    pal.lips = lerp(pal.lips, pal.skin * 0.7f, saturate(d * 3.0f));
    pal.underwear = lerp(pal.underwear, Vec3{0.24f, 0.21f, 0.17f}, 0.7f * d);
    pal.hair = grey(pal.hair, 1.0f - 0.5f * d) * (1.0f - 0.25f * d);
    pal.hairTip = pal.hair * 0.88f;
    // Wasting: the flesh shrinks onto the bones.
    const float w = smoothstep(0.25f, 0.9f, d);
    b.thigh *= 1.0f - 0.22f * w; b.calf *= 1.0f - 0.22f * w; b.arm *= 1.0f - 0.22f * w;
    b.waistWidth *= 1.0f - 0.15f * w; b.chestDepth *= 1.0f - 0.10f * w; b.hipWidth *= 1.0f - 0.06f * w;
    b.bust *= 1.0f - 0.6f * w; b.butt *= 1.0f - 0.5f * w; b.belly *= 1.0f - w; b.muscle *= 1.0f - 0.4f * w;
    Anatomy& an = b.anat;
    an.definition = lerp(an.definition, 1.0f, w);
    for (float* v : {&an.clavicles, &an.ribcage, &an.scapulae, &an.pelvis, &an.knees, &an.elbows, &an.wrists}) *v = lerp(*v, 1.0f, w);
    f.cheekHollow = lerp(f.cheekHollow, 1.0f, w);
    f.eyeDepth = lerp(f.eyeDepth, 1.0f, w);
    f.jowl *= 1.0f - w;
    f.roundness *= 1.0f - w;
    f.lipFullness *= 1.0f - 0.6f * w;
    f.eyeBags = std::max(f.eyeBags, 0.6f * smoothstep(0.1f, 0.4f, d));
    f.ruddy *= 1.0f - saturate(d * 4.0f);
    f.rot = std::max(f.rot, smoothstep(0.18f, 0.55f, d));
    f.milky = smoothstep(0.04f, 0.20f, d);
    f.hollowEyes = d > 0.6f;
    if (!b.skeletal) f.glowEyes = false;
    f.restLid = std::max(f.restLid, 0.25f * smoothstep(0.0f, 0.2f, d));
    // Bared bones painted into the flesh while it still covers the body; past ~0.55 the
    // flesh tears open on the real skeleton instead.
    // From ~0.3 the flesh tears onto the real skeleton (Builder::addDecayHoles); no painted bones.
    b.bones = d < 0.3f ? b.bones : 0;
}

// The skeleton, built from the body's own landmarks: it sits inside the flesh at any
// decay stage and stands alone at full decay.
void Builder::buildBoneSkeleton() {
    const float h = m.h, tl = m.torsoLen, bm = g.body.boneMass, hb = h * bm;  // hb: bone scale
    const RigInfo& r = ch.rig;
    const bool boneEnds = bonesOnly_ || decay_ >= 0.85f;  // hands and feet already bare
    const float hl = bonesOnly_ ? hb : 0.72f * hb;  // limb bones inside wasted flesh stay inside it
    const Vec3 boneC = bonesOnly_ ? Vec3{0.70f, 0.65f, 0.54f} : Vec3{0.62f, 0.55f, 0.44f};  // stained while flesh clings
    // Constructs carry an iron frame instead of bones.
    const bool iron = g.body.skinKind == 1;
    const Mat bone = iron ? Mat{lerp(Vec3{0.30f, 0.30f, 0.32f}, Vec3{0.38f, 0.24f, 0.15f}, 0.35f * decay_), MaterialClass::Metal}
                          : Mat{boneC, MaterialClass::Horn};
    const Mat dark{{0.06f, 0.05f, 0.05f}, MaterialClass::Dark};
    boneParts0_ = parts.size();
    Part& first = newPart();
    first.id = bodyGroup_;
    const size_t Pi = parts.size() - 1;
    auto P = [&]() -> Part& { return parts[Pi]; };
    auto boneMat = [=](float, float) { return bone; };
    // A bone shaft with knobbly ends; wf gives the skin weight along it.
    auto tube = [&](const std::vector<Vec3>& pts, float r0, const std::function<SkinW(float)>& wf, int sides = 5, float knob = 0.4f) {
        const size_t n = pts.size();
        if (n < 2) return;
        if (lod.sparseRings) sides = std::max(4, sides - 1);  // lower LODs: leaner shafts
        Loft L;
        for (size_t k = 0; k < n; ++k) {
            const Vec3 d = normalize(pts[std::min(k + 1, n - 1)] - pts[k > 0 ? k - 1 : 0], Vec3{0, 1, 0});
            const Vec3 v = normalize(cross(d, std::fabs(d.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}), Vec3{0, 0, 1}), u = cross(d, v);
            const float t = float(k) / float(n - 1);
            const float rr = r0 * (0.85f + knob * (bump(t / 0.14f) + bump((t - 1.0f) / 0.14f)));
            L.rings.push_back(makeRing(pts[k], u, v, SuperRing{rr, 0, rr, rr}, sides));
            L.t.push_back(t);
        }
        L.poleStart = true;
        L.startPole = pts[0] - normalize(pts[1] - pts[0]) * r0;
        L.poleEnd = true;
        L.endPole = pts.back() + normalize(pts.back() - pts[n - 2]) * r0;
        L.tEnd = 1;
        emitLoft(P(), L, [&](float t, float, Vec3) { return wf(saturate(t)); }, boneMat);
    };
    auto tubeOne = [&](const std::vector<Vec3>& pts, float r0, int boneId, int sides = 5, float knob = 0.4f) {
        tube(pts, r0, [=](float) { return one(boneId); }, sides, knob);
    };
    auto blob = [&](Vec3 c, Vec3 axis, Vec3 up, float rA, float rU, float rV, int boneId, int lat = 4, int lon = 6) {
        Ellipsoid e;
        e.center = c;
        e.axis = axis;
        e.up = up;
        e.rAxis = rA;
        e.rU = rU;
        e.rV = rV;
        e.lat = lod.sparseRings ? std::max(3, lat - 1) : lat;
        e.lon = lod.sparseRings ? std::max(5, lon - 1) : lon;
        emitEllipsoid(P(), e, [=](float, float, Vec3) { return one(boneId); }, boneMat);
    };
    auto spineAt = [&](float t) { return Vec3{0, m.crotchY + t * tl, cZc_(t) - 0.42f * cDb_(t)}; };
    auto spineW = [&](float t) { return chainWeights({{0.10f, r.hips}, {0.45f, r.spine}, {0.75f, r.chest}}, t); };

    // ---- skull: cranium with deep sockets, nasal cavity and the upper teeth ----
    {
        const Vec3 o = m.headOrigin;
        const float hw = g.body.headWidth;
        const float exS = 0.150f * g.face.eyeSpacing;  // orbit centres (head units)
        auto skullPt = [&](float th, float s) {
            const SuperRing hr = headRing(s);
            Vec2 p = hr.point(th);
            const float sn = std::sin(th);
            p.x *= 0.86f;
            p.y = hr.zc + (p.y - hr.zc) * (sn > 0 ? 0.88f : 0.90f);
            const float xn = p.x / (h * hw), ax = std::fabs(xn), front = smoothstep(0.2f, 0.7f, sn);
            if (g.face.eyePairs >= 2) {  // two stacked orbits per side
                const float es = (ax - 0.97f * exS) / 0.078f, ev = (s - 0.506f) / 0.058f;
                const float es2 = (ax - 1.10f * exS) / 0.062f, ev2 = (s - 0.392f) / 0.045f;
                p.y -= 0.11f * h * front * std::max({0.0f, 1.0f - (es * es + ev * ev), 0.85f * (1.0f - (es2 * es2 + ev2 * ev2))});
            } else {
                const float es = (ax - exS) / 0.085f, ev = (s - 0.462f) / 0.075f;
                p.y -= 0.12f * h * front * std::max(0.0f, 1.0f - (es * es + ev * ev));  // round, deep orbits
            }
            p.y += 0.022f * h * front * bump((s - 0.555f) / 0.035f) * bump(xn / 0.34f);  // brow ridge
            // Temples hollow behind the orbits; the cheekbones flare a little; the face
            // narrows hard to the upper jaw (maxilla), set back under the nasal opening.
            p.x -= std::copysign(0.03f * h, p.x) * bump((s - 0.58f) / 0.10f) * smoothstep(0.2f, 0.6f, sn) * smoothstep(0.25f, 0.33f, ax);
            p.x += std::copysign(0.012f * h, p.x) * bump((s - 0.39f) / 0.04f) * smoothstep(0.15f, 0.3f, ax);
            p.x *= 1.0f - 0.30f * smoothstep(0.36f, 0.22f, s);
            p.y -= 0.03f * h * front * smoothstep(0.34f, 0.24f, s);
            p.y -= 0.05f * h * front * bump((s - 0.37f) / 0.04f) * bump(xn / 0.05f);  // nasal opening
            return o + Vec3{p.x, s * h, p.y};
        };
        Loft L;
        const float ss[] = {0.205f, 0.25f, 0.30f, 0.345f, 0.385f, 0.415f, 0.445f, 0.475f, 0.505f, 0.535f, 0.57f, 0.64f, 0.75f, 0.85f, 0.92f, 0.96f};
        const int N = lod.sparseRings ? 16 : 28;
        for (float s : ss) {
            std::vector<Vec3> ring(static_cast<size_t>(N));
            for (int j = 0; j < N; ++j) ring[size_t(j)] = skullPt(headWarp(kTau * float(j) / float(N)), s);
            L.rings.push_back(ring);
            L.t.push_back(s);
        }
        L.poleStart = true;
        L.startPole = o + Vec3{0, 0.22f * h, hZc_(0.2f) * h};
        L.poleEnd = true;
        L.endPole = o + Vec3{0, 0.985f * h, hZc_(1.0f) * h};
        L.tEnd = 1;
        const float exo = exS;
        // Openings cut exactly (clip fields on the bone surface): round orbits, the
        // inverted-heart nasal opening and the dark gap behind the teeth.
        {
            const Vec3 oc = o;
            const float hh = h, hw2 = hw;
            auto frontOf = [this, oc, hh](Vec3 c) {
                const float s = (c.y - oc.y) / hh;
                return c.z - oc.z > hZc_(clamp(s, 0.0f, 1.0f)) * hh + 0.03f * hh && s > 0.15f && s < 0.62f;
            };
            P().clips.push_back({[=](Vec3 c) {
                if (!frontOf(c)) return 1.0f;
                const float s = (c.y - oc.y) / hh, ax = std::fabs((c.x - oc.x) / (hh * hw2));
                const float es = (ax - exo) / 0.078f, ev = (s - 0.462f) / 0.068f;
                return (std::sqrt(es * es + ev * ev) - 1.0f) * 0.06f * hh;
            }, dark});
            P().clips.push_back({[=](Vec3 c) {
                if (!frontOf(c)) return 1.0f;
                const float s = (c.y - oc.y) / hh, ax = std::fabs((c.x - oc.x) / (hh * hw2));
                const float wdt = 0.010f + 0.035f * saturate((s - 0.335f) / 0.07f) - 0.03f * saturate((s - 0.395f) / 0.02f);
                return std::max({ax - wdt, 0.335f - s, s - 0.41f}) * hh;
            }, dark});
        }
        emitLoft(P(), L, [=](float, float, Vec3) { return one(r.head); }, boneMat);
        // Mandible: a horseshoe under the skull, its teeth meeting the upper row.
        Loft J;
        const float js[] = {0.02f, 0.08f, 0.14f, 0.188f};
        for (float s : js) {
            const SuperRing hr = headRing(s);
            std::vector<Vec3> ring(static_cast<size_t>(N));
            for (int j = 0; j < N; ++j) {
                const float th = headWarp(kTau * float(j) / float(N));
                Vec2 p = hr.point(th);
                p.x *= 0.70f + 0.45f * smoothstep(0.12f, 0.0f, s);  // a broad chin, not a point
                p.y = hr.zc + (p.y - hr.zc) * (std::sin(th) > 0 ? 0.80f : 0.30f);
                ring[size_t(j)] = o + Vec3{p.x, s * h, p.y};
            }
            J.rings.push_back(ring);
            J.t.push_back(s);
        }
        J.poleStart = true;
        J.startPole = o + Vec3{0, -0.005f * h, hZc_(0.0f) * h + 0.05f * h};
        J.poleEnd = true;
        J.endPole = o + Vec3{0, 0.19f * h, hZc_(0.19f) * h};
        J.tEnd = 1;
        emitLoft(P(), J, [=](float, float, Vec3) { return one(r.head); }, boneMat);
        // Teeth: two rows of blocks along the jaw line, on the bone surface.
        for (int row = 0; row < 2; ++row) {
            const float sT = row == 0 ? 0.212f : 0.184f;
            const int n = 8;
            for (int k = 0; k < n; ++k) {
                const float xt = ((float(k) + 0.5f) / float(n) * 2.0f - 1.0f) * 0.11f * h * hw;
                Vec3 best = skullPt(kPi / 2, sT);
                for (int i = 0; i <= 40; ++i) {
                    const Vec3 q = skullPt(kPi / 2 + (float(i) / 40.0f - 0.5f) * 1.6f, sT);
                    if (std::fabs(q.x - o.x - xt) < std::fabs(best.x - o.x - xt)) best = q;
                }
                Ellipsoid e;
                e.center = best + Vec3{0, 0, -0.004f * h};
                e.axis = {0, 1, 0};
                e.up = {0, 0, 1};
                e.rAxis = 0.024f * h;
                e.rU = 0.0145f * h;
                e.rV = 0.013f * h;
                e.lat = 3;
                e.lon = 4;
                const Mat tooth{boneC * 1.08f, MaterialClass::Horn};
                emitEllipsoid(P(), e, [=](float, float, Vec3) { return one(r.head); }, [=](float, float) { return tooth; });
            }
        }
        // Risen skeletons: a cold fire in each socket.
        if (g.face.glowEyes)
            for (float sd : {1.0f, -1.0f}) {
                const float x = sd * exo * h * hw;
                // On the floor of the socket, just in front of it: a small ember in the dark.
                Vec3 sp = skullPt(kPi / 2, 0.462f);
                for (int i = 0; i <= 60; ++i) {
                    const Vec3 q = skullPt(kPi / 2 + (float(i) / 60.0f - 0.5f) * 1.6f, 0.462f);
                    if (std::fabs(q.x - o.x - x) < std::fabs(sp.x - o.x - x)) sp = q;
                }
                const float z = sp.z - o.z + 0.022f * h;
                Ellipsoid e;
                e.center = o + Vec3{x, 0.462f * h, z};
                e.axis = {0, 0, 1};
                e.rAxis = 0.014f * h;
                e.rU = e.rV = 0.030f * h;
                e.lat = 3;
                e.lon = 6;
                const Vec3 gc = g.face.glow * 1.25f + Vec3{0.15f, 0.15f, 0.15f};
                const Mat glowM{Vec3{std::min(gc.x, 1.0f), std::min(gc.y, 1.0f), std::min(gc.z, 1.0f)}, MaterialClass::Glow};
                Part& gp = newPart();  // its own part: the orbit clips must not darken it
                gp.id = bodyGroup_;
                emitEllipsoid(gp, e, [=](float, float, Vec3) { return one(r.head); }, [=](float, float) { return glowM; });
            }
    }

    // ---- spine and neck ----
    for (float t = 0.02f; t < 1.0f; t += 0.075f) {
        const Vec3 c = spineAt(t);
        Ellipsoid e;
        e.center = c;
        e.axis = {0, 1, 0};
        e.up = {0, 0, 1};
        e.rAxis = 0.034f * hb;
        e.rU = 0.085f * hb;
        e.rV = 0.07f * hb;
        e.lat = 3;
        e.lon = 6;
        const SkinW w = spineW(t);
        emitEllipsoid(P(), e, [=](float, float, Vec3) { return w; }, boneMat);
        tube({c, c + Vec3{0, -0.04f * h, -0.10f * h}}, 0.022f * h, [=](float) { return w; }, 4, 0.0f);  // spinous process
    }
    {
        // Cervical column: seven chunky vertebrae on a continuous core up into the skull
        // (no floating discs, whatever the neck length).
        const Vec3 a = spineAt(0.93f), b2 = m.headOrigin + Vec3{0, 0.20f * h, -0.06f * h};
        const Vec3 nd = normalize(b2 - a, {0, 1, 0});
        const float seg = length(b2 - a) / 7.0f;
        tubeOne({a, b2}, 0.028f * hb, r.neck, 5, 0.0f);
        for (int k = 0; k < 7; ++k) {
            const Vec3 c = lerp(a, b2, (float(k) + 0.5f) / 7.0f);
            const float wk = lerp(1.0f, 0.8f, float(k) / 6.0f);
            blob(c, nd, {0, 0, 1}, std::min(0.36f * seg, 0.035f * hb), 0.068f * hb * wk, 0.055f * hb * wk, r.neck, 3, 6);
        }
    }

    // ---- ribcage, sternum, clavicles, scapulae ----
    auto ribPoint = [&](float t, float th, float k) {
        const SuperRing rr = torsoRing(t);
        const Vec2 q = rr.point(th);
        return Vec3{q.x * k, m.crotchY + t * tl, rr.zc + (q.y - rr.zc) * k};
    };
    for (int k = 0; k < 7; ++k) {
        const float t = 0.865f - 0.045f * float(k);
        const float phiEnd = k < 5 ? 0.13f * kPi : 0.42f * kPi;  // the last two float
        for (float sd : {1.0f, -1.0f}) {
            std::vector<Vec3> pts;
            for (int i = 0; i <= 7; ++i) {
                const float phi = lerp(0.93f * kPi, phiEnd, float(i) / 7.0f);
                const float tt = t - 0.07f * (1.0f - phi / kPi);  // ribs slope down towards the front
                pts.push_back(ribPoint(tt, kPi / 2 - sd * phi, 0.84f));
            }
            tubeOne(pts, 0.036f * hb, r.chest, 4, 0.0f);
        }
    }
    tubeOne({ribPoint(0.60f, kPi / 2, 0.86f), ribPoint(0.75f, kPi / 2, 0.86f), ribPoint(0.90f, kPi / 2, 0.86f)}, 0.034f * hb, r.chest, 5, 0.2f);
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& a = m.arms[i];
        const int root = r.arms[i].root;
        const Vec3 st = ribPoint(0.90f, kPi / 2, 0.86f) + Vec3{a.side * 0.04f * h, 0, 0};
        tubeOne({st, lerp(st, a.shoulder, 0.5f) + Vec3{0, 0.03f * h, 0.02f * h}, a.shoulder + Vec3{0, 0.06f * h, 0}}, 0.026f * hb, root, 5, 0.3f);
        if (a.pair == 0) {
            const Vec3 sc = ribPoint(0.82f, -kPi / 2 + a.side * 0.75f, 0.92f);
            blob(sc, normalize(Vec3{0, 0, -1}), {0, 1, 0}, 0.024f * hb, 0.15f * h, 0.19f * h, root, 3, 5);
        }
    }

    // ---- pelvis ----
    {
        const float yP = m.crotchY + 0.10f * tl, P0 = g.body.hipWidth * h;
        for (float sd : {1.0f, -1.0f}) {  // ilium: a flared bowl wing from the sacrum out to the hip crest
            const Vec3 c{sd * 0.40f * P0, yP + 0.02f * h, cZc_(0.1f) - 0.10f * h};
            blob(c, normalize(Vec3{sd * 0.55f, 0.15f, 0.8f}), {0, 1, 0}, 0.05f * h, 0.30f * h, 0.24f * h, r.hips, 3, 7);
            tubeOne({c + Vec3{0, -0.12f * h, 0.04f * h}, m.hipJoint[sd > 0 ? 0 : 1] + Vec3{-sd * 0.06f * h, 0.02f * h, 0}}, 0.07f * h, r.hips, 5, 0.3f);
        }
        blob({0, yP - 0.02f * h, cZc_(0.1f) - 0.6f * cDb_(0.1f)}, {0, 0, -1}, {0, 1, 0}, 0.04f * h, 0.11f * h, 0.16f * h, r.hips, 3, 5);  // sacrum
        tubeOne({Vec3{0.45f * P0, m.crotchY + 0.02f * tl, cZc_(0.0f) + 0.05f * h}, Vec3{0, m.crotchY - 0.01f * tl, cZc_(0.0f) + 0.30f * cDf_(0.0f)},
                 Vec3{-0.45f * P0, m.crotchY + 0.02f * tl, cZc_(0.0f) + 0.05f * h}},
                0.045f * h, r.hips, 5, 0.2f);  // pubic arch
    }

    // ---- arms and hands ----
    // Long bones: shafts stop short of the joint so each joint reads as its own knob
    // (humeral head, the elbow's condyles and olecranon, the wrist's styloids).
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& a = m.arms[i];
        const LimbRig& lr = r.arms[i];
        const float k2 = a.pair ? 0.85f : 1.0f, ua = length(a.elbow - a.shoulder);
        const Vec3 du = normalize(a.elbow - a.shoulder), flex = normalize(cross(a.dir, {0, 0, 1}));
        blob(a.shoulder, du, {0, 1, 0}, 0.085f * hl * k2, 0.08f * hl * k2, 0.08f * hl * k2, lr.upper, 3, 6);  // humeral head
        tubeOne({a.shoulder + du * (0.10f * ua), a.shoulder + du * (0.5f * ua), a.elbow - du * (0.07f * ua)}, 0.050f * hl * k2, lr.upper, 6, 0.35f);
        blob(a.elbow - du * (0.05f * ua), flex, du, 0.085f * hl * k2, 0.045f * hl * k2, 0.05f * hl * k2, lr.upper, 3, 6);  // condyles
        const Vec3 d = normalize(a.wrist - a.elbow), v = orthoFront(d), u = cross(d, v);
        const float fa = length(a.wrist - a.elbow);
        blob(a.elbow - d * (0.02f * fa) + normalize(cross(flex, d)) * (0.04f * hl), d, v, 0.045f * hl * k2, 0.035f * hl * k2, 0.035f * hl * k2, lr.lower, 3, 5);  // olecranon
        for (float o : {-1.0f, 1.0f})
            tubeOne({a.elbow + d * (0.07f * fa) + v * (o * 0.022f * hl), lerp(a.elbow, a.wrist, 0.5f) + v * (o * 0.036f * hl),
                     a.wrist - d * (0.03f * fa) + v * (o * 0.034f * hl)},
                    0.026f * hl * k2, lr.lower, 5, 0.45f);  // radius and ulna
        if (!boneEnds) continue;  // until the hands rot away, their bones would poke through the flesh
        const float Lh = length(a.handTip - a.wrist);
        const Vec3 palm = u * a.side;
        blob(a.wrist + d * (0.10f * Lh), d, palm, 0.07f * Lh, 0.20f * Lh, 0.09f * Lh, lr.end, 3, 6);  // carpals
        for (int f = 0; f < 4; ++f) {
            const float off = (1.5f - float(f)) / 1.5f;
            Vec3 p = a.wrist + d * (0.18f * Lh) + v * (off * 0.16f * Lh);
            Vec3 fd = d;
            std::vector<Vec3> pts{p};
            for (int k = 0; k < 4; ++k) {
                fd = normalize(fd + palm * (k == 0 ? 0.05f : 0.25f));
                p = p + fd * (Lh * (k == 0 ? 0.30f : 0.15f) * (f == 3 ? 0.85f : 1.0f));
                pts.push_back(p);
            }
            tubeOne(pts, std::max(0.05f * Lh, 0.016f * hl), lr.end, 4, 0.3f);
        }
        tubeOne({a.wrist + d * (0.15f * Lh) + v * (0.2f * Lh), a.wrist + d * (0.35f * Lh) + v * (0.38f * Lh) + palm * (0.1f * Lh),
                 a.wrist + d * (0.55f * Lh) + v * (0.45f * Lh) + palm * (0.2f * Lh)},
                0.04f * Lh, lr.end, 4, 0.3f);  // thumb
    }

    // ---- legs and feet ----
    const float T = g.body.thigh * h;
    auto legBones = [&](const LimbRig& lr, Vec3 hip, Vec3 knee, Vec3 ankle, Vec3 ball, float side, bool longFoot, bool hoofed) {
        const Vec3 lat{side, 0, 0}, df = normalize(knee - hip), dt = normalize(ankle - knee);
        const float lf = length(knee - hip), lt = length(ankle - knee);
        // Femur: head in the socket, neck out to the greater trochanter, shaft, two condyles.
        blob(hip, df, {0, 0, 1}, 0.095f * hl, 0.09f * hl, 0.09f * hl, lr.upper, 3, 6);
        blob(hip + lat * (0.10f * hl) + Vec3{0, -0.03f * h, 0}, {0, 1, 0}, {0, 0, 1}, 0.07f * hl, 0.05f * hl, 0.05f * hl, lr.upper, 3, 5);
        tubeOne({hip + df * (0.10f * lf) + lat * (0.05f * hl), hip + df * (0.5f * lf), knee - df * (0.08f * lf)}, 0.064f * hl, lr.upper, 6, 0.35f);
        for (float o : {-1.0f, 1.0f})
            blob(knee - df * (0.05f * lf) + lat * (o * 0.045f * hl), {0, 0, 1}, {0, 1, 0}, 0.07f * hl, 0.045f * hl, 0.06f * hl, lr.upper, 3, 5);
        blob(knee + Vec3{0, 0.01f * h, 0.075f * hl}, {0, 0, 1}, {0, 1, 0}, 0.035f * hl, 0.05f * hl, 0.06f * hl, lr.lower, 3, 5);  // patella
        // Tibia (plateau, shaft, malleolus) and the thin fibula outside it.
        blob(knee + dt * (0.06f * lt), {0, 1, 0}, {0, 0, 1}, 0.04f * hl, 0.11f * hl, 0.075f * hl, lr.lower, 3, 6);
        tubeOne({knee + dt * (0.10f * lt), knee + dt * (0.5f * lt), ankle - dt * (0.04f * lt)}, 0.054f * hl, lr.lower, 6, 0.35f);
        tubeOne({knee + dt * (0.10f * lt) + lat * (0.075f * hl) + Vec3{0, 0, -0.02f * hl}, lerp(knee, ankle, 0.5f) + lat * (0.07f * hl),
                 ankle + lat * (0.07f * hl)},
                0.022f * hl, lr.lower, 4, 0.5f);
        for (float o : {-1.0f, 1.0f}) blob(ankle + lat * (o * 0.06f * hl), dt, {0, 0, 1}, 0.04f * hl, 0.03f * hl, 0.03f * hl, lr.lower, 3, 5);  // malleoli
        if (!boneEnds) return;  // the feet stay flesh until they rot away
        if (longFoot) {  // digitigrade / horse: a long metatarsal, then toes or a hoof bone
            tubeOne({ankle, lerp(ankle, ball, 0.5f), ball + Vec3{0, 0.03f * h, 0}}, 0.09f * T, lr.end, 5, 0.4f);
            if (hoofed) {
                blob(ball + Vec3{0, 0.03f * h, 0.03f * h}, {0, 0, 1}, {0, 1, 0}, 0.07f * h, 0.07f * h, 0.04f * h, lr.tip, 3, 6);
            } else {
                for (int k = 0; k < 4; ++k) {
                    const float off = (float(k) - 1.5f) / 1.5f;
                    const Vec3 b0 = ball + Vec3{off * 0.07f * h, 0.03f * h, 0};
                    tubeOne({b0, b0 + Vec3{off * 0.02f * h, -0.02f * h, 0.12f * h}}, 0.022f * h, lr.tip, 4, 0.3f);
                }
            }
            return;
        }
        // Plantigrade foot: talus and heel, then five rays to the toes.
        const Vec3 fwd = normalize(Vec3{ball.x - ankle.x, 0, ball.z - ankle.z}, Vec3{0, 0, 1});
        blob(ankle + Vec3{0, -0.06f * h, -0.04f * h}, fwd, {0, 1, 0}, 0.12f * h, 0.06f * h, 0.07f * h, lr.end, 3, 6);
        for (int k = 0; k < 5; ++k) {
            const float off = (float(k) - 2.0f) / 2.0f;
            const Vec3 b0 = ankle + fwd * (0.10f * h) + Vec3{side * -off * 0.05f * h, -0.07f * h, 0};
            const Vec3 b1 = ball + Vec3{-side * off * 0.08f * h, -ball.y + 0.03f * h, 0};
            tubeOne({b0, b1, b1 + fwd * (0.12f * h * (1.0f - 0.12f * std::fabs(off)))}, 0.022f * h * (k == 2 ? 1.2f : 1.0f), lr.tip, 4, 0.3f);
        }
    };
    const bool longFoot = g.body.digitigrade || g.body.centaur;
    for (int s = 0; s < 2; ++s)
        legBones(r.legs[size_t(s)], m.hipJoint[s], m.knee[s], m.ankle[s], m.ball[s], s == 0 ? 1.0f : -1.0f, longFoot, g.body.hooves);
    if (g.body.centaur) {
        for (int s = 0; s < 2; ++s)
            legBones(r.legs[size_t(s + 2)], m.hindHip[s], m.hindKnee[s], m.hindAnkle[s], m.hindBall[s], s == 0 ? 1.0f : -1.0f, true, true);
        // The horse's spine and ribcage along the barrel.
        const float yTop = m.barrelY + 0.80f * m.barrelHalfH;
        tubeOne({{0, yTop, m.barrelFront - 0.2f * h}, {0, yTop + 0.05f * h, -1.5f * h}, {0, yTop, m.barrelBack + 0.3f * h}}, 0.06f * h, r.hips, 6, 0.1f);
        for (int k = 0; k < 9; ++k) {
            const float z = lerp(m.barrelFront - 0.4f * h, -2.4f * h, float(k) / 8.0f);
            for (float sd : {1.0f, -1.0f}) {
                std::vector<Vec3> pts;
                for (int i = 0; i <= 6; ++i) {
                    const float a = lerp(0.5f * kPi, -0.45f * kPi, float(i) / 6.0f);
                    pts.push_back({sd * 0.86f * m.barrelHalfW * std::cos(a), m.barrelY + 0.80f * m.barrelHalfH * std::sin(a), z - 0.15f * h * float(i) / 6.0f});
                }
                tubeOne(pts, 0.022f * h, r.hips, 4, 0.0f);
            }
        }
        for (float sd : {1.0f, -1.0f})
            blob({sd * 0.40f * h, m.barrelY + 0.3f * m.barrelHalfH, m.barrelBack + 0.9f * h}, normalize(Vec3{sd, 0, 0.3f}), {0, 1, 0}, 0.04f * h,
                 0.45f * h, 0.30f * h, r.hips, 3, 6);  // horse pelvis
    }
    boneParts1_ = parts.size();
}

// Torn flesh: round holes through the skin onto the skeleton, growing with decay, each
// backed by a dark cavity floor behind the bones.
void Builder::addDecayHoles() {
    // k: 0 small first tears (decay 0.3, undead) .. 1 the flesh mostly gone.
    const float h = m.h, k = g.species == Species::Undead && decay_ < 0.3f ? 0.0f : smoothstep(0.3f, 0.95f, decay_);
    const float side = (g.seed % 2 == 0) ? 1.0f : -1.0f;
    const float R0 = h * lerp(0.13f, 0.25f, k);
    const float T = g.body.thigh * h, Cf = g.body.calf * h;
    auto torsoOnly = [this, h](Vec3 q) {
        const float t = clamp(torsoT(q.y), -0.06f, 1.1f);
        return q.y > m.crotchY + 0.02f * h && std::fabs(q.x) < 1.25f * cHw_(t) + 0.03f * h;
    };
    auto add = [&](Vec3 c, float R, float depth, bool head, std::function<bool(Vec3)> gate) {
        Vec3 n = head ? headNormalDir(c) : normalize(Vec3{c.x, 0, c.z - cZc_(clamp(torsoT(c.y), -0.06f, 1.1f))}, Vec3{0, 0, 1});
        holes_.push_back({c, R, depth, head, n, std::move(gate)});
    };
    add(torsoPoint(0.66f, kPi / 2 - side * 1.25f), R0, 0.30f * h, false, torsoOnly);
    if (k > 0.3f) add(torsoPoint(0.42f, kPi / 2 + side * 0.45f), 0.9f * R0, 0.30f * h, false, torsoOnly);
    if (k > 0.4f) add(torsoPoint(0.60f, -kPi / 2 + side * 0.5f), 0.9f * R0, 0.30f * h, false, torsoOnly);
    if (k > 0.6f) add(torsoPoint(0.78f, kPi / 2 + side * 0.75f), 0.75f * R0, 0.30f * h, false, torsoOnly);
    auto nearSeg = [](Vec3 a, Vec3 b, float rad) {
        return [=](Vec3 q) {
            const Vec3 ab = b - a;
            const float t = saturate(dot(q - a, ab) / std::max(1e-9f, dot(ab, ab)));
            return length(q - (a + ab * t)) < rad;
        };
    };
    if (!g.body.centaur) {
        const int ls = side > 0 ? 1 : 0, lo = 1 - ls;
        const Vec3 kd = normalize(m.knee[ls] - m.hipJoint[ls]);
        const Vec3 fw = normalize(Vec3{0, 0, 1} - kd * kd.z);
        add(lerp(m.hipJoint[ls], m.knee[ls], 0.5f) + fw * (0.95f * T) * 1.0f, h * lerp(0.10f, 0.18f, k), 0.9f * T, false,
            nearSeg(m.hipJoint[ls], m.knee[ls], 1.6f * T));
        if (k > 0.5f) {
            const Vec3 sd2 = normalize(m.ankle[lo] - m.knee[lo]);
            const Vec3 fw2 = normalize(Vec3{0, 0, 1} - sd2 * sd2.z);
            add(lerp(m.knee[lo], m.ankle[lo], 0.45f) + fw2 * (0.9f * Cf), 0.10f * h, 0.85f * Cf, false, nearSeg(m.knee[lo], m.ankle[lo], 1.6f * Cf));
        }
    }
    for (const ArmMarks& a : m.arms) {
        if (a.pair != 0) continue;
        const Vec3 lat = normalize(Vec3{a.side, 0.3f, -0.2f});
        if (a.side == side)
            add(lerp(a.shoulder, a.elbow, 0.55f) + lat * (0.95f * a.radius), h * lerp(0.07f, 0.12f, k), 0.9f * a.radius, false,
                nearSeg(a.shoulder, a.elbow, 1.7f * a.radius));
        else if (k > 0.3f)
            add(lerp(a.elbow, a.wrist, 0.5f) + lat * (0.8f * a.radius), 0.08f * h, 0.75f * a.radius, false, nearSeg(a.elbow, a.wrist, 1.6f * a.radius));
    }
    // Face: the cheek tears open on the teeth and the jaw; late, the temple too.
    auto headAt = [&](float x, float s) { return headPoint(std::acos(clamp(x / 0.36f, -1.0f, 1.0f)), s); };
    add(headAt(side * 0.13f, 0.215f), h * lerp(0.07f, 0.12f, k), 0.0f, true, nullptr);
    if (k > 0.7f) add(headAt(-side * 0.24f, 0.66f), 0.08f * h, 0.0f, true, nullptr);

    // Ragged rims: the radius wobbles around each hole (torn, not punched), and a band of
    // dark, wet flesh lines the edge.
    const Mat hole{{0, 0, 0}, MaterialClass::Count};
    // Flesh: a dark wet band; a construct's shell: torn plate with a bright scraped edge.
    const Mat rim = g.body.skinKind == 1 ? Mat{{0.58f, 0.57f, 0.55f}, MaterialClass::Metal} : Mat{{0.24f, 0.10f, 0.09f}, MaterialClass::Dark};
    int idx = 0;
    for (const Hole& ho : holes_) {
        Part& p = parts[ho.head ? headPart_ : torsoPart_];
        const Vec3 c = ho.c, n = ho.n;
        const Vec3 u = normalize(cross(n, std::fabs(n.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0})), v = cross(n, u);
        const float R = ho.R, ph = float((g.seed + uint64_t(idx++) * 7) % 13);
        const auto gate = ho.gate;
        auto field = [=](Vec3 q, float grow) {
            if (gate && !gate(q)) return 1.0f;
            const Vec3 d = q - c;
            const float a = std::atan2(dot(d, v), dot(d, u));
            const float wob = 1.0f + 0.20f * std::sin(5.0f * a + ph) + 0.11f * std::sin(9.0f * a + 2.3f * ph) + 0.06f * std::sin(17.0f * a + ph);
            return length(d) - R * wob * grow;
        };
        // Torn edges are ragged anyway: little refinement (the default doubles a corpse).
        const int ref = opt.lod == 0 ? 1 : 0;
        p.clips.push_back({[=](Vec3 q) { return field(q, 1.22f); }, rim, ref});
        p.clips.push_back({[=](Vec3 q) { return field(q, 1.0f); }, hole, ref});
    }
}

// Cavities: the body surface around each hole, copied with its winding reversed so
// its inside faces outward. Through a hole one sees the far inner wall of the torso or
// limb as dark flesh, with the bones standing in front of it.
void Builder::buildCavityShells() {
    if (holes_.empty()) return;
    Part shell;
    shell.id = bodyGroup_;
    const Part& src = parts[torsoPart_];
    const Mat flesh = g.body.skinKind == 1 ? Mat{{0.07f, 0.07f, 0.08f}, MaterialClass::Dark} : Mat{{0.17f, 0.07f, 0.06f}, MaterialClass::Dark};
    std::vector<int> map(src.pos.size(), -1);
    for (const auto& tr : src.tris) {
        const Vec3 c = (src.pos[tr.a] + src.pos[tr.b] + src.pos[tr.c]) / 3.0f;
        bool keep = false;
        for (const Hole& ho : holes_)
            if (!ho.head && length(c - ho.c) < 1.3f * ho.R + 2.2f * ho.depth) keep = true;
        if (!keep) continue;
        uint32_t ids[3];
        const uint32_t vs[3] = {tr.a, tr.c, tr.b};  // reversed
        for (int k = 0; k < 3; ++k) {
            if (map[vs[k]] < 0) {
                map[vs[k]] = int(shell.pos.size());
                shell.pos.push_back(src.pos[vs[k]]);
                shell.w.push_back(src.w[vs[k]]);
                shell.uv.push_back({});
            }
            ids[k] = uint32_t(map[vs[k]]);
        }
        shell.tris.push_back({ids[0], ids[1], ids[2], flesh});
    }
    if (!shell.tris.empty()) parts.push_back(std::move(shell));
}

// While flesh still covers most of the body the skeleton is only seen through the
// holes (and at the bare hands and feet): everything else is culled, which halves a
// corpse's triangle count without changing a single visible pixel.
void Builder::cullHiddenBones() {
    if (bonesOnly_ || boneParts1_ <= boneParts0_) return;
    const Skeleton& sk = ch.skeleton;
    const RigInfo& r = ch.rig;
    const bool boneEnds = decay_ >= 0.85f;
    std::vector<uint8_t> extremity(sk.bones.size(), 0);
    if (boneEnds) {
        std::vector<int> ends;
        for (const LimbRig& a : r.arms) ends.push_back(a.end);
        for (const LimbRig& l : r.legs) ends.push_back(l.end);
        for (size_t i = 0; i < sk.bones.size(); ++i)
            for (int b = int(i); b >= 0; b = sk.bones[size_t(b)].parent)
                if (std::find(ends.begin(), ends.end(), b) != ends.end()) {
                    extremity[i] = 1;
                    break;
                }
    }
    const float h = m.h;
    for (size_t pi = boneParts0_; pi < boneParts1_; ++pi) {
        Part& p = parts[pi];
        std::vector<Part::Tri> keep;
        keep.reserve(p.tris.size());
        for (const auto& tr : p.tris) {
            const Vec3 c = (p.pos[tr.a] + p.pos[tr.b] + p.pos[tr.c]) / 3.0f;
            bool vis = false;
            for (const Hole& ho : holes_)
                if (length(c - ho.c) < 1.6f * ho.R + 1.5f * ho.depth + 0.04f * h) {
                    vis = true;
                    break;
                }
            if (!vis && boneEnds) {
                const SkinW& w = p.w[tr.a];
                int best = 0;
                for (int k = 1; k < 4; ++k)
                    if (w.w[k] > w.w[best]) best = k;
                vis = extremity[size_t(w.j[best])] != 0;
            }
            if (vis) keep.push_back(tr);
        }
        p.tris.swap(keep);
    }
}

// ---- constructs in ruin ----

// Dents: blows hammered into the shell, more of them as the construct decays.
void Builder::dentConstruct() {
    if (g.body.skinKind != 1 || decay_ < 0.08f || bonesOnly_) return;
    Part& p = parts[torsoPart_];
    const float h = m.h;
    std::vector<Vec3> nrm(p.pos.size(), Vec3{});
    for (const auto& tr : p.tris) {
        const Vec3 n = cross(p.pos[tr.b] - p.pos[tr.a], p.pos[tr.c] - p.pos[tr.a]);
        nrm[tr.a] += n, nrm[tr.b] += n, nrm[tr.c] += n;
    }
    Rng r(g.seed, 0xDE27);
    const int count = 2 + int(10.0f * saturate(decay_));
    for (int k = 0; k < count && !p.tris.empty(); ++k) {
        const auto& tr = p.tris[size_t(r.rangeInt(0, int(p.tris.size()) - 1))];
        const Vec3 c = (p.pos[tr.a] + p.pos[tr.b] + p.pos[tr.c]) / 3.0f;
        if (c.y < 0.08f * m.H) continue;  // not the soles
        const float R = r.range(0.16f, 0.32f) * h, depth = r.range(0.5f, 1.0f) * 0.07f * h * (0.5f + decay_);
        for (size_t v = 0; v < p.pos.size(); ++v) {
            const float d = length(p.pos[v] - c);
            if (d >= R) continue;
            const float fall = 1.0f - (d / R) * (d / R);
            p.pos[v] -= normalize(nrm[v], {0, 0, 1}) * (depth * fall * fall);
        }
    }
}

// A forearm torn off (late decay): a jagged cut through flesh and frame, a dark socket
// in the stump, the frame's end sticking out and a spray of torn wires.
void Builder::breakConstruct() {
    if (g.body.skinKind != 1 || decay_ < 0.5f || m.arms.empty()) return;  // (bare frames keep the loss too)
    Rng r(g.seed, 0xB2EA);
    const ArmMarks& a = m.arms[size_t(r.rangeInt(0, int(std::min<size_t>(2, m.arms.size())) - 1))];
    const LimbRig& rig = ch.rig.arms[size_t(&a - m.arms.data())];
    const float h = m.h;
    const Vec3 dir = normalize(a.wrist - a.elbow, a.dir);
    const Vec3 c = lerp(a.elbow, a.wrist, r.range(0.30f, 0.65f));
    const float rr = 0.85f * a.radius;
    const Vec3 u = normalize(cross(dir, Vec3{0, 0, 1}), {1, 0, 0}), v = cross(dir, u);
    const Vec3 tip = a.handTip + dir * (0.2f * h);
    const float ph = r.range(0.0f, 6.0f);
    auto nearArm = [=](Vec3 q) {
        const Vec3 ab = tip - c;
        const float t = clamp(dot(q - c, ab) / dot(ab, ab), 0.0f, 1.0f);
        return length(q - (c + ab * t)) - 2.2f * rr;
    };
    auto plane = [=](Vec3 q, float off) {
        const Vec3 d = q - c;
        const float ang = std::atan2(dot(d, v), dot(d, u));
        const float jag = 0.35f * rr * std::sin(5.0f * ang + ph) + 0.18f * rr * std::sin(11.0f * ang + 2.0f * ph);
        return dot(d, dir) - off - jag;
    };
    cutFlesh_ = [=](Vec3 q) { return std::max(-plane(q, 0.0f), nearArm(q)); };
    cutFrame_ = [=](Vec3 q) { return std::max(-plane(q, 0.18f * h), nearArm(q)); };
    // The stump is a hole like any tear: the frame near it is kept, the inside is dark.
    if (!bonesOnly_) holes_.push_back({c, rr, 0.6f * rr, false, dir, nullptr});
    // Socket: a dark disc closing the stump just inside the cut.
    wireParts0_ = parts.size();
    Part& sp = newPart();
    const int lo = rig.lower;
    if (!bonesOnly_) {
        Ellipsoid e;
        e.center = c - dir * (0.25f * rr);
        e.axis = dir;
        e.up = v;
        e.rAxis = 0.12f * rr;
        e.rU = e.rV = 0.92f * rr;
        e.lat = 2;
        e.lon = 8;
        emitEllipsoid(sp, e, [=](float, float, Vec3) { return one(lo); },
                      [](float, float) { return Mat{{0.06f, 0.06f, 0.07f}, MaterialClass::Dark}; });
    }
    // Wires: torn cables spilling out of the stump and drooping.
    static const Vec3 kWire[] = {{0.50f, 0.10f, 0.07f}, {0.09f, 0.09f, 0.10f}, {0.62f, 0.38f, 0.16f}, {0.16f, 0.24f, 0.45f}, {0.40f, 0.36f, 0.10f}};
    const int nw = 4 + r.rangeInt(0, 2);
    for (int k = 0; k < nw; ++k) {
        const float ang = kTau * float(k) / float(nw) + r.range(-0.3f, 0.3f), rad = r.range(0.25f, 0.6f) * rr;
        Vec3 p = c + (u * std::cos(ang) + v * std::sin(ang)) * rad - dir * (0.1f * rr);
        Vec3 d = normalize(dir + (u * std::cos(ang) + v * std::sin(ang)) * r.range(0.2f, 0.6f));
        const float len = r.range(0.28f, 0.55f) * h, wr = r.range(0.013f, 0.020f) * h;
        std::vector<Vec3> pts{p};
        for (int s = 0; s < 4; ++s) {
            d = normalize(d + Vec3{0, -0.45f, 0} + Vec3{r.range(-0.2f, 0.2f), 0, r.range(-0.2f, 0.2f)});
            p = p + d * (len / 4.0f);
            pts.push_back(p);
        }
        Loft L;
        for (size_t s = 0; s < pts.size(); ++s) {
            const Vec3 dd = normalize(s + 1 < pts.size() ? pts[s + 1] - pts[s] : pts[s] - pts[s - 1], dir);
            const Vec3 vv = normalize(cross(dd, Vec3{1, 0, 0}), Vec3{0, 1, 0}), uu = cross(dd, vv);
            L.rings.push_back(makeRing(pts[s], uu, vv, SuperRing{wr, 0, wr, wr}, 4));
            L.t.push_back(float(s) / float(pts.size() - 1));
        }
        L.poleEnd = true;
        L.endPole = pts.back() + normalize(pts.back() - pts[pts.size() - 2], dir) * wr;
        const Vec3 col = kWire[size_t(k) % 5];
        emitLoft(sp, L, [=](float, float, Vec3) { return one(lo); }, [=](float t, float) {
            return t > 0.9f ? Mat{{0.78f, 0.52f, 0.28f}, MaterialClass::Metal} : Mat{col, MaterialClass::Leather};  // bare copper ends
        });
    }
    wireParts1_ = parts.size();
}

// The cut applies to every surface (flesh, hand, shells) except the frame, which keeps
// a stub, and the socket and wires themselves.
void Builder::applyConstructCut() {
    if (!cutFlesh_) return;
    const Mat gone{{0, 0, 0}, MaterialClass::Count};
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].kind != SubmeshKind::Body || (i >= wireParts0_ && i < wireParts1_)) continue;
        const bool frame = i >= boneParts0_ && i < boneParts1_;
        parts[i].clips.push_back({frame ? cutFrame_ : cutFlesh_, gone, 0});
    }
}

}  // namespace cg::detail
