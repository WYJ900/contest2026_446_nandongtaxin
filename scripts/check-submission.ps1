param(
    [switch]$AllowMissingLogs
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$failures = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

function Pass([string]$Message) { Write-Host "PASS $Message" -ForegroundColor Green }
function Fail([string]$Message) { $failures.Add($Message); Write-Host "FAIL $Message" -ForegroundColor Red }
function Warn([string]$Message) { $warnings.Add($Message); Write-Host "WARN $Message" -ForegroundColor Yellow }

# git 通常不在 PowerShell 的 PATH 里（Git for Windows 只写进 Git Bash 的 PATH），显式定位。
$git = $null
$gitCommand = Get-Command git -CommandType Application -ErrorAction SilentlyContinue
if ($gitCommand) { $git = $gitCommand.Source }
if (-not $git) {
    foreach ($candidate in @(
        "$env:ProgramFiles\Git\cmd\git.exe",
        "${env:ProgramFiles(x86)}\Git\cmd\git.exe",
        "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe"
    )) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { $git = $candidate; break }
    }
}
if (-not $git) { throw '未找到 git，请先安装 Git for Windows。' }

$required = @(
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'contest2026_446_nandongtaxin.xml',
    'app/velaride/CMakeLists.txt',
    'agent_skills/ride-safety.md',
    'web-gateway/package.json',
    'patches/apps.patch',
    'patches/nuttx.patch',
    'patches/packages-ai-agent.patch',
    'patches/vendor-sifli.patch',
    'artifacts/velaride-huangshan-pi-verified.bin'
)

foreach ($relative in $required) {
    if (Test-Path -LiteralPath (Join-Path $root $relative)) {
        Pass "required $relative"
    }
    else {
        Fail "missing $relative"
    }
}

$decks = @()
foreach ($deck in Get-ChildItem (Join-Path $root 'submission') -File -Filter '*.pptx') {
    $relativeDeck = $deck.FullName.Substring($root.Length + 1).Replace('\', '/')
    $ErrorActionPreference = 'Continue'
    & $git -C $root check-ignore --quiet -- $relativeDeck
    $ignored = $LASTEXITCODE -eq 0
    $ErrorActionPreference = 'Stop'
    if (-not $ignored) { $decks += $deck }
}
if ($decks.Count -eq 1) { Pass "submission deck $($decks[0].Name)" } else { Fail "submission deck count $($decks.Count)" }

try {
    [xml]$manifest = Get-Content -LiteralPath (Join-Path $root 'contest2026_446_nandongtaxin.xml') -Raw
    $links = @($manifest.manifest.project.linkfile)
    foreach ($link in $links) {
        if (-not (Test-Path -LiteralPath (Join-Path $root ([string]$link.src)))) {
            Fail "manifest source missing: $($link.src)"
        }
    }
    if ($links.Count -eq 2) { Pass 'manifest has 2 project links' } else { Fail "manifest link count $($links.Count)" }
}
catch {
    Fail "manifest XML invalid: $($_.Exception.Message)"
}

$expectedHash = 'E2304000A9F7260DA7EBCC53CC2FDE27BBF51237CF758FA71F76B66E369031F1'
$actualHash = (Get-FileHash -LiteralPath (Join-Path $root 'artifacts/velaride-huangshan-pi-verified.bin') -Algorithm SHA256).Hash
if ($actualHash -eq $expectedHash) { Pass 'firmware SHA-256' } else { Fail "firmware SHA-256 $actualHash" }

Push-Location (Join-Path $root 'web-gateway')
try {
    & npm test
    if ($LASTEXITCODE -eq 0) { Pass 'web-gateway tests' } else { Fail "web-gateway tests exit $LASTEXITCODE" }
}
finally { Pop-Location }

& (Join-Path $root 'scripts/apply-integration.ps1') -Check
if ($LASTEXITCODE -eq 0) { Pass 'integration patches and overlays' } else { Fail 'integration check failed' }

$secretRegex = 'tp-[A-Za-z0-9_-]{20,}|sk-[A-Za-z0-9_-]{20,}|BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY'
$secretFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force |
    Where-Object { $_.FullName -notmatch '[\\/](\.git|artifacts|node_modules)[\\/]' } |
    Where-Object { Select-String -LiteralPath $_.FullName -Pattern $secretRegex -Quiet -ErrorAction SilentlyContinue } |
    ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') })
if ($secretFiles.Count -eq 0) { Pass 'secret scan' } else { Fail "secret-like data: $($secretFiles -join ', ')" }

$logs = @(Get-ChildItem (Join-Path $root 'logs/WYJ900') -File -Recurse -Filter '*.jsonl' -ErrorAction SilentlyContinue)
if ($logs.Count -gt 0) {
    Pass "AI Coding logs $($logs.Count)"
}
elseif ($AllowMissingLogs) {
    Warn 'AI Coding logs are still missing'
}
else {
    Fail 'AI Coding logs are still missing'
}

Push-Location $root
try {
    # git 会把 LF/CRLF 提示写到 stderr；PowerShell 5.1 将其包装成 ErrorRecord，
    # 在 $ErrorActionPreference='Stop' 下会被误判为脚本失败。这里临时降级为
    # Continue，stderr 丢弃，只看退出码。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $null = & $git diff --check 2>&1
        $diffExit = $LASTEXITCODE
        $null = & $git diff --cached --check 2>&1
        $cachedExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    if ($diffExit -eq 0) { Pass 'git diff --check' } else { Fail 'git diff --check failed' }
    if ($cachedExit -eq 0) { Pass 'git diff --cached --check' } else { Fail 'git diff --cached --check failed' }
}
finally { Pop-Location }

Write-Host "Summary: failures=$($failures.Count) warnings=$($warnings.Count)"
if ($failures.Count -gt 0) { exit 1 }
