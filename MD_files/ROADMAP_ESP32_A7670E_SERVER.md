# Roadmap: ESP32-WROOM-32E + SIMCOM A7670E → Your Server

**Goal:** Use an off-the-shelf ESP32 board with a “tailored” SIMCOM A7670E modem to connect to your server (e.g. `gates.crea-cell.com:3000`). Later: add BLE and migrate to your own ESP32-based hardware (replacing ATMEGA644).

**Reference:** `SIMCOM_A7670E_MODEM_INIT&TCP_CONNECTION.txt` (proven init + TCP flow from existing product).

---

## 1. Current Project vs A7670E Reference

### What the current project already has
- **UART AT engine** – `enhanced_freertos_uart_at_commands.c`: queue, send AT, wait for response, timeouts.
- **Modem abstraction** – `modem_definitions.h/c`: modem type (SIMCOM A7670, Telit LE910), PDP/TCP command set.
- **TCP task layer** – `tcp_task_management.c`, `tcp_config.h`: server IP/port, connect/send/receive flow.
- **Modem init** – `init_modem_sequence()`: AT, E0, CMEE, CREG?, CSQ, CGMI/MM/MR (basic only).
- **Config** – `Modem_Config_Handling`, `tcp_config.h`: server, APN, timeouts.

### Gaps vs your A7670E reference log

| Area | Reference (A7670E) | Current project |
|------|--------------------|------------------|
| **TCP stack** | `AT+NETCLOSE` → `AT+NETOPEN` → get IP with `AT+IPADDR` | Uses PDP only; no NETOPEN/NETCLOSE/IPADDR. |
| **TCP connect** | `AT+CIPOPEN=1,"TCP","host",port` (socket ID 1) | Uses `AT+CIPSTART="TCP","host",port` (no socket ID). |
| **Send** | `AT+CIPSEND=1,<len>` then `>` then payload; response `+CIPSEND: 1,len,len` | Assumes `AT+CIPSEND=<len>` and different response. |
| **Receive** | `+IPD<len>` or `+IPD<id>,<len>`; `AT+CIPRXGET=0/1` (non-buffered/buffered URC) | Uses `+IPD`; no explicit CIPRXGET handling. |
| **Init** | AT → CSQ → CREG? → AT&F;E0 → CFUN=1 → CLIP=1 → CGMR/CGSN → CIPCCFG? → CIPRXGET=0 → COPS? → CICCID → CGATT? → CGACT=1,1 → CIPMODE? → CGDCONT → **NETCLOSE → NETOPEN** → IPADDR | Init has basic AT/CSQ/CREG/CGx; no AT&F;E0, CFUN=1, CLIP=1, CIPRXGET, CICCID, CGATT, CGACT, CGDCONT, NETCLOSE/NETOPEN, IPADDR. |

So: the **reference is A7670E-specific** (NETOPEN/CIPOPEN/CIPSEND with socket ID, CIPRXGET). The **current code** is written for an older SIMCOM style (CIPSTART, no NETOPEN). To “tailor” to A7670E you need an **A7670E-specific init + TCP path** and optional later refactor of the generic layer.

---

## 2. Suggested Roadmap (New Project)

### Phase 1 – New ESP32 + A7670E project setup
- Create a **new ESP-IDF project** (e.g. `ESP32_A7670E_Server`) so you keep a clean, product-focused codebase.
- Copy in from current project only what you need:
  - UART AT engine (or a single file that wraps `send_at_command` / `send_at_command_ex`).
  - Minimal modem definitions for **A7670E only** (no need for Telit/others at first).
- Pinout: match your off-the-shelf board (e.g. UART1 TX/RX to A7670E, and any DTR/PWRKEY if used).
- Confirm: power-on → `AT` → `OK` and e.g. `AT+CGMR` → version.

**Deliverable:** New project that prints modem version over UART.

---

### Phase 2 – A7670E init sequence (match reference)
- Implement init **exactly** as in `SIMCOM_A7670E_MODEM_INIT&TCP_CONNECTION.txt`:
  1. After power: delay ~5 s, then `AT` → OK.
  2. `AT+CSQ`, `AT+CREG?` (optional: wait for CREG 0,1 or 0,5).
  3. `AT&F;E0`, `AT+CFUN=1`, `AT+CLIP=1`.
  4. `AT+CGMR`, `AT+CGSN`, `AT+CIPCCFG?`, `AT+CIPRXGET=0`.
  5. `AT+COPS?`, `AT+CICCID`, `AT+CGATT?`, `AT+CGACT=1,1`, `AT+CIPMODE?`, `AT+CGDCONT=1,"IP","internet"`.
  6. **`AT+NETCLOSE`** then **`AT+NETOPEN`** (and wait for `+NETOPEN: 0`).
  7. **`AT+IPADDR`** and parse IP (optional: log it).
- Use your existing `send_at_command` / `send_at_command_ex` and timeouts; add a small “init state machine” or linear sequence that runs once at startup.
- Config: put server host/port (e.g. `gates.crea-cell.com`, 3000) and APN in `tcp_config.h` or a single `app_config.h`.

**Deliverable:** On boot, modem reaches “network open, has IP” and you log IP + “Modem ready”.

---

### Phase 3 – TCP connect and first HTTP-style GET
- Use **A7670E syntax** (no CIPSTART):
  - `AT+CIPOPEN=1,"TCP","gates.crea-cell.com",3000` (or from config).
  - Wait for `OK` (and optionally connection URC if you parse it).
- Send the **first GET chunk** (72 bytes) as in your reference:
  - `AT+CIPSEND=1,72` → wait for `>` → send exactly 72 bytes of the GET request.
  - Parse `+CIPSEND: 1,72,72` (or similar) for success.
- Send the **second chunk** (81 bytes: FW_VERSION, COPS_ID, CSQ, SIM_ID, etc.).
- Then wait for **HTTP response** (e.g. `RECV FROM:...`, `+IPD...`, `HTTP/1.1 200 OK`).
- After “200 OK”, switch to buffered URC: `AT+CIPRXGET=1` as in the reference.

**Deliverable:** One-shot: init → NETOPEN → CIPOPEN → send GET (72+81 bytes) → receive 200 OK → CIPRXGET=1. Log “Got 200” and “In main loop..ready”.

---

### Phase 4 – Robustness and keep-alive
- **Keep-alive:** Periodic small message (or minimal HTTP/GET) so the server and modem keep the TCP connection alive; interval from config (e.g. 30–60 s).
- **Reconnect:** If TCP drops (no response to keep-alive, or CIPCLOSE/CIPERROR), close with `AT+CIPCLOSE=1`, then `AT+NETCLOSE` → `AT+NETOPEN` → `AT+CIPOPEN=1,...` again; retry with backoff.
- **Parsing:** Simple state or substring scan for `+IPD`, `200 OK`, `+CIPCLOSE`, `+CIPERROR` so the app knows connection state.

**Deliverable:** Stable long-running connection with auto-reconnect.

---

### Phase 5 – Commands and call handling (your “to add” list)
- **Receive commands:** Parse server payload (e.g. after `+IPD`), interpret “list of available commands”, execute (e.g. relay, GPIO, BLE later).
- **ACK:** Send response back with `AT+CIPSEND=1,<len>` + payload.
- **Incoming call:** Already have `AT+CLIP=1`; on URC with caller ID, notify server (e.g. send caller ID over TCP); if server says “allowed”, run the requested action.

**Deliverable:** Device that obeys server commands and reports call events.

---

### Phase 6 (Later) – BLE + your own hardware
- **Merge BLE project:** Add BLE (GATT server/client as needed) in the same ESP32 app; BLE for local control/config, cellular for server and remote control.
- **New hardware:** Replace ATMEGA644 board with ESP32-WROOM-32E (or similar) + A7670E on your new PCB; reuse the same firmware, adjust pins and power in `board_config.h` (or similar).

---

## 3. Technical Notes for A7670E

- **TCP:** Use **NETOPEN** (and NETCLOSE) for the data connection; **CIPOPEN** with socket id `1` for TCP; **CIPSEND=1,<length>** and `>` prompt for sends; **+IPD** for receives; **CIPRXGET=0** during init/first connect, **CIPRXGET=1** after connection is up.
- **modem_definitions:** Either add an “A7670E” variant that uses `CIPOPEN`/`NETOPEN`/`CIPSEND=id,len`, or keep a separate `a7670e_tcp.c` in the new project and call it from the main flow. Avoid mixing CIPSTART and CIPOPEN in the same path.
- **Config:** One place for server host, port, APN, unit ID, firmware version string, and keep-alive interval so the reference log and code stay in sync.

---

## 4. Summary Table

| Phase | Focus | Outcome |
|-------|--------|--------|
| 1 | New project + UART + pins | AT works, modem version printed |
| 2 | Full A7670E init (incl. NETOPEN, IPADDR) | Modem ready, IP obtained |
| 3 | CIPOPEN + GET (72+81 bytes) + 200 OK | First successful server connection |
| 4 | Keep-alive + reconnect | Stable connection to server |
| 5 | Commands + ACK + call notification | Production-like behavior |
| 6 | BLE + new hardware | Your new board with BLE + cellular |

Using your reference file as the single source of truth for init and TCP flow will keep the new project aligned with hardware that already works (“EazyGate” style). If you tell me whether you prefer a single new repo or a branch inside the current one, I can outline the exact file list and first code changes for Phase 1 and 2.
