"""串口监视器：只显示麦克风自检等调试输出，给"焊完验证接线"用。

为什么单独做一个：
    网关启动后会一直收发协议帧，屏幕上 [mic] 自检那几行很容易被刷走。
    这个脚本只连串口、不跑协议，专心把设备打印的内容显示出来。

用法：
    python serial_monitor.py            # 自动找 CH340
    python serial_monitor.py COM7       # 指定串口
    python serial_monitor.py COM7 460800
"""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "vendor", "_pylibs"))

import serial  # noqa: E402
from serial.tools import list_ports  # noqa: E402


def find_ch340() -> str | None:
    for p in list_ports.comports():
        if p.vid == 0x1A86:                      # 沁恒 CH340
            return p.device
    return None


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else find_ch340()
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    if not port:
        print("没找到 CH340 串口。请插好板子，或手动指定：python serial_monitor.py COM7")
        for p in list_ports.comports():
            print(f"  可用：{p.device}  {p.description}")
        return 1

    print(f"打开 {port} @ {baud} …（按 Ctrl+C 退出）\n")
    try:
        with serial.Serial(port, baud, timeout=0.2) as sp:
            sp.reset_input_buffer()
            while True:
                line = sp.readline()
                if not line:
                    continue
                try:
                    text = line.decode("utf-8", errors="replace").rstrip()
                except Exception:
                    continue
                if text:
                    print(text, flush=True)
    except KeyboardInterrupt:
        print("\n已退出。")
    except Exception as exc:                     # noqa: BLE001
        print(f"串口打不开：{exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
