# Development Workflow

## Config Storage - NVS (Non-Volatile Storage)

Configuration is stored in NVS, not in the filesystem. This means:

- You can freely upload the filesystem without losing WiFi config.
- No setup-hotspot loop during development.
- Web UI updates don't reset settings.

## Quick Commands

### Backend (C++) Development
```bash
pio run --target upload
```
Changes to the web server, MQTT, WiFi manager, the motor driver output code, etc. Pure logic lives in `lib/` and is tested natively with `pio test -e native`.

### Frontend Development

#### Option 1: Local Dev Server (best for active development)
```bash
cd web
# Set your ESP32's IP address
ESP32_IP=192.168.1.128 npm run dev
```
Then open http://localhost:5173

The WebSocket works because the proxy is configured.

#### Option 2: Build and Upload (test on device)
```bash
cd web
npm run build
cd ..
pio run --target uploadfs   # safe - won't erase WiFi config
```

## First Setup (the hotspot)

A new device, or one with no WiFi saved, opens the **RoofRoof-Setup** hotspot at `192.168.4.1`.

### To Configure WiFi
1. Connect to the "RoofRoof-Setup" WiFi network
2. Go to http://192.168.4.1/wifi (the sign-in window usually opens it)
3. Select your network and enter the password
4. It connects, shows the new address (`roofroof.local` and the IP), and restarts onto your network
5. Your WiFi config is now saved in NVS

### For Development After WiFi is Configured

**Web dev:**
```bash
cd web
ESP32_IP=<your-device-ip> npm run dev
```

**Firmware dev:**
```bash
pio run --target upload  # your WiFi config stays intact
```

**Web UI updates:**
```bash
cd web
npm run build
cd ..
pio run --target uploadfs  # your WiFi config stays intact
```

## Motor driver safety while developing

Until the firmware starts, GPIO25, 26 and 27 float, and uploading or resetting resets them. Keep the 10 kΩ pull-down on EN (GPIO27) fitted, and keep the motor supply off while you flash a board with a driver attached ([docs/hardware/wiring.md](docs/hardware/wiring.md)).

## NVS Storage Details

- **Location:** separate NVS partition (20KB at 0x9000)
- **Namespace:** "roofroof"
- **Key:** "config"
- **Survives:** firmware uploads, filesystem uploads, power cycles
- **Cleared by:** full chip erase only

To manually clear config (force the hotspot):
```bash
pio run --target erase
pio run --target upload
pio run --target uploadfs
```

## WebSocket in Dev Mode

`vite.config.ts` has the WebSocket proxy configuration:

```typescript
{
  target: `ws://${ESP32_IP}`,
  ws: true,
  changeOrigin: true,
}
```

Set the `ESP32_IP` environment variable or update it in `vite.config.ts`.

## Docs and demo

There is no docs or demo host. CI builds both on every pull request and on `main` and uploads them as the `site` artifact (the demo is under `demo/`). To run the demo locally, see [docs/live-demo.md](docs/live-demo.md).
