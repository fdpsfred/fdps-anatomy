"""Read, verify and write FDE.SAV, the FDPS save file.

The layout and the two guards are the ones the game's own code uses
(src/btlmenu.c, src/savefile.c, src/save.c, src/savepnl.c, src/title.c); the
knowledge-base canon is resource_info/save.md, which names the function that
reads and writes every field.

    image (0x59cb bytes, the same on disc and in memory, XOR-enciphered on disc)
      +0x0000  0x08a3  field block      copy of the chapter's MAP%02d.DAT buffer
      +0x08a3  0x0a00  roster           32 unit records of 0x50
      +0x12a3  0x1e00  unit array       96 unit records of 0x50 (count used)
      +0x30a3  0x0020  triggered flags  one byte per cell event code
      +0x30c3  0x0012  resume header    the scalars of the battle in progress
      +0x30d5  0x0056  (never written by the game)
      +0x312b  4 x 0xa28  chapter slots
      +0x59c7  u32     checksum         overlaps the tail of slot 3

    checksum  u32 sum of the plaintext bytes [0, 0x59c7), unsigned, wrapping
    cipher    k = 0xa5; per byte: k = rol16((k + 0x9014) & 0xffff, 3);
              byte ^= k & 0xff -- its own inverse

Writing is always checksum first (over the plaintext) and cipher second, which
is what seal() does; reading is cipher first and checksum second.

Usage:
    python fde_sav.py dump    <FDE.SAV>              print every field as JSON
    python fde_sav.py verify  <FDE.SAV>...           checksum and round trip
    python fde_sav.py decrypt <FDE.SAV> <plain.bin>  write the plaintext image
    python fde_sav.py seal    <plain.bin> <FDE.SAV>  checksum, encrypt, write
"""

import json
import struct
import sys
from pathlib import Path

IMAGE_BYTES = 0x59cb
CHECKSUM_AT = 0x59c7

FIELD_BLOCK_AT = 0x0000
FIELD_BLOCK_BYTES = 0x08a3
ROSTER_AT = 0x08a3
ROSTER_BYTES = 0x0a00
UNIT_ARRAY_AT = 0x12a3
UNIT_ARRAY_BYTES = 0x1e00
TRIGGERED_FLAGS_AT = 0x30a3
TRIGGERED_FLAGS_BYTES = 0x20
RESUME_HEADER_AT = 0x30c3
RESUME_HEADER_BYTES = 0x12
UNWRITTEN_GAP_AT = 0x30d5
SLOTS_AT = 0x312b
SLOT_BYTES = 0xa28
SLOT_COUNT = 4
SLOTS_ON_SCREEN = 3

UNIT_RECORD_BYTES = 0x50
# The chapter byte both the resume header and a slot carry when they have never
# been written: the byte value 255, compared after an unsigned widening.
UNWRITTEN_CHAPTER = 0xff

# Offsets inside the 0x12-byte resume header (src/btlmenu.c, src/savefile.c).
# +0x07 and +0x08 are written as literal zeros and read by nobody.
RESUME_FIELDS = (
    ("turn_counter", 0x00, "B"),
    ("unit_count", 0x01, "B"),
    ("chapter_index", 0x02, "B"),
    ("view_origin_tile_x", 0x03, "B"),
    ("view_origin_tile_y", 0x04, "B"),
    ("cursor_tile_x", 0x05, "B"),
    ("cursor_tile_y", 0x06, "B"),
    ("zero_07", 0x07, "B"),
    ("zero_08", 0x08, "B"),
    ("roster_member_count", 0x09, "B"),
    ("party_gold", 0x0a, "<i"),
    ("battle_animation_enabled", 0x0e, "B"),
    ("terrain_hud_user_enabled", 0x0f, "B"),
    ("bgm_enabled_flag", 0x10, "B"),
    ("sfx_enabled_flag", 0x11, "B"),
)

# Offsets inside one 0xa28-byte slot (struct fdps_save_slot, src/fdpstype.h).
# The five dwords from +0x9ec sit INSIDE the 0xa00-byte roster copy: they are
# stored over the last 0x14 bytes of roster record 31 after the copy.
SLOT_FIELDS = (
    ("bonus_lottery_drawn_flag", 0x9ec, "<I"),
    ("save_minute", 0x9f0, "<I"),
    ("save_hour", 0x9f4, "<I"),
    ("save_day", 0x9f8, "<I"),
    ("save_month", 0x9fc, "<I"),
    ("chapter_index", 0xa00, "B"),
    ("roster_member_count", 0xa01, "B"),
    ("party_gold", 0xa02, "<i"),
    ("terrain_hud_user_enabled", 0xa06, "B"),
    ("battle_animation_enabled", 0xa07, "B"),
    ("bgm_enabled_flag", 0xa08, "B"),
    ("sfx_enabled_flag", 0xa09, "B"),
)

# The unit record fields a save summary shows (struct fdps_unit_record).
UNIT_POS_X = 0x00
UNIT_POS_Y = 0x01
UNIT_SPRITE_CACHE_SLOT = 0x02
UNIT_FLAGS = 0x05
UNIT_SIDE = 0x06
UNIT_PORTRAIT_ID = 0x07
UNIT_CHAR_ID = 0x08
UNIT_LEVEL = 0x21
UNIT_HP_CURRENT = 0x40
UNIT_HP_MAX = 0x42


class SaveError(Exception):
    """The input is not a FDE.SAV image."""


def crypt(data):
    """Apply the FDE.SAV XOR stream cipher; the same call encrypts and decrypts.

    The key is sixteen bits, advanced BEFORE each byte (so the seed itself is
    never used) and only its low byte is applied.
    """
    out = bytearray(data)
    key = 0xa5
    for index in range(len(out)):
        key = (key + 0x9014) & 0xffff
        key = ((key << 3) | (key >> 13)) & 0xffff
        out[index] ^= key & 0xff
    return bytes(out)


def checksum(plain):
    """Sum every byte of the plaintext image except the trailing four."""
    return sum(plain[:len(plain) - 4]) & 0xffffffff


def _require_image(data, what):
    if len(data) != IMAGE_BYTES:
        raise SaveError(f"{what} is {len(data)} bytes, a FDE.SAV image is "
                        f"{IMAGE_BYTES}")


def seal(plain):
    """Turn a plaintext image into the bytes the game writes to disc."""
    _require_image(plain, "plaintext image")
    image = bytearray(plain)
    struct.pack_into("<I", image, CHECKSUM_AT, checksum(image))
    return crypt(image)


def _fields(buf, base, layout):
    return {name: struct.unpack_from(fmt, buf, base + offset)[0]
            for name, offset, fmt in layout}


def _unit(buf, at):
    hp_current, hp_max = struct.unpack_from("<hh", buf, at + UNIT_HP_CURRENT)
    return {
        "pos": [buf[at + UNIT_POS_X], buf[at + UNIT_POS_Y]],
        "sprite_cache_slot": buf[at + UNIT_SPRITE_CACHE_SLOT],
        "flags": buf[at + UNIT_FLAGS],
        "side": buf[at + UNIT_SIDE],
        "portrait_id": buf[at + UNIT_PORTRAIT_ID],
        "char_id": buf[at + UNIT_CHAR_ID],
        "level": buf[at + UNIT_LEVEL],
        "hp": [hp_current, hp_max],
    }


def parse(disc):
    """Decode an on-disc FDE.SAV into a dict of every field the game uses.

    A checksum mismatch is reported, not refused: the game itself loads a
    save whose checksum fails (fdps_load_savegame shows a warning and goes on).
    Record lists stop at the counts the header gives, capped at the room the
    block has; bytes past the counts are stale and carry no meaning.
    """
    _require_image(disc, "save file")
    plain = crypt(disc)
    stored = struct.unpack_from("<I", plain, CHECKSUM_AT)[0]
    computed = checksum(plain)

    resume = _parse_resume(plain)
    slots = [_parse_slot(plain, index) for index in range(SLOT_COUNT)]
    return {
        "checksum_stored": stored,
        "checksum_computed": computed,
        "checksum_ok": stored == computed,
        "resume": resume,
        "slots": slots,
    }


def _parse_resume(plain):
    """The live-state region, or only its marker when no battle was saved."""
    chapter = plain[RESUME_HEADER_AT + 0x02]
    if chapter == UNWRITTEN_CHAPTER:
        return {"battle_saved": False, "chapter_index": chapter}
    resume = _fields(plain, RESUME_HEADER_AT, RESUME_FIELDS)
    resume["battle_saved"] = True
    unit_count = min(resume["unit_count"], UNIT_ARRAY_BYTES // UNIT_RECORD_BYTES)
    roster_count = min(resume["roster_member_count"],
                       ROSTER_BYTES // UNIT_RECORD_BYTES)
    resume["field_block_head"] = list(plain[FIELD_BLOCK_AT:FIELD_BLOCK_AT + 3])
    resume["roster"] = [_unit(plain, ROSTER_AT + i * UNIT_RECORD_BYTES)
                        for i in range(roster_count)]
    resume["units"] = [_unit(plain, UNIT_ARRAY_AT + i * UNIT_RECORD_BYTES)
                       for i in range(unit_count)]
    resume["triggered_flags"] = list(
        plain[TRIGGERED_FLAGS_AT:TRIGGERED_FLAGS_AT + TRIGGERED_FLAGS_BYTES])
    return resume


def _parse_slot(plain, index):
    """One chapter slot, or only its marker when it has never been written.

    An unwritten slot's other bytes are whatever the file was created with
    (a 0xff fill or a fresh malloc block), so nothing past the marker is read.
    """
    base = SLOTS_AT + index * SLOT_BYTES
    slot = {"index": index, "on_screen": index < SLOTS_ON_SCREEN}
    chapter = plain[base + 0xa00]
    slot["written"] = chapter != UNWRITTEN_CHAPTER
    if not slot["written"]:
        slot["chapter_index"] = chapter
        return slot
    slot.update(_fields(plain, base, SLOT_FIELDS))
    slot["leader_portrait_id"] = plain[base + UNIT_PORTRAIT_ID]
    slot["leader_level"] = plain[base + UNIT_LEVEL]
    return slot


def cmd_dump(path):
    print(json.dumps(parse(Path(path).read_bytes()), indent=1,
                     ensure_ascii=False))
    return 0


def cmd_verify(*paths):
    failures = 0
    for path in paths:
        disc = Path(path).read_bytes()
        save = parse(disc)
        round_trip = seal(crypt(disc)) == disc
        ok = save["checksum_ok"] and round_trip
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {path}: stored "
              f"{save['checksum_stored']:#010x} computed "
              f"{save['checksum_computed']:#010x} round trip "
              f"{'same' if round_trip else 'DIFFERENT'}")
    return 1 if failures else 0


def cmd_decrypt(src, dst):
    disc = Path(src).read_bytes()
    _require_image(disc, "save file")
    Path(dst).write_bytes(crypt(disc))
    return 0


def cmd_seal(src, dst):
    Path(dst).write_bytes(seal(Path(src).read_bytes()))
    return 0


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    commands = {"dump": (cmd_dump, 1), "verify": (cmd_verify, None),
                "decrypt": (cmd_decrypt, 2), "seal": (cmd_seal, 2)}
    if len(argv) < 3 or argv[1] not in commands:
        print(__doc__, file=sys.stderr)
        return 2
    handler, arity = commands[argv[1]]
    if arity is not None and len(argv) != 2 + arity:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        return handler(*argv[2:])
    except (SaveError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
