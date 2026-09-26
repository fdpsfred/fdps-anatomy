"""Decode the FDPS .SAF animation containers, verify them and render them.

Every field below was read off FDPS.LE rather than guessed. The four section
tables can only be reached by doing saf + *(u32 *)(saf + 0x0E / 0x18 / 0x22 /
0x2C), and only seven functions in the whole program do that:

    0x144e0  frame count; checks the magic and returns the header's 0x0C. Its
             check is p[0] == 'S' || p[1] == 'A' || p[2] == 'F' -- an OR, so it
             passes almost anything. This decoder demands all three, because
             here a file that is not a .SAF is a bug worth reporting.
    0x140e0  frame pointer for an index, bounds-checked against 0x0C
    0x14550  advance the animation one tick, using the frame's duration
    0x14140  draw one frame: walk its layers, then fire its sound
    0x13fd0  draw one layer: walk a tilemap's cells
    0x13ed0  draw one cell: hand a tile's stream to the blitter at 0x568db
    0x142d0  play a sound through AIL

    header (52 bytes)
      +0x00  char[3]  "SAF" magic
      +0x03  u16      version, always 6      (never read by the game)
      +0x05  u16      header size, always 52 (never read by the game)
      +0x07  u16      cell width  in pixels, 24 in every file
      +0x09  u16      cell height in pixels, 24 in every file
      +0x0B  u8       always 0               (never read by the game)
      +0x0C  4 x { u16 count; u32 offset; u32 size }   the four sections

    Section 0 is the frames, 1 the tilemaps, 2 the tiles and 3 the sounds.
    Sections run back to back: the first starts at 52, each one starts where
    the last ended, and the fourth ends at the end of the file. Each section
    opens with `count` u32 offsets, from the start of the file, and its items
    follow with no gaps -- so item i runs to offset[i+1], and the last one to
    the end of the section. None of the seven reads the sizes at +0x12, +0x1C,
    +0x26 and +0x30; they only ever take items through a count and an offset
    table, and never walk off the end of the last one. Those sizes are what
    makes the file walkable without decoding it, which is what this tool wants.

    frame (10 bytes plus 13 per layer)
      +0x00  i16  sound to fire with this frame, -1 for none
      +0x02  i16  how many ticks the frame is held for
      +0x04  u8   a frame index the caller reads for its own purposes (only
                  frame 0's is read). The player itself ignores this byte and
                  the next one; the only readers are the attack sequence at
                  0x196d0 and the spell sequence at 0x1a4c0 (both bytes), the
                  battle opening at 0x18d60 (this byte), and the credit roll at
                  0x1ba40 (reads this byte and never uses it). All of them
                  reach a frame through 0x140e0 rather than through the
                  section tables.
      +0x05  u8   the impact marker: the attack and spell sequences count these
                  frames and drain the target's HP bar across them
      +0x06  u16  always 0 (never read by the game)
      +0x08  i16  layer count
      +0x0A  the layers

    layer (13 bytes)
      +0x00  u16  which tilemap to draw
      +0x02  i16  x, from the animation's origin
      +0x04  i16  y, likewise
      +0x06  u8   0 to blit opaque, 1 to blit translucent
      +0x07  u8   translucency, 0..16; the player passes 16 - it to blit mode 9
      +0x08  5 bytes, always 0

    tilemap (4 bytes plus 2 per cell)
      +0x00  i16  columns
      +0x02  i16  rows
      +0x04  i16 x (columns * rows), each an index into the tile section, in
                  row-major order. Signed: 0x13fd0 loads a cell through a
                  short * and 0x13ed0 guards it with -1 < index. The layer's
                  tilemap index at layer+0x00 is zero-extended instead, so the
                  two are not the same type.

A tile is one cell-sized sprite in the same 4-op RLE the .CEL sheets use --
0x13ed0 hands its stream to the same blitter at 0x568db, so it is the same
encoding by construction rather than by resemblance. A command byte's top two
bits pick the op and its low six bits are len-1:

    0b00  fill      write the NEXT byte to `len` pixels
    0b01  stretch   write the NEXT byte to `len` pixels at every other column,
                    covering 2*len columns and leaving the gaps untouched
    0b10  literal   copy `len` bytes straight from the stream
    0b11  skip      leave `len` pixels untouched (transparent)

The blitter walks one row at a time and ends the row when its column counter
hits exactly zero, so an op that overshot would run it off into memory. Every
row landing exactly on the cell width, and every stream ending exactly at the
next tile's offset, are therefore hard conditions rather than niceties.

    sound (8 bytes plus the samples)
      +0x00  u8   channels, 1 everywhere
      +0x01  u8   bits per sample, 8 everywhere
      +0x02  u16  sample rate in Hz
      +0x04  u32  how many bytes of samples follow
      +0x08  unsigned PCM

Usage:
    python saf_decode.py list   <saf>
    python saf_decode.py verify <output dir> <saf or directory>...
    python saf_decode.py dump   <output dir> <palette.pal> <saf or directory>...
    python saf_decode.py report <manifest json>
"""

import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"SAF"
VERSION = 6
HEADER_SIZE = 52
SECTION_COUNT = 4
SECTION_DESCRIPTOR_SIZE = 10
FRAMES, TILEMAPS, TILES, SOUNDS = range(SECTION_COUNT)
SECTION_NAMES = ("frames", "tilemaps", "tiles", "sounds")

FRAME_HEADER_SIZE = 10
LAYER_SIZE = 13
LAYER_RESERVED = 5
TILEMAP_HEADER_SIZE = 4
SOUND_HEADER_SIZE = 8
# 0x14247 does MOVSX word ptr [layer + 7] and 0x1424e subtracts it from 16, so
# a layer past this would ask the blitter for a negative merge level.
MAX_TRANSLUCENCY = 16
NO_SOUND = -1

OP_FILL = 0x00
OP_STRETCH = 0x40
OP_LITERAL = 0x80
OP_SKIP = 0xC0

PALETTE_SIZE = 768
# The VGA DAC takes 6 bits per channel, so no byte of a real palette exceeds
# this; load_palette expands them back to 8 bits.
PALETTE_MAX = 63

# 0x196d0 allocates a 0x170-wide back buffer, draws the animation with its
# origin at (0x18, 0x18) and blits 320x200 starting from that origin, so a
# layer's x and y are screen pixels and this is the frame the player sees.
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
STRIP_GAP = 2
STRIP_MAX_WIDTH = 2048
STRIP_BACKGROUND = (0, 0, 0, 0)

WAV_HEADER_SIZE = 44


class SafError(Exception):
    """A field or a stream did not hold what the format requires."""


def fail(source, field, detail):
    raise SafError(f"{source}: field '{field}': {detail}")


def parse_header(data, source):
    """Return (header dict, [section dicts]) for one .SAF file."""
    if len(data) < HEADER_SIZE:
        fail(source, "header", f"file is {len(data)} bytes, too short for a header")
    if data[0:3] != MAGIC:
        fail(source, "magic", f"expected {MAGIC!r}, got {data[0:3]!r}")

    version, header_size = struct.unpack_from("<HH", data, 0x03)
    cell_width, cell_height = struct.unpack_from("<HH", data, 0x07)
    padding = data[0x0B]

    if version != VERSION:
        fail(source, "version", f"expected {VERSION}, got {version}")
    if header_size != HEADER_SIZE:
        fail(source, "header_size", f"expected {HEADER_SIZE}, got {header_size}")
    if cell_width < 1 or cell_height < 1:
        fail(source, "cell", f"a cell of {cell_width}x{cell_height} has no pixels")
    if padding != 0:
        fail(source, "padding", f"expected 0, got {padding}")

    sections = []
    start = HEADER_SIZE
    for index in range(SECTION_COUNT):
        at = 0x0C + index * SECTION_DESCRIPTOR_SIZE
        count, offset, size = struct.unpack_from("<HII", data, at)
        name = SECTION_NAMES[index]
        if offset != start:
            fail(source, f"{name}.offset",
                 f"section starts at {offset}, the one before it ended at {start}")
        if offset + size > len(data):
            fail(source, f"{name}.size",
                 f"a section of {size} bytes at {offset} runs past the end of "
                 f"the {len(data)}-byte file")
        sections.append(parse_section(data, source, name, count, offset, size))
        start = offset + size
    if start != len(data):
        fail(source, "sounds.size",
             f"the sections end at {start}, the file is {len(data)} bytes")

    header = {
        "version": version,
        "header_size": header_size,
        "cell_width": cell_width,
        "cell_height": cell_height,
        "size": len(data),
    }
    return header, sections


def parse_section(data, source, name, count, offset, size):
    """Split one section into its items using its own offset table."""
    section = {"name": name, "count": count, "offset": offset, "size": size,
               "items": []}
    if count == 0:
        if size != 0:
            fail(source, f"{name}.size",
                 f"an empty section still claims {size} bytes")
        return section

    table_end = offset + 4 * count
    if table_end > offset + size:
        fail(source, f"{name}.count",
             f"a table of {count} offsets does not fit in {size} bytes")
    offsets = list(struct.unpack_from(f"<{count}I", data, offset))
    if offsets[0] != table_end:
        fail(source, f"{name}.offset[0]",
             f"the first item starts at {offsets[0]}, its table ends at {table_end}")
    offsets.append(offset + size)
    for index in range(count):
        start, end = offsets[index], offsets[index + 1]
        if end <= start:
            fail(source, f"{name}.offset[{index + 1}]",
                 f"{end} is not past item {index}'s start {start}")
        section["items"].append({"index": index, "offset": start,
                                 "size": end - start})
    return section


def parse_frame(item, source, index, tilemap_count, sound_count):
    """Read one frame record and the layers hanging off it."""
    data = item
    where = f"frames[{index}]"
    if len(data) < FRAME_HEADER_SIZE:
        fail(source, where, f"{len(data)} bytes is too short for a frame header")
    sound, duration, cue, impact, reserved, layer_count = \
        struct.unpack_from("<hhBBHh", data, 0)
    if reserved != 0:
        fail(source, f"{where}.reserved", f"expected 0, got {reserved}")
    if duration < 1:
        fail(source, f"{where}.duration",
             f"a frame held for {duration} ticks would never be shown")
    if sound != NO_SOUND and not 0 <= sound < sound_count:
        fail(source, f"{where}.sound",
             f"{sound} is neither {NO_SOUND} nor one of the {sound_count} sounds")
    if layer_count < 0:
        fail(source, f"{where}.layer_count", f"out of range: {layer_count}")
    if len(data) != FRAME_HEADER_SIZE + LAYER_SIZE * layer_count:
        fail(source, where,
             f"{layer_count} layers need "
             f"{FRAME_HEADER_SIZE + LAYER_SIZE * layer_count} bytes, the record "
             f"is {len(data)}")

    layers = []
    for slot in range(layer_count):
        at = FRAME_HEADER_SIZE + LAYER_SIZE * slot
        tilemap, x, y = struct.unpack_from("<Hhh", data, at)
        blend, translucency = data[at + 6], data[at + 7]
        tail = data[at + 8:at + LAYER_SIZE]
        if tilemap >= tilemap_count:
            fail(source, f"{where}.layer[{slot}].tilemap",
                 f"{tilemap} is past the {tilemap_count} tilemaps")
        if blend not in (0, 1):
            fail(source, f"{where}.layer[{slot}].blend",
                 f"expected 0 or 1, got {blend}")
        if translucency > MAX_TRANSLUCENCY:
            fail(source, f"{where}.layer[{slot}].translucency",
                 f"{translucency} is past {MAX_TRANSLUCENCY}")
        if tail != bytes(LAYER_RESERVED):
            fail(source, f"{where}.layer[{slot}].reserved",
                 f"expected {LAYER_RESERVED} zero bytes, got {tail.hex()}")
        layers.append({"tilemap": tilemap, "x": x, "y": y,
                       "blend": blend, "translucency": translucency})

    return {"index": index, "sound": sound, "duration": duration, "cue": cue,
            "impact": impact, "layers": layers}


def parse_tilemap(item, source, index, tile_count):
    """Read one tilemap: its grid size and the tile index of every cell."""
    where = f"tilemaps[{index}]"
    if len(item) < TILEMAP_HEADER_SIZE:
        fail(source, where, f"{len(item)} bytes is too short for a tilemap header")
    columns, rows = struct.unpack_from("<hh", item, 0)
    if columns < 0 or rows < 0:
        fail(source, where, f"a grid of {columns}x{rows} is not a grid")
    if len(item) != TILEMAP_HEADER_SIZE + 2 * columns * rows:
        fail(source, where,
             f"a {columns}x{rows} grid needs "
             f"{TILEMAP_HEADER_SIZE + 2 * columns * rows} bytes, the record is "
             f"{len(item)}")
    # Signed, because 0x13fd0 loads a cell through a short * and 0x13ed0 bounds
    # it with -1 < index. A cell past 0x7FFF would be a negative index there.
    cells = list(struct.unpack_from(f"<{columns * rows}h", item,
                                    TILEMAP_HEADER_SIZE)) if columns * rows else []
    for cell, tile in enumerate(cells):
        if not 0 <= tile < tile_count:
            fail(source, f"{where}.cell[{cell}]",
                 f"tile {tile} is not one of the {tile_count} tiles")
    return {"index": index, "columns": columns, "rows": rows, "cells": cells}


def decode_tile(stream, width, height, source, index):
    """Replay the blitter at 0x56a0d over one tile's stream.

    Returns (pixels, mask): both width*height bytes, mask byte 1 marking a
    pixel some op actually wrote.
    """
    pixels = bytearray(width * height)
    mask = bytearray(width * height)
    read = 0
    limit = len(stream)
    where = f"tiles[{index}]"

    for row in range(height):
        base = row * width
        column = 0
        while column < width:
            if read >= limit:
                fail(source, where,
                     f"stream ran out on row {row} column {column} of {width}")
            command = stream[read]
            read += 1
            length = (command & 0x3F) + 1
            op = command & 0xC0

            covered = length * 2 if op == OP_STRETCH else length
            if column + covered > width:
                fail(source, where,
                     f"row {row}: an op covering {covered} columns starts at "
                     f"column {column} of {width}")

            if op == OP_FILL:
                if read >= limit:
                    fail(source, where,
                         f"row {row}: a fill's value byte is past the end of the stream")
                value = stream[read]
                read += 1
                at = base + column
                pixels[at:at + length] = bytes([value]) * length
                mask[at:at + length] = b"\x01" * length
                column += length
            elif op == OP_STRETCH:
                if read >= limit:
                    fail(source, where,
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
                    fail(source, where,
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
        fail(source, where, f"the tile ends after {read} bytes, its stream is {limit}")
    return bytes(pixels), bytes(mask)


def parse_sound(item, source, index):
    """Read one sound's header and hand back its samples."""
    where = f"sounds[{index}]"
    if len(item) < SOUND_HEADER_SIZE:
        fail(source, where, f"{len(item)} bytes is too short for a sound header")
    channels, bits = item[0], item[1]
    rate, length = struct.unpack_from("<HI", item, 2)
    if channels < 1:
        fail(source, f"{where}.channels", f"out of range: {channels}")
    if bits not in (8, 16):
        fail(source, f"{where}.bits", f"expected 8 or 16, got {bits}")
    if rate < 1:
        fail(source, f"{where}.rate", f"out of range: {rate}")
    if len(item) != SOUND_HEADER_SIZE + length:
        fail(source, where,
             f"{length} bytes of samples need {SOUND_HEADER_SIZE + length} "
             f"bytes, the record is {len(item)}")
    return {"index": index, "channels": channels, "bits": bits, "rate": rate,
            "length": length, "samples": item[SOUND_HEADER_SIZE:]}


def decode_animation(data, source):
    """Parse and decode one whole .SAF. Returns (header, sections, decoded)."""
    header, sections = parse_header(data, source)
    counts = [section["count"] for section in sections]

    def items(which):
        return [data[item["offset"]:item["offset"] + item["size"]]
                for item in sections[which]["items"]]

    frames = [parse_frame(item, source, index, counts[TILEMAPS], counts[SOUNDS])
              for index, item in enumerate(items(FRAMES))]
    tilemaps = [parse_tilemap(item, source, index, counts[TILES])
                for index, item in enumerate(items(TILEMAPS))]
    tiles = [decode_tile(item, header["cell_width"], header["cell_height"],
                         source, index)
             for index, item in enumerate(items(TILES))]
    sounds = [parse_sound(item, source, index)
              for index, item in enumerate(items(SOUNDS))]
    return header, sections, {"frames": frames, "tilemaps": tilemaps,
                              "tiles": tiles, "sounds": sounds}


def load_palette(path):
    """Turn a 768-byte 6-bit VGA DAC palette into 256 opaque (r, g, b, a) tuples."""
    raw = Path(path).read_bytes()
    if len(raw) != PALETTE_SIZE:
        raise SafError(f"{path}: a palette is {PALETTE_SIZE} bytes, this is {len(raw)}")
    if max(raw) > PALETTE_MAX:
        raise SafError(f"{path}: byte {max(raw)} is past the 6-bit DAC range, "
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


def write_wav(path, sound):
    """Write one sound out as a RIFF/WAVE file so it can just be played."""
    channels, bits, rate = sound["channels"], sound["bits"], sound["rate"]
    block = channels * (bits // 8)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"RIFF" + struct.pack("<I", WAV_HEADER_SIZE - 8 + len(sound["samples"]))
        + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate,
                                    rate * block, block, bits)
        + b"data" + struct.pack("<I", len(sound["samples"])) + sound["samples"])


def compose_frame(frame, decoded, header):
    """Draw one frame the way 0x14140 does. Returns (pixels, mask) of a screen."""
    width, height = SCREEN_WIDTH, SCREEN_HEIGHT
    cell_width, cell_height = header["cell_width"], header["cell_height"]
    pixels = bytearray(width * height)
    mask = bytearray(width * height)

    for layer in frame["layers"]:
        tilemap = decoded["tilemaps"][layer["tilemap"]]
        for row in range(tilemap["rows"]):
            for column in range(tilemap["columns"]):
                tile, tile_mask = decoded["tiles"][
                    tilemap["cells"][row * tilemap["columns"] + column]]
                left = layer["x"] + column * cell_width
                top = layer["y"] + row * cell_height
                for y in range(cell_height):
                    if not 0 <= top + y < height:
                        continue
                    for x in range(cell_width):
                        at = y * cell_width + x
                        if not tile_mask[at] or not 0 <= left + x < width:
                            continue
                        to = (top + y) * width + left + x
                        pixels[to] = tile[at]
                        mask[to] = 1
    return pixels, mask


def write_filmstrip(path, frames, decoded, header, palette):
    """Render every frame of one animation into a single PNG, left to right."""
    count = max(1, len(frames))
    columns = max(1, min(count, (STRIP_MAX_WIDTH + STRIP_GAP)
                         // (SCREEN_WIDTH + STRIP_GAP)))
    rows = (count + columns - 1) // columns
    strip_width = columns * SCREEN_WIDTH + (columns - 1) * STRIP_GAP
    strip_height = rows * SCREEN_HEIGHT + (rows - 1) * STRIP_GAP
    background = bytes(STRIP_BACKGROUND) * strip_width
    canvas = [bytearray(background) for _ in range(strip_height)]

    for index, frame in enumerate(frames):
        pixels, mask = compose_frame(frame, decoded, header)
        left = (index % columns) * (SCREEN_WIDTH + STRIP_GAP)
        top = (index // columns) * (SCREEN_HEIGHT + STRIP_GAP)
        for y in range(SCREEN_HEIGHT):
            row = canvas[top + y]
            for x in range(SCREEN_WIDTH):
                at = y * SCREEN_WIDTH + x
                colour = palette[pixels[at]] if mask[at] else STRIP_BACKGROUND
                row[(left + x) * 4:(left + x) * 4 + 4] = bytes(colour)
    write_png(path, strip_width, strip_height, canvas)
    return strip_width, strip_height


def animations_in(path):
    """Return the files to work on: one file, or every .saf under a directory."""
    path = Path(path)
    if path.is_dir():
        found = sorted(p for p in path.rglob("*") if p.suffix.lower() == ".saf")
        if not found:
            raise SafError(f"{path}: no .saf files here")
        return found
    if not path.is_file():
        raise SafError(f"{path}: no such file or directory")
    return [path]


def gather(source_paths):
    """Resolve the sources to a list of distinct files with distinct stems.

    Sources are allowed to overlap -- naming a directory and one file inside it
    is a natural way to ask for both -- so the same file reached twice is one
    file, while two different files sharing a stem are not, since they would
    write over each other's output.
    """
    found = []
    resolved = set()
    for source in source_paths:
        for path in animations_in(source):
            key = path.resolve()
            if key not in resolved:
                resolved.add(key)
                found.append(path)
    seen = {}
    for path in found:
        stem = path.stem.upper()
        if stem in seen:
            raise SafError(f"{path}: its name collides with {seen[stem]}, both "
                           f"would write the same output")
        seen[stem] = path
    return found


def summarise(path, data, header, sections, decoded):
    """One manifest record: what the file holds and what it cost to hold it."""
    frames, tilemaps, sounds = (decoded["frames"], decoded["tilemaps"],
                                decoded["sounds"])
    record = dict(header)
    record["path"] = path.name
    record["sha256"] = hashlib.sha256(data).hexdigest()
    for section in sections:
        record[section["name"]] = section["count"]
        record[section["name"] + "_bytes"] = section["size"]
    record["layers"] = sum(len(frame["layers"]) for frame in frames)
    record["cells"] = sum(len(tilemap["cells"]) for tilemap in tilemaps)
    record["ticks"] = sum(frame["duration"] for frame in frames)
    record["impacts"] = sum(1 for frame in frames if frame["impact"])
    record["cued"] = sum(1 for frame in frames if frame["sound"] != NO_SOUND)
    record["sample_bytes"] = sum(sound["length"] for sound in sounds)
    record["opaque"] = sum(sum(mask) for _, mask in decoded["tiles"])
    record["pixels"] = (header["cell_width"] * header["cell_height"]
                        * sections[TILES]["count"])
    return record


def cmd_list(animation_path):
    path = Path(animation_path)
    data = path.read_bytes()
    header, sections = parse_header(data, path.name)
    print(f"{path.name}: {header['size']:,} bytes, version {header['version']}, "
          f"{header['cell_width']}x{header['cell_height']} cells")
    for section in sections:
        print(f"  {section['name']:<9} {section['count']:>6} items  "
              f"{section['size']:>9,} bytes  @{section['offset']:,}")


def cmd_verify(out_path, *source_paths):
    out_path = Path(out_path)
    animations = []
    for path in gather(source_paths):
        data = path.read_bytes()
        header, sections, decoded = decode_animation(data, path.name)
        animations.append(summarise(path, data, header, sections, decoded))

    manifest = out_path / "manifest.json"
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(
        json.dumps({"animations": animations}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"{len(animations)} files, "
          f"{sum(a['frames'] for a in animations):,} frames, "
          f"{sum(a['tilemaps'] for a in animations):,} tilemaps, "
          f"{sum(a['tiles'] for a in animations):,} tiles, "
          f"{sum(a['sounds'] for a in animations):,} sounds")
    print(f"manifest -> {manifest}")


def cmd_dump(out_path, palette_path, *source_paths):
    out_path = Path(out_path)
    palette = load_palette(palette_path)
    for path in gather(source_paths):
        data = path.read_bytes()
        header, sections, decoded = decode_animation(data, path.name)
        png = out_path / (path.stem + ".png")
        strip_width, strip_height = write_filmstrip(
            png, decoded["frames"], decoded, header, palette)
        for sound in decoded["sounds"]:
            write_wav(out_path / f"{path.stem}-{sound['index']:02d}.wav", sound)
        print(f"{path.name}: {len(decoded['frames'])} frames -> {png.name} "
              f"({strip_width}x{strip_height}), "
              f"{len(decoded['sounds'])} sounds")


def cmd_report(manifest_path):
    """Print the per-file summary as Markdown."""
    path = Path(manifest_path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SafError(f"{path}: not JSON: {error}") from error
    if "animations" not in manifest:
        raise SafError(f"{path}: not a manifest, it has no 'animations'")
    animations = manifest["animations"]

    print("| 檔案 | frame | tilemap | tile | 音效 |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for animation in animations:
        print(f"| `{animation['path']}` | {animation['frames']} | "
              f"{animation['tilemaps']} | {animation['tiles']} | "
              f"{animation['sounds']} |")
    print(f"\n{len(animations)} 個檔、"
          f"{sum(a['frames'] for a in animations):,} 個 frame、"
          f"{sum(a['layers'] for a in animations):,} 個 layer、"
          f"{sum(a['tilemaps'] for a in animations):,} 個 tilemap、"
          f"{sum(a['cells'] for a in animations):,} 個 cell、"
          f"{sum(a['tiles'] for a in animations):,} 個 tile、"
          f"{sum(a['pixels'] for a in animations):,} 個像素、"
          f"{sum(a['sounds'] for a in animations):,} 段音效"
          f"（{sum(a['sample_bytes'] for a in animations):,} byte 取樣）"
          f"全部解碼成功。")


def main(argv):
    # `report` prints Chinese, and on a zh-TW Windows the default stdout codec
    # is cp950, which mangles it the moment the output is redirected to a file.
    sys.stdout.reconfigure(encoding="utf-8")
    # `verify` and `dump` take any number of sources after their fixed
    # arguments; the others take exactly what they say.
    commands = {"list": (cmd_list, 1, 1), "verify": (cmd_verify, 2, None),
                "dump": (cmd_dump, 3, None), "report": (cmd_report, 1, 1)}
    if len(argv) < 2 or argv[1] not in commands:
        print(__doc__, file=sys.stderr)
        return 2
    handler, least, most = commands[argv[1]]
    if len(argv) - 2 < least or (most is not None and len(argv) - 2 > most):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        handler(*argv[2:])
    except (SafError, OSError) as error:
        # A stream that does not decode is the same class of failure as a field
        # that does not fit: say what went wrong on one line rather than
        # dropping a traceback on the user.
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
