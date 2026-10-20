"""Data-only PE parser cases. Never load these files as executable images."""
from pathlib import Path
import hashlib
import json
import struct
import sys


def image(kind):
    data = bytearray(0x400)
    def put(offset, fmt, *values):
        struct.pack_into('<' + fmt, data, offset, *values)
    data[:2] = b'MZ'
    put(0x3c, 'I', 0x80)
    data[0x80:0x84] = b'PE\0\0'
    put(0x84, 'HHIIIHH', 0x8664, 1, 0, 0, 0, 0xf0, 0x2002)
    optional = 0x98
    put(optional, 'H', 0x20b)
    put(optional + 60, 'I', 0x200)
    put(optional + 108, 'I', 1)
    put(optional + 112, 'II', 0x1000, 40)
    section = optional + 0xf0
    data[section:section + 8] = b'.test\0\0\0'
    put(section + 8, 'IIII', 0x200, 0x1000, 0x200, 0x200)
    put(section + 36, 'I', 0x60000020)
    # One declared name/function. Code RVA is outside the export directory.
    put(0x200 + 16, 'IIIIII', 1, 1, 1, 0x1040, 0x1044, 0x1048)
    put(0x240, 'I', 0x1100)
    put(0x244, 'I', 0x1060)
    put(0x248, 'H', 0)
    data[0x260:0x262] = b'x\0'  # Valid unrelated short export.
    data[0x300] = 0xc3
    if kind == 'unterminated-name':
        put(0x244, 'I', 0x11fe)
        data[0x3fe:] = b'xy'  # No NUL before the raw section/file boundary.
    elif kind == 'nonmatching-invalid-ordinal':
        put(0x248, 'H', 1)  # NumberOfFunctions is one: ordinal one is invalid.
    elif kind == 'header-fallback-section-boundary':
        data.extend(bytes(0x100))
        put(optional + 60, 'I', 0x2000)
        put(0x244, 'I', 0x11fe)
        data[0x3fe:0x400] = b'xy'
        # NUL at 0x400 is in the file but OUTSIDE this raw section.
        assert data[0x400] == 0 and len(data) == 0x500
    elif kind != 'valid-short-name':
        raise ValueError(kind)
    return bytes(data)


def main(destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    cases = []
    for kind, expected, diagnostic in [
        ('valid-short-name', 0, 'required fixture export absent; executable loading refused'),
        ('unterminated-name', 1, 'fixture export name unterminated'),
        ('nonmatching-invalid-ordinal', 1, 'fixture export ordinal invalid'),
        ('header-fallback-section-boundary', 1, 'fixture export name unterminated'),
    ]:
        data = image(kind)
        path = destination / (kind + '.bin')
        path.write_bytes(data)
        cases.append(dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(),
                          command_mode='reject-ordinary', expected_exit=expected,
                          expected_diagnostic=diagnostic))
    receipt = dict(status='DATA_ONLY_CASES_NOT_NATIVE_EXECUTED', cases=cases,
                   ordinary_product_requirement='Also test an actual valid ordinary product; these parser cases do not replace it.',
                   regression='Frozen v5 incorrectly returns zero on the first two malformed cases; frozen v6 incorrectly returns zero on header-fallback-section-boundary. Corrected v7 must return one with the stated diagnostics on all three. Every version returns zero on the valid short name.')
    (destination / 'CASES.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('absolute output directory for data-only parser cases')
    main(sys.argv[1])
