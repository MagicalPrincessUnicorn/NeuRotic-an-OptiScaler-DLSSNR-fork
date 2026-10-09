"""Read fixed PE resources as bytes; never load or execute the product."""
import struct


def resource(raw, resource_id, resource_type=10):
    def u16(offset):
        return struct.unpack_from('<H', raw, offset)[0]
    def u32(offset):
        return struct.unpack_from('<I', raw, offset)[0]
    if raw[:2] != b'MZ':
        raise ValueError('Language resource input is not a PE')
    pe = u32(0x3c)
    if raw[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    sections = u16(pe + 6)
    optional = pe + 24
    magic = u16(optional)
    directory = optional + (112 if magic == 0x20b else 96 if magic == 0x10b else -1)
    if magic not in (0x10b, 0x20b) or sections > 128:
        raise ValueError('Unsupported PE layout')
    section_table = optional + u16(pe + 20)
    def offset(rva, size=1):
        for index in range(sections):
            start = section_table + index * 40
            va, length, file_offset = u32(start + 12), u32(start + 16), u32(start + 20)
            if va <= rva and rva - va + size <= length and file_offset + rva - va + size <= len(raw):
                return file_offset + rva - va
        raise ValueError('Resource lies outside PE file data')
    base = offset(u32(directory + 16))
    def child(at, wanted=None):
        count = u16(at + 12) + u16(at + 14)
        if count > 4096:
            raise ValueError('Resource directory exceeds limit')
        for index in range(count):
            name, target = u32(at + 16 + 8 * index), u32(at + 20 + 8 * index)
            if wanted is None or name == wanted:
                return base + (target & 0x7fffffff), bool(target & 0x80000000)
        raise ValueError('Required language resource missing: ' + str(wanted))
    at, directory_flag = child(base, resource_type)
    if not directory_flag:
        raise ValueError('Invalid resource type directory')
    at, directory_flag = child(at, resource_id)
    if not directory_flag:
        raise ValueError('Invalid resource identifier directory')
    at, directory_flag = child(at)
    if directory_flag:
        raise ValueError('Invalid resource data entry')
    rva, size = u32(at), u32(at + 4)
    start = offset(rva, size)
    return raw[start:start + size]
