---
description: "Flash RoofRoof firmware onto an ESP32 over USB with esptool: the release files and the flash memory map."
---

# Flashing Your Device

Flash your ESP32 over USB once; after that it can be updated over WiFi by uploading a file on its Updates page ([Updates](../user-guide/ota.md)). RoofRoof has no browser flasher: use `esptool` from a computer with a USB data cable.

The RoofRoof repository is private, so the files come from the repository's GitHub release assets or from a CI run's artifacts (you need access to the repository), or from your own build ([BUILD.md](https://github.com/DeanJ87/RoofRoof/blob/main/BUILD.md)).

---

## What's in a Release

| File | What it's for |
|------|---------------|
| `roofroof-l2-usb-vX.Y.Z.zip` | **USB flash**: every part plus the `esptool` command |
| `roofroof-l2-firmware-vX.Y.Z.bin` | Firmware, for an upload on the Updates page |
| `roofroof-l2-littlefs-vX.Y.Z.bin` | The web UI, for an upload on the Updates page |
| `roofroof-openapi-vX.Y.Z.json` | The REST API description for this release |

`l2` is the partition layout the files are for. The device refuses an update file made for a different layout or another product.

---

## Flash with esptool

Install `esptool`:

```bash
pip install esptool
```

Unzip the USB package and, from that folder:

```bash
esptool.py --chip esp32 --port PORT --baud 460800 write_flash \
  0x1000 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin \
  0x10000 firmware.bin 0x390000 littlefs.bin
```

The same command is in the package's `FLASH.txt`. Replace `PORT` with your serial port:

=== "macOS / Linux"
    ```
    /dev/cu.usbserial-*   # macOS
    /dev/ttyUSB0          # Linux
    ```

=== "Windows"
    ```
    COM3
    ```

!!! tip "Finding your port"
    ```bash
    esptool.py --chip esp32 chip_id
    ```
    esptool will scan and print the detected port.

!!! note "Settings are kept"
    Nothing is written between `0x9000` and `0xDFFF`, where the device keeps its settings and WiFi
    credentials. Don't run `erase_flash` unless you want to start again from the setup hotspot.

!!! warning "Wire the EN pull-down first"
    Before the roof's motor driver is connected, fit the 10 kΩ pull-down on its enable pin ([Wiring](../hardware/wiring.md#the-en-pull-down)). Until the firmware starts, the driver's pins float.

---

## Flash Memory Map

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | `0x9000` | 20 KB | Settings, WiFi credentials |
| otadata | `0xE000` | 8 KB | Which app slot boots |
| app0 | `0x10000` | 1.75 MB | Firmware |
| app1 | `0x1D0000` | 1.75 MB | The other firmware slot (updates) |
| spiffs | `0x390000` | 448 KB | Web UI (LittleFS) |
