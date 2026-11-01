"""从 ui_theme.h 读常量，渲染「图形化版」主界面（与 ui_main.c / ui_icons.c 一一对应）

用法：python design/render_c_preview.py
输出：design/ui-mockups/code-preview-home.png     主界面
      design/ui-mockups/code-preview-states.png   三种状态用图形怎么表达
"""
from __future__ import annotations

import os
import re
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "firmware", "ui", "ui_theme.h")
OUT_DIR = os.path.join(ROOT, "design", "ui-mockups")

S = 2
F_NUM = r"C:\Windows\Fonts\msyhbd.ttc"   # 粗体数字，近似 montserrat 观感


def load_theme(path: str) -> dict:
    """解析 ui_theme.h 里的 #define。

    支持两种值：
      1. 字面量：0x1B2129 / 240
      2. 引用前面常量的简单表达式：(UI_W - 12 - UI_DRW_FB_W) / 2
         —— 用 eval 在"已解析的常量"作用域里求值，遇到 / 时用整除语义
    """
    theme: dict = {}
    pat = re.compile(r"#define\s+(UI_[A-Z0-9_]+)\s+(.+?)\s*(?:/\*.*)?$")
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("/*")[0].strip()
            m = pat.match(line)
            if not m:
                continue
            name, expr = m.group(1), m.group(2).strip()
            try:
                val = eval(expr, {"__builtins__": {}}, dict(theme))  # noqa: S307
            except Exception:
                continue
            if isinstance(val, (int, float)):
                theme[name] = int(val)
    return theme


T = load_theme(HEADER)


def c(v: int):
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def px(size, num, den):
    return int(size * num / den)


# ── 图元 ───────────────────────────────────────────────
def put(d, x, y, w, h, r, color):
    if w < 1 or h < 1:
        return
    d.rounded_rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1],
                        radius=max(0, r) * S, fill=color)


def disc(d, cx, cy, r, color):
    if r < 1:
        r = 1
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=color)


def line(d, x1, y1, x2, y2, w, color):
    d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=color, width=max(1, w) * S)


# ── 图标（严格照 ui_icons.c 的公式）────────────────────
def ic_music(d, cx, cy, s, col):
    stem, hr = max(1, s // 9), max(1, s // 7)
    disc(d, cx - px(s, 1, 5), cy + px(s, 1, 4), hr, col)
    disc(d, cx + px(s, 1, 4), cy + px(s, 1, 8), hr, col)
    put(d, cx - px(s, 1, 5) - stem // 2, cy - px(s, 1, 4), stem, px(s, 1, 2), 0, col)
    put(d, cx + px(s, 1, 4) - stem // 2, cy - px(s, 1, 5), stem, px(s, 1, 2), 0, col)
    put(d, cx - px(s, 1, 5) - stem // 2, cy - px(s, 1, 4), px(s, 1, 2) + stem, stem, 0, col)


def ic_moon(d, cx, cy, s, col, card):
    r = px(s, 2, 5)
    disc(d, cx, cy, r, col)
    disc(d, cx + px(s, 1, 5), cy - px(s, 1, 8), r, card)


def ic_folder(d, cx, cy, s, col):
    tabh = px(s, 1, 8)
    bx, by = cx - px(s, 1, 2), cy - px(s, 1, 4)
    put(d, bx, by, s, px(s, 3, 5), max(1, s // 12), col)
    put(d, bx, by - tabh, px(s, 2, 5), tabh, max(1, s // 14), col)   # 页签紧贴主体


def ic_grid(d, cx, cy, s, col):
    q, gap, r = px(s, 2, 5), px(s, 1, 10), max(1, s // 12)
    put(d, cx - q - gap // 2, cy - q - gap // 2, q, q, r, col)
    put(d, cx + gap // 2, cy - q - gap // 2, q, q, r, col)
    put(d, cx - q - gap // 2, cy + gap // 2, q, q, r, col)
    put(d, cx + gap // 2, cy + gap // 2, q, q, r, col)


def ic_mic(d, cx, cy, s, col):
    bw, bh = s // 3, s // 2
    barw = max(2, s // 12)
    sdx = (s * 30) // 100
    sy = cy - (s * 12) // 100
    sh = (s * 26) // 100
    put(d, cx - bw // 2, cy - (s * 43) // 100, bw, bh, bw // 2, col)
    put(d, cx - sdx, sy, barw, sh, barw // 2, col)
    put(d, cx + sdx - barw, sy, barw, sh, barw // 2, col)
    put(d, cx - sdx, sy + sh - barw, sdx * 2, barw, barw // 2, col)
    put(d, cx - barw // 2, sy + sh, barw, (s * 14) // 100, barw // 2, col)


def ic_ok(d, cx, cy, s, col):
    w = max(2, s // 7)
    line(d, cx - px(s, 1, 3), cy, cx - px(s, 1, 12), cy + px(s, 1, 4), w, col)
    line(d, cx - px(s, 1, 12), cy + px(s, 1, 4), cx + px(s, 1, 3), cy - px(s, 1, 3), w, col)


def ic_cross(d, cx, cy, s, col):
    dd, w = px(s, 1, 4), max(2, s // 7)
    line(d, cx - dd, cy - dd, cx + dd, cy + dd, w, col)
    line(d, cx + dd, cy - dd, cx - dd, cy + dd, w, col)


def vol_cone(d, cx, cy, s, col):
    w, h, step = px(s, 1, 8), px(s, 1, 5), px(s, 1, 10)
    x0 = cx - px(s, 2, 5)
    put(d, x0, cy - h, w, h * 2, 0, col)
    for i in range(3):
        hh = h + step * (i + 1)
        put(d, x0 + w + i * step, cy - hh, step + 1, hh * 2, 0, col)


def ic_vol(d, cx, cy, s, col):
    """音量+：喇叭 + 加号"""
    vol_cone(d, cx - px(s, 1, 10), cy, s, col)
    b = max(2, px(s, 1, 12))
    L = px(s, 1, 4)
    ax = cx + px(s, 1, 3)
    put(d, ax - L, cy - b // 2, L * 2, b, 0, col)
    put(d, ax - b // 2, cy - L, b, L * 2, 0, col)


def ic_next(d, cx, cy, s, col):
    """下一首：4 级台阶拼实心三角 + 竖条"""
    h, stepw = px(s, 3, 5), px(s, 1, 9)
    x0 = cx - px(s, 2, 5)
    barw = max(2, px(s, 1, 10))
    for i in range(4):
        hh = max(2, h - (h * i) // 4)
        put(d, x0 + i * stepw, cy - hh // 2, stepw + 1, hh, 0, col)
    put(d, x0 + 4 * stepw + 1, cy - h // 2, barw, h, 0, col)


def ic_mute(d, cx, cy, s, col):
    """音量−：喇叭 + 减号"""
    vol_cone(d, cx - px(s, 1, 10), cy, s, col)
    b = max(2, px(s, 1, 12))
    L = px(s, 1, 4)
    ax = cx + px(s, 1, 3)
    put(d, ax - L, cy - b // 2, L * 2, b, 0, col)


def ic_list(d, cx, cy, s, col):
    y0, dy = -px(s, 1, 4), px(s, 1, 4)
    r = max(1, s // 12)
    for i in range(3):
        y = cy + y0 + i * dy
        disc(d, cx - px(s, 2, 5), y, r, col)
        put(d, cx - px(s, 1, 6), y - r // 2, px(s, 3, 5), max(2, r * 2), 0, col)


def ic_back(d, cx, cy, s, col):
    w = max(2, s // 8)
    dd = px(s, 1, 4)
    line(d, cx + dd, cy - dd, cx - dd, cy, w, col)
    line(d, cx - dd, cy, cx + dd, cy + dd, w, col)


# ── 页面 ───────────────────────────────────────────────
def draw_home(intent="music", ok=True) -> Image.Image:
    W, H = T["UI_W"], T["UI_H"]
    img = Image.new("RGB", (W * S, H * S), c(T["UI_C_BG"]))
    d = ImageDraw.Draw(img)

    # 时钟 / 日期（数字）
    d.text((W / 2 * S, (T["UI_TIME_Y"] + 22) * S), "10:24",
           font=ImageFont.truetype(F_NUM, 44 * S, index=0),
           fill=c(T["UI_C_TEXT"]), anchor="mm")
    d.text((W / 2 * S, (T["UI_DATE_Y"] + 8) * S), "10/20",
           font=ImageFont.truetype(F_NUM, 14 * S, index=0),
           fill=c(T["UI_C_DIM"]), anchor="mm")

    # 结果回执
    put(d, T["UI_RESULT_X"], T["UI_RESULT_Y"], T["UI_RESULT_W"], T["UI_RESULT_H"],
        T["UI_RESULT_RADIUS"], c(T["UI_C_CARD"]))
    sz = 20
    iy = T["UI_RESULT_Y"] + (T["UI_RESULT_H"] - sz) // 2
    ix = T["UI_RESULT_X"] + 8
    if intent == "music":
        ic_music(d, ix + sz // 2 + 1, iy + sz // 2, sz, c(T["UI_C_TEXT"]))
    elif intent == "moon":
        ic_moon(d, ix + sz // 2 + 1, iy + sz // 2, sz, c(T["UI_C_TEXT"]), c(T["UI_C_CARD"]))
    else:
        ic_vol(d, ix + sz // 2 + 1, iy + sz // 2, sz, c(T["UI_C_TEXT"]))
    sx = T["UI_RESULT_X"] + T["UI_RESULT_W"] - 8 - sz
    if ok:
        ic_ok(d, sx + sz // 2 + 1, iy + sz // 2, sz, c(T["UI_C_ACCENT"]))
    else:
        ic_cross(d, sx + sz // 2 + 1, iy + sz // 2, sz, c(T["UI_C_DANGER"]))

    # 3 个意图格
    tiles = [("music", T["UI_C_TEXT"]), ("next", T["UI_C_TEXT"]), ("grid", T["UI_C_ACCENT"])]
    for i, (kind, col) in enumerate(tiles):
        x = T["UI_TILE_X0"] + i * T["UI_TILE_STEP"]
        put(d, x, T["UI_TILE_Y"], T["UI_TILE_SIZE"], T["UI_TILE_SIZE"],
            T["UI_TILE_RADIUS"], c(T["UI_C_CARD"]))
        cx, cy, s = x + T["UI_TILE_SIZE"] // 2, T["UI_TILE_Y"] + T["UI_ICON_CY"], 30
        colr = c(col)
        if kind == "music":
            ic_music(d, cx, cy, s, colr)
        elif kind == "next":
            ic_next(d, cx, cy, s, colr)
        else:
            ic_grid(d, cx, cy, s, colr)

    # 麦克风按钮
    put(d, T["UI_BTN_X"], T["UI_BTN_Y"], T["UI_BTN_W"], T["UI_BTN_H"],
        T["UI_BTN_RADIUS"], c(T["UI_C_BTN"]))
    d.rounded_rectangle(
        [T["UI_BTN_X"] * S, T["UI_BTN_Y"] * S,
         (T["UI_BTN_X"] + T["UI_BTN_W"]) * S - 1, (T["UI_BTN_Y"] + T["UI_BTN_H"]) * S - 1],
        radius=T["UI_BTN_RADIUS"] * S, outline=c(T["UI_C_ACCENT"]),
        width=T["UI_BTN_BORDER"] * S)
    ic_mic(d, T["UI_BTN_X"] + T["UI_BTN_W"] // 2, T["UI_BTN_Y"] + T["UI_BTN_H"] // 2,
           34, c(T["UI_C_ACCENT"]))
    return img


def draw_states() -> Image.Image:
    """三种状态怎么用图形表达（无文字）"""
    W, H = T["UI_W"], T["UI_H"]
    imgs = []

    for kind in ("rec", "ok", "err"):
        img = Image.new("RGB", (W * S, H * S), c(T["UI_C_BG"]))
        d = ImageDraw.Draw(img)
        cx, cy = W // 2, 118
        if kind == "rec":      # 红波形：正在录音
            for i in range(13):
                x = cx - 54 + i * 9
                hgt = 10 + 26 * abs((i % 5) - 2) / 2
                put(d, x, cy - hgt, 4, hgt * 2, 2, c(T["UI_C_DANGER"]))
            disc(d, cx, 44, 6, c(T["UI_C_DANGER"]))
        elif kind == "ok":     # 青对勾 + 外圈
            d.ellipse([(cx - 44) * S, (cy - 44) * S, (cx + 44) * S, (cy + 44) * S],
                      outline=c(T["UI_C_ACCENT"]), width=3 * S)
            ic_ok(d, cx, cy, 64, c(T["UI_C_ACCENT"]))
        else:                  # 红叉 + 外圈
            d.ellipse([(cx - 44) * S, (cy - 44) * S, (cx + 44) * S, (cy + 44) * S],
                      outline=c(T["UI_C_DANGER"]), width=3 * S)
            ic_cross(d, cx, cy, 56, c(T["UI_C_DANGER"]))
        imgs.append(img)

    pad, label_h = 14, 26
    sheet = Image.new("RGB", (3 * W * S + 4 * pad, H * S + label_h + 2 * pad), (32, 36, 42))
    sd = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 15, index=0)
    for i, (title, im) in enumerate(zip(("录音中（红波形）", "成功（青勾）", "失败（红叉）"), imgs)):
        x = pad + i * (W * S + pad)
        sd.text((x, pad), title, font=f, fill=(205, 215, 225))
        sheet.paste(im, (x, pad + label_h))
    return sheet


def draw_drawer() -> Image.Image:
    """抽屉页：顶部条（左返回 + 右反馈）+ 3×2 拓展指令（与 ui_drawer.c 一一对应）"""
    W, H = T["UI_W"], T["UI_H"]
    img = Image.new("RGB", (W * S, H * S), c(T["UI_C_BG"]))
    d = ImageDraw.Draw(img)

    # 顶部条
    put(d, 0, 0, W, T["UI_DRW_TOP_H"], 0, c(T["UI_C_CARD"]))
    ic_back(d, T["UI_BACK_X"] + T["UI_BACK_SIZE"] // 2, T["UI_BACK_Y"] + T["UI_BACK_SIZE"] // 2,
            T["UI_BACK_SIZE"], c(T["UI_C_DIM"]))

    # 顶部反馈（示例：点了「我困了」并成功 → 月亮 + 绿点）
    fb = max(1, 18)
    ic_moon(d, T["UI_DRW_FB_X"] + fb // 2, T["UI_DRW_FB_Y"] + fb // 2, fb, c(T["UI_C_TEXT"]),
            c(T["UI_C_CARD"]))
    disc(d, T["UI_DRW_DOT_X"] + T["UI_DRW_DOT_SZ"] // 2,
         T["UI_DRW_DOT_Y"] + T["UI_DRW_DOT_SZ"] // 2,
         T["UI_DRW_DOT_SZ"] // 2, c(T["UI_C_ACCENT"]))

    # 3×2 拓展指令（顺序与 ui_drawer.c 的 k_items 一致）
    items = [("moon", T["UI_C_TEXT"]), ("vol", T["UI_C_TEXT"]), ("mute", T["UI_C_TEXT"]),
             ("folder", T["UI_C_TEXT"]), ("list", T["UI_C_TEXT"]), ("mic", T["UI_C_ACCENT"])]

    for i, (kind, col) in enumerate(items):
        col_i, row = i % 3, i // 3
        x = T["UI_DRW_X0"] + col_i * T["UI_DRW_STEP_X"]
        y = T["UI_DRW_Y0"] + row * T["UI_DRW_STEP_Y"]
        put(d, x, y, T["UI_DRW_W"], T["UI_DRW_H"], T["UI_DRW_RADIUS"], c(T["UI_C_CARD"]))

        cx, cy = x + T["UI_DRW_W"] // 2, y + T["UI_DRW_ICON_CY"]
        s = T["UI_DRW_ICON_SZ"]
        colr = c(col)
        if kind == "moon":
            ic_moon(d, cx, cy, s, colr, c(T["UI_C_CARD"]))
        elif kind == "vol":
            ic_vol(d, cx, cy, s, colr)
        elif kind == "mute":
            ic_mute(d, cx, cy, s, colr)
        elif kind == "folder":
            ic_folder(d, cx, cy, s, colr)
        elif kind == "list":
            ic_list(d, cx, cy, s, colr)
        else:
            ic_mic(d, cx, cy, s, colr)
    return img


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    draw_home().save(os.path.join(OUT_DIR, "code-preview-home.png"))
    draw_states().save(os.path.join(OUT_DIR, "code-preview-states.png"))
    draw_drawer().save(os.path.join(OUT_DIR, "code-preview-drawer.png"))
    print("已渲染：code-preview-home.png / -states.png / -drawer.png")
    print(f"常量 {len(T)} 个，屏幕 {T['UI_W']}x{T['UI_H']}")


if __name__ == "__main__":
    main()
