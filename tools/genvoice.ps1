# Generates the co-driver's pace-note voice clips with Windows text-to-speech
# (System.Speech / SAPI). Re-run after changing words or voice settings:
#   powershell -ExecutionPolicy Bypass -File tools\genvoice.ps1
param([string]$OutDir = "$PSScriptRoot\..\res\audio\notes", [int]$Rate = 3)

Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voices = $synth.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { $_.VoiceInfo.Name }
# Prefer a clear English voice when several are installed.
foreach ($pref in 'Microsoft David Desktop', 'Microsoft Mark', 'Microsoft Zira Desktop') {
    if ($voices -contains $pref) { $synth.SelectVoice($pref); break }
}
$synth.Rate = $Rate
New-Item -ItemType Directory -Force $OutDir | Out-Null

$words = [ordered]@{
    'left' = 'left'; 'right' = 'right'
    '1' = 'one'; '2' = 'two'; '3' = 'three'; '4' = 'four'; '5' = 'five'; '6' = 'six'
    'hairpin' = 'hairpin'; 'square' = 'square'; 'long' = 'long'
    'tightens' = 'tightens'; 'opens' = 'opens'; 'dontcut' = "don't cut"
    'caution' = 'caution'; 'overcrest' = 'over crest'; 'crest' = 'crest'
    'jump' = 'jump'; 'overbridge' = 'over bridge'; 'finish' = 'flat to finish'
    'into' = 'into'; 'and' = 'and'
    '50' = 'fifty'; '70' = 'seventy'; '100' = 'one hundred'; '150' = 'one fifty'
    '200' = 'two hundred'; '250' = 'two fifty'; '300' = 'three hundred'
    'ready' = 'ready'; 'go' = 'go!'; 'goodluck' = 'Good luck. Lets go.'
    'finished' = 'Stage complete. Nice driving.'
}
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
foreach ($k in $words.Keys) {
    $path = Join-Path $OutDir "$k.wav"
    $synth.SetOutputToWaveFile($path, $format)
    $synth.Speak($words[$k])
}
$synth.SetOutputToNull()
$synth.Dispose()
Write-Output "Voice: $($voices -join ', ')"
Write-Output "Wrote $($words.Count) clips to $OutDir"
