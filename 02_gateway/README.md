# 02 · PC 端网关

- `start_gateway.cmd` **一键启动（日常就用这个）**：自动找 CH340 串口，真执行模式
- `gateway.py` 主循环：收帧 → 去重 → 路由 → 执行 → 应答
- `protocol.py` OV+ 协议解析与组帧
- `transport.py` 套接字 / 串口两种通道（串口掉线自动重连）
- `intents.py` 白名单动作执行（锁屏、媒体键、资料）
- `audio_win.py` 音量与静音（CoreAudio，可回读）
- `fake_watch.py` 无硬件时的模拟手表
- `diagnostics/` 排查脚本（按键注入自检、会话诊断、SMTC 试验）

演练模式（不动系统）：`start_gateway.cmd dry`