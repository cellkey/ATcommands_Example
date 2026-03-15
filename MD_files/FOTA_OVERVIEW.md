## Firmware Over-The-Air (FOTA) – Overview and Plan

This note summarizes what we need for future OTA updates on this ESP32 + A7670E project.

---

### 1. Goals

- **Remote firmware updates** without physical access or USB.
- **Safe updates**: never brick units, allow automatic rollback.
- **Controlled rollout**: server decides when/which unit updates (based on `unit_id` / `fw_ver`).

---

### 2. ESP32 / ESP-IDF prerequisites

- **Partition table** with OTA slots:
  - `nvs` (already used for config: `unit_id`, `fw_ver`, `apn`, etc.).
  - `ota_0`, `ota_1` (two app partitions for ping‑pong updates).
  - Optional `factory` slot for very first firmware.
- **Bootloader**: default ESP‑IDF bootloader already supports OTA partitions.

When we enable FOTA, we will switch to a partition table like `partitions_two_ota.csv` (or a custom one with the same ideas).

---

### 3. Transport: how new firmware is delivered

- We already use the modem and TCP to reach the server and send a **GET**.
- For OTA we need an **HTTP/HTTPS download path**:
  - Server exposes a firmware image (`.bin`) per product/version.
  - Device downloads that image **over the modem link**, chunk by chunk.
- Two options:
  - **Direct ESP‑IDF OTA helper**: `esp_https_ota()` against a known URL.
  - **Manual OTA**: use `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` and feed data from our existing TCP connection.

We can reuse most of the A7670E TCP logic; OTA will just be an additional “client” of that link.

---

### 4. Device‑side OTA flow (high level)

1. **Check with server if update exists**
   - Use `unit_id` and current `fw_ver` (already sent in GET headers).
   - Server decides and answers e.g.:
     - “no update” or
     - “update available at URL + checksum + target version”.
2. **Download firmware image**
   - Open HTTP connection (over modem) to the provided URL.
   - Stream data into the inactive OTA partition using ESP‑IDF OTA APIs.
3. **Verify + set boot partition**
   - ESP‑IDF OTA checks image header.
   - Optionally verify checksum from server.
   - Call `esp_ota_set_boot_partition()` to switch to new app slot.
4. **Reboot and confirm**
   - On first boot into new image, mark it as **“pending”**.
   - Only after the app runs correctly (e.g. KA OK, some uptime, no fatal errors) mark it **“confirmed”**.
   - If not confirmed or boot fails → bootloader rolls back to previous image.

---

### 5. Server‑side responsibilities

- Store firmware images by **product / hardware revision** and **version**.
- Keep metadata:
  - version string (e.g. `1.2.27`),
  - size, checksum (SHA256),
  - minimal required previous version (optional).
- Expose an API (reusing your existing server):
  - e.g. `GET /api/device/<unit_id>/fw` → JSON with:
    - current server‑desired version,
    - URL for `.bin` image,
    - checksum.
- Optionally drive updates by **commands**:
  - e.g. server sends a command in the long‑poll/KA stream: `UPDATE:1.2.30` to tell the unit to start OTA.

---

### 6. Integration with current project

When we are ready to implement FOTA, concrete steps on this codebase will be:

- **Step 1: partition table**
  - Switch to a two‑OTA partition table in the project config.
- **Step 2: OTA module**
  - Create `ota_update.c` + header:
    - function to query server for update (using modem TCP),
    - function to perform OTA download + apply via ESP‑IDF OTA APIs,
    - simple state machine (idle → downloading → pending reboot → confirmed / rollback).
- **Step 3: hook into flow**
  - After successful `run_a7670e_connect_and_register()` and KA OK, optionally:
    - check for update once, or
    - wait for a server command to trigger update.
- **Step 4: health/rollback logic**
  - Define what “boot OK” means (e.g. reaches keepalive OK within X seconds).
  - Store a flag in NVS to mark image confirmed.

---

### 7. What we already have that helps

- **NVS**: for `fw_ver` (current version) and any future OTA settings (e.g. “auto‑update on/off”).
- **Reliable TCP link** with server and detailed logs.
- **Unit ID** as single source for identification.

---

### 8. Next practical step (when you want)

When you say “let’s start FOTA”, we will:

1. Pick/define the OTA partition table and add it to the project.
2. Create a minimal `ota_update.c` skeleton using ESP‑IDF’s OTA APIs.
3. Agree with the server programmer on the **exact API/URL** and metadata format for firmware updates.\n*** End Patch***} ***!
