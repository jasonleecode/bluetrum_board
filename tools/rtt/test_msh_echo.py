#!/usr/bin/env python3
"""Run the real FinSH loop against electrical echo and a small VT100 model."""
import re
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, base_config


class Terminal:
    def __init__(self, row=5):
        self.rows = [[' '] * 100 for _ in range(24)]
        self.row, self.col = row, 0
        self.saved = (row, 0)
        self.escape = ''

    def write(self, data):
        for ch in data.decode('ascii'):
            if self.escape:
                self.escape += ch
                if self.escape == '\x1b[':
                    continue
                if self.escape in ('\x1b7', '\x1b8'):
                    if ch == '7': self.saved = self.row, self.col
                    else: self.row, self.col = self.saved
                    self.escape = ''
                elif self.escape.startswith('\x1b[') and ch.isalpha():
                    digits = self.escape[2:-1]
                    n = int(digits or '1')
                    if ch == 'A': self.row = max(0, self.row - n)
                    elif ch == 'B': self.row = min(23, self.row + n)
                    elif ch == 'C': self.col = min(99, self.col + n)
                    elif ch == 'D': self.col = max(0, self.col - n)
                    elif ch == 'K' and n == 2: self.rows[self.row] = [' '] * 100
                    else: raise AssertionError(self.escape)
                    self.escape = ''
                continue
            if ch == '\x1b': self.escape = ch
            elif ch == '\r': self.col = 0
            elif ch == '\n':
                if self.row == 23:
                    self.rows.pop(0); self.rows.append([' '] * 100)
                else: self.row += 1
            elif ch == '\b': self.col = max(0, self.col - 1)
            elif ch == '\t': self.col = min(99, (self.col // 8 + 1) * 8)
            elif ch == '\x7f': pass
            elif ' ' <= ch <= '~':
                self.rows[self.row][self.col] = ch
                self.col += 1
                assert self.col < 100, 'test command must fit one terminal row'


with tempfile.TemporaryDirectory(prefix='ab32-msh-echo-') as directory:
    temp = Path(directory)
    config = base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text())
    includes = ['rtt/include', 'rtt/components/finsh', 'rtt/components/drivers/include']
    command = ['cc', '-std=gnu11', '-O2', '-g', '-Werror=incompatible-pointer-types',
               '-ffunction-sections', '-fdata-sections', '-fsanitize=undefined',
               '-fno-sanitize-recover=all', '-I'+str(temp),
               *['-I'+str(ROOT / p) for p in includes],
               str(ROOT / 'tools/rtt/tests/msh_echo.c'), '-Wl,--gc-sections', '-o', str(temp / 'msh')]
    cases = [
        (b'version', 'version', 7, []),
        (b'version\r', '', 0, ['version']),
        (b'version\n', '', 0, ['version']),
        (b'versionx\x7f', 'version', 7, []),
        (b'versionx\x08', 'version', 7, []),
        (b'\x08\x7f', '', 0, []),
        (b'abc\x1b[D\x1b[DX', 'aXbc', 2, []),
        (b'abc\x1b[D\x7f', 'ac', 1, []),
        (b'abc\x1b[D\x08', 'ac', 1, []),
        (b'ab\x1b[D\x1b[D\x1b[D\x1b[C', 'ab', 1, []),
        (b'ab\x1b[C', 'ab', 2, []),
        (b'\x1b[A\x1b[B', '', 0, []),
        (b'one\rtwo\r\x1b[A\x1b[A\x1b[B', 'two', 3, ['one', 'two']),
        (b'ver\t', 'version', 7, []),
        (b'ver\t\r', '', 0, ['version']),
    ]
    for enabled in (True, False):
        (temp / 'rtconfig.h').write_text(config if enabled else re.sub(
            r'^#define FINSH_USING_HARDWARE_ECHO[^\n]*', '', config, flags=re.M))
        subprocess.run(command, check=True)
        for device in ('uart0', 'uart1'):
            electrical = enabled and device == 'uart0'
            for data, line, cursor, executed in cases:
                result = subprocess.check_output([temp / 'msh', data.hex(), device], text=True)
                # Exercise top/bottom boundaries: inverse cursor movement would fail here.
                for start_row in (0, 5, 23):
                    terminal, commands, output = Terminal(start_row), [], b''
                    final_line, final_pos = None, None
                    for record in result.splitlines():
                        kind, _, payload = record.partition(' ')
                        if kind == 'P': final_pos = int(payload); continue
                        value = bytes.fromhex(payload)
                        if kind == 'I' and electrical: terminal.write(value)
                        elif kind == 'O': terminal.write(value); output += value
                        elif kind == 'E': commands.append(value.decode())
                        elif kind == 'L': final_line = value.decode()
                    visible = ''.join(terminal.rows[terminal.row]).rstrip()
                    assert (commands, final_line, final_pos) == (executed, line, cursor), (data, result)
                    assert visible == 'msh >' + line, (device, data, visible, result)
                    assert terminal.col == len('msh >') + cursor, (data, terminal.col, result)
                    assert not terminal.escape
                    if data == b'version' and electrical:
                        assert b'version' not in output and b'v' not in output, result
    print('PASS: real FinSH, one printable echo, CR/LF, BS/DEL, insertion, arrows/boundaries, history, tab; UART1 and legacy mode unchanged (UBSan)')
