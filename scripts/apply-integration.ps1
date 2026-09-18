param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$teamRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $teamRoot '..')).Path

# git 通常不在 PowerShell 的 PATH 里（Git for Windows 只写进 Git Bash 的 PATH）。
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

$patches = @(
    @{ Repo = 'apps'; Patch = 'apps.patch' },
    @{ Repo = 'nuttx'; Patch = 'nuttx.patch' },
    @{ Repo = 'packages/ai_agent'; Patch = 'packages-ai-agent.patch' },
    @{ Repo = 'vendor/sifli'; Patch = 'vendor-sifli.patch' }
)

function Invoke-RepositoryPatch {
    param([string]$RepoRelative, [string]$PatchName)

    $repoPath = Join-Path $workspaceRoot $RepoRelative
    $patchPath = Join-Path $teamRoot "patches/$PatchName"
    if (-not (Test-Path -LiteralPath "$repoPath/.git")) {
        throw "Missing Git repository: $repoPath"
    }

    $ErrorActionPreference = 'Continue'
    & $git -C $repoPath apply --check $patchPath 2>$null
    $applyExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($applyExit -eq 0) {
        if ($Check) {
            Write-Host "READY patch $RepoRelative"
        }
        else {
            & $git -C $repoPath apply $patchPath
            if ($LASTEXITCODE -ne 0) { throw "Failed to apply $PatchName" }
            Write-Host "APPLIED patch $RepoRelative"
        }
        return
    }

    $ErrorActionPreference = 'Continue'
    & $git -C $repoPath apply --reverse --check $patchPath 2>$null
    $reverseExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($reverseExit -eq 0) {
        Write-Host "ALREADY patch $RepoRelative"
        return
    }

    throw "Patch conflicts with ${RepoRelative}: $PatchName"
}

function Install-Overlay {
    param([string]$SourceRelative, [string]$DestinationRelative)

    $source = Join-Path $teamRoot "overlays/$SourceRelative"
    $destination = Join-Path $workspaceRoot $DestinationRelative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing overlay: $source"
    }

    if (Test-Path -LiteralPath $destination) {
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ne $destinationHash) {
            throw "Overlay conflicts with existing file: $destination"
        }
        Write-Host "ALREADY overlay $DestinationRelative"
        return
    }

    if ($Check) {
        Write-Host "READY overlay $DestinationRelative"
        return
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    Write-Host "COPIED overlay $DestinationRelative"
}

foreach ($item in $patches) {
    Invoke-RepositoryPatch -RepoRelative $item.Repo -PatchName $item.Patch
}

Install-Overlay 'apps/system/libuv/nuttx_threadpool.c' 'apps/system/libuv/nuttx_threadpool.c'
Install-Overlay 'vendor/sifli/chips/drivers/epic/drv_epic.c' 'vendor/sifli/chips/drivers/epic/drv_epic.c'
Install-Overlay 'vendor/sifli/chips/drivers/epic/drv_epic.h' 'vendor/sifli/chips/drivers/epic/drv_epic.h'
Install-Overlay 'vendor/sifli/chips/drivers/epic/drv_epic_priv.h' 'vendor/sifli/chips/drivers/epic/drv_epic_priv.h'

Write-Host 'Integration check completed.' -ForegroundColor Green
