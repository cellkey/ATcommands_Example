# Git Setup and Usage Guide for ESP32 Projects

## One-Time Setup

### Step 1: Add Git to PATH (Permanent)

**Option A: Using PowerShell (Run as Administrator)**
```powershell
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Program Files\Git\bin", "Machine")
```
Then restart PowerShell/VS Code.

**Option B: Manual (easier)**
1. Press `Win + X` → System
2. Click "Advanced system settings"
3. Click "Environment Variables"
4. Under "System variables", find `Path`
5. Click "Edit" → "New"
6. Add: `C:\Program Files\Git\bin`
7. Click OK, restart PowerShell/VS Code

**Option C: Use Git Bash** (already in PATH)
- Just open "Git Bash" instead of PowerShell
- All commands work the same

### Step 2: Configure Git (First Time Only)
```bash
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

## Using Git with This Project

### Initialize Git (First Time Per Project)
```bash
cd C:\Users\Danny\ESP32_projects\EG_gatt_server

# Initialize Git repository
git init

# Add all files (respects .gitignore automatically)
git add .

# First commit
git commit -m "Initial commit - ESP32 BLE GATT server"
```

### Daily Workflow

**1. Check what changed:**
```bash
git status
```

**2. Save your changes:**
```bash
# Add specific files
git add main/ESP32C3_EG_code_notification.c

# Or add all changes
git add .

# Commit with message
git commit -m "Fixed relay timing issue"
```

**3. View history:**
```bash
git log --oneline
```

**4. Undo changes (before commit):**
```bash
# Discard changes to a file
git checkout -- main/myfile.c

# Discard all changes
git reset --hard
```

### Sync Between PCs

**On PC 1 (initial setup):**
```bash
# Create repository on GitHub/GitLab first, then:
git remote add origin https://github.com/yourusername/EG_gatt_server.git
git push -u origin main
```

**On PC 2 (clone project):**
```bash
cd C:\Users\Danny\ESP32_projects
git clone https://github.com/yourusername/EG_gatt_server.git
cd EG_gatt_server
```

**Daily sync:**
```bash
# Download changes from other PC
git pull

# Upload your changes
git add .
git commit -m "Updated feature X"
git push
```

## What .gitignore Does Automatically

Once `.gitignore` exists, these commands **automatically skip ignored files**:
- `git add .` - only adds tracked files
- `git status` - doesn't show ignored files
- `git commit` - doesn't include ignored files

**You never see:**
- `build/` folder
- `sdkconfig` file
- `.vscode/settings.json`

**You always track:**
- Source code (*.c, *.h)
- CMakeLists.txt
- Documentation
- `.vscode/c_cpp_properties.json`

## Quick Reference

```bash
# Status
git status                    # What changed?
git diff                      # Show exact changes

# Save work
git add .                     # Stage all changes
git commit -m "message"       # Save snapshot

# History
git log                       # View commits
git show <commit>             # View specific commit

# Undo
git checkout -- <file>        # Discard file changes
git reset --soft HEAD~1       # Undo last commit (keep changes)
git reset --hard HEAD~1       # Undo last commit (delete changes)

# Branches (advanced)
git branch feature-x          # Create branch
git checkout feature-x        # Switch branch
git merge feature-x           # Merge branch

# Remote sync
git pull                      # Download changes
git push                      # Upload changes
```

## Troubleshooting

**Problem: "git: command not found"**
- Solution: Follow Step 1 above or use Git Bash

**Problem: "Changes not showing"**
- Check if file is in `.gitignore`
- Run: `git check-ignore -v <filename>`

**Problem: "Merge conflict"**
- Edit conflicted file, look for `<<<<<<` markers
- Fix conflicts, then: `git add <file>` and `git commit`

**Problem: "Want to use GitHub Desktop instead"**
- Download GitHub Desktop
- It handles Git automatically with GUI
- Much easier than command line!

## For New ESP32 Projects

Just copy these files:
```bash
cp .gitignore ../NewProject/
cp GIT_SETUP_GUIDE.md ../NewProject/
cd ../NewProject
git init
git add .
git commit -m "Initial commit"
```

The same `.gitignore` works for all ESP-IDF projects!
