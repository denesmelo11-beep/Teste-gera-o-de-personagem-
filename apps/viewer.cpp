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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/physics.hpp"
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
const vec3 shadowTint[7] = vec3[7](vec3(0.84,0.62,0.62), vec3(0.62,0.62,0.78), vec3(0.64,0.58,0.62),
                                   vec3(0.48,0.50,0.62), vec3(0.58,0.56,0.78), vec3(0.66,0.64,0.72), vec3(0.66,0.62,0.70));
void main() {
    vec3 N = normalize(fragNormal);
    if (faceted != 0) {
        vec3 fn = normalize(cross(dFdx(fragPos), dFdy(fragPos)));
        N = dot(fn, N) < 0.0 ? -fn : fn;
    }
    vec3 L = normalize(lightDir);
    vec3 V = normalize(viewPos - fragPos);
    int cls = clamp(int(fragColor.a * 255.0 / 32.0), 0, 6);
    vec3 base = fragColor.rgb;
    float ndl = dot(N, L);
    // Form-revealing ramp: continuous wrap lighting (planes read as distinct tones)
    // blended with a soft terminator for the stylised two-tone read.
    float wrap = clamp((ndl + 0.35) / 1.35, 0.0, 1.0);
    wrap = wrap * wrap * (3.0 - 2.0 * wrap);
    float band = smoothstep(0.02, 0.30, ndl);
    float light = mix(wrap, band, 0.45);
    vec3 shade = base * shadowTint[cls] * 0.56;
    vec3 col = mix(shade, base * 1.06, light);
    // Cool, dim fill from the opposite side keeps shadows readable.
    float fill = clamp(dot(N, normalize(vec3(0.65, 0.15, 0.75))), 0.0, 1.0);
    col += base * vec3(0.09, 0.10, 0.13) * fill;
    // Hemispheric bounce (sky from above, warm ground from below).
    float hemi = N.y * 0.5 + 0.5;
    col += base * mix(groundColor, skyColor, hemi) * 0.07;
    // Rim and sheen.
    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0) * smoothstep(-0.2, 0.4, N.y + 0.2);
    col += vec3(1.0, 0.96, 0.92) * rim * 0.14;
    vec3 H = normalize(L + V);
    float nh = max(dot(N, H), 0.0);
    if (cls == 4) col += vec3(1.0) * 0.10 * smoothstep(0.80, 0.90, nh) * light;   // hair sheen
    if (cls == 0) col += vec3(1.0, 0.9, 0.85) * 0.04 * pow(nh, 24.0);           // skin
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

struct Actor {
    cg::Character ch;
    std::vector<cg::Clip> clips;
    int clip = 0;
    float time = 0;
    cg::SecondaryMotion* sim = nullptr;
    Mesh body{}, decal{};
    Model bodyModel{}, decalModel{};
    Texture2D atlas{};
    std::vector<int> decalMap;  // decal vertex -> mesh vertex
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

cg::GenOptions gGen;  // mesh style for every spawned character (--smooth / --flat)

cg::Quat yawQuat(float y) { return cg::Quat::axisAngle({0, 1, 0}, y); }

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

    // Decals: eyes + mouth vertices remapped into their own mesh.
    std::vector<int> remap(nv, -1);
    std::vector<unsigned short> idx;
    for (size_t s = 1; s < m.submeshes.size(); ++s) {
        const cg::Submesh& sm = m.submeshes[s];
        for (uint32_t k = 0; k < sm.indexCount; ++k) {
            uint32_t vi = m.indices[sm.firstIndex + k];
            if (remap[vi] < 0) {
                remap[vi] = int(a.decalMap.size());
                a.decalMap.push_back(int(vi));
            }
            idx.push_back((unsigned short)remap[vi]);
        }
    }
    // Flag eyes vs mouth in the colour alpha.
    std::vector<unsigned char> flag(a.decalMap.size(), 255);
    const cg::Submesh& mouth = m.submeshes[2];
    for (uint32_t k = 0; k < mouth.indexCount; ++k) flag[size_t(remap[m.indices[mouth.firstIndex + k]])] = 64;
    const size_t nd = a.decalMap.size();
    a.decal = Mesh{};
    a.decal.vertexCount = int(nd);
    a.decal.triangleCount = int(idx.size() / 3);
    a.decal.vertices = (float*)MemAlloc(unsigned(nd * 3 * sizeof(float)));
    a.decal.normals = (float*)MemAlloc(unsigned(nd * 3 * sizeof(float)));
    a.decal.colors = (unsigned char*)MemAlloc(unsigned(nd * 4));
    a.decal.texcoords = (float*)MemAlloc(unsigned(nd * 2 * sizeof(float)));
    a.decal.indices = (unsigned short*)MemAlloc(unsigned(idx.size() * sizeof(unsigned short)));
    for (size_t i = 0; i < nd; ++i) {
        const cg::Vertex& v = m.vertices[size_t(a.decalMap[i])];
        a.decal.colors[i * 4] = a.decal.colors[i * 4 + 1] = a.decal.colors[i * 4 + 2] = 255;
        a.decal.colors[i * 4 + 3] = flag[i];
        a.decal.texcoords[i * 2] = v.uv.x;
        a.decal.texcoords[i * 2 + 1] = v.uv.y;
    }
    std::memcpy(a.decal.indices, idx.data(), idx.size() * sizeof(unsigned short));
    UploadMesh(&a.decal, true);
    a.decalModel = LoadModelFromMesh(a.decal);
    a.decalModel.materials[0].shader = decal;
    const cg::Image& at = a.ch.faceAtlas;
    Image img{(void*)at.rgba.data(), at.width, at.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    a.atlas = LoadTextureFromImage(img);
    SetTextureFilter(a.atlas, TEXTURE_FILTER_BILINEAR);
    a.decalModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = a.atlas;
}

void unload(Actor& a) {
    UnloadModel(a.bodyModel);
    UnloadModel(a.decalModel);
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
}

struct Scene {
    std::vector<Actor> actors;
    Shader body{}, decal{};
    int locLight = -1, locView = -1, locSky = -1, locGround = -1, locEye = -1, locMouth = -1, locLightD = -1;
    Model disc{}, plane{};
    cg::Vec3 light = cg::normalize(cg::Vec3{-0.55f, 0.72f, 0.42f});
    Color skyTop{92, 146, 222, 255}, skyBottom{200, 222, 246, 255};
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
    if (s.studio) DrawRectangleGradientV(0, 0, w, h, Color{84, 84, 88, 255}, Color{62, 62, 66, 255});
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
    float sky[3] = {0.55f, 0.66f, 0.90f}, gnd[3] = {0.50f, 0.45f, 0.32f};
    SetShaderValue(s.body, s.locLight, lp, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locView, vp, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locSky, sky, SHADER_UNIFORM_VEC3);
    SetShaderValue(s.body, s.locGround, gnd, SHADER_UNIFORM_VEC3);
    int fc = s.faceted ? 1 : 0;
    SetShaderValue(s.body, s.locFaceted, &fc, SHADER_UNIFORM_INT);
    SetShaderValue(s.decal, s.locLightD, lp, SHADER_UNIFORM_VEC3);
    for (Actor& a : s.actors) {
        DrawModel(a.bodyModel, {0, 0, 0}, 1.0f, WHITE);
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
    // 6) Every haircut, 3/4 portraits (hair silhouette is identity at low resolution).
    s.studio = true;
    {
        const int n = int(cg::HairStyle::Count), cols = 6, cw = 240, chh = 270;
        Image sheet = GenImageColor(cols * cw, ((n + cols - 1) / cols) * chh, BLACK);
        for (int i = 0; i < n; ++i) {
            for (auto& a : s.actors) unload(a);
            s.actors.clear();
            cg::GenomeOptions o;
            o.hair = cg::HairStyle(i);
            o.age = cg::AgeGroup::Adult;
            bool masc = i <= int(cg::HairStyle::Undercut) || i == int(cg::HairStyle::ManBun);
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
            Camera3D cam = makeCam({head.x, head.y + 0.05f * hh, head.z}, 0.75f, 0.08f, hh * 6.2f, 30.0f);
            RenderTexture2D prt = LoadRenderTexture(cw, chh);
            BeginTextureMode(prt);
            ClearBackground(BLACK);
            drawScene(s, cam, cw, chh);
            EndTextureMode();
            Image img = grab(prt, 1);
            ImageDrawText(&img, std::string(cg::toString(cg::HairStyle(i))).c_str(), 6, chh - 18, 12, Color{225, 225, 230, 255});
            ImageDraw(&sheet, img, {0, 0, float(cw), float(chh)}, {float((i % cols) * cw), float((i / cols) * chh), float(cw), float(chh)}, WHITE);
            UnloadImage(img);
            UnloadRenderTexture(prt);
        }
        s.studio = false;
        ExportImage(sheet, (dir + "/11_cortes.png").c_str());
        UnloadImage(sheet);
        std::printf("%s/11_cortes.png\n", dir.c_str());
    }
    UnloadRenderTexture(rt);
    UnloadRenderTexture(hi);
    return 0;
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
        s.actors.emplace_back();
        Actor& a = s.actors.back();
        a.ch = cg::generateCharacter(cg::randomGenome(seed + uint64_t(i), o), gGen);
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
    const float yaws[4] = {0.0f, 0.75f, 1.5708f, 3.1416f};
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
        Camera3D cam = makeCam({0, H * fy, 0}, yaws[v % 4], pitch, H * 3.0f / zoom, 21.0f);
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

}  // namespace

int main(int argc, char** argv) {
    std::string shots, studio, population;
    bool faceted = false;
    uint64_t seed = 1;
    int count = 6, res = 480;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shots" && i + 1 < argc) shots = argv[++i];
        else if (a == "--studio" && i + 1 < argc) studio = argv[++i];
        else if (a == "--population" && i + 1 < argc) population = argv[++i];
        else if (a == "--faceted") faceted = true;
        else if (a == "--smooth") gGen.creaseAngle = 180.0f;
        else if (a == "--flat") gGen.faceted = true;
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
        Image chk = GenImageChecked(512, 512, 32, 32, Color{124, 140, 96, 255}, Color{116, 132, 90, 255});
        Texture2D t = LoadTextureFromImage(chk);
        UnloadImage(chk);
        SetTextureFilter(t, TEXTURE_FILTER_POINT);
        s.plane = LoadModelFromMesh(GenMeshPlane(64, 64, 1, 1));
        s.plane.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = t;
    }

    s.faceted = faceted;
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
