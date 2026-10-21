#!/usr/bin/env python3
"""Recover the bundled Downloader 1.9.7 .NET Reactor method resource offline.

Research-only dependencies: dnfile; dncil with --il; cryptography with --booleans.
The Linux downloader itself requires none of these packages. This script never
loads or runs the Windows EXE. Output is analysis data, not a runnable EXE.

Source: initializer MethodDef RID 9379, especially CalculateMagic IL
0x4bdb..0x4d38 and the final QWORD XOR at IL 0x54a9. KEY is the initializer's
32-byte key AFTER its IV mixing. Resource records are checked end-to-end.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXE_SHA256 = "7c625413a1d68c350b1425fb41e3cd6916d11e119aba3c5cf59102af78510880"
RESOURCE = "QhC0SXHShhliefNr3w.oN1ejNIXSAGZwdOcxS"
KEY = bytes.fromhex("78c4b445ddcc5660c397d4d3ab2568f7148dd8838814f8639155ee65c47c0cb0")
MASK = 0xffffffff


def magic(value):
    mixed = value ^ (value >> 9)
    mixed = (mixed + 0xad13e84f) & MASK
    mixed ^= mixed >> 25
    mixed = (mixed + 0x3ae0c632) & MASK
    mixed = (mixed ^ (mixed << 5)) & MASK
    mixed = (mixed + 0x9dc27481 + 0xc2ae6143) & MASK
    return (value + mixed) & MASK


def decode(encrypted):
    state = 0
    plain = bytearray()
    for offset in range(0, len(encrypted), 4):
        state = magic((state + int.from_bytes(KEY[offset % 32:offset % 32 + 4], "little")) & MASK)
        chunk = encrypted[offset:offset + 4]
        word = state ^ int.from_bytes(chunk, "little")
        plain.extend(word.to_bytes(4, "little")[:len(chunk)])
    for offset in range(0, len(plain) - 7, 8):
        word, = struct.unpack_from("<Q", plain, offset)
        struct.pack_into("<Q", plain, offset, word ^ 0x4304e66d)
    return plain


def recover(source, *, emit_il=False, emit_booleans=False):
    if hashlib.sha256(source).hexdigest() != EXE_SHA256:
        raise ValueError("EXE SHA-256 mismatch: this recovery is specific to the bundled Downloader.exe")
    import dnfile
    pe = dnfile.dnPE(data=source)
    resources = {str(resource.name): resource.data for resource in pe.net.resources}
    plain = decode(resources[RESOURCE])
    patch_count, mode = struct.unpack_from("<II", plain)
    if (patch_count, mode, len(plain)) != (2837, 0, 811945):
        raise ValueError("unexpected decrypted resource header")
    patched = bytearray(source)
    position = 8
    patches = []
    for _ in range(patch_count):
        rva, value = struct.unpack_from("<II", plain, position)
        position += 8
        struct.pack_into("<I", patched, pe.get_offset_from_rva(rva), value)
        patches.append([rva, value])
    rva_to_method = {}
    for rid, method in enumerate(pe.net.mdtables.MethodDef.rows, 1):
        if not method.Rva:
            continue
        offset = pe.get_offset_from_rva(method.Rva)
        if patched[offset] & 3 == 2:
            header_size = 1
        elif patched[offset] & 3 == 3:
            header_size = 4 * (patched[offset + 1] >> 4)
        else:
            continue
        rva_to_method[method.Rva + header_size] = rid
    count, = struct.unpack_from("<I", plain, position)
    position += 4
    if count != 8240:
        raise ValueError("unexpected method count")
    methods = {}
    il_files = {}
    for _ in range(count):
        rva, index, size = struct.unpack_from("<III", plain, position)
        position += 12
        code = plain[position:position + size]
        position += size
        if len(code) != size or index >= 0x70000000:
            raise ValueError("truncated/native method record")
        rid = rva_to_method[rva]
        if rid in methods:
            raise ValueError("duplicate method record")
        methods[rid] = {"rva": rva, "index": index, "code": code.hex(),
                        "name": str(pe.net.mdtables.MethodDef.rows[rid - 1].Name)}
        if emit_il:
            from dncil.cil.body.reader import read_method_body_from_bytes
            # Synthetic fat header standardizes displayed offsets to code+12;
            # it deliberately omits the original locals/EH metadata.
            body = read_method_body_from_bytes(struct.pack("<HHII", 0x3013, 64, size, 0) + code)
            lines = []
            for instruction in body.instructions:
                operand = instruction.operand
                annotation = ""
                if hasattr(operand, "value"):
                    table = pe.net.mdtables.tables.get(operand.value >> 24)
                    token_rid = operand.value & 0xffffff
                    if table and 0 < token_rid <= len(table.rows):
                        row = table.rows[token_rid - 1]
                        annotation = " " + str(getattr(row, "Name", getattr(row, "TypeName", "?")))
                lines.append(f"{instruction.offset:04x}: {instruction.opcode.name:16} {operand}{annotation}")
            il_files[f"{rid}.il"] = "\n".join(lines) + "\n"
    if position != len(plain) or len(methods) != 8240:
        raise ValueError("method records do not consume the resource exactly")
    outputs = {"methods.json": json.dumps(methods), "patches.json": json.dumps(patches)}
    outputs.update(il_files)
    if emit_booleans:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key = bytes.fromhex("fe4232f5b640fe36ea87c46c4e86cf752aa8ca73d786f20c118aa9aa5362c437")
        iv = bytes.fromhex("037b277b634a0a2286dc58d26b205726")
        decryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
        data = resources["niEpSDFLI5bR8Vt3wr.MKpXS5GVTBWy0aWMFS"]
        table = decryptor.update(data) + decryptor.finalize()
        padding = table[-1]
        if not 1 <= padding <= 16 or table[-padding:] != bytes([padding]) * padding:
            raise ValueError("boolean resource padding is invalid")
        table = table[:-padding]
        if len(table) % 4:
            raise ValueError("boolean offset table is misaligned")
        booleans = {}
        for offset in range(0, len(table), 4):
            location, = struct.unpack_from("<I", table, offset)
            booleans[offset] = source[location] == 0x80
        outputs["booleans.json"] = json.dumps(booleans, indent=2)
    manifest = {"source_sha256": EXE_SHA256, "resource": RESOURCE,
                "decrypted_sha256": hashlib.sha256(plain).hexdigest(),
                "resource_bytes": len(plain), "patches": patch_count, "methods": len(methods),
                "native_records": 0, "trailing_bytes": 0,
                "il_offset_note": "IL listing offsets include a synthetic 12-byte header"}
    outputs["manifest.json"] = json.dumps(manifest, indent=2)
    return outputs, manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("output", type=Path, help="new output directory, outside the repository recommended")
    parser.add_argument("--il", action="store_true", help="emit all 8240 IL listings (requires dncil)")
    parser.add_argument("--booleans", action="store_true", help="decode opaque branch booleans (requires cryptography)")
    args = parser.parse_args(argv)
    try:
        if args.output.exists():
            raise ValueError("output directory already exists")
        outputs, manifest = recover(args.exe.read_bytes(), emit_il=args.il, emit_booleans=args.booleans)
        args.output.mkdir(parents=True)
        for name, content in outputs.items():
            (args.output / name).write_text(content, encoding="utf-8")
        print(json.dumps(manifest, indent=2))
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
