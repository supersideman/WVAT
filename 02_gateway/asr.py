"""PC 侧语音识别：从电脑麦克风录音，用 Vosk 离线中文模型转成文字。

为什么放在 PC 而不是手表：
    手表（F411 / 512KB Flash / 128KB RAM）跑不了任何语音模型；
    而且没有电池与联网能力。让 PC 负责"听"和"理解"，手表只负责触发与确认，
    这正是项目定位里"终端不做理解"的落点。

依赖（都在 vendor/_pylibs，无需 pip 安装）：
    sounddevice + cffi（录音）、vosk（识别）、numpy
模型：
    vendor/models/vosk-model-small-cn-0.22（约 42MB，离线）

用法：
    python asr.py                 # 录 4 秒并识别
    python asr.py 6               # 录 6 秒
    python asr.py --dev 10 5      # 指定输入设备
"""
from __future__ import annotations

import json
import os
import queue
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR = os.path.abspath(os.path.join(HERE, "..", "vendor"))
# ⚠️ 一律用规范化绝对路径：带 ".." 的 sys.path 条目在某些环境下
#    会让 .pyd（_cffi_backend）导入失败，报 "No module named"。
PYLIBS = os.path.abspath(os.path.join(VENDOR, "_pylibs"))
if PYLIBS not in sys.path:
    sys.path.insert(0, PYLIBS)

RATE = 16000
MODEL_NAME = "vosk-model-small-cn-0.22"
VENDOR_MODEL = os.path.abspath(os.path.join(VENDOR, "models", MODEL_NAME))
ASCII_MODEL = r"C:\Users\Public\vosk-model-cn"      # Public：ASCII 且普通用户可写

MODEL_DIR = ""                                       # 由 _resolve_model() 填充

_model = None
_recognizer = None


def _is_ascii(path: str) -> bool:
    return all(ord(c) < 128 for c in path)


def _looks_like_model(path: str) -> bool:
    return (os.path.isdir(path)
            and os.path.isfile(os.path.join(path, "am", "final.mdl"))
            and os.path.isfile(os.path.join(path, "conf", "mfcc.conf")))


def _resolve_model() -> str:
    """找一个可用的模型目录（**优先 ASCII 路径**），必要时从 vendor 拷一份。

    ⚠️ vosk 底层是 C++（Kaldi），Windows 上用窄字符打开文件：
       项目路径含「李思颉」时它会报 "does not contain model files"，
       而模型其实是完整的。所以必须放到纯英文目录。
    """
    global MODEL_DIR
    if MODEL_DIR:
        return MODEL_DIR

    for p in (os.environ.get("VOSK_MODEL_DIR", ""), ASCII_MODEL, VENDOR_MODEL):
        if p and _looks_like_model(p) and _is_ascii(p):
            MODEL_DIR = p
            return MODEL_DIR

    if _looks_like_model(VENDOR_MODEL):          # 只有中文路径 → 复制到 ASCII 目录
        try:
            import shutil

            print(f"[asr] 模型在中文路径下（Vosk 打不开），复制到 {ASCII_MODEL} …", flush=True)
            if os.path.isdir(ASCII_MODEL):
                shutil.rmtree(ASCII_MODEL, ignore_errors=True)
            shutil.copytree(VENDOR_MODEL, ASCII_MODEL)
            MODEL_DIR = ASCII_MODEL
            return MODEL_DIR
        except Exception as exc:                 # noqa: BLE001
            print(f"[asr] 复制失败：{exc}", flush=True)

    MODEL_DIR = VENDOR_MODEL
    return MODEL_DIR


def model_ready() -> bool:
    return _looks_like_model(_resolve_model())


def model_dir() -> str:
    return _resolve_model()


def preflight() -> tuple[bool, str]:
    """启动自检：逐项检查依赖，返回 (是否可用, 说明)。

    为什么要在启动时检查：
        以前是"按下录音键才发现缺模块"，然后静默降级成假识别——
        看起来能用、其实是假的（踩过）。把检查提前，一眼看出环境问题。
    """
    checks: list[str] = []

    pylibs = os.path.join(VENDOR, "_pylibs")
    if not os.path.isdir(pylibs):
        return False, f"依赖目录不存在：{pylibs}"

    try:
        import _cffi_backend  # noqa: F401
    except Exception as exc:                          # noqa: BLE001
        return False, (f"cffi 后端不可用（{type(exc).__name__}: {exc}）"
                       f"；请确认 {pylibs} 下有 _cffi_backend*.pyd")
    checks.append("cffi")

    try:
        import sounddevice as sd
        dev = sd.query_devices(kind="input")
    except Exception as exc:                          # noqa: BLE001
        return False, f"sounddevice 不可用（{type(exc).__name__}: {exc}）"
    checks.append(f"mic={dev['name'][:18]}")

    try:
        import vosk  # noqa: F401
    except Exception as exc:                          # noqa: BLE001
        return False, f"vosk 不可用（{type(exc).__name__}: {exc}）"
    checks.append("vosk")

    m = _resolve_model()
    if not _looks_like_model(m):
        return False, f"模型缺失：{m}"
    if not _is_ascii(m):
        return False, f"模型路径含非 ASCII 字符（vosk 打不开）：{m}"
    checks.append(os.path.basename(m))

    return True, "自检通过：" + " · ".join(checks)


def _ensure_model():
    """懒加载模型（第一次要 1~2 秒，之后复用）。"""
    global _model, _recognizer
    if _model is not None:
        return
    import vosk

    vosk.SetLogLevel(-1)                      # 关掉它啰嗦的日志
    _model = vosk.Model(_resolve_model())
    _recognizer = vosk.KaldiRecognizer(_model, RATE)


def list_devices() -> list[tuple[int, str]]:
    import sounddevice as sd

    out = []
    for i, d in enumerate(sd.query_devices()):
        if d["max_input_channels"] > 0:
            out.append((i, d["name"]))
    return out


def record(seconds: float, dev: int | None = None, rate: int = RATE):
    """录一段 int16 单声道 PCM，返回 (pcm_bytes, peak, rms)。"""
    import numpy as np
    import sounddevice as sd

    frames = sd.rec(int(rate * seconds), samplerate=rate, channels=1,
                    dtype="int16", device=dev)
    sd.wait()
    flat = frames.reshape(-1)
    peak = int(np.abs(flat).max()) if flat.size else 0
    rms = float(np.sqrt((flat.astype(np.float64) ** 2).mean())) if flat.size else 0.0
    return flat.tobytes(), peak, rms


class LiveRecorder:
    """流式录音器：开始后持续累积 PCM，停止时一次性吐出。

    为什么需要它：
        手表那边"按下开始录音 / 按下停止"是两个时刻，而用户说的话在**两者之间**。
        如果用"停止时才录 N 秒"，录到的是说完之后的环境音，必然识别不出来。
        所以必须按下就开始录，停止才转写。
    """

    def __init__(self, dev: int | None = None, rate: int = RATE):
        self.dev = dev
        self.rate = rate
        self._stream = None
        self._buf = bytearray()
        self.peak = 0

    def start(self) -> None:
        import sounddevice as sd

        self._buf = bytearray()
        self.peak = 0

        def _cb(indata, frames, time_info, status):   # noqa: ANN001, ARG001
            self._buf.extend(indata.tobytes())
            if indata.size:
                m = int(abs(indata).max())
                if m > self.peak:
                    self.peak = m

        self._stream = sd.InputStream(samplerate=self.rate, channels=1,
                                      dtype="int16", device=self.dev, callback=_cb)
        self._stream.start()

    def stop(self) -> bytes:
        if self._stream is not None:
            self._stream.stop()
            self._stream.close()
            self._stream = None
        return bytes(self._buf)

    @property
    def recording(self) -> bool:
        return self._stream is not None

    @property
    def duration_ms(self) -> int:
        return int(len(self._buf) / 2 / self.rate * 1000)

    def transcribe(self, pcm: bytes) -> str:
        """转写一段 PCM（转发到模块级函数，方便调用方只持有一个对象）。"""
        return transcribe(pcm)

    def transcribe_smart(self, pcm: bytes, phrases=None) -> tuple[str, str]:
        """先语法约束、失败再自由识别，返回 (文本, 走的哪条路)。"""
        return transcribe_smart(pcm, phrases)


def transcribe(pcm: bytes) -> str:
    """把 PCM 喂给 Vosk，返回识别出的中文文本（可能为空串）。"""
    _ensure_model()
    _recognizer.Reset()

    # 分块喂，模拟流式；Vosk 内部会自己处理
    chunk = RATE * 2 * 0.2                    # 每块 200ms
    for i in range(0, len(pcm), int(chunk)):
        _recognizer.AcceptWaveform(pcm[i:i + int(chunk)])

    res = json.loads(_recognizer.FinalResult())
    text = (res.get("text") or "").replace(" ", "")
    # Vosk 会在句末加标点；显示和后续关键词匹配都不需要，去掉
    text = text.strip("。，、！？；：,.!?;: ")
    return text


def record_and_transcribe(seconds: float = 4.0, dev: int | None = None):
    """录一段并识别，返回 (文本, peak, rms)。给"不带 start/stop 的老调用"兜底用。"""
    return listen(seconds, dev)


# ── 语法约束识别（命令词表）────────────────────────────────
# 为什么需要：
#   小模型对口音敏感，常把「资料」听成「质量」，只能靠模糊匹配补救。
#   把候选短语喂给 Vosk 做语法约束，输出被限制在词表内，错误率大幅下降。
#
# ⚠️ 中文模型必须**把短语按字用空格分开**（vocabulary 是单字粒度）：
#   整词不分字会整句变成 [unk]（实测过）。

_grammar_rec = None
_grammar_sig = ""


def build_grammar(phrases: list[str]) -> str:
    """把短语列表转成 Vosk 的 grammar JSON（每字空格分开 + [unk]）。"""
    items = [" ".join(p) if p != "[unk]" else p for p in phrases]
    if "[unk]" not in items:
        items.append("[unk]")
    return json.dumps(items, ensure_ascii=False)


def _grammar_recognizer(phrases: list[str]):
    """按短语集合缓存一个语法识别器（重建有开销，别每次都建）。"""
    global _grammar_rec, _grammar_sig

    import vosk

    sig = "|".join(phrases)
    if _grammar_rec is not None and sig == _grammar_sig:
        return _grammar_rec

    _ensure_model()                       # 确保 _model 已加载
    vosk.SetLogLevel(-1)
    _grammar_rec = vosk.KaldiRecognizer(_model, RATE, build_grammar(phrases))
    _grammar_sig = sig
    return _grammar_rec


def transcribe_phrases(pcm: bytes, phrases: list[str]) -> str:
    """在给定短语集合内识别。返回空串表示"没匹配到词表里的任何短语"。"""
    if not phrases:
        return ""

    rec = _grammar_recognizer(phrases)
    rec.Reset()

    chunk = RATE * 2 * 0.2
    for i in range(0, len(pcm), int(chunk)):
        rec.AcceptWaveform(pcm[i:i + int(chunk)])

    text = (json.loads(rec.FinalResult()).get("text") or "").replace(" ", "")
    text = text.strip("。，、！？；：,.!?;: ")
    if text in ("[unk]", "unk"):
        return ""
    return text


def transcribe_smart(pcm: bytes, phrases: list[str] | None = None) -> tuple[str, str]:
    """先语法约束识别，识别不出再退回自由识别。返回 (文本, 用的是哪条路)。

    两段式的价值：
      命中词表 → 结果干净（不会出现「质量」这类近音错字）；
      不在词表 → 仍能得到自由文本（"帮我打开一下资料"这类长句），
                  交给网关的模糊匹配去理解。
    """
    if phrases:
        text = transcribe_phrases(pcm, phrases)
        if text:
            return text, "grammar"
    return transcribe(pcm), "free"


def listen(seconds: float = 4.0, dev: int | None = None) -> tuple[str, int, float]:
    """录一段并识别，返回 (文本, peak, rms)。"""
    pcm, peak, rms = record(seconds, dev)
    return transcribe(pcm), peak, rms


def main() -> int:
    args = [a for a in sys.argv[1:]]
    dev = None
    seconds = 4.0

    if "--dev" in args:
        i = args.index("--dev")
        dev = int(args[i + 1])
        del args[i:i + 2]
    if args:
        seconds = float(args[0])

    if not model_ready():
        print(f"模型不存在：{MODEL_DIR}")
        print("请先下载 vosk-model-small-cn-0.22 解压到 vendor/models/")
        return 1

    print(f"录音 {seconds} 秒 …（现在开始说）")
    text, peak, rms = listen(seconds, dev)
    print(f"peak={peak} rms={rms:.1f}")
    print(f"识别结果：{text!r}" if text else "识别结果：（空——可能太轻或没说话）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
