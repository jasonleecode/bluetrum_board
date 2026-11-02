/* Recovered from libbtctrl.a:lm_proc.o. Original handler tables and IDs kept. */
#include "bt_recovered_internal.h"
extern void lm_leave_in_treat(uint32_t, uint32_t);
extern void ext_util_write_within_brain(uint32_t, uint32_t), ext_util_show_on_finger(uint8_t *, uint8_t *);
extern void *ext_util_read_without_raw(void *), *ext_util_save_of_ease(void *);
extern uint32_t lm_proc_save_about_hearing(uint32_t, uint32_t, uint32_t, uint32_t);
extern void lm_proc_tell_to_complex(const void *);
extern uint32_t lm_proc_save_about_ruin(uint32_t, const void *, const void *);
extern uint8_t lm_proc_pass_to_knee[];
extern uint32_t lm_proc_do_on_kiss(void);
#define HANDLER(name) static uint32_t BT_SEC(".text") name( \
    uint32_t id __attribute__((unused)), uint8_t *payload __attribute__((unused)), \
    uint32_t destination __attribute__((unused)), uint32_t source __attribute__((unused)))
static uint8_t *allocate(uint32_t id, uint32_t size)
{
    typedef uint8_t *(*allocate_t)(uint32_t,uint32_t,uint32_t,uint32_t);
    return ((allocate_t)(uintptr_t)0x843a0)(id,9,4,size);
}
static void send(uint8_t *message)
{ ((void (*)(void *))(uintptr_t)0x843a4)(message); }
HANDLER(lm_proc_save_without_neck)
{
    uint8_t *m=allocate(0x10bd,9);
    m[2]=3; m[7]=3; m[5]=66; m[1]=9; m[4]=9; m[6]=6; m[3]=0; m[8]=0; m[0]=0;
    send(m); return 0;
}
HANDLER(lm_proc_reserve_to_boy)
{ uint8_t *m=allocate(0x10be,65); m[0]=0; send(m); return 0; }
HANDLER(lm_proc_sell_at_space)
{
    uint8_t *m=allocate(0x10c1,8);
    m[1]=167; m[2]=2; m[4]=0; m[5]=0; m[6]=0; m[7]=0; m[3]=0; m[0]=0;
    send(m); return 0;
}
HANDLER(lm_proc_sell_by_coffee) { lm_leave_in_treat(0x107b,0); return 0; }
HANDLER(lm_proc_cause_under_cell) { lm_leave_in_treat(0x10b9,0); return 0; }
HANDLER(lm_proc_take_within_bonus)
{ ext_util_write_within_brain(payload[0],payload[1]); lm_leave_in_treat(0x10bb,0); return 0; }
HANDLER(lm_proc_go_of_chain)
{
    uint8_t *m=allocate(0x10ba,3); m[0]=0;
    ext_util_show_on_finger(m+1,m+2); send(m); return 0;
}
HANDLER(lm_proc_tell_in_film)
{
    uint8_t *m=allocate(0x10bf,9); ext_util_read_without_raw(m+1);
    m[0]=0; send(m); return 0;
}
HANDLER(lm_proc_sell_of_gas)
{
    uint32_t result=lm_proc_save_about_hearing(payload[0]|((uint32_t)payload[1]<<8),payload[2],
                         payload[3]|((uint32_t)payload[4]<<8),payload[5]|((uint32_t)payload[6]<<8));
    lm_leave_in_treat(0x109f,result); return 0;
}
HANDLER(lm_proc_go_about_presentation)
{ lm_proc_tell_to_complex(payload); lm_leave_in_treat(0x107a,0); return 0; }
HANDLER(lm_proc_show_in_cause)
{
    uint8_t *m=allocate(0x10c2,7); ext_util_save_of_ease(m+1);
    m[0]=0; send(m); return 0;
}
HANDLER(lm_proc_send_without_aside)
{
    if (lm_proc_pass_to_knee[80]) {
        uint32_t result=lm_proc_save_about_ruin(payload[0],payload+1,payload+5);
        if (result) lm_leave_in_treat(0x10a0,result);
    }
    return 0;
}

uint8_t lm_get_inside_thanks __attribute__((section(".btmem.bb.lc"),aligned(4))) = 0;
static const uintptr_t lm_proc_table[] __attribute__((section(".rodata.bb.tbl"),aligned(4),used)) = {
    (uintptr_t)((const uint8_t *)lm_proc_table + 0x48), /* +0x000 */
    UINT32_C(0x0000000c), /* +0x004 */
    UINT32_C(0x00000000), /* +0x008 */
    UINT32_C(0x00000000), /* +0x00c */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xf0), /* +0x010 */
    UINT32_C(0x00000003), /* +0x014 */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xd8), /* +0x018 */
    UINT32_C(0x00000003), /* +0x01c */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xc8), /* +0x020 */
    UINT32_C(0x00000002), /* +0x024 */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xb8), /* +0x028 */
    UINT32_C(0x00000002), /* +0x02c */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xb0), /* +0x030 */
    UINT32_C(0x00000001), /* +0x034 */
    (uintptr_t)((const uint8_t *)lm_proc_table + 0xa8), /* +0x038 */
    UINT32_C(0x00000001), /* +0x03c */
    UINT32_C(0x00000000), /* +0x040 */
    UINT32_C(0x00000000), /* +0x044 */
    UINT32_C(0x0000101d), /* +0x048 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x04c */
    UINT32_C(0x0000105e), /* +0x050 */
    (uintptr_t)lm_proc_show_in_cause, /* +0x054 */
    UINT32_C(0x00001059), /* +0x058 */
    (uintptr_t)lm_proc_save_without_neck, /* +0x05c */
    UINT32_C(0x0000105a), /* +0x060 */
    (uintptr_t)lm_proc_reserve_to_boy, /* +0x064 */
    UINT32_C(0x0000101c), /* +0x068 */
    (uintptr_t)lm_proc_go_about_presentation, /* +0x06c */
    UINT32_C(0x0000103e), /* +0x070 */
    (uintptr_t)lm_proc_sell_of_gas, /* +0x074 */
    UINT32_C(0x0000103f), /* +0x078 */
    (uintptr_t)lm_proc_send_without_aside, /* +0x07c */
    UINT32_C(0x0000105d), /* +0x080 */
    (uintptr_t)lm_proc_sell_at_space, /* +0x084 */
    UINT32_C(0x0000105b), /* +0x088 */
    (uintptr_t)lm_proc_tell_in_film, /* +0x08c */
    UINT32_C(0x00001057), /* +0x090 */
    (uintptr_t)lm_proc_go_of_chain, /* +0x094 */
    UINT32_C(0x00001058), /* +0x098 */
    (uintptr_t)lm_proc_take_within_bonus, /* +0x09c */
    UINT32_C(0x00001056), /* +0x0a0 */
    (uintptr_t)lm_proc_cause_under_cell, /* +0x0a4 */
    UINT32_C(0x0000101d), /* +0x0a8 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0ac */
    UINT32_C(0x0000101d), /* +0x0b0 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0b4 */
    UINT32_C(0x0000101d), /* +0x0b8 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0bc */
    UINT32_C(0x0000100c), /* +0x0c0 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x0c4 */
    UINT32_C(0x0000101d), /* +0x0c8 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0cc */
    UINT32_C(0x0000100c), /* +0x0d0 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x0d4 */
    UINT32_C(0x0000101d), /* +0x0d8 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0dc */
    UINT32_C(0x00001004), /* +0x0e0 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x0e4 */
    UINT32_C(0x0000100c), /* +0x0e8 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x0ec */
    UINT32_C(0x0000101d), /* +0x0f0 */
    (uintptr_t)lm_proc_sell_by_coffee, /* +0x0f4 */
    UINT32_C(0x00001004), /* +0x0f8 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x0fc */
    UINT32_C(0x0000100c), /* +0x100 */
    (uintptr_t)lm_proc_do_on_kiss, /* +0x104 */
};
__asm__(".global lm_cause_by_subject\n.type lm_cause_by_subject,@object\n.pushsection .rodata.bb.tbl,\"a\",@progbits\n.balign 4\nlm_cause_by_subject:\n.popsection\n.size lm_cause_by_subject,8\n");
__asm__(".global lm_go_without_farmer\n.type lm_go_without_farmer,@object\n.set lm_go_without_farmer,lm_proc_table+8\n.size lm_go_without_farmer,64\n");
