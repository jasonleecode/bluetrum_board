#!/usr/bin/env python3
"""Analyze AB32VG1 vendor static libraries.

The script keeps the library symbol inventory reproducible on macOS without a
Windows toolchain. It uses riscv64-elf-nm when available and falls back to
source scanning only for the current bare shim coverage.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_NM = "/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-nm"
DEFAULT_AR = "/opt/homebrew/opt/riscv64-elf-binutils/bin/riscv64-elf-ar"
DEFAULT_LIBS = ("bare/lib/libhal.a", "bare/lib/libbtctrl.a")
DEFAULT_OUTPUT_DIR = "docs/lib-analysis/generated"
DEFAULT_SHIM_SOURCE = "bare/drivers/src/vendor_shim.c"
DEFAULT_SHIM_OBJECT = "bare/build/drivers/src/vendor_shim.o"

KNOWN_HOOKS = {
    "bt_get_local_bd_addr",
    "bthw_soft_kick",
    "bthw_thread_post",
    "ep2_isoc",
    "get_sysclk_nhz",
    "hal_get_ticks",
    "hal_mdelay",
    "hal_printf",
    "hal_udelay",
    "hci_host_recv_packet",
    "interrupt_handler_c",
    "nanos_event_set_trigger",
    "os_cache_lock",
    "os_cache_unlock",
    "os_get_interrupt_nest",
    "os_interrupt_enter",
    "os_interrupt_leave",
    "os_mq_ude_ctl_flow_post",
    "os_mq_ude_ep0_setup_post",
    "os_mq_ude_reset_post",
    "os_spiflash_lock",
    "os_spiflash_unlock",
    "register_isr",
    "rt_thread_self",
    "sdadc_analog_aux_exit",
    "sdadc_analog_aux_start",
    "ude_hid_send",
}

LIBC_SYMBOLS = {"memcpy", "memcmp", "memset", "memmove", "strlen", "strcpy"}
ROM_SYMBOLS = {"my_printf", "my_print_r"}

NM_DEFINED_RE = re.compile(
    r"^(?P<lib>.+\.a):(?P<object>[^:]+):(?P<addr>[0-9A-Fa-f]+)\s+"
    r"(?P<type>\S)\s+(?P<symbol>\S+)$"
)
NM_UNDEFINED_RE = re.compile(
    r"^(?P<lib>.+\.a):(?P<object>[^:]+):\s+U\s+(?P<symbol>\S+)$"
)
NM_OBJECT_DEFINED_RE = re.compile(
    r"^(?P<addr>[0-9A-Fa-f]+)\s+(?P<type>\S)\s+(?P<symbol>\S+)$"
)
FUNC_DEF_RE = re.compile(
    r"^\s*(?!static\b)(?P<prefix>[A-Za-z_][\w\s\*]*?)"
    r"(?P<symbol>[A-Za-z_]\w*)\s*\([^;]*\)\s*$"
)
GLOBAL_DEF_RE = re.compile(
    r"^\s*(?!static\b)(?P<prefix>[A-Za-z_][\w\s\*]*?)"
    r"(?P<symbol>[A-Za-z_]\w*)(?:\[[^\]]*\])?\s*(?:=|__attribute__|\s*;)"
)


def run_tool(tool: str, args: list[str], cwd: Path, label: str) -> str:
    cmd = [tool, *args]
    try:
        proc = subprocess.run(
            cmd,
            cwd=cwd,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError as exc:
        raise SystemExit(f"{label} not found: {tool}") from exc
    except subprocess.CalledProcessError as exc:
        stderr = exc.stderr.strip()
        raise SystemExit(f"{label} failed: {' '.join(cmd)}\n{stderr}") from exc
    return proc.stdout


def run_nm(nm: str, args: list[str], cwd: Path) -> str:
    return run_tool(nm, args, cwd, "nm")


def run_ar(ar: str, args: list[str], cwd: Path) -> str:
    return run_tool(ar, args, cwd, "ar")


def parse_library_symbols(nm: str, ar: str, libs: list[Path], repo_root: Path) -> dict:
    rel_libs = [str(path.relative_to(repo_root)) for path in libs]
    output = run_nm(nm, ["-A", "-g", "--defined-only", *rel_libs], repo_root)
    undefined_output = run_nm(nm, ["-A", "-g", "-u", *rel_libs], repo_root)

    libraries: dict[str, dict] = {}
    defined_index: dict[str, list[dict]] = defaultdict(list)

    for lib in libs:
        key = str(lib.relative_to(repo_root))
        libraries[key] = {"objects": defaultdict(lambda: {"defined": [], "undefined": []})}
        for obj in run_ar(ar, ["t", key], repo_root).splitlines():
            obj = obj.strip()
            if obj:
                libraries[key]["objects"][obj]

    for line in output.splitlines():
        match = NM_DEFINED_RE.match(line.strip())
        if not match:
            continue
        lib = match.group("lib")
        obj = match.group("object")
        symbol = {
            "name": match.group("symbol"),
            "type": match.group("type"),
            "address": match.group("addr"),
        }
        libraries[lib]["objects"][obj]["defined"].append(symbol)
        defined_index[symbol["name"]].append(
            {"library": lib, "object": obj, "type": symbol["type"], "address": symbol["address"]}
        )

    for line in undefined_output.splitlines():
        match = NM_UNDEFINED_RE.match(line.strip())
        if not match:
            continue
        lib = match.group("lib")
        obj = match.group("object")
        libraries[lib]["objects"][obj]["undefined"].append(match.group("symbol"))

    return {"libraries": libraries, "defined_index": defined_index}


def parse_shim_from_object(nm: str, shim_object: Path, repo_root: Path) -> tuple[set[str], str]:
    if not shim_object.exists():
        return set(), "missing"

    output = run_nm(
        nm,
        ["-g", "--defined-only", str(shim_object.relative_to(repo_root))],
        repo_root,
    )
    symbols = set()
    for line in output.splitlines():
        match = NM_OBJECT_DEFINED_RE.match(line.strip())
        if match:
            symbols.add(match.group("symbol"))
    return symbols, "object"


def parse_shim_from_source(shim_source: Path) -> set[str]:
    if not shim_source.exists():
        return set()

    symbols: set[str] = set()
    for raw_line in shim_source.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("/*", 1)[0].strip()
        if not line or line.startswith("#") or line.startswith("typedef "):
            continue
        func = FUNC_DEF_RE.match(line)
        if func:
            name = func.group("symbol")
            if name not in {"if", "for", "while", "switch", "return"}:
                symbols.add(name)
            continue
        glob = GLOBAL_DEF_RE.match(line)
        if glob:
            symbols.add(glob.group("symbol"))
    return symbols


def categorize(symbol: str) -> str:
    if symbol.startswith("__") or symbol in {"_gp"}:
        return "toolchain"
    if symbol in LIBC_SYMBOLS:
        return "libc"
    if symbol in ROM_SYMBOLS:
        return "rom"
    if symbol in KNOWN_HOOKS:
        return "platform_hook"
    return "unknown"


def build_external_report(libraries: dict, defined_index: dict) -> dict:
    external: dict[str, dict] = {}
    for lib, lib_data in libraries.items():
        for obj, obj_data in lib_data["objects"].items():
            for symbol in sorted(set(obj_data["undefined"])):
                if symbol in defined_index:
                    continue
                item = external.setdefault(
                    symbol,
                    {"category": categorize(symbol), "needed_by": []},
                )
                item["needed_by"].append({"library": lib, "object": obj})
    return dict(sorted(external.items(), key=lambda item: (item[1]["category"], item[0])))


def serializable_libraries(libraries: dict) -> dict:
    clean = {}
    for lib, lib_data in sorted(libraries.items()):
        objects = {}
        for obj, obj_data in sorted(lib_data["objects"].items()):
            objects[obj] = {
                "defined": sorted(obj_data["defined"], key=lambda item: item["name"]),
                "undefined": sorted(set(obj_data["undefined"])),
            }
        clean[lib] = {"objects": objects}
    return clean


def summarize_library(lib_data: dict) -> dict:
    objects = lib_data["objects"]
    return {
        "objects": len(objects),
        "defined": sum(len(obj["defined"]) for obj in objects.values()),
        "undefined_refs": sum(len(obj["undefined"]) for obj in objects.values()),
        "undefined_unique": len({sym for obj in objects.values() for sym in obj["undefined"]}),
    }


def format_symbol_locations(locations: list[dict], max_items: int = 4) -> str:
    rendered = [f"{Path(item['library']).name}:{item['object']}" for item in locations[:max_items]]
    if len(locations) > max_items:
        rendered.append(f"+{len(locations) - max_items} more")
    return ", ".join(rendered)


def write_markdown(
    path: Path,
    libraries: dict,
    external: dict,
    shim_symbols: set[str],
    shim_source_symbols: set[str],
    shim_source_mode: str,
) -> None:
    category_counts = Counter(item["category"] for item in external.values())
    shim_required = {
        symbol for symbol, item in external.items() if item["category"] != "toolchain"
    }
    covered = sorted(symbol for symbol in shim_required if symbol in shim_symbols)
    source_only = sorted(symbol for symbol in shim_required if symbol in shim_source_symbols and symbol not in shim_symbols)
    missing = sorted(symbol for symbol in shim_required if symbol not in shim_symbols and symbol not in shim_source_symbols)

    lines = [
        "# Generated Library Summary",
        "",
        "Generated by `docs/lib-analysis/analyze_libs.py`. Do not edit this file by hand.",
        "",
        "## Library Scale",
        "",
        "| Library | Objects | Defined globals | Undefined refs | Unique undefined |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for lib, lib_data in sorted(libraries.items()):
        summary = summarize_library(lib_data)
        lines.append(
            f"| `{lib}` | {summary['objects']} | {summary['defined']} | "
            f"{summary['undefined_refs']} | {summary['undefined_unique']} |"
        )

    lines.extend(
        [
            "",
            "## External Dependencies",
            "",
            "| Category | Unique symbols |",
            "| --- | ---: |",
        ]
    )
    for category in ("platform_hook", "libc", "rom", "toolchain", "unknown"):
        lines.append(f"| `{category}` | {category_counts.get(category, 0)} |")

    lines.extend(
        [
            "",
            "## Shim Coverage",
            "",
            f"Shim symbol source: `{shim_source_mode}`.",
            "",
            "| Status | Symbols |",
            "| --- | --- |",
            f"| Provided | {', '.join(f'`{sym}`' for sym in covered) or '-'} |",
            f"| Source-only fallback | {', '.join(f'`{sym}`' for sym in source_only) or '-'} |",
            f"| Missing | {', '.join(f'`{sym}`' for sym in missing) or '-'} |",
        ]
    )

    lines.extend(
        [
            "",
            "## Library-External Symbols",
            "",
            "| Symbol | Category | Needed by |",
            "| --- | --- | --- |",
        ]
    )
    for symbol, item in sorted(external.items(), key=lambda item: (item[1]["category"], item[0])):
        lines.append(
            f"| `{symbol}` | `{item['category']}` | {format_symbol_locations(item['needed_by'])} |"
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".", help="repository root")
    parser.add_argument("--nm", default=os.environ.get("NM", DEFAULT_NM), help="riscv64-elf-nm path")
    parser.add_argument("--ar", default=os.environ.get("AR", DEFAULT_AR), help="riscv64-elf-ar path")
    parser.add_argument(
        "--output-dir",
        default=DEFAULT_OUTPUT_DIR,
        help="directory for generated analysis files",
    )
    parser.add_argument(
        "--shim-source",
        default=DEFAULT_SHIM_SOURCE,
        help="current bare shim source file",
    )
    parser.add_argument(
        "--shim-object",
        default=DEFAULT_SHIM_OBJECT,
        help="optional current bare shim object file",
    )
    parser.add_argument("libraries", nargs="*", default=list(DEFAULT_LIBS))
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    libs = [(repo_root / item).resolve() for item in args.libraries]
    missing_libs = [str(path) for path in libs if not path.exists()]
    if missing_libs:
        raise SystemExit("missing library file(s): " + ", ".join(missing_libs))

    output_dir = (repo_root / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    parsed = parse_library_symbols(args.nm, args.ar, libs, repo_root)
    libraries = serializable_libraries(parsed["libraries"])
    defined_index = dict(sorted(parsed["defined_index"].items()))
    external = build_external_report(libraries, defined_index)

    shim_object_symbols, shim_mode = parse_shim_from_object(
        args.nm, (repo_root / args.shim_object).resolve(), repo_root
    )
    shim_source_symbols = parse_shim_from_source((repo_root / args.shim_source).resolve())
    if shim_object_symbols:
        shim_symbols = shim_object_symbols
    else:
        shim_symbols = shim_source_symbols
        shim_mode = "source"

    data = {
        "libraries": libraries,
        "defined_index": defined_index,
        "external_dependencies": external,
        "shim": {
            "mode": shim_mode,
            "symbols": sorted(shim_symbols),
            "source_symbols": sorted(shim_source_symbols),
        },
    }
    (output_dir / "symbols.json").write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_markdown(
        output_dir / "library-summary.md",
        libraries,
        external,
        shim_symbols,
        shim_source_symbols,
        shim_mode,
    )

    print(f"wrote {output_dir / 'symbols.json'}")
    print(f"wrote {output_dir / 'library-summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
