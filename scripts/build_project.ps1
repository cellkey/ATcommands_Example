#!/usr/bin/env powershell
# Build script for ESP-IDF project.
# Run this from "ESP-IDF 5.3 PowerShell" (Start Menu). Cursor's terminal does not
# have the IDF environment; use the ESP-IDF window for idf.py build/flash/monitor.

Set-Location "c:\Users\Danny\ESP32_projects\ATcommands_Example"

# If we're not in an IDF environment, try to activate it (e.g. when run from Cursor).
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1") {
        Write-Host "Activating ESP-IDF environment..."
        & "C:\Espressif\frameworks\esp-idf-v5.3.1\export.ps1"
    }
    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        Write-Host "idf.py not found. Open 'ESP-IDF 5.3 PowerShell' from Start Menu, then run:"
        Write-Host "  cd C:\Users\Danny\ESP32_projects\ATcommands_Example"
        Write-Host "  .\build_project.ps1"
        exit 1
    }
}

Write-Host "Building project..."
idf.py build