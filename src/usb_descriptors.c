/*
 * USB descriptors: dual-configuration RNDIS + CDC-ECM network device.
 *
 * Adapted from TinyUSB's net_lwip_webserver example, trimmed for the RP2040
 * (full-speed only, so no high-speed/qualifier/other-speed descriptors, and
 * no NCM/BOS block). Serial number is derived from the RP2040 unique flash ID
 * instead of the TinyUSB board BSP.
 *
 * Config order matters for host driver selection:
 *   index 0 = RNDIS  -> chosen by Windows
 *   index 1 = CDC-ECM-> chosen by macOS / Linux / most Android
 */
#include "tusb.h"
#include "class/net/net_device.h"
#include "pico/unique_id.h"

/* Defined in main.c; shared with the net driver. First byte 0x02 marks the
 * address "locally administered" (helps some Android kernels name it ethN). */
extern uint8_t tud_network_mac_address[6];

#define USB_VID 0xCafe   /* TinyUSB's example VID; fine for local/hobby use */
#define USB_PID 0x4020

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_INTERFACE,
  STRID_MAC,
  STRID_COUNT
};

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

enum {
  CONFIG_ID_RNDIS = 0,
  CONFIG_ID_ECM   = 1,
  CONFIG_ID_COUNT
};

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = TUSB_CLASS_MISC,      // IAD
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,
  .iManufacturer      = STRID_MANUFACTURER,
  .iProduct           = STRID_PRODUCT,
  .iSerialNumber      = STRID_SERIAL,
  .bNumConfigurations = CONFIG_ID_COUNT,
};

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptors (full-speed)
//--------------------------------------------------------------------+
#define RNDIS_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN)
#define ECM_CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + TUD_CDC_ECM_DESC_LEN)

#define EPNUM_NET_NOTIF 0x81
#define EPNUM_NET_OUT   0x02
#define EPNUM_NET_IN    0x82

static const uint8_t rndis_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(CONFIG_ID_RNDIS + 1, ITF_NUM_TOTAL, 0, RNDIS_CONFIG_TOTAL_LEN, 0, 100),
  TUD_RNDIS_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE,
                       EPNUM_NET_NOTIF, 8, EPNUM_NET_OUT, EPNUM_NET_IN, 64),
};

static const uint8_t ecm_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(CONFIG_ID_ECM + 1, ITF_NUM_TOTAL, 0, ECM_CONFIG_TOTAL_LEN, 0, 100),
  TUD_CDC_ECM_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE, STRID_MAC,
                         EPNUM_NET_NOTIF, 64, EPNUM_NET_OUT, EPNUM_NET_IN, 64, CFG_TUD_NET_MTU),
};

static const uint8_t *const configuration_arr[CONFIG_ID_COUNT] = {
  [CONFIG_ID_RNDIS] = rndis_configuration,
  [CONFIG_ID_ECM]   = ecm_configuration,
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  if (index >= CONFIG_ID_COUNT) return NULL;
  return configuration_arr[index];
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
static const char *const string_desc_arr[STRID_COUNT] = {
  [STRID_LANGID]       = (const char[]){0x09, 0x04},   // English (0x0409)
  [STRID_MANUFACTURER] = "markdowndevice",
  [STRID_PRODUCT]      = "Markdown Editor (USB Ethernet)",
  [STRID_SERIAL]       = NULL,                          // built from unique ID
  [STRID_INTERFACE]    = "Markdown Net Interface",
  [STRID_MAC]          = NULL,                          // built from MAC
};

static uint16_t _desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  unsigned chr_count = 0;

  switch (index) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[STRID_LANGID], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL: {
      pico_unique_board_id_t id;
      pico_get_unique_board_id(&id);
      for (unsigned i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        _desc_str[1 + chr_count++] = "0123456789ABCDEF"[(id.id[i] >> 4) & 0xF];
        _desc_str[1 + chr_count++] = "0123456789ABCDEF"[(id.id[i] >> 0) & 0xF];
      }
      break;
    }

    case STRID_MAC:
      for (unsigned i = 0; i < 6; i++) {
        _desc_str[1 + chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xF];
        _desc_str[1 + chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 0) & 0xF];
      }
      break;

    default: {
      if (index >= STRID_COUNT || string_desc_arr[index] == NULL) return NULL;
      const char *str = string_desc_arr[index];
      chr_count = (unsigned) strlen(str);
      const unsigned max_count = (sizeof(_desc_str) / sizeof(_desc_str[0])) - 1;
      if (chr_count > max_count) chr_count = max_count;
      for (unsigned i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
      break;
    }
  }

  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
