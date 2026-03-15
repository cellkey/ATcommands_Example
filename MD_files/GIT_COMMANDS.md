# Git Commands Reference
#/////////////////////////////////////////////////////////////////////
## Initial Setup (One-Time Only)

```powershell
# Configure Git identity
& 'C:\Program Files\Git\bin\git.exe' config --global user.name "cellkey"
& 'C:\Program Files\Git\bin\git.exe' config --global user.email "cellkey.tech@gmail.com"

# Verify configuration
& 'C:\Program Files\Git\bin\git.exe' config --global --list
```
#////////////////////////////////////////////////////////////////////

## New Repository Setup (One-Time Per Project)

```powershell
# Initialize Git in project folder
& 'C:\Program Files\Git\bin\git.exe' init

# Add all files (respects .gitignore)
& 'C:\Program Files\Git\bin\git.exe' add .

# Create first commit
& 'C:\Program Files\Git\bin\git.exe' commit -m "Initial commit <current project>"

# Connect to GitHub (replace REPOSITORY_NAME with your repo name)
& 'C:\Program Files\Git\bin\git.exe' remote add origin https://github.com/cellkey/REPOSITORY_NAME.git

# Rename branch to main
& 'C:\Program Files\Git\bin\git.exe' branch -M main

# Push to GitHub (first time)
& 'C:\Program Files\Git\bin\git.exe' push -u origin main
```

## Daily Workflow (After Editing Files)

```powershell
# 1. Stage changes
& 'C:\Program Files\Git\bin\git.exe' add .

# 2. Commit with description
& 'C:\Program Files\Git\bin\git.exe' commit -m "Description of what you changed"

# 3. Upload to GitHub
& 'C:\Program Files\Git\bin\git.exe' push
```

## Useful Commands

```powershell
# Check status (what files changed)
& 'C:\Program Files\Git\bin\git.exe' status

# View commit history
& 'C:\Program Files\Git\bin\git.exe' log --oneline

# Download changes from GitHub
& 'C:\Program Files\Git\bin\git.exe' pull

# See what changed in files
& 'C:\Program Files\Git\bin\git.exe' diff

# Undo changes to a file (before commit)
& 'C:\Program Files\Git\bin\git.exe' checkout -- filename.c

# View remote repository URL
& 'C:\Program Files\Git\bin\git.exe' remote -v
```
#  remove added files that not ignored by .gitignoe! befor commit!
git rm -r --cached BU_12032026  
git rm -r --cached MD_files
git add .
git status

git commit -m "Stop tracking BU_12032026 and MD_files; add to .gitignore"

## Authentication

When pushing for the first time, you'll be asked for credentials:
- **Username:** `cellkey` (your GitHub username)
- **Password:** Use **Personal Access Token** (not your GitHub password)

### Create Personal Access Token:
1. GitHub → Settings → Developer settings
2. Personal access tokens → Tokens (classic) → Generate new token
3. Select scope: `repo` (full control of private repositories)
4. Copy token and use as password

## Your Current Setup

- **GitHub Username:** cellkey
- **Repository:** https://github.com/cellkey/EG_gatt_server (or your repo name)
- **Local Project:** C:\Users\Danny\ESP32_projects\EG_gatt_server
- **Git Path:** C:\Program Files\Git\bin\git.exe

## Notes

- `.gitignore` automatically excludes `build/`, `sdkconfig`, and other generated files
- Commits are saved locally; `push` uploads them to GitHub
- Always commit before switching computers
- Use `git pull` on other PC before starting work
