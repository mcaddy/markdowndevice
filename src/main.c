/*
 * Tiny2040 USB-Ethernet Markdown Editor — entry point.
 *
 * Brings up the USB network device, an lwIP netif at 192.168.7.1, a DHCP
 * server (leases 192.168.7.2), a tiny DNS responder (md.edit -> 192.168.7.1),
 * and our HTTP server. The main loop services TinyUSB and lwIP timeouts.
 *
 * USB glue (tud_network_*) follows TinyUSB's net_lwip_webserver example.
 */
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "tusb.h"
#include "class/net/net_device.h"

#include "dhserver.h"
#include "dnserver.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"

#include "fs/docstore.h"
#include "http/http_server.h"

#define INIT_IP4(a, b, c, d)  { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

/* lwIP netif for the USB link. */
static struct netif netif_data;

/* MAC address advertised over CDC-ECM / used by the RNDIS glue.
 * First byte 0x02 => locally administered. If a particular Android phone
 * refuses to bring the interface up as ethN, try clearing bit 1 (0x02 -> 0x00). */
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

/* Network parameters of this device. */
static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* DHCP pool. Must be in RAM (server stamps in the client MAC). */
static dhcp_entry_t dhcp_entries[] = {
  { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 },
  { {0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60 },
  { {0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config = {
  .router = INIT_IP4(0, 0, 0, 0),
  .port   = 67,
  .dns    = INIT_IP4(192, 168, 7, 1),
  .domain = "usb",
  .num_entry = TU_ARRAY_SIZE(dhcp_entries),
  .entries   = dhcp_entries,
};

//--------------------------------------------------------------------+
// lwIP <-> TinyUSB network glue
//--------------------------------------------------------------------+
static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
  (void) netif;
  for (;;) {
    if (!tud_ready()) return ERR_USE;
    if (tud_network_can_xmit(p->tot_len)) {
      tud_network_xmit(p, 0);
      return ERR_OK;
    }
    tud_task();   /* let TinyUSB drain the previous frame */
  }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
  return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif) {
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu        = CFG_TUD_NET_MTU;
  netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state      = NULL;
  netif->name[0]    = 'u';
  netif->name[1]    = '0';
  netif->linkoutput = linkoutput_fn;
  netif->output     = ip4_output_fn;
  return ERR_OK;
}

static void init_lwip(void) {
  struct netif *netif = &netif_data;
  lwip_init();

  /* lwIP's MAC must differ from the host's; toggle the LSbit of the last byte. */
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ethernet_input);
  netif_set_default(netif);
  netif_set_up(netif);
}

/* DNS: resolve our hostname (and any *.usb) to ourselves for a friendly URL. */
static bool dns_query_proc(const char *name, ip4_addr_t *addr) {
  if (strcmp(name, "md.edit") == 0 || strcmp(name, "markdown.usb") == 0) {
    *addr = ipaddr;
    return true;
  }
  return false;
}

/* Called by TinyUSB when a frame arrives from the host. */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  if (size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p == NULL) return false;
    pbuf_take(p, src, size);
    if (netif_data.input(p, &netif_data) != ERR_OK) {
      pbuf_free(p);
    }
    tud_network_recv_renew();
  }
  return true;
}

/* Called by TinyUSB to copy a queued frame out to the host. */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void) arg;
  struct pbuf *p = (struct pbuf *) ref;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

/* Invoked once the network interface is set up. Nothing extra needed. */
void tud_network_init_cb(void) {
}

//--------------------------------------------------------------------+
// Status LED (Tiny2040 green LED, active-low)
//--------------------------------------------------------------------+
static void led_init(void) {
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

static void led_set(bool on) {
#if PICO_DEFAULT_LED_PIN_INVERTED
  gpio_put(PICO_DEFAULT_LED_PIN, !on);
#else
  gpio_put(PICO_DEFAULT_LED_PIN, on);
#endif
}

static void led_task(void) {
  static uint32_t last = 0;
  static bool state = false;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint32_t interval = tud_mounted() ? 1000 : 250;   /* slow when mounted */
  if (now - last < interval) return;
  last = now;
  state = !state;
  led_set(state);
}

//--------------------------------------------------------------------+
// main
//--------------------------------------------------------------------+
int main(void) {
  stdio_init_all();
  led_init();

  printf("\nmarkdowndevice booting...\n");

  /* Mount (or format) the LittleFS document store before serving. */
  if (docstore_init() != 0) {
    printf("WARN: docstore init failed; storage may be unavailable\n");
  }

  /* Init TinyUSB device stack on RHPort 0. */
  tud_init(0);

  /* Bring up lwIP + servers. */
  init_lwip();
  while (dhserv_init(&dhcp_config) != ERR_OK) { }
  while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK) { }
  http_server_init();

  printf("USB RNDIS/ECM network up at 192.168.7.1 (try http://md.edit/ or http://192.168.7.1/)\n");

  while (1) {
    tud_task();            /* TinyUSB device task */
    sys_check_timeouts();  /* lwIP timers */
    docstore_task();       /* flush any deferred flash writes safely */
    led_task();
  }
}

//--------------------------------------------------------------------+
// TinyUSB device callbacks
//--------------------------------------------------------------------+
void tud_mount_cb(void)   { printf("USB mounted\n"); }
void tud_umount_cb(void)  { printf("USB unmounted\n"); }
void tud_suspend_cb(bool remote_wakeup_en) { (void) remote_wakeup_en; }
void tud_resume_cb(void)  { }

//--------------------------------------------------------------------+
// lwIP NO_SYS support hooks
//--------------------------------------------------------------------+
sys_prot_t sys_arch_protect(void)        { return 0; }
void       sys_arch_unprotect(sys_prot_t pval) { (void) pval; }
uint32_t   sys_now(void) { return to_ms_since_boot(get_absolute_time()); }
