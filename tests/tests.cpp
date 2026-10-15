// Sanity tests: determinism, mesh validity, skinning, winding, export.
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <cstdio>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/engine.hpp"
#include "chargen/gltf.hpp"
#include "chargen/motion.hpp"
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

static bool finitePose(const Pose& p) {
    for (size_t i = 0; i < p.rot.size(); ++i)
        if (!std::isfinite(p.rot[i].x + p.rot[i].y + p.rot[i].z + p.rot[i].w + p.trans[i].x + p.trans[i].y + p.trans[i].z)) return false;
    return true;
}

// Drives the physics animator through every action and checks it stays sane: finite poses,
// planted feet, the gameplay events, a ragdoll that falls, stays on the ground and comes to rest.
static void checkMotion(const Character& c, const char* name) {
    const auto clips = makeAuthoredClips(c);
    auto clip = [&](const char* n) -> const Clip& {
        for (const Clip& k : clips)
            if (k.name == n) return k;
        return clips[0];
    };
    Animator an(c);
    an.setBase(clip("idle"));
    Vec3 pos{};
    an.reset(pos, Quat{});
    const float dt = 1.0f / 30.0f;
    bool ok = true, contact = false, release = false, rag = false;
    auto run = [&](float secs, float speed) {
        for (int f = 0; f < int(secs / dt); ++f) {
            pos.z += speed * dt;
            an.update(dt, pos, Quat{});
            ok = ok && finitePose(an.pose());
            for (const std::string& e : an.events()) contact |= e == "contact", release |= e == "release", rag |= e == "ragdoll";
        }
    };
    run(0.5f, 0);
    // Idle: feet planted at their rest height.
    std::vector<Mat4> g, sk;
    poseMatrices(c.skeleton, an.pose(), g, sk);
    for (const LimbRig& l : c.rig.legs) {
        const float y = g[size_t(l.end)].point({0, 0, 0}).y, rest = c.skeleton.bones[size_t(l.end)].restWorld.y;
        CHECK(std::fabs(y - rest) < 0.03f * c.height, "%s: pe fora do chao em idle (%f vs %f)", name, y, rest);
    }
    // A naga's coil lies on the ground.
    if (!c.rig.serpent.empty() && c.skeleton.bones[size_t(c.rig.serpent[0])].name.rfind("coil", 0) == 0)
        for (int b : c.rig.serpent) {
            const float y = g[size_t(b)].point({0, 0, 0}).y, rest = c.skeleton.bones[size_t(b)].restWorld.y;
            CHECK(std::fabs(y - rest) < 0.06f * c.height, "%s: serpente fora do chao (%f vs %f)", name, y, rest);
        }
    an.play(clip("walk"), 0.2f);
    run(1.5f, clipGroundSpeed(c, clip("walk")));
    for (const char* act : {"attack", "cast", "roar", "dodge"}) {
        an.play(clip(act), 0.15f);
        run(clip(act).duration() + 0.3f, 0);
    }
    an.hit({0, 0, -1}, 1.0f);
    run(0.4f, 0);
    an.hit({1, 0, 0}, 1.5f, Vec3{pos.x, 0.8f * c.height, pos.z});
    run(0.8f, 0);
    CHECK(ok, "%s: pose com NaN", name);
    CHECK(contact && release, "%s: eventos contact/release ausentes", name);
    const Vec3 before = an.bodyCenter();
    an.die({0, 0, -1});
    run(8.0f, 0);
    const Vec3 after = an.bodyCenter();
    CHECK(ok && rag && an.isRagdoll(), "%s: ragdoll nao comecou", name);
    CHECK(an.settled(), "%s: ragdoll nao parou", name);
    CHECK(after.y > 0 && after.y < 0.45f * c.height, "%s: corpo nao caiu (%f de %f)", name, after.y, c.height);
    CHECK(std::hypot(after.x - before.x, after.z - before.z) < 1.2f * c.height, "%s: corpo deslizou %f", name,
          std::hypot(after.x - before.x, after.z - before.z));
    poseMatrices(c.skeleton, an.pose(), g, sk);
    float low = 1e9f;
    for (const Mat4& m : g) low = std::min(low, m.point({0, 0, 0}).y);
    CHECK(low > -0.05f * c.height, "%s: osso abaixo do chao %f", name, low);
}

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
    // Trait peoples: every one, both sexes, alive, rotting and as bones, at every LOD; the
    // trait rigs stay in the bind pose and animate without NaNs.
    for (int sp = int(kFirstTraitSpecies); sp < int(Species::Count); ++sp)
        for (int sx = 0; sx < 2; ++sx) {
            GenomeOptions o;
            o.species = Species(sp);
            o.sex = sx ? Sex::Female : Sex::Male;
            const uint64_t seed = 700 + uint64_t(sp) * 3 + uint64_t(sx);
            const Genome g = randomGenome(seed, o);
            for (float dec : {0.0f, 0.5f, 1.0f}) {
                GenOptions go;
                go.decay = dec;
                go.lod = sx + int(dec * 1.5f) > 2 ? 2 : sx + int(dec * 1.5f);
                validate(generateCharacter(g, go), seed);
            }
            Character c = generateCharacter(g);
            std::vector<Mat4> global, skin;
            std::vector<Vec3> pos, nrm;
            poseMatrices(c.skeleton, restPose(c.skeleton), global, skin);
            skinMesh(c.mesh, skin, pos, nrm);
            float err = 0;
            for (size_t i = 0; i < pos.size(); ++i) err = std::max(err, length(pos[i] - c.mesh.vertices[i].pos));
            CHECK(err < 1e-4f, "bind pose drift %f (%s)", err, std::string(toString(Species(sp))).c_str());
            for (const Clip& clip : makeDefaultClips(c)) {
                poseMatrices(c.skeleton, sampleClip(clip, clip.duration() * 0.41f), global, skin);
                skinMesh(c.mesh, skin, pos, nrm);
                bool finite = true;
                for (const Vec3& v : pos) finite &= std::isfinite(v.x + v.y + v.z);
                CHECK(finite, "NaN anim %s (%s)", clip.name.c_str(), std::string(toString(Species(sp))).c_str());
            }
            CHECK(speciesFromString(toString(Species(sp))) == Species(sp), "nome do povo nao ida-e-volta");
        }
    // Each trait on a plain human, alone: wings (and folded), coils, hands, feet, beaks, crests, skins.
    for (int k = 0; k < 32; ++k) {
        Genome g = randomGenome(900 + uint64_t(k));
        BodyShape& b = g.body;
        switch (k) {
            case 0: case 1: case 2: b.wings = k + 1; break;
            case 3: b.wings = 1, b.wingsFolded = true; break;
            case 4: b.wings = 2, b.wingsFolded = true; break;
            case 5: case 6: b.lowerBody = k - 4; break;
            case 7: case 8: case 9: b.hands = k - 6; break;
            case 10: b.feet = 1; break;
            case 11: b.feet = 1, b.digitigrade = true; break;
            case 12: case 13: case 14: g.face.beak = k - 11; break;
            case 15: case 16: case 17: case 18: b.crest = k - 14; break;
            case 19: b.backTentacles = 6; break;
            case 20: b.shell = true; break;
            case 21: b.skinKind = 4, b.shards = 12; break;
            case 22: b.skinKind = 5; break;
            case 24: b.lowerBody = 3; break;
            case 25: b.hands = 4; break;
            case 26: g.face.eyeKind = 2, g.face.eyePairs = 1; break;
            case 27: g.face.eyeKind = 3, g.face.eyePairs = 1; break;
            case 28: g.face.maw = true; break;
            case 29: b.bodyEyes = 20; break;
            case 30: b.stance = 1, b.digitigrade = true; break;
            case 31: b.orb = true; break;
            default: g.face.mandibles = true, g.face.antennae = 2, g.face.eyeKind = 1, b.ears = EarStyle::Fin; break;
        }
        const Character c = generateCharacter(g);
        validate(c, 900 + uint64_t(k));
        if (k <= 4) CHECK(c.rig.wings.size() == 2, "asas sem rig");
        if (k == 5 || k == 6) CHECK(!c.rig.serpent.empty() && c.rig.legs.size() == 2, "corpo de serpente sem rig");
        if (k == 8) CHECK(c.rig.pincers.size() == 2 && c.rig.pincers[0] >= 0, "pinca sem osso");
        if (k == 19) CHECK(c.rig.tentacles.size() == 6, "tentaculos sem rig");
        if (k == 24) CHECK(c.rig.serpent.size() == 3, "corpo de gosma sem rig");
        if (k == 30) {  // on all fours: four legs, the trunk level along z, the head in front
            const auto& bn = c.skeleton.bones;
            auto P = [&](int b) { return bn[size_t(b)].restWorld; };
            CHECK(c.rig.legs.size() == 4, "besta sem quatro patas (%zu)", c.rig.legs.size());
            const float fore = P(c.rig.legs[0].tip).y, hind = P(c.rig.legs[2].tip).y;
            const float chestZ = P(c.rig.chest).z, hipZ = P(c.rig.hips).z, headZ = P(c.rig.head).z;
            CHECK(fore < 0.1f * c.height && hind < 0.1f * c.height, "patas fora do chao (%f %f)", fore, hind);
            CHECK(chestZ > hipZ + 0.3f && headZ > chestZ && std::fabs(P(c.rig.chest).y - P(c.rig.hips).y) < 0.25f * c.height,
                  "besta nao ficou de quatro (peito %f quadril %f cabeca %f)", chestZ, hipZ, headZ);
        }
        if (k >= 24 && k < 30) {  // the new traits add geometry to a plain human
            Genome plain = g;
            plain.body.lowerBody = 0, plain.body.hands = 0, plain.face.eyeKind = 0, plain.face.maw = false, plain.body.bodyEyes = 0;
            CHECK(generateCharacter(plain).mesh.triangleCount() != c.mesh.triangleCount(), "traco %d sem efeito", k);
        }
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
        // A snapshot saved before HeadForm existed (the same bytes up to it) still loads,
        // with the plain human skull it was drawn with.
        {
            const uint32_t n = uint32_t(offsetof(Genome, head));
            std::vector<uint8_t> old(bytes.begin(), bytes.begin() + 4);
            for (int i = 0; i < 4; ++i) old.push_back(uint8_t(n >> (8 * i)));
            const auto* raw = reinterpret_cast<const uint8_t*>(&g);
            old.insert(old.end(), raw, raw + n);
            uint32_t h = 2166136261u;
            for (uint32_t i = 0; i < n; ++i) h = (h ^ raw[i]) * 16777619u;
            for (int i = 0; i < 4; ++i) old.push_back(uint8_t(h >> (8 * i)));
            auto g3 = loadGenome(old);
            CHECK(g3 && g3->seed == g.seed && g3->body.height == g.body.height && g3->head.forehead == 0.0f &&
                      g3->head.eyeLook == EyeLook::Classic, "genoma antigo (sem HeadForm) nao carregou");
        }
    }
    // Head silhouettes: the peoples the critique found sharing one face (troll, undead,
    // night elf, high elf) now differ in the shadow of the skull, and every pair of them
    // is clearly further apart than with the plain human skull.
    {
        const Species ps[] = {Species::Troll, Species::Undead, Species::NightElf, Species::HighElf, Species::Orc, Species::Ogre};
        const int np = int(std::size(ps)), ns = 3;
        std::vector<HeadSilhouette> cur, plain;
        for (Species sp : ps)
            for (int i = 0; i < ns; ++i) {
                GenomeOptions o;
                o.species = sp;
                o.sex = i % 2 ? Sex::Female : Sex::Male;
                o.age = AgeGroup::Adult;
                Genome g = randomGenome(uint64_t(500 + 31 * i), o);
                g.hair = HairStyle::Bald;
                g.facialHair = FacialHair::None;
                GenOptions go;
                go.lod = 2;
                cur.push_back(headSilhouette(generateCharacter(g, go)));
                g.head = HeadForm{};
                plain.push_back(headSilhouette(generateCharacter(g, go)));
            }
        auto sim = [&](const std::vector<HeadSilhouette>& v, int a, int b) {
            float t = 0;
            for (int i = 0; i < ns; ++i)
                for (int j = 0; j < ns; ++j) t += silhouetteSimilarity(v[size_t(a * ns + i)], v[size_t(b * ns + j)]);
            return t / float(ns * ns);
        };
        for (int a = 0; a < np; ++a)
            for (int b = a + 1; b < np; ++b) {
                const float c = sim(cur, a, b), p = sim(plain, a, b);
                CHECK(c < 0.92f && c < p - 0.02f, "cabecas de %s e %s parecidas demais: %.2f (cranio humano %.2f)",
                      std::string(toString(ps[a])).c_str(), std::string(toString(ps[b])).c_str(), c, p);
            }
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
    {   // A ruined construct loses a forearm: one hand has (almost) nothing skinned to it.
        GenOptions go;
        go.decay = 0.7f;
        const Character c = generateCharacter(*studyGenome("android_monk", 1), go);
        int hands[2] = {0, 0};
        for (const Vertex& v : c.mesh.vertices) {
            int best = 0;
            for (int k = 1; k < 4; ++k)
                if (v.weights[k] > v.weights[best]) best = k;
            for (int a = 0; a < 2; ++a) hands[a] += v.joints[best] == c.rig.arms[size_t(a)].end;
        }
        CHECK(std::min(hands[0], hands[1]) * 10 < std::max(hands[0], hands[1]), "constructo sem antebraco arrancado (%d / %d)", hands[0], hands[1]);
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
    // ---- physics animation ----
    {
        GenomeOptions o;
        o.species = Species::Human;
        checkMotion(generateCharacter(randomGenome(4, o)), "humano");
        o.species = Species::Worgen;  // digitigrade legs, tail
        checkMotion(generateCharacter(randomGenome(5, o)), "worgen");
        o.species = Species::Centaur;  // four legs
        checkMotion(generateCharacter(randomGenome(6, o)), "centauro");
        o.species = Species::Gnome;
        o.armPairs = 2;  // four arms, small body
        checkMotion(generateCharacter(randomGenome(7, o)), "gnomo 4 bracos");
        // Species traits: a coil instead of legs, a wisp, wings (membrane and feathered), a beast on all fours.
        for (Species sp : {Species::Naga, Species::Wraith, Species::Avian, Species::Demon, Species::Dragonborn, Species::Cephalid,
                        Species::Gloomhound}) {
            GenomeOptions so;
            so.species = sp;
            for (uint64_t seed : {3, 4}) {
                const Character c = generateCharacter(randomGenome(seed, so));
                char name[64];
                std::snprintf(name, sizeof name, "%.*s %llu", int(toString(sp).size()), toString(sp).data(), (unsigned long long)seed);
                checkMotion(c, name);
            }
        }
        for (const char* st : {"four_armed_giant", "centaur_warlord", "chitin_horror"}) {
            GenOptions go;
            checkMotion(generateCharacter(*studyGenome(st, 2, &go), go), st);
        }
        for (uint64_t s = 1; s <= 6; ++s) {
            GenOptions go;
            const Character c = generateCharacter(fuzzGenome(s * 104729, &go), go);
            char name[96];
            std::snprintf(name, sizeof name, "fuzz %llu (asas %zu, serpente %zu, pernas %zu)", (unsigned long long)s,
                          c.rig.wings.size(), c.rig.serpent.size(), c.rig.legs.size());
            checkMotion(c, name);
        }
    }
    {   // IK reaches, baked clips stay valid and loops close.
        const Character c = generateCharacter(randomGenome(9));
        Pose p = restPose(c.skeleton);
        const LimbRig& arm = c.rig.arms[1];
        const Vec3 goal = c.skeleton.bones[size_t(arm.upper)].restWorld + Vec3{0.1f, 0.15f, 0.3f} * (c.height / 1.8f);
        twoBoneIK(c.skeleton, p, arm.upper, arm.lower, arm.end, goal, {0, 0, -1});
        std::vector<Mat4> g, sk;
        poseMatrices(c.skeleton, p, g, sk);
        CHECK(length(g[size_t(arm.end)].point({0, 0, 0}) - goal) < 0.01f, "IK nao alcancou");
        const auto clips = makeDefaultClips(c);
        CHECK(clips.size() == 9, "clipes padrao: %zu", clips.size());
        for (const Clip& k : clips) {
            bool fin = true;
            for (const Pose& f : k.frames) fin = fin && finitePose(f);
            CHECK(fin && !k.frames.empty(), "clipe %s invalido", k.name.c_str());
        }
        const Clip& walk = clips[1];
        float seam = 0;
        for (size_t i = 0; i < walk.frames[0].rot.size(); ++i) {
            const Quat a = walk.frames.back().rot[i], b = walk.frames[0].rot[i], m = walk.frames[walk.frames.size() - 2].rot[i];
            // The step over the seam should look like any other step.
            const float over = 1 - std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
            const float inner = 1 - std::fabs(a.x * m.x + a.y * m.y + a.z * m.z + a.w * m.w);
            seam = std::max(seam, over - 4 * inner);
        }
        CHECK(seam < 1e-3f, "loop do walk com emenda %f", seam);
        CHECK(exportGlb(c, clips, "test_motion.glb"), "export com clipes fisicos falhou");
        Image a(8, 8), b(8, 8);
        a.set(1, 1, {1, 0, 0, 1});
        const auto gif = encodeGif({a, b}, 4);
        CHECK(gif.size() > 30 && gif[0] == 'G' && gif.back() == 0x3B, "gif");
    }
    if (failures) std::printf("%d falhas\n", failures);
    else std::printf("ok\n");
    return failures ? 1 : 0;
}
