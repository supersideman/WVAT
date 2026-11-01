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


class _INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", _KEYBDINPUT)]

    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", _U)]


def _press_vk(vk: int) -> None:
    """发一个按键（按下 + 抬起）。仅 Windows 有效。"""
    if sys.platform != "win32":
        raise RuntimeError("动作执行目前只支持 Windows")
    extra = ctypes.c_ulong(0)
    inputs = (_INPUT * 2)()
    for i, flags in enumerate((0, KEYEVENTF_KEYUP)):
        inputs[i].type = 1  # INPUT_KEYBOARD
        inputs[i].ki = _KEYBDINPUT(vk, 0, flags, 0, ctypes.pointer(extra))
    ctypes.windll.user32.SendInput(2, inputs, ctypes.sizeof(_INPUT))


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
_reg("A03", "我困了", "A", "已锁屏并静音",
     lambda arg: (_press_vk(VK_VOLUME_MUTE), _lock_workstation(), _monitor_off()))
_reg("A04", "音量+", "A", "音量已调高",
     lambda arg: _press_vk(VK_VOLUME_UP))
_reg("A05", "音量-", "A", "音量已调低",
     lambda arg: _press_vk(VK_VOLUME_DOWN))
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
            action.run(arg)
        except Exception as exc:               # 单条动作失败不能拖垮网关
            msg = f"{action.name}失败：{exc}"
            self.log.append(msg)
            print("  [!] " + msg, flush=True)
            return False, action.name + "失败"
        self.log.append(f"{action.code} {action.name} ok")
        print(f"  [执行] {action.code} {action.name} ok", flush=True)
        return True, action.reply


def describe_table() -> str:
    lines = ["code  级别  名称           回执"]
    for a in WHITELIST.values():
        lines.append(f"{a.code}   {a.level}    {a.name:<13} {a.reply}")
    return "\n".join(lines)
