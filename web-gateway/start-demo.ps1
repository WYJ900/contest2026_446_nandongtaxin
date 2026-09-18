# VelaRide AI 演示启动器
#
# 一条命令完成：隐藏读取 MiMo Token → 启动 Web 网关 → 用独立应用窗口打开页面。
# 打开后你只需要：点「录入真实手表结果」（数值已按手表结束页填好）→ 点「生成复盘」。
#
#   .\start-demo.ps1                        # 默认 mimo-v2.5-pro
#   .\start-demo.ps1 -Model mimo-v2.5       # 换模型
#   .\start-demo.ps1 -Capture               # 额外用无头浏览器出图，作为真机证据
#
# Token 只存在于本进程环境变量，不写入任何文件；Token 一旦在聊天或截图里出现必须撤销。

param(
    [ValidateSet('mimo-v2.5', 'mimo-v2.5-pro')]
    [string]$Model = 'mimo-v2.5-pro',

    [int]$Port = 4173,

    [switch]$Capture
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$chromeCandidates = @(
    'C:\Program Files\Google\Chrome\Application\chrome.exe',
    'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
    (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe'),
    'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
)
$browser = $chromeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $browser) { throw '未找到 Chrome 或 Edge，请安装后重试。' }

# 上一次演示若非正常退出，网关可能仍占用端口。只回收本项目自己的 node server.mjs，
# 其他进程一律不动，避免误杀。
$stale = Get-CimInstance Win32_Process -Filter "Name = 'node.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*server.mjs*' }
foreach ($process in $stale) {
    Write-Host "回收上次残留的网关进程 (PID $($process.ProcessId))…" -ForegroundColor DarkGray
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}
if ($stale) { Start-Sleep -Milliseconds 800 }

if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    $owner = (Get-NetTCPConnection -LocalPort $Port -State Listen |
        Select-Object -First 1).OwningProcess
    throw "端口 $Port 已被其他进程占用（PID $owner）。关闭它，或改用 .\start-demo.ps1 -Port 4174。"
}

Write-Host 'VelaRide AI 演示网关' -ForegroundColor Cyan
Write-Host "模型: $Model    端口: $Port"
Write-Host '粘贴 MiMo Token（隐藏输入，不写入文件、不落盘）'
$secureToken = Read-Host 'MiMo Token' -AsSecureString
if ($secureToken.Length -eq 0) { throw 'Token 不能为空。' }

$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
$server = $null

try {
    $env:ANTHROPIC_AUTH_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
    $env:ANTHROPIC_BASE_URL = 'https://token-plan-cn.xiaomimimo.com/anthropic'
    $env:ANTHROPIC_MODEL = $Model
    $env:PORT = "$Port"

    $server = Start-Process -FilePath 'node' -ArgumentList 'server.mjs' `
        -WorkingDirectory $PSScriptRoot -PassThru -WindowStyle Hidden

    $ready = $false
    foreach ($attempt in 1..40) {
        Start-Sleep -Milliseconds 500
        if ($server.HasExited) { throw "网关进程提前退出，退出码 $($server.ExitCode)。" }
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 2
            if ($health.ok) { $ready = $true; break }
        }
        catch { }
    }
    if (-not $ready) { throw "网关在 20 秒内未就绪，请检查 http://127.0.0.1:$Port/api/health。" }

    if ($health.ai -eq 'fallback') {
        throw '服务端报告 ai=fallback，MiMo 凭据未生效。请确认粘贴的是 token-plan 的 Token。'
    }

    Write-Host ''
    Write-Host "网关就绪：AI 模式 = $($health.ai)" -ForegroundColor Green
    Write-Host '接下来在页面里：① 录入真实手表结果（数值已填好） ② 生成复盘' -ForegroundColor Yellow
    Write-Host '看到「由 Xiaomi MiMo 生成」再开始录屏；出现「降级」说明调用失败。' -ForegroundColor Yellow

    if ($Capture) {
        Write-Host ''
        Write-Host '抓取真机证据截图…' -ForegroundColor Cyan
        & node (Join-Path $PSScriptRoot '..\scripts\capture-demo.mjs') `
            --url "http://127.0.0.1:$Port" --out (Join-Path $PSScriptRoot '..\submission-preview')
    }

    Write-Host ''
    Write-Host '打开演示窗口。关闭该窗口即结束本次演示。' -ForegroundColor Green
    Start-Process -FilePath $browser `
        -ArgumentList "--app=http://127.0.0.1:$Port", '--window-size=1600,1000', '--window-position=40,40' `
        -Wait
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer)
    Remove-Item Env:ANTHROPIC_AUTH_TOKEN -ErrorAction SilentlyContinue
    Remove-Item Env:ANTHROPIC_BASE_URL -ErrorAction SilentlyContinue
    Remove-Item Env:ANTHROPIC_MODEL -ErrorAction SilentlyContinue
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        Write-Host '网关已停止。' -ForegroundColor DarkGray
    }
}
