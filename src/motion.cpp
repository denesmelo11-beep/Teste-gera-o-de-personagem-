#include "chargen/motion.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace cg {

namespace {

constexpr float kDt = 1.0f / 120.0f;

// Rotation vector (axis * angle) of a unit quaternion, shortest way.
Vec3 qlog(Quat q) {
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    const Vec3 v{q.x, q.y, q.z};
    const float s = length(v);
    if (s < 1e-7f) return v * 2.0f;
    return v * (2.0f * std::atan2(s, q.w) / s);
}
Quat qexp(Vec3 r) {
    const float a = length(r);
    if (a < 1e-7f) return normalize(Quat{r.x * 0.5f, r.y * 0.5f, r.z * 0.5f, 1.0f});
    return Quat::axisAngle(r / a, a);
}
Vec3 clampLen(Vec3 v, float m) {
    const float l = length(v);
    return l > m ? v * (m / l) : v;
}
// Best rotation taking the rest offsets r onto the current offsets d (Mueller et al. 2016),
// starting from q.
Quat fitRotation(Quat q, const std::vector<Vec3>& r, const std::vector<Vec3>& d) {
    for (int it = 0; it < 12; ++it) {
        Vec3 num{};
        float den = 0;
        for (size_t j = 0; j < r.size(); ++j) {
            const Vec3 rr = rotate(q, r[j]);
            num += cross(rr, d[j]);
            den += dot(rr, d[j]);
        }
        const Vec3 w = num / (std::fabs(den) + 1e-9f);
        const float a = length(w);
        if (a < 1e-6f) break;
        q = normalize(Quat::axisAngle(w / a, a) * q);
    }
    return q;
}

void forwardK(const Skeleton& sk, const Pose& p, std::vector<Vec3>& pos, std::vector<Quat>& rot) {
    const size_t n = sk.bones.size();
    pos.resize(n);
    rot.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Bone& b = sk.bones[i];
        const Vec3 t = b.restLocal + (i < p.trans.size() ? p.trans[i] : Vec3{});
        const Quat q = i < p.rot.size() ? p.rot[i] : Quat{};
        if (b.parent < 0) {
            pos[i] = t;
            rot[i] = q;
        } else {
            const size_t pa = size_t(b.parent);
            pos[i] = pos[pa] + rotate(rot[pa], t);
            rot[i] = normalize(rot[pa] * q);
        }
    }
}

bool isChainOrJiggle(const Character& c, int bone) {
    for (const SpringChain& sc : c.physics.chains)
        for (int b : sc.bones)
            if (b == bone) return true;
    for (const JiggleBone& j : c.physics.jiggles)
        if (j.bone == bone) return true;
    const RigInfo& r = c.rig;
    return bone == r.socketHandR || bone == r.socketHandL || bone == r.socketBack || bone == r.socketHip ||
           c.skeleton.bones[size_t(bone)].name.rfind("socket", 0) == 0;
}

float flatGround(const MotionSettings& s, float x, float z) { return s.ground ? s.ground(x, z) : 0.0f; }

}  // namespace

// ---------------------------------------------------------------- two-bone IK

void twoBoneIK(const Skeleton& sk, Pose& pose, int upper, int lower, int end, Vec3 target, Vec3 pole, float weight) {
    if (upper < 0 || lower < 0 || end < 0 || weight <= 0) return;
    if (sk.bones[size_t(lower)].parent != upper || sk.bones[size_t(end)].parent != lower) return;
    std::vector<Vec3> pos;
    std::vector<Quat> rot;
    forwardK(sk, pose, pos, rot);
    const Vec3 a = pos[size_t(upper)], b = pos[size_t(lower)], c = pos[size_t(end)];
    const Vec3 t = lerp(c, target, saturate(weight));
    const float lab = length(b - a), lcb = length(c - b);
    if (lab < 1e-5f || lcb < 1e-5f) return;
    const float lat = clamp(length(t - a), 1e-3f, (lab + lcb) * 0.9995f);
    auto ang = [](Vec3 u, Vec3 v) { return std::acos(clamp(dot(normalize(u), normalize(v)), -1.0f, 1.0f)); };
    const float acab0 = ang(c - a, b - a), babc0 = ang(a - b, c - b);
    const float acab1 = std::acos(clamp((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
    const float babc1 = std::acos(clamp((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
    Vec3 axis0 = cross(c - a, b - a);
    if (length(axis0) < 1e-4f * lab * lab) axis0 = cross(c - a, pole);
    axis0 = normalize(axis0, {1, 0, 0});
    const Quat r0 = Quat::axisAngle(rotate(conj(rot[size_t(upper)]), axis0), acab1 - acab0);
    const Quat r1 = Quat::axisAngle(rotate(conj(rot[size_t(lower)]), axis0), babc1 - babc0);
    pose.rot[size_t(upper)] = normalize(pose.rot[size_t(upper)] * r0);
    pose.rot[size_t(lower)] = normalize(pose.rot[size_t(lower)] * r1);
    // Re-evaluate the chain, then swing it onto the target.
    const Quat pa = sk.bones[size_t(upper)].parent >= 0 ? rot[size_t(sk.bones[size_t(upper)].parent)] : Quat{};
    const Quat wa = normalize(pa * pose.rot[size_t(upper)]);
    const Vec3 b2 = a + rotate(wa, sk.bones[size_t(lower)].restLocal + pose.trans[size_t(lower)]);
    const Quat wb = normalize(wa * pose.rot[size_t(lower)]);
    const Vec3 c2 = b2 + rotate(wb, sk.bones[size_t(end)].restLocal + pose.trans[size_t(end)]);
    const Quat r2w = fromTo(normalize(c2 - a), normalize(t - a));
    pose.rot[size_t(upper)] = normalize(pose.rot[size_t(upper)] * (conj(wa) * r2w * wa));
}

// ---------------------------------------------------------------- animator

Animator::Animator(const Character& c, MotionSettings s) : settings(std::move(s)), c_(c), sec_(c) {
    const auto& bones = c.skeleton.bones;
    const size_t n = bones.size();
    const RigInfo& r = c.rig;
    sim_.assign(n, 1);
    freq_.assign(n, 4.0f);
    damp_.assign(n, 0.5f);
    depth_.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (bones[i].parent >= 0) depth_[i] = depth_[size_t(bones[i].parent)] + 1;
        if (isChainOrJiggle(c, int(i))) sim_[i] = 0;
    }
    if (r.root >= 0) sim_[size_t(r.root)] = 0;
    auto set = [&](int b, float f, float z) {
        if (b >= 0) freq_[size_t(b)] = f, damp_[size_t(b)] = z;
    };
    set(r.hips, 7.0f, 0.9f);
    set(r.spine, 6.0f, 0.8f);
    set(r.chest, 5.5f, 0.72f);
    set(r.neck, 4.5f, 0.6f);
    set(r.head, 3.8f, 0.5f);
    for (const LimbRig& a : r.arms) {
        set(a.root, 6.0f, 0.8f);
        set(a.upper, 4.5f, 0.6f);
        set(a.lower, 4.0f, 0.55f);
        set(a.end, 3.5f, 0.5f);
    }
    for (const LimbRig& l : r.legs) {
        set(l.upper, 7.0f, 0.85f);
        set(l.lower, 7.5f, 0.85f);
        set(l.end, 8.0f, 0.85f);
        set(l.tip, 8.0f, 0.85f);
    }
    // Pendulum scaling: big bodies swing slower, small ones snap.
    sizeK_ = std::sqrt(1.75f / std::max(0.6f, c.height));
    q_.assign(n, Quat{});
    w_.assign(n, Vec3{});
    tr_.assign(n, Vec3{});
    tv_.assign(n, Vec3{});
    jp_.assign(n, Vec3{});
    jv_.assign(n, Vec3{});
    ja_.assign(n, Vec3{});
    reach_.resize(r.arms.size());
    reachW_.assign(r.arms.size(), 0.0f);
    reachTarget_.assign(r.arms.size(), 0.0f);
    feet_.resize(r.legs.size());
    target_ = restPose(c.skeleton);
    out_ = target_;
    buildRagdoll();
}

void Animator::setBase(const Clip& clip) { base_ = clip; }

void Animator::play(const Clip& clip, float blend, float speed) {
    if (rag_) return;
    if (blend > 0 && !cur_.clip.frames.empty()) {
        prev_ = std::move(cur_);
        fade_ = 0;
        fadeDur_ = blend;
    } else {
        fade_ = 1;
    }
    cur_ = Layer{clip, -1e-6f, speed};
}

bool Animator::busy() const { return !cur_.clip.frames.empty() && !cur_.clip.loop && cur_.time < cur_.clip.duration(); }

FaceState Animator::face() const {
    if (rag_) return {EyeExpr::Blink, MouthExpr::Oh};
    for (const Layer& l : add_)
        if (l.time < 0.5f * l.clip.duration()) return sampleFace(l.clip, l.time);
    return cur_.clip.frames.empty() ? FaceState{} : sampleFace(cur_.clip, std::max(0.0f, cur_.time));
}

Vec3 Animator::bodyCenter() const {
    if (rag_) {
        Vec3 s{};
        for (const Node& nd : nodes_) s += nd.p;
        return nodes_.empty() ? lastWorld_ : s / float(nodes_.size());
    }
    const int h = c_.rig.hips;
    return lastWorld_ + rotate(lastRot_, h >= 0 ? jp_[size_t(h)] : Vec3{});
}

void Animator::lookAt(std::optional<Vec3> t, float weight) {
    look_ = t;
    lookTarget_ = t ? saturate(weight) : 0.0f;
}

void Animator::reach(size_t arm, std::optional<Vec3> t, float weight) {
    if (arm >= reach_.size()) return;
    if (t) reach_[arm] = t;
    reachTarget_[arm] = t ? saturate(weight) : 0.0f;
}

void Animator::fk(const Pose& p, std::vector<Vec3>& pos, std::vector<Quat>& rot) const { forwardK(c_.skeleton, p, pos, rot); }

Clip* Animator::advance(Layer& l, float dt, bool fire) {
    if (l.clip.frames.empty()) return nullptr;
    const float t0 = l.time, t1 = l.time + dt * l.speed, d = l.clip.duration();
    if (fire && d > 0)
        for (const auto& e : l.clip.events) {
            if (l.clip.loop) {
                const float a = std::fmod(std::max(0.0f, t0), d), span = t1 - std::max(0.0f, t0);
                float et = e.first;
                if (et < a) et += d;
                if (et > a && et <= a + span) events_.push_back(e.second);
            } else if (e.first > t0 && e.first <= t1) {
                events_.push_back(e.second);
            }
        }
    l.time = t1;
    return &l.clip;
}

void Animator::targetPose(Pose& t) {
    const size_t n = c_.skeleton.bones.size();
    t = cur_.clip.frames.empty() ? restPose(c_.skeleton) : sampleClip(cur_.clip, std::max(0.0f, cur_.time));
    if (t.rot.size() != n) t = restPose(c_.skeleton);
    if (fade_ < 1 && !prev_.clip.frames.empty()) {
        const Pose p = sampleClip(prev_.clip, std::max(0.0f, prev_.time));
        const float w = smoothstep(0.0f, 1.0f, fade_);
        for (size_t i = 0; i < n && i < p.rot.size(); ++i) {
            t.rot[i] = nlerp(p.rot[i], t.rot[i], w);
            t.trans[i] = lerp(p.trans[i], t.trans[i], w);
        }
    }
    // Additive reactions: the delta from the reaction's own first frame.
    for (const Layer& l : add_) {
        const Pose h = sampleClip(l.clip, std::max(0.0f, l.time));
        const Pose& f0 = l.clip.frames.front();
        for (size_t i = 0; i < n && i < h.rot.size(); ++i) {
            t.rot[i] = normalize(h.rot[i] * conj(f0.rot[i]) * t.rot[i]);
            t.trans[i] += h.trans[i] - f0.trans[i];
        }
    }
    const RigInfo& r = c_.rig;
    // Lean into the acceleration of the whole body (banks into turns: centripetal).
    if (settings.lean > 0) {
        Vec3 a = rotate(conj(lastRot_), acc_);
        a.y = 0;
        const float m = length(a);
        if (m > 1e-3f) {
            const Vec3 axis = cross(Vec3{0, 1, 0}, a / m);
            const float ang = std::min(0.35f, 0.035f * m * settings.lean);
            const int bones[3] = {r.hips, r.spine, r.chest};
            const float share[3] = {0.4f, 0.35f, 0.25f};
            for (int k = 0; k < 3; ++k)
                if (bones[k] >= 0) t.rot[size_t(bones[k])] = normalize(Quat::axisAngle(axis, ang * share[k]) * t.rot[size_t(bones[k])]);
        }
    }
    // Look-at spread over spine, chest, neck and head.
    if (lookW_ > 1e-3f && look_ && r.head >= 0) {
        fk(t, fp_, fq_);
        const Vec3 goal = rotate(conj(lastRot_), *look_ - lastWorld_);
        const Vec3 fwd = rotate(fq_[size_t(r.head)], {0, 0, 1});
        const Vec3 want = normalize(goal - fp_[size_t(r.head)], fwd);
        const Vec3 rv = qlog(fromTo(fwd, want));
        const float a = std::min(length(rv), 1.3f) * lookW_;
        if (a > 1e-4f) {
            const Vec3 axis = normalize(rv);
            const int bones[4] = {r.spine, r.chest, r.neck, r.head};
            const float share[4] = {0.15f, 0.2f, 0.3f, 0.35f};
            for (int k = 0; k < 4; ++k) {
                const int b = bones[k];
                if (b < 0) continue;
                const int pa = c_.skeleton.bones[size_t(b)].parent;
                const Quat wp = pa >= 0 ? fq_[size_t(pa)] : Quat{};
                t.rot[size_t(b)] = normalize(conj(wp) * Quat::axisAngle(axis, a * share[k]) * wp * t.rot[size_t(b)]);
            }
        }
    }
    for (size_t i = 0; i < r.arms.size(); ++i) {
        if (reachW_[i] < 1e-3f || !reach_[i]) continue;
        const LimbRig& arm = r.arms[i];
        const Vec3 goal = rotate(conj(lastRot_), *reach_[i] - lastWorld_);
        twoBoneIK(c_.skeleton, t, arm.upper, arm.lower, arm.end, goal, {arm.side * 0.4f, -0.3f, -1.0f}, reachW_[i]);
    }
}

void Animator::reset(Vec3 worldPos, Quat worldRot) {
    lastWorld_ = worldPos;
    lastRot_ = worldRot;
    vel_ = acc_ = {};
    if (cur_.clip.frames.empty() && !base_.frames.empty()) play(base_, 0);
    targetPose(target_);
    const size_t n = c_.skeleton.bones.size();
    for (size_t i = 0; i < n; ++i) {
        q_[i] = target_.rot[i];
        tr_[i] = target_.trans[i];
        w_[i] = tv_[i] = jv_[i] = ja_[i] = {};
    }
    out_ = target_;
    fk(out_, jp_, fq_);
    for (Foot& f : feet_) f = {};
    pelvisDrop_ = 0;
    rag_ = asleep_ = dying_ = false;
    sec_.stiffnessScale = 1;
    sec_.gravityScale = 1;
    sec_.reset(out_, worldPos, worldRot);
    lastNodes_.clear();
    accum_ = 0;
    init_ = true;
}

void Animator::hit(Vec3 dir, float strength, std::optional<Vec3> point) {
    if (!init_ || rag_) return;
    Vec3 d = rotate(conj(lastRot_), dir);
    d = normalize(Vec3{d.x, 0, d.z}, {0, 0, -1});
    const RigInfo& r = c_.rig;
    if (!dying_) {
        add_.push_back(Layer{makeHitClip(c_, d, strength), 0.0f, 1.0f});
        if (add_.size() > 3) add_.erase(add_.begin());
        events_.push_back("impact");
    }
    // Point of impact (character space) and the bone whose segment it is closest to.
    Vec3 p = point ? rotate(conj(lastRot_), *point - lastWorld_)
                   : (r.chest >= 0 ? jp_[size_t(r.chest)] - d * (0.15f * c_.height) : Vec3{0, 0.6f * c_.height, 0});
    int best = r.chest >= 0 ? r.chest : r.hips;
    float bd = 1e9f;
    const auto& bones = c_.skeleton.bones;
    for (size_t i = 0; i < bones.size(); ++i) {
        if (!sim_[i]) continue;
        const Vec3 a = jp_[i];
        Vec3 b = a;
        for (size_t k = 0; k < bones.size(); ++k)
            if (bones[k].parent == int(i) && sim_[k]) {
                b = jp_[k];
                break;
            }
        const Vec3 ab = b - a;
        const float tt = dot(ab, ab) > 1e-8f ? saturate(dot(p - a, ab) / dot(ab, ab)) : 0.0f;
        const float dd = length(p - (a + ab * tt));
        if (dd < bd) bd = dd, best = int(i);
    }
    // Angular momentum up the chain from the struck segment, fading towards the pelvis.
    const Vec3 J = d * (2.4f * strength);
    float f = 1.0f;
    for (int j = best; j >= 0 && f > 0.05f; j = bones[size_t(j)].parent) {
        if (sim_[size_t(j)]) {
            const Vec3 rr = p - jp_[size_t(j)];
            const Vec3 dw = cross(rr, J) / (dot(rr, rr) + 0.01f * c_.height * c_.height) * f;
            const int pa = bones[size_t(j)].parent;
            const Quat wp = pa >= 0 ? fq_[size_t(pa)] : Quat{};
            w_[size_t(j)] = clampLen(w_[size_t(j)] + rotate(conj(wp), dw), 30.0f);
        }
        if (j == r.hips) break;
        f *= 0.6f;
    }
    if (r.hips >= 0) tv_[size_t(r.hips)] += d * (0.9f * strength);
}

void Animator::die(Vec3 dir, float strength) {
    if (!init_ || rag_ || dying_) return;
    Vec3 d = rotate(conj(lastRot_), dir);
    d = normalize(Vec3{d.x, 0, d.z}, {0, 0, -1});
    deathDir_ = dir;
    deathStrength_ = strength;
    const Clip clip = makeDeathClip(c_, d);
    tone_ = clip.frames.back();
    add_.clear();
    play(clip, 0.06f);
    dying_ = true;
    hit(dir, 0.8f * strength);
}

void Animator::ragdoll(Vec3 impulse) {
    if (!init_ || rag_) return;
    if (!dying_) tone_ = makeDeathClip(c_, normalize(rotate(conj(lastRot_), impulse), {0, 0, -1})).frames.back();
    dying_ = true;
    startRagdoll(impulse);
}

void Animator::substep(float dt, Vec3 worldPos, Quat worldRot) {
    (void)worldPos;
    (void)worldRot;
    const size_t n = c_.skeleton.bones.size();
    advance(cur_, dt, true);
    if (fade_ < 1) {
        advance(prev_, dt, false);
        fade_ = fadeDur_ > 0 ? std::min(1.0f, fade_ + dt / fadeDur_) : 1.0f;
    }
    for (Layer& l : add_) advance(l, dt, false);
    add_.erase(std::remove_if(add_.begin(), add_.end(), [](const Layer& l) { return l.time >= l.clip.duration(); }), add_.end());
    for (const std::string& e : events_)
        if (e == "ragdoll" && dying_ && !rag_) {
            startRagdoll(deathDir_ * (1.2f * deathStrength_));
            return;
        }
    if (!dying_ && !cur_.clip.loop && !base_.frames.empty() && cur_.time >= cur_.clip.duration()) play(base_, 0.3f);
    const float ease = 1.0f - std::exp(-dt * 6.0f);
    lookW_ += (lookTarget_ - lookW_) * ease;
    for (size_t i = 0; i < reachW_.size(); ++i) reachW_[i] += (reachTarget_[i] - reachW_[i]) * ease;

    targetPose(target_);
    float tension = cur_.clip.frames.empty() ? 0.5f : sampleTension(cur_.clip, std::max(0.0f, cur_.time));
    for (const Layer& l : add_) tension = std::min(tension, sampleTension(l.clip, l.time));

    // Joint kinematics of the simulated body: velocities and (filtered) accelerations.
    Pose cur;
    cur.rot = q_;
    cur.trans = tr_;
    std::vector<Vec3> prevP = jp_;
    fk(cur, jp_, fq_);
    const float fa = 1.0f - std::exp(-dt / 0.035f);
    const Vec3 rootAcc = rotate(conj(lastRot_), acc_);
    for (size_t i = 0; i < n; ++i) {
        const Vec3 v = (jp_[i] - prevP[i]) / dt;
        ja_[i] = lerp(ja_[i], clampLen((v - jv_[i]) / dt, 200.0f), fa);
        jv_[i] = v;
    }
    const float tone = std::sqrt(std::max(0.05f, settings.stiffness)) * sizeK_ * (0.7f + 0.8f * tension);
    const float gain = 0.75f * settings.overlap;
    const auto& bones = c_.skeleton.bones;
    for (size_t i = 0; i < n; ++i) {
        if (!sim_[i]) {
            q_[i] = target_.rot[i];
            tr_[i] = target_.trans[i];
            w_[i] = tv_[i] = {};
            continue;
        }
        const float om = kTau * freq_[i] * tone, k = om * om, cdamp = 2.0f * damp_[i] * om;
        const Vec3 e = qlog(normalize(target_.rot[i] * conj(q_[i])));
        Vec3 alpha = e * k;
        const int pa = bones[i].parent;
        const Quat wp = pa >= 0 ? fq_[size_t(pa)] : Quat{};
        if (gain > 0 && seg_[i].x * seg_[i].x + seg_[i].y * seg_[i].y + seg_[i].z * seg_[i].z > 1e-8f) {
            // The segment hangs from its joint: the joint's acceleration swings it.
            const Vec3 rr = rotate(fq_[i], seg_[i]);
            const Vec3 a = ja_[i] + rootAcc;
            alpha += rotate(conj(wp), clampLen(cross(rr, -a) * (gain / dot(rr, rr)), 400.0f));
        }
        w_[i] = (w_[i] + alpha * dt) / (1.0f + cdamp * dt);
        q_[i] = normalize(qexp(w_[i] * dt) * q_[i]);
        const float omt = om * 1.2f;
        tv_[i] = (tv_[i] + (target_.trans[i] - tr_[i]) * (omt * omt * dt)) / (1.0f + 2.0f * 0.9f * omt * dt);
        tr_[i] += tv_[i] * dt;
    }
    // Joint limits on the simulated pose; motion into a limit is absorbed.
    Pose lim;
    lim.rot = q_;
    lim.trans = tr_;
    limitPose(c_.rig, lim);
    for (size_t i = 0; i < n; ++i) {
        const float d = std::fabs(lim.rot[i].x * q_[i].x + lim.rot[i].y * q_[i].y + lim.rot[i].z * q_[i].z + lim.rot[i].w * q_[i].w);
        if (d < 0.99999f) w_[i] *= 0.5f;
        q_[i] = lim.rot[i];
    }
}

void Animator::solveFeet(Vec3 worldPos, Quat worldRot, float dt) {
    const RigInfo& r = c_.rig;
    if (r.legs.empty() || r.hips < 0) return;
    const auto& bones = c_.skeleton.bones;
    std::vector<Vec3> tp, op;
    std::vector<Quat> tq, oq;
    fk(target_, tp, tq);
    const float groundRoot = flatGround(settings, worldPos.x, worldPos.z);
    std::vector<Vec3> goals(r.legs.size());
    std::vector<uint8_t> stance(r.legs.size());
    float drop = 0;
    fk(out_, op, oq);
    for (size_t i = 0; i < r.legs.size(); ++i) {
        const LimbRig& L = r.legs[i];
        if (L.upper < 0 || L.lower < 0 || L.end < 0) continue;
        const Vec3 ank = tp[size_t(L.end)];
        const float h0 = bones[size_t(L.end)].restWorld.y;
        const float legLen = length(bones[size_t(L.lower)].restLocal) + length(bones[size_t(L.end)].restLocal);
        Vec3 w = worldPos + rotate(worldRot, ank);
        w.y += flatGround(settings, w.x, w.z) - groundRoot;
        stance[i] = ank.y - h0 < 0.03f * c_.height;
        Foot& f = feet_[i];
        if (settings.footLock && stance[i]) {
            const Vec3 d{w.x - f.lock.x, 0, w.z - f.lock.z};
            if (!f.locked || length(d) > 0.35f * legLen) f.lock = w, f.locked = true;
            f.lockW = std::min(1.0f, f.lockW + dt * 20.0f);
        } else {
            f.locked = false;
            f.lockW = std::max(0.0f, f.lockW - dt * 10.0f);
        }
        if (f.lockW > 0) {
            w.x = lerp(w.x, f.lock.x, f.lockW);
            w.z = lerp(w.z, f.lock.z, f.lockW);
        }
        goals[i] = rotate(conj(worldRot), w - worldPos);
        const float reachLen = (length(bones[size_t(L.lower)].restLocal) + length(bones[size_t(L.end)].restLocal)) * 0.995f;
        const float need = length(goals[i] - op[size_t(L.upper)]) - reachLen;
        drop = std::max(drop, need);
    }
    pelvisDrop_ += (drop - pelvisDrop_) * (1.0f - std::exp(-dt * 14.0f));
    if (pelvisDrop_ > 1e-4f) out_.trans[size_t(r.hips)].y -= pelvisDrop_;
    for (size_t i = 0; i < r.legs.size(); ++i) {
        const LimbRig& L = r.legs[i];
        if (L.upper < 0 || L.lower < 0 || L.end < 0) continue;
        twoBoneIK(c_.skeleton, out_, L.upper, L.lower, L.end, goals[i], {0, 0, 1}, 1.0f);
        // Tilt the planted foot to the slope.
        if (settings.ground && stance[i]) {
            const Vec3 w = worldPos + rotate(worldRot, goals[i]);
            const float e = 0.05f;
            Vec3 nrm{-(settings.ground(w.x + e, w.z) - settings.ground(w.x - e, w.z)) / (2 * e), 1.0f,
                     -(settings.ground(w.x, w.z + e) - settings.ground(w.x, w.z - e)) / (2 * e)};
            nrm = rotate(conj(worldRot), normalize(nrm));
            fk(out_, op, oq);
            const Quat wl = oq[size_t(L.lower)];
            const Quat R = Quat::axisAngle(normalize(cross(Vec3{0, 1, 0}, nrm), {1, 0, 0}),
                                           std::min(0.6f, std::acos(clamp(nrm.y, -1.0f, 1.0f))) * feet_[i].lockW);
            out_.rot[size_t(L.end)] = normalize(conj(wl) * R * wl * out_.rot[size_t(L.end)]);
        }
    }
}

// ---------------------------------------------------------------- ragdoll

void Animator::buildRagdoll() {
    const auto& bones = c_.skeleton.bones;
    const size_t n = bones.size();
    const float H = c_.height;
    std::vector<uint8_t> inR(n, 0);
    for (size_t i = 0; i < n; ++i) inR[i] = sim_[i];
    // Dominant vertices of each bone: limb tips and thickness come from the mesh.
    std::vector<std::vector<Vec3>> verts(n);
    for (const Vertex& v : c_.mesh.vertices) {
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (v.weights[k] > v.weights[best]) best = k;
        if (v.weights[best] > 0.5f && v.joints[best] < n) verts[v.joints[best]].push_back(v.pos);
    }
    boneNode_.assign(n, -1);
    childNodes_.assign(n, {});
    seg_.assign(n, Vec3{});
    for (size_t i = 0; i < n; ++i) {
        if (!inR[i]) continue;
        Node nd;
        nd.bone = int(i);
        boneNode_[i] = int(nodes_.size());
        nodes_.push_back(nd);
    }
    for (size_t i = 0; i < n; ++i) {
        if (!inR[i]) continue;
        const int pa = bones[i].parent;
        if (pa >= 0 && inR[size_t(pa)]) childNodes_[size_t(pa)].push_back(boneNode_[i]);
    }
    for (size_t i = 0; i < n; ++i) {
        if (!inR[i] || !childNodes_[i].empty()) continue;
        const int pa = bones[i].parent;
        const Vec3 dir = normalize(bones[i].restWorld - (pa >= 0 ? bones[size_t(pa)].restWorld : Vec3{}), {0, 1, 0});
        float reach = 0;
        for (const Vec3& v : verts[i]) reach = std::max(reach, dot(v - bones[i].restWorld, dir));
        Node tip;
        tip.bone = int(i);
        tip.tip = true;
        tip.tipOffset = dir * std::max(0.8f * reach, 0.03f * H);
        childNodes_[i].push_back(int(nodes_.size()));
        nodes_.push_back(tip);
    }
    // Segment (joint -> centre) per bone, radii and masses.
    auto nodeRest = [&](int k) {
        const Node& nd = nodes_[size_t(k)];
        return bones[size_t(nd.bone)].restWorld + (nd.tip ? nd.tipOffset : Vec3{});
    };
    std::vector<float> rad(n, 0.04f * H);
    for (size_t i = 0; i < n; ++i) {
        if (!inR[i]) continue;
        Vec3 ctr{};
        for (int k : childNodes_[i]) ctr += nodeRest(k) - bones[i].restWorld;
        ctr = ctr / float(std::max<size_t>(1, childNodes_[i].size()));
        seg_[i] = ctr * 0.5f;
        const Vec3 a = bones[i].restWorld, b = a + ctr;
        float s = 0;
        for (const Vec3& v : verts[i]) {
            const Vec3 ab = b - a;
            const float t = dot(ab, ab) > 1e-8f ? saturate(dot(v - a, ab) / dot(ab, ab)) : 0.0f;
            s += length(v - (a + ab * t));
        }
        if (!verts[i].empty()) rad[i] = clamp(0.85f * s / float(verts[i].size()), 0.015f * H, 0.14f * H);
    }
    for (Node& nd : nodes_) {
        nd.radius = rad[size_t(nd.bone)] * (nd.tip ? 0.8f : 1.0f);
        const float m = std::max(0.3f, 4000.0f * nd.radius * nd.radius * nd.radius * 4.2f);
        nd.invMass = 1.0f / m;
    }
    // Links: bone -> children, and rigid clusters where a bone branches (pelvis, ribcage).
    for (size_t i = 0; i < n; ++i) {
        if (!inR[i]) continue;
        const auto& kids = childNodes_[i];
        for (int k : kids) links_.push_back({boneNode_[i], k, length(nodeRest(k) - bones[i].restWorld), 1.0f});
        if (kids.size() >= 2)
            for (size_t a = 0; a < kids.size(); ++a)
                for (size_t b = a + 1; b < kids.size(); ++b) links_.push_back({kids[a], kids[b], length(nodeRest(kids[a]) - nodeRest(kids[b])), 1.0f});
    }
    // Self collision between nodes at least three links apart.
    const size_t m = nodes_.size();
    std::vector<std::vector<int>> adj(m);
    for (const Link& l : links_) adj[size_t(l.a)].push_back(l.b), adj[size_t(l.b)].push_back(l.a);
    for (size_t s = 0; s < m; ++s) {
        std::vector<int> dist(m, -1);
        std::queue<int> qq;
        dist[s] = 0;
        qq.push(int(s));
        while (!qq.empty()) {
            const int u = qq.front();
            qq.pop();
            if (dist[size_t(u)] >= 3) continue;
            for (int v : adj[size_t(u)])
                if (dist[size_t(v)] < 0) dist[size_t(v)] = dist[size_t(u)] + 1, qq.push(v);
        }
        for (size_t t = s + 1; t < m; ++t)
            if (dist[t] < 0 && length(nodeRest(int(s)) - nodeRest(int(t))) > 0.85f * (nodes_[s].radius + nodes_[t].radius))
                pairs_.push_back({int(s), int(t)});
    }
    ragLocal_.assign(n, Quat{});
    hinge_.assign(n, 0);
    hingeAxis_.assign(n, Vec3{1, 0, 0});
    for (const JointLimit& L : c_.rig.limits)
        if (L.hinge && L.bone >= 0 && size_t(L.bone) < n) hinge_[size_t(L.bone)] = 1, hingeAxis_[size_t(L.bone)] = normalize(L.axis);
}

void Animator::startRagdoll(Vec3 impulse) {
    const auto& bones = c_.skeleton.bones;
    // Start from the pose on screen, moving with the simulated body.
    std::vector<Vec3> pos;
    std::vector<Quat> rot;
    fk(out_, pos, rot);
    const float hipY = c_.rig.hips >= 0 ? bones[size_t(c_.rig.hips)].restWorld.y : 0.5f * c_.height;
    for (size_t k = 0; k < nodes_.size(); ++k) {
        Node& nd = nodes_[k];
        const size_t b = size_t(nd.bone);
        const Vec3 local = pos[b] + (nd.tip ? rotate(rot[b], nd.tipOffset) : Vec3{});
        nd.p = lastWorld_ + rotate(lastRot_, local);
        Vec3 v = clampLen(rotate(lastRot_, jv_[b]) + vel_, 6.0f);
        const float up = bones[b].restWorld.y > hipY ? 1.0f : 0.4f;
        v += impulse * up;
        nd.prev = nd.p - v * kDt;
    }
    for (size_t i = 0; i < bones.size(); ++i) ragLocal_[i] = q_[i];
    rag_ = true;
    asleep_ = restDamp_ = false;
    sleepRef_.clear();
    ragTime_ = sleepTime_ = 0;
    ragAnchor_ = lastWorld_;
    sec_.stiffnessScale = 0.08f;
    sec_.gravityScale = 4.0f;
    events_.push_back("ragdoll");
}

void Animator::ragdollStep(float dt) {
    if (asleep_) return;
    const auto& bones = c_.skeleton.bones;
    const Vec3 g{0, -9.81f, 0};
    // Joint friction: damp the relative motion across every link (flailing dies out, the
    // fall of the whole body does not).
    for (const Link& l : links_) {
        Node &a = nodes_[size_t(l.a)], &b = nodes_[size_t(l.b)];
        const Vec3 rel = (b.p - b.prev) - (a.p - a.prev);
        const float wsum = a.invMass + b.invMass;
        const Vec3 corr = rel * (0.12f / wsum);
        a.prev -= corr * a.invMass;
        b.prev += corr * b.invMass;
    }
    for (Node& nd : nodes_) {
        // Air drag, and rolling resistance once the body is down and nearly still.
        const float settle = std::min(0.045f, 0.012f * std::max(0.0f, ragTime_ - 1.5f));
        const Vec3 v = (nd.p - nd.prev) * (restDamp_ ? 0.95f : 0.995f - settle);
        nd.prev = nd.p;
        nd.p += v + g * (dt * dt);
    }
    auto groundAt = [&](Vec3 p) { return flatGround(settings, p.x, p.z); };
    for (int it = 0; it < 10; ++it) {
        for (const Link& l : links_) {
            Node &a = nodes_[size_t(l.a)], &b = nodes_[size_t(l.b)];
            const Vec3 d = b.p - a.p;
            const float len = length(d);
            if (len < 1e-7f) continue;
            const float wsum = a.invMass + b.invMass;
            const Vec3 corr = d * ((len - l.len) / len * l.k / wsum);
            a.p += corr * a.invMass;
            b.p -= corr * b.invMass;
        }
        for (const auto& pr : pairs_) {
            Node &a = nodes_[size_t(pr.first)], &b = nodes_[size_t(pr.second)];
            const Vec3 d = b.p - a.p;
            const float R = 0.85f * (a.radius + b.radius), len = length(d);
            if (len >= R || len < 1e-6f) continue;
            const float wsum = a.invMass + b.invMass;
            const Vec3 corr = d * ((len - R) / len / wsum);
            a.p += corr * a.invMass;
            b.p -= corr * b.invMass;
        }
        for (Node& nd : nodes_) nd.p.y = std::max(nd.p.y, groundAt(nd.p) + nd.radius);
    }
    // Joint limits and the fading muscle tone, through the rig: rebuild rotations from the
    // particles, clamp them (and pull towards the death pose), pull the particles back.
    // Limits act hard during the fall and soften once the body lies still, so they cannot
    // keep pumping energy into a pose that the ground forbids.
    const float proj = lerp(0.5f, 0.0f, smoothstep(1.0f, 2.5f, ragTime_));
    const float toneW = 0.12f * std::exp(-ragTime_ / 0.8f);
    std::vector<Vec3> r, d;
    std::vector<Quat> W(bones.size()), recon(bones.size());
    Pose loc;
    loc.rot = ragLocal_;
    loc.trans.assign(bones.size(), Vec3{});
    auto parentWorld = [&](size_t i) {
        const int pa = bones[i].parent;
        if (pa < 0) return lastRot_;
        if (boneNode_[size_t(pa)] >= 0) return W[size_t(pa)];
        // An animated parent (the root): its world rotation from the last pose.
        Quat q = lastRot_;
        std::vector<int> chain;
        for (int k = pa; k >= 0; k = bones[size_t(k)].parent) chain.push_back(k);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) q = normalize(q * out_.rot[size_t(*it)]);
        return q;
    };
    for (size_t i = 0; i < bones.size(); ++i) {
        if (boneNode_[i] < 0) continue;
        const Quat wp = parentWorld(i);
        Quat base = normalize(wp * ragLocal_[i]);
        const Vec3 p0 = nodes_[size_t(boneNode_[i])].p;
        r.clear();
        d.clear();
        for (int k : childNodes_[i]) {
            const Node& nd = nodes_[size_t(k)];
            r.push_back(nd.tip ? nd.tipOffset : bones[size_t(nd.bone)].restLocal);
            d.push_back(nd.p - p0);
        }
        if (r.size() == 1) {
            base = normalize(fromTo(normalize(rotate(base, r[0])), normalize(d[0])) * base);
            // Twist is invisible to a single particle; read it off the bend of a hinge below
            // (knee, elbow), so the hinge stays in its plane instead of fighting the limit.
            const Node& cn = nodes_[size_t(childNodes_[i][0])];
            const size_t cb = size_t(cn.bone);
            if (!cn.tip && hinge_[cb] && childNodes_[cb].size() == 1) {
                const Node& gn = nodes_[size_t(childNodes_[cb][0])];
                const Vec3 s0 = rotate(base, gn.tip ? gn.tipOffset : bones[size_t(gn.bone)].restLocal);
                const Vec3 dg = gn.p - cn.p, u = normalize(d[0]);
                const Vec3 nrm = cross(normalize(s0), normalize(dg));
                if (length(nrm) > 0.2f) {
                    const Vec3 aw = rotate(base, hingeAxis_[cb]);
                    const Vec3 a1 = aw - u * dot(aw, u), n1 = normalize(nrm) - u * dot(normalize(nrm), u);
                    const float phi = std::atan2(dot(u, cross(a1, n1)), dot(a1, n1));
                    base = normalize(Quat::axisAngle(u, 0.5f * phi) * base);
                }
            }
        }
        else if (r.size() > 1) base = fitRotation(base, r, d);
        W[i] = base;
        Quat l = normalize(conj(wp) * base);
        recon[i] = l;
        if (toneW > 1e-3f && i < tone_.rot.size()) l = nlerp(l, tone_.rot[i], toneW);
        loc.rot[i] = l;
        W[i] = base;
    }
    limitPose(c_.rig, loc);
    for (size_t i = 0; i < bones.size(); ++i) {
        if (boneNode_[i] < 0) continue;
        ragLocal_[i] = recon[i];  // what the particles show; the limits act through them
        W[i] = normalize(parentWorld(i) * loc.rot[i]);
        // Pull child and parent towards the limited shape, weighted by mass so the
        // correction adds no momentum.
        Node& pn = nodes_[size_t(boneNode_[i])];
        for (int k : childNodes_[i]) {
            Node& nd = nodes_[size_t(k)];
            const Vec3 want = pn.p + rotate(W[i], nd.tip ? nd.tipOffset : bones[size_t(nd.bone)].restLocal);
            const Vec3 delta = (want - nd.p) * proj;
            const float wc = nd.invMass / (nd.invMass + pn.invMass);
            nd.p += delta * wc;
            pn.p -= delta * (1.0f - wc);
        }
    }
    // Ground contact: no sinking, friction on the sliding part of the motion.
    float vsum = 0;
    for (Node& nd : nodes_) {
        const float gy = groundAt(nd.p) + nd.radius;
        if (nd.p.y <= gy + 1e-3f) {
            nd.p.y = gy;
            Vec3 v = nd.p - nd.prev;
            v.x *= 0.55f;
            v.z *= 0.55f;
            v.y = std::max(0.0f, v.y);
            nd.prev = nd.p - v;
        }
        vsum += length(nd.p - nd.prev) / dt;
    }
    ragTime_ += dt;
    const float vavg = vsum / float(std::max<size_t>(1, nodes_.size()));
    restDamp_ = ragTime_ > 1.0f && vavg < 0.25f;
    // Asleep once no particle has wandered more than a hair from where it was half a
    // second ago (a body that only trembles in place is at rest).
    if (sleepRef_.size() != nodes_.size()) sleepRef_.assign(nodes_.size(), Vec3{1e9f, 0, 0});
    float wander = 0;
    for (size_t k = 0; k < nodes_.size(); ++k) wander = std::max(wander, length(nodes_[k].p - sleepRef_[k]));
    if (wander > 0.015f * c_.height) {
        for (size_t k = 0; k < nodes_.size(); ++k) sleepRef_[k] = nodes_[k].p;
        sleepTime_ = 0;
    } else {
        sleepTime_ += dt;
    }
    if (sleepTime_ > 0.5f && ragTime_ > 1.0f) {
        asleep_ = true;
        for (Node& nd : nodes_) nd.prev = nd.p;
    }
}

void Animator::ragdollPose(Vec3 worldPos, Quat worldRot) {
    const auto& bones = c_.skeleton.bones;
    for (size_t i = 0; i < bones.size(); ++i) {
        if (boneNode_[i] < 0) continue;
        const int pa = bones[i].parent;
        out_.trans[i] = {};
        if (pa >= 0 && boneNode_[size_t(pa)] >= 0) {
            out_.rot[i] = ragLocal_[i];
            continue;
        }
        // A body root under the animated root: placed from the particles in world space.
        Quat wp = worldRot;
        Vec3 pp = worldPos;
        std::vector<int> chain;
        for (int k = pa; k >= 0; k = bones[size_t(k)].parent) chain.push_back(k);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            pp = pp + rotate(wp, bones[size_t(*it)].restLocal + out_.trans[size_t(*it)]);
            wp = normalize(wp * out_.rot[size_t(*it)]);
        }
        Quat lastWp = lastRot_;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) lastWp = normalize(lastWp * out_.rot[size_t(*it)]);
        const Quat Wi = normalize(lastWp * ragLocal_[i]);
        out_.rot[i] = normalize(conj(wp) * Wi);
        out_.trans[i] = rotate(conj(wp), nodes_[size_t(boneNode_[i])].p - pp) - bones[i].restLocal;
    }
}

// ---------------------------------------------------------------- frame update

void Animator::update(float dt, Vec3 worldPos, Quat worldRot) {
    events_.clear();
    if (!init_ || (!rag_ && length(worldPos - lastWorld_) > 3.0f)) reset(worldPos, worldRot);
    dt = clamp(dt, 0.0f, 0.1f);
    if (dt > 0 && !rag_) {
        const Vec3 v = (worldPos - lastWorld_) / dt;
        const Vec3 nv = lerp(vel_, v, 1.0f - std::exp(-dt / 0.08f));
        acc_ = lerp(acc_, (nv - vel_) / dt, 1.0f - std::exp(-dt / 0.1f));
        vel_ = nv;
    }
    lastWorld_ = worldPos;
    lastRot_ = worldRot;
    if (cur_.clip.frames.empty() && !base_.frames.empty()) play(base_, 0);
    accum_ += dt;
    int steps = 0;
    while (accum_ >= kDt && steps < 12) {
        accum_ -= kDt;
        ++steps;
        if (rag_) ragdollStep(kDt);
        else substep(kDt, worldPos, worldRot);
    }
    if (steps == 12) accum_ = 0;
    if (rag_) {
        out_.rot = q_;  // the last animated pose for bones outside the ragdoll (hair, bust, sockets)
        out_.trans = tr_;
        ragdollPose(worldPos, worldRot);
    } else {
        out_.rot = q_;
        out_.trans = tr_;
        if (settings.footIK) solveFeet(worldPos, worldRot, dt);
        // Particle positions of this frame, so a ragdoll starts with the body's velocity.
        std::vector<Vec3> pos;
        std::vector<Quat> rot;
        fk(out_, pos, rot);
        lastNodes_.resize(nodes_.size());
        for (size_t k = 0; k < nodes_.size(); ++k) {
            const Node& nd = nodes_[k];
            const size_t b = size_t(nd.bone);
            lastNodes_[k] = worldPos + rotate(worldRot, pos[b] + (nd.tip ? rotate(rot[b], nd.tipOffset) : Vec3{}));
        }
        lastFrameDt_ = dt;
    }
    if (settings.secondary) {
        sec_.floorY = flatGround(settings, worldPos.x, worldPos.z);
        sec_.step(out_, worldPos, worldRot, dt);
    }
}

// ---------------------------------------------------------------- offline baking

Clip bakePhysical(const Character& c, const Clip& clip, MotionSettings s) {
    if (clip.frames.empty()) return clip;
    s.footLock = false;
    s.secondary = false;
    s.ground = nullptr;
    Animator an(c, s);
    an.play(clip, 0);
    an.reset({}, Quat{});
    const size_t N = clip.frames.size();
    const float step = 1.0f / clip.fps;
    Clip out = clip;
    out.frames.clear();
    if (clip.loop) {
        for (size_t f = 0; f < N; ++f) an.update(step, {}, Quat{});  // one cycle to settle into the rhythm
        for (size_t f = 0; f < N; ++f) {
            out.frames.push_back(an.pose());
            an.update(step, {}, Quat{});
        }
        // Spread the remaining seam error over the cycle so the loop closes exactly.
        const Pose& next = an.pose();
        const Pose first = out.frames.front();
        for (size_t f = 0; f < N; ++f) {
            const float w = float(f) / float(N);
            Pose& p = out.frames[f];
            for (size_t i = 0; i < p.rot.size(); ++i) {
                const Quat corr = normalize(first.rot[i] * conj(next.rot[i]));
                p.rot[i] = normalize(nlerp(Quat{}, corr, w) * p.rot[i]);
                p.trans[i] += (first.trans[i] - next.trans[i]) * w;
            }
        }
    } else {
        for (size_t f = 0; f < N; ++f) {
            Pose p = an.pose();
            // Land exactly on the authored last pose, so clips chain cleanly.
            const float w = smoothstep(0.75f, 1.0f, float(f) / float(std::max<size_t>(1, N - 1)));
            for (size_t i = 0; i < p.rot.size(); ++i) {
                p.rot[i] = nlerp(p.rot[i], clip.frames[f].rot[i], w);
                p.trans[i] = lerp(p.trans[i], clip.frames[f].trans[i], w);
            }
            out.frames.push_back(p);
            an.update(step, {}, Quat{});
        }
    }
    return out;
}

Clip bakeDeathClip(const Character& c, Vec3 push, float seconds) {
    MotionSettings s;
    s.footLock = false;
    s.secondary = false;
    Animator an(c, s);
    const Clip idle = makeIdleClip(c);
    an.setBase(idle);
    an.play(idle, 0);
    an.reset({}, Quat{});
    const float step = 1.0f / 30.0f;
    for (int i = 0; i < 4; ++i) an.update(step, {}, Quat{});
    push = normalize(Vec3{push.x, 0, push.z}, {0, 0, -1});
    an.die(push, 1.0f);
    const Clip ref = makeDeathClip(c, push);
    Clip out;
    out.name = "death";
    out.fps = 30;
    out.loop = false;
    out.faceKeys = ref.faceKeys;
    const int n = std::max(2, int(seconds * 30.0f));
    float ragAt = -1;
    for (int f = 0; f < n; ++f) {
        an.update(f == 0 ? 0.0f : step, {}, Quat{});
        for (const std::string& e : an.events())
            if (e == "ragdoll" && ragAt < 0) ragAt = float(f) * step;
        out.frames.push_back(an.pose());
        out.tension.push_back(0.0f);
    }
    out.events = {{0.0f, "impact"}};
    if (ragAt >= 0) out.events.push_back({ragAt, "ragdoll"});
    return out;
}

float clipGroundSpeed(const Character& c, const Clip& clip) {
    if (clip.frames.size() < 3 || c.rig.legs.empty()) return 0;
    const size_t N = clip.frames.size();
    float sum = 0;
    int cnt = 0;
    std::vector<Vec3> pos;
    std::vector<Quat> rot;
    for (const LimbRig& L : c.rig.legs) {
        if (L.end < 0) continue;
        std::vector<Vec3> a(N);
        float lo = 1e9f;
        for (size_t f = 0; f < N; ++f) {
            forwardK(c.skeleton, clip.frames[f], pos, rot);
            a[f] = pos[size_t(L.end)];
            lo = std::min(lo, a[f].y);
        }
        for (size_t f = 0; f + 1 < N; ++f)
            if (a[f].y < lo + 0.02f * c.height && a[f + 1].y < lo + 0.02f * c.height) {
                sum += (a[f].z - a[f + 1].z) * clip.fps;
                ++cnt;
            }
    }
    return cnt ? std::max(0.0f, sum / float(cnt)) : 0.0f;
}

}  // namespace cg
