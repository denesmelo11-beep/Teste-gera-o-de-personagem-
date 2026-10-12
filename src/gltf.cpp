#include "chargen/gltf.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>

namespace cg {

namespace {

struct Bin {
    std::vector<uint8_t> data;
    size_t add(const void* p, size_t n) {
        while (data.size() % 4) data.push_back(0);
        size_t off = data.size();
        data.insert(data.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
        return off;
    }
};

std::string f2s(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.7g", double(std::isfinite(v) ? v : 0.0f));
    return b;
}

std::string jstr(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

struct Json {
    std::vector<std::string> bufferViews, accessors;
    int view(size_t off, size_t len, int target = 0) {
        std::string s = "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) + ",\"byteLength\":" + std::to_string(len);
        if (target) s += ",\"target\":" + std::to_string(target);
        bufferViews.push_back(s + "}");
        return int(bufferViews.size()) - 1;
    }
    int accessor(int bv, size_t byteOffset, int comp, size_t count, const char* type, bool normalized = false,
                 const std::string& minmax = "") {
        std::string s = "{\"bufferView\":" + std::to_string(bv) + ",\"byteOffset\":" + std::to_string(byteOffset) +
                        ",\"componentType\":" + std::to_string(comp) + ",\"count\":" + std::to_string(count) +
                        ",\"type\":\"" + type + "\"";
        if (normalized) s += ",\"normalized\":true";
        if (!minmax.empty()) s += "," + minmax;
        accessors.push_back(s + "}");
        return int(accessors.size()) - 1;
    }
};

template <class T>
int addView(Bin& bin, Json& js, const std::vector<T>& v, int target = 0) {
    size_t off = bin.add(v.data(), v.size() * sizeof(T));
    return js.view(off, v.size() * sizeof(T), target);
}

}  // namespace

bool exportGlb(const Character& c, const std::vector<Clip>& clips, const std::string& path, std::string* error) {
    const Mesh& m = c.mesh;
    const Skeleton& sk = c.skeleton;
    const size_t nv = m.vertices.size(), nb = sk.bones.size();
    Bin bin;
    Json js;

    // ---- vertex streams ----
    std::vector<float> pos(nv * 3), nrm(nv * 3), uv(nv * 2), wts(nv * 4);
    std::vector<uint16_t> col(nv * 4);
    std::vector<uint8_t> jnt(nv * 4);
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < nv; ++i) {
        const Vertex& v = m.vertices[i];
        pos[i * 3] = v.pos.x; pos[i * 3 + 1] = v.pos.y; pos[i * 3 + 2] = v.pos.z;
        nrm[i * 3] = v.normal.x; nrm[i * 3 + 1] = v.normal.y; nrm[i * 3 + 2] = v.normal.z;
        uv[i * 2] = v.uv.x; uv[i * 2 + 1] = v.uv.y;
        Vec3 lin = srgbToLinear(Vec3{v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f});
        col[i * 4] = uint16_t(lin.x * 65535.0f + 0.5f);
        col[i * 4 + 1] = uint16_t(lin.y * 65535.0f + 0.5f);
        col[i * 4 + 2] = uint16_t(lin.z * 65535.0f + 0.5f);
        col[i * 4 + 3] = uint16_t(v.color[3] * 257);
        for (int k = 0; k < 4; ++k) {
            jnt[i * 4 + k] = v.joints[k];
            wts[i * 4 + k] = v.weights[k];
        }
        lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
        hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
    }
    if (nb > 256) {
        if (error) *error = "more than 256 bones";
        return false;
    }
    std::string mm = "\"min\":[" + f2s(lo.x) + "," + f2s(lo.y) + "," + f2s(lo.z) + "],\"max\":[" + f2s(hi.x) + "," +
                     f2s(hi.y) + "," + f2s(hi.z) + "]";
    int aPos = js.accessor(addView(bin, js, pos, 34962), 0, 5126, nv, "VEC3", false, mm);
    int aNrm = js.accessor(addView(bin, js, nrm, 34962), 0, 5126, nv, "VEC3");
    int aCol = js.accessor(addView(bin, js, col, 34962), 0, 5123, nv, "VEC4", true);
    int aUv = js.accessor(addView(bin, js, uv, 34962), 0, 5126, nv, "VEC2");
    int aJnt = js.accessor(addView(bin, js, jnt, 34962), 0, 5121, nv, "VEC4");
    int aWts = js.accessor(addView(bin, js, wts, 34962), 0, 5126, nv, "VEC4");

    // ---- indices (one view, one accessor per submesh) ----
    const bool small = nv < 65535;
    int ibv;
    if (small) {
        std::vector<uint16_t> idx(m.indices.begin(), m.indices.end());
        ibv = addView(bin, js, idx, 34963);
    } else {
        ibv = addView(bin, js, m.indices, 34963);
    }
    std::string prims;
    for (const Submesh& sm : m.submeshes) {
        if (!sm.indexCount) continue;
        int acc = js.accessor(ibv, sm.firstIndex * (small ? 2u : 4u), small ? 5123 : 5125, sm.indexCount, "SCALAR");
        int mat = sm.kind == SubmeshKind::Body ? 0 : sm.kind == SubmeshKind::FaceEyes ? 1 : sm.kind == SubmeshKind::FaceMouth ? 2 : 3;
        if (!prims.empty()) prims += ",";
        prims += "{\"attributes\":{\"POSITION\":" + std::to_string(aPos) + ",\"NORMAL\":" + std::to_string(aNrm) +
                 ",\"COLOR_0\":" + std::to_string(aCol) + ",\"TEXCOORD_0\":" + std::to_string(aUv) +
                 ",\"JOINTS_0\":" + std::to_string(aJnt) + ",\"WEIGHTS_0\":" + std::to_string(aWts) +
                 "},\"indices\":" + std::to_string(acc) + ",\"material\":" + std::to_string(mat) + "}";
    }

    // ---- skin ----
    std::vector<float> ibm(nb * 16);
    for (size_t b = 0; b < nb; ++b) {
        Mat4 t = Mat4::translation(-sk.bones[b].restWorld);
        std::memcpy(&ibm[b * 16], t.m, sizeof(t.m));
    }
    int aIbm = js.accessor(addView(bin, js, ibm), 0, 5126, nb, "MAT4");

    // ---- face atlas image ----
    std::vector<uint8_t> png = encodePng(c.faceAtlas);
    int imgView = addView(bin, js, png);

    // ---- animations ----
    std::string anims;
    for (const Clip& clip : clips) {
        if (clip.frames.empty()) continue;
        size_t nf = clip.frames.size() + (clip.loop ? 1 : 0);
        std::vector<float> times(nf);
        for (size_t f = 0; f < nf; ++f) times[f] = float(f) / clip.fps;
        std::string tmm = "\"min\":[0],\"max\":[" + f2s(times.back()) + "]";
        int aT = js.accessor(addView(bin, js, times), 0, 5126, nf, "SCALAR", false, tmm);
        std::string samplers, channels;
        int si = 0;
        for (size_t b = 0; b < nb; ++b) {
            std::vector<float> rot(nf * 4);
            std::vector<float> tr(nf * 3);
            bool moves = false;
            for (size_t f = 0; f < nf; ++f) {
                const Pose& p = clip.frames[f % clip.frames.size()];
                Quat q = p.rot[b];
                rot[f * 4] = q.x; rot[f * 4 + 1] = q.y; rot[f * 4 + 2] = q.z; rot[f * 4 + 3] = q.w;
                Vec3 t = sk.bones[b].restLocal + p.trans[b];
                tr[f * 3] = t.x; tr[f * 3 + 1] = t.y; tr[f * 3 + 2] = t.z;
                if (length(p.trans[b]) > 1e-6f) moves = true;
            }
            int aR = js.accessor(addView(bin, js, rot), 0, 5126, nf, "VEC4");
            auto addChannel = [&](int out, const char* pathName) {
                if (!samplers.empty()) samplers += ",";
                samplers += "{\"input\":" + std::to_string(aT) + ",\"output\":" + std::to_string(out) + ",\"interpolation\":\"LINEAR\"}";
                if (!channels.empty()) channels += ",";
                channels += "{\"sampler\":" + std::to_string(si++) + ",\"target\":{\"node\":" + std::to_string(b) +
                            ",\"path\":\"" + pathName + "\"}}";
            };
            addChannel(aR, "rotation");
            if (moves) addChannel(js.accessor(addView(bin, js, tr), 0, 5126, nf, "VEC3"), "translation");
        }
        if (!anims.empty()) anims += ",";
        anims += "{\"name\":" + jstr(clip.name) + ",\"samplers\":[" + samplers + "],\"channels\":[" + channels + "]}";
    }

    // ---- nodes ----
    std::string nodes;
    for (size_t b = 0; b < nb; ++b) {
        const Bone& bone = sk.bones[b];
        std::string kids;
        for (size_t k = 0; k < nb; ++k)
            if (sk.bones[k].parent == int(b)) kids += (kids.empty() ? "" : ",") + std::to_string(k);
        if (b) nodes += ",";
        nodes += "{\"name\":" + jstr(bone.name) + ",\"translation\":[" + f2s(bone.restLocal.x) + "," + f2s(bone.restLocal.y) +
                 "," + f2s(bone.restLocal.z) + "]" + (kids.empty() ? "" : ",\"children\":[" + kids + "]") + "}";
    }
    std::ostringstream extras;
    extras << "{\"chargen\":{\"seed\":" << c.genome.seed << ",\"archetype\":" << jstr(toString(c.genome.archetype))
           << ",\"height\":" << f2s(c.height) << ",\"vertexColorAlpha\":\"material class = floor(a*255/32): skin,cloth,leather,metal,hair,dark,horn\""
           << ",\"faceLayout\":{\"atlas\":[" << FaceLayout::kAtlasW << "," << FaceLayout::kAtlasH << "],\"eyeCell\":["
           << FaceLayout::kEyeCellW << "," << FaceLayout::kEyeCellH << "],\"eyeCols\":" << FaceLayout::kEyeCols
           << ",\"mouthCell\":[" << FaceLayout::kMouthCellW << "," << FaceLayout::kMouthCellH << "],\"mouthRowY\":"
           << FaceLayout::kMouthY << ",\"eyes\":[";
    for (int i = 0; i < int(EyeExpr::Count); ++i) extras << (i ? "," : "") << jstr(toString(EyeExpr(i)));
    extras << "],\"mouths\":[";
    for (int i = 0; i < int(MouthExpr::Count); ++i) extras << (i ? "," : "") << jstr(toString(MouthExpr(i)));
    extras << "]}}}";
    nodes += ",{\"name\":\"body\",\"mesh\":0,\"skin\":0}";
    nodes += ",{\"name\":\"character\",\"children\":[0," + std::to_string(nb) + "],\"extras\":" + extras.str() + "}";

    std::string joints;
    for (size_t b = 0; b < nb; ++b) joints += (b ? "," : "") + std::to_string(b);

    std::string bvs, accs;
    for (size_t i = 0; i < js.bufferViews.size(); ++i) bvs += (i ? "," : "") + js.bufferViews[i];
    for (size_t i = 0; i < js.accessors.size(); ++i) accs += (i ? "," : "") + js.accessors[i];
    while (bin.data.size() % 4) bin.data.push_back(0);

    std::string mat = "{\"name\":\"body\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":0.85}}";
    auto decalMat = [](const char* name) {
        return std::string("{\"name\":\"") + name +
               "\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0,\"roughnessFactor\":0.9},"
               "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5}";
    };
    // Skin layer: meant for a 2x multiply blend (texel rgb = multiplier / 2); plain viewers show it as a blend.
    const std::string skinMat =
        "{\"name\":\"face_skin_multiply2x\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0,"
        "\"roughnessFactor\":0.9},\"alphaMode\":\"BLEND\",\"extras\":{\"blend\":\"dst_color,src_color\"}}";
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"chargen procedural character generator\"},"
                       "\"scene\":0,\"scenes\":[{\"nodes\":[" + std::to_string(nb + 1) + "]}],"
                       "\"nodes\":[" + nodes + "],"
                       "\"meshes\":[{\"name\":\"body\",\"primitives\":[" + prims + "]}],"
                       "\"skins\":[{\"inverseBindMatrices\":" + std::to_string(aIbm) + ",\"skeleton\":0,\"joints\":[" + joints + "]}],"
                       "\"materials\":[" + mat + "," + decalMat("face_eyes") + "," + decalMat("face_mouth") + "," + skinMat + "],"
                       "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":33071,\"wrapT\":33071}],"
                       "\"images\":[{\"bufferView\":" + std::to_string(imgView) + ",\"mimeType\":\"image/png\",\"name\":\"face_atlas\"}],"
                       "\"textures\":[{\"sampler\":0,\"source\":0}],"
                       + (anims.empty() ? std::string() : "\"animations\":[" + anims + "],") +
                       "\"bufferViews\":[" + bvs + "],\"accessors\":[" + accs + "],"
                       "\"buffers\":[{\"byteLength\":" + std::to_string(bin.data.size()) + "}]}";
    while (json.size() % 4) json += ' ';

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    uint32_t total = 12 + 8 + uint32_t(json.size()) + 8 + uint32_t(bin.data.size());
    u32(0x46546C67);  // "glTF"
    u32(2);
    u32(total);
    u32(uint32_t(json.size()));
    u32(0x4E4F534A);  // JSON
    std::fwrite(json.data(), 1, json.size(), f);
    u32(uint32_t(bin.data.size()));
    u32(0x004E4942);  // BIN
    std::fwrite(bin.data.data(), 1, bin.data.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace cg
