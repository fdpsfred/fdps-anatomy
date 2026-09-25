"""Unit tests for battle_assets_media.py
(python -m unittest tools/cut_content/test_battle_assets_media.py).

The naming tests are pure.  The generation test runs the real generator over
the original game files and is skipped when fdps_game_files/ is absent.
"""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "saf_decode"))

import battle_assets_media as bam  # noqa: E402
import cut_content  # noqa: E402
import saf_decode  # noqa: E402

GAME = cut_content.DEFAULT_GAME


class MediaNameTest(unittest.TestCase):
    def test_names_are_lower_case_and_drop_the_extension(self):
        self.assertEqual(bam.media_name("B2", "MAGIC003.SAF", "f00", "png"), "b2-magic003-f00.png")
        self.assertEqual(bam.media_name("B4", "EMG19.SAF", "s1", "wav"), "b4-emg19-s1.wav")
        self.assertEqual(bam.media_name("B1", "BACK16.SAF", None, "png"), "b1-back16.png")

    def test_a_stored_wav_keeps_only_its_stem(self):
        self.assertEqual(bam.media_name("B7", "BONUS.WAV", None, "wav"), "b7-bonus.wav")

    def test_a_member_name_with_a_dash_keeps_it(self):
        self.assertEqual(bam.media_name("B7", "BONUS-1.SAF", "f00", "png"), "b7-bonus-1-f00.png")

    def test_every_name_the_spec_can_produce_passes_the_gate_rule(self):
        for item in bam.SPEC:
            for part in (None, "f00", "f48", "sheet", "s0", "s3"):
                for ext in ("png", "wav"):
                    name = bam.media_name(item.entry, item.member, part, ext)
                    self.assertRegex(name, cut_content.MEDIA_NAME, name)

    def test_the_spec_only_names_battle_asset_entries(self):
        for item in bam.SPEC:
            self.assertTrue(item.entry.startswith("B"), item)
            self.assertIn(item.kind, bam.KINDS, item)

    def test_frame_parts_are_zero_padded_to_two_digits(self):
        self.assertEqual(bam.frame_part(0), "f00")
        self.assertEqual(bam.frame_part(48), "f48")


@unittest.skipUnless(GAME.is_dir(), "needs the original game files in fdps_game_files/")
class GenerateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory()
        cls.out = Path(cls.scratch.name)
        bam.generate(cls.out, GAME)
        cls.names = {p.name for p in cls.out.iterdir()}

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def decoded(self, container, member):
        data = cut_content.read_vfs_member(GAME, container, member)
        return saf_decode.decode_animation(data, member)[2]

    def test_every_file_follows_the_naming_rule(self):
        for name in self.names:
            self.assertRegex(name, cut_content.MEDIA_NAME, name)

    def test_nothing_but_the_spec_is_written(self):
        self.assertEqual(self.names, set(bam.planned_names(GAME)))

    def test_an_animation_gets_one_png_per_frame_and_a_sheet(self):
        frames = self.decoded("MISC.VFS", "EL05.SAF")["frames"]
        for i in range(len(frames)):
            self.assertIn("b3-el05-f%02d.png" % i, self.names)
        self.assertNotIn("b3-el05-f%02d.png" % len(frames), self.names)
        self.assertIn("b3-el05-sheet.png", self.names)

    def test_a_backdrop_is_one_screen_sized_png(self):
        png = (self.out / "b1-back16.png").read_bytes()
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (320, 200))

    def test_an_unreferenced_sound_is_written_with_its_own_rate(self):
        sound = self.decoded("MISC.VFS", "EMG19.SAF")["sounds"][1]
        wav = (self.out / "b4-emg19-s1.wav").read_bytes()
        self.assertEqual(wav[:4], b"RIFF")
        self.assertEqual(struct.unpack_from("<I", wav, 24)[0], sound["rate"])
        self.assertEqual(wav[44:], sound["samples"])

    def test_a_sound_that_is_heard_is_not_written(self):
        # EMG19's sounds 0 and 2 are referenced by frames 0 and 5 and do play.
        self.assertNotIn("b4-emg19-s0.wav", self.names)
        self.assertNotIn("b4-emg19-s2.wav", self.names)

    def test_a_stored_wav_member_is_copied_unchanged(self):
        member = cut_content.read_vfs_member(GAME, "MISC.VFS", "BONUS.WAV")
        self.assertEqual((self.out / "b7-bonus.wav").read_bytes(), member)


if __name__ == "__main__":
    unittest.main()
