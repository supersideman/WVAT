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
VENDOR = os.path.join(HERE, "..", "vendor")
sys.path.insert(0, os.path.join(VENDOR, "_pylibs"))

RATE = 16000
MODEL_DIR = os.path.join(VENDOR, "models", "vosk-model-small-cn-0.22")

_model = None
_recognizer = None


def model_ready() -> bool:
    return os.path.isdir(MODEL_DIR)


def _ensure_model():
    """懒加载模型（第一次要 1~2 秒，之后复用）。"""
    global _model, _recognizer
    if _model is not None:
        return
    import vosk

    vosk.SetLogLevel(-1)                      # 关掉它啰嗦的日志
    _model = vosk.Model(MODEL_DIR)
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
    return text


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
