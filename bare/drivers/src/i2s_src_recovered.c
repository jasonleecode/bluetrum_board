/* SPDX-License-Identifier: Apache-2.0
 * Recovered from AB53XX SDK libplatform.a:i2s.o, SRC/master/16-bit path.
 * Original i2s_init configuration byte 0xa8 | mapping, second byte zero.
 * Provenance, differential validation and porting limits: I2S_RECOVERY.md.
 */
#include "i2s_src.h"
#define I2S_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

int i2s_src_init(uint32_t mapping)
{
    uint32_t port;
    if (mapping > 1) return -1;
    port = mapping ? 0x680u : 0x600u;
    I2S_REG(0x024) = 3;
    I2S_REG(0x024) = mapping + 1;
    I2S_REG(port + 0x10) |= 0x1e;
    I2S_REG(port + 0x14) |= 0x1e;
    I2S_REG(port + 0x0c) = (I2S_REG(port + 0x0c) & ~0x1fu) | 1;
    I2S_REG(0x140) |= 0x800000;
    I2S_REG(0x4d8) = 0;
    /* tbl_i2s_16bit_clk[2] = {1, 7}; divider packed as low | high << 7. */
    I2S_REG(0x4dc) = 1u | (7u << 7);
    I2S_REG(0x4d8) = 0x609;
    return 0;
}

void i2s_src_exit(uint32_t mapping)
{
    uint32_t port;
    if (mapping > 1) return;
    port = mapping ? 0x680u : 0x600u;
    I2S_REG(port + 0x10) &= ~0x1eu;
    I2S_REG(port + 0x0c) = (I2S_REG(port + 0x0c) & ~0x1fu) | 1;
    I2S_REG(0x4d8) = 0;
}
