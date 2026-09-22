"""Call-spelled variants of src/ units, for testing what -oe does to them.

src/ open-codes several small same-module functions at the places where
FDPS.LE has an expanded copy of them.  Each variant here puts the call back,
so that fn_match.py / oe_threshold.py can see whether a given -oe setting
re-creates the expansion the original has.  The rewrites are applied to the
current src/ text at run time; a rewrite that no longer finds its target
raises instead of silently producing an unchanged unit.

    palv  palette.c   fdps_pack_rgb spelled as a call in fdps_build_palette_tables
    safv  saf.c       fdps_saf_frame_count / fdps_saf_get_frame spelled as calls
                      in fdps_saf_advance_tick
    gauv  gauge.c     fdps_draw_unit_gauge_proportional (x6) and
                      fdps_draw_stat_gauge (x2) spelled as calls
    chvv  chevt6.c    fdps_unit_is_retired defined in the unit and called as a
                      separate `if`
    chvw  chevt6.c    the same, but called as the right operand of `&&`, which
                      is how the original's condition reads (00010760)
"""

import re

NAMES = ["palv", "safv", "gauv", "chvv", "chvw"]


class VariantError(Exception):
    pass


def _need(n, what):
    if not n:
        raise VariantError("variant rewrite found nothing to replace: %s" % what)


def _palv(read):
    s = read("palette.c")
    m = re.search(r"packed_color = \(\(unsigned int\) \(unsigned char\) red_scaled"
                  r" << 16\)[^;]*;", s)
    _need(m, "packed_color expression in palette.c")
    return s.replace(m.group(0),
                     "packed_color = fdps_pack_rgb((unsigned char) red_scaled, "
                     "(unsigned char) green_scaled, (unsigned char) blue_scaled);")


def _safv(read):
    s = read("saf.c")
    start = s.index("int fdps_saf_advance_tick")
    body = s[start:]
    m1 = re.search(r"if \(saf_base\[0\] == 'S'[^{]*\{\s*frame_count =[^;]*;\s*\}"
                   r" else \{\s*frame_count = 0;\s*\}", body)
    _need(m1, "open-coded frame count in fdps_saf_advance_tick")
    body = body.replace(m1.group(0),
                        "frame_count = fdps_saf_frame_count(saf_base);", 1)
    m2 = re.search(r"if \(cursor\[SAF_CURSOR_FRAME_INDEX\]\s*<[^{]*\{\s*"
                   r"frame_section_start =[^;]*;\s*frame = [^;]*;\s*\} else \{\s*"
                   r"frame = NULL;\s*\}", body)
    _need(m2, "open-coded frame lookup in fdps_saf_advance_tick")
    body = body.replace(m2.group(0),
                        "frame = (unsigned char *) fdps_saf_get_frame("
                        "saf_base, cursor[SAF_CURSOR_FRAME_INDEX]);", 1)
    return s[:start] + body


def _gauv(read):
    s = read("gauge.c")
    s, n1 = re.subn(
        r"if \((\w+)_hp_max <= 0\) \{\s*\1_fill_width = 0;\s*\} else \{\s*"
        r"\1_fill_width =[^;]*;\s*\}\s*fdps_draw_unit_gauge\((\w+), (\w+),\s*"
        r"(\w+), \1_fill_width,\s*([^,]+), ([^)]+)\);",
        r"fdps_draw_unit_gauge_proportional(\2, \3, \4, \1_hp_max, "
        r"\1_hp_current, \5, \6);", s)
    s, n2 = re.subn(
        r"if \((hp|mp)_max <= 0\) \{\s*\1_fill_width = 0;\s*\} else \{\s*"
        r"\1_fill_width =[^;]*;\s*\}\s*fdps_draw_gauge_fill\(([^;]*?),\s*"
        r"dest_stride, (gauge_strip(?: \+ 1)?), \1_fill_width\);",
        r"fdps_draw_stat_gauge(\2, dest_stride, \3, \1_max, \1_current);", s)
    if (n1, n2) != (6, 2):
        raise VariantError("gauge.c rewrites: unit gauge %d (want 6), stat gauge "
                           "%d (want 2)" % (n1, n2))
    return s


def _is_retired(read):
    u = read("unit.c")
    m = re.search(r"int fdps_unit_is_retired\(int unit_index\)\n\{.*?\n\}\n", u, re.S)
    _need(m, "fdps_unit_is_retired in unit.c")
    return m.group(0)


def _chvv(read):
    # Defined after 00010760's function, as in the image: -oe does not care
    # about the order, it only needs the body in the same translation unit.
    return read("chevt6.c") + "\n" + _is_retired(read)


def _chvw(read):
    s = read("chevt6.c")
    start = s.index("void fdps_chapter_30_revive_wave_4_undead(void)\n{")
    end = s.index("\n}\n", start) + 3
    fn = s[start:end]
    guard = re.search(
        r"if \(unit->portrait_id != (CH30_REVIVE_WRAITH_CHAR_ID)\s*"
        r"&& unit->portrait_id != (CH30_REVIVE_SKELETON_CHAR_ID)\) \{\s*"
        r"continue;\s*\}\s*if \(fdps_unit_is_retired\(unit_index\) == 0\) \{\s*"
        r"continue;\s*\}", fn)
    _need(guard, "the two early-continue guards in 00010760")
    fn = fn.replace(guard.group(0),
                    "if ((unit->portrait_id == %s\n"
                    "             || unit->portrait_id == %s)\n"
                    "            && fdps_unit_is_retired(unit_index)) {"
                    % (guard.group(1), guard.group(2)), 1)
    if not fn.endswith("\n    }\n}\n"):
        raise VariantError("00010760 no longer ends with its loop's closing brace")
    fn = fn[:-len("}\n")] + "    }\n}\n"
    return s[:start] + fn + s[end:] + "\n" + _is_retired(read)


_BUILD = {"palv": (_palv, "palette.c"), "safv": (_safv, "saf.c"),
          "gauv": (_gauv, "gauge.c"), "chvv": (_chvv, "chevt6.c"),
          "chvw": (_chvw, "chevt6.c")}


def build(name, read):
    """-> (source text, src/ unit whose functions it is compared against)."""
    fn, unit = _BUILD[name]
    return fn(read), unit
