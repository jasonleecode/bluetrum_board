#include "dac_output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t registers[256], writes, fifo[16], fifo_count, available;
static uint32_t delays, prepares, releases;
static unsigned prepare_mode;
static void *persistent_buffer;

uint32_t dac_output_test_read(uint32_t address)
{
    assert(address < sizeof(registers) && !(address & 3u));
    if (address == 0x108)
        return (registers[address / 4] & ~(1u << 8)) |
               (available ? 0 : 1u << 8);
    return registers[address / 4];
}
void dac_output_test_write(uint32_t address, uint32_t value)
{
    assert(address < sizeof(registers) && !(address & 3u));
    ++writes;
    if (address == 0x104) {
        assert(available && fifo_count < 16);
        --available;
        fifo[fifo_count++] = value;
    }
    registers[address / 4] = value;
}

#ifndef USE_DEFAULT_PLATFORM
static void delay_ms(uint32_t milliseconds)
{
    assert(milliseconds == 1);
    ++delays;
}
bool dac_output_platform_prepare(void *buffer, uint32_t bytes,
                                 struct dac_output_platform *platform)
{
    ++prepares;
    assert(buffer && !((uintptr_t)buffer & 3u) && bytes == 1024);
    for (uint32_t i = 0; i < bytes; ++i) assert(((uint8_t *)buffer)[i] == 0);
    if (persistent_buffer) assert(persistent_buffer == buffer);
    persistent_buffer = buffer;
    if (prepare_mode == 0) return false;
    platform->dma_address = prepare_mode == 2 ? 0x20001u :
                            prepare_mode == 4 ? 0xfffffe00u :
                            prepare_mode == 5 ? 0 :
                            prepare_mode == 6 ? 0x20000000u :
                            prepare_mode == 7 ? 0x2fc04u :
                            prepare_mode == 8 ? 0x10ffcu : 0x20000u;
    platform->delay_ms = prepare_mode == 3 ? NULL : delay_ms;
    return true;
}
void dac_output_platform_release(void) { ++releases; }
#endif

int main(void)
{
    static const int16_t samples[] = {0x1234, -2, -32768, 32767, 0, -1};
    assert(!dac_output_active());
    assert(dac_output_write(samples, 3) == 0);
    assert(!dac_output_mute(false) && !dac_output_set_volume(50));
    dac_output_stop();
    assert(!dac_output_init() && !writes && !delays && !releases);
#ifdef USE_DEFAULT_PLATFORM
    (void)prepare_mode; (void)persistent_buffer; (void)prepares;
    puts("PASS DAC: default platform refuses without MMIO");
    return 0;
#else
    for (prepare_mode = 2; prepare_mode <= 8; ++prepare_mode) {
        assert(!dac_output_init() && !writes && !delays);
        assert(releases == prepare_mode - 1);
    }
    prepare_mode = 1;
    registers[0x1fc / 4] = 0xffffffffu;
    assert(dac_output_init() && dac_output_active());
    assert(delays == 3);
    assert(registers[0x390 / 4] == 0x973ca);
    assert((registers[0x3a8 / 4] & 0xf0) == 0x90);
    assert(registers[0x110 / 4] == 0x8000ff);
    assert(registers[0x10c / 4] == 0x20000);
    assert((registers[0x140 / 4] & 0x5fd) == 0x441);
    assert(registers[0x140 / 4] & 2u);
    assert(registers[0x1fc / 4] == (0xffffffffu & ~0x7f7fu));
    assert(registers[0x1f4 / 4] & 8u);
    uint32_t before = prepares, before_writes = writes;
    assert(!dac_output_init() && prepares == before && writes == before_writes);
    assert(dac_output_write(NULL, 3) == 0);
    assert(dac_output_write(samples, UINT32_MAX) == 0);
    assert(dac_output_write(samples, 0) == 0);
    assert(dac_output_write(samples, 3) == 0); /* FIFO initially full. */
    available = 2;
    assert(dac_output_write(samples, 3) == 2);
    assert(fifo_count == 2 && fifo[0] == 0xfffe1234 && fifo[1] == 0x7fff8000);
    available = 1;
    assert(dac_output_write(samples + 4, 1) == 1 && fifo[2] == 0xffff0000);
    assert(dac_output_set_volume(0) && registers[0x144 / 4] == 0x20000);
    assert(dac_output_set_volume(100) && registers[0x144 / 4] == (0x20000 | 32700));
    before_writes = writes;
    assert(!dac_output_set_volume(101) && writes == before_writes);
    assert(dac_output_mute(false) && !(registers[0x1f4 / 4] & 8u));
    assert(dac_output_mute(true) && (registers[0x1f4 / 4] & 8u));
    dac_output_stop();
    assert(!dac_output_active() && releases == 8);
    assert(!(registers[0x140 / 4] & 0x401u));
    assert(!(registers[0x108 / 4] & 0x10u));
    before_writes = writes;
    dac_output_stop();
    assert(releases == 8 && writes == before_writes);
    assert(dac_output_write(samples, 1) == 0);
    memset(persistent_buffer, 0xa5, 1024);
    assert(dac_output_init()); /* Same RAM, cleared before the hook. */
    dac_output_stop();
    puts("PASS DAC: configuration, partial stereo writes, volume/mute, stop/restart");
    return 0;
#endif
}
