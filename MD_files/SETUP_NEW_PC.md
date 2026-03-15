# ESP-IDF Project Setup for New PC

## Prerequisites
- ESP-IDF installed
- VS Code with ESP-IDF extension installed
- C/C++ extension installed

## Setup Steps (Run on each new PC)

### 1. Configure ESP-IDF Extension
```
Ctrl+Shift+P → ESP-IDF: Configure ESP-IDF Extension
```
- Select your ESP-IDF installation path
- Select Python environment
- Select tools directory

### 2. Set Target Device
```
Ctrl+Shift+P → ESP-IDF: Set Espressif Device Target
```
- Select: `esp32s3`

### 3. Build Project (Generates IntelliSense Data)
```
Ctrl+Shift+P → ESP-IDF: Build your Project
```
OR run in terminal:
```bash
idf.py build
```

### 4. Reload IntelliSense
```
Ctrl+Shift+P → C/C++: Reset IntelliSense Database
```

### 5. Configure COM Port (if different)
```
Ctrl+Shift+P → ESP-IDF: Select Port to Use
```

## Troubleshooting

### "Cannot open source file" errors:

**Solution 1:** Rebuild compile_commands.json
```bash
idf.py build
```

**Solution 2:** Regenerate VS Code config
```
Ctrl+Shift+P → ESP-IDF: Add vscode Configuration Folder
```

**Solution 3:** Check settings
Open `.vscode/settings.json` and verify:
- `idf.espIdfPathWin` points to your ESP-IDF installation
- `idf.toolsPathWin` points to your tools directory
- `idf.pythonInstallPath` points to valid Python

**Solution 4:** Manual paths (if ESP-IDF vars don't work)
Edit `.vscode/c_cpp_properties.json` and replace config variables with absolute paths

### IntelliSense not working:

1. Check that `build/compile_commands.json` exists
2. Rebuild project: `idf.py build`
3. Reload window: `Ctrl+Shift+P → Developer: Reload Window`

### Build errors:

1. Clean build folder:
   ```bash
   idf.py fullclean
   idf.py build
   ```

2. Check IDF environment:
   ```bash
   echo %IDF_PATH%
   ```

## Project-Specific Settings

**Target:** ESP32-S3  
**COM Port:** Check Device Manager (usually COM3-COM8)  
**Flash Method:** UART  
**Baud Rate:** 115200

## Files to Commit to Git

✅ Commit:
- `CMakeLists.txt`
- `main/CMakeLists.txt`
- `sdkconfig.defaults`
- `.vscode/c_cpp_properties.json` (if using portable config)

❌ Don't commit:
- `.vscode/settings.json` (PC-specific paths)
- `build/` directory
- `sdkconfig` (generated file)

## Quick Command Reference

```bash
# Build
idf.py build

# Flash
idf.py flash

# Monitor
idf.py monitor

# Clean
idf.py fullclean

# Set target
idf.py set-target esp32s3

# Menuconfig
idf.py menuconfig
```
