# Advisory: Match A7670E Log Result & Final-Code Approach

**Before any code changes.** This document advises how to reach the same result as `SIMCOM_A7670E_MODEM_INIT&TCP_CONNECTION.log`, which tasks to use for “final” code, and how to store AT commands. It also keeps in mind KEEP ALIVE, server commands, and future steps.

---

## 1. Goal (What “Same Result” Means)

From the log, the target flow is:

1. **Power / reset** → delay ~5 s.
2. **Init** (in order): AT → CSQ → CREG? → AT&F;E0 → CFUN=1 → CLIP=1 → CGMR → CGSN → CIPCCFG? → CIPRXGET=0 → COPS? → CICCID → CGATT? → CGACT=1,1 → CIPMODE? → CGDCONT=1,"IP","internet".
3. **Network** → AT+NETCLOSE → AT+NETOPEN (wait for +NETOPEN: 0) → AT+IPADDR (get IP).
4. **TCP** → AT+CIPOPEN=1,"TCP","gates.crea-cell.com",3000 → OK.
5. **Send GET part 1** → AT+CIPSEND=1,72 → wait for `>` → send 72 bytes. The payload is a fixed-format string (e.g. `GET /api/device/<UNIT_ID>/listen HTTP/1.1`) with **UNIT_ID** inserted before send. UNIT_ID is always the same length: **crXXXXXXXX** (e.g. `cr18061950`). Build the 72-byte string with UNIT_ID, then send.
6. **Send GET part 2** → AT+CIPSEND=1,81 → wait for `>` → send 81 bytes. All parameter values in this string (FW_VERSION, COPS_ID, CSQ, SIM_ID) are **collected during startup and modem init** (e.g. from AT+CGMR, AT+COPS?, AT+CSQ, AT+CICCID), then **inserted into the string before it is sent**.
7. **Wait for HTTP response** → receive +IPD… and "HTTP/1.1 200 OK".
8. **Switch URC mode** → AT+CIPRXGET=1.
9. **“In main loop..ready”** → then later: KEEP ALIVE, commands from server, ACK, incoming call handling, etc.

So “same result” = modem init + network open + TCP connect + GET (72+81 bytes) + receive 200 OK + CIPRXGET=1, with no change to the existing UART/AT engine behaviour other than what sequences we run and how we handle A7670E-specific replies.

---

## 2. Gap vs Current Code (What Must Change Conceptually)

| Step | Log (A7670E) | Current code |
|------|----------------|--------------|
| Init | AT, CSQ, CREG?, AT&F;E0, CFUN=1, CLIP=1, CGMR, CGSN, CIPCCFG?, CIPRXGET=0, COPS?, CICCID, CGATT?, CGACT=1,1, CIPMODE?, CGDCONT | init_modem_sequence: AT, E0, CMEE=1, CREG?, CSQ, CGMI, CGMM, CGMR only |
| Network | NETCLOSE → NETOPEN → IPADDR | tcp_task_management: PDP only (CGDCONT, CGACT); no NETOPEN/NETCLOSE/IPADDR |
| TCP connect | CIPOPEN=1,"TCP","host",port | modem_definitions: CIPSTART="TCP","host",port (no socket id) |
| Send | CIPSEND=1,72 → `>` → raw 72 bytes → +CIPSEND: 1,72,72 | CIPSEND=len → prompt → data; expects SEND OK; no socket id |
| After connect | CIPRXGET=1 | Not used |

So we need:

- An **A7670E-specific init sequence** (same order as log, including NETCLOSE/NETOPEN/IPADDR).
- **A7670E-specific TCP**: CIPOPEN(1,…), CIPSEND=1,<len> with `>` then raw payload, and parsing of +CIPSEND: 1,x,x and +IPD.
- A **small amount of “send then raw data” logic** for the two GET chunks (and later for KEEP ALIVE / ACK).
- **CIPRXGET=1** after 200 OK.

No change to the core UART RX/task or the generic `send_at_command` / `send_at_command_ex` / `execute_command_sequence` is required for “same result”; only the sequences and the way we build and run TCP connect/send need to be A7670E-aware.

---

## 3. Task Assessment: What to Use for “Final” Code

**Keep as-is (core):**

- **uart_rx_task** – Receives bytes, splits lines, pushes to response queue and signals. Essential.
- **at_command_task** – Processes AT queue, sends command, waits for expected response/OK. Essential.
- **modem_init_task** (start/stop pattern) – Good for “run init once”; we will only change *what* init runs (the sequence), not the pattern.

**Use as the single “application” entry (final flow):**

- One **main application task** that:
  1. Waits for system ready (e.g. 3–5 s after boot).
  2. Runs **A7670E init** (see below: table-driven sequence).
  3. Runs **network open** (NETCLOSE → NETOPEN → IPADDR).
  4. Runs **TCP connect** (CIPOPEN=1,…).
  5. Sends **GET part 1** (72 bytes: template + UNIT_ID `crXXXXXXXX`), then **GET part 2** (81 bytes: string built from init-collected values: FW_VERSION, COPS_ID, CSQ, SIM_ID).
  6. Waits for **“200 OK”** in RX (already possible with current response/line handling).
  7. Sends **CIPRXGET=1**.
  8. Enters **“main loop..ready”** where you will add: periodic KEEP ALIVE, reading server commands (+IPD), parsing, executing, sending ACK.

So for “final” code you **do not** need to keep the current “demo” entry points as the main path; they are useful only as examples. Prefer:

- **Either** call the new A7670E flow from a single place (e.g. from `app_main` after UART/AT init), **or**
- Keep “start modem init task” for init only, and have a **separate “connection task”** that waits for init done then does NETOPEN → CIPOPEN → GET → 200 OK → CIPRXGET=1 → main loop.

**Treat as demo-only (not the final path):**

- **integration_demo_task** – Runs examples then deletes itself. Good for testing; final product should not rely on it.
- **demo_task** (enhanced AT examples) – Same; demos only.
- **start_modem_control_examples** / **start_tcp_examples** – Demo entry points. Final code should have one clear path: init → connect → GET → ready → main loop (KEEP ALIVE, server commands, etc.).

**Summary:** For final code, use: **uart_rx_task + at_command_task** unchanged; **one** init path (modem_init_task or equivalent) that runs an **A7670E init table**; **one** connection/session path that does NETOPEN → CIPOPEN → GET (72+81) → wait 200 OK → CIPRXGET=1, then main loop. Demos can stay in the tree but not drive the main flow.

---

## 4. Storing the ~15+ AT Commands (Init + Network)

**Recommended: table-driven sequences with existing `at_command_def_t`.**

You already have:

- `at_command_def_t`: command, expected_response, timeout_ms, wait_for_ok, critical, description, failure_action.
- `execute_command_sequence(sequence, length, name)`.

Best approach:

- **One or more static const arrays** of `at_command_def_t` for A7670E:
  - **Phase 1 – Basic init**: AT, AT+CSQ, AT+CREG?, AT&F;E0, AT+CFUN=1, AT+CLIP=1, AT+CGMR, AT+CGSN, AT+CIPCCFG?, AT+CIPRXGET=0, AT+COPS?, AT+CICCID, AT+CGATT?, AT+CGACT=1,1, AT+CIPMODE?, AT+CGDCONT=1,"IP","internet". (Order as in log; expected_response e.g. "OK" or "+CSQ:" etc., timeouts 2–5 s; critical where needed.)
  - **Phase 2 – Network**: AT+NETCLOSE (expect "OK", then optionally wait for +NETCLOSE URC in RX), AT+NETOPEN (expect "OK" and/or "+NETOPEN"), AT+IPADDR (expect "+IPADDR:").
- **Where to put them**: A single file, e.g. `a7670e_sequences.c` (and `.h`), keeps A7670E-specific tables and a function like `run_a7670e_init()` that calls `execute_command_sequence` for phase 1, then phase 2. No need for a separate “config file” format; C tables are clear and easy to tune (timeouts, critical flags, descriptions).
- **Special cases**:
  - **NETOPEN**: Modem may reply OK then send URC "+NETOPEN: 0". Current engine can already see that line in the response queue; if you need to “wait for +NETOPEN”, you can add a short loop after AT+NETOPEN that consumes responses until "+NETOPEN" is seen or timeout.
  - **CIPSEND=1,72 / 1,81**: These are **not** plain AT+response; they are: send "AT+CIPSEND=1,72\r\n", wait for line containing ">", then send raw 72 bytes, then expect "OK" and "+CIPSEND: 1,72,72". So for “final” code you need a small helper, e.g. `send_at_then_raw_data(socket_id, len, payload)` that: (1) sends the AT+CIPSEND=1,len command, (2) waits for ">" (using existing response handling), (3) sends the raw payload, (4) waits for OK and optionally +CIPSEND. That keeps “storage” as: command string "AT+CIPSEND=1,%d" + length + pointer to payload; the rest is logic.

So: **store init and simple AT commands in `at_command_def_t` tables**. **GET part 1 (72 bytes):** use a template string and insert **UNIT_ID** (fixed length `crXXXXXXXX`) before send. **GET part 2 (81 bytes):** build the string from values **collected during startup and modem init** (FW_VERSION from CGMR, COPS_ID from COPS?, CSQ from CSQ, SIM_ID from CICCID); insert them into the string before send. **Implement one small helper** for “send CIPSEND=1,len then `>` then raw data” and reuse it for GET and later for KEEP ALIVE/ACK.

---

## 5. Extensibility (KEEP ALIVE, Commands, etc.)

Once you have “Got 200” and “In main loop..ready”:

- **KEEP ALIVE**: Timer (e.g. 30–60 s); each tick call the same “send then raw data” helper with a small KA packet (or minimal GET). If no response or connection lost, go to reconnect.
- **Reconnect**: On failure (no ACK to KA, or +CIPCLOSE/+CIPERROR): CIPCLOSE=1 → NETCLOSE → NETOPEN → CIPOPEN=1,… → then resend GET or go to main loop again.
- **Commands from server**: RX task already delivers lines; you can look for +IPD in the line (or a dedicated URC parser), extract payload, parse “list of available commands”, execute, then send ACK with the same CIPSEND=1,len + raw helper.
- **Incoming call**: CLIP=1 is already in init; URC with caller ID will appear as lines; parse and notify “server” (e.g. send caller ID over TCP), then act on server reply if caller is allowed.

So the same **one** “send AT+CIPSEND=1,len then `>` then raw bytes” helper and the **existing RX/response handling** are enough to extend to KEEP ALIVE, server commands, and ACK without redesigning the task layout.

---

## 6. Summary Checklist (Before Coding)

- [ ] **Goal**: Init (log order) → NETCLOSE/NETOPEN/IPADDR → CIPOPEN=1,"TCP",host,3000 → CIPSEND=1,72 + 72 bytes → CIPSEND=1,81 + 81 bytes → wait "200 OK" → CIPRXGET=1 → “main loop..ready”.
- [ ] **Keep**: uart_rx_task, at_command_task, modem_init_task *pattern*; one main application path (init + connect + GET + ready).
- [ ] **Demo-only**: integration_demo_task, demo_task, modem_control_examples, tcp_examples (do not drive final flow).
- [ ] **Storage**: `at_command_def_t` tables in e.g. `a7670e_sequences.c` for init and network; GET payloads as constants or built from config; one helper for CIPSEND=1,len + `>` + raw data.
- [ ] **A7670E-specific**: New init table and network table (no change to generic `modem_definitions` needed for first step; can add A7670E variant later for CIPOPEN/CIPSEND format if you want).
- [ ] **Later**: KEEP ALIVE, reconnect, server commands, ACK, incoming call — all via same CIPSEND helper and existing RX/response handling.

No code changes in the repo are made in this advisory; it only states the plan and the recommended structure so that when you implement, you can do it in a single, consistent way and then add KEEP ALIVE and server commands on top.

---

## 7. Current run baseline

**Source:** ESP32 project run (terminal log), e.g. `terminals/11.txt` or equivalent capture.

**What runs today:** `start_modem_control_examples()` runs five examples (Start & Wait, Start & Continue, Emergency Stop, Retry Mechanism, Conditional Init). The **same 8-step init** runs **four full times** (plus one aborted), so many log lines repeat.

**Observed:**

| Item | Current ESP32 run |
|------|-------------------|
| **Modem** | A7670E-LASA, firmware A131B05A7670M6C |
| **Network** | +CREG: 0,1; +CSQ: 28,99 / 26,99 (registered, good signal) |
| **Init steps** | 8 only: AT → E0 → CMEE=1 → CREG? → CSQ → CGMI → CGMM → CGMR (8/8 success each run) |
| **After init** | “Modem initialization control examples completed”; no NETOPEN, no TCP, no GET |
| **End** | “UART RX: Still listening on GPIO16 (no data for 10s)” |

**Gap vs reference:** No NETCLOSE/NETOPEN/IPADDR, no CIPOPEN, no CIPSEND(72+81), no “200 OK”, no CIPRXGET=1. Use this baseline when comparing future runs to the reference log.

---

## 8. Future merge with BLE project

When this project is done, it will be **merged** with the existing BLE project **`C:\Users\Danny\ESP32_projects\EG_BLE_server`**, which already works and includes **relay control**. The merged product will need both cellular (A7670E) and BLE (with relays). Project name will stay as-is until then; rename when done. Keep this in mind so the A7670E code (init, TCP, GET, main loop) can be integrated with the BLE/relay logic without major refactors.
