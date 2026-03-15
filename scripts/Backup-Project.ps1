# Convenience wrapper for Backup-Project.ps1
# Run from project root: .\Backup-Project.ps1 -BackupPath "D:\MyBackups"

param(
    [string]$BackupPath = "D:\ESP32_Backups",
    [switch]$IncludeTimestamp = $true
)

# Call the actual backup script
& "$PSScriptRoot\MD_files\Backup-Project.ps1" -BackupPath $BackupPath -IncludeTimestamp:$IncludeTimestamp
