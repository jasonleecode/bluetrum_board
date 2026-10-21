#!/usr/bin/env python3
"""Host MMIO model and RV32 compile for native DAC; no physical audio test."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / "bare/drivers/inc"
SRC = ROOT / "bare/drivers/src/dac_output.c"
TEST = ROOT / "docs/lib-analysis/tests/dac_output_test.c"


def run(args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix="ab32-dac-output-") as directory:
        tmp = Path(directory)
        for default in (False, True):
            flags = ["-DUSE_DEFAULT_PLATFORM"] if default else []
            run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DDAC_OUTPUT_TEST", *flags, "-I", INC, SRC, TEST,
                 "-o", tmp / "dac_test"])
            run([tmp / "dac_test"])
        run(["clang", "--target=riscv32-unknown-elf", "-march=rv32imac",
             "-mabi=ilp32", "-std=c11", "-ffreestanding", "-fno-builtin",
             "-Wall", "-Wextra", "-Werror", "-Os", "-I", INC,
             "-c", SRC, "-o", tmp / "dac_output.o"])
        print("PASS DAC: freestanding RV32IMAC compilation")


if __name__ == "__main__":
    main()
