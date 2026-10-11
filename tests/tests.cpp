// Sanity tests: determinism, mesh validity, skinning, winding, export.
#include <cmath>
#include <cstdio>

#include "chargen/animation.hpp"
#include "chargen/character.hpp"
#include "chargen/gltf.hpp"

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
    if (failures) std::printf("%d falhas\n", failures);
    else std::printf("ok\n");
    return failures ? 1 : 0;
}
