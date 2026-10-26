"""从 ui_theme.h 读常量，渲染出「代码会画成什么样」的预览图。

关键：**不重复写坐标**——直接解析 firmware/ui/ui_theme.h，
所以这张图和 ui_main.c 永远是同一个东西（改头文件，图跟着变）。

用法：python design/render_c_preview.py
输出：design/ui-mockups/code-preview-home.png
"""
from __future__ import annotations

import os
import re
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "firmware", "ui", "ui_theme.h")
OUT = os.path.join(ROOT, "design", "ui-mockups", "code-preview-home.png")

S = 2  # 输出放大倍数
F_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
F_REG = r"C:\Windows\Fonts\msyh.ttc"


def load_theme(path: str) -> dict:
    """解析 #define UI_xxx 的数值（十进制或 0x 十六进制）"""
    theme = {}
    pat = re.compile(r"#define\s+(UI_[A-Z0-9_]+)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)\s*\)?")
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("/*")[0]          # 去掉行尾注释
            m = pat.search(line)
            if m:
                theme[m.group(1)] = int(m.group(2), 0)
    return theme


T = load_theme(HEADER)


def c(v: int) -> tuple[int, int, int]:
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def font(path: str, logical_px: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, int(logical_px * S), index=0)


# ── 与 ui_main.c 里的图元一一对应 ─────────────────────────────
def draw_box(d, x, y, w, h, radius, color, outline=None, border=0):
    box = [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1]
    d.rounded_rectangle(box, radius=radius * S, fill=color,
                        outline=outline, width=border * S)


def draw_dot(d, cx, cy, r, color):
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=color)


def icon_music(d, cx, cy, col):
    draw_dot(d, cx - 6, cy + 6, 4, col)
    draw_dot(d, cx + 5, cy + 3, 4, col)
    draw_box(d, cx - 3, cy - 8, 2, 14, 0, col)
    draw_box(d, cx + 8, cy - 11, 2, 14, 0, col)
    draw_box(d, cx - 3, cy - 9, 13, 2, 0, col)


def icon_moon(d, cx, cy, col, card_col):
    draw_dot(d, cx, cy, 8, col)
    draw_dot(d, cx + 5, cy - 3, 8, card_col)


def icon_folder(d, cx, cy, col):
    draw_box(d, cx - 10, cy - 7, 20, 14, 2, col)
    draw_box(d, cx - 10, cy - 10, 9, 4, 1, col)


def icon_grid(d, cx, cy, col):
    for dx in (-8, 2):
        for dy in (-8, 2):
            draw_box(d, cx + dx, cy + dy, 6, 6, 2, col)


def main() -> None:
    W, H = T["UI_W"], T["UI_H"]
    img = Image.new("RGB", (W * S, H * S), c(T["UI_C_BG"]))
    d = ImageDraw.Draw(img)

    # 时钟（代码里是 montserrat_44，这里用粗体近似数字外观）
    d.text((W / 2 * S, (T["UI_TIME_Y"] + 22) * S), "10:24",
           font=font(F_BOLD, 44), fill=c(T["UI_C_TEXT"]), anchor="mm")

    # 日期（CN12）
    d.text((W / 2 * S, (T["UI_DATE_Y"] + 7) * S), "10月20日 周一",
           font=font(F_REG, 12), fill=c(T["UI_C_DIM"]), anchor="mm")

    # 最近结果卡片
    draw_box(d, T["UI_RESULT_X"], T["UI_RESULT_Y"], T["UI_RESULT_W"], T["UI_RESULT_H"],
             T["UI_RESULT_RADIUS"], c(T["UI_C_CARD"]))
    draw_dot(d, T["UI_RESULT_X"] + 8, T["UI_RESULT_Y"] + T["UI_RESULT_H"] // 2, 4,
             c(T["UI_C_ACCENT"]))
    d.text(((T["UI_RESULT_X"] + 16) * S, (T["UI_RESULT_Y"] + 7 + 7) * S),
           "上次：已锁屏并静音", font=font(F_REG, 12), fill=c(T["UI_C_DIM"]), anchor="lm")

    # 4 个快捷格
    items = [("放歌", 0, T["UI_C_DIM"]), ("我困了", 1, T["UI_C_DIM"]),
             ("资料", 2, T["UI_C_DIM"]), ("更多", 3, T["UI_C_ACCENT"])]
    for i, (txt, kind, col) in enumerate(items):
        x = T["UI_TILE_X0"] + i * T["UI_TILE_STEP"]
        draw_box(d, x, T["UI_TILE_Y"], T["UI_TILE_SIZE"], T["UI_TILE_SIZE"],
                 T["UI_TILE_RADIUS"], c(T["UI_C_CARD"]))
        cx, cy = x + T["UI_TILE_SIZE"] // 2, T["UI_TILE_Y"] + 20
        colr = c(col)
        if kind == 0:
            icon_music(d, cx, cy, c(T["UI_C_TEXT"]))
        elif kind == 1:
            icon_moon(d, cx, cy, c(T["UI_C_TEXT"]), c(T["UI_C_CARD"]))
        elif kind == 2:
            icon_folder(d, cx, cy, c(T["UI_C_TEXT"]))
        else:
            icon_grid(d, cx, cy, colr)
        d.text((cx * S, (T["UI_TILE_Y"] + 32 + 6) * S), txt,
               font=font(F_REG, 12), fill=colr, anchor="mm")

    # 大按钮
    draw_box(d, T["UI_BTN_X"], T["UI_BTN_Y"], T["UI_BTN_W"], T["UI_BTN_H"],
             T["UI_BTN_RADIUS"], c(T["UI_C_BTN"]),
             outline=c(T["UI_C_ACCENT"]), border=T["UI_BTN_BORDER"])
    d.text(((T["UI_BTN_X"] + T["UI_BTN_W"] / 2) * S, (T["UI_BTN_Y"] + 16 + 8) * S),
           "按住说话", font=font(F_REG, 16), fill=c(T["UI_C_ACCENT"]), anchor="mm")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("已渲染:", OUT)
    print(f"读到的常量 {len(T)} 个，屏幕 {W}x{H}")


if __name__ == "__main__":
    main()
