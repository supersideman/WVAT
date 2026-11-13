# 把 lv_font_montserrat_44.c 加进 Keil 工程的 Font 组
# 原因：lv_conf.h 打开 LV_FONT_MONTSERRAT_44=1 只是"允许编译"，
#       对应的 .c 文件还必须被加进工程，否则链接报 L6218E 未定义符号
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$uv = Join-Path $root '01_firmware\keil\LVGL_template\MDK-ARM\LCD.uvprojx'
$bak = "$uv.bak_font44"

if (-not (Test-Path $uv)) { throw "找不到工程：$uv" }
Copy-Item $uv $bak -Force
Write-Output "[0] 已备份 → $bak"

$p = Get-Content $uv -Raw

if ($p -match 'lv_font_montserrat_44\.c') {
    Write-Output "[1] 工程里已经有 44 号字体，无需重复添加"
    return
}

# 在 montserrat_18.c 那一项后面插入 44 号
$pat = '(?s)<FileName>lv_font_montserrat_18\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
$add = "`r`n            <File>`r`n              <FileName>lv_font_montserrat_44.c</FileName>`r`n              <FileType>1</FileType>`r`n              <FilePath>..\Middlewares\LVGL\src\font\lv_font_montserrat_44.c</FilePath>`r`n            </File>"

$eval = [System.Text.RegularExpressions.MatchEvaluator] { param($m) return $m.Value + $add }
$p2 = [regex]::Replace($p, $pat, $eval, 1)

if ($p2 -eq $p) {
    Write-Output "[1] ⚠️ 没找到 montserrat_18 作为插入锚点，尝试用 montserrat_16"
    $pat2 = '(?s)<FileName>lv_font_montserrat_16\.c</FileName>\s*<FileType>1</FileType>\s*<FilePath>[^<]+</FilePath>\s*</File>'
    $p2 = [regex]::Replace($p, $pat2, $eval, 1)
    if ($p2 -eq $p) { throw "两种锚点都匹配失败，请手动在 Keil 里添加 lv_font_montserrat_44.c" }
}

Set-Content -Path $uv -Value $p2 -NoNewline
Write-Output "[1] ✅ 已插入 lv_font_montserrat_44.c"

# XML 合法性自检
try {
    [xml](Get-Content $uv -Raw) | Out-Null
    Write-Output "[2] ✅ 工程文件 XML 仍然合法"
} catch {
    Copy-Item $bak $uv -Force
    throw "XML 被改坏了，已自动还原：$($_.Exception.Message)"
}

Write-Output "`n=== Font 组现在的文件 ==="
$c = Get-Content $uv -Raw
$i = $c.IndexOf("Middlewares/LVGL/Font")
$seg = $c.Substring($i, [Math]::Min(2000, $c.Length - $i))
[regex]::Matches($seg, '<FilePath>([^<]+)</FilePath>') | ForEach-Object { "  " + $_.Groups[1].Value }
