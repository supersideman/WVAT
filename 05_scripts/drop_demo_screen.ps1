# 把作者模板里的示例界面（ui_HomePage）从构建中摘掉
# 原因：它用了 lv_slider / lv_btn，而这两个控件已被我们裁剪关闭；
#       而且本项目从来没用到这个演示页面。
$ErrorActionPreference = 'Stop'

$proj = "C:\Users\李思颉\Documents\deepseek-harness\default-workspace\WVAT\01_firmware\keil\LVGL_template"
$uv   = Join-Path $proj "MDK-ARM\LCD.uvprojx"
$uic  = Join-Path $proj "User\GUI\ui.c"

# ── 1. ui.c：去掉对示例界面的调用（该文件是 GBK 编码，必须原样写回）──
$bytes = [System.IO.File]::ReadAllBytes($uic)
$gbk = [System.Text.Encoding]::GetEncoding(936)
$text = $gbk.GetString($bytes)

if ($text -match 'ui_HomePage_screen_destroy') {
    $text = $text -replace '(\s*)ui_HomePage_screen_destroy\(\);', "`$1/* 作者示例界面已从工程移除，这里不再需要销毁 */"
    [System.IO.File]::WriteAllBytes($uic, $gbk.GetBytes($text))
    Write-Output "[1] ui.c: 已移除 ui_HomePage_screen_destroy() 调用"
} else {
    Write-Output "[1] ui.c: 无需修改"
}

# ── 2. uvprojx：移除 ui_HomePage.c 的 File 条目 ──
$p = Get-Content $uv -Raw
if ($p -match 'ui_HomePage\.c') {
    $pattern = '(?s)\s*<File>\s*<FileName>ui_HomePage\.c</FileName>.*?</File>'
    $p2 = [regex]::Replace($p, $pattern, '', 1)
    if ($p2 -eq $p) {
        Write-Output "[2] ⚠️ 没匹配到 ui_HomePage.c 的 File 条目，需手动在 Keil 里移除"
    } else {
        Copy-Item $uv "$uv.bak_demo" -Force
        Set-Content -Path $uv -Value $p2 -NoNewline
        try {
            [xml](Get-Content $uv -Raw) | Out-Null
            Write-Output "[2] ✅ 已从编译组移除 ui_HomePage.c（XML 校验通过）"
        } catch {
            Copy-Item "$uv.bak_demo" $uv -Force
            throw "XML 被改坏，已还原：$($_.Exception.Message)"
        }
    }
} else {
    Write-Output "[2] ui_HomePage.c 已不在编译组里"
}

Write-Output "`n=== 当前工程里的 GUI 源文件 ==="
$c = Get-Content $uv -Raw
[regex]::Matches($c, '<FilePath>\.\.\\User\\[^<]+</FilePath>') | ForEach-Object { "  " + $_.Value }
