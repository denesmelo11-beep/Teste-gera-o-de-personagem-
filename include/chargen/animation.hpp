// Procedural clips for the generated rig + CPU pose evaluation / skinning.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "character.hpp"

namespace cg {

// Local bone transforms: rotation, plus translation added to the rest offset.
struct Pose {
    std::vector<Quat> rot;
    std::vector<Vec3> trans;
};

struct Clip {
    std::string name;
    float fps = 30;
    bool loop = true;
    std::vector<Pose> frames;
    std::vector<std::pair<float, FaceState>> faceKeys;  // (time, state), sorted
    float duration() const { return frames.empty() ? 0.0f : float(frames.size() - (loop ? 0 : 1)) / fps; }
};

Pose restPose(const Skeleton& sk);
// Clamps a pose to the rig's joint limits (how far each joint can bend).
void limitPose(const RigInfo& rig, Pose& pose);
Pose sampleClip(const Clip& clip, float time);
FaceState sampleFace(const Clip& clip, float time);

// global = world transform per bone, skin = global * inverseBind.
void poseMatrices(const Skeleton& sk, const Pose& pose, std::vector<Mat4>& global, std::vector<Mat4>& skin);
void skinMesh(const Mesh& mesh, const std::vector<Mat4>& skin, std::vector<Vec3>& pos, std::vector<Vec3>& nrm);

Clip makeIdleClip(const Character& c);
Clip makeWalkClip(const Character& c);
Clip makeRunClip(const Character& c);
Clip makeAttackClip(const Character& c);
Clip makeDodgeClip(const Character& c);
std::vector<Clip> makeDefaultClips(const Character& c);

}  // namespace cg
