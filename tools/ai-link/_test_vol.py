"""按键注入方式对比自检（调用真实实现，避免自建结构体出错）。

背景：手表点 A04/A05，网关日志显示"已执行 ok"，但系统音量没动；
      而手工测试却把音量推到了 100%。需要确认哪条路子真的有效。

用法：
  python _test_vol.py up   sendinput
  python _test_vol.py down sendinput
  python _test_vol.py up   appcommand
  python _test_vol.py down appcommand

⚠️ 教训：上一版自己重新定义 INPUT 结构体时漏了鼠标/硬件字段，
   sizeof(INPUT) 变成 32（应为 40），SendInput 返回 0——测试工具本身坏了，
   差点让我得出"SendInput 不可用"的错误结论。所以这里直接用 intents 里的实现。
"""
from __future__ import annotations

import ctypes
import sys
import time

import intents

u = ctypes.windll.user32

HWND_BROADCAST = 0xFFFF
WM_APPCOMMAND = 0x0319
APPCOMMAND_VOLUME_DOWN = 9
APPCOMMAND_VOLUME_UP = 10


def via_sendinput(vk: int, times: int) -> None:
    for _ in range(times):
        intents._press_vk(vk)          # ← 真实实现，结构体完整
        print("  已调用 intents._press_vk()", flush=True)
        time.sleep(0.12)


def via_appcommand(up: bool, times: int) -> None:
    cmd = APPCOMMAND_VOLUME_UP if up else APPCOMMAND_VOLUME_DOWN
    for _ in range(times):
        u.SendMessageW(HWND_BROADCAST, WM_APPCOMMAND, 0, cmd << 16)
        print("  WM_APPCOMMAND 已发送", flush=True)
        time.sleep(0.12)


def main() -> int:
    direction = (sys.argv[1] if len(sys.argv) > 1 else "up").lower()
    method = (sys.argv[2] if len(sys.argv) > 2 else "sendinput").lower()
    times = 8
    up = direction == "up"
    vk = intents.VK_VOLUME_UP if up else intents.VK_VOLUME_DOWN

    print(f"方向={direction}  方式={method}  次数={times}", flush=True)
    if method == "sendinput":
        via_sendinput(vk, times)
    elif method == "appcommand":
        via_appcommand(up, times)
    else:
        print("方式只能是 sendinput 或 appcommand")
        return 2
    print("完成", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
