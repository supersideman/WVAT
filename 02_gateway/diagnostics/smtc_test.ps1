# SMTC 媒体会话测试：查询当前媒体会话 + 尝试暂停/继续
# 用法：powershell.exe -ExecutionPolicy Bypass -File smtc_test.ps1
#      powershell.exe -ExecutionPolicy Bypass -File smtc_test.ps1 next
param([string]$Action = "info")

$ErrorActionPreference = 'Stop'

# PS 5.1 里 WinRT 异步桥接所需的程序集默认没加载，必须先 Add-Type
Add-Type -AssemblyName System.Runtime.WindowsRuntime

# WinRT 异步 → .NET Task 的通用桥接（PS 5.1 官方套路）
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and
    $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($op, [Type]$resultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($resultType)
    $task = $asTask.Invoke($null, @($op))
    $task.Wait(-1) | Out-Null
    return $task.Result
}

try {
    $null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime]
    $mgrType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]

    $mgr = Await ($mgrType::RequestAsync()) $mgrType

    # 列出所有会话，便于确认浏览器到底注册没注册
    $all = $mgr.GetSessions()
    Write-Output ("会话数量: " + $all.Count)
    foreach ($s in $all) { Write-Output ("  - " + $s.SourceAppUserModelId) }

    $sess = $mgr.GetCurrentSession()
    if (-not $sess) {
        Write-Output "当前没有活动媒体会话"
        exit 0
    }

    Write-Output ("当前会话: " + $sess.SourceAppUserModelId)
    $infoType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties]
    $info = Await ($sess.TryGetMediaPropertiesAsync()) $infoType
    Write-Output ("标题: " + $info.Title)
    Write-Output ("状态: " + $sess.GetPlaybackInfo().PlaybackStatus)

    switch ($Action.ToLower()) {
        "pause" {
            $ok = Await ($sess.TryPauseAsync()) ([Nullable[bool]])
            Write-Output ("暂停 -> " + $ok)
        }
        "play" {
            $ok = Await ($sess.TryPlayAsync()) ([Nullable[bool]])
            Write-Output ("播放 -> " + $ok)
        }
        "toggle" {
            $ok = Await ($sess.TryTogglePlayPauseAsync()) ([Nullable[bool]])
            Write-Output ("切换播放/暂停 -> " + $ok)
        }
        "next" {
            $ok = Await ($sess.TrySkipNextAsync()) ([Nullable[bool]])
            Write-Output ("下一首 -> " + $ok)
        }
        default { Write-Output "只查询，不操作" }
    }

    Start-Sleep -Milliseconds 600
    Write-Output ("操作后状态: " + $sess.GetPlaybackInfo().PlaybackStatus)
} catch {
    Write-Output ("失败: " + $_.Exception.Message)
    Write-Output ("位置: " + $_.InvocationInfo.PositionMessage)
}
