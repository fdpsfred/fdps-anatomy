"""Media generator for cut_content/battle_assets.md (ticket 25.13).

Registered in cut_content.GENERATORS["battle_assets"], so it runs through

    python tools/cut_content/cut_content.py media battle_assets
    python tools/cut_content/cut_content.py verify-media battle_assets

It writes, from the original game files only:

  still   a one-frame animation (a battle backdrop) as one 320x200 PNG
  clip    an animation no game path loads: one PNG per frame, a sheet of all
          frames, and every sound it carries as a WAV -- none of them is ever
          heard, because the file itself is never loaded
  sounds  selected sounds of an animation that does play, but whose sounds
          are never fired (no frame points at them, or the player is told not
          to fire them)
  wav     a WAV member of a container, copied byte for byte

Frames are drawn by saf_decode.compose_frame in the palette the game shows them
in (MISC.VFS FIGHT.PAL); pixels no layer covers are transparent.  Decoding and
writing go through the format owners (saf_decode, cel_decode, vfs_dump) via the
cut_content helpers; nothing here decodes a format itself.
"""

import sys
import tempfile
from collections import namedtuple
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "saf_decode"))

import saf_decode  # noqa: E402  (owner of the .SAF format)

KINDS = ("still", "clip", "sounds", "wav")
PALETTE_CONTAINER = "MISC.VFS"
PALETTE_MEMBER = "FIGHT.PAL"

Item = namedtuple("Item", "entry kind container member sounds")


def _items(entry, kind, container, members, sounds=()):
    return [Item(entry, kind, container, m, tuple(sounds)) for m in members]


SPEC = (
    # B1: the six backdrops no terrain row and no credit card selects.
    _items("B1", "still", "BACKGRND.VFS",
           ["BACK%02d.SAF" % n for n in (16, 31, 41, 42, 51, 57)])
    # B2: the casting animations of portraits that never cast a spell on the
    # battle screen.
    + _items("B2", "clip", "FIGHT.VFS",
             ["MAGIC%03d.SAF" % n for n in (3, 4, 5, 6, 8, 19, 21, 23, 29, 64, 65)])
    # B3: the enemy-side 落雷術 build-up and main clips (its finish clip EE05
    # is one of the empty shells of B5 and has nothing to show).
    + _items("B3", "clip", "MISC.VFS", ["EB05.SAF", "EL05.SAF"])
    # B4: embedded sounds that are never fired.
    + _items("B4", "sounds", "MISC.VFS", ["EMG19.SAF"], sounds=(1,))
    + _items("B4", "sounds", "MISC.VFS", ["EMG33.SAF"], sounds=(1,))
    + _items("B4", "sounds", "MISC.VFS", ["MAG11.SAF"], sounds=(0, 1))
    # B7: the top-prize fanfare of the bar lottery.
    + _items("B7", "wav", "MISC.VFS", ["BONUS.WAV"])
)


def media_name(entry, member, part, ext):
    """<entry>-<member without extension>[-<part>].<ext>, all lower case."""
    stem = member.rsplit(".", 1)[0]
    name = "%s-%s" % (entry, stem) + ("-%s" % part if part else "")
    return (name + "." + ext).lower()


def frame_part(index):
    return "f%02d" % index


def sound_part(index):
    return "s%d" % index


def _helpers():
    # Imported here rather than at module level: cut_content imports this
    # module to register it, and may itself be running as __main__.
    import cut_content
    return cut_content


def _palette(game_dir):
    cc = _helpers()
    raw = cc.read_vfs_member(game_dir, PALETTE_CONTAINER, PALETTE_MEMBER)
    # saf_decode.load_palette reads a path; hand it the member through a file.
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / PALETTE_MEMBER
        path.write_bytes(raw)
        return saf_decode.load_palette(path)


def _decode(game_dir, item):
    cc = _helpers()
    data = cc.read_vfs_member(game_dir, item.container, item.member)
    header, _, decoded = saf_decode.decode_animation(data, item.member)
    return header, decoded


def _frame_rows(frame, decoded, header, palette):
    pixels, mask = saf_decode.compose_frame(frame, decoded, header)
    width = saf_decode.SCREEN_WIDTH
    transparent = bytes(saf_decode.STRIP_BACKGROUND)
    rows = []
    for y in range(saf_decode.SCREEN_HEIGHT):
        row = bytearray()
        for x in range(width):
            at = y * width + x
            row += bytes(palette[pixels[at]]) if mask[at] else transparent
        rows.append(bytes(row))
    return rows


def planned_names(game_dir):
    """Every file generate() writes, derived from the spec and the game files."""
    names = []
    for item in SPEC:
        if item.kind == "still":
            names.append(media_name(item.entry, item.member, None, "png"))
        elif item.kind == "clip":
            _, decoded = _decode(game_dir, item)
            names += [media_name(item.entry, item.member, frame_part(i), "png")
                      for i in range(len(decoded["frames"]))]
            names.append(media_name(item.entry, item.member, "sheet", "png"))
            names += [media_name(item.entry, item.member, sound_part(i), "wav")
                      for i in range(len(decoded["sounds"]))]
        elif item.kind == "sounds":
            names += [media_name(item.entry, item.member, sound_part(i), "wav")
                      for i in item.sounds]
        else:
            names.append(media_name(item.entry, item.member, None, "wav"))
    return names


def generate(out_dir, game_dir):
    cc = _helpers()
    out_dir, game_dir = Path(out_dir), Path(game_dir)
    palette = _palette(game_dir)
    width, height = saf_decode.SCREEN_WIDTH, saf_decode.SCREEN_HEIGHT

    for item in SPEC:
        if item.kind == "wav":
            data = cc.read_vfs_member(game_dir, item.container, item.member)
            (out_dir / media_name(item.entry, item.member, None, "wav")).write_bytes(data)
            continue

        header, decoded = _decode(game_dir, item)
        if item.kind == "still":
            if len(decoded["frames"]) != 1:
                raise ValueError("%s: a still needs exactly one frame, it has %d"
                                 % (item.member, len(decoded["frames"])))
            rows = _frame_rows(decoded["frames"][0], decoded, header, palette)
            cc.write_png(out_dir / media_name(item.entry, item.member, None, "png"),
                         width, height, rows)
        elif item.kind == "clip":
            for frame in decoded["frames"]:
                rows = _frame_rows(frame, decoded, header, palette)
                cc.write_png(out_dir / media_name(item.entry, item.member,
                                                  frame_part(frame["index"]), "png"),
                             width, height, rows)
            saf_decode.write_filmstrip(
                out_dir / media_name(item.entry, item.member, "sheet", "png"),
                decoded["frames"], decoded, header, palette)
            for sound in decoded["sounds"]:
                cc.write_wav(out_dir / media_name(item.entry, item.member,
                                                  sound_part(sound["index"]), "wav"),
                             sound)
        else:
            for index in item.sounds:
                cc.write_wav(out_dir / media_name(item.entry, item.member,
                                                  sound_part(index), "wav"),
                             decoded["sounds"][index])
