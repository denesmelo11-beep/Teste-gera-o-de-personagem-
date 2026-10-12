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
    b.bones = d < 0.35f ? b.bones : d < 0.45f ? (b.bones | 1 | 8) : d < 0.55f ? 15 : 0;
}

// The skeleton, built from the body's own landmarks: it sits inside the flesh at any
// decay stage and stands alone at full decay.
void Builder::buildBoneSkeleton() {
    const float h = m.h, tl = m.torsoLen;
    const RigInfo& r = ch.rig;
    const Vec3 boneC = bonesOnly_ ? Vec3{0.70f, 0.65f, 0.54f} : Vec3{0.62f, 0.55f, 0.44f};  // stained while flesh clings
    const Mat bone{boneC, MaterialClass::Horn}, dark{{0.06f, 0.05f, 0.05f}, MaterialClass::Dark};
    Part& first = newPart();
    first.id = bodyGroup_;
    const size_t Pi = parts.size() - 1;
    auto P = [&]() -> Part& { return parts[Pi]; };
    auto boneMat = [=](float, float) { return bone; };
    // A bone shaft with knobbly ends; wf gives the skin weight along it.
    auto tube = [&](const std::vector<Vec3>& pts, float r0, const std::function<SkinW(float)>& wf, int sides = 5, float knob = 0.4f) {
        const size_t n = pts.size();
        if (n < 2) return;
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
        e.lat = lat;
        e.lon = lon;
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
            {
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
                p.x *= 0.70f;
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
            const int n = 10;
            for (int k = 0; k < n; ++k) {
                const float xt = ((float(k) + 0.5f) / float(n) * 2.0f - 1.0f) * 0.10f * h * hw;
                Vec3 best = skullPt(kPi / 2, sT);
                for (int i = 0; i <= 40; ++i) {
                    const Vec3 q = skullPt(kPi / 2 + (float(i) / 40.0f - 0.5f) * 1.6f, sT);
                    if (std::fabs(q.x - o.x - xt) < std::fabs(best.x - o.x - xt)) best = q;
                }
                Ellipsoid e;
                e.center = best + Vec3{0, 0, -0.004f * h};
                e.axis = {0, 1, 0};
                e.up = {0, 0, 1};
                e.rAxis = 0.019f * h;
                e.rU = 0.0115f * h;
                e.rV = 0.011f * h;
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
                const Mat glowM{Vec3{std::min(gc.x, 1.0f), std::min(gc.y, 1.0f), std::min(gc.z, 1.0f)}, MaterialClass::Hair};
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
        e.rAxis = 0.034f * h;
        e.rU = 0.085f * h;
        e.rV = 0.07f * h;
        e.lat = 3;
        e.lon = 6;
        const SkinW w = spineW(t);
        emitEllipsoid(P(), e, [=](float, float, Vec3) { return w; }, boneMat);
        tube({c, c + Vec3{0, -0.04f * h, -0.10f * h}}, 0.022f * h, [=](float) { return w; }, 4, 0.0f);  // spinous process
    }
    {
        const Vec3 a = spineAt(1.0f), b2 = m.headOrigin + Vec3{0, 0.18f * h, -0.06f * h};
        for (int k = 0; k < 4; ++k) {
            const Vec3 c = lerp(a, b2, (float(k) + 0.5f) / 4.0f);
            blob(c, {0, 1, 0}, {0, 0, 1}, 0.03f * h, 0.07f * h, 0.06f * h, r.neck, 3, 6);
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
            tubeOne(pts, 0.03f * h, r.chest, 4, 0.0f);
        }
    }
    tubeOne({ribPoint(0.60f, kPi / 2, 0.86f), ribPoint(0.75f, kPi / 2, 0.86f), ribPoint(0.90f, kPi / 2, 0.86f)}, 0.025f * h, r.chest, 5, 0.2f);
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& a = m.arms[i];
        const int root = r.arms[i].root;
        const Vec3 st = ribPoint(0.90f, kPi / 2, 0.86f) + Vec3{a.side * 0.04f * h, 0, 0};
        tubeOne({st, lerp(st, a.shoulder, 0.5f) + Vec3{0, 0.03f * h, 0.02f * h}, a.shoulder + Vec3{0, 0.06f * h, 0}}, 0.018f * h, root, 5, 0.3f);
        if (a.pair == 0) {
            const Vec3 sc = ribPoint(0.82f, -kPi / 2 + a.side * 0.75f, 0.92f);
            blob(sc, normalize(Vec3{0, 0, -1}), {0, 1, 0}, 0.018f * h, 0.15f * h, 0.19f * h, root, 3, 5);
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
    for (size_t i = 0; i < m.arms.size(); ++i) {
        const ArmMarks& a = m.arms[i];
        const LimbRig& lr = r.arms[i];
        const float ar = a.radius;
        tubeOne({a.shoulder, lerp(a.shoulder, a.elbow, 0.5f), a.elbow}, 0.58f * ar, lr.upper, 6, 0.55f);
        const Vec3 d = normalize(a.wrist - a.elbow), v = orthoFront(d), u = cross(d, v);
        for (float o : {-1.0f, 1.0f})
            tubeOne({a.elbow + v * (o * 0.12f * ar), lerp(a.elbow, a.wrist, 0.5f) + v * (o * 0.2f * ar), a.wrist + v * (o * 0.16f * ar)},
                    0.32f * ar, lr.lower, 5, 0.45f);
        if (!bonesOnly_) continue;  // in a corpse the hands are still flesh: their bones would poke through
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
            tubeOne(pts, 0.05f * Lh, lr.end, 4, 0.3f);
        }
        tubeOne({a.wrist + d * (0.15f * Lh) + v * (0.2f * Lh), a.wrist + d * (0.35f * Lh) + v * (0.38f * Lh) + palm * (0.1f * Lh),
                 a.wrist + d * (0.55f * Lh) + v * (0.45f * Lh) + palm * (0.2f * Lh)},
                0.04f * Lh, lr.end, 4, 0.3f);  // thumb
    }

    // ---- legs and feet ----
    const float T = g.body.thigh * h;
    auto legBones = [&](const LimbRig& lr, Vec3 hip, Vec3 knee, Vec3 ankle, Vec3 ball, float side, bool longFoot, bool hoofed) {
        const Vec3 kneeDir = normalize(knee - hip), lat{side, 0, 0};
        tubeOne({hip + Vec3{-side * 0.05f * h, 0.03f * h, 0}, lerp(hip, knee, 0.5f), knee}, 0.34f * T, lr.upper, 6, 0.55f);  // femur
        blob(knee + Vec3{0, 0.01f * h, 0.12f * T + 0.03f * h}, {0, 0, 1}, {0, 1, 0}, 0.04f * h, 0.07f * h, 0.08f * h, lr.lower, 3, 5);  // patella
        tubeOne({knee, lerp(knee, ankle, 0.5f), ankle}, 0.28f * T, lr.lower, 6, 0.55f);  // tibia
        tubeOne({knee + lat * (0.08f * T) + Vec3{0, -0.04f * h, -0.06f * T}, lerp(knee, ankle, 0.5f) + lat * (0.12f * T) + Vec3{0, 0, -0.05f * T},
                 ankle + lat * (0.12f * T)},
                0.11f * T, lr.lower, 4, 0.4f);  // fibula
        (void)kneeDir;
        if (!bonesOnly_) return;  // feet stay flesh until the end
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
}

// Torn flesh: round holes through the skin onto the skeleton, growing with decay, each
// backed by a dark cavity floor behind the bones.
void Builder::addDecayHoles() {
    const float h = m.h, k = smoothstep(0.55f, 0.95f, decay_);
    const float side = (g.seed % 2 == 0) ? 1.0f : -1.0f;
    const float R0 = h * lerp(0.13f, 0.25f, k);
    const float T = g.body.thigh * h, Cf = g.body.calf * h;
    auto torsoOnly = [this, h](Vec3 q) {
        const float t = clamp(torsoT(q.y), -0.06f, 1.1f);
        return q.y > m.crotchY + 0.02f * h && std::fabs(q.x) < 1.25f * cHw_(t) + 0.03f * h;
    };
    auto add = [&](Vec3 c, float R, float depth, bool head, std::function<bool(Vec3)> gate) { holes_.push_back({c, R, depth, head, std::move(gate)}); };
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
        add(lerp(m.hipJoint[ls], m.knee[ls], 0.5f) + fw * (0.95f * T), h * lerp(0.10f, 0.18f, k), 0.9f * T, false,
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

    const Mat hole{{0, 0, 0}, MaterialClass::Count};
    for (const Hole& ho : holes_) {
        Part& p = parts[ho.head ? headPart_ : torsoPart_];
        const Vec3 c = ho.c;
        const float R = ho.R;
        const auto gate = ho.gate;
        p.clips.push_back({[=](Vec3 q) {
            if (gate && !gate(q)) return 1.0f;
            return length(q - c) - R;
        }, hole});
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
    const Mat flesh{{0.17f, 0.07f, 0.06f}, MaterialClass::Dark};
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

}  // namespace cg::detail
