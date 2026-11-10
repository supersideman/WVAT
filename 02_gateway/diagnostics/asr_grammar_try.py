"""对比：自由识别 vs 语法约束识别（用同一段录音）。

为什么做这件事：
    小模型（42MB）对口音/语速敏感，识别成近音字就得靠模糊匹配补救。
    如果**限定只识别我们那十几条命令**，错误率会大幅下降——
    代价是不能自由说长句（但 T 慢通道可以另开一条自由说的路）。

用法：
    python asr_grammar_try.py [wav文件]
"""
from __future__ import annotations

import json
import os
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "..", "vendor", "_pylibs"))

import asr  # noqa: E402

import numpy as np  # noqa: E402


def resample_to_16k(x: np.ndarray, src: int) -> np.ndarray:
    """线性插值重采样到 16k（Vosk 只吃 16k）。"""
    if src == 16000 or x.size == 0:
        return x
    n_out = int(round(x.size * 16000 / src))
    idx = np.linspace(0, x.size - 1, n_out)
    return np.interp(idx, np.arange(x.size), x.astype(np.float64)).astype(np.int16)

WAV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "_mic.wav")

# 与网关白名单一致的命令短语（语法约束只允许输出这些）
PHRASES = [
    "帮我打开资料",
    "打开资料",
    "下一首",
    "播放",
    "暂停",
    "我困了",
    "现在几点",
    "查状态",
    "把音量调高",
    "把音量调低",
    "[unk]",
]


def build_grammar(phrases: list[str], space_chars: bool = False) -> str:
    """生成 Vosk 的 grammar JSON。

    space_chars=True 时把每个汉字用空格隔开——中文模型的词表可能是单字，
    具体哪种生效要实测（这里两种都试）。
    """
    out = []
    for p in phrases:
        if space_chars and p != "[unk]":
            out.append(" ".join(p))
        else:
            out.append(p)
    return json.dumps(out, ensure_ascii=False)


def read_pcm(path: str) -> tuple[bytes, int]:
    with wave.open(path, "rb") as wf:
        rate = wf.getframerate()
        return wf.readframes(wf.getnframes()), rate


def recognize(pcm: bytes, rate: int, grammar: str | None) -> str:
    import vosk

    vosk.SetLogLevel(-1)
    model = vosk.Model(asr.model_dir())
    rec = vosk.KaldiRecognizer(model, rate, grammar) if grammar else vosk.KaldiRecognizer(model, rate)

    chunk = rate * 2 // 5
    for i in range(0, len(pcm), chunk):
        rec.AcceptWaveform(pcm[i:i + chunk])
    res = json.loads(rec.FinalResult())
    return (res.get("text") or "").replace(" ", "")


def main() -> int:
    if not os.path.isfile(WAV):
        print(f"录音文件不存在：{WAV}")
        return 1
    if not asr.model_ready():
        print("模型不可用：", asr.model_dir())
        return 1

    raw, rate = read_pcm(WAV)
    print(f"样本 {os.path.basename(WAV)}: {len(raw)/2/rate:.1f}s @ {rate}Hz")
    x = np.frombuffer(raw, dtype=np.int16)
    pcm = resample_to_16k(x, rate).tobytes()   # Vosk 只接受 16k
    if rate != 16000:
        print(f"已重采样 {rate} -> 16000 Hz")
    rate = 16000

    print("\n[1] 自由识别（当前线上用的方式）")
    print("   →", repr(recognize(pcm, rate, None)))

    print("\n[2] 语法约束 · 整词不分字")
    print("   →", repr(recognize(pcm, rate, build_grammar(PHRASES, False))))

    print("\n[3] 语法约束 · 每字空格分开")
    print("   →", repr(recognize(pcm, rate, build_grammar(PHRASES, True))))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
