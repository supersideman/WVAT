# LVGL Pro 项目骨架（v1 · 已加中文字体注册）

## 一、先解决中文方框（30 秒）

编辑器里中文显示方块 = **项目里没有中文字体**。需要在 `fonts/` 放一个 TTF。

**手动复制**（我这边命令行暂时坏了，跑不了）：

```powershell
New-Item -ItemType Directory -Force "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\03_design\lvgl-pro\fonts" | Out-Null
Copy-Item C:\Windows\Fonts\simhei.ttf "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\03_design\lvgl-pro\fonts\"
```

或者资源管理器里：`C:\Windows\Fonts` → 找 `simhei.ttf`（黑体）→ 复制到 `design\lvgl-pro\fonts\` 里。

放好后重新加载项目，中文就出来了。

> `globals.xml` 里已经按官方语法注册了三档字体：
> `<bin as_file="false" name="cn16" src_path="fonts/simhei.ttf" size="16" bpp="4" range="0x20-0x7F" symbols="…"/>`
> `symbols` 只收录本 UI 用到的汉字（约 90 个），不会生成上万字形撑爆 Flash。

## 二、为什么「做出来和 mockup 不一样」——4 个原因，都是预期内的

| # | 原因 | 能不能修 |
|---|---|---|
| 1 | **字体**：之前用的是 LVGL 默认 Montserrat，没有中文；字号也只是默认 14px | ✅ 放字体 + 挂 `text_font` 就修好了 |
| 2 | **图标全没了**：mockup 里的音符/月亮/文件夹是我用代码画的几何图形；XML 里我只放了文字标签 | 🟡 要么用图片资源，要么用符号字体，都不难但需要额外资源 |
| 3 | **LVGL 默认主题会加自己的内边距/边框**，而我的绝对坐标是按"裸样式"算的 | 🟡 加 `pad_all="0"` 之类的复位即可，但要逐项调 |
| 4 | **对齐方式**：mockup 里是精确居中，XML 里 `width` + `text` 默认左对齐 | 🟡 加 `text_align="center"` |

**结论：框架是对的，差的是"资源 + 微调"这一步。**

## 三、⚠️ 但我要提一个更根本的建议

**别在 LVGL Pro 里追求 1:1 复刻。**

因为：**Pro 生成的是 LVGL v9 的代码，你真机是 8.2**。
你在 Pro 里调到像素级完美，最后还是要**我另写一份 8.2 的 C 代码**——等于做两遍。

**更划算的分工**：

| 工具 | 负责什么 |
|---|---|
| **LVGL Pro** | 定**布局决策**：哪个元素多大、放哪、信息层级、扫视顺序 |
| **mockup + 我的 C 代码** | 负责**视觉细节**：图标、字体、配色、圆角、动效 |

也就是说：Pro 里"位置对、大小对"就够了，颜色字体图标这些不必在 Pro 里死磕。

## 四、文件

| 文件 | 作用 |
|---|---|
| `project.xml` | 240×280 显示配置、LVGL 版本、主题 |
| `globals.xml` | **字体注册** + 全局配色样式 |
| `screens/home.xml` | 主界面 B 版（时钟/日期/最近结果/4 快捷格/大按钮） |

## 五、接下来

- 字体放好后**告诉我中文出没出来**
- 出来之后我把剩下 7 个页面（锁屏/抽屉/录音/识别/确认/成功/失败/设置）补齐
- 你调布局 → 我把坐标同步回 `UI设计规格.md` → 最后写 8.2 的 C 代码
