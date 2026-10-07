# ==============================================================================
# NT Step Driver — remote-side helper for the nt_build.py workflow.
#
# All environment/project settings are read from an nt_config.json — no
# hardcoded paths. Pass -Config explicitly, or place a real nt_config.json
# next to this script. Committed configs may be sanitized templates
# (<placeholder> values); those are rejected with an error.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File nt_steps.ps1 <step> `
#       [-Config <path\to\nt_config.json>] [-PytestArgs "<extra pytest args>"]
#
# Steps:
#   info            Print resolved settings + interpreter sanity check
#   install         pip install -U <project.remote_root> --no-build-isolation
#   ensure_pytest   pip install pytest into the venv
#   ensure_deps     pip install -r <remote_root>\requirements.txt
#   test_src        pytest the source tree (cwd = remote_root)
#   test_installed  pytest against the installed package (cwd = parent dir)
# ==============================================================================

param(
    [Parameter(Position = 0)][string]$Step,
    [string]$Config = (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "nt_config.json"),
    [string]$PytestArgs = ""
)

$ErrorActionPreference = "Stop"

if (-not $Step) {
    Write-Host "[ERROR] No step given. Steps: info | install | ensure_pytest | ensure_deps | test_src | test_installed" -ForegroundColor Red
    exit 2
}

if (-not (Test-Path $Config)) {
    Write-Host "[ERROR] Config not found: $Config" -ForegroundColor Red
    exit 1
}

$cfg = Get-Content -Raw $Config | ConvertFrom-Json

$VenvPath = $cfg.windows.venv_path
$ProjRoot = $cfg.project.remote_root
if ("$VenvPath$ProjRoot" -match '<') {
    Write-Host "[ERROR] $Config looks like a sanitized template (contains <placeholder> values) — supply a real config via -Config." -ForegroundColor Red
    exit 1
}

$Py = Join-Path $VenvPath "Scripts\python.exe"
if (-not (Test-Path $Py)) {
    Write-Host "[ERROR] Venv python not found: $Py" -ForegroundColor Red
    exit 1
}

$ProxyArgs = @()
if ($cfg.windows.proxy) { $ProxyArgs += "--proxy=$($cfg.windows.proxy)" }

$ExtraPytest = @()
if ($PytestArgs) { $ExtraPytest = $PytestArgs -split '\s+' | Where-Object { $_ } }

# Project test-suite conventions: canonical files are test_*.py; the test
# directory is either test/ or tests/ depending on the project.
$PytestBase = @("-q", "--no-header", "-p", "no:cacheprovider", "-o", "python_files=test_*.py")

function Get-TestDir {
    foreach ($d in @("test", "tests")) {
        if (Test-Path (Join-Path $ProjRoot $d)) { return $d }
    }
    Write-Host "[ERROR] No test/ or tests/ directory under $ProjRoot" -ForegroundColor Red
    exit 1
}

switch ($Step) {
    "info" {
        Write-Host "Config : $Config"
        Write-Host "Python : $Py"
        Write-Host "Project: $ProjRoot"
        Write-Host "Proxy  : $($cfg.windows.proxy)"
        & $Py --version
        exit $LASTEXITCODE
    }
    "install" {
        & $Py -m pip install -U $ProjRoot @ProxyArgs --no-build-isolation
        exit $LASTEXITCODE
    }
    "ensure_pytest" {
        & $Py -m pip install pytest @ProxyArgs -q
        exit $LASTEXITCODE
    }
    "ensure_deps" {
        $req = Join-Path $ProjRoot "requirements.txt"
        if (-not (Test-Path $req)) {
            Write-Host "[skip] No requirements.txt in $ProjRoot"
            exit 0
        }
        & $Py -m pip install -r $req @ProxyArgs -q
        exit $LASTEXITCODE
    }
    "test_src" {
        $testDir = Get-TestDir
        Set-Location $ProjRoot
        & $Py -m pytest $testDir @PytestBase @ExtraPytest
        exit $LASTEXITCODE
    }
    "test_installed" {
        $testDir = Get-TestDir
        Set-Location (Split-Path -Parent $ProjRoot)
        & $Py -m pytest (Join-Path $ProjRoot $testDir) @PytestBase @ExtraPytest
        exit $LASTEXITCODE
    }
    default {
        Write-Host "[ERROR] Unknown step: $Step. Steps: info | install | ensure_pytest | ensure_deps | test_src | test_installed" -ForegroundColor Red
        exit 2
    }
}
