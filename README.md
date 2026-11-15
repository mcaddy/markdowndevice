# markdowndevice

Firmware for a **Pimoroni Tiny2040** (RP2040) that turns the board into a
plug-in **markdown editor**: it appears to a host computer as a **USB-Ethernet
adapter**, runs its own **DHCP + DNS + HTTP server** on that link, and serves a
browser-based editor where you can **upload, edit, preview, download and
delete** markdown documents. Documents persist in flash (LittleFS).

No app to install on the host — just plug in and open a browser.

```
 ┌────────────┐   USB    ┌──────────────────────────────────────┐
 │   Host PC  │◀────────▶│ Tiny2040 @ 192.168.7.1                │
 │ (browser)  │  Ethernet│  DHCP → 192.168.7.2                   │
 └────────────┘  gadget  │  DNS  → md.edit                       │
                         │  HTTP → markdown editor + LittleFS    │
                          └──────────────────────────────────────┘
```

## ⚠️ Which hosts work

USB-Ethernet requires the *host* to run USB in host mode with a browser. That
means **PCs (Windows/macOS/Linux)** and **most Android phones (via OTG)** work;
**Kindles and reMarkables do not** over USB (see
[docs/HOST-SETUP.md](docs/HOST-SETUP.md)). A Wi-Fi access-point variant for
those e-readers is planned as separate firmware.

## Layout

| Path | Purpose |
|---|---|
| `src/main.c` | USB net glue, lwIP netif @ 192.168.7.1, DHCP/DNS/HTTP bring-up, main loop |
| `src/usb_descriptors.c` | RNDIS + CDC-ECM dual-config descriptors |
| `src/tusb_config.h`, `src/lwipopts.h` | TinyUSB / lwIP configuration |
| `src/http/http_server.c` | Raw-TCP HTTP/1.1 server: home, editor, list, upload, save, download, delete |
| `src/fs/lfs_flash.c` | LittleFS block device on flash (XIP-safe via `flash_safe_execute`) |
| `src/fs/docstore.c` | Document CRUD over LittleFS |
| `web/` | `style.css`, `md.js` (ES5 markdown renderer), `app.js` (live preview) — embedded into flash at build |
| `tools/embed_web.py` | Bundles `web/` into `src/web_assets.h` |
| `docs/` | Flashing and per-host setup guides |

The home page and editor are **server-rendered HTML**, so upload / edit / save /
download work even with JavaScript disabled (important for e-reader browsers).
`app.js` + `md.js` add a live preview pane when JS is available.

## Build

Prerequisites: CMake, Ninja, Python 3, an Arm GNU toolchain, and the pico-sdk.
See [docs/BUILD.md](docs/BUILD.md) for a from-scratch setup on Windows.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export PICO_TOOLCHAIN_PATH=/path/to/arm-none-eabi/bin
cmake -G Ninja -B build -DPICO_BOARD=pimoroni_tiny2040
cmake --build build
```

Output: `build/markdowndevice.uf2`. Flash it per
[docs/FLASHING.md](docs/FLASHING.md).

## Configuration knobs

- **Flash filesystem size** — `LFS_FLASH_FS_SIZE` in `src/fs/lfs_flash.h`
  (default 1 MB, carved from the top of the 8 MB flash). Reduce it for the
  2 MB Tiny2040 variant.
- **Document size cap** — `DOCSTORE_MAX_DOC_SIZE` in `src/fs/docstore.h`.
- **USB class / Android MAC tweak** — see `src/tusb_config.h` and the
  `tud_network_mac_address` note in `src/main.c`.

## Notes / limitations

- Full-speed USB only (RP2040), which is ample for editing text.
- Flash writes briefly (a few ms) pause USB servicing; the host TCP stack
  retransmits, so transfers are unaffected. Writes only happen from the main
  loop, never an interrupt.
- Vendored third-party code: `src/littlefs/` (LittleFS). lwIP and TinyUSB come
  from the pico-sdk.
