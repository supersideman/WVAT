# OV-Watch 网关启动器（PowerShell 版）
#
# 为什么用 .ps1 而不是 .cmd：
#   1. 用户名含中文时，.cmd 若把它写死会变成 "???", 路径静默失效；
#   2. 批处理的 %% 转义、括号块、pause 与重定向组合行为难以预料；
#   PowerShell 原生 Unicode，路径与参数都不需要转义技巧。
#
# 用法：双击 start_gateway.cmd，或
#       powershell -ExecutionPolicy Bypass -File start_gateway.ps1 [-Dry] [-Fake]

param(
    [switch]$Dry,      # 演练模式：不真正操作电脑
    [switch]$Fake,     # 不启用语音识别
    [string]$Port = "" # 手动指定串口，如 COM13
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

function Write-Info($msg) { Write-Host $msg }
function Fail($msg) {
    Write-Host ""
    Write-Host "[launcher] 错误：$msg" -ForegroundColor Red
    Write-Host ""
    exit 1
}

Write-Host "================================================"
Write-Host "  OV-Watch Gateway"
Write-Host "================================================"
Write-Host ""

# ── 1. 找随附的 Python 3.12 ────────────────────────────
# 必须用 3.12：cffi / vosk 的 C 扩展按 cp312 编译，
# 换解释器会报 "No module named '_cffi_backend'"（踩过：系统 Python 3.15）。
$candidates = @(
    (Join-Path $env:USERPROFILE ".dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe")
)
$py = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $py) {
    Fail "找不到随附的 Python 3.12。期望路径：`n  $($candidates[0])`n  请不要改用系统 Python——本项目的扩展只适用于 3.12。"
}

$ver = & $py -c "import sys;print('%d.%d' % sys.version_info[:2])"
Write-Info "[launcher] python: $py"
Write-Info "[launcher] version: $ver"
if ($ver -ne "3.12") {
    Fail "需要 Python 3.12，当前是 $ver（$py）"
}

# ── 2. 依赖路径 ────────────────────────────────────────
$pylibs = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\vendor\_pylibs"))
$env:PYTHONPATH = $pylibs
Write-Info "[launcher] pylibs: $pylibs"

# ── 3. 组参数 ──────────────────────────────────────────
$argv = @("run_gateway.py")
if ($Dry)  { $argv += "-Dry" }
if ($Fake) { $argv += "fake" }
if ($Port) { $argv += "-Port"; $argv += $Port }

Write-Info "[launcher] 参数: $($argv -join ' ')"
Write-Host ""

# ── 4. 启动 ────────────────────────────────────────────
& $py @argv
$code = $LASTEXITCODE

Write-Host ""
Write-Host "[launcher] 网关已退出（退出码 $code）"
if ($code -eq 2) {
    Write-Host "[launcher] 退出码 2 = 解释器版本不对，请检查上面的提示" -ForegroundColor Yellow
}
exit $code
