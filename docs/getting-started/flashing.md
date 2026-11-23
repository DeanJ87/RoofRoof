---
description: "Flash RoofRoof firmware onto an ESP32 DevKit over USB, or a WT32-ETH01 over a USB-serial adapter, with esptool or PlatformIO: the release files, the first flash of a WT32-ETH01 and the flash memory map."
---

# Flashing Your Device

Flash your ESP32 over USB once; after that it can be updated over the network (WiFi or, on a WT32-ETH01, Ethernet) by uploading a file on its Updates page ([Updates](../user-guide/ota.md)). RoofRoof has no browser flasher: use `esptool` from a computer with a USB data cable, or a USB-serial adapter for a [WT32-ETH01](#wt32-eth01-the-first-flash).

The files come from the repository's GitHub release assets (anyone can download them) or from a CI run's artifacts (you need a GitHub login), or from your own build ([BUILD.md](https://github.com/DeanJ87/RoofRoof/blob/main/BUILD.md)).

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

!!! note "A board that ran SQMeter starts as new"
    RoofRoof keeps its settings under its own NVS namespace (`roofroof`), so the settings, WiFi
    credentials and alert state SQMeter left on the board are not read, and RoofRoof starts from the
    setup hotspot like a new device. They are not erased, so the board can go back to SQMeter. A
    settings backup is not interchangeable either: RoofRoof's backups use their own file key
    (`roofroofBackup`) and encryption derivation context, so a backup made by SQMeter is refused.
    An SQMeter firmware image is refused by the image marker, and the other way round.

!!! warning "Wire the EN pull-down first"
    Before the roof's motor driver is connected, fit the 10 kΩ pull-down on its enable pin ([Wiring](../hardware/wiring.md#the-en-pull-down)). Until the firmware starts, the driver's pins float. The WT32 carrier has it on the board.

---

## WT32-ETH01: the first flash

The same firmware image runs on an ESP32 DevKit and on a WT32-ETH01: there is no separate build, and the release files above are the ones to use. Which board it is, and with it the pins and Ethernet, is a setting chosen after the first start ([Pins](../user-guide/pins.md)).

A DevKit has a USB port. The WT32-ETH01 has none, so its **first** flash goes through a USB-serial adapter:

1. Use a **3.3 V** USB-serial adapter (a 5 V signal level can damage the ESP32). Connect the adapter's GND to the WT32's GND, the adapter's TX to the WT32's **RX0** (IO3) and the adapter's RX to its **TX0** (IO1).
2. Power the WT32 as the carrier needs: from the carrier's own supply, or from the adapter's 5 V or 3.3 V pin. Keep the 12 V motor supply off.
3. Hold **IO0 to GND** while the board comes out of reset: close the carrier's IO0 jumper, then power-cycle the board. The ESP32 starts in its download mode instead of the firmware. (A plain adapter can't reset the board by itself, so the power-cycle is what starts it.)
4. Flash with `esptool` as [above](#flash-with-esptool), or from a checkout ([BUILD.md](https://github.com/DeanJ87/RoofRoof/blob/main/BUILD.md)):

    ```bash
    ~/.platformio/penv/bin/pio run -e esp32dev -t upload --upload-port PORT     # the firmware
    ~/.platformio/penv/bin/pio run -e esp32dev -t uploadfs --upload-port PORT   # the web UI
    ```

    Power-cycle the board with the jumper still closed before the second command, so it is in its download mode again.
5. Open the IO0 jumper and power-cycle the board. It starts the firmware.

With a cable in, a new WT32-ETH01 finds its own Ethernet chip, takes an address by DHCP and is reached at `roofroof.local` or that address; the setup wizard then asks for the board, with the WT32 carrier preselected ([Network](../user-guide/network.md), [First Boot](first-setup.md#ethernet-on-the-wt32-eth01)).

**After that, update over the network**: the **Updates** page or command-line OTA ([Updates](../user-guide/ota.md)), over Ethernet exactly as over WiFi. The adapter is only needed again to recover a board you can't reach.

!!! warning "IO0 is also the Ethernet clock"
    On the WT32-ETH01, IO0 carries the Ethernet chip's 50 MHz clock into the ESP32. Leave the jumper open in normal use: held to GND it stops the board starting the firmware, and it stops Ethernet.

---

## Flash Memory Map

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | `0x9000` | 20 KB | Settings, WiFi credentials |
| otadata | `0xE000` | 8 KB | Which app slot boots |
| app0 | `0x10000` | 1.75 MB | Firmware |
| app1 | `0x1D0000` | 1.75 MB | The other firmware slot (updates) |
| spiffs | `0x390000` | 448 KB | Web UI (LittleFS) |
