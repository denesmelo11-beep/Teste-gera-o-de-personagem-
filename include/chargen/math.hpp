// Small, dependency-free math kit (Y up, +Z forward, right-handed, column-major).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cg {

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTau = 2.0f * kPi;

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};
inline constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr Vec3 xyz() const { return {x, y, z}; }
};

inline constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a, Vec3 fallback = {0, 1, 0}) {
    float l = length(a);
    return l > 1e-12f ? a / l : fallback;
}
inline Vec2 normalize(Vec2 a) {
    float l = length(a);
    return l > 1e-12f ? a / l : Vec2{1, 0};
}

inline constexpr float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline constexpr float saturate(float v) { return clamp(v, 0.0f, 1.0f); }
inline constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline constexpr float smoothstep(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline constexpr float remap(float v, float a0, float a1, float b0, float b1) {
    return b0 + (v - a0) / (a1 - a0) * (b1 - b0);
}
// Signed power: keeps the sign, used by superellipses.
inline float spow(float x, float p) { return std::copysign(std::pow(std::fabs(x), p), x); }
// Smooth bump: 1 at 0, 0 at |x| >= 1.
inline float bump(float x) {
    x = std::fabs(x);
    if (x >= 1.0f) return 0.0f;
    float t = 1.0f - x * x;
    return t * t;
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat axisAngle(Vec3 axis, float angle) {
        axis = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
    }
    // Intrinsic X then Y then Z composition: q = qz * qy * qx.
    static Quat euler(float rx, float ry, float rz);
};
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat normalize(Quat q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0 ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
inline Quat Quat::euler(float rx, float ry, float rz) {
    return axisAngle({0, 0, 1}, rz) * axisAngle({0, 1, 0}, ry) * axisAngle({1, 0, 0}, rx);
}
inline Quat nlerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return normalize(Quat{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t)});
}
// Rotation taking unit vector a onto unit vector b.
inline Quat fromTo(Vec3 a, Vec3 b) {
    Vec3 c = cross(a, b);
    float d = dot(a, b);
    if (d < -0.9999f) {
        Vec3 axis = std::fabs(a.x) < 0.9f ? cross(a, {1, 0, 0}) : cross(a, {0, 1, 0});
        return Quat::axisAngle(axis, kPi);
    }
    return normalize(Quat{c.x, c.y, c.z, 1.0f + d});
}

struct Mat4 {
    float m[16]{};  // column-major: m[col * 4 + row]
    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    static Mat4 translation(Vec3 t) {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 trs(Vec3 t, Quat q, Vec3 s = {1, 1, 1}) {
        Vec3 ax = rotate(q, {s.x, 0, 0}), ay = rotate(q, {0, s.y, 0}), az = rotate(q, {0, 0, s.z});
        Mat4 r;
        r.m[0] = ax.x; r.m[1] = ax.y; r.m[2] = ax.z;
        r.m[4] = ay.x; r.m[5] = ay.y; r.m[6] = ay.z;
        r.m[8] = az.x; r.m[9] = az.y; r.m[10] = az.z;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; r.m[15] = 1;
        return r;
    }
    Vec3 point(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 dir(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
                m[2] * p.x + m[6] * p.y + m[10] * p.z};
    }
    Vec4 operator*(Vec4 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * p.w, m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * p.w,
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * p.w, m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * p.w};
    }
};
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / std::tan(fovy * 0.5f);
    Mat4 r;
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}

// ---- colour helpers (colours are kept in sRGB space, 0..1) ----
inline Vec3 hsv(float h, float s, float v) {
    h = h - std::floor(h);
    float r = std::fabs(h * 6 - 3) - 1, g = 2 - std::fabs(h * 6 - 2), b = 2 - std::fabs(h * 6 - 4);
    Vec3 c{saturate(r), saturate(g), saturate(b)};
    return lerp(Vec3{1, 1, 1}, c, s) * v;
}
inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline Vec3 srgbToLinear(Vec3 c) { return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)}; }
inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace cg
