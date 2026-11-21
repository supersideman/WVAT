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
WX_INTERVAL_SEC = 300      # 每 5 分钟刷一次天气（天气变化慢，但丢帧后能很快自愈）

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

# ── 模糊匹配用的相似度 ────────────────────────────────────
def _similarity(a: str, b: str) -> float:
    """返回 0~1 的相似度（1 = 完全相同）。

    做法：取"编辑距离"和"最长公共子序列"两种口径里更高的那个，
    这样既能容忍错字（资料→质量），也能容忍多说了几个字（请帮我打开一下资料）。

    不用拼音库：要额外依赖，而项目原则是"零安装、纯本地"。
    """
    if not a or not b:
        return 0.0
    if a == b:
        return 1.0
    # 短句里出现，直接给高分（避免"打开桌面"和"打开资料"打平）
    if b in a:
        return 0.9

    def lcs(x: str, y: str) -> int:
        prev = [0] * (len(y) + 1)
        for cx in x:
            cur = [0]
            for j, cy in enumerate(y, 1):
                cur.append(prev[j - 1] + 1 if cx == cy else max(prev[j], cur[j - 1]))
            prev = cur
        return prev[-1]

    return lcs(a, b) / max(len(a), len(b))




class Gateway:
    def __init__(self, channel, executor: Executor, use_llm: bool = False, asr=None,
                 weather=None):
        self.ch = channel
        self.ex = executor
        self.use_llm = use_llm
        self.asr = asr              # 语音识别器（LiveRecorder 实例；None = 不可用）
        self.weather = weather      # 天气取值器（None = 不推天气）
        self.last_wx = 0.0          # 上次推天气时间（0 = 还没推过）
        self.saw_device = False     # 是否收到过设备的任何一帧（= 链路双向通）
        self.asr_error = ""         # 不可用时的原因（启动自检会填）
        self._phrases  = None       # 语法约束用的短语表（惰性生成）
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

            # 天气：连上后立刻推一次，之后每 WX_INTERVAL_SEC 一次。
            # 取天气是**阻塞**的 HTTP 请求（最长 4 秒），所以只在"到点"那一刻做，
            # 平时这一行什么都不开销；取失败会保留旧值并在 weather.py 里退避重试。
            # 天气：**等设备先说过话**再推，避免把帧发进"只收不发"的黑洞。
            # 昨天就踩过：链路的"传入"端口能收到手表的按钮，但发不下去，
            # 于是天气一直卡在设备端显示 "--"。
            if self.weather is not None and getattr(self.ch, "connected", False) and self.saw_device:
                if self.last_wx == 0.0 or now - self.last_wx > WX_INTERVAL_SEC:
                    self.weather.update(WX_INTERVAL_SEC)
                    fields = self.weather.frame_fields()
                    if fields:
                        kind, temp = fields
                        self.ch.send_line(P.wx_event(kind, temp))
                        self.last_wx = now

            if getattr(self.ch, "connected", False) and now - self.last_ping > HEARTBEAT_SEC:
                self.ch.send_line(P.ping_frame())
                self.last_ping = now
                self.missed_pong += 1
                if self.missed_pong > MAX_MISSED_PONG:
                    print("[!] 心跳超时，判为掉线 → 清空去重表（设备可能已复位）", flush=True)
                    self.missed_pong = 0
                    self.dedup.clear()
                    self.last_req_id = 0
                    self.saw_device = False
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

        self.saw_device = True       # 设备说话了 → 说明链路是双向通的

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
                # 查状态回**真实数据**：时间 + 音量 + 是否静音（CoreAudio 可回读）。
                # 以前只回"在线"，等于什么信息都没有——界面上看不出区别。
                try:
                    pct = round(audio_win.get_volume() * 100)
                    muted = audio_win.get_mute()
                    tail = " 静音" if muted else ""
                    return f"{time.strftime('%H:%M')} 音量{pct}%{tail}", P.STATUS_OK
                except Exception as exc:              # noqa: BLE001
                    print(f"  [!] 读音量失败：{exc}", flush=True)
                    return f"{time.strftime('%H:%M')} 在线", P.STATUS_OK
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

        ⚠️ 这里**不再有任何假识别兜底**：任何一步失败都明确回 ERR 并打印原因。
           以前"失败就悄悄用预设句子"，看起来能用、其实是假的，排查时被误导过。
        """
        if self.asr is None:
            err = self.asr_error or "语音识别未启用"
            print(f"  [!] {err}", flush=True)
            return "语音识别未启用", P.STATUS_ERR

        cmd = (arg or "").strip().lower()

        if cmd == "start":
            try:
                self.asr.start()
                print("  [录音] 开始（对着电脑麦克风说话）", flush=True)
                return "录音中", P.STATUS_OK
            except Exception as exc:                  # noqa: BLE001
                print(f"  [!] 打开麦克风失败：{type(exc).__name__}: {exc}", flush=True)
                self.asr_error = f"麦克风不可用：{exc}"
                return "麦克风不可用", P.STATUS_ERR

        if cmd == "stop":
            try:
                pcm = self.asr.stop()
                ms = int(len(pcm) / 2 / 16000 * 1000)
                print(f"  [录音] 结束，{ms} ms，peak={self.asr.peak}", flush=True)
            except Exception as exc:                  # noqa: BLE001
                print(f"  [!] 停止录音失败：{exc}", flush=True)
                return "录音失败", P.STATUS_ERR

            if ms < 300:
                print("  [!] 录音太短（<0.3s），不发识别", flush=True)
                return "录音太短", P.STATUS_ERR

            try:
                text, how = self.asr.transcribe_smart(pcm, self._asr_phrases())
            except Exception as exc:                  # noqa: BLE001
                print(f"  [!] 识别失败：{type(exc).__name__}: {exc}", flush=True)
                return "识别失败", P.STATUS_ERR

            if not text:
                print(f"  [!] 识别为空（peak={self.asr.peak}，声音可能太轻）", flush=True)
                return "没听清", P.STATUS_ERR

            self.ch.send_line(P.ev_frame("ASR", text))
            tag = "词表内" if how == "grammar" else "自由"
            print(f"  [识别/{tag}] → {text}", flush=True)
            return "识别完成", P.STATUS_OK

        # 没带 arg：说明终端发的还是老式 A08 → 固件是旧版
        print("  [!] 收到不带 start/stop 的 A08：固件可能是旧版（请重新编译烧录）", flush=True)
        return "固件需更新（A08 缺 start/stop）", P.STATUS_ERR

    def _asr_phrases(self) -> list[str]:
        """给语法约束识别用的候选短语（来自白名单，单一数据源）。

        为什么从 FALLBACK_KEYWORDS 生成而不是另写一份：
            两处各写一份必然漂移——加了新动作却忘了同步词表，
            后果是"说了也不认"。这里直接复用同一份关键词。
        """
        if self._phrases is not None:
            return self._phrases

        out: list[str] = []
        for words, code in FALLBACK_KEYWORDS:
            out.extend(words)
            act = WHITELIST.get(code)
            if act is not None:
                out.append(act.name)

        # 常见自然说法（带宾语/语气，比单词更像人话）
        out.extend([
            "帮我打开资料", "打开资料", "帮我查一下状态", "现在几点了",
            "把音量调高一点", "把音量调低一点", "帮我放首歌", "切到下一首",
        ])

        # 去重且保序；只保留纯中文、且**至少 2 个字**的短语
        #
        # 为什么过滤单字（歌/困/睡）：
        #   语法约束解码会把语音片段切给任意允许的短语，单字短语太"容易命中"，
        #   于是长句容易被切碎成一串单字（说"我要睡了"→ 输出"睡"）。
        #   两字以上才有足够音素区分度。单字的需求由"自由识别 + 模糊匹配"兜底。
        #
        # 为什么过滤含符号的短语（音量+/音量-）：
        #   Vosk 中文词表里没有 +/- 这类符号，留着只会刷
        #   "Ignoring word missing in vocabulary" 的警告。
        seen: set[str] = set()
        self._phrases = [
            p for p in out
            if p and p not in seen and not seen.add(p)
            and len(p) >= 2
            and all("\u4e00" <= ch <= "\u9fff" for ch in p)
        ]
        return self._phrases

    def slow_channel(self, text: str) -> str:
        if self.use_llm:
            try:
                return self._llm_intent(text)
            except Exception as exc:
                print(f"  [!] LLM 失败，降级本地规则：{exc}", flush=True)
        return self._fallback(text)

    def _fallback(self, text: str) -> str:
        """本地关键词兜底：精确包含 → 模糊相似 → 放弃。

        为什么需要模糊那一层：
            小模型（42MB）对口音、语速、连读很敏感，常把「资料」听成「质量」、
            「下一首」听成「下一手」。只做精确包含的话，差一个字就全崩。
            这里用"编辑距离相似度"给每条关键词打分，超过阈值就认。
        """
        # ① 精确包含（最快、最准）
        for words, code in FALLBACK_KEYWORDS:
            if any(w in text for w in words):
                return self._run_code(code)

        # ② 模糊匹配：整句与关键词的相似度
        best_code, best_score, best_kw = "", 0.0, ""
        for words, code in FALLBACK_KEYWORDS:
            for w in words:
                score = _similarity(text, w)
                if score > best_score:
                    best_code, best_score, best_kw = code, score, w

        if best_score >= 0.5:
            print(f"  [模糊匹配] 「{text}」≈「{best_kw}」({best_score:.2f}) → {best_code}", flush=True)
            return self._run_code(best_code)

        if best_score > 0:
            print(f"  [模糊匹配] 最接近「{best_kw}」但只有 {best_score:.2f}，不执行", flush=True)
        return "没听懂，请再说一次"

    def _run_code(self, code: str) -> str:
        ok, reply = self.ex.execute(code, "")
        return reply if ok else f"执行失败：{reply}"

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
    ap.add_argument("--no-weather", action="store_true",
                    help="不向手表推送天气（默认会推，城市见 weather.py）")
    ap.add_argument("--wx-lat", type=float, default=None, help="天气用的纬度（默认取 weather.py）")
    ap.add_argument("--wx-lon", type=float, default=None, help="天气用的经度（默认取 weather.py）")
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

    # 语音识别器：--asr 时做**启动自检**（依赖问题必须在启动时暴露，不能等按了才报）
    asr = None
    asr_error = ""
    if args.asr:
        try:
            import asr as asr_mod
            ok, msg = asr_mod.preflight()
            print(f"[asr] {msg}", flush=True)
            if ok:
                asr = asr_mod.LiveRecorder(dev=args.mic)
                print("[asr] 已启用本地中文识别", flush=True)
            else:
                asr_error = msg
        except Exception as exc:                      # noqa: BLE001
            asr_error = f"{type(exc).__name__}: {exc}"
            print(f"[asr] 初始化失败：{asr_error}", flush=True)
    else:
        asr_error = "未加 --asr 参数"

    # 天气：默认启用（open-meteo 免费、无需 key）。取失败只打印提示，不影响其它功能。
    weather = None
    if not args.no_weather:
        try:
            import weather as wx_mod
            lat = args.wx_lat if args.wx_lat is not None else wx_mod.LAT
            lon = args.wx_lon if args.wx_lon is not None else wx_mod.LON
            weather = wx_mod.Weather(lat, lon)
            print(f"[weather] 已启用：{weather.city} ({lat:.4f}, {lon:.4f})，"
                  f"每 {WX_INTERVAL_SEC // 60} 分钟推送一次", flush=True)
        except Exception as exc:                      # noqa: BLE001
            print(f"[weather] 初始化失败，跳过天气：{type(exc).__name__}: {exc}", flush=True)

    gw = Gateway(ch, Executor(dry_run=not args.enable_actions), use_llm=args.llm,
                 asr=asr, weather=weather)
    gw.asr_error = asr_error
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
