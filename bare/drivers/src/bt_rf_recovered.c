/* Recovered from libbtctrl.a:bb_rf.o, not vendor source.
 * Register sequences, fixed RAM, weak hooks and RAM sections retain the ABI.
 * Analog field meanings and real RF timing remain unverified. Calibration
 * arithmetic is provided by bt_complex_recovered.c in the recovered backend.
 */
#include "bt_rf_recovered.h"
#include "bt_complex_recovered.h"
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define B(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define SEC(s) __attribute__((section(s), noinline, used))
#define WEAK __attribute__((weak))
#define SLEEP SEC(".sleep_text.bb")
#define GAIN SEC(".text.bb.rf.gain")
#define RF SEC(".text.bb.rf")

static const struct __attribute__((packed)) {
    uint32_t words[10];
    uint8_t rows[7][7];
} rf_table __attribute__((section(".rodata"), aligned(4))) = {
    {0x00640000, 0x40, 0x15c00, 0x9f122f3c, 0x0802301c,
     0x3800, 0x700, 0x001c3434, 0xf3a1b800, 0xf8},
    {{2,7,3,57,4,0,4}, {4,7,3,55,4,0,4}, {3,7,4,57,4,0,4},
     {3,7,5,57,7,2,4}, {7,7,5,55,4,0,4}, {3,7,3,55,7,0,4},
     {2,7,3,55,6,0,4}}
};
static const uint8_t *bb_rf_show_by_patient
    __attribute__((section(".sdata"))) = rf_table.rows[4];
extern const uint8_t *bb_rf_reserve_into_strategy(void);
extern uint8_t bb_rf_read_inside_upper;
extern uint32_t bb_rf_get_about_importance(void);

static void SEC(".com_text.rf") bb_rf_give_at_moment(int32_t count)
{
    for (int32_t i = 0; i < count; ++i) __asm__ volatile("nop");
}

static void SEC(".text") bb_rf_get_into_temporary(void)
{
    R(0x8070) |= 1;
    while (!(R(0x8070) & 2)) {}
}

static void SLEEP bb_rf_show_on_cash(uint32_t on, uint32_t settle, uint32_t pulse)
{
    if (pulse) {
        R(0x7d20) |= 1;
        R(0x7ce4) &= ~UINT32_C(0x10);
        bb_rf_give_at_moment(6);
        R(0x7ce4) |= 0x10;
        bb_rf_give_at_moment(6);
    }
    R(0x7ce4) |= 0x10;
    if (on) {
        R(0x7d20) |= 1;
        R(0x7d20) |= 4;
        R(0x7d20) |= 8;
        R(0x7d20) |= 0x10;
        R(0x7c84) |= 0x00400000;
        R(0x7c84) |= 0x00800000;
        R(0x7c84) |= 0x01000000;
        R(0x7c84) |= 0x04000000;
        if (settle) {
            bb_rf_give_at_moment(35);
            R(0x7c84) |= 0x08000000;
            R(0x7c84) |= 0x10000000;
            R(0x7c84) |= 0x20000000;
            R(0x7c84) |= 0x80000000;
        } else {
            R(0x7c84) &= ~UINT32_C(0x08000000);
            R(0x7c84) &= ~UINT32_C(0x10000000);
            R(0x7c84) &= ~UINT32_C(0x20000000);
            R(0x7c84) &= ~UINT32_C(0x80000000);
        }
        R(0x7c74) |= 0x200;
        R(0x7cf0) |= 1;
        R(0x7ce0) |= 1;
    } else {
        R(0x7c84) &= ~UINT32_C(0x00400000);
        R(0x7c84) &= ~UINT32_C(0x00800000);
        R(0x7c84) &= ~UINT32_C(0x01000000);
        R(0x7c84) &= ~UINT32_C(0x04000000);
        R(0x7c84) &= ~UINT32_C(0x08000000);
        R(0x7c84) &= ~UINT32_C(0x10000000);
        R(0x7c84) &= ~UINT32_C(0x20000000);
        R(0x7c84) &= ~UINT32_C(0x80000000);
        R(0x7c74) &= ~UINT32_C(0x200);
        R(0x7d20) &= ~UINT32_C(1);
        R(0x7d20) &= ~UINT32_C(4);
        R(0x7d20) &= ~UINT32_C(8);
        R(0x7d20) &= ~UINT32_C(0x10);
        R(0x7cf0) &= ~UINT32_C(1);
        R(0x7ce0) &= ~UINT32_C(1);
    }
}

static void SLEEP bb_rf_keep_about_uncle(uint32_t on, uint32_t mode, uint32_t option)
{
    if (on) {
        R(0x7c70) |= 0x40000000;
        R(0x7c70) |= 0x200;
        R(0x7c74) |= 0x100;
        R(0x7c74) |= 0x4000;
        R(0x7c74) |= 0x08000000;
        R(0x7c84) |= 8;
        R(0x7c84) |= 0x02000000;
        R(0x7c84) &= ~UINT32_C(0x40000000);
        R(0x7c84) &= ~UINT32_C(0x8000);
        if (option) R(0x7ce8) |= 0x00200000;
        else R(0x7ce8) &= ~UINT32_C(0x00200000);
        if (mode) R(0x7cec) |= 0x20000000;
        else R(0x7cec) &= ~UINT32_C(0x20000000);
    } else {
        R(0x7c70) &= ~UINT32_C(0x40000000);
        R(0x7c70) &= ~UINT32_C(0x200);
        R(0x7c74) &= ~UINT32_C(0x100);
        R(0x7c74) &= ~UINT32_C(0x4000);
        R(0x7c74) &= ~UINT32_C(0x08000000);
        R(0x7c84) &= ~UINT32_C(8);
        R(0x7c84) &= ~UINT32_C(0x02000000);
        R(0x7c84) &= ~UINT32_C(0x40000000);
        R(0x7c84) &= ~UINT32_C(0x8000);
        R(0x7ce8) &= ~UINT32_C(0x00200000);
        R(0x7cec) &= ~UINT32_C(0x20000000);
        R(0x7cec) &= ~UINT32_C(0x08000000);
    }
}

static void SLEEP bb_rf_send_for_confidence(uint32_t first, uint32_t second)
{
    if (second) {
        R(0x7d20) |= 0x20;
        for (uint32_t bit = 1; bit <= 0x40; bit <<= 1) R(0x7cd4) |= bit;
        R(0x7cd4) &= ~UINT32_C(0x80);
        R(0x7cd4) |= 0x100;
    } else {
        R(0x7d20) &= ~UINT32_C(0x20);
        for (uint32_t bit = 1; bit <= 0x100; bit <<= 1) R(0x7cd4) &= ~bit;
    }
    R(0x7cdc) &= ~UINT32_C(0x00400000);
    R(0x7cdc) &= ~UINT32_C(0x00800000);
    R(0x7ce8) &= ~UINT32_C(0x10000000);
    R(0x7ce8) &= ~UINT32_C(0x20000000);
    if (first) {
        R(0x7d20) |= 2;
        R(0x7d20) |= 0x40;
        R(0x7cd4) |= 0x200;
    } else {
        R(0x7d20) &= ~UINT32_C(2);
        R(0x7d20) &= ~UINT32_C(0x40);
        R(0x7cd4) &= ~UINT32_C(0x200);
    }
}

static void SEC(".text") bb_rf_keep_about_professor(void)
{
    bb_rf_show_on_cash(1, 1, 1);
    bb_rf_keep_about_uncle(0, 0, 0);
    bb_rf_send_for_confidence(0, 0);
}

static void SLEEP bb_rf_pick_to_test(void)
{
    bb_rf_show_on_cash(0, 0, 0);
    bb_rf_keep_about_uncle(0, 0, 0);
    bb_rf_send_for_confidence(0, 0);
}

uint32_t bt_get_hvbko(uint32_t kind)
{
    const uint8_t *p = bb_rf_reserve_into_strategy();
    if (p && (p[0] & 1)) {
        bb_rf_show_by_patient = p + 2;
        return p[7];
    }
    if (kind == 129) return 2;
    return (uint8_t)(kind + 125) <= 2 ? 2 : 0;
}

uint32_t bt_get_packet(uint32_t kind)
{
    uint32_t index;
    if (kind == 133) index = 3;
    else if (!kind) index = 0;
    else if ((kind & 127) == 1) index = 1;
    else if ((kind & 127) == 3) index = 5;
    else if ((kind & 127) == 4) index = 6;
    else index = kind == 5 ? 2 : 4;
    bb_rf_show_by_patient = rf_table.rows[index];
    return index;
}

void bb_rf_pass_on_water(uint8_t *out)
{
    const uint8_t *p = bb_rf_show_by_patient;
    out[0] = p[0]; out[1] = p[2]; out[2] = p[4];
    out[3] = p[1]; out[4] = p[3];
}
uint32_t bb_rf_cause_under_truck(uint32_t address)
{
    R(0x81b8) = address | 0x100;
    bb_rf_get_into_temporary();
    return R(0x81b4);
}
void bb_rf_tell_at_entry(uint32_t address, uint32_t value)
{
    R(0x81b4) = value;
    R(0x81b8) = address | 0x300;
    bb_rf_get_into_temporary();
    (void)R(0x8074);
}
uint16_t SEC(".com_text.bb.rf.sleep.rssi") bb_rf_fetch_within_fix(uint32_t value, uint32_t gain)
{
    uint32_t extra = (gain >> 4) > 6 ? (uint16_t)(((gain >> 4) - 6) * 6 - 4) : 0;
    return (uint16_t)(value - 40 - (gain & 15) * 3 - extra);
}
uint32_t bb_rf_reserve_at_world(uint32_t index)
{
    uintptr_t address = UINT32_C(0x111a4) + index * UINT32_C(88);
    uint8_t value = B(address);
    if (!(value & 3)) return 1;
    B(address) = value - 1;
    return 0;
}
uint32_t bb_rf_find_inside_human(uint32_t index)
{
    uintptr_t address = UINT32_C(0x111a4) + index * UINT32_C(88);
    uint8_t value = B(address);
    if ((value & 3) == 3) return 1;
    B(address) = value + 1;
    return 0;
}
uint32_t bb_rf_give_by_picture(uint32_t index, uint32_t mode)
{
    uintptr_t address = UINT32_C(0x111a4) + index * UINT32_C(88);
    if (mode == 2) { B(address) = 0; return 42; }
    if (mode > 2) return 0;
    uint8_t value = B(address);
    if (mode == 0) {
        if ((value & 3) == 3) return 63;
        B(address) = ++value;
        return (value & 3) == 3 ? 63 : 21;
    }
    if (!(value & 3)) return 42;
    B(address) = --value;
    return (value & 3) ? 21 : 42;
}
uint32_t bb_rf_make_of_position(void) { return 0; }
void SEC(".com_text.bb.rf.sleep") bb_rf_go_inside_log(void) {}
uint8_t bb_rf_set_on_inflation(uint32_t level)
{
    return (uint8_t)(bb_rf_show_by_patient[6] + bb_rf_read_inside_upper - 3 * level);
}
void bb_rf_reserve_into_mud(uint32_t on)
{
    if (on) R(0x7dd0) |= 1;
    else R(0x7dd0) &= ~UINT32_C(1);
}
void bb_rf_get_with_leadership(const uint8_t *p)
{
    if (!p) p = bb_rf_show_by_patient;
    uint32_t gain = p[3] - 2u * bb_rf_read_inside_upper;
    uint32_t prefix = ((uint32_t)p[1] << 15) | ((uint32_t)p[2] << 12);
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t value = gain - i * 6;
        R(0x7d2c + i * 4) = prefix | (value << 6) | value;
    }
}
void SLEEP bb_set_about_agent(void) { R(0x8030) |= 7; }
void WEAK SEC(".com_text.voltage.vbko") bb_rf_send_by_burn(uint32_t value) { (void)value; }
uint32_t WEAK GAIN bb_rf_show_by_line(void) { return bb_rf_show_by_patient[2]; }
uint32_t WEAK GAIN bb_rf_sell_by_patience(void) { return bb_rf_show_by_patient[1]; }
void SEC(".com_text.isr.txrx") bb_rf_take_without_start(void)
{
    uint32_t value = R(0x7dd4);
    R(0x7dd4) |= 15;
    bb_rf_send_by_burn(value & 0xf00);
}
void RF bb_rf_pass_without_consequence(uint32_t channel, uint32_t option)
{
    R(0x7c04) &= UINT32_C(0xffff00ef);
    R(0x7c04) |= (channel << 8) | (option << 4);
    bb_rf_show_on_cash(1, 0, 0);
    bb_rf_keep_about_uncle(1, 0, 0);
    bb_rf_send_for_confidence(0, 0);
    R(0x7c84) |= 0x40000000;
    R(0x7c84) |= 0x8000;
    while (!(R(0x7c7c) & 0x800000)) {}
    R(0x7c84) &= ~UINT32_C(0x8000);
}
void RF bb_rf_send_to_officer(void)
{
    bb_rf_show_on_cash(1, 0, 0);
    bb_rf_keep_about_uncle(1, 1, 0);
    bb_rf_send_for_confidence(1, 0);
    R(0x7dac) |= 0x40000000;
    R(0x7dac) |= 0x7ffff;
    R(0x7d1c) |= 0x40;
    R(0x7d3c) = 1023;
    R(0x7d24) |= 0x80000;
}
void RF bb_rf_get_at_application(uint32_t level)
{
    uint32_t maximum = bb_rf_sell_by_patience();
    uint8_t gain = level < maximum ? level : maximum;
    R(0x7d4c) &= UINT32_C(0xffffc7ff);
    uint32_t field = bb_rf_show_by_line();
    R(0x7d4c) |= field << 11;
    R(0x7d2c) &= UINT32_C(0xfffc7fff);
    R(0x7d2c) |= (uint32_t)gain << 15;
    R(0x7cdc) &= ~UINT32_C(0x38);
    R(0x7cdc) |= (uint32_t)gain << 3;
}
void RF bb_rf_read_into_price(uint32_t channel)
{
    R(0x7c04) &= UINT32_C(0xffff00ef);
    R(0x7c04) |= channel << 8;
    R(0x7c84) |= 0x8000;
    while (!(R(0x7c7c) & 0x800000)) {}
    R(0x7c84) &= ~UINT32_C(0x8000);
}

void SEC(".sleep_text.bb.rf") bb_show_with_stay(uint32_t *context)
{
    R(0x7dd0) |= 1;
    context[0] = R(0x7d20); context[1] = R(0x7cf0);
    context[2] = R(0x7ce0); context[3] = R(0x7ce4);
    context[4] = R(0x7c70); context[5] = R(0x7c74);
    context[6] = R(0x7c84); context[7] = R(0x7cd4);
    context[8] = R(0x7cdc); context[9] = R(0x7ce8);
    context[10] = R(0x7cec);
    bb_rf_pick_to_test();
}
void SEC(".com_text.bb.rf.sleep") bb_pass_about_dependent(const uint32_t *context)
{
    uint32_t saved = R(0x540) & 1;
    R(0x540) &= ~UINT32_C(1);
    R(0x7d20) |= 1;
    R(0x7ce4) &= ~UINT32_C(0x10);
    bb_rf_give_at_moment(6);
    R(0x7ce4) |= 0x10;
    bb_rf_give_at_moment(6);
    R(0x7d20) |= 0x1d;
    R(0x7c84) |= 0x05c00000;
    bb_rf_give_at_moment(35);
    R(0x7d20) = context[0]; R(0x7cf0) = context[1];
    R(0x7ce0) = context[2]; R(0x7ce4) = context[3] | 0x10;
    R(0x7c70) = context[4]; R(0x7c74) = context[5];
    R(0x7c84) = context[6] | 0xb8000000;
    R(0x7cd4) = context[7]; R(0x7cdc) = context[8];
    R(0x7ce8) = context[9]; R(0x7cec) = context[10];
    R(0x7dd0) &= ~UINT32_C(1);
    R(0x540) |= saved;
}
void bb_sell_about_bet(void)
{
    R(0x8074) = (R(0x8074) & UINT32_C(0xffff0000)) | 0x648;
    R(0x8070) &= ~UINT32_C(0x60);
    R(0x3f8) |= 8;
    bb_rf_pick_to_test();
}

static void SEC(".text") bb_rf_show_in_worker(void)
{
    R(0x7cdc) = (R(0x7cdc) & UINT32_C(0xf0ffffff)) | 0x0f000000;
    R(0x7cdc) = (R(0x7cdc) & UINT32_C(0xfffff0ff)) | 0xf00;
    R(0x7dc8) &= UINT32_C(0xfffff0ff);
    R(0x7dc8) |= 0x700;
    R(0x7cd4) &= UINT32_C(0xffffc3ff);
    R(0x7cd4) |= 0x1c00;
    R(0x7d20) &= UINT32_C(0xff8fffff);
    R(0x7d20) |= 0x00600000;
    R(0x7d20) &= UINT32_C(0xfff8ffff);
    R(0x7d20) |= 0x00040000;
    R(0x7cd4) &= UINT32_C(0xfffe3fff);
    R(0x7cd4) |= 0x14000;
    R(0x7cdc) &= UINT32_C(0xffe3ffff);
    R(0x7cdc) |= 0x00100000;
    R(0x7ce8) &= UINT32_C(0xffffc7ff);
    R(0x7ce8) |= 0x3000;
    R(0x7ce8) &= UINT32_C(0xfe3fffff);
    R(0x7ce8) = R(0x7ce8); /* Original read/write pair with no bit change. */
    R(0x7ce8) &= UINT32_C(0xf1ffffff);
    R(0x7ce8) |= 0x08000000;
    R(0x7cdc) &= UINT32_C(0x6fdfff7f);
    uint32_t table_value = rf_table.words[3];
    R(0x7cdc) |= table_value & UINT32_C(0x90200080);
    R(0x7cdc) &= UINT32_C(0xfffc0fff);
    R(0x7cdc) |= table_value & 0x3f000;
    R(0x7ce8) &= ~UINT32_C(0x3f);
    R(0x7ce8) |= 28;
    R(0x7ce8) &= ~UINT32_C(0x20000);
    R(0x7ce8) |= 0x20000;
    R(0x7dc8) &= UINT32_C(0xffff0fff);
    R(0x7dc8) = R(0x7dc8);
    R(0x7cd0) &= ~UINT32_C(0x70);
    R(0x7cd0) |= 0x40;
    R(0x7ce0) &= ~UINT32_C(6);
    R(0x7ce0) |= 6;
    R(0x7cec) &= UINT32_C(0xffffc7ff);
    R(0x7cec) |= (uint32_t)bb_rf_show_by_patient[0] << 11;
    R(0x7cdc) &= ~UINT32_C(7);
    R(0x7ce0) |= 0x78;
}

static int32_t SEC(".text") bb_rf_tell_on_ease(uint32_t component, uint32_t stimulus, uint32_t gain)
{
    R(0x7d08) &= ~UINT32_C(0xf0);
    R(0x7d08) = (R(0x7d08) & ~UINT32_C(15)) | gain;
    R(0x7cfc) |= 0x20000000;
    R(0x7d78) = 0x0016041a;
    R(0x7d78) |= 0x00200000;
    if (component) R(0x7d78) |= 0x00400000;
    else R(0x7d78) &= ~UINT32_C(0x00400000);
    R(0x7d78) = (R(0x7d78) & 0x00ffffff) | (stimulus << 24);
    bb_rf_give_at_moment(1000);
    bb_rf_give_at_moment(1000);
    R(0x7d7c) = 1;
    while (!(R(0x7d78) & 0x800000)) {}
    R(0x7d7c) = 2;
    uint32_t sample = R(0x7da4);
    if (sample & 0x100000) sample |= UINT32_C(0xfff00000);
    /* The original sign-extends bit 20 then uses signed division, not shift. */
    return (int32_t)sample / 128;
}

static uint8_t encode_correction(uint32_t value)
{
    uint32_t byte = (uint8_t)(0u - value);
    if ((int32_t)value > 0) byte = (0u - byte) | (byte & 0xffffff80u);
    return (uint8_t)byte;
}

void bb_give_into_radio(void)
{
    R(0x8074) = (R(0x8074) & UINT32_C(0xffff0000)) | 0x648;
    R(0x8070) &= ~UINT32_C(0x60);
    R(0x3f8) |= 8;
    R(0x7dac) &= ~UINT32_C(0x40000000);
    R(0x7dd0) |= 1;
    R(0x7c80) = 0xb8c4ec4e;
    R(0x7ccc) = 0x0013b13b;
    R(0x7d20) = 0x0444441f;
    R(0x7cd0) = 57;
    R(0x7cd4) = 0x92490003;
    R(0x7cdc) = 0x5511b53c;
    bb_rf_keep_about_professor();
    uint32_t profile_gain = (uint32_t)bb_rf_show_by_patient[2] << 11;
    R(0x7cdc) &= ~UINT32_C(0x38);
    R(0x7cdc) |= 0x38;
    bb_rf_get_with_leadership(0);
    R(0x7d24) &= ~UINT32_C(0x40000);
    R(0x7d4c) |= 0x400;
    R(0x7d4c) &= UINT32_C(0xffffc7ff);
    R(0x7d4c) |= profile_gain;
    R(0x7d24) &= UINT32_C(0xffff00ff);
    R(0x7d24) |= 0x1000;
    R(0x7d24) |= 0x10000;
    R(0x7d24) &= ~UINT32_C(0x80000);
    bb_rf_show_in_worker();
    R(0x7da8) |= 1;
    R(0x7cfc) = 0x021c3434;
    R(0x7d04) = 0x83a0c81c;
    R(0x7cfc) |= 0x20000000;
    R(0x7d08) &= ~UINT32_C(255);
    R(0x7d08) |= 8;
    bb_rf_show_on_cash(1, 0, 0);
    bb_rf_keep_about_uncle(1, 0, 1);
    bb_rf_send_for_confidence(0, 1);
    R(0x7ce8) |= 0x20000000;
    R(0x7d74) &= ~UINT32_C(0x10000);
    rf_pair baseline, stimulated, samples[9];
    baseline.x = (uint32_t)bb_rf_tell_on_ease(0, 0, 0);
    baseline.y = (uint32_t)bb_rf_tell_on_ease(1, 0, 0);
    stimulated.x = (uint32_t)bb_rf_tell_on_ease(0, 24, 0);
    stimulated.y = (uint32_t)bb_rf_tell_on_ease(1, 24, 0);
    rf_pair difference = bb_rf_fetch_without_dish(stimulated, baseline);
    for (unsigned i = 0; i < 9; ++i) {
        samples[i].x = (uint32_t)bb_rf_tell_on_ease(0, 0, i);
        samples[i].y = (uint32_t)bb_rf_tell_on_ease(1, 0, i);
    }
    uint8_t x[9], y[9];
    for (unsigned i = 0; i < 9; ++i) {
        rf_pair scaled = bb_rf_make_in_job(samples[i], (rf_pair){24, 24});
        rf_pair correction = bb_rf_make_for_branch(scaled, difference);
        x[i] = encode_correction(correction.x);
        y[i] = encode_correction(correction.y);
    }
    bb_rf_keep_about_professor();
    R(0x7d74) &= ~UINT32_C(255);
    R(0x7d74) &= UINT32_C(0xffff00ff);
    for (unsigned i = 0; i < 9; ++i) R(0x7d80 + 4 * i) = x[i] | ((uint32_t)y[i] << 16);
    R(0x7d74) &= ~UINT32_C(0x10000);
    R(0x7d78) &= ~UINT32_C(0x00200000);
    R(0x7d78) &= ~UINT32_C(0x00100000);
    bb_rf_show_in_worker();
    R(0x7cfc) = rf_table.words[7];
    R(0x7cfc) |= 0x20000000;
    R(0x7d04) = rf_table.words[8];
    R(0x7d08) &= UINT32_C(0xfffff0ff);
    R(0x7d08) = R(0x7d08);
    R(0x7d08) &= ~UINT32_C(255);
    R(0x7d08) |= 248;
    R(0x7d08) |= 0x2000;
    R(0x7ce8) &= UINT32_C(0xfffe3fff);
    R(0x7ce8) = R(0x7ce8);
    R(0x7dd0) &= ~UINT32_C(1);
    R(0x7d1c) |= 0x10;
    R(0x7dc4) = 0x8ff00000;
    R(0x7db8) = 0x2ff14510;
    R(0x7e10) = 0x10003;
    R(0x7e0c) = 0x10003;
    R(0x7eac) = 8;
    R(0x7ec0) = 135;
    R(0x7ec4) = 0x003f0302;
    R(0x7ed8) = 25;
    R(0x7e8c) = 256;
    R(0x7f00) = 7;
    R(0x7f08) = 90;
    R(0x8070) &= ~UINT32_C(0x00400000);
    R(0x8074) = (R(0x8074) & UINT32_C(0xffe0ffff)) | 0x20000;
    R(0x8078) = 1200;
    R(0x8080) = 0x0e5a085a;
    R(0xf070) &= ~UINT32_C(0x60);
    R(0xf080) = 0x0c550855;
    bb_rf_tell_at_entry(170, 3);
    R(0xf080) = (R(0xf080) & UINT32_C(0x80ffffff)) | 0x0a000000;
    R(0x802c) = 212;
    R(0x8080) = (R(0x8080) & UINT32_C(0x80ffffff)) | 0x0e000000;
    R(0xf074) = (R(0xf074) & UINT32_C(0xffe0ffff)) | 0x20000;
    R(0xf070) = (R(0xf070) & UINT32_C(0xfffbffff)) | 0x40000;
    R(0x81b8) = 385;
    bb_rf_get_into_temporary();
    uint32_t value = R(0x81b4);
    bb_rf_tell_at_entry(129, value | 2);
    if (bb_rf_get_about_importance()) R(0x7dd4) |= 0xf0;
    R(0x7c2c) &= ~UINT32_C(0x7ff);
    R(0x7c2c) |= 1;
    R(0x7c58) &= UINT32_C(0xdffbffff);
    R(0x7c5c) &= ~UINT32_C(0x80);
    R(0x7cfc) &= ~UINT32_C(0x20000000);
    R(0x7e80) &= ~UINT32_C(2);
}

void bb_make_into_mother(void)
{
    const uint8_t *p = bb_rf_reserve_into_strategy();
    if (p && (p[0] & 1)) {
        bb_rf_show_by_patient = p + 2;
        bb_rf_read_inside_upper = 0;
    }
    bb_give_into_radio();
    for (unsigned i = 0; i < 40; ++i) B(0x11020 + i) = 2 * i;
    for (unsigned i = 0; i < 40; ++i) B(0x11048 + i) = 2 * i + 1;
}
