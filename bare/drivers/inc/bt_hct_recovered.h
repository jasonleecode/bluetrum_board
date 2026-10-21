#pragma once
#include <stdint.h>

/* Internal RV32 ABI recovered from libbtctrl.a:hct_msg.o/hct_proc.o.
 * These operate on controller-owned messages with a 16-byte prefix; they
 * are not application APIs and must not be called with ordinary byte arrays.
 * The original symbol names are retained to replace complete archive members.
 */
struct bt_hct_descriptor {
    int8_t offset;
    uint8_t length;
    uint16_t value;
    uint32_t (*format)(uint8_t *, const struct bt_hct_descriptor *, uint32_t);
};
struct bt_hct_handler {
    uint16_t id;
    uint16_t reserved;
    uint32_t (*callback)(uint32_t, uint8_t *);
};
struct bt_hct_handler_table {
    const struct bt_hct_handler *handlers;
    uint16_t count;
    uint16_t reserved;
};

void hct_get_on_birth(uint8_t *payload);
void hct_find_with_problem(uint8_t *payload);
void hct_msg_do_in_vacation(uint8_t *payload);
void hct_msg_cause_of_primary(uint8_t *message, uint8_t value);
uint32_t hct_cause_to_ladder(uint8_t *message, const struct bt_hct_descriptor *descriptor);
uint32_t hct_give_without_resolve(uint8_t *message, const struct bt_hct_descriptor *descriptor);
uint32_t hct_save_within_tea(uint8_t *message, const struct bt_hct_descriptor *descriptor);
const struct bt_hct_descriptor *hct_proc_send_on_network(uint32_t id);
extern uint8_t hct_find_without_crack;
extern const struct bt_hct_handler hct_proc_sell_to_horror[2];
extern const struct bt_hct_handler_table hct_cause_without_cup;
