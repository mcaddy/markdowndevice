/*
 * TinyUSB configuration for the Tiny2040 markdown device.
 *
 * We present a dual-configuration RNDIS + CDC-ECM network device:
 *   - Windows selects RNDIS
 *   - macOS / Linux / (most) Android select CDC-ECM
 * This is the broadest "just works" combination for a USB-Ethernet gadget.
 * (NCM is the modern alternative but needs a manual driver pick on Windows.)
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// CFG_TUSB_MCU is provided by the pico-sdk TinyUSB integration (OPT_MCU_RP2040).
#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined (expected from pico-sdk)
#endif

#ifndef CFG_TUSB_OS
  #define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
  #define CFG_TUSB_DEBUG 0
#endif

// RHPort 0, full-speed (RP2040 USB is full-speed only).
#define BOARD_TUD_RHPORT      0
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
  #define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
  #define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
  #define CFG_TUD_ENDPOINT0_SIZE 64
#endif

//------------- Network class -------------//
// The net class ships two drivers: ECM/RNDIS and NCM. Enable exactly one.
// We want RNDIS + CDC-ECM.
#define CFG_TUD_ECM_RNDIS     1
#define CFG_TUD_NCM           0

// Ethernet MTU. 1514 = 1500 payload + 14 header. Matches lwIP's netif MTU.
#define CFG_TUD_NET_MTU       1514

// No other device classes.
#define CFG_TUD_CDC           0
#define CFG_TUD_MSC           0
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
