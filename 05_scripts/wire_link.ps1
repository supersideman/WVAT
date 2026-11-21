# 把 user_link.c/h 接进 Keil 工程，并在 ui_init() 里启动链路
# 幂等：重复运行不会重复插入
$ErrorActionPreference = 'Stop'

# 路径全部从脚本自身位置推导（05_scripts 的上一级 = WVAT）：
# 以前写死 "C:\Users\李思颉\..."，用户名一换（=lisijie）整套脚本第一行就抛"找不到工程"。
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$proj = Join-Path $root '01_firmware\keil\LVGL_template'
$src  = Join-Path $root '01_firmware\ui'

if (-not (Test-Path $proj)) { throw "找不到工程：$proj" }

# ── 1. 拷源文件（**全部** UI 源码）
#     ⚠️ 教训：只拷 user_link.* 会让工程里的 ui_main.c 停留在旧版本，
#        链接时报 “Undefined symbol ui_main_set_intent_cb”。所以一次拷全。
$allsrc = Get-ChildItem $src -File | Where-Object { $_.Extension -in '.c', '.h' }
$allsrc | Copy-Item -Destination (Join-Path $proj "User\GUI") -Force
Write-Output ("[1] 已同步 {0} 个源文件到 User\GUI\" -f $allsrc.Count)

# ── 2. 加进 uvprojx 的编译组 ────────────────────────────
$uv = Join-Path $proj "MDK-ARM\LCD.uvprojx"
$p = Get-Content $uv -Raw

# 需要参与编译的 .c（新增文件必须列进来，否则 Keil 根本不会编它）
$needC = @("ui_main.c", "ui_icons.c", "user_link.c", "ui_drawer.c", "ui_lock.c", "ui_stage.c", "user_clock.c", "ui_status.c", "ui_rec.c", "ui_history.c", "user_i2s.c", "lv_font_cn_14.c", "ui_settings.c", "user_backlight.c", "ui_date.c")

$missing = @()
foreach ($c in $needC) { if ($p -notmatch [regex]::Escape($c)) { $missing += $c } }

if ($missing.Count -eq 0) {
    Write-Output "[2] uvprojx: 需要编译的 .c 都在工程里，跳过"
} else {
    $anchor = '(?s)<FileName>ui_icons\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
    $add = ""
    foreach ($c in $missing) {
        $add += "`r`n            <File>`r`n              <FileName>$c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\User\GUI\$c</FilePath>`r`n            </File>"
    }
    $eval = [System.Text.RegularExpressions.MatchEvaluator] { param($m) return $m.Value + $add }
    $p2 = [regex]::Replace($p, $anchor, $eval, 1)

    if ($p2 -eq $p) {
        Write-Output "[2] ⚠️ 没找到 ui_icons.c 作锚点，需手动在 Keil 里添加：$($missing -join ', ')"
    } else {
        Copy-Item $uv "$uv.bak_link" -Force
        Set-Content -Path $uv -Value $p2 -NoNewline
        try {
            [xml](Get-Content $uv -Raw) | Out-Null
            Write-Output ("[2] ✅ 已加入编译组：{0}（XML 校验通过）" -f ($missing -join ', '))
        } catch {
            Copy-Item "$uv.bak_link" $uv -Force
            throw "XML 被改坏，已还原：$($_.Exception.Message)"
        }
    }
}

# ── 2b. 中文字库源文件也要参与编译（lv_conf 里 LV_FONT_SIMSUN_16_CJK=1）──
$p = Get-Content $uv -Raw
if ($p -notmatch 'lv_font_simsun_16_cjk\.c') {
    $anchor2 = '(?s)<FileName>lv_font_montserrat_44\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
    $add2 = "`r`n            <File>`r`n              <FileName>lv_font_simsun_16_cjk.c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\Middlewares\LVGL\src\font\lv_font_simsun_16_cjk.c</FilePath>`r`n            </File>"
    $eval2 = [System.Text.RegularExpressions.MatchEvaluator] { param($m) return $m.Value + $add2 }
    $p3 = [regex]::Replace($p, $anchor2, $eval2, 1)
    if ($p3 -ne $p) {
        Copy-Item $uv "$uv.bak_font" -Force
        Set-Content -Path $uv -Value $p3 -NoNewline
        try {
            [xml](Get-Content $uv -Raw) | Out-Null
            Write-Output "[2b] ✅ 中文字库 lv_font_simsun_16_cjk.c 已加入编译组"
        } catch {
            Copy-Item "$uv.bak_font" $uv -Force
            throw "XML 被改坏，已还原：$($_.Exception.Message)"
        }
    } else {
        Write-Output "[2b] ⚠️ 没找到 montserrat_44 作锚点，需手动在 Keil 里添加中文字库文件"
    }
} else {
    Write-Output "[2b] 中文字库已在工程里"
}

# ── 3. ui.c：开机进锁屏 + 启动指令链路 ──────────────────
$uic = Join-Path $proj "User\GUI\ui.c"
# ui.c 是全项目唯一的 GBK 文件（ARMCC 在中文 Windows 下认它）。
# 显式按 GBK 读写：Get-Content 的默认编码在 PS 5.1 是 ANSI、在 PS7 是 UTF-8，
# 靠默认行为迟早会把中文注释写成 ???（drop_demo_screen.ps1 早就这么改了）。
$gbk = [System.Text.Encoding]::GetEncoding(936)
$u = $gbk.GetString([System.IO.File]::ReadAllBytes($uic))

if ($u -notmatch 'user_link\.h') {
    $u = $u -replace '#include\s+"ui_main\.h"', "#include `"ui_main.h`"`r`n#include `"user_link.h`""
}
if ($u -notmatch 'ui_stage\.h') {
    $u = $u -replace '#include\s+"user_link\.h"', "#include `"user_link.h`"`r`n#include `"ui_stage.h`""
}
if ($u -notmatch 'user_clock\.h') {
    $u = $u -replace '#include\s+"ui_stage\.h"', "#include `"ui_stage.h`"`r`n#include `"user_clock.h`""
}

# 开机：初始化舞台（锁屏 + 主界面在同一个可拖动容器里）
if ($u -match 'ui_lock_create\(\)') {
    $u = $u -replace 'lv_obj_t \* scr_lock = ui_lock_create\(\);', 'ui_stage_init();'
    $u = $u -replace 'lv_disp_load_scr\(scr_lock\);', 'lv_disp_load_scr(ui_stage_screen());'
}
elseif ($u -match 'ui_main_create\(\)') {
    $u = $u -replace 'lv_obj_t \* scr_main = ui_main_create\(\);', 'ui_stage_init();'
    $u = $u -replace 'lv_disp_load_scr\(scr_main\);', 'lv_disp_load_scr(ui_stage_screen());'
}

if ($u -notmatch 'link_init\s*\(\s*\)') {
    $u = $u -replace '(lv_disp_load_scr\(ui_stage_screen\(\)\);)', "`$1`r`n    link_init();   /* 启动 USART1 指令链路 */"
}

# 真实时钟：初始化 RTC + 建 1 秒刷新定时器（必须在界面建好之后）
if ($u -notmatch 'clock_init\s*\(\s*\)') {
    $u = $u -replace '(link_init\(\);[^\r\n]*)', "`$1`r`n    clock_init();            /* 开 LSE，初始化内部 RTC */`r`n    clock_start_ui_timer();  /* 每 1 秒刷新锁屏与主界面 */
    mic_boot_selftest();     /* I2S 麦克风自检：[mic] 开头的串口输出 */"
}
# I2S 麦克风自检：独立且幂等的补丁（ui.c 早就有了 clock_init，不能靠上面的分支）
if ($u -notmatch 'user_i2s\.h') {
    $u = $u -replace '#include\s+"user_clock\.h"', "#include `"user_clock.h`"`r`n#include `"user_i2s.h`""
}
if ($u -notmatch 'mic_boot_selftest') {
    $u = $u -replace '(clock_start_ui_timer\(\);[^\r\n]*)', "`$1`r`n    mic_boot_selftest();     /* I2S 麦克风自检：串口打印 [mic] */"
}
[System.IO.File]::WriteAllBytes($uic, $gbk.GetBytes($u))
Write-Output "[3] ui.c: 舞台 + link_init() + RTC 时钟（按 GBK 原编码写回）"

# ── 4. 自检 ─────────────────────────────────────────────
Write-Output "`n=== ui_init 现在长这样 ==="
(Get-Content $uic -Raw) -split "`n" | Select-String -Pattern "include|stage|load_scr|link_init|clock_" | ForEach-Object { "  " + $_.Line.Trim() }

Write-Output "`n=== 工程里我们的 .c ==="
Select-String -Path $uv -Pattern "user_clock\.c|ui_stage\.c|ui_lock\.c|ui_drawer\.c|user_link\.c|ui_icons\.c|ui_main\.c" | ForEach-Object { "  " + $_.Line.Trim() }

# ── 5. 编码自检（防呆）─────────────────────────────────────
# 踩过的坑：批量补 UTF-8 BOM 时对已带 BOM 的文件又写了一次，
# 文件开头变成 EF BB BF EF BB BF，ARMCC 读到第二个 BOM 就在第 1 行
# 报 "#7: unrecognized token"，然后所有包含它的文件连锁报错（一次 64 个）。
Write-Output "`n=== 源文件编码自检 ==="
$probs = 0
Get-ChildItem "$src\*" -File -Include *.c,*.h | ForEach-Object {
    $b = [System.IO.File]::ReadAllBytes($_.FullName)
    $n = 0
    while ($n + 2 -lt $b.Length -and $b[$n] -eq 0xEF -and $b[$n+1] -eq 0xBB -and $b[$n+2] -eq 0xBF) { $n += 3 }
    if ($n -ne 3) {
        Write-Output ("  " + $_.Name + "  BOM 数量=" + ($n / 3) + "（应为 1）")
        $probs++
    }
}
if ($probs -eq 0) { Write-Output "  OK：全部 .c/.h 都是单个 UTF-8 BOM" }
else { Write-Output "  警告：上面这些文件 BOM 异常，编译会报 unrecognized token" }