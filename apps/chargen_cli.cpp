// chargen_cli: generate characters, previews (PNG) and game-ready GLB files.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/engine.hpp"
#include "chargen/genome.hpp"
#include "chargen/gltf.hpp"
#include "chargen/motion.hpp"
#include "chargen/physics.hpp"
#include "chargen/render.hpp"
#include "chargen/studies.hpp"

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
    if (a.flag("arms")) o.armPairs = a.num("arms", 2) >= 4 ? 2 : 1;
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
        "  anim    --seed N --clip idle|walk|run|attack|dodge|hit|cast|roar|death [--frames 8] [--yaw 0.9] [--authored] [--out anim.png]\n"
        "  motion  --actors \"human,ogre@4,study:centaur_warlord\" --script \"idle:1,attack,hit:front,cast,roar,death:back\"\n"
        "          [--fps 25] [--cell 300] [--samescale] [--hills] [--stiffness 1] [--overlap 1] [--out motion.gif|.png]\n"
        "          (animacao procedural com fisica: walk:s run:s circle:s idle:s hit:lado combo death:lado e qualquer clipe)\n"
        "  sheet   --seed N [--archetype A] [--out sheet.png]      (turnaround: frente, 3/4, perfil, costas)\n"
        "  portraits --seed N --count K [--out portraits.png]      (variedade de rostos)\n"
        "  atlas   --seed N [--out atlas.png]                      (atlas facial 2D)\n"
        "  stats   [--count 300]                                   (tempo e contagem de triangulos)\n"
        "  info    --seed N | --key CG1-...                         (chave, descricao, metricas, tracos, hitboxes)\n"
        "  family  --seed N [--children 4] [--species S]           (pais, filhos herdando, e o pai envelhecendo)\n"
        "  corpse  --seed N [--days 30]                            (linha do tempo da decomposicao)\n"
        "  stress  [--count 1000] [--seed 1]                       (fuzz: genomas extremos, validacao, tempos)\n"
        "filtros: --sex female|male --age young|adult|mature|elder --persona heroic|villainous|cute|sexy|stern|gentle|rogue|noble\n"
        "anatomia: --arms 2|4 (dois pares de bracos)\n"
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
    Clip clip = makeWalkClip(c);
    for (Clip& k : a.flag("authored") ? makeAuthoredClips(c) : makeDefaultClips(c))
        if (k.name == name) clip = k;
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

void printTraits(const PhysicalTraits& t) {
    std::printf("  tracos: forca %.2f  agilidade %.2f  resistencia %.2f  porte %.2f  presenca %.2f  vigor %.2f\n", t.strength,
                t.agility, t.endurance, t.size, t.presence, t.vigor);
}

int cmdInfo(const Args& a) {
    CharacterKey k;
    if (a.flag("key")) {
        auto dk = decodeKey(a.get("key", ""));
        if (!dk) return std::fprintf(stderr, "chave invalida\n"), 1;
        k = *dk;
    } else {
        k.seed = uint64_t(a.num("seed", 1));
        k.options = genomeOpts(a);
        k.decay = a.flt("decay", 0.0f);
        k.lod = int(a.num("lod", 0));
    }
    Character c = generateFromKey(k);
    const BodyMetrics m = measure(c);
    std::printf("%s\n  chave: %s\n", describe(c.genome).c_str(), encodeKey(k).c_str());
    std::printf("  altura %.2f m, olhos %.2f m, ombros %.2f m (vao %.2f m), quadril %.2f m, alcance %.2f m\n", m.height,
                m.eyeHeight, m.shoulderHeight, m.shoulderSpan, m.hipHeight, m.reach);
    std::printf("  massa ~%.0f kg, andar %.2f m/s, correr %.2f m/s, capsula r %.2f h %.2f, %d bracos%s\n", m.mass, m.walkSpeed,
                m.runSpeed, m.capsuleRadius, m.capsuleHeight, m.arms, m.quadruped ? ", quadrupede" : "");
    printTraits(physicalTraits(c.genome));
    const auto vols = hitVolumes(c);
    std::printf("  hitboxes (%zu):", vols.size());
    for (const HitVolume& v : vols)
        std::printf(" %s%s", std::string(toString(v.zone)).c_str(), v.side > 0 ? ".L" : v.side < 0 ? ".R" : "");
    const PackedMesh pm = packMesh(c.mesh);
    std::printf("\n  malha: %zu vertices, %zu tris; empacotada %.1f KB (%s indices), original %.1f KB\n", c.mesh.vertices.size(),
                c.mesh.triangleCount(), (pm.vertices.size() * sizeof(PackedVertex) + pm.indices16.size() * 2 + pm.indices32.size() * 4) / 1024.0,
                pm.wide() ? "32-bit" : "16-bit", (c.mesh.vertices.size() * sizeof(Vertex) + c.mesh.indices.size() * 4) / 1024.0);
    return 0;
}

int cmdFamily(const Args& a) {
    const uint64_t seed = uint64_t(a.num("seed", 1));
    GenomeOptions o = genomeOpts(a);
    o.age = AgeGroup::Adult;
    o.sex = Sex::Female;
    const Genome mo = randomGenome(seed, o);
    o.sex = Sex::Male;
    if (a.flag("father")) o.species = speciesFromString(a.get("father", ""));
    const Genome fa = randomGenome(seed + 1000, o);
    std::printf("mae: %s\npai: %s\n", describe(mo).c_str(), describe(fa).c_str());
    for (int i = 0; i < int(a.num("children", 4)); ++i) {
        const Genome ch = childOf(mo, fa, seed * 31 + uint64_t(i));
        std::printf("  filho %d: %s\n", i + 1, describe(ch).c_str());
        printTraits(physicalTraits(ch));
    }
    for (float age : {50.0f, 65.0f, 80.0f}) {
        const Genome old = aged(fa, age);
        std::printf("pai aos %.0f: %s\n", age, describe(old).c_str());
        printTraits(physicalTraits(old));
    }
    return 0;
}

// ---- motion: physics-driven animation scripts rendered to an animated GIF ----

struct Actor {
    std::string label;
    Character ch;
    std::unique_ptr<Animator> anim;
    std::vector<Clip> clips;
    Vec3 pos;
    float yaw = 0;
    const Clip& clip(const std::string& n) const {
        for (const Clip& k : clips)
            if (k.name == n) return k;
        return clips[0];
    }
};

// Actor tokens: [study:]name[@4][#seed], e.g. "orc", "ogre@4#7", "study:centaur_warlord".
bool makeActor(const Args& a, const std::string& tok, uint64_t seed, Actor& out) {
    std::string t = tok;
    int arms = 0;
    if (auto h = t.find('#'); h != std::string::npos) seed = std::strtoull(t.substr(h + 1).c_str(), nullptr, 10), t = t.substr(0, h);
    if (auto at = t.find('@'); at != std::string::npos) arms = std::atoi(t.substr(at + 1).c_str()), t = t.substr(0, at);
    GenOptions go = genOpts(a);
    Genome g;
    if (t.rfind("study:", 0) == 0) {
        auto sg = studyGenome(t.substr(6), seed, &go);
        if (!sg) return std::fprintf(stderr, "estudo desconhecido: %s\n", t.c_str()), false;
        g = *sg;
    } else {
        GenomeOptions o = genomeOpts(a);
        if (!t.empty() && t != "random") {
            if (!(o.species = speciesFromString(t))) return std::fprintf(stderr, "especie desconhecida: %s\n", t.c_str()), false;
        }
        if (arms) o.armPairs = arms >= 4 ? 2 : 1;
        g = randomGenome(seed, o);
    }
    out.label = t.rfind("study:", 0) == 0 ? t.substr(6) : (t.empty() ? std::string("random") : t);
    out.ch = generateCharacter(g, go);
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        if (j > i) v.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return v;
}

int cmdMotion(const Args& a) {
    const uint64_t seed = uint64_t(a.num("seed", 1));
    std::vector<Actor> actors;
    for (const std::string& tok : split(a.get("actors", "human"), ',')) {
        Actor ac;
        if (!makeActor(a, tok, seed, ac)) return 1;
        actors.push_back(std::move(ac));
    }
    const std::vector<std::string> script = split(a.get("script", "idle:1,attack,hit:front,cast,roar,death:front"), ',');
    const int fps = int(a.num("fps", 25));
    const float dt = 1.0f / float(fps);
    const int cw = int(a.num("cell", 300)), chh = int(a.num("cellh", cw * 4 / 3));
    const bool sameScale = a.flag("samescale");
    const float yawCam = a.flt("yaw", 0.75f);
    float maxH = 0;
    for (Actor& ac : actors) maxH = std::max(maxH, ac.ch.height * (ac.ch.rig.legs.size() > 2 ? 1.25f : 1.0f));
    for (size_t i = 0; i < actors.size(); ++i) {
        Actor& ac = actors[i];
        ac.clips = makeAuthoredClips(ac.ch);
        MotionSettings ms;
        ms.stiffness = a.flt("stiffness", 1.0f);
        ms.overlap = a.flt("overlap", 1.0f);
        ms.footIK = !a.flag("nofootik");
        ms.footLock = !a.flag("nofootlock");
        if (a.flag("hills")) ms.ground = [](float x, float z) { return 0.18f * std::sin(1.3f * x) * std::cos(0.9f * z) + 0.06f * z; };
        ac.anim = std::make_unique<Animator>(ac.ch, ms);
        ac.anim->setBase(ac.clip("idle"));
        ac.pos = {0, ms.ground ? ms.ground(0, 0) : 0.0f, 0};
        ac.anim->reset(ac.pos, Quat{});
    }
    std::vector<Image> frames;
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    auto render = [&](const std::string& caption) {
        Image img(cw * int(actors.size()), chh);
        for (size_t i = 0; i < actors.size(); ++i) {
            Actor& ac = actors[i];
            poseMatrices(ac.ch.skeleton, ac.anim->pose(), global, skin);
            skinMesh(ac.ch.mesh, skin, pos, nrm);
            const float H = sameScale ? maxH : ac.ch.height * (ac.ch.rig.legs.size() > 2 ? 1.25f : 1.0f);
            ToonRenderer r(cw, chh, 2, style(a));
            const Vec3 ctr = ac.anim->bodyCenter();
            const Vec3 target{ctr.x, ac.pos.y + H * 0.45f, ctr.z};
            r.setCamera(target + Vec3{std::sin(yawCam), 0.2f, std::cos(yawCam)} * (H * 2.5f), target, 0.5f);
            r.clearSky({0.93f, 0.93f, 0.95f}, {0.99f, 0.99f, 0.99f});
            r.drawFloor(40, 0.5f * std::max(1.0f, H / 1.8f), {0.90f, 0.90f, 0.91f}, {0.95f, 0.95f, 0.96f},
                        {{ctr.x, ctr.z, ac.ch.height * 0.2f}});
            const Mat4 model = Mat4::trs(ac.pos, Quat::axisAngle({0, 1, 0}, ac.yaw));
            r.drawCharacter(ac.ch, pos, nrm, model, ac.anim->face(), 10);
            r.drawText(ac.label, 8, 8, 2, {0.35f, 0.35f, 0.4f});
            if (i == 0 && !caption.empty()) r.drawText(caption, 8, chh - 24, 2, {0.55f, 0.2f, 0.2f});
            img.blit(r.finish(), int(i) * cw, 0);
        }
        frames.push_back(std::move(img));
    };
    auto stepAll = [&](float secs, const std::string& caption, float speed, float turn) {
        const int n = std::max(1, int(secs * fps + 0.5f));
        for (int f = 0; f < n; ++f) {
            for (Actor& ac : actors) {
                ac.yaw += turn * dt;
                const Vec3 fwd{std::sin(ac.yaw), 0, std::cos(ac.yaw)};
                if (speed != 0) {
                    const float v = speed * clipGroundSpeed(ac.ch, ac.anim->clip() ? *ac.anim->clip() : ac.clips[0]);
                    ac.pos += fwd * (v * dt);
                }
                if (ac.anim->settings.ground) ac.pos.y = ac.anim->settings.ground(ac.pos.x, ac.pos.z);
                ac.anim->update(dt, ac.pos, Quat::axisAngle({0, 1, 0}, ac.yaw));
            }
            render(caption);
        }
    };
    auto blowDir = [](const Actor& ac, const std::string& from) {
        Vec3 d{0, 0, -1};  // from the front: the blow travels backwards
        if (from == "back") d = {0, 0, 1};
        else if (from == "left") d = {-1, 0, 0};
        else if (from == "right") d = {1, 0, 0};
        return rotate(Quat::axisAngle({0, 1, 0}, ac.yaw), d);
    };
    for (const std::string& step : script) {
        const auto parts = split(step, ':');
        const std::string act = parts.empty() ? "idle" : parts[0];
        const std::string arg = parts.size() > 1 ? parts[1] : "";
        const float secs = arg.empty() ? -1.0f : std::strtof(arg.c_str(), nullptr);
        if (act == "idle") {
            for (Actor& ac : actors) ac.anim->play(ac.clip("idle"), 0.3f);
            stepAll(secs > 0 ? secs : 1.0f, "", 0, 0);
        } else if (act == "walk" || act == "run") {
            for (Actor& ac : actors) ac.anim->play(ac.clip(act), 0.3f);
            stepAll(secs > 0 ? secs : 2.0f, act, 1.0f, 0);
        } else if (act == "circle") {  // run in a circle: leaning into the turn
            for (Actor& ac : actors) ac.anim->play(ac.clip("run"), 0.3f);
            stepAll(secs > 0 ? secs : 3.0f, "run", 1.0f, 1.6f);
        } else if (act == "hit") {
            for (Actor& ac : actors) ac.anim->hit(blowDir(ac, arg.empty() ? "front" : arg), a.flt("strength", 1.0f));
            stepAll(0.9f, "hit", 0, 0);
        } else if (act == "combo") {  // three blows in quick succession from different sides
            for (const char* side : {"front", "left", "right"}) {
                for (Actor& ac : actors) ac.anim->hit(blowDir(ac, side), 0.8f);
                stepAll(0.35f, "hit", 0, 0);
            }
            stepAll(0.6f, "hit", 0, 0);
        } else if (act == "death" || act == "die") {
            for (Actor& ac : actors) ac.anim->die(blowDir(ac, arg.empty() ? "front" : arg), a.flt("strength", 1.0f));
            stepAll(secs > 0 ? secs : 3.0f, "death", 0, 0);
        } else {
            float longest = 0;
            for (Actor& ac : actors) {
                const Clip& k = ac.clip(act);
                ac.anim->play(k, 0.15f);
                longest = std::max(longest, k.duration());
            }
            stepAll(longest + (secs > 0 ? secs : 0.35f), act, 0, 0);
        }
    }
    const std::string out = a.get("out", "motion.gif");
    if (out.size() > 4 && out.substr(out.size() - 4) == ".png") {  // contact sheet of every Nth frame
        const int every = int(a.num("every", 6)), cols = int(a.num("cols", 8));
        const int count = (int(frames.size()) + every - 1) / every, rows = (count + cols - 1) / cols;
        const int fw = frames[0].width, fh = frames[0].height;
        Image sheet(fw * std::min(cols, count), fh * rows);
        for (int k = 0; k < count; ++k) sheet.blit(frames[size_t(k * every)], (k % cols) * fw, (k / cols) * fh);
        return writePng(out, sheet) ? (std::printf("%s (%d quadros)\n", out.c_str(), count), 0) : 1;
    }
    if (!writeGif(out, frames, 100 / fps)) return 1;
    std::printf("%s (%zu quadros, %.1f s)\n", out.c_str(), frames.size(), frames.size() * dt);
    return 0;
}

int cmdCorpse(const Args& a) {
    const float days = a.flt("days", 30.0f);
    float built = 0.0f;
    int rebuilds = 0;
    for (float d : {0.0f, 0.25f, 1.0f, 2.0f, 4.0f, 7.0f, 10.0f, 14.0f, 21.0f, 30.0f, 45.0f}) {
        const float dec = decayFromTime(d * 24.0f, days);
        const bool rb = needsRebuild(built, dec);
        if (rb) built = decayBucket(dec), ++rebuilds;
        std::printf("dia %5.2f: decay %.2f  %-10s%s\n", d, dec, std::string(toString(decayStage(dec))).c_str(),
                    rb ? "  (regerar)" : "");
    }
    std::printf("%d regeneracoes\n", rebuilds);
    return 0;
}

int cmdStress(const Args& a) {
    const int count = int(a.num("count", 1000));
    const uint64_t seed0 = uint64_t(a.num("seed", 1));
    int bad = 0;
    double total = 0, worst = 0;
    uint64_t worstSeed = 0;
    size_t maxTris = 0;
    double motionMs = 0;
    int motionRuns = 0;
    for (int i = 0; i < count; ++i) {
        const uint64_t seed = seed0 + uint64_t(i);
        GenOptions go;
        const Genome g = fuzzGenome(seed, &go);
        const auto t0 = std::chrono::steady_clock::now();
        const Character c = generateCharacter(g, go);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total += ms;
        if (ms > worst) worst = ms, worstSeed = seed;
        maxTris = std::max(maxTris, c.mesh.triangleCount());
        std::string why;
        bool ok = validateCharacter(c, &why);
        if (ok) {  // the engine utilities must cope with whatever comes out
            const BodyMetrics m = measure(c);
            const auto vols = hitVolumes(c);
            const PackedMesh pm = packMesh(c.mesh);
            if (!std::isfinite(m.mass) || m.mass <= 0) ok = false, why = "mass";
            else if (vols.size() < 10) ok = false, why = "few hit volumes";
            else if (pm.vertices.size() != c.mesh.vertices.size()) ok = false, why = "pack";
        }
        if (ok && i % int(a.num("motionevery", 10)) == 0) {  // physics animation: every action, then death to rest
            const auto t1 = std::chrono::steady_clock::now();
            const auto clips = makeAuthoredClips(c);
            Animator an(c);
            an.setBase(clips[0]);
            an.reset({}, Quat{});
            int updates = 0;
            auto finite = [&]() {
                for (size_t b = 0; b < an.pose().rot.size(); ++b) {
                    const Quat q = an.pose().rot[b];
                    const Vec3 t = an.pose().trans[b];
                    if (!std::isfinite(q.x + q.y + q.z + q.w + t.x + t.y + t.z)) return false;
                }
                return true;
            };
            for (const Clip& k : clips) {
                if (k.name == "death") continue;
                an.play(k, 0.15f);
                for (int f = 0; f < 30 && ok; ++f, ++updates) an.update(1.0f / 30.0f, {}, Quat{}), ok = finite();
                if (k.name == "hit") an.hit({0, 0, -1}, 1.5f);
            }
            an.die({1, 0, 0});
            for (int f = 0; f < 300 && ok && !an.settled(); ++f, ++updates) an.update(1.0f / 30.0f, {}, Quat{}), ok = finite();
            if (!ok) why = "motion NaN";
            else if (!an.settled()) ok = false, why = "ragdoll never settled";
            else if (an.bodyCenter().y > 0.5f * c.height || an.bodyCenter().y < 0) ok = false, why = "ragdoll off the ground";
            motionMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / std::max(1, updates);
            ++motionRuns;
        }
        if (!ok) {
            ++bad;
            if (bad <= 20)
                std::printf("FALHA seed %llu (%s, decay %.2f, lod %d): %s\n", (unsigned long long)seed, describe(g).c_str(), go.decay,
                            go.lod, why.c_str());
        }
    }
    std::printf("%d/%d validos; %.1f ms em media, pior %.1f ms (seed %llu); max %zu tris\n", count - bad, count, total / count, worst,
                (unsigned long long)worstSeed, maxTris);
    if (motionRuns)
        std::printf("animacao com fisica: %d personagens, %.3f ms por quadro (30 fps) em media\n", motionRuns, motionMs / motionRuns);
    return bad ? 1 : 0;
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
    if (a.cmd == "info") return cmdInfo(a);
    if (a.cmd == "family") return cmdFamily(a);
    if (a.cmd == "corpse") return cmdCorpse(a);
    if (a.cmd == "stress") return cmdStress(a);
    if (a.cmd == "motion") return cmdMotion(a);
    usage();
    return a.cmd.empty() ? 0 : 1;
}
