"""用 Windows 自带 TTS 合成中文语音，喂给 Vosk，验证识别引擎本身是否正常。

这样可以把"引擎问题"和"麦克风太轻"两类原因分开。
用法：python asr_tts_check.py
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "..", "vendor", "_pylibs"))

import numpy as np  # noqa: E402

import asr  # noqa: E402

PHRASE = "帮我打开资料"

PS = r"""
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voices = $s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name }
Write-Output ("VOICES=" + ($voices -join '|'))
$cn = $s.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like 'zh*' } | Select-Object -First 1
if ($cn) { $s.SelectVoice($cn.VoiceInfo.Name); Write-Output ("USING=" + $cn.VoiceInfo.Name) }
else { Write-Output "USING=(no chinese voice)" }
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$s.SetOutputToWaveFile("__WAV__", $fmt)
$s.Speak("__TEXT__")
$s.Dispose()
"""


def synth(text: str, wav: str) -> None:
    script = PS.replace("__WAV__", wav).replace("__TEXT__", text)
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                       capture_output=True, text=True, encoding="mbcs", errors="replace")
    for line in (r.stdout or "").splitlines():
        if line.startswith(("VOICES=", "USING=")):
            print(line)


def read_wav_pcm(path: str) -> bytes:
    """读 WAV 的 data 段（假设已经是 16k/16bit/mono，由 SAPI 保证）。"""
    with open(path, "rb") as fh:
        data = fh.read()
    idx = data.find(b"data")
    if idx < 0:
        return b""
    size = int.from_bytes(data[idx + 4:idx + 8], "little")
    return data[idx + 8: idx + 8 + size]


def main() -> int:
    if not asr.model_ready():
        print("模型不可用：", asr.model_dir())
        return 1

    wav = os.path.join(HERE, "_tts.wav")   # 放工作区内：沙箱临时目录写不进去
    print(f"合成语音：{PHRASE}")
    synth(PHRASE, wav)
    if not os.path.isfile(wav):
        print("合成失败：没有生成 wav")
        return 2

    pcm = read_wav_pcm(wav)
    samples = np.frombuffer(pcm, dtype=np.int16)
    print(f"wav 采样点数 {samples.size}（{samples.size/16000:.1f}s），peak={int(np.abs(samples).max()) if samples.size else 0}")

    text = asr.transcribe(pcm)
    print("=" * 40)
    print(f"Vosk 对合成语音的识别结果：【{text}】")
    print("（如果这句能识别出来，说明引擎与位图格式都没问题，问题只在麦克风太轻）")
    return 0 if text else 3


if __name__ == "__main__":
    raise SystemExit(main())
