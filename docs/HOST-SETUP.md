# Connecting to the device from each host

The device presents itself as a **USB-Ethernet adapter** with two
configurations:

- **RNDIS** — used automatically by **Windows**
- **CDC-ECM** — used automatically by **macOS**, **Linux**, and most **Android**

Once the host brings up the interface, the device's built-in DHCP server hands
it an address in `192.168.7.x`. The device itself is always **192.168.7.1**,
and answers to the name **md.edit**.

Open **http://192.168.7.1/** (or **http://md.edit/**) in a browser.

---

## ⚠️ Important compatibility note for e-readers

USB-Ethernet requires the *reader* to act as a USB **host**. In practice:

| Device | Works over USB? | Why |
|---|---|---|
| Windows / macOS / Linux PC | ✅ Yes | Full USB host + browser |
| Android phone (with OTG) | ⚠️ Usually | Needs USB-OTG; some kernels don't auto-name the interface (see below) |
| Kindle (e-ink) | ❌ No | Cannot act as a USB host (OTG sense pin not wired) |
| reMarkable 2 | ❌ Not without hacks | Needs root to enable host mode, a powered cable, and has no stock browser |

For Kindles and reMarkables, a **Wi-Fi access-point** firmware variant (on a
Wi-Fi-capable board such as a Pico 2 W) is the right approach — this USB
firmware cannot reach them. That variant is tracked separately.

---

## Windows

Windows selects the RNDIS configuration automatically and installs its built-in
"Remote NDIS" driver. A new network adapter appears; it gets a `192.168.7.x`
lease. If it doesn't auto-install, open Device Manager, find the unknown
"RNDIS" device, **Update driver → Browse → Let me pick → Network adapters →
Microsoft → Remote NDIS Compatible Device**.

## macOS

macOS selects CDC-ECM automatically. A new interface appears in **System
Settings → Network**; it takes a DHCP lease. Browse to the address above.

## Linux

The `cdc_ether` (ECM) driver binds automatically. Check with `ip addr` for a
new `enx...`/`usb0` interface with a `192.168.7.x` address. If it doesn't get
an address automatically, run a DHCP client on it, e.g.
`sudo dhclient enxXXXXXXXXXXXX`.

## Android (with USB-OTG)

1. Connect the device through a USB-OTG adapter.
2. In most cases an **Ethernet** entry appears under Settings; the phone takes
   a `192.168.7.x` lease and you can browse to the device.
3. **If the interface never comes up**: this is the well-known Android quirk
   where a CDC-ECM interface is named `usb0` instead of `eth0` and ignored.
   Options:
   - Try a different phone (Samsung kernels tend to include ECM support).
   - As a firmware tweak, edit `tud_network_mac_address` in `src/main.c`:
     change the first byte from `0x02` to `0x00` (clearing the
     locally-administered bit) and re-flash — some kernels prefer this.

## Troubleshooting

- **Pings work but the page hangs / crashes**: usually a toolchain issue with
  older builds. This project builds against a current Arm GNU toolchain; if you
  see this, rebuild with a clean `build/` directory.
- **No lease**: confirm the host actually bound a USB-Ethernet driver (see per-
  OS notes) rather than treating the device as USB serial.
- **Can't reach md.edit but 192.168.7.1 works**: the host isn't using the
  device's DNS. Use the numeric address; it always works.
