// chargen_viewer: real-time raylib/OpenGL viewer for generated characters.
//
//  * smooth low-poly look (custom GLSL: soft two-band light, sky/ground fill,
//    material-aware shadow tints, rim light, hair sheen)
//  * intentionally low internal resolution, nearest-neighbour upscale
//  * runtime physics (hair chains + bust springs) driven by animation and by
//    characters running around the arena
//  * 2D face expressions switched by UV offset
//
// Interactive:  chargen_viewer [--seed N] [--count K] [--res 480]
// Headless:     chargen_viewer --shots out_dir   (renders a set of PNGs and exits)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/physics.hpp"
#include "chargen/studies.hpp"
#include "raylib.h"
#include "rlgl.h"

namespace {

const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragUV;
void main() {
    fragPos = vertexPosition;
    fragNormal = vertexNormal;
    fragColor = vertexColor;
    fragUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Decals: same inputs, pulled 3 mm towards the camera in view space so the painted
// face never sinks into the sculpted head (engines can use polygon offset instead).
const char* kVSDecal = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 matView;
uniform mat4 matProjection;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragUV;
void main() {
    fragPos = vertexPosition;
    fragNormal = vertexNormal;
    fragColor = vertexColor;
    fragUV = vertexTexCoord;
    vec4 vp = matView * vec4(vertexPosition, 1.0);
    vp.xyz += normalize(-vp.xyz) * 0.003;
    gl_Position = matProjection * vp;
}
)";

// Body: vertex colour (sRGB) + material class in alpha.
const char* kFSBody = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragUV;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 skyColor;
uniform vec3 groundColor;
uniform int faceted;
out vec4 finalColor;
const vec3 shadowTint[8] = vec3[8](vec3(0.78,0.60,0.60), vec3(0.62,0.64,0.74), vec3(0.62,0.58,0.60),
                                   vec3(0.50,0.52,0.62), vec3(0.58,0.58,0.72), vec3(0.64,0.64,0.70), vec3(0.64,0.62,0.66),
                                   vec3(1.0));
void main() {
    vec3 N = normalize(fragNormal);
    if (faceted != 0) {
        vec3 fn = normalize(cross(dFdx(fragPos), dFdy(fragPos)));
        N = dot(fn, N) < 0.0 ? -fn : fn;
    }
    vec3 L = normalize(lightDir);
    vec3 V = normalize(viewPos - fragPos);
    int cls = clamp(int(fragColor.a * 255.0 / 32.0), 0, 7);
    vec3 base = fragColor.rgb;
    if (cls == 7) { finalColor = vec4(min(base * 1.15 + vec3(0.04), vec3(1.0)), 1.0); return; }  // emissive
    float ndl = dot(N, L);
    // Form-revealing ramp: continuous wrap lighting (planes read as distinct tones)
    // blended with a soft terminator for the stylised two-tone read.
    float wrap = clamp((ndl + 0.35) / 1.35, 0.0, 1.0);
    wrap = wrap * wrap * (3.0 - 2.0 * wrap);
    float band = smoothstep(0.02, 0.30, ndl);
    float light = mix(wrap, band, 0.45);
    vec3 shade = base * shadowTint[cls] * 0.44;
    vec3 col = mix(shade, base * 1.06, light);
    // Cool, dim fill from the opposite side keeps shadows readable.
    float fill = clamp(dot(N, normalize(vec3(0.65, 0.15, 0.75))), 0.0, 1.0);
    col += base * vec3(0.09, 0.10, 0.13) * fill;
    // Hemispheric bounce (sky from above, warm ground from below).
    float hemi = N.y * 0.5 + 0.5;
    col += base * mix(groundColor, skyColor, hemi) * 0.07;
    // Rim and sheen.
    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0) * smoothstep(-0.2, 0.4, N.y + 0.2);
    col += vec3(0.80, 0.86, 1.0) * rim * 0.09;
    vec3 H = normalize(L + V);
    float nh = max(dot(N, H), 0.0);
    if (cls == 4) col += vec3(1.0) * 0.10 * smoothstep(0.80, 0.90, nh) * light;   // hair sheen
    if (cls == 0) col += vec3(1.0, 0.9, 0.85) * 0.04 * pow(nh, 24.0);           // skin
    if (cls == 3) {                                                                // metal: tight highlight + sky reflection
        // Tight lobe: on flat facets a wide one lights whole faces and reads as camouflage.
        col += mix(vec3(1.0, 0.97, 0.92), base * 2.0, 0.35) * 0.30 * smoothstep(0.965, 0.995, nh) * (0.4 + 0.6 * light);
        col += mix(groundColor, skyColor, clamp(reflect(-V, N).y * 0.5 + 0.5, 0.0, 1.0)) * base * 0.18;
    }
    // Grade: muted, slightly cool, no candy saturation (grim fantasy).
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(lum), col, 0.80);
    col *= mix(vec3(0.93, 0.95, 1.02), vec3(1.0), smoothstep(0.15, 0.6, lum));
    finalColor = vec4(col, 1.0);
}
)";

// Face decals: atlas sampled with per-feature UV offsets (eyes vs mouth flag in alpha).
const char* kFSDecal = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragUV;
uniform sampler2D texture0;
uniform vec2 eyeOffset;
uniform vec2 mouthOffset;
uniform vec3 lightDir;
out vec4 finalColor;
void main() {
    vec2 off = fragColor.a > 0.75 ? eyeOffset : mouthOffset;
    vec4 t = texture(texture0, fragUV + off);
    if (t.a < 0.35) discard;
    float ndl = dot(normalize(fragNormal), normalize(lightDir));
    float light = mix(0.82, 1.0, smoothstep(-0.2, 0.3, ndl));
    finalColor = vec4(t.rgb * light, 1.0);
}
)";

// Skin layer: 2x multiply (blend DST_COLOR, SRC_COLOR), texel rgb = multiplier / 2.
const char* kFSSkin = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragUV;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    vec4 t = texture(texture0, fragUV);
    vec3 mul = mix(vec3(1.0), t.rgb * 2.0, t.a);
    finalColor = vec4(mul * 0.5, 1.0);
}
)";

struct Actor {
    cg::Character ch;
    std::vector<cg::Clip> clips;
    int clip = 0;
    float time = 0;
    cg::SecondaryMotion* sim = nullptr;
    Mesh body{}, decal{}, skinL{};
    Model bodyModel{}, decalModel{}, skinModel{};
    Texture2D atlas{};
    std::vector<int> decalMap, skinMap;  // decal vertex -> mesh vertex
    std::vector<cg::Vec3> pos, nrm;
    std::vector<cg::Mat4> global, skin;
    cg::Vec3 worldPos;
    float yaw = 0;
    bool runner = false;
    float runPhase = 0, runRadius = 2.0f;
    cg::FaceState face;
    bool faceOverride = false;
    bool posed = false;  // studio: hold showcasePose instead of playing a clip
    cg::Pose fixed;
};

cg::GenOptions gGen;  // mesh style for every spawned character (faceted; --crease / --smooth)

cg::Quat yawQuat(float y) { return cg::Quat::axisAngle({0, 1, 0}, y); }

Shader skinSh{};  // skin-layer shader (loaded once)

void buildMeshes(Actor& a, Shader body, Shader decal) {
    const cg::Mesh& m = a.ch.mesh;
    const size_t nv = m.vertices.size();
    const cg::Submesh& sb = m.submeshes[0];
    a.body = Mesh{};
    a.body.vertexCount = int(nv);
    a.body.triangleCount = int(sb.indexCount / 3);
    a.body.vertices = (float*)MemAlloc(unsigned(nv * 3 * sizeof(float)));
    a.body.normals = (float*)MemAlloc(unsigned(nv * 3 * sizeof(float)));
    a.body.colors = (unsigned char*)MemAlloc(unsigned(nv * 4));
    a.body.texcoords = (float*)MemAlloc(unsigned(nv * 2 * sizeof(float)));
    a.body.indices = (unsigned short*)MemAlloc(unsigned(sb.indexCount * sizeof(unsigned short)));
    for (size_t i = 0; i < nv; ++i) {
        const cg::Vertex& v = m.vertices[i];
        std::memcpy(&a.body.colors[i * 4], v.color, 4);
        a.body.texcoords[i * 2] = v.uv.x;
        a.body.texcoords[i * 2 + 1] = v.uv.y;
    }
    for (uint32_t k = 0; k < sb.indexCount; ++k) a.body.indices[k] = (unsigned short)m.indices[sb.firstIndex + k];
    UploadMesh(&a.body, true);
    a.bodyModel = LoadModelFromMesh(a.body);
    a.bodyModel.materials[0].shader = body;

    // Decals: eyes + mouth (alpha-tested, flag in colour alpha) and the skin layer
    // (multiplied), each remapped into its own mesh.
    auto makeDecal = [&](std::initializer_list<cg::SubmeshKind> kinds, std::vector<int>& map, Mesh& dm, Model& model, Shader sh) {
        std::vector<int> remap(nv, -1);
        std::vector<unsigned short> idx;
        std::vector<unsigned char> flag;
        for (const cg::Submesh& sm : m.submeshes) {
            if (std::find(kinds.begin(), kinds.end(), sm.kind) == kinds.end()) continue;
            for (uint32_t k = 0; k < sm.indexCount; ++k) {
                uint32_t vi = m.indices[sm.firstIndex + k];
                if (remap[vi] < 0) {
                    remap[vi] = int(map.size());
                    map.push_back(int(vi));
                    flag.push_back(sm.kind == cg::SubmeshKind::FaceMouth ? 64 : 255);
                }
                idx.push_back((unsigned short)remap[vi]);
            }
        }
        const size_t nd = map.size();
        dm = Mesh{};
        dm.vertexCount = int(nd);
        dm.triangleCount = int(idx.size() / 3);
        dm.vertices = (float*)MemAlloc(unsigned(std::max<size_t>(nd, 1) * 3 * sizeof(float)));
        dm.normals = (float*)MemAlloc(unsigned(std::max<size_t>(nd, 1) * 3 * sizeof(float)));
        dm.colors = (unsigned char*)MemAlloc(unsigned(std::max<size_t>(nd, 1) * 4));
        dm.texcoords = (float*)MemAlloc(unsigned(std::max<size_t>(nd, 1) * 2 * sizeof(float)));
        dm.indices = (unsigned short*)MemAlloc(unsigned(std::max<size_t>(idx.size(), 3) * sizeof(unsigned short)));
        for (size_t i = 0; i < nd; ++i) {
            const cg::Vertex& v = m.vertices[size_t(map[i])];
            dm.colors[i * 4] = dm.colors[i * 4 + 1] = dm.colors[i * 4 + 2] = 255;
            dm.colors[i * 4 + 3] = flag[i];
            dm.texcoords[i * 2] = v.uv.x;
            dm.texcoords[i * 2 + 1] = v.uv.y;
        }
        if (!idx.empty()) std::memcpy(dm.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&dm, true);
        model = LoadModelFromMesh(dm);
        model.materials[0].shader = sh;
    };
    makeDecal({cg::SubmeshKind::FaceEyes, cg::SubmeshKind::FaceMouth}, a.decalMap, a.decal, a.decalModel, decal);
    makeDecal({cg::SubmeshKind::FaceSkin}, a.skinMap, a.skinL, a.skinModel, skinSh);
    const cg::Image& at = a.ch.faceAtlas;
    Image img{(void*)at.rgba.data(), at.width, at.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    a.atlas = LoadTextureFromImage(img);
    if (const char* dump = std::getenv("CHARGEN_DUMPATLAS"); dump && !FileExists(dump)) ExportImage(img, dump);
    SetTextureFilter(a.atlas, TEXTURE_FILTER_BILINEAR);
    a.decalModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = a.atlas;
    a.skinModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = a.atlas;
}

void unload(Actor& a) {
    UnloadModel(a.bodyModel);
    UnloadModel(a.decalModel);
    UnloadModel(a.skinModel);
    UnloadTexture(a.atlas);
    delete a.sim;
    a.sim = nullptr;
}

// Advance animation + locomotion + physics by dt and upload skinned vertices.
void update(Actor& a, float dt) {
    a.time += dt;
    if (a.runner) {
        a.runPhase += dt * (a.clips[size_t(a.clip)].name == "run" ? 3.6f : 1.4f) / a.runRadius;
        a.worldPos = {std::cos(a.runPhase) * a.runRadius, 0, std::sin(a.runPhase) * a.runRadius};
        a.yaw = -a.runPhase;  // facing along the circle
    }
    const cg::Clip& clip = a.clips[size_t(a.clip)];
    float t = clip.loop ? a.time : std::fmod(a.time, clip.duration() + 0.6f);
    cg::Pose pose = a.posed ? a.fixed : cg::sampleClip(clip, t);
    a.sim->step(pose, a.worldPos, yawQuat(a.yaw), dt);
    if (!a.faceOverride) a.face = cg::sampleFace(clip, t);
    cg::poseMatrices(a.ch.skeleton, pose, a.global, a.skin);
    cg::skinMesh(a.ch.mesh, a.skin, a.pos, a.nrm);
    const cg::Quat q = yawQuat(a.yaw);
    const size_t nv = a.pos.size();
    for (size_t i = 0; i < nv; ++i) {
        cg::Vec3 p = a.worldPos + cg::rotate(q, a.pos[i]), n = cg::rotate(q, a.nrm[i]);
        a.body.vertices[i * 3] = p.x; a.body.vertices[i * 3 + 1] = p.y; a.body.vertices[i * 3 + 2] = p.z;
        a.body.normals[i * 3] = n.x; a.body.normals[i * 3 + 1] = n.y; a.body.normals[i * 3 + 2] = n.z;
    }
    UpdateMeshBuffer(a.body, 0, a.body.vertices, int(nv * 3 * sizeof(float)), 0);
    UpdateMeshBuffer(a.body, 2, a.body.normals, int(nv * 3 * sizeof(float)), 0);
    for (size_t i = 0; i < a.decalMap.size(); ++i) {
        size_t vi = size_t(a.decalMap[i]);
        std::memcpy(&a.decal.vertices[i * 3], &a.body.vertices[vi * 3], 3 * sizeof(float));
        std::memcpy(&a.decal.normals[i * 3], &a.body.normals[vi * 3], 3 * sizeof(float));
    }
    UpdateMeshBuffer(a.decal, 0, a.decal.vertices, int(a.decalMap.size() * 3 * sizeof(float)), 0);
    UpdateMeshBuffer(a.decal, 2, a.decal.normals, int(a.decalMap.size() * 3 * sizeof(float)), 0);
    if (!a.skinMap.empty()) {
        for (size_t i = 0; i < a.skinMap.size(); ++i) {
            size_t vi = size_t(a.skinMap[i]);
            std::memcpy(&a.skinL.vertices[i * 3], &a.body.vertices[vi * 3], 3 * sizeof(float));
            std::memcpy(&a.skinL.normals[i * 3], &a.body.normals[vi * 3], 3 * sizeof(float));
        }
        UpdateMeshBuffer(a.skinL, 0, a.skinL.vertices, int(a.skinMap.size() * 3 * sizeof(float)), 0);
        UpdateMeshBuffer(a.skinL, 2, a.skinL.normals, int(a.skinMap.size() * 3 * sizeof(float)), 0);
    }
}

struct Scene {
    std::vector<Actor> actors;
    Shader body{}, decal{};
    int locLight = -1, locView = -1, locSky = -1, locGround = -1, locEye = -1, locMouth = -1, locLightD = -1;
    Model disc{}, plane{};
    cg::Vec3 light = cg::normalize(cg::Vec3{-0.55f, 0.72f, 0.42f});
    Color skyTop{52, 62, 78, 255}, skyBottom{128, 132, 136, 255};
    bool studio = false, faceted = false;  // studio: neutral grey backdrop, no arena
    int locFaceted = -1;
};

// Model-sheet stance: arms lowered from the bind A-pose to ~25 degrees, soft elbows.
cg::Pose showcasePose(const cg::Character& c) {
    cg::Pose p = cg::restPose(c.skeleton);
    for (const cg::LimbRig& a : c.rig.arms) {
        float lower = a.restDir.y < -0.85f ? 0.0f : 0.36f;
        p.rot[size_t(a.upper)] = cg::Quat::axisAngle({0, 0, 1}, -a.side * lower);
        cg::Vec3 flex = cg::normalize(cg::cross(a.restDir, {0, 0, 1}));
        p.rot[size_t(a.lower)] = cg::Quat::axisAngle(flex, 0.12f);
    }
    for (const cg::LimbRig& l : c.rig.legs) {  // relaxed stance: feet a little wider than the hips
        p.rot[size_t(l.upper)] = cg::Quat::axisAngle({0, 0, 1}, l.side * 0.09f);
        p.rot[size_t(l.end)] = cg::Quat::axisAngle({0, 0, 1}, -l.side * 0.09f);
    }
    return p;
}

void spawn(Scene& s, uint64_t seed, int count, const cg::GenomeOptions& opt, float spacing) {
    for (auto& a : s.actors) unload(a);
    s.actors.clear();
    s.actors.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(i), opt), gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.worldPos = {(float(i) - (count - 1) * 0.5f) * spacing, 0, 0};
        a.time = 0.37f * float(i);
        buildMeshes(a, s.body, s.decal);
        cg::Pose p = cg::sampleClip(a.clips[0], a.time);
        a.sim->reset(p, a.worldPos, cg::Quat{});
        for (int k = 0; k < 90; ++k) update(a, 1.0f / 60.0f);  // settle hair
    }
}

void drawScene(Scene& s, const Camera3D& cam, int w, int h) {
    if (s.studio) DrawRectangleGradientV(0, 0, w, h, Color{40, 47, 58, 255}, Color{22, 27, 35, 255});
    else DrawRectangleGradientV(0, 0, w, h, s.skyTop, s.skyBottom);
    BeginMode3D(cam);
    if (!s.studio) DrawModel(s.plane, {0, 0, 0}, 1.0f, WHITE);
    // Soft blob shadows (after the ground so they blend over it).
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    for (const Actor& a : s.actors) {
        float r = a.ch.height * 0.17f;
        DrawModelEx(s.disc, {a.worldPos.x, 0.004f, a.worldPos.z}, {0, 1, 0}, 0, {r * 1.2f, 1, r}, Color{30, 30, 40, 70});
        DrawModelEx(s.disc, {a.worldPos.x, 0.005f, a.worldPos.z}, {0, 1, 0}, 0, {r * 0.7f, 1, r * 0.6f}, Color{30, 30, 40, 50});
    }
    rlEnableDepthMask();
    EndBlendMode();
    float lp[3] = {s.light.x, s.light.y, s.light.z};
    float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
    float sky[3] = {0.45f, 0.52f, 0.66f}, gnd[3] = {0.36f, 0.32f, 0.26f};
    SetShaderValue(s.body, s.locLight, lp, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locView, vp, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locSky, sky, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locGround, gnd, SHADER_UNIFORM_VEC3);
    int fc = s.faceted ? 1 : 0;
    SetShaderValue(s.body, s.locFaceted, &fc, SHADER_UNIFORM_INT);
    SetShaderValue(s.decal, s.locLightD, lp, SHADER_UNIFORM_VEC3);
    for (Actor& a : s.actors) {
        DrawModel(a.bodyModel, {0, 0, 0}, 1.0f, WHITE);
        if (!a.skinMap.empty() && !std::getenv("CHARGEN_NOSKIN")) {
            rlSetBlendFactors(RL_DST_COLOR, RL_SRC_COLOR, RL_FUNC_ADD);
            BeginBlendMode(BLEND_CUSTOM);
            rlDisableDepthMask();
            DrawModel(a.skinModel, {0, 0, 0}, 1.0f, WHITE);
            rlEnableDepthMask();
            EndBlendMode();
        }
        cg::Vec2 eo = cg::FaceLayout::eyeUvOffset(a.face.eyes), mo = cg::FaceLayout::mouthUvOffset(a.face.mouth);
        float e[2] = {eo.x, eo.y}, mm[2] = {mo.x, mo.y};
        SetShaderValue(s.decal, s.locEye, e, SHADER_UNIFORM_VEC2);
        SetShaderValue(s.decal, s.locMouth, mm, SHADER_UNIFORM_VEC2);
        rlDisableBackfaceCulling();
        DrawModel(a.decalModel, {0, 0, 0}, 1.0f, WHITE);
        rlEnableBackfaceCulling();
    }
    EndMode3D();
}

Image grab(RenderTexture2D rt, int scale) {
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    if (scale > 1) ImageResizeNN(&img, img.width * scale, img.height * scale);
    return img;
}

Camera3D makeCam(cg::Vec3 target, float yaw, float pitch, float dist, float fovy) {
    Camera3D c{};
    c.target = {target.x, target.y, target.z};
    c.position = {target.x + std::sin(yaw) * std::cos(pitch) * dist, target.y + std::sin(pitch) * dist,
                  target.z + std::cos(yaw) * std::cos(pitch) * dist};
    c.up = {0, 1, 0};
    c.fovy = fovy;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

void label(Image& img, const Actor& a, int x, int y, int size, Color col = Color{40, 40, 50, 255}) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s %s %s", std::string(cg::toString(a.ch.genome.persona)).c_str(),
                  std::string(cg::toString(a.ch.genome.ageGroup)).c_str(), std::string(cg::toString(a.ch.genome.hair)).c_str());
    ImageDrawText(&img, buf, x, y, size, col);
}

void setClip(Actor& a, const char* name) {
    for (size_t i = 0; i < a.clips.size(); ++i)
        if (a.clips[i].name == name) a.clip = int(i);
}

// Every haircut on one sheet (CHARGEN_CUTYAW overrides the 3/4 yaw, e.g. 2.4 for the back).
int runCuts(Scene& s, const std::string& path, uint64_t seed) {
    const char* ye = std::getenv("CHARGEN_CUTYAW");
    const float yaw = ye ? float(std::atof(ye)) : 0.75f;
    s.studio = true;
    // Hair silhouette is identity at low resolution.
    // CHARGEN_CUTS="22 23" restricts the sheet to those styles, drawn larger.
    std::vector<int> list;
    if (const char* ce = std::getenv("CHARGEN_CUTS")) {
        std::istringstream in(ce);
        for (int v; in >> v;) list.push_back(v);
    }
    if (list.empty())
        for (int i = 0; i < int(cg::HairStyle::Count); ++i) list.push_back(i);
    const bool zoom = list.size() < size_t(cg::HairStyle::Count);
    const int n = int(list.size()), cols = std::min(n, 8), cw = zoom ? 400 : 220, chh = zoom ? 440 : 250;
    Image sheet = GenImageColor(cols * cw, ((n + cols - 1) / cols) * chh, BLACK);
    for (int cell = 0; cell < n; ++cell) {
        const int i = list[size_t(cell)];
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        cg::GenomeOptions o;
        o.hair = cg::HairStyle(i);
        o.age = cg::AgeGroup::Adult;
        const cg::HairStyle hsI = cg::HairStyle(i);
        bool masc = (cg::isShortHair(hsI) && hsI != cg::HairStyle::Pixie) || hsI == cg::HairStyle::ManBun || hsI == cg::HairStyle::Topknot ||
                    hsI == cg::HairStyle::Mullet || hsI == cg::HairStyle::Locs;
        o.sex = masc ? cg::Sex::Male : cg::Sex::Female;
        if (i == int(cg::HairStyle::Bald)) o.age = cg::AgeGroup::Elder;
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(cg::randomGenome(seed + 900 + uint64_t(i), o), gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(cg::sampleClip(a.clips[0], 0), a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        cg::Vec3 head = a.global[size_t(a.ch.rig.head)].point({0, 0, 0});
        float hh = a.ch.height / a.ch.genome.body.headsTall;
        Camera3D cam = makeCam({head.x, head.y + 0.05f * hh, head.z}, yaw, 0.08f, hh * (zoom ? 4.4f : 6.2f), 30.0f);
        RenderTexture2D prt = LoadRenderTexture(cw, chh);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, cw, chh);
        EndTextureMode();
        Image img = grab(prt, 1);
        ImageDrawText(&img, std::string(cg::toString(cg::HairStyle(i))).c_str(), 6, chh - 18, 12, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(cw), float(chh)}, {float((cell % cols) * cw), float((cell / cols) * chh), float(cw), float(chh)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    s.studio = false;
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    return 0;
}

int runShots(Scene& s, const std::string& dir, uint64_t seed) {
    const int lowW = 480, lowH = 270, scale = 3;
    RenderTexture2D rt = LoadRenderTexture(lowW, lowH);
    SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);
    RenderTexture2D hi = LoadRenderTexture(lowW * scale, lowH * scale);
    auto shoot = [&](RenderTexture2D& target, const Camera3D& cam, const std::string& name, int sc,
                     const std::function<void(Image&)>& post = nullptr) {
        BeginTextureMode(target);
        ClearBackground(BLACK);
        drawScene(s, cam, target.texture.width, target.texture.height);
        EndTextureMode();
        Image img = grab(target, sc);
        if (post) post(img);
        ExportImage(img, (dir + "/" + name).c_str());
        UnloadImage(img);
        std::printf("%s/%s\n", dir.c_str(), name.c_str());
    };
    cg::GenomeOptions opt;
    // 1) Population line-up (natural distribution), low-res game view and a crisp version.
    spawn(s, seed, 8, opt, 0.82f);
    {
        float span = 0.82f * 7;
        Camera3D cam = makeCam({0, 0.92f, 0}, 0.0f, 0.06f, span * 0.92f + 1.6f, 30.0f);
        shoot(rt, cam, "01_elenco_lowres.png", scale);
        shoot(hi, cam, "02_elenco_nitido.png", 1, [&](Image& img) {
            for (size_t i = 0; i < s.actors.size(); ++i) {
                Vector2 sp = GetWorldToScreenEx({s.actors[i].worldPos.x - 0.3f, 0, 0.5f}, cam, img.width, img.height);
                label(img, s.actors[i], int(sp.x), int(sp.y) + 8 + int(i % 2) * 16, 12);
            }
        });
        Camera3D cam34 = makeCam({0, 0.92f, 0}, 0.6f, 0.08f, span * 0.92f + 1.6f, 30.0f);
        shoot(hi, cam34, "03_elenco_3quartos.png", 1);
        Camera3D back = makeCam({0, 0.92f, 0}, 3.1416f, 0.06f, span * 0.92f + 1.6f, 30.0f);
        shoot(hi, back, "04_elenco_costas.png", 1);
    }
    // 2) Portraits: faces as they read at game resolution and up close.
    s.studio = true;
    {
        Image sheet = GenImageColor(4 * 320, 2 * 360, BLACK);
        for (int i = 0; i < 8; ++i) {
            Actor& a = s.actors[size_t(i)];
            cg::Vec3 head = a.global[size_t(a.ch.rig.head)].point({0, 0, 0});
            head = a.worldPos + head;
            float hh = a.ch.height / a.ch.genome.body.headsTall;
            Camera3D cam = makeCam({head.x, head.y + 0.25f * hh, head.z}, 0.35f, 0.05f, hh * 5.2f, 30.0f);
            RenderTexture2D prt = LoadRenderTexture(320, 360);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, 320, 360);
            EndTextureMode();
            Image img = grab(prt, 1);
            label(img, a, 6, 340, 12, Color{225, 225, 230, 255});
            ImageDraw(&sheet, img, {0, 0, 320, 360}, {float((i % 4) * 320), float((i / 4) * 360), 320, 360}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
        ExportImage(sheet, (dir + "/05_retratos.png").c_str());
        UnloadImage(sheet);
        std::printf("%s/05_retratos.png\n", dir.c_str());
    }
    // 3) Expressions of one character.
    {
        Actor& a = s.actors[1];
        a.faceOverride = true;
        Image sheet = GenImageColor(4 * 240, 2 * 260, BLACK);
        const cg::Emotion em[] = {cg::Emotion::Calm, cg::Emotion::Joy, cg::Emotion::Rage, cg::Emotion::Sorrow,
                                  cg::Emotion::Shock, cg::Emotion::Pain, cg::Emotion::Confident, cg::Emotion::Talk};
        const char* names[] = {"calm", "joy", "rage", "sorrow", "shock", "pain", "confident", "talk"};
        cg::Vec3 head = a.worldPos + a.global[size_t(a.ch.rig.head)].point({0, 0, 0});
        float hh = a.ch.height / a.ch.genome.body.headsTall;
        for (int i = 0; i < 8; ++i) {
            a.face = cg::emotionFace(em[i]);
            Camera3D cam = makeCam({head.x, head.y + 0.3f * hh, head.z}, 0.25f, 0.04f, hh * 3.8f, 30.0f);
            RenderTexture2D prt = LoadRenderTexture(240, 260);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, 240, 260);
            EndTextureMode();
            Image img = grab(prt, 1);
            ImageDrawText(&img, names[i], 6, 240, 14, Color{225, 225, 230, 255});
            ImageDraw(&sheet, img, {0, 0, 240, 260}, {float((i % 4) * 240), float((i / 4) * 260), 240, 260}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
        a.faceOverride = false;
        ExportImage(sheet, (dir + "/06_expressoes.png").c_str());
        UnloadImage(sheet);
        std::printf("%s/06_expressoes.png\n", dir.c_str());
    }
    s.studio = false;
    // 4) Physics in action: long-haired / busty characters running in a circle, then dodging.
    {
        cg::GenomeOptions o2;
        spawn(s, seed + 100, 3, o2, 1.2f);
        // Prefer showcase characters: long hair or ponytail.
        for (int i = 0; i < 3; ++i) {
            Actor& a = s.actors[size_t(i)];
            setClip(a, "run");
            a.runner = true;
            a.runRadius = 1.6f + 0.5f * i;
            a.runPhase = 2.0f * i;
        }
        for (int k = 0; k < 120; ++k)
            for (Actor& a : s.actors) update(a, 1.0f / 60.0f);
        Image strip = GenImageColor(6 * 320, 360, BLACK);
        for (int f = 0; f < 6; ++f) {
            for (int k = 0; k < 7; ++k)
                for (Actor& a : s.actors) update(a, 1.0f / 60.0f);
            Actor& a = s.actors[0];
            cg::Vec3 c = a.worldPos + cg::Vec3{0, a.ch.height * 0.55f, 0};
            Camera3D cam = makeCam(c, -a.runPhase + 1.9f, 0.12f, a.ch.height * 1.6f, 40.0f);
            RenderTexture2D prt = LoadRenderTexture(320, 360);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, 320, 360);
            EndTextureMode();
            Image img = grab(prt, 1);
            ImageDraw(&strip, img, {0, 0, 320, 360}, {float(f * 320), 0, 320, 360}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
        ExportImage(strip, (dir + "/07_fisica_corrida.png").c_str());
        UnloadImage(strip);
        std::printf("%s/07_fisica_corrida.png\n", dir.c_str());
        // Dodge sequence (in place) for actor 1.
        Actor& d = s.actors[1];
        d.runner = false;
        d.worldPos = {0, 0, 0};
        d.yaw = 0;
        setClip(d, "dodge");
        d.time = 0;
        d.sim->reset(cg::sampleClip(d.clips[size_t(d.clip)], 0), d.worldPos, cg::Quat{});
        for (Actor& a : s.actors)
            if (&a != &d) a.worldPos = {50, 0, 50};
        Image strip2 = GenImageColor(6 * 320, 360, BLACK);
        for (int f = 0; f < 6; ++f) {
            for (int k = 0; k < 5; ++k) update(d, 1.0f / 60.0f);
            cg::Vec3 c = cg::Vec3{0.3f, d.ch.height * 0.5f, 0};
            Camera3D cam = makeCam(c, 0.5f, 0.1f, d.ch.height * 1.9f, 40.0f);
            RenderTexture2D prt = LoadRenderTexture(320, 360);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, 320, 360);
            EndTextureMode();
            Image img = grab(prt, 1);
            ImageDraw(&strip2, img, {0, 0, 320, 360}, {float(f * 320), 0, 320, 360}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
        ExportImage(strip2, (dir + "/08_fisica_esquiva.png").c_str());
        UnloadImage(strip2);
        std::printf("%s/08_fisica_esquiva.png\n", dir.c_str());
    }
    // 5) Age and persona spread at game resolution.
    {
        std::vector<cg::GenomeOptions> opts;
        const cg::Persona ps[] = {cg::Persona::Heroic, cg::Persona::Villainous, cg::Persona::Cute, cg::Persona::Sexy,
                                  cg::Persona::Stern, cg::Persona::Gentle, cg::Persona::Rogue, cg::Persona::Noble};
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        s.actors.reserve(8);
        for (int i = 0; i < 8; ++i) {
            cg::GenomeOptions o;
            o.persona = ps[i];
            o.age = cg::AgeGroup(i % 4);
            s.actors.emplace_back();
            Actor& a = s.actors.back();
            a.ch = cg::generateCharacter(cg::randomGenome(seed + 500 + uint64_t(i), o), gGen);
            a.clips = cg::makeDefaultClips(a.ch);
            a.sim = new cg::SecondaryMotion(a.ch);
            a.worldPos = {(float(i) - 3.5f) * 0.82f, 0, 0};
            buildMeshes(a, s.body, s.decal);
            a.sim->reset(cg::sampleClip(a.clips[0], 0), a.worldPos, cg::Quat{});
            for (int k = 0; k < 90; ++k) update(a, 1.0f / 60.0f);
        }
        Camera3D cam = makeCam({0, 0.92f, 0}, 0.25f, 0.06f, 0.82f * 7 * 0.92f + 1.6f, 30.0f);
        shoot(hi, cam, "09_personas_idades.png", 1, [&](Image& img) {
            for (size_t i = 0; i < s.actors.size(); ++i) {
                Vector2 sp = GetWorldToScreenEx({s.actors[i].worldPos.x - 0.3f, 0, 0.5f}, cam, img.width, img.height);
                label(img, s.actors[i], int(sp.x), int(sp.y) + 8 + int(i % 2) * 16, 12);
            }
        });
        shoot(rt, cam, "10_personas_lowres.png", scale);
    }
    UnloadRenderTexture(rt);
    UnloadRenderTexture(hi);
    return runCuts(s, dir + "/11_cortes.png", seed);
}

// Debug overrides for the new body variables: CHARGEN_TAIL=type (0..5),
// CHARGEN_SPINES="count kind size", CHARGEN_TUSKCURVE=-1..1.
cg::Genome withOverrides(cg::Genome g) {
    if (const char* t = std::getenv("CHARGEN_TAIL")) {
        g.body.tail = true;
        g.body.tailFur = false;
        g.body.tailType = std::atoi(t);
        g.body.tailLength = std::max(g.body.tailLength, 0.8f);
    }
    if (const char* sp = std::getenv("CHARGEN_SPINES")) {
        float sz = 1.0f;
        std::sscanf(sp, "%d %d %f", &g.body.spines, &g.body.spineKind, &sz);
        g.body.spineSize = sz;
    }
    if (const char* tc = std::getenv("CHARGEN_TUSKCURVE")) g.body.tuskCurve = float(std::atof(tc));
    if (const char* d = std::getenv("CHARGEN_DEF")) g.body.anat.definition = float(std::atof(d));  // debug: muscle definition
    if (const char* mu = std::getenv("CHARGEN_MUSCLE")) g.body.muscle = float(std::atof(mu));
    if (const char* hs = std::getenv("CHARGEN_HAIR"))  // hair style by name
        for (int i = 0; i < int(cg::HairStyle::Count); ++i)
            if (cg::toString(cg::HairStyle(i)) == hs) g.hair = cg::HairStyle(i);
    return g;
}

// Model sheet of a natural adult woman and man: front and three-quarter views on a
// neutral backdrop (how base meshes are judged).
int runStudio(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    for (auto& a : s.actors) unload(a);
    s.actors.clear();
    s.actors.reserve(2);
    for (int i = 0; i < 2; ++i) {
        cg::GenomeOptions o;
        o.sex = i == 0 ? cg::Sex::Female : cg::Sex::Male;
        o.age = cg::AgeGroup::Adult;
        o.persona = cg::Persona::Heroic;
        if (std::getenv("CHARGEN_RAW")) o = cg::GenomeOptions{};  // debug: the plain genome of this seed
        if (const char* se = std::getenv("CHARGEN_SPECIES")) o.species = cg::speciesFromString(se);
        if (const char* ap = std::getenv("CHARGEN_ARMS")) o.armPairs = std::atoi(ap) >= 4 ? 2 : 1;
        if (const char* ar = std::getenv("CHARGEN_ARCH")) {  // debug: studio sheet of a preset
            o.archetype = cg::archetypeFromString(ar);
            o.sex.reset();
        }
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(withOverrides(cg::randomGenome(seed + uint64_t(i), o)), gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.posed = true;
        a.fixed = showcasePose(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
    }
    // Rows: woman, man. Columns: front, three-quarter, side, back.
    const int pw = 340, ph = 640;
    float yaws[4] = {0.0f, 0.75f, 1.5708f, 3.1416f};
    if (const char* ys = std::getenv("CHARGEN_YAWS")) std::sscanf(ys, "%f %f %f %f", &yaws[0], &yaws[1], &yaws[2], &yaws[3]);
    Image sheet = GenImageColor(4 * pw, 2 * ph, BLACK);
    for (int v = 0; v < 8; ++v) {
        Actor& a = s.actors[size_t(v / 4)];
        for (Actor& o : s.actors) o.worldPos = &o == &a ? cg::Vec3{} : cg::Vec3{50, 0, 50};
        for (Actor& o : s.actors) update(o, 0.0f);
        float H = a.ch.height;
        // Debug close-ups: CHARGEN_FOCUS="y zoom" (y as a fraction of height).
        float fy = 0.5f, zoom = 1.0f;
        if (const char* f = std::getenv("CHARGEN_FOCUS")) std::sscanf(f, "%f %f", &fy, &zoom);
        float pitch = 0.02f;
        if (const char* pp = std::getenv("CHARGEN_PITCH")) pitch = float(std::atof(pp));
        // Long bodies (centaurs) are framed by their length as well as their height.
        const float span = a.ch.genome.body.centaur ? 1.45f : 1.0f;
        const float zc = a.ch.genome.body.centaur ? -0.55f * H / a.ch.genome.body.headsTall * 2.0f : 0.0f;
        Camera3D cam = makeCam({0, H * fy, zc}, yaws[v % 4], pitch, H * 3.0f * span / zoom, 21.0f);
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((v % 4) * pw), float((v / 4) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// Population sheet: a row of natural characters (mixed sex, age, persona) and a row
// of the stylised presets, front view in studio light.
// Every people of the world, male and female side by side (each pair framed to its own height).
int runRaces(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const int n = int(cg::Species::Count), cols = 4, pw = 420, ph = 500;
    const int rows = (n + cols - 1) / cols;
    Image sheet = GenImageColor(cols * pw, rows * ph, BLACK);
    const char* yawEnv = std::getenv("CHARGEN_RACEYAW");
    const float yaw = yawEnv ? float(std::atof(yawEnv)) : 0.30f;
    for (int i = 0; i < n; ++i) {
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        float Hmax = 0;
        for (int sx = 0; sx < 2; ++sx) {
            cg::GenomeOptions o;
            o.species = cg::Species(i);
            o.sex = sx == 0 ? cg::Sex::Male : cg::Sex::Female;
            o.age = cg::AgeGroup::Adult;
            s.actors.emplace_back();
            Actor& a = s.actors.back();
            a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(i * 7 + sx), o), gGen);
            Hmax = std::max(Hmax, a.ch.height);
        }
        for (int sx = 0; sx < 2; ++sx) {
            Actor& a = s.actors[size_t(sx)];
            a.worldPos = {(sx == 0 ? -0.24f : 0.24f) * Hmax, 0, 0};
            a.clips = cg::makeDefaultClips(a.ch);
            a.sim = new cg::SecondaryMotion(a.ch);
            a.posed = true;
            a.fixed = showcasePose(a.ch);
            buildMeshes(a, s.body, s.decal);
            a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
            for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        }
        const float H = Hmax * 1.06f;
        Camera3D cam = makeCam({0, H * 0.5f, 0}, yaw, 0.03f, H * 2.55f, 26.0f);
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s  %.2fm / %.2fm", std::string(cg::toString(cg::Species(i))).c_str(), s.actors[0].ch.height,
                      s.actors[1].ch.height);
        ImageDrawText(&img, buf, 8, ph - 18, 12, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((i % cols) * pw), float((i / cols) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// New anatomy variables: tail types with dorsal ridges (top row, from behind) and
// tusks splayed outwards vs hooked inwards (bottom row, portraits).
int runAnatomy(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const int cols = 4, pw = 340, ph = 470;
    Image sheet = GenImageColor(cols * pw, 2 * ph, BLACK);
    struct Panel {
        cg::Species sp;
        cg::Sex sex;
        int tail, spines, kind;
        float tusk;
        const char* label;
    };
    const Panel panels[] = {
        {cg::Species::Orc, cg::Sex::Male, 1, 8, 0, 0.6f, "cauda reptil + espinhos"},
        {cg::Species::Troll, cg::Sex::Female, 2, 7, 2, 0.6f, "chicote com lamina + nos"},
        {cg::Species::Ogre, cg::Sex::Male, 4, 7, 1, 0.6f, "maca com espinhos + placas"},
        {cg::Species::Human, cg::Sex::Female, 5, 0, 0, 0.0f, "cauda de rato"},
        {cg::Species::Orc, cg::Sex::Male, -1, 0, 0, -1.0f, "presas para fora"},
        {cg::Species::Orc, cg::Sex::Male, -1, 0, 0, 1.0f, "presas para dentro"},
        {cg::Species::Troll, cg::Sex::Male, -1, 0, 0, -1.0f, "presas longas para fora"},
        {cg::Species::Troll, cg::Sex::Male, -1, 0, 0, 1.0f, "presas longas para dentro"},
    };
    for (int i = 0; i < 8; ++i) {
        const Panel& P = panels[i];
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        cg::GenomeOptions o;
        o.species = P.sp;
        o.sex = P.sex;
        o.age = cg::AgeGroup::Adult;
        cg::Genome g = cg::randomGenome(seed + uint64_t(i < 4 ? i * 13 : 4 * 13 + (i - 4) / 2 * 13), o);
        if (P.tail >= 0) {
            g.body.tail = true;
            g.body.tailFur = false;
            g.body.tailType = P.tail;
            g.body.tailLength = std::max(g.body.tailLength, 0.9f);
            g.body.spines = P.spines;
            g.body.spineKind = P.kind;
            g.body.spineSize = 1.1f;
            if (P.sp == cg::Species::Human) g.body.horns = 2;
        }
        g.body.tuskCurve = P.tusk;
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(g, gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.posed = true;
        a.fixed = showcasePose(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        const float H = a.ch.height;
        Camera3D cam;
        if (i < 4) {
            cam = makeCam({-0.05f * H, H * 0.46f, -0.30f * H}, 2.0f, 0.05f, H * 3.5f, 21.0f);
        } else {
            const cg::Vec3 hp = a.ch.skeleton.bones[size_t(a.ch.rig.head)].restWorld;
            const float hh = H - hp.y;
            cam = makeCam({0, hp.y + 0.38f * hh, 0.1f * hh}, 0.42f, 0.0f, hh * 7.2f, 21.0f);
        }
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        ImageDrawText(&img, P.label, 8, ph - 18, 12, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((i % cols) * pw), float((i / cols) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// Character studies (cg::studies): full bodies, or portraits with CHARGEN_STUDYFACES=1.
// CHARGEN_STUDIES="a b c" picks a subset.
int runStudies(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    std::vector<std::string> names;
    if (const char* e = std::getenv("CHARGEN_STUDIES")) {
        std::string w;
        for (const char* p = e;; ++p) {
            if (*p == ' ' || *p == 0) {
                if (!w.empty()) names.push_back(w), w.clear();
                if (!*p) break;
            } else w += *p;
        }
    } else
        for (const cg::Study& st : cg::studies()) names.emplace_back(st.name);
    const bool faces = std::getenv("CHARGEN_STUDYFACES") != nullptr;
    const int n = int(names.size()), cols = std::min(n, 6), rows = (n + cols - 1) / cols;
    const int pw = faces ? 300 : 300, ph = faces ? 330 : 520;
    Image sheet = GenImageColor(cols * pw, rows * ph, BLACK);
    for (int i = 0; i < n; ++i) {
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        cg::GenOptions go = gGen;
        auto g = cg::studyGenome(names[size_t(i)], seed, &go);
        if (!g) continue;
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(withOverrides(*g), go);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.posed = true;
        a.fixed = showcasePose(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        const float H = a.ch.height;
        Camera3D cam;
        if (faces) {
            const cg::Vec3 hp = a.ch.skeleton.bones[size_t(a.ch.rig.head)].restWorld;
            const float hh = H - hp.y;
            cam = makeCam({0, hp.y + 0.40f * hh, 0.1f * hh}, 0.38f, 0.0f, hh * 6.8f, 21.0f);
        } else {
            const bool longBody = a.ch.genome.body.centaur || a.ch.genome.body.tail;
            cam = makeCam({0, H * 0.5f, longBody ? -0.2f * H : 0.0f}, 0.55f, 0.03f, H * (longBody ? 3.4f : 2.9f), 21.0f);
        }
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        std::string label = names[size_t(i)];
        for (const cg::Study& st : cg::studies())
            if (st.name == names[size_t(i)]) label = std::string(st.label);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s  %.2fm", label.c_str(), H);
        ImageDrawText(&img, buf, 8, ph - 18, 12, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((i % cols) * pw), float((i / cols) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// Death and decay: columns from alive to bare bones, one row per character.
int runDecay(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const float stages[] = {0.0f, 0.15f, 0.40f, 0.70f, 0.88f, 1.0f};
    const char* labels[] = {"vivo", "recem-morto", "apodrecendo", "putrefato", "quase ossada", "ossada"};
    struct Row { cg::Species sp; cg::Sex sex; };
    const Row rows[] = {{cg::Species::Human, cg::Sex::Male}, {cg::Species::Human, cg::Sex::Female}, {cg::Species::Orc, cg::Sex::Male},
                        {cg::Species::Worgen, cg::Sex::Male}};
    const int cols = 6, nr = 4, pw = 260, ph = 470;
    Image sheet = GenImageColor(cols * pw, nr * ph, BLACK);
    const char* yawEnv = std::getenv("CHARGEN_RACEYAW");
    const float yaw = yawEnv ? float(std::atof(yawEnv)) : 0.45f;
    for (int ri = 0; ri < nr; ++ri)
        for (int c = 0; c < cols; ++c) {
            for (auto& a : s.actors) unload(a);
            s.actors.clear();
            cg::GenomeOptions o;
            o.species = rows[ri].sp;
            o.sex = rows[ri].sex;
            o.age = cg::AgeGroup::Adult;
            cg::GenOptions go = gGen;
            go.decay = stages[c];
            s.actors.emplace_back();
            Actor& a = s.actors.back();
            a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(ri), o), go);
            a.clips = cg::makeDefaultClips(a.ch);
            a.sim = new cg::SecondaryMotion(a.ch);
            a.posed = true;
            a.fixed = showcasePose(a.ch);
            buildMeshes(a, s.body, s.decal);
            a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
            for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
            const float H = a.ch.height * 1.04f;
            Camera3D cam = makeCam({0, H * 0.5f, 0}, yaw, 0.03f, H * 3.1f, 22.0f);
            RenderTexture2D prt = LoadRenderTexture(pw, ph);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, pw, ph);
            EndTextureMode();
            Image img = grab(prt, 1);
            if (ri == 0) ImageDrawText(&img, labels[c], 8, 8, 12, Color{225, 225, 230, 255});
            if (c == 0) ImageDrawText(&img, std::string(cg::toString(a.ch.genome.species)).c_str(), 8, ph - 18, 12, Color{225, 225, 230, 255});
            ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float(c * pw), float(ri * ph), float(pw), float(ph)}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

int runPopulation(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const int n = 8, pw = 220, ph = 560;
    Image sheet = GenImageColor(n * pw, 2 * ph, BLACK);
    const cg::Archetype presets[] = {cg::Archetype::Bombshell, cg::Archetype::Dorito, cg::Archetype::Brute, cg::Archetype::Amazon,
                                     cg::Archetype::Slender, cg::Archetype::Natural, cg::Archetype::Natural, cg::Archetype::Monster};
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < n; ++i) {
            for (auto& a : s.actors) unload(a);
            s.actors.clear();
            cg::GenomeOptions o;
            if (row == 1) {
                o.archetype = presets[i];
                if (i == 5) o.age = cg::AgeGroup::Elder;
                if (i == 6) o.age = cg::AgeGroup::Young;
            }
            s.actors.emplace_back();
            Actor& a = s.actors.back();
            a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(row * 100 + i), o), gGen);
            a.clips = cg::makeDefaultClips(a.ch);
            a.sim = new cg::SecondaryMotion(a.ch);
            a.posed = true;
            a.fixed = showcasePose(a.ch);
            buildMeshes(a, s.body, s.decal);
            a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
            for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
            float H = 2.1f;  // common scale so heights compare
            Camera3D cam = makeCam({0, H * 0.5f, 0}, 0.25f, 0.02f, H * 3.0f, 21.0f);
            RenderTexture2D prt = LoadRenderTexture(pw, ph);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, pw, ph);
            EndTextureMode();
            Image img = grab(prt, 1);
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s %s %s", std::string(cg::toString(a.ch.genome.sex)).c_str(),
                          std::string(cg::toString(a.ch.genome.ageGroup)).c_str(),
                          row ? std::string(cg::toString(a.ch.genome.archetype)).c_str() : std::string(cg::toString(a.ch.genome.persona)).c_str());
            ImageDrawText(&img, buf, 6, ph - 16, 10, Color{220, 220, 225, 255});
            ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float(i * pw), float(row * ph), float(pw), float(ph)}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// Close 3/4 portraits of men with every facial-hair type and a mix of short cuts.
int runHeads(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const int n = 8, pw = 300, ph = 330;
    Image sheet = GenImageColor(4 * pw, 2 * ph, BLACK);
    const cg::HairStyle hs[] = {cg::HairStyle::ShortCrop, cg::HairStyle::SidePart, cg::HairStyle::SlickedBack, cg::HairStyle::Messy,
                                cg::HairStyle::Undercut, cg::HairStyle::ManBun, cg::HairStyle::Ponytail, cg::HairStyle::Shoulder};
    for (int i = 0; i < n; ++i) {
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        cg::GenomeOptions o;
        o.sex = cg::Sex::Male;
        o.age = cg::AgeGroup(1 + i % 3);
        o.hair = hs[i];
        // CHARGEN_HEADRAND=m|f: plain random people of that sex (identity check), else the hair/beard tour.
        const char* hr = std::getenv("CHARGEN_HEADRAND");
        if (hr) {
            o = cg::GenomeOptions{};
            o.sex = hr[0] == 'f' ? cg::Sex::Female : cg::Sex::Male;
        }
        // CHARGEN_SPECIES=name (or "all": one people per portrait).
        if (const char* se = std::getenv("CHARGEN_SPECIES")) {
            if (std::string(se) == "all") o.species = cg::Species((i + int(seed)) % int(cg::Species::Count));
            else o.species = cg::speciesFromString(se);
        }
        cg::Genome g = cg::randomGenome(seed + uint64_t(i), o);
        if (!hr) g.facialHair = cg::FacialHair(i % int(cg::FacialHair::Count));
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(withOverrides(g), gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.posed = true;
        a.fixed = showcasePose(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        cg::Vec3 head = a.global[size_t(a.ch.rig.head)].point({0, 0, 0});
        float hh = a.ch.height / a.ch.genome.body.headsTall;
        const char* hy = std::getenv("CHARGEN_HEADYAW");
        const float hz = std::getenv("CHARGEN_HEADZOOM") ? float(std::atof(std::getenv("CHARGEN_HEADZOOM"))) : 4.6f;
        Camera3D cam = makeCam({head.x, head.y + 0.05f * hh, head.z}, hy ? float(std::atof(hy)) : 0.55f, 0.06f, hh * hz, 30.0f);
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s %s %s", std::string(cg::toString(g.species)).c_str(), std::string(cg::toString(g.hair)).c_str(),
                      std::string(cg::toString(g.facialHair)).c_str());
        ImageDrawText(&img, buf, 6, ph - 16, 10, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((i % 4) * pw), float((i / 4) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

// Random natural population as close portraits (judging how distinct faces are).
int runFaces(Scene& s, const std::string& path, uint64_t seed) {
    s.studio = true;
    const int cols = 6, rows = 3, pw = 240, ph = 270;
    Image sheet = GenImageColor(cols * pw, rows * ph, BLACK);
    for (int i = 0; i < cols * rows; ++i) {
        for (auto& a : s.actors) unload(a);
        s.actors.clear();
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(i) * 7919u), gGen);
        a.clips = cg::makeDefaultClips(a.ch);
        a.sim = new cg::SecondaryMotion(a.ch);
        a.posed = true;
        a.fixed = showcasePose(a.ch);
        buildMeshes(a, s.body, s.decal);
        a.sim->reset(a.fixed, a.worldPos, cg::Quat{});
        for (int k = 0; k < 60; ++k) update(a, 1.0f / 60.0f);
        cg::Vec3 head = a.global[size_t(a.ch.rig.head)].point({0, 0, 0});
        float hh = a.ch.height / a.ch.genome.body.headsTall;
        Camera3D cam = makeCam({head.x, head.y + 0.05f * hh, head.z}, 0.30f, 0.05f, hh * 4.4f, 30.0f);
        RenderTexture2D prt = LoadRenderTexture(pw, ph);
        BeginTextureMode(prt);
        ClearBackground(BLACK);
        drawScene(s, cam, pw, ph);
        EndTextureMode();
        Image img = grab(prt, 1);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s %s %s", std::string(cg::toString(a.ch.genome.persona)).c_str(),
                      std::string(cg::toString(a.ch.genome.ageGroup)).c_str(), std::string(cg::toString(a.ch.genome.hair)).c_str());
        ImageDrawText(&img, buf, 4, ph - 14, 10, Color{225, 225, 230, 255});
        ImageDraw(&sheet, img, {0, 0, float(pw), float(ph)}, {float((i % cols) * pw), float((i / cols) * ph), float(pw), float(ph)}, WHITE);
        UnloadImage(img);
        UnloadRenderTexture(prt);
    }
    ExportImage(sheet, path.c_str());
    UnloadImage(sheet);
    std::printf("%s\n", path.c_str());
    s.studio = false;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string shots, studio, population, heads, faces, cuts, races, decayOut, anatomy, studiesOut;
    bool faceted = false;
    uint64_t seed = 1;
    int count = 6, res = 480;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shots" && i + 1 < argc) shots = argv[++i];
        else if (a == "--studio" && i + 1 < argc) studio = argv[++i];
        else if (a == "--population" && i + 1 < argc) population = argv[++i];
        else if (a == "--heads" && i + 1 < argc) heads = argv[++i];
        else if (a == "--cuts" && i + 1 < argc) cuts = argv[++i];
        else if (a == "--races" && i + 1 < argc) races = argv[++i];
        else if (a == "--decay" && i + 1 < argc) decayOut = argv[++i];
        else if (a == "--anatomy" && i + 1 < argc) anatomy = argv[++i];
        else if (a == "--studies" && i + 1 < argc) studiesOut = argv[++i];
        else if (a == "--faces" && i + 1 < argc) faces = argv[++i];
        else if (a == "--faceted") faceted = true;
        else if (a == "--smooth") gGen.faceted = false, gGen.creaseAngle = 180.0f;
        else if (a == "--crease") gGen.faceted = false;
        else if (a == "--decaylevel" && i + 1 < argc) gGen.decay = float(std::atof(argv[++i]));
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
        else if (a == "--res" && i + 1 < argc) res = std::atoi(argv[++i]);
    }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1440, 810, "chargen viewer");
    Scene s;
    s.body = LoadShaderFromMemory(kVS, kFSBody);
    s.decal = LoadShaderFromMemory(kVSDecal, kFSDecal);
    skinSh = LoadShaderFromMemory(kVSDecal, kFSSkin);
    s.locLight = GetShaderLocation(s.body, "lightDir");
    s.locView = GetShaderLocation(s.body, "viewPos");
    s.locSky = GetShaderLocation(s.body, "skyColor");
    s.locGround = GetShaderLocation(s.body, "groundColor");
    s.locFaceted = GetShaderLocation(s.body, "faceted");
    s.locEye = GetShaderLocation(s.decal, "eyeOffset");
    s.locMouth = GetShaderLocation(s.decal, "mouthOffset");
    s.locLightD = GetShaderLocation(s.decal, "lightDir");
    s.disc = LoadModelFromMesh(GenMeshCylinder(1.0f, 0.001f, 24));
    {   // Stone-and-grass tiles: subtle so characters stay the focus.
        Image chk = GenImageChecked(512, 512, 32, 32, Color{78, 80, 70, 255}, Color{72, 74, 64, 255});
        Texture2D t = LoadTextureFromImage(chk);
        UnloadImage(chk);
        SetTextureFilter(t, TEXTURE_FILTER_POINT);
        s.plane = LoadModelFromMesh(GenMeshPlane(64, 64, 1, 1));
        s.plane.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = t;
    }

    s.faceted = faceted;
    if (!faces.empty()) {
        int rc = runFaces(s, faces, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!studiesOut.empty()) {
        int rc = runStudies(s, studiesOut, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!anatomy.empty()) {
        int rc = runAnatomy(s, anatomy, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!decayOut.empty()) {
        int rc = runDecay(s, decayOut, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!races.empty()) {
        int rc = runRaces(s, races, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!cuts.empty()) {
        int rc = runCuts(s, cuts, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!heads.empty()) {
        int rc = runHeads(s, heads, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!population.empty()) {
        int rc = runPopulation(s, population, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!studio.empty()) {
        int rc = runStudio(s, studio, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }
    if (!shots.empty()) {
        int rc = runShots(s, shots, seed);
        for (auto& a : s.actors) unload(a);
        CloseWindow();
        return rc;
    }

    // ---- interactive ----
    cg::GenomeOptions opt;
    spawn(s, seed, count, opt, 1.1f);
    const int lowW = res, lowH = res * 9 / 16;
    RenderTexture2D rt = LoadRenderTexture(lowW, lowH);
    SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);
    float camYaw = 0.3f, camPitch = 0.15f, camDist = 6.0f;
    bool lowres = true, paused = false;
    int emotion = 0;
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        float dt = paused ? 0.0f : GetFrameTime();
        if (IsKeyPressed(KEY_N)) spawn(s, seed += uint64_t(count), count, opt, 1.1f);
        for (int k = 0; k < 5; ++k)
            if (IsKeyPressed(KEY_ONE + k))
                for (Actor& a : s.actors) a.clip = std::min(k, int(a.clips.size()) - 1), a.time = 0;
        if (IsKeyPressed(KEY_R))
            for (size_t i = 0; i < s.actors.size(); ++i) {
                Actor& a = s.actors[i];
                a.runner = !a.runner;
                a.runRadius = 1.5f + 0.45f * float(i);
                a.runPhase = float(i);
                if (a.runner) setClip(a, "run");
            }
        if (IsKeyPressed(KEY_E)) {
            emotion = (emotion + 1) % int(cg::Emotion::Count);
            for (Actor& a : s.actors) a.faceOverride = emotion != 0, a.face = cg::emotionFace(cg::Emotion(emotion));
        }
        if (IsKeyPressed(KEY_L)) lowres = !lowres;
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_W))
            for (Actor& a : s.actors) a.sim->windStrength = a.sim->windStrength > 0 ? 0.0f : 6.0f;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 d = GetMouseDelta();
            camYaw -= d.x * 0.006f;
            camPitch = std::fmin(1.2f, std::fmax(-0.1f, camPitch + d.y * 0.004f));
        }
        camDist = std::fmax(1.0f, camDist - GetMouseWheelMove() * 0.4f);
        for (Actor& a : s.actors) update(a, dt);
        Camera3D cam = makeCam({0, 0.9f, 0}, camYaw, camPitch, camDist, 40.0f);
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (lowres) {
            BeginTextureMode(rt);
            ClearBackground(BLACK);
            drawScene(s, cam, lowW, lowH);
            EndTextureMode();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        if (lowres) DrawTexturePro(rt.texture, {0, 0, float(lowW), -float(lowH)}, {0, 0, float(sw), float(sh)}, {0, 0}, 0, WHITE);
        else drawScene(s, cam, sw, sh);
        DrawText("N novos | 1-5 idle/walk/run/attack/dodge | R correr em circulo | E expressao | W vento | L baixa res | arrastar: camera",
                 10, 10, 18, Color{20, 20, 30, 255});
        EndDrawing();
    }
    for (auto& a : s.actors) unload(a);
    CloseWindow();
    return 0;
}
