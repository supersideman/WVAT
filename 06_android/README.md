# WVAT Go · 安卓手机网关

把 `02_gateway`（Python，跑在电脑上）的职责搬到手机上：
**手表通过蓝牙连这台手机，手机就是原来的那台电脑。**

> 参考实现：`ccch1mneyyy/dsh-Go`（DSH 的安卓客户端）。只借鉴分层思路，
> 代码全部自写——它做的是"AI 对话客户端"，我们要的是"蓝牙网关"。

---

## 实测状态

**已在一台华为 P40（HarmonyOS 4.0 / 底层 Android 10）上跑通全链路：**

```
18:14:07  →TX OV+EV=TIME,18:14:07,2026-11-21     ← 授时
18:14:07  ←RX OV+PONG                            ← 手表回应，双向通
18:14:09  →TX OV+EV=WX,0,4   → 手表显示"晴 4℃"    ← 天气
18:15:04  ←RX OV+AI=1,A02    → 手机切到下一首      ← 动作
18:15:51  ←RX OV+AI=6,A04    → 音量 0%→6%→…→60%  ← 音量
18:15:57  ←RX OV+AI=11,A03   → 已静音             ← 我困了
```

手表端同步变化：右上角链路点 `U`（USB）→ `B`（蓝牙）、时间自动校准、日期页显示天气。

---

## 技术选型

| 项 | 选择 | 理由 |
|---|---|---|
| 语言 | Kotlin（**AGP 9.5.1 内置，无需 kotlin 插件**）| 零额外版本冲突 |
| UI | 系统原生控件代码构建（不用 Compose / AppCompat）| 依赖少、构建快 |
| 蓝牙 | `BluetoothSocket` + SPP UUID（经典蓝牙）| HC-05 走的就是 SPP |
| 天气 | `HttpURLConnection` + open-meteo（免 key）| 不引第三方库 |
| 动作 | `AudioManager` / `Intent` / `AlarmClock` | 全是系统 API |

**零第三方依赖**——`app/build.gradle.kts` 里的 AndroidX 是模板自带，我们的代码没用到。

---

## 构建

### 方式一：Android Studio（推荐）

```
File → Open → 选这个 06_android 目录
等 Gradle sync 完成 → 点 ▶ Run
```

环境要求（本工程已在下面这套上验证过）：

```
Android Studio AI-262（2026 版，内置 JDK 25）
AGP 9.5.1 · Gradle 9.6.0 · compileSdk 37（Android 17）
minSdk 24 · targetSdk 37
```

⚠️ 首次打开会生成 `local.properties`（里面是 SDK 路径），**这个文件不要入库**。

### 方式二：命令行（我验证过可用）

```powershell
$env:JAVA_HOME = 'D:\Android\jbr'          # 用 Android Studio 自带的 JDK
cd 06_android
.\gradlew.bat assembleDebug
# 产物：app/build/outputs/apk/debug/app-debug.apk
```

---

## 手机侧准备

```
① 设置 → 关于手机 → 连点「版本号」7 次 → 开发者选项
② 开发者选项 → USB 调试 = 开
   （华为还要开「仅充电模式下允许 ADB 调试」，否则不弹授权框）
③ 插 USB → 通知栏把「仅充电」改成【传输文件】→ 手机上点【允许 USB 调试】
④ 系统蓝牙里配对 HC-05（配对码 1234）
⑤ 打开 WVAT Go → 点【连接手表】→ 选 HC-05
```

---

## 排错：远程看日志

App 里所有日志同时打到系统日志，**不用截图**：

```powershell
adb logcat -s WVAT:V
```

输出形如：

```
I WVAT : 已连接：HC-05
I WVAT : 网关已启动：连上即授时 · 20 秒心跳 · 5 分钟天气
D WVAT : →TX OV+EV=TIME,18:14:07,2026-11-21
D WVAT : ←RX OV+PONG
D WVAT : ←RX OV+AI=1,A02
I WVAT : → OK 已切到下一首
```

`→TX` 是发给手表的原始帧，`←RX` 是从手表收到的原始帧。

---

## 动作映射（手机 vs 电脑的能力差异）

| 码 | 动作 | 手机实现 | 与 PC 的差异 |
|---|---|---|---|
| A01 | 播放/暂停 | `dispatchMediaKeyEvent` | 一致 |
| A02 | 下一首 | 同上 | 一致 |
| A03 | 我困了 | **只能静音** | PC 能锁屏+关屏；手机要设备管理员权限 |
| A04/A05 | 音量 ± | `AudioManager` | 一致 |
| A06 | 打开资料 | 打开系统"文档"界面 | PC 打开文件夹，**语义变化** |
| A07 | 查状态 | 音量 + 天气 | 一致 |
| A08 | 语音 | **未实现**（二期：手机麦克风 + 识别）| — |

---

## 已知限制与后续

| 限制 | 原因 | 计划 |
|---|---|---|
| 息屏后可能被系统杀掉 | 没有前台服务 | 二期加 `ForegroundService` |
| `A08 语音` 返回 ERR | 语音未接 | 二期 |
| 自由文本 `T,...` 返回 ERR | AI 兜底未接 | 三期接 LLM |
| 城市写死北京 | 没有设置页 | 二期加设置页（改 `Weather.kt` 的 lat/lon）|
| 手表断电后时间归零 | FryPi **没有 VBAT 电池**，RTC 备份域掉电就清 | 不是 App 的问题；连上手机 30 秒内自动对时 |

---

## 目录

```
06_android/
├── settings.gradle.kts / build.gradle.kts / gradle.properties
├── gradle/libs.versions.toml          ← AGP 版本集中在这里
├── gradle/wrapper/                    ← Gradle 9.6.0
└── app/
    ├── build.gradle.kts
    └── src/main/
        ├── AndroidManifest.xml        ← 蓝牙/网络权限 + MainActivity
        ├── res/                        ← 图标与主题（模板生成）
        └── java/com/wvat/go/
            ├── OvProtocol.kt   OV+ 帧解析与生成（对齐 protocol.py）
            ├── BtLink.kt       蓝牙 SPP 连接/收发/断线
            ├── Weather.kt      open-meteo 取天气（对齐 weather.py）
            ├── Actions.kt      A01~A07 的动作执行
            ├── GatewayCore.kt  授时/心跳/天气/去重/分发（对齐 gateway.py）
            └── MainActivity.kt 连接状态、日志、选设备
```
