// Physics-driven procedural animation: the runtime layer between authored clips and
// the skinned mesh.
//
//   cg::Animator anim(character);
//   anim.setBase(idle);                        // what non-looping actions return to
//   anim.play(attack, 0.15f);                  // crossfade into an action
//   each frame:
//     anim.update(dt, worldPos, worldRot);     // muscles, IK, ragdoll, hair / tail physics
//     cg::poseMatrices(character.skeleton, anim.pose(), global, skin);
//     for (auto& e : anim.events()) ...        // "contact", "release", "footL", "ragdoll"...
//   anim.hit(dir, 1.0f, point);                // physical flinch, any time
//   anim.die(dir);                             // death phrase, then a full ragdoll
//
// How it moves:
//   * Muscles. Every joint follows the clip through a damped angular spring instead of
//     copying it. Proximal joints are stiff, distal ones looser and slightly under-damped,
//     so motion breaks joint after joint (overlap) and limbs overshoot and settle
//     (follow-through). Frequencies scale with body size like pendulums: an ogre swings
//     heavy and slow, a gnome snaps. Clips carry a tension curve: strikes go rigid, the
//     recovery goes loose.
//   * Inertia. The acceleration of each joint swings the segment hanging from it, so the
//     head whips when the chest stops, arms lag behind a turn and the whole body leans into
//     acceleration and banks into curves.
//   * Impacts. hit() adds angular momentum along the chain from the point of impact plus
//     a stagger of the hips, layered with a directional recoil phrase.
//   * IK. Feet are placed on the ground (any height function), locked while in stance and
//     tilted to the slope, with the pelvis lowered when a leg cannot reach; the head and
//     spine can track a target and any arm can reach for a point. Two or four legs, two or
//     four arms.
//   * Ragdoll. A Verlet particle body (one particle per joint and limb tip, rigid clusters
//     for pelvis and ribcage) with the rig's joint limits, self collision, ground friction
//     and a fading muscle tone, so a death crumples instead of falling like a plank. Hair
//     and tails keep their spring chains and come to rest on the ground.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "animation.hpp"
#include "character.hpp"
#include "physics.hpp"

namespace cg {

struct MotionSettings {
    float stiffness = 1.0f;  // muscle tone: higher tracks the clip tighter, lower is looser and floppier
    float overlap = 1.0f;    // how much inertia swings the segments (follow-through, whip)
    float lean = 1.0f;       // lean into acceleration and bank into turns
    bool footIK = true;      // plant the feet on the ground
    bool footLock = true;    // pin feet while in stance (stops sliding)
    bool secondary = true;   // hair, tail and bust springs (cg::SecondaryMotion)
    // World ground height; empty means the plane y = 0.
    std::function<float(float x, float z)> ground;
};

class Animator {
public:
    explicit Animator(const Character& c, MotionSettings s = {});

    // Clip that non-looping actions blend back into when they end (usually idle).
    void setBase(const Clip& clip);
    // Crossfade into a clip. Non-looping clips return to the base when done.
    void play(const Clip& clip, float blend = 0.2f, float speed = 1.0f);
    // Physical hit: `dir` is the direction of the blow (world), `point` where it lands
    // (world; defaults to the chest). strength 1 is a solid sword hit.
    void hit(Vec3 dir, float strength = 1.0f, std::optional<Vec3> point = std::nullopt);
    // Death: a recoil and collapse phrase in the direction of the blow, then the ragdoll.
    void die(Vec3 dir, float strength = 1.0f);
    // Go limp at once, with an optional extra velocity (m/s, world).
    void ragdoll(Vec3 impulse = {});
    void lookAt(std::optional<Vec3> worldTarget, float weight = 1.0f);
    void reach(size_t arm, std::optional<Vec3> worldTarget, float weight = 1.0f);

    // Snap every state to the current clip (spawn / teleport).
    void reset(Vec3 worldPos, Quat worldRot);
    void update(float dt, Vec3 worldPos, Quat worldRot);

    const Pose& pose() const { return out_; }
    FaceState face() const;
    // Events crossed during the last update ("contact", "release", "footL", "ragdoll"...).
    const std::vector<std::string>& events() const { return events_; }
    const Clip* clip() const { return cur_.clip.frames.empty() ? nullptr : &cur_.clip; }
    float clipTime() const { return cur_.time; }
    bool busy() const;               // a non-looping action is still playing
    bool isRagdoll() const { return rag_; }
    bool settled() const { return rag_ && asleep_; }  // the body came to rest
    Vec3 bodyCenter() const;          // world centre of the ragdoll / pelvis

    MotionSettings settings;

private:
    struct Layer {
        Clip clip;
        float time = 0, speed = 1;
    };
    struct Node {
        int bone = -1;       // joint of this bone, or the tip of a leaf bone
        bool tip = false;
        Vec3 tipOffset;      // rest offset of a tip from its bone's joint
        float radius = 0.05f, invMass = 1;
        Vec3 p, prev;
    };
    struct Link {
        int a, b;
        float len, k;
    };
    void substep(float dt, Vec3 worldPos, Quat worldRot);
    void targetPose(Pose& t);
    void fk(const Pose& p, std::vector<Vec3>& pos, std::vector<Quat>& rot) const;
    void solveFeet(Vec3 worldPos, Quat worldRot, float dt);
    void buildRagdoll();
    void startRagdoll(Vec3 impulse);
    void ragdollStep(float dt);
    void ragdollPose(Vec3 worldPos, Quat worldRot);
    Clip* advance(Layer& l, float dt, bool events);

    const Character& c_;
    SecondaryMotion sec_;
    std::vector<uint8_t> sim_;        // bone follows the muscle springs
    std::vector<float> freq_, damp_;  // per-bone spring
    std::vector<int> depth_;
    std::vector<Vec3> seg_;           // joint -> segment centre, rest (inertia lever)
    Layer cur_, prev_;
    std::vector<Layer> add_;          // additive reactions
    Clip base_;
    float fade_ = 1, fadeDur_ = 0;
    std::vector<Quat> q_;
    std::vector<Vec3> w_, tr_, tv_;   // rotation, angular velocity, translation, linear velocity
    std::vector<Vec3> jp_, jv_, ja_;  // joint position / velocity / filtered acceleration (character space)
    std::vector<Vec3> fp_;            // scratch FK
    std::vector<Quat> fq_;
    Pose target_, out_;
    Vec3 lastWorld_, vel_, acc_;
    Quat lastRot_;
    float accum_ = 0, sizeK_ = 1;
    bool init_ = false;
    std::vector<std::string> events_;
    // look / reach
    std::optional<Vec3> look_;
    float lookW_ = 0, lookTarget_ = 0;
    std::vector<std::optional<Vec3>> reach_;
    std::vector<float> reachW_, reachTarget_;
    // feet
    struct Foot {
        bool locked = false;
        Vec3 lock;
        float lockW = 0;
    };
    std::vector<Foot> feet_;
    float pelvisDrop_ = 0;
    // death / ragdoll
    bool dying_ = false, rag_ = false, asleep_ = false, restDamp_ = false;
    float ragTime_ = 0, sleepTime_ = 0;
    Vec3 deathDir_;
    float deathStrength_ = 1;
    Pose tone_;
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    std::vector<std::pair<int, int>> pairs_;  // self-collision
    std::vector<int> boneNode_;               // bone -> joint node (-1 if none)
    std::vector<std::vector<int>> childNodes_;
    std::vector<Vec3> lastNodes_;             // node positions last frame (for velocities)
    std::vector<Vec3> sleepRef_;
    std::vector<Quat> ragLocal_;              // ragdoll local rotations
    std::vector<uint8_t> hinge_;              // knees / elbows
    std::vector<Vec3> hingeAxis_;
    float lastFrameDt_ = 1.0f / 60.0f;
    Vec3 ragAnchor_;
};

// Two-bone IK in character space: moves `end`'s joint to `target`, bending in the plane
// that already holds the limb (or towards `pole` when it is straight).
void twoBoneIK(const Skeleton& sk, Pose& pose, int upper, int lower, int end, Vec3 target, Vec3 pole, float weight = 1.0f);

// Ground speed an in-place locomotion clip implies (how fast the planted feet travel back):
// move the character at this speed and the feet neither slide nor skate.
float clipGroundSpeed(const Character& c, const Clip& clip);

// Offline versions for engines that only play clips: runs a clip through the muscle model
// (overlap, follow-through, the tension curve) and records it again; loops stay seamless.
Clip bakePhysical(const Character& c, const Clip& clip, MotionSettings s = {});
// A death simulated to rest with the ragdoll and recorded (falls along `push`, character space).
Clip bakeDeathClip(const Character& c, Vec3 push = {0, 0, -1}, float seconds = 2.6f);

}  // namespace cg
