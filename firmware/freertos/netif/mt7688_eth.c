/*
 * mt7688_eth.c — MT7688 RAETH stub Ethernet netif for lwIP.
 *
 * This file registers a network interface with lwIP.  The actual MT7688
 * RAETH Ethernet controller driver (frame DMA, MAC programming, etc.) is
 * left as a TODO — this stub is sufficient for the firmware to compile,
 * link, and start all service threads.
 *
 * Hardware
 * ────────
 * The MT7688 integrates a MIPS24KEc core with a built-in fast Ethernet
 * switch (RAETH) at KSEG1 base 0xB0100000.  Implementing a full driver
 * requires:
 *   - MAC/MII initialisation
 *   - DMA ring-buffer setup (TX/RX descriptors at a known RAM address)
 *   - Interrupt handler for DMA completion
 * None of that is implemented here — the netif output function simply
 * drops packets until the driver is written.
 *
 * To implement the full driver, replace the stub functions below with real
 * register accesses following the MT7688 Datasheet §12 (RAETH).
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"
#include "netif/etharp.h"
#include "mt7688_eth.h"

#include <string.h>

/* Stub MAC address: locally administered, unicast. */
static const uint8_t mt7688_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

/* ── Stub output function ────────────────────────────────────────────────── */
static err_t mt7688_eth_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    (void)p;
    /*
     * TODO: copy pbuf chain to TX DMA descriptor and trigger transmission.
     * For now, silently discard the packet.
     */
    return ERR_OK;
}

/* ── lwIP netif initialisation callback ─────────────────────────────────── */
err_t mt7688_eth_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = 't';

    /* Copy stub MAC address. */
    SMEMCPY(netif->hwaddr, mt7688_mac, 6);
    netif->hwaddr_len = 6;

    /* Maximum transmission unit: standard Ethernet. */
    netif->mtu = 1500;

    /* Ethernet + broadcast + multicast capabilities. */
    netif->flags = NETIF_FLAG_BROADCAST
                 | NETIF_FLAG_ETHARP
                 | NETIF_FLAG_IGMP;

    /* Use the ARP/Ethernet output function. */
    netif->output     = etharp_output;
    netif->linkoutput = mt7688_eth_output;

    /*
     * Mark the link as UP so that DHCP and the service threads can start.
     * WARNING: This is a STUB — no real Ethernet frames are sent or received
     * until the MT7688 RAETH hardware driver is implemented.  All outgoing
     * packets are silently dropped by mt7688_eth_output().
     */
    netif_set_link_up(netif);

    return ERR_OK;
}
