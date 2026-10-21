# audio.o analog input recovery

Source: `bare/lib/libhal.a:audio.o`; the object has six global `.text`
functions and one global, four-byte-aligned `.rodata` object
`mic_anl_table[24]`. Its only undefined symbols are compiler save/restore
helpers. There are no DAC playback or I2S exports in this member.

| Function | Recovered ABI / behavior |
| --- | --- |
| `set_aux_analog_gain` | `(uint32_t gain, uint32_t channels)`, no defined return; clamp to 4, update AUANGCON1 at `0x1f4`, low/high channel nibble enables gain fields at bits 21 / 24 |
| `set_mic_analog_gain` | Same parameter order, clamp to 23; lookup table `0..11,18..29`; write five-bit gain fields at `0x1e0` bits 5 / 21 |
| `aux_analog_channel_select` | `(uint32_t channels)`, no defined return; independent low/high-nibble routing; selectors 1..8 have observed cases; 6/7 also set MIC gain to 2 |
| `aux_analog_channel_close` | Same single argument; any nonzero nibble clears that bank's general AUX routing mask, selectors 6/7 additionally disable the corresponding MIC route |
| `sdadc_analog_mic_start` | `(uint32_t channels, uint32_t analog_gain)`, no defined return; shared analog bias setup followed by bank selection, then `set_mic_analog_gain(analog_gain, channels)` |
| `sdadc_analog_mic_exit` | `(uint32_t channels)`, no defined return; clears selected MIC routing/enables and sets power-down bits |

`sdadc.o` supplies encoded channel values. MIC selectors are low nibble
`0x0c` / `0x0d`, or high nibble `0xc0` / `0xd0`; exact physical pin mapping
has not been established here. A nonzero unexpected MIC nibble still enables
the corresponding bank in `audio.o`, so a public wrapper must validate its
own accepted inputs. `aux_analog_channel_select()` only ORs routing bits;
switching routes does not automatically clear the preceding selection.
`sdadc_analog_mic_exit()` does not undo all shared bias registers written by
start. The reconstruction preserves these behaviors and separate MMIO RMWs.

`sdadc.o` also calls `sdadc_analog_aux_start/exit`, which are **not** exported
by `audio.o`. `aux_analog_channel_select/close` are not proven ABI-equivalent
substitutes: they lack the full analog startup/exit sequence. Do not invent
aliases merely to satisfy the linker.

## Public DAC evidence

`rtt/bsp/board/ports/audio/drv_sound.c` (Bluetrum, Apache-2.0) already contains
readable DAC/PLL/AUBUF register operations. In contrast, the similarly named
HAL DAC source only supplies an empty weak MSP hook. Relevant public source
entry points are `adpll_init`, `dac_start`, `saia_frequency_set`,
`saia_channels_set`, `saia_volume_set`, `sound_start`, `sound_transmit`,
and `audio_isr`.

The reference initializes a 1024-byte AUBUF ring, writes its DMA address,
sets `AUBUFSIZE=(1024/4-1)|((1024/8)<<16)`, enables DACDIGCON0 bits 0 and 10,
and feeds packed 32-bit words via AUBUFDATA. AUBUFCON bit 8 denotes full;
the RT-Thread path waits on a semaphore serviced by its interrupt handler.
The public `sound_stop` is empty and is not evidence of a working hardware
stop sequence. `DMA_ADR(x)` in `ab32vgx.h` is a cast, not a DMA allocator or
cache-coherency operation. Pin routing, DMA-visible memory, actual clock,
safe stopping and peripheral ownership remain board-level responsibilities.

## Validation boundary

`test_audio_recovery.py` compares 2938 cases between original RV32 machine
code and compiled recovered C, including every low-byte selector value,
gain clamping, all lookup entries, two seeded register patterns and combined
start/change/stop sequences. It compares complete ordered MMIO read/write
traces and final register state, checks callee-saved register restoration,
and asserts exported binding/sections/table size/alignment/content.

The original custom `__riscv_restore_1` is represented by an ABI shim; its
save counterpart executes unchanged. No ADC/DAC samples, audio quality,
DMA transfer, analog settling, pin mapping or real interrupt timing were
tested. The JSON includes source/header and original member hashes.
