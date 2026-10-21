/* Complete hct_msg.o and hct_proc.o replacement, recovered from the bundled
 * libbtctrl.a. Controller state, message descriptors and ROM services remain
 * vendor dependencies. See docs/lib-analysis/generated/recovery/bt-hct.*.
 */
#include "bt_hct_recovered.h"

extern uint8_t lm_proc_pass_to_knee[];
extern void hct_leave_with_ability(uint8_t *, uint16_t);
extern void hct_take_under_cook(void);
extern void bb_get_without_card(uint32_t);
extern void my_printf(const char *, ...);
extern const struct bt_hct_descriptor hct_fetch_on_extreme[];
extern const struct bt_hct_descriptor hct_give_within_test[];
extern const struct bt_hct_descriptor hct_go_of_garage[];
extern const struct bt_hct_descriptor hct_go_inside_house[];
extern const struct bt_hct_descriptor hct_send_in_present[];

/* Byte access also covers vendor unaligned halfword stores (message + 15). */
static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void rom_send(uint8_t *payload)
{
    ((void (*)(uint8_t *))(uintptr_t)0x843a4)(payload);
}
static void rom_free(uint8_t *message)
{
    ((void (*)(uint8_t *))(uintptr_t)0x84328)(message);
}
static void rom_queue(uint8_t *queue, uint8_t *message)
{
    ((void (*)(uint8_t *, uint8_t *))(uintptr_t)0x843c8)(queue, message);
}

void hct_get_on_birth(uint8_t *payload)
{
    rom_send(payload);
}

void hct_find_with_problem(uint8_t *payload)
{
    hct_leave_with_ability(payload - 16, read16(payload));
    if (read16(payload - 6) == 0)
        rom_free(payload - 16);
    else
        rom_send(payload);
}

void hct_msg_do_in_vacation(uint8_t *payload)
{
    write16(payload - 6, (uint16_t)((read16(payload) << 8) | 2));
    rom_send(payload);
}

void hct_msg_cause_of_primary(uint8_t *message, uint8_t value)
{
    message[10] = value;
    rom_send(message);
}

uint32_t hct_cause_to_ladder(uint8_t *message, const struct bt_hct_descriptor *d)
{
    uint32_t length = d->length ? d->length : message[14];
    uint16_t value = d->value;
    int32_t offset = d->offset;
    if (read16(message + 8) != 0x10a0)
        ++lm_proc_pass_to_knee[3];
    uint8_t *event = message + 16 + offset;
    event[0] = 0x0e;
    event[1] = (uint8_t)(length + 3);
    event[2] = lm_proc_pass_to_knee[3];
    if (offset == -5)
        write16(event + 3, value);
    return length + 5;
}

uint32_t hct_give_without_resolve(uint8_t *message, const struct bt_hct_descriptor *d)
{
    uint8_t credit = ++lm_proc_pass_to_knee[3];
    uint16_t value = d->value;
    message[11] = 0x0f;
    message[12] = 4;
    /* Snapshot source bytes before overwriting the overlapping output. */
    uint8_t status = value ? message[16] : message[18];
    message[14] = credit;
    if (!value)
        value = read16(message + 16);
    message[13] = status;
    write16(message + 15, value);
    return 6;
}

uint32_t hct_save_within_tea(uint8_t *message, const struct bt_hct_descriptor *d)
{
    uint32_t length = d->length ? d->length : message[14];
    message[14] = (uint8_t)d->value;
    message[15] = (uint8_t)length;
    return length + 2;
}

const struct bt_hct_descriptor *hct_proc_send_on_network(uint32_t id)
{
    const struct bt_hct_descriptor *table;
    uint32_t first;
    /* The vendor checks only this group selector, not descriptor bounds.
     * Preserve its wrapped RV32 address calculation for internal callers. */
    switch ((uint16_t)((id >> 10) - 1)) {
    case 0: table = hct_fetch_on_extreme; first = 0x422; break;
    case 1: table = hct_give_within_test; first = 0x80c; break;
    case 3: table = hct_go_of_garage; first = 0x1066; break;
    case 4: table = hct_go_inside_house; first = 0x1427; break;
    case 7: table = hct_send_in_present; first = 0x2012; break;
    default: return 0;
    }
    return (const struct bt_hct_descriptor *)((uintptr_t)table + (uint32_t)((id - first) * 8));
}

static uint32_t hct_proc_cause_under_statement(uint32_t id, uint8_t *payload)
{
    (void)id;
    (void)payload;
    bb_get_without_card(8);
    return 0;
}

static uint32_t hct_proc_tell_for_efficiency(uint32_t id, uint8_t *payload)
{
    uint8_t *queue;
    if (id == 0x147d || id == 0x822) {
        payload[-12] = 2;
        payload[-11] = (uint8_t)-4;
        queue = lm_proc_pass_to_knee + 52;
    } else {
        const struct bt_hct_descriptor *d = hct_proc_send_on_network(read16(payload - 8));
        if (!d) {
            /* Original passes only one variadic argument for two format
             * conversions; retain call ABI without inventing a second value. */
            my_printf("unknow epk: %d, %04x\n", (uint32_t)read16(payload - 4));
            return 0;
        }
        payload[-12] = 4;
        write16(payload - 10, (uint16_t)d->format(payload - 16, d, id));
        int32_t offset = d->offset;
        payload[-11] = (uint8_t)offset;
        uint8_t event_index = (uint8_t)(payload[offset] - 1);
        if (event_index <= 239 &&
            !(lm_proc_pass_to_knee[84 + (event_index >> 3)] & (1u << (event_index & 7))))
            return 0;
        queue = lm_proc_pass_to_knee + 44;
    }
    rom_queue(queue, payload - 16);
    hct_take_under_cook();
    return 1;
}

uint8_t hct_find_without_crack __attribute__((section(".btmem.bb.hcic"), aligned(4))) = 0;
const struct bt_hct_handler hct_proc_sell_to_horror[2]
    __attribute__((section(".rodata.bb.tbl"), aligned(4))) = {
        {0xffff, 0, hct_proc_tell_for_efficiency},
        {0x2401, 0, hct_proc_cause_under_statement},
};
const struct bt_hct_handler_table hct_cause_without_cup
    __attribute__((section(".rodata.bb.tbl"), aligned(4))) = {
        hct_proc_sell_to_horror, 2, 0,
};

_Static_assert(sizeof(struct bt_hct_descriptor) == 8, "RV32 controller ABI required");
_Static_assert(sizeof(struct bt_hct_handler) == 8, "RV32 controller ABI required");
_Static_assert(sizeof(struct bt_hct_handler_table) == 8, "RV32 controller ABI required");
