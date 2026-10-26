# ST-Link 到货后的烧录流程（FryPi + LVGL 8.2）

> 目标：**第一次就能点亮**，别在接线和工程配置上浪费时间。
> 模板来自作者仓库 `2.software/2.Advanced/6.LVGL-SmartWatch/LVGL_template`（已核实结构）

---

## 进度（2026-10-20 更新）

| # | 验证项 | 状态 |
|---|---|---|
| 1 | ST-Link 连接、烧录通路 | ✅ **已通**（作者上机测试跑通） |
| 2 | 串口通信（CH340N） | ✅ **已通**（上位机能收到模式信息） |
| 3 | USB 模拟 U 盘（W25Q128） | ✅ **已通**（枚举成 U 盘、已格式化） |
| 4 | **屏幕点亮 + LVGL 渲染** | ⬜ **下一步**（在等这一步） |
| 5 | 触摸（CST816） | ⬜ 待验证 |
| 6 | 换上本项目 UI | ⬜ 待验证 |

> ⚠️ 注意：第 1~3 项验的是**作者的上机测试例程**（`0.template`：L2 闪烁 + 按键切模式 + 上位机），
> **不包含屏幕和 LVGL**。所以「能烧、能串口、能 U 盘」不等于「屏能出图」。

> 💡 顺带收获：U 盘能格式化说明板载 **W25Q128（16MB）** 的 USB MSC 是通的。
> 以后背景图、字库、日志都可以放这里，不用挤 512KB 内部 Flash。

---

## 第 0 步：拿模板工程

```powershell
# 只要这一个文件夹。建议走 codeload（raw.githubusercontent 在国内会被 reset）
# 下载后从 zip 里取出：2.software/2.Advanced/6.LVGL-SmartWatch/LVGL_template
```

如果仓库太大下不动，也可以只解压出 `LVGL_template` 这一层，其余丢掉。

## 第 1 步：改 `lv_conf.h`（**两处，不改编不过**）

路径：`LVGL_template/Middlewares/LVGL/lv_conf.h`

| 行号附近 | 现在 | 改成 | 原因 |
|---|---|---|---|
| L346 | `#define LV_FONT_MONTSERRAT_44 0` | **`1`** | 时钟用 44 号数字 |
| L331 | `#define LV_FONT_MONTSERRAT_14 1` | 已是 1，**不用动** | 日期用 14 号 |

已核实的其他设置（**不用改**）：
- `LV_COLOR_DEPTH 16` ✅ 与代码假设一致
- `LV_MEM_SIZE (48U * 1024U)` ✅ 48KB，够（我的 UI 约 45 个对象，占 ~6KB）
- `LV_DISP_DEF_REFR_PERIOD 30` ✅

> ⚠️ 44 号字体大约多占 **30~40KB Flash**。FryPi 的 Flash 512KB 基本是空的（不像 OV-Watch 被 APP 吃满），放得下。

## 第 2 步：拷 4 个文件

把本仓库 `firmware/ui/` 下的文件拷到模板的 `User/GUI/` 里：

```
ui_theme.h
ui_icons.h
ui_icons.c
ui_main.h
ui_main.c
```

## 第 3 步：改 `ui_init()`（3 行）

路径：`User/GUI/ui.c`，把第 25~26 行：

```c
    ui_HomePage_screen_init();
    lv_disp_load_scr(ui_HomePage);
```

改成：

```c
    lv_obj_t *scr = ui_main_create();
    lv_disp_load_scr(scr);
```

并在文件顶部 `#include "ui.h"` 下面加一行：

```c
#include "ui_main.h"
```

> 想保留原界面做对照，就别删 `ui_HomePage` 相关代码，只把上面两行换掉即可。

## 第 4 步：Keil 工程设置（**这一步最容易错**）

| 项 | 值 |
|---|---|
| Device | **STM32F411RE**（不是 CE/VE！） |
| Target → Flash | 512KB（IROM1 起始 0x08000000） |
| Debug → Use | **ST-Link Debugger** |
| Debug → Settings → Port | **SW**（不要 JTAG，F411 这板没引 JTAG） |
| Debug → Settings → SW Device | 能看到 **IDCODE** 就算通了 |
| Reset and Run | ✅ 勾上，烧完自动跑 |

**⚠️ 还有一个必开项：C99**

`Options for Target → C/C++ → Language Standard` 选 **C99**（或勾 `--c99`）。

LVGL 8.2 的源码用到 C99 语法（复合字面量、`//` 注释混用等），Keil AC5 默认是 C90，
**不开这一项会报几百个语法错误**，然后你会以为工程坏了。

## 第 5 步：**先烧原模板验证链路**

**别一上来就烧我的 UI。** 先原样编译烧录作者的 `LVGL_template`：

- 能出图 → 说明屏幕、SPI、供电、下载器全对 ✅ 再进下一步
- 不出图 → 问题在硬件/驱动/时钟，跟 UI 代码无关

这一步能把问题范围砍一半。

## 第 6 步：换成我们的 UI

改完第 1~3 步，重新编译烧录。

---

## 接线（4 根，别插反）

```
ST-Link          FryPi 下载口
3V3   ────────   3V3
SWDIO ────────   DIO
SWCLK ────────   CLK
GND   ────────   GND
```

- FryPi 的下载口丝印就是 `GND / CLK / DIO / 3V3`（见仓库 `images/frypi-pin-map.jpg`）
- **供电走 Type-C**，ST-Link 的 3V3 只作电平参考，不要靠它带整板
- 屏用 18P FPC 插到板上的座子里，**插反会烧背光**

---

## 常见报错对照

| 现象 | 原因 | 处理 |
|---|---|---|
| `No target connected` | SWD 线序错 / 没供电 | 检查 4 根线，Type-C 供电 |
| `Cannot access target` | 芯片被读保护 / 之前烧过 BootLoader | Keil 里 `Target → Erase Full Chip` |
| 编译报 `lv_font_montserrat_44 undefined` | 忘了改 `lv_conf.h` | 回第 1 步 |
| 烧进去黑屏但能下载 | 屏初始化没跑 / 引脚不对 | 先烧原模板对照 |
| 画面撕裂/花屏 | SPI 时钟太快或杜邦线太长 | 降 SPI 分频到 4~8 |
| 只剩左上角一块 | 显示区偏移不对 | 改 `lv_port_disp.c` 的 `hor_res/ver_res` 和 LCD 偏移 |
| 重启后黑屏 | 没勾 Reset and Run | 手动按复位 |

---

## 我会在那边等的

烧完把**实机照片**发我（或截图），我按真机效果调：

- 图标大小/位置（改 `ui_theme.h` 一个数，图和代码同步更新）
- 颜色（LVGL 在真实 IPS 上的观感会比预览更亮/更艳，可能要压暗）
- 刷新是否卡顿（`lv_timer_handler` 返回时间、SPI 分频）
