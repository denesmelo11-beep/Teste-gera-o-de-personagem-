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
        scaleY_ = chh / (s1 - s0);
    }

    void fill(const Sdf& sdf, const ColorFn& col, Vec2 lo, Vec2 hi) {
        int ix0 = std::max(0, int((lo.x - x0_) * scale_) - 2), ix1 = std::min(cw_ - 1, int((hi.x - x0_) * scale_) + 2);
        int iy0 = std::max(0, int((s1_ - hi.y) * scaleY_) - 2), iy1 = std::min(ch_ - 1, int((s1_ - lo.y) * scaleY_) + 2);
        // Leave a 1px transparent frame so bilinear filtering never bleeds between cells.
        ix0 = std::max(ix0, 1); iy0 = std::max(iy0, 1);
        ix1 = std::min(ix1, cw_ - 2); iy1 = std::min(iy1, ch_ - 2);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) {
                Vec2 p{x0_ + (x + 0.5f) / scale_, s1_ - (y + 0.5f) / scaleY_};
                float d = sdf(p) * std::min(scale_, scaleY_);
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
    float x0_, x1_, s0_, s1_, scale_, scaleY_;
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
    // Slopes are clamped: where a curve's tangent turns vertical (lid corners) an
    // unclamped normalisation would shrink distances to ~0 and leak thin slivers.
    float dh = clamp((hi(x + e) - hi(x - e)) / (2 * e), -2.5f, 2.5f), dl = clamp((lo(x + e) - lo(x - e)) / (2 * e), -2.5f, 2.5f);
    float a = (p.y - hi(x)) / std::sqrt(1 + dh * dh);
    float b = (lo(x) - p.y) / std::sqrt(1 + dl * dl);
    float c = std::fabs(p.x) - W;
    return std::max({a, b, c});
}

Vec3 min3(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }

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
    float W, Hh;      // half width; height of the upper lid above the eye line
    float lowK;       // depth of the lower lid (in Hh)
    float slant;      // how much lower the inner end of the upper lid sits (in Hh); < 0 droops outwards
    float bow;        // convexity of the upper lid
    float squareK;    // lower-lid corner squareness (2 round .. 5 square)
    float iris;       // iris size factor
    float tilt;       // whole-eye rotation
};

// Seven eye families in the reference style (a heavy, nearly straight upper lash,
// a wide opening about half as tall as it is wide, a big round iris cropped by
// both lids): standard, round, narrow, sharp, droopy, hooded, small.
EyeShape eyeShapeFor(const Genome& g) {
    const FaceStyle& f = g.face;
    EyeShape e{};
    switch (f.eyeShape) {
        case 1: e = {0.096f, 0.064f, 1.00f, 0.04f, 0.14f, 2.2f, 1.00f, 0.00f}; break;   // round
        case 2: e = {0.100f, 0.040f, 0.85f, 0.16f, 0.05f, 3.5f, 0.92f, 0.02f}; break;   // narrow
        case 3: e = {0.102f, 0.046f, 0.80f, 0.30f, 0.06f, 3.2f, 0.95f, 0.06f}; break;   // sharp
        case 4: e = {0.098f, 0.050f, 0.95f, -0.20f, 0.12f, 2.4f, 1.00f, -0.04f}; break; // droopy
        case 5: e = {0.098f, 0.048f, 0.90f, 0.08f, 0.02f, 3.0f, 0.95f, 0.00f}; break;   // hooded
        case 6: e = {0.086f, 0.042f, 0.90f, 0.10f, 0.06f, 2.8f, 0.85f, 0.02f}; break;   // small
        default: e = {0.100f, 0.055f, 0.95f, 0.10f, 0.10f, 2.6f, 1.00f, 0.02f}; break;  // standard (reference)
    }
    e.W *= f.eyeSize;
    e.Hh *= f.eyeSize * (g.ageGroup == AgeGroup::Young ? 1.06f : g.ageGroup == AgeGroup::Elder ? 0.92f : 1.0f);
    e.tilt += f.eyeTilt;
    return e;
}

void paintEyes(CellPainter& cp, const Genome& g, EyeExpr expr) {
    const FaceStyle& f = g.face;
    const Palette& pal = g.palette;
    const EyeParams P = eyeParams(expr);
    const EyeShape S = eyeShapeFor(g);
    const float W = S.W, Hh = S.Hh;
    const float ey = 0.462f, exOff = 0.188f * f.eyeSpacing;
    const Vec3 lash = lerp(pal.brow * 0.4f, Vec3{0.08f, 0.05f, 0.05f}, 0.6f);
    const Vec3 skinDark = pal.skin * Vec3{0.70f, 0.56f, 0.54f};
    const float lidDrop = saturate(f.restLid + 0.18f * f.ageLines + (f.eyeShape == 5 ? 0.22f : 0.0f) + P.lidDrop);
    const float lw = f.lashWeight;

    // Reference style: the upper lid is a heavy, nearly straight line rising from
    // the inner corner to the outer one; the inner end of the eye is squared, the
    // bottom is a full, rounded curve with no drawn line.
    auto yUp = [&](float x) {
        const float t = saturate((x + W) / (2.0f * W)), u = clamp(x / W, -1.0f, 1.0f);
        float y = Hh * P.open * (1.0f - S.slant * (1.0f - t) - std::max(0.0f, -S.slant) * t * 0.0f) + Hh * S.bow * (1.0f - u * u);
        if (S.slant < 0) y = Hh * P.open * (1.0f + S.slant * t) + Hh * S.bow * (1.0f - u * u);
        y -= Hh * P.lidTilt * (0.5f - 0.5f * u) * 0.9f;
        return std::min(y, Hh * (1.0f - lidDrop));
    };
    auto yLo = [&](float x) {
        const float u = clamp(x / W, -1.0f, 1.0f), t = saturate((x + W) / (2.0f * W));
        const float k = std::sqrt(std::max(0.0f, 1.0f - std::pow(std::fabs(u), x > 0 ? 2.6f : S.squareK)));
        float y = -Hh * S.lowK * std::sqrt(P.open) * k + 0.12f * Hh * t;
        return y + P.lowerLift * Hh * 0.6f * k;
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
            float by = Hh * (1.0f + S.bow) + 0.034f + f.browHeight + P.browY;
            float tiltB = f.browTilt + P.browAngle;
            Stroke st = curveStroke(
                [&](float t) {
                    float x = lerp(-0.70f * W * f.browLength + P.browPinch, 0.95f * W * f.browLength, t);
                    float y = by - tiltB * 0.045f * (t - 0.35f);
                    if (f.browShape == 1) y += 0.012f * bump((t - 0.55f) / 0.6f) - 0.006f * t;
                    else if (f.browShape == 2) y += 0.010f * std::min(t / 0.65f, 1.0f) - 0.016f * std::max(0.0f, (t - 0.65f) / 0.35f);
                    else y -= 0.004f * t;
                    return Vec2{x, y};
                },
                [&](float t) { return (0.0024f + 0.0040f * f.browThick) * (1.0f - 0.55f * t) + 0.0012f; });
            strokeLocal(st, Vec4(lerp(pal.brow, pal.skin * 0.6f, 0.40f), 0.85f));
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
            const Vec3 sclera = lerp(Vec3{0.97f, 0.97f, 0.97f}, pal.skin, 0.06f);
            cp.fill(openSdf, [&](Vec2 p) {
                Vec2 l = toLocal(p);
                float sh = smoothstep(yUp(l.x) - 0.40f * Hh, yUp(l.x), l.y);  // shadow under the lash
                if (f.glowEyes) {  // the whole opening glows: a white-hot core fading to saturated edges
                    const float k = saturate(length(Vec2{(l.x + 0.05f * W) / (0.75f * W), (l.y + 0.1f * Hh) / (0.9f * Hh)}));
                    Vec3 gc = lerp(min3(f.glow * 0.6f + Vec3{0.45f, 0.45f, 0.45f}, Vec3{1, 1, 1}), f.glow * 0.80f, smoothstep(0.15f, 1.0f, k));
                    gc = lerp(gc, f.glow * 0.45f, 0.6f * sh);
                    return Vec4(gc, 1);
                }
                return Vec4(lerp(sclera, sclera * Vec3{0.72f, 0.74f, 0.84f}, sh), 1);
            }, lo, hi);
            // Iris: huge and slightly taller than wide, set towards the nose so the white
            // shows on the outer side; dark under the lash, luminous at the bottom.
            // Iris taller than the opening (cropped by both lids), ~60% of the eye wide.
            const float irR = 0.56f * W * S.iris * f.irisSize * P.irisScale, irH = 1.04f * irR;
            const Vec2 ir{irR, irH};
            const Vec2 ic{-0.01f * W, 0.5f * (Hh - Hh * S.lowK) - 0.10f * Hh};
            const bool blind = f.scarType == 5 && (side > 0) == (g.seed % 2 == 0);
            const Vec2 icF = toFace(ic);
            const Vec2 ilo = icF - Vec2{irH * 1.2f, irH * 1.2f}, ihi = icF + Vec2{irH * 1.2f, irH * 1.2f};
            if (!f.glowEyes) cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), ic, ir)); },
                    [&](Vec2 p) {
                        Vec2 l = toLocal(p) - ic;
                        float k = length(Vec2{l.x / ir.x, l.y / ir.y});
                        float v = saturate(0.5f - 0.5f * l.y / ir.y);  // 0 top .. 1 bottom
                        const Vec3 irc = blind ? lerp(pal.iris, Vec3{0.80f, 0.84f, 0.86f}, 0.78f) : pal.iris;
                        Vec3 col = lerp(irc * 0.22f, irc * 0.95f, smoothstep(0.16f, 0.60f, v));
                        col = lerp(col, irc * 1.45f + Vec3{0.06f, 0.06f, 0.06f}, smoothstep(0.62f, 0.95f, v) * 0.8f);
                        col = lerp(col, irc * 0.30f, smoothstep(0.86f, 1.0f, k) * 0.8f);  // rim
                        if (blind) col = lerp(col, Vec3{0.86f, 0.88f, 0.90f}, 0.5f);
                        return Vec4(min3(col, Vec3{1, 1, 1}), 1);
                    }, ilo, ihi);
            Sdf pupil = g.face.pupil == PupilStyle::Slit
                            ? Sdf([=](Vec2 p) { return sdEllipse(toLocal(p), ic, Vec2{0.12f * ir.x, 0.72f * ir.y}); })
                            : Sdf([=](Vec2 p) { return sdEllipse(toLocal(p), ic + Vec2{0, 0.10f * ir.y}, Vec2{0.26f * ir.x, 0.30f * ir.y}); });
            if (!blind && !f.glowEyes) cp.fill([&](Vec2 p) { return std::max(openSdf(p), pupil(p)); }, Vec4(pal.iris * 0.10f, 0.55f), ilo, ihi);
            // Highlights: a crisp spot up in the iris and a soft glint at the bottom.
            const Vec2 hl = ic + Vec2{-0.28f * ir.x, 0.22f * ir.y};
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), hl, Vec2{0.16f * ir.x, 0.12f * ir.y})); },
                    Vec4(1, 1, 1, 0.90f), ilo, ihi);
            // Soft vertical glint in the lower iris (as in the reference).
            const Vec2 hl2 = ic + Vec2{0.08f * ir.x, -0.40f * ir.y};
            if (!f.glowEyes)
                cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), hl2, Vec2{0.12f * ir.x, 0.26f * ir.y})); },
                        Vec4(lerp(pal.iris, Vec3{1, 1, 1}, 0.5f), 0.35f), ilo, ihi);
            // Upper lash: a heavy band along the lid, thin at the inner corner, thick
            // outside, wrapping down a little past the outer corner.
            const float lt0 = 0.0048f + 0.0018f * lw, lt1 = 0.0095f + 0.0050f * lw;
            Stroke up = curveStroke([&](float x01) {
                float x = lerp(-1.0f * W, 1.0f * W, x01);
                float w = lerp(lt0, lt1, smoothstep(0.0f, 0.75f, x01));
                return Vec2{x, yUp(x) + 0.55f * w};
            }, [&](float t) { return lerp(lt0, lt1, smoothstep(0.0f, 0.75f, t)); }, 24);
            {   // a short wing past the outer corner, then down the outer edge
                Vec2 end = up.pts.back();
                const float yb = yLo(W);
                const float wing = 0.010f + 0.012f * lw;
                Stroke wg{{end, end + Vec2{wing, 0.25f * wing}}, {0.85f * lt1, 0.0008f}};
                strokeLocal(wg, Vec4(lash, 1));
                up.pts.push_back(Vec2{W + 0.001f, lerp(end.y, yb, 0.30f)});
                up.w.push_back(0.45f * lt1);
                up.pts.push_back(Vec2{W - 0.003f, lerp(end.y, yb, 0.55f)});
                up.w.push_back(0.0008f);
            }
            strokeLocal(up, Vec4(lash, 1));
            // Lid crease: a soft warm shadow above the lash (depth without a drawn line).
            Stroke crease = curveStroke([&](float t) {
                float x = lerp(-0.85f * W, 0.95f * W, t);
                return Vec2{x, yUp(x) + 2.0f * lt1 + 0.010f};
            }, [&](float t) { return (0.0040f + 0.003f * f.eyeDepth) * std::sin(kPi * (0.1f + 0.8f * t)); }, 14);
            strokeLocal(crease, Vec4(lerp(skinDark, Vec3{0.62f, 0.36f, 0.40f}, 0.25f), 0.30f + 0.2f * f.eyeDepth));
            // Inner corner (caruncle): a small warm notch.
            cp.fill([&](Vec2 p) { return std::max(openSdf(p), sdEllipse(toLocal(p), Vec2{-W + 0.004f, -0.12f * Hh}, Vec2{0.007f, 0.30f * Hh})); },
                    Vec4(lerp(pal.skin, Vec3{0.86f, 0.52f, 0.52f}, 0.45f), 0.85f), lo, hi);
            // A soft lighter skin edge under the eye (no drawn lower line).
            Stroke lower = curveStroke([&](float t) {
                float x = lerp(-0.8f * W, 0.9f * W, t);
                return Vec2{x, yLo(x) - 0.006f};
            }, [&](float t) { return 0.0025f * std::sin(kPi * t) + 0.0005f; }, 10);
            strokeLocal(lower, Vec4(lerp(pal.skin, Vec3{1, 1, 1}, 0.35f), 0.35f));
        }
        // Age: crow's feet and the under-eye fold.
        if (f.ageLines > 0.45f) {
            float a = f.ageLines - 0.3f;
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
        // Markings (identity): moles, war paint, eye black, a slit in the brow, a forehead mark.
        {
            const Vec4 mc(f.markColor, 0.92f);
            auto rect = [&](Vec2 a, Vec2 b, float hw, Vec4 col) {
                Stroke st{{a, b}, {hw, hw * 0.8f}};
                Vec2 lo2, hi2;
                st.bounds(lo2, hi2);
                cp.fill(st, col, lo2, hi2);
            };
            switch (f.marking) {
                case 1:
                    if ((side > 0) == (g.seed % 2 == 0))
                        cp.fill([=](Vec2 p) { return length(p - Vec2{side * 0.255f, 0.395f}) - 0.0055f; },
                                Vec4(pal.skin * 0.35f, 0.9f), Vec2{side * 0.255f - 0.01f, 0.385f}, Vec2{side * 0.255f + 0.01f, 0.405f});
                    break;
                case 2:
                    for (int k = 0; k < 2; ++k) {
                        float y = 0.372f - 0.026f * k;
                        rect(Vec2{side * 0.15f, y + 0.004f}, Vec2{side * 0.30f, y - 0.004f}, 0.0065f, mc);
                    }
                    break;
                case 3:
                    rect(Vec2{side * 0.12f, 0.392f}, Vec2{side * 0.29f, 0.385f}, 0.010f, Vec4(lerp(f.markColor, Vec3{0.08f, 0.08f, 0.1f}, 0.6f), 0.9f));
                    break;
                case 5:
                    if ((side > 0) == (g.seed % 2 == 0)) {
                        const float by0 = ey + Hh * (1.0f + S.bow) + 0.034f + f.browHeight;
                        rect(Vec2{side * (exOff + 0.25f * W), by0 + 0.02f}, Vec2{side * (exOff + 0.32f * W), by0 - 0.02f}, 0.0035f,
                             Vec4(pal.skin, 1.0f));
                    }
                    break;
                case 6:
                    if (side > 0)
                        cp.fill([=](Vec2 p) { return std::fabs(p.x) + std::fabs(p.y - 0.62f) * 0.55f - 0.016f; }, mc,
                                Vec2{-0.03f, 0.57f}, Vec2{0.03f, 0.67f});
                    break;
                default: break;
            }
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
    if (f.scarType == 1 || f.scarType == 5) {  // the scar splits the brow (the rest is in the skin layer)
        const float sx = (g.seed % 2 == 0) ? 1.0f : -1.0f;
        const Vec2 c = f.scarType == 1 ? Vec2{sx * 0.150f, 0.585f} : Vec2{sx * 0.200f, 0.585f};
        cp.fill([=](Vec2 p) { return std::fabs(p.x - c.x + 0.25f * (p.y - c.y)) - 0.0045f + 0.0f * p.y; },
                Vec4(lerp(pal.skin, Vec3{0.95f, 0.80f, 0.78f}, 0.3f), 1.0f), c - Vec2{0.02f, 0.04f}, c + Vec2{0.02f, 0.04f});
    }
}

void paintMouth(CellPainter& cp, const Genome& g, MouthExpr e) {
    const FaceStyle& f = g.face;
    const Vec3 skin = g.palette.skin;
    const float lf = f.lipFullness;
    const Vec3 lipC = lerp(skin * Vec3{0.92f, 0.80f, 0.78f}, g.palette.lips, lerp(0.35f, 0.9f, lf));
    const Vec3 line = lerp(skin * 0.45f, Vec3{0.35f, 0.14f, 0.14f}, 0.4f);  // a soft line, not an outline
    const Vec3 inner{0.30f, 0.08f, 0.10f}, teeth{0.92f, 0.90f, 0.86f}, tongue{0.75f, 0.40f, 0.42f};
    const Vec3 skinDark = skin * Vec3{0.72f, 0.58f, 0.56f};
    const float my = 0.222f, mw = 0.060f * f.mouthWidth;
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
        const float lipA = 0.25f + 0.6f * lf;  // lips read as colour only when full
        cp.fill([=](Vec2 p) { return sdBetween(p, 0.84f * mw, yM, yBot); }, Vec4(lipC, lipA), lo, hi);
        cp.fill([=](Vec2 p) { return sdBetween(p, mw, yTop, yM); }, Vec4(lipC * 0.82f, lipA), lo, hi);
        Stroke ml = curveStroke([=](float t) {
            float x = lerp(-mw, mw, t);
            return Vec2{x, yM(x)};
        }, [](float t) { return 0.0028f + 0.0012f * std::sin(kPi * t); }, 16);
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

    if (f.marking == 4) {  // chin stripe
        Stroke st{{Vec2{0.0f, 0.195f}, Vec2{0.0f, 0.125f}}, {0.010f, 0.007f}};
        cp.fill(st, Vec4(f.markColor, 0.92f), lo, hi);
    }
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


// Skin layer: colour multipliers (stored at half value, 0.5 = unchanged) over the
// faceted head. Soft shapes only: they shade the planes, never outline them.
void paintSkin(CellPainter& cp, const Genome& g) {
    const FaceStyle& f = g.face;
    const Palette& pal = g.palette;
    auto soft = [&](Vec2 c, Vec2 r, Vec3 mul, float a, float power = 1.0f) {
        if (a <= 0.004f) return;
        cp.fill([=](Vec2 p) { return sdEllipse(p, c, r); },
                [=](Vec2 p) {
                    float k = 1.0f - saturate(length(Vec2{(p.x - c.x) / r.x, (p.y - c.y) / r.y}));
                    return Vec4(mul * 0.5f, a * std::pow(smoothstep(0.0f, 1.0f, k), power));
                },
                c - r, c + r);
    };
    auto line = [&](std::vector<Vec2> pts, std::vector<float> w, Vec3 mul, float a) {
        Stroke st{std::move(pts), std::move(w)};
        Vec2 lo, hi;
        st.bounds(lo, hi);
        cp.fill(st, Vec4(mul * 0.5f, a), lo, hi);
    };
    const float ex = 0.188f * f.eyeSpacing, ey = 0.462f;
    const float sx = (g.seed % 2 == 0) ? 1.0f : -1.0f;  // the side scars and marks favour

    // Eye sockets: shade under the brow ridge and around the eye, deeper for deep-set eyes.
    for (float sd : {1.0f, -1.0f}) {
        soft({sd * ex, ey + 0.020f}, {0.150f, 0.090f}, {0.80f, 0.70f, 0.74f}, 0.30f + 0.55f * f.eyeDepth, 0.8f);
        soft({sd * 0.075f, ey + 0.012f}, {0.050f, 0.060f}, {0.78f, 0.68f, 0.70f}, 0.25f + 0.35f * f.eyeDepth);  // inner corner
        // Tired, puffy under-eyes.
        soft({sd * (ex + 0.005f), ey - 0.062f}, {0.085f, 0.024f}, {0.80f, 0.70f, 0.78f}, 0.55f * f.eyeBags);
        // Hollow below the cheekbone.
        soft({sd * 0.235f, 0.270f}, {0.095f, 0.055f}, {0.84f, 0.74f, 0.74f}, 0.55f * f.cheekHollow + 0.10f);
        // Weathered cheeks.
        soft({sd * 0.215f, 0.355f}, {0.085f, 0.050f}, {1.10f, 0.86f, 0.84f}, 0.55f * f.ruddy);
    }
    soft({0.0f, 0.345f}, {0.050f, 0.040f}, {1.12f, 0.84f, 0.82f}, 0.60f * f.ruddy);  // nose tip

    // Beard shadow: upper lip, chin and jaw up to the sideburn; soft upper edge, lips kept clear.
    if (f.stubble > 0.01f) {
        const Vec3 rel{clamp(pal.hair.x / std::max(pal.skin.x, 0.05f), 0.22f, 1.0f), clamp(pal.hair.y / std::max(pal.skin.y, 0.05f), 0.22f, 1.0f),
                       clamp(pal.hair.z / std::max(pal.skin.z, 0.05f), 0.22f, 1.0f)};
        const Vec3 mul = lerp(Vec3{1, 1, 1}, rel * Vec3{0.95f, 0.97f, 1.05f}, 0.72f);
        const float mw = 0.060f * f.mouthWidth;
        // Bearded faces: the shadow runs a little above the beard's edge so the mass grows out of it.
        const float up = g.facialHair == FacialHair::None || g.facialHair == FacialHair::Stubble ? 0.0f : 0.03f;
        auto top = [up](float ax) { return 0.300f - 0.060f * smoothstep(0.04f, 0.12f, ax) + 0.20f * smoothstep(0.16f, 0.42f, ax) + up; };
        cp.fill([=](Vec2 p) { return p.y - top(std::fabs(p.x)); },
                [=](Vec2 p) {
                    float a = 0.95f * f.stubble * smoothstep(0.0f, 0.035f, top(std::fabs(p.x)) - p.y);
                    a *= 1.0f - 0.85f * (1.0f - saturate(length(Vec2{p.x / (mw * 1.05f), (p.y - 0.222f) / 0.020f})));  // lips clear
                    return Vec4(mul * 0.5f, a);
                },
                {-0.5f, -0.08f}, {0.5f, 0.46f});
    }

    // Age: forehead lines and the frown furrow.
    if (f.ageLines > 0.35f) {
        const float a = 0.45f * (f.ageLines - 0.25f);
        for (int k = 0; k < 3; ++k) {
            const float y = 0.665f + 0.030f * float(k);
            std::vector<Vec2> pts;
            std::vector<float> w;
            for (int i = 0; i <= 8; ++i) {
                const float t = float(i) / 8.0f, x = lerp(-0.17f, 0.17f, t) * (1.0f - 0.15f * float(k));
                pts.push_back({x, y + 0.006f * std::sin(t * kPi * 2.0f + float(k))});
                w.push_back(0.0025f * std::sin(kPi * t) + 0.0005f);
            }
            line(pts, w, {0.80f, 0.68f, 0.68f}, a);
        }
    }
    if (f.browTilt < -0.15f || f.ageLines > 0.6f)
        for (float sd : {1.0f, -1.0f}) line({{sd * 0.020f, 0.545f}, {sd * 0.026f, 0.600f}}, {0.0025f, 0.0015f}, {0.80f, 0.68f, 0.68f}, 0.45f);
    if (f.chinWidth > 0.45f && (g.seed % 3 == 0)) line({{0.0f, 0.050f}, {0.0f, 0.105f}}, {0.004f, 0.002f}, {0.84f, 0.74f, 0.74f}, 0.5f);  // cleft

    // Freckles across the nose and cheeks.
    if (f.freckles) {
        Rng fr(g.seed ^ 0xF4ECull, 3);
        for (int k = 0; k < 26; ++k) {
            Vec2 q{fr.range(-0.27f, 0.27f), fr.range(0.31f, 0.43f)};
            if (std::fabs(q.x) < 0.05f && q.y > 0.40f) continue;
            const float rr = fr.range(0.0035f, 0.0060f);
            cp.fill([=](Vec2 p) { return length(p - q) - rr; }, Vec4(Vec3{0.80f, 0.62f, 0.54f} * 0.5f, 0.65f), q - Vec2{0.01f, 0.01f},
                    q + Vec2{0.01f, 0.01f});
        }
    }

    // Dirt: a few smudges (survivors, labourers, soldiers).
    if (f.grime > 0.01f) {
        Rng gr(g.seed ^ 0x6A11ull, 5);
        const int n = 3 + int(4.0f * f.grime);
        for (int k = 0; k < n; ++k) {
            const Vec2 c{gr.range(-0.34f, 0.34f), gr.range(0.04f, 0.70f)};
            for (int j = 0; j < 3; ++j)
                soft(c + Vec2{gr.range(-0.03f, 0.03f), gr.range(-0.02f, 0.02f)}, {gr.range(0.025f, 0.06f), gr.range(0.015f, 0.04f)},
                     {0.66f, 0.60f, 0.54f}, 0.55f * f.grime, 0.6f);
        }
    }

    // Undead decay: mottled dark patches, sunken sockets and stitched seams.
    if (f.rot > 0.01f) {
        Rng rr(g.seed ^ 0xDEADull, 7);
        const int n = 4 + int(6.0f * f.rot);
        for (int k = 0; k < n; ++k)
            soft({rr.range(-0.36f, 0.36f), rr.range(0.0f, 0.75f)}, {rr.range(0.03f, 0.08f), rr.range(0.02f, 0.06f)},
                 {0.62f, 0.66f, 0.56f}, 0.65f * f.rot, 0.7f);
        for (float sd : {1.0f, -1.0f}) soft({sd * ex, ey}, {0.13f, 0.08f}, {0.55f, 0.50f, 0.58f}, 0.6f * f.rot, 0.6f);
        const float y0 = rr.range(0.08f, 0.30f), x0 = rr.range(0.10f, 0.25f) * (rr.chance(0.5f) ? 1.0f : -1.0f);
        line({{x0, y0}, {x0 + 0.10f * (x0 > 0 ? 1.0f : -1.0f), y0 + 0.08f}}, {0.003f, 0.003f}, {0.55f, 0.45f, 0.45f}, 0.8f);
        for (int k = 0; k < 4; ++k) {
            const float t = (float(k) + 0.5f) / 4.0f;
            const Vec2 c{x0 + 0.10f * t * (x0 > 0 ? 1.0f : -1.0f), y0 + 0.08f * t};
            line({c + Vec2{-0.008f, 0.010f}, c + Vec2{0.008f, -0.010f}}, {0.0022f, 0.0022f}, {0.40f, 0.35f, 0.35f}, 0.85f);
        }
    }

    // Scars: a pale seam with a darker, pinker margin.
    auto scar = [&](std::vector<Vec2> pts, float wd) {
        std::vector<float> wo(pts.size(), wd * 1.8f), wi(pts.size(), wd);
        wo.front() = wo.back() = wd * 0.6f;
        wi.front() = wi.back() = wd * 0.3f;
        line(pts, wo, {1.02f, 0.78f, 0.78f}, 0.55f);
        line(pts, wi, {1.20f, 1.06f, 1.04f}, 0.85f);
    };
    switch (f.scarType) {
        case 1: scar({{sx * 0.125f, 0.660f}, {sx * 0.165f, 0.520f}, {sx * 0.205f, 0.400f}, {sx * 0.235f, 0.320f}}, 0.0045f); break;
        case 2: scar({{-sx * 0.150f, 0.450f}, {0.0f, 0.410f}, {sx * 0.170f, 0.360f}}, 0.0045f); break;
        case 3: scar({{sx * 0.030f, 0.300f}, {sx * 0.038f, 0.240f}, {sx * 0.046f, 0.185f}}, 0.0040f); break;
        case 4: scar({{sx * 0.120f, 0.330f}, {sx * 0.210f, 0.270f}, {sx * 0.300f, 0.220f}}, 0.0065f); break;
        case 5: scar({{sx * 0.205f, 0.640f}, {sx * 0.198f, 0.520f}, {sx * 0.192f, 0.400f}, {sx * 0.188f, 0.320f}}, 0.0055f); break;
        default: break;
    }

    // Ink: woad lines, tribal cheek marks, a forehead sigil.
    if (f.tattoo) {
        const Vec3 rel{clamp(f.tattooColor.x * 1.7f + 0.06f, 0.0f, 1.0f), clamp(f.tattooColor.y * 1.7f + 0.06f, 0.0f, 1.0f),
                       clamp(f.tattooColor.z * 1.7f + 0.06f, 0.0f, 1.0f)};
        const float a = 0.85f;
        switch (f.tattoo) {
            case 1:  // runic: a line down the forehead to the nose root, ticks, and two chin bars
                line({{0.0f, 0.76f}, {0.0f, 0.575f}}, {0.0050f, 0.0040f}, rel, a);
                for (int k = 0; k < 3; ++k) {
                    const float y = 0.72f - 0.045f * float(k);
                    line({{-0.025f, y + 0.010f}, {0.0f, y}, {0.025f, y + 0.010f}}, {0.0035f, 0.0035f, 0.0035f}, rel, a);
                }
                for (float sd : {1.0f, -1.0f}) line({{sd * 0.018f, 0.150f}, {sd * 0.022f, 0.040f}}, {0.0045f, 0.0035f}, rel, a);
                break;
            case 2:  // three claw strokes over a cheek (both cheeks on some)
                for (float sd : {1.0f, -1.0f}) {
                    if (sd != sx && g.seed % 3 != 0) continue;
                    for (int k = 0; k < 3; ++k) {
                        const float y = 0.40f - 0.040f * float(k);
                        line({{sd * 0.135f, y}, {sd * 0.215f, y - 0.022f}, {sd * 0.300f, y - 0.010f}}, {0.0050f, 0.0060f, 0.0015f}, rel, a);
                    }
                }
                break;
            default: {  // sigil: a ring with a dot and a stem
                cp.fill([=](Vec2 p) { return std::fabs(length(p - Vec2{0.0f, 0.690f}) - 0.032f) - 0.0045f; }, Vec4(rel * 0.5f, a),
                        {-0.05f, 0.64f}, {0.05f, 0.74f});
                cp.fill([=](Vec2 p) { return length(p - Vec2{0.0f, 0.690f}) - 0.009f; }, Vec4(rel * 0.5f, a), {-0.02f, 0.67f}, {0.02f, 0.71f});
                line({{0.0f, 0.655f}, {0.0f, 0.590f}}, {0.0040f, 0.0020f}, rel, a);
                break;
            }
        }
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
    // Skin cell: neutral multiplier (0.5) under zero coverage.
    for (int y = FaceLayout::kSkinCellY; y < FaceLayout::kSkinCellY + FaceLayout::kSkinCellH; ++y)
        for (int x = FaceLayout::kSkinCellX; x < FaceLayout::kSkinCellX + FaceLayout::kSkinCellW; ++x) px[size_t(y) * W + x] = Vec4{0.5f, 0.5f, 0.5f, 0.0f};
    auto skinJob = [&] {
        CellPainter cp(px, W, FaceLayout::kSkinCellX, FaceLayout::kSkinCellY, FaceLayout::kSkinCellW, FaceLayout::kSkinCellH,
                       FaceLayout::kSkinX0, FaceLayout::kSkinX1, FaceLayout::kSkinS0, FaceLayout::kSkinS1);
        paintSkin(cp, g);
    };
    const int jobs = int(EyeExpr::Count) + int(MouthExpr::Count) + 1;
    const int nThreads = std::clamp(int(std::thread::hardware_concurrency()), 1, 8);
    std::vector<std::thread> pool;
    for (int t = 0; t < nThreads; ++t)
        pool.emplace_back([&, t] {
            for (int j = t; j < jobs; j += nThreads)
                j < int(EyeExpr::Count) ? eyeJob(j) : j < jobs - 1 ? mouthJob(j - int(EyeExpr::Count)) : skinJob();
        });
    for (auto& th : pool) th.join();
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) img.set(x, y, px[size_t(y) * W + x]);
    return img;
}

}  // namespace cg
