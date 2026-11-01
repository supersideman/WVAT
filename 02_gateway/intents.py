"""动作白名单与执行器（对应《AI链路设计》§3.4）

设计要点
- **白名单是唯一的执行入口**：LLM 只能输出白名单里的 code，不能生成命令
- 默认 `dry_run=True`，只打印不执行；加 `--enable-actions` 才真正动手
- 纯 ctypes 实现 Windows 媒体键/锁屏/关屏，**不需要装任何第三方库**
"""

from __future__ import annotations

import ctypes
import sys
from dataclasses import dataclass
from typing import Callable, Dict, Optional

import audio_win          # 音量/静音走 CoreAudio，确定性且可回读

# ── Windows 虚拟键码 ─────────────────────────────────────────────
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_PLAY_PAUSE = 0xB3
VK_VOLUME_MUTE = 0xAD
VK_VOLUME_DOWN = 0xAE
VK_VOLUME_UP = 0xAF

KEYEVENTF_KEYUP = 0x0002


class _KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", ctypes.c_ushort),
        ("wScan", ctypes.c_ushort),
        ("dwFlags", ctypes.c_ulong),
        ("time", ctypes.c_ulong),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]


class _MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", ctypes.c_long),
        ("dy", ctypes.c_long),
        ("mouseData", ctypes.c_ulong),
        ("dwFlags", ctypes.c_ulong),
        ("time", ctypes.c_ulong),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]


class _HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", ctypes.c_ulong),
        ("wParamL", ctypes.c_ushort),
        ("wParamH", ctypes.c_ushort),
    ]


class _INPUT(ctypes.Structure):
    """⚠️ union 里 **三个成员一个都不能少**。

    Windows 要求 SendInput 的 cbSize 精确等于 sizeof(INPUT)（x64 = 40）。
    之前这里只写了 ki，sizeof 算成 32，于是 SendInput 每次都返回 0
    ——不报错、不抛异常，按键注入**全部静默失败**（播放/暂停、下一首、
    音量、静音都无效），排查了很久才发现。
    """

    class _U(ctypes.Union):
        _fields_ = [
            ("mi", _MOUSEINPUT),
            ("ki", _KEYBDINPUT),
            ("hi", _HARDWAREINPUT),
        ]

    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", _U)]


def _press_vk(vk: int) -> None:
    """发一个按键（按下 + 抬起）。仅 Windows 有效。

    ⚠️ 一定要检查 SendInput 的返回值：它失败时**不会抛异常**，
       只返回"实际插入的事件数"。以前这里丢弃了返回值，
       结构体写错导致每次都返回 0 却无人知晓，动作"显示成功但实际没发生"。
    """
    if sys.platform != "win32":
        raise RuntimeError("动作执行目前只支持 Windows")

    size = ctypes.sizeof(_INPUT)
    if size != 40:                       # x64 下必须 40，否则必定失败
        raise RuntimeError(f"INPUT 结构体大小异常（{size}，应为 40），按键注入必失败")

    extra = ctypes.c_ulong(0)
    inputs = (_INPUT * 2)()
    for i, flags in enumerate((0, KEYEVENTF_KEYUP)):
        inputs[i].type = 1  # INPUT_KEYBOARD
        inputs[i].ki = _KEYBDINPUT(vk, 0, flags, 0, ctypes.pointer(extra))

    n = ctypes.windll.user32.SendInput(2, inputs, size)
    if n != 2:
        raise RuntimeError(f"SendInput 只插入了 {n}/2 个事件（VK=0x{vk:02X}）")


def _lock_workstation() -> None:
    if not ctypes.windll.user32.LockWorkStation():
        raise RuntimeError("LockWorkStation 调用失败")


def _monitor_off() -> None:
    HWND_BROADCAST = 0xFFFF
    WM_SYSCOMMAND = 0x0112
    SC_MONITORPOWER = 0xF170
    ctypes.windll.user32.SendMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2)


def _open_path(path: str) -> None:
    """打开路径/网址。

    手表上只有图标、没有输入框，所以 ARG 通常是空的 → 给个默认落点（桌面），
    免得每次点「资料」都回一个"执行失败"。
    """
    import os
    if not path:
        path = os.path.join(os.path.expanduser("~"), "Desktop")
    if path.startswith(("http://", "https://")):
        os.startfile(path)  # type: ignore[attr-defined]
    elif os.path.exists(path):
        os.startfile(path)  # type: ignore[attr-defined]
    else:
        raise FileNotFoundError(path)


# ── 白名单 ───────────────────────────────────────────────────────
@dataclass
class Action:
    code: str
    name: str
    level: str                    # A=安全动作（直接执行） B=破坏性（需二次确认）
    reply: str                    # 成功时回给手表的一句话
    run: Callable[[str], None]


WHITELIST: Dict[str, Action] = {}


def _reg(code: str, name: str, level: str, reply: str, fn: Callable[[str], None]) -> None:
    WHITELIST[code] = Action(code, name, level, reply, fn)


_reg("A01", "放歌/暂停", "A", "已切换播放",
     lambda arg: _press_vk(VK_MEDIA_PLAY_PAUSE))
_reg("A02", "下一首", "A", "已切到下一首",
     lambda arg: _press_vk(VK_MEDIA_NEXT_TRACK))

# ── 音量/静音改走 CoreAudio（确定性，且能把音量读回来做回执）──
# 为什么不用模拟音量键：同样的 SendInput 在前台进程里有效，在长时间运行的
# 网关进程里却"返回成功但毫无效果"，依赖输入桌面太不可靠，且无法验证。
def _vol_up(arg: str) -> str:
    pct = round(audio_win.step_volume(+0.05) * 100)
    return f"音量已调高 {pct}%"


def _vol_down(arg: str) -> str:
    pct = round(audio_win.step_volume(-0.05) * 100)
    return f"音量已调低 {pct}%"


def _sleepy(arg: str) -> str:
    audio_win.set_mute(True)
    _lock_workstation()
    _monitor_off()
    return "已静音并锁屏"


_reg("A03", "我困了", "A", "已静音并锁屏", _sleepy)
_reg("A04", "音量+", "A", "音量已调高", _vol_up)
_reg("A05", "音量-", "A", "音量已调低", _vol_down)
_reg("A06", "打开资料", "A", "已打开",
     lambda arg: _open_path(arg))
_reg("A07", "查状态", "A", "在线",
     lambda arg: None)          # 实际回执由 gateway 动态生成


class Executor:
    """白名单执行器。dry_run=True 时只回显不动作。"""

    def __init__(self, dry_run: bool = True):
        self.dry_run = dry_run
        self.log: list[str] = []

    def execute(self, code: str, arg: str = "") -> tuple[bool, str]:
        """返回 (是否成功, 回给手表的话)。"""
        action = WHITELIST.get(code)
        if action is None:
            return False, "未知指令"

        if self.dry_run:
            fn = "print"
            msg = f"[演练] {action.code} {action.name}{(' ' + arg) if arg else ''} → {action.reply}"
            self.log.append(msg)
            print("  " + msg, flush=True)
            return True, action.reply

        try:
            dynamic = action.run(arg)
        except Exception as exc:               # 单条动作失败不能拖垮网关
            msg = f"{action.name}失败：{exc}"
            self.log.append(msg)
            print("  [!] " + msg, flush=True)
            return False, action.name + "失败"
        self.log.append(f"{action.code} {action.name} ok")
        print(f"  [执行] {action.code} {action.name} ok", flush=True)

        # 动作可以返回一句话（例如"音量 63%"），用动态结果替代固定的回执文案
        if isinstance(dynamic, str) and dynamic:
            return True, dynamic
        return True, action.reply


def describe_table() -> str:
    lines = ["code  级别  名称           回执"]
    for a in WHITELIST.values():
        lines.append(f"{a.code}   {a.level}    {a.name:<13} {a.reply}")
    return "\n".join(lines)
