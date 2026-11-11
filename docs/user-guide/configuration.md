---
description: "Configure RoofRoof from the web UI or REST API: device name, network, MQTT, time, password protection and alerts. Settings live in NVS."
---

# Configuration

All settings are stored in NVS (Non-Volatile Storage) and survive firmware and filesystem updates. Configure via the web UI (**Settings**) or the REST API. Roof settings (travel, speed, safety rules) *come in later specs*.

## Settings pages

**Settings** has four tabs. Changes are kept until you **Save**; a tab with a problem shows a red dot, and settings that only take effect after a restart ask for one when you save.

Some settings only work when something else is on - MQTT alerts need MQTT, internet alerts need WiFi. Such a setting can't be switched on while what it needs is off, and if you switch the other thing off later the setting is kept exactly as it was but marked **Inactive** with the reason and a button that takes you to the fix. The device doesn't act on it until then, and nothing has to be set up again when you switch the other thing back on. Before you save, a note lists anything the change would make inactive. Every such setting is listed in [Settings dependencies](../reference/settings-dependencies.md).

=== "Device"
    Name, password protection, command-line uploads (ArduinoOTA) and Backup & restore.

    ![Device settings](../assets/screenshots/settings-device.png)

=== "Network"
    WiFi, mDNS, IPv6, MQTT including Home Assistant discovery - see [MQTT](mqtt.md) - and ASCOM Alpaca - see [ASCOM Alpaca](alpaca.md).

    ![Network settings](../assets/screenshots/settings-network.png)

=== "Time"
    NTP and the time zone.

    ![Time & Location settings](../assets/screenshots/settings-time.png)

=== "Alerts"
    Events, levels, wording and channels - see [Alerts](alerts.md).

    ![Alert settings](../assets/screenshots/settings-alerts.png)

---

## Full Configuration Reference

`GET /api/config` returns every setting (secrets masked). The fields, with their defaults:

```json
{
  "deviceName": "RoofRoof",
  "wifi": {
    "ssid": "YourWiFiSSID",
    "password": "YourWiFiPassword",
    "hostname": "roofroof",
    "mdns": true,
    "ipv6": true,
    "autoReconnect": true,
    "reconnectDelayMs": 1000,
    "maxReconnectDelayMs": 300000
  },
  "ntp": {
    "enabled": true,
    "server1": "pool.ntp.org",
    "server2": "time.nist.gov",
    "timezone": "UTC0",
    "syncIntervalMs": 600000
  },
  "mqtt": {
    "enabled": false,
    "broker": "",
    "port": 1883,
    "username": "",
    "password": "",
    "topic": "roofroof",
    "publishIntervalMs": 60000,
    "homeAssistant": { "enabled": false, "discoveryPrefix": "homeassistant" }
  },
  "ota": { "enabled": false, "password": "" },
  "auth": { "enabled": false, "username": "admin", "password": "" },
  "alpaca": { "enabled": false },
  "alerts": {
    "enabled": false,
    "sendMode": "any",
    "cooldownSeconds": 300,
    "clientSilentSeconds": 120,
    "events": {
      "client_lost": { "level": 3, "sound": "", "title": "", "message": "" },
      "client_back": { "level": 1, "sound": "", "title": "", "message": "" },
      "client_disconnected": { "level": 0, "sound": "", "title": "", "message": "" }
    },
    "pushover": { "enabled": false, "userKey": "", "appToken": "", "sound": "" },
    "ntfy": { "enabled": false, "server": "https://ntfy.sh", "topic": "", "token": "" },
    "webhook": { "enabled": false, "url": "", "authHeader": "", "insecureTls": false },
    "mqtt": { "enabled": false }
  }
}
```

The JSON Schema is `config.schema.json`; the [API Reference](../api/reference.md) links it from `GET /api/config`.

## Fields

### Device

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `deviceName` | string | `"RoofRoof"` | Friendly name shown in the web UI, in alerts and as the Alpaca server name |

### WiFi

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ssid` | string | — | Network name (2.4 GHz only) |
| `password` | string | — | Network password |
| `hostname` | string | `"roofroof"` | Network name: `hostname.local` via mDNS and the DHCP host name. Letters, numbers and hyphens, up to 32 |
| `mdns` | bool | `true` | Advertise `hostname.local` and the web service via mDNS |
| `ipv6` | bool | `true` | IPv6 alongside IPv4 on the joined network; applies after a restart - see [IPv6](ipv6.md) |
| `autoReconnect` | bool | `true` | Reconnect on WiFi drop |
| `reconnectDelayMs` | int | `1000` | Initial reconnect delay (ms) |
| `maxReconnectDelayMs` | int | `300000` | Max reconnect backoff — 5 min |

### NTP

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `true` | Enable NTP time sync |
| `server1` | string | `"pool.ntp.org"` | Primary NTP server |
| `server2` | string | `"time.nist.gov"` | Fallback NTP server |
| `timezone` | string | `"UTC0"` | POSIX timezone string |
| `syncIntervalMs` | int | `600000` | Re-sync interval — default 10 minutes |

!!! tip "POSIX timezone strings"
    Examples: `GMT0`, `EST5EDT,M3.2.0,M11.1.0`, `PST8PDT,M3.2.0,M11.1.0`, `CET-1CEST,M3.5.0,M10.5.0/3`.
    A full list is available at [timezonedb.com](https://timezonedb.com).

### MQTT

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable MQTT |
| `broker` | string | — | Broker hostname, IPv4 address, or IPv6 address (`fd00::10`, or `[fd00::10]:1883` to give the port here) - see [IPv6](ipv6.md) |
| `port` | int | `1883` | Broker port |
| `username` | string | `""` | Auth username (leave empty if none) |
| `password` | string | `""` | Auth password |
| `topic` | string | `"roofroof"` | Base topic: letters, numbers, `_`, `-`, with `/` between levels. See [MQTT](mqtt.md#topics) |
| `publishIntervalMs` | int | `60000` | Interval for periodic documents (nothing is published periodically yet) |
| `homeAssistant.enabled` | bool | `false` | Home Assistant MQTT discovery |
| `homeAssistant.discoveryPrefix` | string | `"homeassistant"` | Discovery prefix |

### ArduinoOTA

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable command-line ArduinoOTA uploads |
| `password` | string | `""` | Needed for command-line ArduinoOTA to start |

ArduinoOTA only runs when `ota.enabled` is `true` and `ota.password` is set. Switched on without a password it is saved but inactive ("Set an upload password"). The web UI upload page ([Updates](ota.md)) is separate from command-line ArduinoOTA.

### HTTP Authentication

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Require credentials for mutation endpoints |
| `username` | string | `"admin"` | Username for HTTP Basic Auth |
| `password` | string | `""` | Password - a save with auth on and no password is refused: "HTTP auth password is required when auth is enabled" |

When `auth.enabled` is `true`, the endpoints marked "required" in the [Security](security.md#protected-endpoints-when-auth-is-enabled) table need HTTP Basic Auth credentials. Read-only endpoints (`GET /api/status`, `GET /api/config`, `/ws/status`, ...) remain accessible without credentials.

The `auth.password` field is masked in `GET /api/config` responses. Send the mask placeholder or an empty string to preserve the stored password; send `null` to clear it.

### ASCOM Alpaca

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `alpaca.enabled` | bool | `false` | Serve Alpaca discovery and the management API (a restart starts or stops the discovery listener). See [ASCOM Alpaca](alpaca.md) |

### Alerts

See [Alerts](alerts.md) for events, levels and channels.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Send alerts at all |
| `events.<event>.level` | int | see above | 0 off, 1 quiet, 2 normal, 3 urgent, 4 wake me |
| `events.<event>.sound` | string | `""` | Pushover sound; empty uses `pushover.sound` |
| `events.<event>.title` / `.message` | string | `""` | Your own wording with `{variables}` (up to 80 / 240 characters); empty uses the default |
| `sendMode` | string | `"any"` | When to send: `any` (any time, unless paused) or `whileConnected` (only while an imaging app has an Alpaca device connected) |
| `clientSilentSeconds` | int | `120` | "The imaging app stops checking" after this long without a request (30-3600) |
| `cooldownSeconds` | int | `300` | Minimum gap between alerts of the same kind (0-86400) |
| `pushover`, `ntfy`, `webhook`, `mqtt` | object | off | Channel settings; secrets are masked in `GET /api/config` |

Events: `client_lost`, `client_back`, `client_disconnected`. Paused or sending is live state, not a setting - see [When to send](alerts.md#when-to-send).

## Security Notes

RoofRoof is designed for a trusted LAN environment. See the [Security Guide](security.md) for a full threat model and setup recommendations.

- By default there is no HTTP authentication; read-only and mutation endpoints are both accessible to any LAN device.
- Enable `auth.enabled` with a strong password to protect mutation endpoints (config save, uploads, restart, WiFi change).
- `GET /api/config` redacts stored secrets (WiFi, MQTT, OTA, and auth passwords), but still exposes non-secret settings.
- HTTP traffic is plaintext, so credentials sent via config updates are visible to LAN observers. TLS is not supported on the ESP32 in this firmware.
- Command-line ArduinoOTA is disabled unless `ota.enabled` is `true` and `ota.password` is set.

Keep the device on a private network or isolated observatory VLAN. Do not expose port 80 or ArduinoOTA to the public internet.

---

## Updating via API

```bash
# Read current config
curl http://roofroof.local/api/config

# Partial update — only the fields you include are changed
curl -X POST http://roofroof.local/api/config \
  -H "Content-Type: application/json" \
  -d '{"deviceName": "observatory-roof", "ntp": {"server1": "time.google.com"}}'
```

Password fields returned by `GET /api/config` are masked as `********`. Sending a masked or empty password back to `POST /api/config` preserves the stored value; send `null` for a password field only when you intentionally want to clear it.

---

## Backup & Restore

**Settings → Device → Backup & restore** saves every setting to a file and puts it back, on the same
device or on a replacement.

### Back up

1. Optionally type a **Passphrase** (8 characters or more), twice.
2. Press **Back up settings**. The browser saves `roofroof-<name>-<date>.json`.

The file is readable JSON: a short header (format, firmware version, board, build, device name, when it
was made) and `settings`, exactly as `GET /api/config` shows them. Without a passphrase it holds **no
password, key or token** at all. With a passphrase, the secrets (WiFi, MQTT, ArduinoOTA and device
passwords, the Pushover keys, the ntfy token and the webhook authorization) are
added encrypted: AES-256-GCM under a key derived from the passphrase with PBKDF2-HMAC-SHA256 (20,000
iterations, random salt). The device makes the file; the secrets never leave it in clear text.

!!! warning "The passphrase can't be recovered"
    The passphrase isn't stored anywhere. If it's lost, the passwords and keys in that file are lost
    too; everything else in it still restores. Keep the file somewhere private all the same: it shows
    your network name, MQTT broker and alert settings.

### Restore

1. Press **Restore settings** and choose the file.
2. If the file has encrypted secrets, type its passphrase under **Passphrase for this file** and press
   **Preview**. Without it, this device keeps its own passwords and keys and everything else restores.
3. Check the preview, then press **Restore** (or **Cancel**: nothing changes until you press Restore).

The preview lists, per section, every setting that will change with its current and new value; each
password or key only as "Will change" or unchanged; settings **Not in the backup** (a backup from older
firmware: they keep their values); settings **Unknown to this firmware** (a backup from newer firmware:
they're ignored); and settings this board **Can't apply here**, with the reason. It says when restoring
restarts the device and warns before changes that can cut you off: another WiFi network (if the device
can't join it, use the [WiFi setup hotspot](../getting-started/first-setup.md)), a new device password
(the next page load asks for the backup's), a new local name, or a restart while an imaging app is
connected. Restore applies exactly what the preview showed, as one settings save with the usual checks.

A wrong passphrase, a damaged file, a file from newer firmware's backup format, or a file that isn't a
RoofRoof backup is refused with the reason, and nothing changes.

### Across boards and versions

A setting this device can't use isn't applied; the device keeps its own value and the preview lists it
under **Can't apply here**. So is Pushover switched on when its keys weren't restored.
A plain `GET /api/config` export still restores, without secrets.

### From a script

The same three steps are in the [REST API](../api/rest.md#post-apibackup), each needing the password
when protection is on:

```bash
# Back up (secrets encrypted with the passphrase)
curl -u admin:pass -X POST http://roofroof.local/api/backup \
  -H "Content-Type: application/json" -d '{"passphrase": "correct horse battery"}' -o backup.json

# Preview: nothing changes yet; note the token
jq -n --rawfile f backup.json '{backup: $f, passphrase: "correct horse battery"}' |
  curl -u admin:pass -X POST http://roofroof.local/api/backup/preview \
  -H "Content-Type: application/json" -d @-

# Apply exactly that preview
curl -u admin:pass -X POST http://roofroof.local/api/backup/apply \
  -H "Content-Type: application/json" -d '{"token": "<token from the preview>"}'
```
