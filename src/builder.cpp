#include "builder.hpp"

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
            part.tris.push_back({a, b, c, m});
            part.tris.push_back({a, c, d, m});
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

void finalizeParts(std::vector<Part>& parts, Mesh& mesh, bool faceted) {
    const SubmeshKind order[] = {SubmeshKind::Body, SubmeshKind::FaceEyes, SubmeshKind::FaceMouth};
    for (SubmeshKind kind : order) {
        Submesh sm{kind, uint32_t(mesh.indices.size()), 0};
        for (Part& part : parts) {
            if (part.kind != kind || part.tris.empty()) continue;
            const bool flat = faceted || part.flat;
            // Angle-weighted smooth normals per logical vertex.
            std::vector<Vec3> nrm(part.pos.size(), Vec3{});
            std::vector<Vec3> faceN(part.tris.size());
            for (size_t t = 0; t < part.tris.size(); ++t) {
                const auto& tr = part.tris[t];
                Vec3 a = part.pos[tr.a], b = part.pos[tr.b], c = part.pos[tr.c];
                Vec3 n = cross(b - a, c - a);
                faceN[t] = normalize(n, {0, 0, 0});
                if (flat) continue;
                auto ang = [](Vec3 p, Vec3 q, Vec3 r) {
                    Vec3 e1 = normalize(q - p, {0, 0, 0}), e2 = normalize(r - p, {0, 0, 0});
                    return std::acos(clamp(dot(e1, e2), -1.0f, 1.0f));
                };
                nrm[tr.a] += faceN[t] * ang(a, b, c);
                nrm[tr.b] += faceN[t] * ang(b, c, a);
                nrm[tr.c] += faceN[t] * ang(c, a, b);
            }
            // Weld on (logical vertex, material[, face]).
            std::unordered_map<uint64_t, uint32_t> weld;
            weld.reserve(part.tris.size() * 2);
            for (size_t t = 0; t < part.tris.size(); ++t) {
                const auto& tr = part.tris[t];
                if (faceN[t].x == 0 && faceN[t].y == 0 && faceN[t].z == 0) continue;  // degenerate
                uint32_t pm = packMat(tr.m);
                for (uint32_t li : {tr.a, tr.b, tr.c}) {
                    uint64_t key = flat ? (uint64_t(t) << 32 | li) : (uint64_t(pm) << 32 | li);
                    auto it = weld.find(key);
                    if (it == weld.end()) {
                        Vertex v{};
                        v.pos = part.pos[li];
                        v.normal = flat ? faceN[t] : normalize(nrm[li]);
                        if (part.normalFn && !flat) v.normal = part.normalFn(v.pos, v.normal);
                        v.uv = part.uv[li];
                        v.color[0] = uint8_t(pm & 0xFF);
                        v.color[1] = uint8_t((pm >> 8) & 0xFF);
                        v.color[2] = uint8_t((pm >> 16) & 0xFF);
                        v.color[3] = kind == SubmeshKind::Body ? uint8_t(uint32_t(tr.m.cls) * 32 + 16) : uint8_t(255);
                        SkinW sw = part.w[li];
                        sw.normalize();
                        for (int k = 0; k < 4; ++k) {
                            v.joints[k] = uint8_t(sw.w[k] > 0 ? sw.j[k] : 0);
                            v.weights[k] = sw.w[k];
                        }
                        it = weld.emplace(key, uint32_t(mesh.vertices.size())).first;
                        mesh.vertices.push_back(v);
                        mesh.partIds.push_back(part.id);
                    }
                    mesh.indices.push_back(it->second);
                }
            }
        }
        sm.indexCount = uint32_t(mesh.indices.size()) - sm.firstIndex;
        mesh.submeshes.push_back(sm);
    }
}

}  // namespace cg::detail
