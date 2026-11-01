# 当前进度与下一步（从这里接着干）

> 最后更新：2026-10-29 深夜 · 对应 git tag `v0.4-ui-shell`

## 一句话状�?
**手表有了完整�?UI 外壳（锁�?�?跟手滑动解锁 �?主界�?�?抽屉），并且能真正控制电脑�?*
8 个动作码 A01~A08 全部在实机双向验证通过�?
---

## 已验证通过（有实测数据，不�?应该�?�?
| # | 能力 | 状�?| 证据 |
|---|---|---|---|
| 1 | ST-Link 烧录 | �?| 多次成功烧录 |
| 2 | 串口 CH340�?*口号会变**：COM6→COM13, 115200�?| �?| 网关收发正常 |
| 3 | USB 模拟 U 盘（W25Q128�?| �?| 枚举�?U 盘并可格式化 |
| 4 | 屏幕 + LVGL 8.2 渲染 | �?| 实机出图 |
| 5 | 触摸（CST816�?| �?| 按下高亮、点击有响应 |
| 6 | 协议 `OV+AI` / `OV+AR` | �?| REQ_ID 递增、去重正�?|
| 7 | **8 类意图全部路�?* | �?| A01~A08 实机各点一遍，网关逐条�?OK |
| 8 | **真执�?*：锁�?+ 静音 + 关显示器 | �?| 点月亮真的锁屏，耗时 180~200ms |
| 9 | 媒体键（播放/暂停/下一首） | �?| `[执行] A01 ok` / `A02 ok` |
| 10 | **抽屉�?*�? 意图 + 返回�?×3�?| �?| 实机可见可用 |
| 11 | **锁屏 + 跟手滑动解锁** | �?| 拖动 1:1 跟随，超 80px 吸附进主界面 |
| 12 | 串口掉线自动重连 | �?| 拔插不再让网关崩溃（2s 重试�?|

实测时延：查询类 0ms，真动作�?180~200ms（Win32 API 同步阻塞，属正常）�?
---

## 界面结构（当前）

```
ui_stage（舞台，240×560 容器�?├─ 锁屏�?(y=0)      ui_lock_build   �?大时�?+ 日期，上�?80px 解锁
└─ 主界面层 (y=280)  ui_main_build   �?时钟/日期/回执�?3 �?麦克风按�?                                      �?田字�?�?ui_drawer（独立屏幕，3×3�?```

开�?�?`ui_stage_init()` �?锁屏；抽屉返�?�?`ui_stage_show_home()`�?

---

## 关键实现位置

```
桌面 Keil 工程（作者模�?+ 我们的代码）
└─ ...\LVGL_template\User\GUI\
   ├─ ui_theme.h     配色 + 所有坐�?阈值（改样式只改这里）
   ├─ ui_icons.c/h   13 个图标，纯图元拼，零位图/零字�?   ├─ ui_main.c/h    主界面内容（可嵌入层�? 触摸事件 + 意图回调钩子
   ├─ ui_lock.c/h    锁屏层内容（大时�?+ 日期�?   ├─ ui_stage.c/h   **舞台**�?40×560 容器，跟手滑�?吸附/锁屏
   ├─ ui_drawer.c/h  抽屉页（独立屏幕�?×3�? 意图 + 返回�?   ├─ user_link.c/h  USART1 协议收发（中断收�?+ lv_timer 轮询�?   └─ ui.c           ui_init() 里调 ui_stage_init() + link_init()

WVAT\firmware\ui\      以上文件的源版本（改动从这里同步过去�?WVAT\tools\ai-link\    PC 端网�?gateway.py / 模拟手表 fake_watch.py / 串口日志 serial_log.py
WVAT\tools\*.ps1       接入脚本（拷�?+ �?uvprojx + �?ui.c，带备份�?WVAT\design\           render_c_preview.py 渲染预览（直接解�?ui_theme.h，图=代码�?```

**改完代码同步进工程的方法**�?```powershell
powershell -ExecutionPolicy Bypass -File WVAT\tools\wire_link.ps1
```

---

## 踩过的坑（别再踩一遍）

| �?| 现象 | 解法 |
|---|---|---|
| 中文注释吞行 | �?未声�?、编译莫名其妙失�?| **源文件必须存�?UTF-8 �?BOM**；编辑工具会�?BOM，改完要�?|
| `Get-ChildItem -Include` 静默失效 | �?BOM 的循�?跑了但没做事"，编译照旧报�?| 不加 `-Recurse`/通配符路径时 `-Include` 匹配不到任何文件；改�?`Where-Object { $_.Extension -in ... }` |
| 只开 `lv_conf` 不够 | `L6218E: Undefined symbol lv_font_montserrat_44` | 字体 `.c` 还得**加进 Keil 工程编译�?* |
| `Target not created` + 1 �?| 后面�?`Flash Download failed` | **真错误在前面**，往上翻日志�?`Error:` |
| CubeMX 没开 USART1 中断 | 能发不能收，回执条不�?| �?`user_link.c` 里补 `USART1_IRQHandler` + `HAL_NVIC_EnableIRQ` |
| **`remove_style_all` 清掉坐标** | 主界面层跑到 y=0 盖住锁屏；切页后黑屏 | `lv_obj_set_pos/set_size` 写的�?*样式属�?*；必�?*�?build �?set_pos** |
| `lv_obj_get_width/get_y` 恒为 0 | 调试日志误导排查方向 | 读之前先 `lv_obj_update_layout()`，否则读的是未计算的 coords |
| `lv_line` 共用点数�?| 对勾只画一�?| 每条线段用独�?static 数组 |
| 小尺寸描边图形不可读 | "下一�?像两根细线、喇叭像大于�?| �?*实心台阶**拼形状（LVGL 无填充三角形图元�?|
| Keil 里没�?.c 文件 | 文件在文件夹里但没被编译 | 加进 `.uvprojx` �?Group（脚本已自动化） |
| COM 口号会变 | 网关�?打不开串口 COM6" | 拔插后重新枚举（COM6→COM13），**每次先查再传�?* |

---

## 接下来可以做的（按建议优先级�?
| 顺序 | 任务 | 预估 | 说明 |
|---|---|---|---|
| 1 | **抽屉页细节确�?打磨** | 视需�?| 你现在正在做这个 |
| 2 | **真实时钟**（RTC �?`ui_main_set_time` / `ui_lock_set_time`�?| 半天 | 现在两边显示的都是写死的 `10:24` |
| 3 | 状态页（录音中/成功/失败全屏�?| 1 �?| 设计稿已出（`design/ui-mockups/`�?|
| 4 | **语音输入**（I2S �?+ ASR�?| 3~5 �?| 大头，需先接�?PB12/PB13/PB15 |

**另外**：现在这套能真锁屏、真放歌、有完整 UI 外壳，是录答辩素材的最佳状态—�?*建议尽快录一段留�?*�?
---

## 恢复现场的三个命�?
```powershell
# 1. 看进�?历史
cd WVAT; git log --oneline; git tag -l

# 2. 启动网关（真执行�?$env:PYTHONPATH="C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\tools\_pylibs"
python WVAT\tools\ai-link\gateway.py --transport serial --serial-port COM13 --enable-actions

# 3. 只想演练不真动系统，去掉 --enable-actions
```
