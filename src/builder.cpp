#include "builder.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <unordered_map>

namespace cg::detail {

void SkinW::add(int bone, float wt) {
    if (wt <= 0) return;
    for (int i = 0; i < 4; ++i)
        if (w[i] > 0 && j[i] == bone) {
            w[i] += wt;
            return;
        }
    int slot = 0;
    for (int i = 1; i < 4; ++i)
        if (w[i] < w[slot]) slot = i;
    if (w[slot] < wt) {
        j[slot] = bone;
        w[slot] = wt;
    }
}

void SkinW::normalize() {
    float s = w[0] + w[1] + w[2] + w[3];
    if (s <= 0) {
        w[0] = 1;
        return;
    }
    for (float& x : w) x /= s;
}

SkinW one(int bone) {
    SkinW s;
    s.j[0] = bone;
    s.w[0] = 1;
    return s;
}

SkinW blend(int a, int b, float t) {
    t = saturate(t);
    SkinW s;
    s.add(a, 1 - t);
    s.add(b, t);
    s.normalize();
    return s;
}

// ---------------- Curve ----------------
Curve::Curve(std::initializer_list<std::pair<float, float>> keys) {
    for (auto& k : keys) key(k.first, k.second);
}

void Curve::key(float t, float v) {
    t_.push_back(t);
    v_.push_back(v);
    m_.clear();
}

void Curve::prepare() const {
    size_t n = t_.size();
    m_.assign(n, 0.0f);
    if (n < 2) return;
    std::vector<float> d(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) d[i] = (v_[i + 1] - v_[i]) / std::max(1e-6f, t_[i + 1] - t_[i]);
    m_[0] = d[0];
    m_[n - 1] = d[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) m_[i] = (d[i - 1] * d[i] <= 0) ? 0.0f : 0.5f * (d[i - 1] + d[i]);
    for (size_t i = 0; i + 1 < n; ++i) {
        if (d[i] == 0) {
            m_[i] = m_[i + 1] = 0;
            continue;
        }
        float a = m_[i] / d[i], b = m_[i + 1] / d[i];
        float s = a * a + b * b;
        if (s > 9) {
            float tau = 3.0f / std::sqrt(s);
            m_[i] = tau * a * d[i];
            m_[i + 1] = tau * b * d[i];
        }
    }
}

float Curve::operator()(float t) const {
    size_t n = t_.size();
    if (n == 0) return 0;
    if (n == 1 || t <= t_[0]) return v_[0];
    if (t >= t_[n - 1]) return v_[n - 1];
    if (m_.size() != n) prepare();
    size_t i = 0;
    while (i + 2 < n && t > t_[i + 1]) ++i;
    float h = t_[i + 1] - t_[i], s = (t - t_[i]) / h;
    float s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * v_[i] + (s3 - 2 * s2 + s) * h * m_[i] + (-2 * s3 + 3 * s2) * v_[i + 1] +
           (s3 - s2) * h * m_[i + 1];
}

// ---------------- SuperRing ----------------
Vec2 SuperRing::point(float theta) const {
    float c = std::cos(theta), s = std::sin(theta);
    float n = s >= 0 ? nF : nB;
    float x = w * spow(c, 2.0f / n);
    float z = zc + (s >= 0 ? dF : dB) * spow(s, 2.0f / n);
    return {x, z};
}

float SuperRing::frontZ(float x) const {
    float a = std::min(std::fabs(x) / w, 1.0f);
    float c = std::pow(a, nF * 0.5f);
    float s = std::sqrt(std::max(0.0f, 1 - c * c));
    return zc + dF * std::pow(s, 2.0f / nF);
}

float SuperRing::backZ(float x) const {
    float a = std::min(std::fabs(x) / w, 1.0f);
    float c = std::pow(a, nB * 0.5f);
    float s = std::sqrt(std::max(0.0f, 1 - c * c));
    return zc - dB * std::pow(s, 2.0f / nB);
}

float SuperRing::field(float x, float z) const {
    bool front = z >= zc;
    float n = front ? nF : nB, d = front ? dF : dB;
    float f = std::pow(std::fabs(x) / w, n) + std::pow(std::fabs(z - zc) / d, n);
    return std::pow(f, 1.0f / n);
}

std::vector<Vec3> makeRing(Vec3 c, Vec3 u, Vec3 v, const SuperRing& sr, int n) {
    std::vector<Vec3> r(n);
    for (int j = 0; j < n; ++j) {
        Vec2 p = sr.point(kTau * j / n);
        r[j] = c + u * p.x + v * p.y;
    }
    return r;
}

// ---------------- emission ----------------
namespace {
// Split along the diagonal that keeps the two triangles most coplanar:
// fewer sliver facets on curved, low-poly surfaces.
void emitQuad(Part& part, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const Mat& m) {
    const Vec3 &A = part.pos[a], &B = part.pos[b], &C = part.pos[c], &D = part.pos[d];
    float k1 = dot(normalize(cross(B - A, C - A), {0, 0, 0}), normalize(cross(C - A, D - A), {0, 0, 0}));
    float k2 = dot(normalize(cross(B - A, D - A), {0, 0, 0}), normalize(cross(C - B, D - B), {0, 0, 0}));
    if (k2 > k1 + 1e-3f) {
        part.tris.push_back({a, b, d, m});
        part.tris.push_back({b, c, d, m});
    } else {
        part.tris.push_back({a, b, c, m});
        part.tris.push_back({a, c, d, m});
    }
}
}  // namespace

void emitStrip(Part& part, const std::vector<uint32_t>& A, const std::vector<uint32_t>& B, const Mat& m) {
    const size_t n = A.size();
    for (size_t j = 0; j < n; ++j) emitQuad(part, A[j], B[j], B[(j + 1) % n], A[(j + 1) % n], m);
}

void emitLoft(Part& part, const Loft& loft, const WeightFn& wf, const PaintFn& pf) {
    const size_t R = loft.rings.size();
    if (R == 0) return;
    const int N = int(loft.rings[0].size());
    const uint32_t base = uint32_t(part.pos.size());
    for (size_t i = 0; i < R; ++i)
        for (int j = 0; j < N; ++j) {
            Vec3 p = loft.rings[i][j];
            part.pos.push_back(p);
            part.w.push_back(wf(loft.t[i], kTau * j / N, p));
            part.uv.push_back({});
        }
    auto idx = [&](size_t i, int j) { return base + uint32_t(i * N + ((j % N + N) % N)); };
    auto midTheta = [&](int j) { return kTau * (j + 0.5f) / N; };
    for (size_t i = 0; i + 1 < R; ++i) {
        float tm = 0.5f * (loft.t[i] + loft.t[i + 1]);
        for (int j = 0; j < N; ++j) {
            uint32_t a = idx(i, j), b = idx(i + 1, j), c = idx(i + 1, j + 1), d = idx(i, j + 1);
            Mat m = part.paintAt ? part.paintAt((part.pos[a] + part.pos[b] + part.pos[c] + part.pos[d]) * 0.25f)
                                 : pf(tm, midTheta(j));
            emitQuad(part, a, b, c, d, m);
        }
    }
    if (loft.poleStart) {
        uint32_t p = uint32_t(part.pos.size());
        part.pos.push_back(loft.startPole);
        part.w.push_back(wf(loft.tStart, 0, loft.startPole));
        part.uv.push_back({});
        float tm = 0.5f * (loft.tStart + loft.t[0]);
        for (int j = 0; j < N; ++j) {
            Mat m = part.paintAt ? part.paintAt((part.pos[p] + part.pos[idx(0, j)] + part.pos[idx(0, j + 1)]) / 3.0f) : pf(tm, midTheta(j));
            part.tris.push_back({p, idx(0, j), idx(0, j + 1), m});
        }
    }
    if (loft.poleEnd) {
        uint32_t p = uint32_t(part.pos.size());
        part.pos.push_back(loft.endPole);
        part.w.push_back(wf(loft.tEnd, 0, loft.endPole));
        part.uv.push_back({});
        float tm = 0.5f * (loft.tEnd + loft.t[R - 1]);
        for (int j = 0; j < N; ++j) {
            Mat m = part.paintAt ? part.paintAt((part.pos[p] + part.pos[idx(R - 1, j)] + part.pos[idx(R - 1, j + 1)]) / 3.0f) : pf(tm, midTheta(j));
            part.tris.push_back({p, idx(R - 1, j + 1), idx(R - 1, j), m});
        }
    }
}

void emitEllipsoid(Part& part, const Ellipsoid& e, const WeightFn& wf, const PaintFn& pf) {
    Vec3 d = normalize(e.axis);
    Vec3 v = normalize(e.up - d * dot(e.up, d));
    Vec3 u = cross(d, v);
    Loft L;
    for (int i = 1; i < e.lat; ++i) {
        float a = kPi * i / e.lat;  // 0 = back pole, pi = front pole
        float along = -std::cos(a), rad = std::sin(a);
        SuperRing sr{e.rU * rad, 0, e.rV * rad, e.rV * rad};
        L.rings.push_back(makeRing(e.center + d * (along * e.rAxis), u, v, sr, e.lon));
        L.t.push_back(float(i) / e.lat);
    }
    L.poleStart = L.poleEnd = true;
    L.startPole = e.center - d * e.rAxis;
    L.endPole = e.center + d * e.rAxis;
    L.tStart = 0;
    L.tEnd = 1;
    emitLoft(part, L, wf, pf);
}

namespace {
uint32_t packMat(const Mat& m) {
    auto q = [](float c) { return uint32_t(saturate(c) * 255.0f + 0.5f); };
    return q(m.color.x) | (q(m.color.y) << 8) | (q(m.color.z) << 16) | (uint32_t(m.cls) << 24);
}
}  // namespace

namespace {

// Splits the part's triangles along each clip boundary. New vertices interpolate
// position, weights and uv; every triangle carries its own corner normals, which
// are interpolated along the cut edge so the cut leaves no shading seam.
// Conforming (red-green) refinement near a clip boundary: triangles whose
// interior the boundary touches (even when all three corners lie on the same
// side, e.g. a thin band inside one large facet) are subdivided at edge midpoints;
// neighbours split the shared edges too, so no T-junctions appear. Sub-triangles
// stay in their parent's plane, so the shape and flat shading are unchanged.
void refineNear(Part& part, std::vector<std::array<Vec3, 3>>& cn, std::vector<uint32_t>& src, std::vector<float>& shade,
                const std::function<float(Vec3)>& field, int passes) {
    for (int pass = 0; pass < passes; ++pass) {
        auto edgeKey = [](uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a); };
        std::unordered_map<uint64_t, uint32_t> mid;  // marked edge -> midpoint (UINT32_MAX until created)
        for (const auto& tr : part.tris) {
            const Vec3 a = part.pos[tr.a], b = part.pos[tr.b], c = part.pos[tr.c];
            const bool sa = field(a) < 0, sb = field(b) < 0, sc = field(c) < 0;
            // An edge the boundary crosses an even number of times (0 or 2) while its
            // interior changes sign cannot be cut exactly: sample a few points per edge.
            auto edgeTrouble = [&](Vec3 p, Vec3 q, bool sp, bool sq) {
                int changes = 0;
                bool prev = sp;
                for (float k : {0.25f, 0.5f, 0.75f}) {
                    bool cur = field(lerp(p, q, k)) < 0;
                    changes += cur != prev;
                    prev = cur;
                }
                changes += sq != prev;
                return changes >= 2;
            };
            bool interior = false;
            if (sa == sb && sb == sc) {  // boundary may sit entirely inside this face
                const Vec3 inner[] = {(a + b + c) / 3.0f, (a * 2.0f + b + c) * 0.25f, (a + b * 2.0f + c) * 0.25f, (a + b + c * 2.0f) * 0.25f};
                for (const Vec3& q : inner) interior |= (field(q) < 0) != sa;
            }
            if (!interior && !edgeTrouble(a, b, sa, sb) && !edgeTrouble(b, c, sb, sc) && !edgeTrouble(c, a, sc, sa)) continue;
            for (auto [x, y] : {std::pair{tr.a, tr.b}, std::pair{tr.b, tr.c}, std::pair{tr.c, tr.a}}) mid.emplace(edgeKey(x, y), UINT32_MAX);
        }
        if (mid.empty()) return;
        auto midOf = [&](uint32_t x, uint32_t y) -> uint32_t {
            auto it = mid.find(edgeKey(x, y));
            if (it == mid.end()) return UINT32_MAX;
            if (it->second == UINT32_MAX) {
                SkinW w;
                for (int i = 0; i < 4; ++i) {
                    w.add(part.w[x].j[i], part.w[x].w[i] * 0.5f);
                    w.add(part.w[y].j[i], part.w[y].w[i] * 0.5f);
                }
                it->second = uint32_t(part.pos.size());
                part.pos.push_back((part.pos[x] + part.pos[y]) * 0.5f);
                part.w.push_back(w);
                part.uv.push_back((part.uv[x] + part.uv[y]) * 0.5f);
                if (!shade.empty()) shade.push_back(0.5f * (shade[x] + shade[y]));
            }
            return it->second;
        };
        std::vector<Part::Tri> out;
        std::vector<std::array<Vec3, 3>> outN;
        std::vector<uint32_t> outSrc;
        auto nmid = [](Vec3 p, Vec3 q) { return normalize(p + q, p); };
        auto emit = [&](uint32_t a, uint32_t b, uint32_t c, const Mat& m, Vec3 na, Vec3 nb, Vec3 nc, uint32_t s0) {
            out.push_back({a, b, c, m});
            outN.push_back({na, nb, nc});
            outSrc.push_back(s0);
        };
        for (size_t t = 0; t < part.tris.size(); ++t) {
            const Part::Tri tr = part.tris[t];
            const uint32_t v[3] = {tr.a, tr.b, tr.c};
            const auto& n = cn[t];
            uint32_t m3[3] = {midOf(v[0], v[1]), midOf(v[1], v[2]), midOf(v[2], v[0])};
            const Vec3 nm[3] = {nmid(n[0], n[1]), nmid(n[1], n[2]), nmid(n[2], n[0])};
            const int k = int(m3[0] != UINT32_MAX) + int(m3[1] != UINT32_MAX) + int(m3[2] != UINT32_MAX);
            if (k == 0) {
                emit(v[0], v[1], v[2], tr.m, n[0], n[1], n[2], src[t]);
            } else if (k == 3) {
                emit(v[0], m3[0], m3[2], tr.m, n[0], nm[0], nm[2], src[t]);
                emit(m3[0], v[1], m3[1], tr.m, nm[0], n[1], nm[1], src[t]);
                emit(m3[2], m3[1], v[2], tr.m, nm[2], nm[1], n[2], src[t]);
                emit(m3[0], m3[1], m3[2], tr.m, nm[0], nm[1], nm[2], src[t]);
            } else if (k == 1) {
                int e = m3[0] != UINT32_MAX ? 0 : m3[1] != UINT32_MAX ? 1 : 2;  // edge v[e] -> v[e+1]
                const int a = e, b = (e + 1) % 3, c = (e + 2) % 3;
                emit(v[a], m3[e], v[c], tr.m, n[size_t(a)], nm[e], n[size_t(c)], src[t]);
                emit(m3[e], v[b], v[c], tr.m, nm[e], n[size_t(b)], n[size_t(c)], src[t]);
            } else {  // two split edges: the unsplit one is opposite the corner both split edges share
                int u = m3[0] == UINT32_MAX ? 0 : m3[1] == UINT32_MAX ? 1 : 2;  // unsplit edge v[u] -> v[u+1]
                const int a = u, b = (u + 1) % 3, c = (u + 2) % 3;          // c is the shared corner
                const uint32_t mbc = m3[b], mca = m3[c];                    // edges b->c and c->a
                const Vec3 nbc = nm[b], nca = nm[c];
                emit(v[c], mca, mbc, tr.m, n[size_t(c)], nca, nbc, src[t]);
                emit(v[a], v[b], mbc, tr.m, n[size_t(a)], n[size_t(b)], nbc, src[t]);
                emit(v[a], mbc, mca, tr.m, n[size_t(a)], nbc, nca, src[t]);
            }
        }
        part.tris.swap(out);
        cn.swap(outN);
        src.swap(outSrc);
    }
}

void applyClips(Part& part, std::vector<std::array<Vec3, 3>>& cn, std::vector<uint32_t>& src, std::vector<float>& shade) {
    src.resize(part.tris.size());
    for (size_t t = 0; t < part.tris.size(); ++t) src[t] = uint32_t(t);
    for (const ClipRegion& clip : part.clips) {
        refineNear(part, cn, src, shade, clip.field, 2);
        std::vector<float> f(part.pos.size());
        for (size_t i = 0; i < part.pos.size(); ++i) f[i] = clip.field(part.pos[i]);
        std::unordered_map<uint64_t, uint32_t> cut;
        // Where the boundary crosses edge a-b: refined on the true field (bisection), so
        // the cut follows the curve even when the field is not linear along long edges.
        std::unordered_map<uint64_t, float> kCache;
        auto kOn = [&](uint32_t a, uint32_t b) {
            const bool swap = a > b;
            if (swap) std::swap(a, b);
            uint64_t key = uint64_t(a) << 32 | b;
            auto it = kCache.find(key);
            float k;
            if (it != kCache.end()) k = it->second;
            else {
                float lo = 0, hi = 1, flo = f[a];
                for (int i = 0; i < 12; ++i) {
                    float mid = 0.5f * (lo + hi), fm = clip.field(lerp(part.pos[a], part.pos[b], mid));
                    if ((fm < 0) == (flo < 0)) lo = mid, flo = fm;
                    else hi = mid;
                }
                k = 0.5f * (lo + hi);
                kCache.emplace(key, k);
            }
            return swap ? 1.0f - k : k;
        };
        auto edge = [&](uint32_t a, uint32_t b) {
            uint64_t key = a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a);
            auto it = cut.find(key);
            if (it != cut.end()) return it->second;
            float k = kOn(a, b);
            SkinW w;
            for (int i = 0; i < 4; ++i) {
                w.add(part.w[a].j[i], part.w[a].w[i] * (1 - k));
                w.add(part.w[b].j[i], part.w[b].w[i] * k);
            }
            uint32_t n = uint32_t(part.pos.size());
            part.pos.push_back(lerp(part.pos[a], part.pos[b], k));
            part.w.push_back(w);
            part.uv.push_back(lerp(part.uv[a], part.uv[b], k));
            if (!shade.empty()) shade.push_back(lerp(shade[a], shade[b], k));
            f.push_back(0.0f);
            cut.emplace(key, n);
            return n;
        };
        std::vector<Part::Tri> out;
        std::vector<std::array<Vec3, 3>> outN;
        std::vector<uint32_t> outSrc;
        out.reserve(part.tris.size() + part.tris.size() / 8);
        for (size_t t = 0; t < part.tris.size(); ++t) {
            Part::Tri tr = part.tris[t];
            const uint32_t v[3] = {tr.a, tr.b, tr.c};
            const bool in[3] = {f[v[0]] < 0, f[v[1]] < 0, f[v[2]] < 0};
            const int nIn = int(in[0]) + int(in[1]) + int(in[2]);
            if (nIn == 0 || nIn == 3) {
                if (nIn == 3) tr.m = clip.m;
                out.push_back(tr);
                outN.push_back(cn[t]);
                outSrc.push_back(src[t]);
                continue;
            }
            // The vertex on its own side of the boundary, then the other two in winding order.
            int lone = 0;
            for (int i = 0; i < 3; ++i)
                if (in[i] == (nIn == 1)) lone = i;
            const int ib = (lone + 1) % 3, ic = (lone + 2) % 3;
            uint32_t a = v[lone], b = v[ib], c = v[ic];
            const float kab = kOn(a, b), kca = kOn(c, a);
            uint32_t ab = edge(a, b), ca = edge(c, a);
            const Vec3 na = cn[t][size_t(lone)], nb = cn[t][size_t(ib)], nc = cn[t][size_t(ic)];
            const Vec3 nab = normalize(lerp(na, nb, kab), na), nca = normalize(lerp(nc, na, kca), na);
            Mat loneM = in[lone] ? clip.m : tr.m, restM = in[lone] ? tr.m : clip.m;
            out.push_back({a, ab, ca, loneM});
            outN.push_back({na, nab, nca});
            out.push_back({ab, b, c, restM});
            outN.push_back({nab, nb, nc});
            out.push_back({ab, c, ca, restM});
            outN.push_back({nab, nc, nca});
            for (int i = 0; i < 3; ++i) outSrc.push_back(src[t]);
        }
        part.tris.swap(out);
        cn.swap(outN);
        src.swap(outSrc);
    }
}

// Curvature shading ("cavity"): how much each vertex sits below (+) or above (-)
// its neighbours relative to the surface normal, high-passed so that only local
// forms count (a groove between abdominal blocks, the pectoral shelf, a ridge of
// the clavicle), not the overall roundness of a limb. Returns a multiplier per vertex.
std::vector<float> cavityShade(const Part& part, const std::vector<Vec3>& faceN, float strength) {
    const size_t nv = part.pos.size();
    std::vector<Vec3> nrm(nv, Vec3{}), sum(nv, Vec3{});
    std::vector<float> cnt(nv, 0.0f), edge(nv, 0.0f);
    std::vector<std::vector<uint32_t>> nb(nv);
    auto link = [&](uint32_t a, uint32_t b) {
        if (std::find(nb[a].begin(), nb[a].end(), b) == nb[a].end()) nb[a].push_back(b);
    };
    for (size_t t = 0; t < part.tris.size(); ++t) {
        const auto& tr = part.tris[t];
        const uint32_t v[3] = {tr.a, tr.b, tr.c};
        for (int k = 0; k < 3; ++k) {
            nrm[v[k]] += faceN[t];
            link(v[k], v[(k + 1) % 3]);
            link(v[k], v[(k + 2) % 3]);
        }
    }
    std::vector<float> kap(nv, 0.0f);
    for (size_t i = 0; i < nv; ++i) {
        if (nb[i].empty()) continue;
        Vec3 c{};
        float e = 0;
        for (uint32_t j : nb[i]) {
            c += part.pos[j];
            e += length(part.pos[j] - part.pos[i]);
        }
        c = c / float(nb[i].size());
        e /= float(nb[i].size());
        kap[i] = e > 1e-6f ? dot(c - part.pos[i], normalize(nrm[i], {0, 1, 0})) / e : 0.0f;
    }
    std::vector<float> lo = kap, tmp(nv);
    for (int it = 0; it < 3; ++it) {
        for (size_t i = 0; i < nv; ++i) {
            float a = lo[i];
            for (uint32_t j : nb[i]) a += lo[j];
            tmp[i] = a / float(nb[i].size() + 1);
        }
        lo.swap(tmp);
    }
    std::vector<float> out(nv, 1.0f);
    for (size_t i = 0; i < nv; ++i) {
        float hp = kap[i] - lo[i];
        out[i] = hp > 0 ? 1.0f - strength * std::min(0.24f, hp * 2.0f) : 1.0f + strength * std::min(0.09f, -hp * 1.1f);
    }
    return out;
}

// Corner normals with crease detection ("auto smooth"): around each vertex, faces
// joined across an edge with a dihedral angle below the threshold share one
// angle-weighted normal; sharper edges stay crisp. cosThr <= -1 smooths everything,
// cosThr > 1 gives flat facets.
std::vector<std::array<Vec3, 3>> cornerNormals(const Part& part, const std::vector<Vec3>& faceN, float cosThr) {
    const size_t nt = part.tris.size();
    std::vector<std::array<Vec3, 3>> cn(nt);
    std::vector<std::vector<std::pair<uint32_t, int>>> inc(part.pos.size());  // vertex -> (tri, corner)
    for (size_t t = 0; t < nt; ++t) {
        const auto& tr = part.tris[t];
        inc[tr.a].push_back({uint32_t(t), 0});
        inc[tr.b].push_back({uint32_t(t), 1});
        inc[tr.c].push_back({uint32_t(t), 2});
    }
    auto vert = [&](uint32_t t, int k) {
        const auto& tr = part.tris[t];
        return k == 0 ? tr.a : k == 1 ? tr.b : tr.c;
    };
    auto angleAt = [&](uint32_t t, int k) {
        Vec3 p = part.pos[vert(t, k)], q = part.pos[vert(t, (k + 1) % 3)], r = part.pos[vert(t, (k + 2) % 3)];
        Vec3 e1 = normalize(q - p, {0, 0, 0}), e2 = normalize(r - p, {0, 0, 0});
        return std::acos(clamp(dot(e1, e2), -1.0f, 1.0f));
    };
    std::vector<int> group;
    std::vector<Vec3> sum;
    for (size_t v = 0; v < inc.size(); ++v) {
        const auto& fan = inc[v];
        const size_t n = fan.size();
        if (n == 0) continue;
        group.resize(n);
        for (size_t i = 0; i < n; ++i) group[i] = int(i);
        auto root = [&](int i) {
            while (group[size_t(i)] != i) i = group[size_t(i)] = group[size_t(group[size_t(i)])];
            return i;
        };
        const float thr = part.smoothAt && cosThr <= 1.0f && part.smoothAt(part.pos[v]) ? -1.0f : cosThr;
        if (thr <= 1.0f)
            for (size_t i = 0; i < n; ++i)
                for (size_t j = i + 1; j < n; ++j) {
                    const auto [ti, ki] = fan[i];
                    const auto [tj, kj] = fan[j];
                    if (thr > -1.0f) {
                        if (dot(faceN[ti], faceN[tj]) < thr) continue;
                        uint32_t wi1 = vert(ti, (ki + 1) % 3), wi2 = vert(ti, (ki + 2) % 3);
                        uint32_t wj1 = vert(tj, (kj + 1) % 3), wj2 = vert(tj, (kj + 2) % 3);
                        if (!(wi1 == wj1 || wi1 == wj2 || wi2 == wj1 || wi2 == wj2)) continue;  // not edge neighbours
                    }
                    group[size_t(root(int(i)))] = root(int(j));
                }
        sum.assign(n, Vec3{});
        for (size_t i = 0; i < n; ++i) sum[size_t(root(int(i)))] += faceN[fan[i].first] * angleAt(fan[i].first, fan[i].second);
        for (size_t i = 0; i < n; ++i)
            cn[fan[i].first][size_t(fan[i].second)] = normalize(sum[size_t(root(int(i)))], faceN[fan[i].first]);
    }
    return cn;
}

}  // namespace

void finalizeParts(std::vector<Part>& parts, Mesh& mesh, bool faceted, float creaseDeg) {
    const SubmeshKind order[] = {SubmeshKind::Body, SubmeshKind::FaceSkin, SubmeshKind::FaceEyes, SubmeshKind::FaceMouth};
    auto quant = [](Vec3 n) {
        auto q = [](float c) { return uint64_t(std::lround((c + 1.0f) * 1023.0f)) & 0x7FF; };
        return q(n.x) | (q(n.y) << 11) | (q(n.z) << 22);
    };
    for (SubmeshKind kind : order) {
        Submesh sm{kind, uint32_t(mesh.indices.size()), 0};
        for (Part& part : parts) {
            if (part.kind != kind || part.tris.empty()) continue;
            // Faceted style: every body surface (head and hair too) is flat; decals stay smooth.
            const bool flat = faceted ? kind == SubmeshKind::Body : part.flat;
            std::vector<Vec3> faceN(part.tris.size());
            for (size_t t = 0; t < part.tris.size(); ++t) {
                const auto& tr = part.tris[t];
                Vec3 a = part.pos[tr.a], b = part.pos[tr.b], c = part.pos[tr.c];
                faceN[t] = normalize(cross(b - a, c - a), {0, 0, 0});
            }
            // flat: every face its own normal; smoothAlways: no creases; else the crease angle.
            const float cosThr = flat ? 2.0f : part.smoothAlways || creaseDeg >= 179.0f ? -1.0f : std::cos(creaseDeg * kPi / 180.0f);
            std::vector<std::array<Vec3, 3>> cn = cornerNormals(part, faceN, cosThr);
            std::vector<uint32_t> src;
            std::vector<float> shade = part.cavity > 0 ? cavityShade(part, faceN, part.cavity) : std::vector<float>{};
            applyClips(part, cn, src, shade);
            // Weld on (logical vertex, normal, material).
            std::unordered_map<uint64_t, std::vector<uint32_t>> weld;
            weld.reserve(part.tris.size() * 2);
            for (size_t t = 0; t < part.tris.size(); ++t) {
                const auto& tr = part.tris[t];
                const Vec3 fn = faceN[src[t]];
                if (fn.x == 0 && fn.y == 0 && fn.z == 0) continue;  // degenerate
                const uint32_t pm = packMat(tr.m);
                const uint8_t alpha = kind == SubmeshKind::Body ? uint8_t(uint32_t(tr.m.cls) * 32 + 16) : uint8_t(255);
                const uint32_t lis[3] = {tr.a, tr.b, tr.c};
                for (int k = 0; k < 3; ++k) {
                    const uint32_t li = lis[k];
                    Vec3 nrm = cn[t][size_t(k)];
                    // normalFn also applies to flat facets: it is evaluated at the face centre so
                    // every face keeps a single (edited) normal.
                    if (part.normalFn) {
                        if (!flat) nrm = part.normalFn(part.pos[li], nrm);
                        else nrm = part.normalFn((part.pos[tr.a] + part.pos[tr.b] + part.pos[tr.c]) / 3.0f, nrm);
                    }
                    const uint64_t key = (uint64_t(li) << 33) ^ quant(nrm);
                    auto& bucket = weld[key];
                    // vertex colour = material colour shaded by curvature
                    // (grooves take a warm, reddish darkening like skin in shadow)
                    const float sh = shade.empty() ? 1.0f : shade[li];
                    const float dk = std::max(0.0f, 1.0f - sh), lt = std::max(0.0f, sh - 1.0f);
                    auto ch = [&](uint32_t c, float warm) {
                        float f = 1.0f - dk * warm + lt;
                        return uint8_t(std::clamp(int(std::lround(float(c & 0xFF) * f)), 0, 255));
                    };
                    const uint8_t c0 = ch(pm, 0.70f), c1 = ch(pm >> 8, 1.0f), c2 = ch(pm >> 16, 1.05f);
                    uint32_t found = UINT32_MAX;
                    for (uint32_t vi : bucket) {
                        const Vertex& o = mesh.vertices[vi];
                        if (o.color[0] == c0 && o.color[1] == c1 && o.color[2] == c2 && o.color[3] == alpha) found = vi;
                    }
                    if (found == UINT32_MAX) {
                        Vertex v{};
                        v.pos = part.pos[li];
                        v.normal = nrm;
                        v.uv = part.uv[li];
                        v.color[0] = c0;
                        v.color[1] = c1;
                        v.color[2] = c2;
                        v.color[3] = alpha;
                        SkinW sw = part.w[li];
                        sw.normalize();
                        for (int q = 0; q < 4; ++q) {
                            v.joints[q] = uint8_t(sw.w[q] > 0 ? sw.j[q] : 0);
                            v.weights[q] = sw.w[q];
                        }
                        found = uint32_t(mesh.vertices.size());
                        bucket.push_back(found);
                        mesh.vertices.push_back(v);
                        mesh.partIds.push_back(part.id);
                    }
                    mesh.indices.push_back(found);
                }
            }
        }
        sm.indexCount = uint32_t(mesh.indices.size()) - sm.firstIndex;
        mesh.submeshes.push_back(sm);
    }
}

}  // namespace cg::detail
