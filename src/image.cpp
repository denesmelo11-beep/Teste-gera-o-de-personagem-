#include "chargen/image.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace cg {

void Image::set(int x, int y, Vec4 c) {
    uint8_t* p = at(x, y);
    p[0] = uint8_t(saturate(c.x) * 255.0f + 0.5f);
    p[1] = uint8_t(saturate(c.y) * 255.0f + 0.5f);
    p[2] = uint8_t(saturate(c.z) * 255.0f + 0.5f);
    p[3] = uint8_t(saturate(c.w) * 255.0f + 0.5f);
}

Vec4 Image::get(int x, int y) const {
    const uint8_t* p = at(x, y);
    constexpr float k = 1.0f / 255.0f;
    return {p[0] * k, p[1] * k, p[2] * k, p[3] * k};
}

Vec4 Image::sample(Vec2 uv) const {
    float fx = clamp(uv.x * width - 0.5f, 0.0f, float(width - 1));
    float fy = clamp(uv.y * height - 0.5f, 0.0f, float(height - 1));
    int x0 = int(fx), y0 = int(fy);
    int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    float tx = fx - x0, ty = fy - y0;
    auto mix = [](Vec4 a, Vec4 b, float t) {
        return Vec4{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t)};
    };
    return mix(mix(get(x0, y0), get(x1, y0), tx), mix(get(x0, y1), get(x1, y1), tx), ty);
}

void Image::blit(const Image& src, int x, int y) {
    for (int j = 0; j < src.height; ++j)
        for (int i = 0; i < src.width; ++i) {
            int dx = x + i, dy = y + j;
            if (dx < 0 || dy < 0 || dx >= width || dy >= height) continue;
            const uint8_t* s = src.at(i, j);
            uint8_t* d = at(dx, dy);
            for (int k = 0; k < 4; ++k) d[k] = s[k];
        }
}

namespace {

struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int n = 0;
    void put(uint32_t bits, int count) {  // LSB first
        acc |= bits << n;
        n += count;
        while (n >= 8) {
            out.push_back(uint8_t(acc));
            acc >>= 8;
            n -= 8;
        }
    }
    void putHuff(uint32_t code, int len) {  // Huffman codes are MSB first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
        put(r, len);
    }
    void flush() {
        if (n > 0) out.push_back(uint8_t(acc));
        acc = 0;
        n = 0;
    }
};

void putLiteral(BitWriter& bw, int v) {
    if (v < 144) bw.putHuff(0x30 + v, 8);
    else if (v < 256) bw.putHuff(0x190 + v - 144, 9);
    else if (v < 280) bw.putHuff(v - 256, 7);
    else bw.putHuff(0xC0 + v - 280, 8);
}

constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putMatch(BitWriter& bw, int len, int dist) {
    int li = 28;
    while (kLenBase[li] > len) --li;
    putLiteral(bw, 257 + li);
    bw.put(uint32_t(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > dist) --di;
    bw.putHuff(uint32_t(di), 5);
    bw.put(uint32_t(dist - kDistBase[di]), kDistExtra[di]);
}

std::vector<uint8_t> deflateFixed(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    BitWriter bw{out};
    bw.put(1, 1);  // BFINAL
    bw.put(1, 2);  // fixed Huffman
    constexpr int kWin = 32768, kHashBits = 15, kMaxChain = 48;
    std::vector<int> head(1 << kHashBits, -1), prev(in.size(), -1);
    auto hash = [&](size_t i) {
        uint32_t v = uint32_t(in[i]) | (uint32_t(in[i + 1]) << 8) | (uint32_t(in[i + 2]) << 16);
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    size_t i = 0, n = in.size();
    auto insert = [&](size_t p) {
        if (p + 2 < n) {
            uint32_t h = hash(p);
            prev[p] = head[h];
            head[h] = int(p);
        }
    };
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hash(i)];
            int chain = 0;
            int maxLen = int(std::min<size_t>(258, n - i));
            while (cand >= 0 && int(i) - cand <= kWin && chain++ < kMaxChain) {
                int l = 0;
                while (l < maxLen && in[cand + l] == in[i + l]) ++l;
                if (l > bestLen) {
                    bestLen = l;
                    bestDist = int(i) - cand;
                    if (l == maxLen) break;
                }
                cand = prev[cand];
            }
        }
        if (bestLen >= 3) {
            putMatch(bw, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + k);
            i += bestLen;
        } else {
            putLiteral(bw, in[i]);
            insert(i);
            ++i;
        }
    }
    putLiteral(bw, 256);
    bw.flush();
    return out;
}

uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}

void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    put32(png, uint32_t(data.size()));
    size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    put32(png, crc32(&png[start], png.size() - start) ^ 0xFFFFFFFFu);
}

}  // namespace

std::vector<uint8_t> encodePng(const Image& img) {
    const int bpp = 4, stride = img.width * bpp;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(stride + 1) * img.height);
    std::vector<uint8_t> line(stride), best(stride);
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* cur = img.at(0, y);
        const uint8_t* up = y > 0 ? img.at(0, y - 1) : nullptr;
        long bestScore = -1;
        int bestFilter = 0;
        for (int f = 0; f < 5; ++f) {
            long score = 0;
            for (int x = 0; x < stride; ++x) {
                int a = x >= bpp ? cur[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
                int pred = 0;
                if (f == 1) pred = a;
                else if (f == 2) pred = b;
                else if (f == 3) pred = (a + b) / 2;
                else if (f == 4) {
                    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                }
                line[x] = uint8_t(cur[x] - pred);
                score += std::abs(int(int8_t(line[x])));
            }
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestFilter = f;
                best = line;
            }
        }
        raw.push_back(uint8_t(bestFilter));
        raw.insert(raw.end(), best.begin(), best.end());
    }
    std::vector<uint8_t> z{0x78, 0x01};
    std::vector<uint8_t> def = deflateFixed(raw);
    z.insert(z.end(), def.begin(), def.end());
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    put32(z, (s2 << 16) | s1);

    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(img.width));
    put32(ihdr, uint32_t(img.height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

bool writePng(const std::string& path, const Image& img) {
    std::vector<uint8_t> data = encodePng(img);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

}  // namespace cg
