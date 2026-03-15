# Quick ESP-IDF Environment Initialization
# Run this with: . .\init_idf.ps1

$env:IDF_TOOLS_PATH = "c:\Espressif"
$env:IDF_PATH = "c:\Espressif\frameworks\esp-idf-v5.3.1"

# Prepend ESP-IDF Python to PATH (avoids Windows Store stub taking precedence)
$idfPython = "c:\Espressif\python_env\idf5.3_py3.11_env\Scripts"
if (Test-Path $idfPython) { $env:PATH = "$idfPython;$env:PATH" }

& "$env:IDF_PATH\export.ps1"

Write-Host "ESP-IDF environment ready!" -ForegroundColor Green
Write-Host "You can now use: idf.py build, idf.py flash, idf.py monitor, etc." -ForegroundColor Cyan
