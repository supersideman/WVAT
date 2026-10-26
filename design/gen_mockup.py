"""生成 240×280 手表面板的像素级界面稿（用于设计评审，不是真机渲染）

用法：python design/gen_mockup.py
输出：design/ui-mockups/*.png（2 倍放大，便于查看）+ all-states.png 总览
字体：微软雅黑（示意用；真机上是 LVGL 点阵字库，字形会有差异）
"""
from __future__ import annotations

import os
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 280          # 真机逻辑分辨率
S = 2                    # 输出放大倍数
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui-mockups")

# ── 配色（全部落在 RGB565 安全区，真机上不会出现色带）────────────
BG        = (14, 17, 22)
CARD      = (27, 33, 41)
TEXT      = (232, 237, 242)
DIM       = (138, 150, 163)
ACCENT    = (53, 208, 186)     # 青：成功 / 在线
DANGER    = (255, 92, 92)      # 红：录音中 / 失败
WARN      = (255, 194, 75)     # 黄：识别中
INFO      = (74, 158, 255)     # 蓝：处理中

FONT_DIR = r"C:\Windows\Fonts"
F_BOLD = os.path.join(FONT_DIR, "msyhbd.ttc")
F_REG = os.path.join(FONT_DIR, "msyh.ttc")


def font(path: str, logical_size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, logical_size * S, index=0)


def new_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (W * S, H * S), BG)
    return img, ImageDraw.Draw(img)


def rrect(d: ImageDraw.ImageDraw, box, r, fill=None, outline=None, width=2):
    d.rounded_rectangle([c * S for c in box], radius=r * S, fill=fill,
                        outline=outline, width=width * S)


def text(d, xy, s, f, fill=TEXT, anchor="la"):
    d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)


def status_bar(d, color, label, icon="dot"):
    """顶部 40px 状态条：左侧图标 + 状态词"""
    rrect(d, (0, 0, W, 40), 0, fill=CARD)
    d.rectangle([0, 0, 3 * S, 40 * S], fill=color)
    cx, cy = 22, 20
    if icon == "dot":
        d.ellipse([(cx - 6) * S, (cy - 6) * S, (cx + 6) * S, (cy + 6) * S], fill=color)
    elif icon == "mic":
        d.rounded_rectangle([(cx - 4) * S, (cy - 9) * S, (cx + 4) * S, (cy + 3) * S],
                            radius=3 * S, fill=color)
        d.arc([(cx - 8) * S, (cy - 2) * S, (cx + 8) * S, (cy + 10) * S],
              0, 180, fill=color, width=2 * S)
        d.line([cx * S, (cy + 10) * S, cx * S, (cy + 13) * S], fill=color, width=2 * S)
    elif icon == "check":
        d.line([(cx - 7) * S, cy * S, (cx - 2) * S, (cy + 6) * S], fill=color, width=3 * S)
        d.line([(cx - 2) * S, (cy + 6) * S, (cx + 8) * S, (cy - 6) * S], fill=color, width=3 * S)
    elif icon == "cross":
        d.line([(cx - 6) * S, (cy - 6) * S, (cx + 6) * S, (cy + 6) * S], fill=color, width=3 * S)
        d.line([(cx + 6) * S, (cy - 6) * S, (cx - 6) * S, (cy + 6) * S], fill=color, width=3 * S)
    text(d, (40, 20), label, font(F_BOLD, 17), fill=color, anchor="lm")


def hint_bar(d, left, right=""):
    rrect(d, (0, H - 36, W, H), 0, fill=CARD)
    if left:
        text(d, (12, H - 18), left, font(F_REG, 12), fill=DIM, anchor="lm")
    if right:
        text(d, (W - 12, H - 18), right, font(F_REG, 12), fill=DIM, anchor="rm")


def intent_glyph(d, kind, cx, cy, color):
    """意图图标（手绘几何图形，避免依赖 emoji）"""
    if kind == "music":
        d.ellipse([(cx - 8) * S, (cy + 2) * S, (cx - 1) * S, (cy + 9) * S], fill=color)
        d.ellipse([(cx + 3) * S, (cy - 1) * S, (cx + 10) * S, (cy + 6) * S], fill=color)
        d.line([(cx - 1) * S, (cy + 5) * S, (cx - 1) * S, (cy - 9) * S], fill=color, width=2 * S)
        d.line([(cx + 10) * S, (cy + 2) * S, (cx + 10) * S, (cy - 12) * S], fill=color, width=2 * S)
        d.line([(cx - 1) * S, (cy - 9) * S, (cx + 10) * S, (cy - 12) * S], fill=color, width=2 * S)
    elif kind == "moon":
        # 实心圆 + 同底色圆挖缺，做出月牙
        d.ellipse([(cx - 10) * S, (cy - 10) * S, (cx + 10) * S, (cy + 10) * S], fill=color)
        d.ellipse([(cx - 3) * S, (cy - 12) * S, (cx + 15) * S, (cy + 8) * S], fill=CARD)
    elif kind == "vol":
        d.polygon([((cx - 10) * S, (cy - 4) * S), ((cx - 4) * S, (cy - 4) * S),
                   ((cx + 2) * S, (cy - 10) * S), ((cx + 2) * S, (cy + 10) * S),
                   ((cx - 4) * S, (cy + 4) * S), ((cx - 10) * S, (cy + 4) * S)], fill=color)
        d.arc([(cx + 2) * S, (cy - 8) * S, (cx + 12) * S, (cy + 8) * S],
              -60, 60, fill=color, width=2 * S)
    elif kind == "folder":
        rrect(d, (cx - 11, cy - 7, cx + 11, cy + 8), 2, fill=color)
        d.rectangle([(cx - 11) * S, (cy - 10) * S, (cx - 2) * S, (cy - 6) * S], fill=color)
    elif kind == "list":
        for i, y in enumerate((-7, 0, 7)):
            d.ellipse([(cx - 10) * S, (cy + y - 1) * S, (cx - 8) * S, (cy + y + 1) * S], fill=color)
            d.line([(cx - 5) * S, (cy + y) * S, (cx + 10) * S, (cy + y) * S],
                   fill=color, width=2 * S)
    elif kind == "mic":
        d.rounded_rectangle([(cx - 5) * S, (cy - 11) * S, (cx + 5) * S, (cy + 3) * S],
                            radius=4 * S, fill=color)
        d.arc([(cx - 10) * S, (cy - 3) * S, (cx + 10) * S, (cy + 12) * S],
              0, 180, fill=color, width=2 * S)
        d.line([cx * S, (cy + 12) * S, cx * S, (cy + 15) * S], fill=color, width=2 * S)


# ── 页面 ───────────────────────────────────────────────────────
def page_home() -> Image.Image:
    img, d = new_canvas()
    text(d, (W / 2, 52), "10:24", font(F_BOLD, 40), anchor="mm")
    text(d, (W / 2, 80), "10月20日 周一", font(F_REG, 13), fill=DIM, anchor="mm")

    items = [("music", "放歌"), ("moon", "我困了"), ("vol", "音量"),
             ("folder", "资料"), ("list", "状态"), ("mic", "说话")]
    for i, (kind, name) in enumerate(items):
        col, row = i % 3, i // 3
        x0 = 12 + col * 74
        y0 = 100 + row * 66
        rrect(d, (x0, y0, x0 + 66, y0 + 58), 10, fill=CARD)
        intent_glyph(d, kind, x0 + 33, y0 + 22, ACCENT if kind == "mic" else TEXT)
        text(d, (x0 + 33, y0 + 44), name, font(F_REG, 12), fill=DIM, anchor="mm")
    hint_bar(d, "长按 KEY1 说话", "")
    return img


def page_lock() -> Image.Image:
    img, d = new_canvas()
    text(d, (W / 2, 96), "10:24", font(F_BOLD, 52), anchor="mm")
    text(d, (W / 2, 132), "10月20日 周一", font(F_REG, 14), fill=DIM, anchor="mm")
    rrect(d, (60, 176, 180, 200), 12, outline=ACCENT, width=1)
    text(d, (W / 2, 188), "上滑解锁", font(F_REG, 13), fill=ACCENT, anchor="mm")
    text(d, (W / 2, 236), "↑", font(F_BOLD, 18), fill=DIM, anchor="mm")
    return img


def page_recording() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, DANGER, "正在录音", icon="mic")
    # 波形
    import math
    cx, cy = W / 2, 120
    for i in range(17):
        x = cx - 64 + i * 8
        h = 6 + 22 * abs(math.sin(i * 0.7))
        d.line([x * S, (cy - h) * S, x * S, (cy + h) * S], fill=DANGER, width=3 * S)
    text(d, (W / 2, 176), "请说话…", font(F_REG, 14), fill=TEXT, anchor="mm")
    text(d, (W / 2, 200), "00:02", font(F_BOLD, 16), fill=DIM, anchor="mm")
    hint_bar(d, "KEY2 取消", "松开发送")
    return img


def page_asr() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, WARN, "识别中", icon="dot")
    text(d, (W / 2, 120), "…", font(F_BOLD, 30), fill=WARN, anchor="mm")
    text(d, (W / 2, 160), "把语音转成文字", font(F_REG, 13), fill=DIM, anchor="mm")
    hint_bar(d, "")
    return img


def page_confirm() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, INFO, "请确认", icon="dot")
    rrect(d, (16, 62, 224, 150), 10, fill=CARD)
    text(d, (120, 78), "识别结果", font(F_REG, 12), fill=DIM, anchor="mm")
    text(d, (120, 104), "帮我锁屏并静音", font(F_BOLD, 16), anchor="mm")
    text(d, (120, 128), "将执行：A03 我困了", font(F_REG, 12), fill=INFO, anchor="mm")
    # 倒计时
    d.arc([96 * S, 168 * S, 144 * S, 216 * S], -90, 180, fill=ACCENT, width=4 * S)
    text(d, (120, 192), "3", font(F_BOLD, 24), fill=ACCENT, anchor="mm")
    hint_bar(d, "KEY1 确认", "KEY2 重录")
    return img


def page_ok() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, ACCENT, "已完成", icon="check")
    d.ellipse([84 * S, 96 * S, 156 * S, 168 * S], outline=ACCENT, width=3 * S)
    d.line([(101) * S, (132) * S, (114) * S, (146) * S], fill=ACCENT, width=5 * S)
    d.line([(114) * S, (146) * S, (141) * S, (116) * S], fill=ACCENT, width=5 * S)
    text(d, (W / 2, 196), "已锁屏并静音", font(F_BOLD, 16), anchor="mm")
    hint_bar(d, "1.5 秒后返回", "")
    return img


def page_err() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, DANGER, "失败", icon="cross")
    d.ellipse([84 * S, 96 * S, 156 * S, 168 * S], outline=DANGER, width=3 * S)
    d.line([(103) * S, (117) * S, (137) * S, (151) * S], fill=DANGER, width=5 * S)
    d.line([(137) * S, (117) * S, (103) * S, (151) * S], fill=DANGER, width=5 * S)
    text(d, (W / 2, 196), "网络未连接", font(F_BOLD, 16), fill=DANGER, anchor="mm")
    text(d, (W / 2, 220), "指令仍可用", font(F_REG, 12), fill=DIM, anchor="mm")
    hint_bar(d, "按任意键返回", "")
    return img


def page_settings() -> Image.Image:
    img, d = new_canvas()
    status_bar(d, INFO, "设置", icon="dot")
    rows = [("亮度", "70%"), ("音量", "40%"), ("蓝牙", "已连接"),
            ("固件", "v1.0.0"), ("关于", ">")]
    for i, (k, v) in enumerate(rows):
        y = 58 + i * 38
        rrect(d, (12, y, 228, y + 32), 8, fill=CARD)
        text(d, (24, y + 16), k, font(F_REG, 13), fill=TEXT, anchor="lm")
        text(d, (216, y + 16), v, font(F_REG, 13), fill=DIM, anchor="rm")
    hint_bar(d, "KEY2 返回", "")
    return img


PAGES = [
    ("01-home", "主界面 · 意图网格", page_home),
    ("02-lock", "锁屏", page_lock),
    ("03-recording", "录音中", page_recording),
    ("04-asr", "识别中", page_asr),
    ("05-confirm", "确认（含倒计时）", page_confirm),
    ("06-ok", "成功", page_ok),
    ("07-error", "失败", page_err),
    ("08-settings", "设置", page_settings),
]


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    images = []
    for key, title, fn in PAGES:
        img = fn()
        img.save(os.path.join(OUT, f"{key}.png"))
        images.append((title, img))

    # 总览图：4 列 × 2 行
    cols, pad, label_h = 4, 16, 26
    cw, ch = W * S, H * S
    sheet = Image.new("RGB", (cols * cw + (cols + 1) * pad,
                              ((len(images) + cols - 1) // cols) * (ch + label_h + pad) + pad),
                      (32, 36, 42))
    sd = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(F_REG, 15, index=0)
    for i, (title, img) in enumerate(images):
        c, r = i % cols, i // cols
        x = pad + c * (cw + pad)
        y = pad + r * (ch + label_h + pad)
        sd.text((x, y), title, font=f, fill=(200, 210, 220))
        sheet.paste(img, (x, y + label_h))
    sheet.save(os.path.join(OUT, "all-states.png"))
    print(f"已生成 {len(images)} 张 + all-states.png → {OUT}")


if __name__ == "__main__":
    main()
