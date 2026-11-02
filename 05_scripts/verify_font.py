"""校验生成的 LVGL 字库：按 LVGL 的取位规则把字形画成图，肉眼确认是否可读。

为什么需要它：
    字库格式的错误（码表、位图打包、ofs_y 符号）在设备上只表现为"雪花点"，
    烧一次板子才能发现，耗时且在浪费用户时间。
    这里直接在 PC 上按 LVGL 的规则解码自己生成的 .c 文件，先看一眼效果。

用法：
    python verify_font.py <字体.c> [测试文字] [输出.png]
"""
from __future__ import annotations

import bisect
import re
import sys

from PIL import Image


def parse_font(path: str):
    src = open(path, encoding="utf-8-sig").read()

    def nums(block: str) -> list[int]:
        return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", block)]

    m = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("没找到 glyph_bitmap")
    bitmap = bytes(nums(m.group(1)))

    m = re.search(r"unicode_list\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("没找到 unicode_list")
    ulist = nums(m.group(1))

    m = re.search(r"\.range_start\s*=\s*(0x[0-9a-fA-F]+|\d+)", src)
    range_start = int(m.group(1), 0)

    m = re.search(r"\.line_height\s*=\s*(\d+)", src)
    line_height = int(m.group(1))
    m = re.search(r"\.base_line\s*=\s*(-?\d+)", src)
    base_line = int(m.group(1))

    m = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("没找到 glyph_dsc")
    dsc = []
    for g in re.finditer(r"\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*"
                         r"\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+),\s*"
                         r"\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)",
                         m.group(1)):
        dsc.append(tuple(int(g.group(i)) for i in range(1, 7)))

    return bitmap, ulist, range_start, line_height, base_line, dsc


def render(text: str, font, scale: int = 4):
    bitmap, ulist, range_start, line_height, base_line, dsc = font
    width = 20 + sum(16 for _ in text) + 20       # 粗估画布宽度
    img = Image.new("L", (width, line_height + 16), 0)
    px = img.load()
    pen = 10
    baseline = 8 + base_line

    for ch in text:
        rcp = ord(ch) - range_start
        i = bisect.bisect_left(ulist, rcp)
        if i >= len(ulist) or ulist[i] != rcp:
            pen += 16                              # 缺字：留空跳过
            continue
        gid = 1 + i
        if gid >= len(dsc):
            continue
        bidx, adv_w, bw, bh, ox, oy = dsc[gid]

        # 按 LVGL 规则逐位取：连续比特流，bit_ofs = y*bw + x
        for y in range(bh):
            for x in range(bw):
                bit = y * bw + x
                byte = bitmap[bidx + (bit >> 3)]
                if byte & (1 << (7 - (bit & 7))):
                    gx = pen + ox + x
                    gy = baseline + oy + y
                    if 0 <= gx < width and 0 <= gy < img.height:
                        px[gx, gy] = 255
        pen += adv_w // 16

    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def main() -> int:
    path = sys.argv[1]
    text = sys.argv[2] if len(sys.argv) > 2 else "帮我打开资料"
    out = sys.argv[3] if len(sys.argv) > 3 else "font_check.png"

    font = parse_font(path)
    bitmap, ulist, rs, lh, bl, dsc = font
    print(f"字形数 {len(dsc)-1} · unicode_list {len(ulist)} · range_start 0x{rs:04X} · "
          f"line_height {lh} · base_line {bl} · 位图 {len(bitmap)/1024:.1f} KB")

    lines = ["帮我打开资料", "现在几点", "把音量调高一点", "下一首 我困了"]
    imgs = [render(t, font) for t in lines]

    pad = 8
    sheet = Image.new("L", (max(i.width for i in imgs) + 2 * pad,
                            sum(i.height + pad for i in imgs) + pad), 40)
    y = pad
    for im in imgs:
        sheet.paste(im, (pad, y))
        y += im.height + pad
    sheet.save(out)
    print(f"已输出 {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
