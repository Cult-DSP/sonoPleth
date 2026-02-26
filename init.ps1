# init.ps1 - Native Windows setup for sonoPleth (PowerShell)
# Mirrors init.sh steps:
# 1) Create Python venv
# 2) Install Python deps
# 3) Run setupCppTools() from src.config.configCPP (OS-specific router)
# 4) Write .init_complete
#
# Usage:
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\init.ps1
#
# To activate venv in current shell automatically, dot-source:
#   . .\init.ps1

[CmdletBinding()]
param(
  [string]$VenvDir = "sonoPleth"
)

$ErrorActionPreference = "Continue"  # Don't stop on errors, handle them explicitly

function Section($title) {
  Write-Host "============================================================"
  Write-Host $title
  Write-Host "============================================================"
  Write-Host ""
}

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

# Detect OS and set appropriate paths
$IsWindowsOS = $PSVersionTable.Platform -eq "Win32NT" -or $env:OS -eq "Windows_NT"
if ($IsWindowsOS) {
    $ScriptsDir = "Scripts"
    $PythonExe = "python.exe"
    $PipExe = "pip.exe"
    $ActivateScript = "Activate.ps1"
} else {
    $ScriptsDir = "bin"
    $PythonExe = "python"
    $PipExe = "pip"
    $ActivateScript = "activate"
}

$venvScripts = Join-Path (Join-Path $ProjectRoot $VenvDir) $ScriptsDir
$venvPython = Join-Path $venvScripts $PythonExe
$venvPip = Join-Path $venvScripts $PipExe

$pythonCmd = $null
if (Get-Command python3 -ErrorAction SilentlyContinue) { $pythonCmd = "python3" }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $pythonCmd = "python" }
elseif (Get-Command py -ErrorAction SilentlyContinue) { $pythonCmd = "py" }
else { throw "Python not found. Install Python 3 and ensure it's on PATH." }

if (Test-Path $venvPython) {
  Write-Host "✓ Virtual environment already exists at $VenvDir/"
} else {
  Write-Host "Creating virtual environment..."
  & $pythonCmd -m venv $VenvDir
  if (-not (Test-Path $venvPython)) {
    throw "✗ Failed to create virtual environment at $VenvDir/"
  }
  Write-Host "✓ Virtual environment created"
}
Write-Host ""

Write-Host "Step 2: Installing Python dependencies..."

$reqPath = Join-Path $ProjectRoot "requirements.txt"
if (-not (Test-Path $reqPath)) {
  throw "✗ requirements.txt not found in repo root."
}

try {
  & $venvPip install -r $reqPath | Out-Host
  Write-Host "✓ Python dependencies installed"
} catch {
  Write-Host "✗ Error installing Python dependencies" -ForegroundColor Red
  throw
}
Write-Host ""

Write-Host "Step 3: Setting up C++ tools (allolib, embedded ADM extractor, spatial renderers)..."
Write-Host "  Project root: $ProjectRoot"
Write-Host "  Venv Python: $venvPython"
Write-Host "  Venv exists: $(Test-Path $venvPython)"

$cppOk = $true
try {
  Write-Host "  Running: $venvPython -c 'from src.config.configCPP import setupCppTools; ...'"
  Push-Location $ProjectRoot
  $args = @("-c", "from src.config.configCPP import setupCppTools; import sys; result = setupCppTools(); sys.exit(0 if result else 1)")
  & $venvPython $args
  $exitCode = $LASTEXITCODE
  Pop-Location
  Write-Host "  Exit code: $exitCode"
  if ($exitCode -ne 0) {
    $cppOk = $false
    Write-Host "  C++ setup failed with exit code $exitCode" -ForegroundColor Yellow
  }
} catch {
  $cppOk = $false
  Write-Host "  Exception during C++ setup: $($_.Exception.Message)" -ForegroundColor Red
}

if ($cppOk) {
  Write-Host "✓ C++ tools setup complete"
} else {
  Write-Host "⚠ Warning: C++ tools setup had issues — run .\init.ps1 again or check CMake logs" -ForegroundColor Yellow
}
Write-Host ""

Write-Host "Step 4: Creating initialization flag..."

$flagPath = Join-Path $ProjectRoot ".init_complete"
$timestamp = Get-Date

@"
# sonoPleth initialization complete
# Generated: $timestamp
# Python venv: $VenvDir/
# Delete this file to force re-initialization.
"@ | Set-Content -Path $flagPath -Encoding UTF8

Write-Host "✓ Initialization flag created (.init_complete)"
Write-Host ""

Section "✓ Initialization complete!"

Write-Host "Activating virtual environment..."

# Try to find the activation script - check both Windows (Scripts/) and Unix (bin/) paths
$activateScript = $null
$possiblePaths = @(
    (Join-Path $venvScripts $ActivateScript),  # Windows path: Scripts/Activate.ps1
    (Join-Path $venvScripts "../bin/Activate.ps1")  # Unix path: bin/Activate.ps1
)

foreach ($path in $possiblePaths) {
    if (Test-Path $path) {
        $activateScript = $path
        break
    }
}

if ($activateScript) {
    Write-Host "Found activation script at: $activateScript"
    . $activateScript
    Write-Host "✓ Virtual environment activated in current PowerShell session"
} else {
    Write-Host "⚠ Warning: Could not find activation script at any expected location" -ForegroundColor Yellow
    Write-Host "  Checked paths:" -ForegroundColor Yellow
    $possiblePaths | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
}

Write-Host ""
Write-Host "You can now run:"
Write-Host "  python utils/getExamples.py          # Download example files"
Write-Host "  python runPipeline.py <file.wav>     # Run the pipeline"
Write-Host ""
Write-Host "To reactivate the environment later, run:"
Write-Host "  .\sonoPleth\bin\Activate.ps1"
Write-Host ""
Write-Host "If you encounter dependency errors, delete .init_complete and re-run:"
Write-Host "  Remove-Item .init_complete; .\init.ps1"
Write-Host ""