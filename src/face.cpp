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

// Per-character eye geometry: shape family + maturity.
struct EyeShape {
    float W, Hh, peak, lowK, tilt, irisR, hiScale, crease;
    bool wing;
};

EyeShape eyeShapeFor(const Genome& g) {
    const FaceStyle& f = g.face;
    const float mat = f.maturity;
    EyeShape e{};
    e.W = 0.097f * f.eyeSize * (0.95f + 0.12f * (1 - mat));
    switch (f.eyeShape) {
        case 1: e.Hh = 0.078f; e.peak = 0.0f; e.lowK = 0.78f; e.tilt = 0.0f; break;     // round
        case 2: e.Hh = 0.050f; e.peak = 0.15f; e.lowK = 0.45f; e.tilt = 0.02f; break;   // hooded / narrow
        case 3: e.Hh = 0.058f; e.peak = 0.30f; e.lowK = 0.42f; e.tilt = 0.15f; break;   // upturned (cat)
        case 4: e.Hh = 0.064f; e.peak = -0.20f; e.lowK = 0.62f; e.tilt = -0.10f; break; // downturned (soft)
        default: e.Hh = 0.062f; e.peak = 0.10f; e.lowK = 0.55f; e.tilt = 0.05f; break;  // almond
    }
    e.Hh *= f.eyeSize * (1.0f + 0.3f * (1 - mat));
    e.tilt += f.eyeTilt * 0.5f;
    e.irisR = e.W * (0.60f - 0.10f * mat);  // absolute: narrow eyes clip the iris, like real eyes
    e.hiScale = 1.0f - 0.45f * mat;
    e.crease = f.eyeShape == 2 ? 0.010f : 0.019f;
    e.wing = g.femininity > 0.5f && (f.eyeShape == 3 || f.eyeShape == 0) && f.lashWeight > 0.55f;
    return e;
}

void paintEyes(CellPainter& cp, const Genome& g, EyeExpr expr) {
    const FaceStyle& f = g.face;
    const Palette& pal = g.palette;
    const EyeParams P = eyeParams(expr);
    const EyeShape S = eyeShapeFor(g);
    const float W = S.W, Hh = S.Hh, mat = f.maturity;
    const float ey = 0.41f, exOff = 0.170f * f.eyeSpacing;
    const Vec3 lash = lerp(pal.hair * 0.22f, Vec3{0.10f, 0.06f, 0.09f}, 0.6f);
    const Vec3 brow = lerp(pal.hair * 0.6f, lash, 0.25f);
    const Vec3 skinDark = pal.skin * Vec3{0.72f, 0.58f, 0.58f};
    const float lw = f.lashWeight;

    auto yUp = [&](float x) {
        float pk = S.peak * W;
        float uu = x < pk ? (x - pk) / (W + pk) : (x - pk) / (W - pk);
        uu = clamp(uu, -1.0f, 1.0f);
        float y = Hh * P.open * std::sqrt(std::max(0.0f, 1 - uu * uu));
        y -= Hh * P.lidTiltIn * (0.5f - 0.5f * x / W) * 0.9f;
        return std::min(y, Hh * (1 - P.lidFlat));
    };
    auto yLo = [&](float x) {
        float u = clamp((x + 0.1f * W) / (1.1f * W), -1.0f, 1.0f);
        return -Hh * S.lowK * std::pow(std::max(0.0f, 1 - u * u), 0.8f) * std::sqrt(P.open);
    };

    for (float side : {1.0f, -1.0f}) {
        Vec2 c{side * exOff, ey};
        float tilt = S.tilt;
        auto toLocal = [=](Vec2 p) { return rot(Vec2{(p.x - c.x) * side, p.y - c.y}, -tilt); };
        auto toFace = [=](Vec2 l) {
            Vec2 r = rot(l, tilt);
            return Vec2{c.x + r.x * side, c.y + r.y};
        };
        Vec2 lo = c - Vec2{W * 1.3f, Hh * 1.6f + 0.02f}, hi = c + Vec2{W * 1.3f, Hh * 1.6f + 0.06f};
        auto strokeLocal = [&](const Stroke& sl, Vec4 col) {
            Stroke st = sl;
            for (Vec2& p : st.pts) p = toFace(p);
            Vec2 a, b;
            st.bounds(a, b);
            cp.fill(st, col, a, b);
        };

        // Eyeshadow: a soft wash between lid and brow.
        if (f.eyeShadow > 0.02f && P.special != 3) {
            const float band = 0.035f + 0.015f * f.eyeShadow;
            cp.fill([&](Vec2 p) {
                Vec2 l = toLocal(p);
                return sdBetween(l, W * 1.12f, [&](float x) { return std::max(yUp(x), 0.0f) + band * (1 - 0.6f * (x / W) * (x / W)); },
                                 [&](float x) { return std::max(yUp(x), 0.0f) - 0.004f; });
            }, [&](Vec2 p) {
                Vec2 l = toLocal(p);
                float k = saturate((l.y - std::max(yUp(l.x), 0.0f)) / band);
                return Vec4(pal.eyeShadow, f.eyeShadow * 0.55f * (1 - k) * (1 - k));
            }, lo, hi);
        }

        // Brow.
        {
            float by = Hh * 0.95f + (0.052f - 0.014f * mat) + P.browY;
            Stroke st = curveStroke(
                [&](float t) {
                    float x = lerp(-0.80f * W, 1.05f * W, t);
                    float y = by;
                    if (f.browShape == 0) y += 0.006f * t - 0.004f;
                    else if (f.browShape == 1) y += 0.020f * bump((t - 0.6f) / 0.6f) - 0.010f * t;
                    else y += 0.026f * std::min(t / 0.65f, 1.0f) - 0.045f * std::max(0.0f, (t - 0.65f) / 0.35f);
                    y += 0.018f * P.browArch * bump((t - 0.55f) / 0.7f) - 0.018f * bump((t - 0.55f) / 0.7f);
                    y += P.browAngle * (0.5f - t) * 0.09f;
                    return Vec2{x, y};
                },
                [&](float t) { return (0.004f + 0.010f * f.browThick) * (1.0f - 0.6f * t) + 0.0015f; });
            strokeLocal(st, Vec4(brow, 0.95f));
        }

        if (P.special == 1 || P.special == 2) {  // closed / happy arcs
            float dir = P.special == 1 ? -1.0f : 1.0f;
            Stroke st = curveStroke(
                [&](float t) {
                    float x = lerp(-W, W * 1.05f, t), u = x / W;
                    return Vec2{x, dir * 0.40f * Hh * (1 - u * u) - 0.12f * Hh};
                },
                [&](float t) { return 0.003f + (0.006f + 0.008f * lw) * std::sin(kPi * (0.15f + 0.85f * t)); });
            strokeLocal(st, Vec4(lash, 1));
        } else if (P.special == 3) {  // > <
            Stroke st{{{W * 0.9f, Hh * 0.6f}, {-W * 0.55f, 0.0f}, {W * 0.9f, -Hh * 0.6f}}, {0.005f, 0.010f, 0.005f}};
            strokeLocal(st, Vec4(lash, 1));
        } else {
            auto openSdf = [&](Vec2 p) { return sdBetween(toLocal(p), W, yUp, yLo); };
            // Sclera with upper-lid shadow (mature eyes get a warmer, less white sclera).
            Vec3 sclera = lerp(Vec3{0.98f, 0.97f, 1.0f}, Vec3{0.95f, 0.92f, 0.90f}, mat);
            cp.fill(openSdf, [&](Vec2 p) {
                Vec2 l = toLocal(p);
                float sh = smoothstep(yUp(l.x) - 0.45f * Hh, yUp(l.x) - 0.05f * Hh, l.y);
                return Vec4(lerp(sclera, sclera * Vec3{0.78f, 0.74f, 0.80f}, sh), 1);
            }, lo, hi);
            // Iris: dark rim, radial-ish gradient, clipped by the lids.
            Vec2 ic{0.0f, -0.06f * Hh};
            Vec2 ir{S.irisR * P.irisScale, S.irisR * 1.10f * P.irisScale};
            const Vec2 icF = toFace(ic);
            const float irMax = std::max(ir.x, ir.y) * 1.05f;
            const Vec2 ilo = icF - Vec2{irMax, irMax}, ihi = icF + Vec2{irMax, irMax};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir)); }, Vec4(pal.iris * 0.30f, 1), ilo, ihi);
            Vec2 ir2 = ir * 0.84f;
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir2)); },
                    [&](Vec2 p) {
                        Vec2 l = toLocal(p) - ic;
                        float t = saturate((l.y + ir2.y) / (2 * ir2.y));
                        float rr = length(Vec2{l.x / ir2.x, l.y / ir2.y});
                        Vec3 col = t > 0.55f ? lerp(pal.iris, pal.iris * 0.40f, (t - 0.55f) / 0.45f) : lerp(pal.iris2, pal.iris, t / 0.55f);
                        col = lerp(col, pal.iris * 0.6f, 0.35f * smoothstep(0.55f, 1.0f, rr) * mat);  // limbal darkening
                        return Vec4(col, 1);
                    }, ilo, ihi);
            // Pupil.
            Vec2 pc = ic + Vec2{0, 0.03f * Hh};
            Vec4 pcol(pal.iris * 0.10f, 1);
            float ps = P.pupilScale * (1.0f - 0.25f * mat);
            Sdf pupil;
            switch (f.pupil) {
                case PupilStyle::Slit: pupil = [=](Vec2 p) { return sdEllipse(toLocal(p), pc, Vec2{0.08f * W, 0.9f * ir.y} * ps); }; break;
                case PupilStyle::Ring:
                    pupil = [=](Vec2 p) { return std::fabs(sdEllipse(toLocal(p), pc, Vec2{0.42f * ir.x, 0.42f * ir.y} * ps)) - 0.005f; };
                    break;
                default: pupil = [=](Vec2 p) { return sdEllipse(toLocal(p), pc, Vec2{0.42f * ir.x, 0.42f * ir.y} * ps); }; break;
            }
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), pupil(p)); }, pcol, ilo, ihi);
            // Highlights (smaller and fewer on mature faces).
            float hs = S.hiScale * (P.sparkle ? 1.35f : 1.0f);
            Vec2 h1 = ic + Vec2{-0.32f * ir.x, 0.40f * ir.y}, h2 = ic + Vec2{0.32f * ir.x, -0.45f * ir.y};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), h1, Vec2{0.32f * ir.x, 0.30f * ir.y} * hs)); },
                    Vec4(1, 1, 1, 0.92f), ilo, ihi);
            if (mat < 0.75f || P.sparkle)
                cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), h2, Vec2{0.14f * ir.x, 0.14f * ir.x} * hs)); },
                        Vec4(1, 1, 1, 0.8f), ilo, ihi);
            // Upper lash line (+ eyeliner wing).
            Stroke up = curveStroke(
                [&](float t) {
                    float x = lerp(-1.02f * W, 1.03f * W, t);
                    return Vec2{x, yUp(x) + 0.003f};
                },
                [&](float t) { return 0.0025f + (0.004f + 0.011f * lw) * smoothstep(0.0f, 0.8f, t); });
            if (S.wing) {
                Vec2 end = up.pts.back();
                up.pts.push_back(end + Vec2{0.028f * lw, 0.016f * lw + 0.004f});
                up.w.push_back(0.0015f);
            }
            strokeLocal(up, Vec4(lash, 1));
            // Lower lid: lashes for heavy-lash faces, a soft lid line otherwise.
            Stroke lower = curveStroke([&](float t) {
                float x = lerp(-0.6f * W, 1.0f * W, t);
                return Vec2{x, yLo(x) - 0.002f};
            }, [&](float t) { return 0.0012f + 0.003f * lw * t; }, 12);
            strokeLocal(lower, Vec4(lash, 0.35f + 0.5f * lw));
            // Lid crease (stronger with maturity) and a hint of under-eye structure.
            if (P.lidFlat == 0 && P.lidTiltIn <= 0) {
                Stroke crease = curveStroke([&](float t) {
                    float x = lerp(-0.4f * W, 0.95f * W, t);
                    return Vec2{x, yUp(x) + S.crease};
                }, [&](float t) { return 0.0012f + 0.0018f * std::sin(kPi * t); }, 12);
                strokeLocal(crease, Vec4(lerp(lash, skinDark, 0.5f), 0.30f + 0.35f * mat));
            }
            if (mat > 0.5f) {
                Stroke under = curveStroke([&](float t) {
                    float x = lerp(-0.2f * W, 0.85f * W, t);
                    return Vec2{x, yLo(x) - 0.016f - 0.004f * std::sin(kPi * t)};
                }, [&](float t) { return 0.0018f * std::sin(kPi * t) + 0.0005f; }, 10);
                strokeLocal(under, Vec4(skinDark, 0.30f * (mat - 0.5f) * 2));
            }
            if (P.tear)
                cp.fill([&](Vec2 p) { return sdEllipse(toLocal(p), Vec2{0.2f * W, yLo(0.2f * W) - 0.014f}, Vec2{0.014f, 0.020f}); },
                        Vec4(0.75f, 0.9f, 1.0f, 0.9f), lo, hi);
        }

        if (P.blush || f.blush) {
            Vec2 bc{side * 0.21f, 0.305f};
            float a = P.blush ? 0.40f : 0.22f;
            cp.fill([=](Vec2 p) { return sdEllipse(p, bc, {0.065f, 0.026f}); },
                    [=](Vec2 p) {
                        float k = 1 - saturate(length(Vec2{(p.x - bc.x) / 0.065f, (p.y - bc.y) / 0.026f}));
                        return Vec4(0.95f, 0.45f, 0.5f, a * std::sqrt(k));
                    }, bc - Vec2{0.07f, 0.03f}, bc + Vec2{0.07f, 0.03f});
        }
        if (f.freckles) {
            Rng fr(g.seed ^ 0xF4ECull, uint64_t(side > 0 ? 1 : 2));
            for (int k = 0; k < 9; ++k) {
                Vec2 q{side * fr.range(0.08f, 0.26f), fr.range(0.28f, 0.33f)};
                float rr = fr.range(0.0025f, 0.004f);
                cp.fill([=](Vec2 p) { return length(p - q) - rr; }, Vec4(pal.skin * 0.62f, 0.55f), q - Vec2{0.01f, 0.01f}, q + Vec2{0.01f, 0.01f});
            }
        }
    }
    if (f.scar) {  // a pale slash across the left brow and cheek
        Vec2 a0{0.11f, 0.53f}, a1{0.22f, 0.30f};
        Stroke st{{a0, (a0 + a1) * 0.5f + Vec2{0.01f, 0}, a1}, {0.002f, 0.005f, 0.002f}};
        cp.fill(st, Vec4(lerp(pal.skin, Vec3{1, 0.9f, 0.88f}, 0.5f), 0.9f), {0.08f, 0.27f}, {0.26f, 0.56f});
        Stroke edge{{a0 + Vec2{0.004f, 0}, a1 + Vec2{0.004f, 0}}, {0.001f, 0.001f}};
        cp.fill(edge, Vec4(pal.skin * 0.6f, 0.5f), {0.08f, 0.27f}, {0.26f, 0.56f});
    }
}

void paintMouth(CellPainter& cp, const Genome& g, MouthExpr e) {
    const FaceStyle& f = g.face;
    const Vec3 skin = g.palette.skin;
    const float mat = f.maturity, lf = f.lipFullness;
    const Vec3 lipC = lerp(skin * Vec3{0.92f, 0.78f, 0.78f}, g.palette.lips, lerp(0.25f, 1.0f, lf));
    const Vec3 line = lerp(skin * 0.38f, Vec3{0.32f, 0.08f, 0.10f}, 0.55f);
    const Vec3 inner{0.42f, 0.10f, 0.14f}, tongue{0.88f, 0.46f, 0.52f}, teeth{0.97f, 0.96f, 0.94f};
    const float my = 0.19f, mw = 0.046f * f.mouthWidth * (1.0f + 0.12f * mat);
    const Vec2 lo{-0.15f, 0.08f}, hi{0.15f, 0.31f};
    const float ns = f.noseSize;

    // Nose: a small tick when youthful, tip + nostrils + side shadow when mature.
    if (mat < 0.9f) {
        Stroke n{{{0.006f, 0.300f}, {-0.002f, 0.284f}}, {0.003f, 0.0012f}};
        cp.fill(n, Vec4(skin * 0.62f, 0.85f * (1 - mat)), {-0.02f, 0.27f}, {0.02f, 0.31f});
    }
    if (mat > 0.3f) {
        float a = (mat - 0.3f) / 0.7f;
        Stroke tip = curveStroke([=](float t) {
            float x = lerp(-0.013f, 0.013f, t) * ns;
            return Vec2{x, 0.282f - 0.004f * std::sin(kPi * t)};
        }, [](float t) { return 0.0008f + 0.0014f * std::sin(kPi * t); }, 8);
        cp.fill(tip, Vec4(skin * 0.55f, 0.55f * a), {-0.03f, 0.27f}, {0.03f, 0.30f});
        for (float sd : {1.0f, -1.0f}) {
            Stroke nos{{{sd * 0.009f * ns, 0.2795f}, {sd * 0.015f * ns, 0.2815f}}, {0.0016f, 0.0010f}};
            cp.fill(nos, Vec4(skin * 0.42f, 0.6f * a), {-0.03f, 0.27f}, {0.03f, 0.29f});
        }
        Stroke shadow{{{-0.013f * ns, 0.345f}, {-0.016f * ns, 0.300f}, {-0.017f * ns, 0.288f}}, {0.003f, 0.005f, 0.004f}};
        cp.fill(shadow, Vec4(skin * 0.7f, 0.16f * a), {-0.03f, 0.28f}, {0.0f, 0.31f});
    }

    // Closed lips: cupid's-bow upper lip, fuller lower lip with a highlight.
    auto lips = [&](float curve, float asym) {
        auto yM = [=](float x) {
            float u = x / mw;
            return my + curve * u * u + asym * std::max(0.0f, u) * u;
        };
        const float Hu = 0.0035f + 0.008f * lf, Hl = 0.005f + 0.012f * lf;
        auto yTop = [=](float x) {
            float u = std::fabs(x / mw);
            return yM(x) + Hu * std::pow(std::max(0.0f, 1 - u), 0.8f) * (1 - 0.45f * bump(x / (0.22f * mw)));
        };
        auto yBot = [=](float x) {
            float u = clamp(x / (0.84f * mw), -1.0f, 1.0f);
            return yM(x) - Hl * std::pow(std::max(0.0f, 1 - u * u), 0.6f);
        };
        cp.fill([=](Vec2 p) { return sdBetween(p, 0.84f * mw, yM, yBot); }, Vec4(lipC, 0.9f), lo, hi);
        cp.fill([=](Vec2 p) { return sdEllipse(p, {0.0f, yM(0) - Hl * 0.45f}, {0.28f * mw, 0.22f * Hl}); },
                Vec4(lerp(lipC, Vec3{1, 1, 1}, 0.35f), 0.35f * lf), lo, hi);
        cp.fill([=](Vec2 p) { return sdBetween(p, mw, yTop, yM); }, Vec4(lipC * 0.86f, 0.9f), lo, hi);
        Stroke ml = curveStroke([=](float t) {
            float x = lerp(-mw, mw, t);
            return Vec2{x, yM(x)};
        }, [](float t) { return 0.0012f + 0.0013f * std::sin(kPi * t); }, 16);
        cp.fill(ml, Vec4(line, 1), lo, hi);
        // Mouth corners and the soft shadow under the lower lip.
        Stroke under = curveStroke([=](float t) {
            float x = lerp(-0.45f * mw, 0.45f * mw, t);
            return Vec2{x, yBot(x) - 0.006f};
        }, [](float t) { return 0.0015f * std::sin(kPi * t) + 0.0004f; }, 8);
        cp.fill(under, Vec4(skin * 0.7f, 0.25f * mat), lo, hi);
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
        // Lip rim first (thicker below), then the dark line and the interior.
        const float rim = 0.003f + 0.006f * lf;
        cp.fill([=](Vec2 p) { return std::min(sd(p) - rim, sd(p + Vec2{0, rim * 0.8f}) - rim * 1.2f); }, Vec4(lipC, 0.95f), lo, hi);
        cp.fill([=](Vec2 p) { return sd(p) - 0.0018f; }, Vec4(line, 1), lo, hi);
        cp.fill(sd, Vec4(inner, 1), lo, hi);
        if (showTongue)
            cp.fill([=](Vec2 p) { return std::max(sd(p), sdEllipse(p, {0.0f, my - bottom * 0.95f}, {halfW * 0.7f, bottom * 0.55f})); },
                    Vec4(tongue, 1), lo, hi);
        if (showTeeth)
            cp.fill([=](Vec2 p) { return std::max(sd(p), (yHi(p.x) - bottom * 0.3f) - p.y); }, Vec4(teeth, 1), lo, hi);
        if (f.fang) {
            Vec2 a{halfW * 0.45f, yHi(halfW * 0.45f) + 0.002f};
            Stroke st{{a, a + Vec2{0.002f, -0.014f}}, {0.005f, 0.0008f}};
            cp.fill([&](Vec2 p) { return std::max(st(p), sd(p) - 0.003f); }, Vec4(teeth, 1), lo, hi);
        }
    };

    switch (e) {
        case MouthExpr::Neutral: lips(-0.002f, 0); break;
        case MouthExpr::Smile: lips(0.012f, 0); break;
        case MouthExpr::Open: openShape(mw * 0.65f, 0.004f, 0.026f, 0.0f, false, true); break;
        case MouthExpr::Grin: openShape(mw * 1.05f, 0.008f, 0.034f, 0.85f, true, true); break;
        case MouthExpr::Frown: lips(-0.010f, 0); break;
        case MouthExpr::Shout: openShape(mw * 1.1f, 0.014f, 0.056f, 0.3f, true, true); break;
        case MouthExpr::Oh: openShape(mw * 0.42f, 0.010f, 0.024f, 0.0f, false, false); break;
        case MouthExpr::Smirk: lips(0.0f, 0.013f); break;
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
