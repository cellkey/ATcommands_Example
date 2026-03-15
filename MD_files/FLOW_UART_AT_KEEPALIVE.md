# UART, AT Commands & Keepalive Flow (Educational)

This document describes how data flows between the ESP32, the modem (A7670E), and the server: UART RX/TX, AT command handling, and the keepalive (KA) task. Use it to understand the “new flow” and where each piece of logic lives.

---

## 1. High-level: main tasks and data paths

```
  ┌─────────────────┐     UART (modem)      ┌──────────────┐     TCP        ┌────────┐
  │  ESP32          │  ◄──────────────────►  │  A7670E      │  ◄──────────►  │ Server │
  │                 │   RX bytes / TX bytes  │  modem       │   link 1      │        │
  └────────┬────────┘                        └──────────────┘                └────────┘
           │
           │  main creates:
           │  • uart_rx_task        – reads UART, splits lines, process_line()
           │  • at_command_task     – takes AT commands from queue, sends on UART, waits for response
           │  • srv_ka (keepalive)  – sends KA "1\r\nA\r\n", waits for ACK, handles RING / +IPCLOSE
           │  • config_uart         – SET/GET/LIST on console UART0 (separate from modem UART)
```

- **Modem UART** is one port (e.g. UART2); only one task should be *sending* at a time → protected by **uart_mutex**.
- **RX path**: UART bytes → `uart_rx_task` → `process_rx_buffer()` → complete lines → `process_line()`.
- **TX path**: AT commands or raw data go out via the **AT command task** (queue + mutex) or via `uart_send_raw_bytes()` (mutex only).

---

## 2. UART RX path (modem → ESP32)

**File:** `enhanced_freertos_uart_at_commands.c`

1. **`uart_rx_task`**  
   - Reads from `UART_NUM` into `rx_buffer`.  
   - Calls **`process_rx_buffer()`** whenever new data arrives.

2. **`process_rx_buffer()`**  
   - Splits on `\n`, strips `\r`, trims leading whitespace so lines like `\r\n+CIPRXGET: 1,1` are matched.  
   - For each complete line: logs “RX Line: …”, then calls **`process_line(line)`**.

3. **`process_line(line)`**  
   - **URCs (unsolicited result codes):**  
     - **`+CIPRXGET:`** → parses link/mode, calls **`server_on_ciprxget_urc(link_id, param2)`** (e.g. “data available” on link 1).  
     - **`+IPCLOSE:`** → parses link_id, calls **`server_on_ipclose(link_id)`** (server closed connection).  
     - **`+CLCC:`** → extracts caller number, calls **`server_on_ring(caller_id)`** (incoming call).  
   - **Every line** is also:  
     - Copied into **`last_response`** (for code that expects “last AT response”).  
     - Pushed into **`response_queue`** (copy of line).  
     - **`response_ready_sem`** is given → so the AT command task can consume one line.

So: **one UART RX line** → possibly a server/keepalive callback (URC) **and** one entry in the response queue + one semaphore give for the AT command task.

---

## 3. AT command path (ESP32 → modem, then wait for response)

**File:** `enhanced_freertos_uart_at_commands.c`

1. **Caller** (e.g. keepalive task, modem init) builds an AT command and uses **`send_at_command_ex(cmd, expected, timeout_ms, wait_for_ok)`**.  
   - That pushes an **`at_command_t`** into **`at_command_queue`** and blocks on a **completion semaphore**.

2. **`at_command_task`** (single consumer):  
   - Takes one **`at_command_t`** from **`at_command_queue`**.  
   - **Takes `uart_mutex`** (so no one else sends on the modem UART).  
   - Sends `command + "\r\n"` with **`uart_write_bytes()`**, then **`uart_wait_tx_done(..., 100 ms)`** (wait *up to* 100 ms for TX to finish, not a fixed 100 ms delay).  
   - Waits for response: in a loop, **`xSemaphoreTake(response_ready_sem, 100 ms)`**; when taken, drains **`response_queue`** and checks each line for:  
     - ERROR/FAIL → result = ERROR.  
     - Expected data response (e.g. `+CIPRXGET: 2,1,6,0`) → stored in **`last_response`**; if `wait_for_ok` is false, done; else keep waiting for an **“OK”** line (exact line match, so “HTTP/1.1 200 OK” is not treated as AT OK).  
     - Standalone **“OK”** line → success, exit.  
   - On timeout or success: **releases `uart_mutex`**, gives **completion semaphore** → **`send_at_command_ex()`** returns.

So: **`uart_mutex`** = “who is allowed to send on the modem UART”. It is **not** a “message pending” flag; **`response_ready_sem` + `response_queue`** carry “a response line is available”.

---

## 4. Sending raw data after “>” (e.g. KA, OPENED, CHECK_USER)

**File:** `enhanced_freertos_uart_at_commands.c` (+ `server_keepalive.c` for payloads)

1. **`send_at_then_raw_data(socket_id, len, payload)`**  
   - Builds **`AT+CIPSEND=<socket_id>,<len>`** and calls **`send_at_command_ex(cmd, ">", 1000, false)`** so the AT task sends the command and waits until the modem replies with **`>`** (prompt to send raw bytes).  
   - Then **`uart_send_raw_bytes(payload, len)`**:  
     - Takes **`uart_mutex`**, sends **exactly `len` bytes**, **`uart_wait_tx_done(..., 500 ms)`**, releases **`uart_mutex`**.  
   - Then waits for a line containing **`+CIPSEND`** (e.g. `+CIPSEND: 1,6,6`) via **`wait_for_line_containing("+CIPSEND", 10000)`** (internal: uses response queue / semaphore).

So: **CIPSEND flow** = one AT command (get “>”) → one raw send (mutex, then done) → wait for `+CIPSEND` confirmation. All TX is still serialized by **`uart_mutex`**.

---

## 5. Keepalive (KA) task flow

**File:** `server_keepalive.c` (+ `server_keepalive.h` for timing constants)

**Role:** Send KA **`1\r\nA\r\n`** (6 bytes) to the server on link 1; treat “1” then “A” in received payload as ACK; handle RING (CHECK_USER), +IPCLOSE (reconnect), and “no ACK” (retry then reconnect).

### 5.1 Timing

- **First KA:** sent **8 s** after the task starts (`SERVER_KA_FIRST_DELAY_SEC`).  
- **Next KAs:** every **45 s** when idle (`SERVER_KA_INTERVAL_SEC`).  
- When waiting for ACK, the task uses a **1 s** timeout (`SERVER_KA_ACK_TIMEOUT_SEC`); if no ACK in that window, it treats “KA not ACKed” and will retry or reconnect.

### 5.2 Task loop (summary)

- **Wait** with **`ulTaskNotifyTake(pdTRUE, wait_sec * 1000)`**:  
  - `wait_sec` = 1 s if we just sent a KA and are waiting for ACK, else 45 s.  
- **If notified** (`n > 0`):  
  - **`s_pending_ring`** → hang up, send **CHECK_USER:<number>** to server, set **`s_waiting_check_user_response`**; next buffered read will parse APPROVED/REJECT.  
  - **`s_pending_read`** → **`do_read_buffered_data(s_pending_read_link)`** (see below).  
  - **`s_pending_ipclose_reconnect`** → **`run_a7670e_reconnect()`**, then continue.  
- **If timeout** (`n == 0`):  
  - If **`s_ka_sent_waiting_ack`** still true:  
    - First time: wait 8 s, send KA again (retry), set **`s_ka_retry_pending`**.  
    - Second time: **`run_a7670e_reconnect()`**, clear ACK/retry flags.  
  - Else: send next periodic KA, set **`s_ka_sent_waiting_ack`**.

### 5.3 How “pending read” is triggered (URC → task)

- Modem sends a line **`+CIPRXGET: 1,1`** (or similar) → **`process_line()`** calls **`server_on_ciprxget_urc(link_id, param2)`**.  
- **`server_on_ciprxget_urc()`** sets **`s_pending_read_link`**, **`s_pending_read = true`**, and **`xTaskNotifyGive(s_ka_task_handle)`**.  
- On next loop iteration the keepalive task sees **`s_pending_read`** and calls **`do_read_buffered_data(s_pending_read_link)`**.

So: **“pending read”** = “modem said there is data to read on this link”; the **flag** is **`s_pending_read`**; the **notification** wakes the KA task so it does the read immediately (or as soon as it’s not handling ring/ipclose).

### 5.4 Reading buffered data and detecting KA ACK

**`do_read_buffered_data(link_id)`** (in `server_keepalive.c`):

1. Sends **`AT+CIPRXGET=2,<link_id>`** (e.g. link 1) via **`send_at_command_ex(..., "+CIPRXGET: 2", 3000, false)`**.  
2. Parses **`last_response`** for payload length (e.g. `+CIPRXGET: 2,1,6,0` → 6 bytes).  
3. Sets **`s_ka_ack_since_read = false`**.  
4. **Drains** response lines with **`get_next_response_line(line, ..., 2000)`** until a line is exactly **“OK”** (or timeout).  
   - **`get_next_response_line`** = **`xQueueReceive(response_queue, ..., timeout)`**; those lines were enqueued by **`process_line()`** when the UART RX task received modem output (including the TCP payload lines).  
5. For each line before **“OK”**, calls **`server_handle_incoming_line(line)`**.  
6. **`server_handle_incoming_line()`** implements:  
   - **KA ACK:** if we see line **“1”** then line **“A”** → set **`s_ka_ack_since_read = true`**, log “KA ACK from server”.  
   - **CHECK_USER response:** if **`s_waiting_check_user_response`**, parse **APPROVEDxxx** / **REJECT** and set **`s_check_user_approved`**, **`s_approved_id`**.  
   - **OPEN302-style:** parse chunk length + command, drive relay and send **OPENED** ack.  
7. After drain: if **`s_ka_ack_since_read`** → **`s_ka_sent_waiting_ack = false`**, **`s_ka_retry_pending = false`**, log “KA OK..Unit Ready..”.  
8. If we were waiting for CHECK_USER and got APPROVED/REJECT → run relay, send OPENED ack if approved, then “Unit Ready”.

So: **KA ACK** is detected only **inside a buffered read**: modem sends **+CIPRXGET**, we do **CIPRXGET=2**, then the payload lines (from the server) are fed to **`server_handle_incoming_line()`**; “1” then “A” = ACK.

---

## 6. Synchronization summary

| Object | Role |
|--------|------|
| **uart_mutex** | Only one sender at a time on modem UART (AT task or `uart_send_raw_bytes`). **Not** “message pending”. |
| **at_command_queue** | Pending AT commands; **at_command_task** is the single consumer. |
| **response_queue** | Last N lines from modem (each line from **process_line**); consumed by AT task (response handling) and by **get_next_response_line** (drain after CIPRXGET=2). |
| **response_ready_sem** | One semaphore give per line pushed to **response_queue**; AT task waits on this to know “next line available”. |
| **xTaskNotifyGive(s_ka_task_handle)** | Wakes keepalive task when: **+CIPRXGET** (pending read), **+CLCC** (ring), **+IPCLOSE** (reconnect). |

---

## 7. Quick reference: who calls what

- **uart_rx_task** → **process_rx_buffer()** → **process_line()** → **server_on_ciprxget_urc** / **server_on_ipclose** / **server_on_ring**, and **response_queue** + **response_ready_sem**.
- **at_command_task** → sends AT commands, waits on **response_ready_sem** + **response_queue**, releases **uart_mutex** when done.
- **keepalive task** → **send_at_then_raw_data()** (KA, OPENED, CHECK_USER), **do_read_buffered_data()** → **get_next_response_line()** + **server_handle_incoming_line()**; waits on **ulTaskNotifyTake** (wake by **xTaskNotifyGive** from URC handlers).
- **send_at_then_raw_data** → **send_at_command_ex(..., ">", ...)** then **uart_send_raw_bytes()** then **wait_for_line_containing("+CIPSEND", ...)**.

---

## 8. KA payload (what we send)

- **Format:** exactly **6 bytes**: `0x31 0x0D 0x0A 0x41 0x0D 0x0A` = **`"1\r\nA\r\n"`** (same as old ATMEGA working code; no chunk prefix, no hidden chars).
- Sent with **AT+CIPSEND=1,6** then raw bytes. No extra **`\0`** is sent (C string terminator is not transmitted).

This file is for education and debugging; when you trace logs or add code, you can refer back to these sections to see which task and which queue/mutex/semaphore is involved.
