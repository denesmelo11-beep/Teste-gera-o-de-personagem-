// Builder: turns a Genome into skeleton + mesh. Split across generator.cpp
// (body) and hair.cpp (head accessories: hair, horns, ears).
#pragma once
#include "builder.hpp"

namespace cg::detail {

struct LodSpec {
    int torsoSeg, headSeg, limbSeg, handSeg, ellLon, ellLat, lockSeg, lockSteps, capCols, capRows;
    bool sparseRings;
};
LodSpec lodSpec(int lod);

struct ArmMarks {
    Vec3 clav, shoulder, elbow, wrist, handTip;
    Vec3 dir;
    float radius = 0;    // metres
    float deltoid = 0;   // metres
    float side = 1;
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

struct Collider {  // ellipsoid used for hair collision
    Vec3 c, r;
};

class Builder {
public:
    Builder(const Genome& g, const GenOptions& opt);
    Character build();

    // ---- shared helpers (also used by hair.cpp) ----
    const Genome& g;
    GenOptions opt;
    LodSpec lod;
    Marks m;
    Character ch;
    std::vector<Part> parts;
    std::vector<Collider> colliders;

    Vec3 topColor, bottomColor, accent, bootColor, gloveColor;

    Part& newPart(bool flat = false, SubmeshKind kind = SubmeshKind::Body);
    SuperRing torsoRing(float t) const;   // metres, centred on x = 0
    SuperRing headRing(float s) const;    // metres, relative to headOrigin
    float torsoT(float y) const { return (y - m.crotchY) / m.torsoLen; }
    Vec3 headSurface(float theta, float s, float offset) const;
    Vec3 headCenter() const;
    Vec3 collide(Vec3 p, float margin) const;
    Mat skin() const { return {g.palette.skin, MaterialClass::Skin}; }

private:
    void computeMarks();
    void buildSkeleton();
    void buildTorso();
    void buildNeck();
    void buildHead();
    void buildFaceDecals();
    void buildArms();
    void buildHand(const ArmMarks& a, const LimbRig& rig);
    void buildLegs();
    void buildFeet();
    void buildBelt();
    void buildSkirt();
    void buildLoincloth();
    void buildPanels(float yTop, float yBot, float wTop, float wBot, Mat main, Mat trim, bool vTip, bool back);
    void buildShoulderPads();
    void buildAccessories();
    void buildTail();
    // hair.cpp
    void buildHair();
    void buildHorns();
    void buildEars();

    Mat topPaint(float t, float theta) const;
    Mat paintTorsoAt(Vec3 p) const;
    float gluteDepth(float t) const;
    float warpTheta(float phi) const;
    float bustField(float t, float th) const;
    Vec3 torsoPoint(float t, float th) const;  // sculpted torso surface
    Vec3 insideTorso(Vec3 p, float k) const;   // clamp a point inside the torso (k = fraction of the surface radius)
    Mat clothAt(Vec3 base, float t, float theta) const;  // applies pattern

    uint16_t headPartId_ = 0, bodyGroup_ = 0, armGroup_ = 0;
    Curve cHw_, cDf_, cDb_, cZc_;
    Curve hW_, hZc_, hDf_, hDb_, hNf_, hNb_;
};

}  // namespace cg::detail
