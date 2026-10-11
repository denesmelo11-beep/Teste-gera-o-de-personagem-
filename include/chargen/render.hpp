// Tiny software toon renderer used for previews / thumbnails (no GPU needed):
// 2-band cel shading with per-material shadow tints, rim light, hair "angel
// ring" and metal glints, ink outlines from part ids + depth, SSAA.
#pragma once
#include <string>
#include <vector>

#include "character.hpp"
#include "image.hpp"

namespace cg {

struct RenderSettings {
    enum class Style { LowPoly, Toon };
    Style style = Style::LowPoly;  // LowPoly: soft faceted lighting (stylised ARPG); Toon: 2-band cel + ink
    Vec3 lightDir = normalize(Vec3{0.45f, 0.75f, 0.55f});
    float outlineWidth = 2.0f;  // output pixels
    bool outlines = false;
};

class ToonRenderer {
public:
    ToonRenderer(int width, int height, int ssaa = 2, RenderSettings rs = {});
    void setCamera(Vec3 eye, Vec3 target, float fovy, bool ortho = false, float orthoHeight = 2.0f);
    void clearSky(Vec3 top, Vec3 bottom);
    // Floor (checkerboard when c0 != c1) with soft blob shadows: blobs = {x, z, radius}.
    void drawFloor(float halfSize, float tile, Vec3 c0, Vec3 c1, const std::vector<Vec3>& blobs);
    // pos/nrm: skinned vertex data (same order as mesh.vertices).
    void drawCharacter(const Character& c, const std::vector<Vec3>& pos, const std::vector<Vec3>& nrm,
                       const Mat4& model, FaceState face, int idBase);
    void drawText(const std::string& text, int x, int y, int scale, Vec3 color);  // output-pixel coords
    Image finish();

private:
    struct V {
        Vec4 clip;
        float a[12];
    };
    template <class Shade>
    void raster(const V* in, int n, bool cull, Shade&& shade);
    int W_, H_, ss_, w_, h_;
    RenderSettings rs_;
    Mat4 view_, proj_, vp_;
    Vec3 eye_;
    bool ortho_ = false;
    std::vector<Vec3> color_;
    std::vector<float> depth_;
    std::vector<uint32_t> id_;
    struct Text {
        std::string s;
        int x, y, scale;
        Vec3 c;
    };
    std::vector<Text> texts_;
};

// Convenience: renders N characters side by side (idle pose at time t).
Image renderLineup(const std::vector<Character>& chars, int width, int height, float time = 0.6f,
                   bool labels = true, RenderSettings rs = {});

}  // namespace cg
