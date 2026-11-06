# Sends one set_llm command to the emulator's ai_agent. The key is read hidden
# and goes straight to the agent over 127.0.0.1:5999; it is not logged or saved here.
Write-Host 'VelaRide ai_agent: set LLM (OpenAI-compatible /v1/chat/completions)' -ForegroundColor Cyan
Write-Host '  preset example: mimo   |   custom example: https://your-relay/v1'
$target = Read-Host 'Preset name or base URL'
$model = ''
if ($target -match '^https?://') { $model = Read-Host 'Model name' }
$secret = Read-Host 'API key (hidden)' -AsSecureString
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
try {
    $key = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    $line = if ($model) { "set_llm $target $model $key" } else { "set_llm $target $key" }
    $client = New-Object Net.Sockets.TcpClient('127.0.0.1', 5999)
    $stream = $client.GetStream()
    $bytes = [Text.Encoding]::UTF8.GetBytes($line + "`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $reply = (New-Object IO.StreamReader($stream)).ReadLine()
    $client.Close()
    Write-Host "agent console: $reply" -ForegroundColor Green
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr); $key = $null; $line = $null
}
