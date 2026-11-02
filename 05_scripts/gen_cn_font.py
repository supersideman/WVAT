"""从系统字体生成 LVGL 8.x 的 1bpp 中文字库 C 文件。

为什么要自己生成：
    LVGL 自带的 lv_font_simsun_16_cjk 只含 1000 个常用偏旁/字，
    实测"帮、开、资、现、调"这类常用字都没有，无法用于显示语音转写。

做法：
    - 用 PIL 从系统字体（微软雅黑 / 黑体）按像素栅格化指定字符集
    - 输出 1bpp 位图 + LVGL 的 lv_font_fmt_txt_dsc_t 结构
    - 结构与 LVGL 官方生成的字体文件保持一致（已对照 lv_font_simsun_16_cjk.c）

用法：
    python gen_cn_font.py --size 14 --out <输出.c> [--name lv_font_cn_14]
                          [--charset gb1|common] [--font "C:/Windows/Fonts/msyh.ttc"]
"""
from __future__ import annotations

import argparse
import os

from PIL import Image, ImageDraw, ImageFont

# GB2312 一级汉字：B0A1–D7F9（3755 个常用字）
GB1_RANGES = [(0xB0, 0xA1, 0xD7, 0xF9)]

# 中文标点与符号（GB2312 的 A1/A3 区常用部分）
PUNCT = "，。、；：？！“”‘’（）《》…—·"


def build_charset(kind: str, with_ascii: bool) -> list[str]:
    chars: set[str] = set()
    if kind == "gb1":
        for hi0, lo0, hi1, lo1 in GB1_RANGES:
            for hi in range(hi0, hi1 + 1):
                lo_start = lo0 if hi == hi0 else 0xA1
                lo_end = lo1 if hi == hi1 else 0xFE
                for lo in range(lo_start, lo_end + 1):
                    try:
                        chars.add(bytes([hi, lo]).decode("gbk"))
                    except UnicodeDecodeError:
                        pass
    chars.update(PUNCT)
    if with_ascii:
        for c in range(0x20, 0x7F):
            chars.add(chr(c))
    # 过滤掉空白与不可打印
    return sorted(c for c in chars if c.strip() or c == " ")


def pack_1bpp(img: Image.Image) -> bytes:
    """把 mode='1' 图像按行打包成 1bpp，每行字节对齐，MSB 在前。"""
    w, h = img.size
    pixels = img.load()
    out = bytearray()
    for y in range(h):
        byte = 0
        bits = 0
        for x in range(w):
            byte = (byte << 1) | (1 if pixels[x, y] else 0)
            bits += 1
            if bits == 8:
                out.append(byte)
                byte = 0
                bits = 0
        if bits:                       # 补齐到整字节
            out.append(byte << (8 - bits))
    return bytes(out)


def generate(font_path: str, size: int, charset: list[str], name: str, out_path: str) -> None:
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyph_blobs: list[bytes] = []
    glyphs = []           # (bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y)
    unicodes: list[int] = []

    bitmap = bytearray()

    for ch in charset:
        bbox = font.getbbox(ch)
        if not bbox:
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            continue
        if w > 255 or h > 255:
            continue

        # 画在足够大的画布上再裁切，保证不越界
        canvas = Image.new("1", (size * 3, size * 3), 0)
        ImageDraw.Draw(canvas).text((size, size), ch, font=font, fill=1)
        glyph = canvas.crop((size + x0, size + y0, size + x1, size + y1))

        blob = pack_1bpp(glyph)
        idx = len(bitmap)
        bitmap += blob

        adv_w = int(round(font.getlength(ch) * 16))
        glyphs.append((idx, adv_w, w, h, x0, y0 - ascent))
        unicodes.append(ord(ch))

    # ── 生成 C 文件 ──────────────────────────────
    guard = name.upper()
    lines: list[str] = []
    a = lines.append

    a("/* 自动生成，请勿手改 —— 由 05_scripts/gen_cn_font.py 生成")
    a(f" * 字号 {size}px / 1bpp / 字符数 {len(glyphs)} / 源字体 {os.path.basename(font_path)}")
    a(" */")
    a(f'#include "lvgl.h"')
    a("")
    a(f"#ifndef {guard}")
    a(f"#define {guard}")
    a("")
    a("/* 字形位图（1bpp，每行字节对齐，MSB 在前） */")
    a("static const uint8_t glyph_bitmap[] = {")
    for i in range(0, len(bitmap), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in bitmap[i:i + 16])
        a(f"    {chunk},")
    a("};")
    a("")
    a("/* 字形描述：[0] 为保留项（LVGL 约定），之后与 unicode_list 一一对应 */")
    a("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    a("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for idx, adv_w, bw, bh, ox, oy in glyphs:
        a(f"    {{.bitmap_index = {idx}, .adv_w = {adv_w}, .box_w = {bw}, .box_h = {bh}, "
          f".ofs_x = {ox}, .ofs_y = {oy}}},")
    a("};")
    a("")
    a("static const uint16_t unicode_list[] = {")
    for i in range(0, len(unicodes), 10):
        chunk = ", ".join(f"0x{u:04x}" for u in unicodes[i:i + 10])
        a(f"    {chunk},")
    a("};")
    a("")
    a("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    a("    {")
    a("        .range_start = 0x20,")
    a("        .range_length = 0,")
    a("        .glyph_id_start = 1,")
    a("        .unicode_list = unicode_list,")
    a("        .glyph_id_ofs_list = NULL,")
    a(f"        .list_length = {len(unicodes)},")
    a("        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,")
    a("    },")
    a("};")
    a("")
    a("#if LV_VERSION_CHECK(8, 0, 0)")
    a("static lv_font_fmt_txt_glyph_cache_t cache;")
    a("#endif")
    a("")
    a("#if LV_VERSION_CHECK(8, 0, 0)")
    a("static lv_font_fmt_txt_dsc_t font_dsc = {")
    a("#else")
    a("static lv_font_fmt_txt_dsc_t font_dsc = {")
    a("#endif")
    a("    .glyph_bitmap = glyph_bitmap,")
    a("    .glyph_dsc = glyph_dsc,")
    a("    .cmaps = cmaps,")
    a("    .kern_dsc = NULL,")
    a("    .kern_scale = 0,")
    a("    .cmap_num = 1,")
    a("    .bpp = 1,")
    a("    .kern_classes = 0,")
    a("    .bitmap_format = 0,")
    a("#if LV_VERSION_CHECK(8, 0, 0)")
    a("    .cache = &cache")
    a("#endif")
    a("};")
    a("")
    a(f"const lv_font_t {name} = {{")
    a("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    a("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    a(f"    .line_height = {line_height},")
    a(f"    .base_line = {descent},")
    a("    .subpx = LV_FONT_SUBPX_NONE,")
    a("    .underline_position = -2,")
    a("    .underline_thickness = 1,")
    a("    .dsc = &font_dsc,")
    a("};")
    a("")
    a(f"#endif /* {guard} */")

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")

    total = len(bitmap) + len(glyphs) * 8 + len(unicodes) * 2
    print(f"已生成 {out_path}")
    print(f"  字符数 {len(glyphs)} · 位图 {len(bitmap)/1024:.1f} KB · 预计 ROM 占用 {total/1024:.1f} KB")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=14)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="")
    ap.add_argument("--charset", default="gb1", choices=["gb1", "common"])
    ap.add_argument("--font", default="C:/Windows/Fonts/msyh.ttc")
    ap.add_argument("--no-ascii", action="store_true")
    args = ap.parse_args()

    name = args.name or f"lv_font_cn_{args.size}"
    if not os.path.exists(args.font):
        raise SystemExit(f"字体不存在：{args.font}")

    charset = build_charset(args.charset, not args.no_ascii)
    generate(args.font, args.size, charset, name, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
