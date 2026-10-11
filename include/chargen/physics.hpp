// Runtime secondary motion: spring chains for hair / tails and soft-tissue
// springs for the bust, with capsule collisions against the body.
//
//   cg::SecondaryMotion sim(character);
//   each frame:
//     cg::Pose pose = cg::sampleClip(clip, t);           // animated pose
//     sim.step(pose, worldPos, worldRot, dt);             // adds physics in place
//     cg::poseMatrices(character.skeleton, pose, global, skin);
//
// The simulation runs in world space at a fixed 120 Hz, so moving or turning
// the character drives the hair and bust exactly like the animation does.
#pragma once
#include <vector>

#include "animation.hpp"
#include "character.hpp"

namespace cg {

class SecondaryMotion {
public:
    explicit SecondaryMotion(const Character& c);
    // Snap all particles to the current pose (spawn / teleport).
    void reset(const Pose& pose, Vec3 worldPos, Quat worldRot);
    // Advance by dt seconds and write dynamic bone rotations / translations into `pose`.
    void step(Pose& pose, Vec3 worldPos, Quat worldRot, float dt);
    float gravityScale = 1.0f;
    float windStrength = 0.0f;
    float carry = 0.85f;  // share of the body's translation the hair follows rigidly (rest acts as air drag)
    Vec3 windDir{1, 0, 0};

private:
    struct Xf {
        Vec3 p;
        Quat q;
    };
    void forward(const Pose& pose, Vec3 worldPos, Quat worldRot);
    void substep(Pose& pose, float dt, Vec3 rootDelta);
    const Character& c_;
    std::vector<Xf> g_;                       // world transforms (scratch)
    std::vector<std::vector<Vec3>> cur_, prev_;  // chain tails, world space
    std::vector<Vec3> jPos_, jVel_, jPrevTarget_;
    Vec3 lastRoot_, prevDelta_;
    float accum_ = 0, time_ = 0;
    bool init_ = false;
};

}  // namespace cg
