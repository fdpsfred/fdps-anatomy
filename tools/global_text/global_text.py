"""Transcribe the text that belongs to no chapter: FDETXT00.TXT, the resident
global block, and FDETXT31..65, the blocks of the extra scenes (map 30 and up).

Everything written is derived, nothing is typed by hand:

  - the words come from text_decode (the owner of the block format and the
    glyph table);
  - who reads an FDETXT00 entry comes from scanning src/ for every
    fdps_draw_text(data_fdps_all_game_text_ptr, ...) call and every store into
    the two substitution slots, with the id expression resolved through the
    file's #defines;
  - which scene shows an FDETXT31..65 entry comes from cutscene_script's trace
    (every DRAW_TEXT and ASK_THREE_WAY reference, with the map current when the
    script reaches it).

What IS hand data here is the naming of FDETXT00's regions and message groups
(REGIONS, MESSAGE_GROUPS) and the prose around the tables.  `verify` checks
that hand data against the scan: the regions tile the block, every indexed
reader adds the base of exactly one region, every fixed reader lands in the
message region, every non-empty message has a reader, the gaps are empty, and
the only non-empty entry nobody reads is the one listed in NO_READER.

CLI:
    python global_text.py build  [--game DIR]   write the two assets/text pages
    python global_text.py verify [--game DIR]   run the gates and compare the
                                                pages on disk with a rebuild
"""

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "text_decode"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "cutscene_script"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "code_emit"))
import text_decode  # noqa: E402
from build_emit import strip_c  # noqa: E402  (the owner of src/ scanning)

DEFAULT_GAME = REPO_ROOT / "fdps_game_files"
SRC_DIR = REPO_ROOT / "src"
GLOBAL_PAGE = REPO_ROOT / "assets" / "text" / "global_text.md"
SCENE_PAGE = REPO_ROOT / "assets" / "text" / "scene_text.md"
FIELD_ARCHIVE = "FIELD.VFS"

GLOBAL_PTR = "data_fdps_all_game_text_ptr"
CHAPTER_PTRS = ("data_fdps_current_chapter_text_ptr", "chapter_text")
SUBST_SLOTS = {
    "data_fdps_dialog_last_action_text_id_param": "subst1",
    "data_fdps_dialog_subst_text_id_2": "subst2",
}
CHAPTER_HEADER_ENTRIES = 9
FIRST_SCENE_BLOCK = 31
LAST_SCENE_BLOCK = 65


class ScanError(Exception):
    """A reader in src/ could not be resolved to entry ids."""


class GateError(Exception):
    """The hand data and the scan disagree."""


# ---------------------------------------------------------------------------
# FDETXT00's regions and message groups (hand data, checked by verify)
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Region:
    first: int
    last: int
    key: str
    title: str
    base: int | None      # the constant readers add to a code, None if fixed ids
    code_name: str | None  # what the added code is


REGIONS = (
    Region(0x000, 0x000, "glyph_row", "字模列", None, None),
    Region(0x001, 0x096, "unit_names", "單位名", 0x001, "角色編號"),
    Region(0x097, 0x09d, "race_names", "種族名", 0x097, "種族代碼"),
    Region(0x09e, 0x0a0, "gap", "空白", None, None),
    Region(0x0a1, 0x0c8, "class_names", "職業名", 0x0a1, "職業代碼"),
    Region(0x0c9, 0x1aa, "item_names", "物品名", 0x0c9, "物品編號"),
    Region(0x1ab, 0x1bd, "gap", "空白", None, None),
    Region(0x1be, 0x1e5, "spell_names", "法術名", 0x1be, "法術編號"),
    Region(0x1e6, 0x22a, "messages", "系統訊息", None, None),
)

MESSAGE_GROUPS = (
    (0x1e6, 0x1e9, "敵兵死亡時的掉落"),
    (0x1ea, 0x1ef, "戰場系統選單：全軍行動與結束回合"),
    (0x1f0, 0x1f3, "離開遊戲與記錄戰況"),
    (0x1f4, 0x202, "城鎮：道具、商店、酒店、教會與秘密商店"),
    (0x203, 0x209, "記錄與讀取"),
    (0x20a, 0x213, "搜尋寶箱與寶物、錢不夠"),
    (0x214, 0x21c, "教會轉職"),
    (0x21d, 0x221, "道具使用：強化套件與高能量裝置"),
    (0x222, 0x223, "換片提示"),
    (0x224, 0x229, "酒店抽獎"),
    (0x22a, 0x22a, "換片之後"),
)

REGION_BY_KEY = {r.key: r for r in REGIONS if r.key != "gap"}

# Non-empty entries no code in src/ reads.
NO_READER = {0x000}


# ---------------------------------------------------------------------------
# Scanning src/ for readers
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Reader:
    file: str
    line: int
    function: str
    via: str               # "draw", "subst1" or "subst2"
    expression: str        # the id expression as written
    ids: tuple | None      # fixed entry ids, sorted
    base: int | None       # the constant added to a runtime code
    index: str | None      # the runtime part of an indexed expression


_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+([^\n]+)$", re.M)
_FUNCTION = re.compile(r"^[A-Za-z_][^;{}()\n]*?\b([A-Za-z_]\w*)[ \t]*\([^;{}]*\)\s*\{", re.M)
_IDENT = re.compile(r"(?<!\w)[A-Za-z_]\w*")   # not the x of 0x1f0
_CAST = re.compile(r"\(\s*(?:unsigned\s+|signed\s+)?(?:int|char|short|long)\s*\)")
_NUMBER = re.compile(r"^(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$")


def _c_integer(literal):
    """The value of a C integer literal: 0x hex, leading-0 octal, else decimal."""
    digits = _NUMBER.match(literal.strip()).group(1)
    if digits[:2] in ("0x", "0X"):
        return int(digits, 16)
    if len(digits) > 1 and digits[0] == "0":
        return int(digits, 8)
    return int(digits, 10)


def _read_sources(src_dir):
    """{file name: text with comments and literals blanked}, line numbers kept."""
    return {p.name: strip_c(p.read_text(encoding="utf-8", errors="replace"))
            for p in sorted(Path(src_dir).glob("*.[ch]"))}


def _collect_macros(sources):
    per_file = {}
    for name, text in sources.items():
        per_file[name] = {m.group(1): m.group(2).strip() for m in _DEFINE.finditer(text)}
    headers = {}
    for name, macros in per_file.items():
        if name.endswith(".h"):
            for key, value in macros.items():
                headers.setdefault(key, set()).add(value)
    shared = {k: next(iter(v)) for k, v in headers.items() if len(v) == 1}
    return per_file, shared


def _macro_value(name, local, shared, depth=0):
    raw = local.get(name, shared.get(name))
    if raw is None or depth > 8:
        return None
    raw = raw.strip()
    while raw.startswith("(") and raw.endswith(")"):
        raw = raw[1:-1].strip()
    if _NUMBER.match(raw):
        return _c_integer(raw)
    if _IDENT.fullmatch(raw):
        return _macro_value(raw, local, shared, depth + 1)
    return None


def _argument(text, pos):
    """The text of one call argument starting at pos, and where it ends."""
    depth = 0
    start = pos
    while pos < len(text):
        ch = text[pos]
        if ch in "([":
            depth += 1
        elif ch in ")]":
            if depth == 0:
                break
            depth -= 1
        elif ch == "," and depth == 0:
            break
        pos += 1
    return text[start:pos].strip(), pos


def _function_at(text, pos):
    name = "?"
    for m in _FUNCTION.finditer(text, 0, pos):
        name = m.group(1)
    return name


def _function_body(text, pos):
    """The source of the function definition that contains pos."""
    start = 0
    for m in _FUNCTION.finditer(text, 0, pos):
        start = m.start()
    following = _FUNCTION.search(text, pos)
    return text[start:following.start() if following else len(text)]


def _resolve(expression, local, shared, body):
    """(ids, base, index) for one id expression, or None if it is not one of
    the three shapes src/ uses: all macros and literals, a runtime value plus
    one macro, or a local assigned only macro values in the same function."""
    bare = " ".join(_CAST.sub(" ", expression).split())
    # Subscripts are part of a runtime value: fold them away before looking
    # at the top level of the expression.
    top = bare
    while re.search(r"\[[^\[\]]*\]", top):
        top = re.sub(r"\[[^\[\]]*\]", "@", top)
    top = top.replace("@", "[]")
    idents = _IDENT.findall(top)
    macros = [i for i in idents if _macro_value(i, local, shared) is not None]
    runtime = [i for i in idents if i not in macros]
    if not runtime:
        # A sum of macros and literals: nothing else is accepted.
        terms = re.findall(r"([+-]?)\s*([A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+)", top)
        if not terms or re.sub(r"[+\-\s]", "", top) != "".join(t for _, t in terms):
            return None
        total = 0
        for sign, term in terms:
            value = _macro_value(term, local, shared) if _IDENT.fullmatch(term) \
                else _c_integer(term)
            total += -value if sign == "-" else value
        return (total,), None, None
    if len(macros) == 1:
        name = macros[0]
        m = re.fullmatch(rf"(.+?)\s*\+\s*{name}|{name}\s*\+\s*(.+)", top)
        rest = m and (m.group(1) or m.group(2))
        if rest and not re.search(r"[+*/]|-(?!>)", rest):
            base = _macro_value(name, local, shared)
            # An element of a local table with a literal initializer: the
            # reachable ids are the base plus each value in the table.
            table = re.match(r"(\w+)\[\]$", rest)
            init = table and re.search(rf"\b{table.group(1)}\s*\[[^\]]*\]\s*=\s*\{{([^}}]*)\}}", body)
            if init:
                values = [v.strip() for v in init.group(1).split(",") if v.strip()]
                if all(_NUMBER.match(v) for v in values):
                    return tuple(sorted({base + _c_integer(v) for v in values})), None, None
            index = re.sub(rf"\s*\+\s*\b{name}\b|\b{name}\b\s*\+\s*", "", expression, count=1)
            return None, base, " ".join(index.split())
        return None
    if not macros and len(runtime) == 1 and _IDENT.fullmatch(bare):
        # A plain local: every write to it must be a plain assignment of
        # macros; a compound assignment or an increment is not followed.
        if re.search(rf"(?<![\w.>]){bare}\s*(?:[-+*/%&|^]|<<|>>)=|(?<![\w.>]){bare}\s*(\+\+|--)"
                     rf"|(\+\+|--)\s*{bare}\b", body):
            return None
        values = set()
        for m in re.finditer(rf"(?<![\w.>]){bare}\s*=(?!=)\s*([^;]+);", body):
            assigned = _resolve(m.group(1), local, shared, "")
            if assigned is None or assigned[0] is None:
                return None
            values.update(assigned[0])
        if values:
            return tuple(sorted(values)), None, None
    return None


def _draw_calls(text, pointers):
    pattern = re.compile(r"\bfdps_draw_text\(\s*(" + "|".join(pointers) + r")\s*,")
    for m in pattern.finditer(text):
        expression, _ = _argument(text, m.end())
        yield m.start(), m.group(1), " ".join(expression.split())


def scan_readers(src_dir=SRC_DIR):
    """Every reader of FDETXT00 entries in src/: direct draws and slot stores."""
    sources = _read_sources(src_dir)
    per_file, shared = _collect_macros(sources)
    readers = []
    problems = []
    for name, text in sources.items():
        if not name.endswith(".c"):
            continue
        local = per_file[name]
        sites = [(pos, "draw", expr) for pos, _, expr in _draw_calls(text, [GLOBAL_PTR])]
        slot = re.compile(r"\b(" + "|".join(SUBST_SLOTS) + r")\s*=(?!=)\s*([^;]+);")
        sites += [(m.start(), SUBST_SLOTS[m.group(1)], " ".join(m.group(2).split()))
                  for m in slot.finditer(text) if _function_at(text, m.start()) != "?"]
        for pos, via, expression in sorted(sites):
            if via == "draw" and expression in SUBST_SLOTS:
                continue  # fdps_draw_text's own substitution: the slot stores are the readers
            line = text.count("\n", 0, pos) + 1
            resolved = _resolve(expression, local, shared, _function_body(text, pos))
            if resolved is None:
                problems.append(f"{name}:{line}: cannot resolve `{expression}`")
                continue
            ids, base, index = resolved
            readers.append(Reader(name, line, _function_at(text, pos), via, expression,
                                  ids, base, index))
    if problems:
        raise ScanError("\n".join(problems))
    return readers


def scan_chapter_header_readers(src_dir=SRC_DIR):
    """Draws of a chapter block's first CHAPTER_HEADER_ENTRIES entries by a
    fixed id: {entry: [(function, file)]}.

    Only fixed ids are in scope.  The chapter block is also read by ids that
    come out of data -- a script's DRAW_TEXT operand, a death script's operand,
    a unit index plus a bias, the credits' caption base -- and which of those
    land below CHAPTER_HEADER_ENTRIES is a question about the data, which the
    chapter pages answer, so such calls are passed over here by design."""
    sources = _read_sources(src_dir)
    per_file, shared = _collect_macros(sources)
    found = {}
    for name, text in sources.items():
        if not name.endswith(".c"):
            continue
        for pos, _, expression in _draw_calls(text, CHAPTER_PTRS):
            resolved = _resolve(expression, per_file[name], shared, _function_body(text, pos))
            if resolved is None or resolved[0] is None:
                continue
            for entry in resolved[0]:
                if entry < CHAPTER_HEADER_ENTRIES:
                    site = (_function_at(text, pos), name)
                    found.setdefault(entry, [])
                    if site not in found[entry]:
                        found[entry].append(site)
    return found


# ---------------------------------------------------------------------------
# Scene blocks
# ---------------------------------------------------------------------------

def classify_scene_block(texts, refs):
    """Split one block's entries by whether a script shows them.

    texts: the rendered entries, in order.  refs: (script, offset, entry) for
    every script reference into this block.  Returns ({entry: [(script,
    offset)]} for shown entries, [never-shown non-empty entries], [empty
    entries])."""
    shown = {}
    for script, offset, entry in refs:
        shown.setdefault(entry, [])
        if (script, offset) not in shown[entry]:
            shown[entry].append((script, offset))
    unshown = [i for i, t in enumerate(texts) if t and i not in shown]
    empty = [i for i, t in enumerate(texts) if not t and i not in shown]
    return dict(sorted(shown.items())), unshown, empty


# ---------------------------------------------------------------------------
# Loading the game data
# ---------------------------------------------------------------------------

def load_blocks(game_dir):
    import cutscene_script  # the owner of the container read used here too
    field = cutscene_script.read_container(Path(game_dir) / FIELD_ARCHIVE)
    table = text_decode.load_glyph_table()
    blocks = {}
    for number in [0] + list(range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1)):
        entries = text_decode.parse_block(field[f"FDETXT{number:02d}.TXT"])
        blocks[number] = [text_decode.render_entry(e, table) for e in entries]
    return blocks


def load_scripts(game_dir):
    import cutscene_script
    return cutscene_script.decode_all(game_dir, cutscene_script.scan_callers())


# ---------------------------------------------------------------------------
# Gates
# ---------------------------------------------------------------------------

def check_global(texts, readers):
    """Every claim the global page makes about FDETXT00, checked."""
    problems = []
    if REGIONS[0].first != 0 or REGIONS[-1].last != len(texts) - 1:
        problems.append(f"regions cover {REGIONS[0].first:#x}..{REGIONS[-1].last:#x}, "
                        f"the block has {len(texts)} entries")
    for a, b in zip(REGIONS, REGIONS[1:]):
        if b.first != a.last + 1:
            problems.append(f"regions {a.key} and {b.key} do not meet")
    messages = REGION_BY_KEY["messages"]
    if MESSAGE_GROUPS[0][0] != messages.first or MESSAGE_GROUPS[-1][1] != messages.last:
        problems.append("message groups do not cover the message region")
    for a, b in zip(MESSAGE_GROUPS, MESSAGE_GROUPS[1:]):
        if b[0] != a[1] + 1:
            problems.append(f"message groups at {a[0]:#x} and {b[0]:#x} do not meet")
    bases = {r.base: r for r in REGIONS if r.base is not None}
    read = set()
    for reader in readers:
        where = f"{reader.file}:{reader.line} {reader.function}"
        entries = reader.ids
        if entries is None:
            if reader.base not in bases:
                problems.append(f"{where}: base {reader.base:#x} starts no region")
        else:
            for entry in entries:
                if not messages.first <= entry <= messages.last:
                    problems.append(f"{where}: fixed id {entry:#x} outside the messages")
                read.add(entry)
    for region in REGIONS:
        for entry in range(region.first, region.last + 1):
            text = texts[entry]
            if region.key == "gap" and text:
                problems.append(f"entry {entry:#x} in a gap is not empty")
            if region.key == "messages" and text and entry not in read:
                problems.append(f"message {entry:#x} has no reader")
            if region.base is None and region.key not in ("gap", "messages") \
                    and text and entry not in NO_READER:
                problems.append(f"entry {entry:#x} is read by nobody and not in NO_READER")
    for entry in NO_READER:
        if entry in read or any(r.first <= entry <= r.last and r.base is not None
                                for r in REGIONS):
            problems.append(f"NO_READER entry {entry:#x} does have a reader")
    return problems


def scene_refs(reports):
    """{block: [(script, offset, entry)]} over every script."""
    refs = {}
    for report in reports:
        script = report.member.removesuffix(".DAT")
        for ref in report.trace.text_refs:
            refs.setdefault(ref["block"], []).append((script, ref["offset"], ref["entry"]))
    return refs


def check_scene(blocks, reports):
    problems = []
    for report in reports:
        problems += [f"{report.member}: {p}" for p in report.trace.problems]
    for block, refs in scene_refs(reports).items():
        if FIRST_SCENE_BLOCK <= block <= LAST_SCENE_BLOCK:
            for script, offset, entry in refs:
                if entry >= len(blocks[block]):
                    problems.append(f"{script} {offset:#x}: FDETXT{block:02d} has no entry {entry:#x}")
                    continue
                text = blocks[block][entry]
                if not text:
                    problems.append(f"{script} {offset:#x} shows empty FDETXT{block:02d} {entry:#x}")
                if "{subst" in text or "{number}" in text:
                    problems.append(f"{script} {offset:#x}: FDETXT{block:02d} {entry:#x} "
                                    "substitutes, which the page does not describe")
    return problems


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

CUT_CONTENT = "../../cut_content/_index.md"


def _cell(text):
    return text.replace("|", "\\|")


def _hex(value, width=2):
    return f"`0x{value:0{width}x}`"


def _span(first, last, width=2):
    return _hex(first, width) if first == last else f"{_hex(first, width)}–{_hex(last, width)}"


def _runs(values):
    """Consecutive runs of a sorted list of ints, as (first, last) pairs."""
    runs = []
    for v in values:
        if runs and v == runs[-1][1] + 1:
            runs[-1][1] = v
        else:
            runs.append([v, v])
    return [tuple(r) for r in runs]


def _functions(readers):
    seen = []
    for r in readers:
        site = f"`{r.function}`（`src/{r.file}`）"
        if site not in seen:
            seen.append(site)
    return "、".join(seen)


def _indexed_readers(readers, base):
    lines = []
    for via, label in (("draw", "直接繪製"), ("subst1", "存進代入槽一，由訊息的 `{subst1}` 繪製"),
                       ("subst2", "存進代入槽二，由訊息的 `{subst2}` 繪製")):
        group = [r for r in readers if r.base == base and r.via == via]
        if not group:
            continue
        lines.append(f"- {label}：")
        seen = []
        for r in group:
            item = f"`{r.function}`（`src/{r.file}`）以 `{r.index}`"
            if item not in seen:
                seen.append(item)
                lines.append(f"  - {item}")
    return lines


def _table_rows(texts, first, last, code_base=None, code_label=None, extra=None,
                extra_label=None):
    """Rows for one run of entries, empty runs folded into one row."""
    header = ["條目"] + ([code_label] if code_label else []) + ([extra_label] if extra else []) \
        + ["原文"]
    rows = ["| " + " | ".join(header) + " |", "|" + " --- |" * len(header)]
    entry = first
    while entry <= last:
        if not texts[entry]:
            end = entry
            while end + 1 <= last and not texts[end + 1]:
                end += 1
            cells = [_span(entry, end, 3)]
            if code_label:
                cells.append(_span(entry - code_base, end - code_base))
            if extra:
                cells.append("")
            cells.append("（空字串）")
            rows.append("| " + " | ".join(cells) + " |")
            entry = end + 1
            continue
        cells = [_hex(entry, 3)]
        if code_label:
            cells.append(_hex(entry - code_base))
        if extra:
            cells.append(extra(entry))
        cells.append(_cell(texts[entry]))
        rows.append("| " + " | ".join(cells) + " |")
        entry += 1
    return rows


def render_global(texts, readers, header_readers):
    out = []
    add = out.append
    add("# 全域文字：`FDETXT00.TXT`")
    add("")
    add("`FIELD.VFS` 的 `FDETXT00.TXT` 是不屬於任何一章的文字，由 `fdps_load_global_resources` 在啟動時以固定檔名載入進 "
        "`data_fdps_all_game_text_ptr`，一直留到結束（`src/gamedata.h`）。區塊格式與控制碼見 "
        "[`resource_info/text.md`](../../resource_info/text.md)；本頁的原文由 [`tools/global_text/`](../../tools/global_text/_index.md) "
        "以 [`text_decode`](../../tools/text_decode/_index.md) 解出，讀取端由它掃描 `src/` 產生。")
    add("")
    add(f"共 {len(texts)} 條（`0x000`–`0x{len(texts) - 1:03x}`）。控制碼照解碼器的寫法：`{{br}}` 換行、`{{page}}` 換頁、"
        "`{subst1}`／`{subst2}` 代入、`{number}` 數字。")
    add("")
    add("## 讀取方式")
    add("")
    add("讀取端只有兩種取條目的方式，全部經過 `fdps_draw_text`：")
    add("")
    add("- **編號加常數**：名稱表以某個欄位值加上該表的起點當條目。加法不檢查上界，也沒有分表：編號超出一張表的尾端就讀到下一張表的開頭。")
    add("- **寫死的條目**：系統訊息由呼叫端直接寫條目編號。")
    add("")
    add("代入碼 `{subst1}`／`{subst2}` 畫的也是本區塊的條目：呼叫端先把「名稱表起點 + 編號」存進 "
        "`data_fdps_dialog_last_action_text_id_param`／`data_fdps_dialog_subst_text_id_2`，再畫含代入碼的訊息，"
        "`fdps_draw_text` 讀到代入碼時以那個值遞迴畫本區塊的一條，顏色固定用標準訊息色。下表的「代入槽」就是這條路徑。")
    add("")
    add("## 語意分區")
    add("")
    add("| 條目 | 內容 | 取條目的方式 |")
    add("| --- | --- | --- |")
    for r in REGIONS:
        if r.base is not None:
            how = f"{r.code_name} + `0x{r.base:x}`"
        elif r.key == "messages":
            how = "寫死的條目，見下方各組"
        elif r.key == "glyph_row":
            how = "沒有讀取端"
        else:
            how = "—"
        add(f"| {_span(r.first, r.last, 3)} | {r.title} | {how} |")
    add("")
    add("章名、章節副標、勝敗條件與城鎮招牌不在本區塊，而在每一章自己的文字區塊開頭，內容由各章頁 "
        "[`chapters/`](../../chapters/_index.md) 記。以寫死條目讀章節區塊前 9 條的地方：")
    add("")
    add("| 章節區塊條目 | 讀取端 |")
    add("| --- | --- |")
    for entry in sorted(header_readers):
        sites = "、".join(f"`{f}`（`src/{n}`）" for f, n in header_readers[entry])
        add(f"| {_hex(entry)} | {sites} |")
    add("")
    add("## `0x000`：字模列")
    add("")
    add("第 0 條是一整列數字與大寫字母。沒有任何讀取端：名稱表的起點都大於 0，系統訊息也沒有寫死 0。"
        f"它是永遠不會顯示的文字，內容由 [`cut_content/`]({CUT_CONTENT}) 收錄。")
    add("")

    unit = REGION_BY_KEY["unit_names"]
    add(f"## {_span(unit.first, unit.last, 3)}：單位名")
    add("")
    add("單位的名稱是本區塊第「角色編號 + 1」條。角色編號就是肖像編號：`fdps_deploy_unit`（`src/deploy.c`）部署時與 "
        "`fdps_roster_add_character`（`src/roster.c`）入隊時，都把同一個值同時寫進單位記錄的 `portrait_id` 與 `char_id`，"
        "所以讀取端取哪一個結果都一樣——敵方單位的名稱因此是「肖像編號 + 1」，也就是 `ENEMYDAT.DAT` 第 r 列"
        "（肖像編號 `0x3C` + r）的名稱在第 `0x3D` + r 條。")
    add("")
    add("本表延伸到 `0x096` 為止，後面緊接種族名。部署記錄用到的肖像編號 `0x97`–`0x9C`"
        "（見 [`assets/enemies.md`](../enemies.md)）照公式落在 `0x098`–`0x09d`，讀到的是種族名「妖鬼」到「其他」。")
    add("")
    add("一個編號的名稱會不會真的出現在畫面上，取決於那個單位有沒有出場；從未出場的單位由 "
        f"[`cut_content/`]({CUT_CONTENT}) 擁有。")
    add("")
    add("讀取端：")
    add("")
    out += _indexed_readers(readers, unit.base)
    add("")

    out += _table_rows(texts, unit.first, unit.last, unit.base, "角色編號")
    add("")

    items = REGION_BY_KEY["item_names"]
    spells = REGION_BY_KEY["spell_names"]
    classes = REGION_BY_KEY["class_names"]
    item_count = items.last - items.first + 1
    item_gap = spells.first - items.last - 1
    item_ff = items.base + 0xff
    for key, note in (
            ("race_names", "種族代碼 0–6 各有名稱，後面三條是空字串。"),
            ("class_names", f"職業代碼 `0x00`–`0x{classes.last - classes.base:02x}` 共 {classes.last - classes.first + 1} 個。"),
            ("item_names", f"物品編號 `0x00`–`0x{item_count - 1:02X}` 共 {item_count} 個，與 `ITEM.DAT` 有內容的範圍相同（[`assets/items.md`](../items.md)）。"
                           f"表尾之後是 {item_gap} 條空字串，再來就是法術名：照公式，物品編號 `0x{item_count:02X}`–`0x{item_count + item_gap - 1:02X}` 的名稱是空字串，"
                           f"`0x{spells.first - items.base:02X}` 起讀到法術名，物品編號 `0xFF` 讀到第 `0x{item_ff:03x}` 條、"
                           f"法術 `0x{item_ff - spells.base:02X}` 的「{texts[item_ff]}」——攻略站把編號 `FF` 的 BUG 物品叫做「{texts[item_ff]}」，來源就是這個越界。"),
            ("spell_names", f"法術編號 `0x00`–`0x{spells.last - spells.base:02x}` 共 {spells.last - spells.first + 1} 個，"
                            "與 `MAGICDAT.DAT` 相同（[`assets/spells.md`](../spells.md)）。")):
        region = REGION_BY_KEY[key]
        add(f"## {_span(region.first, region.last, 3)}：{region.title}")
        add("")
        add(note)
        add("")
        add("讀取端：")
        add("")
        out += _indexed_readers(readers, region.base)
        add("")
        out += _table_rows(texts, region.first, region.last, region.base, region.code_name)
        add("")

    messages = REGION_BY_KEY["messages"]
    add(f"## {_span(messages.first, messages.last, 3)}：系統訊息")
    add("")
    add("每一條都有寫死它的讀取端。`0x224`–`0x229` 的抽獎只在系統日期是 1998 年 1 月 28 日時開"
        f"（`fdps_run_bonus_lottery`，`src/vilbar.c`），大獎是被封住的內容，見 [`cut_content/`]({CUT_CONTENT})。")
    add("")
    by_entry = {}
    for reader in readers:
        for entry in reader.ids or ():
            by_entry.setdefault(entry, []).append(reader)
    for first, last, title in MESSAGE_GROUPS:
        add(f"### {_span(first, last, 3)}：{title}")
        add("")
        add("| 條目 | 原文 | 讀取端 |")
        add("| --- | --- | --- |")
        for entry in range(first, last + 1):
            add(f"| {_hex(entry, 3)} | {_cell(texts[entry])} | {_functions(by_entry.get(entry, []))} |")
        add("")
    return "\n".join(out).rstrip() + "\n"


CALLER_KINDS = {"init": "開場", "end": "勝利", "event": "戰鬥中事件"}


def _script_label(report):
    return "、".join(f"第 {c.chapter} 章{CALLER_KINDS.get(c.kind, c.kind)}，`{c.function}`"
                    for c in report.callers)


def _fence(text):
    body = text.replace("{br}", "\n").replace("{page}", "{page}\n")
    body = re.sub(r"(?<=[^\n])\{speaker", "\n{speaker", body)
    return ["```text", body.rstrip("\n"), "```"]


def render_scene(blocks, reports):
    refs = scene_refs(reports)
    loaders = {}
    for report in reports:
        for map_no in report.trace.switches:
            loaders.setdefault(map_no + 1, set()).add(report.member.removesuffix(".DAT"))
    loaders = {block: sorted(names) for block, names in loaders.items()}
    example = next((block, entry) for block in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1)
                   for _, _, entry in sorted(refs.get(block, []), key=lambda r: r[2])
                   if entry < CHAPTER_HEADER_ENTRIES)
    callers = {r.member.removesuffix(".DAT"): r for r in reports}

    out = []
    add = out.append
    add("# 額外場景的文字：`FDETXT31`–`FDETXT65`")
    add("")
    add("地圖編號 30 以上的場景各有一個文字區塊 `FDETXT(地圖編號 + 1).TXT`，共 35 個。它們不屬於任何一章，"
        "只在過場腳本以 `SWITCH_MAP` 切到該地圖時隨整組地圖資源載入，腳本的 `DRAW_TEXT` 與 `ASK_THREE_WAY` "
        "從中取條目（[`resource_info/cutscene_script.md`](../../resource_info/cutscene_script.md)）。本頁的原文由 "
        "[`tools/global_text/`](../../tools/global_text/_index.md) 以 [`text_decode`](../../tools/text_decode/_index.md) 解出，"
        "每條由哪支腳本顯示取自 [`cutscene_script`](../../tools/cutscene_script/_index.md) 的追蹤。")
    add("")
    add("## 只有腳本會顯示這些區塊")
    add("")
    add("章節索引（也就是決定目前文字區塊的那個值）只有過場腳本的 `SWITCH_MAP` 會寫成 30 以上。腳本以外的寫入者——"
        "章節結束處理函式寫入的下一章（最大 `0x1D`）、標題畫面的新遊戲與示範戰鬥、讀檔——寫的都是 0–29。"
        "勝利腳本結束在過場地圖上時，結束處理函式接著只寫入下一章，不畫字；之後城鎮階段（`fdps_load_field_chapter_resources`）"
        "或下一章的開場（`fdps_chapter_state_reset`）照新的章節索引重新載入文字區塊。存讀檔畫面的章名則另外以存檔格記錄的章節載入。"
        "腳本執行中從目前區塊取字的只有 `DRAW_TEXT` 與 `ASK_THREE_WAY`，所以本頁區塊的一條條目會不會顯示，完全由有沒有腳本在該地圖上引用它決定。")
    add("")
    add("由此：")
    add("")
    add("- 章節區塊開頭放章名、勝敗條件與城鎮招牌的前 9 條，在這裡只是普通條目：有腳本引用就會顯示"
        f"（例如 `FDETXT{example[0]:02d}` 的 {_hex(example[1])}），沒有就不會。")
    add(f"- 沒有腳本引用的條目與沒有任何腳本切過去的區塊不在本頁轉錄：它們是永遠不會顯示的文字，由 [`cut_content/`]({CUT_CONTENT}) 收錄。")
    add("- `{speaker char=n}` 換上肖像編號 n 的頭像與新的對話框、`{speaker unit=n}` 換上目前單位陣列第 n 格的頭像，"
        "都不顯示名字；角色是誰見 [`assets/`](../_index.md)。轉錄時 `{br}` 換成換行，`{speaker …}` 之前也換行。")
    add("")
    add("## 總表")
    add("")
    add("| 區塊 | 地圖 | 條數 | 切過去的腳本 | 顯示的條目 |")
    add("| --- | ---: | ---: | --- | --- |")
    classified = {}
    for block in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1):
        texts = blocks[block]
        shown, unshown, empty = classify_scene_block(texts, refs.get(block, []))
        classified[block] = (shown, unshown, empty)
        scripts = "、".join(f"`{s}`" for s in loaders.get(block, [])) or "（無）"
        shown_text = ", ".join(_span(a, b) for a, b in _runs(sorted(shown))) or "—"
        add(f"| `FDETXT{block:02d}` | {block - 1} | {len(texts)} | {scripts} | {shown_text} |")
    add("")

    never_loaded = [b for b in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1) if b not in loaders]
    nothing_shown = [b for b in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1)
                     if b in loaders and not classified[b][0]]
    add("沒有任何腳本切過去、整個區塊永遠不會載入的："
        + "、".join(f"`FDETXT{b:02d}`（地圖 {b - 1}）" for b in never_loaded)
        + f"。有腳本切過去、但沒有一條被顯示的：" + "、".join(f"`FDETXT{b:02d}`" for b in nothing_shown)
        + f"。這些區塊下文不再列出，見 [`cut_content/`]({CUT_CONTENT})。")
    add("")

    for block in range(FIRST_SCENE_BLOCK, LAST_SCENE_BLOCK + 1):
        texts = blocks[block]
        shown, unshown, empty = classified[block]
        if not shown:
            continue
        add(f"## `FDETXT{block:02d}`：地圖 {block - 1}")
        add("")
        parts = [f"`{s}`（{_script_label(callers[s])}）" for s in loaders[block]]
        add(f"切到地圖 {block - 1} 的腳本：{'、'.join(parts)}。")
        if unshown:
            add("")
            add(f"本區塊另有 {len(unshown)} 條有內容、但沒有腳本引用的條目，永遠不會顯示，見 [`cut_content/`]({CUT_CONTENT})。")
        add("")
        for entry, sites in shown.items():
            where = "、".join(f"`{s}` `0x{offset:03x}`" for s, offset in sites)
            add(f"### {_hex(entry)}（{where}）")
            add("")
            out += _fence(texts[entry])
            add("")
    return "\n".join(out).rstrip() + "\n"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_pages(game_dir):
    blocks = load_blocks(game_dir)
    readers = scan_readers()
    reports = load_scripts(game_dir)
    problems = check_global(blocks[0], readers) + check_scene(blocks, reports)
    if problems:
        raise GateError("\n".join(problems))
    return {
        GLOBAL_PAGE: render_global(blocks[0], readers, scan_chapter_header_readers()),
        SCENE_PAGE: render_scene(blocks, reports),
    }


def cmd_build(args):
    for path, text in build_pages(args.game).items():
        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {path.relative_to(REPO_ROOT)}")
    return 0


def cmd_verify(args):
    stale = []
    for path, text in build_pages(args.game).items():
        on_disk = path.read_text(encoding="utf-8") if path.exists() else None
        if on_disk != text:
            stale.append(str(path.relative_to(REPO_ROOT)))
    if stale:
        print("FAIL: out of date with a rebuild: " + ", ".join(stale))
        return 1
    print("every gate passes and both pages match a rebuild")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("build", "verify"):
        p = sub.add_parser(name)
        p.add_argument("--game", default=DEFAULT_GAME)
    args = parser.parse_args(argv)
    try:
        return cmd_build(args) if args.command == "build" else cmd_verify(args)
    except (ScanError, GateError, text_decode.TextBlockError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
