# 盯 30 秒，一旦出现媒体会话就打印出来（用于确认 SMTC 是否可见）
param([int]$Seconds = 30)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime

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

$null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime]
$mgrType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]
$mgr = Await ($mgrType::RequestAsync()) $mgrType

Write-Output ("开始监视 {0} 秒，请在媒体软件里点播放…" -f $Seconds)
$deadline = (Get-Date).AddSeconds($Seconds)
$seen = @{}
while ((Get-Date) -lt $deadline) {
    $list = $mgr.GetSessions()
    if ($list) {
        foreach ($s in $list) {
            $id = $s.SourceAppUserModelId
            if (-not $seen.ContainsKey($id)) {
                $seen[$id] = $true
                $status = $s.GetPlaybackInfo().PlaybackStatus
                Write-Output ("发现会话: {0}  状态={1}" -f $id, $status)
            }
        }
    }
    Start-Sleep -Milliseconds 500
}

if ($seen.Count -eq 0) {
    Write-Output "30 秒内没有任何媒体会话"
} else {
    Write-Output ("共发现 {0} 个会话" -f $seen.Count)
}
