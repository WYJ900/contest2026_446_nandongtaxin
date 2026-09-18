param(
    [ValidateSet('mimo-v2.5', 'mimo-v2.5-pro')]
    [string]$Model = 'mimo-v2.5-pro'
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

Write-Host 'VelaRide AI secure gateway launcher' -ForegroundColor Cyan
Write-Host "MiMo model: $Model"
Write-Host 'Paste a newly generated MiMo Token. Input is hidden and is not saved.'
$secureToken = Read-Host 'MiMo Token' -AsSecureString

if ($secureToken.Length -eq 0) {
    throw 'Token must not be empty.'
}

$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)

try {
    $env:ANTHROPIC_AUTH_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
    $env:ANTHROPIC_BASE_URL = 'https://token-plan-cn.xiaomimimo.com/anthropic'
    $env:ANTHROPIC_MODEL = $Model

    Write-Host 'Starting http://127.0.0.1:4173' -ForegroundColor Green
    Write-Host 'The web log should report ai=mimo-anthropic when ready.'
    npm start
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer)
    Remove-Item Env:ANTHROPIC_AUTH_TOKEN -ErrorAction SilentlyContinue
}
