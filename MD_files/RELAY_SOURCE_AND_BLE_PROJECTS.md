# Relay Code Source & BLE Project Reference

## Which BLE Project Was Used

- **Relay code merged into ATcommands_Example** was taken from:
  - **`EG_BLE_ESP32-WROOM-32E_board`** (latching relays: SET/RESET, GPIO 6/7, 8/9).

## Your BLE Projects (by board / relay type)

| Project | Board | Relay type | GPIOs | Notes |
|--------|--------|------------|-------|--------|
| **EG_BLE_ESP32-WROOM-32E_board** | ESP32 WROOM 32E | **Latching** | 6,7 and 8,9 (SET/RESET) | Source used for initial merge |
| **EG_BLE_server** | ESP32 (Eli board) | **Latching or NON_LATCH** | 12,13 (one pin per relay when `NON_LATCH` defined) | Use this for **non-latching** on WROOM 32E |

## Non-latching on ESP32 WROOM 32E

- Use **EG_BLE_server** as the reference for **non-latching** relays.
- In that project, `relay_control.c` supports:
  - **`NON_LATCH`** – single GPIO per relay (no RESET coil); ON = drive pin, OFF = release.
  - **GPIO 12** = Relay 1, **GPIO 13** = Relay 2 (Eli new ESP32 board).
  - Optional **`RELAY_ACTIVE_LOW`** – invert logic (LOW = ON).

## Backup Before Merge

- Back up **ATcommands_Example** and the chosen BLE project (e.g. **EG_BLE_server** for non-latch) before further merge steps.

## Current State in ATcommands_Example

- `main/relay_control.c` is currently the **latching** variant (from EG_BLE_ESP32-WROOM-32E_board).
- To match **non-latching** on ESP32 WROOM 32E, replace it with the **EG_BLE_server** version and define **NON_LATCH** (and GPIO 12/13 as above).
