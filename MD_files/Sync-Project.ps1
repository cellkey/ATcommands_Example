# Quick Sync to USB/Another Location
# Respects .gitignore patterns automatically

param(
    [Parameter(Mandatory=$true)]
    [string]$DestinationPath,
    [switch]$DryRun = $false
)

$ProjectRoot = $PSScriptRoot
$ProjectName = Split-Path -Leaf $ProjectRoot
$FullDestPath = Join-Path $DestinationPath $ProjectName

Write-Host "=== Quick Project Sync ===" -ForegroundColor Cyan
Write-Host "From: $ProjectRoot"
Write-Host "To: $FullDestPath"
if ($DryRun) {
    Write-Host "MODE: DRY RUN (no files will be copied)" -ForegroundColor Yellow
}
Write-Host ""

# Load ignore patterns
$IgnorePatterns = @()
$GitignorePath = Join-Path $ProjectRoot ".gitignore"
if (Test-Path $GitignorePath) {
    Get-Content $GitignorePath | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#")) {
            $IgnorePatterns += $line.Replace("/", "\")
        }
    }
}

# Build robocopy exclude parameters
$ExcludeDirs = @()
$ExcludeFiles = @()

foreach ($pattern in $IgnorePatterns) {
    if ($pattern.EndsWith("\") -or -not $pattern.Contains(".")) {
        # Directory pattern
        $ExcludeDirs += $pattern.TrimEnd("\")
    } else {
        # File pattern
        $ExcludeFiles += $pattern
    }
}

# Build robocopy command
$RobocopyArgs = @(
    $ProjectRoot,
    $FullDestPath,
    "/MIR",  # Mirror (sync)
    "/NP",   # No progress per file
    "/NDL",  # No directory list
    "/NJH",  # No job header
    "/NJS"   # No job summary
)

if ($ExcludeDirs.Count -gt 0) {
    $RobocopyArgs += "/XD"
    $RobocopyArgs += $ExcludeDirs
}

if ($ExcludeFiles.Count -gt 0) {
    $RobocopyArgs += "/XF"
    $RobocopyArgs += $ExcludeFiles
}

if ($DryRun) {
    $RobocopyArgs += "/L"  # List only
}

Write-Host "Excluded directories: $($ExcludeDirs -join ', ')" -ForegroundColor Yellow
Write-Host "Excluded files: $($ExcludeFiles -join ', ')" -ForegroundColor Yellow
Write-Host ""

# Execute sync
Write-Host "Syncing..." -ForegroundColor Cyan
robocopy @RobocopyArgs

Write-Host ""
Write-Host "Sync complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Usage examples:" -ForegroundColor White
Write-Host "  .\Sync-Project.ps1 D:\USB" -ForegroundColor Gray
Write-Host "  .\Sync-Project.ps1 \\OtherPC\SharedFolder" -ForegroundColor Gray
Write-Host "  .\Sync-Project.ps1 D:\USB -DryRun" -ForegroundColor Gray
