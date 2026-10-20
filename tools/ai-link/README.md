# P0 联调工具：模拟手表 ↔ PC 网关

**不需要任何硬件、不需要装第三方库**（纯 Python 标准库），就能把《AI链路设计》里的协议与双通道路由跑通。

## 文件

| 文件 | 作用 |
|---|---|
| `protocol.py` | `OV+` 协议帧的构造 / 解析 / 文本截断（对应文档 §3.3） |
| `transport.py` | 链路抽象：本地 TCP（模拟）、真实串口（需 pyserial） |
| `intents.py` | **动作白名单**与执行器（纯 ctypes，Windows 媒体键/锁屏/关屏） |
| `gateway.py` | PC 网关：去重 → 双通道路由 → 执行 → 应答 → 心跳 |
| `fake_watch.py` | 模拟手表：交互菜单 / 自动演示 / 时延采集 |
| `latency.csv` | 每次请求的时延记录（跑完自动追加，供论文取数） |

## 快速开始（三条命令）

```powershell
cd tools\ai-link

# ① 终端 A：启动网关（默认演练模式，不会真的锁屏/切歌）
python gateway.py

# ② 终端 B：跑自动演示
python fake_watch.py --demo --yes

# ③ 想自己点？用交互模式
python fake_watch.py
#   a1 放歌  a2 下一首  a3 我困了  a4 音量+  a5 音量-  a6 打开资料
#   a7 查状态  a8 语音  t 自由指令  r 重发（验证去重）  p 心跳  q 退出
```

## 常用参数

```powershell
python gateway.py --list                    # 打印白名单
python gateway.py --port 8766               # 换端口
python gateway.py --llm                     # 启用 DeepSeek 慢通道（需 DEEPSEEK_API_KEY）
python gateway.py --enable-actions          # ⚠️ 真执行：会锁屏/切歌/调音量
python gateway.py --transport serial --serial-port COM14   # 真实串口/蓝牙 SPP
```

## 已经验证过的行为（2026-10-19 实测）

| 用例 | 结果 |
|---|---|
| 快通道 A07 查状态 | `OK,22:19:41 在线`，21 ms |
| 快通道 A01/A05 | 正确回执，20–21 ms |
| 慢通道 `T` 自由文本"我有点困了" | 本地关键词兜底命中 A03，回"已锁屏并静音" |
| 未知指令 A99 | **`ERR,未知指令`** |
| 同一 REQ_ID 重发 | 网关打 `[去重]`，只执行一次 |
| 文本超长 | 按 60 字节截断，不切坏汉字 |

时延记录见 `latency.csv`（本地 TCP 约 20 ms；换成真串口/蓝牙会明显变大，那才是要写进论文的数）。

## 还没做的（属于后面阶段）

- `A08 语音输入` 目前只回"语音识别待接入"——P2 阶段接 I2S 采音 + SenseVoice
- `--enable-actions` **我没有实测**（会真的锁你的屏），你确认后再自己试
- 状态镜像窗口（`mirror_ui`）还没写——演示时给评委看画面用
- 分级确认（KEY1 确认 / KEY2 重录）在终端侧，等真机 UI 一起做

## 接真机时的改动点

1. `transport.py` 里换 `SerialChannel`（`pip install pyserial`）
2. 终端侧不需要改协议，只是把 `send_intent()` 换成 I2S 采音 → ADPCM → `OV+AI=<id>,A08` + 音频分片
3. 网关侧在 `dispatch()` 的 `A08` 分支里接 ASR

> 协议先定死，两边才好并行——这也是 P0 放在最前面的原因。
