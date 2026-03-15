# Git commands – local repository and branches

Use this in **any project** where you want version control in a local folder before (or without) using GitHub.  
Run all commands from the **project root** (the directory that contains your source, e.g. `main/`, `MD_files/`, and this `Git_Local/` folder).

---

## 1. One-time setup in a new project

```bash
# Go to project root (e.g. ATcommands_Example)
cd /path/to/your/project

# Create local Git repository
git init

# (Optional) Set name and email if not set globally
git config user.name "Your Name"
git config user.email "your.email@example.com"
```

Your project should have a **`.gitignore`** at the root. Git will automatically exclude those files from `git add` and `git status`. No need to list build outputs, `build/`, `.bin`, etc. manually.

---

## 2. First commit (save current state)

```bash
# See what will be included (respects .gitignore)
git status

# Add all tracked and new files (ignored files stay ignored)
git add .

# Or add only specific paths
# git add main/
# git add MD_files/

# Commit with a message
git commit -m "Initial commit: project state"
```

---

## 3. Later commits (after changes)

```bash
# Check what changed
git status

# Add everything that changed (still obeys .gitignore)
git add .

# Commit
git commit -m "Short description of what you changed"
```

---

## 4. Branches (when you need them)

```bash
# List local branches (current has *)
git branch

# Create and switch to a new branch
git checkout -b feature/my-feature

# Or (newer syntax)
git switch -c feature/my-feature

# Switch to an existing branch
git checkout main
# or: git switch main

# Create branch without switching
git branch backup-before-refactor
# Then switch when needed: git checkout backup-before-refactor
```

---

## 5. Useful everyday commands

```bash
# Short status
git status -s

# See commit history
git log --oneline -10

# See what’s in .gitignore (no command – open .gitignore)
# Ensure build/, *.bin, etc. are listed so they’re never committed
```

---

## 6. When you’re ready for GitHub (optional)

```bash
# Add remote (replace with your repo URL)
git remote add origin https://github.com/YourUser/YourRepo.git

# Push main branch
git push -u origin main

# Push another branch
git push -u origin feature/my-feature
```

---

## Quick reference

| Goal                 | Command |
|----------------------|--------|
| Init repo           | `git init` |
| Add all (by .gitignore) | `git add .` |
| Commit              | `git commit -m "message"` |
| New branch          | `git checkout -b branch-name` |
| Switch branch       | `git checkout branch-name` |
| List branches       | `git branch` |
| Status              | `git status` |

Always run these from the **project root** (where `.git` and `.gitignore` live). The `Git_Local` folder is just for this guide; the repo is the whole project directory.
