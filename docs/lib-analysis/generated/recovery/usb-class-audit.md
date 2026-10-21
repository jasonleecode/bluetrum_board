# USB class / lifecycle audit

Evidence is the supplied `bare/lib/libhal.a`, not a generic USB stack. Exact
object hashes, exported and undefined symbols, and descriptor bytes are in
[`usb-class-audit.json`](usb-class-audit.json); relocation-bearing disassembly
is in [`usb-class.llvm.txt`](usb-class.llvm.txt). The reproducible extraction
and HID differential test is `docs/lib-analysis/test_usb_class_recovery.py`.

## Scope and result

| Object | Result |
| --- | --- |
| `usb_device_hid.o` | All five functions recovered in `bare/drivers/src/usb_class_recovered.c`; 103 original-versus-recovered RV32 cases pass. |
| `usb_device_run_loop.o` | All five exported functions and its global context allocations traced; vendor object remains in the build. |
| `usb_device_audio.o` | All 14 functions inspected, including seven weak callbacks; retained as vendor code. Several callbacks and the audio run loop do nothing. |
| `usb_device_table.o` | All 16 descriptor/string arrays extracted exactly; these are templates requiring an external control-request implementation. |

HID tests compare endpoint-call order, context/buffer contents at each external
call, final RAM, and callee-saved registers. They cover volatile countdown reads,
initialization with both the global and a separate context, random starting
state, transfer completion, countdown expiry, and a nonreturning busy transfer
without timer progress. Original save helpers execute; unsupported vendor
restore custom instructions are replaced by an ABI shim. Endpoint calls and
`memset` are hooks. This does not validate USB signalling, DMA, timer cadence,
host enumeration or interrupt entry on silicon.

## Descriptor contents

The weak device descriptor is USB 1.10 (`bcdUSB=0x0110`), EP0 maximum packet 64,
VID `0x8087`, PID `0x1024`, one configuration. The product string says
`USB2.0 Device`; that string is not evidence of a USB 2.0 controller or high-speed
support. The weak strings are manufacturer `Generic`, product `USB2.0 Device`,
and serial `20170726905923`. These are vendor defaults, not a newly allocated
product identity.

| Template | Direction / endpoint | Descriptor transfer / maximum packet | Meaning |
| --- | --- | --- | --- |
| HID | IN `0x83` | Interrupt, 16 bytes, interval 1 | Consumer control; **one byte** per input report, no report ID |
| Speaker stream | OUT `0x02` | Isochronous, 192 bytes, interval 1 | 48 kHz, two channels, 16-bit samples |
| Microphone stream | IN `0x82` | Isochronous, 192 bytes, interval 1 | 48 kHz, one- or two-channel variants, 16-bit samples |
| Storage | OUT `0x01`, IN `0x81` | Bulk, 64 bytes | Mass storage interface template; no storage worker recovered here |

No CDC descriptor is present in this object. Its presence elsewhere in the
RT-Thread source tree does not provide a working controller port for this HAL.

The 33-byte HID report descriptor declares Consumer usage page `0x0c`, usage
`0x01`, logical range 0..1, report size 1 and count 8. Bits from least to most
significant map to usages `b3`, `b5`, `b6`, `b7`, `cd`, `e2`, `e9`, `ea`:
Fast Forward, Scan Next Track, Scan Previous Track, Stop, Play/Pause, Mute,
Volume Increment, Volume Decrement. It does not declare an output report or
an arbitrary bidirectional data protocol. The 16-byte endpoint maximum is not
the report length.

`desc_config_general` initially encodes total length 158 and five interfaces.
Other templates retain their original interface indices (storage 0, audio
control 1, speaker 2, microphone 3, HID 4). Sending that header plus a subset
without fixing total length, interface count, and interface indices is invalid.
`usb_device_init(flags)` only selects initialization branches; it does not by
itself build or serve the final descriptor tree. A HID-only implementation
must construct a coherent configuration and handle the setup requests.

There are three distinct sizes to keep separate:

* `ep3_buf` is only eight bytes in `usb_device_run_loop.o`.
* HID endpoint descriptor maximum packet is 16 and its report is one byte.
* `epsize_tbl` is `01 01 04 10`; `udh_init` sets internal EP3 `max_packet` to
  `0x10 << 6 = 1024`. `uda_init` sets EP2 `max_packet` to `4 << 6 = 256` but
  clears only 192 bytes at the externally provided `ep2_isoc` pointer.

The internal fields are not proof of valid buffer capacity or host-authorized
report lengths. In particular, the eight-byte default HID buffer cannot
justify a 16- or 1024-byte transfer.

## Lifecycle and context ABI

`usb_device_run_loop.o` supplies these common symbols, aligned to four bytes:

| Symbol | RV32 size | Observed use |
| --- | --- | --- |
| `ude_0` | 40 | Enumeration/control context; mode byte at 27, flag word at 28 (modified byte at 31) |
| `uda_0` | 16 | Audio state; endpoint pointer at +4, volume/mute/level at +10/+11/+12 |
| `udh_0` | 8 | Endpoint pointer at +0, valid byte at +4, volatile countdown at +5 |
| `epcb2_rx` | 24 | Audio OUT endpoint state |
| `epcb3_tx` | 24 | HID IN endpoint state |

`usb_device_init(flags)` clears all 40 bytes of `ude_0` and the 36-byte `eps`
pointer array, saves the low byte of flags at `ude_0+27`, and calls
`ude_init(&ude_0)`. If flags `& 0x0a` is nonzero, it assigns
`uda_0.endpoint = &epcb2_rx` and calls `uda_init(&uda_0)`. If flags `& 4` is
nonzero, it assigns `udh_0.endpoint = &epcb3_tx` and calls `udh_init(&udh_0)`.
It finishes with a 32-bit write of `0x80` to register `0x53c`.

`usb_device_enter(flags)` performs the vendor controller register sequence,
calls `usbchk_only_device()`, writes `0x50000` to `0x304`, and then calls
`usb_device_init(flags)`. `usb_device_exit()` checks `0x30c & 1`; when set it
writes `0x80` to `0x538` and calls `usb_init()`. Exit does not itself demonstrate
that pending DMA has been cancelled or all application buffers can be freed.

The weak `usb_dev_isr()` is exactly `ude_run_loop_execute(&ude_0)`.
`ude_init`, `uda_init`, and `ude_run_loop_execute` require context pointers;
the old wrappers declared them as zero-argument functions and could therefore
dereference an uncontrolled address. Likewise, endpoint APIs take endpoint
state pointers, not numeric endpoint indices. These are actual ABI errors.

The established low-level sequence is controller initialization, a completed
platform/control-request port, then `usb_device_enter(selected_flags)`, with
the actual USB ISR feeding `usb_dev_isr()` and a real periodic timer feeding
`ude_tmr_isr()`. `usbchk_connect()` is a detection-state routine, not evidence
that interrupt dispatch or enumeration has been installed. The prior empty
`os_mq_ude_*_post` stubs cannot handle EP0 setup/control work. See the separate
enumeration and communication recovery for those paths.

## HID behavior

`udh_init(context)` zeroes the eight-byte `ep3_buf`, obtains the context's
endpoint pointer, sets register base `0x200`, buffer address, IN flag, endpoint
number 3, internal type 2, and `epsize_tbl[3] << 6` as maximum packet size.
It calls `usb_ep_init(endpoint)` and clears the supplied context's valid byte.
It does **not** clear its countdown byte.

`ude_hid_setvalid()` sets `udh_0.valid=1` and countdown to 10. The timer ISR
checks the volatile countdown, then reads it again before decrementing if
nonzero. `usb_int_ep_reset()` calls `usb_ep_reset(udh_0.endpoint)` and clears
valid, but leaves countdown unchanged.

`usbd_ep3_transmit(buffer, length)` has this observed sequence:

1. Select `udh_0.endpoint`, remembering the previous endpoint pointer.
2. Reload the endpoint pointer, assign its buffer, set HID countdown to 30,
   and call `usb_ep_start_transfer(endpoint, length)`.
3. Call `usb_ep_transfer(udh_0.endpoint)` at least once, stopping when it
   reports completion or the volatile countdown becomes zero.
4. Clear countdown and restore the previous endpoint selection.

The endpoint length ABI is 16-bit bytes. The helper provides no completion
result and does not cancel/reset a transfer on timeout. With no timer progress
and a permanently pending endpoint it loops forever. A caller must retain the
buffer while DMA may still use it; a timed-out call does not prove release.

`usb_device_hid_send(value)` returns 0 when valid is zero or countdown is
nonzero. Otherwise it calls the **undefined** `ude_hid_send(value)` followed by
`ude_hid_send(0)`, and returns 1. Its body forwards all of `a0` without a mask;
the original API's C integer width cannot be recovered from this body alone.
The supplied archive has no implementation or established ROM address for
`ude_hid_send`, and its only archive references are these two calls. A new
one-byte report adapter must be identified as new integration code, not as a
recovered ROM implementation or an established vendor key mapping.

## Audio behavior and remaining dependencies

`uda_init(context)` clears 192 bytes through the global **pointer** `ep2_isoc`,
sets the endpoint at context+4 to register base `0x200`, clears IN, sets number
2 and internal type 3, sets the internal maximum packet as above, supplies
the same buffer, and calls `usb_ep_init`. It then reads `ex_spk_vol()` and
`ude_get_sys_vol_level()`, clears context bytes +8, +9 and mute at +11, stores
the level at +12, and sets volume at +10 to `(volume * level - 1) & 255`.

| Function | Observed behavior |
| --- | --- |
| `uda_get_spk_mute(output)` | Writes the stored byte at `uda_0+11` |
| `uda_set_spk_mute(value)` | Stores the low byte at `uda_0+11`; does not call DAC hardware |
| `uda_get_spk_volume(request, output)` | For `0x81` uses current volume; `0x82/0x83/0x84` select the table bytes `00/ff/01`; high output byte is `0x80` except `0x84`, which gets zero |
| `uda_set_spk_volume(value)` | Stores low byte at +10, divides `(low_byte + 1)` by level at +12, and passes quotient low byte to `ude_set_sys_volume` |
| `usb_isoc_reset()` | Calls `usb_ep_reset` using endpoint pointer at `uda_0+4` |
| `uda_run_loop_execute()` | Returns immediately |
| Weak `ex_spk_vol()` | Tail-calls `ude_get_sys_volume()` |
| Weak `ude_get_sys_volume()`, `ude_get_sys_vol_level()` | Return zero |
| Weak `ude_set_sys_volume`, `ude_mic_start`, `ude_mic_stop`, `ude_micdnr_process` | Return immediately |

For request values outside the documented GET selector set, the original
`uda_get_spk_volume` indexes outside its three-byte table; no safe general
request contract follows from that code. Default weak getters yield level
zero and initial stored volume 255. RISC-V signed DIV by zero returns -1,
whose low byte is 255; rewriting that operation as unchecked C division would
introduce undefined behavior. No audio implementation was added in this audit.

The microphone descriptors do not establish an implemented microphone input
path: initialization here only sets up `epcb2_rx`, and the default microphone
callbacks are empty. Sending FM samples on numeric EP2 is not a recovered
audio uplink. Audio clock/ring buffers, alternate settings, actual mixer
callbacks, microphone DMA, and host playback/capture remain unverified.

The four audited objects make named relocatable calls, including endpoint and
enumeration helpers, `memset`, callbacks and vendor save/restore helpers. No
absolute ROM entry point occurs in their class/lifecycle bodies. This is a
narrow observation, not proof that the entire SDK or USB platform has no ROM
or hardware dependency. The missing `ude_hid_send`, timer and interrupt
dispatch, EP0 setup worker, DMA memory, clock setup and physical connection
still require concrete integration and board validation.
