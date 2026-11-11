---
description: "RoofRoof's security model: built for a trusted LAN, optional HTTP Basic Auth on changes, no TLS, and how to deploy it safely."
---

# Security

RoofRoof is an observatory roof controller (its roof features come in later specs) designed to run on a trusted LAN. This page describes the security model, what HTTP auth protects, and recommended deployment.

---

## Threat model

RoofRoof assumes the local network is trusted. The main risks on a typical home or observatory LAN are:

- **Unauthorised config changes** — any device on the same WiFi segment can call `POST /api/config` and change settings, including WiFi credentials or the MQTT broker.
- **Unauthorised updates** — any device on the LAN can flash new firmware or a new filesystem image via `POST /api/update`.
- **Credential exposure** — credentials sent over HTTP are visible to passive LAN observers. RoofRoof does not support TLS.
- **Restart/reboot abuse** — `POST /api/restart` is unauthenticated by default; any LAN device can reboot the device.

RoofRoof does **not** defend against:

- An attacker who already has LAN access and captures HTTP traffic (no TLS).
- Brute-force attacks; there is no rate limiting or account lockout.
- Remote internet attackers — do not expose the device to the public internet.

Over IPv6 the device only accepts requests from link-local addresses and its own network prefixes
([IPv6](ipv6.md)), so a router that lets IPv6 in doesn't open it to the internet by itself. That's a
backstop, not protection: keep it behind your firewall.

---

## HTTP authentication

HTTP Basic Auth can be enabled to require credentials on mutation endpoints. This is disabled by default.

### Protected endpoints (when auth is enabled)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/config` | POST / PUT | Save configuration |
| `/api/backup`, `/api/backup/preview`, `/api/backup/apply` | POST | Back up and restore settings |
| `/api/restart` | POST | Reboot device |
| `/api/update` | POST | Flash firmware OTA |
| `/api/update/fs` | POST | Flash filesystem OTA |
| `/api/wifi/scan` | GET | Scan for WiFi networks (starts a radio scan) |
| `/api/wifi/connect` | POST | Change WiFi network |
| `/api/mqtt/test` | POST | Test MQTT broker connection |
| `/api/alerts/test` | POST | Send a test alert |
| `/api/alerts/arm`, `/api/alerts/disarm` | POST | Resume / pause alerts |
| `/api/alerts/clear` | POST | Clear the recent-alerts list |
| `/api/roof/open`, `/api/roof/close`, `/api/roof/stop`, `/api/roof/clear-fault` | POST | Move the roof, clear a fault (also need the header `X-RoofRoof: 1`) |

### Unprotected endpoints (always accessible)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/status` | GET | System status |
| `/api/config` | GET | Read config (passwords, keys and tokens masked) |
| `/api/alerts/recent`, `/api/alerts/armed` | GET | Recent alerts, sending or paused and why |
| `/api/settings/effective` | GET | Which settings are in effect, and why not (no secrets) |
| `/api/v1/...`, `/management/...` | GET / PUT | ASCOM Alpaca (the Alpaca spec has no auth; no device is served yet) |
| `/ws/status` | WS | Live status stream |

Read-only integrations (Home Assistant, scripts polling the status) continue to work without credentials when auth is enabled.

Every route and whether it needs the password is listed in `tools/api/routes.json`; CI fails if a new route isn't declared there, so a route can't ship open by accident.

### Setup

1. Open **Settings → Device** in the web UI.
2. Under **Security**, turn on **Password-protect changes**.
3. Set a username (default `admin`) and a strong password.
4. **Save**.

Once saved, the browser will prompt for credentials on the next mutation request. Most browsers remember credentials per origin for the session.

### Password masking and preservation

`GET /api/config` returns `********` for the auth password (and likewise for the WiFi/MQTT/OTA passwords, Pushover keys, ntfy token and webhook auth header). When you POST config back, sending `********` or an empty string preserves the stored password unchanged. Send `null` only to intentionally clear the password.

### Backups

A settings backup made without a passphrase holds no secret. With one, the secrets are encrypted on the
device (AES-256-GCM, key from the passphrase with PBKDF2-HMAC-SHA256), so a lost backup file doesn't give
away your passwords; a weak passphrase can still be guessed offline, so use a long one. The passphrase
itself travels to the device over plain HTTP like the device password does, so back up and restore on
your own network. See [Backup & Restore](configuration.md#backup-restore).

### Password reset / lockout

If you forget the HTTP auth credentials:

1. Connect the device to a serial console (115200 baud).
2. Factory-reset via NVS clear: flash a sketch that calls `nvs_flash_erase()`, or use `esptool.py` to erase NVS.
3. The device will start with default config (no auth).

Alternatively, if you have physical access and can reflash, flash the release's USB package ([USB flash](../getting-started/usb-flash.md)) after erasing the flash.

---

## OTA security

Web upload (`/api/update`, `/api/update/fs`) does not require authentication by default — any LAN device can push firmware. Enable HTTP auth to gate these endpoints.

Command-line ArduinoOTA is separate and controlled by the `ota.enabled` and `ota.password` config fields. It is disabled unless explicitly configured.

---

## Recommendations

| Scenario | Recommendation |
|----------|---------------|
| Home observatory, trusted LAN | Enable HTTP auth with a strong password |
| Shared network or observatory with multiple users | Enable HTTP auth; consider VLAN isolation |
| Public internet exposure | **Do not do this.** Place behind a VPN or firewall. |
| Roof safety | Keep physical safety interlocks (limit switches, supply cut-outs) independent of the network and of this firmware's web interface |

---

## What HTTP auth does not protect

- Traffic confidentiality: credentials and device data travel in plaintext over HTTP. Anyone who can observe the LAN can capture them.
- Physical access: someone with physical access to the device can reset it.
- Denial of service: unauthenticated read endpoints remain open and could be flooded.
- Replay attacks: no session management. Cross-site requests: the roof commands need the header `X-RoofRoof: 1`, which another web page can't make your browser send in a cross-site request, so a malicious page can't move the roof. Other protected routes rely on HTTP auth alone.
