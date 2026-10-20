"""PC 端网关（对应《AI链路设计》§3）

职责：收帧 → 去重 → 双通道路由 → 执行 → 应答。
不负责：界面渲染（除了可选的镜像窗）。

用法（P0 无硬件，用本地 TCP 模拟）：
    python gateway.py                 # 默认：TCP 8765 + 演练模式（不动系统）
    python gateway.py --enable-actions   # 真执行（会锁屏/切歌/调音量，注意）
    python gateway.py --transport serial --port COM14   # 真实串口/蓝牙 SPP

路由规则：
    A01–A07  → 快通道：本地白名单，毫秒级，断网可用
    T        → 慢通道：LLM 解析（未配置 key 时自动降级为本地关键词兜底）
"""

from __future__ import annotations

import argparse
import os
import queue
import sys
import threading
import time

import protocol as P
from intents import WHITELIST, Executor, describe_table
from transport import (DEFAULT_HOST, DEFAULT_PORT, SerialChannel,
                       SocketServerChannel)

DEDUP_WINDOW = 10          # 记住最近 N 个 REQ_ID，重传不重复执行
HEARTBEAT_SEC = 20
MAX_MISSED_PONG = 3

# 慢通道兜底关键词（没有 LLM 也能工作）
FALLBACK_KEYWORDS = [
    (("歌", "音乐", "播放", "放首"), "A01"),
    (("下一首", "切歌", "换一首"), "A02"),
    (("困", "睡", "锁屏", "休息"), "A03"),
    (("大声", "音量大", "调高"), "A04"),
    (("小声", "音量小", "调低", "静音"), "A05"),
    (("几点", "时间", "状态", "在线"), "A07"),
]


class Gateway:
    def __init__(self, channel, executor: Executor, use_llm: bool = False):
        self.ch = channel
        self.ex = executor
        self.use_llm = use_llm
        self.dedup: "queue.OrderedDict[int, str]" = __import__("collections").OrderedDict()
        self.missed_pong = 0
        self.last_ping = 0.0
        self.running = True
        self.counters = {"fast": 0, "slow": 0, "err": 0, "dedup": 0}

    # ── 主循环 ──────────────────────────────────────────────
    def run(self) -> None:
        print("\n网关已启动。等待终端发来 OV+AI=... （Ctrl+C 退出）\n", flush=True)
        while self.running:
            for line in self.ch.recv_lines():
                self.on_line(line)

            now = time.time()
            if getattr(self.ch, "connected", False) and now - self.last_ping > HEARTBEAT_SEC:
                self.ch.send_line(P.ping_frame())
                self.last_ping = now
                self.missed_pong += 1
                if self.missed_pong > MAX_MISSED_PONG:
                    print("[!] 心跳超时，判为掉线", flush=True)
                    self.missed_pong = 0
            time.sleep(0.02)

    # ── 单帧处理 ────────────────────────────────────────────
    def on_line(self, line: str) -> None:
        frame = P.parse(line)
        if frame is None:
            print(f"[忽略] 无法解析：{line!r}", flush=True)
            return

        if frame.kind == P.KIND_PONG:
            self.missed_pong = 0
            return
        if frame.kind == P.KIND_PING:
            self.ch.send_line(P.pong_frame())
            return
        if frame.kind != P.KIND_REQ:
            return

        t0 = time.time()
        req_id, code, arg = frame.req_id, frame.code, frame.arg
        print(f"← 收到 OV+AI={req_id},{code}{',' + arg if arg else ''}", flush=True)

        cached = self.dedup.get(req_id)
        if cached is not None:
            self.counters["dedup"] += 1
            print(f"  [去重] REQ_ID={req_id} 已处理过，直接回缓存", flush=True)
            self.ch.send_line(cached)
            return

        text, status = self.dispatch(code, arg)
        reply = P.ar_frame(req_id, status, text)
        self.ch.send_line(reply)
        self.dedup[req_id] = reply
        while len(self.dedup) > DEDUP_WINDOW:
            self.dedup.popitem(last=False)
        print(f"→ 回 {reply.strip()}   ({(time.time() - t0) * 1000:.0f} ms)\n", flush=True)

    # ── 双通道路由 ──────────────────────────────────────────
    def dispatch(self, code: str, arg: str) -> tuple[str, str]:
        """返回 (回给手表的话, 状态) —— 状态用于区分 OK/ERR。"""
        # 快通道
        if code in WHITELIST:
            self.counters["fast"] += 1
            if code == "A07":
                return f"{time.strftime('%H:%M:%S')} 在线", P.STATUS_OK
            ok, text = self.ex.execute(code, arg)
            return (text, P.STATUS_OK) if ok else (f"执行失败：{text}", P.STATUS_ERR)

        # A08 语音输入（P2 才接真 ASR，这里留钩子）
        if code == "A08":
            self.counters["slow"] += 1
            return "语音识别待接入", P.STATUS_OK

        # 慢通道：自由文本
        if code == "T":
            self.counters["slow"] += 1
            return self.slow_channel(arg), P.STATUS_OK

        self.counters["err"] += 1
        return "未知指令", P.STATUS_ERR

    def slow_channel(self, text: str) -> str:
        if self.use_llm:
            try:
                return self._llm_intent(text)
            except Exception as exc:
                print(f"  [!] LLM 失败，降级本地规则：{exc}", flush=True)
        return self._fallback(text)

    def _fallback(self, text: str) -> str:
        for words, code in FALLBACK_KEYWORDS:
            if any(w in text for w in words):
                action = WHITELIST[code]
                ok, reply = self.ex.execute(code, "")
                return reply if ok else f"执行失败：{reply}"
        return "没听懂，请再说一次"

    def _llm_intent(self, text: str) -> str:
        """调用 DeepSeek 把自由文本映射成动作码（返回值仍是白名单 code）。"""
        import json
        import urllib.request

        key = os.environ.get("DEEPSEEK_API_KEY", "")
        if not key:
            raise RuntimeError("未设置 DEEPSEEK_API_KEY")
        codes = ", ".join(WHITELIST.keys())
        prompt = (
            "你是意图分类器。只输出 JSON：{\"code\":\"...\",\"say\":\"...\"}。\n"
            f"code 必须在这些里面：{codes}，或者 UNKNOWN。\n"
            "say 是给用户看的一句话，不超过 15 个汉字。\n"
            f"用户说：{text}"
        )
        body = json.dumps({
            "model": "deepseek-chat",
            "messages": [{"role": "user", "content": prompt}],
            "temperature": 0,
        }).encode("utf-8")
        req = urllib.request.Request(
            "https://api.deepseek.com/chat/completions",
            data=body,
            headers={"Content-Type": "application/json",
                     "Authorization": f"Bearer {key}"},
        )
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        content = data["choices"][0]["message"]["content"].strip()
        content = content.removeprefix("```json").removeprefix("```").removesuffix("```").strip()
        obj = json.loads(content)
        code = obj.get("code", "UNKNOWN")
        if code in WHITELIST:
            ok, reply = self.ex.execute(code, "")
            return reply if ok else f"执行失败：{reply}"
        return obj.get("say", "这个我还不会")

    def summary(self) -> str:
        c = self.counters
        return (f"快通道 {c['fast']} · 慢通道 {c['slow']} · "
                f"去重命中 {c['dedup']} · 错误 {c['err']}")


def main() -> int:
    ap = argparse.ArgumentParser(description="个人 AI 控制终端 · PC 端网关")
    ap.add_argument("--transport", choices=["socket", "serial"], default="socket")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help="socket 模式端口")
    ap.add_argument("--serial-port", default="", help="serial 模式端口名，如 COM14")
    ap.add_argument("--baudrate", type=int, default=115200)
    ap.add_argument("--enable-actions", action="store_true",
                    help="真正执行动作（默认演练，不动系统）")
    ap.add_argument("--llm", action="store_true", help="启用 DeepSeek 慢通道")
    ap.add_argument("--list", action="store_true", help="打印白名单并退出")
    args = ap.parse_args()

    if args.list:
        print(describe_table())
        return 0

    if args.transport == "serial":
        if not args.serial_port:
            print("serial 模式需要 --serial-port COMxx", file=sys.stderr)
            return 2
        ch = SerialChannel(args.serial_port, args.baudrate)
        print(f"[transport] 串口 {args.serial_port} @ {args.baudrate}", flush=True)
    else:
        ch = SocketServerChannel(args.host, args.port)

    gw = Gateway(ch, Executor(dry_run=not args.enable_actions), use_llm=args.llm)
    print(f"[gateway] 动作执行：{'真执行' if args.enable_actions else '演练模式（不改系统）'}",
          flush=True)
    print(f"[gateway] 慢通道：{'DeepSeek' if args.llm else '本地关键词兜底'}", flush=True)
    try:
        gw.run()
    except KeyboardInterrupt:
        print("\n[gateway] 退出。" + gw.summary(), flush=True)
    finally:
        ch.close()
        if hasattr(ch, "shutdown"):
            ch.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
