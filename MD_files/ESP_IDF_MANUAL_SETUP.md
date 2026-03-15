# ESP-IDF Manual Setup Guide (No PowerShell Required)

## Quick Setup for New AT Command Projects

### Prerequisites
- ESP-IDF installed and working
- Access to your AT command source files

### Step-by-Step Instructions

#### 1. Create New Project Directory
```bash
# Navigate to your ESP32 projects folder
cd /c/ESP32_Projects  # Adjust path as needed

# Create new project
mkdir MyNewATProject
cd MyNewATProject
```

#### 2. Create Directory Structure
```bash
# Create required directories
mkdir main
mkdir components
mkdir components/at_command_lib
mkdir components/at_command_lib/include
mkdir components/at_command_lib/src
```

#### 3. Copy AT Command Files

**Copy header files:**
```bash
# From your source directory to include/
cp "/c/Users/cellk/project-name/ATcommand_example/at_command_api.h" components/at_command_lib/include/
cp "/c/Users/cellk/project-name/ATcommand_example/modem_task_control.h" components/at_command_lib/include/
cp "/c/Users/cellk/project-name/ATcommand_example/at_command_examples.h" components/at_command_lib/include/
```

**Copy source files:**
```bash
# From your source directory to src/
cp "/c/Users/cellk/project-name/ATcommand_example/enhanced_freertos_uart_at_commands.c" components/at_command_lib/src/
cp "/c/Users/cellk/project-name/ATcommand_example/integration_example.c" components/at_command_lib/src/
cp "/c/Users/cellk/project-name/ATcommand_example/modem_init_usage_example.c" components/at_command_lib/src/
```

#### 4. Copy Template Files

**Copy the ready-made template files:**
```bash
# Copy CMakeLists.txt files
cp "/c/Users/cellk/project-name/ATcommand_example/templates/root_CMakeLists.txt" ./CMakeLists.txt
cp "/c/Users/cellk/project-name/ATcommand_example/templates/main_CMakeLists.txt" main/CMakeLists.txt
cp "/c/Users/cellk/project-name/ATcommand_example/templates/component_CMakeLists.txt" components/at_command_lib/CMakeLists.txt

# Copy template main.c
cp "/c/Users/cellk/project-name/ATcommand_example/template_main.c" main/main.c
```

#### 5. Build and Test
```bash
# Build the project
idf.py build

# If successful, flash and monitor
idf.py flash monitor
```

### Alternative: Use Windows Copy Commands

If you prefer Windows Command Prompt:

```cmd
REM Navigate to your projects directory
cd C:\ESP32_Projects

REM Create new project
mkdir MyNewATProject
cd MyNewATProject

REM Create directory structure
mkdir main
mkdir components\at_command_lib\include
mkdir components\at_command_lib\src

REM Copy files (adjust source path as needed)
copy "C:\Users\cellk\project-name\ATcommand_example\*.h" components\at_command_lib\include\
copy "C:\Users\cellk\project-name\ATcommand_example\*.c" components\at_command_lib\src\
copy "C:\Users\cellk\project-name\ATcommand_example\templates\*" .
copy "C:\Users\cellk\project-name\ATcommand_example\template_main.c" main\main.c

REM Build
idf.py build
```

### What You Get

After following these steps, your project structure will be:

```
MyNewATProject/
├── CMakeLists.txt              # Project root CMake
├── main/
│   ├── CMakeLists.txt          # Main component CMake
│   └── main.c                  # Your application code
└── components/
    └── at_command_lib/
        ├── CMakeLists.txt      # AT library component CMake
        ├── include/            # Header files
        │   ├── at_command_api.h
        │   ├── modem_task_control.h
        │   └── at_command_examples.h
        └── src/                # Source files
            ├── enhanced_freertos_uart_at_commands.c
            ├── integration_example.c
            └── modem_init_usage_example.c
```

### Customization

1. **Edit `main/main.c`** - Add your application logic
2. **Modify pin assignments** - Update GPIO pins in the AT command source if needed
3. **Add features** - Extend the AT command library for your specific modem

### Troubleshooting

- **Build errors**: Check that all CMakeLists.txt files are in place
- **Include errors**: Verify header files are in `components/at_command_lib/include/`
- **Link errors**: Ensure all .c files are in `components/at_command_lib/src/`

This approach uses only standard ESP-IDF and system commands - no PowerShell required!