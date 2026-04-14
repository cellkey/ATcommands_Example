# status_reg bit 0x0080 — relay digital-input gate

GPIO22 digital input (`digital_input_alert.c`, active **LOW** with pull-up).  
Bit is read from NVS **`status_reg` at boot** (`nvs_config_set_relay_digital_gate_from_reg` in `app_main`). Changing the bit via config UART requires **REBOOT** to refresh the cached flag.

## Affected: user remote activation only

| status_reg **0x0080** | Server **OPENxxx** (modem / WiFi TCP) | **CHECK_USER → APPROVED** (dial / WiFi) | Command **duration** (yz) | Relay **OFF** when |
|------------------------|----------------------------------------|----------------------------------------|---------------------------|---------------------|
| **Clear**              | Normal timed OPEN                      | Normal                                 | Used                      | Timer, CLOSE, etc. |
| **Set**                | Only if GPIO22 active; no **OPENED** if blocked | Same | **Ignored** — ON while input active | GPIO22 inactive, CLOSE, e-stop, etc. |

## Not affected (unchanged with 0x0080)

| Command / source | Notes |
|------------------|--------|
| **KEEPOPEN** / **CLOSE** | Operational / door-mode commands; not treated as end-user remote activation. |
| **BLE** JSON relay | Not gated. |
| **KEEPOPEN restore** at boot | Not gated. |
| **Local button** | Not gated. |

## Implementation

- `relay_execute_gated_server_activation()` for OPEN and APPROVED only (`server_keepalive.c`, `wifi_tcp_client.c`).
- Relay task clears `release_when_digital_inactive` when GPIO22 is inactive.
- Digital-input task starts before relay init.
