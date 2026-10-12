// chargen_cli: generate characters, previews (PNG) and game-ready GLB files.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/genome.hpp"
#include "chargen/gltf.hpp"
#include "chargen/physics.hpp"
#include "chargen/render.hpp"

using namespace cg;

namespace {

struct Args {
    std::string cmd;
    std::map<std::string, std::string> kv;
    std::string get(const std::string& k, const std::string& d) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : it->second;
    }
    long num(const std::string& k, long d) const { return std::strtol(get(k, std::to_string(d)).c_str(), nullptr, 10); }
    float flt(const std::string& k, float d) const { return std::strtof(get(k, std::to_string(d)).c_str(), nullptr); }
    bool flag(const std::string& k) const { return kv.count(k) > 0; }
};

Args parse(int argc, char** argv) {
    Args a;
    if (argc > 1) a.cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) continue;
        s = s.substr(2);
        if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) a.kv[s] = argv[++i];
        else a.kv[s] = "1";
    }
    return a;
}

GenomeOptions genomeOpts(const Args& a) {
    GenomeOptions o;
    if (a.flag("archetype")) {
        o.archetype = archetypeFromString(a.get("archetype", ""));
        if (!o.archetype) std::fprintf(stderr, "arquetipo desconhecido: %s\n", a.get("archetype", "").c_str());
    }
    o.exaggeration = a.flt("exaggeration", 0.0f);
    if (a.flag("sex")) o.sex = a.get("sex", "") == "male" ? Sex::Male : Sex::Female;
    if (a.flag("age")) o.age = ageFromString(a.get("age", ""));
    if (a.flag("persona")) o.persona = personaFromString(a.get("persona", ""));
    if (a.flag("species")) {
        if (a.get("species", "") == "mixed") o.mixedSpecies = true;
        else if (!(o.species = speciesFromString(a.get("species", "")))) std::fprintf(stderr, "especie desconhecida: %s\n", a.get("species", "").c_str());
    }
    return o;
}

GenOptions genOpts(const Args& a) {
    GenOptions o;
    o.lod = int(a.num("lod", 0));
    o.faceted = !(a.flag("smooth") || a.flag("crease"));
    if (a.flag("smooth")) o.creaseAngle = 180.0f;
    o.decay = a.flt("decay", 0.0f);
    return o;
}

void usage() {
    std::puts(
        "chargen_cli <comando> [opcoes]\n"
        "  lineup  --seed N --count K [--archetype A] [--exaggeration E] [--smooth] [--out lineup.png]\n"
        "  export  --seed N [--archetype A] [--lod 0|1|2] [--smooth] [--out personagem.glb]\n"
        "  faces   --seed N [--archetype A] [--out faces.png]      (folha de expressoes)\n"
        "  anim    --seed N --clip idle|walk|run|attack [--frames 8] [--yaw 0.9] [--out anim.png]\n"
        "  sheet   --seed N [--archetype A] [--out sheet.png]      (turnaround: frente, 3/4, perfil, costas)\n"
        "  portraits --seed N --count K [--out portraits.png]      (variedade de rostos)\n"
        "  atlas   --seed N [--out atlas.png]                      (atlas facial 2D)\n"
        "  stats   [--count 300]                                   (tempo e contagem de triangulos)\n"
        "filtros: --sex female|male --age young|adult|mature|elder --persona heroic|villainous|cute|sexy|stern|gentle|rogue|noble\n"
        "morte: --decay 0..1 (0 vivo, 0.15 recem-morto, 0.4 apodrecendo, 0.7 putrefato, 1 ossada)\n"
        "estilo: padrao low-poly facetado; --crease = suave com vincos; --smooth = totalmente suave; --toon = cel com contorno\n"
        "presets: natural bombshell dorito brute amazon slender chibi monster\n"
        "especies (--species): human dwarf gnome orc troll undead nightelf highelf draenei tauren goblin worgen ogre centaur skeleton | mixed");
}

RenderSettings style(const Args& a) {
    RenderSettings rs;
    if (a.flag("toon")) {
        rs.style = RenderSettings::Style::Toon;
        rs.outlines = true;
    }
    return rs;
}

Character make(const Args& a, uint64_t seed) {
    Genome g = randomGenome(seed, genomeOpts(a));
    if (a.flag("nude")) g.underwear = false;
    return generateCharacter(g, genOpts(a));
}

int cmdLineup(const Args& a) {
    uint64_t seed = uint64_t(a.num("seed", 1));
    int count = int(a.num("count", 8));
    std::vector<Character> chars;
    for (int i = 0; i < count; ++i) chars.push_back(make(a, seed + uint64_t(i)));
    int w = int(a.num("width", 220 * count)), h = int(a.num("height", 560));
    Image img = renderLineup(chars, w, h, 0.6f, !a.flag("nolabels"), style(a));
    std::string out = a.get("out", "lineup.png");
    if (!writePng(out, img)) return 1;
    std::printf("%s (%d personagens)\n", out.c_str(), count);
    return 0;
}

int cmdExport(const Args& a) {
    Character c = make(a, uint64_t(a.num("seed", 1)));
    std::string out = a.get("out", "personagem.glb"), err;
    if (!exportGlb(c, makeDefaultClips(c), out, &err)) {
        std::fprintf(stderr, "erro: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s: %s, %zu tris, %zu verts, %zu ossos\n", out.c_str(), std::string(toString(c.genome.archetype)).c_str(),
                c.mesh.triangleCount(), c.mesh.vertices.size(), c.skeleton.bones.size());
    return 0;
}

// Head close-ups for each emotion preset.
int cmdFaces(const Args& a) {
    Character c = make(a, uint64_t(a.num("seed", 1)));
    const int n = int(Emotion::Count), cell = 256;
    Image sheet(cell * 4, cell * 2);
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    Pose p = restPose(c.skeleton);
    poseMatrices(c.skeleton, p, global, skin);
    skinMesh(c.mesh, skin, pos, nrm);
    Vec3 head = global[size_t(c.rig.head)].point({0, 0, 0});
    float hh = c.height / c.genome.body.headsTall;
    Vec3 target = head + Vec3{0, 0.32f * hh, 0.1f * hh};
    for (int e = 0; e < n; ++e) {
        ToonRenderer r(cell, cell, 2, style(a));
        float yaw = e % 2 ? -0.35f : 0.3f;
        Vec3 eye = target + Vec3{std::sin(yaw), 0.08f, std::cos(yaw)} * (hh * 4.2f);
        r.setCamera(eye, target, 0.45f);
        r.clearSky({0.96f, 0.96f, 0.97f}, {0.99f, 0.99f, 0.99f});
        r.drawCharacter(c, pos, nrm, Mat4::identity(), emotionFace(Emotion(e)), 10);
        static const char* names[] = {"CALM", "JOY", "RAGE", "SORROW", "SHOCK", "PAIN", "CONFIDENT", "TALK"};
        r.drawText(names[e], 8, cell - 22, 2, {0.2f, 0.15f, 0.3f});
        sheet.blit(r.finish(), (e % 4) * cell, (e / 4) * cell);
    }
    std::string out = a.get("out", "faces.png");
    return writePng(out, sheet) ? (std::printf("%s\n", out.c_str()), 0) : 1;
}

int cmdAnim(const Args& a) {
    Character c = make(a, uint64_t(a.num("seed", 1)));
    std::string name = a.get("clip", "walk");
    Clip clip = name == "idle" ? makeIdleClip(c) : name == "run" ? makeRunClip(c) : name == "attack" ? makeAttackClip(c)
              : name == "dodge" ? makeDodgeClip(c) : makeWalkClip(c);
    int frames = int(a.num("frames", 8));
    const int cw = 240, chh = 400;
    Image strip(cw * frames, chh);
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    float H = c.height;
    SecondaryMotion sim(c);
    float simT = 0;
    {
        Pose p0 = sampleClip(clip, 0);
        sim.reset(p0, {}, Quat{});
    }
    for (int f = 0; f < frames; ++f) {
        float t = clip.duration() * float(f) / float(clip.loop ? frames : std::max(1, frames - 1));
        Pose p = sampleClip(clip, t);
        while (simT + 1.0f / 60.0f < t) {  // run the simulation up to this frame
            simT += 1.0f / 60.0f;
            Pose ps = sampleClip(clip, simT);
            sim.step(ps, {}, Quat{}, 1.0f / 60.0f);
        }
        sim.step(p, {}, Quat{}, 0.0f);
        poseMatrices(c.skeleton, p, global, skin);
        skinMesh(c.mesh, skin, pos, nrm);
        ToonRenderer r(cw, chh, 2, style(a));
        Vec3 target{0, H * 0.5f, 0};
        float yaw = a.flt("yaw", 0.9f);
        r.setCamera(target + Vec3{std::sin(yaw), 0.15f, std::cos(yaw)} * (H * 2.6f), target, 0.48f);
        r.clearSky({0.96f, 0.96f, 0.97f}, {0.99f, 0.99f, 0.99f});
        r.drawFloor(20, 0.5f, {0.97f, 0.97f, 0.97f}, {0.97f, 0.97f, 0.97f}, {{0, 0, H * 0.17f}});
        r.drawCharacter(c, pos, nrm, Mat4::identity(), sampleFace(clip, t), 10);
        strip.blit(r.finish(), f * cw, 0);
    }
    std::string out = a.get("out", "anim.png");
    return writePng(out, strip) ? (std::printf("%s\n", out.c_str()), 0) : 1;
}

// Turnaround sheet: front, 3/4, side, back (idle pose, frozen).
int cmdSheet(const Args& a) {
    Character c = make(a, uint64_t(a.num("seed", 1)));
    const int cw = int(a.num("cell", 360)), chh = int(cw * 1.6f);
    const float yaws[] = {0.0f, 0.6f, 1.5708f, 3.1416f};
    Image sheet(cw * 4, chh);
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    Pose p = sampleClip(makeIdleClip(c), float(a.flt("time", 0.0f)));
    poseMatrices(c.skeleton, p, global, skin);
    skinMesh(c.mesh, skin, pos, nrm);
    float H = c.height;
    for (int i = 0; i < 4; ++i) {
        ToonRenderer r(cw, chh, 2, style(a));
        Vec3 target{0, H * 0.5f, 0};
        r.setCamera(target + Vec3{std::sin(yaws[i]), 0.05f, std::cos(yaws[i])} * (H * 2.5f), target, 0.45f);
        r.clearSky({0.96f, 0.96f, 0.97f}, {0.99f, 0.99f, 0.99f});
        r.drawFloor(20, 0.5f, {0.97f, 0.97f, 0.97f}, {0.97f, 0.97f, 0.97f}, {{0, 0, H * 0.17f}});
        r.drawCharacter(c, pos, nrm, Mat4::identity(), FaceState{}, 10);
        if (i == 0) {
            std::string label = std::string(toString(c.genome.persona)) + " " + std::string(toString(c.genome.ageGroup)) + " #" + std::to_string(c.genome.seed);
            r.drawText(label, 10, 10, 2, {0.3f, 0.3f, 0.35f});
        }
        sheet.blit(r.finish(), i * cw, 0);
    }
    std::string out = a.get("out", "sheet.png");
    return writePng(out, sheet) ? (std::printf("%s\n", out.c_str()), 0) : 1;
}

// Portrait grid: one head close-up per seed (face / hair variety).
int cmdPortraits(const Args& a) {
    uint64_t seed = uint64_t(a.num("seed", 1));
    int count = int(a.num("count", 8)), cols = std::min(count, 4), cell = 256;
    int rows = (count + cols - 1) / cols;
    Image sheet(cell * cols, cell * rows);
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    for (int i = 0; i < count; ++i) {
        Character c = make(a, seed + uint64_t(i));
        poseMatrices(c.skeleton, restPose(c.skeleton), global, skin);
        skinMesh(c.mesh, skin, pos, nrm);
        Vec3 head = global[size_t(c.rig.head)].point({0, 0, 0});
        float hh = c.height / c.genome.body.headsTall;
        Vec3 target = head + Vec3{0, 0.30f * hh, 0.1f * hh};
        ToonRenderer r(cell, cell, 2, style(a));
        float yaw = 0.35f;
        r.setCamera(target + Vec3{std::sin(yaw), 0.06f, std::cos(yaw)} * (hh * 4.6f), target, 0.45f);
        r.clearSky({0.30f, 0.29f, 0.31f}, {0.20f, 0.19f, 0.21f});
        r.drawCharacter(c, pos, nrm, Mat4::identity(), FaceState{}, 10);
        r.drawText(std::string(toString(c.genome.persona)) + " " + std::string(toString(c.genome.ageGroup)) + " #" + std::to_string(c.genome.seed), 6, cell - 18, 1, {0.85f, 0.85f, 0.85f});
        sheet.blit(r.finish(), (i % cols) * cell, (i / cols) * cell);
    }
    std::string out = a.get("out", "portraits.png");
    return writePng(out, sheet) ? (std::printf("%s\n", out.c_str()), 0) : 1;
}

int cmdAtlas(const Args& a) {
    Character c = make(a, uint64_t(a.num("seed", 1)));
    std::string out = a.get("out", "atlas.png");
    // Composite over a skin-coloured background so it is readable.
    Image img = c.faceAtlas;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            Vec4 p = img.get(x, y);
            Vec3 bg = c.genome.palette.skin * (((x / 16 + y / 16) & 1) ? 1.0f : 0.94f);
            img.set(x, y, Vec4(lerp(bg, p.xyz(), p.w), 1));
        }
    return writePng(out, img) ? (std::printf("%s\n", out.c_str()), 0) : 1;
}

int cmdStats(const Args& a) {
    int count = int(a.num("count", 300));
    size_t tris[3] = {0, 0, 0}, minT = SIZE_MAX, maxT = 0;
    double ms[3] = {0, 0, 0};
    for (int lod = 0; lod < 3; ++lod)
        for (int i = 0; i < count; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            Genome g = randomGenome(uint64_t(i + 1));
            GenOptions o;
            o.lod = lod;
            Character c = generateCharacter(g, o);
            auto t1 = std::chrono::steady_clock::now();
            ms[lod] += std::chrono::duration<double, std::milli>(t1 - t0).count();
            tris[lod] += c.mesh.triangleCount();
            if (lod == 0) {
                minT = std::min(minT, c.mesh.triangleCount());
                maxT = std::max(maxT, c.mesh.triangleCount());
            }
        }
    for (int lod = 0; lod < 3; ++lod)
        std::printf("LOD%d: %.0f tris em media, %.2f ms/personagem (inclui atlas facial)\n", lod, double(tris[lod]) / count,
                    ms[lod] / count);
    std::printf("LOD0 min/max tris: %zu / %zu\n", minT, maxT);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a = parse(argc, argv);
    if (a.cmd == "lineup") return cmdLineup(a);
    if (a.cmd == "export") return cmdExport(a);
    if (a.cmd == "faces") return cmdFaces(a);
    if (a.cmd == "anim") return cmdAnim(a);
    if (a.cmd == "atlas") return cmdAtlas(a);
    if (a.cmd == "sheet") return cmdSheet(a);
    if (a.cmd == "portraits") return cmdPortraits(a);
    if (a.cmd == "stats") return cmdStats(a);
    usage();
    return a.cmd.empty() ? 0 : 1;
}
