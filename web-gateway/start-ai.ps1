param([ValidateRange(1024,65535)][int]$Port = 4173)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
Write-Host 'VelaRide AI - 本机安全配置' -ForegroundColor Cyan
Write-Host '1. Xiaomi MiMo  2. OpenAI 兼容/中转  3. Anthropic 兼容/中转  4. Responses'
$choice = Read-Host '选择接口 (1-4)'
$protocol = switch ($choice) { '1' { 'anthropic' }; '2' { 'openai' }; '3' { 'anthropic' }; '4' { 'responses' }; default { throw '请选择 1-4' } }
if ($choice -eq '1') {
    $baseUrl = 'https://token-plan-cn.xiaomimimo.com/anthropic'
    $model = Read-Host '模型名称 (直接回车使用 mimo-v2.5-pro)'
    if ([string]::IsNullOrWhiteSpace($model)) { $model = 'mimo-v2.5-pro' }
    $providerName = 'Xiaomi MiMo'
} else {
    $baseUrl = Read-Host 'Base URL 或完整接口地址 (不含 Key)'
    $model = Read-Host '模型名称 (按服务商提供的名称填写)'
    $providerName = Read-Host '服务名称 (例如 自定义中转)'
    if ([string]::IsNullOrWhiteSpace($providerName)) { $providerName = '自定义 AI' }
}
$endpointUri = $null
if (-not [Uri]::TryCreate($baseUrl, [UriKind]::Absolute, [ref]$endpointUri) -or
    $endpointUri.Scheme -notin @('http','https') -or $endpointUri.UserInfo -or
    $endpointUri.Query -or $endpointUri.Fragment) { throw '地址无效，请勿把 Key 放进 URL。' }
if ([string]::IsNullOrWhiteSpace($model)) { throw '模型名称不能为空。' }
$auth = 'bearer'
if ($choice -eq '3') {
    $authChoice = Read-Host '鉴权: 1=Bearer (默认), 2=x-api-key'
    if ($authChoice -eq '2') { $auth = 'x-api-key' }
}
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    throw "端口 $Port 已占用。请关闭已有网关，或用 -Port 4174 启动。"
}
Write-Host "目标服务: $($endpointUri.Host)  模型: $model" -ForegroundColor Yellow
Write-Host '输入 API Key。内容隐藏，仅保留在本进程内存，不写入文件。'
$secret = Read-Host 'API Key' -AsSecureString
if ($secret.Length -eq 0) { throw 'API Key 不能为空。' }
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
$names = @('AI_PROTOCOL','AI_BASE_URL','AI_MODEL','AI_PROVIDER_NAME','AI_AUTH','AI_API_KEY','PORT')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:AI_PROTOCOL = $protocol
    $env:AI_BASE_URL = $baseUrl.Trim()
    $env:AI_MODEL = $model.Trim()
    $env:AI_PROVIDER_NAME = $providerName
    $env:AI_AUTH = $auth
    $env:AI_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    $env:PORT = [string]$Port
    Write-Host "网关启动: http://127.0.0.1:$Port；按 Ctrl+C 结束。" -ForegroundColor Green
    & node --watch (Join-Path $PSScriptRoot 'server.mjs')
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
    $secret.Dispose()
}
