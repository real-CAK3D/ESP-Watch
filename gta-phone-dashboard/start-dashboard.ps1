$ErrorActionPreference = "Stop"

$portOpen = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue
if ($portOpen) {
  exit 0
}

$dashboardDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Process -FilePath python -ArgumentList @("serve_dashboard.py") -WorkingDirectory $dashboardDir -WindowStyle Hidden
