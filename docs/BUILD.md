# Building from scratch (Windows)

You need four things: **CMake**, **Ninja**, **Python 3**, an **Arm GNU
toolchain** (`arm-none-eabi-gcc`), and a checkout of the **pico-sdk**.

## 1. Toolchain + SDK

The easiest all-in-one is the official **Raspberry Pi Pico** VS Code extension,
or the **pico-setup-windows** installer — both bundle the Arm toolchain and set
`PICO_SDK_PATH`.

To set things up by hand (no admin required):

```sh
# pico-sdk with the TinyUSB + lwIP submodules
git clone -b master --recurse-submodules --depth 1 --shallow-submodules \
    https://github.com/raspberrypi/pico-sdk.git /c/Users/you/pico-sdk

# Arm GNU toolchain (xPack portable build) — extract anywhere
#   https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases
```

CMake and Ninja: install via `winget install Kitware.CMake Ninja-build.Ninja`
(or the Pico extension provides them).

## 2. Configure + build

From the project root (Git Bash shown; adjust paths for PowerShell):

```sh
export PICO_SDK_PATH=/c/Users/you/pico-sdk
export PICO_TOOLCHAIN_PATH=/c/Users/you/xpack-arm-none-eabi-gcc-<ver>/bin

cmake -G Ninja -B build \
      -DPICO_BOARD=pimoroni_tiny2040 \
      -DPICO_SDK_PATH="$PICO_SDK_PATH" \
      -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH"

cmake --build build
```

Result: `build/markdowndevice.uf2`.

> For the 2 MB Tiny2040, also pass `-DPICO_BOARD=pimoroni_tiny2040_2mb` and
> reduce `LFS_FLASH_FS_SIZE` in `src/fs/lfs_flash.h`.

## 3. Flash

See [FLASHING.md](FLASHING.md).

## Notes

- The web assets in `web/` are bundled into `src/web_assets.h` automatically by
  `tools/embed_web.py` during the build; edit the files in `web/` and rebuild.
- lwIP and TinyUSB are compiled from the pico-sdk's bundled copies, using the
  `src/lwipopts.h` / `src/tusb_config.h` in this project.
- A clean rebuild is just `rm -rf build` then re-run the configure step.
