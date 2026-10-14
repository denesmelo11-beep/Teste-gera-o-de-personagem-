#include "chargen/render.hpp"

#include <cstring>

#include "chargen/animation.hpp"

namespace cg {

ToonRenderer::ToonRenderer(int width, int height, int ssaa, RenderSettings rs)
    : W_(width), H_(height), ss_(std::max(1, ssaa)), w_(width * ss_), h_(height * ss_), rs_(rs) {
    color_.assign(size_t(w_) * h_, Vec3{});
    depth_.assign(size_t(w_) * h_, 1e30f);
    id_.assign(size_t(w_) * h_, 0);
}

void ToonRenderer::setCamera(Vec3 eye, Vec3 target, float fovy, bool ortho, float orthoHeight) {
    eye_ = eye;
    ortho_ = ortho;
    view_ = lookAt(eye, target, {0, 1, 0});
    float aspect = float(W_) / H_;
    if (ortho) {
        Mat4 p = Mat4::identity();
        p.m[0] = 2.0f / (orthoHeight * aspect);
        p.m[5] = 2.0f / orthoHeight;
        p.m[10] = -2.0f / 200.0f;
        p.m[14] = -1.0f;
        proj_ = p;
    } else {
        proj_ = perspective(fovy, aspect, 0.05f, 200.0f);
    }
    vp_ = proj_ * view_;
}

void ToonRenderer::clearSky(Vec3 top, Vec3 bottom) {
    for (int y = 0; y < h_; ++y) {
        Vec3 c = lerp(top, bottom, float(y) / (h_ - 1));
        for (int x = 0; x < w_; ++x) {
            size_t i = size_t(y) * w_ + x;
            // Soft diagonal cloud bands for a bit of life.
            float band = std::sin(x * 0.004f / ss_ + y * 0.011f / ss_) * std::sin(x * 0.0013f / ss_ - y * 0.003f / ss_);
            color_[i] = lerp(c, Vec3{1, 1, 1}, 0.10f * smoothstep(0.35f, 0.9f, band));
            depth_[i] = 1e30f;
            id_[i] = 0;
        }
    }
}

template <class Shade>
void ToonRenderer::raster(const V* in, int n, bool cull, Shade&& shade) {
    // Clip polygon against the near plane (z > -w).
    V poly[16];
    int pn = 0;
    for (int i = 0; i < n; ++i) {
        const V &a = in[i], &b = in[(i + 1) % n];
        float da = a.clip.z + a.clip.w, db = b.clip.z + b.clip.w;
        if (da >= 0) poly[pn++] = a;
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            V c;
            c.clip = {lerp(a.clip.x, b.clip.x, t), lerp(a.clip.y, b.clip.y, t), lerp(a.clip.z, b.clip.z, t), lerp(a.clip.w, b.clip.w, t)};
            for (int k = 0; k < 12; ++k) c.a[k] = lerp(a.a[k], b.a[k], t);
            poly[pn++] = c;
        }
    }
    if (pn < 3) return;
    struct S {
        float x, y, z, iw;
    } s[16];
    for (int i = 0; i < pn; ++i) {
        float iw = 1.0f / poly[i].clip.w;
        s[i] = {(poly[i].clip.x * iw * 0.5f + 0.5f) * w_, (1 - (poly[i].clip.y * iw * 0.5f + 0.5f)) * h_, poly[i].clip.z * iw, iw};
    }
    for (int t = 1; t + 1 < pn; ++t) {
        const S &A = s[0], &B = s[t], &C = s[t + 1];
        float area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        if (std::fabs(area) < 1e-9f) continue;
        if (cull && area > 0) continue;  // screen y is flipped: CCW front faces have negative area
        int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, C.x})))), x1 = std::min(w_ - 1, int(std::ceil(std::max({A.x, B.x, C.x}))));
        int y0 = std::max(0, int(std::floor(std::min({A.y, B.y, C.y})))), y1 = std::min(h_ - 1, int(std::ceil(std::max({A.y, B.y, C.y}))));
        const V &va = poly[0], &vb = poly[t], &vc = poly[t + 1];
        float inv = 1.0f / area;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((B.x - px) * (C.y - py) - (B.y - py) * (C.x - px)) * inv;
                float w1 = ((C.x - px) * (A.y - py) - (C.y - py) * (A.x - px)) * inv;
                float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = w0 * A.z + w1 * B.z + w2 * C.z;
                float iw = w0 * A.iw + w1 * B.iw + w2 * C.iw;
                float vz = ortho_ ? z : 1.0f / iw;  // view depth for outlines
                size_t idx = size_t(y) * w_ + x;
                float attr[12];
                float k0 = w0 * A.iw / iw, k1 = w1 * B.iw / iw, k2 = w2 * C.iw / iw;
                for (int k = 0; k < 12; ++k) attr[k] = va.a[k] * k0 + vb.a[k] * k1 + vc.a[k] * k2;
                shade(idx, vz, attr);
            }
    }
}

void ToonRenderer::drawFloor(float hs, float tile, Vec3 c0, Vec3 c1, const std::vector<Vec3>& blobs) {
    const int n = 16;  // tessellate so near-plane clipping / perspective stay well behaved
    float step = 2 * hs / n;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            Vec3 p[4] = {{-hs + i * step, 0, -hs + j * step}, {-hs + i * step, 0, -hs + (j + 1) * step},
                         {-hs + (i + 1) * step, 0, -hs + (j + 1) * step}, {-hs + (i + 1) * step, 0, -hs + j * step}};
            V v[4];
            for (int k = 0; k < 4; ++k) {
                v[k].clip = vp_ * Vec4(p[k], 1);
                std::memset(v[k].a, 0, sizeof(v[k].a));
                v[k].a[0] = p[k].x;
                v[k].a[1] = p[k].z;
            }
            raster(v, 4, false, [&](size_t idx, float z, const float* a) {
                if (z >= depth_[idx]) return;
                int cx = int(std::floor(a[0] / tile)), cz = int(std::floor(a[1] / tile));
                Vec3 col = ((cx + cz) & 1) ? c0 : c1;
                (void)cx;
                float sh = 1;
                for (const Vec3& b : blobs) {
                    float d = length(Vec2{a[0] - b.x, (a[1] - b.y) * 1.3f}) / b.z;
                    sh *= 1 - 0.38f * (1 - smoothstep(0.35f, 1.0f, d));
                }
                // Fade into the sky with distance.
                float fog = smoothstep(4.0f, 14.0f, length(Vec2{a[0] - eye_.x, a[1] - eye_.z}));
                depth_[idx] = z;
                id_[idx] = 1;
                color_[idx] = lerp(col * sh, color_[idx], fog * 0.85f);
            });
        }
}

void ToonRenderer::drawCharacter(const Character& c, const std::vector<Vec3>& pos, const std::vector<Vec3>& nrm,
                                 const Mat4& model, FaceState face, int idBase) {
    const Mesh& m = c.mesh;
    const Vec3 L = normalize(rs_.lightDir);
    static const Vec3 kShadow[] = {{0.86f, 0.64f, 0.66f}, {0.62f, 0.60f, 0.78f}, {0.62f, 0.56f, 0.62f},
                                   {0.45f, 0.46f, 0.58f}, {0.60f, 0.58f, 0.80f}, {0.70f, 0.68f, 0.75f},
                                   {0.66f, 0.62f, 0.70f}, {1.0f, 1.0f, 1.0f}};
    const Vec2 eyeOff = FaceLayout::eyeUvOffset(face.eyes), mouthOff = FaceLayout::mouthUvOffset(face.mouth);
    for (const Submesh& sm : m.submeshes) {
        const bool decal = sm.kind != SubmeshKind::Body;
        const bool skinLayer = sm.kind == SubmeshKind::FaceSkin;
        const Vec2 off = sm.kind == SubmeshKind::FaceEyes ? eyeOff : skinLayer ? Vec2{0, 0} : mouthOff;
        for (uint32_t i = sm.firstIndex; i < sm.firstIndex + sm.indexCount; i += 3) {
            V v[3];
            uint32_t vid[3] = {m.indices[i], m.indices[i + 1], m.indices[i + 2]};
            for (int k = 0; k < 3; ++k) {
                const Vertex& vx = m.vertices[vid[k]];
                Vec3 wp = model.point(pos[vid[k]]), wn = normalize(model.dir(nrm[vid[k]]));
                v[k].clip = vp_ * Vec4(wp, 1);
                float* a = v[k].a;
                a[0] = wn.x; a[1] = wn.y; a[2] = wn.z;
                a[3] = vx.color[0] / 255.0f; a[4] = vx.color[1] / 255.0f; a[5] = vx.color[2] / 255.0f;
                a[6] = vx.uv.x + off.x; a[7] = vx.uv.y + off.y;
                a[8] = wp.x; a[9] = wp.y; a[10] = wp.z;
                a[11] = float(vx.color[3] / 32);
            }
            const uint32_t pid = uint32_t(idBase) + m.partIds[vid[0]];
            raster(v, 3, true, [&](size_t idx, float z, const float* a) {
                if (decal ? z > depth_[idx] + 0.004f : z >= depth_[idx]) return;
                Vec3 N = normalize(Vec3{a[0], a[1], a[2]});
                Vec3 Vd = normalize(eye_ - Vec3{a[8], a[9], a[10]});
                float ndl = dot(N, L);
                float lit = smoothstep(-0.03f, 0.05f, ndl);
                if (decal) {
                    Vec4 t = c.faceAtlas.sample({a[6], a[7]});
                    if (t.w < 0.02f) return;
                    if (skinLayer) {  // 2x multiply: rgb holds half the multiplier
                        color_[idx] = color_[idx] * lerp(Vec3{1, 1, 1}, t.xyz() * 2.0f, t.w);
                        return;
                    }
                    Vec3 col = t.xyz() * lerp(0.86f, 1.0f, lit);
                    color_[idx] = lerp(color_[idx], col, t.w);
                    return;  // keep depth/id of the head underneath
                }
                int cls = std::clamp(int(a[11] + 0.5f), 0, 7);
                Vec3 base{a[3], a[4], a[5]};
                if (cls == int(MaterialClass::Glow)) {  // emissive: unlit
                    depth_[idx] = z;
                    id_[idx] = pid;
                    color_[idx] = base;
                    return;
                }
                if (rs_.style == RenderSettings::Style::LowPoly) {
                    // Soft faceted lighting: wide wrap, cool tinted shadow side, sky fill.
                    float k = smoothstep(-0.65f, 0.95f, ndl);
                    Vec3 shadow = base * lerp(kShadow[cls], Vec3{1, 1, 1}, 0.35f) * 0.78f;
                    Vec3 col = lerp(shadow, base * 1.04f, k) + base * (0.05f * N.y);
                    Vec3 Hh = normalize(L + Vd);
                    if (cls == int(MaterialClass::Metal)) col += Vec3{1, 0.97f, 0.9f} * (0.35f * smoothstep(0.9f, 0.97f, dot(N, Hh)));
                    float rim = std::pow(1 - saturate(dot(N, Vd)), 4.0f);
                    col += Vec3{1, 0.97f, 0.95f} * (0.07f * rim);
                    depth_[idx] = z;
                    id_[idx] = pid;
                    color_[idx] = col;
                    return;
                }
                Vec3 col = base * lerp(kShadow[cls], Vec3{1, 1, 1}, lit);
                col = col * lerp(0.93f, 1.0f, smoothstep(0.35f, 0.6f, ndl));  // soft form band
                Vec3 Hh = normalize(L + Vd);
                float nh = dot(N, Hh);
                if (cls == int(MaterialClass::Hair)) col += Vec3{1, 1, 1} * (0.16f * lit * smoothstep(0.86f, 0.9f, nh) * (1 - smoothstep(0.95f, 0.98f, nh)));
                if (cls == int(MaterialClass::Metal)) col += Vec3{1, 0.97f, 0.9f} * (0.5f * smoothstep(0.88f, 0.92f, nh));
                if (cls == int(MaterialClass::Skin)) col += Vec3{1, 0.9f, 0.85f} * (0.05f * lit * smoothstep(0.8f, 0.95f, nh));
                float rim = std::pow(1 - saturate(dot(N, Vd)), 3.0f) * smoothstep(-0.2f, 0.3f, N.y + 0.3f);
                col += Vec3{0.85f, 0.9f, 1.0f} * (0.22f * rim);
                depth_[idx] = z;
                id_[idx] = pid;
                color_[idx] = col;
            });
        }
    }
}

void ToonRenderer::drawText(const std::string& text, int x, int y, int scale, Vec3 color) {
    texts_.push_back({text, x, y, scale, color});
}

namespace {
// 5x7 bitmap font: uppercase, digits and a few symbols.
const char* glyph(char ch) {
    switch (ch) {
        case 'A': return "01110100011000111111100011000110001";
        case 'B': return "11110100011000111110100011000111110";
        case 'C': return "01110100011000010000100001000101110";
        case 'D': return "11110100011000110001100011000111110";
        case 'E': return "11111100001000011110100001000011111";
        case 'F': return "11111100001000011110100001000010000";
        case 'G': return "01110100011000010111100011000101111";
        case 'H': return "10001100011000111111100011000110001";
        case 'I': return "01110001000010000100001000010001110";
        case 'J': return "00111000100001000010000101001001100";
        case 'K': return "10001100101010011000101001001010001";
        case 'L': return "10000100001000010000100001000011111";
        case 'M': return "10001110111010110101100011000110001";
        case 'N': return "10001100011100110101100111000110001";
        case 'O': return "01110100011000110001100011000101110";
        case 'P': return "11110100011000111110100001000010000";
        case 'Q': return "01110100011000110001101011001001101";
        case 'R': return "11110100011000111110101001001010001";
        case 'S': return "01111100001000001110000010000111110";
        case 'T': return "11111001000010000100001000010000100";
        case 'U': return "10001100011000110001100011000101110";
        case 'V': return "10001100011000110001100010101000100";
        case 'W': return "10001100011000110101101011010101010";
        case 'X': return "10001100010101000100010101000110001";
        case 'Y': return "10001100010101000100001000010000100";
        case 'Z': return "11111000010001000100010001000011111";
        case '0': return "01110100011001110101110011000101110";
        case '1': return "00100011000010000100001000010001110";
        case '2': return "01110100010000100010001000100011111";
        case '3': return "11111000100010000010000011000101110";
        case '4': return "00010001100101010010111110001000010";
        case '5': return "11111100001111000001000011000101110";
        case '6': return "00110010001000011110100011000101110";
        case '7': return "11111000010001000100010000100001000";
        case '8': return "01110100011000101110100011000101110";
        case '9': return "01110100011000101111000010001001100";
        case '#': return "01010010101111101010111110101001010";
        case '-': return "00000000000000011111000000000000000";
        case '.': return "00000000000000000000000000110001100";
        case ':': return "00000011000110000000011000110000000";
        case '/': return "00001000010001000100010001000010000";
        default: return nullptr;
    }
}
}  // namespace

Image ToonRenderer::finish() {
    // Ink lines on the nearer pixel of every id boundary.
    std::vector<Vec3> out = color_;
    if (rs_.outlines) {
        const int r = std::max(1, int(std::round(rs_.outlineWidth * ss_ * 0.5f)));
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                size_t i = size_t(y) * w_ + x;
                uint32_t id = id_[i];
                if (id < 2) continue;
                float d = depth_[i];
                bool edge = false;
                for (int dy = -r; dy <= r && !edge; ++dy)
                    for (int dx = -r; dx <= r && !edge; ++dx) {
                        if (dx * dx + dy * dy > r * r) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                        size_t j = size_t(ny) * w_ + nx;
                        if (id_[j] == id) {
                            if (depth_[j] > d * 1.02f + 0.01f) edge = true;  // self-overlap (leg over leg)
                            continue;
                        }
                        if (id_[j] < 2 || depth_[j] > d + 0.002f * d) edge = true;
                    }
                if (edge) out[i] = color_[i] * 0.28f + Vec3{0.03f, 0.02f, 0.05f};
            }
    }
    Image img(W_, H_);
    const float inv = 1.0f / (ss_ * ss_);
    for (int y = 0; y < H_; ++y)
        for (int x = 0; x < W_; ++x) {
            Vec3 s{};
            for (int j = 0; j < ss_; ++j)
                for (int i = 0; i < ss_; ++i) s += out[size_t(y * ss_ + j) * w_ + size_t(x * ss_ + i)];
            img.set(x, y, Vec4(s * inv, 1));
        }
    for (const Text& t : texts_) {
        int cx = t.x;
        for (char ch : t.s) {
            char u = (ch >= 'a' && ch <= 'z') ? char(ch - 32) : ch;
            if (const char* gph = glyph(u))
                for (int gy = 0; gy < 7; ++gy)
                    for (int gx = 0; gx < 5; ++gx)
                        if (gph[gy * 5 + gx] == '1')
                            for (int sy = 0; sy < t.scale; ++sy)
                                for (int sx = 0; sx < t.scale; ++sx) {
                                    int px = cx + gx * t.scale + sx, py = t.y + gy * t.scale + sy;
                                    if (px >= 0 && py >= 0 && px < W_ && py < H_) img.set(px, py, Vec4(t.c, 1));
                                }
            cx += 6 * t.scale;
        }
    }
    return img;
}

Image renderLineup(const std::vector<Character>& chars, int width, int height, float time, bool labels, RenderSettings rs) {
    ToonRenderer r(width, height, 2, rs);
    const size_t n = chars.size();
    float maxH = 0, spacing = 0;
    for (const auto& c : chars) {
        maxH = std::max(maxH, c.height);
        spacing = std::max(spacing, c.height * 0.42f);
    }
    spacing = std::max(spacing, 0.75f);
    float total = spacing * float(n - 1);
    float aspect = float(width) / height;
    float viewH = std::max(maxH * 1.25f, (total + spacing * 1.2f) / aspect);
    Vec3 target{0, viewH * 0.42f, 0};
    float fov = 0.5f;
    float dist = viewH * 0.5f / std::tan(fov * 0.5f);
    r.setCamera(target + Vec3{0, viewH * 0.08f, dist}, target, fov);
    const bool toon = rs.style == RenderSettings::Style::Toon;
    if (toon) r.clearSky({0.33f, 0.55f, 0.92f}, {0.78f, 0.88f, 1.0f});
    else r.clearSky({0.96f, 0.96f, 0.97f}, {0.99f, 0.99f, 0.99f});
    std::vector<Vec3> blobs;
    for (size_t i = 0; i < n; ++i) blobs.push_back({-total * 0.5f + spacing * float(i), 0, chars[i].height * 0.17f});
    if (toon) r.drawFloor(30, 0.5f, {0.93f, 0.92f, 0.98f}, {0.58f, 0.62f, 0.85f}, blobs);
    else r.drawFloor(30, 0.5f, {0.97f, 0.97f, 0.97f}, {0.97f, 0.97f, 0.97f}, blobs);
    std::vector<Mat4> global, skin;
    std::vector<Vec3> pos, nrm;
    for (size_t i = 0; i < n; ++i) {
        const Character& c = chars[i];
        Clip idle = makeIdleClip(c);
        Pose p = sampleClip(idle, time + 0.37f * float(i));
        poseMatrices(c.skeleton, p, global, skin);
        skinMesh(c.mesh, skin, pos, nrm);
        Mat4 model = Mat4::trs({blobs[i].x, 0, 0}, Quat::axisAngle({0, 1, 0}, 0.25f * std::sin(float(i) * 1.7f)));
        r.drawCharacter(c, pos, nrm, model, FaceState{}, int(i + 1) * 1000);
    }
    if (labels) {
        for (size_t i = 0; i < n; ++i) {
            const Character& c = chars[i];
            std::string a(toString(c.genome.archetype));
            std::string s = "#" + std::to_string(c.genome.seed);
            // Project the foot position to place the label.
            float fx = (-total * 0.5f + spacing * float(i)) / (viewH * aspect) + 0.5f;
            int x = int(fx * width) - int(a.size()) * 6;
            r.drawText(a, x, height - 44, 2, {0.15f, 0.15f, 0.3f});
            r.drawText(s, x, height - 24, 2, {0.25f, 0.25f, 0.45f});
        }
    }
    return r.finish();
}

}  // namespace cg
