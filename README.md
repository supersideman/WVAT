# 个人 AI 便携语音控制终端（WVAT）

本科毕业设计 · 大连大学 自动化

> 按住手表说一句话 → 云端 AI 理解意图 → 调度电脑执行 → 结果回传屏幕。

**它不是智能手表，是戴在手上的 AI 遥控器。**

## 目录

| 路径 | 内容 |
|---|---|
| [`research/`](research/) | 立项书、可行性评估、协议与交互设计文档 |
| [`tools/ai-link/`](tools/ai-link/) | P0 联调工具：模拟手表 + PC 网关（纯标准库，无需硬件） |

先看 [立项书-开发需求与流程.md](research/立项书-开发需求与流程.md) —— 那是总纲。

## 快速开始

```powershell
cd tools\ai-link
python gateway.py                    # 终端 A：启动网关（演练模式，不动系统）
python fake_watch.py --demo --yes    # 终端 B：跑自动演示
```

详见 [tools/ai-link/README.md](tools/ai-link/README.md)。

## 系统架构（三层，职责不重叠）

| 角色 | 承担 | 不承担 |
|---|---|---|
| 终端（手表） | 语音采集、意图触发、状态显示、本地降级 | 不联网、不解析、不执行 |
| 代理（PC 网关） | ASR、意图解析、白名单调度、执行、语音播报 | 不做界面渲染 |
| 认知（云端 LLM） | 模糊语义 → 结构化动作 | **绝不直接执行任何操作** |

**双通道意图路由**：白名单命中走本地快通道（毫秒级、断网可用）；未命中才走慢通道（LLM 理解）。LLM 只输出 JSON 动作码，绝不生成命令。

## 开源合规

本课题基于以下 GPL-3.0 开源项目二次开发，在此致谢：

- [No-Chicken/OV-Watch](https://github.com/No-Chicken/OV-Watch) —— 手表软件参考实现
- [No-Chicken/FryPi](https://github.com/No-Chicken/FryPi) —— 硬件平台与例程

本仓库中的**增量部分**（通信协议、双通道路由、动作白名单、网关实现、交互与反馈设计）为本人设计实现。原项目版权归原作者所有。

## 硬件平台

FryPi / STM32F411RET6（512KB Flash / 128KB SRAM，Cortex-M4F）。
选型依据：语音需 I2S，`PB12(WS) / PB13(CK) / PB15(SD)` 空闲且已引出 —— 详见 [FryPi 评估](research/FryPi-毕设方案评估.md)。

---

> 注：本工作区还存放着一些与本项目无关的临时目录，未纳入版本管理。
