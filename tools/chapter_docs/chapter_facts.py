"""chapter_facts.py -- the mechanical half of the chapters/chNN.md pages (ticket 25.8).

Every chapter page is prose a person (an agent) writes around blocks this
script generates.  The blocks hold what can be read straight off the shipped
data and the emitted source, so they are never typed by hand:

    header       the chapter's resources, handlers and title strings
    deployments  every MAPnn.DAT deployment record, grouped by wave
    treasure     searchable cells, their records, and death-script drops
    village      the village that comes before the chapter
    events       turn events, cell events and the handler each one calls
    scripts      every cut-scene script the chapter's handlers run, step by step
    dialogue     the chapter's own text block FDETXTnn, entry by entry, with
                 every reader of each entry

Two blocks need a judgement the data cannot make, and take it from the
chapter's judgement record (tools/chapter_docs/judgements/chNN.json once
landed, the draft's .meta.json before that):

    waves        which waves other than 0 are ever deployed, when and by whom
    text_readers readers of text entries the source scan cannot resolve
    never_shown  entries nothing ever displays (owned by cut_content/)

Sources, all imported, never re-implemented (tools/_index.md owners):
    map_decode       MAPnn.DAT / MAPnn.COD / event-code layer
    cutscene_script  the scripts, their callers and traces
    text_decode      FDETXTnn.TXT
    data_tables      names out of FDETXT00, SHOPnn.DAT, CHAPTER_HAS_NO_VILLAGE
    global_text      the src/ scanner for fdps_draw_text and its entry ids
    data_emit        the Ghidra snapshot's name table (function addresses)

Usage:
    python tools/chapter_docs/chapter_facts.py facts <n|all>     write workspace/chapter_docs/facts/
    python tools/chapter_docs/chapter_facts.py block <n> <key>   print one generated block
    python tools/chapter_docs/chapter_facts.py render-maps       annotated map PNGs into workspace/
"""
import argparse
import json
import re
import sys
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
for sub in ("map_decode", "cutscene_script", "text_decode", "data_tables", "global_text",
            "data_emit"):
    sys.path.insert(0, str(TOOLS / sub))

import map_decode  # noqa: E402
import cutscene_script as cs  # noqa: E402
import text_decode  # noqa: E402
import data_tables  # noqa: E402
import global_text  # noqa: E402

GAME = ROOT / "fdps_game_files"
DUMP = ROOT / "workspace" / "vfs_dump"
WS = ROOT / "workspace" / "chapter_docs"
FACTS = WS / "facts"
DRAFTS = WS / "drafts"
JUDGEMENTS = Path(__file__).resolve().parent / "judgements"
SRC = ROOT / "src"

CHAPTERS = range(1, 31)
FIRST_SCENE_MAP = 31
CHAPTER_TEXT_PTRS = ("data_fdps_current_chapter_text_ptr", "chapter_text")
CUT = "../cut_content/_index.md"
CD_DISC_2_FROM_INDEX = 18          # program_info/cd_audio.md: index < 18 needs disc 1
OPENING_WAVE = 0                   # src/deploy.c: fdps_build_map_unit_array deploys wave 0
NEVER_SHOWN_TEXT = "永遠不會顯示"

SIDE = {0: "敵方", 1: "NPC", 2: "我方"}
AI = {0: "0 主動", 1: "1 追擊", 2: "2 原地", 3: "3 追角色", 4: "4 走向目的地",
      5: "5 搶寶箱", 6: "6 不動", 7: "7 撤離", 8: "8 靜止", 9: "9 先追角色",
      10: "10 道具優先", 11: "11 法術優先"}
PHASE = {1: "NPC 階段前", 0: "敵方階段前", 2: "下一個我方階段前"}
TRIGGER = {0: "走進", 1: "停留"}
CELL_KIND = {"chest": "寶箱", "buried": "埋藏"}

# Readers of a chapter block outside the per-chapter handlers, by file:
#   "all"      runs in every chapter (the win/fail window, the save panel's
#              chapter title -- the panel loads the slot's own block)
#   "village"  runs in the village phase only, which reads the block of the
#              chapter the village comes before (src/rsrc.c)
#   "data"     the entry comes out of data this script reads itself (the
#              cut-scene interpreter's DRAW_TEXT / ASK_THREE_WAY via the
#              script traces, the death scripts via MAPnn.DAT), so the code
#              site adds nothing
#   "ending"   the credit roll, run by fdps_chapter_30_end only
GENERIC_READERS = {"btlend.c": "all", "savepnl.c": "all",
                   "vilmenu.c": "village", "vilshop.c": "village", "vilbar.c": "village",
                   "village.c": "village", "icon.c": "data", "death.c": "data",
                   "ending.c": "ending"}
ENDING_CHAPTER = 30


class FactsError(Exception):
    """The data or the source does not hold what this script relies on."""


# ---------------------------------------------------------------------------
# Loading, once per process
# ---------------------------------------------------------------------------

@lru_cache(maxsize=None)
def snapshot_addresses():
    """{function name: address} from the Ghidra snapshot."""
    from check_data import load_original_names
    out = {}
    for addr, name in load_original_names().items():
        out.setdefault(name, addr)
    return out


def cite(name):
    addr = snapshot_addresses().get(name)
    if addr is None:
        raise FactsError(f"{name} is not in the Ghidra snapshot")
    return f"`{name}`（`0x{addr:x}`）"


@lru_cache(maxsize=None)
def handler_tables():
    """The four dispatch tables, read out of src/chapter.c's initializers."""
    text = (SRC / "chapter.c").read_text(encoding="utf-8")
    out = {}
    for key, symbol in (("init", "data_fdps_chapter_init_handler_table"),
                        ("event", "data_fdps_chapter_event_handler_table"),
                        ("post", "data_fdps_chapter_post_action_handler_table"),
                        ("end", "data_fdps_chapter_end_handler_table")):
        m = re.search(symbol + r"\[\d+\][^=]*=\s*\{([^}]*)\}", text)
        if not m:
            raise FactsError(f"src/chapter.c: no initializer for {symbol}")
        out[key] = re.findall(r"fdps_\w+", m.group(1))
    if [len(out[k]) for k in ("init", "event", "post", "end")] != [30, 50, 30, 30]:
        raise FactsError("src/chapter.c: a dispatch table does not have its 30/50 entries")
    return out


TABLE_ADDRESS = {"init": 0x60074, "event": 0x601c4, "post": 0x6028c, "end": 0x60304}


@lru_cache(maxsize=None)
def game():
    return data_tables.load(DUMP)


@lru_cache(maxsize=None)
def glyphs():
    return text_decode.load_glyph_table()


@lru_cache(maxsize=None)
def text_block(number):
    return text_decode.parse_block((DUMP / "FIELD" / f"FDETXT{number:02d}.TXT").read_bytes())


def entry_line(number, index):
    entries = text_block(number)
    return text_decode.render_entry(entries[index], glyphs()) if index < len(entries) else None


@lru_cache(maxsize=None)
def scripts():
    return {r.member: r for r in cs.decode_all(GAME, cs.scan_callers())}


@lru_cache(maxsize=None)
def battle_map(map_no):
    return map_decode.load_map(DUMP, map_no)


@lru_cache(maxsize=None)
def chapter_text_draws():
    """Every fdps_draw_text of the current chapter block in src/*.c.

    [{file, line, function, ids (tuple or None), expression}].  ids is None
    when the entry comes out of a runtime value the scan cannot fold."""
    sources = global_text._read_sources(SRC)
    per_file, shared = global_text._collect_macros(sources)
    out = []
    for name, text in sources.items():
        if not name.endswith(".c"):
            continue
        for pos, _, expression in global_text._draw_calls(text, list(CHAPTER_TEXT_PTRS)):
            resolved = global_text._resolve(expression, per_file[name], shared,
                                            global_text._function_body(text, pos))
            ids = resolved[0] if resolved and resolved[0] is not None else None
            out.append({"file": name, "line": text.count("\n", 0, pos) + 1,
                        "function": global_text._function_at(text, pos),
                        "ids": ids, "expression": expression})
    return out


@lru_cache(maxsize=None)
def deploy_call_sites():
    """Every fdps_deploy_wave call in src/*.c with its wave argument, folded
    to a number where the source spells it with macros (a hint for the
    wave judgement, not a conclusion)."""
    sources = global_text._read_sources(SRC)
    per_file, shared = global_text._collect_macros(sources)
    out = []
    for name, text in sources.items():
        if not name.endswith(".c") or name == "deploy.c":
            continue
        for m in re.finditer(r"\bfdps_deploy_wave\(", text):
            _, pos = global_text._argument(text, m.end())
            wave, _ = global_text._argument(text, pos + 1)
            resolved = global_text._resolve(" ".join(wave.split()), per_file[name], shared,
                                            global_text._function_body(text, m.start()))
            out.append({"file": name, "line": text.count("\n", 0, m.start()) + 1,
                        "function": global_text._function_at(text, m.start()),
                        "wave_expression": " ".join(wave.split()),
                        "wave": list(resolved[0]) if resolved and resolved[0] else None})
    return out


# ---------------------------------------------------------------------------
# Per-chapter facts
# ---------------------------------------------------------------------------

def chapter_title(n):
    return entry_line(n, 1).replace("{br}", "").strip()


def one_line(text):
    return text.replace("{br}", "／").replace("{page}", "").strip("／ ")


def village_before(n):
    """(shop number, text block) of the village played before chapter n, or None."""
    index = n - 1
    if index < 1 or data_tables.no_village(index):
        return None
    return index, n


def event_slots_used(m):
    """{slot: [(where, detail)]} for every event slot map m's data can call."""
    dat = m["dat"]
    used = {}

    def add(slot, where):
        used.setdefault(slot, [])
        if where not in used[slot]:
            used[slot].append(where)

    for t in dat["turn_events"]:
        if (t["turn"], t["handler"]) != (0xFF, 0xFF) and t["handler"] < 50:
            add(t["handler"], "回合事件")
    for c in map_decode.all_cells(m):
        e = c.get("tile_event")
        if e and e["handler"] is not None:
            add(e["handler"], "格子事件")
        if c["kind"] in map_decode.SEARCHABLE and c.get("record") and c["record"]["kind"] >= 2:
            add(c["record"]["payload"], "搜尋")
    for s in dat["spawns"]:
        if s["death_op"] == 2:
            add(s["death_arg"], "死亡腳本")
    return dict(sorted(used.items()))


def chapter_scripts(n):
    """The scripts run by chapter n's handlers, opening first, then victory,
    then in-battle events, each group in member order."""
    order = {"init": 0, "end": 1, "event": 2}
    found = []
    for member, r in scripts().items():
        mine = [c for c in r.callers if c.chapter == n]
        if mine:
            found.append((min(order[c.kind] for c in mine), member, r, mine))
    found.sort(key=lambda x: (x[0], x[1]))
    return [(member, r, mine) for _, member, r, mine in found]


def chapter_functions(n):
    """Every snapshot function named for chapter n."""
    prefix = f"fdps_chapter_{n:02d}_"
    return sorted((addr, name) for name, addr in snapshot_addresses().items()
                  if name.startswith(prefix))


def draw_runs_in(draw, n):
    """Whether a chapter-text draw site can run while chapter n's block is
    loaded, and so read it: True, False, or "data" when the entry comes out of
    data this script reads directly (scripts, death scripts)."""
    m = re.match(r"fdps_chapter_(\d\d)_", draw["function"])
    if m:
        return int(m.group(1)) == n
    users = slot_users().get(draw["function"])
    if users is not None:
        return n in users
    scope = GENERIC_READERS.get(draw["file"])
    if scope == "all":
        return True
    if scope == "village":
        return village_before(n) is not None
    if scope == "ending":
        return n == ENDING_CHAPTER
    if scope == "data":
        return "data"
    raise FactsError(f"{draw['file']}:{draw['line']}: {draw['function']} draws a chapter "
                     "text entry and no rule says which chapters it runs in")


@lru_cache(maxsize=None)
def slot_users():
    """{event handler name: {chapters whose map data calls it}}."""
    table = handler_tables()["event"]
    out = {}
    for n in CHAPTERS:
        for slot in event_slots_used(battle_map(n - 1)):
            if slot < len(table):
                out.setdefault(table[slot], set()).add(n)
    return out


def text_readers(n):
    """{entry: [reader label]} for every reader of FDETXTnn the scan finds.

    Readers are: cut-scene DRAW_TEXT / ASK_THREE_WAY steps running on map
    n - 1 (any script), chapter handlers drawing a fixed entry, the fixed-entry
    readers of the block's header (save panel, win/fail window, village), and
    death scripts of MAP(n-1) that draw a text entry."""
    readers = {}

    def add(entry, label):
        readers.setdefault(entry, [])
        if label not in readers[entry]:
            readers[entry].append(label)

    for member, r in sorted(scripts().items()):
        for traced in r.trace.steps:
            ctx = traced.context
            refs = [ctx["text"]] if "text" in ctx else list(ctx.get("texts", []))
            for ref in refs:
                if ref and ref["block"] == n:
                    add(ref["entry"], f"`{member}` `0x{traced.step.offset:03x}`")
    for draw in chapter_text_draws():
        if draw["ids"] is None or draw_runs_in(draw, n) is not True:
            continue
        for entry in draw["ids"]:
            add(entry, cite(draw["function"]))
    m = battle_map(n - 1)
    for s in m["dat"]["spawns"]:
        if 3 <= s["death_op"] < 0xFF and s["death_arg"] not in (0xFF, -1):
            add(s["death_arg"], f"`MAP{n - 1:02d}.DAT` 記錄 #{s['index']} 的死亡腳本")
    return {k: readers[k] for k in sorted(readers)}


def unresolved_draws(n):
    """Chapter-text draws whose entry the scan cannot fold, in functions that
    can run in chapter n -- the leads for the text_readers judgement."""
    return [draw for draw in chapter_text_draws()
            if draw["ids"] is None and draw_runs_in(draw, n) is True]


def wave_list(m):
    return sorted({s["wave"] for s in m["dat"]["spawns"]})


def facts(n):
    """Everything the chapter's agent and the generated blocks need, as JSON."""
    if n not in CHAPTERS:
        raise FactsError(f"no chapter {n}")
    map_no = n - 1
    m = battle_map(map_no)
    tables = handler_tables()
    g = game()
    width, height = map_decode.grid_size(m)
    entries = text_block(n)
    readers = text_readers(n)
    return {
        "chapter": n, "chapter_index": map_no, "map": map_no,
        "title": chapter_title(n),
        "header_strings": {f"0x{i:02x}": entry_line(n, i) for i in range(9)},
        "handlers": {k: tables[k][map_no] for k in ("init", "end", "post")},
        "chapter_functions": [{"name": name, "address": f"0x{addr:x}"}
                              for addr, name in chapter_functions(n)],
        "event_slots": {str(slot): {"handler": tables["event"][slot], "used_by": where}
                        for slot, where in event_slots_used(m).items()},
        "map_size": f"{width}×{height}",
        "player_slots": m["dat"]["player_slots"],
        "waves": {str(w): [s["index"] for s in m["dat"]["spawns"] if s["wave"] == w]
                  for w in wave_list(m)},
        "deploy_call_sites": [d for d in deploy_call_sites()
                              if re.match(rf"fdps_chapter_{n:02d}_", d["function"])],
        "script_deploys": [{"script": member, "offset": f"0x{t.step.offset:03x}",
                            "map": t.context.get("map"), "wave": t.step.operands.get("wave")}
                           for member, r in sorted(scripts().items())
                           for t in r.trace.steps if t.step.mnemonic == "DEPLOY_WAVE"
                           and t.context.get("map") == map_no],
        "scripts": [{"member": member, "callers": [vars(c) for c in mine],
                     "switches": r.trace.switches, "final_map": r.trace.final_map}
                    for member, r, mine in chapter_scripts(n)],
        "text_block": f"FDETXT{n:02d}.TXT", "text_entries": len(entries),
        "text_readers_found": {f"0x{k:02x}": v for k, v in readers.items()},
        "text_entries_without_reader": [f"0x{i:02x}" for i in range(len(entries))
                                        if i not in readers and entry_line(n, i)],
        "unresolved_draws": unresolved_draws(n),
        "village_before": village_before(n),
        "_note": "generated by tools/chapter_docs/chapter_facts.py; a lead list, not the page",
    }


# ---------------------------------------------------------------------------
# Judgements
# ---------------------------------------------------------------------------

def load_judgement(n, draft=False):
    """The chapter's judgement record: the landed one, or the draft's meta."""
    path = (DRAFTS / f"ch{n:02d}.meta.json") if draft else (JUDGEMENTS / f"ch{n:02d}.json")
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


JUDGEMENT_KEYS = ("waves", "text_readers", "never_shown")


def judgement_subset(meta):
    """What the generated blocks read from a judgement, and nothing else."""
    return {k: meta.get(k, []) for k in JUDGEMENT_KEYS}


def validate_judgement(n, meta):
    """Problems with a judgement against chapter n's data (empty when usable)."""
    problems = []
    m = battle_map(n - 1)
    need = [w for w in wave_list(m) if w != OPENING_WAVE]
    got = {}
    for w in meta.get("waves", []):
        if not isinstance(w, dict) or "wave" not in w or "deployed" not in w:
            problems.append(f"waves: malformed entry {w!r}")
            continue
        got[w["wave"]] = w
        if w["deployed"] and not str(w.get("when", "")).strip():
            problems.append(f"waves: wave {w['wave']} is deployed but says nothing of when")
    for w in need:
        if w not in got:
            problems.append(f"waves: wave {w} has records but no judgement")
    for w in got:
        if w not in need:
            problems.append(f"waves: wave {w} has no deployment record (or is wave 0)")
    readers = text_readers(n)
    count = len(text_block(n))
    missing = {i for i in range(count) if i not in readers and entry_line(n, i)}
    covered = set()
    for key in ("text_readers", "never_shown"):
        for item in meta.get(key, []):
            try:
                entry = int(str(item["entry"]), 16)
            except (KeyError, TypeError, ValueError):
                problems.append(f"{key}: malformed entry {item!r}")
                continue
            if entry not in missing:
                problems.append(f"{key}: entry 0x{entry:02x} is not one the scan left without a "
                                f"reader (found readers, empty, or out of range)")
            if entry in covered:
                problems.append(f"{key}: entry 0x{entry:02x} is judged twice")
            covered.add(entry)
            text = item.get("reader" if key == "text_readers" else "why", "")
            if not str(text).strip():
                problems.append(f"{key}: entry 0x{entry:02x} gives no "
                                f"{'reader' if key == 'text_readers' else 'reason'}")
    for entry in sorted(missing - covered):
        problems.append(f"text entry 0x{entry:02x} has no scanned reader and is neither in "
                        f"text_readers nor in never_shown")
    return problems


# ---------------------------------------------------------------------------
# Generated blocks
# ---------------------------------------------------------------------------

def _cell(text):
    return str(text).replace("|", "｜").replace("\n", " ")


def _hex(v):
    return f"`{v:02X}`"


def unit_name(char_id):
    return game().unit_label(char_id)


def block_header(n, judgement):
    map_no = n - 1
    tables = handler_tables()
    m = battle_map(map_no)
    width, height = map_decode.grid_size(m)
    slots = event_slots_used(m)
    lines = ["| 項目 | 內容 |", "| --- | --- |",
             f"| 章號／章節索引 | {n}／{map_no} |",
             f"| 章名（文字區塊 `0x01`） | {_cell(chapter_title(n))} |",
             f"| 勝利條件（`0x02`） | {_cell(one_line(entry_line(n, 2)))} |",
             f"| 敗北條件（`0x03`） | {_cell(one_line(entry_line(n, 3)))} |",
             f"| 戰場 | `MAP{map_no:02d}.DAT`／`MAP{map_no:02d}.COD`／`M{map_no:02d}.DTL`，"
             f"{width}×{height} 格，我方 slot {m['dat']['player_slots']} 個，"
             f"部署記錄 {m['dat']['spawn_count']} 筆 |",
             f"| 文字區塊 | `FDETXT{n:02d}.TXT`，{len(text_block(n))} 條 |",
             f"| 進入處理（`0x{TABLE_ADDRESS['init']:x}`[{map_no}]） | {cite(tables['init'][map_no])} |",
             f"| 行動後檢查（`0x{TABLE_ADDRESS['post']:x}`[{map_no}]） | {cite(tables['post'][map_no])} |",
             f"| 勝利處理（`0x{TABLE_ADDRESS['end']:x}`[{map_no}]） | {cite(tables['end'][map_no])} |"]
    if slots:
        rows = "、".join(f"slot {s}：{cite(tables['event'][s])}（{'、'.join(w)}）"
                         for s, w in slots.items())
    else:
        rows = "無"
    lines.append(f"| 地圖資料呼叫的事件處理（`0x{TABLE_ADDRESS['event']:x}`） | {rows} |")
    kinds = {"init": "開場", "end": "勝利", "event": "戰鬥中事件"}
    scripts_cell = "、".join(
        f"`{member}`（{'／'.join(sorted({kinds[c.kind] for c in mine}))}）"
        for member, _, mine in chapter_scripts(n)) or "無"
    lines.append(f"| 過場腳本 | {scripts_cell} |")
    village = village_before(n)
    lines.append("| 本章之前的村莊 | " + (f"`SHOP{village[0]:02d}.DAT`" if village else "無") + " |")
    disc = 1 if map_no < CD_DISC_2_FROM_INDEX else 2
    lines.append(f"| 光碟 | 第 {disc} 片（[CD 音軌](../program_info/cd_audio.md)） |")
    return "\n".join(lines)


def _wave_judgement(judgement, wave):
    if wave == OPENING_WAVE:
        return {"wave": 0, "deployed": True,
                "when": f"進入戰場時由 {cite('fdps_build_map_unit_array')} 部署在錨點上"}
    for w in (judgement or {}).get("waves", []):
        if w.get("wave") == wave:
            return w
    return None


def _ai_cell(s, char_names):
    ai = s["ai"] & 0x0F
    text = AI.get(ai, f"{ai} 不動")
    if s["ai"] & 0xF0:
        text += f"（高位 `{s['ai'] & 0xF0:02X}`）"
    if ai in (3, 9):
        text += f"：{_hex(s['dest'][0])} {char_names(s['dest'][0])}"
    elif ai in (4, 7):
        text += f"：({s['dest'][0]}, {s['dest'][1]})"
    elif ai == 5:
        text += f"：碼 {s['cell_code']}"
    return text


def death_cell(n, s):
    op, arg = s["death_op"], s["death_arg"]
    g = game()
    if op == 0xFF:
        return "—"
    if op == 0:
        return f"掉落 {_hex(arg)} {g.item_name(arg)}"
    if op == 1:
        return f"掉落 {arg} 金"
    if op == 2:
        table = handler_tables()["event"]
        return f"事件 slot {arg}：{cite(table[arg])}" if 0 <= arg < len(table) else f"事件 slot {arg}"
    words = "不畫文字" if arg in (0xFF, -1) else f"顯示 `0x{arg:02x}`"
    if op == 4:
        return words + "，之後過關"
    if op == 5:
        return words + "，之後敗北"
    return words


def block_deployments(n, judgement):
    map_no = n - 1
    m = battle_map(map_no)
    dat, cod = m["dat"], m["cod"]
    anchors = cod[:dat["spawn_count"]] if cod else []
    party = cod[dat["spawn_count"]:dat["spawn_count"] + dat["player_slots"]] if cod else []
    lines = ["欄位的意義見 [`resource_info/map.md`](../resource_info/map.md)：陣營、AI 行為"
             "（低 4 bit）、死亡腳本；錨點是 `MAPnn.COD` 的座標，開場波次放在錨點上，其餘波次依部署"
             "方式放在錨點或最近的空格。單位名是全域文字的「角色編號 + 1」條。", ""]
    if party:
        lines.append("我方起始格：" + "、".join(f"slot {i} ({x}, {y})"
                                           for i, (_, x, y) in enumerate(party)) + "。")
        lines.append("")
    # summary
    lines += ["| 波次 | 陣營 | 單位 | 等級 | 數量 |", "| ---: | --- | --- | --- | ---: |"]
    for w in wave_list(m):
        verdict = _wave_judgement(judgement, w)
        if verdict is not None and not verdict["deployed"]:
            continue
        groups = {}
        for s in dat["spawns"]:
            if s["wave"] == w:
                key = (s["side"], s["char_id"], s["level"])
                groups[key] = groups.get(key, 0) + 1
        for (side, char_id, level), count in groups.items():
            lines.append(f"| {'FF' if w == 0xFF else w} | {SIDE.get(side, side)} | "
                         f"{_hex(char_id)} {_cell(unit_name(char_id))} | {level} | {count} |")
    for w in wave_list(m):
        records = [s for s in dat["spawns"] if s["wave"] == w]
        verdict = _wave_judgement(judgement, w)
        label = "FF" if w == 0xFF else str(w)
        lines += ["", f"### 波次 {label}", ""]
        if verdict is None:
            lines.append("（未判定）")
            continue
        if not verdict["deployed"]:
            why = f"：{verdict['why']}" if verdict.get("why") else ""
            lines.append(f"{len(records)} 筆記錄（#{records[0]['index']}–#{records[-1]['index']}）"
                         f"永遠不會部署{why}，見 [刪減與未用]({CUT})。")
            continue
        lines.append(f"出場：{verdict['when']}")
        lines += ["", "| # | 陣營 | 單位 | 等級 | AI 行為 | 錨點 | 死亡腳本 |",
                  "| ---: | --- | --- | ---: | --- | --- | --- |"]
        for s in records:
            anchor = anchors[s["index"]] if s["index"] < len(anchors) else None
            lines.append(
                f"| {s['index']} | {SIDE.get(s['side'], s['side'])} | {_hex(s['char_id'])} "
                f"{_cell(unit_name(s['char_id']))} | {s['level']} | "
                f"{_cell(_ai_cell(s, unit_name))} | "
                f"{f'({anchor[1]}, {anchor[2]})' if anchor else '—'} | {_cell(death_cell(n, s))} |")
    return "\n".join(lines)


def _record_text(rec):
    g = game()
    if rec["kind"] == 0:
        return f"{_hex(rec['payload'])} {g.item_name(rec['payload'])}"
    if rec["kind"] == 1:
        return "什麼都沒有（0 金）" if rec["payload"] == 0 else f"{rec['payload']} 金"
    table = handler_tables()["event"]
    slot = rec["payload"]
    return (f"事件 slot {slot}：{cite(table[slot])}" if 0 <= slot < len(table)
            else f"事件 slot {slot}")


def block_treasure(n, judgement):
    map_no = n - 1
    m = battle_map(map_no)
    cells = map_decode.searchable_cells(m)
    by_code = {}
    for c in cells:
        by_code.setdefault(c["code"], []).append(c)
    lines = []
    if by_code:
        lines += ["搜尋的規則（同碼的格共用一筆記錄與一個旗標、背包滿時的交換）見 "
                  "[`resource_info/map.md`](../resource_info/map.md)。", "",
                  "| 事件碼 | 格 | 內容 |", "| ---: | --- | --- |"]
        for code in sorted(by_code):
            group = by_code[code]
            where = "、".join(f"({c['x']}, {c['y']}) {CELL_KIND[c['kind']]}" for c in group)
            content = _record_text(group[0]["record"])
            if len(group) > 1:
                content += f"（{len(group)} 格共用，只拿得到一次）"
            lines.append(f"| {code} | {where} | {_cell(content)} |")
    else:
        lines.append("本章地圖沒有寶箱或埋藏格。")
    referenced = set(by_code)
    unref = [(i, r) for i, r in enumerate(m["dat"]["search"])
             if i not in referenced and (r["kind"], r["payload"]) != (0, 0)]
    if unref:
        lines += ["", "沒有任何格引用、拿不到的記錄：" + "、".join(
            f"記錄 {i}（種類 {r['kind']}、內容 `{r['payload'] & 0xFFFF:X}`）" for i, r in unref)
            + f"，見 [刪減與未用]({CUT})。"]
    drops = [s for s in m["dat"]["spawns"] if s["death_op"] in (0, 1)]
    lines += [""]
    if drops:
        lines += ["擊倒後掉落（死亡腳本 opcode 0／1，擊殺者須是存活的我方單位）：", "",
                  "| # | 單位 | 波次 | 掉落 |", "| ---: | --- | ---: | --- |"]
        for s in drops:
            verdict = _wave_judgement(judgement, s["wave"])
            note = "" if verdict is None or verdict["deployed"] else "（所在波次不出場）"
            lines.append(f"| {s['index']} | {_hex(s['char_id'])} {_cell(unit_name(s['char_id']))} | "
                         f"{'FF' if s['wave'] == 0xFF else s['wave']} | "
                         f"{_cell(death_cell(n, s))}{note} |")
    else:
        lines.append("本章沒有擊倒掉落。")
    return "\n".join(lines)


def block_village(n, judgement):
    village = village_before(n)
    if n == 1:
        return "新遊戲直接進入本章，本章之前沒有村莊。"
    if village is None:
        return (f"第 {n - 1} 章勝利後沒有村莊（`CHAPTER_HAS_NO_VILLAGE`，`src/village.c`），"
                "直接進存檔畫面再進本章。")
    shop, block = village
    return (f"第 {n - 1} 章勝利後、本章之前的村莊載入 `SHOP{shop:02d}.DAT`，道具店、武器店、"
            f"秘密商店的貨見 [`assets/shops.md`](../assets/shops.md)。村莊載入的文字區塊是 "
            f"`FDETXT{block:02d}`（章節索引 + 1，{cite('fdps_load_field_chapter_resources')}），"
            f"也就是本章的區塊，所以村莊畫面的文字是本章區塊的 `0x04`–`0x08`，全文在下方「對話」。")


def block_events(n, judgement):
    map_no = n - 1
    m = battle_map(map_no)
    dat = m["dat"]
    table = handler_tables()["event"]
    lines = ["觸發時機與陣營階段的意義見 [`resource_info/map.md`](../resource_info/map.md)。", ""]
    turns = [t for t in dat["turn_events"] if (t["turn"], t["handler"]) != (0xFF, 0xFF)]
    if turns:
        lines += ["| 回合 | 陣營階段 | slot | 處理函式 |", "| ---: | --- | ---: | --- |"]
        for t in turns:
            phase = PHASE.get(t["side"], f"值 {t['side']}")
            if t["turn"] == 0xFF:
                phase += "（填充筆，回合計數走到 255 才比對成立）"
            elif t["side"] == 2:
                phase += f"（回合計數 {t['turn']}）"
                if t["turn"] == 1:
                    phase += "，永遠不觸發"
            handler = cite(table[t["handler"]]) if t["handler"] < len(table) else "—"
            lines.append(f"| {t['turn']} | {phase} | {t['handler']} | {handler} |")
    else:
        lines.append("本章沒有回合事件。")
    lines.append("")
    cells = {}
    for c in map_decode.all_cells(m):
        e = c.get("tile_event")
        if e and e["handler"] is not None:
            cells.setdefault(c["code"], []).append(c)
    if cells:
        lines += ["| 事件碼 | 格 | 觸發 | slot | 處理函式 |", "| ---: | --- | --- | ---: | --- |"]
        for code in sorted(cells):
            e = cells[code][0]["tile_event"]
            where = "、".join(f"({c['x']}, {c['y']})" for c in cells[code])
            handler = cite(table[e["handler"]]) if e["handler"] < len(table) else "—"
            lines.append(f"| {code} | {where} | {TRIGGER.get(e['trigger'], e['trigger'])} | "
                         f"{e['handler']} | {handler} |")
    else:
        lines.append("本章沒有格子事件。")
    deaths = [s for s in dat["spawns"] if 2 <= s["death_op"] < 0xFF]
    if deaths:
        lines += ["", "死亡腳本裡的事件與文字：", "", "| # | 單位 | 波次 | 死亡腳本 |",
                  "| ---: | --- | ---: | --- |"]
        for s in deaths:
            lines.append(f"| {s['index']} | {_hex(s['char_id'])} {_cell(unit_name(s['char_id']))} | "
                         f"{'FF' if s['wave'] == 0xFF else s['wave']} | {_cell(death_cell(n, s))} |")
    return "\n".join(lines)


def _scene_text(n):
    def render(block, entry):
        if block == n:
            return None      # the chapter's own block is transcribed below
        return entry_line(block, entry)
    return render


def block_scripts(n, judgement):
    found = chapter_scripts(n)
    if not found:
        return "本章的處理函式不播放過場腳本。"
    kinds = {"init": "開場", "end": "勝利", "event": "戰鬥中事件"}
    lines = ["格式與每個指令的意義見 [`resource_info/cutscene_script.md`](../resource_info/"
             f"cutscene_script.md)。`FDETXT{n:02d}#0xNN` 是本章文字區塊的條目，全文在下方「對話」；"
             "切到額外場景之後顯示的文字直接列出（那些區塊的正典是 "
             "[`assets/text/scene_text.md`](../assets/text/scene_text.md)）。"
             "`u3=我方1` 是地圖單位索引 3 對到我方 slot 1，`u12=MAP02#9` 是 `MAP02.DAT` 第 9 筆部署記錄。"]
    for member, r, mine in found:
        lines += ["", f"### `{member}`", ""]
        callers = []
        for c in mine:
            label = f"{kinds[c.kind]}：{cite(c.function)}"
            if label not in callers:
                callers.append(label)
        lines.append("- " + "；".join(callers))
        if member in cs.CHAINED_AFTER:
            lines.append(f"- 接在 `{cs.CHAINED_AFTER[member]}` 之後播放，起始地圖沿用它的結束地圖")
        switches = " → ".join(str(x) for x in r.trace.switches) or "無"
        lines.append(f"- {r.size} byte、{len(r.steps)} 步；起始地圖 {r.initial_map}，切換地圖 "
                     f"{switches}，結束於地圖 {r.trace.final_map}")
        for finding in r.trace.findings:
            lines.append(f"- 越界：{_cell(finding)}")
        lines += ["", "| 偏移 | 指令 | 內容 |", "| ---: | --- | --- |"]
        render = _scene_text(n)
        for traced in r.trace.steps:
            lines.append(f"| `{traced.step.offset:04x}` | `{traced.step.mnemonic}` | "
                         f"{_cell(cs.describe(traced, render))} |")
    return "\n".join(lines)


def transcript(n, index):
    """One entry as lines: speakers as 【name】, a line per {br}, ▼ per {page}."""
    entry = text_block(n)[index]
    table = glyphs()
    out, line = [], ""
    for t in entry.tokens:
        if t.kind == "glyph":
            line += text_decode.render_token(t, table)
        elif t.kind == "line_break":
            out.append(line)
            line = ""
        elif t.kind == "page_break":
            out.append(line)
            out.append("▼")
            line = ""
        elif t.kind == "speaker_char":
            if line:
                out.append(line)
            out.append(f"【{unit_name(t.operand)}】（角色 {t.operand}）")
            line = ""
        elif t.kind == "speaker_unit":
            if line:
                out.append(line)
            out.append(f"【地圖單位 {t.operand}】")
            line = ""
        else:
            line += text_decode.render_token(t, table)
    if line:
        out.append(line)
    while out and out[-1] == "":
        out.pop()
    return out


def block_dialogue(n, judgement):
    readers = text_readers(n)
    judged_readers = {int(str(x["entry"]), 16): x["reader"]
                      for x in (judgement or {}).get("text_readers", [])}
    never = {int(str(x["entry"]), 16): x.get("why", "")
             for x in (judgement or {}).get("never_shown", [])}
    count = len(text_block(n))
    lines = [f"`FDETXT{n:02d}.TXT` 全部 {count} 條，依條目編號排列。每條先列讀取端（顯示它的腳本步驟、"
             "處理函式或死亡腳本），再列全文：【名稱】是換說話者（頭像取自括號裡的角色編號；"
             "【地圖單位 n】取執行當下第 n 個地圖單位），每一行是一次換行，▼ 是換頁（等按鍵）。"
             "`{subst1}`、`{subst2}`、`{number}` 是執行期代入的名稱與數字"
             "（[`resource_info/text.md`](../resource_info/text.md)）。"]
    empty = [i for i in range(count) if not entry_line(n, i) and i not in readers]
    if empty:
        lines += ["", "空字串的條目：" + "、".join(f"`0x{i:02x}`" for i in empty) + "。"]
    for i in range(count):
        text = entry_line(n, i)
        if not text and i not in readers:
            continue
        found = list(readers.get(i, []))
        if i in judged_readers:
            found.append(judged_readers[i])
        lines += ["", f"### `0x{i:02x}`", ""]
        if not found:
            if i in never:
                why = f"：{never[i]}" if never[i] else ""
                lines.append(f"{NEVER_SHOWN_TEXT}{why}，全文見 [刪減與未用]({CUT})。")
            else:
                lines.append("（讀取端未判定）")
            continue
        lines.append("讀取端：" + "；".join(found))
        body = transcript(n, i)
        lines += ["", "```text"] + (body or ["（空字串）"]) + ["```"]
    return "\n".join(lines)


BLOCKS = {
    "header": block_header,
    "deployments": block_deployments,
    "treasure": block_treasure,
    "village": block_village,
    "events": block_events,
    "scripts": block_scripts,
    "dialogue": block_dialogue,
}


def render_block(n, key, judgement):
    return BLOCKS[key](n, judgement)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def _chapters(arg):
    return list(CHAPTERS) if arg == "all" else [int(arg)]


def cmd_facts(arg):
    FACTS.mkdir(parents=True, exist_ok=True)
    for n in _chapters(arg):
        data = facts(n)
        (FACTS / f"ch{n:02d}.json").write_text(json.dumps(data, ensure_ascii=False, indent=2),
                                               encoding="utf-8")
        preview = [f"# chapter {n} generated blocks (preview, no judgement applied)"]
        for key in BLOCKS:
            preview += ["", f"<!-- {key} -->", render_block(n, key, None)]
        (FACTS / f"ch{n:02d}.blocks.md").write_text("\n".join(preview) + "\n", encoding="utf-8")
        print(f"ch{n:02d}: {FACTS / f'ch{n:02d}.json'}")
    return 0


def cmd_render_maps():
    out = WS / "maps"
    palette = map_decode.load_palette(DUMP)
    for n in CHAPTERS:
        paths = map_decode.render_map(battle_map(n - 1), palette, out / f"ch{n:02d}.png",
                                      annotate=True)
        print(" ".join(str(p) for p in paths))
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("facts")
    p.add_argument("chapter")
    p = sub.add_parser("block")
    p.add_argument("chapter", type=int)
    p.add_argument("key", choices=sorted(BLOCKS))
    p.add_argument("--draft", action="store_true", help="apply the draft's meta judgement")
    sub.add_parser("render-maps")
    a = ap.parse_args(argv)
    if a.cmd == "facts":
        return cmd_facts(a.chapter)
    if a.cmd == "block":
        judgement = load_judgement(a.chapter, draft=a.draft)
        print(render_block(a.chapter, a.key, judgement))
        return 0
    return cmd_render_maps()


if __name__ == "__main__":
    sys.exit(main())
