// Deterministic PCG32 random generator with stable, named sub-streams.
#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace cg {

inline constexpr uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

class Rng {
public:
    explicit Rng(uint64_t seed, uint64_t stream = 0) : seed_(seed) {
        inc_ = (splitmix64(stream ^ 0xDA3E39CB94B95BDBull) << 1u) | 1u;
        state_ = 0;
        next();
        state_ += splitmix64(seed);
        next();
    }
    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int rangeInt(int lo, int hiInclusive) {
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hiInclusive - lo + 1));
    }
    bool chance(float p) { return uniform() < p; }
    // Approximate gaussian (Irwin-Hall, 4 samples), clamped to +-3 sigma.
    float gauss(float mean, float sd) {
        float s = uniform() + uniform() + uniform() + uniform() - 2.0f;
        return mean + sd * std::fmax(-3.0f, std::fmin(3.0f, s * 1.7320508f));
    }
    // Index chosen proportionally to weights.
    int weighted(std::span<const float> w) {
        float total = 0;
        for (float x : w) total += x;
        float r = uniform() * total;
        for (size_t i = 0; i < w.size(); ++i) {
            if (r < w[i]) return static_cast<int>(i);
            r -= w[i];
        }
        return static_cast<int>(w.size()) - 1;
    }
    int weighted(std::initializer_list<float> w) { return weighted(std::span<const float>(w.begin(), w.size())); }
    // Independent stream derived from the original seed: adding draws in one
    // feature never reshuffles another feature.
    Rng fork(uint64_t tag) const { return Rng(splitmix64(seed_ ^ splitmix64(tag)), tag); }
    uint64_t seed() const { return seed_; }

private:
    uint64_t seed_, state_, inc_;
};

}  // namespace cg
