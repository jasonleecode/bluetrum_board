"""Linux Bluetrum downloader. Hardware flashing is not yet board-validated."""

import argparse
from dataclasses import asdict
import json
import math
from pathlib import Path
import sys
import termios

from bluetrum_dcf import parse_dcf
from bluetrum_flash import flash_image, plan_flash
from bluetrum_uart import inspect_loader, loader_name, unpack_loader


DEFAULT_DLLS = Path(__file__).resolve().parents[1] / "windows/downloader/dlls"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect = sub.add_parser("inspect-loader", help="decrypt and CRC-check target RAM loader")
    inspect.add_argument("file", type=Path)
    unpack = sub.add_parser("unpack-loader", help="extract target RAM loader to a new file")
    unpack.add_argument("file", type=Path)
    unpack.add_argument("output", type=Path)
    probe = sub.add_parser("probe", help="synchronize with boot ROM and read chip information")
    flash = sub.add_parser("flash", help="erase, write and verify a default DCF image")
    flash.add_argument("image", type=Path)
    flash.add_argument("--loader", type=Path, help="override bundled target RAM loader container")
    flash.add_argument("--dry-run", action="store_true", help="validate files and show plan without opening a port")
    flash.add_argument("--no-reboot", action="store_true", help="leave chip in loader after successful verification")
    for command in (probe, flash):
        command.add_argument("--port", help="explicit Linux serial device, e.g. /dev/ttyUSB0")
        command.add_argument("--baud", type=int, default=115200, choices=(115200,),
                             help="keep original low-speed path (default: 115200)")
        command.add_argument("--no-echo", action="store_true", help="adapter does not echo transmitted bytes")
        command.add_argument("--timeout", type=float, default=2.0, help="per-transfer timeout in seconds")
        command.add_argument("--connect-timeout", type=float, default=10.0, help="boot ROM synchronization deadline")
    args = parser.parse_args(argv)
    try:
        if args.command in ("inspect-loader", "unpack-loader"):
            result = inspect_loader(args.file)
            if args.command == "unpack-loader":
                payload = unpack_loader(args.file.read_bytes())
                with args.output.open("xb") as output:
                    output.write(payload)
                result["output"] = str(args.output)
            print(json.dumps(result, indent=2))
            return 0

        if any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.connect_timeout)):
            raise ValueError("timeouts must be finite and positive")
        if args.command == "flash":
            image = parse_dcf(args.image.read_bytes())
            plan = plan_flash(image)
            loader_path = args.loader or DEFAULT_DLLS / loader_name(image.chip)
            loader_container = loader_path.read_bytes()
            loader_payload = unpack_loader(loader_container)
            summary = {"chip": image.chip, "image": str(args.image),
                       "loader": str(loader_path), "loader_bytes": len(loader_payload),
                       **asdict(plan)}
            if args.dry_run:
                summary["serial_opened"] = False
                print(json.dumps(summary, indent=2))
                return 0
        if not args.port:
            raise ValueError("--port is required for a live connection")

        from bluetrum_download import Bootloader, FramedUart
        from bluetrum_serial import SerialPort
        print(f"Connecting to {args.port}; reset the board to enter its bootloader.", file=sys.stderr)
        with SerialPort(args.port, args.baud, args.timeout) as serial:
            boot = Bootloader(FramedUart(serial, echo=not args.no_echo, timeout=args.timeout))
            info = boot.connect(timeout=args.connect_timeout)
            if args.command == "probe":
                print(json.dumps(asdict(info), indent=2))
                return 0
            if info.chip != image.chip:
                raise ValueError(f"DCF chip {image.chip} does not match connected chip {info.chip}")
            print(f"Connected: {info.chip}; uploading RAM loader.", file=sys.stderr)
            boot.load_loader(loader_container)
            flash_info = boot.initialize_flash()
            print(f"Flash: {flash_info.flash_size} bytes; image body: {len(image.body)} bytes.", file=sys.stderr)
            last_status = None

            def progress(phase, done, total):
                nonlocal last_status
                status = (phase, min(100, done * 100 // total) // 10)
                if status != last_status:
                    print(f"{phase}: {done}/{total}", file=sys.stderr)
                    last_status = status

            flash_image(boot, image, flash_info.flash_size, progress=progress, reboot=not args.no_reboot)
            print(f"Verified and committed {args.image} to {args.port}.")
    except (OSError, termios.error, ValueError, RuntimeError, EOFError) as error:
        parser.exit(1, f"error: {error}\n")
    except KeyboardInterrupt:
        parser.exit(130, "interrupted\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
