# AB32VG1 Static Library Analysis

This directory records the reverse-engineering notes for the vendor static
libraries used by the bare-metal AB32VG1 project.

The scope here is interoperability and debugging: identify symbols, ABI,
required platform hooks, probable function prototypes, and usable wrapper
interfaces. Do not use this work to bypass licensing, encryption, DRM, or other
vendor protection mechanisms.

## Libraries

- `bare/lib/libhal.a`
  - 23 object files.
  - RISC-V ELF32 relocatable objects.
  - ELF flags: RVC, soft-float ABI.
  - 179 exported global/weak data or code symbols.
  - Contains peripheral-facing code: audio ADC, FM RX, SPI flash, UART, USB
    device/audio/HID, cache, debug, delay, ROM printf aliases, and RISC-V
    save/restore helpers.

- `bare/lib/libbtctrl.a`
  - 32 object files.
  - RISC-V ELF32 relocatable objects.
  - ELF flags: RVC, soft-float ABI.
  - 323 exported global/weak data or code symbols.
  - Contains baseband, BLE link-layer/control, HCI transport, manager, RF,
    and platform hook code.
  - Many symbol names are obfuscated or machine-renamed. Treat this library as
    a second phase after the HAL integration points are stable.

## Generated Artifacts

Generated files live under `generated/`:

- `libhal.nm.txt`
- `libbtctrl.nm.txt`
- `libhal.readelf-h.txt`
- `libbtctrl.readelf-h.txt`
- `libhal.objdump-dr.txt`
- `libbtctrl.objdump-dr.txt`

They were generated with the Homebrew RISC-V toolchain:

```sh
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-nm -g bare/lib/libhal.a
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-nm -g bare/lib/libbtctrl.a
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-readelf -h bare/lib/libhal.a
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-readelf -h bare/lib/libbtctrl.a
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-objdump -dr bare/lib/libhal.a
/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-objdump -dr bare/lib/libbtctrl.a
```

The macOS/Xcode `objdump` can identify these files as `elf32-littleriscv`,
but it cannot disassemble them. Use `riscv64-elf-objdump`.

## ABI Notes

- Object format: `elf32-littleriscv`.
- Architecture: `riscv32`.
- ISA flag: compressed instructions enabled (`RVC`).
- ABI flag: soft-float.
- The `save-restore.o` object in `libhal.a` provides `__riscv_save_*` and
  `__riscv_restore_*`, so vendor objects were likely built with GCC
  `-msave-restore`.

## HAL Surface

`libhal.a` is the most immediately useful library. Exported symbols group into
these functional areas:

- ADC/audio input:
  - `sdadc_set_channel`
  - `sdadc_set_sample_rate`
  - `sdadc_set_gain`
  - `sdadc_set_dma_samples`
  - `sdadc_get_channel`
  - `sdadc_get_sample_rate`
  - `sdadc_get_gain`
  - `sdadc_get_dma_samples`
  - `sdadc_start`
  - `sdadc_exit`
  - `sdadc_dma_notice`
  - `sdadc_isr`
  - `set_mic_analog_gain`
  - `set_aux_analog_gain`
  - `aux_analog_channel_select`
  - `aux_analog_channel_close`
  - `sdadc_analog_mic_start`
  - `sdadc_analog_mic_exit`

- FM RX:
  - `fmrx_analog_init`
  - `fmrx_set_pll`
  - `fmrx_set_rf_cap`
  - `fmrx_power_on`
  - `fmrx_power_off`
  - `fmrx_digital_start`
  - `fmrx_digital_stop`
  - `fmrx_dma_to_aubuf`
  - `fmrx_get_audio_data`
  - `fmrx_dac_sync`

- SPI flash/cache:
  - `spiflash_read`
  - `os_spiflash_read`
  - `os_spiflash_program`
  - `os_spiflash_erase`
  - `os_cache_init`
  - `os_cache_setfunc`
  - `load_cache`
  - `load_cache_execute`
  - `load_bank`
  - `search_bank`
  - `lock_cache`
  - `set_correct_line`
  - `isr_cache`

- UART:
  - `huart_init_do`
  - `huart_exit`
  - `huart_setbaudrate`
  - `huart_putchar`
  - `huart_getchar`
  - `huart_get_rxcnt`
  - `huart_rxfifo_clear`
  - `huart_if_rx_ovflow`
  - `huart_tx`
  - `huart_tx_done`

- USB device/audio/HID:
  - `usb_init`
  - `usb_disable`
  - `usb_interrupt_disable`
  - `usb_isr`
  - `usb_device_init`
  - `usb_device_enter`
  - `usb_device_exit`
  - `usb_device_hid_send`
  - `uda_init`
  - `uda_run_loop_execute`
  - `uda_set_spk_volume`
  - `uda_get_spk_volume`
  - `uda_set_spk_mute`
  - `uda_get_spk_mute`
  - `usb_isoc_reset`
  - `usb_ep_init`
  - `usb_ep_transfer`
  - `usb_ep_start_transfer`
  - `usb_ep_do_transfer`
  - `usb_ep_reset`
  - `usb_ep_halt`
  - `usb_ep_clear`
  - `usb_set_cur_ep`
  - `udh_init`
  - `ude_init`
  - `ude_run_loop_execute`

## Required Platform Hooks

Some vendor objects reference functions that must be provided by the board or
bare-metal support layer.

Known hooks from `libhal.a`:

- Timing/logging:
  - `hal_mdelay`
  - `hal_get_ticks`
  - `hal_printf`
  - `my_printf`
  - `my_print_r`

- Interrupt/OS shims:
  - `register_isr`
  - `os_interrupt_enter`
  - `os_interrupt_leave`
  - `rt_thread_self`
  - `os_get_interrupt_nest`

- Cache/flash locks:
  - `os_spiflash_lock`
  - `os_spiflash_unlock`
  - `os_cache_lock`
  - `os_cache_unlock`

- USB event shims:
  - `os_mq_ude_ctl_flow_post`
  - `os_mq_ude_ep0_setup_post`
  - `os_mq_ude_reset_post`

`rtt/bsp/board/board.c` already provides RT-Thread implementations for many of
these hooks. For the bare-metal build, equivalent no-RTOS shims should be added
instead of pulling in RT-Thread.

Known hooks from `libbtctrl.a`:

- `bt_get_local_bd_addr`
- `bthw_soft_kick`
- `bthw_thread_post`
- `nanos_event_set_trigger`
- `hci_host_recv_packet`
- `register_isr`
- `os_interrupt_enter`
- `os_interrupt_leave`
- `my_printf`

Bluetooth work should start by implementing or identifying these platform hooks,
then tracing the init/run-loop path around `bb_init`, `bb_run_loop`,
`bthw_isr_do`, `hct_send_command`, and `hct_acl_segment`.

## Current Wrapper Status

The bare tree now has a no-RTOS vendor shim at
`bare/drivers/src/vendor_shim.c`. It provides the currently required platform
hooks for linking `libhal.a` and the first-pass `libbtctrl.a` entry points:
timing, logging no-ops, interrupt/OS stubs, cache/flash locks, USB event posts,
minimal memory routines, local BT address, and the USB audio `ep2_isoc`
endpoint work area.

Existing bare wrappers cover part of `libhal.a`:

- `bare/drivers/src/drv_adc.c` wraps the `sdadc_*` family.
- `bare/drivers/src/drv_fm.c` wraps the `fmrx_*` family.
- `bare/drivers/src/drv_spiflash.c` wraps `os_spiflash_*`.
- `bare/drivers/src/drv_uart.c` wraps `huart_*`.
- `bare/drivers/src/drv_usb.c` and `drv_usb_audio.c` wrap USB/audio symbols.

The FM and ADC wrappers were reconciled against disassembly for the first
usable pass:

- `fmrx_dac_sync` is exposed as `void fmrx_dac_sync(uint32_t samples_or_words)`.
- `fmrx_dma_to_aubuf` is treated as an enable/disable path, not a data-copy API.
- `fmrx_get_audio_data(buf, len)` is treated as starting a capture, not returning
  a synchronous sample count.
- `sdadc_get_dma_samples()` is treated as a configured DMA sample-count getter,
  not as a buffer read.
- `sdadc_dma_notice(event)` is implemented as a strong wrapper that forwards to
  an application callback.

The older synchronous helper APIs remain only as compatibility stubs:
`drv_adc_read()` and `drv_fm_get_audio()` return `0` because the current
`libhal.a` symbols do not provide synchronous buffer reads.

The RT-Thread FM sample calls:

```c
fmrx_power_on(0);
fmrx_dma_to_aubuf(RT_TRUE);
fmrx_dac_sync(buf_size / 4);
```

The current bare wrapper declares:

```c
extern void fmrx_power_on(void);
extern void fmrx_dac_sync(uint32_t samples_or_words);
extern void fmrx_dma_to_aubuf(uint32_t enable);
```

Hardware testing is still required before the FM wrapper should be considered
stable.

### FM Disassembly Notes

Initial RISC-V disassembly gives stronger evidence for the FM prototypes:

- `fmrx_power_on`
  - Does not read `a0`.
  - The RT-Thread sample passes `0`, but the current object ignores it.
  - Treat as `void fmrx_power_on(void)` unless another vendor object expects a
    non-void ABI for compatibility.

- `fmrx_dac_sync`
  - Reads `a0`, divides it by 3, and compares the result against an audio
    buffer/status register.
  - Should not be declared as `void fmrx_dac_sync(void)`.
  - Probable prototype: `void fmrx_dac_sync(uint32_t samples_or_words)`.

- `fmrx_dma_to_aubuf`
  - Only tests whether `a0` is zero.
  - Does not use `a1`.
  - Probable prototype: `void fmrx_dma_to_aubuf(uint8_t enable)` or
    `void fmrx_dma_to_aubuf(bool enable)`.

- `fmrx_get_audio_data`
  - Stores `a0` to an FM buffer address register.
  - Stores `a1 - 1` to a length register.
  - Sets an enable/start register.
  - Does not compute or return a sample count.
  - Probable prototype: `void fmrx_get_audio_data(void *buf, uint32_t len)`.

`bare/drivers/src/drv_fm.c` now treats this as an asynchronous start path. The
remaining open work is to identify the completion/status path for direct
buffer capture. For streaming, the known usable route is:

- FM-to-audio-buffer DMA, matching the RT-Thread sample.
- Periodic `fmrx_dac_sync(samples_or_words)` to keep the audio buffer/DAC path
  in sync.

### ADC Disassembly Notes

Initial `sdadc.o` disassembly also shows a wrapper mismatch:

- `sdadc_set_dma_samples`
  - Stores `a0` into a 16-bit config field at `.LANCHOR0 + 4`.
  - Probable prototype: `int sdadc_set_dma_samples(uint16_t samples)`.

- `sdadc_get_dma_samples`
  - Loads the 16-bit config field at `.LANCHOR0 + 4` and returns it in `a0`.
  - Does not read from a DMA buffer.
  - Does not use any incoming buffer pointer.
  - Probable prototype: `uint16_t sdadc_get_dma_samples(void)`.

The original bare wrapper declared:

```c
extern uint32_t sdadc_get_dma_samples(int16_t *buf, uint32_t max);
```

That was incorrect. The current wrapper exposes `drv_adc_set_dma_samples()`,
`drv_adc_get_dma_samples()`, and `drv_adc_set_dma_notice_callback()`. A real
bare ADC read path still needs the actual DMA buffer symbol/register path from
the ADC ISR.

## Build Smoke Test

The bare build currently links all app targets with Homebrew's split RISC-V GCC
and binutils packages:

```sh
cd bare
make CROSS_COMPILE=/opt/homebrew/opt/riscv64-elf-gcc/bin/riscv64-elf- \
     BINUTILS_PREFIX=/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-
```

Current generated targets:

- `build/main.elf` / `build/main.bin`
- `build/mic_read.elf` / `build/mic_read.bin`
- `build/usb_fm.elf` / `build/usb_fm.bin`

The startup path now calls `main` directly, so every `app/*.c` target has the
same bare-metal entry contract.

## Immediate Next Tasks

1. Flash `build/main.bin` first and confirm FM RX audio-buffer routing produces
   expected hardware behavior.

2. Flash `build/mic_read.bin` and verify `sdadc_dma_notice(event)` fires. Then
   trace the real DMA data buffer path.

3. Flash `build/usb_fm.bin` and verify USB enumeration/audio behavior. If it
   enumerates but does not stream, continue disassembling `uda_init`,
   `usb_ep_transfer`, and the endpoint descriptor tables.

4. Add one minimal smoke target each for UART TX and SPI flash read.

5. Only after HAL is stable, begin `libbtctrl.a`:
   identify the platform hooks, classify the HCI/baseband entry points, and
   create a minimal init/run-loop harness.
