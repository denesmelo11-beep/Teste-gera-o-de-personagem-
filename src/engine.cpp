#include "chargen/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>

#include "chargen/rng.hpp"

namespace cg {

namespace {

constexpr char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford: no I, L, O, U

int b32Value(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    if (ch == 'O') ch = '0';
    if (ch == 'I' || ch == 'L') ch = '1';
    for (int i = 0; i < 32; ++i)
        if (kB32[i] == ch) return i;
    return -1;
}

uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

float percentile(std::vector<float>& v, float q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(q * float(v.size() - 1) + 0.5f));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

Vec3 closestOnSegment(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 ab = b - a;
    const float l2 = dot(ab, ab);
    if (l2 < 1e-12f) return a;
    return a + ab * saturate(dot(p - a, ab) / l2);
}

// Ray (unit dir) against a capsule; -1 on a miss. (Inigo Quilez's formulation.)
float rayCapsule(Vec3 ro, Vec3 rd, Vec3 pa, Vec3 pb, float r) {
    const Vec3 ba = pb - pa, oa = ro - pa;
    const float baba = dot(ba, ba);
    auto sphere = [&](Vec3 c) {
        const Vec3 oc = ro - c;
        const float b = dot(rd, oc), cc = dot(oc, oc) - r * r, h = b * b - cc;
        if (h < 0) return -1.0f;
        const float t = -b - std::sqrt(h);
        return t >= 0 ? t : (cc <= 0 ? 0.0f : -1.0f);
    };
    if (baba < 1e-10f) return sphere(pa);
    const float bard = dot(ba, rd), baoa = dot(ba, oa), rdoa = dot(rd, oa), oaoa = dot(oa, oa);
    const float a = baba - bard * bard, b = baba * rdoa - baoa * bard, c = baba * oaoa - baoa * baoa - r * r * baba;
    const float h = b * b - a * c;
    if (h >= 0 && std::fabs(a) > 1e-12f) {
        const float t = (-b - std::sqrt(h)) / a;
        const float y = baoa + t * bard;
        if (y > 0 && y < baba && t >= 0) return t;
    }
    const float t0 = sphere(pa), t1 = sphere(pb);
    if (t0 < 0) return t1;
    if (t1 < 0) return t0;
    return std::min(t0, t1);
}

AgeGroup groupOfAge(float age) {
    return age < 23 ? AgeGroup::Young : age < 40 ? AgeGroup::Adult : age < 60 ? AgeGroup::Mature : AgeGroup::Elder;
}

constexpr std::string_view kZoneNames[] = {"head", "neck", "chest", "abdomen", "pelvis", "upper_arm",
                                           "forearm", "hand", "thigh", "shin", "foot", "tail", "wing", "tentacle"};
static_assert(std::size(kZoneNames) == size_t(HitZone::Count));
constexpr float kZoneDamage[] = {2.0f, 1.6f, 1.0f, 1.1f, 0.9f, 0.7f, 0.6f, 0.45f, 0.8f, 0.6f, 0.45f, 0.4f, 0.5f, 0.45f};
static_assert(std::size(kZoneDamage) == size_t(HitZone::Count));
constexpr std::string_view kStageNames[] = {"alive", "fresh", "rotting", "putrefied", "skeletal"};
static_assert(std::size(kStageNames) == size_t(DecayStage::Count));

}  // namespace

// ---------------------------------------------------------------- keys & saves

std::string toBase32(const std::vector<uint8_t>& bytes) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : bytes) {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 5) {
            out += kB32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits > 0) out += kB32[(acc << (5 - bits)) & 31u];
    return out;
}

std::optional<std::vector<uint8_t>> fromBase32(std::string_view text) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : text) {
        if (ch == '-' || ch == ' ') continue;
        const int v = b32Value(ch);
        if (v < 0) return std::nullopt;
        acc = (acc << 5) | uint32_t(v);
        bits += 5;
        if (bits >= 8) {
            out.push_back(uint8_t(acc >> (bits - 8)));
            bits -= 8;
        }
    }
    return out;
}

std::string encodeKey(const CharacterKey& k) {
    std::vector<uint8_t> b;
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t(k.seed >> (8 * i)));
    const GenomeOptions& o = k.options;
    b.push_back(o.archetype ? uint8_t(*o.archetype) : 0xFF);
    b.push_back(o.sex ? uint8_t(*o.sex) : 0xFF);
    b.push_back(o.age ? uint8_t(*o.age) : 0xFF);
    b.push_back(o.persona ? uint8_t(*o.persona) : 0xFF);
    b.push_back(o.hair ? uint8_t(*o.hair) : 0xFF);
    b.push_back(o.species ? uint8_t(*o.species) : 0xFF);
    b.push_back(o.armPairs ? uint8_t(std::clamp(*o.armPairs, 1, 2)) : 0);
    b.push_back(o.mixedSpecies ? 1 : 0);
    b.push_back(uint8_t(std::lround(clamp(o.exaggeration, 0.0f, 2.55f) * 100.0f)));
    b.push_back(uint8_t(std::lround(saturate(k.decay) * 255.0f)));
    b.push_back(uint8_t(std::clamp(k.lod, 0, 2)));
    b.push_back(uint8_t(fnv1a(b.data(), b.size())));
    return "CG1-" + toBase32(b);
}

std::optional<CharacterKey> decodeKey(std::string_view text) {
    if (text.size() < 4 || text.substr(0, 4) != "CG1-") return std::nullopt;
    auto bytes = fromBase32(text.substr(4));
    if (!bytes || bytes->size() < 20) return std::nullopt;
    const auto& b = *bytes;
    if (uint8_t(fnv1a(b.data(), 19)) != b[19]) return std::nullopt;
    CharacterKey k;
    for (int i = 0; i < 8; ++i) k.seed |= uint64_t(b[size_t(i)]) << (8 * i);
    GenomeOptions& o = k.options;
    auto opt = [&](size_t i, int count, auto tag) -> std::optional<decltype(tag)> {
        if (b[i] == 0xFF || b[i] >= count) return std::nullopt;
        return decltype(tag)(b[i]);
    };
    o.archetype = opt(8, int(Archetype::Count), Archetype{});
    o.sex = opt(9, 2, Sex{});
    o.age = opt(10, int(AgeGroup::Count), AgeGroup{});
    o.persona = opt(11, int(Persona::Count), Persona{});
    o.hair = opt(12, int(HairStyle::Count), HairStyle{});
    o.species = opt(13, int(Species::Count), Species{});
    if (b[14]) o.armPairs = int(b[14]);
    o.mixedSpecies = b[15] & 1;
    o.exaggeration = float(b[16]) / 100.0f;
    k.decay = float(b[17]) / 255.0f;
    k.lod = std::min(int(b[18]), 2);
    return k;
}

Genome genomeFromKey(const CharacterKey& k) { return randomGenome(k.seed, k.options); }

Character generateFromKey(const CharacterKey& k, GenOptions base) {
    base.decay = k.decay;
    base.lod = k.lod;
    return generateCharacter(genomeFromKey(k), base);
}

static_assert(std::is_trivially_copyable_v<Genome>, "saveGenome copies the genome as raw bytes");
namespace {
constexpr uint8_t kGenomeMagic[4] = {'C', 'G', 'G', 1};
}

std::vector<uint8_t> saveGenome(const Genome& g) {
    std::vector<uint8_t> out(kGenomeMagic, kGenomeMagic + 4);
    const uint32_t n = sizeof(Genome);
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(n >> (8 * i)));
    const size_t at = out.size();
    out.resize(at + n);
    std::memcpy(out.data() + at, &g, n);
    const uint32_t h = fnv1a(out.data() + at, n);
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(h >> (8 * i)));
    return out;
}

std::optional<Genome> loadGenome(const std::vector<uint8_t>& bytes) {
    // Snapshots from before HeadForm existed are a prefix of the current layout: they load
    // with the default (human) skull and classic eyes, exactly as they were drawn then.
    constexpr uint32_t kPreHead = uint32_t(offsetof(Genome, head));
    if (bytes.size() < 12 || std::memcmp(bytes.data(), kGenomeMagic, 4) != 0) return std::nullopt;
    uint32_t n = 0, h = 0;
    for (int i = 0; i < 4; ++i) n |= uint32_t(bytes[size_t(4 + i)]) << (8 * i);
    if ((n != sizeof(Genome) && n != kPreHead) || bytes.size() != 8 + size_t(n) + 4) return std::nullopt;
    for (int i = 0; i < 4; ++i) h |= uint32_t(bytes[8 + n + size_t(i)]) << (8 * i);
    if (fnv1a(bytes.data() + 8, n) != h) return std::nullopt;
    Genome g;
    std::memcpy(&g, bytes.data() + 8, n);
    return g;
}

// ---------------------------------------------------------------- head silhouettes

HeadSilhouette headSilhouette(const Character& c, int res) {
    HeadSilhouette out;
    out.res = res;
    out.front.assign(size_t(res * res), 0);
    out.side.assign(size_t(res * res), 0);
    const int head = c.rig.head;
    if (head < 0) return out;
    const float h = c.height / c.genome.body.headsTall;
    // The head's frame: the pivot sits 0.14 h above the chin and 0.10 h behind the face's
    // origin (see the builder); the frame spans the head with room for snouts and ears.
    const Vec3 o = c.skeleton.bones[size_t(head)].restWorld - Vec3{0, 0.14f * h, -0.10f * h};
    const float y0 = -0.35f, y1 = 1.45f, x0 = -0.90f, x1 = 0.90f, z0 = -0.80f, z1 = 1.00f;
    const Mesh& m = c.mesh;
    auto onHead = [&](uint32_t i) {
        const Vertex& v = m.vertices[i];
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (v.weights[k] > v.weights[best]) best = k;
        return v.joints[best] == head && v.weights[best] > 0.5f;
    };
    auto fill = [&](std::vector<uint8_t>& mask, Vec2 a, Vec2 b, Vec2 d) {
        const float lo_x = std::min({a.x, b.x, d.x}), hi_x = std::max({a.x, b.x, d.x});
        const float lo_y = std::min({a.y, b.y, d.y}), hi_y = std::max({a.y, b.y, d.y});
        const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
        if (std::fabs(area) < 1e-9f) return;
        for (int py = std::max(0, int(lo_y)); py <= std::min(res - 1, int(hi_y)); ++py)
            for (int px = std::max(0, int(lo_x)); px <= std::min(res - 1, int(hi_x)); ++px) {
                const Vec2 p{float(px) + 0.5f, float(py) + 0.5f};
                const float w0 = ((b.x - p.x) * (d.y - p.y) - (b.y - p.y) * (d.x - p.x)) / area;
                const float w1 = ((d.x - p.x) * (a.y - p.y) - (d.y - p.y) * (a.x - p.x)) / area;
                if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1) mask[size_t(py * res + px)] = 1;
            }
    };
    const float R = float(res);
    for (const Submesh& sm : m.submeshes) {
        if (sm.kind != SubmeshKind::Body) continue;
        for (uint32_t t = sm.firstIndex; t + 2 < sm.firstIndex + sm.indexCount; t += 3) {
            const uint32_t ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
            if (!onHead(ia) || !onHead(ib) || !onHead(ic)) continue;
            Vec3 q[3] = {m.vertices[ia].pos - o, m.vertices[ib].pos - o, m.vertices[ic].pos - o};
            Vec2 fr[3], sd[3];
            for (int k = 0; k < 3; ++k) {
                const Vec3 u = q[k] / h;
                const float py = (y1 - u.y) / (y1 - y0) * R;
                fr[k] = {(u.x - x0) / (x1 - x0) * R, py};
                sd[k] = {(u.z - z0) / (z1 - z0) * R, py};
            }
            fill(out.front, fr[0], fr[1], fr[2]);
            fill(out.side, sd[0], sd[1], sd[2]);
        }
    }
    return out;
}

float silhouetteSimilarity(const HeadSilhouette& a, const HeadSilhouette& b) {
    if (a.res != b.res) return 0.0f;
    auto iou = [](const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) {
        int in = 0, un = 0;
        for (size_t i = 0; i < x.size(); ++i) in += x[i] && y[i], un += x[i] || y[i];
        return un ? float(in) / float(un) : 1.0f;
    };
    return 0.5f * (iou(a.front, b.front) + iou(a.side, b.side));
}

// ---------------------------------------------------------------- hit volumes

std::string_view toString(HitZone z) { return z < HitZone::Count ? kZoneNames[size_t(z)] : "?"; }

std::vector<HitVolume> hitVolumes(const Character& c) {
    const Skeleton& sk = c.skeleton;
    const RigInfo& r = c.rig;
    const size_t nb = sk.bones.size();
    // Which bones carry a volume, and the zone of each.
    std::vector<int> zoneOf(nb, -1);
    auto set = [&](int b, HitZone z) {
        if (b >= 0 && size_t(b) < nb) zoneOf[size_t(b)] = int(z);
    };
    set(r.head, HitZone::Head);
    set(r.neck, HitZone::Neck);
    set(r.chest, HitZone::Chest);
    set(r.spine, HitZone::Abdomen);
    set(r.hips, HitZone::Pelvis);
    for (const LimbRig& a : r.arms) set(a.upper, HitZone::UpperArm), set(a.lower, HitZone::Forearm), set(a.end, HitZone::Hand);
    for (const LimbRig& l : r.legs) set(l.upper, HitZone::Thigh), set(l.lower, HitZone::Shin), set(l.end, HitZone::Foot);
    for (int t : r.tail) set(t, HitZone::Tail);
    for (int t : r.serpent) set(t, HitZone::Tail);
    for (const LimbRig& w : r.wings) set(w.upper, HitZone::Wing), set(w.lower, HitZone::Wing), set(w.end, HitZone::Wing);
    for (const auto& t : r.tentacles)
        for (int b : t) set(b, HitZone::Tentacle);
    // Every other bone hands its vertices to its nearest volume-carrying ancestor
    // (fingers to the hand, bust to the chest, jaw to the head...).
    std::vector<int> owner(nb, -1);
    for (size_t i = 0; i < nb; ++i) {
        int b = int(i);
        while (b >= 0 && zoneOf[size_t(b)] < 0) b = sk.bones[size_t(b)].parent;
        owner[i] = b;
    }
    // Body surface only (no decals), and no hair: hits land on flesh.
    std::vector<std::vector<Vec3>> pts(nb);
    const Mesh& m = c.mesh;
    std::vector<uint8_t> used(m.vertices.size(), 0);
    for (const Submesh& s : m.submeshes) {
        if (s.kind != SubmeshKind::Body) continue;
        for (uint32_t i = s.firstIndex; i < s.firstIndex + s.indexCount; ++i) used[m.indices[i]] = 1;
    }
    // Hair on spring chains is not a target (fur and beards on the body are).
    std::vector<uint8_t> hairBone(nb, 0);
    for (const auto& chain : r.hairChains)
        for (int b : chain)
            if (b >= 0 && size_t(b) < nb) hairBone[size_t(b)] = 1;
    for (size_t vi = 0; vi < m.vertices.size(); ++vi) {
        if (!used[vi]) continue;
        const Vertex& v = m.vertices[vi];
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (v.weights[k] > v.weights[best]) best = k;
        if (hairBone[v.joints[best]]) continue;
        const int o = owner[v.joints[best]];
        if (o >= 0) pts[size_t(o)].push_back(v.pos);
    }
    std::vector<HitVolume> out;
    for (size_t b = 0; b < nb; ++b) {
        if (zoneOf[b] < 0 || pts[b].size() < 4) continue;
        const auto& P = pts[b];
        // Principal axis of the vertices (power iteration on the covariance).
        Vec3 mean{};
        for (Vec3 p : P) mean += p;
        mean = mean * (1.0f / float(P.size()));
        float cv[3][3] = {};
        for (Vec3 p : P) {
            const float d[3] = {p.x - mean.x, p.y - mean.y, p.z - mean.z};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) cv[i][j] += d[i] * d[j];
        }
        Vec3 ax{0.3f, 1.0f, 0.2f};
        for (int it = 0; it < 24; ++it) {
            Vec3 n{cv[0][0] * ax.x + cv[0][1] * ax.y + cv[0][2] * ax.z, cv[1][0] * ax.x + cv[1][1] * ax.y + cv[1][2] * ax.z,
                   cv[2][0] * ax.x + cv[2][1] * ax.y + cv[2][2] * ax.z};
            ax = normalize(n, ax);
        }
        std::vector<float> proj, dist;
        proj.reserve(P.size());
        for (Vec3 p : P) proj.push_back(dot(p - mean, ax));
        std::vector<float> pr = proj;
        const float lo = percentile(pr, 0.06f), hi = percentile(pr, 0.94f);
        for (size_t i = 0; i < P.size(); ++i) dist.push_back(length(P[i] - (mean + ax * proj[i])));
        const float rad = std::max(0.01f, percentile(dist, 0.80f));
        float a = lo + rad, e = hi - rad;
        if (a > e) a = e = 0.5f * (lo + hi);
        HitVolume hv;
        hv.bone = int(b);
        hv.zone = HitZone(zoneOf[b]);
        const Vec3 o = sk.bones[b].restWorld;
        hv.a = mean + ax * a - o;
        hv.b = mean + ax * e - o;
        hv.radius = rad;
        hv.damageScale = kZoneDamage[size_t(zoneOf[b])];
        hv.side = std::fabs(mean.x) > 0.03f ? (mean.x > 0 ? 1.0f : -1.0f) : 0.0f;
        out.push_back(hv);
    }
    return out;
}

RayHit raycast(const std::vector<HitVolume>& vols, const std::vector<Mat4>& global, Vec3 origin, Vec3 dir, float maxDist) {
    RayHit best;
    best.t = maxDist;
    dir = normalize(dir, {0, 0, 1});
    for (size_t i = 0; i < vols.size(); ++i) {
        const HitVolume& v = vols[i];
        const Mat4& M = global[size_t(v.bone)];
        const float t = rayCapsule(origin, dir, M.point(v.a), M.point(v.b), v.radius);
        if (t >= 0 && t < best.t) best.t = t, best.volume = int(i);
    }
    if (best.volume >= 0) best.point = origin + dir * best.t;
    return best;
}

std::vector<int> overlapSphere(const std::vector<HitVolume>& vols, const std::vector<Mat4>& global, Vec3 center, float radius) {
    std::vector<std::pair<float, int>> hits;
    for (size_t i = 0; i < vols.size(); ++i) {
        const HitVolume& v = vols[i];
        const Mat4& M = global[size_t(v.bone)];
        const float d = length(center - closestOnSegment(center, M.point(v.a), M.point(v.b))) - v.radius;
        if (d <= radius) hits.push_back({d, int(i)});
    }
    std::sort(hits.begin(), hits.end());
    std::vector<int> out;
    for (auto& h : hits) out.push_back(h.second);
    return out;
}

// ---------------------------------------------------------------- metrics

BodyMetrics measure(const Character& c) {
    BodyMetrics bm;
    const Skeleton& sk = c.skeleton;
    const RigInfo& r = c.rig;
    auto P = [&](int b) { return b >= 0 ? sk.bones[size_t(b)].restWorld : Vec3{}; };
    bm.height = c.height;
    bm.arms = int(r.arms.size());
    bm.quadruped = c.genome.body.centaur || c.genome.body.stance == 1;
    bm.boundsMin = {1e9f, 1e9f, 1e9f};
    bm.boundsMax = {-1e9f, -1e9f, -1e9f};
    for (const Vertex& v : c.mesh.vertices) {
        bm.boundsMin = {std::min(bm.boundsMin.x, v.pos.x), std::min(bm.boundsMin.y, v.pos.y), std::min(bm.boundsMin.z, v.pos.z)};
        bm.boundsMax = {std::max(bm.boundsMax.x, v.pos.x), std::max(bm.boundsMax.y, v.pos.y), std::max(bm.boundsMax.z, v.pos.z)};
    }
    // Eye level: the eye decal itself (or a fraction of the head when there is none).
    double ey = 0;
    size_t en = 0;
    for (const Submesh& s : c.mesh.submeshes)
        if (s.kind == SubmeshKind::FaceEyes)
            for (uint32_t i = s.firstIndex; i < s.firstIndex + s.indexCount; ++i) ey += c.mesh.vertices[c.mesh.indices[i]].pos.y, ++en;
    bm.eyeHeight = en ? float(ey / double(en)) : lerp(P(r.head).y, c.height, 0.45f);
    if (r.arms.size() >= 2) {
        const LimbRig& a = r.arms[0];
        bm.shoulderHeight = P(a.upper).y;
        bm.shoulderSpan = length(P(r.arms[0].upper) - P(r.arms[1].upper));
        bm.reach = length(P(a.lower) - P(a.upper)) + length(P(a.end) - P(a.lower)) +
                   (a.tip >= 0 ? 1.6f * length(P(a.tip) - P(a.end)) : 0.10f * c.height);
    }
    if (!r.legs.empty()) {
        bm.hipHeight = P(r.legs[0].upper).y;
        bm.legLength = bm.hipHeight;
    }
    // Mass from the fitted volumes (flesh ~1010 kg/m^3; capsules overlap a little at
    // the joints and miss the hollows, which roughly cancel).
    const auto vols = hitVolumes(c);
    double vol = 0;
    float torsoR = 0;
    for (const HitVolume& v : vols) {
        const float L = length(v.b - v.a), rr = v.radius;
        vol += kPi * rr * rr * L + 4.0f / 3.0f * kPi * rr * rr * rr;
        if (v.zone == HitZone::Chest || v.zone == HitZone::Abdomen || v.zone == HitZone::Pelvis) {
            const Vec3 o = P(v.bone);
            for (Vec3 e : {v.a + o, v.b + o}) torsoR = std::max(torsoR, std::max(std::fabs(e.x), std::fabs(e.z - o.z)) + rr);
        }
    }
    bm.mass = float(vol) * 1010.0f * 0.90f;
    // Natural walking pace at Froude number ~0.25, running ~2.2.
    const float L = std::max(0.3f, bm.legLength);
    bm.walkSpeed = std::sqrt(0.25f * 9.81f * L) * (bm.quadruped ? 1.25f : 1.0f);
    bm.runSpeed = std::sqrt(2.2f * 9.81f * L) * (bm.quadruped ? 1.35f : 1.0f);
    bm.capsuleRadius = std::max(torsoR, 0.45f * bm.shoulderSpan);
    if (bm.quadruped) bm.capsuleRadius = std::max(bm.capsuleRadius, 0.5f * (bm.boundsMax.z - bm.boundsMin.z) * 0.8f);
    bm.capsuleHeight = std::max(bm.height, 2.0f * bm.capsuleRadius);
    return bm;
}

// ---------------------------------------------------------------- sockets

Mat4 socketMatrix(const Character& c, const std::vector<Mat4>& global, Socket s) {
    const RigInfo& r = c.rig;
    int b = -1;
    switch (s) {
        case Socket::HandR: b = r.socketHandR; break;
        case Socket::HandL: b = r.socketHandL; break;
        case Socket::Back: b = r.socketBack; break;
        case Socket::Hip: b = r.socketHip; break;
        case Socket::Head: b = r.head; break;
        default: break;
    }
    if (b < 0 || size_t(b) >= global.size()) return Mat4::identity();
    if (s == Socket::Head) {  // helmets sit on the middle of the skull
        const float up = 0.5f * (c.height - c.skeleton.bones[size_t(b)].restWorld.y);
        return global[size_t(b)] * Mat4::translation({0, up, 0});
    }
    return global[size_t(b)];
}

// ---------------------------------------------------------------- packed mesh

PackedMesh packMesh(const Mesh& m) {
    PackedMesh out;
    out.submeshes = m.submeshes;
    out.vertices.resize(m.vertices.size());
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const Vertex& v = m.vertices[i];
        PackedVertex& p = out.vertices[i];
        p.pos[0] = v.pos.x, p.pos[1] = v.pos.y, p.pos[2] = v.pos.z;
        p.normal[0] = int8_t(std::lround(clamp(v.normal.x, -1, 1) * 127.0f));
        p.normal[1] = int8_t(std::lround(clamp(v.normal.y, -1, 1) * 127.0f));
        p.normal[2] = int8_t(std::lround(clamp(v.normal.z, -1, 1) * 127.0f));
        p.normal[3] = 0;
        p.uv[0] = uint16_t(std::lround(saturate(v.uv.x) * 65535.0f));
        p.uv[1] = uint16_t(std::lround(saturate(v.uv.y) * 65535.0f));
        std::memcpy(p.color, v.color, 4);
        std::memcpy(p.joints, v.joints, 4);
        // Quantise weights so they sum to exactly 255 (largest remainders get the rest).
        int q[4], sum = 0;
        float rem[4];
        for (int k = 0; k < 4; ++k) {
            const float w = saturate(v.weights[k]) * 255.0f;
            q[k] = int(w);
            rem[k] = w - float(q[k]);
            sum += q[k];
        }
        while (sum < 255) {
            int bi = 0;
            for (int k = 1; k < 4; ++k)
                if (rem[k] > rem[bi]) bi = k;
            ++q[bi], ++sum;
            rem[bi] = -1;
            if (rem[0] < 0 && rem[1] < 0 && rem[2] < 0 && rem[3] < 0) break;
        }
        if (sum != 255) q[0] += 255 - sum;
        for (int k = 0; k < 4; ++k) p.weights[k] = uint8_t(std::clamp(q[k], 0, 255));
    }
    if (m.vertices.size() <= 65535) out.indices16.assign(m.indices.begin(), m.indices.end());
    else out.indices32 = m.indices;
    return out;
}

// ---------------------------------------------------------------- death & decay

std::string_view toString(DecayStage s) { return s < DecayStage::Count ? kStageNames[size_t(s)] : "?"; }

DecayStage decayStage(float d) {
    // Matches the builder: torn open onto the skeleton from 0.3, bare hands and feet
    // from 0.85, nothing but bones from 0.97.
    if (d <= 0.0f) return DecayStage::Alive;
    if (d < 0.30f) return DecayStage::Fresh;
    if (d < 0.60f) return DecayStage::Rotting;
    if (d < 0.97f) return DecayStage::Putrefied;
    return DecayStage::Skeletal;
}

float decayFromTime(float hours, float daysToBones) {
    if (hours <= 0.0f) return 0.0f;
    const float x = hours / (24.0f * std::max(0.1f, daysToBones));
    // Fast early changes (pallor, clouded eyes within a day), long tail to clean bones.
    return x >= 1.0f ? 1.0f : std::pow(x, 0.6f);
}

float decayBucket(float decay, int steps) {
    if (decay <= 0.0f) return 0.0f;
    if (decay >= 0.97f) return 1.0f;
    steps = std::max(steps, 2);
    // Never round a corpse back to alive, and keep the builder's thresholds exact.
    float b = std::ceil(decay * float(steps)) / float(steps);
    for (float th : {0.30f, 0.85f, 0.97f})
        if (decay < th && b >= th) b = th - 1e-3f;
    return std::min(b, 0.96f);
}

bool needsRebuild(float builtDecay, float decay, int steps) {
    return decayBucket(builtDecay, steps) != decayBucket(decay, steps) || decayStage(builtDecay) != decayStage(decay);
}

// ---------------------------------------------------------------- LOD

float screenHeightPx(float worldHeight, float distance, float fovY, int viewportHeight) {
    distance = std::max(distance, 1e-3f);
    return worldHeight / (2.0f * distance * std::tan(0.5f * fovY)) * float(viewportHeight);
}

int pickLod(float px, int current) {
    constexpr float kT0 = 160.0f, kT1 = 70.0f, kH = 0.12f;
    float t0 = kT0, t1 = kT1;
    if (current == 0) t0 *= 1.0f - kH;          // stay detailed a little longer
    if (current == 1) t0 *= 1.0f + kH, t1 *= 1.0f - kH;
    if (current == 2) t1 *= 1.0f + kH;
    return px >= t0 ? 0 : px >= t1 ? 1 : 2;
}

// ---------------------------------------------------------------- populations & families

std::vector<CharacterKey> planPopulation(uint64_t seed, int count, const PopulationSpec& spec) {
    std::vector<CharacterKey> out;
    Rng r(seed, 0x504F50);
    std::vector<float> sw, aw;
    for (auto& s : spec.species) sw.push_back(std::max(0.0f, s.second));
    for (auto& a : spec.archetypes) aw.push_back(std::max(0.0f, a.second));
    for (int i = 0; i < count; ++i) {
        CharacterKey k;
        k.seed = splitmix64(seed ^ splitmix64(uint64_t(i) + 1));
        GenomeOptions& o = k.options;
        o.species = spec.species.empty() ? Species::Human : spec.species[size_t(r.weighted(sw))].first;
        if (!spec.archetypes.empty()) {
            const Archetype a = spec.archetypes[size_t(r.weighted(aw))].first;
            if (a != Archetype::Natural) o.archetype = a;
        }
        o.sex = r.chance(spec.femaleShare) ? Sex::Female : Sex::Male;
        o.age = AgeGroup(r.weighted(std::span<const float>(spec.ageWeights.data(), spec.ageWeights.size())));
        if (spec.fourArmShare > 0 && r.chance(spec.fourArmShare)) o.armPairs = 2;
        out.push_back(k);
    }
    return out;
}

Genome childOf(const Genome& mo, const Genome& fa, uint64_t seed, std::optional<Sex> sex) {
    Rng r(seed, 0xC41D);
    GenomeOptions o;
    o.species = mo.species == fa.species ? mo.species : (r.chance(0.5f) ? mo.species : fa.species);
    o.sex = sex ? *sex : (r.chance(0.5f) ? Sex::Female : Sex::Male);
    o.age = AgeGroup::Young;
    Genome g = randomGenome(seed, o);
    // Each trait: a blend of the parents biased to one side, then a share of the
    // child's own fresh draw (so siblings differ and lineages drift).
    auto mix = [&](float& out, float m, float f, float fresh = 0.25f) {
        const float t = clamp(r.gauss(0.5f, 0.22f), 0.0f, 1.0f);
        out = lerp(lerp(m, f, t), out, fresh);
    };
    auto mixV = [&](Vec3& out, Vec3 m, Vec3 f, float fresh = 0.15f) {
        const float t = clamp(r.gauss(0.5f, 0.2f), 0.0f, 1.0f);
        out = lerp(lerp(m, f, t), out, fresh);
    };
    auto pick = [&](auto& out, auto m, auto f) { out = r.chance(0.5f) ? m : f; };
    const Genome* same = mo.species == g.species ? &mo : &fa;  // the parent of the child's people
    // Height and frame relative to each parent's sex.
    auto rel = [](const Genome& p) { return p.body.height / (p.sex == Sex::Male ? 1.0f : 0.93f); };
    const float hk = g.sex == Sex::Male ? 1.0f : 0.93f;
    mix(g.body.height, rel(mo) * hk, rel(fa) * hk, 0.3f);
    mix(g.body.boneMass, mo.body.boneMass, fa.body.boneMass);
    mix(g.body.legRatio, mo.body.legRatio, fa.body.legRatio);
    mix(g.body.armLength, mo.body.armLength, fa.body.armLength);
    mix(g.body.headWidth, mo.body.headWidth, fa.body.headWidth);
    mix(g.body.neckLength, mo.body.neckLength, fa.body.neckLength);
    mix(g.body.handSize, mo.body.handSize, fa.body.handSize);
    mix(g.body.footSize, mo.body.footSize, fa.body.footSize);
    // Face: the family resemblance.
    FaceStyle& f = g.face;
    const FaceStyle &fm = mo.face, &ff = fa.face;
    pick(f.eyeShape, fm.eyeShape, ff.eyeShape);
    pick(f.noseShape, fm.noseShape, ff.noseShape);
    pick(f.browShape, fm.browShape, ff.browShape);
    mix(f.eyeSize, fm.eyeSize, ff.eyeSize);
    mix(f.eyeSpacing, fm.eyeSpacing, ff.eyeSpacing);
    mix(f.eyeTilt, fm.eyeTilt, ff.eyeTilt);
    mix(f.irisSize, fm.irisSize, ff.irisSize);
    mix(f.noseSize, fm.noseSize, ff.noseSize);
    mix(f.noseBridge, fm.noseBridge, ff.noseBridge);
    mix(f.noseLength, fm.noseLength, ff.noseLength);
    mix(f.noseProj, fm.noseProj, ff.noseProj);
    mix(f.noseWidth, fm.noseWidth, ff.noseWidth);
    mix(f.noseHump, fm.noseHump, ff.noseHump);
    mix(f.noseTip, fm.noseTip, ff.noseTip);
    mix(f.mouthWidth, fm.mouthWidth, ff.mouthWidth);
    mix(f.lipFullness, fm.lipFullness, ff.lipFullness);
    mix(f.cheek, fm.cheek, ff.cheek);
    mix(f.crown, fm.crown, ff.crown);
    mix(f.faceWidth, fm.faceWidth, ff.faceWidth);
    mix(f.chinWidth, fm.chinWidth, ff.chinWidth);
    mix(f.browRidge, fm.browRidge, ff.browRidge);
    mix(f.earSize, fm.earSize, ff.earSize);
    mix(f.jaw, fm.jaw, ff.jaw, 0.4f);  // jaw is strongly dimorphic: keep more of the child's own
    f.freckles = (fm.freckles || ff.freckles) ? r.chance(0.6f) : r.chance(0.05f);
    // Colour: skin blends, eyes follow one parent, hair blends unless a parent has
    // gone grey (then the child keeps its own colour on that side).
    Palette& p = g.palette;
    mixV(p.skin, mo.palette.skin, fa.palette.skin);
    p.iris = lerp(r.chance(0.5f) ? mo.palette.iris : fa.palette.iris, p.iris, 0.1f);
    const Vec3 hm = mo.age > 42 ? p.hair : mo.palette.hair, hf = fa.age > 42 ? p.hair : fa.palette.hair;
    mixV(p.hair, hm, hf, 0.2f);
    p.hairTip = p.hair * 0.88f;
    p.brow = luminance(p.hair) > 0.55f ? lerp(p.hair * 0.6f, Vec3{0.30f, 0.24f, 0.20f}, 0.5f) : p.hair * 0.8f;
    p.horn = same->palette.horn;
    // People features from the parent of that people (tusks, ears, horns...).
    g.body.ear = same->body.ear;
    mix(g.body.tuskSize, mo.body.tuskSize, fa.body.tuskSize, 0.3f);
    mix(g.body.tuskCurve, mo.body.tuskCurve, fa.body.tuskCurve, 0.3f);
    if (mo.body.spines > 0 || fa.body.spines > 0) {
        const Genome& sp = mo.body.spines >= fa.body.spines ? mo : fa;
        if (r.chance(0.6f)) g.body.spines = sp.body.spines, g.body.spineKind = sp.body.spineKind, g.body.spineSize = sp.body.spineSize;
    }
    if ((mo.body.tail || fa.body.tail) && !g.body.tail && r.chance(0.5f)) {
        const Genome& tp = mo.body.tail ? mo : fa;
        g.body.tail = true, g.body.tailType = tp.body.tailType, g.body.tailLength = tp.body.tailLength, g.body.tailFur = tp.body.tailFur;
    }
    if (mo.body.armPairs == 2 && fa.body.armPairs == 2) g.body.armPairs = 2;
    return g;
}

Genome aged(const Genome& src, float newAge) {
    Genome g = src;
    const float a0 = src.age, a1 = std::max(newAge, 16.0f), d = a1 - a0;
    g.age = a1;
    g.ageGroup = groupOfAge(a1);
    auto toward = [](float& v, float target, float k) { v = lerp(v, target, saturate(k)); };
    // Grey hair: same curve as the sampler; greying only moves forward.
    auto grey = [](float a) { return std::pow(saturate((a - 42.0f) / 30.0f), 1.2f) * 0.8f; };
    const float g0 = grey(a0), g1 = grey(a1);
    if (g1 > g0) {
        const float k = (g1 - g0) / std::max(1e-3f, 1.0f - g0);
        g.palette.hair = lerp(g.palette.hair, Vec3{0.74f, 0.73f, 0.71f}, k);
        g.palette.hairTip = g.palette.hair * 0.88f;
        g.palette.brow = lerp(g.palette.brow, Vec3{0.62f, 0.60f, 0.58f}, 0.7f * k);
    }
    FaceStyle& f = g.face;
    f.ageLines = saturate((a1 - 34.0f) / 34.0f);
    f.eyeBags = saturate(f.eyeBags + d / 45.0f);
    if (a1 > 25.0f || d < 0) f.wrinkles = saturate(f.wrinkles + d / 60.0f);
    if (a1 > 40.0f || d < 0) f.jowl = saturate(f.jowl + d / 80.0f);
    f.roundness = saturate(f.roundness - d / 60.0f);
    if (a1 > 55.0f) f.cheekHollow = saturate(f.cheekHollow + std::max(0.0f, d) / 90.0f);
    BodyShape& b = g.body;
    const float sagT = a1 >= 60 ? 0.75f : a1 >= 40 ? 0.30f : 0.05f;
    const float hunchT = a1 >= 60 ? 0.6f : a1 >= 40 ? 0.08f : 0.0f;
    toward(b.sag, sagT, std::fabs(d) / 20.0f);
    toward(b.hunch, std::max(hunchT, d < 0 ? 0.0f : b.hunch), std::fabs(d) / 25.0f);
    // Muscle and definition fade after ~45; height shrinks a little after ~60.
    auto decline = [](float a) { return saturate((a - 45.0f) / 40.0f); };
    const float dm = decline(a1) - decline(a0);
    b.muscle = std::max(0.0f, b.muscle * (1.0f - 0.45f * dm));
    b.anat.definition = saturate(b.anat.definition - 0.35f * dm);
    b.belly = std::max(0.0f, b.belly + 0.12f * dm);
    auto shrink = [](float a) { return 1.0f - 0.04f * saturate((a - 60.0f) / 30.0f); };
    b.height *= shrink(a1) / shrink(a0);
    return g;
}

// ---------------------------------------------------------------- traits & text

PhysicalTraits physicalTraits(const Genome& g) {
    const BodyShape& b = g.body;
    PhysicalTraits t;
    const float mp = b.height * b.height * b.height * (0.6f + 0.25f * b.muscle + 0.2f * b.belly + 0.3f * (b.boneMass - 1.0f)) *
                     (b.shoulderWidth / 0.82f) * (b.centaur ? 2.5f : 1.0f);
    t.size = saturate((std::log(std::max(mp, 0.05f)) - std::log(1.2f)) / (std::log(20.0f) - std::log(1.2f)));
    t.vigor = g.age < 25 ? lerp(0.8f, 1.0f, saturate((g.age - 16.0f) / 9.0f)) : 1.0f - 0.8f * saturate((g.age - 30.0f) / 50.0f);
    t.strength = saturate((0.22f + 0.5f * b.muscle * b.boneMass + 0.45f * (t.size - 0.4f)) * (0.55f + 0.45f * t.vigor));
    t.agility = saturate((0.5f + 0.9f * (b.legRatio - 0.5f) + 0.25f * (b.anat.definition - 0.4f) - 0.55f * (t.size - 0.4f) -
                          0.35f * b.belly + (b.digitigrade ? 0.1f : 0.0f)) * (0.55f + 0.45f * t.vigor));
    t.endurance = saturate((0.35f + 0.3f * (b.boneMass - 1.0f) + 0.2f * b.muscle + 0.25f * t.size - 0.2f * b.belly) *
                           (0.65f + 0.35f * t.vigor) + (g.species == Species::Undead || b.skeletal ? 0.25f : 0.0f));
    t.presence = saturate(0.15f + 0.5f * t.size + 0.12f * float(b.tusks) * b.tuskSize * (1.0f + 0.3f * b.tuskCurve) +
                          (b.horns ? 0.12f * b.hornSize : 0.0f) + (b.spines ? 0.1f * b.spineSize : 0.0f) +
                          (g.face.scarType ? 0.06f : 0.0f) + 0.08f * g.face.browRidge + (b.armPairs == 2 ? 0.1f : 0.0f) +
                          (g.face.glowEyes ? 0.06f : 0.0f));
    return t;
}

std::string describe(const Genome& g) {
    const BodyShape& b = g.body;
    std::string s(toString(g.species));
    if (!s.empty()) s[0] = char(s[0] - 'a' + 'A');
    s += g.sex == Sex::Male ? " man" : " woman";
    s += ", " + std::to_string(int(g.age + 0.5f));
    static constexpr std::string_view kBuild[] = {"average", "lean", "heavy", "stocky", "lanky", "strong"};
    s += ", " + std::string(kBuild[size_t(std::clamp(b.build, 0, 5))]) + " build";
    char hb[16];
    std::snprintf(hb, sizeof hb, ", %.2f m", double(b.height));
    s += hb;
    std::vector<std::string> fx;
    if (b.centaur) fx.push_back("horse body");
    if (b.stance == 1 && !b.centaur && !b.lowerBody) fx.push_back("on all fours");
    if (b.orb && !b.centaur && !b.lowerBody) fx.push_back("a living world for a body");
    if (b.skeletal) fx.push_back("bare bones");
    if (b.armPairs == 2) fx.push_back("four arms");
    if (b.tusks) {
        std::string t = b.tusks == 2 ? "long tusks" : "short tusks";
        if (b.tuskCurve > 0.35f) t += " hooked inwards";
        else if (b.tuskCurve < -0.35f) t += " splayed outwards";
        fx.push_back(t);
    }
    if (b.horns) fx.push_back("horns");
    if (b.muzzle > 0.3f) fx.push_back(b.snout == 1 ? "wolf muzzle" : b.snout == 2 ? "reptile snout" : "bovine snout");
    {
        static constexpr std::string_view kWings[] = {"", "membrane wings", "feathered wings", "insect wings"};
        static constexpr std::string_view kLower[] = {"", "serpent coil", "wraith wisp", "ooze body"};
        static constexpr std::string_view kHands[] = {"", "taloned hands", "crab pincers", "tendril hands", "digging spades"};
        static constexpr std::string_view kCrest[] = {"", "head fin", "feather crest", "bony frill", "cobra hood"};
        static constexpr std::string_view kBeak[] = {"", "raptor beak", "long beak", "short beak", "parrot beak", "duck bill", "hornbill casque", "crow beak"};
        static constexpr std::string_view kSkin[] = {"", "metal skin", "carapace", "scales", "crystal hide", "magma crust"};
        if (b.wings > 0 && b.wings <= 3) fx.emplace_back(kWings[size_t(b.wings)]);
        if (b.lowerBody > 0 && b.lowerBody <= 3) fx.emplace_back(kLower[size_t(b.lowerBody)]);
        if (b.hands > 0 && b.hands <= 4) fx.emplace_back(kHands[size_t(b.hands)]);
        if (b.feet == 1) fx.push_back("bird talons");
        if (b.backTentacles > 0) fx.push_back("back tentacles");
        if (b.shell) fx.push_back("turtle shell");
        if (b.crest > 0 && b.crest <= 4) fx.emplace_back(kCrest[size_t(b.crest)]);
        if (b.skinKind > 0 && b.skinKind <= 5) fx.emplace_back(kSkin[size_t(b.skinKind)]);
        if (b.shards > 0) fx.push_back("crystal shards");
        if (g.face.beak > 0 && g.face.beak <= 7) fx.emplace_back(kBeak[size_t(g.face.beak)]);
        if (g.face.mandibles) fx.push_back("mandibles");
        if (g.face.antennae) fx.push_back("antennae");
        if (g.face.eyeKind == 1) fx.push_back("compound eyes");
        if (g.face.eyeKind == 2) fx.push_back("a single cyclopean eye");
        if (g.face.eyeKind == 3) fx.push_back("eyeless");
        if (g.face.maw) fx.push_back("a maw split ear to ear");
        if (b.bodyEyes > 0) fx.push_back(std::to_string(b.bodyEyes) + " eyes over the body");
    }
    if (b.tail) {
        static constexpr std::string_view kTail[] = {"tail", "heavy reptile tail", "whip tail with a spade", "bushy tail",
                                                     "spiked club tail", "thin rat tail"};
        fx.emplace_back(kTail[size_t(b.tailFur ? 3 : std::clamp(b.tailType, 0, 5))]);
    }
    if (b.spines) {
        static constexpr std::string_view kSp[] = {"spiked dorsal ridge", "bony dorsal plates", "knobbed spine"};
        fx.emplace_back(kSp[size_t(std::clamp(b.spineKind, 0, 2))]);
    }
    if (b.hooves) fx.push_back("hooves");
    else if (b.digitigrade) fx.push_back("digitigrade legs");
    if (b.claws) fx.push_back("claws");
    if (g.face.glowEyes) fx.push_back("glowing eyes");
    if (g.face.scarType) fx.push_back("scarred");
    if (g.hair != HairStyle::Bald) fx.push_back(std::string(toString(g.hair)) + " hair");
    else fx.push_back("bald");
    if (g.facialHair != FacialHair::None && g.facialHair != FacialHair::Stubble) fx.emplace_back(toString(g.facialHair));
    for (size_t i = 0; i < fx.size(); ++i) s += (i == 0 ? "; " : ", ") + fx[i];
    return s;
}

// ---------------------------------------------------------------- validation & stress

bool validateCharacter(const Character& c, std::string* problem) {
    auto fail = [&](const std::string& why) {
        if (problem) *problem = why;
        return false;
    };
    const Mesh& m = c.mesh;
    if (m.vertices.empty() || m.indices.empty() || m.indices.size() % 3) return fail("empty mesh");
    if (m.triangleCount() > 30000) return fail("over budget: " + std::to_string(m.triangleCount()) + " tris");
    for (uint32_t i : m.indices)
        if (i >= m.vertices.size()) return fail("index out of range");
    for (const Submesh& s : m.submeshes)
        if (size_t(s.firstIndex) + s.indexCount > m.indices.size()) return fail("submesh out of range");
    const size_t nb = c.skeleton.bones.size();
    float minY = 1e9f, maxY = -1e9f;
    for (const Vertex& v : m.vertices) {
        if (!std::isfinite(v.pos.x + v.pos.y + v.pos.z + v.normal.x + v.normal.y + v.normal.z)) return fail("NaN vertex");
        if (std::fabs(length(v.normal) - 1.0f) > 0.02f) return fail("non-unit normal");
        float sw = 0;
        for (int k = 0; k < 4; ++k) {
            if (v.joints[k] >= nb) return fail("bad joint id");
            if (!(v.weights[k] >= 0.0f)) return fail("negative weight");
            sw += v.weights[k];
        }
        if (std::fabs(sw - 1.0f) > 1e-3f) return fail("weights sum " + std::to_string(sw));
        minY = std::min(minY, v.pos.y), maxY = std::max(maxY, v.pos.y);
    }
    for (const Bone& b : c.skeleton.bones)
        if (!std::isfinite(b.restWorld.x + b.restWorld.y + b.restWorld.z)) return fail("NaN bone");
    if (minY < -0.05f * c.height) return fail("below the floor: " + std::to_string(minY));
    if (maxY > 1.8f * c.height || maxY < 0.8f * c.height) return fail("height mismatch: " + std::to_string(maxY) + " vs " + std::to_string(c.height));
    if (c.faceAtlas.width != FaceLayout::kAtlasW || c.faceAtlas.height != FaceLayout::kAtlasH) return fail("face atlas size");
    return true;
}

Genome fuzzGenome(uint64_t seed, GenOptions* opt) {
    Rng r(seed, 0xF022);
    GenomeOptions o;
    if (r.chance(0.7f)) o.species = Species(r.rangeInt(0, int(Species::Count) - 1));
    if (r.chance(0.3f)) o.archetype = Archetype(r.rangeInt(0, int(Archetype::Count) - 1));
    if (r.chance(0.5f)) o.sex = r.chance(0.5f) ? Sex::Female : Sex::Male;
    if (r.chance(0.5f)) o.age = AgeGroup(r.rangeInt(0, 3));
    if (r.chance(0.25f)) o.armPairs = 2;
    o.exaggeration = r.chance(0.3f) ? r.range(0.0f, 2.0f) : 0.0f;
    Genome g = randomGenome(seed, o);
    BodyShape& b = g.body;
    FaceStyle& f = g.face;
    auto mul = [&](float& v, float lo, float hi) { v *= r.range(lo, hi); };
    mul(b.height, 0.75f, 1.35f);
    mul(b.shoulderWidth, 0.75f, 1.35f), mul(b.chestWidth, 0.75f, 1.35f), mul(b.waistWidth, 0.75f, 1.35f);
    mul(b.hipWidth, 0.8f, 1.25f), mul(b.chestDepth, 0.8f, 1.3f);
    mul(b.arm, 0.7f, 1.5f), mul(b.thigh, 0.7f, 1.4f), mul(b.calf, 0.7f, 1.4f), mul(b.neckThick, 0.7f, 1.6f);
    mul(b.neckLength, 0.5f, 1.5f), mul(b.armLength, 0.85f, 1.2f), mul(b.handSize, 0.8f, 1.4f), mul(b.footSize, 0.8f, 1.3f);
    b.muscle = r.range(0.0f, 1.3f), b.belly = r.chance(0.3f) ? r.range(0.0f, 0.9f) : b.belly;
    b.boneMass = r.range(0.7f, 1.8f);
    if (r.chance(0.35f)) {
        b.tail = true;
        b.tailType = r.rangeInt(0, 5);
        b.tailFur = b.tailType == 3;
        b.tailLength = r.range(0.5f, 1.4f);
    }
    if (r.chance(0.3f)) b.spines = r.rangeInt(3, 10), b.spineKind = r.rangeInt(0, 2), b.spineSize = r.range(0.5f, 1.5f);
    if (r.chance(0.3f)) b.tusks = r.rangeInt(1, 2), b.tuskSize = r.range(0.5f, 1.4f), b.tuskCurve = r.range(-1.0f, 1.0f);
    if (r.chance(0.25f)) b.horns = 2, b.hornKind = r.rangeInt(-1, 9), b.hornSize = r.range(0.5f, 1.5f);
    if (r.chance(0.2f)) b.skinKind = r.rangeInt(1, 2), b.carapace = r.range(0.3f, 1.0f);
    if (r.chance(0.15f)) b.core = r.rangeInt(1, 2);
    if (r.chance(0.15f)) f.eyePairs = 2;
    // Species traits.
    if (r.chance(0.15f)) b.wings = r.rangeInt(1, 3), b.wingsFolded = r.chance(0.3f), b.wingSize = r.range(0.7f, 1.3f);
    if (r.chance(0.10f)) b.lowerBody = r.rangeInt(1, 3), b.serpentLength = r.range(0.7f, 1.4f);
    if (r.chance(0.15f)) b.hands = r.rangeInt(1, 4);
    if (r.chance(0.06f)) b.bodyEyes = r.rangeInt(4, 20);
    if (r.chance(0.05f)) f.maw = true;
    if (r.chance(0.06f)) b.stance = 1, b.bust = 0.0f;
    if (r.chance(0.04f)) b.orb = true;
    if (r.chance(0.10f)) b.feet = 1;
    if (r.chance(0.10f)) b.backTentacles = r.rangeInt(1, 6), b.tentacleLength = r.range(0.6f, 1.4f);
    if (r.chance(0.08f)) b.shell = true;
    if (r.chance(0.12f)) b.crest = r.rangeInt(1, 4), b.crestSize = r.range(0.6f, 1.4f);
    if (r.chance(0.10f)) b.skinKind = r.rangeInt(3, 5), b.shards = r.chance(0.5f) ? r.rangeInt(0, 16) : 0;
    if (r.chance(0.12f)) f.beak = r.rangeInt(1, 7);
    if (r.chance(0.08f)) f.mandibles = true;
    if (r.chance(0.08f)) f.antennae = r.rangeInt(1, 2), f.antennaLength = r.range(0.6f, 1.5f);
    if (r.chance(0.10f)) f.eyeKind = r.rangeInt(1, 3), f.eyePairs = 1;
    if (r.chance(0.08f)) b.ears = EarStyle(r.rangeInt(0, int(EarStyle::Count) - 1));
    if (r.chance(0.2f)) f.mouthStyle = r.rangeInt(0, 3);
    if (r.chance(0.3f)) g.hair = HairStyle(r.rangeInt(0, int(HairStyle::Count) - 1));
    if (r.chance(0.3f)) g.facialHair = FacialHair(r.rangeInt(0, int(FacialHair::Count) - 1));
    f.eyeSize *= r.range(0.8f, 1.2f), f.jaw = r.range(-1.0f, 1.0f), f.chin = r.range(-1.0f, 1.0f);
    f.noseLength *= r.range(0.7f, 1.3f), f.headTop = r.range(-1.0f, 1.0f);
    {   // skull construction at its extremes (a stream of its own: the draws above keep their values)
        Rng rk = r.fork(77);
        HeadForm& k = g.head;
        k.forehead = rk.range(0.0f, 1.0f), k.occiput = rk.range(-0.5f, 1.3f), k.temples = rk.range(0.0f, 1.0f);
        k.zygoma = rk.range(0.0f, 1.0f), k.box = rk.range(-1.0f, 1.0f), k.flatTop = rk.range(0.0f, 1.0f);
        k.rostrum = rk.chance(0.3f) ? rk.range(0.0f, 1.0f) : 0.0f, k.socket = rk.range(0.0f, 1.0f);
        k.eyeLook = EyeLook(rk.rangeInt(0, int(EyeLook::Count) - 1));
    }
    if (opt) {
        static constexpr float kDecay[] = {0.0f, 0.0f, 0.0f, 0.15f, 0.4f, 0.7f, 0.9f, 1.0f};
        opt->decay = kDecay[r.rangeInt(0, 7)];
        opt->lod = r.rangeInt(0, 2);
    }
    return g;
}

}  // namespace cg
