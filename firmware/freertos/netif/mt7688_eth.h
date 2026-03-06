/*
 * mt7688_eth.h — MT7688 RAETH stub Ethernet netif header.
 */

#ifndef MT7688_ETH_H
#define MT7688_ETH_H

#include "lwip/netif.h"

/*
 * mt7688_eth_init() — register the MT7688 Ethernet interface with lwIP.
 *
 * This is a stub implementation.  It initialises a netif with a static
 * IP address so that the firmware compiles and links, and the service threads
 * start up on the UART console.
 *
 * NOTE: A full Ethernet driver must implement the MT7688 RAETH (Ralink/
 * MediaTek Ethernet) controller to send/receive actual frames.  The RAETH
 * base address is 0xB0100000 (KSEG1).
 */
err_t mt7688_eth_init(struct netif *netif);

/* Network configuration — update to match your network.
 * These are used only when LWIP_DHCP=0. */
#define MT7688_IP_ADDR0     192
#define MT7688_IP_ADDR1     168
#define MT7688_IP_ADDR2       1
#define MT7688_IP_ADDR3     100

#define MT7688_NETMASK0     255
#define MT7688_NETMASK1     255
#define MT7688_NETMASK2     255
#define MT7688_NETMASK3       0

#define MT7688_GW_ADDR0     192
#define MT7688_GW_ADDR1     168
#define MT7688_GW_ADDR2       1
#define MT7688_GW_ADDR3       1

#endif /* MT7688_ETH_H */
