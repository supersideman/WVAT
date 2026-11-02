# 裁剪 lv_conf.h：关掉本项目完全没用到的 LVGL 控件，为字库腾 Flash
# 只保留我们真正用到的：obj（基类）、label、line
$ErrorActionPreference = 'Stop'

$conf = "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\01_firmware\keil\LVGL_template\Middlewares\LVGL\lv_conf.h"
$bak = "$conf.bak_trim"

if (-not (Test-Path $conf)) { throw "找不到 lv_conf.h：$conf" }
if (-not (Test-Path $bak)) { Copy-Item $conf $bak -Force; Write-Output "[0] 已备份 → $bak" }

# 需要关闭的控件（本项目零使用）
$off = @(
    'LV_USE_ARC', 'LV_USE_ANIMIMG', 'LV_USE_BAR', 'LV_USE_BTN', 'LV_USE_BTNMATRIX',
    'LV_USE_CANVAS', 'LV_USE_CHECKBOX', 'LV_USE_DROPDOWN', 'LV_USE_IMG',
    'LV_USE_ROLLER', 'LV_USE_SLIDER', 'LV_USE_SWITCH', 'LV_USE_TEXTAREA',
    'LV_USE_TABLE', 'LV_USE_CALENDAR', 'LV_USE_CHART', 'LV_USE_COLORWHEEL',
    'LV_USE_IMGBTN', 'LV_USE_KEYBOARD', 'LV_USE_LED', 'LV_USE_LIST', 'LV_USE_MENU',
    'LV_USE_METER', 'LV_USE_MSGBOX', 'LV_USE_SPINBOX', 'LV_USE_SPINNER',
    'LV_USE_TABVIEW', 'LV_USE_TILEVIEW', 'LV_USE_WIN', 'LV_USE_SPAN',
    'LV_USE_THEME_BASIC', 'LV_USE_THEME_MONO', 'LV_USE_FLEX', 'LV_USE_GRID',
    'LV_USE_ANIMIMG', 'LV_USE_SNAPSHOT', 'LV_USE_FS_STDIO', 'LV_USE_FS_POSIX',
    'LV_USE_FS_WIN32', 'LV_USE_SJPG', 'LV_USE_PNG', 'LV_USE_BMP', 'LV_USE_GIF',
    'LV_USE_QRCODE', 'LV_USE_FREETYPE', 'LV_USE_RLOTTIE', 'LV_USE_FRAGMENT'
)

$bytes = [System.IO.File]::ReadAllBytes($conf)
$hasBom = ($bytes[0] -eq 0xEF)
$text = [System.Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)

$changed = 0
foreach ($name in $off) {
    $pattern = "(?m)^(\s*#define\s+$name)\s+1\b"
    $new = [regex]::Replace($text, $pattern, "`$1 0")
    if ($new -ne $text) { $changed++; $text = $new }
}

[System.IO.File]::WriteAllText($conf, $text, (New-Object System.Text.UTF8Encoding($hasBom)))
Write-Output "[1] 关闭了 $changed 个未使用的控件/特性"

Write-Output "`n=== 现在仍启用的控件 ==="
Select-String -Path $conf -Pattern "^#define LV_USE_\w+\s+1\b" | ForEach-Object { "  " + ($_.Line.Trim() -replace '\s+', ' ') }
