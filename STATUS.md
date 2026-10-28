# 当前进度与下一步（明天从这里接着干）

> 最后更新：2026-10-29 晚 · 对应 git tag `v0.3-link-live`

## 一句话状态

**手表能真正控制电脑了。** 点屏幕上的意图格 → 串口上行 → PC 执行 → 回执下行 → 屏幕显示结果。

---

## 已验证通过（有实测数据，不是"应该能"）

| # | 能力 | 状态 | 证据 |
|---|---|---|---|
| 1 | ST-Link 烧录 | ✅ | 多次成功烧录 |
| 2 | 串口 CH340（COM6, 115200） | ✅ | 网关收发正常 |
| 3 | USB 模拟 U 盘（W25Q128） | ✅ | 枚举成 U 盘并可格式化 |
| 4 | 屏幕 + LVGL 8.2 渲染 | ✅ | 实机出图 |
| 5 | 触摸（CST816） | ✅ | 按下高亮、点击有响应 |
| 6 | 协议 `OV+AI` / `OV+AR` | ✅ | REQ_ID 递增、去重正常 |
| 7 | 四类意图路由 | ✅ | A01 放歌 / A03 我困了 / A07 查状态 / A08 语音占位 |
| 8 | **真执行**：锁屏 + 静音 + 关显示器 | ✅ | 点月亮真的锁屏，耗时 180~200ms |
| 9 | 媒体键（播放/暂停） | ✅ | `[执行] A01 ok` |

实测时延：查询类 0ms，真动作类 180~200ms（Win32 API 同步阻塞，属正常）。

---

## 关键实现位置

```
桌面 Keil 工程（作者模板 + 我们的代码）
└─ ...\LVGL_template\User\GUI\
   ├─ ui_theme.h     配色 + 所有坐标（改样式只改这里）
   ├─ ui_icons.c/h   12 个图标，纯图元拼，零位图/零字库
   ├─ ui_main.c/h    主界面 + 触摸事件 + 意图回调钩子
   ├─ user_link.c/h  USART1 协议收发（中断收帧 + lv_timer 轮询）
   └─ ui.c           ui_init() 里调 ui_main_create() + link_init()

WVAT\firmware\ui\      以上文件的源版本（改动从这里同步过去）
WVAT\tools\ai-link\    PC 端网关 gateway.py + 模拟手表 fake_watch.py
WVAT\tools\*.ps1       接入脚本（拷贝 + 改 uvprojx + 改 ui.c，带备份）
```

**改完代码同步进工程的方法**：
```powershell
powershell -ExecutionPolicy Bypass -File WVAT\tools\wire_link.ps1
```

---

## 踩过的坑（别再踩一遍）

| 坑 | 现象 | 解法 |
|---|---|---|
| 中文注释吞行 | 宏"未声明"、编译莫名其妙失败 | **源文件必须存成 UTF-8 带 BOM**；编辑工具会剥 BOM，改完要补 |
| 只开 `lv_conf` 不够 | `L6218E: Undefined symbol lv_font_montserrat_44` | 字体 `.c` 还得**加进 Keil 工程编译组** |
| `Target not created` + 1 秒 | 后面跟 `Flash Download failed` | **真错误在前面**，往上翻日志找 `Error:` |
| CubeMX 没开 USART1 中断 | 能发不能收，回执条不动 | 在 `user_link.c` 里补 `USART1_IRQHandler` + `HAL_NVIC_EnableIRQ` |
| `lv_line` 共用点数组 | 对勾只画一笔 | 每条线段用独立 static 数组 |
| Keil 里没加 .c 文件 | 文件在文件夹里但没被编译 | 加进 `.uvprojx` 的 Group |

---

## 明天可以接着做的（按建议优先级）

| 顺序 | 任务 | 预估 | 说明 |
|---|---|---|---|
| 1 | **「更多」抽屉页**（9 个意图摊开） | 半天 | 现在只有 4 个可点 |
| 2 | **锁屏页 + 上滑解锁** | 半天 | 最早提的需求，"手表感"关键 |
| 3 | **真实时钟**（RTC → `ui_main_set_time`） | 半天 | 现在显示的是写死的 `10:24` |
| 4 | 状态页（录音中/成功/失败全屏） | 1 天 | 设计稿已出，照实现即可 |
| 5 | **语音输入**（I2S 麦 + ASR） | 3~5 天 | 大头，需先接线 PB12/PB13/PB15 |

**另外**：现在这套能真锁屏、真放歌，是录答辩素材的最佳状态——**建议尽快录一段留证**。

---

## 恢复现场的三个命令

```powershell
# 1. 看进度/历史
cd WVAT; git log --oneline; git tag -l

# 2. 启动网关（真执行）
$env:PYTHONPATH="C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\tools\_pylibs"
python WVAT\tools\ai-link\gateway.py --transport serial --serial-port COM6 --enable-actions

# 3. 只想演练不真动系统，去掉 --enable-actions
```
