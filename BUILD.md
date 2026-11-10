# RoofRoof - Build and Upload Guide

RoofRoof builds for one target: the classic ESP32 (`esp32dev`), with one partition layout (`l2`, see `partitions.csv`). There is no Bluetooth build, no ESP32-S3 build and no language pack.

## Quick Start

### 1. Install Dependencies

**PlatformIO:** use the PlatformIO IDE extension in VS Code, or the CLI. The firmware uses the pioarduino platform (Arduino-ESP32 3.x), which needs PlatformIO Core 6.2 or newer.

```bash
pip install platformio
```

**Node.js 24 or newer** (the web UI and the docs tooling):

```bash
# macOS
brew install node
```

### 2. Build Firmware

```bash
pio run -e esp32dev
```

The build fails on any warning in the project's own sources (`-Werror`).

### 3. Build Web Interface

```bash
cd web
npm install
npm run build
cd ..
```

`scripts/build_web.py` builds the web UI automatically before the filesystem image is created.

Check the docs build:

```bash
python3 -m venv .venv-docs && .venv-docs/bin/pip install -r docs/requirements.txt
(cd web && node scripts/vendor-mermaid.mjs && node scripts/vendor-scalar.mjs)
.venv-docs/bin/mkdocs build --strict
```

### 4. Upload to ESP32

**Upload firmware only (for code changes):**
```bash
pio run --target upload
```

**Upload filesystem (for web UI changes):**
```bash
pio run --target uploadfs
```

**First-time setup (upload both):**
```bash
pio run --target uploadfs && pio run --target upload
```

**Note:** Configuration (WiFi credentials, MQTT settings, etc.) is stored in NVS (Non-Volatile Storage), which is separate from the filesystem. You can safely upload the filesystem without losing your WiFi configuration.

**Before wiring** the motor driver to the ESP32, fit the 10 kΩ pull-down on EN ([docs/hardware/wiring.md](docs/hardware/wiring.md)). While flashing, the driver's pins float.

### 5. Monitor Serial Output

```bash
pio device monitor
```

## Development Workflow

### Backend Development (C++)

1. Make changes to C++ files in `src/`, `include/` or `lib/`
2. Build: `pio run`
3. Test the pure logic on your computer: `pio test -e native`
4. Upload: `pio run --target upload`
5. Monitor: `pio device monitor`

### Frontend Development (TypeScript/Preact)

1. Start dev server:
   ```bash
   cd web
   ESP32_IP=192.168.1.xxx npm run dev
   ```
2. Open http://localhost:5173
3. Edit files in `web/src/`
4. Changes auto-reload in browser

In dev mode, API and WebSocket calls are proxied to the ESP32 device. Set the `ESP32_IP` environment variable to your device's IP address.

### Production Build

1. Build web UI: `cd web && npm run build`
2. Upload filesystem: `pio run --target uploadfs`
   - This only updates the web UI files
   - Your WiFi config and settings are stored in NVS and won't be affected
   - For firmware-only updates, use `pio run --target upload`

## Updates

Updates are **manual upload only**. The device never checks GitHub or any server for a new version.

### Via the web interface

1. Build firmware: `pio run` (`.pio/build/esp32dev/firmware.bin`) and the web UI image: `pio run --target buildfs` (`.pio/build/esp32dev/littlefs.bin`), or take both from a release.
2. Open **Updates** in the web UI.
3. Choose the image (**Firmware** or **Web UI**), choose the file and upload it.
4. The device checks the file is RoofRoof firmware for its partition layout, writes it and restarts.

See [docs/user-guide/ota.md](docs/user-guide/ota.md).

### Via PlatformIO (ArduinoOTA)

ArduinoOTA uploads are disabled unless `ota.enabled` is `true` and `ota.password` is configured on the device. Use the same password in PlatformIO.

```bash
# Configure in platformio.ini:
# upload_protocol = espota
# upload_port = <device-ip>
# upload_flags = --auth=<ota-password>

pio run --target upload
```

The web upload endpoints (`POST /api/update` for firmware and `POST /api/update/fs` for LittleFS) need the password only when **Password-protect changes** is on, and plain HTTP carries it. Use them on a trusted LAN.

## Partition Table

One whole-chip layout, `l2` (`partitions.csv`):

| Name | Type | SubType | Offset | Size |
|------|------|---------|--------|------|
| nvs | data | nvs | 0x9000 | 20KB |
| otadata | data | ota | 0xe000 | 8KB |
| app0 | app | ota_0 | 0x10000 | 1.75MB |
| app1 | app | ota_1 | 0x1D0000 | 1.75MB |
| spiffs | data | spiffs | 0x390000 | 448KB |

This allows:
- Updates with two app partitions, written while the old firmware runs and rolled back if the new one fails to start
- 448KB for the compressed LittleFS web UI
- NVS for WiFi credentials, MQTT settings, and other configuration

Changing the layout needs a USB flash: an upload can't rewrite the partition table. Every firmware image carries a marker naming its product and layout, and the device refuses an upload made for another ([docs/user-guide/ota.md](docs/user-guide/ota.md)).

## USB flash package

CI builds `roofroof-l2-usb-<tag>.zip` (the bootloader, partition table, firmware and web UI at their offsets, plus `FLASH.txt` with the `esptool` command). To make one locally after a build:

```bash
python3 tools/firmware/usb_package.py --version 0.1.0 --build-dir .pio/build/esp32dev \
  --fs .pio/build/esp32dev/littlefs.bin --boot-app0 <path to boot_app0.bin> --out roofroof-l2-usb.zip
```

## Troubleshooting

### Build Errors

**Missing libraries:**
```bash
pio pkg install
```

**Clean build:**
```bash
pio run --target clean
pio run
```

### Upload Errors

**Port not found:**
```bash
pio device list
pio run --target upload --upload-port /dev/cu.usbserial-*
```

**Permission denied (macOS/Linux):**
```bash
sudo chmod 666 /dev/ttyUSB0
# or
sudo usermod -a -G dialout $USER
```

### Web UI Issues

**Build fails:**
```bash
cd web
rm -rf node_modules
npm ci
npm run build
```

**Hot reload not working:**
- Check proxy configuration in `vite.config.ts`
- Verify the ESP32 is reachable at the configured IP
- Check browser console for errors

### Filesystem Upload Fails

- Check the UI still fits: `python3 tools/ui/size_check.py` (the stored UI must stay under 75% of the partition)
- If LittleFS won't mount, re-upload the filesystem: `pio run --target uploadfs`

## Continuous Integration

Three workflows matter for a build:

- **Build Firmware & LittleFS** (`.github/workflows/build.yml`): web typecheck and tests, native tests, contract and size checks, the firmware and filesystem builds, the USB package. On a `v*` tag its release job publishes `roofroof-l2-firmware-<tag>.bin`, `roofroof-l2-littlefs-<tag>.bin`, `roofroof-l2-usb-<tag>.zip` and `roofroof-openapi-<tag>.json`.
- **Quality** (`quality.yml`): the coding standard, on every pull request.
- **Docs & Demo** (`docs.yml`): the demo, screenshots, accessibility and diagram checks and `mkdocs build --strict`, uploaded as the `site` artifact. There is no Pages deploy.

## Backup & Restore

Use **Settings → Device → Backup & restore**, or:

```bash
curl http://device-ip/api/config > config-backup.json     # secrets masked
```

See [docs/user-guide/configuration.md](docs/user-guide/configuration.md#backup-restore) for the encrypted backup with secrets. For a complete backup also keep `.pio/build/esp32dev/firmware.bin` and `littlefs.bin`.
