# Quick Reference - Backup & Sync Commands

## Daily Use Commands

### Backup (Creates Timestamped Copies)
```powershell
# Basic backup with timestamp
.\Backup-Project.ps1 -BackupPath "D:\Backups"

# Creates: D:\Backups\EG_gatt_server_20251230_142838\

# Backup without timestamp (overwrites previous)
.\Backup-Project.ps1 -BackupPath "D:\Backups" -IncludeTimestamp:$false
```

### Sync (Mirrors Project)
```powershell
# Sync to destination (overwrites)
.\Sync-Project.ps1 "E:\USB"

# Test first (shows what will be copied)
.\Sync-Project.ps1 "E:\USB" -DryRun

# Sync to network drive
.\Sync-Project.ps1 "\\OtherPC\SharedFolder"
```

## What Gets Backed Up

✅ **Included** (Essential files):
- All source code: `*.c`, `*.h`
- Project configs: `CMakeLists.txt`, `sdkconfig.defaults*`
- Documentation: `*.md`, `README.md`
- Portable VS Code settings: `.vscode/c_cpp_properties.json`
- `.gitignore` file itself

❌ **Excluded** (Auto-generated/PC-specific):
- `build/` folder (~100+ MB)
- `sdkconfig` (auto-generated)
- `.vscode/settings.json` (PC-specific paths)
- Binary files: `*.bin`, `*.elf`, `*.map`
- Log files: `*.log`

**Result:** ~0.5-2 MB backup instead of 100+ MB!

## Common Scenarios

### Moving to New PC
```powershell
# On old PC - sync to USB
.\Sync-Project.ps1 "E:\USB"

# On new PC - copy from USB
cp -Recurse E:\USB\EG_gatt_server C:\Users\Danny\ESP32_projects\

# Then rebuild
cd C:\Users\Danny\ESP32_projects\EG_gatt_server
idf.py build
```

### Daily Work Backup
```powershell
# End of day - create backup
.\Backup-Project.ps1 -BackupPath "D:\Daily_Backups"

# Keeps history: project_20251230_170000, project_20251231_170000, etc.
```

### Two PCs in Sync
```powershell
# Use shared network folder
# On PC 1
.\Sync-Project.ps1 "\\HOMESERVER\Projects"

# On PC 2
.\Sync-Project.ps1 "\\HOMESERVER\Projects"

# Always sync before/after work session
```

### Quick USB Backup Before Testing
```powershell
# Before making risky changes
.\Sync-Project.ps1 "E:\USB"

# If something breaks, restore from USB
cp -Recurse E:\USB\EG_gatt_server C:\...\EG_gatt_server_restored
```

## Script Locations

Both scripts must be run **from project root folder**:
```
C:\Users\Danny\ESP32_projects\EG_gatt_server\
├── Backup-Project.ps1    ← Run from here
├── Sync-Project.ps1      ← Run from here
├── .gitignore            ← Tells scripts what to skip
└── main\
```

## Output Examples

### Backup Output:
```
=== ESP32 Project Backup ===
Source: C:\Users\Danny\ESP32_projects\EG_gatt_server
Destination: D:\Backups\EG_gatt_server_20251230_142838

Loaded ignore patterns from .gitignore
  Skip: build\esp-idf\...
  Copy: main\ESP32C3_EG_code_notification.c
  Copy: CMakeLists.txt
  ...

=== Backup Complete ===
Files copied: 45
Files skipped: 1631
Total size: 0.62 MB
Location: D:\Backups\EG_gatt_server_20251230_142838
```

### Sync Output:
```
=== Quick Project Sync ===
From: C:\Users\Danny\ESP32_projects\EG_gatt_server
To: E:\USB\EG_gatt_server

Excluded directories: build, sdkconfig, ...
Excluded files: *.log, *.bin, ...

Syncing...
  New File: main\ESP32C3_EG_code_notification.c
  Updated: CMakeLists.txt
  ...

Sync complete!
```

## Troubleshooting

### "Script cannot be loaded"
```powershell
# Run once as Administrator
Set-ExecutionPolicy RemoteSigned
```

### "Access denied"
- Check destination drive is writable
- Verify you have permissions
- Try different path

### Want to exclude additional files
Edit `.gitignore` and add pattern:
```
# Add to .gitignore
MyTestFolder/
*.temp
debug_output/
```
Scripts automatically read this file!

## For New Projects

Copy these files to any ESP32 project:
```powershell
# Navigate to new project
cd C:\...\NewProject

# Copy from working project
cp ..\EG_gatt_server\.gitignore .
cp ..\EG_gatt_server\Backup-Project.ps1 .
cp ..\EG_gatt_server\Sync-Project.ps1 .

# Use immediately
.\Backup-Project.ps1 -BackupPath "D:\Backups"
```

Same `.gitignore` works for **all ESP-IDF projects**!

## Related Files

- `BACKUP_GUIDE.md` - Detailed backup guide
- `GIT_SETUP_GUIDE.md` - Git version control guide  
- `SETUP_NEW_PC.md` - Setup ESP-IDF on new PC
- `.gitignore` - Defines what to skip

## Quick Tips

💡 **Backup before big changes** - Takes 5 seconds  
💡 **Sync to USB weekly** - Easy disaster recovery  
💡 **Use -DryRun first** - See what will be copied  
💡 **Keep timestamped backups** - Can roll back anytime  
💡 **Same .gitignore everywhere** - Copy to all ESP32 projects
