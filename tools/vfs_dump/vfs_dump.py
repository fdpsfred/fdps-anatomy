"""Parse the FDPS .VFS containers and extract every member to a real file.

The container format was read off the loader in FDPS.LE (open at 0x39ab0, member
lookup at 0x39990, member read at 0x39bd0), so the field offsets here are what
the game itself uses rather than a guess that happens to fit:

    header (35 bytes)
      +0x00  char[3]  "VFS" magic          (never read by the game)
      +0x03  u16      version, always 1    (never read by the game)
      +0x05  u16      offset of the entry table, always 35
      +0x07  u32      entry count
      +0x0B  char[24] "Dynasty Information Co.," (never read by the game)

    entry (26 bytes, entry_count of them, immediately after the header)
      +0x00  char[13] member name, NUL-padded 8.3
      +0x0D  u32      member size
      +0x11  u32      the same size again  (never read by the game)
      +0x15  u8       always 0             (never read by the game)
      +0x16  u32      member offset from the start of the container

Payloads follow the entry table back to back in entry order, so the container is
self-checking: every offset must be the previous member's end, and the last
member must end exactly at the end of the file. Any field that does not line up
aborts the run naming the field, because a container that fails these checks is
not this format and extracting from it would produce plausible-looking garbage.

Members that are themselves containers (MISC.VFS carries two) are extracted as
files and additionally expanded into a "<member name>.d" directory beside them.

Usage:
    python vfs_dump.py list   <container>
    python vfs_dump.py dump   <container or directory> <output dir>
    python vfs_dump.py report <manifest json>
"""

import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path

MAGIC = b"VFS"
VERSION = 1
SIGNATURE = b"Dynasty Information Co.,"
SIGNATURE_OFFSET = 0x0B
ENTRY_SIZE = 26
NAME_SIZE = 13
# The lookup at 0x39990 loads the entry count through AL, so a container with
# more than 255 members could not have its later members addressed at all.
MAX_ENTRIES = 255
# The open at 0x39ab0 seeks to the entry table with a sign-extended 16-bit value.
MAX_TABLE_OFFSET = 0x7FFF
NESTED_SUFFIX = ".d"
# Names become paths under the output directory, so nothing outside DOS 8.3 gets
# through: a name like "C:BOOT.INI" would resolve relative to the C: drive
# rather than the output tree, and "NUL" or "COM1" would open a device.
NAME_PATTERN = re.compile(r"[A-Za-z0-9!#$%&'()@^_`{}~-]{1,8}(\.[A-Za-z0-9!#$%&'()@^_`{}~-]{1,3})?$")
DOS_DEVICE_NAMES = frozenset(
    ["CON", "PRN", "AUX", "NUL", "CLOCK$"]
    + [f"COM{n}" for n in range(1, 10)]
    + [f"LPT{n}" for n in range(1, 10)])


class VfsError(Exception):
    """A field did not hold what the format requires."""


def fail(source, field, detail):
    raise VfsError(f"{source}: field '{field}': {detail}")


def parse_name(source, index, raw):
    """Decode the 13-byte 8.3 name field of one entry."""
    if b"\0" not in raw:
        fail(source, f"entry[{index}].name", f"not NUL-terminated: {raw!r}")
    name, padding = raw.split(b"\0", 1)
    if padding.strip(b"\0"):
        fail(source, f"entry[{index}].name", f"non-zero padding after NUL: {raw!r}")
    if any(b < 0x20 or b > 0x7E for b in name):
        fail(source, f"entry[{index}].name", f"not printable ASCII: {raw!r}")
    text = name.decode("ascii")
    if not NAME_PATTERN.match(text):
        fail(source, f"entry[{index}].name", f"not a DOS 8.3 name: {text!r}")
    if text.partition(".")[0].upper() in DOS_DEVICE_NAMES:
        fail(source, f"entry[{index}].name", f"names a DOS device: {text!r}")
    return text


def parse_container(data, source):
    """Return (header dict, [entry dicts]) for one container image.

    `source` only names the container in error messages; `data` is the whole
    container, since every check here is against the container's own size.
    """
    if len(data) < SIGNATURE_OFFSET + len(SIGNATURE):
        fail(source, "header", f"file is {len(data)} bytes, too short for a header")
    if data[0:3] != MAGIC:
        fail(source, "magic", f"expected {MAGIC!r}, got {data[0:3]!r}")

    version = struct.unpack_from("<H", data, 0x03)[0]
    if version != VERSION:
        fail(source, "version", f"expected {VERSION}, got {version}")

    table_offset = struct.unpack_from("<H", data, 0x05)[0]
    if not SIGNATURE_OFFSET + len(SIGNATURE) <= table_offset <= MAX_TABLE_OFFSET:
        fail(source, "table_offset", f"out of range: {table_offset}")

    count = struct.unpack_from("<I", data, 0x07)[0]
    if not 1 <= count <= MAX_ENTRIES:
        fail(source, "entry_count", f"out of range 1..{MAX_ENTRIES}: {count}")

    signature = data[SIGNATURE_OFFSET:SIGNATURE_OFFSET + len(SIGNATURE)]
    if signature != SIGNATURE:
        fail(source, "signature", f"expected {SIGNATURE!r}, got {signature!r}")

    payload_start = table_offset + ENTRY_SIZE * count
    if payload_start > len(data):
        fail(source, "entry_count", f"table of {count} entries runs past the end of the file")

    entries = []
    expected_offset = payload_start
    for index in range(count):
        base = table_offset + ENTRY_SIZE * index
        raw = data[base:base + ENTRY_SIZE]
        name = parse_name(source, index, raw[0:NAME_SIZE])
        size, size_again = struct.unpack_from("<II", raw, 0x0D)
        reserved = raw[0x15]
        offset = struct.unpack_from("<I", raw, 0x16)[0]
        if size_again != size:
            fail(source, f"entry[{index}].size_again",
                 f"{name}: expected the size {size}, got {size_again}")
        if reserved != 0:
            fail(source, f"entry[{index}].reserved", f"{name}: expected 0, got {reserved}")
        if offset != expected_offset:
            fail(source, f"entry[{index}].offset",
                 f"{name}: expected {expected_offset} (end of the previous member), got {offset}")
        if offset + size > len(data):
            fail(source, f"entry[{index}].size",
                 f"{name}: member runs past the end of the {len(data)} byte container")
        entries.append({"index": index, "name": name, "offset": offset, "size": size})
        expected_offset = offset + size

    if expected_offset != len(data):
        fail(source, f"entry[{count - 1}].size",
             f"last member ends at {expected_offset}, container is {len(data)} bytes")

    header = {
        "version": version,
        "table_offset": table_offset,
        "entry_count": count,
        "signature": signature.decode("ascii"),
        "size": len(data),
    }
    return header, entries


def is_container(data, offset, size):
    """Does the member at `offset` look like a container in its own right?"""
    return size >= SIGNATURE_OFFSET + len(SIGNATURE) and data[offset:offset + 3] == MAGIC


def clear_stale(out_dir, expected):
    """Drop anything in `out_dir` this container is not about to write.

    Two containers of the same name can hold different members — the CD copies
    of FIELD.VFS and ICONANI.VFS differ from the installed ones — so re-dumping
    over an earlier run would otherwise leave members that manifest.json does
    not describe.
    """
    for path in out_dir.iterdir():
        if path.name in expected:
            continue
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()


def extract(data, source, out_dir, rel_path, containers):
    """Extract one container into `out_dir`, recursing into nested containers.

    Appends one record per container to `containers`, outermost first, and
    returns this container's own record.
    """
    header, entries = parse_container(data, source)
    for entry in entries:
        entry["nested"] = is_container(data, entry["offset"], entry["size"])

    existed = out_dir.is_dir()
    out_dir.mkdir(parents=True, exist_ok=True)
    if existed:
        clear_stale(out_dir, {e["name"] for e in entries}
                    | {e["name"] + NESTED_SUFFIX for e in entries if e["nested"]})

    seen = {}
    record = dict(header)
    record["path"] = rel_path
    record["sha256"] = hashlib.sha256(data).hexdigest()
    record["entries"] = []
    containers.append(record)

    for entry in entries:
        name = entry["name"]
        key = name.upper()
        if key in seen:
            fail(source, f"entry[{entry['index']}].name",
                 f"{name}: already used by entry[{seen[key]}]")
        seen[key] = entry["index"]

        payload = data[entry["offset"]:entry["offset"] + entry["size"]]
        (out_dir / name).write_bytes(payload)

        member = dict(entry)
        member["sha256"] = hashlib.sha256(payload).hexdigest()
        record["entries"].append(member)

        if member["nested"]:
            extract(payload, f"{source}!{name}", out_dir / (name + NESTED_SUFFIX),
                    f"{rel_path}!{name}", containers)

    return record


def containers_in(path):
    """Return the containers to work on: one file, or every .vfs in a directory."""
    path = Path(path)
    if path.is_dir():
        found = sorted(p for p in path.iterdir() if p.suffix.lower() == ".vfs")
        if not found:
            raise VfsError(f"{path}: no .vfs files here")
        return found
    if not path.is_file():
        raise VfsError(f"{path}: no such file or directory")
    return [path]


def cmd_list(container_path):
    path = Path(container_path)
    header, entries = parse_container(path.read_bytes(), path.name)
    print(f"{path.name}: {header['entry_count']} entries, {header['size']:,} bytes, "
          f"version {header['version']}, table at {header['table_offset']}, "
          f"signature {header['signature']!r}")
    for entry in entries:
        print(f"  {entry['index']:4d}  {entry['name']:<13}{entry['size']:>10,}  "
              f"@{entry['offset']:,}")


def cmd_dump(source_path, out_path):
    out_path = Path(out_path)
    containers = []
    top_level = []
    for path in containers_in(source_path):
        record = extract(path.read_bytes(), path.name, out_path / path.stem, path.name,
                         containers)
        top_level.append(record)
        print(f"{path.name}: {record['entry_count']} entries")

    manifest = out_path / "manifest.json"
    manifest.write_text(
        json.dumps({"containers": containers}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    outer = {id(c) for c in top_level}
    nested = [c for c in containers if id(c) not in outer]
    print(f"{len(top_level)} containers, {sum(c['entry_count'] for c in top_level)} entries; "
          f"{len(nested)} nested, {sum(c['entry_count'] for c in nested)} entries -> {out_path}")
    print(f"manifest -> {manifest}")


def cmd_report(manifest_path):
    """Print the per-container summary and the full member listing as Markdown."""
    path = Path(manifest_path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise VfsError(f"{path}: not JSON: {error}") from error
    if "containers" not in manifest:
        raise VfsError(f"{path}: not a manifest, it has no 'containers'")
    containers = manifest["containers"]

    print("| 容器 | entry 數 | 容器大小 | 副檔名分布 |")
    print("| --- | ---: | ---: | --- |")
    # Nested containers are counted separately: their members sit inside a member
    # of an outer container and are already covered by that member's size.
    totals = {}
    nested_totals = {}
    for container in containers:
        nested = "!" in container["path"]
        exts = {}
        for entry in container["entries"]:
            ext = entry["name"].rpartition(".")[2] if "." in entry["name"] else "(無)"
            exts[ext] = exts.get(ext, 0) + 1
            bucket = nested_totals if nested else totals
            bucket[ext] = bucket.get(ext, 0) + 1
        spread = "、".join(f"{ext} {n}" for ext, n in sorted(exts.items()))
        print(f"| `{container['path']}` | {container['entry_count']} | "
              f"{container['size']:,} | {spread} |")
    print(f"\n最外層合計 {sum(totals.values())} 個 entry："
          + "、".join(f"{ext} {n}" for ext, n in sorted(totals.items())))
    print(f"\n巢狀容器內另有 {sum(nested_totals.values())} 個 entry："
          + "、".join(f"{ext} {n}" for ext, n in sorted(nested_totals.items())))

    for container in containers:
        print(f"\n### `{container['path']}`\n")
        print("| 檔名 | 大小 |")
        print("| --- | ---: |")
        for entry in container["entries"]:
            print(f"| `{entry['name']}` | {entry['size']:,} |")


def main(argv):
    # `report` prints Chinese, and on a zh-TW Windows the default stdout codec is
    # cp950, which mangles it the moment the output is redirected to a file.
    sys.stdout.reconfigure(encoding="utf-8")
    commands = {"list": (cmd_list, 1), "dump": (cmd_dump, 2), "report": (cmd_report, 1)}
    if len(argv) < 2 or argv[1] not in commands:
        print(__doc__, file=sys.stderr)
        return 2
    handler, arity = commands[argv[1]]
    if len(argv) != 2 + arity:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        handler(*argv[2:])
    except (VfsError, OSError) as error:
        # A missing container or a manifest that is not one is the same class of
        # failure as a field that does not fit: say what went wrong on one line
        # rather than dropping a traceback on the user.
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
