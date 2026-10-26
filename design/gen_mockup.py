"""生成 240×280 手表面板的像素级界面稿（设计评审用，不是真机渲染）

v2 变更（依据 2026-10-20 评审）：
- 主界面改为「快捷 4 格 + 大号说话按钮」，多个意图收进抽屉页
- 主界面出 A/B 两版：是否常驻「最近一次结果」
- 锁屏去掉了文字提示与箭头，只留大时钟
- 新增抽屉页（3×3 全部意图）

用法：python design/gen_mockup.py
"""
from __future__ import annotations

import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 280
S = 2
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui-mockups")

BG     = (14, 17, 22)
CARD   = (27, 33, 41)
TEXT   = (232, 237, 242)
DIM    = (138, 150, 163)
ACCENT = (53, 208, 186)
DANGER = (255, 92, 92)
WARN   = (255, 194, 75)
INFO   = (74, 158, 255)

F_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
F_REG = r"C:\Windows\Fonts\msyh.ttc"


def font(path: str, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, size * S, index=0)


def new_canvas():
    img = Image.new("RGB", (W * S, H * S), BG)
    return img, ImageDraw.Draw(img)


def rrect(d, box, r, fill=None, outline=None, width=2):
    d.rounded_rectangle([c * S for c in box], radius=r * S, fill=fill,
                        outline=outline, width=width * S)


def text(d, xy, s, f, fill=TEXT, anchor="la"):
    d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)


def status_bar(d, color, label, icon="dot"):
    rrect(d, (0, 0, W, 40), 0, fill=CARD)
    d.rectangle([0, 0, 3 * S, 40 * S], fill=color)
    cx, cy = 22, 20
    if icon == "mic":
        d.rounded_rectangle([(cx - 4) * S, (cy - 9) * S, (cx + 4) * S, (cy + 3) * S], radius=3 * S, fill=color)
        d.arc([(cx - 8) * S, (cy - 2) * S, (cx + 8) * S, (cy + 10) * S], 0, 180, fill=color, width=2 * S)
        d.line([cx * S, (cy + 10) * S, cx * S, (cy + 13) * S], fill=color, width=2 * S)
    elif icon == "check":
        d.line([(cx - 7) * S, cy * S, (cx - 2) * S, (cy + 6) * S], fill=color, width=3 * S)
        d.line([(cx - 2) * S, (cy + 6) * S, (cx + 8) * S, (cy - 6) * S], fill=color, width=3 * S)
    elif icon == "cross":
        d.line([(cx - 6) * S, (cy - 6) * S, (cx + 6) * S, (cy + 6) * S], fill=color, width=3 * S)
        d.line([(cx + 6) * S, (cy - 6) * S, (cx - 6) * S, (cy + 6) * S], fill=color, width=3 * S)
    else:
        d.ellipse([(cx - 6) * S, (cy - 6) * S, (cx + 6) * S, (cy + 6) * S], fill=color)
    text(d, (40, 20), label, font(F_BOLD, 17), fill=color, anchor="lm")


def hint_bar(d, left, right=""):
    rrect(d, (0, H - 36, W, H), 0, fill=CARD)
    if left:
        text(d, (12, H - 18), left, font(F_REG, 12), fill=DIM, anchor="lm")
    if right:
        text(d, (W - 12, H - 18), right, font(F_REG, 12), fill=DIM, anchor="rm")


def glyph(d, kind, cx, cy, color, bg=CARD):
    if kind == "music":
        d.ellipse([(cx - 8) * S, (cy + 2) * S, (cx - 1) * S, (cy + 9) * S], fill=color)
        d.ellipse([(cx + 3) * S, (cy - 1) * S, (cx + 10) * S, (cy + 6) * S], fill=color)
        d.line([(cx - 1) * S, (cy + 5) * S, (cx - 1) * S, (cy - 9) * S], fill=color, width=2 * S)
        d.line([(cx + 10) * S, (cy + 2) * S, (cx + 10) * S, (cy - 12) * S], fill=color, width=2 * S)
        d.line([(cx - 1) * S, (cy - 9) * S, (cx + 10) * S, (cy - 12) * S], fill=color, width=2 * S)
    elif kind == "next":
        d.polygon([((cx - 8) * S, (cy - 8) * S), ((cx - 8) * S, (cy + 8) * S), ((cx + 2) * S, cy * S)], fill=color)
        d.rectangle([(cx + 3) * S, (cy - 8) * S, (cx + 7) * S, (cy + 8) * S], fill=color)
    elif kind == "moon":
        d.ellipse([(cx - 10) * S, (cy - 10) * S, (cx + 10) * S, (cy + 10) * S], fill=color)
        d.ellipse([(cx - 3) * S, (cy - 12) * S, (cx + 15) * S, (cy + 8) * S], fill=bg)
    elif kind == "vol":
        d.polygon([((cx - 10) * S, (cy - 4) * S), ((cx - 4) * S, (cy - 4) * S),
                   ((cx + 2) * S, (cy - 10) * S), ((cx + 2) * S, (cy + 10) * S),
                   ((cx - 4) * S, (cy + 4) * S), ((cx - 10) * S, (cy + 4) * S)], fill=color)
        d.arc([(cx + 2) * S, (cy - 8) * S, (cx + 12) * S, (cy + 8) * S], -60, 60, fill=color, width=2 * S)
    elif kind == "mute":
        d.polygon([((cx - 10) * S, (cy - 4) * S), ((cx - 4) * S, (cy - 4) * S),
                   ((cx + 2) * S, (cy - 10) * S), ((cx + 2) * S, (cy + 10) * S),
                   ((cx - 4) * S, (cy + 4) * S), ((cx - 10) * S, (cy + 4) * S)], fill=color)
        d.line([(cx + 5) * S, (cy - 5) * S, (cx + 11) * S, (cy + 5) * S], fill=color, width=2 * S)
        d.line([(cx + 11) * S, (cy - 5) * S, (cx + 5) * S, (cy + 5) * S], fill=color, width=2 * S)
    elif kind == "folder":
        rrect(d, (cx - 11, cy - 7, cx + 11, cy + 8), 2, fill=color)
        d.rectangle([(cx - 11) * S, (cy - 10) * S, (cx - 2) * S, (cy - 6) * S], fill=color)
    elif kind == "list":
        for y in (-7, 0, 7):
            d.ellipse([(cx - 10) * S, (cy + y - 1) * S, (cx - 8) * S, (cy + y + 1) * S], fill=color)
            d.line([(cx - 5) * S, (cy + y) * S, (cx + 10) * S, (cy + y) * S], fill=color, width=2 * S)
    elif kind == "mic":
        d.rounded_rectangle([(cx - 5) * S, (cy - 11) * S, (cx + 5) * S, (cy + 3) * S], radius=4 * S, fill=color)
        d.arc([(cx - 10) * S, (cy - 3) * S, (cx + 10) * S, (cy + 12) * S], 0, 180, fill=color, width=2 * S)
        d.line([cx * S, (cy + 12) * S, cx * S, (cy + 15) * S], fill=color, width=2 * S)
    elif kind == "gear":
        d.ellipse([(cx - 7) * S, (cy - 7) * S, (cx + 7) * S, (cy + 7) * S], outline=color, width=3 * S)
        for i in range(8):
            a = i * math.pi / 4
            d.line([(cx + 8 * math.cos(a)) * S, (cy + 8 * math.sin(a)) * S,
                    (cx + 12 * math.cos(a)) * S, (cy + 12 * math.sin(a)) * S], fill=color, width=3 * S)
    elif kind == "grid":
        for gx in (cx - 9, cx + 1):
            for gy in (cy - 9, cy + 1):
                d.rounded_rectangle([gx * S, gy * S, (gx + 8) * S, (gy + 8) * S], radius=2 * S, fill=color)


def quick_tile(d, kind, label, x, y, color=TEXT):
    rrect(d, (x, y, x + 48, y + 48), 10, fill=CARD)
    glyph(d, kind, x + 24, y + 18, color)
    text(d, (x + 24, y + 39), label, font(F_REG, 11), fill=DIM, anchor="mm")


def big_talk_button(d, y=196):
    rrect(d, (12, y, 228, y + 52), 12, outline=ACCENT, width=2)
    glyph(d, "mic", 46, y + 26, ACCENT, bg=BG)
    text(d, (86, y + 26), "按住说话", font(F_BOLD, 17), fill=ACCENT, anchor="lm")


# ── 页面 ───────────────────────────────────────────────────────
def home(with_result: bool):
    img, d = new_canvas()
    text(d, (W / 2, 42), "10:24", font(F_BOLD, 38), anchor="mm")
    text(d, (W / 2, 74), "10月20日 周一", font(F_REG, 13), fill=DIM, anchor="mm")

    if with_result:
        rrect(d, (12, 96, 228, 122), 8, fill=CARD)
        d.ellipse([20 * S, 105 * S, 28 * S, 113 * S], fill=ACCENT)
        text(d, (34, 109), "上次：已锁屏并静音", font(F_REG, 12), fill=DIM, anchor="lm")
        tiles_y, talk_y = 132, 196
    else:
        tiles_y, talk_y = 112, 196

    quick_tile(d, "music", "放歌", 12, tiles_y)
    quick_tile(d, "moon", "我困了", 68, tiles_y)
    quick_tile(d, "folder", "资料", 124, tiles_y)
    quick_tile(d, "grid", "更多", 180, tiles_y, ACCENT)

    big_talk_button(d, talk_y)
    return img


def lock():
    img, d = new_canvas()
    text(d, (W / 2, 112), "10:24", font(F_BOLD, 52), anchor="mm")
    text(d, (W / 2, 150), "10月20日 周一", font(F_REG, 14), fill=DIM, anchor="mm")
    return img


def drawer():
    """全部意图：3×3 收纳格"""
    img, d = new_canvas()
    rrect(d, (0, 0, W, 40), 0, fill=CARD)
    text(d, (16, 20), "全部意图", font(F_BOLD, 17), anchor="lm")
    text(d, (W - 14, 20), "9", font(F_REG, 13), fill=DIM, anchor="rm")

    items = [("music", "放歌"), ("next", "下一首"), ("moon", "我困了"),
             ("vol", "音量+"), ("mute", "音量-"), ("folder", "资料"),
             ("list", "状态"), ("mic", "说话"), ("gear", "设置")]
    for i, (kind, name) in enumerate(items):
        col, row = i % 3, i // 3
        x = 12 + col * 74
        y = 50 + row * 64
        rrect(d, (x, y, x + 66, y + 56), 10, fill=CARD)
        glyph(d, kind, x + 33, y + 21, ACCENT if kind == "mic" else TEXT)
        text(d, (x + 33, y + 43), name, font(F_REG, 12), fill=DIM, anchor="mm")
    hint_bar(d, "KEY2 返回", "按住 KEY1 说话")
    return img


def recording():
    img, d = new_canvas()
    status_bar(d, DANGER, "正在录音", icon="mic")
    cx, cy = W / 2, 120
    for i in range(17):
        x = cx - 64 + i * 8
        h = 6 + 22 * abs(math.sin(i * 0.7))
        d.line([x * S, (cy - h) * S, x * S, (cy + h) * S], fill=DANGER, width=3 * S)
    text(d, (W / 2, 176), "请说话…", font(F_REG, 14), anchor="mm")
    text(d, (W / 2, 200), "00:02", font(F_BOLD, 16), fill=DIM, anchor="mm")
    hint_bar(d, "KEY2 取消", "松开发送")
    return img


def asr():
    img, d = new_canvas()
    status_bar(d, WARN, "识别中")
    text(d, (W / 2, 120), "· · ·", font(F_BOLD, 26), fill=WARN, anchor="mm")
    text(d, (W / 2, 160), "把语音转成文字", font(F_REG, 13), fill=DIM, anchor="mm")
    hint_bar(d, "")
    return img


def confirm():
    img, d = new_canvas()
    status_bar(d, INFO, "请确认")
    rrect(d, (16, 62, 224, 150), 10, fill=CARD)
    text(d, (120, 78), "识别结果", font(F_REG, 12), fill=DIM, anchor="mm")
    text(d, (120, 104), "帮我锁屏并静音", font(F_BOLD, 16), anchor="mm")
    text(d, (120, 128), "将执行：A03 我困了", font(F_REG, 12), fill=INFO, anchor="mm")
    d.arc([96 * S, 168 * S, 144 * S, 216 * S], -90, 180, fill=ACCENT, width=4 * S)
    text(d, (120, 192), "3", font(F_BOLD, 24), fill=ACCENT, anchor="mm")
    hint_bar(d, "KEY1 确认", "KEY2 重录")
    return img


def ok():
    img, d = new_canvas()
    status_bar(d, ACCENT, "已完成", icon="check")
    d.ellipse([84 * S, 96 * S, 156 * S, 168 * S], outline=ACCENT, width=3 * S)
    d.line([101 * S, 132 * S, 114 * S, 146 * S], fill=ACCENT, width=5 * S)
    d.line([114 * S, 146 * S, 141 * S, 116 * S], fill=ACCENT, width=5 * S)
    text(d, (W / 2, 196), "已锁屏并静音", font(F_BOLD, 16), anchor="mm")
    hint_bar(d, "1.5 秒后返回", "")
    return img


def error():
    img, d = new_canvas()
    status_bar(d, DANGER, "失败", icon="cross")
    d.ellipse([84 * S, 96 * S, 156 * S, 168 * S], outline=DANGER, width=3 * S)
    d.line([103 * S, 117 * S, 137 * S, 151 * S], fill=DANGER, width=5 * S)
    d.line([137 * S, 117 * S, 103 * S, 151 * S], fill=DANGER, width=5 * S)
    text(d, (W / 2, 196), "网络未连接", font(F_BOLD, 16), fill=DANGER, anchor="mm")
    text(d, (W / 2, 220), "指令仍可用", font(F_REG, 12), fill=DIM, anchor="mm")
    hint_bar(d, "按任意键返回", "")
    return img


def settings():
    img, d = new_canvas()
    status_bar(d, INFO, "设置")
    rows = [("亮度", "70%"), ("音量", "40%"), ("蓝牙", "已连接"),
            ("固件", "v1.0.0"), ("关于", ">")]
    for i, (k, v) in enumerate(rows):
        y = 58 + i * 38
        rrect(d, (12, y, 228, y + 32), 8, fill=CARD)
        text(d, (24, y + 16), k, font(F_REG, 13), anchor="lm")
        text(d, (216, y + 16), v, font(F_REG, 13), fill=DIM, anchor="rm")
    hint_bar(d, "KEY2 返回", "")
    return img


PAGES = [
    ("01-home-a-无结果行", "主界面 A · 无结果行", lambda: home(False)),
    ("02-home-b-有结果行", "主界面 B · 有结果行", lambda: home(True)),
    ("03-lock", "锁屏（无提示）", lock),
    ("04-drawer", "全部意图（收纳格）", drawer),
    ("05-recording", "录音中", recording),
    ("06-asr", "识别中", asr),
    ("07-confirm", "确认（3s 倒计时）", confirm),
    ("08-ok", "成功", ok),
    ("09-error", "失败", error),
    ("10-settings", "设置", settings),
]


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    images = []
    for key, title, fn in PAGES:
        img = fn()
        img.save(os.path.join(OUT, f"{key}.png"))
        images.append((title, img))

    cols, pad, label_h = 5, 14, 24
    cw, ch = W * S, H * S
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw + (cols + 1) * pad,
                              rows * (ch + label_h + pad) + pad), (32, 36, 42))
    sd = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(F_REG, 14, index=0)
    for i, (title, img) in enumerate(images):
        c, r = i % cols, i // cols
        x = pad + c * (cw + pad)
        y = pad + r * (ch + label_h + pad)
        sd.text((x, y), title, font=f, fill=(205, 215, 225))
        sheet.paste(img, (x, y + label_h))
    sheet.save(os.path.join(OUT, "all-states.png"))
    print(f"已生成 {len(images)} 张 + all-states.png → {OUT}")


if __name__ == "__main__":
    main()
