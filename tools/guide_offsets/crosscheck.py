"""Compare the decoded data tables against the numbers printed on the strategy guide.

Every expected value below was read off the mirrored guide pages by hand and written
here as a literal; nothing parses the guide text (see ADR-0006).  Each block names the
page it came from.  Rows that are known to differ, and why, are listed in ACCEPTED.

Usage:
    python tools/guide_offsets/crosscheck.py <tables.json> <output markdown>
"""
import json
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# item.htm: 裝備一覽表.  code -> (AP, HIT, DP, EV); blank columns on the page are 0.
# ---------------------------------------------------------------------------
GUIDE_ITEM_STATS = {
    0x00: (45, 95, 0, 0), 0x01: (20, 95, 0, 0), 0x02: (30, 100, 0, 0),
    0x03: (50, 95, 0, 0), 0x04: (70, 100, 0, 0), 0x05: (85, 110, 0, 0),
    0x06: (110, 115, 10, 0), 0x07: (135, 120, 0, 0), 0x08: (160, 125, 0, 0),
    0x09: (180, 130, 0, 0), 0x0A: (230, 130, 0, 0), 0x0B: (280, 130, 0, 0),
    0x0C: (300, 95, 150, 0), 0x0D: (280, 95, 100, 30), 0x0E: (250, 95, 50, 10),
    0x0F: (80, 100, 0, 0), 0x10: (105, 100, 0, 0), 0x11: (125, 110, 0, 0),
    0x12: (100, 100, 0, 0), 0x13: (145, 120, 0, 0), 0x14: (170, 120, 0, 0),
    0x15: (195, 120, 0, 0), 0x16: (245, 125, 0, 0), 0x17: (300, 125, 0, 0),
    0x18: (320, 90, 80, 10), 0x19: (280, 95, 0, 20), 0x1A: (220, 100, 0, 0),
    0x1B: (280, 110, 0, 0), 0x1C: (250, 120, 0, 0), 0x1D: (50, 90, 0, 0),
    0x1E: (55, 100, 0, 0), 0x1F: (50, 105, 0, 0), 0x20: (75, 110, 0, 0),
    0x21: (90, 115, 0, 0), 0x22: (165, 115, 0, 0), 0x23: (170, 115, 0, 0),
    0x24: (250, 135, 10, 0), 0x25: (220, 95, 0, 0), 0x26: (220, 95, 100, 0),
    0x27: (110, 115, 0, 0), 0x28: (450, 135, 20, 0), 0x29: (140, 120, 0, 0),
    0x2A: (120, 120, 0, 0), 0x2B: (190, 120, 0, 0), 0x2C: (240, 125, 0, 0),
    0x2D: (290, 125, 0, 0), 0x2E: (10, 95, 0, 0), 0x2F: (20, 90, 0, 5),
    0x30: (30, 95, 5, 5), 0x31: (50, 95, 0, 0), 0x32: (70, 95, 0, 0),
    0x33: (90, 90, 5, 0), 0x34: (110, 90, 0, 0), 0x35: (130, 90, 0, 0),
    0x36: (150, 100, 0, 0), 0x37: (200, 120, 0, 0), 0x38: (20, 95, 0, 0),
    0x39: (40, 95, 0, 0), 0x3A: (70, 100, 0, 0), 0x3B: (90, 95, 0, 0),
    0x3C: (90, 90, 0, 0), 0x3D: (110, 95, 0, 0), 0x3E: (170, 100, 0, 0),
    0x3F: (130, 150, 0, 0), 0x40: (180, 200, 0, 0), 0x41: (260, 250, 0, 0),
    0x42: (500, 300, 100, 0), 0x43: (320, 95, 100, 5), 0x44: (350, 95, 0, 0),
    0x45: (60, 100, 0, 0), 0x46: (75, 100, 0, 0), 0x47: (100, 110, 0, 0),
    0x48: (130, 100, 0, 0), 0x49: (160, 100, 0, 0), 0x4A: (700, 100, 30, 0),
    0x4B: (160, 95, 0, 0), 0x4C: (215, 110, 0, 0), 0x4D: (270, 110, 0, 0),
    0x4E: (200, 95, 0, 15), 0x4F: (220, 90, 0, 0), 0x50: (280, 95, 0, 0),
    0x51: (80, 90, 0, 0), 0x52: (100, 95, 0, 0), 0x53: (120, 95, 0, 0),
    0x54: (130, 95, 0, 0), 0x55: (155, 95, 0, 0), 0x56: (180, 100, 0, 0),
    0x57: (230, 110, 0, 0), 0x58: (130, 110, 0, 0), 0x59: (120, 100, 20, 5),
    0x5A: (150, 110, 0, 0), 0x5B: (180, 150, 0, 0), 0x5C: (500, 300, 0, 0),
    0x5D: (30, 95, 0, 5), 0x5E: (100, 95, 0, 0), 0x5F: (160, 200, 0, 0),
    0x60: (200, 150, 0, 0), 0x61: (180, 90, 0, 20), 0x62: (400, 120, 0, 0),
    0x63: (10000, 0, 0, 0),
    0x64: (0, 0, 30, 0), 0x65: (0, 0, 50, 0), 0x66: (0, 0, 65, 5),
    0x67: (0, 0, 75, 0), 0x68: (0, 0, 85, 0), 0x69: (0, 0, 95, 0),
    0x6A: (0, 0, 105, 0), 0x6B: (0, 0, 130, 0), 0x6C: (0, 0, 150, 0),
    0x6D: (0, 0, 150, 0), 0x6E: (0, 0, 170, 0), 0x6F: (0, 0, 240, 0),
    0x70: (0, 0, 180, 40), 0x71: (0, 0, 10, 0), 0x72: (0, 0, 15, 0),
    0x73: (0, 0, 20, 5), 0x74: (0, 0, 30, 0), 0x75: (0, 0, 45, 0),
    0x76: (0, 0, 55, 0), 0x77: (0, 0, 65, 0), 0x78: (0, 0, 75, 0),
    0x79: (0, 0, 85, 0), 0x7A: (0, 0, 95, 0), 0x7B: (0, 0, 110, 0),
    0x7C: (0, 0, 125, 0), 0x7D: (0, 0, 140, 0), 0x7E: (0, 0, 220, 0),
    0x7F: (0, 0, 250, 0), 0x80: (0, 0, 200, 40), 0x81: (0, 0, 10, 2),
    0x82: (0, 0, 20, 2), 0x83: (0, 0, 40, 3), 0x84: (0, 0, 50, 5),
    0x85: (0, 0, 60, 15), 0x86: (0, 0, 70, 5), 0x87: (0, 0, 80, 0),
    0x88: (0, 0, 150, 20), 0x89: (0, 0, 130, 0), 0x8A: (0, 0, 110, 0),
    0x8B: (0, 0, 90, 0), 0x8C: (0, 0, 100, 0), 0x8D: (0, 0, 0, 100),
    0x8E: (0, 0, 40, 0), 0x8F: (0, 0, 55, 0), 0x90: (0, 0, 65, 0),
    0x91: (0, 0, 75, 0), 0x92: (0, 0, 85, 0), 0x93: (0, 0, 95, 0),
    0x94: (0, 0, 115, 0), 0x95: (0, 0, 140, 0), 0x96: (0, 0, 280, 0),
    0x97: (0, 0, 300, 0), 0x98: (0, 0, 280, 20), 0x99: (0, 0, 50, 0),
    0x9A: (0, 0, 80, 0), 0x9B: (0, 0, 120, 0), 0x9C: (0, 0, 250, 20),
    0x9D: (0, 0, 280, 20), 0x9E: (0, 0, 300, 30), 0x9F: (0, 0, 30, 0),
    0xA0: (200, 130, 10, 5), 0xA1: (340, 140, 20, 5), 0xA2: (800, 300, 200, 100),
    0xA3: (0, 0, 0, 0), 0xA4: (0, 0, 0, 0), 0xA5: (170, 130, 0, 0),
    0xA6: (270, 140, 0, 0), 0xA7: (400, 180, 0, 0), 0xA8: (170, 120, 10, 0),
    0xA9: (0, 0, 0, 0), 0xAA: (170, 120, 0, 0), 0xAB: (150, 120, 0, 0),
    0xAC: (250, 120, 0, 0), 0xAD: (0, 0, 0, 0), 0xAE: (0, 0, 450, 0),
    0xAF: (200, 120, 0, 0), 0xB0: (0, 0, 120, 0), 0xB1: (400, 150, 0, 0),
    0xB2: (300, 120, 0, 0), 0xB3: (0, 0, 0, 0), 0xB4: (0, 0, 0, 0),
    0xB5: (0, 0, 0, 0), 0xB6: (0, 0, 0, 0), 0xB7: (0, 0, 0, 0),
    0xB8: (0, 0, 0, 0), 0xB9: (0, 0, 0, 0), 0xBA: (0, 0, 0, 0),
    0xBB: (0, 0, 0, 0), 0xBC: (180, 120, 0, 0), 0xBD: (500, 180, 0, 0),
    0xBE: (750, 150, 100, 0), 0xBF: (200, 150, 30, 0), 0xC0: (300, 150, 40, 0),
    0xC1: (400, 150, 50, 0), 0xC2: (0, 0, 0, 0), 0xC3: (0, 0, 0, 0),
    0xC4: (0, 0, 0, 0), 0xC5: (0, 0, 0, 0), 0xC6: (0, 0, 0, 0),
    0xC7: (300, 150, 0, 0), 0xC8: (0, 0, 0, 0), 0xC9: (0, 0, 0, 0),
    0xCA: (0, 0, 0, 0), 0xCB: (0, 0, 0, 0), 0xCC: (0, 0, 0, 0),
    0xCD: (0, 0, 0, 0), 0xCE: (0, 0, 0, 0), 0xCF: (0, 0, 0, 0),
    0xD0: (0, 0, 0, 0), 0xD1: (0, 0, 400, 0), 0xD2: (0, 0, 0, 0),
    0xD3: (0, 0, 0, 0), 0xD4: (0, 0, 0, 0), 0xD5: (0, 0, 0, 0),
    0xD6: (0, 0, 0, 0), 0xD7: (0, 0, 0, 0), 0xD8: (0, 0, 0, 0),
    0xD9: (0, 0, 0, 0), 0xDA: (0, 0, 0, 0), 0xDB: (0, 0, 0, 0),
    0xDC: (0, 0, 0, 0), 0xDD: (0, 0, 0, 0), 0xDE: (0, 0, 0, 0),
    0xDF: (0, 0, 0, 0), 0xE0: (0, 0, 0, 0), 0xE1: (0, 0, 0, 0),
}

# item.htm, 附加屬性欄: code -> hit_effect code.  01=麻痺 02=雙擊 03=暴擊 04=中毒.
# The rate is only meaningful for 麻痺/暴擊/中毒, so 雙擊 rows carry None.
GUIDE_ITEM_EFFECT = {
    0x12: (0x04, 10), 0x24: (0x03, 100), 0x26: (0x02, None), 0x28: (0x03, 100),
    0x2A: (0x03, 100), 0x31: (0x04, 20), 0x3C: (0x04, 30), 0x3E: (0x01, 10),
    0x44: (0x03, 40), 0x47: (0x03, 10), 0x4A: (0x03, 20), 0x4B: (0x01, 10),
    0x4E: (0x03, 20), 0x4F: (0x02, None), 0x50: (0x02, None), 0x5A: (0x03, 10),
    0x5F: (0x01, 30), 0xA1: (0x03, 10), 0xA2: (0x03, 50), 0xA5: (0x01, 20),
    0xA6: (0x01, 20), 0xA7: (0x01, 20), 0xA8: (0x02, None), 0xAA: (0x04, 40),
    0xAB: (0x01, 30), 0xAC: (0x03, 10), 0xB1: (0x03, 30), 0xB2: (0x04, 20),
    0xBE: (0x03, 20), 0xBF: (0x03, 100), 0xC0: (0x03, 100), 0xC1: (0x03, 100),
}

# item.htm, 距離欄.  The page writes deltas against a per-weapon standard; the absolute
# min/max below is the delta applied to the standard the page itself states.
GUIDE_ITEM_RANGE = {
    0x1F: (1, 1), 0x22: (1, 1), 0x23: (1, 1), 0x26: (1, 1), 0x45: (2, 3),
    0x4A: (2, 7), 0x4F: (2, 5), 0x50: (2, 5), 0x51: (1, 2), 0x5B: (1, 2),
    0x5C: (1, 5), 0x5F: (1, 3), 0xA8: (2, 5), 0xAB: (1, 3), 0xAC: (1, 4),
    0xAF: (1, 2), 0xB2: (2, 5), 0xBC: (1, 2), 0xBE: (1, 6), 0xC7: (1, 2),
}

# item.htm, 使用後效果欄與功用欄: code -> (K1, amount, K4 距離, K6 範圍).
# Covers both the rows written 「使用後…」 and the consumables whose 功用 column states an
# HP/MP amount (藥草 etc.).  The permanent boosters (`D6`–`DA`, `DD`, `A4`, `AD`) are left
# out on purpose: their magnitude is not in the record — see assets/items.md.
GUIDE_ITEM_USE = {
    0x09: (0x08, 130, 0x02, 1), 0x0A: (0x09, 150, 0x02, 1), 0x0B: (0x07, 200, 0x02, 0),
    0x1C: (0x20, 400, 0x02, 0), 0x22: (0x09, 140, 0x02, 1), 0x23: (0x08, 100, 0x02, 1),
    0x24: (0x20, 200, 0x02, 2), 0x28: (0x20, 200, 0x02, 2), 0x2F: (0x07, 30, 0x02, 0),
    0x32: (0x07, 80, 0x02, 0), 0x33: (0x20, 80, 0x02, 0), 0x34: (0x20, 120, 0x01, 1),
    0x35: (0x07, 150, 0x02, 0), 0x36: (0x07, 200, 0x02, 0), 0x37: (0x09, 200, 0x03, 0),
    0x4A: (0x20, 200, 0x03, 2), 0x58: (0x07, 100, 0x02, 0), 0x5A: (0x09, 100, 0x02, 1),
    0x62: (0x07, 300, 0x03, 2), 0x63: (0x1E, 9999, 0x1E, 0), 0xA0: (0x07, 200, 0x03, 2),
    0xA1: (0x07, 300, 0x03, 2), 0xA2: (0x07, 500, 0x04, 2), 0xA8: (0x20, 120, 0x03, 2),
    0xBE: (0x07, 400, 0x16, 0), 0xC3: (0x01, 100, 0x02, 1), 0xC4: (0x02, 100, 0x02, 1),
    0xC5: (0x03, 100, 0x02, 1), 0xC6: (0x04, 100, 0x02, 1), 0xC7: (0x1E, 300, 0x1E, 0),
    0xC8: (0x01, 150, 0x02, 1), 0xC9: (0x02, 150, 0x02, 1), 0xCA: (0x03, 150, 0x02, 1),
    0xCB: (0x04, 150, 0x02, 1), 0xCD: (0x01, 200, 0x02, 1), 0xCE: (0x02, 200, 0x02, 1),
    0xCF: (0x03, 200, 0x02, 1), 0xD2: (0x01, 400, 0x02, 1), 0xD3: (0x02, 400, 0x02, 1),
    0xD4: (0x03, 400, 0x02, 1),
    0xB4: (0x0B, 50, 0x01, 0), 0xB5: (0x0B, 100, 0x01, 0), 0xB6: (0x0B, 250, 0x01, 0),
    0xB7: (0x0C, 40, 0x01, 0), 0xB8: (0x0C, 100, 0x01, 0), 0xB9: (0x0C, 250, 0x01, 0),
    0xC2: (0x0B, 1000, 0x01, 0),
}

# spell.htm: code -> (power, hit, distance byte, radius, mp, target).
# 絕招 store a negated AP multiplier; 距離 is 0x10 | n for the straight-line ones.
GUIDE_SPELLS = {
    0x00: (70, 95, 0x03, 1, 7, 0), 0x01: (150, 95, 0x03, 1, 15, 0),
    0x02: (310, 95, 0x03, 1, 35, 0), 0x03: (300, 100, 0x07, 0, 20, 0),
    0x04: (600, 100, 0x08, 0, 40, 0), 0x05: (50, 90, 0x03, 2, 8, 0),
    0x06: (110, 90, 0x04, 2, 15, 0), 0x07: (250, 90, 0x04, 2, 32, 0),
    0x08: (60, 95, 0x04, 1, 10, 0), 0x09: (130, 95, 0x04, 1, 20, 0),
    0x0A: (150, 85, 0x00, 6, 45, 0), 0x0B: (350, 85, 0x00, 8, 95, 0),
    0x0C: (350, 95, 0x06, 1, 35, 0), 0x0D: (500, 90, 0x05, 2, 100, 0),
    0x0E: (70, 100, 0x04, 1, 4, 1), 0x0F: (200, 100, 0x03, 2, 10, 1),
    0x10: (500, 100, 0x03, 2, 25, 1), 0x11: (0, 50, 0x04, 2, 25, 0),
    0x12: (0, 80, 0x02, 0, 10, 0), 0x13: (0, 60, 0x03, 1, 20, 0),
    0x14: (0, 100, 0x05, 3, 50, 1), 0x15: (0, 100, 0x01, 0, 50, 1),
    0x16: (0, 100, 0x03, 1, 40, 1), 0x17: (9999, 50, 0x04, 0, 100, 0),
    0x18: (0, 100, 0x04, 2, 30, 1), 0x19: (-160, 100, 0x17, 0, 65, 0),
    0x1A: (-160, 100, 0x17, 0, 50, 0), 0x1B: (-200, 100, 0x01, 0, 30, 0),
    0x1C: (-160, 100, 0x04, 1, 65, 0), 0x1D: (-120, 100, 0x09, 3, 75, 0),
    0x1E: (-120, 100, 0x06, 2, 40, 0), 0x1F: (-140, 100, 0x14, 0, 35, 0),
    0x20: (520, 90, 0x04, 2, 70, 0), 0x21: (800, 100, 0x03, 2, 55, 1),
    0x22: (1000, 95, 0x06, 1, 80, 0), 0x23: (-250, 100, 0x1F, 0, 100, 0),
    0x24: (700, 95, 0x03, 1, 60, 0), 0x25: (270, 95, 0x04, 1, 35, 0),
    0x26: (700, 95, 0x04, 1, 65, 0), 0x27: (1400, 95, 0x06, 1, 130, 0),
}

# list.htm 滿級屬性表, each character's first line: index -> (LV, AP, DP, DX, HP, MP).
GUIDE_APPEARANCE = {
    0x00: (1, 18, 7, 5, 56, 0), 0x01: (8, 36, 26, 19, 84, 89),
    0x02: (15, 53, 36, 38, 162, 163), 0x03: (15, 105, 60, 15, 225, 28),
    0x04: (7, 51, 26, 18, 124, 12), 0x05: (20, 150, 80, 70, 250, 38),
    0x06: (3, 22, 9, 5, 64, 38), 0x07: (15, 180, 91, 45, 228, 40),
    0x08: (14, 90, 50, 42, 167, 39), 0x09: (16, 112, 80, 0, 240, 45),
    0x0A: (15, 170, 190, 115, 420, 400), 0x0B: (2, 240, 160, 66, 434, 0),
}

# spell.htm 人物職業等級擁有的法術, the rows whose level column is `--`: index -> the
# spells the page says the character already has on the form they join with.
# 蘭斯洛特 (0B) does not appear on that page at all, so his expectation is the empty set.
GUIDE_INITIAL_SPELLS = {
    0x00: (0x00,), 0x01: (0x05,), 0x02: (0x08,), 0x03: (), 0x04: (), 0x05: (),
    0x06: (0x0E,), 0x07: (0x1F,), 0x08: (), 0x09: (0x1D,),
    0x0A: (0x05, 0x06, 0x07, 0x0C, 0x20), 0x0B: (),
}

# modify2.htm 4.升級屬性資料: index -> the eleven bytes the page prints.
GUIDE_LEVELUP = {
    0x00: (4, 6, 3, 5, 2, 3, 9, 11, 3, 4, 0x00),
    0x01: (2, 4, 2, 4, 2, 3, 6, 9, 7, 10, 0x01),
    0x02: (3, 6, 2, 6, 2, 4, 6, 8, 7, 10, 0x02),
    0x03: (5, 11, 4, 6, 1, 2, 10, 16, 2, 4, 0xFF),
    0x04: (5, 7, 3, 5, 2, 2, 10, 13, 2, 4, 0xFF),
    0x05: (5, 7, 3, 5, 3, 4, 8, 10, 2, 4, 0xFF),
    0x06: (4, 6, 2, 4, 1, 2, 8, 10, 4, 6, 0x06),
    0x07: (8, 11, 5, 7, 3, 4, 12, 15, 2, 4, 0xFF),
    0x08: (4, 6, 3, 5, 3, 3, 9, 11, 3, 5, 0x08),
    0x09: (7, 9, 5, 7, 0, 0, 12, 15, 3, 5, 0xFF),
    0x0A: (6, 8, 6, 8, 4, 5, 11, 14, 12, 14, 0x0A),
    0x0B: (10, 16, 10, 16, 3, 4, 14, 16, 0, 0, 0xFF),
    0x0F: (6, 9, 10, 14, 2, 3, 14, 20, 7, 10, 0x0F),
    0x10: (3, 6, 6, 10, 2, 3, 12, 16, 11, 16, 0x10),
    0x11: (6, 10, 8, 14, 2, 3, 12, 14, 13, 17, 0x11),
    0x12: (10, 16, 10, 16, 2, 2, 12, 18, 4, 6, 0x12),
    0x13: (7, 11, 9, 13, 2, 3, 13, 18, 5, 9, 0x13),
    0x14: (9, 13, 7, 10, 3, 3, 14, 18, 4, 6, 0x14),
    0x15: (6, 9, 6, 10, 2, 2, 11, 17, 6, 10, 0x15),
    0x16: (11, 14, 9, 14, 3, 3, 14, 20, 3, 4, 0xFF),
    0x17: (7, 10, 7, 10, 3, 3, 13, 17, 4, 6, 0x17),
    0x18: (9, 13, 6, 8, 2, 3, 11, 14, 5, 7, 0x18),
    0x19: (7, 12, 6, 9, 2, 2, 10, 12, 12, 16, 0x19),
    0x1A: (4, 7, 5, 10, 2, 2, 8, 12, 8, 11, 0x1A),
    0x1B: (15, 22, 7, 10, 2, 2, 15, 24, 3, 4, 0x1B),
    0x1C: (10, 16, 6, 10, 2, 3, 15, 20, 3, 4, 0x1C),
    0x1D: (10, 17, 5, 8, 3, 3, 12, 15, 3, 4, 0x1D),
    0x1E: (9, 13, 4, 8, 2, 2, 12, 15, 9, 15, 0x1E),
    0x1F: (12, 17, 5, 9, 3, 4, 10, 15, 2, 4, 0xFF),
    0x20: (10, 15, 6, 8, 3, 3, 11, 15, 7, 12, 0x20),
    0x21: (15, 18, 10, 13, 3, 4, 18, 20, 14, 18, 0x21),
}

# modify2.htm 5.法術習得等級資料: index -> the six (level, spell) pairs the page prints.
_F = (0xFF, 0xFF)
GUIDE_LEARN = {
    0x00: [(0x10, 0x01), (0x1E, 0x02), _F, _F, _F, _F],
    0x01: [(0x0B, 0x13), (0x0F, 0x06), (0x14, 0x0E), (0x19, 0x07), _F, _F],
    0x02: [(0x0F, 0x09), (0x14, 0x0A), (0x1C, 0x25), _F, _F, _F],
    0x03: [_F] * 6, 0x04: [_F] * 6, 0x05: [_F] * 6,
    0x06: [(0x0A, 0x11), (0x10, 0x0F), (0x1C, 0x10), _F, _F, _F],
    0x07: [_F] * 6, 0x08: [_F] * 6, 0x09: [_F] * 6,
    0x0A: [(0x14, 0x22), _F, _F, _F, _F, _F],
    0x0B: [_F] * 6, 0x0C: [_F] * 6, 0x0D: [_F] * 6, 0x0E: [_F] * 6,
    0x0F: [(0x08, 0x24), _F, _F, _F, _F, _F],
    0x10: [(0x02, 0x0F), (0x06, 0x03), (0x0C, 0x04), (0x0F, 0x10), (0x14, 0x20), _F],
    0x11: [(0x02, 0x0F), (0x06, 0x03), (0x0C, 0x04), (0x11, 0x26), (0x16, 0x10), (0x1A, 0x16)],
    0x12: [(0x05, 0x1B), _F, _F, _F, _F, _F],
    0x13: [(0x0F, 0x10), _F, _F, _F, _F, _F],
    0x14: [(0x02, 0x0F), (0x0D, 0x1E), _F, _F, _F, _F],
    0x15: [(0x05, 0x18), (0x0A, 0x21), _F, _F, _F, _F],
    0x16: [_F] * 6,
    0x17: [(0x0A, 0x13), _F, _F, _F, _F, _F],
    0x18: [(0x08, 0x19), (0x0F, 0x24), _F, _F, _F, _F],
    0x19: [(0x02, 0x0F), (0x06, 0x0C), (0x0C, 0x20), (0x12, 0x22), (0x19, 0x27), _F],
    0x1A: [(0x0D, 0x0B), (0x11, 0x26), _F, _F, _F, _F],
    0x1B: [(0x08, 0x1B), _F, _F, _F, _F, _F],
    0x1C: [(0x0A, 0x1A), _F, _F, _F, _F, _F],
    0x1D: [(0x0A, 0x12), (0x14, 0x13), _F, _F, _F, _F],
    0x1E: [(0x02, 0x03), (0x0A, 0x14), (0x0F, 0x21), (0x14, 0x04), (0x19, 0x18), _F],
    0x1F: [_F] * 6,
    0x20: [(0x0C, 0x1C), _F, _F, _F, _F, _F],
    0x21: [(0x03, 0x19), (0x06, 0x10), (0x0A, 0x24), (0x0F, 0x15), (0x14, 0x23), _F],
}

# modify2.htm 6.職業相關資料: class code -> (eight terrain costs, 暴擊, 100-魔抗).
GUIDE_CLASSES = {
    0x00: ((1, 2, 1, 2, 255, 255, 255, 255), 0x03, 0x5F),
    0x01: ((1, 2, 1, 1, 255, 255, 255, 255), 0x05, 0x50),
    0x02: ((1, 2, 1, 1, 255, 255, 255, 255), 0x08, 0x5A),
    0x03: ((1, 2, 1, 1, 255, 255, 255, 255), 0x0A, 0x32),
    0x04: ((1, 2, 2, 2, 255, 255, 255, 255), 0x03, 0x64),
    0x05: ((1, 2, 1, 2, 255, 255, 255, 255), 0x05, 0x50),
    0x06: ((1, 2, 1, 2, 255, 255, 255, 255), 0x0A, 0x5F),
    0x07: ((1, 2, 1, 2, 255, 255, 255, 255), 0x03, 0x64),
    0x08: ((1, 2, 1, 1, 255, 255, 255, 255), 0x05, 0x5F),
    0x09: ((1, 2, 1, 1, 255, 255, 255, 255), 0x08, 0x55),
    0x0A: ((1, 2, 1, 1, 255, 255, 255, 255), 0x03, 0x64),
    0x0B: ((1, 2, 1, 1, 255, 255, 255, 255), 0x05, 0x55),
    0x0C: ((1, 2, 1, 1, 255, 255, 255, 255), 0x0F, 0x5F),
    0x0D: ((1, 2, 1, 1, 255, 255, 255, 255), 0x00, 0x4B),
    0x0E: ((1, 2, 1, 1, 255, 255, 255, 255), 0x00, 0x3C),
    0x0F: ((1, 2, 1, 1, 255, 255, 255, 255), 0x00, 0x41),
    0x10: ((1, 2, 1, 1, 255, 255, 255, 255), 0x03, 0x50),
    0x11: ((1, 2, 1, 1, 255, 255, 255, 255), 0x05, 0x46),
    0x12: ((1, 2, 1, 1, 255, 255, 255, 255), 0x05, 0x41),
    0x13: ((1, 2, 1, 1, 255, 255, 255, 255), 0x0A, 0x5F),
    0x14: ((1, 2, 1, 1, 255, 255, 255, 255), 0x0A, 0x50),
    0x15: ((1, 2, 1, 1, 255, 255, 255, 255), 0x0F, 0x5A),
    0x16: ((1, 1, 1, 1, 1, 1, 255, 255), 0x05, 0x64),
    0x17: ((1, 2, 1, 1, 1, 1, 255, 255), 0x05, 0x50),
    0x18: ((1, 2, 1, 1, 1, 1, 255, 1), 0x0A, 0x5A),
    0x19: ((1, 2, 2, 2, 255, 255, 255, 255), 0x00, 0x3C),
    0x1A: ((255, 255, 255, 255, 255, 255, 255, 255), 0x00, 0x5A),
    0x1B: ((1, 1, 1, 1, 255, 255, 255, 255), 0x00, 0x46),
    0x1C: ((1, 1, 1, 2, 255, 255, 255, 255), 0x05, 0x55),
    0x1D: ((1, 1, 1, 1, 255, 255, 255, 255), 0x03, 0x64),
    0x1E: ((1, 2, 1, 2, 255, 255, 255, 255), 0x05, 0x64),
    0x1F: ((1, 1, 1, 1, 1, 1, 255, 255), 0x03, 0x64),
    0x20: ((1, 2, 2, 2, 255, 255, 255, 255), 0x00, 0x50),
    0x21: ((1, 1, 1, 1, 255, 255, 255, 255), 0x00, 0x32),
    0x22: ((1, 1, 1, 1, 255, 255, 255, 255), 0x0A, 0x5A),
    0x23: ((1, 2, 2, 2, 255, 255, 255, 255), 0x05, 0x64),
    0x24: ((255, 255, 255, 255, 255, 255, 255, 255), 0x00, 0x64),
    0x25: ((1, 1, 1, 1, 1, 1, 255, 255), 0x00, 0x64),
    0x26: ((1, 1, 1, 1, 1, 255, 255, 255), 0x0A, 0x0A),
}

# Mismatches that have been investigated; the data file wins in every case.
# tools/data_skill/build.py ships this same set to the fdps-data skill as DISCREPANCIES,
# so an entry added here has to be added there too.
ACCEPTED = {
    ("item.hit", 0x4A): "攻略站的風神弓 HIT 寫 100，資料檔是 150",
    ("spell.hit", 0x17): "攻略站的咒殺術命中率寫 50%，資料檔是 60",
    ("spell.target", 0x16): "神行術的作用對象是 3；攻略站只列了 00／01 兩個值",
    ("class", 0x18): "攻略站的機械大師第八個地形消耗寫 01，資料檔是 FF",
    ("appearance.spells", 0x00): "攻略站把業火列為劍士蘭迪斯的初始法術，資料檔的遮罩是空的",
    ("appearance.spells", 0x02): "攻略站只給費塔加冰爆術，資料檔另有 00 業火與 09 絕殺冰封"
                                 "（09 是他 Lv15 的習得，攻略站列他以 15 級出場）",
    ("appearance.spells", 0x09): "攻略站列蓋亞有轟神砲，資料檔的遮罩是空的——那來自 A4 強化套件",
    ("appearance.spells", 0x0A): "珊的法術，攻略站列的五個與資料檔的七個不同",
    ("appearance.spells", 0x0B): "資料檔的遮罩有 06 奔雷彈，攻略站的法術頁沒有列蘭斯洛特",
}


class Tally:
    """Counts every comparison made, so the reported total cannot drift from the checks."""

    def __init__(self):
        self.compared = 0
        self.findings = []

    def compare(self, kind, key, expected, actual):
        self.compared += 1
        if expected != actual:
            self.findings.append({"kind": kind, "key": key, "expected": expected,
                                  "actual": actual, "accepted": ACCEPTED.get((kind, key))})


def check_items(records, tally):
    for code, (ap, hit, dp, ev) in sorted(GUIDE_ITEM_STATS.items()):
        r = records[code]
        for field, want in (("ap", ap), ("hit", hit), ("dp", dp), ("ev", ev)):
            tally.compare("item." + field, code, want, r[field])
    for code, (eff, rate) in sorted(GUIDE_ITEM_EFFECT.items()):
        r = records[code]
        tally.compare("item.hit_effect", code, eff, r["hit_effect"])
        if rate is not None:
            tally.compare("item.hit_effect_rate", code, rate, r["hit_effect_rate"])
    for code, want in sorted(GUIDE_ITEM_RANGE.items()):
        r = records[code]
        tally.compare("item.range", code, want, (r["range_min"], r["range_max"]))
    for code, want in sorted(GUIDE_ITEM_USE.items()):
        r = records[code]
        tally.compare("item.use", code, want,
                      (r["use_effect"], r["use_amount"], r["use_distance"], r["use_radius"]))


def check_spells(records, tally):
    fields = ("power", "hit", "distance", "radius", "mp", "target")
    for code, want in sorted(GUIDE_SPELLS.items()):
        r = records[code]
        for i, name in enumerate(fields):
            tally.compare("spell." + name, code, want[i], r[name])


def check_appearance(appearance, levelup, tally):
    """The page prints in-game stats; the tables hold bases, so apply the growth."""
    for index, (lv, ap, dp, dx, hp, mp) in sorted(GUIDE_APPEARANCE.items()):
        a = appearance[index]
        g = levelup[index]
        got = {
            "ap": a["ap_base"] + lv * g["ap_min"],
            "dp": a["dp_base"] + lv * g["dp_min"],
            "dx": a["dx_base"] + lv * g["dx_min"],
            "hp": a["hp_base"] + (lv - 1) * g["hp_min"],
            "mp": a["mp_base"] + (lv - 1) * g["mp_min"],
        }
        for name, want in (("ap", ap), ("dp", dp), ("dx", dx), ("hp", hp), ("mp", mp)):
            tally.compare("appearance." + name, index, want, got[name])


def check_initial_spells(records, tally):
    """The record holds a 32-bit mask; bit n is spell n."""
    for index, want in sorted(GUIDE_INITIAL_SPELLS.items()):
        mask = records[index]["spells"]
        tally.compare("appearance.spells", index, want,
                      tuple(i for i in range(32) if mask >> i & 1))


def check_levelup(records, tally):
    keys = ("ap_min", "ap_max", "dp_min", "dp_max", "dx_min", "dx_max",
            "hp_min", "hp_max", "mp_min", "mp_max", "learn_index")
    for index, want in sorted(GUIDE_LEVELUP.items()):
        tally.compare("levelup", index, want, tuple(records[index][k] for k in keys))


def check_learn(records, tally):
    for index, want in sorted(GUIDE_LEARN.items()):
        got = tuple((p["level"], p["spell"]) for p in records[index])
        tally.compare("learn", index, tuple(want), got)


def check_classes(records, tally):
    """The guide's class 00 sits at PROMAP.DAT + 10, so the record index is code + 1."""
    for code, want in sorted(GUIDE_CLASSES.items()):
        r = records[code + 1]
        got = (tuple(r["move_cost"]), r["critical"], r["magic_resist_complement"])
        tally.compare("class", code, want, got)


def render(tally):
    lines = ["# 攻略站數值與資料檔的逐筆比對", "",
             f"比對 {tally.compared} 個欄位，不一致 {len(tally.findings)} 處。", ""]
    if tally.findings:
        lines += ["| 項目 | 編號 | 攻略站 | 資料檔 | 已查明 |",
                  "| --- | ---: | --- | --- | --- |"]
        for f in tally.findings:
            lines.append("| %s | %02X | `%s` | `%s` | %s |"
                         % (f["kind"], f["key"], f["expected"], f["actual"],
                            f["accepted"] or "**未查明**"))
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    tables = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))["tables"]
    rows = {k: tables[k]["records"] for k in tables}

    tally = Tally()
    check_items(rows["item"], tally)
    check_spells(rows["spell"], tally)
    check_appearance(rows["appearance"], rows["levelup"], tally)
    check_initial_spells(rows["appearance"], tally)
    check_levelup(rows["levelup"], tally)
    check_learn(rows["learn"], tally)
    check_classes(rows["class"], tally)

    Path(sys.argv[2]).write_text(render(tally), encoding="utf-8")

    unexplained = [f for f in tally.findings if not f["accepted"]]
    for f in tally.findings:
        print("%-22s %02X guide=%s data=%s %s"
              % (f["kind"], f["key"], f["expected"], f["actual"],
                 "accepted" if f["accepted"] else "UNEXPLAINED"))
    print("compared %d fields, %d mismatches, %d unexplained"
          % (tally.compared, len(tally.findings), len(unexplained)))
    if unexplained:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
