"""Media generator for cut_content/units.md (ticket 25.11).

Registered in cut_content.GENERATORS under "units"; run it through

    python tools/cut_content/cut_content.py media units
    python tools/cut_content/cut_content.py verify-media units

What it writes, all straight from the original game files:

    u01-face-087.png         FACE.CEL portrait 87, 125x100, palette FDE.PAL
    u01-icon-087.png         ICON.CEL group 87: its 12 map icons (sprites
                             87*12 .. 87*12+11) left to right, palette FDE.PAL
    u01-stand087-f00.png     one frame of FIGHT.VFS STAND087.SAF, palette FIGHT.PAL
    u01-stand087-sheet.png   every frame of it on one sheet
    u01-act087-s0.wav        sound 0 embedded in FIGACT.VFS ACT087.SAF

A portrait or icon index in a name is the decimal index into the sheet, which
is the unit's portrait id.  An animation frame is drawn the way the game
composes it (saf_decode.compose_frame) on the 320x200 battle screen and then
cropped to the smallest box that holds every frame of that animation, so the
frames of one animation line up with each other and keep their screen offsets
relative to each other.  Pixels nothing wrote are transparent.
"""

import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
for owner in ("cel_decode", "saf_decode"):
    sys.path.insert(0, str(TOOLS / owner))

import cel_decode  # noqa: E402  (owner of the .CEL format)
import saf_decode  # noqa: E402  (owner of the .SAF format)

FIELD_PALETTE = "FDE.PAL"
BATTLE_PALETTE = "FIGHT.PAL"
ICONS_PER_GROUP = 12
TRANSPARENT = bytes((0, 0, 0, 0))
SHEET_MAX_WIDTH = 2048

# U01's four never-deployed enemies: 傭兵戰士, 禁衛隊, 寶箱怪, 寶箱妖精.
U01_ENEMIES = (87, 92, 110, 111)

# entry -> portrait ids.  U03's portraits 57 and 58 are blank, so U03 has none.
FACES = {
    "u01": U01_ENEMIES,
    "u02": (52,),
    "u04": (131, 140),
}
# entry -> icon groups
ICONS = {
    "u01": U01_ENEMIES,
    "u02": (52,),
    "u03": (57, 58),
}
# entry -> (container, member prefix, portrait id)
ANIMATIONS = {
    "u01": tuple((container, prefix, portrait)
                 for portrait in U01_ENEMIES
                 for container, prefix in (("FIGHT.VFS", "STAND"), ("FIGACT.VFS", "ACT"))),
}


def union_box(masks, width, height):
    """(left, top, right, bottom) of every pixel set in any mask, or None."""
    left, top, right, bottom = width, height, -1, -1
    for mask in masks:
        for y in range(height):
            row = mask[y * width:(y + 1) * width]
            if not any(row):
                continue
            xs = [x for x, bit in enumerate(row) if bit]
            left, right = min(left, xs[0]), max(right, xs[-1])
            top, bottom = min(top, y), max(bottom, y)
    if right < 0:
        return None
    return left, top, right + 1, bottom + 1


def grid(count, cell_width, cell_height, gap, max_width=SHEET_MAX_WIDTH):
    """Lay count cells left to right, wrapping at max_width.

    Returns (width, height, [(x, y) of each cell])."""
    columns = max(1, min(count, (max_width + gap) // (cell_width + gap)))
    rows = (count + columns - 1) // columns
    places = [((i % columns) * (cell_width + gap), (i // columns) * (cell_height + gap))
              for i in range(count)]
    return (columns * cell_width + (columns - 1) * gap,
            rows * cell_height + (rows - 1) * gap, places)


def rgba_rows(pixels, mask, width, height, palette, box=None):
    """Palette-index pixels to RGBA rows, optionally cropped to box."""
    left, top, right, bottom = box or (0, 0, width, height)
    rows = []
    for y in range(top, bottom):
        row = bytearray()
        for x in range(left, right):
            at = y * width + x
            row += bytes(palette[pixels[at]]) if mask[at] else TRANSPARENT
        rows.append(row)
    return rows


def paste(canvas, rows, x, y):
    for dy, row in enumerate(rows):
        canvas[y + dy][x * 4:x * 4 + len(row)] = row


def write_sheet(write_png, path, images, cell_width, cell_height, gap):
    width, height, places = grid(len(images), cell_width, cell_height, gap)
    canvas = [bytearray(TRANSPARENT * width) for _ in range(height)]
    for rows, (x, y) in zip(images, places):
        paste(canvas, rows, x, y)
    write_png(path, width, height, canvas)


def load_palette(game_dir, name, read_vfs_member):
    """A MISC.VFS palette through its owner's loader (which reads a path)."""
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / name
        path.write_bytes(read_vfs_member(game_dir, "MISC.VFS", name))
        return cel_decode.load_palette(path)


def generate(out_dir, game_dir):
    import cut_content  # the helpers' owner; imported late to avoid a cycle
    out_dir, game_dir = Path(out_dir), Path(game_dir)
    field = load_palette(game_dir, FIELD_PALETTE, cut_content.read_vfs_member)
    battle = load_palette(game_dir, BATTLE_PALETTE, cut_content.read_vfs_member)

    header, _, faces = cel_decode.decode_sheet(
        cut_content.read_game_file(game_dir, "FACE.CEL"), "FACE.CEL")
    w, h = header["width"], header["height"]
    for entry, portraits in FACES.items():
        for portrait in portraits:
            pixels, mask = faces[portrait]
            cut_content.write_png(out_dir / f"{entry}-face-{portrait:03d}.png", w, h,
                                  rgba_rows(pixels, mask, w, h, field))

    header, _, icons = cel_decode.decode_sheet(
        cut_content.read_game_file(game_dir, "ICON.CEL"), "ICON.CEL")
    w, h = header["width"], header["height"]
    for entry, groups in ICONS.items():
        for group in groups:
            cells = [rgba_rows(*icons[group * ICONS_PER_GROUP + i], w, h, field)
                     for i in range(ICONS_PER_GROUP)]
            write_sheet(cut_content.write_png, out_dir / f"{entry}-icon-{group:03d}.png",
                        cells, w, h, cel_decode.SHEET_GAP)

    for entry, animations in ANIMATIONS.items():
        for container, prefix, portrait in animations:
            member = f"{prefix}{portrait:03d}.SAF"
            data = cut_content.read_vfs_member(game_dir, container, member)
            header, _, decoded = saf_decode.decode_animation(data, member)
            screens = [saf_decode.compose_frame(frame, decoded, header)
                       for frame in decoded["frames"]]
            sw, sh = saf_decode.SCREEN_WIDTH, saf_decode.SCREEN_HEIGHT
            box = union_box([mask for _, mask in screens], sw, sh)
            if box is None:
                raise ValueError(f"{member}: no frame draws anything")
            bw, bh = box[2] - box[0], box[3] - box[1]
            stem = f"{entry}-{prefix.lower()}{portrait:03d}"
            frames = [rgba_rows(pixels, mask, sw, sh, battle, box) for pixels, mask in screens]
            for index, rows in enumerate(frames):
                cut_content.write_png(out_dir / f"{stem}-f{index:02d}.png", bw, bh, rows)
            write_sheet(cut_content.write_png, out_dir / f"{stem}-sheet.png",
                        frames, bw, bh, saf_decode.STRIP_GAP)
            for sound in decoded["sounds"]:
                cut_content.write_wav(out_dir / f"{stem}-s{sound['index']}.wav", sound)
