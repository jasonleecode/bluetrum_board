#ifndef BT_COMPLEX_RECOVERED_H
#define BT_COMPLEX_RECOVERED_H

#include <stdint.h>

/* Original RV32 ABI: two 32-bit components in a0/a1, second input in a2/a3.
 * Unsigned fields preserve the machine's modulo-2^32 arithmetic; division
 * interprets those bit patterns as signed values.
 */
typedef struct { uint32_t x, y; } rf_pair;

rf_pair complex_set_into_brave(rf_pair a, rf_pair b);
rf_pair bb_rf_fetch_without_dish(rf_pair a, rf_pair b);
rf_pair bb_rf_make_in_job(rf_pair a, rf_pair b);
rf_pair bb_rf_make_for_branch(rf_pair a, rf_pair b);

#endif
