// Builder: turns a Genome into skeleton + mesh + physics rig. Split across
// generator.cpp (body), head.cpp (head & face decals) and hair.cpp (hair,
// facial hair, horns, ears).
#pragma once
#include <functional>
#include "builder.hpp"

namespace cg::detail {

struct LodSpec {
    int torsoSeg, headSeg, limbSeg, handSeg, lockSeg, lockSteps, capCols, capRows;
    bool sparseRings;
};
LodSpec lodSpec(int lod);
void applyDecay(Genome& g, float decay);
SkinW chainWeights(std::initializer_list<std::pair<float, int>> keys, float t);  // weights along a bone chain
Vec3 orthoFront(Vec3 d);                                                       // perpendicular to d, facing +z  // bones.cpp: the corpse's genome at a decay stage

struct Part;
struct Mat;
void emitSpike(Part& p, Vec3 base, Vec3 dir, Vec3 sweep, float len, float wid, int kind, const SkinW& w, const Mat& m);

struct ArmMarks {
    Vec3 clav, shoulder, elbow, wrist, handTip;
    Vec3 dir;
    float radius = 0, deltoid = 0, side = 1;
    int pair = 0;
};

struct Marks {
    float H, h, torsoLen;
    float crotchY, hipJointY, waistY, chestY, shoulderY, chinY, kneeY, ankleY;
    Vec3 hipJoint[2], knee[2], ankle[2], ball[2];
    float footLen, footHalfW;
    std::vector<ArmMarks> arms;
    Vec3 neckBase, headPivot, headOrigin;
    // Centaur: hind legs (hip, stifle, hock, fetlock) and the horse barrel.
    Vec3 hindHip[2], hindKnee[2], hindAnkle[2], hindBall[2];
    float barrelY = 0, barrelFront = 0, barrelBack = 0, barrelHalfW = 0, barrelHalfH = 0;
};

class Builder {
public:
    Builder(const Genome& g, const GenOptions& opt);
    Character build();

    // ---- shared helpers ----
    const Genome& g;
    GenOptions opt;
    LodSpec lod;
    Marks m;
    Character ch;
    std::vector<Part> parts;

    Part& newPart(bool flat = false, SubmeshKind kind = SubmeshKind::Body);
    Mat skin() const { return {g.palette.skin, MaterialClass::Skin}; }

    // Torso
    SuperRing torsoRing(float t) const;
    static constexpr float kTorsoBase = 0.12f;  // hip line: the torso loft starts here, legs below
    float torsoT(float y) const { return (y - m.crotchY) / m.torsoLen; }
    float warpTheta(float phi) const;
    float bustField(float t, float th) const;
    float gluteDepth(float t) const;
    Vec3 torsoPoint(float t, float th) const;   // sculpted torso surface
    Vec3 insideTorso(Vec3 p, float k) const;    // clamp a point inside the torso
    Vec3 outsideTorso(Vec3 p) const;            // push a point out onto the torso surface
    Vec3 torsoSurfaceToward(Vec3 p, float* theta = nullptr) const;  // torso surface on the ray from its axis through p

    // Head (head.cpp). x in head units (unscaled by headWidth), s from chin.
    SuperRing headRing(float s) const;           // metres, relative to headOrigin
    float faceRelief(float xn, float s) const;   // sculpted features (nose, brow, cheeks, lips), head units
    float headWarp(float phi) const;             // denser angles at the face centre
    Vec3 headPoint(float theta, float s) const;  // sculpted surface, character space
    Vec3 headSurface(float theta, float s, float offset) const;
    Vec3 headNormalDir(Vec3 p) const;
    Vec3 headCenter() const;
    Vec3 collide(Vec3 p, float margin) const;    // keeps hair out of head and body
    bool headMeshZ(float x, float y, float& z) const;  // front of the built head mesh along -z
    Vec3 outsideHeadMesh(Vec3 p, float margin) const;  // p, pushed out of the built head mesh

private:
    void computeMarks();
    void buildSkeleton();
    void buildTorso();
    void buildArms();
    void buildHand(const ArmMarks& a, const LimbRig& rig);
    void buildLegs();
    void buildHorseBody();
    void buildFeet();
    void buildDigitigradeFeet();
    void buildBones();
    void applyFur();
    void buildFurTufts();
    void buildTail();
    void buildSpines();  // dorsal spikes, plates or knobs along the back
    void buildCore();    // a glowing orb set into the abdomen or chest
    void applySkinKind();  // metallic skin or armoured carapace plates over the flesh
    void buildColliders();
    void initHead();
    // traits.cpp: composable species traits
    void buildWings();
    void buildSerpent();        // replaces the legs (lowerBody 1 coil, 2 wisp, 3 ooze)
    void buildBackTentacles();
    void buildShell();
    void buildShards();
    void buildTraitHand(const ArmMarks& a, const LimbRig& rig);
    void buildTalonFeet();
    void buildBeak();
    void buildMandibles();
    void buildAntennae();
    void buildCompoundEyes();
    void buildCrest();
    void buildTraitEyes();  // cyclopean eye, eyeless sockets
    void buildMaw();
    void buildBodyEyes();
    // head.cpp
    void buildHead();
    void buildNose();
    void buildFaceDecals();
    void buildEyelids();  // modelled lids over stacked eyes (four-eyed peoples)
    // hair.cpp
    void buildHair();
    void buildFacialHair();
    // One faceted lock (a ridged wedge ending in a point) laid on the head surface
    // from (theta, s) along a comb direction; continues freely below the chin.
    void emitClump(Part& p, float theta, float s, float off, float len, float width, float thick, float lift,
                   const std::function<Vec3(Vec3)>& flow, Vec3 color, int bone) const;
    void buildHorns();
    void buildBoneSkeleton();
    void addDecayHoles();
    void buildCavityShells();
    void cullHiddenBones();  // drop skeleton faces no hole can show (most of it while flesh remains)
    // Constructs in ruin (bones.cpp): dents hammered into the shell, and late a forearm
    // torn off at a jagged edge with the iron frame and torn wiring sticking out.
    void dentConstruct();
    void breakConstruct();
    void applyConstructCut();
    void buildTusks();
    void buildEars();

    void addUnderwear(Part& p, bool chestBand) const;
    size_t torsoPart_ = 0;               // index into parts: torso + legs share one surface
    size_t headPart_ = 0;
    size_t nosePart_ = SIZE_MAX;
    float decay_ = 0.0f;
    int upperArmI0_ = 0;                 // first torso ring of the upper arms' opening (four arms)
    bool bonesOnly_ = false;             // full decay / risen skeleton: no flesh at all
    struct Hole {
        Vec3 c;
        float R, depth;
        bool head;
        Vec3 n;  // outward normal (for the ragged rim)
        std::function<bool(Vec3)> gate;
    };
    std::vector<Hole> holes_;
    float torsoD_ = 0.0f, profK_ = 1.0f;     // torso depth and how much its profile may swing (short torsos)
    size_t boneParts0_ = 0, boneParts1_ = 0;  // parts built by buildBoneSkeleton
    std::function<float(Vec3)> cutFlesh_, cutFrame_;  // torn-off limb (< 0 removed)
    size_t wireParts0_ = 0, wireParts1_ = 0;
    size_t torsoTriStart_ = 0;           // first torso-loft triangle in the torso part
    uint32_t torsoFirst_ = 0;            // first torso-loft vertex
    int torsoN_ = 0;                     // vertices per torso ring
    std::vector<float> torsoRingT_;      // t of each torso ring
    std::vector<size_t> torsoHoles_;     // torso triangles replaced by the arms
    std::vector<uint32_t> torsoRing0_;   // bottom ring of the torso (hip line), split by the legs

    uint16_t headPartId_ = 0, bodyGroup_ = 0, armGroup_ = 0;
    Curve cHw_, cDf_, cDb_, cZc_;
    Curve hW_, hZc_, hDf_, hDb_, hNf_, hNb_;
};

}  // namespace cg::detail
