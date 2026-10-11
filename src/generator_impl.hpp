// Builder: turns a Genome into skeleton + mesh + physics rig. Split across
// generator.cpp (body), head.cpp (head & face decals) and hair.cpp (hair,
// facial hair, horns, ears).
#pragma once
#include "builder.hpp"

namespace cg::detail {

struct LodSpec {
    int torsoSeg, headSeg, limbSeg, handSeg, lockSeg, lockSteps, capCols, capRows;
    bool sparseRings;
};
LodSpec lodSpec(int lod);

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
    float torsoT(float y) const { return (y - m.crotchY) / m.torsoLen; }
    float warpTheta(float phi) const;
    float bustField(float t, float th) const;
    float gluteDepth(float t) const;
    Vec3 torsoPoint(float t, float th) const;   // sculpted torso surface
    Vec3 insideTorso(Vec3 p, float k) const;    // clamp a point inside the torso

    // Head (head.cpp). x in head units (unscaled by headWidth), s from chin.
    SuperRing headRing(float s) const;           // metres, relative to headOrigin
    float faceRelief(float xn, float s) const;   // sculpted features (nose, brow, cheeks, lips), head units
    float headWarp(float phi) const;             // denser angles at the face centre
    Vec3 headPoint(float theta, float s) const;  // sculpted surface, character space
    Vec3 headSurface(float theta, float s, float offset) const;
    Vec3 headNormalDir(Vec3 p) const;
    Vec3 headCenter() const;
    Vec3 collide(Vec3 p, float margin) const;    // keeps hair out of head and body

private:
    void computeMarks();
    void buildSkeleton();
    void buildTorso();
    void buildNeck();
    void buildArms();
    void buildHand(const ArmMarks& a, const LimbRig& rig);
    void buildLegs();
    void buildFeet();
    void buildTail();
    void buildColliders();
    void initHead();
    // head.cpp
    void buildHead();
    void buildFaceDecals();
    // hair.cpp
    void buildHair();
    void buildFacialHair();
    void buildHorns();
    void buildEars();

    Mat underwearAt(float t, float th) const;
    Mat paintTorsoAt(Vec3 p) const;

    uint16_t headPartId_ = 0, bodyGroup_ = 0, armGroup_ = 0;
    Curve cHw_, cDf_, cDb_, cZc_;
    Curve hW_, hZc_, hDf_, hDb_, hNf_, hNb_;
};

}  // namespace cg::detail
