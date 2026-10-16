#include "chargen/image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

// ---------------------------------------------------------------- GIF

namespace cg {

namespace {

struct ColorBox {
    std::vector<std::pair<uint16_t, uint32_t>> cols;  // 15-bit colour, count
};

inline int c5(uint16_t c, int ch) { return (c >> (10 - 5 * ch)) & 31; }

}  // namespace

std::vector<uint8_t> encodeGif(const std::vector<Image>& frames, int delay) {
    std::vector<uint8_t> out;
    if (frames.empty()) return out;
    const int W = frames[0].width, H = frames[0].height;
    // Palette: median cut over a 15-bit histogram of every frame.
    std::vector<uint32_t> hist(32768, 0);
    auto key = [](const uint8_t* p) { return uint16_t(((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3)); };
    for (const Image& f : frames)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) ++hist[key(f.at(x, y))];
    std::vector<ColorBox> boxes(1);
    for (uint32_t c = 0; c < 32768; ++c)
        if (hist[c]) boxes[0].cols.push_back({uint16_t(c), hist[c]});
    while (boxes.size() < 255) {
        int bi = -1, axis = 0;
        double bestScore = 0;
        for (size_t b = 0; b < boxes.size(); ++b) {
            if (boxes[b].cols.size() < 2) continue;
            int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
            uint64_t n = 0;
            for (auto& e : boxes[b].cols) {
                for (int ch = 0; ch < 3; ++ch) lo[ch] = std::min(lo[ch], c5(e.first, ch)), hi[ch] = std::max(hi[ch], c5(e.first, ch));
                n += e.second;
            }
            for (int ch = 0; ch < 3; ++ch) {
                const double score = double(hi[ch] - lo[ch]) * std::sqrt(double(n));
                if (score > bestScore) bestScore = score, bi = int(b), axis = ch;
            }
        }
        if (bi < 0) break;
        auto& cols = boxes[size_t(bi)].cols;
        std::sort(cols.begin(), cols.end(), [axis](auto& a, auto& b) { return c5(a.first, axis) < c5(b.first, axis); });
        uint64_t total = 0, acc = 0;
        for (auto& e : cols) total += e.second;
        size_t cut = 1;
        for (; cut < cols.size(); ++cut) {
            acc += cols[cut - 1].second;
            if (acc * 2 >= total) break;
        }
        cut = std::clamp<size_t>(cut, 1, cols.size() - 1);
        ColorBox nb;
        nb.cols.assign(cols.begin() + long(cut), cols.end());
        cols.resize(cut);
        boxes.push_back(std::move(nb));
    }
    std::vector<std::array<uint8_t, 3>> pal(256, {0, 0, 0});
    for (size_t b = 0; b < boxes.size(); ++b) {
        double s[3] = {0, 0, 0}, n = 0;
        for (auto& e : boxes[b].cols) {
            for (int ch = 0; ch < 3; ++ch) s[ch] += (c5(e.first, ch) * 8 + 4) * double(e.second);
            n += e.second;
        }
        for (int ch = 0; ch < 3; ++ch) pal[b][size_t(ch)] = uint8_t(std::clamp(s[ch] / std::max(1.0, n), 0.0, 255.0));
    }
    const int ncol = int(boxes.size());
    std::vector<uint8_t> lut(32768, 0);
    for (uint32_t c = 0; c < 32768; ++c) {
        if (!hist[c]) continue;
        int best = 0, bd = 1 << 30;
        const int r = c5(uint16_t(c), 0) * 8 + 4, g = c5(uint16_t(c), 1) * 8 + 4, b = c5(uint16_t(c), 2) * 8 + 4;
        for (int i = 0; i < ncol; ++i) {
            const int dr = r - pal[size_t(i)][0], dg = g - pal[size_t(i)][1], db = b - pal[size_t(i)][2];
            const int d = 2 * dr * dr + 4 * dg * dg + db * db;
            if (d < bd) bd = d, best = i;
        }
        lut[c] = uint8_t(best);
    }
    auto u16 = [&](int v) {
        out.push_back(uint8_t(v & 255));
        out.push_back(uint8_t(v >> 8));
    };
    const char* sig = "GIF89a";
    out.insert(out.end(), sig, sig + 6);
    u16(W);
    u16(H);
    out.push_back(0xF7);
    out.push_back(0);
    out.push_back(0);
    for (auto& c : pal) out.insert(out.end(), c.begin(), c.end());
    const uint8_t loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00};
    out.insert(out.end(), loop, loop + sizeof(loop));
    const uint8_t kTransparent = 255;
    std::vector<uint8_t> prev, idx(size_t(W) * H);
    std::vector<std::array<uint16_t, 256>> tree(4096);
    for (size_t fi = 0; fi < frames.size(); ++fi) {
        const Image& f = frames[fi];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) idx[size_t(y) * W + x] = lut[key(f.at(x, y))];
        std::vector<uint8_t> px = idx;
        if (!prev.empty())
            for (size_t i = 0; i < px.size(); ++i)
                if (idx[i] == prev[i]) px[i] = kTransparent;
        prev = idx;
        const uint8_t gce[] = {0x21, 0xF9, 0x04, 0x05, uint8_t(delay & 255), uint8_t(delay >> 8), kTransparent, 0x00};
        out.insert(out.end(), gce, gce + sizeof(gce));
        out.push_back(0x2C);
        u16(0);
        u16(0);
        u16(W);
        u16(H);
        out.push_back(0);
        const int minCode = 8, clearCode = 256;
        out.push_back(uint8_t(minCode));
        // LZW, LSB-first bits packed into 255-byte sub-blocks.
        std::vector<uint8_t> block;
        uint32_t bits = 0;
        int nbits = 0;
        auto flushBlock = [&]() {
            if (block.empty()) return;
            out.push_back(uint8_t(block.size()));
            out.insert(out.end(), block.begin(), block.end());
            block.clear();
        };
        auto put = [&](int code, int size) {
            bits |= uint32_t(code) << nbits;
            nbits += size;
            while (nbits >= 8) {
                block.push_back(uint8_t(bits & 255));
                bits >>= 8;
                nbits -= 8;
                if (block.size() == 255) flushBlock();
            }
        };
        for (auto& t : tree) t.fill(0);
        int codeSize = minCode + 1, maxCode = clearCode + 1, cur = -1;
        put(clearCode, codeSize);
        for (uint8_t v : px) {
            if (cur < 0) {
                cur = v;
            } else if (tree[size_t(cur)][v]) {
                cur = tree[size_t(cur)][v];
            } else {
                put(cur, codeSize);
                tree[size_t(cur)][v] = uint16_t(++maxCode);
                if (maxCode >= (1 << codeSize)) ++codeSize;
                if (maxCode == 4095) {
                    put(clearCode, codeSize);
                    for (auto& t : tree) t.fill(0);
                    codeSize = minCode + 1;
                    maxCode = clearCode + 1;
                }
                cur = v;
            }
        }
        put(cur, codeSize);
        put(clearCode, codeSize);
        put(clearCode + 1, minCode + 1);
        if (nbits > 0) block.push_back(uint8_t(bits & 255));
        flushBlock();
        out.push_back(0);
    }
    out.push_back(0x3B);
    return out;
}

bool writeGif(const std::string& path, const std::vector<Image>& frames, int delay) {
    const std::vector<uint8_t> data = encodeGif(frames, delay);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

}  // namespace cg
