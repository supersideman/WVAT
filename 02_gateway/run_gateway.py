"""一键启动网关：自动找 CH340 串口 + 真执行模式。

为什么需要它：
    网关**必须由你自己启动**（在你自己登录的桌面上）。
    如果由别的高权限/沙箱进程代起，Windows 的按键注入（SendInput）会被系统
    挡掉——"返回成功但输入进不了桌面"，表现为播放/暂停、下一首毫无反应。

用法：
    双击同目录的 start_gateway.cmd
    或在终端：python run_gateway.py            （自动找串口）
             python run_gateway.py COM13      （手动指定）
             python run_gateway.py COM13 dry  （演练模式，不动系统）
"""
from __future__ import annotations

import os
import sys

# ── 解释器自检：必须用随附的 Python 3.12 ──────────────────
# 原因：cffi / vosk 的 C 扩展按 cp312 编译，换解释器会报
#       "No module named '_cffi_backend'"，且极难猜到原因（踩过：
#       启动器把中文用户名写成 ??? → 回退到系统 Python 3.15 → 全挂）。
if sys.version_info[:2] != (3, 12):
    print(f"[launcher] 需要 Python 3.12，当前是 {sys.version.split()[0]}")
    print(f"[launcher] 解释器：{sys.executable}")
    print("[launcher] 请用 02_gateway\\start_gateway.cmd 启动，不要直接双击 .py")
    sys.exit(2)


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# 解压后的 pyserial 放在项目根的 vendor/_pylibs（见 .gitignore，不入库）
_pylibs = os.path.abspath(os.path.join(HERE, "..", "vendor", "_pylibs"))
if _pylibs not in sys.path:
    sys.path.insert(0, _pylibs)

# ── 参数 ────────────────────────────────────────────────
#   python run_gateway.py            → 自动找 CH340（USB 串口）
#   python run_gateway.py auto       → **自动选链路**：USB 优先，不行转蓝牙
#   python run_gateway.py bt         → 只找蓝牙 SPP 串口（HC-05）
#   python run_gateway.py COM13      → 手动指定
#   再跟 dry / fake 可组合
port = ""
dry = False
bt = False
auto = False
for a in sys.argv[1:]:
    la = a.lower()
    if la == "dry":
        dry = True
    elif la == "bt":
        bt = True
    elif la == "auto":
        auto = True
    else:
        port = a

baud_used = 115200          # auto/bt 模式下会被改成实际生效的波特率


def find_ch340() -> str:
    """找 CH340（VID_1A86）。优先描述里带 CH340 的。"""
    try:
        from serial.tools import list_ports
    except ImportError:
        return ""
    cands = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").upper()
        desc = (p.description or "")
        if "1A86" in hwid or "CH340" in desc.upper():
            cands.append((p.device, desc))
    if cands:
        print(f"[launcher] 找到串口设备：{cands[0][1]} → {cands[0][0]}")
        return cands[0][0]
    return ""


def list_bt_ports():
    """列出所有蓝牙 SPP 串口。

    HC-05 配对成功后 Windows 会建**两个** COM 口（一"传出"一"传入"），
    只有一个能真正收发。哪个对不能靠猜，所以下面用 PING 探测。
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    cands = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").upper()
        desc = (p.description or "")
        if "BTHENUM" in hwid or "BLUETOOTH" in desc.upper() or "蓝牙" in desc:
            cands.append((p.device, desc))
    return cands


def probe_port(port: str, baud: int, timeout: float = 2.5) -> bool:
    """往这个口发一条 OV+PING，看有没有 OV+PONG 回来。

    固件收到 PING 会回 PONG（user_link.c 的 s_pong_pending），
    所以"有回应"就等于"这个口能真正收发"。

    ⚠️ 必须在**子线程 + 硬超时**里做：Windows 的蓝牙 SPP 口在建立链路时，
       serial.Serial() 的 open() 可能卡几十秒甚至一直不返回（踩过：
       主线程直接 open 会卡死，界面停在"逐个探测"没有任何输出）。
       线程是 daemon，卡住也不影响继续往下走。
    """
    import threading

    result = {"ok": False}

    def _run() -> None:
        try:
            import serial
            import time as _t
            with serial.Serial(port, baud, timeout=0.2, write_timeout=1.0) as sp:
                sp.reset_input_buffer()
                sp.write(b"OV+PING\r\n")
                t0 = _t.time()
                while _t.time() - t0 < timeout:
                    line = sp.readline()
                    if b"OV+PONG" in line:
                        result["ok"] = True
                        return
        except Exception:
            pass

    th = threading.Thread(target=_run, daemon=True)
    th.start()
    th.join(timeout + 3.0)          # 打开卡住也只等这么久
    return result["ok"]


if not port:
    import time

    if bt:
        wait_sec = 20
        print(f"[launcher] 正在查找蓝牙串口（最多 {wait_sec} 秒）…")
        deadline = time.time() + wait_sec
        cands = []
        while time.time() < deadline and not cands:
            cands = list_bt_ports()
            if cands:
                break
            time.sleep(0.5)

        if cands:
            print("[launcher] 发现蓝牙串口：")
            for dev, desc in cands:
                print(f"           {dev}   {desc}")
            print("[launcher] 逐个探测（每个最多 5 秒）：", flush=True)
            for dev, desc in cands:
                print(f"           {dev}  探测中…", flush=True)
                ok = probe_port(dev, 9600)
                print(f"           {dev}  {'✅ 有回应' if ok else '✗ 无回应'}", flush=True)
                if ok:
                    port = dev
                    baud_used = 9600
                    break
            if not port:
                port = cands[0][0]
                baud_used = 9600
                print("[launcher] ⚠️ 没有口回应 PING，先按第一个试")
                print("[launcher]    若按钮点了网关没反应 → 确认模块 TXD→PA3、RXD→PA2（交叉）")

    elif auto:
        # ── 自动选链路：USB 优先，不通再转蓝牙 ──────────────────
        print("[launcher] 自动模式：先找 USB 串口，不通再转蓝牙")
        cands = []
        deadline = time.time() + 15
        while time.time() < deadline:
            ch = find_ch340()
            if ch:
                cands.append((ch, 115200, "USB"))
            for dev, desc in list_bt_ports():
                cands.append((dev, 9600, "蓝牙"))
            if cands:
                break
            time.sleep(0.5)

        if cands:
            print("[launcher] 候选链路：")
            for dev, baud, kind in cands:
                print(f"           {dev}  {baud}  {kind}")
            print("[launcher] 逐个探测（发 OV+PING，等 OV+PONG）：", flush=True)
            for dev, baud, kind in cands:
                print(f"           {dev}（{kind}）探测中…", flush=True)
                ok = probe_port(dev, baud)
                print(f"           {dev}  {'✅ 有回应' if ok else '✗ 无回应'}", flush=True)
                if ok:
                    port = dev
                    baud_used = baud
                    print(f"[launcher] 选用 {dev}（{kind} @{baud}）", flush=True)
                    break
            if not port:
                port, baud_used, kind = cands[0]
                print("[launcher] ⚠️ 没有链路回应 PING，先按第一个试")
                print("[launcher]    检查：USB 线是否插好 / 手表是否已复位并运行本版固件")

    else:
        # 等板子插上：CH340 有时"设备枚举成功但没生成 COM 口"，拔插一次即恢复。
        # 所以这里给 30 秒窗口，而不是立刻退出——插线顺序就不讲究了。
        wait_sec = 30
        print(f"[launcher] 暂未发现串口，等待板子插上（最多 {wait_sec} 秒）…")
        deadline = time.time() + wait_sec
        while time.time() < deadline and not port:
            time.sleep(0.5)
            port = find_ch340()

if not port:
    if bt:
        print("[launcher] 没找到蓝牙串口。请检查：")
        print("  1) HC-05 是否已在「设置 → 蓝牙和其他设备」里配对成功")
        print("  2) 配对后「更多蓝牙选项 → COM 端口」里应有两个「传出/传入」端口")
        print("  3) 或手动指定：python run_gateway.py COM7 bt")
    else:
        print("[launcher] 没找到 CH340 串口。")
        print("  1) 确认板子上标着「串口」的那个 Type-C 已插好（不是标「USB」的那个）")
        print("  2) 试试拔掉重插：有时设备枚举成功但端口没挂上，拔插一次即恢复")
        print("  3) 或手动指定口号：python run_gateway.py COM13")
        print("  4) 想走蓝牙：python run_gateway.py bt")
    try:
        from serial.tools import list_ports
        allp = list(list_ports.comports())
        print("  当前系统串口：", ", ".join(p.device for p in allp) or "（一个都没有）")
    except Exception:
        pass
    raise SystemExit(1)

# ── 交给 gateway.main() ─────────────────────────────────
import gateway  # noqa: E402

argv = ["gateway.py", "--transport", "serial", "--serial-port", port]
if baud_used != 115200:
    # HC-05 出厂 9600；USB（CH340）是 115200。波特率跟着探测结果走，
    # 不要按"模式"猜——自动模式下两条链路都可能是候选。
    argv += ["--baudrate", str(baud_used)]
if not dry:
    argv.append("--enable-actions")
# 默认启用电脑麦克风 + 本地识别；想用假识别就传 fake
if "fake" not in sys.argv[1:]:
    argv.append("--asr")
sys.argv = argv
print(f"[launcher] 启动网关：串口 {port} @{baud_used}，"
      f"{'演练模式' if dry else '真执行模式'}")
raise SystemExit(gateway.main())
