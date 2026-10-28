# 把 user_link.c/h 接进 Keil 工程，并在 ui_init() 里启动链路
# 幂等：重复运行不会重复插入
$ErrorActionPreference = 'Stop'

$proj = "C:\Users\李思颉\Desktop\FryPi-master\FryPi-master\2.software\2.Advanced\6.LVGL-SmartWatch\LVGL_template"
$src  = "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\firmware\ui"

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

if ($p -match 'user_link\.c') {
    Write-Output "[2] uvprojx: 已经在工程里，跳过"
} else {
    $pat = '(?s)<FileName>ui_icons\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
    $add = "`r`n            <File>`r`n              <FileName>user_link.c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\User\GUI\user_link.c</FilePath>`r`n            </File>"
    $eval = [System.Text.RegularExpressions.MatchEvaluator] { param($m) return $m.Value + $add }
    $p2 = [regex]::Replace($p, $pat, $eval, 1)
    if ($p2 -eq $p) {
        Write-Output "[2] ⚠️ 没找到 ui_icons.c 作锚点，需手动在 Keil 里添加"
    } else {
        Copy-Item $uv "$uv.bak_link" -Force
        Set-Content -Path $uv -Value $p2 -NoNewline
        try {
            [xml](Get-Content $uv -Raw) | Out-Null
            Write-Output "[2] ✅ user_link.c 已加入编译组，XML 校验通过"
        } catch {
            Copy-Item "$uv.bak_link" $uv -Force
            throw "XML 被改坏，已还原：$($_.Exception.Message)"
        }
    }
}

# ── 3. ui.c 里启动链路 ──────────────────────────────────
$uic = Join-Path $proj "User\GUI\ui.c"
$u = Get-Content $uic -Raw

if ($u -notmatch 'user_link\.h') {
    $u = $u -replace '#include\s+"ui_main\.h"', "#include `"ui_main.h`"`r`n#include `"user_link.h`""
}
if ($u -notmatch 'link_init\s*\(\s*\)') {
    $u = $u -replace '(lv_disp_load_scr\(scr_main\);)', "`$1`r`n    link_init();   /* 启动 USART1 指令链路 */"
}
Set-Content -Path $uic -Value $u -NoNewline
Write-Output "[3] ui.c: 已加入 link_init()"

# ── 4. 自检 ─────────────────────────────────────────────
Write-Output "`n=== ui_init 现在长这样 ==="
(Get-Content $uic -Raw) -split "`n" | Select-String -Pattern "include|ui_main_create|load_scr|link_init" | ForEach-Object { "  " + $_.Line.Trim() }

Write-Output "`n=== 工程里的 link 文件 ==="
Select-String -Path $uv -Pattern "user_link\.c|ui_icons\.c|ui_main\.c" | ForEach-Object { "  " + $_.Line.Trim() }
