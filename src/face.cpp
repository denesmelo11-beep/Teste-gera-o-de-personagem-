// Procedural 2D anime face painter. Every expression is drawn with signed
// distance shapes (anti-aliased) into cells of one atlas; the 3D decals pick
// cells by UV offset at runtime, so eyes and mouth combine freely.
#include <functional>
#include <thread>

#include "chargen/character.hpp"

namespace cg {

FaceState emotionFace(Emotion e) {
    switch (e) {
        case Emotion::Joy: return {EyeExpr::Happy, MouthExpr::Grin};
        case Emotion::Rage: return {EyeExpr::Angry, MouthExpr::Shout};
        case Emotion::Sorrow: return {EyeExpr::Sad, MouthExpr::Frown};
        case Emotion::Shock: return {EyeExpr::Surprised, MouthExpr::Oh};
        case Emotion::Pain: return {EyeExpr::Hurt, MouthExpr::Shout};
        case Emotion::Confident: return {EyeExpr::Smug, MouthExpr::Smirk};
        case Emotion::Talk: return {EyeExpr::Neutral, MouthExpr::Open};
        default: return {EyeExpr::Neutral, MouthExpr::Neutral};
    }
}

std::string_view toString(EyeExpr e) {
    static constexpr std::string_view n[] = {"neutral", "blink", "happy", "angry", "sad", "surprised", "hurt", "smug"};
    return e < EyeExpr::Count ? n[size_t(e)] : "?";
}

std::string_view toString(MouthExpr e) {
    static constexpr std::string_view n[] = {"neutral", "smile", "open", "grin", "frown", "shout", "oh", "smirk"};
    return e < MouthExpr::Count ? n[size_t(e)] : "?";
}

namespace {

using Sdf = std::function<float(Vec2)>;
using ColorFn = std::function<Vec4(Vec2)>;

// Draws into one atlas cell; shapes are given in face units (head height = 1).
class CellPainter {
public:
    CellPainter(std::vector<Vec4>& px, int atlasW, int cx, int cy, int cw, int chh, float x0, float x1, float s0, float s1)
        : px_(px), W_(atlasW), cx_(cx), cy_(cy), cw_(cw), ch_(chh), x0_(x0), x1_(x1), s0_(s0), s1_(s1) {
        scale_ = cw / (x1 - x0);
    }

    void fill(const Sdf& sdf, const ColorFn& col, Vec2 lo, Vec2 hi) {
        int ix0 = std::max(0, int((lo.x - x0_) * scale_) - 2), ix1 = std::min(cw_ - 1, int((hi.x - x0_) * scale_) + 2);
        int iy0 = std::max(0, int((s1_ - hi.y) * scale_) - 2), iy1 = std::min(ch_ - 1, int((s1_ - lo.y) * scale_) + 2);
        // Leave a 1px transparent frame so bilinear filtering never bleeds between cells.
        ix0 = std::max(ix0, 1); iy0 = std::max(iy0, 1);
        ix1 = std::min(ix1, cw_ - 2); iy1 = std::min(iy1, ch_ - 2);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) {
                Vec2 p{x0_ + (x + 0.5f) / scale_, s1_ - (y + 0.5f) / scale_};
                float d = sdf(p) * scale_;
                float cov = saturate(0.5f - d);
                if (cov <= 0) continue;
                Vec4 c = col(p);
                float a = c.w * cov;
                Vec4& dst = px_[size_t(cy_ + y) * W_ + size_t(cx_ + x)];
                float oa = a + dst.w * (1 - a);
                if (oa <= 0) continue;
                Vec3 rgb = (c.xyz() * a + dst.xyz() * (dst.w * (1 - a))) / oa;
                dst = {rgb, oa};
            }
    }
    void fill(const Sdf& sdf, Vec4 c, Vec2 lo, Vec2 hi) {
        fill(sdf, [c](Vec2) { return c; }, lo, hi);
    }

private:
    std::vector<Vec4>& px_;
    int W_, cx_, cy_, cw_, ch_;
    float x0_, x1_, s0_, s1_, scale_;
};

float sdEllipse(Vec2 p, Vec2 c, Vec2 r) {
    Vec2 q{(p.x - c.x) / r.x, (p.y - c.y) / r.y};
    float k = length(q);
    return (k - 1.0f) * std::min(r.x, r.y);
}

// Variable-width polyline stroke.
struct Stroke {
    std::vector<Vec2> pts;
    std::vector<float> w;  // half widths
    float operator()(Vec2 p) const {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Vec2 a = pts[i], b = pts[i + 1], ab = b - a;
            float t = saturate(dot(p - a, ab) / std::max(1e-9f, dot(ab, ab)));
            float d = length(p - (a + ab * t)) - lerp(w[i], w[i + 1], t);
            best = std::min(best, d);
        }
        return best;
    }
    void bounds(Vec2& lo, Vec2& hi) const {
        lo = {1e9f, 1e9f};
        hi = {-1e9f, -1e9f};
        for (size_t i = 0; i < pts.size(); ++i) {
            lo = {std::min(lo.x, pts[i].x - w[i]), std::min(lo.y, pts[i].y - w[i])};
            hi = {std::max(hi.x, pts[i].x + w[i]), std::max(hi.y, pts[i].y + w[i])};
        }
    }
};

Stroke curveStroke(const std::function<Vec2(float)>& f, const std::function<float(float)>& width, int n = 24) {
    Stroke s;
    for (int i = 0; i <= n; ++i) {
        float t = float(i) / n;
        s.pts.push_back(f(t));
        s.w.push_back(width(t));
    }
    return s;
}

// Region between two curves y_lo(x) < y < y_hi(x), x in [-W, W], with an
// approximate (gradient-normalised) signed distance.
float sdBetween(Vec2 p, float W, const std::function<float(float)>& hi, const std::function<float(float)>& lo) {
    float x = clamp(p.x, -W, W);
    const float e = 1e-3f;
    float dh = (hi(x + e) - hi(x - e)) / (2 * e), dl = (lo(x + e) - lo(x - e)) / (2 * e);
    float a = (p.y - hi(x)) / std::sqrt(1 + dh * dh);
    float b = (lo(x) - p.y) / std::sqrt(1 + dl * dl);
    float c = std::fabs(p.x) - W;
    return std::max({a, b, c});
}

struct EyeParams {
    float open = 1, lidTiltIn = 0, lidFlat = 0, irisScale = 1, pupilScale = 1;
    float browY = 0, browAngle = 0, browArch = 1;
    int special = 0;  // 0 open, 1 closed arc, 2 happy arc, 3 chevron
    bool tear = false, blush = false, sparkle = false;
};

EyeParams eyeParams(EyeExpr e) {
    EyeParams p;
    switch (e) {
        case EyeExpr::Blink: p.special = 1; break;
        case EyeExpr::Happy: p.special = 2; p.browY = 0.012f; p.blush = true; break;
        case EyeExpr::Angry: p.lidTiltIn = 0.55f; p.irisScale = 0.9f; p.pupilScale = 0.75f; p.browAngle = -0.45f; p.browY = -0.02f; p.browArch = 0.2f; break;
        case EyeExpr::Sad: p.lidTiltIn = -0.35f; p.browAngle = 0.4f; p.browY = 0.01f; p.tear = true; p.sparkle = true; break;
        case EyeExpr::Surprised: p.open = 1.15f; p.irisScale = 0.72f; p.pupilScale = 0.5f; p.browY = 0.018f; p.browArch = 1.3f; break;
        case EyeExpr::Hurt: p.special = 3; p.browAngle = 0.35f; p.browY = -0.005f; break;
        case EyeExpr::Smug: p.lidFlat = 0.5f; p.browY = 0.01f; p.browAngle = -0.12f; break;
        default: break;
    }
    return p;
}

Vec2 rot(Vec2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

void paintEyes(CellPainter& cp, const Genome& g, EyeExpr expr) {
    const FaceStyle& f = g.face;
    const Palette& pal = g.palette;
    const EyeParams P = eyeParams(expr);
    const float W = 0.105f * f.eyeSize, Hh = 0.082f * f.eyeSize * (0.72f + 0.5f * f.eyeRound);
    const float ey = 0.41f, exOff = 0.172f * f.eyeSpacing;
    const Vec3 lash = lerp(pal.hair * 0.22f, Vec3{0.10f, 0.06f, 0.09f}, 0.6f);
    const Vec3 brow = lerp(pal.hair * 0.55f, lash, 0.3f);
    const float lw = f.lashWeight;

    auto yUp = [&](float x) {
        float u = clamp(x / W, -1.0f, 1.0f);
        float y = Hh * P.open * std::pow(std::max(0.0f, 1 - u * u), 0.55f);
        y -= Hh * P.lidTiltIn * (0.5f - 0.5f * u) * 0.9f;
        y = std::min(y, Hh * (1 - P.lidFlat));
        return y;
    };
    auto yLo = [&](float x) {
        float u = clamp(x / W, -1.0f, 1.0f);
        return -Hh * 0.72f * std::pow(std::max(0.0f, 1 - u * u), 0.8f) * std::sqrt(P.open);
    };

    for (float side : {1.0f, -1.0f}) {
        // Eye-local frame: +x towards the outer corner, tilted.
        Vec2 c{side * exOff, ey};
        float tilt = f.eyeTilt;
        auto toLocal = [=](Vec2 p) { return rot(Vec2{(p.x - c.x) * side, p.y - c.y}, -tilt); };
        auto toFace = [=](Vec2 l) {
            Vec2 r = rot(l, tilt);
            return Vec2{c.x + r.x * side, c.y + r.y};
        };
        // Bounds of the eye opening (tilt included) - brows/lashes use their own stroke bounds.
        Vec2 lo = c - Vec2{W * 1.25f, Hh * 1.5f}, hi = c + Vec2{W * 1.25f, Hh * 1.5f};
        auto strokeLocal = [&](const Stroke& sl, Vec4 col) {
            Stroke s = sl;
            for (Vec2& p : s.pts) p = toFace(p);
            Vec2 a, b;
            s.bounds(a, b);
            cp.fill(s, col, a, b);
        };

        // Brow.
        {
            float by = Hh * P.open * 0.9f + 0.055f + P.browY;
            Stroke s = curveStroke(
                [&](float t) {
                    float x = lerp(-0.85f * W, 1.05f * W, t);
                    float arch = 0.018f * P.browArch * bump((t - 0.55f) / 0.7f);
                    float ang = P.browAngle * (0.5f - t) * 0.09f;
                    return Vec2{x, by + arch + ang - 0.01f * t};
                },
                [&](float t) { return (0.006f + 0.011f * f.browThick) * (1.0f - 0.65f * t) + 0.002f; });
            strokeLocal(s, Vec4(brow, 1));
        }

        if (P.special == 1 || P.special == 2) {  // closed / happy arcs
            float dir = P.special == 1 ? -1.0f : 1.0f;
            Stroke s = curveStroke(
                [&](float t) {
                    float x = lerp(-W, W * 1.05f, t), u = x / W;
                    return Vec2{x, dir * 0.45f * Hh * (1 - u * u) - (P.special == 1 ? 0.1f * Hh : 0.15f * Hh)};
                },
                [&](float t) { return 0.004f + (0.010f + 0.010f * lw) * std::sin(kPi * (0.15f + 0.85f * t)); });
            strokeLocal(s, Vec4(lash, 1));
            if (P.special == 1 && lw > 0.5f)  // little lashes when closed
                for (int k = 0; k < 3; ++k) {
                    float x = W * (0.45f + 0.22f * k);
                    float y = -0.45f * Hh * (1 - (x / W) * (x / W)) - 0.1f * Hh;
                    Stroke l{{{x, y}, {x + 0.012f, y - 0.022f}}, {0.004f, 0.001f}};
                    strokeLocal(l, Vec4(lash, 1));
                }
        } else if (P.special == 3) {  // > <
            Stroke s{{{W * 0.9f, Hh * 0.55f}, {-W * 0.55f, 0.0f}, {W * 0.9f, -Hh * 0.55f}}, {0.006f, 0.012f, 0.006f}};
            strokeLocal(s, Vec4(lash, 1));
        } else {
            auto openSdf = [&](Vec2 p) { return sdBetween(toLocal(p), W, yUp, yLo); };
            // Sclera with upper-lid shadow.
            cp.fill(openSdf, [&](Vec2 p) {
                Vec2 l = toLocal(p);
                float sh = smoothstep(yUp(l.x) - 0.35f * Hh, yUp(l.x) - 0.1f * Hh, l.y);
                return Vec4(lerp(Vec3{0.98f, 0.97f, 1.0f}, Vec3{0.78f, 0.76f, 0.88f}, sh), 1);
            }, lo, hi);
            // Iris (clipped by the opening): dark rim, vertical gradient.
            Vec2 ic{0.0f, -0.10f * Hh};
            Vec2 ir{0.60f * W * P.irisScale, 0.95f * Hh * P.irisScale};
            const Vec2 icF = toFace(ic);
            const float irMax = std::max(ir.x, ir.y) * 1.05f;
            const Vec2 ilo = icF - Vec2{irMax, irMax}, ihi = icF + Vec2{irMax, irMax};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir)); },
                    Vec4(pal.iris * 0.35f, 1), ilo, ihi);
            Vec2 ir2 = ir * 0.86f;
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir2)); },
                    [&](Vec2 p) {
                        float t = saturate((toLocal(p).y - (ic.y - ir2.y)) / (2 * ir2.y));
                        Vec3 col = t > 0.55f ? lerp(pal.iris, pal.iris * 0.45f, (t - 0.55f) / 0.45f)
                                             : lerp(pal.iris2, pal.iris, t / 0.55f);
                        return Vec4(col, 1);
                    }, ilo, ihi);
            // Pupil.
            Vec2 pc = ic + Vec2{0, 0.05f * Hh};
            Vec4 pcol(pal.iris * 0.12f, 1);
            float ps = P.pupilScale;
            Sdf pupil;
            switch (f.pupil) {
                case PupilStyle::Slit: pupil = [=](Vec2 p) { return sdEllipse(toLocal(p), pc, Vec2{0.07f * W, 0.62f * Hh} * ps); }; break;
                case PupilStyle::Ring:
                    pupil = [=](Vec2 p) { return std::fabs(sdEllipse(toLocal(p), pc, Vec2{0.26f * W, 0.36f * Hh} * ps)) - 0.006f; };
                    break;
                case PupilStyle::Star:
                    pupil = [=](Vec2 p) {
                        Vec2 l = toLocal(p) - pc;
                        float a = std::atan2(l.y, l.x), rr = length(l);
                        float k = 0.5f + 0.5f * std::cos(5 * (a - kPi / 2));
                        return rr - (0.10f + 0.12f * std::pow(k, 3.0f)) * W * 1.6f * ps;
                    };
                    pcol = Vec4(lerp(pal.iris2, Vec3{1, 1, 0.9f}, 0.6f), 1);
                    break;
                default: pupil = [=](Vec2 p) { return sdEllipse(toLocal(p), pc, Vec2{0.26f * W, 0.40f * Hh} * ps); }; break;
            }
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), pupil(p)); }, pcol, ilo, ihi);
            // Highlights.
            Vec2 h1 = ic + Vec2{-0.28f * ir.x, 0.38f * ir.y}, h2 = ic + Vec2{0.30f * ir.x, -0.45f * ir.y};
            float hs = P.sparkle ? 1.35f : 1.0f;
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), h1, Vec2{0.20f * W, 0.22f * Hh} * hs)); },
                    Vec4(1, 1, 1, 0.95f), ilo, ihi);
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), h2, Vec2{0.09f * W, 0.09f * Hh} * hs)); },
                    Vec4(1, 1, 1, 0.85f), ilo, ihi);
            // Upper lash line (+ wing flick for heavy lashes).
            Stroke up = curveStroke(
                [&](float t) {
                    float x = lerp(-1.02f * W, 1.04f * W, t);
                    return Vec2{x, yUp(x) + 0.004f};
                },
                [&](float t) { return 0.004f + (0.006f + 0.016f * lw) * smoothstep(0.0f, 0.75f, t); });
            if (lw > 0.45f) {
                Vec2 end = up.pts.back();
                up.pts.push_back(end + Vec2{0.035f * lw, 0.020f * lw});
                up.w.push_back(0.002f);
            }
            strokeLocal(up, Vec4(lash, 1));
            // Lower lash, outer half.
            Stroke lower = curveStroke([&](float t) {
                float x = lerp(0.25f * W, 1.0f * W, t);
                return Vec2{x, yLo(x) - 0.002f};
            }, [&](float t) { return 0.0015f + 0.004f * lw * t; }, 10);
            strokeLocal(lower, Vec4(lash, 0.85f));
            // Double-eyelid crease.
            if (P.lidFlat == 0 && P.lidTiltIn <= 0) {
                Stroke crease = curveStroke([&](float t) {
                    float x = lerp(0.0f, 0.9f * W, t);
                    return Vec2{x, yUp(x) + 0.022f};
                }, [&](float t) { return 0.0015f + 0.0015f * std::sin(kPi * t); }, 10);
                strokeLocal(crease, Vec4(lash, 0.45f));
            }
            if (P.tear)
                cp.fill([&](Vec2 p) { return sdEllipse(toLocal(p), Vec2{0.2f * W, yLo(0.2f * W) - 0.015f}, Vec2{0.018f, 0.026f}); },
                        Vec4(0.75f, 0.9f, 1.0f, 0.9f), lo, hi);
        }

        if (P.blush || f.blush) {
            Vec2 bc{side * 0.21f, 0.305f};
            float a = P.blush ? 0.45f : 0.25f;
            cp.fill([=](Vec2 p) { return sdEllipse(p, bc, {0.065f, 0.026f}); },
                    [=](Vec2 p) {
                        float k = 1 - saturate(length(Vec2{(p.x - bc.x) / 0.065f, (p.y - bc.y) / 0.026f}));
                        return Vec4(1.0f, 0.45f, 0.5f, a * std::sqrt(k));
                    }, bc - Vec2{0.07f, 0.03f}, bc + Vec2{0.07f, 0.03f});
            if (P.blush)
                for (int k = 0; k < 3; ++k) {
                    Vec2 a0 = bc + Vec2{-0.03f + 0.025f * k, -0.012f}, a1 = a0 + Vec2{0.012f, 0.022f};
                    Stroke s{{a0, a1}, {0.0025f, 0.0025f}};
                    cp.fill(s, Vec4(0.9f, 0.3f, 0.35f, 0.6f), a0 - Vec2{0.01f, 0.01f}, a1 + Vec2{0.01f, 0.01f});
                }
        }
    }
}

void paintMouth(CellPainter& cp, const Genome& g, MouthExpr e) {
    const FaceStyle& f = g.face;
    const Vec3 skin = g.palette.skin;
    const Vec3 line = lerp(skin * 0.4f, Vec3{0.38f, 0.10f, 0.12f}, 0.55f);
    const Vec3 inner{0.50f, 0.12f, 0.17f}, tongue{0.95f, 0.50f, 0.56f}, teeth{0.99f, 0.98f, 0.97f};
    const float my = 0.19f, mw = 0.045f * f.mouthWidth;
    const Vec2 lo{-0.15f, 0.08f}, hi{0.15f, 0.31f};

    // Nose: a tiny shadow tick (always present).
    {
        Stroke n{{{0.006f, 0.300f}, {-0.002f, 0.283f}}, {0.0035f, 0.0015f}};
        cp.fill(n, Vec4(skin * 0.62f, 0.9f), {-0.02f, 0.27f}, {0.02f, 0.31f});
    }
    auto arc = [&](float halfW, float depth, float lift, float wid, float asym = 0) {
        Stroke s = curveStroke([=](float t) {
            float x = lerp(-halfW, halfW, t), u = x / halfW;
            return Vec2{x, my + lift - depth * (1 - u * u) + asym * std::max(0.0f, u) * u};
        }, [=](float t) { return wid * (0.55f + 0.45f * std::sin(kPi * t)); }, 16);
        cp.fill(s, Vec4(line, 1), lo, hi);
    };
    auto openShape = [&](float halfW, float top, float bottom, float flatTop, bool showTeeth, bool showTongue) {
        auto yHi = [=](float x) {
            float u = x / halfW;
            return my + top * (1 - flatTop) * (1 - u * u) + top * flatTop;
        };
        auto yLo = [=](float x) {
            float u = clamp(x / halfW, -1.0f, 1.0f);
            return my - bottom * std::pow(std::max(0.0f, 1 - u * u), 0.6f);
        };
        auto sd = [=](Vec2 p) { return sdBetween(p, halfW, yHi, yLo); };
        cp.fill([=](Vec2 p) { return sd(p) - 0.003f; }, Vec4(line, 1), lo, hi);
        cp.fill(sd, Vec4(inner, 1), lo, hi);
        if (showTongue)
            cp.fill([=](Vec2 p) { return std::max(sd(p), sdEllipse(p, {0.0f, my - bottom * 0.95f}, {halfW * 0.7f, bottom * 0.55f})); },
                    Vec4(tongue, 1), lo, hi);
        if (showTeeth)
            cp.fill([=](Vec2 p) { return std::max(sd(p), (yHi(p.x) - bottom * 0.3f) - p.y); }, Vec4(teeth, 1), lo, hi);
        if (f.fang) {
            Vec2 a{halfW * 0.45f, yHi(halfW * 0.45f) + 0.002f};
            Stroke s{{a, a + Vec2{0.002f, -0.016f}}, {0.006f, 0.0008f}};
            cp.fill([&](Vec2 p) { return std::max(s(p), sd(p) - 0.004f); }, Vec4(teeth, 1), lo, hi);
        }
    };

    switch (e) {
        case MouthExpr::Neutral: arc(mw * 0.55f, -0.002f, 0, 0.0028f); break;
        case MouthExpr::Smile:
            arc(mw, 0.012f, 0.006f, 0.0032f);
            if (f.fang) {
                Vec2 a{mw * 0.45f, my + 0.006f - 0.012f * (1 - 0.2f) - 0.001f};
                Stroke s{{a, a + Vec2{0.001f, -0.012f}}, {0.005f, 0.0008f}};
                cp.fill(s, Vec4(teeth, 1), lo, hi);
                Stroke o{{a + Vec2{-0.005f, 0}, a + Vec2{0.001f, -0.013f}, a + Vec2{0.005f, 0}}, {0.0012f, 0.0012f, 0.0012f}};
                cp.fill(o, Vec4(line, 1), lo, hi);
            }
            break;
        case MouthExpr::Open: openShape(mw * 0.7f, 0.004f, 0.030f, 0.0f, false, true); break;
        case MouthExpr::Grin: openShape(mw * 1.15f, 0.010f, 0.040f, 0.85f, true, true); break;
        case MouthExpr::Frown: arc(mw * 0.8f, -0.012f, -0.008f, 0.003f); break;
        case MouthExpr::Shout: openShape(mw * 1.2f, 0.016f, 0.062f, 0.3f, true, true); break;
        case MouthExpr::Oh:
            cp.fill([&](Vec2 p) { return sdEllipse(p, {0, my - 0.01f}, {0.018f, 0.024f}) - 0.003f; }, Vec4(line, 1), lo, hi);
            cp.fill([&](Vec2 p) { return sdEllipse(p, {0, my - 0.01f}, {0.018f, 0.024f}); }, Vec4(inner, 1), lo, hi);
            break;
        case MouthExpr::Smirk: arc(mw * 0.8f, 0.004f, 0.0f, 0.003f, 0.016f); break;
        default: break;
    }
}

}  // namespace

Image paintFaceAtlas(const Genome& g) {
    const int W = FaceLayout::kAtlasW, H = FaceLayout::kAtlasH;
    std::vector<Vec4> px(size_t(W) * H, Vec4{g.palette.skin, 0});  // skin rgb under alpha 0 avoids dark fringes
    // Cells never overlap, so they are painted in parallel.
    auto eyeJob = [&](int i) {
        auto c = FaceLayout::eyeCell(EyeExpr(i));
        CellPainter cp(px, W, c[0], c[1], FaceLayout::kEyeCellW, FaceLayout::kEyeCellH, FaceLayout::kEyeX0,
                       FaceLayout::kEyeX1, FaceLayout::kEyeS0, FaceLayout::kEyeS1);
        paintEyes(cp, g, EyeExpr(i));
    };
    auto mouthJob = [&](int i) {
        auto c = FaceLayout::mouthCell(MouthExpr(i));
        CellPainter cp(px, W, c[0], c[1], FaceLayout::kMouthCellW, FaceLayout::kMouthCellH, FaceLayout::kMouthX0,
                       FaceLayout::kMouthX1, FaceLayout::kMouthS0, FaceLayout::kMouthS1);
        paintMouth(cp, g, MouthExpr(i));
    };
    const int jobs = int(EyeExpr::Count) + int(MouthExpr::Count);
    const int nThreads = std::clamp(int(std::thread::hardware_concurrency()), 1, 8);
    std::vector<std::thread> pool;
    for (int t = 0; t < nThreads; ++t)
        pool.emplace_back([&, t] {
            for (int j = t; j < jobs; j += nThreads) j < int(EyeExpr::Count) ? eyeJob(j) : mouthJob(j - int(EyeExpr::Count));
        });
    for (auto& th : pool) th.join();
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) img.set(x, y, px[size_t(y) * W + x]);
    return img;
}

}  // namespace cg
