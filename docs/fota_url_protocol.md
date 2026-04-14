# FOTA over HTTP(S) — server protocol (closed spec)

This document defines how the **cellular gateway server** cooperates with the **ESP32 unit** when firmware is delivered by **URL fetch** instead of a raw binary stream on the existing modem TCP session.

It is intended for the server/backend implementer. The **in-band TCP framing** (length line + payload line) matches the style described in `main/fota_modem.h` for the legacy stream FOTA invite.

---

## 1. Goals

- Keep the **same TCP session** and **same “start FOTA” signal** where possible.
- Move the **large firmware payload** to **HTTP(S) GET** (normal web server, CDN, or static file).
- Allow **minimal server changes**: either a **fixed URL** (no new TCP text) or **one URL line** after the invite (small protocol extension).

---

## 2. Terms

| Term | Meaning |
|------|--------|
| **Unit** | ESP32 + modem, TCP client to gateway |
| **Gateway** | Existing TCP server (modem path) |
| **Image** | Raw ESP-IDF application binary (`.bin` for OTA partition), not JSON |

---

## 3. Variant A — Fixed URL (smallest gateway change)

**Gateway behavior**

1. Sends the **same FOTA invite** as today (e.g. length line `4` + payload line `FOTA`, CRLF-terminated as per existing protocol).
2. **Does not** send a firmware binary on the TCP socket after the unit acknowledges (see §5).
3. Hosts the image at a **well-known URL** agreed out-of-band (same host as today, CDN, etc.). Example: `https://ota.example.com/firmware/EG_Cellular_BLE.bin`.

**Unit behavior** (firmware — to be implemented)

1. On FOTA invite, responds as today (e.g. `START_FOTA` with existing framing).
2. Opens **HTTP(S) GET** to the **fixed/NVS-configured** URL (modem HTTP AT and/or second TCP socket — implementation detail on device).
3. Writes the response body to the OTA partition, validates, reboots.
4. After successful boot, may send **`FOTA_OK`** as today (NVS pending notify); on failure **`FOTA_OK`** is not sent and **`FOTA_ERROR`** may be sent (same as `fota_modem.h`).

**Gateway / ops**

- Ensure the URL returns **200 OK**, body = **raw image bytes** only.
- Prefer **`Content-Length`**; if chunked, unit firmware must support it (specify in implementation).

---

## 4. Variant B — URL supplied on TCP (one extra line)

Use when the image location must change per rollout without reflashing units.

**Sequence (after TCP connection is already established and length-prefixed lines are in use)**

1. **Gateway → Unit:** same FOTA invite as today (`FOTA` payload line).
2. **Unit → Gateway:** same ack as today (`START_FOTA` or equivalent).
3. **Gateway → Unit:** **one** payload line whose content is a **single absolute URL**, ASCII, UTF-8 compatible (US-ASCII URL only recommended).

   Suggested framing (example — align with your existing length-prefix rules):

   - First line: decimal length of the **next** line including its terminating CRLF, or length of URL line only — **must be specified identically in unit firmware and server**.
   - Second line: URL only, e.g. `https://cdn.example.com/builds/123/app.bin` then `\r\n`.

4. **Unit** performs HTTP(S) GET to that URL; **gateway sends no further binary firmware** on this TCP socket for this FOTA.

**URL constraints (recommended)**

- Scheme: `https:` preferred; `http:` only if acceptable for the deployment.
- No spaces; path/query as usual. Max length: **512 bytes** (or value TBD in firmware).

---

## 5. TCP socket during URL fetch

While the unit downloads via HTTP(S), the gateway should:

- **Either** send **no** data on the FOTA TCP socket until the unit reports completion/failure, **or**
- Send only **small control/keepalive** lines if the existing product protocol requires it — **must not** interleave a **legacy raw FOTA binary stream** with the URL-based flow unless the unit explicitly supports a combined mode (not recommended).

This avoids ambiguity with the old “binary after `START_FOTA`” behavior in `fota_modem.h`.

---

## 6. HTTP(S) hosting requirements (both variants)

| Item | Requirement |
|------|-------------|
| Method | `GET` |
| Success | HTTP **200**; body = **entire** OTA image |
| Type | `application/octet-stream` recommended (not mandatory if body is correct) |
| Size | Must match **compiled** OTA image size; unit may enforce a **maximum** (document in firmware) |
| TLS | Server certificate must validate against the trust store configured on the unit (or modem) |

---

## 7. Legacy stream FOTA (reference)

For comparison, the **current** modem FOTA stream (binary on the same TCP socket) is documented in `main/fota_modem.h`: sized line, raw `0xE9` magic, or `FOTA_DONE` terminated raw stream.

URL-based FOTA is a **separate mode**: once the unit enters “URL fetch” state, the server must **not** rely on that legacy binary framing for the same session unless explicitly negotiated (future work).

### 7.1 Firmware: HTTP download test only (no flash)

In `a7670e_config.h`, **`FOTA_URL_DOWNLOAD_TEST_ENABLE`** and **`FOTA_URL_DOWNLOAD_TEST_URL`** (set the full URL string when testing; empty **`""`** disables the URL path):

- When **ENABLE** is **1** and the URL is non-empty, the server line **`FOTA`** starts a background task that **GETs** the URL, reads the **full body**, logs **byte count** and **first byte vs 0xE9**, and **does not** call `esp_ota_*`, **does not** send **`START_FOTA`**, and **does not** reboot.
- **Transport:** if **WiFi STA has IP**, the ESP32 uses **`esp_http_client`** (TLS via certificate bundle). **Otherwise**, if the unit is **not** WiFi-only (modem build), the task uses **SIMCOM `AT+HTTPINIT` / `HTTPPARA` / `HTTPACTION` / `HTTPREAD`** on **cellular** (no WiFi required). HTTPS on the modem uses best-effort **`AT+CSSLCFG`** / **`HTTPPARA="SSLCFG"`** — adjust for your module/firmware if TLS fails.
- **WiFi-only** with STA down: URL test does not start; legacy stream FOTA is N/A on WiFi TCP (no modem).
- When **ENABLE** is **0** or URL is empty, modem path uses **legacy stream FOTA** as in `fota_modem.h`.

---

## 8. Versioning

- Document revision: **1.0** (2026-03-31).
- When the unit firmware implements this spec, record in release notes: **which variant (A and/or B)** and exact **length-prefix** rules for Variant B.

---

## 9. Contact / ownership

- **Firmware / protocol owner:** product team (this repo).
- **Server implementation:** gateway maintainer implements §3 or §4 and §6 on the infrastructure side.
