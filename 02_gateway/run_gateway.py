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
#   python run_gateway.py bt         → 自动找蓝牙 SPP 串口（HC-05）
#   python run_gateway.py COM13      → 手动指定
#   再跟 dry / fake 可组合
port = ""
dry = False
bt = False
for a in sys.argv[1:]:
    la = a.lower()
    if la == "dry":
        dry = True
    elif la == "bt":
        bt = True
    else:
        port = a


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


def find_bt_port() -> str:
    """找蓝牙 SPP 串口。

    HC-05 在 Windows 里配对成功后，会出现**两个** COM 口（一个出方向、一个入方向），
    设备管理器里都叫「标准串行 over 蓝牙链接 / Standard Serial over Bluetooth link」。
    哪个能用取决于模块和驱动，所以这里把所有蓝牙口都列出来，
    自动挑第一个；不行就手动指定（run_gateway.py COM7 bt）。
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        return ""
    cands = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").upper()
        desc = (p.description or "")
        if "BTHENUM" in hwid or "BLUETOOTH" in desc.upper() or "蓝牙" in desc:
            cands.append((p.device, desc))
    if cands:
        print("[launcher] 发现蓝牙串口：")
        for dev, desc in cands:
            print(f"           {dev}  {desc}")
        print(f"[launcher] 自动选用 {cands[0][0]}（不行就手动指定另一个）")
        return cands[0][0]
    return ""


if not port:
    import time

    if bt:
        wait_sec = 20
        print(f"[launcher] 正在查找蓝牙串口（最多 {wait_sec} 秒）…")
        deadline = time.time() + wait_sec
        while time.time() < deadline and not port:
            port = find_bt_port()
            if port:
                break
            time.sleep(0.5)
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
if bt:
    # HC-05 出厂就是 9600 8N1；USART1（USB）才是 115200
    argv += ["--baudrate", "9600"]
if not dry:
    argv.append("--enable-actions")
# 默认启用电脑麦克风 + 本地识别；想用假识别就传 fake
if "fake" not in sys.argv[1:]:
    argv.append("--asr")
sys.argv = argv
print(f"[launcher] 启动网关：{'蓝牙' if bt else 'USB 串口'} {port}"
      f"{' @9600' if bt else ''}，{'演练模式' if dry else '真执行模式'}")
raise SystemExit(gateway.main())
