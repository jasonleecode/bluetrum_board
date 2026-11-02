#ifndef BT_RF_RECOVERED_H
#define BT_RF_RECOVERED_H
#include <stdint.h>
/* Original controller ABI, not a GAP/GATT application interface. */
uint32_t bt_get_hvbko(uint32_t kind);
uint32_t bt_get_packet(uint32_t kind);
void bb_rf_pass_on_water(uint8_t *out);
uint32_t bb_rf_cause_under_truck(uint32_t address);
void bb_rf_tell_at_entry(uint32_t address, uint32_t value);
uint16_t bb_rf_fetch_within_fix(uint32_t value, uint32_t gain);
uint32_t bb_rf_reserve_at_world(uint32_t index);
uint32_t bb_rf_find_inside_human(uint32_t index);
uint32_t bb_rf_give_by_picture(uint32_t index, uint32_t mode);
uint32_t bb_rf_make_of_position(void);
void bb_rf_go_inside_log(void);
uint8_t bb_rf_set_on_inflation(uint32_t level);
void bb_rf_reserve_into_mud(uint32_t on);
void bb_rf_get_with_leadership(const uint8_t *profile);
void bb_set_about_agent(void);
void bb_rf_send_by_burn(uint32_t value);
uint32_t bb_rf_show_by_line(void);
uint32_t bb_rf_sell_by_patience(void);
void bb_rf_take_without_start(void);
void bb_rf_pass_without_consequence(uint32_t channel, uint32_t option);
void bb_rf_send_to_officer(void);
void bb_rf_get_at_application(uint32_t level);
void bb_rf_read_into_price(uint32_t channel);
void bb_show_with_stay(uint32_t *context);
void bb_pass_about_dependent(const uint32_t *context);
void bb_sell_about_bet(void);
void bb_give_into_radio(void);
void bb_make_into_mother(void);
#endif
