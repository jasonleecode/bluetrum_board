# Bluetooth controller initialization audit

This is a static audit of `bare/lib/libbtctrl.a`, not an on-board BLE result.
Names that are obfuscated are retained verbatim. Their English words are not
used to infer semantics. All object offsets below are **section-relative before
linker relaxation**. Symbol names, sizes, relocation calls, member SHA-256 values
and LE command descriptors are in [bt-init-audit.json](bt-init-audit.json).
The checked-in `../libbtctrl.objdump-dr.txt` supplies the instruction evidence.

Archive SHA-256:
`60cda98155de21b3feda330d494d38a79d61db6f279f750dc1b12e1c45c4ac9b`.

## Initialization observed in the binary

`bb.o:.text+0x62`, `bb_init`, size 272 bytes:

1. Clear byte 0 of `.btmem.bthw`, set bit 30 of MMIO `0x64`, and replace
   bits 10..11 there with `0x800`.
2. Call ROM `0x845c8(0x11000)`; write `0x11000` to MMIO `0x350`.
3. Zero 52 bytes of `.btmem.bb.sniff` (`bb_write_in_spend`).
4. Call ROM `0x84320(2, .btmem.bb.buf+0x14, 129)` and
   `0x84320(1, .btmem.bb.buf+0xa4, 4208)`.
5. Call ROM `0x84324(28, 2)` and `0x84324(72, 2)`; pass the returned
   pointers to ROM `0x84300(first, 7, second, 9)`.
6. Call, in order: `bb_do_on_ordinary`, `bb_make_into_mother`,
   `bb_do_at_review`, `bb_go_on_valuable`.
7. Twice clear bit 1 in `0x548` and set bit 1 in `0x544`; after the first
   pair set bit 0 in `0x3f8`, after the second pair set bit 1 in `0x3f8`.

There is **no call to `bthw_irq_init` or ROM `0x84140` inside `bb_init`**.
ROM routines other than `nsmem_cb_init` have no established public semantic name
in this audit. They must not be replaced with guessed allocators or no-op stubs.

The four direct subtrees are:

- `nanos.o:.text+0x8`, `bb_do_on_ordinary`: ROM `0x84390` receives
  `nanos_save_to_draft` and `nanos_write_inside_process`; ROM `0x8435c`
  receives `bb_find_to_afternoon`. This registers scheduler callbacks.
- `bb_rf.o:.text+0xbe8`, `bb_make_into_mother`: query optional
  `bb_rf_reserve_into_strategy()` data; if its first byte has bit 0 set,
  retain the address at returned pointer +2 and reset a platform byte;
  call `bb_give_into_radio`; fill fixed RAM `0x11020..0x11047` with
  0,2,...,78 and `0x11048..0x1106f` with 1,3,...,79.
- `bt21.o:.text+0`, `bb_do_at_review`: call `bt21_give_to_dust`,
  `bt21_find_on_you`, `bt21_save_under_dig`, `bt21_set_on_tennis`,
  `bt21_keep_within_grocery`, in that order.
- `ble.o:.text+0x22`, `bb_go_on_valuable`: call
  `ble_leave_about_television(0)`, `ble_write_about_black()`, then write
  zero to `0xf050`. `ble_write_about_black` calls `ble_pick_to_teacher`
  and `ble_take_for_screw(0)`.

`ext_init.o:.text+0x56`, `bt21_set_on_tennis`, uses fixed RAM including
`0x11644`, `0x11658`, `0x11190`, `0x111e8`, `0x11240`. It calls
`bt_get_local_bd_addr`, reverses the six returned bytes locally, and calls
`ext_init_cause_within_lift`. It initializes `0x11000..0x1101f` as 16
halfwords containing `0x6000`, and accesses the `0x8000` register block.
These are concrete reasons to preserve the vendor memory layout.

## Reset, scheduler and interrupt paths

`bb.o:.text+0x172`, `bb_reset`, size 116 bytes:

1. Save `read32(0x544) & 0x180`, then clear those bits in `0x544`.
2. Clear then set bit 2 in `0x3f8`.
3. Call ROM `0x845c8(0x11000)`; write `0x11000` to `0x350`.
4. Call ROM `0x84304`, `bb_tell_with_base`, `bb_take_about_press`,
   `bb_give_into_radio`, in order.
5. OR the saved `0x180` bits back into `0x544`.

`bb_take_about_press` is in `ble.o:.text+0x6c`; it masks the same `0x544`
bits around `bb_pick_under_strike`, `ble_leave_about_television(1)` and
`ble_sell_about_sun`. The latter calls `ble_make_of_distribution` then
`ble_take_for_screw(1)`. Reset is therefore more than re-running a small
register initializer.

`bb.o:.com_text.bb.loop+0`, `bb_run_loop`, is only 24 bytes. If bit 0 in
the first halfword of `.btmem.bb.sniff` is clear, it tail-calls ROM
`0x84350`; otherwise it returns. It is **not a recovered software BLE
scheduler**. `bthw_soft_isr` tail-calls ROM `0x84664`.

The `baseband_platform.o` path is explicit:

- `bthw_irq_init` at `.text+0x46` calls `bt_get_packet(255)`, configures
  `0x3a4`, and registers `baseband_platform_get_of_officer` as vector **1**.
  It reads the vector base from `0x54c`, conditionally writes a JAL at
  base + `0x1e0` to `bb_rf_take_without_start`, and enables bit 22 in
  `0x548` / `0x544`. Thus a valid writable vector table must pre-exist.
- `baseband_platform_get_of_officer` at `.com_text.irq+0x2` calls
  `os_interrupt_enter`, clears `0x3f8 & 3`, calls `bthw_thread_post`, then
  `os_interrupt_leave`. It does not process all radio interrupts itself.
- `bthw_isr_do_plus` at `.com_text.bthw+0` calls `bthw_isr_do`, then ORs
  `3` into `0x3f8`, re-enabling those two bits.
- `bthw_isr_do` at `bb.o:.com_text.bb.btisr+0xa` dispatches nonzero
  `0x8010` status to `bb_pass_in_airport`, and nonzero `0xf010` status to
  `bb_reserve_without_volume`. The BLE ISR processes bits through ROM
  entry points `0x84684`, `0x8467c`, `0x84680`, `0x8461c` and loops until
  status clears.

The event callback `nanos.o:.com_text.bb.nanos+0x20`,
`bb_find_to_afternoon`, calls `bthw_soft_kick` when its argument equals 6,
otherwise ROM `0x84344`; both paths then call `nanos_event_set_trigger`.
`nanos_save_to_draft` returns `(ROM(0x845b8)() >> 4) & 0xffff`.
`nanos_write_inside_process` updates timer-related registers `0x80e0`,
`0x800c`, `0x8018`. `platform_asm.o` defines `nanos_env = 0x104e4`.

A conservative integration order is: platform/memory/heap setup, IRQ
installation, `bb_init`; subsequently handle deferred radio work with
`bthw_isr_do_plus`, followed by pending soft work and scheduler polling.
Calling `bthw_soft_isr` then `bb_run_loop` matches the existing wrapper's
poll order. The complete SDK task/event scheduling policy is absent from the
checked-in source, so that particular combined loop is an integration proposal,
not a proven reconstruction of the original task implementation.

## Startup dependencies and defects found in the original bare wrapper

The SDK's `rtt/bsp/app/blehr_app.c` explicitly performs
`bthw_get_heap_info(&heap, &sizes, &block_count)` followed by
`nsmem_cb_init(heap, sizes, block_count)` at ROM **`0x84140`**, via
`INIT_BOARD_EXPORT(btctrl_mem_init)`, before its BLE thread calls `bb_init`.

`bthw_get_heap_info` returns the address of `.btmem.bb.buf+0`, the address
at +12, and the value **3**. These are respectively 12 bytes of pointer
metadata and 6 bytes of 16-bit size metadata; the output is not a single
large heap buffer with a byte count. Do not substitute application malloc.

`rtt/bsp/link.lds` reserves 10 KiB from **`0x11000`**, puts
`.btmem.bthw` first, then `.btmem*`; `.com_text*` executes from common RAM,
and flash starts at `0x10000000`. `bb.o` alone contributes a 1666-byte
`.btmem.bthw`, 4384-byte `.btmem.bb.buf`, and 52-byte `.btmem.bb.sniff`.
The remaining library objects have additional memory requirements.

The bare implementation inspected at the start of this audit had these gaps:

- `drv_bt_init` never initialized the ROM controller heap and returned true
  unconditionally after calling `bb_init`.
- `bare/link.ld` used FLASH `0` and RAM `0x20000000`, with no explicit
  `.btmem*`, `.com_text*` or fixed-ROM workspace reservations. Orphan placement
  is not a substitute for the observed `0x11000` contract.
- `bare/startup.S` did not install a real vector base, had a weak empty
  `SystemInit`, and returned from a simplified trap with an ordinary `jalr`.
- `vendor_shim.c` registered ISR pointers in an array but had an empty
  `interrupt_handler_c`; `bthw_thread_post`, `bthw_soft_kick` and
  `nanos_event_set_trigger` were empty.
- Calling `bthw_isr_do` alone omitted the deferred path's `0x3f8 |= 3`.
- `hal_get_ticks` advanced a counter on reads rather than measuring elapsed
  time. This is not a trustworthy board time base, although the controller
  itself also uses the separate ROM/hardware time source above.

The audit does not claim that RT-Thread's example alone supplies every missing
piece: NimBLE integration source and the complete scheduler glue must also be
present in the selected board build. The bare platform must explicitly reject
initialization until these dependencies are implemented and validated.

## Hardware RF recovery boundary

`bb_rf.o:.text+0x5ea`, `bb_give_into_radio`, is 1492 bytes and performs
many MMIO operations around `0x7c80..0x7dd0` and `0x8070/0x8074`, invokes
multiple internal RF helpers, and works with optional platform calibration
data. Its relocation call list is retained in the JSON. Initial fixed values
include `0x7c80 = 0xb8c4ec4e`, `0x7ccc = 0x0013b13b`,
`0x7d20 = 0x0444441f`. Register field meanings and calibration behavior
are not established just by these constants.

`ext_init.o:.text+0`, `bb_write_without_passenger`, waits without a timeout
for bits 31 then 30 in `0x8000` to clear. Other paths similarly depend on
real hardware status transitions. Static call extraction does not establish
RF lock, packet reception, timing or radio compliance. Retain this backend
until each recovered routine has meaningful execution-level comparison.

## BLE advertising, scanning and connection command routes

The binary's HCI OGF 8 descriptor table is
`hct.o:.rodata.bb.tbl+0xb0`, with 32 four-byte slots. Each populated
slot provides a little-endian internal message ID and declared parameter
length. `hct_send_command` passes through `hct_reserve_within_physics`
and posts messages through ROM `0x843a0/0x843a4/0x843a8`; it does not
directly call the manager function in the following table.

The manager table in `mgr_proc.o:.rodata.ble.tbl` supplies the corresponding
internal ID-to-handler relocation. The HCI command names below are standard
opcode labels; handler attribution is from binary table bytes/relocations.
The ROM dispatcher implementation is outside the archive.

| HCI opcode | Command | Bytes | Internal ID | `mgr_proc.o:.text` handler |
| --- | --- | ---: | --- | --- |
| `0x2005` | LE Set Random Address | 6 | `0x040d` | `mgr_proc_set_inside_cash +0x72a` |
| `0x2006` | LE Set Advertising Parameters | 15 | `0x040e` | `mgr_proc_show_under_diamond +0x6de` |
| `0x2008` | LE Set Advertising Data | 32 | `0x0410` | `mgr_proc_give_by_serve +0x636` |
| `0x2009` | LE Set Scan Response Data | 32 | `0x0411` | `mgr_proc_give_for_clothes +0x578` |
| `0x200a` | LE Set Advertising Enable | 1 | `0x0412` | `mgr_proc_fetch_into_dream +0x5c4` |
| `0x200b` | LE Set Scan Parameters | 7 | `0x0413` | `mgr_proc_go_without_purple +0x52c` |
| `0x200c` | LE Set Scan Enable | 2 | `0x0414` | `mgr_proc_leave_to_result +0x4ba` |
| `0x200d` | LE Create Connection | 25 | `0x0415` | `mgr_proc_send_on_muscle +0x466` |
| `0x200e` | LE Create Connection Cancel | 0 | `0x0416` | `mgr_proc_tell_within_string +0x9b8` |

The 32 bytes for advertising/scan-response data comprise the length byte and
the fixed 31-byte data field. These descriptor lengths are not evidence that
every malformed HCI command is rejected by the original controller.
The library lookup retains only `opcode & 0xff` for OCF although the HCI
field is wider; a wrapper must not let high OCF bits silently alias entries.

## Smallest reliable next integration step

Keep the hardware RF/link layer and required ROM entry points; recover and
test bounded HCI transport/dispatch routines first. Add an explicit platform
initialization contract that fails by default on the current bare skeleton.
On a board build with the vendor memory map, clock initialization, heap setup
and working deferred interrupts, first validate HCI Reset and Command Complete,
then public-address/features queries, then one legacy advertising configuration.
Wait for each command result before issuing the next command. Follow with
scan reports and finally connection/ACL traffic. No live result from any of
those stages is asserted in this audit.
