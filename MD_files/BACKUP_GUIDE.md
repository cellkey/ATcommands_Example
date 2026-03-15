# Backup & Sync Guide (No Git Required)

## Quick Start

### Option 1: Backup to External Drive

```powershell
# Run from project folder
.\Backup-Project.ps1 -BackupPath "D:\MyBackups"
```

Creates a timestamped backup like: `D:\MyBackups\EG_gatt_server_20241230_143052\`

### Option 2: Sync to USB/Network

```powershell
# Sync to USB drive
.\Sync-Project.ps1 "E:\USB_Backup"

# Test first (dry run)
.\Sync-Project.ps1 "E:\USB_Backup" -DryRun
```

Mirrors project to: `E:\USB_Backup\EG_gatt_server\`

## What Gets Backed Up

✅ **Included** (essential files):
- All source code (*.c, *.h)
- CMakeLists.txt
- Configuration files (sdkconfig.defaults*)
- Documentation (*.md)
- .gitignore
- .vscode/c_cpp_properties.json

❌ **Excluded** (auto-generated/PC-specific):
- build/ folder (~100+ MB)
- sdkconfig (~50 KB, auto-generated)
- .vscode/settings.json (PC-specific paths)
- *.bin, *.elf files
- *.log files

**Result:** Backup is ~2-5 MB instead of 100+ MB!

## Common Scenarios

### Scenario 1: Move to New PC

**On old PC:**
```powershell
.\Sync-Project.ps1 "E:\USB"
```

**On new PC:**
```powershell
# Copy from USB
cp -Recurse E:\USB\EG_gatt_server C:\Users\Danny\ESP32_projects\

# Follow setup guide
cd C:\Users\Danny\ESP32_projects\EG_gatt_server
# See SETUP_NEW_PC.md
```

### Scenario 2: Daily Backup

**Create scheduled task:**
```powershell
# Run this once
$Action = New-ScheduledTaskAction -Execute "PowerShell.exe" -Argument "-File C:\Users\Danny\ESP32_projects\EG_gatt_server\Backup-Project.ps1 -BackupPath D:\Backups"
$Trigger = New-ScheduledTaskTrigger -Daily -At 6PM
Register-ScheduledTask -TaskName "ESP32_Daily_Backup" -Action $Action -Trigger $Trigger
```

### Scenario 3: Keep Two PCs in Sync

**Use shared network folder:**
```powershell
# On PC 1
.\Sync-Project.ps1 "\\HOMESERVER\SharedProjects"

# On PC 2
.\Sync-Project.ps1 "\\HOMESERVER\SharedProjects"
```

Both PCs sync to same location. Last change wins.

## Restore from Backup

```powershell
# Just copy files back
cp -Recurse "D:\Backups\EG_gatt_server_20241230_143052\*" "C:\Users\Danny\ESP32_projects\EG_gatt_server\"

# Then rebuild
idf.py build
```

## Customize What Gets Backed Up

Edit `.gitignore` to change what's excluded:

```gitignore
# To ALSO backup build folder (not recommended):
# build/    ← Comment out by adding #

# To exclude your custom folder:
MyTestFolder/    ← Add new line
```

Scripts read `.gitignore` automatically!

## Script Reference

### Backup-Project.ps1
```powershell
# Basic backup
.\Backup-Project.ps1 -BackupPath "D:\Backups"

# No timestamp (overwrite previous)
.\Backup-Project.ps1 -BackupPath "D:\Backups" -IncludeTimestamp:$false
```

### Sync-Project.ps1
```powershell
# Sync to destination
.\Sync-Project.ps1 "E:\USB"

# Test without copying
.\Sync-Project.ps1 "E:\USB" -DryRun

# Sync to network
.\Sync-Project.ps1 "\\PC2\Projects"
```

## For New Projects

Copy these files to any new ESP32 project:
```powershell
cp .gitignore, Backup-Project.ps1, Sync-Project.ps1, BACKUP_GUIDE.md ..\NewProject\
```

The scripts work for ANY project with a `.gitignore` file!

## Troubleshooting

**"Script cannot be loaded" error:**
```powershell
# Run once (as Administrator)
Set-ExecutionPolicy RemoteSigned
```

**"Access denied" to destination:**
- Check drive is mounted
- Verify write permissions
- Try different path

**Want to see what will be copied:**
```powershell
.\Sync-Project.ps1 "E:\USB" -DryRun
```

## Git vs Manual Backup

| Feature | Git | Manual Scripts |
|---------|-----|----------------|
| Version history | ✅ | ❌ |
| Auto-sync | ✅ | ❌ |
| Merge changes | ✅ | ❌ |
| Simple backup | ✅ | ✅ |
| No learning curve | ❌ | ✅ |
| Works offline | ✅ | ✅ |

**Recommendation:** Start with scripts, learn Git later!
