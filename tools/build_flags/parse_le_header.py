"""Dump the LE header fields of a Watcom-linked DOS/4G module.

The linker flag set (target system, stack size, entry point, automatic data
object) is recorded in the LE header, so the header is the primary evidence for
reconstructing the wlink directive file.  Prints every field verbatim; no
interpretation happens here.

Usage:
    python parse_le_header.py <FDPS.LE|FDPS.EXE> [more files...]
"""

import struct
import sys

# offset -> (field name, struct format)
LE_FIELDS = [
    (0x00, "signature", "2s"),
    (0x02, "byte_order", "B"),
    (0x03, "word_order", "B"),
    (0x04, "format_level", "I"),
    (0x08, "cpu_type", "H"),
    (0x0A, "os_type", "H"),
    (0x0C, "module_version", "I"),
    (0x10, "module_flags", "I"),
    (0x14, "module_pages", "I"),
    (0x18, "eip_object", "I"),
    (0x1C, "eip", "I"),
    (0x20, "esp_object", "I"),
    (0x24, "esp", "I"),
    (0x28, "page_size", "I"),
    (0x2C, "last_page_size", "I"),
    (0x30, "fixup_sect_size", "I"),
    (0x34, "fixup_sect_checksum", "I"),
    (0x38, "loader_sect_size", "I"),
    (0x3C, "loader_sect_checksum", "I"),
    (0x40, "object_tbl_off", "I"),
    (0x44, "object_count", "I"),
    (0x48, "object_page_map_off", "I"),
    (0x4C, "object_iter_map_off", "I"),
    (0x50, "resource_tbl_off", "I"),
    (0x54, "resource_count", "I"),
    (0x58, "resident_name_tbl_off", "I"),
    (0x5C, "entry_tbl_off", "I"),
    (0x60, "module_directives_off", "I"),
    (0x64, "module_directives_count", "I"),
    (0x68, "fixup_page_tbl_off", "I"),
    (0x6C, "fixup_record_tbl_off", "I"),
    (0x70, "import_module_tbl_off", "I"),
    (0x74, "import_module_count", "I"),
    (0x78, "import_proc_tbl_off", "I"),
    (0x7C, "page_checksum_off", "I"),
    (0x80, "data_pages_off", "I"),
    (0x84, "preload_page_count", "I"),
    (0x88, "nonres_name_tbl_off", "I"),
    (0x8C, "nonres_name_tbl_len", "I"),
    (0x90, "nonres_name_checksum", "I"),
    (0x94, "auto_data_object", "I"),
    (0x98, "debug_info_off", "I"),
    (0x9C, "debug_info_len", "I"),
    (0xA0, "preload_instance_pages", "I"),
    (0xA4, "demand_instance_pages", "I"),
    (0xA8, "extra_heap_alloc", "I"),
]

OBJ_FLAGS = [
    (0x0001, "READABLE"),
    (0x0002, "WRITABLE"),
    (0x0004, "EXECUTABLE"),
    (0x0008, "RESOURCE"),
    (0x0010, "DISCARDABLE"),
    (0x0020, "SHARED"),
    (0x0040, "PRELOAD"),
    (0x0080, "INVALID"),
    (0x0100, "ZERO_FILLED"),
    (0x0200, "RESIDENT"),
    (0x0400, "RESIDENT_CONTIGUOUS"),
    (0x0800, "RESIDENT_LONG_LOCKABLE"),
    (0x1000, "ALIAS16"),
    (0x2000, "BIG32"),
    (0x4000, "CONFORMING"),
    (0x8000, "IO_PRIVILEGE"),
]


def find_le_base(data):
    """Return the file offset of the LE header (0 for a bare .LE module)."""
    if data[:2] == b"LE":
        return 0
    if data[:2] == b"MZ":
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 2] == b"LE":
            return e_lfanew
    raise ValueError("no LE header found")


def decode_flags(value):
    names = [name for bit, name in OBJ_FLAGS if value & bit]
    return "|".join(names) if names else "-"


def dump(path):
    with open(path, "rb") as fh:
        data = fh.read()
    base = find_le_base(data)
    print("=" * 72)
    print("file           : %s" % path)
    print("file size      : %d" % len(data))
    print("LE base        : 0x%x (MZ stub = %d bytes)" % (base, base))

    hdr = {}
    for off, name, fmt in LE_FIELDS:
        value = struct.unpack_from("<" + fmt, data, base + off)[0]
        hdr[name] = value
        if isinstance(value, bytes):
            print("  +0x%02x %-24s %s" % (off, name, value))
        else:
            print("  +0x%02x %-24s 0x%-10x (%d)" % (off, name, value, value))

    print("-- object table --")
    obj_off = base + hdr["object_tbl_off"]
    objects = []
    for i in range(hdr["object_count"]):
        vsize, base_addr, flags, page_idx, page_cnt, _res = struct.unpack_from(
            "<IIIIII", data, obj_off + i * 24)
        objects.append((vsize, base_addr, flags, page_idx, page_cnt))
        print("  obj %d: base=0x%06x vsize=0x%-6x flags=0x%04x pages=%d@%d  %s"
              % (i + 1, base_addr, vsize, flags, page_cnt, page_idx,
                 decode_flags(flags)))

    esp_obj = hdr["esp_object"]
    if 1 <= esp_obj <= len(objects):
        vsize, base_addr, _f, _pi, page_cnt = objects[esp_obj - 1]
        esp_lin = base_addr + hdr["esp"]
        print("-- derived --")
        print("  initial ESP linear   : 0x%x (object %d + 0x%x)"
              % (esp_lin, esp_obj, hdr["esp"]))
        print("  object %d end         : 0x%x" % (esp_obj, base_addr + vsize))
        print("  bytes above ESP      : %d" % (base_addr + vsize - esp_lin))
        print("  file-backed end      : 0x%x"
              % (base_addr + page_cnt * hdr["page_size"]))

    print("-- resident name table --")
    rn = base + hdr["resident_name_tbl_off"]
    length = data[rn]
    print("  module name: %r" % data[rn + 1:rn + 1 + length])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    for arg in sys.argv[1:]:
        dump(arg)
