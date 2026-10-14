// Sanity tests: determinism, mesh validity, skinning, winding, export.
#include <cmath>
#include <cstdio>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/engine.hpp"
#include "chargen/gltf.hpp"
#include "chargen/studies.hpp"

using namespace cg;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            ++failures;                                     \
            std::printf("FALHA %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                       \
            std::printf("\n");                              \
        }                                                   \
    } while (0)

static void validate(const Character& c, uint64_t seed) {
    const Mesh& m = c.mesh;
    CHECK(!m.vertices.empty() && m.indices.size() % 3 == 0, "malha vazia seed %llu", (unsigned long long)seed);
    for (uint32_t i : m.indices) CHECK(i < m.vertices.size(), "indice fora seed %llu", (unsigned long long)seed);
    for (const Vertex& v : m.vertices) {
        float s = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
        CHECK(std::fabs(s - 1) < 1e-3f, "pesos somam %f seed %llu", s, (unsigned long long)seed);
        for (int k = 0; k < 4; ++k) CHECK(v.joints[k] < c.skeleton.bones.size(), "osso invalido");
        CHECK(std::isfinite(v.pos.x + v.pos.y + v.pos.z + v.normal.x + v.normal.y + v.normal.z), "NaN seed %llu",
              (unsigned long long)seed);
        if (std::fabs(length(v.normal) - 1) > 1e-2f) {
            CHECK(false, "normal nao unitaria seed %llu", (unsigned long long)seed);
            break;
        }
    }
    // Outward winding: on the body submesh, most face normals should agree
    // with the stored vertex normals.
    size_t agree = 0, total = 0;
    const Submesh& body = m.submeshes[0];
    for (uint32_t i = body.firstIndex; i < body.firstIndex + body.indexCount; i += 3) {
        const Vertex &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &cc = m.vertices[m.indices[i + 2]];
        Vec3 fn = cross(b.pos - a.pos, cc.pos - a.pos);
        if (length(fn) < 1e-12f) continue;
        ++total;
        if (dot(fn, a.normal + b.normal + cc.normal) > 0) ++agree;
    }
    CHECK(total && agree > total * 97 / 100, "winding inconsistente %zu/%zu seed %llu", agree, total, (unsigned long long)seed);
    // Feet on the floor, head at the right height.
    float minY = 1e9f, maxY = -1e9f;
    for (const Vertex& v : m.vertices) {
        minY = std::min(minY, v.pos.y);
        maxY = std::max(maxY, v.pos.y);
    }
    CHECK(minY > -0.02f && minY < 0.03f, "pes fora do chao (%f) seed %llu", minY, (unsigned long long)seed);
    CHECK(maxY > c.height * 0.95f && maxY < c.height * 1.6f, "altura estranha %f vs %f seed %llu", maxY, c.height,
          (unsigned long long)seed);
}

int main() {
    // Determinism.
    {
        Character a = generateCharacter(randomGenome(42)), b = generateCharacter(randomGenome(42));
        CHECK(a.mesh.vertices.size() == b.mesh.vertices.size() && a.mesh.indices == b.mesh.indices, "nao deterministico");
    }
    for (uint64_t s = 1; s <= 120; ++s) {
        GenomeOptions o;
        o.exaggeration = (s % 3) * 0.6f;
        Character c = generateCharacter(randomGenome(s, o));
        validate(c, s);
        // Rest pose skinning must reproduce the bind pose.
        std::vector<Mat4> global, skin;
        std::vector<Vec3> pos, nrm;
        poseMatrices(c.skeleton, restPose(c.skeleton), global, skin);
        skinMesh(c.mesh, skin, pos, nrm);
        float err = 0;
        for (size_t i = 0; i < pos.size(); ++i) err = std::max(err, length(pos[i] - c.mesh.vertices[i].pos));
        CHECK(err < 1e-4f, "bind pose drift %f", err);
        if (s % 20 == 0)
            for (const Clip& clip : makeDefaultClips(c)) {
                Pose p = sampleClip(clip, clip.duration() * 0.37f);
                poseMatrices(c.skeleton, p, global, skin);
                skinMesh(c.mesh, skin, pos, nrm);
                for (const Vec3& v : pos) CHECK(std::isfinite(v.x + v.y + v.z), "NaN anim %s", clip.name.c_str());
            }
    }
    for (int lod = 1; lod <= 2; ++lod) {
        GenOptions o;
        o.lod = lod;
        Character c = generateCharacter(randomGenome(7), o);
        validate(c, 7);
    }
    {
        Character c = generateCharacter(randomGenome(3));
        CHECK(exportGlb(c, makeDefaultClips(c), "test_export.glb"), "export falhou");
    }
    // Tails, dorsal spines and tusk curves: every variant builds a valid mesh, also as bones.
    for (int t = 0; t <= 5; ++t)
        for (float dec : {0.0f, 1.0f}) {
            GenomeOptions o;
            o.species = Species::Orc;
            Genome g = randomGenome(50 + uint64_t(t), o);
            g.body.tail = true;
            g.body.tailType = t;
            g.body.spines = 5 + t;
            g.body.spineKind = t % 3;
            g.body.tuskCurve = t % 2 ? 1.0f : -1.0f;
            GenOptions go;
            go.decay = dec;
            validate(generateCharacter(g, go), 50 + uint64_t(t));
        }
    // ---- engine utilities ----
    {
        CharacterKey k;
        k.seed = 0xDEADBEEFCAFE1234ull;
        k.options.species = Species::Troll;
        k.options.sex = Sex::Male;
        k.options.armPairs = 2;
        k.decay = 0.4f;
        k.lod = 1;
        const std::string txt = encodeKey(k);
        auto back = decodeKey(txt);
        CHECK(back && back->seed == k.seed && back->options.species == Species::Troll && back->options.sex == Sex::Male &&
                  back->options.armPairs == 2 && !back->options.age && std::fabs(back->decay - 0.4f) < 0.01f && back->lod == 1,
              "chave nao ida-e-volta: %s", txt.c_str());
        std::string bad = txt;
        bad[6] = bad[6] == 'A' ? 'B' : 'A';
        CHECK(!decodeKey(bad), "chave corrompida aceita");
        const Genome g = genomeFromKey(k);
        auto bytes = saveGenome(g);
        auto g2 = loadGenome(bytes);
        CHECK(g2 && g2->seed == g.seed && g2->body.height == g.body.height && g2->palette.skin.x == g.palette.skin.x, "genoma salvo difere");
        auto b32 = fromBase32(toBase32(bytes));
        CHECK(b32 && *b32 == bytes, "base32 nao ida-e-volta");
        bytes[20] ^= 0x5A;
        CHECK(!loadGenome(bytes), "genoma corrompido aceito");
    }
    {
        Character c = generateCharacter(randomGenome(11));
        const BodyMetrics m = measure(c);
        CHECK(m.eyeHeight > 0.85f * m.height && m.eyeHeight < m.height, "altura dos olhos %f de %f", m.eyeHeight, m.height);
        CHECK(m.mass > 35 && m.mass < 160, "massa estranha %f", m.mass);
        CHECK(m.reach > 0.25f * m.height && m.reach < 0.55f * m.height, "alcance %f", m.reach);
        const auto vols = hitVolumes(c);
        CHECK(vols.size() >= 17, "poucos hit volumes %zu", vols.size());
        std::vector<Mat4> global, skin;
        poseMatrices(c.skeleton, restPose(c.skeleton), global, skin);
        // A shot from the front at eye level hits the head; one at the knees hits a leg.
        RayHit h = raycast(vols, global, {0, m.eyeHeight, 3.0f}, {0, 0, -1});
        CHECK(h.volume >= 0 && vols[size_t(h.volume)].zone == HitZone::Head, "tiro na cabeca acertou %d", h.volume);
        const Vec3 knee = c.skeleton.bones[size_t(c.rig.legs[0].lower)].restWorld;
        h = raycast(vols, global, {knee.x, knee.y + 0.1f, 3.0f}, {0, 0, -1});
        CHECK(h.volume >= 0 && (vols[size_t(h.volume)].zone == HitZone::Shin || vols[size_t(h.volume)].zone == HitZone::Thigh),
              "tiro no joelho acertou %d", h.volume);
        CHECK(raycast(vols, global, {3, m.eyeHeight, 3}, {0, 0, -1}).volume < 0, "tiro no vazio acertou");
        CHECK(!overlapSphere(vols, global, {0, 0.6f * m.height, 0}, 0.1f).empty(), "esfera no torso nao acertou");
        const Mat4 hand = socketMatrix(c, global, Socket::HandR);
        CHECK(length(Vec3{hand.m[12], hand.m[13], hand.m[14]}) > 0.1f, "socket da mao na origem");
        const PackedMesh pm = packMesh(c.mesh);
        CHECK(pm.vertices.size() == c.mesh.vertices.size() && !pm.wide() && pm.indices16.size() == c.mesh.indices.size(), "pack");
        for (const PackedVertex& v : pm.vertices) {
            const int sw = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
            if (sw != 255) {
                CHECK(false, "pesos empacotados somam %d", sw);
                break;
            }
        }
    }
    {
        float prev = 0;
        for (float hrs = 0; hrs < 24 * 60; hrs += 6) {
            const float d = decayFromTime(hrs);
            CHECK(d >= prev && d <= 1.0f, "decay nao monotono");
            CHECK(decayBucket(d) >= (d > 0 ? 0.0f : 0.0f) && (d <= 0 || decayBucket(d) > 0), "bucket voltou a vivo");
            CHECK((decayStage(d) == DecayStage::Skeletal) == (decayBucket(d) >= 0.97f), "bucket cruzou o estagio");
            prev = d;
        }
        CHECK(decayStage(decayFromTime(24 * 31)) == DecayStage::Skeletal, "nao virou ossada");
        CHECK(decayStage(decayFromTime(12)) == DecayStage::Fresh, "meio dia nao e fresco");
        CHECK(needsRebuild(0.0f, 0.1f) && !needsRebuild(0.41f, 0.42f), "needsRebuild");
        CHECK(pickLod(400) == 0 && pickLod(100) == 1 && pickLod(20) == 2 && pickLod(150, 0) == 0 && pickLod(150, 1) == 1, "pickLod");
        CHECK(std::fabs(screenHeightPx(1.8f, 10.0f, 1.0f, 1080) - 1.8f / (20.0f * std::tan(0.5f)) * 1080) < 1e-2f, "screenHeight");
    }
    {
        PopulationSpec ps;
        ps.species = {{Species::Human, 3}, {Species::Dwarf, 1}, {Species::Orc, 1}};
        ps.fourArmShare = 0.1f;
        auto keys = planPopulation(9, 200, ps);
        int dw = 0, fem = 0;
        for (auto& k : keys) dw += k.options.species == Species::Dwarf, fem += k.options.sex == Sex::Female;
        CHECK(keys.size() == 200 && dw > 20 && dw < 70 && fem > 70 && fem < 130, "populacao %d anoes %d mulheres", dw, fem);
        GenomeOptions o;
        o.sex = Sex::Female;
        const Genome mo = randomGenome(100, o);
        o.sex = Sex::Male;
        const Genome fa = randomGenome(200, o);
        const Genome c1 = childOf(mo, fa, 5), c2 = childOf(mo, fa, 5), c3 = childOf(mo, fa, 6);
        CHECK(c1.face.noseLength == c2.face.noseLength && c1.palette.skin.x == c2.palette.skin.x, "filho nao deterministico");
        CHECK(c1.face.noseLength != c3.face.noseLength || c1.palette.skin.x != c3.palette.skin.x, "irmaos identicos");
        const float lo = std::min(mo.palette.skin.x, fa.palette.skin.x) - 0.15f, hi = std::max(mo.palette.skin.x, fa.palette.skin.x) + 0.15f;
        CHECK(c1.palette.skin.x >= lo && c1.palette.skin.x <= hi, "pele do filho fora dos pais");
        validate(generateCharacter(c1), 5);
        const Genome old = aged(fa, 80.0f);
        CHECK(old.age == 80.0f && old.ageGroup == AgeGroup::Elder && luminance(old.palette.hair) > luminance(fa.palette.hair) - 1e-4f &&
                  old.face.wrinkles >= fa.face.wrinkles && old.body.height <= fa.body.height,
              "envelhecer");
        validate(generateCharacter(old), 200);
        const PhysicalTraits tOld = physicalTraits(old), tYoung = physicalTraits(fa);
        CHECK(tOld.vigor < tYoung.vigor && tOld.strength <= tYoung.strength + 1e-4f, "tracos com idade");
        GenomeOptions og;
        og.species = Species::Ogre;
        GenomeOptions on;
        on.species = Species::Gnome;
        CHECK(physicalTraits(randomGenome(1, og)).size > physicalTraits(randomGenome(1, on)).size, "ogro menor que gnomo");
        CHECK(!describe(fa).empty(), "descricao vazia");
    }
    // Studies and fuzzing: hand-made extremes and randomised genomes far outside the
    // population must still build valid characters the engine utilities can digest.
    for (const Study& st : studies()) {
        GenOptions go;
        auto g = studyGenome(st.name, 3, &go);
        CHECK(g.has_value(), "estudo %s", std::string(st.name).c_str());
        if (!g) continue;
        const Character c = generateCharacter(*g, go);
        std::string why;
        CHECK(validateCharacter(c, &why), "estudo %s invalido: %s", std::string(st.name).c_str(), why.c_str());
        CHECK(hitVolumes(c).size() >= 10, "estudo %s sem hitboxes", std::string(st.name).c_str());
    }
    {
        GenOptions go;
        const Character c = generateCharacter(*studyGenome("four_armed_giant", 1, &go), go);
        CHECK(c.rig.arms.size() == 4 && c.genome.face.eyePairs == 2, "gigante sem 4 bracos/olhos");
        int glow = 0, metal = 0;
        const Character a = generateCharacter(*studyGenome("android_monk", 1));
        for (const Vertex& v : a.mesh.vertices) glow += v.color[3] / 32 == int(MaterialClass::Glow), metal += v.color[3] / 32 == int(MaterialClass::Metal);
        CHECK(glow > 0 && metal > int(a.mesh.vertices.size()) / 3, "androide: nucleo %d / metal %d", glow, metal);
    }
    {   // Constructs do not rot: rust (matte patches), dead core, an iron frame at the end.
        GenOptions go;
        go.decay = 0.7f;
        const Character c = generateCharacter(*studyGenome("android_monk", 1), go);
        int rust = 0;
        for (const Vertex& v : c.mesh.vertices) rust += v.color[3] / 32 == int(MaterialClass::Leather);
        CHECK(rust > 200 && !c.genome.face.glowEyes, "constructo sem ferrugem (%d)", rust);
        go.decay = 1.0f;
        const Character b = generateCharacter(*studyGenome("android_monk", 1), go);
        int metal = 0, horn = 0;
        for (const Vertex& v : b.mesh.vertices) metal += v.color[3] / 32 == int(MaterialClass::Metal), horn += v.color[3] / 32 == int(MaterialClass::Horn);
        CHECK(metal > horn, "esqueleto do constructo nao e de ferro (%d metal, %d osso)", metal, horn);
    }
    {   // Corpses only keep the bones a hole can show.
        GenOptions a, b;
        a.decay = 0.5f;
        b.decay = 1.0f;
        const Character half = generateCharacter(randomGenome(4), a), bones = generateCharacter(randomGenome(4), b);
        CHECK(half.mesh.triangleCount() < generateCharacter(randomGenome(4)).mesh.triangleCount() + bones.mesh.triangleCount(),
              "cadaver sem poda de ossos: %zu", half.mesh.triangleCount());
    }
    for (uint64_t s = 1; s <= 80; ++s) {
        GenOptions go;
        const Genome g = fuzzGenome(s * 7919, &go);
        const Character c = generateCharacter(g, go);
        std::string why;
        CHECK(validateCharacter(c, &why), "fuzz seed %llu: %s", (unsigned long long)(s * 7919), why.c_str());
    }
    if (failures) std::printf("%d falhas\n", failures);
    else std::printf("ok\n");
    return failures ? 1 : 0;
}
