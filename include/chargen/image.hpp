// RGBA8 image + a self-contained PNG encoder (LZ77 + fixed-Huffman deflate).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "math.hpp"

namespace cg {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(size_t(w) * h * 4, 0) {}
    uint8_t* at(int x, int y) { return &rgba[(size_t(y) * width + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
    void set(int x, int y, Vec4 c);
    Vec4 get(int x, int y) const;
    // Bilinear fetch, uv in [0,1] with v = 0 at the top row. Clamped.
    Vec4 sample(Vec2 uv) const;
    // Copy `src` into this image at (x, y).
    void blit(const Image& src, int x, int y);
};

std::vector<uint8_t> encodePng(const Image& img);
bool writePng(const std::string& path, const Image& img);

}  // namespace cg
