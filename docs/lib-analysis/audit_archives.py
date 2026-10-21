#!/usr/bin/env python3
"""Inventory the two SDK archives and verify cached GNU disassembly bytes.

Uses Python's standard library and ar; LLVM is only needed for fresh sdadc
disassembly. This verifies byte provenance, not instruction semantics or C
equivalence. Run from any directory. No firmware is executed.
"""
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/lib-analysis/generated/recovery"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def cstring(data, offset):
    return data[offset:data.index(b"\0", offset)].decode("utf-8", "replace")


def parse_elf(data):
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected little-endian ELF32")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine, _, _, _, shoff, flags, _, _, _, shentsize, shnum, shstrndx = header
    if kind != 1 or machine != 243 or shentsize != 40:
        raise ValueError("Expected RISC-V ELF32 relocatable with standard sections")
    raw = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    strings = data[raw[shstrndx][4]:raw[shstrndx][4] + raw[shstrndx][5]]
    sections = {}
    symbols = []
    for section in raw:
        name, typ, secflags, _, offset, size, link, _, _, entsize = section
        name = cstring(strings, name)
        content = data[offset:offset + size] if typ != 8 else b""
        sections[name] = {"type": typ, "flags": secflags, "size": size, "data": content}
        if typ == 2:
            if entsize != 16:
                raise ValueError("Unexpected symbol entry size")
            names = data[raw[link][4]:raw[link][4] + raw[link][5]]
            for pos in range(0, size, entsize):
                n, value, length, info, _, index = struct.unpack_from("<IIIBBH", content, pos)
                symbols.append({"name": cstring(names, n), "value": value,
                                "size": length, "bind": info >> 4,
                                "type": info & 15, "section_index": index})
    return flags, sections, symbols


def verify_listing(path, objects):
    member = section = None
    checked = 0
    covered = set()
    for line in path.read_text().splitlines():
        match = re.match(r"^(\S+\.o):\s+file format", line)
        if match:
            member = match[1]
            section = None
        match = re.match(r"Disassembly of section (.+):", line)
        if match:
            section = match[1]
        match = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4,16})\s+", line)
        if match and member and section:
            offset = int(match[1], 16)
            word = match[2]
            encoded = int(word, 16).to_bytes(len(word) // 2, "little")
            actual = objects[member][1][section]["data"][offset:offset + len(encoded)]
            if actual != encoded:
                raise ValueError(f"Cached bytes differ: {member}:{section}+{offset:#x}")
            checked += 1
            covered.update((member, section, offset + i) for i in range(len(encoded)))
    if not checked:
        raise ValueError("No instruction bytes checked")
    executable = sum(s["size"] for _, sections, _ in objects.values()
                     for s in sections.values() if s["flags"] & 4 and s["type"] != 8)
    code_covered = sum(1 for m, s, _ in covered if objects[m][1][s]["flags"] & 4)
    return {"listing_sha256": sha256(path.read_bytes()), "instruction_rows_checked": checked,
            "executable_bytes_covered": code_covered, "executable_bytes_total": executable,
            "uncovered_executable_bytes": executable - code_covered,
            "meaning": "Listed encoded bytes match archive; mnemonics/relocations not validated here."}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {"libraries": {}}
    for name in ("libhal", "libbtctrl"):
        archive = ROOT / f"bare/lib/{name}.a"
        members = subprocess.check_output(["ar", "t", str(archive)], text=True).splitlines()
        if len(members) != len(set(members)):
            raise ValueError("Duplicate member names require ordinal extraction")
        objects = {}
        rows = []
        for member in members:
            data = subprocess.check_output(["ar", "p", str(archive), member])
            flags, sections, symbols = parse_elf(data)
            objects[member] = flags, sections, symbols
            rows.append({"member": member, "sha256": sha256(data), "file_bytes": len(data),
                         "elf_flags": flags,
                         "debug_sections": [s for s in sections if s.startswith((".debug", ".zdebug"))],
                         "code_bytes": sum(s["size"] for s in sections.values() if s["flags"] & 4),
                         "relocations": sum(s["size"] // (12 if s["type"] == 4 else 8)
                                            for s in sections.values() if s["type"] in (4, 9)),
                         "defined_functions": [s for s in symbols if s["type"] == 2 and s["section_index"]],
                         "global_definitions": sum(s["bind"] in (1, 2) and s["section_index"] != 0
                                                   for s in symbols)})
            if name == "libhal" and member in ("sdadc.o", "driver_spi.o"):
                (OUT / member).write_bytes(data)
        verification = verify_listing(ROOT / f"docs/lib-analysis/generated/{name}.objdump-dr.txt", objects)
        manifest["libraries"][name] = {"path": str(archive.relative_to(ROOT)),
                                     "sha256": sha256(archive.read_bytes()),
                                     "members": rows, "cached_listing_verification": verification}
    objdump = shutil.which("llvm-objdump") or shutil.which("llvm-objdump-10")
    if objdump:
        commands = []
        for stem in ("sdadc", "driver_spi"):
            command = [objdump, "-dr", "--mattr=+m,+a,+c", str(OUT / f"{stem}.o")]
            output = subprocess.check_output(command, text=True)
            (OUT / f"{stem}.llvm.txt").write_text(output)
            commands.append(command)
        manifest["fresh_disassembler"] = {"commands": commands,
            "version": subprocess.check_output([objdump, "--version"], text=True).splitlines()[0]}
    else:
        manifest["fresh_disassembler"] = None
    (OUT / "archive-audit.json").write_text(json.dumps(manifest, indent=2) + "\n")
    for name, lib in manifest["libraries"].items():
        rows = lib["members"]
        print(name, json.dumps({"objects": len(rows),
            "functions": sum(len(r["defined_functions"]) for r in rows),
            "code_bytes": sum(r["code_bytes"] for r in rows),
            "globals": sum(r["global_definitions"] for r in rows),
            "objects_with_debug": sum(bool(r["debug_sections"]) for r in rows),
            "verification": lib["cached_listing_verification"]}))


if __name__ == "__main__":
    main()
