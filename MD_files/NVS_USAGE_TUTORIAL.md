# NVS Usage Tutorial

This guide explains how the unit uses **NVS (Non-Volatile Storage)** for configurable values and how to update them via the **Config UART** (external serial port).

---

## 1. What is stored in NVS?

All config is stored under the namespace **`unit_cfg`**. The following keys are used:

| Key        | Purpose                    | Example value           | Used by                    |
|-----------|----------------------------|-------------------------|----------------------------|
| `unit_id` | Device identifier          | `cr18061950`            | **NVS is single source:** modem GET path `/api/device/<unit_id>/listen`, BLE advertising name, auth |
| `fw_ver`  | Firmware version string    | `1.2.26`                | GET request header         |
| `apn`     | Cellular APN               | `internet`              | Modem PDP context (CGDCONT)|
| `host`    | TCP server hostname or IP  | `gates.crea-cell.com`   | CIPOPEN (connect)          |
| `port`    | TCP server port            | `3000`                  | CIPOPEN (connect)          |

If a key is **not** set in NVS, the firmware uses **compile-time defaults** from `a7670e_config.h` (e.g. `A7670E_UNIT_ID`, `A7670E_FW_VERSION`, `"internet"`, `A7670E_SERVER_HOST`, `A7670E_SERVER_PORT`).

---

## 2. Config shell (same UART as debug/flash)

**You do not need a second or third UART.** The config shell uses the **same serial port** as your debug console and flashing (UART0, the USB‑serial cable you use with `idf.py monitor`).

- **Port:** The console (UART0) — same cable as `idf.py flash monitor`.
- **Baud rate:** Whatever your monitor uses (typically 115200).

So: one UART for the modem, one for debug/flash — and **config is on the debug UART**. When you run `idf.py monitor`, you can type config commands in the same terminal.

---

## 3. Config shell commands

Open the serial monitor (e.g. `idf.py monitor`) on the same port you use for debug. Send **one command per line** (ending with Enter). The unit echoes responses and errors; log lines will appear in the same window.

### 3.1 Set a value (saved to NVS immediately)

```
SET unit_id=cr18061952
SET fw_ver=1.2.27
SET apn=internet
SET host=gates.crea-cell.com
SET port=3000
```

Spaces around `=` are optional. After each `SET` you should see `OK saved` (or an error message).

### 3.2 Read a value

```
GET unit_id
GET fw_ver
GET apn
GET host
GET port
```

The current value is printed (or `(not set)` if the key is missing).

### 3.3 List all config

```
LIST
```

Prints all keys and their current values (or `(default)` when not set).

### 3.4 Reboot the unit

```
REBOOT
```

The unit restarts. **New NVS values are used on the next boot** (modem init, connect, BLE name, etc.). For changes to `unit_id`, `apn`, `host`, or `port`, use `REBOOT` (or power cycle) after `SET` so they take effect.

### 3.5 Typing vs pasting

- **Prefer typing** commands character by character. Some terminals do not show pasted text on screen (no local echo), and in some environments pasted text may not be sent to the device correctly—only Enter is sent, so the unit sees an empty line and replies with `? SET key=val | GET key | LIST | REBOOT`.
- If you get `?` after entering a command, check the log for a line like `cfg received N chars: [your line]`. If `N` is 0 or the line is empty, the device did not receive your typed/pasted text; try typing the command manually (e.g. `SET fw_ver=1.2.27`) and press Enter once.

---

## 4. When do NVS values take effect?

| When you change… | Takes effect after… |
|------------------|---------------------|
| `unit_id`        | Next boot (GET, BLE name, encryption seed). |
| `fw_ver`         | Next boot (GET header). |
| `apn`            | Next boot (modem init runs CGDCONT with new APN). |
| `host` / `port`  | Next boot (connect uses new host/port). |

So after updating any of these via the config shell, send **REBOOT** (or power cycle) so the next modem init and connect use the new config.

---

## 5. Typical workflow

1. Connect the board with the same USB cable you use for flashing and open the monitor (`idf.py monitor`).
2. Optional: run **LIST** to see current values.
3. Update values, for example:
   - `SET unit_id=cr18061952`
   - `SET fw_ver=1.2.27`
   - `SET apn=internet`
4. Run **REBOOT** so the unit restarts with the new config.
5. After reboot, the unit runs with the new values (modem on UART1, console/config and logs on UART0).

---

## 6. Using NVS from code (optional)

If you want to read or write config from your own code:

```c
#include "nvs_config.h"

// Call after nvs_flash_init()
nvs_config_init();

// Read (with default if not set)
char unit_id[NVS_CONFIG_MAX_LEN];
nvs_config_get_string(NVS_KEY_UNIT_ID, unit_id, sizeof(unit_id), "cr18061952");

// Write and commit
nvs_config_set_string(NVS_KEY_UNIT_ID, "cr18061953");

// Port
int port = nvs_config_get_port(NVS_KEY_SERVER_PORT, 3000);
nvs_config_set_port(NVS_KEY_SERVER_PORT, 3001);
```

Keys and namespace are defined in `nvs_config.h` (`NVS_CONFIG_NAMESPACE`, `NVS_KEY_*`).

---

## 7. Adding new predefined keys

To add a new config key (e.g. `status_reg`) that can be set/get via the config shell, you need to update **3 files**:

### Step 1: Define the key constant

**File:** `main/nvs_config.h` (around line 17-21)

Add your new key constant:
```c
#define NVS_KEY_UNIT_ID       "unit_id"
#define NVS_KEY_FW_VER        "fw_ver"
#define NVS_KEY_APN           "apn"
#define NVS_KEY_SERVER_HOST   "host"
#define NVS_KEY_SERVER_PORT   "port"
#define NVS_KEY_STATUS_REG    "status_reg"    // <-- ADD YOUR NEW KEY HERE
```

### Step 2: Add to SET command parser

**File:** `main/config_uart.c` (around line 110-122)

In the `SET` handler, add your key to the string keys check:

```c
} else if (strcmp(key, "unit_id") == 0 || strcmp(key, "fw_ver") == 0 ||
           strcmp(key, "apn") == 0 || strcmp(key, "host") == 0 ||
           strcmp(key, "status_reg") == 0) {    // <-- ADD HERE
    const char *nkey = (strcmp(key, "unit_id") == 0) ? NVS_KEY_UNIT_ID :
                      (strcmp(key, "fw_ver") == 0) ? NVS_KEY_FW_VER :
                      (strcmp(key, "apn") == 0) ? NVS_KEY_APN :
                      (strcmp(key, "host") == 0) ? NVS_KEY_SERVER_HOST :
                      NVS_KEY_STATUS_REG;    // <-- ADD HERE
    // ... rest of SET logic ...
}
```

Also update the error message to include your new key:
```c
send_line("ERR unknown key (unit_id,fw_ver,apn,host,port,status_reg)");
```

### Step 3: Add to GET command parser

**File:** `main/config_uart.c` (around line 128-131)

Add your key to the GET handler:

```c
if (strcmp(key, "unit_id") == 0) nkey = NVS_KEY_UNIT_ID;
else if (strcmp(key, "fw_ver") == 0) nkey = NVS_KEY_FW_VER;
else if (strcmp(key, "apn") == 0) nkey = NVS_KEY_APN;
else if (strcmp(key, "host") == 0) nkey = NVS_KEY_SERVER_HOST;
else if (strcmp(key, "status_reg") == 0) nkey = NVS_KEY_STATUS_REG;    // <-- ADD HERE
```

### Step 4: Add to LIST output

**File:** `main/config_uart.c` (around line 61-75)

Add your key to the `list_all()` function:

```c
static void list_all(void) {
    char buf[NVS_CONFIG_MAX_LEN];
    // ... existing keys ...
    nvs_config_get_string(NVS_KEY_STATUS_REG, buf, sizeof(buf), "");
    send_line("status_reg="); send_line(buf[0] ? buf : "(default)");
}
```

### Summary: Files to update

| File | What to change | Approx. lines |
|------|----------------|---------------|
| `nvs_config.h` | Add `#define NVS_KEY_YOUR_KEY "your_key"` | 17-21 |
| `config_uart.c` | Add to SET parser (check + mapping) | 110-122 |
| `config_uart.c` | Add to GET parser (check) | 128-131 |
| `config_uart.c` | Add to LIST output (`list_all()`) | 61-75 |

After these changes, `SET your_key=value`, `GET your_key`, and `LIST` will work for your new key.

**Note:** If your new key needs special handling (e.g. integer validation like `port`), add a separate `if` block similar to the `port` handling (around line 103-109).

---

## 8. Files reference

| File                | Role |
|---------------------|------|
| `main/nvs_config.c` | NVS read/write for `unit_cfg` namespace. |
| `main/nvs_config.h` | API and key names. |
| `main/config_uart.c`| Config shell task (reads from console stdin, SET/GET/LIST/REBOOT). |
| `main/config_uart.h`| Config shell start API. |
| `main/a7670e_config.h` | Compile-time defaults (used when NVS key is missing). |

---

## 9. Troubleshooting

- **Config commands not responding**  
  Make sure you are typing in the same serial session as the one showing logs (e.g. `idf.py monitor`). Send one command per line and press Enter.

- **"Config shell task create failed" in logs**  
  Config shell is optional. If the task fails to create, the rest of the firmware still runs with defaults or existing NVS values.

- **Values don’t change after SET**  
  Send **REBOOT** (or power cycle). Modem init and connect run only at boot and use NVS then.

- **Reset all config to defaults**  
  Erase NVS (e.g. `idf.py erase-flash` or NVS partition erase). On next boot, all keys fall back to `a7670e_config.h` defaults. Use with care.
