"""Painted backdrops for the showcase scenes (tools/vitrine/*.txt).

    python3 tools/vitrine/fundos.py tools/vitrine

papel.png: warm paper with a pencil sketch of hills, pines and clouds (the party lineups).
noite.png: a dusky moor under a low moon (the bestiary).
Both are drawn at the scenes' internal resolution so they get the same chunky upscale.
"""
import math
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

W, H = 960, 540


def noise_layer(rnd, w, h, scale, amp):
    small = Image.new("L", (w // scale + 2, h // scale + 2))
    small.putdata([int(128 + rnd.uniform(-amp, amp)) for _ in range(small.width * small.height)])
    return small.resize((w, h), Image.BICUBIC)


def wobble(rnd, pts, amp):
    return [(x + rnd.uniform(-amp, amp), y + rnd.uniform(-amp, amp)) for x, y in pts]


def ridge(rnd, y0, rough, step=6):
    pts, y = [], y0
    for x in range(-10, W + 20, step):
        y += rnd.uniform(-rough, rough) + (y0 - y) * 0.08
        pts.append((x, y))
    return pts


def pine(d, rnd, x, base, h, col, width=1):
    # Sketchy conifer: trunk line plus a few tiers of zig-zag boughs.
    d.line([(x, base), (x + rnd.uniform(-1, 1), base - h)], fill=col, width=width)
    tiers = rnd.randint(4, 6)
    for i in range(tiers):
        ty = base - h * (0.25 + 0.75 * i / tiers)
        span = h * 0.32 * (1 - i / tiers) + 2
        d.line([(x - span, ty + span * 0.45), (x, ty), (x + span, ty + span * 0.45)], fill=col, width=1)


def paper(out):
    rnd = random.Random(7)
    top, bottom = (241, 232, 214), (232, 220, 197)
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / H
        c = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3))
        for x in range(W):
            px[x, y] = c
    grain = noise_layer(rnd, W, H, 3, 10)
    blot = noise_layer(rnd, W, H, 60, 22).filter(ImageFilter.GaussianBlur(20))
    img = Image.composite(img, Image.new("RGB", (W, H), (214, 198, 170)), Image.eval(blot, lambda v: min(255, v + 90)))
    img = Image.blend(img, Image.merge("RGB", (grain, grain, grain)), 0.05)
    d = ImageDraw.Draw(img)
    ink = (170, 160, 146)
    faint = (196, 186, 170)
    horizon = int(H * 0.52)
    # Far hills, two pencil passes.
    for y0, col, rough in ((horizon - 40, faint, 2.2), (horizon - 18, ink, 1.6)):
        pts = ridge(rnd, y0, rough)
        d.line(pts, fill=col, width=1)
        d.line(wobble(rnd, pts, 0.8), fill=col, width=1)
    # Billowing cloud outlines: uneven bumps along a soft upper edge, some lines left open.
    for cx, cy, r in ((230, 130, 70), (450, 95, 95), (700, 150, 75), (870, 85, 55)):
        x = cx - r * 1.5
        while x < cx + r * 1.5:
            u = (x - cx) / (r * 1.5)
            rr = r * rnd.uniform(0.18, 0.4) * (1 - 0.5 * u * u)
            y = cy + r * 0.35 * u * u - rr * 0.3
            if rnd.random() < 0.85:
                d.arc([x - rr, y - rr, x + rr, y + rr], rnd.uniform(170, 200), rnd.uniform(330, 370), fill=faint, width=1)
            x += rr * rnd.uniform(1.1, 1.6)
    # Pines on both flanks, a tall birch-like trunk cluster at the right like a stage wing.
    for i in range(18):
        x = rnd.uniform(0, 230) if i % 2 else rnd.uniform(730, W)
        h = rnd.uniform(40, 110)
        pine(d, rnd, x, horizon + rnd.uniform(-4, 10), h, faint if h < 70 else ink)
    for x in (888, 912, 941):
        d.line([(x, 0), (x + rnd.uniform(-4, 4), horizon + 30)], fill=ink, width=2)
        d.line([(x + 6, 0), (x + 6 + rnd.uniform(-3, 3), horizon + 30)], fill=faint, width=1)
    # Grass tufts and ground strokes.
    for _ in range(260):
        x = rnd.uniform(0, W)
        y = rnd.uniform(horizon + 8, H - 6)
        h = rnd.uniform(3, 10) * (0.6 + (y - horizon) / (H - horizon))
        col = faint if rnd.random() < 0.6 else ink
        for k in range(rnd.randint(2, 4)):
            dx = rnd.uniform(-3, 3)
            d.line([(x + k * 2, y), (x + k * 2 + dx, y - h)], fill=col, width=1)
    for _ in range(70):
        x = rnd.uniform(0, W)
        y = rnd.uniform(horizon + 30, H)
        d.line([(x, y), (x + rnd.uniform(10, 40), y + rnd.uniform(-1, 1))], fill=faint, width=1)
    # Birds.
    for _ in range(7):
        x, y = rnd.uniform(300, 700), rnd.uniform(40, 150)
        d.line([(x - 4, y - 2), (x, y), (x + 4, y - 2)], fill=ink, width=1)
    img.save(out)


def night(out):
    rnd = random.Random(11)
    img = Image.new("RGB", (W, H))
    px = img.load()
    stops = [(0.0, (22, 24, 38)), (0.45, (52, 50, 70)), (0.56, (86, 74, 82)), (0.6, (44, 42, 48)), (1.0, (24, 22, 26))]
    for y in range(H):
        t = y / H
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                k = (t - t0) / (t1 - t0)
                c = tuple(int(c0[i] * (1 - k) + c1[i] * k) for i in range(3))
                break
        for x in range(W):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    # Moon and its halo.
    halo = Image.new("L", (W, H))
    ImageDraw.Draw(halo).ellipse([700, 40, 860, 200], fill=120)
    halo = halo.filter(ImageFilter.GaussianBlur(40))
    img = Image.composite(Image.new("RGB", (W, H), (190, 180, 170)), img, halo)
    d = ImageDraw.Draw(img)
    d.ellipse([752, 92, 808, 148], fill=(226, 218, 196))
    # Stars.
    for _ in range(90):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H * 0.4)
        v = rnd.randint(110, 200)
        d.point((x, y), fill=(v, v, v + 20))
    horizon = int(H * 0.56)
    # Distant crags and a dead tree line, as flat silhouettes.
    for y0, col, rough in ((horizon - 50, (58, 54, 72), 4.0), (horizon - 20, (44, 40, 52), 2.8), (horizon - 4, (34, 31, 38), 1.8)):
        pts = ridge(rnd, y0, rough, 5)
        d.polygon(pts + [(W + 20, H), (-10, H)], fill=col)
    for i in range(14):
        x = rnd.uniform(0, 220) if i % 2 else rnd.uniform(760, W)
        base, h = horizon + rnd.uniform(-2, 6), rnd.uniform(40, 120)
        col = (28, 26, 32)
        d.line([(x, base), (x, base - h)], fill=col, width=2)
        for k in range(5):
            y = base - h * (0.4 + 0.12 * k)
            s = rnd.choice((-1, 1))
            d.line([(x, y), (x + s * rnd.uniform(8, 22), y - rnd.uniform(6, 16))], fill=col, width=1)
    # Low mist band.
    mist = Image.new("L", (W, H))
    md = ImageDraw.Draw(mist)
    for _ in range(40):
        x, y = rnd.uniform(-50, W), rnd.uniform(horizon - 10, horizon + 40)
        md.ellipse([x, y, x + rnd.uniform(80, 200), y + rnd.uniform(8, 20)], fill=70)
    mist = mist.filter(ImageFilter.GaussianBlur(10))
    img = Image.composite(Image.new("RGB", (W, H), (120, 112, 130)), img, mist)
    grain = noise_layer(rnd, W, H, 2, 8)
    img = Image.blend(img, Image.merge("RGB", (grain, grain, grain)), 0.04)
    img.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    paper(f"{out}/papel.png")
    night(f"{out}/noite.png")
