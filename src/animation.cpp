#include "chargen/animation.hpp"

#include <functional>

#include "chargen/motion.hpp"

namespace cg {

Pose restPose(const Skeleton& sk) {
    Pose p;
    p.rot.assign(sk.bones.size(), Quat{});
    p.trans.assign(sk.bones.size(), Vec3{});
    return p;
}

Pose sampleClip(const Clip& clip, float time) {
    const size_t n = clip.frames.size();
    if (n == 0) return {};
    float f = time * clip.fps;
    size_t i0, i1;
    float t;
    if (clip.loop) {
        f = std::fmod(f, float(n));
        if (f < 0) f += float(n);
        i0 = size_t(f) % n;
        i1 = (i0 + 1) % n;
        t = f - std::floor(f);
    } else {
        f = clamp(f, 0.0f, float(n - 1));
        i0 = size_t(f);
        i1 = std::min(i0 + 1, n - 1);
        t = f - float(i0);
    }
    const Pose &a = clip.frames[i0], &b = clip.frames[i1];
    Pose out;
    out.rot.resize(a.rot.size());
    out.trans.resize(a.trans.size());
    for (size_t k = 0; k < a.rot.size(); ++k) {
        out.rot[k] = nlerp(a.rot[k], b.rot[k], t);
        out.trans[k] = lerp(a.trans[k], b.trans[k], t);
    }
    return out;
}

float sampleTension(const Clip& clip, float time) {
    const size_t n = clip.tension.size();
    if (n == 0) return 0.5f;
    float f = time * clip.fps;
    if (clip.loop) {
        f = std::fmod(f, float(n));
        if (f < 0) f += float(n);
        const size_t i = size_t(f) % n;
        return lerp(clip.tension[i], clip.tension[(i + 1) % n], f - std::floor(f));
    }
    f = clamp(f, 0.0f, float(n - 1));
    const size_t i = size_t(f);
    return lerp(clip.tension[i], clip.tension[std::min(i + 1, n - 1)], f - float(i));
}

FaceState sampleFace(const Clip& clip, float time) {
    FaceState s;
    if (clip.loop && clip.duration() > 0) time = std::fmod(time, clip.duration());
    for (auto& k : clip.faceKeys)
        if (k.first <= time) s = k.second;
    return s;
}

void poseMatrices(const Skeleton& sk, const Pose& pose, std::vector<Mat4>& global, std::vector<Mat4>& skin) {
    const size_t n = sk.bones.size();
    global.resize(n);
    skin.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Bone& b = sk.bones[i];
        Quat q = i < pose.rot.size() ? pose.rot[i] : Quat{};
        Vec3 t = b.restLocal + (i < pose.trans.size() ? pose.trans[i] : Vec3{});
        Mat4 local = Mat4::trs(t, q);
        global[i] = b.parent >= 0 ? global[size_t(b.parent)] * local : local;
        skin[i] = global[i] * Mat4::translation(-b.restWorld);
    }
}

void skinMesh(const Mesh& mesh, const std::vector<Mat4>& skin, std::vector<Vec3>& pos, std::vector<Vec3>& nrm) {
    const size_t n = mesh.vertices.size();
    pos.resize(n);
    nrm.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Vertex& v = mesh.vertices[i];
        Vec3 p{}, q{};
        for (int k = 0; k < 4; ++k) {
            float w = v.weights[k];
            if (w <= 0) continue;
            const Mat4& m = skin[v.joints[k]];
            p += m.point(v.pos) * w;
            q += m.dir(v.normal) * w;
        }
        pos[i] = p;
        nrm[i] = normalize(q);
    }
}

namespace {

// Helpers to author poses in terms of the rig.
struct Author {
    const Character& c;
    const RigInfo& r;
    float h;
    Pose p;
    float tension = 0.5f;

    explicit Author(const Character& ch) : c(ch), r(ch.rig), h(ch.height / ch.genome.body.headsTall) {}
    void reset() {
        p = restPose(c.skeleton);
        tension = 0.5f;
    }
    void rot(int bone, Quat q) {
        if (bone >= 0) p.rot[size_t(bone)] = q * p.rot[size_t(bone)];
    }
    void euler(int bone, float x, float y, float z) { rot(bone, Quat::euler(x, y, z)); }
    void move(int bone, Vec3 t) {
        if (bone >= 0) p.trans[size_t(bone)] += t;
    }
    // Arm: lower from the A-pose, swing forward/back, bend elbow (radians).
    void arm(size_t i, float lower, float swing, float elbow, float out = 0) {
        if (i >= r.arms.size()) return;
        const LimbRig& a = r.arms[i];
        Quat q = Quat::axisAngle({1, 0, 0}, swing) * Quat::axisAngle({0, 0, 1}, -a.side * (lower - out));
        rot(a.upper, q);
        Vec3 flex = normalize(cross(a.restDir, {0, 0, 1}));
        rot(a.lower, Quat::axisAngle(flex, elbow));
    }
    // Arm aimed at a direction (in chest space) with an elbow bend.
    void aim(size_t i, Vec3 dir, float elbow) {
        if (i >= r.arms.size()) return;
        const LimbRig& a = r.arms[i];
        rot(a.upper, fromTo(a.restDir, normalize(dir)));
        Vec3 flex = normalize(cross(a.restDir, {0, 0, 1}));
        rot(a.lower, Quat::axisAngle(flex, elbow));
    }
    // The extra (lower) pair at rest: hanging along the flanks with the forearms brought
    // forward, hands by the hips. `fwd` swings it forward (+) / back, `out` away from the body.
    Vec3 hang(size_t i, float fwd = 0, float out = 0) const {
        const float sd = i < r.arms.size() ? r.arms[i].side : 1.0f;
        return normalize(Vec3{sd * (0.28f + out), -0.93f, 0.22f + fwd});
    }
    void lowerArm(size_t i, float fwd, float out, float elbow) { aim(i, hang(i, fwd, out), elbow); }
    // Tail: raise (+) / drop, swing sideways, spread along the chain (curl > 0 bends the tip more).
    void tail(float lift, float sway, float curl = 0.5f) {
        const size_t n = r.tail.size();
        float tot = 0;
        for (size_t i = 0; i < n; ++i) tot += 1 + curl * float(i);
        for (size_t i = 0; i < n; ++i) {
            const float f = (1 + curl * float(i)) / tot;
            euler(r.tail[i], lift * f, sway * f, 0);
        }
    }
    // Serpent coil: a sideways wave travelling from the hips to the tip (slithering).
    void slither(float phase, float amp) {
        const size_t n = r.serpent.size();
        for (size_t i = 1; i < n; ++i) euler(r.serpent[i], 0, amp * std::sin(phase - 0.9f * float(i)) * (0.6f + 0.4f * float(i) / float(n)), 0);
    }
    // Wings: raise (+) / lower the arm, and open (+) / close the elbow and wrist.
    void wings(float raise, float open) {
        for (const LimbRig& w : r.wings) {
            euler(w.upper, 0, 0, w.side * raise);
            euler(w.lower, 0, -w.side * open * 0.6f, 0);
            euler(w.end, 0, -w.side * open * 0.4f, 0);
        }
    }
    // Rotation about a character-space axis on a spine bone (tilts toward a push).
    void tilt(int bone, Vec3 axis, float angle) {
        if (length(axis) > 1e-5f) rot(bone, Quat::axisAngle(axis, angle));
    }
    // Plant a foot: IK the leg so the ankle sits at its rest height (where it is in x/z) and
    // level the foot. weight 0..1 (0 = leave the authored leg alone).
    void plant(size_t i, float weight = 1.0f) {
        if (i >= r.legs.size() || weight <= 0) return;
        const LimbRig& L = r.legs[i];
        if (L.upper < 0 || L.lower < 0 || L.end < 0) return;
        const auto& bones = c.skeleton.bones;
        std::vector<Vec3> pos(bones.size());
        std::vector<Quat> q(bones.size());
        auto fk = [&]() {
            for (size_t b = 0; b < bones.size(); ++b) {
                const Vec3 t = bones[b].restLocal + p.trans[b];
                if (bones[b].parent < 0) pos[b] = t, q[b] = p.rot[b];
                else {
                    const size_t pa = size_t(bones[b].parent);
                    pos[b] = pos[pa] + rotate(q[pa], t);
                    q[b] = normalize(q[pa] * p.rot[b]);
                }
            }
        };
        fk();
        Vec3 goal = pos[size_t(L.end)];
        goal.y = lerp(goal.y, bones[size_t(L.end)].restWorld.y, saturate(weight));
        // Out of reach (a leg flung too far back): bring the foot in along the ground.
        const Vec3 hip = pos[size_t(L.upper)];
        const float len = 0.985f * (length(bones[size_t(L.lower)].restLocal) + length(bones[size_t(L.end)].restLocal));
        const float dy = hip.y - goal.y;
        Vec3 hz{goal.x - hip.x, 0, goal.z - hip.z};
        if (dy < len) {
            const float dmax = std::sqrt(len * len - dy * dy);
            if (length(hz) > dmax) hz = hz * (dmax / length(hz));
        } else {
            hz = {};
        }
        goal = {hip.x + hz.x, goal.y, hip.z + hz.z};
        twoBoneIK(c.skeleton, p, L.upper, L.lower, L.end, goal, {0, 0, 1}, 1.0f);
        fk();
        Quat w = q[size_t(L.end)];
        Vec3 f = rotate(w, {0, 0, 1});
        w = normalize(fromTo(f, normalize(Vec3{f.x, 0, f.z}, {0, 0, 1})) * w);
        w = normalize(fromTo(rotate(w, {0, 1, 0}), {0, 1, 0}) * w);
        const Quat flat = normalize(conj(q[size_t(L.lower)]) * w);
        p.rot[size_t(L.end)] = nlerp(p.rot[size_t(L.end)], flat, saturate(weight));
    }
    void plantAll(float weight = 1.0f) {
        for (size_t i = 0; i < r.legs.size(); ++i) plant(i, weight);
    }
    void leg(size_t i, float swing, float knee, float foot, float spread = 0) {
        if (i >= r.legs.size()) return;
        const LimbRig& l = r.legs[i];
        rot(l.upper, Quat::axisAngle({0, 0, 1}, l.side * spread) * Quat::axisAngle({1, 0, 0}, swing));
        rot(l.lower, Quat::axisAngle({1, 0, 0}, knee));
        rot(l.end, Quat::axisAngle({1, 0, 0}, foot));
    }
};

}  // namespace

void limitPose(const RigInfo& rig, Pose& pose) {
    for (const JointLimit& L : rig.limits) {
        if (L.bone < 0 || size_t(L.bone) >= pose.rot.size()) continue;
        Quat q = normalize(pose.rot[size_t(L.bone)]);
        if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
        if (L.hinge) {
            // Keep only the twist about the hinge axis, clamped to the range.
            const Vec3 ax = normalize(L.axis);
            const float proj = q.x * ax.x + q.y * ax.y + q.z * ax.z;
            const float ang = 2.0f * std::atan2(proj, q.w);
            pose.rot[size_t(L.bone)] = Quat::axisAngle(ax, clamp(ang, L.minAngle, L.maxAngle));
        } else {
            const float ang = 2.0f * std::acos(clamp(q.w, -1.0f, 1.0f));
            if (ang > L.maxAngle) {
                const Vec3 ax = normalize(Vec3{q.x, q.y, q.z}, Vec3{1, 0, 0});
                pose.rot[size_t(L.bone)] = Quat::axisAngle(ax, L.maxAngle);
            }
        }
    }
}

namespace {

Clip author(const Character& c, std::string name, int frames, float fps, bool loop,
            const std::function<void(Author&, float)>& fn) {
    Clip clip;
    clip.name = std::move(name);
    clip.fps = fps;
    clip.loop = loop;
    Author a(c);
    for (int f = 0; f < frames; ++f) {
        a.reset();
        float t = loop ? float(f) / frames : float(f) / std::max(1, frames - 1);
        fn(a, t);
        limitPose(c.rig, a.p);
        clip.frames.push_back(a.p);
        clip.tension.push_back(saturate(a.tension));
    }
    return clip;
}


// Motion phrasing: each beat says how the value arrives there. Anticipation eases In,
// strikes Snap (explosive start, long settle), follow-through overshoots (Back).
enum class Ease : uint8_t { Smooth, In, Out, Snap, Back, Linear };
struct Beat {
    float t, v;
    Ease e = Ease::Smooth;
};
float shape(Ease e, float x) {
    switch (e) {
        case Ease::In: return x * x * x;
        case Ease::Out: return 1 - (1 - x) * (1 - x) * (1 - x);
        case Ease::Snap: {
            const float y = 1 - x;
            return 1 - y * y * y * y * y;
        }
        case Ease::Back: {
            const float s = 1.9f, y = x - 1;
            return 1 + y * y * ((s + 1) * y + s);
        }
        case Ease::Linear: return x;
        default: return x * x * (3 - 2 * x);
    }
}
float phrase(std::initializer_list<Beat> keys, float t) {
    const Beat* k = keys.begin();
    if (t <= k[0].t) return k[0].v;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
        if (t <= k[i + 1].t) return lerp(k[i].v, k[i + 1].v, shape(k[i + 1].e, (t - k[i].t) / std::max(1e-5f, k[i + 1].t - k[i].t)));
    return k[keys.size() - 1].v;
}
// Hind legs of a four-legged body are indices 2 and 3.
bool hind(size_t leg) { return leg >= 2; }

// ---------------------------------------------------------------- beasts on all fours
//
// The trunk lies along +z (hips at the back, chest in front), legs[0..1] are the forelegs
// and legs[2..3] the hind legs; the arms are folded away inside the chest. Pitch about x
// tips the front down (+) or up (-), yaw about y turns the nose to +x, roll about z.

bool beastBody(const Character& c) {
    const BodyShape& b = c.genome.body;
    return b.stance == 1 && !b.centaur && !b.lowerBody && c.rig.legs.size() >= 4;
}

// A gait: the share of the cycle each foot is on the ground, the swing of the leg
// (radians either side), how high it folds, and when each leg starts its stance.
struct Gait {
    float duty, stride, lift;
    float phase[4];  // fore L, fore R, hind L, hind R
};

// One leg through its cycle: planted, it travels from reaching forward to pushing back;
// lifted, it folds and swings forward again. Returns how planted it is.
float gaitLeg(Author& a, size_t i, float t, const Gait& g) {
    float p = t + g.phase[std::min<size_t>(i, 3)];
    p -= std::floor(p);
    const bool hindLeg = i >= 2;
    float sw, knee, foot, planted;
    if (p < g.duty) {
        const float u = p / g.duty;
        sw = g.stride * (2.0f * u - 1.0f);
        knee = 0.08f + 0.12f * std::sin(kPi * u);  // the leg yields as it takes the weight
        foot = 0.0f;
        planted = 1.0f;
    } else {
        const float u = (p - g.duty) / (1.0f - g.duty), l = std::sin(kPi * u);
        sw = g.stride * (1.0f - 2.0f * shape(Ease::Smooth, u));
        knee = 0.08f + g.lift * l * (hindLeg ? 1.0f : 1.2f);
        foot = (hindLeg ? -0.35f : 0.7f) * g.lift * l;  // the forepaw curls back, the hind paw trails
        planted = 1.0f - smoothstep(0.0f, 0.12f, u) * (1.0f - smoothstep(0.88f, 1.0f, u));
    }
    a.leg(i, sw, knee, foot);
    return planted;
}

void beastLegs(Author& a, float t, const Gait& g) {
    float w[4];
    for (size_t i = 0; i < 4; ++i) w[i] = gaitLeg(a, i, t, g);
    for (size_t i = 0; i < 4; ++i) a.plant(i, w[i]);
}

Clip beastIdle(const Character& c) {
    Clip clip = author(c, "idle", 120, 30, true, [&](Author& a, float t) {
        const float ph = kTau * t, breath = std::sin(ph * 3);
        a.tension = 0.3f;
        a.move(a.r.hips, {0.012f * a.h * std::sin(ph), -0.006f * a.h * (1 + breath), 0});
        a.euler(a.r.hips, 0, 0.03f * std::sin(ph), 0.02f * std::sin(ph));
        a.euler(a.r.chest, -0.015f * breath, 0, 0);
        // Head low and searching: it turns, pauses, sniffs.
        const float look = std::sin(ph) + 0.35f * std::sin(2 * ph + 1.0f);
        a.euler(a.r.neck, 0.05f * std::sin(2 * ph + 0.5f), 0.22f * look, 0);
        a.euler(a.r.head, 0.06f * std::sin(4 * ph) * std::max(0.0f, std::cos(ph)), 0.12f * look, 0.06f * std::sin(ph + 0.3f));
        for (size_t i = 0; i < a.r.legs.size(); ++i) a.leg(i, 0, 0.06f + 0.02f * breath, 0);
        a.tail(0.1f + 0.05f * breath, 0.3f * std::sin(ph + 0.8f), 0.8f);
        a.wings(0.03f * breath, 0);
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {}}};
    return clip;
}

// Four-beat walk (hind, fore on the same side, then the other side): always three feet down.
Clip beastWalk(const Character& c) {
    const Gait g{0.68f, 0.30f, 0.85f, {0.25f, 0.75f, 0.0f, 0.5f}};
    Clip clip = author(c, "walk", 40, 30, true, [&](Author& a, float t) {
        const float ph = kTau * t;
        a.tension = 0.45f;
        a.move(a.r.hips, {0.012f * a.h * std::sin(ph), 0.012f * a.h * std::cos(2 * ph) - 0.01f * a.h, 0});
        a.euler(a.r.hips, 0, 0.07f * std::sin(ph), 0.04f * std::sin(ph));
        a.euler(a.r.spine, 0, -0.04f * std::sin(ph), 0);
        a.euler(a.r.chest, 0, -0.06f * std::sin(ph + 0.5f), -0.04f * std::sin(ph + kPi / 2));
        a.euler(a.r.neck, 0.05f * std::sin(2 * ph + 1.2f), 0.05f * std::sin(ph + 1.0f), 0);  // the head nods with the forelegs
        a.euler(a.r.head, -0.04f * std::sin(2 * ph + 1.2f), 0.03f * std::sin(ph + 1.4f), 0);
        a.tail(0.05f, -0.35f * std::sin(ph + 0.6f), 0.8f);
        a.wings(0, 0);
        beastLegs(a, t, g);
    });
    clip.faceKeys = {{0.0f, {}}};
    clip.events = {{0.75f * clip.duration(), "footL"}, {0.25f * clip.duration(), "footR"}};
    return clip;
}

// Trot: diagonal pairs together, a springy bounce on every beat.
Clip beastTrot(const Character& c) {
    const Gait g{0.48f, 0.42f, 1.05f, {0.0f, 0.5f, 0.5f, 0.0f}};
    Clip clip = author(c, "trot", 24, 30, true, [&](Author& a, float t) {
        const float ph = kTau * t;
        a.tension = 0.55f;
        a.move(a.r.hips, {0, 0.035f * a.h * std::cos(2 * ph) - 0.02f * a.h, 0});
        a.euler(a.r.hips, 0.03f, 0.05f * std::sin(ph), 0.05f * std::sin(ph));
        a.euler(a.r.chest, -0.02f * std::cos(2 * ph), -0.05f * std::sin(ph), -0.05f * std::sin(ph));
        a.euler(a.r.neck, -0.05f + 0.06f * std::cos(2 * ph + 0.8f), 0, 0);
        a.euler(a.r.head, 0.05f - 0.05f * std::cos(2 * ph + 0.8f), 0, 0);
        a.tail(0.2f + 0.1f * std::cos(2 * ph), -0.3f * std::sin(ph + 0.5f), 0.6f);
        a.wings(-0.05f, -0.1f);
        beastLegs(a, t, g);
    });
    clip.faceKeys = {{0.0f, {}}};
    clip.events = {{0.0f, "footL"}, {0.5f * clip.duration(), "footR"}};
    return clip;
}

// Rotary gallop: hind left, hind right, fore right, fore left, then all four off the
// ground. The spine coils as the hind legs reach under the chest and stretches out as
// they drive; the head counters the pitch of the body.
Clip beastGallop(const Character& c) {
    const Gait g{0.36f, 0.62f, 1.35f, {0.55f, 0.45f, 0.0f, 0.9f}};
    Clip clip = author(c, "run", 18, 30, true, [&](Author& a, float t) {
        const float ph = kTau * t, coil = std::sin(ph - 0.6f);
        a.tension = 0.7f;
        a.move(a.r.hips, {0, 0.07f * a.h * std::max(0.0f, std::sin(ph + 1.2f)) - 0.05f * a.h, 0});
        a.euler(a.r.hips, 0.12f * std::sin(ph + 0.3f), 0.03f * std::sin(ph), 0);
        a.euler(a.r.spine, 0.16f * coil, 0, 0);
        a.euler(a.r.chest, 0.10f * coil - 0.08f * std::sin(ph + 0.3f), 0, 0);
        a.euler(a.r.neck, -0.12f - 0.14f * std::sin(ph + 2.0f), 0, 0);
        a.euler(a.r.head, 0.18f + 0.06f * std::sin(ph + 2.4f), 0, 0);
        a.tail(0.15f + 0.15f * std::sin(ph + 1.5f), 0.08f * std::sin(ph), 0.4f);  // streams out behind
        a.wings(-0.15f, -0.3f);
        beastLegs(a, t, g);
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Open}}};
    clip.events = {{0.45f * clip.duration(), "footL"}, {0.55f * clip.duration(), "footR"}};
    return clip;
}

// Bite: sink back on the haunches with the head drawn in, then a snapping lunge, the
// forelegs reaching, the jaws closing at full stretch, and a shake before letting go.
Clip beastBite(const Character& c) {
    const float dur = 1.1f;
    Clip clip = author(c, "attack", int(dur * 30), 30, false, [&](Author& a, float t) {
        const float coil = phrase({{0.0f, 0.0f}, {0.28f, 1.0f, Ease::Out}, {0.36f, 0.0f, Ease::Snap}}, t);
        const float lunge = phrase({{0.28f, 0.0f}, {0.38f, 1.0f, Ease::Snap}, {0.62f, 0.85f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float snap = phrase({{0.34f, 0.0f}, {0.4f, 1.0f, Ease::Snap}, {0.5f, 0.6f}, {0.75f, 0.0f}}, t);
        const float shake = phrase({{0.42f, 0.0f}, {0.48f, 1.0f}, {0.66f, 0.0f}}, t) * std::sin(t * dur * kTau * 7.0f);
        a.tension = 0.35f + 0.65f * lunge;
        a.move(a.r.hips, {0, (-0.16f * coil + 0.04f * lunge) * a.h, (-0.12f * coil + 0.42f * lunge) * a.h});
        a.euler(a.r.hips, -0.08f * coil + 0.05f * lunge, 0, 0);
        a.euler(a.r.spine, 0.06f * coil - 0.05f * lunge, 0, 0);
        a.euler(a.r.chest, 0.12f * coil - 0.1f * lunge, 0.08f * shake, 0);
        a.euler(a.r.neck, 0.25f * coil - 0.3f * lunge, 0.12f * shake, 0.1f * shake);
        a.euler(a.r.head, -0.3f * coil - 0.2f * lunge + 0.35f * snap, 0.1f * shake, 0);
        for (size_t i = 0; i < 2; ++i) a.leg(i, 0.2f * coil - 0.55f * lunge, 0.35f * coil + 0.35f * std::sin(kPi * saturate(lunge)), 0.2f * coil);
        for (size_t i = 2; i < 4; ++i) a.leg(i, -0.15f * coil + 0.45f * lunge, 0.55f * coil, -0.2f * coil);
        a.tail(0.15f * coil + 0.45f * lunge, 0.25f * shake, 0.7f);
        a.wings(0.4f * lunge, 0.35f * lunge);
        a.plantAll(1.0f - 0.6f * std::sin(kPi * saturate((t - 0.3f) / 0.16f)) * float(t > 0.3f && t < 0.46f));
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Frown}}, {0.3f * dur, {EyeExpr::Angry, MouthExpr::Shout}},
                     {0.75f * dur, {EyeExpr::Angry, MouthExpr::Neutral}}};
    clip.events = {{0.4f * dur, "contact"}};
    return clip;
}

// Swipe: rear back onto the haunches, raise a forepaw high and rake it down and across.
Clip beastSwipe(const Character& c) {
    const float dur = 1.0f;
    Clip clip = author(c, "swipe", int(dur * 30), 30, false, [&](Author& a, float t) {
        const float rear = phrase({{0.0f, 0.0f}, {0.3f, 1.0f, Ease::Out}, {0.62f, 0.7f}, {1.0f, 0.0f}}, t);
        const float rake = phrase({{0.3f, 0.0f}, {0.42f, 1.0f, Ease::Snap}, {0.66f, 1.0f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float sd = a.r.legs[0].side;
        a.tension = 0.4f + 0.6f * rake;
        a.move(a.r.hips, {0, -0.08f * a.h * rear, -0.1f * a.h * rear + 0.14f * a.h * rake});
        a.euler(a.r.hips, -0.26f * rear + 0.08f * rake, 0, sd * 0.06f * rear);
        a.euler(a.r.spine, -0.14f * rear + 0.1f * rake, 0, 0);
        a.euler(a.r.chest, -0.1f * rear + 0.12f * rake, sd * 0.15f * rear - sd * 0.3f * rake, sd * 0.08f * rear);
        a.euler(a.r.neck, -0.1f * rear + 0.1f * rake, -sd * 0.2f * rake, 0);
        a.euler(a.r.head, 0.05f * rake, -sd * 0.1f * rake, 0);
        // The striking forepaw: up and out, then down and across the body.
        a.leg(0, -1.5f * rear + 1.7f * rake, 1.1f * rear * (1.0f - rake) + 0.2f, -0.5f * rear + 0.4f * rake, 0.5f * rear - 0.6f * rake);
        a.leg(1, 0.1f * rear, 0.25f * rear, 0);
        for (size_t i = 2; i < 4; ++i) a.leg(i, -0.3f * rear, 0.8f * rear, -0.3f * rear);
        a.tail(0.3f * rear + 0.2f * rake, sd * 0.4f * rake, 0.7f);
        a.wings(0.5f * rear, 0.4f * rear);
        const float up = std::sin(kPi * saturate(t / 0.85f));
        a.plant(0, 1.0f - up);
        for (size_t i = 1; i < 4; ++i) a.plant(i);
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Frown}}, {0.35f * dur, {EyeExpr::Angry, MouthExpr::Shout}},
                     {0.8f * dur, {EyeExpr::Angry, MouthExpr::Neutral}}};
    clip.events = {{0.42f * dur, "contact"}};
    return clip;
}

// Dodge: crouch, spring sideways with the body twisting in the air, land on the forepaws.
Clip beastDodge(const Character& c) {
    Clip clip = author(c, "dodge", 30, 30, false, [&](Author& a, float t) {
        const float crouch = phrase({{0.0f, 0.0f}, {0.18f, 1.0f, Ease::Out}, {0.28f, 0.0f, Ease::Snap}, {0.58f, 0.0f}, {0.68f, 0.8f, Ease::Out}, {1.0f, 0.0f}}, t);
        const float air = phrase({{0.22f, 0.0f}, {0.4f, 1.0f, Ease::Out}, {0.58f, 0.0f, Ease::In}}, t);
        const float side = phrase({{0.2f, 0.0f}, {0.58f, 1.0f, Ease::Out}, {1.0f, 0.0f, Ease::Smooth}}, t);
        const float twist = phrase({{0.2f, 0.0f}, {0.42f, 1.0f}, {0.62f, 0.2f}, {1.0f, 0.0f}}, t);
        a.tension = 0.5f + 0.4f * air;
        a.move(a.r.hips, {0.5f * a.h * side, (-0.14f * crouch + 0.25f * air) * a.h, 0});
        a.euler(a.r.hips, 0, 0.25f * twist, -0.15f * twist);
        a.euler(a.r.chest, 0, 0.2f * twist, -0.1f * twist);
        a.euler(a.r.neck, 0.1f * crouch, -0.25f * twist, 0);
        for (size_t i = 0; i < 4; ++i) {
            const bool hindLeg = i >= 2;
            a.leg(i, (hindLeg ? 0.3f : -0.35f) * air, 0.6f * crouch + 0.5f * air, (hindLeg ? -0.3f : 0.5f) * air, 0.2f * air);
        }
        a.tail(0.3f * air, -0.6f * twist, 0.6f);
        a.wings(0.6f * air, 0.4f * air);
        a.plantAll(1.0f - smoothstep(0.0f, 0.2f, air));
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Neutral}}, {0.3f, {EyeExpr::Angry, MouthExpr::Open}}, {0.7f, {EyeExpr::Neutral, MouthExpr::Neutral}}};
    clip.events = {{0.58f, "land"}};
    return clip;
}

// Howl (the beast's "cast"): sit back on the haunches, throw the head up and hold the note.
Clip beastHowl(const Character& c) {
    const float dur = 2.0f;
    Clip clip = author(c, "cast", int(dur * 30), 30, false, [&](Author& a, float t) {
        const float sit = phrase({{0.0f, 0.0f}, {0.3f, 1.0f, Ease::Smooth}, {0.82f, 1.0f}, {1.0f, 0.0f}}, t);
        const float howl = phrase({{0.25f, 0.0f}, {0.45f, 1.0f, Ease::Out}, {0.8f, 1.0f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float quaver = howl * std::sin(t * dur * kTau * 5.0f);
        a.tension = 0.3f + 0.4f * howl;
        a.move(a.r.hips, {0, -0.22f * a.h * sit, -0.05f * a.h * sit});
        a.euler(a.r.hips, -0.32f * sit, 0, 0);  // the front rises over the sitting haunches
        a.euler(a.r.spine, -0.06f * sit, 0, 0);
        a.euler(a.r.chest, -0.06f * sit - 0.05f * howl, 0, 0);
        a.euler(a.r.neck, -0.55f * howl + 0.01f * quaver, 0, 0);
        a.euler(a.r.head, -0.35f * howl, 0.03f * quaver, 0);
        for (size_t i = 0; i < 2; ++i) a.leg(i, -0.32f * sit, 0.1f * sit, 0.2f * sit);
        for (size_t i = 2; i < 4; ++i) a.leg(i, -0.6f * sit, 1.0f * sit, -0.5f * sit);
        a.tail(-0.2f * sit, 0.1f * quaver, 0.6f);
        a.wings(0.3f * howl, 0.2f * howl);
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Neutral, MouthExpr::Neutral}}, {0.4f * dur, {EyeExpr::Blink, MouthExpr::Oh}}, {0.85f * dur, {EyeExpr::Neutral, MouthExpr::Neutral}}};
    clip.events = {{0.45f * dur, "release"}};
    return clip;
}

// Snarl (the beast's "roar"): hackles up, the forequarters low, the head thrust forward and
// shaking, the tail rigid.
Clip beastSnarl(const Character& c) {
    const float dur = 2.0f;
    Clip clip = author(c, "roar", int(dur * 30), 30, false, [&](Author& a, float t) {
        const float low = phrase({{0.0f, 0.0f}, {0.2f, 1.0f, Ease::Out}, {0.85f, 1.0f}, {1.0f, 0.0f}}, t);
        const float roar = phrase({{0.18f, 0.0f}, {0.26f, 1.0f, Ease::Snap}, {0.8f, 0.8f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float shake = roar * std::sin(t * dur * kTau * 9.0f);
        a.tension = 0.4f + 0.5f * roar;
        a.move(a.r.hips, {0, -0.06f * a.h * low, -0.08f * a.h * low + 0.06f * a.h * roar});
        a.euler(a.r.hips, -0.05f * low, 0, 0);
        a.euler(a.r.chest, 0.14f * low, 0, 0.02f * shake);
        a.euler(a.r.neck, 0.1f * low - 0.12f * roar, 0.05f * shake, 0);
        a.euler(a.r.head, -0.2f * roar + 0.03f * shake, 0.04f * shake, 0);
        for (size_t i = 0; i < 2; ++i) a.leg(i, -0.15f * low, 0.55f * low, 0.25f * low, 0.12f * low);
        for (size_t i = 2; i < 4; ++i) a.leg(i, 0.05f * low, 0.15f * low, 0, 0.08f * low);
        a.tail(0.45f * roar, 0.05f * shake, 0.3f);
        a.wings(0.2f + 0.5f * roar, 0.5f * roar);
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Neutral, MouthExpr::Neutral}}, {0.2f * dur, {EyeExpr::Angry, MouthExpr::Shout}},
                     {0.88f * dur, {EyeExpr::Angry, MouthExpr::Neutral}}};
    clip.events = {{0.25f * dur, "roar"}};
    return clip;
}

// Recoil of a body on four legs: shoved along the blow, it rolls and pitches with it, the
// head whips after, the legs on the struck side splay to catch it.
void beastRecoil(Author& a, Vec3 push, float k, float kh, float s, float stepLift) {
    a.move(a.r.hips, push * (0.12f * a.h * k * s) + Vec3{0, -0.04f * a.h * k * s, 0});
    a.euler(a.r.hips, 0.05f * push.z * k * s, 0.1f * push.x * k * s, -0.12f * push.x * k * s);
    a.euler(a.r.spine, 0.08f * push.z * k * s, 0.12f * push.x * k * s, -0.1f * push.x * k * s);
    a.euler(a.r.chest, 0.1f * push.z * k * s - 0.05f * k * s, 0.15f * push.x * k * s, -0.1f * push.x * k * s);
    a.euler(a.r.neck, -0.15f * kh * s + 0.15f * push.z * kh * s, 0.35f * push.x * kh * s, 0);
    a.euler(a.r.head, -0.2f * kh * s, 0.2f * push.x * kh * s, 0.2f * push.x * kh * s);
    for (size_t i = 0; i < a.r.legs.size(); ++i) {
        const float sd = a.r.legs[i].side;
        a.leg(i, 0.2f * push.z * k * s * (i >= 2 ? 1.0f : -1.0f), 0.3f * k * s, -0.1f * k * s, std::max(0.0f, sd * push.x) * 0.35f * k * s);
    }
    a.tail(-0.3f * k * s, -push.x * 0.6f * k * s, 0.8f);
    for (size_t i = 0; i < a.r.legs.size(); ++i) a.plant(i, i % 2 == 1 ? 1.0f - stepLift : 1.0f);
}

Clip beastHit(const Character& c, Vec3 push, float s) {
    Clip clip = author(c, "hit", 24, 30, false, [&](Author& a, float t) {
        const float k = phrase({{0.0f, 0.0f}, {0.12f, 1.0f, Ease::Snap}, {0.3f, 0.85f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float kh = phrase({{0.0f, 0.0f}, {0.06f, 0.0f}, {0.2f, 1.0f, Ease::Snap}, {0.4f, 0.7f}, {1.0f, 0.0f, Ease::Back}}, t);
        a.tension = phrase({{0.0f, 0.1f}, {0.25f, 0.2f}, {1.0f, 0.5f}}, t);
        beastRecoil(a, push, k, kh, s, phrase({{0.0f, 0.0f}, {0.08f, 1.0f, Ease::Out}, {0.22f, 0.0f, Ease::In}}, t));
    });
    const float d = clip.duration();
    clip.faceKeys = {{0.0f, {EyeExpr::Hurt, MouthExpr::Shout}}, {0.45f * d, {EyeExpr::Hurt, MouthExpr::Frown}},
                     {0.8f * d, {EyeExpr::Angry, MouthExpr::Neutral}}};
    clip.events = {{0.0f, "impact"}};
    return clip;
}

// Death: the forelegs fold first and the chest drops, the haunches follow, and the body
// rolls onto its side away from the blow before the ragdoll takes it.
Clip beastDeath(const Character& c, Vec3 push) {
    Clip clip = author(c, "death", 36, 30, false, [&](Author& a, float t) {
        const float k = phrase({{0.0f, 0.0f}, {0.1f, 1.0f, Ease::Snap}, {0.3f, 0.6f}, {1.0f, 0.3f}}, t);
        const float kh = phrase({{0.0f, 0.0f}, {0.05f, 0.0f}, {0.16f, 1.0f, Ease::Snap}, {1.0f, 0.5f}}, t);
        const float fore = phrase({{0.08f, 0.0f}, {0.42f, 1.0f, Ease::In}, {1.0f, 1.0f}}, t);
        const float back = phrase({{0.18f, 0.0f}, {0.6f, 1.0f, Ease::In}, {1.0f, 1.0f}}, t);
        const float roll = phrase({{0.3f, 0.0f}, {1.0f, 1.0f, Ease::In}}, t);
        const float side = push.x >= 0 ? 1.0f : -1.0f;
        a.tension = 0.08f;
        beastRecoil(a, push, k, kh, 1.1f, phrase({{0.0f, 0.0f}, {0.06f, 1.0f, Ease::Out}, {0.16f, 0.0f, Ease::In}}, t));
        a.move(a.r.hips, {0, -0.3f * a.h * back, 0});
        a.euler(a.r.hips, 0.12f * fore - 0.05f * back, 0, -side * 0.5f * roll);
        a.euler(a.r.spine, 0.1f * fore, 0, -side * 0.2f * roll);
        a.euler(a.r.chest, 0.12f * fore, 0, -side * 0.15f * roll);
        a.euler(a.r.neck, 0.35f * fore, side * 0.2f * roll, 0);
        a.euler(a.r.head, 0.25f * fore, 0, side * 0.3f * roll);
        for (size_t i = 0; i < 2; ++i) a.leg(i, -0.4f * fore, 1.7f * fore, 0.5f * fore, 0.2f * roll);
        for (size_t i = 2; i < 4; ++i) a.leg(i, -0.35f * back, 1.4f * back, -0.4f * back, 0.2f * roll);
        a.tail(-0.45f * back, side * 0.3f * roll, 0.5f);
        a.wings(-0.3f * back, -0.3f * back);
        a.plantAll(smoothstep(0.1f, 0.25f, t) * (1.0f - roll));
    });
    const float d = clip.duration();
    clip.faceKeys = {{0.0f, {EyeExpr::Hurt, MouthExpr::Shout}}, {0.25f * d, {EyeExpr::Hurt, MouthExpr::Open}},
                     {0.6f * d, {EyeExpr::Blink, MouthExpr::Oh}}};
    clip.events = {{0.0f, "impact"}, {0.5f * d, "ragdoll"}};
    return clip;
}

}  // namespace

Clip makeIdleClip(const Character& c) {
    if (beastBody(c)) return beastIdle(c);
    const float fem = c.genome.femininity;
    Clip clip = author(c, "idle", 90, 30, true, [&](Author& a, float t) {
        float ph = kTau * t;
        float breath = std::sin(ph * 2);
        a.tension = 0.35f;
        a.move(a.r.hips, {0.012f * a.h * std::sin(ph), -0.006f * a.h * (1 + std::cos(ph * 2)), 0});
        a.euler(a.r.hips, 0, 0.03f * std::sin(ph), 0.035f * std::sin(ph) * (0.5f + fem));
        a.euler(a.r.spine, -0.02f * breath, 0, -0.02f * std::sin(ph));
        a.euler(a.r.chest, -0.025f * breath, -0.02f * std::sin(ph), -0.01f * std::sin(ph));
        a.euler(a.r.neck, 0.02f * breath, 0, 0);
        a.euler(a.r.head, 0.03f + 0.02f * std::sin(ph + 1), 0.08f * std::sin(ph * 0.5f + 0.3f), 0.02f * std::sin(ph));
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            bool lower = i >= 2;
            float sd = (i % 2 == 0) ? 1.0f : -1.0f;
            if (lower) a.lowerArm(i, 0.06f * std::sin(ph + 1.7f + sd), 0.03f * breath, 0.75f + 0.06f * breath);  // own rhythm, not a copy
            else a.arm(i, 0.62f, 0.05f * std::sin(ph + sd), 0.25f + 0.05f * breath);
            if (i < 2) a.euler(a.r.arms[i].root, 0, 0, 0.015f * sd * breath);
        }
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            // Counter the pelvis tilt so the feet stay planted.
            a.leg(i, 0, 0.03f, 0, -0.035f * std::sin(ph) * (0.5f + fem) * a.r.legs[i].side);
        }
        a.tail(0.06f + 0.04f * breath, 0.18f * std::sin(ph + 0.8f), 0.8f);
        a.wings(0.04f * breath, 0);
        a.slither(ph, 0.04f);
        a.plantAll();
    });
    // Blinks.
    clip.faceKeys = {{0.0f, {}}, {1.1f, {EyeExpr::Blink, MouthExpr::Neutral}}, {1.22f, {}},
                     {2.4f, {EyeExpr::Blink, MouthExpr::Neutral}}, {2.5f, {}}};
    return clip;
}

Clip makeWalkClip(const Character& c) {
    if (beastBody(c)) return beastWalk(c);
    const float fem = c.genome.femininity;
    Clip clip = author(c, "walk", 30, 30, true, [&](Author& a, float t) {
        float ph = kTau * t, s = std::sin(ph), co = std::cos(ph);
        a.tension = 0.45f;
        a.move(a.r.hips, {0.02f * a.h * s * (0.4f + fem), 0.03f * a.h * std::cos(2 * ph) - 0.02f * a.h, 0});
        a.euler(a.r.hips, 0.03f, 0.12f * s, 0.05f * co * (0.4f + fem));
        a.euler(a.r.spine, 0.03f, -0.06f * s, -0.03f * co);
        a.euler(a.r.chest, 0.0f, -0.12f * s, 0);
        a.euler(a.r.head, 0.0f, 0.06f * s, 0);
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            float k = (i % 2 == 0 ? 1.0f : -1.0f) * (i >= 2 ? -1.0f : 1.0f);  // four legs: diagonal pairs (trot)
            float sw = -0.45f * s * k;
            float lift = std::max(0.0f, co * k);
            float knee = 0.12f + 0.75f * std::pow(lift, 1.5f);
            a.leg(i, sw - 0.1f * lift, knee, -0.3f * knee * 0.6f + 0.2f * std::max(0.0f, -sw));
        }
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            float k = (i % 2 == 0) ? 1.0f : -1.0f;
            float sw = 0.42f * s * k * (i >= 2 ? -0.8f : 1.0f);
            if (i >= 2) a.lowerArm(i, 0.55f * sw, 0.0f, 0.8f + 0.3f * std::max(0.0f, sw));
            else a.arm(i, 0.66f, sw, 0.25f + 0.25f * std::max(0.0f, -sw));
        }
        a.tail(0.05f, -0.3f * s, 0.8f);
        a.wings(0.04f * co, 0);
        a.slither(ph, 0.14f);
    });
    clip.faceKeys = {{0.0f, {}}};
    clip.events = {{0.25f * clip.duration(), "footL"}, {0.75f * clip.duration(), "footR"}};
    return clip;
}

Clip makeRunClip(const Character& c) {
    if (beastBody(c)) return beastGallop(c);
    Clip clip = author(c, "run", 20, 30, true, [&](Author& a, float t) {
        float ph = kTau * t, s = std::sin(ph), co = std::cos(ph);
        a.tension = 0.6f;
        a.move(a.r.hips, {0, 0.05f * a.h * std::fabs(std::cos(ph)) - 0.06f * a.h, 0});
        a.euler(a.r.hips, 0.12f, 0.18f * s, 0.04f * co);
        a.euler(a.r.spine, 0.18f, -0.1f * s, 0);
        a.euler(a.r.chest, 0.1f, -0.2f * s, 0);
        a.euler(a.r.head, -0.25f, 0.08f * s, 0);
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            float k = (i % 2 == 0 ? 1.0f : -1.0f) * (i >= 2 ? -1.0f : 1.0f);  // four legs: diagonal pairs (trot)
            float sw = -0.85f * s * k;
            float lift = std::max(0.0f, co * k);
            float knee = 0.3f + 1.5f * std::pow(lift, 1.2f);
            a.leg(i, sw - 0.35f * lift - 0.1f, knee, -0.4f * lift + 0.35f * std::max(0.0f, -sw));
        }
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            float k = (i % 2 == 0) ? 1.0f : -1.0f;
            float sw = 0.75f * s * k * (i >= 2 ? -0.8f : 1.0f);
            if (i >= 2) a.lowerArm(i, 0.2f + 0.6f * sw, 0.05f, 1.35f + 0.2f * std::max(0.0f, sw));
            else a.arm(i, 0.62f, sw - 0.1f, 1.35f + 0.25f * std::max(0.0f, -sw));
        }
        a.tail(0.3f, -0.35f * s, 0.6f);
        a.wings(-0.12f, -0.2f);
        a.slither(ph, 0.22f);
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Smug, MouthExpr::Open}}};
    clip.events = {{0.25f * clip.duration(), "footL"}, {0.75f * clip.duration(), "footR"}};
    return clip;
}

// Lunge punch with the right arm, guard with the left. Phrased as anticipation (weight
// back, torso coiled), an explosive strike, a held contact and a loose recovery.
Clip makeAttackClip(const Character& c) {
    if (beastBody(c)) return beastBite(c);
    const float dur = 27.0f / 30.0f;
    Clip clip = author(c, "attack", 27, 30, false, [&](Author& a, float t) {
        float twist = phrase({{0.0f, 0.0f}, {0.28f, -0.62f, Ease::Out}, {0.40f, 0.6f, Ease::Snap}, {0.62f, 0.5f, Ease::Linear}, {1.0f, 0.0f}}, t);
        float lunge = phrase({{0.0f, 0.0f}, {0.28f, -0.25f, Ease::Out}, {0.40f, 1.0f, Ease::Snap}, {0.62f, 1.0f, Ease::Linear}, {1.0f, 0.0f}}, t);
        a.tension = phrase({{0.0f, 0.5f}, {0.26f, 0.7f}, {0.36f, 1.0f, Ease::In}, {0.55f, 0.9f}, {0.75f, 0.3f}, {1.0f, 0.4f}}, t);
        a.move(a.r.hips, {0, -0.10f * a.h * std::fabs(lunge) - 0.03f * a.h, 0.25f * a.h * lunge});
        a.euler(a.r.hips, 0.05f, twist * 0.4f, 0);
        a.euler(a.r.spine, 0.1f * lunge, twist * 0.3f, 0);
        a.euler(a.r.chest, 0.08f * lunge, twist * 0.45f, 0);
        a.euler(a.r.head, -0.1f * lunge, -twist * 0.6f, 0);
        a.leg(0, -0.25f - 0.35f * lunge, 0.35f + 0.25f * lunge, -0.1f, 0.05f);
        a.leg(1, 0.20f + 0.25f * lunge, 0.25f + 0.1f * lunge, 0.2f, 0.05f);
        for (size_t i = 2; i < a.r.legs.size(); ++i) a.leg(i, 0.1f * lunge, 0.2f + 0.25f * std::fabs(lunge), -0.1f * lunge);
        // Right arm (index 1): windup -> strike -> recover.
        Vec3 wind = normalize(Vec3{-0.6f, -0.45f, -0.45f}), strike = normalize(Vec3{0.05f, 0.08f, 1.0f});
        Vec3 rest = a.r.arms.size() > 1 ? a.r.arms[1].restDir : Vec3{-0.6f, -0.8f, 0};
        float k1 = phrase({{0.0f, 0.0f}, {0.28f, 1.0f, Ease::Out}, {0.40f, 1.0f}, {1.0f, 0.0f}}, t);
        float k2 = phrase({{0.28f, 0.0f}, {0.38f, 1.0f, Ease::Snap}, {0.62f, 1.0f, Ease::Linear}, {1.0f, 0.0f}}, t);
        Vec3 dir = normalize(lerp(lerp(rest, wind, k1), strike, k2));
        a.aim(1, dir, lerp(lerp(0.3f, 2.0f, k1), 0.05f, k2));
        a.aim(0, lerp(a.r.arms[0].restDir, normalize(Vec3{0.25f, -0.35f, 0.9f}), std::min(1.0f, t * 5) * (1 - k2 * 0.3f)),
              lerp(0.3f, 2.1f, std::min(1.0f, t * 5)));
        if (a.r.arms.size() > 2) {  // lower pair: delayed uppercut + guard
            float k3 = phrase({{0.35f, 0.0f}, {0.5f, 1.0f, Ease::Snap}, {0.7f, 1.0f}, {1.0f, 0.0f}}, t);
            const float g = std::min(1.0f, t * 4);
            a.aim(3, normalize(lerp(a.hang(3, -0.2f * g), normalize(Vec3{-0.1f, 0.5f, 1.0f}), k3)), lerp(0.75f + 0.5f * g, 0.9f, k3));
            a.aim(2, normalize(lerp(a.hang(2), normalize(Vec3{0.12f, -0.5f, 0.8f}), g)), lerp(0.75f, 1.8f, g));  // fist up, guarding the belly
        }
        a.tail(0.15f + 0.3f * std::max(0.0f, lunge), -0.9f * twist, 0.7f);
        a.wings(0.35f * std::max(0.0f, lunge), 0.3f * std::max(0.0f, lunge));
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Neutral}},
                     {0.25f, {EyeExpr::Angry, MouthExpr::Shout}},
                     {0.65f, {EyeExpr::Smug, MouthExpr::Smirk}}};
    clip.events = {{0.38f * dur, "contact"}};
    return clip;
}

// Combat dodge: crouch, side leap with a torso twist, land, recover.
Clip makeDodgeClip(const Character& c) {
    if (beastBody(c)) return beastDodge(c);
    Clip clip = author(c, "dodge", 30, 30, false, [&](Author& a, float t) {
        float crouch = phrase({{0.0f, 0.0f}, {0.15f, 1.0f, Ease::Out}, {0.25f, 0.2f, Ease::Snap}, {0.5f, 0.1f}, {0.62f, 1.0f, Ease::In}, {1.0f, 0.0f}}, t);
        float side = phrase({{0.0f, 0.0f}, {0.15f, -0.1f}, {0.55f, 1.0f, Ease::Out}, {1.0f, 1.0f}}, t);
        float air = phrase({{0.15f, 0.0f}, {0.35f, 1.0f, Ease::Out}, {0.55f, 0.0f, Ease::In}}, t);
        float twist = phrase({{0.0f, 0.0f}, {0.2f, -0.3f}, {0.45f, 0.7f}, {0.7f, 0.3f}, {1.0f, 0.0f}}, t);
        a.tension = 0.55f + 0.3f * crouch;
        a.move(a.r.hips, {1.1f * a.h * side, -0.22f * a.h * crouch + 0.35f * a.h * air, 0});
        a.euler(a.r.hips, 0.15f * crouch, twist * 0.5f, -0.25f * air);
        a.euler(a.r.spine, 0.2f * crouch, twist * 0.3f, 0.15f * air);
        a.euler(a.r.chest, 0.1f * crouch, twist * 0.3f, 0.1f * air);
        a.euler(a.r.head, -0.15f * crouch, -twist * 0.5f, 0);
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            float k = (i % 2 == 0 ? 1.0f : -1.0f) * (i >= 2 ? -1.0f : 1.0f);  // four legs: diagonal pairs (trot)
            a.leg(i, -0.6f * crouch - 0.3f * air, 1.1f * crouch + 0.7f * air, -0.3f * crouch, 0.15f * air * k);
        }
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            float k = (i % 2 == 0) ? 1.0f : -1.0f;
            if (i >= 2) a.lowerArm(i, 0.25f * crouch - 0.2f * k * air, 0.45f * air, 0.9f + 0.5f * crouch);
            else a.arm(i, 0.35f - 0.5f * air, -0.4f * crouch + 0.3f * k * air, 0.6f + 0.6f * crouch, 0.3f * air);
        }
        a.tail(0.4f * air - 0.1f * crouch, -0.6f * twist, 0.6f);
        a.wings(0.5f * air, 0.3f * air);
        a.plantAll(1.0f - smoothstep(0.0f, 0.15f, air));
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Neutral}}, {0.3f, {EyeExpr::Angry, MouthExpr::Open}},
                     {0.7f, {EyeExpr::Neutral, MouthExpr::Neutral}}};
    clip.events = {{0.55f, "land"}};
    return clip;
}

namespace {

Vec3 flatDir(Vec3 v) {
    v.y = 0;
    return normalize(v, {0, 0, -1});
}

// Shared recoil of hit and death: the torso tilts along the push, the head whips after
// it, arms fly the other way (they lag behind the body), the knees give.
void recoil(Author& a, Vec3 push, float k, float kh, float s, float stepLift) {
    const Vec3 axis = cross(Vec3{0, 1, 0}, push);
    a.move(a.r.hips, push * (0.12f * a.h * k * s) + Vec3{0, -0.05f * a.h * k * s, 0});
    a.tilt(a.r.hips, axis, 0.05f * k * s);
    a.tilt(a.r.spine, axis, 0.22f * k * s);
    a.tilt(a.r.chest, axis, 0.26f * k * s);
    a.euler(a.r.chest, 0, 0.3f * k * s * push.x, 0);
    a.tilt(a.r.neck, axis, 0.12f * kh * s);
    a.tilt(a.r.head, axis, 0.3f * kh * s);
    for (size_t i = 0; i < a.r.arms.size(); ++i) {
        const float sd = a.r.arms[i].side;
        if (i >= 2) a.lowerArm(i, 0.5f * k * s * push.z, 0.35f * k * s + 0.4f * k * s * std::max(0.0f, -sd * push.x), 0.75f + 0.5f * k * s);
        else a.arm(i, 0.62f - 0.45f * k * s, 0.6f * k * s * push.z, 0.3f + 0.7f * k * s, 0.45f * k * s * std::max(0.0f, -sd * push.x));
    }
    for (size_t i = 0; i < a.r.legs.size(); ++i) {
        const float sd = a.r.legs[i].side;
        const bool step = i % 2 == 1;  // one leg (and its hind partner) catches the body
        a.leg(i, step ? -push.z * 0.3f * k * s : 0.05f * k * s, 0.3f * k * s + (step ? 0.1f * k * s : 0.0f), -0.1f * k * s,
              std::max(0.0f, sd * push.x) * 0.3f * k * s);
    }
    a.tail(-0.35f * k * s, -push.x * 0.6f * k * s, 0.8f);
    for (size_t i = 0; i < a.r.legs.size(); ++i) a.plant(i, i % 2 == 1 ? 1.0f - stepLift : 1.0f);
}

}  // namespace

Clip makeHitClip(const Character& c, Vec3 push, float strength) {
    push = flatDir(push);
    const float s = clamp(strength, 0.0f, 2.0f);
    if (beastBody(c)) return beastHit(c, push, s);
    Clip clip = author(c, "hit", 24, 30, false, [&](Author& a, float t) {
        const float k = phrase({{0.0f, 0.0f}, {0.12f, 1.0f, Ease::Snap}, {0.3f, 0.85f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float kh = phrase({{0.0f, 0.0f}, {0.06f, 0.0f}, {0.2f, 1.0f, Ease::Snap}, {0.4f, 0.7f}, {1.0f, 0.0f, Ease::Back}}, t);
        a.tension = phrase({{0.0f, 0.1f}, {0.25f, 0.2f}, {1.0f, 0.5f}}, t);
        recoil(a, push, k, kh, s, phrase({{0.0f, 0.0f}, {0.08f, 1.0f, Ease::Out}, {0.22f, 0.0f, Ease::In}}, t));
    });
    const float d = clip.duration();
    clip.faceKeys = {{0.0f, {EyeExpr::Hurt, MouthExpr::Shout}}, {0.45f * d, {EyeExpr::Hurt, MouthExpr::Frown}},
                     {0.8f * d, {EyeExpr::Angry, MouthExpr::Neutral}}};
    clip.events = {{0.0f, "impact"}};
    return clip;
}

Clip makeDeathClip(const Character& c, Vec3 push) {
    push = flatDir(push);
    if (beastBody(c)) return beastDeath(c, push);
    const bool fourLegs = c.rig.legs.size() > 2;
    Clip clip = author(c, "death", 30, 30, false, [&](Author& a, float t) {
        const float k = phrase({{0.0f, 0.0f}, {0.1f, 1.0f, Ease::Snap}, {0.3f, 0.7f}, {1.0f, 0.5f}}, t);
        const float kh = phrase({{0.0f, 0.0f}, {0.05f, 0.0f}, {0.16f, 1.0f, Ease::Snap}, {1.0f, 0.6f}}, t);
        const float b = phrase({{0.1f, 0.0f}, {0.6f, 1.0f, Ease::In}, {1.0f, 1.0f}}, t);  // the legs give way
        a.tension = 0.08f;
        recoil(a, push, k, kh, 1.15f, phrase({{0.0f, 0.0f}, {0.06f, 1.0f, Ease::Out}, {0.16f, 0.0f, Ease::In}}, t));
        // Buckle: the knees give, the pelvis drops, the body keeps going the way the blow
        // sent it (a blow from behind folds it forward) and the head hangs.
        const Vec3 axis = cross(Vec3{0, 1, 0}, push);
        const float fwd = std::max(0.0f, push.z);  // pushed forwards: folds over
        a.move(a.r.hips, {0, -(fourLegs ? 0.2f : 0.35f) * a.h * b, 0});
        a.tilt(a.r.spine, axis, 0.2f * b);
        a.tilt(a.r.chest, axis, 0.15f * b);
        a.euler(a.r.spine, 0.25f * b * fwd, 0, 0);
        a.euler(a.r.neck, 0.25f * b, 0, 0);
        a.euler(a.r.head, 0.3f * b, 0, 0.15f * b);
        for (size_t i = 0; i < a.r.legs.size(); ++i) a.leg(i, -0.55f * b, 1.2f * b, 0.25f * b, 0.08f * b);
        for (size_t i = 0; i < a.r.arms.size(); ++i) a.arm(i, 0.2f * b, -0.2f * b, 0.6f * b, 0.15f * b);
        a.tail(-0.4f * b, 0, 0.5f);
        a.wings(-0.3f * b, -0.3f * b);
        a.plantAll(smoothstep(0.1f, 0.25f, t));
    });
    const float d = clip.duration();
    clip.faceKeys = {{0.0f, {EyeExpr::Hurt, MouthExpr::Shout}}, {0.25f * d, {EyeExpr::Hurt, MouthExpr::Open}},
                     {0.6f * d, {EyeExpr::Blink, MouthExpr::Oh}}};
    clip.events = {{0.0f, "impact"}, {0.42f * d, "ragdoll"}};
    return clip;
}

// Spell: gather the power at the hip with the torso coiled, rise channelling it between the
// hands in front of the chest (trembling), then thrust both palms with a lunge and hold the
// shape. A second pair of arms spreads wide while channelling and drives forward with the release.
Clip makeCastClip(const Character& c) {
    if (beastBody(c)) return beastHowl(c);
    const int n = 48;
    const float dur = (n - 1) / 30.0f;
    Clip clip = author(c, "cast", n, 30, false, [&](Author& a, float t) {
        const float gather = phrase({{0.0f, 0.0f}, {0.2f, 1.0f, Ease::Out}, {0.6f, 1.0f}, {0.64f, 0.0f, Ease::Linear}}, t);
        const float channel = phrase({{0.2f, 0.0f}, {0.52f, 1.0f}, {0.6f, 1.0f}, {0.64f, 0.0f, Ease::Linear}}, t);
        const float thrust = phrase({{0.58f, 0.0f}, {0.64f, 1.0f, Ease::Snap}, {0.86f, 0.92f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float crouch = phrase({{0.0f, 0.0f}, {0.2f, 0.7f, Ease::Out}, {0.55f, 0.25f}, {0.64f, 1.0f, Ease::Snap}, {0.86f, 0.8f}, {1.0f, 0.0f}}, t);
        const float twist = phrase({{0.0f, 0.0f}, {0.22f, -0.5f, Ease::Out}, {0.55f, -0.12f}, {0.64f, 0.28f, Ease::Snap}, {0.86f, 0.22f}, {1.0f, 0.0f}}, t);
        const float tremble = channel * std::sin(t * dur * kTau * 13.0f);
        a.tension = 0.35f + 0.45f * channel + 0.6f * thrust;
        a.move(a.r.hips, {0, -0.12f * a.h * crouch, 0.16f * a.h * thrust});
        a.euler(a.r.hips, 0.05f * crouch, twist * 0.35f, 0);
        a.euler(a.r.spine, 0.12f * gather - 0.12f * channel + 0.12f * thrust, twist * 0.3f, 0.01f * tremble);
        a.euler(a.r.chest, 0.05f * gather - 0.14f * channel + 0.08f * thrust + 0.012f * tremble, twist * 0.4f, 0);
        a.euler(a.r.head, 0.3f * gather - 0.2f * channel - 0.1f * thrust, -twist * 0.6f, 0);
        for (size_t i = 0; i < std::min<size_t>(2, a.r.arms.size()); ++i) {
            const float sd = a.r.arms[i].side;
            const Vec3 hip = normalize(Vec3{-sd * 0.35f, -0.75f, 0.55f});
            const Vec3 chest = normalize(Vec3{-sd * 0.45f, -0.6f + 0.03f * tremble, 0.75f});
            const Vec3 palm = normalize(Vec3{-sd * 0.1f, 0.14f, 1.0f});
            Vec3 d = lerp(a.r.arms[i].restDir, hip, gather);
            d = lerp(d, chest, channel);
            d = lerp(d, palm, thrust);
            a.aim(i, normalize(d), lerp(lerp(lerp(0.3f, 1.3f, gather), 1.25f, channel), 0.08f, thrust));
        }
        for (size_t i = 2; i < a.r.arms.size(); ++i) {
            const float sd = a.r.arms[i].side;
            // Palms low and open at the sides, feeding the spell, then a shove forward.
            const Vec3 spread = normalize(Vec3{sd * 0.75f, -0.6f, 0.3f});
            const Vec3 drive = normalize(Vec3{sd * 0.45f, -0.35f, 0.85f});
            Vec3 d = lerp(a.hang(i), spread, std::max(gather * 0.5f, channel));
            d = lerp(d, drive, thrust);
            a.aim(i, normalize(d), lerp(lerp(0.75f, 0.95f, std::max(gather * 0.5f, channel)), 0.45f, thrust));
        }
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            const bool front = i % 2 == 0;
            const float h = hind(i) ? 0.5f : 1.0f;
            a.leg(i, h * (front ? -0.2f * crouch - 0.3f * thrust : 0.15f * crouch + 0.3f * thrust), h * (0.25f * crouch + (front ? 0.35f : 0.15f) * thrust),
                  h * (front ? -0.1f : 0.15f) * thrust, 0.06f * crouch);
        }
        a.tail(0.6f * channel + 0.35f * thrust - 0.1f * gather, 0.25f * twist, 0.8f);
        a.wings(0.3f * channel, 0.2f * channel);
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Neutral}}, {0.2f * dur, {EyeExpr::Angry, MouthExpr::Frown}},
                     {0.6f * dur, {EyeExpr::Angry, MouthExpr::Shout}}, {0.9f * dur, {EyeExpr::Smug, MouthExpr::Smirk}}};
    clip.events = {{0.62f * dur, "release"}};
    return clip;
}

// War cry: a deep breath that lifts the chest and shoulders, then the roar snaps into a wide,
// low stance with the chest thrown out, arms flung back and fists clenched, held with a
// tremor and a heaving chest; the tail lifts and lashes.
Clip makeRoarClip(const Character& c) {
    if (beastBody(c)) return beastSnarl(c);
    const int n = 60;
    const float dur = (n - 1) / 30.0f;
    Clip clip = author(c, "roar", n, 30, false, [&](Author& a, float t) {
        const float inhale = phrase({{0.0f, 0.0f}, {0.2f, 1.0f, Ease::Out}, {0.27f, 0.0f, Ease::Snap}}, t);
        const float roar = phrase({{0.18f, 0.0f}, {0.27f, 1.0f, Ease::Snap}, {0.72f, 0.93f, Ease::Linear}, {1.0f, 0.0f}}, t);
        const float shake = std::sin(t * dur * kTau * 11.0f) * roar;
        const float heave = std::sin(t * dur * kTau * 1.7f) * roar;
        a.tension = 0.4f + 0.6f * roar;
        // Inhale: rise, chest and shoulders up, head back. Roar: sink into a wide stance, hips
        // pitched forward and the chest thrown out over them, chin driven forward.
        a.move(a.r.hips, {0, -0.32f * a.h * roar + 0.04f * a.h * inhale, -0.05f * a.h * inhale - 0.04f * a.h * roar});
        a.euler(a.r.hips, 0.22f * roar, 0, 0);
        a.euler(a.r.spine, -0.12f * inhale - 0.12f * roar + 0.01f * shake, 0, 0);
        a.euler(a.r.chest, -0.15f * inhale - 0.28f * roar - 0.04f * heave, 0, 0.01f * shake);
        a.euler(a.r.neck, -0.25f * inhale + 0.35f * roar, 0, 0);
        a.euler(a.r.head, -0.35f * inhale - 0.12f * roar + 0.02f * shake, 0.05f * heave, 0);
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            const float sd = a.r.arms[i].side;
            if (i < 2) {
                // Shoulders up on the breath, then pulled back; arms flared out and back with
                // the elbows bent hard and the fists low (a gorilla's display).
                a.euler(a.r.arms[i].root, 0, -sd * 0.12f * roar, sd * (0.25f * inhale + 0.1f * roar));
                const Vec3 flare = normalize(Vec3{sd * 0.8f, -0.5f, 0.32f});
                const Vec3 up = normalize(Vec3{sd * 0.9f, -0.15f, -0.2f});
                Vec3 d = lerp(a.r.arms[i].restDir, up, inhale);
                d = lerp(d, flare, roar);
                a.aim(i, normalize(d), 0.3f + 0.4f * inhale + 1.55f * roar);
            } else {
                // The lower pair draws back on the breath, then flexes: fists in front of the hips.
                Vec3 d = lerp(a.hang(i), a.hang(i, -0.35f, 0.15f), inhale);
                d = lerp(d, a.hang(i, 0.25f, 0.3f), roar);
                a.aim(i, normalize(d), 0.75f + 0.25f * inhale + 1.0f * roar);
            }
        }
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            const float h = hind(i) ? 0.5f : 1.0f;
            a.leg(i, -0.45f * roar * h, 0.9f * roar * h, -0.35f * roar * h, 0.32f * roar);
        }
        a.tail(0.3f * inhale + 0.85f * roar, 0.4f * std::sin(t * dur * kTau * 1.3f) * roar, 0.9f);
        a.wings(0.15f * inhale + 0.6f * roar, 0.4f * roar);
        a.plantAll();
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Neutral, MouthExpr::Neutral}}, {0.08f * dur, {EyeExpr::Angry, MouthExpr::Oh}},
                     {0.24f * dur, {EyeExpr::Angry, MouthExpr::Shout}}, {0.86f * dur, {EyeExpr::Angry, MouthExpr::Neutral}},
                     {0.95f * dur, {EyeExpr::Smug, MouthExpr::Smirk}}};
    clip.events = {{0.25f * dur, "roar"}};
    return clip;
}

Clip makeTrotClip(const Character& c) {
    if (beastBody(c)) return beastTrot(c);
    Clip clip = makeRunClip(c);
    clip.name = "trot";
    return clip;
}

Clip makeSwipeClip(const Character& c) {
    if (beastBody(c)) return beastSwipe(c);
    Clip clip = makeAttackClip(c);
    clip.name = "swipe";
    return clip;
}

std::vector<Clip> makeAuthoredClips(const Character& c) {
    std::vector<Clip> clips{makeIdleClip(c), makeWalkClip(c), makeRunClip(c), makeAttackClip(c), makeDodgeClip(c),
                            makeHitClip(c),  makeCastClip(c), makeRoarClip(c), makeDeathClip(c)};
    if (beastBody(c)) clips.push_back(makeTrotClip(c)), clips.push_back(makeSwipeClip(c));  // run is a gallop, attack a bite
    return clips;
}

std::vector<Clip> makeDefaultClips(const Character& c, bool physical) {
    std::vector<Clip> clips = makeAuthoredClips(c);
    if (!physical) return clips;
    for (Clip& clip : clips) {
        if (clip.name == "death") clip = bakeDeathClip(c);
        else clip = bakePhysical(c, clip);
    }
    return clips;
}

}  // namespace cg
