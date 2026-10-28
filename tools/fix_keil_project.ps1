# 把本项目的 LVGL UI 接入作者的 LVGL_template 工程
# 用法：pwsh -File tools/fix_keil_project.ps1
# 特性：改之前先备份到 _backup_before_ui/，可随时还原
$ErrorActionPreference = 'Stop'

$proj = "C:\Users\李思颉\Desktop\FryPi-master\FryPi-master\2.software\2.Advanced\6.LVGL-SmartWatch\LVGL_template"
$src  = "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\firmware\ui"
$bak  = Join-Path $proj "_backup_before_ui"

if (-not (Test-Path $proj)) { throw "找不到工程目录：$proj" }

# ── 0. 备份 ─────────────────────────────────────────────
New-Item -ItemType Directory -Force $bak | Out-Null
foreach ($f in @("User\GUI\ui.c", "Middlewares\LVGL\lv_conf.h", "MDK-ARM\LCD.uvprojx")) {
    $p = Join-Path $proj $f
    Copy-Item $p (Join-Path $bak (Split-Path $f -Leaf)) -Force
}
Write-Output "[0] 已备份 ui.c / lv_conf.h / LCD.uvprojx → _backup_before_ui\"

# ── 1. 拷 UI 源文件 ─────────────────────────────────────
$files = @("ui_theme.h", "ui_icons.h", "ui_icons.c", "ui_main.h", "ui_main.c")
foreach ($f in $files) {
    Copy-Item (Join-Path $src $f) (Join-Path $proj "User\GUI") -Force
}
Write-Output ("[1] 已拷入 {0} 个文件到 User\GUI\" -f $files.Count)

# ── 2. lv_conf.h：打开 44 号字体 ─────────────────────────
$conf = Join-Path $proj "Middlewares\LVGL\lv_conf.h"
$t = Get-Content $conf -Raw
if ($t -match '#define\s+LV_FONT_MONTSERRAT_44\s+1') {
    Write-Output "[2] lv_conf.h: 44 号字体已经是 1，跳过"
} elseif ($t -match '#define\s+LV_FONT_MONTSERRAT_44\s+0') {
    $t = $t -replace '(#define\s+LV_FONT_MONTSERRAT_44\s+)0', '${1}1'
    Set-Content -Path $conf -Value $t -NoNewline
    Write-Output "[2] lv_conf.h: LV_FONT_MONTSERRAT_44 0 → 1 ✅"
} else {
    Write-Output "[2] ⚠️ lv_conf.h 里没找到该行，需手动处理"
}

# ── 3. ui.c：换成我们的界面 ─────────────────────────────
$uic = Join-Path $proj "User\GUI\ui.c"
$u = Get-Content $uic -Raw
if ($u -notmatch 'ui_main\.h') {
    $u = $u -replace '#include\s+"ui\.h"', "#include `"ui.h`"`r`n#include `"ui_main.h`""
}
$u = $u -replace 'ui_HomePage_screen_init\(\);', 'lv_obj_t * scr_main = ui_main_create();'
$u = $u -replace 'lv_disp_load_scr\(ui_HomePage\);', 'lv_disp_load_scr(scr_main);'
Set-Content -Path $uic -Value $u -NoNewline
Write-Output "[3] ui.c: 已改成调用 ui_main_create()"

# ── 4. LCD.uvprojx：把两个 .c 加进编译 ───────────────────
$uv = Join-Path $proj "MDK-ARM\LCD.uvprojx"
$p = Get-Content $uv -Raw
if ($p -match 'ui_main\.c') {
    Write-Output "[4] uvprojx: 已在工程里，跳过"
} else {
    $pat = '(?s)<FileName>ui_HomePage\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
    $add = "`r`n            <File>`r`n              <FileName>ui_main.c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\User\GUI\ui_main.c</FilePath>`r`n            </File>`r`n            <File>`r`n              <FileName>ui_icons.c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\User\GUI\ui_icons.c</FilePath>`r`n            </File>"
    $eval = [System.Text.RegularExpressions.MatchEvaluator]{
        param($m)
        return $m.Value + $add
    }
    $p2 = [regex]::Replace($p, $pat, $eval, 1)
    if ($p2 -eq $p) {
        Write-Output "[4] ⚠️ uvprojx: 没匹配到插入点，需手动添加"
    } else {
        Set-Content -Path $uv -Value $p2 -NoNewline
        Write-Output "[4] uvprojx: 已把 ui_main.c / ui_icons.c 加入 Application/User/GUI 组 ✅"
    }
}

# ── 5. 结果自检 ─────────────────────────────────────────
Write-Output "`n=== 自检 ==="
Get-ChildItem (Join-Path $proj "User\GUI") -File | Select-Object Name, @{n = 'KB'; e = { [math]::Round($_.Length / 1KB, 1) } } | Format-Table -AutoSize
Write-Output "--- ui.c 现在的 ui_init ---"
(Get-Content $uic -Raw) -split "`n" | Select-String -Pattern "ui_main|load_scr|include" | ForEach-Object { $_.Line.Trim() }
Write-Output "--- lv_conf 44 号 ---"
Select-String -Path $conf -Pattern "LV_FONT_MONTSERRAT_44" | ForEach-Object { $_.Line.Trim() }
Write-Output "--- uvprojx 里的新文件 ---"
Select-String -Path $uv -Pattern "ui_main\.c|ui_icons\.c" | ForEach-Object { $_.Line.Trim() }
