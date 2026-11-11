---
description: "Flash RoofRoof over USB with the release's USB package and esptool, keeping the device's settings."
---

# USB flash

The USB package flashes every part of the firmware at once: the bootloader, the partition table, the firmware and the web UI. Use it for a new device, or to recover one you can't reach over WiFi. A partition layout can only be changed over USB, because an upload can't rewrite the partition table.

**Your settings are kept**, WiFi included. They live in a part of the flash the package doesn't write (`0x9000` to `0xDFFF`).

You need a computer with Python (for `esptool`) and a USB data cable. There is no browser flasher.

---

## esptool

Get `roofroof-l2-usb-<tag>.zip` (see [What's in a Release](flashing.md#whats-in-a-release)), unzip it, and from that folder:

```bash
pip install esptool
esptool.py --chip esp32 --port PORT --baud 460800 write_flash \
  0x1000 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin \
  0x10000 firmware.bin 0x390000 littlefs.bin
```

Don't add `--erase-all` and don't run `erase_flash`: both wipe your settings. See [Flashing](flashing.md) for finding the port.

!!! tip "No port to pick?"
    The ESP32 needs a USB-to-serial driver on some computers (CP210x or CH340, depending on the board),
    and some USB cables only carry power.

---

## Check it worked

Open the device's page (its address, `roofroof.local`, doesn't change):

- **System → Firmware** shows the version you flashed.
- Your settings are as you left them.
- `http://roofroof.local/api/status` shows `"layout": "l2"` and `"product": "roofroof"` under `firmware`.

If the device comes up as the **RoofRoof-Setup** hotspot instead, its settings didn't survive (for example the flash was erased): join the hotspot and set it up again ([First Boot](first-setup.md)).

---

## Why the layout is "l2"

| Partition | Size |
|---|---|
| Two firmware slots | 1.75 MB each |
| Web UI (LittleFS) | 448 KB (the web UI is stored compressed) |
| Settings (NVS) and boot data | 20 KB and 8 KB |

The two firmware slots are what let an update be written while the old firmware keeps running, and rolled back if the new one fails to start. Every release file is built for this layout; the device refuses one made for another ([Updates](../user-guide/ota.md#how-an-update-works)).
