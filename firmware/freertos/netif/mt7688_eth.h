/*
 * mt7688_eth.h — MT7688 RAETH PDMA Ethernet driver header.
 */

#ifndef MT7688_ETH_H
#define MT7688_ETH_H

#include "lwip/netif.h"

/*
 * mt7688_eth_init() — register the MT7688 Ethernet interface with lwIP.
 *
 * Initialises the PDMA engine, TX/RX descriptor rings, and the MAC address
 * from the GDMA1 hardware registers (programmed by U-Boot from OTP/NVRAM).
 * Marks the link as UP immediately — the MT7688 internal switch is already
 * configured by U-Boot at firmware hand-off.
 *
 * Called indirectly via netif_add(); pass as the 'init' callback.
 */
err_t mt7688_eth_init(struct netif *netif);

/*
 * mt7688_eth_rx_poll() — process received frames.
 *
 * Walks the RX descriptor ring, forwards any completed frames to lwIP via
 * netif->input(), and returns free descriptors to the DMA engine.  Should
 * be called periodically (every 1–5 ms) from a dedicated polling task
 * (eth_rx_thread in main.c).
 */
void mt7688_eth_rx_poll(void);

/*
 * Network configuration — static fallback used when no DHCP server responds.
 *
 * lwIP's dhcp_start() clears the interface address to 0.0.0.0 while a lease
 * is being negotiated.  If no DHCP server exists (e.g. the device is plugged
 * directly into a laptop for recovery), the dhcp_fallback_thread in main.c
 * waits DHCP_FALLBACK_TIMEOUT_MS and then restores these values.
 *
 * Address assignment:
 *   192.168.0.1   — this device (firmware fallback / U-Boot recovery)
 *   192.168.0.100 — the directly-connected host (laptop / MacBook)
 *
 * To reach the device after a direct connection:
 *   1. Set the laptop's Ethernet adapter to a manual IP of 192.168.0.100,
 *      subnet mask 255.255.255.0, no default gateway.
 *   2. Wait ~30 seconds for DHCP to time out and the fallback to apply.
 *   3. ping 192.168.0.1  /  http://192.168.0.1/
 */
#define MT7688_IP_ADDR0     192
#define MT7688_IP_ADDR1     168
#define MT7688_IP_ADDR2       0
#define MT7688_IP_ADDR3       1

#define MT7688_NETMASK0     255
#define MT7688_NETMASK1     255
#define MT7688_NETMASK2     255
#define MT7688_NETMASK3       0

#define MT7688_GW_ADDR0     192
#define MT7688_GW_ADDR1     168
#define MT7688_GW_ADDR2       0
#define MT7688_GW_ADDR3       1

#endif /* MT7688_ETH_H */
