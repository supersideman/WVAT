"""把 Markdown 转成排版良好的 DOCX（专门为本项目的讲解文档写的轻量转换器）。

支持：# ~ #### 标题、正文、- 无序列表、1. 有序列表、| 表格 |、``` 代码块、
> 引用、--- 分隔线、行内 **粗体** 与 `代码`。

不做完整 Markdown 规范，只覆盖本文档实际用到的语法。
"""
from __future__ import annotations

import re
import sys

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

CODE_FONT = "Consolas"
BODY_FONT = "微软雅黑"
MONO_COLOR = RGBColor(0x1F, 0x3A, 0x5F)


# ── 基础排版工具 ────────────────────────────────────────
def set_cjk(run, font_name: str) -> None:
    """中文必须同时设置 eastAsia，否则 Word 用默认宋体，观感不一致。"""
    run.font.name = font_name
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn("w:rFonts"))
    if rFonts is None:
        rFonts = OxmlElement("w:rFonts")
        rPr.append(rFonts)
    rFonts.set(qn("w:ascii"), font_name)
    rFonts.set(qn("w:hAnsi"), font_name)
    rFonts.set(qn("w:eastAsia"), font_name)


def shade(element, fill: str) -> None:
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    element.append(shd)


def add_code_block(doc: Document, lines: list[str]) -> None:
    """代码块：等宽字体 + 浅灰底 + 细边框，每行一个段落便于分页。"""
    for text in lines:
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.space_before = Pt(0)
        pf.space_after = Pt(0)
        pf.left_indent = Cm(0.4)
        pf.line_spacing = 1.0
        run = p.add_run(text if text else " ")
        set_cjk(run, CODE_FONT)
        run.font.size = Pt(8.5)
        run.font.color.rgb = MONO_COLOR
        shade(p._p.get_or_add_pPr(), "F2F4F7")
    # 代码块后留一点空
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


INLINE = re.compile(r"(\*\*.+?\*\*|`[^`]+`)")


def is_cjk(ch: str) -> bool:
    return "\u4e00" <= ch <= "\u9fff" or ch in "，。、；：？！（）《》“”‘’"


def join_lines(parts: list[str]) -> str:
    """Markdown 里单换行 = 同一段落。英文之间补空格，中文之间不补。"""
    out = ""
    for part in parts:
        if not out:
            out = part
        elif out and is_cjk(out[-1]) or (out and part and is_cjk(part[0])):
            out += part
        else:
            out += " " + part
    return out


def add_rich_text(paragraph, text: str) -> None:
    """处理行内 **粗体** 与 `代码`。"""
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            run = paragraph.add_run(part[2:-2])
            set_cjk(run, BODY_FONT)
            run.bold = True
        elif part.startswith("`") and part.endswith("`"):
            run = paragraph.add_run(part[1:-1])
            set_cjk(run, CODE_FONT)
            run.font.size = Pt(9.5)
            run.font.color.rgb = MONO_COLOR
        else:
            run = paragraph.add_run(part)
            set_cjk(run, BODY_FONT)


def add_table(doc: Document, rows: list[list[str]]) -> None:
    cols = max(len(r) for r in rows)
    table = doc.add_table(rows=len(rows), cols=cols)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, row in enumerate(rows):
        for j in range(cols):
            cell = table.cell(i, j)
            cell.text = ""
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(1)
            p.paragraph_format.space_after = Pt(1)
            value = row[j] if j < len(row) else ""
            add_rich_text(p, value)
            for run in p.runs:
                run.font.size = Pt(9)
                if i == 0:
                    run.bold = True
            if i == 0:
                shade(cell._tc.get_or_add_tcPr(), "E8EEF5")
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


# ── 主转换 ──────────────────────────────────────────────
def convert(md_path: str, docx_path: str, title: str) -> None:
    with open(md_path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()

    doc = Document()

    # 页面：A4 + 适中页边距
    section = doc.sections[0]
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(2.2)
    section.bottom_margin = Cm(2.2)
    section.left_margin = Cm(2.4)
    section.right_margin = Cm(2.0)

    normal = doc.styles["Normal"]
    normal.font.size = Pt(10.5)
    normal.font.name = BODY_FONT
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), BODY_FONT)
    normal.paragraph_format.line_spacing = 1.25
    normal.paragraph_format.space_after = Pt(4)

    # 标题由 Markdown 里的第一个 # 提供，这里不再单独加，避免重复
    first_h1 = True
    doc.add_paragraph()          # 顶部留白

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()

        # 代码块
        if stripped.startswith("```"):
            i += 1
            block: list[str] = []
            while i < n and not lines[i].strip().startswith("```"):
                block.append(lines[i].rstrip())
                i += 1
            i += 1
            add_code_block(doc, block)
            continue

        # 表格
        if stripped.startswith("|") and i + 1 < n and set(lines[i + 1].strip()) <= set("|-: "):
            rows: list[list[str]] = []
            header = [c.strip() for c in stripped.strip("|").split("|")]
            rows.append(header)
            i += 2
            while i < n and lines[i].strip().startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            add_table(doc, rows)
            continue

        # 标题
        m = re.match(r"^(#{1,4})\s+(.*)$", stripped)
        if m:
            level = len(m.group(1))
            text = m.group(2)
            if level == 1 and first_h1:
                doc.add_heading(text, level=0)      # 文档标题
                first_h1 = False
            elif level == 1:
                doc.add_heading(text, level=1)
            elif level == 2:
                doc.add_heading(text, level=2)
            else:
                doc.add_heading(text, level=3)
            i += 1
            continue

        # 分隔线
        if stripped in ("---", "***"):
            p = doc.add_paragraph()
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            pPr = p._p.get_or_add_pPr()
            bdr = OxmlElement("w:pBdr")
            bottom = OxmlElement("w:bottom")
            bottom.set(qn("w:val"), "single")
            bottom.set(qn("w:sz"), "6")
            bottom.set(qn("w:color"), "BFC7D1")
            bdr.append(bottom)
            pPr.append(bdr)
            i += 1
            continue

        # 引用（连续多行合并成一段）
        if stripped.startswith(">"):
            buf = []
            while i < n and lines[i].strip().startswith(">"):
                buf.append(lines[i].strip().lstrip(">").strip())
                i += 1
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.6)
            add_rich_text(p, join_lines([b for b in buf if b]))
            for run in p.runs:
                run.italic = True
                run.font.color.rgb = RGBColor(0x44, 0x4F, 0x5A)
            continue

        # 无序列表
        if re.match(r"^[-*]\s+", stripped):
            text = re.sub(r"^[-*]\s+", "", stripped)
            p = doc.add_paragraph(style="List Bullet")
            add_rich_text(p, text)
            i += 1
            continue

        # 有序列表
        if re.match(r"^\d+\.\s+", stripped):
            text = re.sub(r"^\d+\.\s+", "", stripped)
            p = doc.add_paragraph(style="List Number")
            add_rich_text(p, text)
            i += 1
            continue

        # 空行
        if not stripped:
            i += 1
            continue

        # 普通段落：把连续的非空普通行合并成一段（Markdown 的单换行语义）
        buf = []
        while i < n:
            cur = lines[i].strip()
            if (not cur or cur.startswith(("#", ">", "```", "|", "---", "***"))
                    or re.match(r"^[-*]\s+", cur) or re.match(r"^\d+\.\s+", cur)):
                break
            buf.append(cur)
            i += 1
        if not buf:
            # 兜底：这一行虽然开头像特殊语法，但没有被上面任何分支消化，
            # 必须强行消费掉，否则 i 不前进 → 死循环
            buf.append(stripped)
            i += 1
        p = doc.add_paragraph()
        add_rich_text(p, join_lines(buf))

    doc.core_properties.title = title
    doc.core_properties.author = "WVAT 项目"
    doc.save(docx_path)
    print(f"已生成：{docx_path}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("用法：md2docx.py <输入.md> <输出.docx> [标题]")
        raise SystemExit(2)
    convert(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "文档")
