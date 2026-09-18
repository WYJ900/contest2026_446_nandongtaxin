Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.SelectVoice('Microsoft Huihui Desktop')
$s.Rate = 0
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(24000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)

$tmp = "D:\desktop\VelaRide_AI\my-contest-repo\submission-preview\_tts"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$lines = @(
    @('01_dial', 'VelaRide AI 运行在 openvela 黄山派上。手表提供五个表盘和可二维拖动的蜂窝菜单，全部用 L V G L 原生实现。'),
    @('02_config', '快速双击实体按键进入运动流程。选择运动类型和目标后，直接在手表上开始骑行。'),
    @('03_ride', '传感器持续采集运动数据，本地规则判断运动、静止和冲击。骑行记录存入独立 Flash 区，跨复位不丢失。'),
    @('04_mimo', '网页录入刚才手表生成的真实数据，调用小米 MiMo 生成自然语言骑行复盘。页面会明确标记来源，不冒充 A I 结果。'),
    @('05_outro', 'VelaRide AI 已完成腕上交互、运动检测、本地安全规则和真实 MiMo 复盘。')
)

foreach ($pair in $lines) {
    $name = $pair[0]
    $text = $pair[1]
    $out = Join-Path $tmp "tts_$name.wav"
    $s.SetOutputToWaveFile($out, $fmt)
    $s.Speak($text)
    $size = (Get-Item $out).Length
    $dur = [math]::Round(($size - 44) / (24000 * 2), 2)
    Write-Host "$name : $dur s  ($size bytes)"
}
$s.Dispose()
Write-Host "TTS done"
