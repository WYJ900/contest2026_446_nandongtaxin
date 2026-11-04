param([string]$SdkRoot, [string]$JavaHome)
$ErrorActionPreference = 'Stop'
$mobileWorkspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $SdkRoot) { $SdkRoot = Join-Path $mobileWorkspace '.local-tools/android-sdk' }
if (-not (Test-Path (Join-Path $SdkRoot 'platforms/android-35/android.jar'))) {
    throw 'Android SDK platform 35 is missing. Pass -SdkRoot with platforms;android-35 and build-tools;35.0.0 installed.'
}
if (-not $JavaHome) { $JavaHome = $env:JAVA_HOME }
if (-not $JavaHome -or -not (Test-Path (Join-Path $JavaHome 'bin/javac.exe'))) {
    $JavaHome = 'E:/AppDownload/Android_Studio/jbr'
}
if (-not (Test-Path (Join-Path $JavaHome 'bin/javac.exe'))) { throw 'Pass -JavaHome with a JDK 17 or newer.' }
$env:JAVA_HOME = $JavaHome
$mobileBuild = Join-Path $PSScriptRoot ('.build/run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$mobileTools = Join-Path $SdkRoot 'build-tools/35.0.0'
$mobilePlatform = Join-Path $SdkRoot 'platforms/android-35/android.jar'
$mobileApp = Join-Path $PSScriptRoot 'app/src/main'
$mobileExport = Join-Path $mobileWorkspace 'exports/mobile'
@($mobileBuild, "$mobileBuild/generated", "$mobileBuild/classes", "$mobileBuild/dex", "$mobileBuild/tests", $mobileExport) |
    ForEach-Object { New-Item -ItemType Directory -Force -Path $_ | Out-Null }
function Invoke-MobileTool([string]$Tool, [string[]]$ToolArgs) {
    $ErrorActionPreference = 'Continue'
    & $Tool @ToolArgs 2>&1 | ForEach-Object { Write-Output "$_" }
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool" }
}
Invoke-MobileTool "$JavaHome/bin/javac.exe" @('-encoding', 'UTF-8', '-d', "$mobileBuild/tests",
    "$mobileApp/java/com/velaride/companion/WireCodec.java", "$PSScriptRoot/tests/WireCodecTest.java",
    "$mobileApp/java/com/velaride/companion/WatchHandshake.java", "$PSScriptRoot/tests/WatchHandshakeTest.java",
    "$mobileApp/java/com/velaride/companion/RideHistory.java", "$PSScriptRoot/tests/RideHistoryTest.java",
    "$mobileApp/java/com/velaride/companion/GpsTracker.java", "$PSScriptRoot/tests/GpsTrackerTest.java",
    "$mobileApp/java/com/velaride/companion/RideGps.java",
    "$mobileApp/java/com/velaride/companion/RideSafety.java",
    "$mobileApp/java/com/velaride/companion/HistorySync.java", "$PSScriptRoot/tests/HistorySyncTest.java",
    "$mobileApp/java/com/velaride/companion/RideReview.java", "$PSScriptRoot/tests/RideReviewTest.java",
    "$mobileApp/java/com/velaride/companion/ReviewClient.java", "$PSScriptRoot/tests/ReviewClientTest.java",
    "$mobileApp/java/com/velaride/companion/RideExtras.java",
    "$mobileApp/java/com/velaride/companion/RoutePlan.java", "$PSScriptRoot/tests/RoutePlanTest.java")
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.WireCodecTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.WatchHandshakeTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.RideHistoryTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.GpsTrackerTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.HistorySyncTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.RideReviewTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.ReviewClientTest')
Invoke-MobileTool "$JavaHome/bin/java.exe" @('-ea', '-cp', "$mobileBuild/tests", 'com.velaride.companion.RoutePlanTest')
Invoke-MobileTool "$mobileTools/aapt2.exe" @('compile', '--dir', "$mobileApp/res", '-o', "$mobileBuild/resources.zip")
Invoke-MobileTool "$mobileTools/aapt2.exe" @('link', '-o', "$mobileBuild/base.apk", '-I', $mobilePlatform,
    '--manifest', "$mobileApp/AndroidManifest.xml", '--java', "$mobileBuild/generated", '--min-sdk-version', '26',
    '--target-sdk-version', '35', '--version-code', '10', '--version-name', '0.1.9', "$mobileBuild/resources.zip")
$mobileSources = @(Get-ChildItem "$mobileApp/java", "$mobileBuild/generated" -Recurse -Filter '*.java' |
    ForEach-Object { '"' + $_.FullName.Replace('\', '/') + '"' })
[System.IO.File]::WriteAllLines("$mobileBuild/sources.txt", $mobileSources, (New-Object System.Text.UTF8Encoding($false)))
Invoke-MobileTool "$JavaHome/bin/javac.exe" @('--release', '8', '-encoding', 'UTF-8',
    '-classpath', $mobilePlatform, '-d', "$mobileBuild/classes", "@$mobileBuild/sources.txt")
Invoke-MobileTool "$JavaHome/bin/jar.exe" @('--create', '--file', "$mobileBuild/classes.jar", '-C', "$mobileBuild/classes", '.')
Invoke-MobileTool "$mobileTools/d8.bat" @('--release', '--min-api', '26', '--lib', $mobilePlatform,
    '--output', "$mobileBuild/dex", "$mobileBuild/classes.jar")
Copy-Item -LiteralPath "$mobileBuild/base.apk" -Destination "$mobileBuild/unsigned.apk"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$mobileArchive = [System.IO.Compression.ZipFile]::Open("$mobileBuild/unsigned.apk", 'Update')
try {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($mobileArchive, "$mobileBuild/dex/classes.dex", 'classes.dex') | Out-Null
} finally { $mobileArchive.Dispose() }
Invoke-MobileTool "$mobileTools/zipalign.exe" @('-f', '4', "$mobileBuild/unsigned.apk", "$mobileBuild/aligned.apk")
$mobileKey = Join-Path $PSScriptRoot '.build/debug.keystore'
if (-not (Test-Path $mobileKey)) {
    Invoke-MobileTool "$JavaHome/bin/keytool.exe" @('-genkeypair', '-keystore', $mobileKey,
        '-storepass', 'android', '-keypass', 'android', '-alias', 'androiddebugkey',
        '-dname', 'CN=Android Debug,O=Android,C=US', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000')
}
$mobileApk = Join-Path $mobileExport 'VelaRide-0.1.9-debug.apk'
Invoke-MobileTool "$mobileTools/apksigner.bat" @('sign', '--ks', $mobileKey, '--ks-pass', 'pass:android',
    '--key-pass', 'pass:android', '--out', $mobileApk, "$mobileBuild/aligned.apk")
Invoke-MobileTool "$mobileTools/apksigner.bat" @('verify', '--verbose', $mobileApk)
Invoke-MobileTool "$mobileTools/aapt2.exe" @('dump', 'badging', $mobileApk)
Get-FileHash -LiteralPath $mobileApk -Algorithm SHA256 | Format-List Path,Hash
