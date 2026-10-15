#include "chargen/physics.hpp"

namespace cg {

namespace {
constexpr float kStep = 1.0f / 120.0f;

Vec3 closestOnSegment(Vec3 p, Vec3 a, Vec3 b) {
    Vec3 ab = b - a;
    float d = dot(ab, ab);
    float t = d > 1e-10f ? saturate(dot(p - a, ab) / d) : 0.0f;
    return a + ab * t;
}
}  // namespace

SecondaryMotion::SecondaryMotion(const Character& c) : c_(c) {
    cur_.resize(c.physics.chains.size());
    prev_.resize(c.physics.chains.size());
    jPos_.resize(c.physics.jiggles.size());
    jVel_.resize(c.physics.jiggles.size());
    jPrevTarget_.resize(c.physics.jiggles.size());
}

void SecondaryMotion::forward(const Pose& pose, Vec3 worldPos, Quat worldRot) {
    const auto& bones = c_.skeleton.bones;
    g_.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& b = bones[i];
        Vec3 t = b.restLocal + (i < pose.trans.size() ? pose.trans[i] : Vec3{});
        Quat q = i < pose.rot.size() ? pose.rot[i] : Quat{};
        if (b.parent < 0) g_[i] = {worldPos + rotate(worldRot, t), normalize(worldRot * q)};
        else {
            const Xf& P = g_[size_t(b.parent)];
            g_[i] = {P.p + rotate(P.q, t), normalize(P.q * q)};
        }
    }
}

void SecondaryMotion::reset(const Pose& pose, Vec3 worldPos, Quat worldRot) {
    forward(pose, worldPos, worldRot);
    const auto& bones = c_.skeleton.bones;
    for (size_t ci = 0; ci < c_.physics.chains.size(); ++ci) {
        const SpringChain& sc = c_.physics.chains[ci];
        cur_[ci].resize(sc.bones.size());
        prev_[ci].resize(sc.bones.size());
        for (size_t j = 0; j < sc.bones.size(); ++j) {
            int b = sc.bones[j];
            Vec3 tailLocal = j + 1 < sc.bones.size() ? bones[size_t(sc.bones[j + 1])].restLocal : sc.tail;
            cur_[ci][j] = prev_[ci][j] = g_[size_t(b)].p + rotate(g_[size_t(b)].q, tailLocal);
        }
    }
    for (size_t k = 0; k < c_.physics.jiggles.size(); ++k) {
        const JiggleBone& jb = c_.physics.jiggles[k];
        const Bone& b = bones[size_t(jb.bone)];
        const Xf& P = g_[size_t(b.parent)];
        jPos_[k] = jPrevTarget_[k] = P.p + rotate(P.q, b.restLocal);
        jVel_[k] = {};
    }
    lastRoot_ = worldPos;
    prevDelta_ = {};
    accum_ = 0;
    init_ = true;
}

void SecondaryMotion::step(Pose& pose, Vec3 worldPos, Quat worldRot, float dt) {
    if (pose.rot.size() < c_.skeleton.bones.size()) return;
    if (!init_ || length(worldPos - lastRoot_) > 2.0f) reset(pose, worldPos, worldRot);
    // Interpolate the root across fixed substeps so fast motion stays smooth.
    const Pose animated = pose;
    accum_ += std::min(dt, 0.1f);
    Vec3 from = lastRoot_;
    int n = 0;
    while (accum_ >= kStep) {
        accum_ -= kStep;
        ++n;
    }
    for (int i = 0; i < n; ++i) {
        float f0 = float(i) / float(n), f = float(i + 1) / float(n);
        pose = animated;
        forward(pose, lerp(from, worldPos, f), worldRot);
        substep(pose, kStep, (worldPos - from) * (f - f0));
    }
    if (n == 0) {  // keep the last simulated result
        pose = animated;
        forward(pose, worldPos, worldRot);
        substep(pose, 0.0f, {});
    }
    lastRoot_ = worldPos;
}

void SecondaryMotion::substep(Pose& pose, float dt, Vec3 rootDelta) {
    const auto& bones = c_.skeleton.bones;
    const Vec3 gravity{0, -9.81f * gravityScale, 0};
    time_ += dt;

    // ---- soft tissue (bust) ----
    for (size_t k = 0; k < c_.physics.jiggles.size(); ++k) {
        const JiggleBone& jb = c_.physics.jiggles[k];
        const Bone& b = bones[size_t(jb.bone)];
        const Xf& P = g_[size_t(b.parent)];
        Vec3 target = P.p + rotate(P.q, b.restLocal + pose.trans[size_t(jb.bone)]);
        if (dt > 0) {
            Vec3 targetVel = (target - jPrevTarget_[k]) / dt;
            float w = kTau * jb.frequency;
            Vec3 a = (target - jPos_[k]) * (w * w) - (jVel_[k] - targetVel) * (2.0f * jb.damping * w) + gravity * jb.gravity;
            jVel_[k] += a * dt;
            jPos_[k] += jVel_[k] * dt;
        }
        jPrevTarget_[k] = target;
        // Clamp the offset to an ellipsoid in the parent frame.
        Vec3 off = rotate(conj(P.q), jPos_[k] - target);
        Vec3 q{off.x / jb.limit.x, off.y / jb.limit.y, off.z / jb.limit.z};
        float l = length(q);
        if (l > 1.0f) {
            off = off / l;
            jPos_[k] = target + rotate(P.q, off);
            jVel_[k] = jVel_[k] * 0.5f;
        }
        pose.trans[size_t(jb.bone)] += off;
        g_[size_t(jb.bone)].p = target + rotate(P.q, off);
    }

    // ---- colliders in world space ----
    struct Cap {
        Vec3 a, b;
        float r;
    };
    std::vector<Cap> caps;
    caps.reserve(c_.physics.colliders.size());
    for (const PhysCollider& col : c_.physics.colliders) {
        const Xf& X = g_[size_t(col.bone)];
        caps.push_back({X.p + rotate(X.q, col.a), X.p + rotate(X.q, col.b), col.radius});
    }

    // ---- spring chains ----
    const Vec3 wind = windDir * (windStrength * (0.6f + 0.4f * std::sin(time_ * 2.3f) * std::sin(time_ * 0.7f)));
    for (size_t ci = 0; ci < c_.physics.chains.size(); ++ci) {
        const SpringChain& sc = c_.physics.chains[ci];
        for (size_t j = 0; j < sc.bones.size(); ++j) {
            const int bi = sc.bones[j];
            const Bone& b = bones[size_t(bi)];
            const Xf& P = g_[size_t(b.parent)];
            Xf X{P.p + rotate(P.q, b.restLocal + pose.trans[size_t(bi)]), normalize(P.q * pose.rot[size_t(bi)])};
            const Vec3 tailLocal = j + 1 < sc.bones.size() ? bones[size_t(sc.bones[j + 1])].restLocal : sc.tail;
            const float len = length(tailLocal);
            if (len < 1e-5f) {
                g_[size_t(bi)] = X;
                continue;
            }
            const Vec3 restDir = rotate(X.q, tailLocal) / len;
            Vec3& cur = cur_[ci][j];
            Vec3& prev = prev_[ci][j];
            Vec3 next = cur;
            if (dt > 0) {
                // Inertia relative to the body; drag only damps that relative motion.
                Vec3 rel = (cur - prev) - prevDelta_ * carry;
                next = cur + rootDelta * carry + rel * (1.0f - sc.drag) + restDir * (sc.stiffness * stiffnessScale * len * dt * 6.0f) +
                       (gravity * sc.gravity + wind) * (dt * dt);
            }
            next = X.p + normalize(next - X.p, restDir) * len;
            for (const Cap& cp : caps) {
                Vec3 q = closestOnSegment(next, cp.a, cp.b);
                Vec3 d = next - q;
                float R = cp.r + sc.hitRadius, dl = length(d);
                if (dl < R && dl > 1e-6f) {
                    next = q + d * (R / dl);
                    next = X.p + normalize(next - X.p, restDir) * len;
                }
            }
            if (next.y < floorY + sc.hitRadius) {
                next.y = floorY + sc.hitRadius;
                next = X.p + normalize(next - X.p, restDir) * len;
            }
            // Angle limit around the styled direction.
            Vec3 dir = normalize(next - X.p, restDir);
            float ang = std::acos(clamp(dot(dir, restDir), -1.0f, 1.0f));
            if (ang > sc.maxAngle) {
                Quat lim = fromTo(restDir, dir);
                Vec3 axis = normalize(Vec3{lim.x, lim.y, lim.z}, {1, 0, 0});
                dir = rotate(Quat::axisAngle(axis, sc.maxAngle), restDir);
                next = X.p + dir * len;
            }
            prev = cur;
            cur = next;
            Quat q = normalize(fromTo(restDir, dir) * X.q);
            pose.rot[size_t(bi)] = normalize(conj(P.q) * q);
            g_[size_t(bi)] = {X.p, q};
        }
    }
    if (dt > 0) prevDelta_ = rootDelta;
}

}  // namespace cg
