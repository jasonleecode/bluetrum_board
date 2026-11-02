/* Recovered from libbtctrl.a:lm.o. Descriptor points to lm_proc.o data. */
#include "bt_recovered_internal.h"
extern const uint8_t lm_go_without_farmer[], lm_cause_by_subject[], lm_get_inside_thanks[];
static const struct { uint32_t zero; const void *handlers, *default_handler, *states; uint32_t counts; } descriptor
    __attribute__((section(".rodata.bb.tbl"), aligned(4))) = {
        0, lm_go_without_farmer, lm_cause_by_subject, lm_get_inside_thanks, 0x00010008
    };
void bt21_keep_within_grocery(void)
{
    typedef void (*register_t)(uint32_t, const void *);
    typedef void (*state_t)(uint32_t, uint32_t);
    ((register_t)(uintptr_t)0x84368)(4, &descriptor);
    ((state_t)(uintptr_t)0x84364)(4, 0);
}
void lm_leave_in_treat(uint32_t id, uint32_t value)
{
    typedef uint8_t *(*allocate_t)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef void (*send_t)(void *);
    uint8_t *message = ((allocate_t)(uintptr_t)0x843a0)(id, 9, 4, 1);
    *message = value;
    ((send_t)(uintptr_t)0x843a4)(message);
}
void lm_send_with_consequence(uint32_t id, uint32_t value)
{
    typedef uint8_t *(*allocate_t)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef void (*send_t)(void *);
    uint8_t *message = ((allocate_t)(uintptr_t)0x843a0)(id, 9, 4, 1);
    *message = value;
    ((send_t)(uintptr_t)0x843a4)(message);
}
