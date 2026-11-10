"""打印当前 ASR 语法约束用的短语表（给"词表里到底有什么"这个问题用）。"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "..", "vendor", "_pylibs"))

import gateway as G  # noqa: E402


def main() -> int:
    g = G.Gateway.__new__(G.Gateway)
    g._phrases = None
    phrases = G.Gateway._asr_phrases(g)

    print(f"语法约束短语表：共 {len(phrases)} 条\n")
    for i, p in enumerate(phrases, 1):
        print(f"  {i:2d}. {p}")

    print("\n来源（白名单关键词）：")
    for words, code in G.FALLBACK_KEYWORDS:
        act = G.WHITELIST.get(code)
        name = act.name if act else "?"
        print(f"  {code}  {name:10s} 关键词：{' / '.join(words)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
