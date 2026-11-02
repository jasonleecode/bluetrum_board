/* libbtctrl.a:bb.o recovered entry points. Internal controller ABI only. */
#pragma once
#include <stdint.h>

void bthw_get_heap_info(void **heap, uint16_t **sizes, uint32_t *blocks);
void bb_go_about_way(void);
void bb_keep_within_error(uint32_t value);
void bthw_soft_isr(void);
void bthw_isr_do(void);
uint32_t bb_make_on_husband(void);
void bb_init(void);
void bb_reset(void);
void bb_off(void);
void bb_run_loop(void);
void bb_find_on_influence(void);
void bb_pass_under_iron(void);
uint8_t bb_sleep(void);
uint32_t bb_keep_within_opening(void);
void bb_fetch_in_event(uint32_t mask);
void bb_get_without_card(uint32_t mask);
void bb_give_on_enthusiasm(void);
uint32_t bb_is_sleep(void);
void bb_get_at_clue(void);
extern uint8_t bb_get_without_vehicle[1666];
