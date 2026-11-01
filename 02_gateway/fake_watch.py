"""模拟手表终端（P0 阶段的核心工具）

没有硬件时用它把整条链路跑通，并顺便采时延数据。

用法：
    python fake_watch.py                 # 交互：按提示输入指令
    python fake_watch.py --demo          # 自动演示（适合录屏/答辩脚本）
    python fake_watch.py --demo --yes    # 不需要回车确认，直接跑完

交互菜单对应白名单：
    a1 放歌/暂停   a2 下一首   a3 我困了   a4 音量+   a5 音量-
    a6 打开资料    a7 查状态   a8 语音输入
    t  自由指令（走慢通道）    p 心跳    q 退出
"""

from __future__ import annotations

import argparse
import csv
import os
import queue
import sys
import threading
import time

import protocol as P
from transport import DEFAULT_HOST, DEFAULT_PORT, SocketClientChannel

CSV_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "latency.csv")


class FakeWatch:
    def __init__(self, host: str, port: int, log_csv: bool = True):
        self.ch = SocketClientChannel(host, port)
        self.req_id = 0
        self.sent_at: dict[int, tuple[str, float]] = {}
        self.rows: list[list[str]] = []
        self.log_csv = log_csv
        self.inkey: "queue.Queue[str]" = queue.Queue()
        self.alive = True

    # ── 发送 ────────────────────────────────────────────────
    def send_intent(self, code: str, arg: str = "") -> None:
        self.req_id = (self.req_id + 1) & 0xFF
        line = P.ai_frame(self.req_id, code, arg)
        self.sent_at[self.req_id] = (code, time.time())
        self.ch.send_line(line)
        shown = f"OV+AI={self.req_id},{code}" + (f",{arg}" if arg else "")
        print(f"  🎤 发出  {shown}", flush=True)

    def resend(self) -> None:
        """重发上一个请求（用于验证去重：代理应只执行一次）。"""
        if self.req_id in self.sent_at:
            code, _ = self.sent_at[self.req_id]
            line = P.ai_frame(self.req_id, code)
            self.ch.send_line(line)
            # 关键：把计时戳刷新为重发时刻，否则时延会被算成"从首次发送到现在"
            self.sent_at[self.req_id] = (code, time.time())
            print(f"  🔁 重发  OV+AI={self.req_id},{code}（应被去重）", flush=True)

    # ── 接收 ────────────────────────────────────────────────
    def poll(self) -> None:
        for line in self.ch.recv_lines():
            frame = P.parse(line)
            if frame is None:
                continue
            if frame.kind == P.KIND_PING:
                self.ch.send_line(P.pong_frame())
                continue
            if frame.kind != P.KIND_ACK:
                continue

            rid = frame.req_id
            status = frame.fields[1] if len(frame.fields) > 1 else ""
            text = frame.arg
            info = self.sent_at.get(rid)
            latency = (time.time() - info[1]) * 1000 if info else float("nan")
            code = info[0] if info else "?"
            icon = "✅" if status == P.STATUS_OK else "❌"
            print(f"  {icon} 收到  OV+AR={rid},{status},{text}   "
                  f"[{latency:.0f} ms]  ← {code}", flush=True)
            if self.log_csv and info:
                self.rows.append([time.strftime("%H:%M:%S"), str(rid), code,
                                  f"{latency:.0f}", status, text])

    # ── 演示脚本 ────────────────────────────────────────────
    def demo(self, ask: bool = True) -> None:
        steps = [
            ("A07", "", "① 先问一句状态（快通道，秒回）", 1.0),
            ("A01", "", "② 放首歌（媒体键）", 1.2),
            ("A05", "", "③ 音量小一点", 1.0),
            ("T", "我有点困了", "④ 自由说话 → 走慢通道（无 key 时本地兜底）", 1.6),
            ("A03", "", "⑤ 锁屏（演练模式不会真锁）", 1.2),
            ("A06", "C:\\Windows", "⑥ 打开一个文件夹", 1.0),
            ("A99", "", "⑦ 发一个不存在的指令 → 看错误回执", 1.0),
        ]
        print("\n=== 自动演示开始（演练模式，不会真的锁屏）===\n", flush=True)
        for code, arg, desc, wait in steps:
            print(f"{desc}", flush=True)
            self.send_intent(code, arg)
            t_end = time.time() + wait
            while time.time() < t_end:
                self.poll()
                time.sleep(0.02)
            print("", flush=True)

        print("⑧ 验证去重：同一 REQ_ID 重发一次", flush=True)
        self.resend()
        t_end = time.time() + 1.2
        while time.time() < t_end:
            self.poll()
            time.sleep(0.02)

        print("\n=== 演示结束 ===", flush=True)
        self.dump_csv()

    # ── CSV（时延数据，论文 §7 用）────────────────────────────
    def dump_csv(self) -> None:
        if not self.rows:
            return
        new_file = not os.path.exists(CSV_PATH)
        with open(CSV_PATH, "a", newline="", encoding="utf-8-sig") as fh:
            w = csv.writer(fh)
            if new_file:
                w.writerow(["time", "req_id", "code", "latency_ms", "status", "text"])
            w.writerows(self.rows)
        lat = [float(r[3]) for r in self.rows]
        print(f"  📄 已追加 {len(self.rows)} 条到 {CSV_PATH}", flush=True)
        print(f"  ⏱  时延 最小 {min(lat):.0f} ms / 最大 {max(lat):.0f} ms / "
              f"平均 {sum(lat) / len(lat):.0f} ms", flush=True)

    # ── 交互 ────────────────────────────────────────────────
    def _input_thread(self) -> None:
        while self.alive:
            try:
                s = input()
            except (EOFError, KeyboardInterrupt):
                self.inkey.put("q")
                return
            self.inkey.put(s.strip())

    def interactive(self) -> None:
        menu = ("\n指令：a1 放歌 a2 下一首 a3 我困了 a4 音量+ a5 音量- "
                "a6 打开资料 a7 查状态 a8 语音 t 自由指令 r 重发 p 心跳 q 退出\n> ")
        print(menu, end="", flush=True)
        threading.Thread(target=self._input_thread, daemon=True).start()
        while self.alive:
            self.poll()
            try:
                cmd = self.inkey.get_nowait()
            except queue.Empty:
                time.sleep(0.02)
                continue
            if not cmd:
                print(menu, end="", flush=True)
                continue
            low = cmd.lower()
            if low == "q":
                self.alive = False
                break
            elif low == "r":
                self.resend()
            elif low == "p":
                self.ch.send_line(P.ping_frame())
                print("  心跳已发", flush=True)
            elif low == "t":
                text = input("  说点什么：").strip()
                if text:
                    self.send_intent("T", text)
            else:
                code = {"a1": "A01", "a2": "A02", "a3": "A03", "a4": "A04",
                        "a5": "A05", "a6": "A06", "a7": "A07", "a8": "A08"}.get(low)
                if code:
                    arg = ""
                    if code == "A06":
                        arg = input("  要打开什么路径：").strip() or "C:\\Windows"
                    self.send_intent(code, arg)
                else:
                    print("  不认识的指令", flush=True)
            print(menu, end="", flush=True)

        self.dump_csv()
        self.ch.close()
        print("已断开。", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="模拟手表终端")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--demo", action="store_true", help="自动演示")
    ap.add_argument("--yes", action="store_true", help="演示前不等回车")
    ap.add_argument("--no-csv", action="store_true", help="不写 latency.csv")
    args = ap.parse_args()

    try:
        w = FakeWatch(args.host, args.port, log_csv=not args.no_csv)
    except ConnectionError as exc:
        print(f"[错误] {exc}\n提示：先启动 gateway.py", file=sys.stderr)
        return 1

    print(f"[watch] 已连上代理 {args.host}:{args.port}", flush=True)

    if args.demo:
        if not args.yes:
            try:
                input("按回车开始自动演示…")
            except (EOFError, KeyboardInterrupt):
                pass
        w.demo()
        w.ch.close()
        return 0

    w.interactive()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
