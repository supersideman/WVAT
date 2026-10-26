"""背景图方案：生成样图 + 合成界面预览 + 实测体积（RGB565 / RLE）

用法：python design/gen_backgrounds.py
输出：
  design/ui-mockups/bg-1~3.png      纯背景
  design/ui-mockups/bg-1~3-ui.png   叠上主界面（B 版）的预览
  design/ui-mockups/bg-sheet.png    三张对比
  并在控制台打印每种方案的实测字节数
"""
from __future__ import annotations

import math
import os
import random
from PIL import Image, ImageDraw, ImageFont

W, H, S = 240, 280, 2
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui-mockups")

CARD = (27, 33, 41)
TEXT = (232, 237, 242)
DIM = (150, 162, 175)
ACCENT = (53, 208, 186)

F_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
F_REG = r"C:\Windows\Fonts\msyh.ttc"


def font(p, s):
    return ImageFont.truetype(p, s * S, index=0)


# ── 三张背景 ───────────────────────────────────────────────────
def bg_gradient() -> Image.Image:
    """深蓝渐变 + 顶部光晕（最省事，压缩率极高）"""
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / H
        r = int(10 + 18 * (1 - t) ** 2)
        g = int(16 + 30 * (1 - t) ** 2)
        b = int(28 + 52 * (1 - t) ** 2)
        for x in range(W):
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img, "RGBA")
    for i in range(26, 0, -1):                      # 顶部柔光
        a = int(2.2 * i / 26 * 26)
        d.ellipse([(W / 2 - 8 * i) * 1, (-14 * i), (W / 2 + 8 * i), (10 * i)], fill=(58, 108, 200, max(1, a // 5)))
    return img


def bg_stars() -> Image.Image:
    """星空：细颗粒多，压缩率差（用来做反面教材）"""
    img = Image.new("RGB", (W, H), (8, 10, 18))
    d = ImageDraw.Draw(img, "RGBA")
    for i in range(12, 0, -1):                      # 底部地平线微光
        d.rectangle([0, H - i * 3, W, H], fill=(20, 30, 55, 40))
    rnd = random.Random(20261020)
    for _ in range(260):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r = rnd.choice([1, 1, 1, 2])
        br = rnd.randrange(90, 256)
        a = rnd.randrange(120, 256)
        d.ellipse([x, y, x + r, y + r], fill=(br, br, min(255, br + 20), a))
    for _ in range(3):                               # 几颗大星带光晕
        x, y = rnd.randrange(30, W - 30), rnd.randrange(20, 150)
        for k in range(6, 0, -1):
            d.ellipse([x - k * 2, y - k * 2, x + k * 2, y + k * 2], fill=(200, 220, 255, 12))
    return img


def bg_waves() -> Image.Image:
    """暗色几何波纹（中等复杂度，压缩率居中）"""
    img = Image.new("RGB", (W, H), (12, 15, 22))
    d = ImageDraw.Draw(img, "RGBA")
    for band in range(7):
        base = 120 + band * 22
        amp = 14 - band
        col = (40 + band * 12, 90 + band * 14, 150 + band * 12, 26)
        pts = []
        for x in range(0, W + 1, 2):
            y = base + amp * math.sin(x / 34 + band * 0.9)
            pts.append((x, y))
        d.line(pts, fill=col, width=int(6 - band * 0.5), joint="curve")
    return img


BACKGROUNDS = [
    ("bg-1-gradient", "渐变光晕（推荐）", bg_gradient),
    ("bg-2-stars", "星空（颗粒多）", bg_stars),
    ("bg-3-waves", "几何波纹", bg_waves),
]


# ── 把主界面叠到背景上 ─────────────────────────────────────────
def compose_ui(bg: Image.Image) -> Image.Image:
    """深色卡片半透明压上去，保证文字可读"""
    canvas = bg.resize((W * S, H * S), Image.NEAREST).convert("RGBA")
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    def rr(box, r, fill, outline=None, width=2):
        d.rounded_rectangle([c * S for c in box], radius=r * S, fill=fill,
                            outline=outline, width=width * S)

    def tx(xy, s, f, fill, anchor="la"):
        d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)

    # 顶部压暗，保证时钟清晰
    d.rectangle([0, 0, W * S, 130 * S], fill=(0, 0, 0, 70))

    tx((W / 2, 42), "10:24", font(F_BOLD, 38), TEXT + (255,), "mm")
    tx((W / 2, 74), "10月20日 周一", font(F_REG, 13), DIM + (255,), "mm")

    rr((12, 96, 228, 122), 8, CARD + (238,))
    d.ellipse([20 * S, 105 * S, 28 * S, 113 * S], fill=ACCENT + (255,))
    tx((34, 109), "上次：已锁屏并静音", font(F_REG, 12), DIM + (255,), "lm")

    for x, label in ((12, "放歌"), (68, "我困了"), (124, "资料"), (180, "更多")):
        rr((x, 132, x + 48, 180), 10, CARD + (230,))
        tx((x + 24, 171), label, font(F_REG, 11), DIM + (255,), "mm")

    rr((12, 196, 228, 248), 12, (10, 20, 24, 238), ACCENT + (255,), 2)
    tx((86, 222), "按住说话", font(F_BOLD, 17), ACCENT + (255,), "lm")

    return Image.alpha_composite(canvas, layer).convert("RGB")


# ── 体积实测 ───────────────────────────────────────────────────
def rgb565_bytes(img: Image.Image) -> bytes:
    out = bytearray()
    data = img.convert("RGB").tobytes()          # 直接用字节流，比 getdata() 快且不废弃
    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i + 1], data[i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((v >> 8, v & 0xFF))         # 大端
    return bytes(out)


def rle_bytes(raw: bytes) -> bytes:
    """简易 RLE：把连续相同像素压成 (像素, 次数)。真机就是这种思路。"""
    out = bytearray()
    i = 0
    n = len(raw) // 2
    while i < n:
        px = raw[i * 2:i * 2 + 2]
        run = 1
        while i + run < n and run < 255 and raw[(i + run) * 2:(i + run) * 2 + 2] == px:
            run += 1
        out += px + bytes((run,))
        i += run
    return bytes(out)


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    previews = []
    print(f"{'背景':<16}{'RGB565':>10}{'RLE':>10}{'压缩比':>8}")
    print("-" * 46)
    for key, title, fn in BACKGROUNDS:
        bg = fn()
        bg.save(os.path.join(OUT, f"{key}.png"))
        ui = compose_ui(bg)
        ui.save(os.path.join(OUT, f"{key}-ui.png"))
        previews.append((title, ui))

        raw = rgb565_bytes(bg)
        rle = rle_bytes(raw)
        print(f"{key:<16}{len(raw) / 1024:>9.0f}K{len(rle) / 1024:>9.0f}K"
              f"{len(raw) / len(rle):>7.1f}x")

    cols = len(previews)
    pad, label_h = 14, 24
    sheet = Image.new("RGB", (cols * W * S + (cols + 1) * pad,
                              H * S + label_h + pad * 2), (32, 36, 42))
    sd = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(F_REG, 14, index=0)
    for i, (title, im) in enumerate(previews):
        x = pad + i * (W * S + pad)
        sd.text((x, pad), title, font=f, fill=(205, 215, 225))
        sheet.paste(im, (x, pad + label_h))
    sheet.save(os.path.join(OUT, "bg-sheet.png"))
    print(f"\n预览已生成 → {OUT}")


if __name__ == "__main__":
    main()
