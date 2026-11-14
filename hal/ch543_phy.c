/* CH543 register-level driver. Register sequencing follows CH543DS1 v1.6
 * 18.3 and WCH EVT PD_PHY.c, independently implemented here.
 * SOP-only policy; hardware also supports SOP-prime/double-prime.
 * CRC32 is accumulated at RX_BYTE so GoodCRC does not wait for an entire
 * packet's software CRC. No assumption is made about UPD_CRC32 residual.
 * The 256-entry table costs 1024 bytes of code; no RAM CRC table exists.
 * Real 48 MHz ISR latency, CRC wire order and response timing are UNVERIFIED.
 */
#include "pd.h"
#include "ch543_regs.h"
#include "board.h"
#include "ch543_isr.h"

static uint8_t XDATA dma_rx[PD_DMA_SIZE];
static uint8_t XDATA goodcrc[2];
static volatile uint8_t events, tx_kind, enabled_orientation, rx_locked;
volatile uint8_t phy_rx_len, phy_ack_id;
static uint8_t crc_seen;
static uint32_t crc;
#define TX_NORMAL 1u
#define TX_GOODCRC 2u
#define TX_RESET 3u
#define TX_ACK_WAIT 4u

static const uint32_t CODE crc_table[256] = {
    0x00000000UL, 0x77073096UL, 0xee0e612cUL, 0x990951baUL,
    0x076dc419UL, 0x706af48fUL, 0xe963a535UL, 0x9e6495a3UL,
    0x0edb8832UL, 0x79dcb8a4UL, 0xe0d5e91eUL, 0x97d2d988UL,
    0x09b64c2bUL, 0x7eb17cbdUL, 0xe7b82d07UL, 0x90bf1d91UL,
    0x1db71064UL, 0x6ab020f2UL, 0xf3b97148UL, 0x84be41deUL,
    0x1adad47dUL, 0x6ddde4ebUL, 0xf4d4b551UL, 0x83d385c7UL,
    0x136c9856UL, 0x646ba8c0UL, 0xfd62f97aUL, 0x8a65c9ecUL,
    0x14015c4fUL, 0x63066cd9UL, 0xfa0f3d63UL, 0x8d080df5UL,
    0x3b6e20c8UL, 0x4c69105eUL, 0xd56041e4UL, 0xa2677172UL,
    0x3c03e4d1UL, 0x4b04d447UL, 0xd20d85fdUL, 0xa50ab56bUL,
    0x35b5a8faUL, 0x42b2986cUL, 0xdbbbc9d6UL, 0xacbcf940UL,
    0x32d86ce3UL, 0x45df5c75UL, 0xdcd60dcfUL, 0xabd13d59UL,
    0x26d930acUL, 0x51de003aUL, 0xc8d75180UL, 0xbfd06116UL,
    0x21b4f4b5UL, 0x56b3c423UL, 0xcfba9599UL, 0xb8bda50fUL,
    0x2802b89eUL, 0x5f058808UL, 0xc60cd9b2UL, 0xb10be924UL,
    0x2f6f7c87UL, 0x58684c11UL, 0xc1611dabUL, 0xb6662d3dUL,
    0x76dc4190UL, 0x01db7106UL, 0x98d220bcUL, 0xefd5102aUL,
    0x71b18589UL, 0x06b6b51fUL, 0x9fbfe4a5UL, 0xe8b8d433UL,
    0x7807c9a2UL, 0x0f00f934UL, 0x9609a88eUL, 0xe10e9818UL,
    0x7f6a0dbbUL, 0x086d3d2dUL, 0x91646c97UL, 0xe6635c01UL,
    0x6b6b51f4UL, 0x1c6c6162UL, 0x856530d8UL, 0xf262004eUL,
    0x6c0695edUL, 0x1b01a57bUL, 0x8208f4c1UL, 0xf50fc457UL,
    0x65b0d9c6UL, 0x12b7e950UL, 0x8bbeb8eaUL, 0xfcb9887cUL,
    0x62dd1ddfUL, 0x15da2d49UL, 0x8cd37cf3UL, 0xfbd44c65UL,
    0x4db26158UL, 0x3ab551ceUL, 0xa3bc0074UL, 0xd4bb30e2UL,
    0x4adfa541UL, 0x3dd895d7UL, 0xa4d1c46dUL, 0xd3d6f4fbUL,
    0x4369e96aUL, 0x346ed9fcUL, 0xad678846UL, 0xda60b8d0UL,
    0x44042d73UL, 0x33031de5UL, 0xaa0a4c5fUL, 0xdd0d7cc9UL,
    0x5005713cUL, 0x270241aaUL, 0xbe0b1010UL, 0xc90c2086UL,
    0x5768b525UL, 0x206f85b3UL, 0xb966d409UL, 0xce61e49fUL,
    0x5edef90eUL, 0x29d9c998UL, 0xb0d09822UL, 0xc7d7a8b4UL,
    0x59b33d17UL, 0x2eb40d81UL, 0xb7bd5c3bUL, 0xc0ba6cadUL,
    0xedb88320UL, 0x9abfb3b6UL, 0x03b6e20cUL, 0x74b1d29aUL,
    0xead54739UL, 0x9dd277afUL, 0x04db2615UL, 0x73dc1683UL,
    0xe3630b12UL, 0x94643b84UL, 0x0d6d6a3eUL, 0x7a6a5aa8UL,
    0xe40ecf0bUL, 0x9309ff9dUL, 0x0a00ae27UL, 0x7d079eb1UL,
    0xf00f9344UL, 0x8708a3d2UL, 0x1e01f268UL, 0x6906c2feUL,
    0xf762575dUL, 0x806567cbUL, 0x196c3671UL, 0x6e6b06e7UL,
    0xfed41b76UL, 0x89d32be0UL, 0x10da7a5aUL, 0x67dd4accUL,
    0xf9b9df6fUL, 0x8ebeeff9UL, 0x17b7be43UL, 0x60b08ed5UL,
    0xd6d6a3e8UL, 0xa1d1937eUL, 0x38d8c2c4UL, 0x4fdff252UL,
    0xd1bb67f1UL, 0xa6bc5767UL, 0x3fb506ddUL, 0x48b2364bUL,
    0xd80d2bdaUL, 0xaf0a1b4cUL, 0x36034af6UL, 0x41047a60UL,
    0xdf60efc3UL, 0xa867df55UL, 0x316e8eefUL, 0x4669be79UL,
    0xcb61b38cUL, 0xbc66831aUL, 0x256fd2a0UL, 0x5268e236UL,
    0xcc0c7795UL, 0xbb0b4703UL, 0x220216b9UL, 0x5505262fUL,
    0xc5ba3bbeUL, 0xb2bd0b28UL, 0x2bb45a92UL, 0x5cb36a04UL,
    0xc2d7ffa7UL, 0xb5d0cf31UL, 0x2cd99e8bUL, 0x5bdeae1dUL,
    0x9b64c2b0UL, 0xec63f226UL, 0x756aa39cUL, 0x026d930aUL,
    0x9c0906a9UL, 0xeb0e363fUL, 0x72076785UL, 0x05005713UL,
    0x95bf4a82UL, 0xe2b87a14UL, 0x7bb12baeUL, 0x0cb61b38UL,
    0x92d28e9bUL, 0xe5d5be0dUL, 0x7cdcefb7UL, 0x0bdbdf21UL,
    0x86d3d2d4UL, 0xf1d4e242UL, 0x68ddb3f8UL, 0x1fda836eUL,
    0x81be16cdUL, 0xf6b9265bUL, 0x6fb077e1UL, 0x18b74777UL,
    0x88085ae6UL, 0xff0f6a70UL, 0x66063bcaUL, 0x11010b5cUL,
    0x8f659effUL, 0xf862ae69UL, 0x616bffd3UL, 0x166ccf45UL,
    0xa00ae278UL, 0xd70dd2eeUL, 0x4e048354UL, 0x3903b3c2UL,
    0xa7672661UL, 0xd06016f7UL, 0x4969474dUL, 0x3e6e77dbUL,
    0xaed16a4aUL, 0xd9d65adcUL, 0x40df0b66UL, 0x37d83bf0UL,
    0xa9bcae53UL, 0xdebb9ec5UL, 0x47b2cf7fUL, 0x30b5ffe9UL,
    0xbdbdf21cUL, 0xcabac28aUL, 0x53b39330UL, 0x24b4a3a6UL,
    0xbad03605UL, 0xcdd70693UL, 0x54de5729UL, 0x23d967bfUL,
    0xb3667a2eUL, 0xc4614ab8UL, 0x5d681b02UL, 0x2a6f2b94UL,
    0xb40bbe37UL, 0xc30c8ea1UL, 0x5a05df1bUL, 0x2d02ef8dUL,
};

/* Macros avoid a non-reentrant helper shared by foreground and interrupts.
 * Byte-addressed DMA must be wholly below xRAM 0x100: linker enforces this. */
#define RX_ARM() do { \
    PD_START = 0; \
    UPD_CTRL |= bPD_CLR_ALL; \
    CH543_CLEAR_OBSERVED(); \
    UPD_CTRL &= (uint8_t)~(bPD_CLR_ALL | bPD_TX_EN); \
    UPD_DMA_L = CH543_DMA_ADDRESS(dma_rx); \
    UPD_TIMER = 119u; \
    UPD_INT_EN = bPIE_RX_ACT | bPIE_RX_RST | bPIE_RX_BYTE; \
    crc = 0xffffffffUL; \
    crc_seen = 0; \
    PD_START = 1; \
} while (0)

#define TX_START(buffer, length, sop) do { \
    PD_START = 0; \
    UPD_CTRL |= bPD_CLR_ALL | bPD_TX_EN; \
    CH543_CLEAR_OBSERVED(); \
    UPD_CTRL &= (uint8_t)~bPD_CLR_ALL; \
    UPD_DMA_L = CH543_DMA_ADDRESS(buffer); \
    UPD_TIMER = 79u; \
    UPD_T_SOP = (sop); \
    UPD_T_LEN = (length); \
    UPD_INT_EN = bPIE_TX_END; \
    if (UPD_CTRL & bPD_CC_SEL) CC2_CTRL |= bCC_LVO; \
    else CC1_CTRL |= bCC_LVO; \
    PD_START = 1; \
} while (0)

void hal_init(void)
{
    EA = 0;
    SAFE_MOD = 0x55u;
    SAFE_MOD = 0xaau;
    POWER_CFG |= bLDO_CORE_VOL;
    CLOCK_CFG = (CLOCK_CFG & (uint8_t)~0x07u) | 0x07u; /* 48 MHz */
    SAFE_MOD = 0;
    board_init();
    events = tx_kind = rx_locked = enabled_orientation = 0;
    phy_rx_len = phy_ack_id = 0;
    /* 0.22 V detects default-current Rp as well as 1.5 A/3 A Rp. */
    CC1_CTRL = CC2_CTRL = bCC_CE | bCC_CVS0 | bCC_PD;
    UPD_CTRL = bPD_CLR_ALL;
    CH543_CLEAR_OBSERVED();
    UPD_INT_EN = 0;
    IE_USBPD = 0;
    IP = 0;
    IP_EX = bIP_USBPD; /* PD high priority; timer/HPD low priority. */
    EA = 1;
}

uint8_t typec_get_orientation(void)
{
    uint8_t cc1, cc2;
    if (!board_vbus_valid()) return 0;
    /* During PD traffic the active comparator is the 0.55 V BMC receiver.
     * It cannot simultaneously measure default-Rp DC attachment (about 0.4V).
     * A powered Sink therefore retains orientation until real VBUS_OK falls.
     * The external VBUS monitor must reflect cable detach without long hold-up. */
    if (enabled_orientation) return enabled_orientation;
    cc1 = CC1_CTRL & bCC_CMPO;
    cc2 = CC2_CTRL & bCC_CMPO;
    if (cc1 && !cc2) return 1;
    if (cc2 && !cc1) return 2;
    return 0; /* accessory/ambiguous/no attachment */
}

void pd_phy_enable(uint8_t orientation)
{
    uint8_t irq_enabled = EA;
    EA = 0;
    if ((orientation != 1u && orientation != 2u) || !board_vbus_valid()) {
        events |= PHY_FAULT;
        EA = irq_enabled;
        return;
    }
    TR0 = 0;
    tx_kind = rx_locked = 0;
    enabled_orientation = orientation;
    /* Clear RESET_EN: a received Hard Reset is an event, never an MCU reset. */
    UPD_CTRL = bPD_DMA_EN | (orientation == 2u ? bPD_CC_SEL : 0u);
    if (orientation == 1u) CC1_CTRL = bCC_CE | bCC_PD;
    else CC2_CTRL = bCC_CE | bCC_PD;
    RX_ARM();
    IE_USBPD = 1;
    EA = irq_enabled;
}

void pd_phy_disable(void)
{
    uint8_t irq_enabled = EA;
    EA = 0;
    IE_USBPD = 0;
    TR0 = 0;
    TF0 = 0;
    PD_START = 0;
    UPD_CTRL = bPD_CLR_ALL;
    CH543_CLEAR_OBSERVED();
    UPD_INT_EN = 0;
    CC1_CTRL = CC2_CTRL = bCC_CE | bCC_CVS0 | bCC_PD;
    enabled_orientation = tx_kind = events = rx_locked = 0;
    EA = irq_enabled;
}

uint8_t pd_phy_send(uint8_t len)
{
    uint8_t irq_enabled = EA;
    EA = 0;
    /* Refuse a collision with a packet already started or waiting for service.
     * The policy engine retries later; do not destroy pending receive flags. */
    if (!enabled_orientation || tx_kind || len < 2u || len > PD_MAX_FRAME ||
        (UPD_INT_FG & (bPIE_RX_ACT | bPIE_RX_RST)) ||
        (UPD_COUNT && (UPD_INT_FG & MASK_PD_STAT))) {
        EA = irq_enabled;
        return 0;
    }
    tx_kind = TX_NORMAL;
    TX_START(pd_tx, len, UPD_SOP0);
    EA = irq_enabled;
    return 1;
}

void pd_phy_hard_reset(void)
{
    uint8_t irq_enabled = EA;
    EA = 0;
    if (enabled_orientation) {
        TR0 = 0;
        TF0 = 0;
        tx_kind = TX_RESET;
        rx_locked = 0;
        TX_START(goodcrc, 0, UPD_HARD_RESET);
    }
    EA = irq_enabled;
}

uint8_t pd_phy_events(void)
{
    uint8_t result;
    uint8_t irq_enabled = EA;
    EA = 0;
    result = events;
    events = 0;
    EA = irq_enabled;
    return result;
}

void pd_phy_rx_release(void) { rx_locked = 0; }

/* Only called by the PD interrupt: no generic pointer, no allocation, no wait. */
#if defined(CH543_PHY_TEST)
static void crc_update(void)
#elif defined(__SDCC)
static void crc_update(void) __using(1)
#else
static void crc_update(void) using 1
#endif
{
    /* Dedicated storage: an interrupt helper must not overlay foreground
     * locals in SDCC's shared OSEG. This costs one direct-data byte. */
    static uint8_t count;
    uint8_t index;
    count = UPD_COUNT;
    if (count > PD_DMA_SIZE) return;
    while (crc_seen < count) {
        index = (uint8_t)crc ^ dma_rx[crc_seen++];
        crc = (crc >> 8) ^ crc_table[index];
    }
}

void pd_isr(void) ISR(10,1)
{
    uint8_t count, i, len, status;
    if (PIF_RX_RST) {
        status = UPD_INT_FG & MASK_PD_STAT;
        PD_START = 0;
        TR0 = 0;
        tx_kind = 0;
        rx_locked = 0;
        if (status == PD_RX_SOP1_HRST) events = PHY_RESET;
        /* SOP cable reset is ignored by this port-only implementation. */
        RX_ARM();
        return;
    }
    if (PIF_TX_END) {
        CC1_CTRL &= (uint8_t)~bCC_LVO;
        CC2_CTRL &= (uint8_t)~bCC_LVO;
        if (tx_kind == TX_NORMAL) events |= PHY_TX_END;
        else if (tx_kind == TX_GOODCRC) events |= PHY_RX;
        else if (tx_kind == TX_RESET) events = PHY_RESET;
        tx_kind = 0;
        RX_ARM();
        return;
    }
    if (PIF_RX_ACT) {
        PD_START = 0;
        status = UPD_INT_FG & MASK_PD_STAT;
        count = UPD_COUNT;
        crc_update();
        /* Hardware maximum is 123 payload bytes + 4 CRC, hence DMA128.
         * Only standard PD2 messages up to 7 data objects reach the core. */
        if (status != PD_RX_SOP0 || count < 6u || count > 34u ||
            crc != 0xdebb20e3UL) {
            RX_ARM();
            return;
        }
        len = count - 4u;
        if ((dma_rx[1] & 0x80u) ||
            len != (2u + ((dma_rx[1] >> 4) & 7u) * 4u)) {
            RX_ARM();
            return;
        }
        if (len == 2u && (dma_rx[0] & 0x1fu) == PD_GOODCRC) {
            /* PD2/PD3 peer revision and opposite SOP roles are required.
             * MessageID is matched by the link state machine; malformed
             * GoodCRC must never advance its transmission state. */
            i = (dma_rx[0] >> 6) & 3u;
            if ((i == 1u || i == 2u) &&
                ((dma_rx[0] >> 5) & 1u) != pd.data_role &&
                (dma_rx[1] & 1u) != pd.power_role) {
                phy_ack_id = (dma_rx[1] >> 1) & 7u;
                events |= PHY_ACK;
            }
            RX_ARM();
            return;
        }
        /* Never acknowledge a second unique packet that cannot be delivered.
         * The peer retries it after the foreground releases pd_rx. */
        if (rx_locked) {
            RX_ARM();
            return;
        }
        rx_locked = 1;
        phy_rx_len = len;
        /* Build GoodCRC using current independent USB and power roles. */
        goodcrc[0] = PD_GOODCRC | 0x40u | (pd.data_role ? 0x20u : 0u);
        goodcrc[1] = (dma_rx[1] & 0x0eu) | (pd.power_role ? 1u : 0u);
        tx_kind = TX_ACK_WAIT;
        UPD_INT_EN = 0;
        TR0 = 0;
        TH0 = 0xffu;
        TL0 = 0x38u; /* 200 counts / 4 MHz = 50 us after CRC verification */
        TF0 = 0;
        TR0 = 1;
        for (i = 0; i < len; ++i) pd_rx[i] = dma_rx[i];
        return;
    }
    if (PIF_RX_BYTE) {
        PIF_RX_BYTE = 0; /* W0C bit instruction, not a register RMW. */
        crc_update();
    }
}

void timer0_isr(void) ISR(1,2)
{
    TR0 = 0;
    if (tx_kind == TX_ACK_WAIT && enabled_orientation) {
        tx_kind = TX_GOODCRC;
        TX_START(goodcrc, 2u, UPD_SOP0);
    }
}
