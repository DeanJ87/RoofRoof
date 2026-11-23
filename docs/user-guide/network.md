---
description: "RoofRoof on the network: WiFi on an ESP32 DevKit, wired Ethernet on the WT32-ETH01 with WiFi as an optional fallback, the network mode (Auto, Ethernet only, WiFi only), when the setup hotspot opens, and the Ethernet status."
---

# Network: WiFi and Ethernet

How the controller reaches your network depends on its board ([Pins](pins.md)):

- **ESP32 DevKit**: WiFi, set up from the **RoofRoof-Setup** hotspot ([First Boot](../getting-started/first-setup.md)). Nothing on this page changes it.
- **WT32-ETH01** (on the RoofRoof carrier): wired **Ethernet**, with WiFi as an optional fallback. Wired Ethernet keeps the roof's controller on the network where WiFi drops out.

The same firmware runs on both. Ethernet starts only on a board that has it, so a DevKit never touches the WT32's Ethernet pins.

---

## Ethernet on the WT32-ETH01

With the WT32-ETH01 board in effect (chosen on the Pins page, or found by the probe at boot while no board is chosen yet), the device starts Ethernet at boot: the LAN8720 chip through the ESP32's own Ethernet controller, at PHY address 1. It asks for an address by DHCP under its host name (`wifi.hostname`, default `roofroof`, which also names it for mDNS), with IPv6 as well when **IPv6** is on ([IPv6](ipv6.md)). Most starts have an address within a few seconds of the cable having a link.

While Ethernet has an address it is the device's default route, so the connections the device makes itself (N.I.N.A., SQMeter, alerts, MQTT, NTP) go over the cable even when WiFi is up beside it.

**Everything works over Ethernet**, exactly as over WiFi:

- the web UI, the REST API and the live status;
- `roofroof.local` (mDNS);
- updates: the **Updates** page and command-line OTA ([Updates](ota.md));
- MQTT and Home Assistant discovery;
- ASCOM Alpaca and its discovery;
- polling SQMeter and N.I.N.A.;
- alerts (ntfy, Pushover, webhook, MQTT);
- the time from NTP.

Whatever used to wait for WiFi now waits for "a network": WiFi joined, or Ethernet with an address. The settings that need a network ([Settings dependencies](../reference/settings-dependencies.md)) count Ethernet too.

### WiFi is optional

On a board with Ethernet, empty WiFi settings are normal: the device works on the cable alone. **Settings → Network** shows the Ethernet status and says WiFi is optional, and nothing calls the empty WiFi settings an error or "not set up". Save a WiFi network there only if you want a fallback for when the cable fails.

---

## The network mode

On a board with Ethernet, **Settings → Network** has the network mode (`network.mode`). On a DevKit it isn't shown, and the device uses WiFi.

| Mode | Ethernet | WiFi | Setup hotspot |
|---|---|---|---|
| **Auto** (the default) | Starts at boot and is preferred | A saved WiFi network is joined once Ethernet has had no address for 30 s (a pulled cable, a dead switch), and stays up beside Ethernet until the next restart. Without a saved network nothing else happens | Only at boot, with no network at all ([below](#the-setup-hotspot)) |
| **Ethernet only** | Starts at boot | The radio stays off. A WiFi scan or join is refused (`409`, *"Wi-Fi is off: the network mode is Ethernet only"*) | Never |
| **WiFi only** | Not started | Exactly as on a DevKit | As on a DevKit |

The mode applies at the next restart, like the WiFi settings. A save that changes it is refused while the roof is busy (moving, a close waiting, the automatic close under way, a calibration run or a jog), as a pin change is.

Auto doesn't switch back and forth: once its WiFi fallback has joined, it stays until a restart, so clients that found the device on its WiFi address keep it. When the cable comes back, Ethernet is the default route again.

!!! warning "Ethernet only needs a working cable"
    With **Ethernet only** the hotspot never opens, so a device whose cable or switch has failed can't be reached at all. Change the mode before you move the cable to another network. Otherwise the only way back is a USB flash with the settings erased ([First Boot](../getting-started/first-setup.md#wifi-config-is-persistent)).

---

## The setup hotspot

On a DevKit, and in **WiFi only**, the hotspot follows the DevKit's rules ([First Boot](../getting-started/first-setup.md#if-the-wifi-is-down-at-startup)).

On a board with Ethernet, in **Auto**, the open **RoofRoof-Setup** hotspot opens only when no network can be reached after a start:

- no network (Ethernet or WiFi) has been joined since the start, **and**
- 45 seconds have passed since the start, **and**
- if the cable has a link, 20 seconds have passed since the link came up, so a slow DHCP server has time to answer.

A new WT32-ETH01 on a cable therefore never opens it: open `roofroof.local` or its DHCP address, and the [setup wizard](setup-wizard.md) runs in the browser over Ethernet, its WiFi question skippable (*"You're on Ethernet"*).

Once any network has been joined since the start, a later outage (a pulled cable) never opens the hotspot: Ethernet is retried, and a saved WiFi network is joined after 30 s. An open hotspot has no password, and on a running roof controller it would widen who can reach the device; a power cycle without a network brings the hotspot back. When the hotspot is open and Ethernet or WiFi comes up, the device restarts onto that network about 15 seconds later, as it does after joining a network from the hotspot.

In **Ethernet only** the hotspot never opens.

While the hotspot is open the roof can't be opened, on any board ([First Boot](../getting-started/first-setup.md#if-the-wifi-is-down-at-startup)).

---

## Ethernet in the status

The dashboard's and the **System** page's network rows show Ethernet when it is up. [`GET /api/status`](../api/rest.md#get-apistatus) and `/ws/status` carry an `ethernet` block beside `wifi`:

```json
"ethernet": { "present": true, "started": true, "phyFound": true, "link": true, "speedMbps": 100, "fullDuplex": true, "ip": "192.168.1.43", "mac": "AA:BB:CC:DD:EE:01", "ipv6": ["fe80::aabb:ccff:fedd:ee01"] }
```

`present` is false on a DevKit and in **WiFi only**; `phyFound` false means the LAN8720 didn't answer (for example a WT32 preset on a board that isn't one). The [wiring check](pins.md#the-ethernet-check) reports the link, its speed and the address too.
