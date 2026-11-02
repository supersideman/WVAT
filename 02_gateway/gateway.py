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
TIME_SYNC_SEC = 30         # 每 30 秒给手表授时一次（校正 RTC）

# 慢通道兜底关键词（没有 LLM 也能工作）
FALLBACK_KEYWORDS = [
    (("歌", "音乐", "播放", "放首"), "A01"),
    (("下一首", "切歌", "换一首"), "A02"),
    (("困", "睡", "锁屏", "休息"), "A03"),
    (("大声", "音量大", "调高"), "A04"),
    (("小声", "音量小", "调低", "静音"), "A05"),
    (("资料", "文件", "桌面", "文件夹", "打开"), "A06"),
    (("几点", "时间", "状态", "在线"), "A07"),
]

# 假 ASR 用：轮流返回这几句（真 ASR 接上后整段删掉）
FAKE_ASR_SENTENCES = [
    "帮我打开资料",
    "下一首",
    "我困了",
    "现在几点",
    "把音量调高一点",
]


class Gateway:
    def __init__(self, channel, executor: Executor, use_llm: bool = False, asr=None):
        self.ch = channel
        self.ex = executor
        self.use_llm = use_llm
        self.asr = asr              # 语音识别器（None = 用假识别）
        self.dedup: "queue.OrderedDict[int, str]" = __import__("collections").OrderedDict()
        self.last_req_id = 0        # 用于识别设备复位（REQ_ID 回退）
        self.missed_pong = 0
        self.last_ping = 0.0
        self.last_time_sync = 0.0   # 上次授时时间（0 = 还没授过，下一轮就发）
        self.running = True
        self.counters = {"fast": 0, "slow": 0, "err": 0, "dedup": 0}

    # ── 主循环 ──────────────────────────────────────────────
    def run(self) -> None:
        print("\n网关已启动。等待终端发来 OV+AI=... （Ctrl+C 退出）\n", flush=True)
        while self.running:
            for line in self.ch.recv_lines():
                self.on_line(line)

            now = time.time()

            # 授时：启动后立刻发一次，之后每 30 秒一次（手表收到会写 RTC）
            if getattr(self.ch, "connected", False) and now - self.last_time_sync > TIME_SYNC_SEC:
                self.ch.send_line(P.time_event())
                self.last_time_sync = now

            if getattr(self.ch, "connected", False) and now - self.last_ping > HEARTBEAT_SEC:
                self.ch.send_line(P.ping_frame())
                self.last_ping = now
                self.missed_pong += 1
                if self.missed_pong > MAX_MISSED_PONG:
                    print("[!] 心跳超时，判为掉线 → 清空去重表（设备可能已复位）", flush=True)
                    self.missed_pong = 0
                    self.dedup.clear()
                    self.last_req_id = 0
            time.sleep(0.02)

    # ── 单帧处理 ────────────────────────────────────────────
    def on_line(self, line: str) -> None:
        frame = P.parse(line)
        if frame is None:
            # 固件的 printf 调试日志走同一条串口（形如 "[clock] ..."），
            # 它不是协议帧，但对我们很有用，单独标出来而不是当成坏帧报警。
            if line.startswith("["):
                print(f"[设备] {line}", flush=True)
            else:
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

        # 设备复位检测：手表一上电 REQ_ID 就从 1 重新数，而网关还记着上一轮
        # 的 1..N，会把全新的请求误判成"重传"、只回缓存不执行——指令被静默吞掉。
        # 所以看到 REQ_ID 回退就清空去重表。
        # （REQ_ID 是 uint8，255→1 的回绕也会触发一次，代价仅是可能重复执行一次，
        #   可以接受；相比"吞指令"这个代价小得多。）
        if req_id < self.last_req_id and self.dedup:
            print("  [去重表] REQ_ID 回退 → 判定设备已复位，清空缓存", flush=True)
            self.dedup.clear()
        self.last_req_id = req_id

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

        # A08 语音输入。两种工作方式：
        #   麦克风版（--asr）：手表发 A08,start 开始录音，A08,stop 结束并转写
        #   假识别版（默认）：直接回预设句子，用于验证链路与界面
        if code == "A08":
            self.counters["slow"] += 1
            return self._handle_voice(arg)

        # 慢通道：自由文本
        if code == "T":
            self.counters["slow"] += 1
            return self.slow_channel(arg), P.STATUS_OK

        self.counters["err"] += 1
        return "未知指令", P.STATUS_ERR

    # ── 语音：录音控制 + 识别 ────────────────────────────────
    def _handle_voice(self, arg: str) -> tuple[str, str]:
        """arg = "start" / "stop" / 空。

        为什么要分 start/stop：
            用户说的话在"按下开始"和"按下停止"之间。如果等 stop 才开始录音，
            录到的是说完之后的环境音，必然识别不出来。
        """
        if self.asr is None:
            text = self._fake_asr()
            self.ch.send_line(P.ev_frame("ASR", text))
            print(f"  [假识别] → {text}", flush=True)
            return "识别完成", P.STATUS_OK

        cmd = (arg or "").strip().lower()

        if cmd == "start":
            try:
                self.asr.start()
                print("  [录音] 开始（对着电脑麦克风说话）", flush=True)
                return "录音中", P.STATUS_OK
            except Exception as exc:                  # noqa: BLE001
                print(f"  [!] 打开麦克风失败：{exc}", flush=True)
                self.asr = None                        # 降级到假识别
                text = self._fake_asr()
                self.ch.send_line(P.ev_frame("ASR", text))
                return "麦克风不可用", P.STATUS_ERR

        if cmd == "stop":
            try:
                pcm = self.asr.stop()
                ms = int(len(pcm) / 2 / 16000 * 1000)
                print(f"  [录音] 结束，{ms} ms，peak={self.asr.peak}", flush=True)
                text = self.asr.transcribe(pcm) if ms >= 300 else ""
            except Exception as exc:                  # noqa: BLE001
                print(f"  [!] 识别失败：{exc}", flush=True)
                text = ""
            if not text:
                text = self._fake_asr()
                print(f"  [空结果→假识别兜底] → {text}", flush=True)
            self.ch.send_line(P.ev_frame("ASR", text))
            print(f"  [识别] → {text}", flush=True)
            return "识别完成", P.STATUS_OK

        # 没带 arg：退化为"录一小段再识别"（兼容不带参数的老调用）
        try:
            pcm, peak, rms = self.asr.record_and_transcribe(4.0)
            text = pcm or self._fake_asr()
        except Exception as exc:                      # noqa: BLE001
            print(f"  [!] 录音/识别失败：{exc}", flush=True)
            text = self._fake_asr()
        self.ch.send_line(P.ev_frame("ASR", text))
        return "识别完成", P.STATUS_OK

    # ── 假 ASR（真识别接上前用它把流程跑通）──────────────────
    def _fake_asr(self) -> str:
        """轮流返回预设句子，模拟"识别出你说的话"。"""
        idx = self.counters.get("fake_asr_idx", 0)
        text = FAKE_ASR_SENTENCES[idx % len(FAKE_ASR_SENTENCES)]
        self.counters["fake_asr_idx"] = idx + 1
        return text

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
    ap.add_argument("--asr", action="store_true", help="用电脑麦克风 + 本地 Vosk 做真语音识别")
    ap.add_argument("--mic", type=int, default=None, help="指定输入设备编号（见 asr.py 的设备列表）")
    ap.add_argument("--list", action="store_true", help="打印白名单并退出")
    args = ap.parse_args()

    if args.list:
        print(describe_table())
        return 0

    if args.transport == "serial":
        if not args.serial_port:
            print("serial 模式需要 --serial-port COMxx", file=sys.stderr)
            return 2
        try:
            ch = SerialChannel(args.serial_port, args.baudrate)
        except RuntimeError as exc:
            print(f"[错误] {exc}", file=sys.stderr)
            return 1
    else:
        ch = SocketServerChannel(args.host, args.port)

    # 语音识别器：--asr 时才加载（模型加载要 1~2 秒）
    asr = None
    if args.asr:
        try:
            import asr as asr_mod
            if not asr_mod.model_ready():
                print("[asr] 模型缺失，改用假识别；请把 vosk-model-small-cn-0.22 放到 vendor/models/", flush=True)
            else:
                asr = asr_mod.LiveRecorder(dev=args.mic)
                print(f"[asr] 已启用本地中文识别（默认设备：{args.mic if args.mic is not None else "系统默认"}）", flush=True)
        except Exception as exc:                      # noqa: BLE001
            print(f"[asr] 初始化失败，改用假识别：{exc}", flush=True)

    gw = Gateway(ch, Executor(dry_run=not args.enable_actions), use_llm=args.llm, asr=asr)
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
