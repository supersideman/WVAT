# 05 · 工程脚本

- `wire_link.ps1` **主要脚本**：把 `01_firmware/ui/*.c|h` 同步进 Keil 工程，自动补 uvprojx 编译组、改 `ui.c`，带备份
- `fix_keil_project.ps1` 首次接入用（改 lv_conf 字体开关、替换模板 UI）
- `add_font44.ps1` 把 44 号字体源文件加入工程
- `watch_git_history.cmd` 双击查看彩色提交历史

PowerShell 执行策略限制时用：`powershell -ExecutionPolicy Bypass -File <脚本>`