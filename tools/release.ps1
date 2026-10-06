# Build the watch firmware + Android app and publish them as a GitHub release.
#
#   .\release.ps1 -Version 2.1.0 -Notes ..\docs\release-notes\v2.1.0.md
#
# Version numbers in firmware/platformio.ini (FW_VERSION) and android/app/build.gradle.kts
# (versionName, versionCode) are updated automatically. The phone app finds new releases through the
# GitHub API and expects these asset names:
#   GTA-Watch-v<ver>-merged-0x0.bin    full image for USB flashing at 0x0
#   GTA-Watch-v<ver>-app-0x10000.bin   app image, also what the phone sends to the watch over Bluetooth
#   GTA-Watch.apk                      Android app (debug-signed on this PC, so updates install over each other)
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$Notes,
  [switch]$Draft
)
$ErrorActionPreference = "Stop"
$root = "G:\GTA-Watch"
$repo = "real-CAK3D/ESP-Watch"
$notesPath = (Resolve-Path $Notes).Path

function Set-FileText($path, $text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false)) }

# ---- versions
$ini = "$root\firmware\platformio.ini"
Set-FileText $ini ((Get-Content $ini -Raw) -replace 'FW_VERSION=\\"[^"\\]*\\"', "FW_VERSION=\`"$Version\`"")
$gradle = "$root\android\app\build.gradle.kts"
$g = Get-Content $gradle -Raw
$code = [int]([regex]::Match($g, 'versionCode = (\d+)').Groups[1].Value) + 1
$g = $g -replace 'versionCode = \d+', "versionCode = $code" -replace 'versionName = "[^"]*"', "versionName = `"$Version`""
Set-FileText $gradle $g
Write-Host "version $Version (app versionCode $code)"

# ---- build
$env:PLATFORMIO_CORE_DIR = "$root\.pio"
Push-Location "$root\firmware"
& "$root\tools\pio-venv\Scripts\pio.exe" run | Select-String 'SUCCESS|FAILED|error'
if ($LASTEXITCODE -ne 0) { throw "firmware build failed" }
Pop-Location
$env:JAVA_HOME = "G:\Android\jdk"; $env:GRADLE_USER_HOME = "G:\Android\gradle"; $env:ANDROID_HOME = "G:\Android\sdk"
Push-Location "$root\android"
.\gradlew.bat assembleRelease --console=plain -q
if ($LASTEXITCODE -ne 0) { throw "app build failed" }
Pop-Location

# ---- package
$out = "$root\build-tmp\release-v$Version"
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out | Remove-Item
$fw = "$root\firmware\.pio\build\watch"
Copy-Item "$fw\firmware.factory.bin" "$out\GTA-Watch-v$Version-merged-0x0.bin"
Copy-Item "$fw\firmware.bin" "$out\GTA-Watch-v$Version-app-0x10000.bin"
Copy-Item "$root\android\app\build\outputs\apk\release\app-release.apk" "$out\GTA-Watch.apk"
Push-Location $out
(Get-ChildItem -File | Where-Object Name -ne SHA256SUMS.txt | ForEach-Object {
  "{0}  {1}" -f (Get-FileHash $_.Name -Algorithm SHA256).Hash.ToLower(), $_.Name
}) -join "`n" | Set-Content SHA256SUMS.txt -NoNewline
Pop-Location
Get-ChildItem $out | Format-Table Name, Length

# ---- commit, tag, publish (git prints harmless warnings on stderr, so check exit codes instead)
$ErrorActionPreference = "Continue"
Push-Location $root
git add -A 2>$null
git commit -q -m "GTA-Watch v$Version" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
if ($LASTEXITCODE -ne 0) { throw "git commit failed" }
git tag -a "v$Version" -m "GTA-Watch v$Version"
if ($LASTEXITCODE -ne 0) { throw "git tag failed" }
git push -q origin main "v$Version"
if ($LASTEXITCODE -ne 0) { throw "git push failed" }
$ghArgs = @("release", "create", "v$Version", "-R", $repo, "--title", "GTA-Watch v$Version", "--notes-file", $notesPath, "--latest")
if ($Draft) { $ghArgs += "--draft" }
$ghArgs += (Get-ChildItem $out).FullName
gh @ghArgs
Pop-Location
