"""真机录音 + 识别试跑（带音量包络与 WAV 留证）。

改进点：
  · 按设备**原生采样率**录，再用 numpy 重采样到 16k
    （避免 PortAudio 的自动重采样在某些设备上失真）
  · 保存 WAV 到本目录，可以直接播放来听"电脑到底听到了什么"
  · 打印分段 RMS 包络，一眼看出这段时间有没有人声

用法：python asr_try.py [等待秒数] [录音秒数] [设备编号]
"""
from __future__ import annotations

import os
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "..", "vendor", "_pylibs"))

import numpy as np  # noqa: E402

import asr  # noqa: E402

WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
REC = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
DEV = int(sys.argv[3]) if len(sys.argv) > 3 else None


def resample_to_16k(x: np.ndarray, src_rate: int) -> np.ndarray:
    """线性插值重采样（语音够用，不引入额外依赖）。"""
    if src_rate == 16000 or x.size == 0:
        return x
    n_out = int(round(x.size * 16000 / src_rate))
    idx = np.linspace(0, x.size - 1, n_out)
    return np.interp(idx, np.arange(x.size), x.astype(np.float64)).astype(np.int16)


def main() -> int:
    import sounddevice as sd

    if not asr.model_ready():
        print("模型不可用：", asr.model_dir())
        return 1

    dev = DEV
    if dev is None:
        dev = sd.default.device[0]
    info = sd.query_devices(dev)
    rate = int(info["default_samplerate"])
    print(f"设备[{dev}] {info['name'].encode('ascii', 'replace').decode()}")
    print(f"原生采样率 {rate} Hz，将重采样到 16000 Hz")

    print(f"加载模型 …", flush=True)
    asr.transcribe(b"\x00" * 3200)

    print(f"{WAIT:.0f} 秒后开始录音 {REC:.0f} 秒 —— 请准备好，**大声**说话", flush=True)
    for i in range(int(WAIT), 0, -1):
        print(f"  {i} ×", flush=True)
        time.sleep(1)

    print(">>> 现在开始说（大声、靠近麦克风）<<<", flush=True)
    frames = sd.rec(int(rate * REC), samplerate=rate, channels=1, dtype="int16", device=dev)
    sd.wait()
    x = frames.reshape(-1)
    peak = int(np.abs(x).max()) if x.size else 0
    print(f"原始：peak={peak}  rms={float(np.sqrt((x.astype(np.float64)**2).mean())):.1f}", flush=True)

    # 分段包络：每 0.25 秒一段，看有没有人声
    seg = max(1, int(rate * 0.25))
    env = []
    for i in range(0, x.size, seg):
        part = x[i:i + seg]
        if part.size:
            env.append(int(np.sqrt((part.astype(np.float64) ** 2).mean())))
    print("包络(每0.25s RMS): " + " ".join(str(v) for v in env), flush=True)

    # 存 WAV（原始采样率），可以自己听
    wav_path = os.path.join(HERE, "_mic.wav")
    with wave.open(wav_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(x.tobytes())
    print(f"已保存 {wav_path}（可双击播放听效果）", flush=True)

    pcm16 = resample_to_16k(x, rate).tobytes()
    text = asr.transcribe(pcm16)
    print("=" * 40, flush=True)
    print(f"识别结果：【{text}】" if text else "识别结果：（空）", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
