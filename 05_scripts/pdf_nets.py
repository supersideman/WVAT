"""从作者 exCam 扩展板原理图 PDF 里提取网络名/引脚标号，用来确认底部 2x9 排针是什么。

只做一件事：解压 PDF 的 stream，抓括号里的文本片段，过滤出可能的网络名。
"""
from __future__ import annotations

import re
import sys
import zlib

PDF = sys.argv[1] if len(sys.argv) > 1 else "exCam_sch.pdf"

raw = open(PDF, "rb").read()

texts: list[str] = []
for m in re.finditer(rb"stream\r?\n", raw):
    start = m.end()
    end = raw.find(b"endstream", start)
    if end < 0:
        continue
    try:
        data = zlib.decompress(raw[start:end])
    except Exception:
        continue
    for t in re.findall(rb"\((?:[^()\\]|\\.)*\)", data):
        try:
            s = t[1:-1].decode("latin-1")
        except Exception:
            continue
        s = s.strip()
        if s:
            texts.append(s)

print(f"文本片段总数: {len(texts)}")

uniq: list[str] = []
seen: set[str] = set()
for t in texts:
    if t not in seen:
        seen.add(t)
        uniq.append(t)

print(f"唯一片段数: {len(uniq)}")
print("--- 看起来像网络名/引脚名的片段 ---")
pat = re.compile(r"^(P[A-C]\d+|PB\d+|PC\d+|PD\d+|PA\d+|3V3|5V|GND|SCL|SDA|VSYNC|HREF|PCLK|XCLK|D[0-7]|SDA|SCL|CS|RESET|PWDN|LCD_\w+|TP_\w+|\d{1,2})$")
hits = [t for t in uniq if pat.match(t)]
print("  " + " | ".join(hits[:80]) if hits else "  （没有匹配到）")

print("--- 含 GND/3V3 的片段 ---")
gnd = [t for t in uniq if "GND" in t or "3V3" in t]
print("  " + " | ".join(gnd[:40]) if gnd else "  （无）")
