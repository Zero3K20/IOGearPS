/*
 * mt7688_eth.c — MT7688 RAETH PDMA Ethernet driver for lwIP.
 *
 * Implements a complete Frame Engine + PDMA Ethernet driver for the
 * MediaTek MT7688 SoC (MT7628AN-compatible) as used in the IOGear GPSU21.
 *
 * Architecture
 * ────────────
 * The MT7688 integrates a MIPS24KEc core with a built-in 5-port Fast
 * Ethernet switch connected to the CPU via the Frame Engine (FE).  The FE
 * uses a PDMA (Peripheral DMA) engine with descriptor ring-buffers for
 * TX and RX packet transfers.
 *
 * Key register blocks (KSEG1 uncached aliases):
 *   Frame Engine base:  0xB0100000
 *   PDMA registers:     0xB0100208 (GLO_CFG) … 0xB010090C
 *   TX ring 0:          0xB0100800 … 0xB010080C
 *   RX ring 0:          0xB0100900 … 0xB010090C
 *
 * Design
 * ──────
 *   - TX: single-segment descriptors, mutex-protected, polling-based
 *     (waits for prior TX to complete before submitting new frame).
 *   - RX: dedicated polling task (mt7688_eth_rx_poll called from
 *     eth_rx_thread in main.c) checks for received frames and forwards
 *     them to lwIP via netif->input() = tcpip_input().
 *   - Cache coherency: all DMA descriptor and buffer accesses go through
 *     KSEG1 (uncached) aliases of the KSEG0 storage variables; no cache
 *     flush instructions are required.
 *
 * Register offsets verified against:
 *   - MediaTek MT7628AN/MT7688 Programming Guide §12 (RAETH/PDMA)
 *   - OpenWRT Barrier Breaker RAETH driver (ralink/mtk_eth_soc.c)
 *   - U-Boot mt7628 Ethernet driver
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/etharp.h"
#include "netif/etharp.h"
#include "mt7688_eth.h"

#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

/* ── Frame Engine register base (KSEG1 uncached) ────────────────────────── */
#define FE_BASE     0xB0100000UL
#define FE_REG(off) (*(volatile uint32_t *)(FE_BASE + (uint32_t)(off)))

/* ── GDMA1 MAC address registers (CPU port; programmed by U-Boot) ────────── *
 *
 * The ZOT U-Boot bootloader reads the device MAC address from the
 * MT7688 NVRAM/OTP and programs it into the GDMA1 MAC registers before
 * handing off to firmware.  We read it back here rather than hard-coding
 * a stub address.
 *
 * GDMA1_MAC_ADRH bits[15:0]  = MAC[47:32]
 * GDMA1_MAC_ADRL bits[31:0]  = MAC[31:0]
 */
#define GDMA1_MAC_ADRH  FE_REG(0x0508)
#define GDMA1_MAC_ADRL  FE_REG(0x050C)

/* ── PDMA global and ring registers ─────────────────────────────────────── */
#define PDMA_GLO_CFG    FE_REG(0x0208)  /* PDMA global config                */
#define PDMA_RST_IDX    FE_REG(0x020C)  /* Reset ring index registers         */
#define TX_BASE_PTR0    FE_REG(0x0800)  /* TX ring 0 base physical address    */
#define TX_MAX_CNT0     FE_REG(0x0804)  /* TX ring 0 size (number of entries) */
#define TX_CTX_IDX0     FE_REG(0x0808)  /* TX ring 0: next entry CPU submits  */
#define TX_DTX_IDX0     FE_REG(0x080C)  /* TX ring 0: current DMA index (RO)  */
#define RX_BASE_PTR0    FE_REG(0x0900)  /* RX ring 0 base physical address    */
#define RX_MAX_CNT0     FE_REG(0x0904)  /* RX ring 0 size                     */
#define RX_CALC_IDX0    FE_REG(0x0908)  /* RX ring 0: last CPU-processed idx  */
#define RX_DRX_IDX0     FE_REG(0x090C)  /* RX ring 0: current DMA index (RO)  */

/* ── PDMA_GLO_CFG bit definitions ────────────────────────────────────────── */
#define PDMA_TX_DMA_EN   (1u <<  0)  /* Enable TX DMA                         */
#define PDMA_RX_DMA_EN   (1u <<  1)  /* Enable RX DMA                         */
#define PDMA_BURST_4DW   (0u <<  8)  /* Burst size = 4  double-words          */
#define PDMA_BURST_8DW   (1u <<  9)  /* Burst size = 8  double-words          */

/* ── PDMA_RST_IDX: reset individual ring index registers ─────────────────── */
#define RST_TX_IDX0      (1u <<  0)  /* Reset TX ring 0 context index          */
#define RST_RX_IDX0      (1u << 16)  /* Reset RX ring 0 calculated index       */

/* ── TX descriptor word 1 (txd1) bits ───────────────────────────────────── */
#define TXD_SDL0(len)    ((uint32_t)(len) & 0x3FFFU)  /* data length [13:0]   */
#define TXD_LAST_SEC0    (1u << 14)  /* Last (and only) segment flag           */
#define TXD_DDONE        (1u << 31)  /* 1 = CPU owns (TX done); 0 = DMA owns  */

/* ── RX descriptor word 1 (rxd1) bits ───────────────────────────────────── */
#define RXD_SDL0(rxd1)   ((uint32_t)(rxd1) & 0x3FFFU) /* received length [13:0] */
#define RXD_DONE         (1u << 31)  /* 1 = frame ready for CPU; 0 = DMA owns */

/* ── Ring and buffer dimensions ──────────────────────────────────────────── */
#define NUM_TX_DESC     16           /* TX ring entries                        */
#define NUM_RX_DESC     16           /* RX ring entries                        */
#define ETH_BUF_SIZE    1536         /* ≥ max Ethernet frame (1514 B)          */

/* ── MIPS address-space helpers ──────────────────────────────────────────── *
 *
 * KSEG0 (0x80000000–0x9FFFFFFF): cached; virtual→physical = addr & 0x1FFFFFFF
 * KSEG1 (0xA0000000–0xBFFFFFFF): uncached; virtual→physical = addr & 0x1FFFFFFF
 * Both regions map to the same physical DRAM addresses.
 *
 * We allocate ring and buffer storage as ordinary BSS variables (KSEG0).
 * For all DMA-related accesses we use KSEG1 aliases so that no cache-flush
 * instructions are needed — the DMA engine and the CPU see the same bytes.
 *
 * VIRT_TO_PHYS: strip the KSEG region bits → physical address for DMA.
 * KSEG1_ALIAS:  convert a KSEG0 pointer → uncached KSEG1 pointer.
 */
#define VIRT_TO_PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFUL)
#define KSEG1_ALIAS(p)   ((void *)((uintptr_t)(p) | 0x20000000UL))

/* ── PDMA descriptor structures ─────────────────────────────────────────── */

/* TX descriptor — 4 words (16 bytes).  Aligned to 16 bytes. */
typedef struct {
    uint32_t txd0;  /* Segment 0 buffer physical address                      */
    uint32_t txd1;  /* SDL0[13:0] = length; bit14 = LAST_SEC0; bit31 = DDONE  */
    uint32_t txd2;  /* Segment 1 buffer address (0 for single-segment frames)  */
    uint32_t txd3;  /* Segment 1 control (0)                                   */
} raeth_txd_t;

/* RX descriptor — 4 words (16 bytes).  Aligned to 16 bytes. */
typedef struct {
    uint32_t rxd0;  /* Receive buffer physical address (set by CPU)            */
    uint32_t rxd1;  /* bit31 = DONE; SDL0[13:0] = received frame length        */
    uint32_t rxd2;  /* VLAN/misc info (not used)                               */
    uint32_t rxd3;  /* Additional info (not used)                              */
} raeth_rxd_t;

/* ── Static DMA storage (allocated in BSS, accessed via KSEG1 aliases) ─── */
static raeth_txd_t  tx_ring[NUM_TX_DESC] __attribute__((aligned(16)));
static raeth_rxd_t  rx_ring[NUM_RX_DESC] __attribute__((aligned(16)));
static uint8_t      tx_bufs[NUM_TX_DESC][ETH_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t      rx_bufs[NUM_RX_DESC][ETH_BUF_SIZE] __attribute__((aligned(32)));

/* ── Driver state ────────────────────────────────────────────────────────── */
static uint32_t      tx_idx;          /* index of the next TX descriptor to use */
static uint32_t      rx_idx;          /* index of the next RX descriptor to check */
static struct netif *g_netif;         /* lwIP netif pointer (for RX task)       */
static int           g_eth_ready;     /* non-zero once hardware is initialised   */

/* Mutex protecting the TX ring from concurrent service-thread access. */
static StaticSemaphore_t tx_mutex_buf;
static SemaphoreHandle_t tx_mutex;

/* ── Convenience wrappers for uncached ring/buffer access ───────────────── */
static inline raeth_txd_t *txr(void)
{
    return (raeth_txd_t *)KSEG1_ALIAS(tx_ring);
}
static inline raeth_rxd_t *rxr(void)
{
    return (raeth_rxd_t *)KSEG1_ALIAS(rx_ring);
}
static inline uint8_t *tx_buf(uint32_t i)
{
    return (uint8_t *)KSEG1_ALIAS(tx_bufs[i]);
}
static inline uint8_t *rx_buf(uint32_t i)
{
    return (uint8_t *)KSEG1_ALIAS(rx_bufs[i]);
}

/* ── TX output function (called by lwIP) ────────────────────────────────── */
static err_t mt7688_eth_output(struct netif *netif, struct pbuf *p)
{
    raeth_txd_t *td;
    uint8_t     *dst;
    struct pbuf *q;
    uint16_t     total = 0;
    uint32_t     next_idx;

    (void)netif;

    if (!g_eth_ready)
        return ERR_IF;

    xSemaphoreTake(tx_mutex, portMAX_DELAY);

    td = &txr()[tx_idx];

    /*
     * Wait for the current TX descriptor to be free (DDONE=1 means the
     * DMA engine has finished transmitting the previous frame and has
     * returned ownership to the CPU).
     *
     * Under normal load the descriptor should already be free.  Spin with
     * a short yield so other tasks can run if we must wait.
     */
    while (!(td->txd1 & TXD_DDONE)) {
        xSemaphoreGive(tx_mutex);
        taskYIELD();
        xSemaphoreTake(tx_mutex, portMAX_DELAY);
        td = &txr()[tx_idx];
    }

    /* Copy the pbuf chain to the TX buffer via the uncached KSEG1 alias. */
    dst = tx_buf(tx_idx);
    for (q = p; q != NULL; q = q->next) {
        if (total + q->len > ETH_BUF_SIZE)
            break;   /* Oversized frame — drop the excess. */
        SMEMCPY(dst + total, q->payload, q->len);
        total += (uint16_t)q->len;
    }

    /* Programme the descriptor.  Write txd1 last with DDONE=0 to hand
     * ownership to the DMA engine atomically after all other fields are set. */
    td->txd0 = VIRT_TO_PHYS(tx_bufs[tx_idx]);
    td->txd2 = 0;
    td->txd3 = 0;
    __asm__ volatile("sync" ::: "memory");   /* ensure prior writes complete */
    td->txd1 = TXD_SDL0(total) | TXD_LAST_SEC0;   /* DDONE=0: DMA now owns */

    /* Advance the ring index and tell the DMA engine about the new frame. */
    next_idx = (tx_idx + 1u) % (uint32_t)NUM_TX_DESC;
    tx_idx   = next_idx;
    __asm__ volatile("sync" ::: "memory");
    TX_CTX_IDX0 = next_idx;

    xSemaphoreGive(tx_mutex);
    return ERR_OK;
}

/* ── RX poll (called from the dedicated eth_rx_thread in main.c) ─────────── */
void mt7688_eth_rx_poll(void)
{
    raeth_rxd_t *rd;
    uint32_t     rxd1;
    uint16_t     frame_len;
    struct pbuf *p;
    struct pbuf *q;
    uint16_t     offset;
    int          budget = NUM_RX_DESC; /* max frames per call */

    if (!g_eth_ready || !g_netif)
        return;

    while (budget-- > 0) {
        rd   = &rxr()[rx_idx];
        rxd1 = rd->rxd1;

        if (!(rxd1 & RXD_DONE))
            break;   /* No more received frames in this ring. */

        frame_len = (uint16_t)RXD_SDL0(rxd1);

        /*
         * Some variants include the 4-byte Ethernet FCS in the reported
         * length.  Accept frames of 14 … ETH_BUF_SIZE bytes.
         */
        if (frame_len >= 14u && frame_len <= (uint16_t)ETH_BUF_SIZE) {
            p = pbuf_alloc(PBUF_RAW, frame_len, PBUF_POOL);
            if (p != NULL) {
                /*
                 * Copy received data from the uncached RX buffer into the
                 * pbuf chain.  The chain is usually a single entry because
                 * PBUF_POOL buffers are sized to hold a full Ethernet frame,
                 * but iterate over q->next for correctness.
                 */
                offset = 0;
                for (q = p; q != NULL; q = q->next) {
                    SMEMCPY(q->payload, rx_buf(rx_idx) + offset, q->len);
                    offset += (uint16_t)q->len;
                }

                if (g_netif->input(p, g_netif) != ERR_OK)
                    pbuf_free(p);
            }
            /* On pbuf_alloc failure we still recycle the descriptor below. */
        }

        /* Return the descriptor to the DMA engine.
         * Re-set the buffer address (in case it was clobbered) and clear
         * the DONE bit so the hardware can use the slot for the next frame. */
        rd->rxd0 = VIRT_TO_PHYS(rx_bufs[rx_idx]);
        rd->rxd2 = 0;
        rd->rxd3 = 0;
        __asm__ volatile("sync" ::: "memory");
        rd->rxd1 = 0;   /* DONE=0: DMA now owns this descriptor again */
        __asm__ volatile("sync" ::: "memory");

        /* Tell the DMA engine which slot we just freed. */
        RX_CALC_IDX0 = rx_idx;

        rx_idx = (rx_idx + 1u) % (uint32_t)NUM_RX_DESC;
    }
}

/* ── lwIP netif initialisation callback ─────────────────────────────────── */
err_t mt7688_eth_init(struct netif *netif)
{
    int      i;
    uint32_t hi, lo;
    uint8_t  mac[6];

    g_netif    = netif;
    g_eth_ready = 0;

    netif->name[0] = 'e';
    netif->name[1] = 't';

    /*
     * Read the device MAC address from the GDMA1 hardware registers.
     * The ZOT U-Boot bootloader programmes these from the factory-assigned
     * address stored in the MT7688 OTP/NVRAM before jumping to firmware.
     *
     *   GDMA1_MAC_ADRH bits[15:0]  → MAC bytes 0–1  (most-significant)
     *   GDMA1_MAC_ADRL bits[31:0]  → MAC bytes 2–5  (least-significant)
     */
    hi = GDMA1_MAC_ADRH;
    lo = GDMA1_MAC_ADRL;
    mac[0] = (uint8_t)((hi >>  8) & 0xFFu);
    mac[1] = (uint8_t)((hi >>  0) & 0xFFu);
    mac[2] = (uint8_t)((lo >> 24) & 0xFFu);
    mac[3] = (uint8_t)((lo >> 16) & 0xFFu);
    mac[4] = (uint8_t)((lo >>  8) & 0xFFu);
    mac[5] = (uint8_t)((lo >>  0) & 0xFFu);

    /*
     * Validate: if the register read returns all-zeroes (register address
     * incorrect for this silicon revision) or all-ones (erased/unprogrammed),
     * fall back to a locally-administered unicast address so that the driver
     * still functions on the local network for debugging purposes.
     */
    if ((mac[0] == 0x00 && mac[1] == 0x00 && mac[2] == 0x00 &&
         mac[3] == 0x00 && mac[4] == 0x00 && mac[5] == 0x00) ||
        (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF &&
         mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF)) {
        mac[0] = 0x02; mac[1] = 0x00; mac[2] = 0x00;
        mac[3] = 0x00; mac[4] = 0x00; mac[5] = 0x01;
    }
    SMEMCPY(netif->hwaddr, mac, 6);
    netif->hwaddr_len = 6;
    netif->mtu        = 1500;
    netif->flags      = NETIF_FLAG_BROADCAST
                      | NETIF_FLAG_ETHARP
                      | NETIF_FLAG_IGMP;
    netif->output     = etharp_output;
    netif->linkoutput = mt7688_eth_output;

    /* Create the TX mutex (static allocation — no heap required). */
    tx_mutex = xSemaphoreCreateMutexStatic(&tx_mutex_buf);

    /* ── Initialise the PDMA engine ──────────────────────────────────────── */

    /* Step 1: disable DMA while reconfiguring ring registers. */
    PDMA_GLO_CFG = 0;

    /* Step 2: reset ring index hardware registers. */
    PDMA_RST_IDX = RST_TX_IDX0 | RST_RX_IDX0;

    /* Step 3: TX ring — all descriptors start CPU-owned (DDONE=1). */
    {
        raeth_txd_t *td = txr();
        for (i = 0; i < NUM_TX_DESC; i++) {
            td[i].txd0 = 0;
            td[i].txd1 = TXD_DDONE;   /* CPU owns; free for next TX frame */
            td[i].txd2 = 0;
            td[i].txd3 = 0;
        }
    }
    TX_BASE_PTR0 = VIRT_TO_PHYS(tx_ring);
    TX_MAX_CNT0  = (uint32_t)NUM_TX_DESC;
    TX_CTX_IDX0  = 0;
    tx_idx       = 0;

    /* Step 4: RX ring — pre-fill buffer addresses; all descriptors DMA-owned. */
    {
        raeth_rxd_t *rd = rxr();
        for (i = 0; i < NUM_RX_DESC; i++) {
            rd[i].rxd0 = VIRT_TO_PHYS(rx_bufs[i]);
            rd[i].rxd1 = 0;   /* DONE=0: DMA owns; ready to receive */
            rd[i].rxd2 = 0;
            rd[i].rxd3 = 0;
        }
    }
    RX_BASE_PTR0 = VIRT_TO_PHYS(rx_ring);
    RX_MAX_CNT0  = (uint32_t)NUM_RX_DESC;
    /*
     * RX_CALC_IDX0 = NUM_RX_DESC - 1: tells the DMA engine that all
     * descriptors 0 … NUM_RX_DESC-1 are available for reception.
     * (The ring is "full" from the DMA's point of view — it can start
     * writing received frames immediately at index 0.)
     */
    RX_CALC_IDX0 = (uint32_t)(NUM_RX_DESC - 1);
    rx_idx       = 0;

    /* Step 5: enable DMA with 8-double-word burst transfers. */
    __asm__ volatile("sync" ::: "memory");
    PDMA_GLO_CFG = PDMA_TX_DMA_EN | PDMA_RX_DMA_EN | PDMA_BURST_8DW;

    /*
     * Mark the link as UP.  The MT7688 internal switch is already configured
     * by the ZOT U-Boot bootloader; its port VLAN and forwarding rules are
     * intact at firmware hand-off, so frames can flow between the external
     * RJ-45 and the CPU (PDMA) immediately.
     */
    netif_set_link_up(netif);

    g_eth_ready = 1;
    return ERR_OK;
}
