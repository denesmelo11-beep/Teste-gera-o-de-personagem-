// Procedural 2D anime face painter. Every expression is drawn with signed
// distance shapes (anti-aliased) into cells of one atlas; the 3D decals pick
// cells by UV offset at runtime, so eyes and mouth combine freely.
#include <functional>
#include <thread>

#include "chargen/character.hpp"
#include "chargen/rng.hpp"

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

Vec2 rot(Vec2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

// Expression deltas on top of the character's resting face.
struct EyeParams {
    float open = 1;        // opening height scale
    float lidDrop = 0;     // upper lid lowered (flat cut), 0..1
    float lidTilt = 0;     // + lowers the inner end of the upper lid (anger), - the outer end (sadness)
    float lowerLift = 0;   // lower lid raised (smile / squint), 0..1
    float irisScale = 1;
    float browY = 0, browAngle = 0, browPinch = 0;
    int closed = 0;        // 1 relaxed closed, 2 squeezed shut
};

EyeParams eyeParams(EyeExpr e) {
    EyeParams p;
    switch (e) {
        case EyeExpr::Blink: p.closed = 1; break;
        case EyeExpr::Happy: p.lowerLift = 0.45f; p.open = 0.9f; p.browY = 0.004f; p.browAngle = 0.12f; break;
        case EyeExpr::Angry: p.lidDrop = 0.3f; p.lidTilt = 0.55f; p.browAngle = -0.6f; p.browY = -0.012f; p.browPinch = 0.012f; p.irisScale = 0.92f; break;
        case EyeExpr::Sad: p.lidTilt = -0.4f; p.open = 0.9f; p.browAngle = 0.55f; p.browPinch = 0.006f; break;
        case EyeExpr::Surprised: p.open = 1.3f; p.irisScale = 0.86f; p.browY = 0.022f; break;
        case EyeExpr::Hurt: p.closed = 2; p.browAngle = 0.35f; p.browY = -0.006f; p.browPinch = 0.014f; break;
        case EyeExpr::Smug: p.lidDrop = 0.42f; p.lowerLift = 0.15f; p.browAngle = -0.12f; break;
        default: break;
    }
    return p;
}

// Per-character eye geometry.
struct EyeShape {
    float W, Hh, peak, lowK, tilt;
};

EyeShape eyeShapeFor(const Genome& g) {
    const FaceStyle& f = g.face;
    EyeShape e{};
    e.W = 0.074f * f.eyeSize;
    switch (f.eyeShape) {
        case 1: e.Hh = 0.042f; e.peak = 0.0f; e.lowK = 0.75f; e.tilt = 0.0f; break;     // round
        case 2: e.Hh = 0.027f; e.peak = 0.15f; e.lowK = 0.50f; e.tilt = 0.02f; break;   // hooded
        case 3: e.Hh = 0.031f; e.peak = 0.25f; e.lowK = 0.45f; e.tilt = 0.12f; break;   // upturned
        case 4: e.Hh = 0.034f; e.peak = -0.20f; e.lowK = 0.62f; e.tilt = -0.10f; break; // downturned
        default: e.Hh = 0.033f; e.peak = 0.10f; e.lowK = 0.55f; e.tilt = 0.04f; break;  // almond
    }
    e.Hh *= f.eyeSize * (g.ageGroup == AgeGroup::Young ? 1.08f : 1.0f);
    e.tilt += f.eyeTilt;
    return e;
}

void paintEyes(CellPainter& cp, const Genome& g, EyeExpr expr) {
    const FaceStyle& f = g.face;
    const Palette& pal = g.palette;
    const EyeParams P = eyeParams(expr);
    const EyeShape S = eyeShapeFor(g);
    const float W = S.W, Hh = S.Hh;
    const float ey = 0.47f, exOff = 0.165f * f.eyeSpacing;
    const Vec3 lash = lerp(pal.brow * 0.4f, Vec3{0.08f, 0.05f, 0.05f}, 0.6f);
    const Vec3 skinDark = pal.skin * Vec3{0.70f, 0.56f, 0.54f};
    const float lidDrop = saturate(f.restLid + 0.18f * f.ageLines + P.lidDrop);
    const float lw = f.lashWeight;

    auto yUp = [&](float x) {
        float pk = S.peak * W;
        float uu = clamp(x < pk ? (x - pk) / (W + pk) : (x - pk) / (W - pk), -1.0f, 1.0f);
        float y = Hh * P.open * std::sqrt(std::max(0.0f, 1 - uu * uu));
        y -= Hh * P.lidTilt * (0.5f - 0.5f * x / W) * 0.9f;
        return std::min(y, Hh * (1.0f - lidDrop));
    };
    auto yLo = [&](float x) {
        float u = clamp((x + 0.1f * W) / (1.1f * W), -1.0f, 1.0f);
        float y = -Hh * S.lowK * std::pow(std::max(0.0f, 1 - u * u), 0.8f) * std::sqrt(P.open);
        return y + P.lowerLift * Hh * 0.6f * (1 - u * u);
    };

    for (float side : {1.0f, -1.0f}) {
        Vec2 c{side * exOff, ey};
        const float tilt = S.tilt;
        auto toLocal = [=](Vec2 p) { return rot(Vec2{(p.x - c.x) * side, p.y - c.y}, -tilt); };
        auto toFace = [=](Vec2 l) {
            Vec2 r = rot(l, tilt);
            return Vec2{c.x + r.x * side, c.y + r.y};
        };
        Vec2 lo = c - Vec2{W * 1.4f, Hh * 2.0f + 0.02f}, hi = c + Vec2{W * 1.4f, Hh * 2.0f + 0.02f};
        auto strokeLocal = [&](const Stroke& sl, Vec4 col) {
            Stroke st = sl;
            for (Vec2& p : st.pts) p = toFace(p);
            Vec2 a, b;
            st.bounds(a, b);
            cp.fill(st, col, a, b);
        };

        // Brow: a solid tapered shape, the main carrier of expression.
        {
            float by = Hh + 0.040f + f.browHeight + P.browY;
            float tiltB = f.browTilt + P.browAngle;
            Stroke st = curveStroke(
                [&](float t) {
                    float x = lerp(-0.95f * W + P.browPinch, 1.10f * W, t);
                    float y = by - tiltB * 0.045f * (t - 0.35f);
                    if (f.browShape == 1) y += 0.012f * bump((t - 0.55f) / 0.6f) - 0.006f * t;
                    else if (f.browShape == 2) y += 0.016f * std::min(t / 0.62f, 1.0f) - 0.03f * std::max(0.0f, (t - 0.62f) / 0.38f);
                    else y -= 0.004f * t;
                    return Vec2{x, y};
                },
                [&](float t) { return (0.007f + 0.010f * f.browThick) * (1.0f - 0.55f * t) + 0.002f; });
            strokeLocal(st, Vec4(pal.brow, 1.0f));
        }

        if (P.closed) {
            // Closed lids: the lash line on a gentle arc; squeezed adds crease lines.
            float sag = P.closed == 2 ? 0.25f : 0.12f;
            Stroke st = curveStroke([&](float t) {
                float x = lerp(-W, W * 1.02f, t), u = x / W;
                return Vec2{x, -sag * Hh * (1 - u * u) - 0.1f * Hh};
            }, [&](float t) { return 0.004f + (0.004f + 0.006f * lw) * std::sin(kPi * (0.15f + 0.85f * t)); });
            strokeLocal(st, Vec4(lash, 1));
            if (P.closed == 2)
                for (int k = 0; k < 2; ++k) {
                    float yy = Hh * (0.55f + 0.45f * k);
                    Stroke cr = curveStroke([&](float t) {
                        float x = lerp(-0.6f * W, 0.8f * W, t);
                        return Vec2{x, yy - 0.012f * (t - 0.5f) * (t - 0.5f)};
                    }, [](float t) { return 0.0015f + 0.002f * std::sin(kPi * t); }, 10);
                    strokeLocal(cr, Vec4(skinDark, 0.55f));
                }
        } else {
            auto openSdf = [&](Vec2 p) { return sdBetween(toLocal(p), W, yUp, yLo); };
            Vec3 sclera = lerp(Vec3{0.90f, 0.88f, 0.86f}, pal.skin, 0.12f);
            cp.fill(openSdf, [&](Vec2 p) {
                Vec2 l = toLocal(p);
                float sh = smoothstep(yUp(l.x) - 0.6f * Hh, yUp(l.x), l.y);
                return Vec4(lerp(sclera, sclera * Vec3{0.72f, 0.68f, 0.70f}, sh), 1);
            }, lo, hi);
            // Iris: big enough to read at low resolution; clipped by the lids.
            Vec2 ic{0.0f, -0.08f * Hh};
            float irR = std::max(0.55f * W, 1.05f * Hh) * P.irisScale;
            Vec2 ir{irR, irR};
            const Vec2 icF = toFace(ic);
            const Vec2 ilo = icF - Vec2{irR * 1.1f, irR * 1.1f}, ihi = icF + Vec2{irR * 1.1f, irR * 1.1f};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir)); },
                    [&](Vec2 p) {
                        Vec2 l = toLocal(p) - ic;
                        float top = smoothstep(-0.2f * irR, 0.6f * irR, l.y);
                        float rim = smoothstep(0.65f, 1.0f, length(l) / irR);
                        Vec3 col = lerp(pal.iris * 1.1f, pal.iris * 0.55f, top);
                        return Vec4(lerp(col, pal.iris * 0.35f, rim * 0.7f), 1);
                    }, ilo, ihi);
            float pr = irR * 0.42f;
            Sdf pupil = g.face.pupil == PupilStyle::Slit
                            ? Sdf([=](Vec2 p) { return sdEllipse(toLocal(p), ic, Vec2{0.18f * irR, 0.85f * irR}); })
                            : Sdf([=](Vec2 p) { return sdEllipse(toLocal(p), ic, Vec2{pr, pr}); });
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), pupil(p)); }, Vec4(pal.iris * 0.08f, 1), ilo, ihi);
            Vec2 hl = ic + Vec2{-0.35f * irR, 0.35f * irR};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), hl, Vec2{0.20f * irR, 0.20f * irR})); },
                    Vec4(1, 1, 1, 0.85f), ilo, ihi);
            // Upper lash line: the defining dark stroke.
            Stroke up = curveStroke([&](float x01) {
                float x = lerp(-1.02f * W, 1.06f * W, x01);
                return Vec2{x, yUp(x) + 0.002f};
            }, [&](float t) { return 0.0045f + (0.0035f + 0.006f * lw) * smoothstep(0.0f, 0.85f, t); });
            if (lw > 0.6f) {
                Vec2 end = up.pts.back();
                up.pts.push_back(end + Vec2{0.014f * lw, 0.006f + 0.004f * lw});
                up.w.push_back(0.0015f);
            }
            strokeLocal(up, Vec4(lash, 1));
            // Lower lid: a soft line; lid crease above.
            Stroke lower = curveStroke([&](float t) {
                float x = lerp(-0.7f * W, 1.0f * W, t);
                return Vec2{x, yLo(x) - 0.0015f};
            }, [&](float t) { return 0.0018f + 0.0018f * t; }, 12);
            strokeLocal(lower, Vec4(lerp(lash, skinDark, 0.5f), 0.55f));
            Stroke crease = curveStroke([&](float t) {
                float x = lerp(-0.5f * W, 0.95f * W, t);
                return Vec2{x, std::max(yUp(x), Hh * 0.5f) + 0.013f + 0.004f * lidDrop};
            }, [&](float t) { return 0.0015f + 0.0022f * std::sin(kPi * t); }, 12);
            strokeLocal(crease, Vec4(skinDark, 0.40f));
        }
        // Age: crow's feet and the under-eye fold.
        if (f.ageLines > 0.05f) {
            float a = f.ageLines;
            for (int k = 0; k < 2; ++k) {
                Vec2 p0{W * 1.12f, -0.004f + 0.012f * k}, p1{W * 1.12f + 0.022f, -0.012f + 0.02f * k};
                Stroke st{{p0, p1}, {0.0018f, 0.0008f}};
                strokeLocal(st, Vec4(skinDark, 0.45f * a));
            }
            Stroke bag = curveStroke([&](float t) {
                float x = lerp(-0.5f * W, 0.8f * W, t);
                return Vec2{x, -Hh * 0.9f - 0.016f - 0.004f * std::sin(kPi * t)};
            }, [](float t) { return 0.0012f + 0.0018f * std::sin(kPi * t); }, 10);
            strokeLocal(bag, Vec4(skinDark, 0.45f * a));
        }
        if (f.blush) {
            Vec2 bc{side * 0.20f, 0.37f};
            cp.fill([=](Vec2 p) { return sdEllipse(p, bc, {0.06f, 0.024f}); },
                    [=](Vec2 p) {
                        float k = 1 - saturate(length(Vec2{(p.x - bc.x) / 0.06f, (p.y - bc.y) / 0.024f}));
                        return Vec4(0.9f, 0.45f, 0.45f, 0.22f * std::sqrt(k));
                    }, bc - Vec2{0.07f, 0.03f}, bc + Vec2{0.07f, 0.03f});
        }
        if (f.freckles) {
            Rng fr(g.seed ^ 0xF4ECull, uint64_t(side > 0 ? 1 : 2));
            for (int k = 0; k < 8; ++k) {
                Vec2 q{side * fr.range(0.07f, 0.24f), fr.range(0.34f, 0.40f)};
                float rr = fr.range(0.003f, 0.0045f);
                cp.fill([=](Vec2 p) { return length(p - q) - rr; }, Vec4(pal.skin * 0.66f, 0.5f), q - Vec2{0.01f, 0.01f}, q + Vec2{0.01f, 0.01f});
            }
        }
    }
    if (f.scar) {  // a pale slash through the left brow onto the cheek
        Vec2 a0{0.12f, 0.60f}, a1{0.21f, 0.38f};
        Stroke st{{a0, (a0 + a1) * 0.5f + Vec2{0.008f, 0}, a1}, {0.003f, 0.006f, 0.0025f}};
        cp.fill(st, Vec4(lerp(pal.skin, Vec3{0.95f, 0.85f, 0.82f}, 0.5f), 0.95f), {0.09f, 0.35f}, {0.25f, 0.63f});
        Stroke edge{{a0 + Vec2{0.005f, 0}, a1 + Vec2{0.005f, 0}}, {0.0012f, 0.0012f}};
        cp.fill(edge, Vec4(pal.skin * 0.6f, 0.5f), {0.09f, 0.35f}, {0.25f, 0.63f});
    }
}

void paintMouth(CellPainter& cp, const Genome& g, MouthExpr e) {
    const FaceStyle& f = g.face;
    const Vec3 skin = g.palette.skin;
    const float lf = f.lipFullness;
    const Vec3 lipC = lerp(skin * Vec3{0.92f, 0.80f, 0.78f}, g.palette.lips, lerp(0.35f, 0.9f, lf));
    const Vec3 line = lerp(skin * 0.35f, Vec3{0.28f, 0.10f, 0.10f}, 0.5f);
    const Vec3 inner{0.30f, 0.08f, 0.10f}, teeth{0.92f, 0.90f, 0.86f}, tongue{0.75f, 0.40f, 0.42f};
    const Vec3 skinDark = skin * Vec3{0.72f, 0.58f, 0.56f};
    const float my = 0.235f, mw = 0.052f * f.mouthWidth;
    const Vec2 lo{-0.155f, 0.12f}, hi{0.155f, 0.35f};

    // Age: nasolabial folds from the nostril wings towards the mouth corners.
    if (f.ageLines > 0.05f)
        for (float sd : {1.0f, -1.0f}) {
            Stroke st = curveStroke([=](float t) {
                return Vec2{sd * (0.058f + 0.022f * t + 0.008f * std::sin(kPi * t)), lerp(0.305f, 0.215f, t)};
            }, [](float t) { return 0.0015f + 0.0025f * std::sin(kPi * t); }, 10);
            cp.fill(st, Vec4(skinDark, 0.5f * f.ageLines), lo, hi);
        }

    auto lips = [&](float curve, float asym) {
        curve += f.mouthRest;
        asym += f.mouthAsym;
        auto yM = [=](float x) {
            float u = x / mw;
            return my + curve * u * u + asym * u * std::fabs(u);
        };
        const float Hu = 0.004f + 0.007f * lf, Hl = 0.006f + 0.010f * lf;
        auto yTop = [=](float x) {
            float u = std::fabs(x / mw);
            return yM(x) + Hu * std::pow(std::max(0.0f, 1 - u), 0.8f) * (1 - 0.4f * bump(x / (0.22f * mw)));
        };
        auto yBot = [=](float x) {
            float u = clamp(x / (0.84f * mw), -1.0f, 1.0f);
            return yM(x) - Hl * std::pow(std::max(0.0f, 1 - u * u), 0.6f);
        };
        cp.fill([=](Vec2 p) { return sdBetween(p, 0.84f * mw, yM, yBot); }, Vec4(lipC, 0.85f), lo, hi);
        cp.fill([=](Vec2 p) { return sdBetween(p, mw, yTop, yM); }, Vec4(lipC * 0.82f, 0.85f), lo, hi);
        Stroke ml = curveStroke([=](float t) {
            float x = lerp(-mw, mw, t);
            return Vec2{x, yM(x)};
        }, [](float t) { return 0.0025f + 0.0015f * std::sin(kPi * t); }, 16);
        cp.fill(ml, Vec4(line, 1), lo, hi);
        Stroke under = curveStroke([=](float t) {
            float x = lerp(-0.45f * mw, 0.45f * mw, t);
            return Vec2{x, yBot(x) - 0.006f};
        }, [](float t) { return 0.0025f * std::sin(kPi * t) + 0.0005f; }, 8);
        cp.fill(under, Vec4(skinDark, 0.35f), lo, hi);
    };
    auto openMouth = [&](float halfW, float top, float bottom, float curve, bool upperTeeth, bool lowerTeeth) {
        curve += f.mouthRest;
        auto yHi = [=](float x) {
            float u = x / halfW;
            return my + top * (1 - u * u) + curve * u * u;
        };
        auto yLo = [=](float x) {
            float u = clamp(x / halfW, -1.0f, 1.0f);
            return my - bottom * std::pow(std::max(0.0f, 1 - u * u), 0.7f) + curve * u * u;
        };
        auto sd = [=](Vec2 p) { return sdBetween(p, halfW, yHi, yLo); };
        const float rim = 0.003f + 0.005f * lf;
        cp.fill([=](Vec2 p) { return std::min(sd(p) - rim, sd(p + Vec2{0, rim}) - rim * 1.2f); }, Vec4(lipC, 0.9f), lo, hi);
        cp.fill([=](Vec2 p) { return sd(p) - 0.0018f; }, Vec4(line, 1), lo, hi);
        cp.fill(sd, Vec4(inner, 1), lo, hi);
        cp.fill([=](Vec2 p) { return std::max(sd(p), sdEllipse(p, {0.0f, my - bottom * 0.9f}, {halfW * 0.6f, bottom * 0.45f})); },
                Vec4(tongue, 1), lo, hi);
        if (upperTeeth) cp.fill([=](Vec2 p) { return std::max(sd(p), (yHi(p.x) - 0.007f) - p.y); }, Vec4(teeth, 1), lo, hi);
        if (lowerTeeth) cp.fill([=](Vec2 p) { return std::max(sd(p), p.y - (yLo(p.x) + 0.005f)); }, Vec4(teeth * 0.95f, 1), lo, hi);
        if (f.fang) {
            Vec2 a{halfW * 0.5f, yHi(halfW * 0.5f)};
            Stroke st{{a, a + Vec2{0.001f, -0.011f}}, {0.0045f, 0.0008f}};
            cp.fill([&](Vec2 p) { return std::max(st(p), sd(p) - 0.002f); }, Vec4(teeth, 1), lo, hi);
        }
    };

    switch (e) {
        case MouthExpr::Neutral: lips(-0.001f, 0); break;
        case MouthExpr::Smile: lips(0.010f, 0); break;
        case MouthExpr::Open: openMouth(mw * 0.62f, 0.003f, 0.016f, 0.0f, false, false); break;
        case MouthExpr::Grin: openMouth(mw * 0.95f, 0.004f, 0.016f, 0.010f, true, false); break;
        case MouthExpr::Frown: lips(-0.009f, 0); break;
        case MouthExpr::Shout: openMouth(mw * 0.85f, 0.008f, 0.036f, -0.004f, true, true); break;
        case MouthExpr::Oh: openMouth(mw * 0.38f, 0.008f, 0.018f, 0.0f, false, false); break;
        case MouthExpr::Smirk: lips(0.002f, 0.009f); break;
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
