# WVAT Go · 安卓手机网关

把 `02_gateway`（Python，跑在电脑上）的职责搬到手机上：
**手表通过蓝牙连这台手机，手机就是原来的那台电脑。**

> 参考实现：`ccch1mneyyy/dsh-Go`（DSH 的安卓客户端）。
> 本工程只借鉴它的**分层思路**，代码全部自写——因为我们要的是"蓝牙网关"，
> 它做的是"AI 对话客户端"，两者没有可以直接复用的模块。

---

## 它解决的问题

| 原来 | 现在 |
|---|---|
| 手表必须插 USB 线连电脑 | 手表连**手机**，手机放口袋里就行 |
| 电脑上要跑 Python + vendor 依赖 | 一个 APK，零第三方依赖 |
| 演示时拖着一根线 | 手表 + 充电宝 + 手机，**全程无线** |

**协议一字不改**：手表固件里的 `OV+` 帧已经写好了，
手机端只要"像原来那台电脑一样说话"，固件一行都不用动。

---

## 一期做了什么（当前状态）

| 能力 | 状态 | 对应 PC 网关 |
|---|---|---|
| 蓝牙 SPP 连接 HC-05 | ✅ | `SerialChannel` |
| 指令解析 `OV+AI` / 回执 `OV+AR` | ✅ | `protocol.py` |
| 授时（连上即发 + 每 30 秒） | ✅ | `time_event()` |
| 心跳 `PING`/`PONG` + 掉线判定 | ✅ | `HEARTBEAT_SEC` |
| 天气 `OV+EV=WX`（open-meteo） | ✅ | `weather.py` |
| 白名单动作 A01~A07 | ✅ | `executor.py` |
| REQ_ID 去重 | ✅ | `dedup` |
| 语音（手机麦克风 + 识别） | ⬜ 二期 | `asr.py` |
| AI 兜底（自由文本 → 动作） | ⬜ 三期 | `--llm` |

## 动作映射（手机和电脑能力不同，论文里要写清楚）

| 码 | 动作 | 手机实现 | 与 PC 的差异 |
|---|---|---|---|
| A01 | 播放/暂停 | `dispatchMediaKeyEvent` | 一致 |
| A02 | 下一首 | 同上 | 一致 |
| A03 | 我困了 | **只能静音** | PC 能锁屏+关屏；手机要设备管理员 |
| A04/A05 | 音量 ± | `AudioManager` | 一致 |
| A06 | 打开资料 | 打开系统"文档"界面 | PC 是打开文件夹，**语义变了** |
| A07 | 查状态 | 音量 + 天气 | 一致 |

---

## 怎么构建

### 需要装一次 Android Studio

本工程**不含** `gradle/wrapper`，首次打开时让 Android Studio 自己生成最省事。

1. 装 [Android Studio](https://developer.android.com/studio)（约 4 GB，自带 JDK 17 和 SDK）
2. `File → Open`，选这个 `06_android` 目录
3. 它会提示"没有 Gradle wrapper / 是否使用 Gradle 8.x" → **同意**（会自动下载）
4. 等 SDK 组件下载完（Android 14 / API 34 平台包）
5. 手机用 USB 连电脑，开"开发者选项 → USB 调试"
6. 点 ▶ Run

> 首次构建要下载 Gradle + AGP，**联网约 5~10 分钟**。之后再构建就快了。

### 命令行（可选，装了 SDK 之后）

```bash
cd 06_android
# 第一次需要生成 wrapper
gradle wrapper --gradle-version 8.5
./gradlew assembleDebug
# 产物：app/build/outputs/apk/debug/app-debug.apk
```

---

## 手机上的使用步骤

```
① 系统设置 → 蓝牙 → 配对 HC-05（配对码 1234）
② 打开 WVAT Go → 授予蓝牙权限
③ 点「连接手表」→ 选 HC-05
④ 日志出现「已连接」→ 手表右上角的链路点应该亮起来
```

### 日志里应该看到

```
网关已启动：连上即授时 · 20 秒心跳 · 5 分钟天气
← #1 A02                 ← 手表按了「下一首」
→ OK 已切到下一首
→ 天气 类别0 5℃          ← 天气推给手表
```

### 手表端应该看到

- 右上角链路点：**USB 变蓝牙**（`B`）
- 日期页：天气图标 + 温度
- 点意图格：回执亮绿点

---

## 已知限制（都是刻意留到后面）

| 限制 | 原因 | 计划 |
|---|---|---|
| 息屏后可能断开 | 没做成前台服务 | 二期加 `ForegroundService` |
| 自由文本一律回 `ERR` | AI 兜底未接 | 三期接 LLM |
| 城市写死北京 | 没做设置页 | 二期加设置页 |
| A03 不能关屏 | 需要设备管理员权限 | 写进论文的"平台能力差异" |

---

## 目录

```
06_android/
├── settings.gradle.kts
├── build.gradle.kts
├── gradle.properties
├── .gitignore
├── README.md
└── app/
    ├── build.gradle.kts
    └── src/main/
        ├── AndroidManifest.xml
        ├── res/values/strings.xml
        └── java/com/wvat/go/
            ├── OvProtocol.kt    OV+ 帧解析与生成（对齐 protocol.py）
            ├── BtLink.kt        蓝牙 SPP 连接/收发/断线
            ├── Weather.kt       open-meteo 取天气（对齐 weather.py）
            ├── Actions.kt       A01~A07 的动作执行
            ├── GatewayCore.kt   周期任务 + 指令分发（对齐 gateway.py）
            └── MainActivity.kt  连接状态、日志、选设备
```
