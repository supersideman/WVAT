"""OV+ 协议：帧的构造与解析（对应《AI链路设计》§3.3）

帧格式（ASCII 文本行 + \\r\\n 结尾，载荷 UTF-8）：
    终端→代理   OV+AI=<REQ_ID>,<CODE>[,<ARG>]
    代理→终端   OV+AR=<REQ_ID>,<STATUS>,<TEXT>
    代理→终端   OV+EV=<EVENT>,<TEXT>
    双向        OV+PING / OV+PONG
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import List, Optional

TERMINATOR = "\r\n"
MAX_TEXT_BYTES = 60  # 屏上显示得下的量级（约 20 个汉字）

KIND_REQ = "AI"    # 终端 → 代理：意图请求
KIND_ACK = "AR"    # 代理 → 终端：应答
KIND_EVT = "EV"    # 代理 → 终端：异步事件
KIND_PING = "PING"
KIND_PONG = "PONG"

STATUS_OK = "OK"
STATUS_ERR = "ERR"
STATUS_BUSY = "BUSY"

_FRAME_RE = re.compile(r"^OV\+([A-Z]+)(?:=(.*))?$", re.S)


def clean_text(text: str) -> str:
    """去掉会破坏行结构的字符，并按字节数截断（不切坏多字节字符）。"""
    text = (text or "").replace("\r", " ").replace("\n", " ").strip()
    if len(text.encode("utf-8")) <= MAX_TEXT_BYTES:
        return text
    out = text
    while out and len(out.encode("utf-8")) > MAX_TEXT_BYTES:
        out = out[:-1]
    return out


def ai_frame(req_id: int, code: str, arg: str = "") -> str:
    """终端 → 代理：发起意图。"""
    body = f"{int(req_id) & 0xFF},{code}"
    if arg:
        body += f",{arg}"
    return f"OV+{KIND_REQ}={body}{TERMINATOR}"


def ar_frame(req_id: int, status: str, text: str = "") -> str:
    """代理 → 终端：应答。"""
    return f"OV+{KIND_ACK}={int(req_id) & 0xFF},{status},{clean_text(text)}{TERMINATOR}"


def ev_frame(event: str, text: str = "") -> str:
    """代理 → 终端：异步事件。"""
    return f"OV+{KIND_EVT}={event},{clean_text(text)}{TERMINATOR}"


def time_event(now=None) -> str:
    """代理 → 终端：授时（终端收到后写入内部 RTC）。

    格式：OV+EV=TIME,<HH:MM:SS>,<YYYY-MM-DD>
    """
    from datetime import datetime

    now = now or datetime.now()
    return ev_frame("TIME", f"{now:%H:%M:%S},{now:%Y-%m-%d}")


def ping_frame() -> str:
    return f"OV+{KIND_PING}{TERMINATOR}"


def pong_frame() -> str:
    return f"OV+{KIND_PONG}{TERMINATOR}"


@dataclass
class Frame:
    kind: str              # AI / AR / EV / PING / PONG
    fields: List[str]

    @property
    def req_id(self) -> int:
        if self.kind in (KIND_REQ, KIND_ACK) and self.fields:
            try:
                return int(self.fields[0])
            except ValueError:
                return -1
        return -1

    @property
    def code(self) -> str:
        return self.fields[1] if len(self.fields) > 1 else ""

    @property
    def arg(self) -> str:
        return self.fields[2] if len(self.fields) > 2 else ""

    def __repr__(self) -> str:
        return f"<{self.kind} {self.fields}>"


def parse(line: str) -> Optional[Frame]:
    """解析一行；不认识就返回 None（前向兼容：忽略未知帧）。"""
    line = (line or "").strip("\r\n \t")
    if not line:
        return None
    m = _FRAME_RE.match(line)
    if not m:
        return None
    kind, body = m.group(1), m.group(2)
    fields = body.split(",") if body is not None else []

    if kind == KIND_REQ:
        if len(fields) < 2:
            return None
        return Frame(kind, [fields[0], fields[1], ",".join(fields[2:])])
    if kind == KIND_ACK:
        if len(fields) < 2:
            return None
        return Frame(kind, [fields[0], fields[1], ",".join(fields[2:])])
    if kind == KIND_EVT:
        if not fields:
            return None
        return Frame(kind, [fields[0], ",".join(fields[1:])])
    if kind in (KIND_PING, KIND_PONG):
        return Frame(kind, [])
    return None
