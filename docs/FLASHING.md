# Flashing the Tiny2040

The build produces `build/markdowndevice.uf2`.

## Put the board in BOOTSEL mode

1. Unplug the Tiny2040.
2. Hold down the **BOOT** button (the one nearest the USB connector).
3. While holding BOOT, plug the board into your computer via USB.
4. Release BOOT.

The board appears as a USB mass-storage drive named **RPI-RP2**.

## Copy the firmware

Drag `build/markdowndevice.uf2` onto the `RPI-RP2` drive (or copy it there).
The board reboots automatically and starts running the firmware.

```sh
# macOS / Linux example
cp build/markdowndevice.uf2 /Volumes/RPI-RP2/     # macOS
cp build/markdowndevice.uf2 /media/$USER/RPI-RP2/ # Linux
```

On Windows, copy it to the `RPI-RP2` drive in Explorer.

## After flashing

The board no longer shows up as a drive — it now enumerates as a **USB
network adapter**. See [HOST-SETUP.md](HOST-SETUP.md) for connecting from each
kind of host, then browse to <http://192.168.7.1/> (or <http://md.edit/>).

## Reset / recovery

- To re-flash, repeat the BOOTSEL steps above.
- The document filesystem lives in the **top 1 MB of flash** and is *not*
  erased by a normal UF2 flash of firmware of the usual size. A full-chip
  erase (e.g. `flash_nuke.uf2`) will wipe stored documents.

## Serial debug (optional)

Debug logs are printed over **UART** on GP0 (TX) / GP1 (RX) at 115200 baud
(USB is taken by the network class, so it can't also be a serial port). Attach
a 3.3 V USB-serial adapter to view boot messages.
