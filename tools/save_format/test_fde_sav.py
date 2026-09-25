"""Tests for fde_sav at its public seams: crypt(), checksum(), parse() and
seal().

Expected values come from sources independent of the code under test: the
first keystream bytes worked by hand from the two instructions at 000568c9 /
000568ce (ADD DX,0x9014 then ROL DX,3, seed 0xa5), the offsets the game's own
readers and writers use (src/btlmenu.c, src/savefile.c, src/save.c,
src/savepnl.c, src/title.c), and the save file shipped in fdps_game_files/.

    python -m unittest tools/save_format/test_fde_sav.py
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fde_sav  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
GAME_SAVE = ROOT / "fdps_game_files" / "FDE.SAV"


def blank_image(fill=0):
    return bytearray([fill]) * fde_sav.IMAGE_BYTES


class CryptTest(unittest.TestCase):
    def test_keystream_starts_cc_then_advances_before_every_byte(self):
        # 0xa5 + 0x9014 = 0x90b9, rol16 by 3 = 0x85cc -> 0xcc;
        # 0x85cc + 0x9014 = 0x15e0, rol16 by 3 = 0xaf00 -> 0x00;
        # 0xaf00 + 0x9014 = 0x3f14, rol16 by 3 = 0xf8a1 -> 0xa1.
        self.assertEqual(fde_sav.crypt(bytes(3)), bytes([0xcc, 0x00, 0xa1]))

    def test_crypt_is_its_own_inverse(self):
        data = bytes(range(256)) * 4
        self.assertEqual(fde_sav.crypt(fde_sav.crypt(data)), data)


class ChecksumTest(unittest.TestCase):
    def test_sum_leaves_out_the_last_four_bytes(self):
        image = blank_image()
        image[0] = 0x10
        image[fde_sav.IMAGE_BYTES - 5] = 0x20
        image[fde_sav.CHECKSUM_AT:] = b"\xff\xff\xff\xff"
        self.assertEqual(fde_sav.checksum(image), 0x30)

    def test_bytes_are_summed_unsigned(self):
        image = blank_image()
        image[0] = 0x80
        image[1] = 0xff
        self.assertEqual(fde_sav.checksum(image), 0x17f)


class SealTest(unittest.TestCase):
    def test_seal_stores_the_checksum_and_then_encrypts(self):
        plain = blank_image()
        plain[0x100] = 7
        disc = fde_sav.seal(plain)
        back = fde_sav.crypt(disc)
        self.assertEqual(struct.unpack_from("<I", back, 0x59c7)[0], 7)
        self.assertEqual(back[0x100], 7)

    def test_seal_refuses_an_image_of_the_wrong_size(self):
        with self.assertRaises(fde_sav.SaveError):
            fde_sav.seal(bytes(100))


class ParseTest(unittest.TestCase):
    def test_resume_header_fields_come_from_their_offsets(self):
        plain = blank_image()
        header = bytes([10, 23, 4, 3, 5, 6, 7, 0, 0, 2]) \
            + struct.pack("<i", -5) + bytes([1, 0, 1, 0])
        plain[0x30c3:0x30d5] = header
        save = fde_sav.parse(fde_sav.seal(plain))
        r = save["resume"]
        self.assertEqual(r["turn_counter"], 10)
        self.assertEqual(r["unit_count"], 23)
        self.assertEqual(r["chapter_index"], 4)
        self.assertEqual((r["view_origin_tile_x"], r["view_origin_tile_y"]),
                         (3, 5))
        self.assertEqual((r["cursor_tile_x"], r["cursor_tile_y"]), (6, 7))
        self.assertEqual(r["roster_member_count"], 2)
        self.assertEqual(r["party_gold"], -5)
        self.assertEqual((r["battle_animation_enabled"],
                          r["terrain_hud_user_enabled"],
                          r["bgm_enabled_flag"], r["sfx_enabled_flag"]),
                         (1, 0, 1, 0))
        self.assertTrue(r["battle_saved"])

    def test_chapter_ff_in_the_resume_header_means_no_battle(self):
        save = fde_sav.parse(fde_sav.seal(blank_image(0xff)))
        self.assertFalse(save["resume"]["battle_saved"])

    def test_slot_fields_come_from_the_slot_tail(self):
        plain = blank_image()
        base = 0x312b + 2 * 0xa28
        struct.pack_into("<5I", plain, base + 0x9ec, 1, 33, 15, 7, 1)
        plain[base + 0xa00] = 25
        plain[base + 0xa01] = 12
        struct.pack_into("<i", plain, base + 0xa02, 42504)
        plain[base + 0xa06:base + 0xa0a] = bytes([1, 0, 1, 1])
        plain[base + 0x07] = 0x05          # roster[0].portrait_id
        plain[base + 0x21] = 30            # roster[0].level
        slot = fde_sav.parse(fde_sav.seal(plain))["slots"][2]
        self.assertTrue(slot["written"])
        self.assertEqual(slot["chapter_index"], 25)
        self.assertEqual(slot["roster_member_count"], 12)
        self.assertEqual(slot["party_gold"], 42504)
        self.assertEqual((slot["save_month"], slot["save_day"],
                          slot["save_hour"], slot["save_minute"]),
                         (1, 7, 15, 33))
        self.assertEqual(slot["bonus_lottery_drawn_flag"], 1)
        self.assertEqual((slot["terrain_hud_user_enabled"],
                          slot["battle_animation_enabled"],
                          slot["bgm_enabled_flag"], slot["sfx_enabled_flag"]),
                         (1, 0, 1, 1))
        self.assertEqual((slot["leader_portrait_id"], slot["leader_level"]),
                         (5, 30))

    def test_slot_with_chapter_ff_is_unwritten(self):
        slots = fde_sav.parse(fde_sav.seal(blank_image(0xff)))["slots"]
        self.assertEqual([s["written"] for s in slots], [False] * 4)

    def test_unwritten_regions_are_not_interpreted(self):
        save = fde_sav.parse(fde_sav.seal(blank_image(0xff)))
        self.assertNotIn("save_minute", save["slots"][0])
        self.assertNotIn("units", save["resume"])

    def test_parse_reports_a_checksum_mismatch_without_refusing(self):
        disc = bytearray(fde_sav.seal(blank_image()))
        disc[0] ^= 1
        save = fde_sav.parse(bytes(disc))
        self.assertFalse(save["checksum_ok"])


@unittest.skipUnless(GAME_SAVE.is_file(), "fdps_game_files/FDE.SAV absent")
class ShippedSaveTest(unittest.TestCase):
    def setUp(self):
        self.disc = GAME_SAVE.read_bytes()

    def test_stored_checksum_matches_the_recomputed_one(self):
        self.assertTrue(fde_sav.parse(self.disc)["checksum_ok"])

    def test_resealing_the_decrypted_image_gives_the_same_file(self):
        self.assertEqual(fde_sav.seal(fde_sav.crypt(self.disc)), self.disc)


if __name__ == "__main__":
    unittest.main()
