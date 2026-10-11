#include "chargen/animation.hpp"

#include <functional>

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

    explicit Author(const Character& ch) : c(ch), r(ch.rig), h(ch.height / ch.genome.body.headsTall) {}
    void reset() { p = restPose(c.skeleton); }
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
    void leg(size_t i, float swing, float knee, float foot, float spread = 0) {
        if (i >= r.legs.size()) return;
        const LimbRig& l = r.legs[i];
        rot(l.upper, Quat::axisAngle({0, 0, 1}, l.side * spread) * Quat::axisAngle({1, 0, 0}, swing));
        rot(l.lower, Quat::axisAngle({1, 0, 0}, knee));
        rot(l.end, Quat::axisAngle({1, 0, 0}, foot));
    }
};

// Damped spring driven by the parent bone's acceleration (bust, hair, tail).
struct Spring {
    Vec3 x{}, v{};
    void step(Vec3 accel, float dt, float freq, float damping, float limit) {
        float w = kTau * freq;
        Vec3 a = accel - x * (w * w) - v * (2 * damping * w);
        v += a * dt;
        x += v * dt;
        float l = length(x);
        if (l > limit) {
            x = x * (limit / l);
            v = v * 0.5f;
        }
    }
};

Vec3 invRotate(const Mat4& m, Vec3 v) {  // transpose of the rotation part
    Vec3 cx{m.m[0], m.m[1], m.m[2]}, cy{m.m[4], m.m[5], m.m[6]}, cz{m.m[8], m.m[9], m.m[10]};
    return {dot(cx, v), dot(cy, v), dot(cz, v)};
}

// Adds secondary motion to an authored clip by simulating springs over a few
// loops (so looping clips stay seamless enough) and writing the last loop.
void addSecondary(const Character& c, Clip& clip) {
    const RigInfo& r = c.rig;
    const Skeleton& sk = c.skeleton;
    const size_t n = clip.frames.size();
    if (n < 2) return;
    const float dt = 1.0f / clip.fps, h = c.height / c.genome.body.headsTall;
    std::vector<std::vector<Mat4>> globals(n);
    std::vector<Mat4> skin;
    for (size_t f = 0; f < n; ++f) poseMatrices(sk, clip.frames[f], globals[f], skin);

    auto worldAccel = [&](int bone, size_t f, Vec3 localPoint) {
        auto P = [&](size_t k) { return globals[k][size_t(bone)].point(localPoint); };
        size_t a = clip.loop ? (f + n - 1) % n : (f ? f - 1 : 0), b = clip.loop ? (f + 1) % n : std::min(f + 1, n - 1);
        return (P(b) - P(f) * 2.0f + P(a)) / (dt * dt);
    };

    const int loops = clip.loop ? 3 : 1;
    // Bust: translation of the bust bones in chest space.
    if (r.bustL >= 0) {
        const float soft = 1.0f + 0.5f * std::min(c.genome.body.bust, 1.5f);
        for (int bone : {r.bustL, r.bustR}) {
            Spring s;
            Vec3 lp = sk.bones[size_t(bone)].restLocal;
            for (int L = 0; L < loops; ++L)
                for (size_t f = 0; f < n; ++f) {
                    Vec3 acc = invRotate(globals[f][size_t(r.chest)], -worldAccel(r.chest, f, lp));
                    acc.y -= 0.0f;
                    s.step(acc, dt, 2.6f / std::sqrt(soft), 0.22f, 0.07f * h * soft);
                    if (L == loops - 1) clip.frames[f].trans[size_t(bone)] += s.x * 0.8f;
                }
        }
    }
    // Hair chains: spring on the tip, converted to a rotation spread down the chain.
    for (const auto& chain : r.hairChains) {
        if (chain.empty()) continue;
        Spring s;
        int root = chain[0];
        Vec3 tipLocal = sk.bones[size_t(chain.back())].restWorld - sk.bones[size_t(root)].restWorld;
        float len = std::max(length(tipLocal), 1e-3f);
        int parent = sk.bones[size_t(root)].parent;
        for (int L = 0; L < loops; ++L)
            for (size_t f = 0; f < n; ++f) {
                Vec3 acc = invRotate(globals[f][size_t(parent)], -worldAccel(parent, f, sk.bones[size_t(root)].restLocal));
                acc += invRotate(globals[f][size_t(parent)], Vec3{0, -9.8f, 0}) * 0.05f;
                s.step(acc, dt, 1.6f, 0.3f, 0.6f * len);
                if (L == loops - 1) {
                    Quat q = fromTo(normalize(tipLocal), normalize(tipLocal + s.x));
                    for (size_t k = 0; k < chain.size(); ++k) {
                        float share = float(k + 1) / float(chain.size() * (chain.size() + 1) / 2);
                        clip.frames[f].rot[size_t(chain[k])] = nlerp(Quat{}, q, share * 1.6f) * clip.frames[f].rot[size_t(chain[k])];
                    }
                }
            }
    }
    // Tail: travelling wave whose amplitude follows the clip's energy.
    if (!r.tail.empty()) {
        float energy = clip.name == "run" ? 1.6f : (clip.name == "walk" ? 1.0f : 0.6f);
        for (size_t f = 0; f < n; ++f) {
            float t = float(f) / float(n);
            float cycles = clip.name == "idle" ? 1.0f : 2.0f;
            for (size_t k = 0; k < r.tail.size(); ++k) {
                float ph = kTau * (t * cycles) - 0.7f * float(k);
                Quat q = Quat::euler(0.06f * energy * std::cos(ph * 0.5f), 0.22f * energy * std::sin(ph), 0);
                clip.frames[f].rot[size_t(r.tail[k])] = q * clip.frames[f].rot[size_t(r.tail[k])];
            }
        }
    }
}

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
        clip.frames.push_back(a.p);
    }
    addSecondary(c, clip);
    return clip;
}

float ease(float t) { return t * t * (3 - 2 * t); }

// Keyframe track for non-looping authored motion: (time, value).
float track(std::initializer_list<std::pair<float, float>> keys, float t) {
    const auto* k = keys.begin();
    if (t <= k[0].first) return k[0].second;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
        if (t <= k[i + 1].first) return lerp(k[i].second, k[i + 1].second, ease((t - k[i].first) / (k[i + 1].first - k[i].first)));
    return k[keys.size() - 1].second;
}

}  // namespace

Clip makeIdleClip(const Character& c) {
    const float fem = c.genome.femininity;
    Clip clip = author(c, "idle", 90, 30, true, [&](Author& a, float t) {
        float ph = kTau * t;
        float breath = std::sin(ph * 2);
        a.move(a.r.hips, {0.012f * a.h * std::sin(ph), -0.006f * a.h * (1 + std::cos(ph * 2)), 0});
        a.euler(a.r.hips, 0, 0.03f * std::sin(ph), 0.035f * std::sin(ph) * (0.5f + fem));
        a.euler(a.r.spine, -0.02f * breath, 0, -0.02f * std::sin(ph));
        a.euler(a.r.chest, -0.025f * breath, -0.02f * std::sin(ph), -0.01f * std::sin(ph));
        a.euler(a.r.neck, 0.02f * breath, 0, 0);
        a.euler(a.r.head, 0.03f + 0.02f * std::sin(ph + 1), 0.08f * std::sin(ph * 0.5f + 0.3f), 0.02f * std::sin(ph));
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            bool lower = i >= 2;
            float sd = (i % 2 == 0) ? 1.0f : -1.0f;
            a.arm(i, lower ? 0.25f : 0.62f, 0.05f * std::sin(ph + sd) - (lower ? 0.3f : 0.0f), 0.25f + 0.05f * breath);
            if (i < 2) a.euler(a.r.arms[i].root, 0, 0, 0.015f * sd * breath);
        }
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            // Counter the pelvis tilt so the feet stay planted.
            a.leg(i, 0, 0.03f, 0, -0.035f * std::sin(ph) * (0.5f + fem) * a.r.legs[i].side);
        }
    });
    // Blinks.
    clip.faceKeys = {{0.0f, {}}, {1.1f, {EyeExpr::Blink, MouthExpr::Neutral}}, {1.22f, {}},
                     {2.4f, {EyeExpr::Blink, MouthExpr::Neutral}}, {2.5f, {}}};
    return clip;
}

Clip makeWalkClip(const Character& c) {
    const float fem = c.genome.femininity;
    Clip clip = author(c, "walk", 30, 30, true, [&](Author& a, float t) {
        float ph = kTau * t, s = std::sin(ph), co = std::cos(ph);
        a.move(a.r.hips, {0.02f * a.h * s * (0.4f + fem), 0.03f * a.h * std::cos(2 * ph) - 0.02f * a.h, 0});
        a.euler(a.r.hips, 0.03f, 0.12f * s, 0.05f * co * (0.4f + fem));
        a.euler(a.r.spine, 0.03f, -0.06f * s, -0.03f * co);
        a.euler(a.r.chest, 0.0f, -0.12f * s, 0);
        a.euler(a.r.head, 0.0f, 0.06f * s, 0);
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            float k = i == 0 ? 1.0f : -1.0f;
            float sw = -0.45f * s * k;
            float lift = std::max(0.0f, co * k);
            float knee = 0.12f + 0.75f * std::pow(lift, 1.5f);
            a.leg(i, sw - 0.1f * lift, knee, -0.3f * knee * 0.6f + 0.2f * std::max(0.0f, -sw));
        }
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            float k = (i % 2 == 0) ? 1.0f : -1.0f;
            float sw = 0.42f * s * k * (i >= 2 ? -0.8f : 1.0f);
            a.arm(i, i >= 2 ? 0.25f : 0.66f, sw, 0.25f + 0.25f * std::max(0.0f, -sw));
        }
    });
    clip.faceKeys = {{0.0f, {}}};
    return clip;
}

Clip makeRunClip(const Character& c) {
    Clip clip = author(c, "run", 20, 30, true, [&](Author& a, float t) {
        float ph = kTau * t, s = std::sin(ph), co = std::cos(ph);
        a.move(a.r.hips, {0, 0.05f * a.h * std::fabs(std::cos(ph)) - 0.06f * a.h, 0});
        a.euler(a.r.hips, 0.12f, 0.18f * s, 0.04f * co);
        a.euler(a.r.spine, 0.18f, -0.1f * s, 0);
        a.euler(a.r.chest, 0.1f, -0.2f * s, 0);
        a.euler(a.r.head, -0.25f, 0.08f * s, 0);
        for (size_t i = 0; i < a.r.legs.size(); ++i) {
            float k = i == 0 ? 1.0f : -1.0f;
            float sw = -0.85f * s * k;
            float lift = std::max(0.0f, co * k);
            float knee = 0.3f + 1.5f * std::pow(lift, 1.2f);
            a.leg(i, sw - 0.35f * lift - 0.1f, knee, -0.4f * lift + 0.35f * std::max(0.0f, -sw));
        }
        for (size_t i = 0; i < a.r.arms.size(); ++i) {
            float k = (i % 2 == 0) ? 1.0f : -1.0f;
            float sw = 0.75f * s * k * (i >= 2 ? -0.8f : 1.0f);
            a.arm(i, i >= 2 ? 0.3f : 0.62f, sw - 0.1f, 1.35f + 0.25f * std::max(0.0f, -sw));
        }
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Smug, MouthExpr::Open}}};
    return clip;
}

Clip makeAttackClip(const Character& c) {
    // Lunge punch with the right arm, guard with the left.
    Clip clip = author(c, "attack", 27, 30, false, [&](Author& a, float t) {
        float twist = track({{0.0f, 0.0f}, {0.28f, -0.55f}, {0.42f, 0.55f}, {0.62f, 0.5f}, {1.0f, 0.0f}}, t);
        float lunge = track({{0.0f, 0.0f}, {0.28f, -0.2f}, {0.42f, 1.0f}, {0.62f, 1.0f}, {1.0f, 0.0f}}, t);
        a.move(a.r.hips, {0, -0.10f * a.h * std::fabs(lunge) - 0.03f * a.h, 0.25f * a.h * lunge});
        a.euler(a.r.hips, 0.05f, twist * 0.4f, 0);
        a.euler(a.r.spine, 0.1f * lunge, twist * 0.3f, 0);
        a.euler(a.r.chest, 0.08f * lunge, twist * 0.45f, 0);
        a.euler(a.r.head, -0.1f * lunge, -twist * 0.6f, 0);
        a.leg(0, -0.25f - 0.35f * lunge, 0.35f + 0.25f * lunge, -0.1f, 0.05f);
        a.leg(1, 0.20f + 0.25f * lunge, 0.25f + 0.1f * lunge, 0.2f, 0.05f);
        // Right arm (index 1): windup -> strike -> recover.
        Vec3 wind = normalize(Vec3{-0.6f, -0.45f, -0.45f}), strike = normalize(Vec3{0.05f, 0.08f, 1.0f});
        Vec3 rest = a.r.arms.size() > 1 ? a.r.arms[1].restDir : Vec3{-0.6f, -0.8f, 0};
        float k1 = track({{0.0f, 0.0f}, {0.28f, 1.0f}, {0.42f, 1.0f}, {1.0f, 0.0f}}, t);
        float k2 = track({{0.28f, 0.0f}, {0.40f, 1.0f}, {0.62f, 1.0f}, {1.0f, 0.0f}}, t);
        Vec3 dir = normalize(lerp(lerp(rest, wind, k1), strike, k2));
        a.aim(1, dir, lerp(lerp(0.3f, 2.0f, k1), 0.05f, k2));
        a.aim(0, lerp(a.r.arms[0].restDir, normalize(Vec3{0.25f, -0.35f, 0.9f}), std::min(1.0f, t * 5) * (1 - k2 * 0.3f)),
              lerp(0.3f, 2.1f, std::min(1.0f, t * 5)));
        if (a.r.arms.size() > 2) {  // lower pair: delayed uppercut + guard
            float k3 = track({{0.35f, 0.0f}, {0.5f, 1.0f}, {0.7f, 1.0f}, {1.0f, 0.0f}}, t);
            a.aim(3, lerp(a.r.arms[3].restDir, normalize(Vec3{-0.1f, 0.5f, 1.0f}), k3), lerp(0.4f, 0.6f, k3));
            a.arm(2, 0.3f, -0.6f, 1.6f);
        }
    });
    clip.faceKeys = {{0.0f, {EyeExpr::Angry, MouthExpr::Neutral}},
                     {0.25f, {EyeExpr::Angry, MouthExpr::Shout}},
                     {0.65f, {EyeExpr::Smug, MouthExpr::Smirk}}};
    return clip;
}

std::vector<Clip> makeDefaultClips(const Character& c) {
    return {makeIdleClip(c), makeWalkClip(c), makeRunClip(c), makeAttackClip(c)};
}

}  // namespace cg
