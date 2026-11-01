# 01 · 手表端固件

- `ui/` 本项目自己的 UI 源码（9 个文件：theme / icons / main / lock / stage / drawer / link）
- `keil/LVGL_template/` 完整 Keil 工程，直接用 Keil 打开 `MDK-ARM/LCD.uvprojx` 编译烧录
- `ui/INTEGRATE.md` 接入步骤与已验证的 Keil 设置

改完 `ui/` 里的源码后，运行 `05_scripts/wire_link.ps1` 同步进 Keil 工程。