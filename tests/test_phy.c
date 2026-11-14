/* Functional SFR/DMA model for the actual target PHY C implementation.
 * This is NOT silicon emulation, a cycle/timing simulator or physical proof.
 * The harness explicitly supplies DMA bytes, IRQ delivery, W0C bit semantics,
 * bPD_CLR_ALL FIFO/count/flag side effects, and timer expiry. SFR byte/bit
 * aliases are synchronized at interrupt boundaries. CC analogue behavior,
 * electrical BMC, interrupt latency and the real hardware CRC are not modeled.
 * Golden CRC is an independent bit-at-a-time implementation (no DUT table). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CH543_PHY_TEST 1
#define ISR(number, bank)
#include "../hal/ch543_phy.c"

pd_context pd;
uint8_t pd_rx[PD_MAX_FRAME], pd_tx[PD_MAX_FRAME];
static uint8_t vbus = 1;
static unsigned checks;

void board_init(void) { }
uint8_t board_vbus_valid(void) { return vbus; }

void ch543_test_clear_fifo(void)
{
    assert(UPD_CTRL & bPD_CLR_ALL);
    UPD_COUNT = 0;
    UPD_INT_FG = 0;
    PIF_TX_END = PIF_RX_RST = PIF_RX_ACT = PIF_RX_BYTE = 0;
    PD_START = 0;
}

uint8_t ch543_test_dma_address(const uint8_t *buffer)
{
    /* Synthetic valid xRAM layout; the target linker checks its own layout. */
    if (buffer == dma_rx) return 0;
    if (buffer == pd_rx) return 128;
    if (buffer == pd_tx) return 158;
    assert(buffer == goodcrc);
    return 188;
}

static uint32_t golden_step(uint32_t value, uint8_t byte)
{
    unsigned i;
    value ^= byte;
    for (i = 0; i < 8; ++i)
        value = (value >> 1) ^ ((value & 1u) ? UINT32_C(0xedb88320) : 0u);
    return value;
}

static uint32_t golden_crc(const uint8_t *bytes, unsigned count)
{
    uint32_t value = UINT32_C(0xffffffff);
    unsigned i;
    for (i = 0; i < count; ++i) value = golden_step(value, bytes[i]);
    return ~value;
}

static void append_crc(uint8_t *bytes, unsigned count)
{
    uint32_t value = golden_crc(bytes, count);
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[count + i] = (uint8_t)(value >> (8u * i));
}

static void sync_flag_byte(void)
{
    /* Model direct bit writes as W0C; preserve the read-only SOP status. */
    UPD_INT_FG = (UPD_INT_FG & MASK_PD_STAT) |
        (PIF_TX_END ? 0x80u : 0u) | (PIF_RX_RST ? 0x40u : 0u) |
        (PIF_RX_ACT ? 0x20u : 0u) | (PIF_RX_BYTE ? 0x10u : 0u) |
        (PD_START ? 0x04u : 0u);
}

static void init_model(uint8_t power, uint8_t data, uint8_t orientation)
{
    memset(&pd, 0, sizeof pd);
    memset(pd_rx, 0xa5, sizeof pd_rx);
    memset(pd_tx, 0, sizeof pd_tx);
    memset(dma_rx, 0xcd, sizeof dma_rx);
    EA = TR0 = TF0 = 0;
    vbus = 1;
    hal_init();
    pd.power_role = power;
    pd.data_role = data;
    pd_phy_enable(orientation);
    sync_flag_byte();
    assert(EA && IE_USBPD && PD_START);
    assert(UPD_DMA_L == 0 && UPD_TIMER == 119u);
    assert(UPD_INT_EN == 0x70u && UPD_COUNT == 0);
    assert(!(UPD_CTRL & 0x10u)); /* Hard Reset must not reset the MCU. */
}

static void feed(const uint8_t *packet, unsigned count, uint8_t sop, unsigned batch)
{
    unsigned i;
    assert(count <= sizeof dma_rx);
    assert(PD_START && !(UPD_CTRL & bPD_TX_EN));
    for (i = 0; i < count; ++i) {
        dma_rx[i] = packet[i]; /* explicit modeled hardware DMA write */
        UPD_COUNT = (uint8_t)(i + 1u);
        if (batch && (i + 1u) % batch == 0u) {
            PIF_RX_BYTE = 1;
            UPD_INT_FG = sop | bPIE_RX_BYTE;
            pd_isr();
            sync_flag_byte();
            assert(!PIF_RX_BYTE);
        }
    }
    PIF_RX_ACT = 1;
    UPD_INT_FG = sop | bPIE_RX_ACT;
    pd_isr();
    sync_flag_byte();
}

static void tx_complete(void)
{
    PD_START = 0; /* hardware clears START at end of transmit */
    PIF_TX_END = 1;
    UPD_INT_FG = bPIE_TX_END;
    pd_isr();
    sync_flag_byte();
    assert(!(CC1_CTRL & bCC_LVO) && !(CC2_CTRL & bCC_LVO));
    assert(PD_START && !(UPD_CTRL & bPD_TX_EN));
}

static void ack_timer(void)
{
    assert(TR0 && tx_kind == TX_ACK_WAIT);
    TF0 = 0; /* timer IRQ entry auto-clears flag on the real controller */
    timer0_isr();
    sync_flag_byte();
    assert(!TR0 && tx_kind == TX_GOODCRC);
    assert(UPD_T_LEN == 2u && UPD_T_SOP == 0u && UPD_TIMER == 79u);
    assert(UPD_DMA_L == 188u && UPD_INT_EN == bPIE_TX_END);
}

static void assert_dropped(void)
{
    assert(pd_phy_events() == 0u);
    assert(tx_kind == 0u && !TR0 && !rx_locked);
    assert(PD_START && UPD_COUNT == 0 && UPD_DMA_L == 0);
}

static void test_crc_golden(void)
{
    unsigned i;
    uint8_t packet[34];
    assert(golden_crc((const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    for (i = 0; i < 256; ++i) assert(crc_table[i] == golden_step(0, (uint8_t)i));
    for (i = 0; i < 30; ++i) packet[i] = (uint8_t)(i * 73u + 0x5du);
    packet[0] = 0x6fu; packet[1] = 0x7bu; /* 7 DO, MessageID5, peer Source */
    append_crc(packet, 30);
    init_model(0, 0, 1);
    feed(packet, 34, PD_RX_SOP0, 1);
    assert(crc == UINT32_C(0xdebb20e3));
    assert(phy_rx_len == 30 && memcmp(pd_rx, packet, 30) == 0);
    assert(goodcrc[0] == 0x41 && goodcrc[1] == 0x0a);
    ack_timer(); tx_complete();
    assert(pd_phy_events() == PHY_RX);
    ++checks;
}

static void test_goodcrc_roles_and_copy(void)
{
    unsigned power, data, orientation;
    uint8_t packet[10];
    for (power = 0; power < 2; ++power)
        for (data = 0; data < 2; ++data)
            for (orientation = 1; orientation <= 2; ++orientation) {
                init_model((uint8_t)power, (uint8_t)data, (uint8_t)orientation);
                packet[0] = (uint8_t)(0x4fu | (!data ? 0x20u : 0u));
                packet[1] = (uint8_t)(0x1au | !power); /* ID5, 1 DO */
                packet[2] = 1; packet[3] = 0x80; packet[4] = 0; packet[5] = 0xff;
                append_crc(packet, 6);
                feed(packet, 10, PD_RX_SOP0, 3); /* final byte drained at RX_ACT */
                assert(phy_rx_len == 6 && memcmp(pd_rx, packet, 6) == 0);
                assert(pd_rx[6] == 0xa5 && pd_phy_events() == 0);
                assert(TH0 == 0xff && TL0 == 0x38);
                assert(goodcrc[0] == (0x41u | (data << 5)));
                assert(goodcrc[1] == (0x0au | power));
                assert(!pd_phy_send(6)); /* ACK timer owns the PHY */
                ack_timer();
                assert((orientation == 2 ? CC2_CTRL : CC1_CTRL) & bCC_LVO);
                tx_complete();
                assert(pd_phy_events() == PHY_RX); /* never normal TX_END */
                pd_phy_rx_release(); assert(!rx_locked);
            }
    ++checks;
}

static void test_bad_crc_and_sop(void)
{
    unsigned byte, bit, sop;
    uint8_t packet[10] = {0x6f, 0x1b, 1, 0x80, 0, 0xff};
    append_crc(packet, 6);
    /* Every bit position in a complete message+CRC must be protected. */
    for (byte = 0; byte < 10; ++byte)
        for (bit = 0; bit < 8; ++bit) {
            init_model(0, 0, 1);
            packet[byte] ^= (uint8_t)(1u << bit);
            feed(packet, 10, PD_RX_SOP0, 1);
            assert_dropped();
            packet[byte] ^= (uint8_t)(1u << bit);
        }
    for (sop = 0; sop <= 3; ++sop) if (sop != PD_RX_SOP0) {
        init_model(0, 0, 1);
        feed(packet, 10, (uint8_t)sop, 1);
        assert_dropped();
    }
    ++checks;
}

static void test_frame_lengths(void)
{
    unsigned count;
    uint8_t packet[128];
    for (count = 0; count <= 128; ++count) {
        init_model(0, 0, 1);
        memset(packet, 0x5a, sizeof packet);
        packet[0] = 0x6f; packet[1] = 0x71; /* claims exactly 30+4 bytes */
        if (count >= 4) append_crc(packet, count - 4);
        feed(packet, count, PD_RX_SOP0, 1);
        if (count == 34) {
            assert(phy_rx_len == 30 && tx_kind == TX_ACK_WAIT);
        } else assert_dropped();
    }
    /* The model does not DMA beyond its 128-byte backing; an impossible count
     * exercises the software guard without pretending hardware allowed it. */
    init_model(0, 0, 1);
    UPD_COUNT = 255; PIF_RX_ACT = 1; UPD_INT_FG = 0x21;
    pd_isr(); sync_flag_byte(); assert_dropped();
    init_model(0, 0, 1);
    packet[0] = 0x6f; packet[1] = 0x91; /* Extended is unsupported */
    append_crc(packet, 6); feed(packet, 10, PD_RX_SOP0, 1); assert_dropped();
    ++checks;
}

static void test_received_goodcrc(void)
{
    unsigned power, data, revision, id, wrong;
    uint8_t packet[6];
    for (power = 0; power < 2; ++power)
        for (data = 0; data < 2; ++data)
            for (revision = 1; revision <= 2; ++revision)
                for (id = 0; id < 8; ++id) {
                    init_model((uint8_t)power, (uint8_t)data, 1);
                    packet[0] = (uint8_t)(1u | (revision << 6) | (!data << 5));
                    packet[1] = (uint8_t)((id << 1) | !power);
                    append_crc(packet, 2); feed(packet, 6, PD_RX_SOP0, 1);
                    assert(pd_phy_events() == PHY_ACK && phy_ack_id == id);
                    assert(!TR0 && !tx_kind && !rx_locked);
                }
    for (wrong = 0; wrong < 5; ++wrong) {
        init_model(0, 0, 1);
        packet[0] = 0x61; packet[1] = 0x0b;
        if (wrong == 0) packet[0] = 0x41; /* same DR */
        if (wrong == 1) packet[1] = 0x0a; /* same PR */
        if (wrong == 2) packet[0] = 0x21; /* revision 0 */
        if (wrong == 3) packet[0] = 0xe1; /* reserved revision 3 */
        if (wrong == 4) packet[1] = 0x8b; /* Extended bit */
        append_crc(packet, 2); feed(packet, 6, PD_RX_SOP0, 1);
        assert_dropped();
    }
    ++checks;
}

static void test_transmit_and_reset_events(void)
{
    init_model(0, 0, 2);
    pd_tx[0] = 0x4f; pd_tx[1] = 0x10;
    assert(!pd_phy_send(1) && !pd_phy_send(31));
    assert(pd_phy_send(6));
    assert(UPD_DMA_L == 158 && UPD_T_LEN == 6 && UPD_T_SOP == 0);
    assert(tx_kind == TX_NORMAL && (CC2_CTRL & bCC_LVO));
    assert(!pd_phy_send(6));
    tx_complete(); assert(pd_phy_events() == PHY_TX_END);
    pd_phy_hard_reset();
    assert(UPD_T_LEN == 0 && UPD_T_SOP == 0xa9 && tx_kind == TX_RESET);
    tx_complete(); assert(pd_phy_events() == PHY_RESET);
    /* Received cable reset is not a port Hard Reset. */
    PIF_RX_RST = 1; UPD_INT_FG = 0x43;
    pd_isr(); sync_flag_byte(); assert(pd_phy_events() == 0);
    PIF_RX_RST = 1; UPD_INT_FG = 0x42;
    pd_isr(); sync_flag_byte(); assert(pd_phy_events() == PHY_RESET);
    ++checks;
}

static void test_backpressure_and_detach(void)
{
    uint8_t packet[10] = {0x6f, 0x11, 1, 0x80, 0, 0xff};
    uint8_t saved[6];
    init_model(0, 0, 1);
    append_crc(packet, 6); feed(packet, 10, PD_RX_SOP0, 0);
    ack_timer(); tx_complete(); assert(pd_phy_events() == PHY_RX);
    memcpy(saved, pd_rx, 6);
    packet[1] = 0x13; append_crc(packet, 6);
    feed(packet, 10, PD_RX_SOP0, 1);
    assert(!TR0 && !tx_kind && pd_phy_events() == 0);
    assert(rx_locked && memcmp(saved, pd_rx, 6) == 0);
    pd_phy_rx_release();
    feed(packet, 10, PD_RX_SOP0, 1);
    assert(tx_kind == TX_ACK_WAIT && goodcrc[1] == 2);
    pd_phy_disable(); timer0_isr();
    assert(!tx_kind && !PD_START && !IE_USBPD && !TR0);
    assert(!(CC1_CTRL & bCC_LVO) && !(CC2_CTRL & bCC_LVO));
    assert(!pd_phy_send(6));
    vbus = 0; pd_phy_enable(1);
    assert(pd_phy_events() == PHY_FAULT && !IE_USBPD);
    ++checks;
}

int main(void)
{
    test_crc_golden();
    test_goodcrc_roles_and_copy();
    test_bad_crc_and_sop();
    test_frame_lengths();
    test_received_goodcrc();
    test_transmit_and_reset_events();
    test_backpressure_and_detach();
    printf("PHY register model: %u groups passed (functional only; no hardware/timing claim)\n", checks);
    return 0;
}
