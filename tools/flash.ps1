# Build the watch firmware and flash it over USB (COM5). Usage: .\flash.ps1 [-BuildOnly]
param([switch]$BuildOnly)
$env:PLATFORMIO_CORE_DIR = "G:\GTA-Watch\.pio"
Push-Location G:\GTA-Watch\firmware
try {
  $target = if ($BuildOnly) { @() } else { @("-t", "upload") }
  & G:\GTA-Watch\tools\pio-venv\Scripts\pio.exe run @target 2>&1 |
    Select-String -Pattern ' error|Error [0-9]|RAM:|Flash:|Hash of data verified|SUCCESS|FAILED'
} finally { Pop-Location }
