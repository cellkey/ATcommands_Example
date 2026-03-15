# ESP-IDF Setup and Build Script
# This script installs ESP-IDF tools and builds the project

Write-Host "Setting up ESP-IDF environment..." -ForegroundColor Cyan

# Set environment variables
$env:IDF_TOOLS_PATH = "c:\Espressif"
$env:IDF_PATH = "C:\Users\Danny\esp\v5.5.1\esp-idf"
$python = "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$idfTools = "C:\Users\Danny\esp\v5.5.1\esp-idf\tools\idf_tools.py"
$idfPy = "C:\Users\Danny\esp\v5.5.1\esp-idf\tools\idf.py"

Write-Host "Installing ESP-IDF tools (this may take 10-20 minutes)..." -ForegroundColor Yellow
& $python $idfTools install all

if ($LASTEXITCODE -eq 0) {
    Write-Host "Tools installed successfully!" -ForegroundColor Green
    
    Write-Host "Exporting environment variables..." -ForegroundColor Cyan
    & $python $idfTools export --format=key-value | ForEach-Object {
        if ($_ -match '^(.+?)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }
    
    Write-Host "Building project..." -ForegroundColor Cyan
    & $python $idfPy build
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`nBuild completed successfully!" -ForegroundColor Green
    } else {
        Write-Host "`nBuild failed!" -ForegroundColor Red
    }
} else {
    Write-Host "Tool installation failed!" -ForegroundColor Red
}
