param(
    [string]$VenvPath = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigPath = Join-Path $ScriptDir "nt_config.json"

# --- Resolve venv path ---
if ($VenvPath) {
    if ($env:VIRTUAL_ENV -and ($env:VIRTUAL_ENV -ne $VenvPath)) {
        [Console]::Error.WriteLine("[WARNING] -VenvPath '$VenvPath' differs from activated venv '$env:VIRTUAL_ENV'. Using -VenvPath.")
    }
    [Console]::Error.WriteLine("[venv] (-VenvPath) $VenvPath")
} elseif ($env:VIRTUAL_ENV) {
    $VenvPath = $env:VIRTUAL_ENV
    [Console]::Error.WriteLine("[venv] (activated) $VenvPath")
} elseif (Test-Path $ConfigPath) {
    $config = Get-Content $ConfigPath -Raw | ConvertFrom-Json
    $VenvPath = $config.windows.venv_path
    [Console]::Error.WriteLine("[venv] (nt_config.json) $VenvPath")
}

if (-not $VenvPath) {
    Write-Host "[ERROR] No venv found. Use -VenvPath, activate a venv, or set windows.venv_path in nt_config.json" -ForegroundColor Red
    exit 1
}

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " PyDecisionGraph NT Build Script" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# Activate venv
$ActivateScript = Join-Path $VenvPath "Scripts\Activate.ps1"
if (-not (Test-Path $ActivateScript)) {
    Write-Host "[ERROR] Venv not found: $VenvPath" -ForegroundColor Red
    exit 1
}

Write-Host "[venv] Activating: $VenvPath" -ForegroundColor Green
. $ActivateScript

# Verify python
$py = & python --version 2>&1
Write-Host "[python] $py" -ForegroundColor Green

# Clean build artifacts
Write-Host "[clean] Removing build artifacts..." -ForegroundColor Yellow
Push-Location $ScriptDir
try {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue build, "PyDecisionGraph.egg-info", "decision_graph\includes"
    Write-Host "[clean] Done" -ForegroundColor Green
}
finally {
    Pop-Location
}

# Build
Write-Host "[build] Compiling Cython extensions..." -ForegroundColor Yellow
Push-Location $ScriptDir
try {
    python setup.py build_ext --inplace --verbose --force
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[build] FAILED with exit code $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host "[build] Complete — decision_graph compiled in-place" -ForegroundColor Green
}
finally {
    Pop-Location
}

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " Build successful." -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan
