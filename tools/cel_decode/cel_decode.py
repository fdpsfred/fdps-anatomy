"""Decode the FDPS .CEL sprite sheets and render them to viewable PNGs.

The container layout and the pixel encoding were both read off FDPS.LE rather
than guessed: the sheet accessor at 0x2dba0 takes a sheet pointer and a sprite
index and hands the blitter at 0x568db a stream address, and the blitter's
mode 0 body at 0x56a0d is the RLE loop itself.

    header (15 bytes)
      +0x00  char[3]  "CEL" magic       (never read by the game)
      +0x03  u16      version, always 1 (never read by the game)
      +0x05  u16      offset of the sprite table, always 15 -- the game does not
                      read this either, 0x2dba0 hardcodes +0x0F
      +0x07  i16      sprite width,  every sprite in the sheet shares it
      +0x09  i16      sprite height, likewise
      +0x0B  i16      sprite count
      +0x0D  u16      the pixel format the authoring tool used: 2 for the RLE
                      the blitter reads, 1 for the row-terminated encoding only
                      M310.CEL carries. The game never reads this field and has
                      no second decoder, so it blits every sheet as if it said
                      2 -- see resource_info/cel.md.

    sprite table (u32 x (count + 1), immediately after the header)
      offset[i] is sprite i's stream, from the start of the file; offset[count]
      is the sentinel and equals the file size.

The stream a 0x0D of 2 selects is the same 4-op RLE the previous game used. A
command byte's top two bits pick the op and its low six bits are len-1:

    0b00  fill      write the NEXT byte to `len` pixels
    0b01  stretch   write the NEXT byte to `len` pixels at every other column,
                    covering 2*len columns and leaving the gaps untouched
    0b10  literal   copy `len` bytes straight from the stream
    0b11  skip      leave `len` pixels untouched (transparent)

The blitter walks one row at a time: BX starts each row at the sheet width and
every op subtracts the columns it covered, the row ending when BX hits exactly
zero. An op that overshot would wrap BX to a huge value and run the blitter off
into memory, so the encoder must land every row exactly on the width -- which
makes it a check worth making here rather than a flat decode that would paper
over it. The same goes for the stream ending exactly at the next sprite's
offset: the pixel budget and the byte budget have to run out together.

The one sheet whose 0x0D says 1 uses an unrelated encoding, with an explicit
end-of-row byte instead of a column count:

    0x00        end of row; whatever columns are left stay transparent
    0x01..0x7F  one pixel of that value
    0x80..0xBF  copy (b & 0x3F) + 1 literal bytes
    0xC0..0xFF  write the NEXT byte to (b & 0x3F) + 1 pixels

Its sprites decode exactly -- each one ends after `height` terminators with its
stream fully consumed -- so it is a real format rather than corruption, but
nothing in FDPS.LE reads it.

Output is one contact sheet PNG per .CEL with every sprite in it, coloured
through a 768-byte 6-bit VGA palette (any of the .PAL members of MISC.VFS).
Pixels no op ever wrote are transparent in the PNG, so a sprite's silhouette is
visible instead of being confused with palette index 0.

Usage:
    python cel_decode.py list   <cel>
    python cel_decode.py dump   <output dir> <palette.pal> <cel or directory>...
    python cel_decode.py report <manifest json>
"""

import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"CEL"
VERSION = 1
HEADER_SIZE = 15
# 0x2dba0 reads the sprite table at a hardcoded +0x0F, so a sheet whose 0x05
# says anything else would still be read from 15 and is not this format.
TABLE_OFFSET = HEADER_SIZE
TABLE_ENTRY_SIZE = 4
# Widths and heights are sign-extended by MOVSX at 0x2dbaf/0x2dbb9, and the row
# counter at 0x56a0d is 16-bit, so a dimension outside this range could not be
# blitted.
MAX_DIMENSION = 0x7FFF
OP_FILL = 0x00
OP_STRETCH = 0x40
OP_LITERAL = 0x80
OP_SKIP = 0xC0
# Field 0x0D. FORMAT_BLIT is the encoding the blitter at 0x56a0d reads;
# FORMAT_ROWS is the row-terminated one only M310.CEL carries.
FORMAT_BLIT = 2
FORMAT_ROWS = 1
ROWS_END_OF_ROW = 0x00
ROWS_LITERAL = 0x80
ROWS_FILL = 0xC0
PALETTE_SIZE = 768
# The VGA DAC takes 6 bits per channel, so no byte of a real palette exceeds
# this; load_palette expands them back to 8 bits.
PALETTE_MAX = 63
SHEET_GAP = 1
SHEET_MAX_WIDTH = 2048
SHEET_BACKGROUND = (0, 0, 0, 0)


class CelError(Exception):
    """A field or a stream did not hold what the format requires."""


def fail(source, field, detail):
    raise CelError(f"{source}: field '{field}': {detail}")


def parse_header(data, source):
    """Return (header dict, [sprite dicts]) for one .CEL image."""
    if len(data) < HEADER_SIZE:
        fail(source, "header", f"file is {len(data)} bytes, too short for a header")
    if data[0:3] != MAGIC:
        fail(source, "magic", f"expected {MAGIC!r}, got {data[0:3]!r}")

    version, table_offset = struct.unpack_from("<HH", data, 0x03)
    width, height, count = struct.unpack_from("<hhh", data, 0x07)
    pixel_format = struct.unpack_from("<H", data, 0x0D)[0]

    if version != VERSION:
        fail(source, "version", f"expected {VERSION}, got {version}")
    if table_offset != TABLE_OFFSET:
        fail(source, "table_offset",
             f"expected {TABLE_OFFSET}, got {table_offset}; the game reads the "
             f"table from {TABLE_OFFSET} regardless")
    if not 1 <= width <= MAX_DIMENSION:
        fail(source, "width", f"out of range 1..{MAX_DIMENSION}: {width}")
    if not 1 <= height <= MAX_DIMENSION:
        fail(source, "height", f"out of range 1..{MAX_DIMENSION}: {height}")
    if count < 1:
        fail(source, "count", f"out of range: {count}")
    if pixel_format not in (FORMAT_BLIT, FORMAT_ROWS):
        fail(source, "pixel_format",
             f"expected {FORMAT_BLIT} or {FORMAT_ROWS}, got {pixel_format}")

    table_end = TABLE_OFFSET + TABLE_ENTRY_SIZE * (count + 1)
    if table_end > len(data):
        fail(source, "count",
             f"a table of {count + 1} offsets runs past the end of the file")

    offsets = list(struct.unpack_from(f"<{count + 1}I", data, TABLE_OFFSET))
    if offsets[count] != len(data):
        fail(source, f"offset[{count}]",
             f"sentinel is {offsets[count]}, file is {len(data)} bytes")
    if offsets[0] < table_end:
        fail(source, "offset[0]",
             f"first sprite starts at {offsets[0]}, inside the table that ends "
             f"at {table_end}")

    sprites = []
    for index in range(count):
        start, end = offsets[index], offsets[index + 1]
        if end < start:
            fail(source, f"offset[{index + 1}]",
                 f"{end} is before sprite {index}'s start {start}")
        sprites.append({"index": index, "offset": start, "size": end - start})

    header = {
        "version": version,
        "table_offset": table_offset,
        "width": width,
        "height": height,
        "count": count,
        "pixel_format": pixel_format,
        "size": len(data),
        # Everything between the table and the first sprite is addressed by no
        # offset. It is leftover encoder output, not a field: it decodes as
        # partial sprites and the game never walks it.
        "unreferenced_bytes": offsets[0] - table_end,
    }
    return header, sprites


def decode_sprite_blit(stream, width, height, source, index):
    """Replay the blitter at 0x56a0d over one sprite's stream.

    Returns (pixels, mask, consumed): `pixels` and `mask` are width*height
    bytes, mask byte 1 marking a pixel some op actually wrote, and `consumed` is
    how many stream bytes that took.
    """
    pixels = bytearray(width * height)
    mask = bytearray(width * height)
    read = 0
    limit = len(stream)

    for row in range(height):
        base = row * width
        column = 0
        while column < width:
            if read >= limit:
                fail(source, f"sprite[{index}]",
                     f"stream ran out on row {row} column {column} of {width}")
            command = stream[read]
            read += 1
            length = (command & 0x3F) + 1
            op = command & 0xC0

            covered = length * 2 if op == OP_STRETCH else length
            if column + covered > width:
                fail(source, f"sprite[{index}]",
                     f"row {row}: an op covering {covered} columns starts at "
                     f"column {column} of {width}")

            if op == OP_FILL:
                if read >= limit:
                    fail(source, f"sprite[{index}]",
                         f"row {row}: a fill's value byte is past the end of the stream")
                value = stream[read]
                read += 1
                at = base + column
                pixels[at:at + length] = bytes([value]) * length
                mask[at:at + length] = b"\x01" * length
                column += length
            elif op == OP_STRETCH:
                if read >= limit:
                    fail(source, f"sprite[{index}]",
                         f"row {row}: a stretch's value byte is past the end of the stream")
                value = stream[read]
                read += 1
                # The blitter does INC EDI then STOSB, so it writes the second
                # pixel of each pair and steps over the first.
                for step in range(length):
                    at = base + column + step * 2 + 1
                    pixels[at] = value
                    mask[at] = 1
                column += covered
            elif op == OP_LITERAL:
                if read + length > limit:
                    fail(source, f"sprite[{index}]",
                         f"row {row}: a literal run of {length} bytes is past the "
                         f"end of the stream")
                at = base + column
                pixels[at:at + length] = stream[read:read + length]
                mask[at:at + length] = b"\x01" * length
                read += length
                column += length
            else:
                column += length

    if read != limit:
        fail(source, f"sprite[{index}]",
             f"the sprite ends after {read} bytes, its stream is {limit}")
    return bytes(pixels), bytes(mask), read


def decode_sprite_rows(stream, width, height, source, index):
    """Decode one sprite of the row-terminated encoding (pixel_format 1).

    Same return shape as decode_sprite_blit. A row ends on its 0x00 byte with any
    remaining columns left transparent, so the exactness this checks is the row
    count and the byte count rather than the column count.
    """
    pixels = bytearray(width * height)
    mask = bytearray(width * height)
    read = 0
    limit = len(stream)

    for row in range(height):
        base = row * width
        column = 0
        while True:
            if read >= limit:
                fail(source, f"sprite[{index}]",
                     f"stream ran out on row {row} column {column} of {width}")
            command = stream[read]
            read += 1
            if command == ROWS_END_OF_ROW:
                break

            if command < ROWS_LITERAL:
                length = 1
            else:
                length = (command & 0x3F) + 1
            if column + length > width:
                fail(source, f"sprite[{index}]",
                     f"row {row}: an op covering {length} columns starts at "
                     f"column {column} of {width}")

            at = base + column
            if command < ROWS_LITERAL:
                pixels[at] = command
            elif command < ROWS_FILL:
                if read + length > limit:
                    fail(source, f"sprite[{index}]",
                         f"row {row}: a literal run of {length} bytes is past the "
                         f"end of the stream")
                pixels[at:at + length] = stream[read:read + length]
                read += length
            else:
                if read >= limit:
                    fail(source, f"sprite[{index}]",
                         f"row {row}: a fill's value byte is past the end of the stream")
                pixels[at:at + length] = bytes([stream[read]]) * length
                read += 1
            mask[at:at + length] = b"\x01" * length
            column += length

    if read != limit:
        fail(source, f"sprite[{index}]",
             f"the sprite ends after {read} bytes, its stream is {limit}")
    return bytes(pixels), bytes(mask), read


DECODERS = {FORMAT_BLIT: decode_sprite_blit, FORMAT_ROWS: decode_sprite_rows}


def decode_sheet(data, source):
    """Decode every sprite in one .CEL. Returns (header, sprites, pixel data)."""
    header, sprites = parse_header(data, source)
    decode = DECODERS[header["pixel_format"]]
    decoded = []
    for sprite in sprites:
        start = sprite["offset"]
        stream = data[start:start + sprite["size"]]
        pixels, mask, _ = decode(stream, header["width"], header["height"],
                                 source, sprite["index"])
        sprite["opaque"] = sum(mask)
        decoded.append((pixels, mask))
    return header, sprites, decoded


def load_palette(path):
    """Turn a 768-byte 6-bit VGA DAC palette into 256 opaque (r, g, b, a) tuples."""
    raw = Path(path).read_bytes()
    if len(raw) != PALETTE_SIZE:
        raise CelError(f"{path}: a palette is {PALETTE_SIZE} bytes, this is {len(raw)}")
    if max(raw) > PALETTE_MAX:
        raise CelError(f"{path}: byte {max(raw)} is past the 6-bit DAC range, "
                       f"this is not a VGA palette")
    return [(raw[i * 3] << 2 | raw[i * 3] >> 4,
             raw[i * 3 + 1] << 2 | raw[i * 3 + 1] >> 4,
             raw[i * 3 + 2] << 2 | raw[i * 3 + 2] >> 4,
             255) for i in range(256)]


def write_png(path, width, height, rows):
    """Write 8-bit RGBA rows (each width*4 bytes) as a PNG."""
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    raw = bytearray()
    for row in rows:
        raw.append(0)          # filter type 0, no prediction
        raw.extend(row)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def sheet_layout(count, width, height):
    """Pick a grid for the contact sheet: as square as fits the width cap."""
    columns = max(1, min(count, (SHEET_MAX_WIDTH + SHEET_GAP) // (width + SHEET_GAP)))
    while columns > 1 and columns * columns > count * 2:
        columns -= 1
    rows = (count + columns - 1) // columns
    return columns, rows


def write_contact_sheet(path, header, decoded, palette):
    """Render every sprite of one sheet into a single PNG, left to right."""
    width, height = header["width"], header["height"]
    columns, rows = sheet_layout(len(decoded), width, height)
    sheet_width = columns * width + (columns - 1) * SHEET_GAP
    sheet_height = rows * height + (rows - 1) * SHEET_GAP
    background = bytes(SHEET_BACKGROUND) * sheet_width
    canvas = [bytearray(background) for _ in range(sheet_height)]

    for index, (pixels, mask) in enumerate(decoded):
        left = (index % columns) * (width + SHEET_GAP)
        top = (index // columns) * (height + SHEET_GAP)
        for y in range(height):
            row = canvas[top + y]
            for x in range(width):
                at = y * width + x
                colour = palette[pixels[at]] if mask[at] else SHEET_BACKGROUND
                row[(left + x) * 4:(left + x) * 4 + 4] = bytes(colour)
    write_png(path, sheet_width, sheet_height, canvas)
    return sheet_width, sheet_height


def sheets_in(path):
    """Return the sheets to work on: one file, or every .cel under a directory."""
    path = Path(path)
    if path.is_dir():
        found = sorted(p for p in path.rglob("*") if p.suffix.lower() == ".cel")
        if not found:
            raise CelError(f"{path}: no .cel files here")
        return found
    if not path.is_file():
        raise CelError(f"{path}: no such file or directory")
    return [path]


def cmd_list(sheet_path):
    path = Path(sheet_path)
    header, sprites = parse_header(path.read_bytes(), path.name)
    print(f"{path.name}: {header['count']} sprites of {header['width']}x"
          f"{header['height']}, {header['size']:,} bytes, version "
          f"{header['version']}, pixel format {header['pixel_format']}, "
          f"{header['unreferenced_bytes']} unreferenced bytes")
    for sprite in sprites:
        print(f"  {sprite['index']:4d}  {sprite['size']:>8,}  @{sprite['offset']:,}")


def cmd_dump(out_path, palette_path, *source_paths):
    out_path = Path(out_path)
    palette = load_palette(palette_path)
    sheets = []
    found = [path for source in source_paths for path in sheets_in(source)]
    seen = {}
    for path in found:
        if path.stem.upper() in seen:
            raise CelError(f"{path}: its name collides with {seen[path.stem.upper()]}, "
                           f"both would write the same contact sheet")
        seen[path.stem.upper()] = path
    for path in found:
        data = path.read_bytes()
        header, sprites, decoded = decode_sheet(data, path.name)
        png = out_path / (path.stem + ".png")
        sheet_width, sheet_height = write_contact_sheet(png, header, decoded, palette)

        record = dict(header)
        record["path"] = path.name
        record["sha256"] = hashlib.sha256(data).hexdigest()
        record["sheet_png"] = png.name
        record["sheet_size"] = [sheet_width, sheet_height]
        record["opaque"] = sum(s["opaque"] for s in sprites)
        record["pixels"] = header["width"] * header["height"] * header["count"]
        record["sprites"] = sprites
        sheets.append(record)
        print(f"{path.name}: {header['count']} sprites of {header['width']}x"
              f"{header['height']} -> {png.name}")

    manifest = out_path / "manifest.json"
    manifest.write_text(
        json.dumps({"palette": Path(palette_path).name, "sheets": sheets},
                   indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"{len(sheets)} sheets, {sum(s['count'] for s in sheets)} sprites, "
          f"{sum(s['pixels'] for s in sheets):,} pixels -> {out_path}")
    print(f"manifest -> {manifest}")


def cmd_report(manifest_path):
    """Print the per-sheet summary as Markdown."""
    path = Path(manifest_path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise CelError(f"{path}: not JSON: {error}") from error
    if "sheets" not in manifest:
        raise CelError(f"{path}: not a manifest, it has no 'sheets'")
    sheets = manifest["sheets"]

    print("| 檔案 | sprite 數 | 尺寸 | 像素格式 |")
    print("| --- | ---: | ---: | ---: |")
    for sheet in sheets:
        print(f"| `{sheet['path']}` | {sheet['count']} | "
              f"{sheet['width']}×{sheet['height']} | {sheet['pixel_format']} |")
    print(f"\n{len(sheets)} 個檔、{sum(s['count'] for s in sheets)} 個 sprite、"
          f"{sum(s['pixels'] for s in sheets):,} 個像素全部解碼成功。")
    slack = [s for s in sheets if s["unreferenced_bytes"]]
    if slack:
        print("\n偏移表與第一個 sprite 之間留有沒被任何偏移指到的 byte 的檔案："
              + "、".join(f"`{s['path']}` {s['unreferenced_bytes']}"
                          for s in slack))


def main(argv):
    # `report` prints Chinese, and on a zh-TW Windows the default stdout codec
    # is cp950, which mangles it the moment the output is redirected to a file.
    sys.stdout.reconfigure(encoding="utf-8")
    # `dump` takes at least three arguments and any number of sources after
    # them; the others take exactly what they say.
    commands = {"list": (cmd_list, 1, 1), "dump": (cmd_dump, 3, None),
                "report": (cmd_report, 1, 1)}
    if len(argv) < 2 or argv[1] not in commands:
        print(__doc__, file=sys.stderr)
        return 2
    handler, least, most = commands[argv[1]]
    if len(argv) - 2 < least or (most is not None and len(argv) - 2 > most):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        handler(*argv[2:])
    except (CelError, OSError) as error:
        # A stream that does not decode is the same class of failure as a field
        # that does not fit: say what went wrong on one line rather than
        # dropping a traceback on the user.
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
