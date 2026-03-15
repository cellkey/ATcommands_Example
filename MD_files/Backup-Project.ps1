# Smart Backup Script for ESP32 Projects
# Uses .gitignore patterns to backup only essential files

param(
    [string]$BackupPath = "D:\ESP32_Backups",
    [switch]$IncludeTimestamp = $true
)

# Get project root (parent directory if script is in MD_files, otherwise use script directory)
$ScriptDirName = Split-Path -Leaf $PSScriptRoot
if ($ScriptDirName -eq "MD_files") {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
} else {
    $ProjectRoot = $PSScriptRoot
}
$ProjectName = Split-Path -Leaf $ProjectRoot

# Create backup folder with timestamp
if ($IncludeTimestamp) {
    $Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $BackupFolder = Join-Path $BackupPath "$ProjectName`_$Timestamp"
} else {
    $BackupFolder = Join-Path $BackupPath $ProjectName
}

Write-Host "=== ESP32 Project Backup ===" -ForegroundColor Cyan
Write-Host "Source: $ProjectRoot"
Write-Host "Destination: $BackupFolder"
Write-Host ""

# Read .gitignore patterns (if exists)
$IgnorePatterns = @(
    "build",
    "sdkconfig",
    "sdkconfig.old",
    "*.pyc",
    "__pycache__",
    ".vscode\settings.json",
    "*.log",
    "*.bin",
    "*.elf",
    "*.map"
)

$GitignorePath = Join-Path $ProjectRoot ".gitignore"
if (Test-Path $GitignorePath) {
    Get-Content $GitignorePath | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#")) {
            $IgnorePatterns += $line
        }
    }
    Write-Host "Loaded ignore patterns from .gitignore" -ForegroundColor Green
} else {
    Write-Host "No .gitignore found, using default patterns" -ForegroundColor Yellow
}

# Function to check if path should be ignored
function Should-Ignore {
    param([string]$RelativePath)
    
    foreach ($pattern in $IgnorePatterns) {
        $pattern = $pattern.Replace("/", "\").TrimEnd("\")
        
        # Exact match
        if ($RelativePath -eq $pattern) { return $true }
        
        # Directory match
        if ($RelativePath.StartsWith("$pattern\")) { return $true }
        
        # Wildcard match
        if ($pattern.Contains("*")) {
            if ($RelativePath -like $pattern) { return $true }
        }
    }
    
    return $false
}

# Create backup directory
New-Item -ItemType Directory -Force -Path $BackupFolder | Out-Null

# Copy files recursively
$FilesCopied = 0
$FilesSkipped = 0
$TotalSize = 0

Get-ChildItem -Recurse -Path $ProjectRoot -File | ForEach-Object {
    $RelativePath = $_.FullName.Substring($ProjectRoot.Length + 1)
    
    if (Should-Ignore $RelativePath) {
        $FilesSkipped++
        Write-Host "  Skip: $RelativePath" -ForegroundColor DarkGray
    } else {
        $DestPath = Join-Path $BackupFolder $RelativePath
        $DestDir = Split-Path $DestPath
        
        if (-not (Test-Path $DestDir)) {
            New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
        }
        
        Copy-Item $_.FullName -Destination $DestPath
        $FilesCopied++
        $TotalSize += $_.Length
        Write-Host "  Copy: $RelativePath" -ForegroundColor Green
    }
}

$TotalSizeMB = [math]::Round($TotalSize / 1MB, 2)

Write-Host ""
Write-Host "=== Backup Complete ===" -ForegroundColor Cyan
Write-Host "Files copied: $FilesCopied" -ForegroundColor Green
Write-Host "Files skipped: $FilesSkipped" -ForegroundColor Yellow
Write-Host "Total size: $TotalSizeMB MB" -ForegroundColor Green
Write-Host "Location: $BackupFolder" -ForegroundColor Cyan
Write-Host ""
Write-Host "To restore: Copy contents back to project folder" -ForegroundColor White
