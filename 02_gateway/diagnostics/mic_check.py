"""自检：能不能从默认麦克风录到声音。

用途：确认 sounddevice 可用、能打开输入设备、采到非静音数据。
（和 SendInput 一样，受限进程里录音也可能被拦，所以先单独验证。）
"""
from __future__ import annotations

import sys

try:
    import numpy as np
    import sounddevice as sd
except Exception as exc:            # noqa: BLE001
    print("import failed:", exc)
    raise SystemExit(1)

RATE = 16000
SECONDS = 1.5


def main() -> int:
    devs = sd.query_devices()
    ins = [(i, d["name"]) for i, d in enumerate(devs) if d["max_input_channels"] > 0]
    print(f"input devices: {len(ins)}")
    for i, name in ins[:8]:
        print(f"  [{i}] {name.encode('ascii', 'replace').decode()}")

    try:
        default_in = sd.query_devices(kind="input")
        print("default input:", default_in["name"].encode("ascii", "replace").decode())
    except Exception as exc:        # noqa: BLE001
        print("default input query failed:", exc)

    print(f"recording {SECONDS}s ...")
    try:
        rec = sd.rec(int(RATE * SECONDS), samplerate=RATE, channels=1, dtype="int16")
        sd.wait()
    except Exception as exc:        # noqa: BLE001
        print("RECORD FAILED:", exc)
        return 2

    peak = int(np.abs(rec).max())
    rms = float(np.sqrt((rec.astype(np.float64) ** 2).mean()))
    print(f"peak={peak}  rms={rms:.1f}  (peak>200 说明有声音；全程 0 说明没采到)")
    return 0 if peak > 200 else 3


if __name__ == "__main__":
    raise SystemExit(main())
