# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Tiny2040 USB-Ethernet Markdown Editor
#
# Presents a USB network adapter (RNDIS + CDC-ECM), runs DHCP + DNS + a small
# HTTP server on the link, and serves a markdown editor backed by LittleFS.
#
# We compile lwIP core + TinyUSB's networking helpers directly into the
# executable (the proven RP2040 USB-net pattern) rather than relying on
# pico_lwip, so lwipopts.h / tusb_config.h from src/ are picked up cleanly.
# ---------------------------------------------------------------------------

include(pico_sdk_import.cmake)

project(markdowndevice C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(TINYUSB  ${PICO_SDK_PATH}/lib/tinyusb)
set(LWIP     ${PICO_SDK_PATH}/lib/lwip)
set(LITTLEFS ${CMAKE_CURRENT_LIST_DIR}/src/littlefs)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- Generate the embedded web assets header at configure/build time -------
set(WEB_ASSETS_H ${CMAKE_CURRENT_LIST_DIR}/src/web_assets.h)
file(GLOB WEB_SOURCES ${CMAKE_CURRENT_LIST_DIR}/web/*)
add_custom_command(
  OUTPUT  ${WEB_ASSETS_H}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/embed_web.py
          ${CMAKE_CURRENT_LIST_DIR}/web ${WEB_ASSETS_H}
  DEPENDS ${WEB_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/tools/embed_web.py
  COMMENT "Embedding web/ assets into web_assets.h"
  VERBATIM)
add_custom_target(web_assets DEPENDS ${WEB_ASSETS_H})

add_executable(markdowndevice
  src/main.c
  src/usb_descriptors.c
  src/http/http_server.c
  src/fs/lfs_flash.c
  src/fs/docstore.c

  # TinyUSB networking helpers (DHCP server, DNS server, RNDIS glue)
  ${TINYUSB}/lib/networking/dhserver.c
  ${TINYUSB}/lib/networking/dnserver.c
  ${TINYUSB}/lib/networking/rndis_reports.c

  # LittleFS
  ${LITTLEFS}/lfs.c
  ${LITTLEFS}/lfs_util.c

  # lwIP core (NO_SYS raw API) — mirrors the TinyUSB net_lwip_webserver example
  ${LWIP}/src/core/def.c
  ${LWIP}/src/core/dns.c
  ${LWIP}/src/core/inet_chksum.c
  ${LWIP}/src/core/init.c
  ${LWIP}/src/core/ip.c
  ${LWIP}/src/core/mem.c
  ${LWIP}/src/core/memp.c
  ${LWIP}/src/core/netif.c
  ${LWIP}/src/core/pbuf.c
  ${LWIP}/src/core/raw.c
  ${LWIP}/src/core/stats.c
  ${LWIP}/src/core/sys.c
  ${LWIP}/src/core/tcp.c
  ${LWIP}/src/core/tcp_in.c
  ${LWIP}/src/core/tcp_out.c
  ${LWIP}/src/core/timeouts.c
  ${LWIP}/src/core/udp.c
  ${LWIP}/src/core/ipv4/autoip.c
  ${LWIP}/src/core/ipv4/dhcp.c
  ${LWIP}/src/core/ipv4/etharp.c
  ${LWIP}/src/core/ipv4/icmp.c
  ${LWIP}/src/core/ipv4/igmp.c
  ${LWIP}/src/core/ipv4/ip4.c
  ${LWIP}/src/core/ipv4/ip4_addr.c
  ${LWIP}/src/core/ipv4/ip4_frag.c
  ${LWIP}/src/netif/ethernet.c
  )

add_dependencies(markdowndevice web_assets)

target_include_directories(markdowndevice PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/src/http
  ${CMAKE_CURRENT_LIST_DIR}/src/fs
  ${LITTLEFS}
  ${TINYUSB}/lib/networking
  ${LWIP}/src/include
  ${LWIP}/src/include/ipv4
  # arch/cc.h for lwIP is shipped inside pico_lwip
  ${PICO_SDK_PATH}/src/rp2_common/pico_lwip/include
  )

# LittleFS: use the C stdlib malloc/free provided by the SDK; keep asserts light.
target_compile_definitions(markdowndevice PRIVATE
  LFS_NO_DEBUG
  LFS_NO_WARN
  LFS_NO_ERROR
  PICO_FLASH_ASSUME_CORE1_SAFE=1   # single-core: flash_safe_execute needs no core1 lockout
  )

target_link_libraries(markdowndevice PRIVATE
  pico_stdlib
  tinyusb_device
  hardware_flash
  hardware_sync
  pico_unique_id
  pico_flash
  pico_rand
  )

# lwIP / TinyUSB networking sources trip -Wconversion etc.; don't fail on them.
set_source_files_properties(
  ${TINYUSB}/lib/networking/dhserver.c
  ${TINYUSB}/lib/networking/dnserver.c
  ${TINYUSB}/lib/networking/rndis_reports.c
  PROPERTIES COMPILE_FLAGS "-Wno-error -w")

# stdio over UART (GP0/GP1) for debug; USB is taken by the network class.
pico_enable_stdio_uart(markdowndevice 1)
pico_enable_stdio_usb(markdowndevice 0)

pico_add_extra_outputs(markdowndevice)   # .uf2, .hex, .bin, .map, .dis
