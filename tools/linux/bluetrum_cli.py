"""Linux entry points for the recovered default xmaker packaging path."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from xmaker_re import build_dcf
from bluetrum_dcf import parse_dcf


def parse_xm(raw: bytes) -> list[tuple[str, list[str]]]:
    """Parse make/save, with optional quoted paths, without executing script code."""
    text = raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    commands = []
    offset = 0
    pattern = re.compile(r"\s*(make|save)\s*\(([^()]*)\)\s*;", re.DOTALL)
    arg_pattern = re.compile(r'\s*(?:"([^"\r\n]*)"|([^,\s"()]+))\s*(,|$)')
    while text[offset:].strip():
        match = pattern.match(text, offset)
        if not match:
            if text[offset:].strip().rstrip(";").strip() == "download":
                raise ValueError("XM download launches the Windows GUI; on Linux use downloader flash IMAGE --port PORT")
            raise ValueError(f"unsupported XM syntax near {text[offset:offset + 60]!r}; only make/save are supported")
        command, raw_args = match.groups()
        args = []
        pos = 0
        while pos < len(raw_args):
            arg = arg_pattern.match(raw_args, pos)
            if not arg:
                raise ValueError("invalid XM argument; quote paths containing spaces")
            args.append(arg.group(1) if arg.group(1) is not None else arg.group(2))
            pos = arg.end()
            if arg.group(3) == "," and not raw_args[pos:].strip():
                raise ValueError("trailing comma in XM command")
        if len(args) != {"make": 3, "save": 2}[command]:
            raise ValueError(f"wrong argument count for {command}")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args[0]):
            raise ValueError("invalid XM buffer name")
        commands.append((command, args))
        offset = match.end()
    if not commands:
        raise ValueError("empty XM script")
    return commands


def script_path(base: Path, value: str) -> Path:
    if not value or re.match(r"^[A-Za-z]:", value) or value.startswith("\\\\"):
        raise ValueError(f"expected a Linux path, got {value!r}")
    return (base / value.replace("\\", "/")).resolve()


def atomic_write(path: Path, data: bytes) -> None:
    """Replace one output only after its bytes have been completely written."""
    name = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".xmaker-", delete=False) as output:
            name = output.name
            output.write(data)
        os.replace(name, path)
    finally:
        if name is not None and os.path.exists(name):
            os.unlink(name)


def run_xm(path: Path) -> list[Path]:
    """Resolve filenames relative to the script; validate/build before any save."""
    commands = parse_xm(path.read_bytes())
    buffers = {}
    outputs = {}
    inputs = {path.resolve()}
    for command, args in commands:
        if command == "make":
            header_path, app_path = (script_path(path.parent, item) for item in args[1:])
            inputs.update((header_path, app_path))
            buffers[args[0]] = build_dcf(header_path.read_bytes(), app_path.read_bytes())
        else:
            if args[0] not in buffers:
                raise ValueError(f"unknown XM buffer {args[0]!r}")
            output_path = script_path(path.parent, args[1])
            if not output_path.parent.is_dir():
                raise ValueError(f"output directory does not exist: {output_path.parent}")
            outputs[output_path] = buffers[args[0]]
    if not outputs:
        raise ValueError("XM script has no save command")
    if inputs & outputs.keys():
        raise ValueError("refusing to overwrite an XM script or input binary")
    for output_path, data in outputs.items():
        parse_dcf(data)
    for output_path, data in outputs.items():
        atomic_write(output_path, data)
    return list(outputs)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("make", help="combine bundled header.bin and raw application into DCF")
    make.add_argument("header", type=Path)
    make.add_argument("app", type=Path)
    make.add_argument("output", type=Path)
    xm = sub.add_parser("xmaker", help="run the supported make/save XM commands")
    xm.add_argument("-b", dest="script", required=True, type=Path)
    inspect = sub.add_parser("inspect", help="check container lengths and CRCs")
    inspect.add_argument("image", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "make":
            if args.output.resolve() in (args.header.resolve(), args.app.resolve()):
                raise ValueError("output must differ from input files")
            data = build_dcf(args.header.read_bytes(), args.app.read_bytes())
            parse_dcf(data)
            atomic_write(args.output, data)
            print(f"{args.output}: {len(data)} bytes")
        elif args.command == "xmaker":
            for path in run_xm(args.script):
                print(path)
        else:
            data = args.image.read_bytes()
            image = parse_dcf(data)
            print(json.dumps({"chip": image.chip, "version": image.version,
                              "file_bytes": len(data), "body_bytes": len(image.body),
                              "segments": image.segments, "crc_valid": True,
                              "sha256": hashlib.sha256(data).hexdigest()}, indent=2))
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
