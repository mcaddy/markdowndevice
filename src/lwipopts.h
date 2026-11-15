/*
 * lwIP options for the Tiny2040 markdown device.
 *
 * NO_SYS=1 (raw/callback API, no threads). We run a small custom HTTP server
 * on the raw TCP API (see http/http_server.c) rather than lwIP's httpd, so we
 * don't need the httpd/fsdata machinery here.
 *
 * Single core, single netif. sys_now()/sys_arch_protect() live in main.c.
 */
#ifndef LWIPOPTS_H_
#define LWIPOPTS_H_

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define MEM_ALIGNMENT                   4

#define LWIP_RAW                        0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ICMP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DNS                        0   /* we run our own tiny DNS responder */
#define LWIP_DHCP                       0   /* we are the DHCP *server*, not client */

/* We are the DHCP server: accept UDP destined to port 67. */
#define LWIP_IP_ACCEPT_UDP_PORT(p)      ((p) == PP_NTOHS(67))

#define ETH_PAD_SIZE                    0
#define LWIP_SINGLE_NETIF               1
#define LWIP_NETIF_LINK_CALLBACK        0
#define LWIP_NETIF_STATUS_CALLBACK      0
#define ETHARP_SUPPORT_STATIC_ENTRIES   1

/* Memory sizing — the RP2040 has 264 KB SRAM, so we can be generous. */
#define MEM_SIZE                        (16 * 1024)
#define TCP_MSS                         (1500 - 40)   /* iphdr + tcphdr */
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_WND                         (4 * TCP_MSS)
#define MEMP_NUM_TCP_SEG                16
#define PBUF_POOL_SIZE                  8
#define MEMP_NUM_TCP_PCB                6
#define MEMP_NUM_UDP_PCB                4

/* No OS thread protection needed with NO_SYS + single core. */
#define LWIP_MULTICAST_PING             0
#define LWIP_BROADCAST_PING             0

/* Trim unused features to save flash/RAM. */
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0
#define LWIP_NETIF_HOSTNAME             1

/* Checksums done in software. */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1

#endif /* LWIPOPTS_H_ */
