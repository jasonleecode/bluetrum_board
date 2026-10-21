/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
/* Recovered AB5300-family SRC output subset; see I2S_RECOVERY.md.
 * Master, 16-bit stereo, normal I2S, MCLK=256fs. mapping: 0=PA, 1=PE.
 * Caller supplies the audio clock/source and peripheral gate. No DMA or ISR.
 * AB32VG1 compatibility inferred from register/pin correspondence, not tested
 * on silicon. Initialization rejects mappings outside the recovered pair.
 */
int i2s_src_init(uint32_t mapping);
void i2s_src_exit(uint32_t mapping);
