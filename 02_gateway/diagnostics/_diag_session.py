"""诊断：打印当前进程的窗口站 / 桌面 / 会话号。

SendInput 只在"交互式窗口站（WinSta0）的默认桌面（Default）"上生效。
如果在别的窗口站里，SendInput 会返回成功但输入到不了用户桌面——
表现为"日志说执行成功，但系统毫无反应"。
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes

u = ctypes.windll.user32
k = ctypes.windll.kernel32


def obj_name(handle, index: int = 2) -> str:
    buf = ctypes.create_string_buffer(256)
    need = wintypes.DWORD()
    if u.GetUserObjectInformationA(handle, index, buf, 256, ctypes.byref(need)):
        return buf.value.decode(errors="replace")
    return "<读取失败>"


def main() -> None:
    ws = u.GetProcessWindowStation()
    desk = u.GetThreadDesktop(k.GetCurrentThreadId())

    pid = k.GetCurrentProcessId()
    sid = wintypes.DWORD()
    k.ProcessIdToSessionId(pid, ctypes.byref(sid))

    print(f"PID       = {pid}")
    print(f"SessionId = {sid.value}")
    print(f"窗口站    = {obj_name(ws)}")
    print(f"桌面      = {obj_name(desk)}")

    interactive = obj_name(ws).lower().startswith("winsta0")
    print(f"能向用户桌面注入输入 = {'是' if interactive else '否（这就是按键没反应的原因）'}")


if __name__ == "__main__":
    main()
