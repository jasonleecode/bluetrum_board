# USB EP0 reconstruction notes

Source: `bare/lib/libhal.a:usb_device_enum.o`. All ten exported functions are
implemented in `bare/drivers/src/usb_enum_recovered.c`; the object also defines
the ten-byte, four-byte-aligned zero-initialized `usb_device_qualifier` object.
The descriptor contents are not populated by this member.

The RV32 `ude_0` layout is 40 bytes. Its first 24 bytes match the endpoint
control block: register base at 0, endpoint type/number at 4/5, DMA buffer at 8,
transmit cursor at 12, remaining count at 16 and max packet at 20. State,
address, configuration and mode are bytes 24–27; the flag word is at 28 and
the eight-byte SETUP request is at 32. Vendor code changes flag byte 31.
The global is supplied by the still-vendor `usb_device_run_loop.o`.

`ude_init(ude)` sets register base `0x200`, initial EP0 packet size 8,
`ep0_buf` and its DMA address at `0x314`, and selected/IN0/OUT0 endpoint slots.
It does **not** zero the full object, install descriptors, connect the bus,
or enable the CPU interrupt. `usb_device_init(mode)` in the run-loop member
zeros `ude_0`, zeros the nine-pointer `eps` array, writes mode, calls
`ude_init(&ude_0)`, and optionally starts the audio/HID components.

`ude_run_loop_execute(ude)` reads bus and endpoint interrupt status/masks.
Bus event bit 2 takes precedence and calls `os_mq_ude_reset_post(ude)`; bus
bit 0 sets flag byte 31 bit 0, while bit 1 sets bus interrupt enable to 4.
TX interrupt bit 0 posts `os_mq_ude_ctl_flow_post(ude)`. It reads RX status
and mask but performs no endpoint RX dispatch in this object.

`ude_control_flow()` selects EP0, applies pending address, handles CSR
conditions and EP0 transfer state, then restores the previous endpoint.
For SETUP, `get_setup(ude)` copies exactly eight bytes from the DMA buffer
and drains `regs[0x58/4]` words from `regs[0x80/4]`. The controller event hook
`os_mq_ude_ep0_setup_post(ude->setup)` receives a mutable eight-byte pointer;
a deferred consumer must copy it before returning. Only request types 0
(standard) and 1 (class) are posted. Types 2 and 3 are ignored by the original
state machine. The object contains **no** GET_DESCRIPTOR/SET_CONFIGURATION/
class-request dispatcher and is not a complete enumeration implementation.

`usbd_ep0_transmit(buffer, uint16_t length)` returns 1 after queuing a packet;
this does not mean the USB transfer has completed. It copies at most max
packet bytes into EP0 DMA storage and writes `0x10000 | count` to `0x308`.
A full final packet retains transmit state until a further control-flow
iteration changes CSR to 10 without copying more data. The pointer must stay
valid until all chunks are queued. A zero max packet with nonzero length
cannot progress; callers must not construct that invalid state.

`usbd_ep0_set_address()` has no argument: it takes the low byte of the saved
SETUP wValue, only while configuration is zero, sets packet size 64, and
defers address application via flag bit 25. Neither address range checking
nor correct request-stage selection is supplied by this function.
`usbd_ep0_send_status()` writes CSR 72. Vendor `usbd_ep_set_stall()` sets CSR
bit 5, but `usbd_ep_clear_stall()` clears CSR bit 2; these operations were
preserved exactly, not interpreted as symmetric software flags.

`usbd_ep_reset()` clears address/configuration/state, reinitializes peripheral
interrupt masks, optionally resets the interrupt endpoint when mode bit 2 is
set, and performs the original controller reset sequence with a 5 us delay.
It does not rebuild descriptors or reset every software state field.

The differential script executes original and recovered RV32 code with
synthetic MMIO, memcpy, endpoint-selection and OS-event hooks. It compares
ordered MMIO and external calls, all 40 state bytes, DMA buffer contents,
endpoint slots and transmit return values across 387 cases: mode/reset,
packet boundaries including zero/maximum lengths, deferred addressing,
all CSR low-bit/state combinations, request types, FIFO draining, interrupt
priority/masks, random registers, and multipacket progress. Vendor save
instructions execute normally; unsupported custom restore instructions use
an ABI shim. USB bus timing, physical DMA behavior, actual enumeration,
interrupt wiring and host compatibility remain outside this test model.
