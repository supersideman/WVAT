"""纯串口日志读取器：不解析协议，只把固件 printf 的东西原样打出来。

用途：排查固件内部状态（LVGL 对象尺寸/位置、事件时序）。
用法：python serial_log.py [COM口] [秒数]

默认 COM13、读 20 秒。按 Ctrl+C 提前结束。
"""
from __future__ import annotations

import sys
import time

try:
    import serial  # pyserial
except ImportError:
    print("需要 pyserial：把 wheel 解压到 tools/_pylibs 并设 PYTHONPATH")
    raise SystemExit(1)

port = sys.argv[1] if len(sys.argv) > 1 else "COM13"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

print(f"[log] 打开 {port} @115200，读 {seconds:.0f} 秒…（Ctrl+C 结束）", flush=True)
ser = serial.Serial(port, 115200, timeout=0.1)
buf = b""
t0 = time.time()
try:
    while time.time() - t0 < seconds:
        chunk = ser.read(256)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip("\r")
            if text.strip():
                print(f"{time.time() - t0:6.2f}s | {text}", flush=True)
finally:
    ser.close()
    print("[log] 结束", flush=True)
