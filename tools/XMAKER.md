# Xmaker / DCF Format Notes

This note records the current analysis of the Bluetrum `riscv32-elf-xmaker`
flow used before serial downloading.

## Scope

The compiler-produced `.bin` is not the serial downloader image. The vendor
flow uses `riscv32-elf-xmaker.exe` to combine a fixed 4096-byte `header.bin`
with the application `.bin` into a `.dcf` file. The Windows downloader then
uses that `.dcf` file for flashing over UART.

## XM Script Format

The `.xm` files are UTF-16LE text with a BOM.

Current scripts:

```text
tools/final_bin.xm:
make(dcf_buf, header.bin, user.bin);
save(dcf_buf, final_bin.dcf);

rtt/bsp/rtthread.xm:
make(dcf_buf, header.bin, rtthread.bin);
save(dcf_buf, rtthread.dcf);

tools/download.xm:
download
```

`make(dst, header, app)` creates an in-memory DCF buffer named `dst`.
`save(buf, file)` writes that buffer to disk. The `download` command is a
separate xmaker command and is expected to invoke the vendor downloader flow.

## Tool Files

- `tools/windows/riscv32-elf-xmaker.exe` and
  `rtt/bsp/tools/riscv32-elf-xmaker.exe` are identical.
- `tools/header.bin` and `rtt/bsp/header.bin` are identical.
- `tools/linux/*.c` and `tools/linux/Makefile` are currently empty placeholders,
  not a usable Linux implementation.

Useful checks:

```sh
file tools/final_bin.xm tools/windows/riscv32-elf-xmaker.exe tools/header.bin
shasum -a 256 tools/header.bin rtt/bsp/header.bin
shasum -a 256 tools/windows/riscv32-elf-xmaker.exe rtt/bsp/tools/riscv32-elf-xmaker.exe
```

## Xmaker Commands Found In The EXE

UTF-16 strings in `riscv32-elf-xmaker.exe` show these script commands:

- `save`
- `savehex`
- `loadbin`
- `savebin`
- `savever`
- `savercf`
- `saveasc`
- `makeres`
- `makeresdir`
- `makeresdef`
- `makecfgfile`
- `makecfgdef`
- `make`
- `download`

The CLI usage string is:

```text
Usage: %s [-b] file
```

So batch mode is:

```sh
riscv32-elf-xmaker.exe -b final_bin.xm
```

## DCF Structure Findings

The DCF output is not `header.bin + app.bin`. It has a DCF wrapper, segment
records, alignment, checksums, and xmaker-specific obfuscation.

Confirmed from disassembly:

- The output file starts with `DCF\0`.
- `make()` rejects a header that is not exactly 4096 bytes:
  `header size must be 4096 bytes`.
- Existing DCF-like buffers are identified with `XCFG`.
- The generated image embeds xmaker tags. Some tags are plain ASCII, some are
  stored with the high bit set on each byte.

Observed tags/constants:

| Stored bytes | Decoded tag | Notes |
| --- | --- | --- |
| `44 43 46 00` | `DCF\0` | top-level output header |
| `58 43 46 47` | `XCFG` | existing config/header marker |
| `58 46 49 4c` | `XFIL` | file/block marker |
| `58 41 50 50` | `XAPP` | app marker |
| `4c 56 4d 47` | `LVMG` | header/body marker used in keying |
| `d8 c5 c1 c4` | `XEAD` after XOR `0x80` on high-bit bytes | header-related record |
| `d8 d2 c5 d3` | `XRES` after XOR `0x80` | resource record |
| `d8 c3 cf c4` | `XCOD` after XOR `0x80` | code record |
| `cb c5 d9 00` | `KEY\0` after XOR `0x80` on high-bit bytes | key metadata |
| `c4 c1 d4 c1` | `DATA` after XOR `0x80` | data record |
| `c4 c1 d4 c1` / `c4 d4 c1 00` | `DATA` / `DTA\0` | data metadata variants |

The generated body is aligned in several places:

- The fixed `header.bin` input is 4096 bytes.
- The first internal image area is rounded to an 8192-byte boundary.
- User/app payload handling uses 4096-byte and 512-byte alignment paths.

Checksum/key paths identified:

- CRC16-style table at `0x43dc78`.
- A 32-bit table at `0x43de78`.
- Per-block XOR/keying paths use tags such as `XFIL`, `XAPP`, and `LVMG`.
- One app payload path processes 512-byte blocks and mutates bytes with a
  rolling value derived from the 32-bit table.

The current analysis is enough to say that a compatible Linux implementation is
not a plain concatenation tool. It must reproduce the DCF record layout,
alignment, CRC fields, and block obfuscation.

## Practical Build Flow

For bare-metal builds, prepare the chosen app image as `tools/user.bin`, then
run the vendor xmaker on Windows or a compatible Windows runtime:

```bat
cd tools
riscv32-elf-xmaker.exe -b final_bin.xm
```

This should produce:

```text
tools/final_bin.dcf
```

Then use the vendor `Downloader.exe` UART tool against `final_bin.dcf`.

On macOS in the current workspace, Wine/CrossOver is not available from the
shell, so `riscv32-elf-xmaker.exe` could not be executed locally for byte-level
output comparison.

## Mac-Side Research Tooling

`tools/xmaker_re.py` is a Mac/Linux helper for the parts of xmaker that are
already confirmed from the Windows binary. It intentionally does not claim to
be a full `.dcf` generator yet.

Examples:

```sh
# Decode a UTF-16LE .xm script.
python3 tools/xmaker_re.py xm tools/final_bin.xm

# Extract the CRC/XOR tables from the Windows xmaker executable.
python3 tools/xmaker_re.py tables

# Compute the xmaker CRC16 over a file or range.
python3 tools/xmaker_re.py crc16 tools/header.bin --offset 0 --length 4096

# Apply the rolling XOR stream used by xmaker. Applying it twice with the same
# key restores the original data.
python3 tools/xmaker_re.py xor tools/header.bin /tmp/header.xfil --key 0x4c494658
python3 tools/xmaker_re.py xor /tmp/header.xfil /tmp/header.roundtrip --key 0x4c494658
cmp tools/header.bin /tmp/header.roundtrip

# Build the currently confirmed internal body image. This is useful for reverse
# engineering and validation, but it is not the final flashable .dcf wrapper.
python3 tools/xmaker_re.py make-body tools/header.bin bare/build/main.bin /tmp/main.xbody
python3 tools/xmaker_re.py inspect-body /tmp/main.xbody

# Apply the recovered internal body/app XOR passes as well. This is closer to
# xmaker's internal body before the outer DCF wrapper is emitted.
python3 tools/xmaker_re.py make-body tools/header.bin bare/build/main.bin /tmp/main.xbody.enc --encrypt
python3 tools/xmaker_re.py inspect-body /tmp/main.xbody.enc --encrypted
```

Current verified results on macOS:

```text
crc16_table_matches_ccitt=True
crc16_first16=0000 1021 2042 3063 4084 50a5 60c6 70e7 8108 9129 a14a b16b c18c d1ad e1ce f1ef
xor32_first8=00000000 01460000 028c0000 03ca0000 05180000 045e0000 07940000 06d20000
crc16(tools/header.bin)=b539
crc16(bare/build/main.bin)=ed23
```

Current `make-body` result for `bare/build/main.bin`:

```text
size=10752
record_offset=0x2000
tag_raw=d8c3cfc4 tag=XCOD
record_overhead=512 expected=512 ok=True
payload_size=2048 block_count=4 encrypted=False
record_crc=stored=0xe7e1 computed=0xe7e1 ok=True
payload_offset=0x2200
block_crc_ok=True
```

The first 4096 bytes of that body decrypt back to `tools/header.bin` with the
`XFIL` key (`0x4c494658`), matching the recovered rolling XOR routine.

Current `make-body --encrypt` result for `bare/build/main.bin`:

```text
size=10752
record_offset=0x2000
tag_raw=d8c3cfc4 tag=XCOD
record_overhead=512 expected=512 ok=True
payload_size=2048 block_count=4 encrypted=True
record_crc=stored=0xe7e1 computed=0xe7e1 ok=True
payload_offset=0x2200
app_info_crc=0x04a8
block_crc_ok=True
```

The encrypted body path now reproduces these recovered passes:

- header import and `XFIL` rolling XOR over the first 4096 bytes
- `XCOD` record metadata and per-512-byte app CRC table
- app-info CRC at body offset `0x80`
- body header/header-block rolling XOR passes using `LVMG`
- app payload rolling XOR passes using `XAPP` and each block CRC

## Outer DCF Wrapper Findings

The final `DCF\0` file is emitted after the internal body has been prepared.
For the normal `header.bin + app.bin` path, the wrapper starts with:

| Offset | Meaning |
| --- | --- |
| `0x00` | `DCF\0` |
| `0x04` | total size field, derived from internal body size plus wrapper length |
| `0x08` | high-bit `XEAD` record |
| `0x10` | high-bit `INFO` record |
| later | high-bit `DEV`, `SEG`, and optional config/resource records |

The wrapper length is computed from the internal body size rounded to 1024-byte
pages plus optional app/resource/config records. This section is not yet emitted
by `xmaker_re.py` because several fields are still tied to xmaker's parsed
header/config context and need byte-for-byte validation against a vendor output.

## Next Reverse-Engineering Tasks

1. Continue translating the `make()` disassembly into a Mac-side implementation:
   outer `DCF\0` header records and any remaining optional `XRES`/config paths.
2. Add a parser/checker for generated `.dcf` buffers so the Mac implementation
   can validate its own record layout before hardware testing.
3. Reimplement only the confirmed `make()` and `save()` subset for Linux/macOS.
4. Validate the generated `.dcf` by flashing with the serial protocol/tool path.
