# 当前进度与下一步（从这里接着干）

> 最后更新：2026-10-30 · 待打标签 `v0.5-verified`

## 一句话状态

**手表有了完整的 UI 外壳（锁屏 → 跟手滑动解锁 → 主界面 → 抽屉），8 个动作码 A01~A08 全部在实机双向跑通，而且动作真的作用到系统上了。**

---

## 已验证通过（有实测数据，不是"应该能"）

| # | 能力 | 状态 | 证据 |
|---|---|---|---|
| 1 | ST-Link 烧录 | ✅ | 多次成功烧录 |
| 2 | 串口 CH340（**口号会变**：COM6 / COM13，115200） | ✅ | 网关收发正常 |
| 3 | USB 模拟 U 盘（W25Q128） | ✅ | 枚举成 U 盘并可格式化 |
| 4 | 屏幕 + LVGL 8.2 渲染 | ✅ | 实机出图 |
| 5 | 触摸（CST816） | ✅ | 按下高亮、点击有响应 |
| 6 | 协议 `OV+AI` / `OV+AR` | ✅ | REQ_ID 递增、去重正常、设备复位自动清缓存 |
| 7 | **8 类意图全部路由** | ✅ | A01~A08 实机各点一遍，网关逐条回 OK |
| 8 | **真执行**：锁屏 + 关显示器 | ✅ | 点月亮真的锁屏，耗时 180~200ms |
| 9 | **按键类动作**（播放/暂停、下一首、音量±、静音） | ✅ | 修好 INPUT 结构体后实测：音量键能真正改变系统音量 |
| 10 | **抽屉页**（6 拓展意图，顶部条给反馈） | ✅ | 实机可见可用；反馈圆点三态：淡灰=已发出、绿=成功、红=失败或超时（3s） |
| 11 | **锁屏 + 跟手滑动解锁** | ✅ | 拖动 1:1 跟随，超 80px 吸附进主界面 |
| 12 | 串口掉线自动重连 | ✅ | 拔插不再让网关崩溃（2s 重试） |
| 13 | **真实时钟**（内部 RTC + PC 自动授时） | ✅ | 实机确认走时；LSE→RTC 手工链路；网关每 30s 下发 `OV+EV=TIME` |
| 14 | **录音页**（波形 + 录音键 + 中文转写 + 确认发送） | ✅ | 实机可见；文字显示完整，确认后走 `T` 慢通道执行 |
| 15 | **自生成中文字库**（14px，GB2312 一级汉字 3755 字） | ✅ | 81KB 位图；自带工具 `gen_cn_font.py` + PC 端预检 `verify_font.py` |

实测时延：查询类 0ms，真动作类 180~200ms（Win32 API 同步阻塞，属正常）。

> ⚠️ **记录更正**：第 9 项修好之前，所有"按键类动作成功"的判断都是**错的**——
> `SendInput` 静默失败，日志却打印 `[执行] ... ok`。已修正并加防呆。

---

## 界面结构（当前）

```
ui_stage（舞台，240×560 容器）
├─ 锁屏层 (y=0)      ui_lock_build   → 大时钟 + 日期，上滑 80px 解锁（长按 1.2s 兜底）
└─ 主界面层 (y=280)  ui_main_build   → 时钟/日期/回执条/♪播放 · ▶|下一首 · 田字格/麦克风按钮
                                      └ 田字格 → ui_drawer（独立屏幕）
                                         顶部条：左=返回键，右=意图图标 + 结果圆点
                                         3×2：我困了 / 音量+ / 音量− / 资料 / 查状态 / 语音
```

开机 → `ui_stage_init()` → 锁屏；抽屉返回 → `ui_stage_show_home()`。

---

## 关键实现位置

```
桌面 Keil 工程（作者模板 + 我们的代码）
└─ ...\LVGL_template\User\GUI\
   ├─ ui_theme.h     配色 + 所有坐标/阈值（改样式只改这里）
   ├─ ui_icons.c/h   13 个图标，纯图元拼，零位图/零字库
   ├─ ui_main.c/h    主界面内容（可嵌入层）+ 触摸事件 + 意图回调钩子
   ├─ ui_lock.c/h    锁屏层内容
   ├─ ui_stage.c/h   舞台：跟手滑动/吸附/锁屏
   ├─ ui_drawer.c/h  抽屉页（6 拓展意图 + 顶部反馈 + 3s 超时守卫）
   ├─ user_link.c/h  USART1 协议收发（中断收帧 + lv_timer 轮询）
   └─ ui.c           ui_init(): ui_stage_init() + link_init()

WVAT\01_firmware\ui\      以上文件的源版本（改动从这里同步过去）
WVAT\02_gateway\    gateway.py / fake_watch.py / serial_log.py / intents.py
WVAT\tools\*.ps1       接入脚本（同步源码 + 改 uvprojx + 改 ui.c，带备份）
WVAT\03_design\           render_c_preview.py（解析 ui_theme.h 出预览，图=代码）
```

**改完代码同步进工程**：
```powershell
powershell -ExecutionPolicy Bypass -File WVAT\05_scripts\wire_link.ps1
```

---

## 踩过的坑（别再踩一遍）

| 坑 | 现象 | 解法 |
|---|---|---|
| **网关必须用户自己启动** | 播放/暂停、下一首点了没反应 | 代启的进程（沙箱/高权限宿主）**没有输入注入权限**：`SendInput` 返回 2 但输入进不了桌面。**必须用户在自己终端启动网关**：双击 `02_gateway\start_gateway.cmd` |
| **音量别用模拟按键** | 音量键时灵时不灵、无法验证 | 改用 CoreAudio 直接设音量（`audio_win.py`），能回读百分比，回执带真实数字 |
| **`.cmd` 必须 CRLF + ASCII** | 双击窗口一闪就退，报 `'le' is not recognized` | 批处理文件必须 **CRLF 换行 + 纯 ASCII**；LF 会被 cmd 拆错行 |
| 中文注释吞行 | 宏"未声明"、编译莫名失败 | 源文件必须 **UTF-8 带 BOM**；编辑工具会剥 BOM，改完要补 |
| **`SendInput` 静默失败** | 日志说"执行 ok"，系统毫无反应 | `INPUT` 的 union 必须含 `mi/ki/hi` 三个成员，x64 上 `sizeof` = **40**；并且**一定要检查返回值** |
| **`Set-Content` 毁中文** | .md 变成 `?` / 非法 UTF-8 | PowerShell 5.1 默认 ANSI；写文本用 `[System.IO.File]::WriteAllText(..., UTF8Encoding)` |
| `remove_style_all` 清坐标 | 主界面层跑到 y=0 盖住锁屏；切页后黑屏 | `set_pos/set_size` 写的是样式属性；**先 build 再 set_pos** |
| `lv_obj_get_width/get_y` 恒为 0 | 调试数据误导排查 | 读之前先 `lv_obj_update_layout()` |
| 去重表跨会话失效 | 复位后前 N 条指令被当成重传、静默吞掉 | REQ_ID 回退或心跳超时即清空去重表 |
| `Get-ChildItem -Include` 静默失效 | 补 BOM 的循环"跑了但没做事" | 改用 `Where-Object { $_.Extension -in ... }` |
| 小尺寸描边图形不可读 | "下一首"像两根细线、喇叭像大于号 | 用**实心台阶**拼形状（LVGL 无填充三角形图元） |
| 只开 `lv_conf` 不够 | `L6218E: Undefined symbol lv_font_montserrat_44` | 字体 `.c` 还得**加进 Keil 编译组** |
| `Target not created` + 1 秒 | 后面跟 `Flash Download failed` | **真错误在前面**，往上翻日志找 `Error:` |
| CubeMX 没开 USART1 中断 | 能发不能收，回执不动 | 在 `user_link.c` 里补 `USART1_IRQHandler` + `HAL_NVIC_EnableIRQ` |
| Keil 里没加 .c 文件 | 文件在文件夹里但没被编译 | 加进 `.uvprojx` 的 Group（脚本已自动化） |
| COM 口号会变 | 网关报"打不开串口 COM6" | 拔插后重新枚举（COM6→COM13），每次先查再传参 |
| 演练模式长得像真执行 | 以为改了系统，其实没动 | 启动参数要 `--enable-actions`；日志里 `[执行]` vs `[演练]` 要分清 |

---

## 接下来可以做的

| 顺序 | 任务 | 预估 | 说明 |
|---|---|---|---|
| ~~1~~ | ~~真实时钟（RTC）~~ | — | ✅ 已完成 |
| ~~2~~ | ~~录音页 + 中文转写 + 确认发送~~ | — | ✅ 已完成（识别为假数据，链路真实） |
| 3 | **接真麦克风**（I2S 数字麦 → 真实波形 + 音频上行） | 2~3 天 | 需硬件：INMP441 / ICS-43434 接 PB12/PB13/PB15；串口要提到 460800 |
| 4 | **接真 ASR**（网关侧 本地模型 或 云端） | 2~3 天 | 依赖第 3 项；模型建议 sherpa-onnx / SenseVoice-small |
| 5 | 音量回读上屏 | 半天 | 让 `A07 查状态` 报真实音量（CoreAudio 已能读） |
| 6 | 语音历史列表 / 多轮对话 | 2 天 | 锦上添花 |

> **注意**：`ui_status.c`（全屏三态反馈）目前**没有被调用**——录音页自己做了状态显示。
> 它适合放"发送后的结果"或者将来的"唤醒/超时"提示，暂时保留备用。

**另外**：现在这套能真锁屏、真放歌、真调音量、有完整 UI 外壳 + 录音页，是录答辩素材的最佳状态——**建议尽快录一段**。

---

## 恢复现场

**日常用法：双击这个就行**

```
WVAT\02_gateway\start_gateway.cmd
```

它会自动找 CH340 串口、以真执行模式启动。**必须由你自己双击**（见坑清单第一条：代启进程没有输入注入权限）。

手动方式（需要指定口号/演练模式时）：

```powershell
# 1. 看进度
cd WVAT; git log --oneline; git tag -l

# 2. 查口号（会变！）
[System.IO.Ports.SerialPort]::GetPortNames()

# 3. 手动启网关（真执行）
$env:PYTHONPATH="C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\vendor\_pylibs"
python WVAT\02_gateway\gateway.py --transport serial --serial-port COM13 --enable-actions

# 4. 只演练不动系统：去掉 --enable-actions
# 5. 用启动器演练：start_gateway.cmd dry
```
