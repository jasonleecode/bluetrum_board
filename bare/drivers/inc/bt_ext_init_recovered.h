#ifndef BT_EXT_INIT_RECOVERED_H
#define BT_EXT_INIT_RECOVERED_H

#include <stdint.h>

/* Preserve the binary ABI. Only the word offsets used by ext_init.o are
 * understood; this is not a recovered definition of the complete context.
 */
extern uint32_t ext_init_reserve_on_test[9];
extern uint8_t ext_init_do_of_weather;

void bb_write_without_passenger(void);
void bt21_set_on_tennis(void);
void ext_init_reserve_to_word(uint32_t clock);
void ext_init_pick_of_edge(void);
void ext_init_pass_within_equivalent(uint32_t disable);

#endif
