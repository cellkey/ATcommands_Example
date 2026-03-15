# ESP32 Monitor with Logging Script
# Usage: .\monitor_with_log.ps1

param(
    [string]$LogDir = "logs",
    [string]$ProjectName = "ATCommand"
)

# Create logs directory if it doesn't exist
if (!(Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir | Out-Null
}

# Generate timestamp for filename
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = Join-Path $LogDir "${ProjectName}_monitor_${timestamp}.txt"

Write-Host "🚀 Starting ESP32 Monitor with logging..." -ForegroundColor Green
Write-Host "📁 Log file: $logFile" -ForegroundColor Cyan
Write-Host "⚡ Press Ctrl+C to stop monitoring" -ForegroundColor Yellow
Write-Host "=" * 50

try {
    # Start monitor with both console output and file logging
    idf.py monitor --print_filter "*" | Tee-Object -FilePath $logFile
}
catch {
    Write-Host "❌ Error: $_" -ForegroundColor Red
    Write-Host "💡 Make sure ESP-IDF environment is loaded" -ForegroundColor Yellow
}
finally {
    Write-Host ""
    Write-Host "📄 Monitor session ended. Log saved to: $logFile" -ForegroundColor Green
    
    # Show log file info
    if (Test-Path $logFile) {
        $fileSize = (Get-Item $logFile).Length
        Write-Host "📊 Log file size: $([math]::Round($fileSize/1KB, 2)) KB" -ForegroundColor Cyan
    }
    
    # Ask if user wants to open the log
    $openLog = Read-Host "🔍 Open log file? (y/n)"
    if ($openLog -eq 'y' -or $openLog -eq 'Y') {
        Start-Process notepad $logFile
    }
}