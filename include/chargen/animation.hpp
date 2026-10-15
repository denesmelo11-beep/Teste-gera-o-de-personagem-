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
    // Muscle tension per frame, 0 limp .. 1 rigid (empty = 0.5): cg::Animator tracks the
    // clip tighter where it is high (strikes, held poses) and lets it swing where it is low.
    std::vector<float> tension;
    // Gameplay / sound cues (time, name), sorted: "contact" (a strike lands), "release"
    // (a spell leaves the hands), "footL" / "footR", "ragdoll" (death hands over to physics).
    std::vector<std::pair<float, std::string>> events;
    float duration() const { return frames.empty() ? 0.0f : float(frames.size() - (loop ? 0 : 1)) / fps; }
};

Pose restPose(const Skeleton& sk);
// Clamps a pose to the rig's joint limits (how far each joint can bend).
void limitPose(const RigInfo& rig, Pose& pose);
Pose sampleClip(const Clip& clip, float time);
FaceState sampleFace(const Clip& clip, float time);
float sampleTension(const Clip& clip, float time);

// global = world transform per bone, skin = global * inverseBind.
void poseMatrices(const Skeleton& sk, const Pose& pose, std::vector<Mat4>& global, std::vector<Mat4>& skin);
void skinMesh(const Mesh& mesh, const std::vector<Mat4>& skin, std::vector<Vec3>& pos, std::vector<Vec3>& nrm);

Clip makeIdleClip(const Character& c);
Clip makeWalkClip(const Character& c);
Clip makeRunClip(const Character& c);
Clip makeAttackClip(const Character& c);
Clip makeDodgeClip(const Character& c);
// Recoil from a blow pushing the body along `push` (character space; {0,0,-1} = hit from the front).
Clip makeHitClip(const Character& c, Vec3 push = {0, 0, -1}, float strength = 1.0f);
// Death phrase: the blow, knees giving way, collapse ("ragdoll" event where physics takes over).
Clip makeDeathClip(const Character& c, Vec3 push = {0, 0, -1});
// Spell: gather at the hip, channel in front of the chest, thrust both palms (lower arms too).
Clip makeCastClip(const Character& c);
// War cry: inhale, then a wide stance with chest out, arms flung back and a held roar.
Clip makeRoarClip(const Character& c);
// idle, walk, run, attack, dodge, hit, cast, roar, death. With `physical` every clip is
// run through the muscle model (cg::bakePhysical) and the death is a baked ragdoll.
std::vector<Clip> makeDefaultClips(const Character& c, bool physical = true);
// The authored clips only (no physics pass, no ragdoll): the key poses as designed.
std::vector<Clip> makeAuthoredClips(const Character& c);

}  // namespace cg
