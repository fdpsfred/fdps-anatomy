"""Decode the IconAni cut-scene scripts (ICONANI.VFS *.DAT) into step lists.

A cut-scene script is a byte stream: an opcode byte followed by that opcode's
operand bytes, run by fdps_icon_script_run (src/icon.c, 0x21650).  Opcode 0
and every value the interpreter does not recognise end the script.  The
lengths and operand meanings below are the interpreter's own; the resource
format document is resource_info/cutscene_script.md.

Decoding a script alone gives the instructions.  Tracing it adds what the
instructions refer to, which depends on state the interpreter reads from
globals rather than from the script:

  * DRAW_TEXT n draws entry n of the text block that is loaded, and that is
    FDETXT(map + 1).TXT for whatever map is current -- SWITCH_MAP m reloads
    the whole map, text block included (fdps_chapter_state_reset ->
    fdps_build_map_unit_array -> fdps_field_load_chapter_resources).
  * DEPLOY_WAVE deploys from the current map's MAPnn.DAT.
  * A unit operand is an index into the map unit array: the map's player
    slots first, then its wave-0 records in record order, then each later
    deployment appended in record order (resource_info/map.md).  After a
    chapter start or a SWITCH_MAP that array is known exactly; a script run
    at the end of a battle starts on whatever the battle left.

Library use (for other tools):
    reports = decode_all(game_dir, scan_callers())
    report.member, report.callers, report.initial_map, report.steps,
    report.trace.steps[i].context, report.trace.maps_visited,
    report.trace.switches, report.trace.final_map, report.trace.final_units,
    report.trace.text_refs, report.trace.problems, report.trace.findings

Text is shown as a (block, entry) reference.  To show the words, pass a
renderer: a callable (block_no, entry_no) -> str or None.  On the command
line --text uses tools/text_decode (text_decode_renderer), and
--text-renderer path/to/module.py:function plugs in any other.

Usage:
    python cutscene_script.py show   <game dir> <member> [--text | --text-renderer M:F]
    python cutscene_script.py dump   <game dir> <out dir> [--text | --text-renderer M:F]
    python cutscene_script.py verify <game dir>
    python cutscene_script.py report <game dir>
"""

import importlib.util
import json
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1]
REPO_DIR = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR / "vfs_dump"))

from vfs_dump import parse_container  # noqa: E402  (the container's owner)

SCRIPT_ARCHIVE = "ICONANI.VFS"
FIELD_ARCHIVE = "FIELD.VFS"

# MAPnn.DAT: player slot count at +1, deployment record count at +2, records
# of 0x1a bytes from +0x83 with the character id at +1 and the wave at +0x15.
MAP_PLAYER_SLOTS_OFFSET = 0x01
MAP_RECORD_COUNT_OFFSET = 0x02
MAP_RECORD_BASE = 0x83
MAP_RECORD_SIZE = 0x1A
MAP_RECORD_CHAR_ID = 0x01
MAP_RECORD_WAVE = 0x15

# Opcode 0x63's six lines, entries 0x10..0x15 of the current text block.
CHOICE_TEXT_ENTRIES = list(range(0x10, 0x16))
# Opcode 0x61 always works on this battle unit.
XP_AWARD_UNIT = 3


class ScriptError(Exception):
    """The byte stream is not a well-formed script."""


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------

@dataclass
class Step:
    offset: int
    opcode: int
    mnemonic: str
    length: int
    raw: bytes
    operands: dict

    def as_json(self):
        return {"offset": self.offset, "opcode": self.opcode,
                "mnemonic": self.mnemonic, "bytes": self.raw.hex(" "),
                "operands": self.operands}


def _signed(byte):
    return byte - 0x100 if byte > 0x7F else byte


def _unit_pairs(data, start, count):
    return [{"unit": data[start + 2 * i], "facing": data[start + 2 * i + 1]}
            for i in range(count)]


# Each entry: mnemonic, length(data, offset) and operands(data, offset).  The
# length functions only read bytes that are already known to exist: the fixed
# header of a variable-length opcode is checked before its count is read.
def _fixed(n):
    return lambda data, o: n


OPCODES = {
    0x01: ("WALK_UNITS", 4, lambda d, o: 4 + 2 * d[o + 3],
           lambda d, o: {"frames_per_sub_step": d[o + 1], "tiles": d[o + 2],
                         "units": _unit_pairs(d, o + 4, d[o + 3])}),
    0x02: ("FACE_UNITS", 3, lambda d, o: 3 + 2 * d[o + 2],
           lambda d, o: {"hold_frames": d[o + 1],
                         "units": _unit_pairs(d, o + 3, d[o + 2])}),
    0x03: ("DRAW_TEXT", 2, _fixed(2), lambda d, o: {"entry": d[o + 1]}),
    0x04: ("DEPLOY_WAVE", 3, _fixed(3),
           lambda d, o: {"wave": "choice" if d[o + 1] == 0xFF else d[o + 1],
                         "place_exact": d[o + 2]}),
    0x05: ("SCROLL_VIEW", 3, _fixed(3), lambda d, o: {"x": d[o + 1], "y": d[o + 2]}),
    0x06: ("PLAY_SAF", 2, _fixed(2), lambda d, o: {"saf": d[o + 1]}),
    0x07: ("SET_MUSIC", 2, _fixed(2),
           lambda d, o: {"music": "silence"} if d[o + 1] == 0xFF
           else {"music": d[o + 1], "cd_track": d[o + 1] + 1}),
    0x08: ("PLAY_WAV", 2, _fixed(2), lambda d, o: {"wav": d[o + 1]}),
    0x09: ("BLINK_UNITS_OUT", 2, lambda d, o: 2 + d[o + 1],
           lambda d, o: {"units": list(d[o + 2:o + 2 + d[o + 1]])}),
    0x0A: ("PLACE_UNIT", 5, _fixed(5),
           lambda d, o: {"unit": d[o + 1], "x": d[o + 2], "y": d[o + 3],
                         "facing": d[o + 4]}),
    0x0B: ("RETIRE_UNIT", 2, _fixed(2), lambda d, o: {"unit": d[o + 1]}),
    0x0C: ("REVIVE_UNIT", 2, _fixed(2), lambda d, o: {"unit": d[o + 1]}),
    0x0D: ("SET_VIEW_TILE", 3, _fixed(3), lambda d, o: {"x": d[o + 1], "y": d[o + 2]}),
    0x0E: ("FADE_OUT", 2, _fixed(2), lambda d, o: {"step_delay_ms": d[o + 1]}),
    0x0F: ("FADE_IN", 2, _fixed(2), lambda d, o: {"step_delay_ms": d[o + 1]}),
    0x10: ("SHAKE_VIEW", 3, lambda d, o: 3 + 2 * d[o + 2],
           lambda d, o: {"frames_per_step": d[o + 1],
                         "offsets": [[_signed(d[o + 3 + 2 * i]), _signed(d[o + 4 + 2 * i])]
                                     for i in range(d[o + 2])]}),
    0x11: ("SWITCH_MAP", 2, _fixed(2), lambda d, o: {"map": d[o + 1]}),
    0x12: ("SET_UNIT_TIMER", 4, _fixed(4),
           lambda d, o: {"unit": d[o + 1], "timer": d[o + 2], "value": d[o + 3]}),
    0x13: ("TRIGGER_CELL_EVENT", 3, _fixed(3),
           lambda d, o: {"event_code": d[o + 1], "value": d[o + 2]}),
    0x14: ("SET_MAP_CELL", 5, _fixed(5),
           lambda d, o: {"x": d[o + 1], "y": d[o + 2],
                         "value": struct.unpack_from("<H", d, o + 3)[0]}),
    0x15: ("BIAS_PALETTE", 4, _fixed(4),
           lambda d, o: {"red": d[o + 1], "green": d[o + 2], "blue": d[o + 3]}),
    0x61: ("AWARD_XP_UNIT_3", 1, _fixed(1), lambda d, o: {}),
    0x62: ("ACTOR_BEHAVIOR_STEP", 3, _fixed(3),
           lambda d, o: {"unit": d[o + 1], "side_select": d[o + 2]}),
    0x63: ("ASK_THREE_WAY", 1, _fixed(1), lambda d, o: {}),
}

END = "END"


def decode_script(data):
    """Return the script's instructions, the terminating END included.

    Raises ScriptError when an instruction's operands run past the end of the
    data, or when the data ends before any terminator.  Bytes after the
    terminator are not looked at here; verify() is what insists there are none.
    """
    data = bytes(data)
    steps = []
    offset = 0
    while True:
        if offset >= len(data):
            raise ScriptError(f"no terminator: the data ends at {offset}")
        opcode = data[offset]
        spec = OPCODES.get(opcode)
        if spec is None:
            steps.append(Step(offset, opcode, END, 1, data[offset:offset + 1], {}))
            return steps
        mnemonic, header, length_of, operands_of = spec
        if offset + header > len(data):
            raise ScriptError(f"{mnemonic} at {offset}: header runs past the end")
        length = length_of(data, offset)
        if offset + length > len(data):
            raise ScriptError(f"{mnemonic} at {offset}: {length} bytes run past the end "
                              f"({len(data)})")
        steps.append(Step(offset, opcode, mnemonic, length,
                          data[offset:offset + length], operands_of(data, offset)))
        offset += length


# ---------------------------------------------------------------------------
# Tracing
# ---------------------------------------------------------------------------

@dataclass
class MapInfo:
    player_slots: int
    records: list  # [(char_id, wave)] in record order


@dataclass
class TracedStep:
    step: Step
    context: dict


@dataclass
class UnitArray:
    """What is known of the map unit array.

    known holds the identities of the first len(known) units.  With tail_open
    the array may be longer than that and what lies beyond is unknown (a
    battle deployed it); without it, len(known) is the unit count.
    """
    known: list
    tail_open: bool

    def resolve(self, index):
        """(identity, in_range): identity None when unknown."""
        if index < len(self.known):
            return self.known[index], True
        return None, self.tail_open

    def appended(self, units):
        if self.tail_open:
            return self
        return UnitArray(self.known + units, False)

    def as_json(self):
        return {"known": self.known, "tail_open": self.tail_open}


@dataclass
class Trace:
    steps: list
    maps_visited: list   # distinct maps, first visit order, the initial map first
    switches: list       # SWITCH_MAP targets in script order
    final_map: int
    final_units: UnitArray
    text_refs: list      # [{"offset", "block", "entry"}] in script order
    problems: list       # the script and the shipped resources disagree
    findings: list       # the script does something the original does out of bounds


def _units_after_reset(map_no, resources):
    """The unit array fdps_build_map_unit_array leaves for this map."""
    info = resources.map_info(map_no)
    if info is None:
        return UnitArray([], True)
    units = [{"party_slot": i} for i in range(info.player_slots)]
    units += _wave_records(map_no, info, 0)
    return UnitArray(units, False)


def _wave_records(map_no, info, wave):
    return [{"map": map_no, "record": i, "char_id": char_id}
            for i, (char_id, record_wave) in enumerate(info.records) if record_wave == wave]


# Which operands name units, per mnemonic.
def _unit_operands(step):
    ops = step.operands
    if step.mnemonic in ("WALK_UNITS", "FACE_UNITS"):
        return [u["unit"] for u in ops["units"]]
    if step.mnemonic == "BLINK_UNITS_OUT":
        return list(ops["units"])
    if step.mnemonic in ("PLACE_UNIT", "RETIRE_UNIT", "REVIVE_UNIT", "SET_UNIT_TIMER",
                         "ACTOR_BEHAVIOR_STEP"):
        return [ops["unit"]]
    if step.mnemonic == "AWARD_XP_UNIT_3":
        return [XP_AWARD_UNIT]
    return []


# The interpreter bounds-checks the unit index for these three and skips the
# opcode when it is out of range; every other unit opcode indexes regardless.
BOUNDS_CHECKED = ("PLACE_UNIT", "RETIRE_UNIT", "REVIVE_UNIT")


def trace_script(steps, initial_map, resources, units_known=True, initial_units=None):
    """Attach to every step the state it acts on.

    resources answers map_info(map_no) -> MapInfo or None,
    text_entry_count(block_no) -> int or None and has_saf(number) -> bool.

    With units_known the unit array starts as a fresh load of initial_map,
    which is what a chapter start leaves.  Without it the script runs on a
    battle's array: its front is still the fresh load -- player slots and
    wave 0 come first and nothing is ever removed or reordered -- but what the
    battle appended after that is unknown.  A given initial_units (UnitArray)
    overrides both.
    """
    current_map = initial_map
    if initial_units is not None:
        units = initial_units
    else:
        units = _units_after_reset(initial_map, resources)
        if not units_known:
            units = UnitArray(units.known, True)
    maps_visited = [initial_map]
    switches = []
    text_refs = []
    problems = []
    findings = []
    traced = []

    def problem(step, message):
        problems.append(f"{step.offset:#06x} {step.mnemonic}: {message}")

    def check_text(step, entry):
        block = current_map + 1
        count = resources.text_entry_count(block)
        if count is None:
            problem(step, f"text block FDETXT{block:02d}.TXT does not exist")
        elif entry >= count:
            problem(step, f"entry {entry:#x} is past FDETXT{block:02d}.TXT's {count} entries")
        text_refs.append({"offset": step.offset, "block": block, "entry": entry})
        return {"block": block, "entry": entry}

    for step in steps:
        context = {"map": current_map}
        m = step.mnemonic

        if m == "DRAW_TEXT":
            context["text"] = check_text(step, step.operands["entry"])
        elif m == "ASK_THREE_WAY":
            context["texts"] = [check_text(step, e) for e in CHOICE_TEXT_ENTRIES]
        elif m == "SWITCH_MAP":
            current_map = step.operands["map"]
            context["map"] = current_map
            if resources.map_info(current_map) is None:
                problem(step, f"map {current_map} has no MAP{current_map:02d}.DAT")
            if resources.text_entry_count(current_map + 1) is None:
                problem(step, f"map {current_map} has no FDETXT{current_map + 1:02d}.TXT")
            units = _units_after_reset(current_map, resources)
            switches.append(current_map)
            if current_map not in maps_visited:
                maps_visited.append(current_map)
        elif m == "DEPLOY_WAVE":
            wave = step.operands["wave"]
            if wave == "choice":
                units = UnitArray(units.known, True)
            else:
                info = resources.map_info(current_map)
                if info is None:
                    problem(step, f"map {current_map} has no MAP{current_map:02d}.DAT")
                    units = UnitArray(units.known, True)
                else:
                    deployed = _wave_records(current_map, info, wave)
                    context["deployed"] = deployed
                    if not deployed:
                        context.setdefault("notes", []).append(
                            f"MAP{current_map:02d}.DAT has no record in wave {wave}")
                    units = units.appended(deployed)
        elif m == "PLAY_SAF":
            if not resources.has_saf(step.operands["saf"]):
                problem(step, f"ICON{step.operands['saf']:04d}.SAF is not in {SCRIPT_ARCHIVE}")

        named = _unit_operands(step)
        if named:
            resolved = {}
            for index in named:
                identity, in_range = units.resolve(index)
                if in_range:
                    resolved[index] = identity
                    continue
                resolved[index] = "out_of_range"
                count = len(units.known)
                if m in BOUNDS_CHECKED:
                    context.setdefault("notes", []).append(
                        f"unit {index} >= unit count {count}: the opcode skips it")
                else:
                    findings.append(f"{step.offset:#06x} {m}: unit {index} >= unit count "
                                    f"{count}, written past the end of the unit array")
            context["units"] = resolved

        traced.append(TracedStep(step, context))

    return Trace(traced, maps_visited, switches, current_map, units, text_refs,
                 problems, findings)


# ---------------------------------------------------------------------------
# Resources out of the shipped containers
# ---------------------------------------------------------------------------

def read_container(path):
    """{member name: bytes} for one .VFS container."""
    data = Path(path).read_bytes()
    _, entries = parse_container(data, Path(path).name)
    return {e["name"]: data[e["offset"]:e["offset"] + e["size"]] for e in entries}


class GameResources:
    """Answers the trace's questions out of FIELD.VFS and ICONANI.VFS."""

    def __init__(self, game_dir):
        game_dir = Path(game_dir)
        self.field = read_container(game_dir / FIELD_ARCHIVE)
        self.icon = read_container(game_dir / SCRIPT_ARCHIVE)

    def map_info(self, map_no):
        data = self.field.get(f"MAP{map_no:02d}.DAT")
        if data is None:
            return None
        count = data[MAP_RECORD_COUNT_OFFSET]
        records = []
        for i in range(count):
            base = MAP_RECORD_BASE + MAP_RECORD_SIZE * i
            records.append((data[base + MAP_RECORD_CHAR_ID], data[base + MAP_RECORD_WAVE]))
        return MapInfo(data[MAP_PLAYER_SLOTS_OFFSET], records)

    def text_entry_count(self, block_no):
        # Only the entry count, which is the offset table's first word / 2
        # (resource_info/text.md).  Parsing the entries is text_decode's job;
        # this tool deliberately does not need it to run.
        data = self.field.get(f"FDETXT{block_no:02d}.TXT")
        if data is None:
            return None
        return struct.unpack_from("<h", data, 0)[0] // 2

    def has_saf(self, number):
        return f"ICON{number:04d}.SAF" in self.icon

    def scripts(self):
        return {name: data for name, data in sorted(self.icon.items())
                if name.endswith(".DAT")}


# ---------------------------------------------------------------------------
# Who runs each script: read off src/
# ---------------------------------------------------------------------------

@dataclass
class Caller:
    member: str      # upper-cased, as the VFS lookup folds it
    function: str
    chapter: int     # the player-facing chapter number, 1-based, out of the handler's name
    kind: str        # "init", "end" or "event"
    source: str      # file:line of the call


# GOODEND.DAT runs after WIN29.DAT in the same handler with nothing in
# between that reloads the map, so it starts on whatever map and units WIN29
# left (src/chend2b.c, fdps_chapter_30_end).
CHAINED_AFTER = {"GOODEND.DAT": "WIN29.DAT"}

_FUNCTION_RE = re.compile(r"^void (fdps_\w+)\(")
_CALL_RE = re.compile(r"fdps_icon_script_run\((\w+)\)")
_DEFINE_RE = re.compile(r'^#define (\w+) "([^"]+)"')
_CHAPTER_RE = re.compile(r"^fdps_chapter_(\d\d)_(init|end|event\w*)$")


def scan_callers(src_dir=REPO_DIR / "src"):
    """Every fdps_icon_script_run call site in src/*.c, with its script name."""
    callers = []
    for path in sorted(Path(src_dir).glob("*.c")):
        if path.name == "icon.c":
            continue
        defines = {}
        function = None
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if (d := _DEFINE_RE.match(line)):
                defines[d.group(1)] = d.group(2)
            if (f := _FUNCTION_RE.match(line)):
                function = f.group(1)
            for call in _CALL_RE.finditer(line):
                name = defines.get(call.group(1))
                chapter = _CHAPTER_RE.match(function or "")
                if name is None or chapter is None:
                    raise ScriptError(f"{path.name}:{number}: cannot resolve the call "
                                      f"{call.group(0)} in {function}")
                kind = chapter.group(2)
                callers.append(Caller(name.upper(), function, int(chapter.group(1)),
                                      "event" if kind.startswith("event") else kind,
                                      f"src/{path.name}:{number}"))
    return callers


# ---------------------------------------------------------------------------
# Whole-container decode
# ---------------------------------------------------------------------------

@dataclass
class ScriptReport:
    member: str
    size: int
    callers: list
    initial_map: int
    units_known: bool
    steps: list
    trace: Trace
    problems: list = field(default_factory=list)


def decode_all(game_dir, callers):
    """Decode and trace every script in ICONANI.VFS, callers attached."""
    resources = GameResources(game_dir)
    scripts = resources.scripts()
    by_member = {}
    for caller in callers:
        by_member.setdefault(caller.member, []).append(caller)

    reports = {}
    pending = sorted(scripts)
    # Chained scripts need their predecessor traced first.
    pending.sort(key=lambda name: name in CHAINED_AFTER)
    for name in pending:
        data = scripts[name]
        steps = decode_script(data)
        own = by_member.get(name, [])
        problems = []
        if not own:
            problems.append("no call site in src/ runs this script")
        if name in CHAINED_AFTER:
            before = reports[CHAINED_AFTER[name]]
            initial_map = before.trace.final_map
            trace = trace_script(steps, initial_map, resources,
                                 initial_units=before.trace.final_units)
            units_known = not before.trace.final_units.tail_open
        else:
            initial_map = own[0].chapter - 1 if own else 0
            units_known = bool(own) and all(c.kind == "init" for c in own)
            trace = trace_script(steps, initial_map, resources, units_known=units_known)
        reports[name] = ScriptReport(name, len(data), own, initial_map, units_known,
                                     steps, trace, problems)
    for member in by_member:
        if member not in scripts:
            raise ScriptError(f"{member} is run by {by_member[member][0].source} "
                              f"but is not in {SCRIPT_ARCHIVE}")
    return [reports[name] for name in sorted(reports)]


def verify(report):
    """Every hard check on one script; returns a list of failures."""
    failures = list(report.problems) + list(report.trace.problems)
    last = report.steps[-1]
    if last.opcode != 0:
        failures.append(f"ends on unknown opcode {last.opcode:#04x} at {last.offset:#x}, "
                        "not on opcode 0")
    if last.offset != report.size - 1:
        failures.append(f"terminator at {last.offset:#x} leaves "
                        f"{report.size - 1 - last.offset} bytes after it")
    return failures


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

FACING = {0: "下", 1: "左", 2: "上", 3: "右"}
KIND_LABEL = {"init": "開場", "end": "勝利", "event": "戰鬥中事件"}


def _unit_label(index, identity):
    if identity is None:
        return f"u{index}"
    if identity == "out_of_range":
        return f"u{index}(越界)"
    if "party_slot" in identity:
        return f"u{index}=我方{identity['party_slot']}"
    return f"u{index}=MAP{identity['map']:02d}#{identity['record']}(角色{identity['char_id']:#04x})"


def describe(traced, render_text=None):
    """One line of Chinese for one traced step."""
    s, c = traced.step, traced.context
    o = s.operands
    units = c.get("units", {})

    def unit(i):
        return _unit_label(i, units.get(i))

    def text(ref):
        label = f"FDETXT{ref['block']:02d}#{ref['entry']:#04x}"
        if render_text is not None:
            words = render_text(ref["block"], ref["entry"])
            if words is not None:
                label += f" 「{words}」"
        return label

    m = s.mnemonic
    if m == "END":
        return "結束" if s.opcode == 0 else f"結束（未知 opcode {s.opcode:#04x}）"
    if m == "WALK_UNITS":
        who = "、".join(f"{unit(u['unit'])}往{FACING.get(u['facing'], '右')}" for u in o["units"])
        return f"行走 {o['tiles']} 格，每步 {o['frames_per_sub_step']} 幀：{who}"
    if m == "FACE_UNITS":
        who = "、".join(f"{unit(u['unit'])}朝{FACING.get(u['facing'], str(u['facing']))}"
                       for u in o["units"])
        return f"轉向後停 {o['hold_frames']} 幀：{who}" if who else f"停 {o['hold_frames']} 幀"
    if m == "DRAW_TEXT":
        return f"顯示文字 {text(c['text'])}"
    if m == "DEPLOY_WAVE":
        how = "錨點原位" if o["place_exact"] else "找最近空格"
        if o["wave"] == "choice":
            return f"部署 MAP{c['map']:02d} 的波次＝三選一的答案（{how}）"
        who = "、".join(f"#{d['record']}(角色{d['char_id']:#04x})" for d in c.get("deployed", []))
        return f"部署 MAP{c['map']:02d} 波次 {o['wave']}（{how}）：{who or '無記錄'}"
    if m == "SCROLL_VIEW":
        return f"捲動視野到格 ({o['x']}, {o['y']})"
    if m == "PLAY_SAF":
        return f"播放動畫 ICON{o['saf']:04d}.SAF"
    if m == "SET_MUSIC":
        if o["music"] == "silence":
            return "停止音樂"
        return f"音樂 {o['music']}（CD 第 {o['cd_track']} 軌）"
    if m == "PLAY_WAV":
        return f"播放音效 ICON{o['wav']:04d}.WAV"
    if m == "BLINK_UNITS_OUT":
        return "閃爍後退場：" + "、".join(unit(u) for u in o["units"])
    if m == "PLACE_UNIT":
        return (f"放置 {unit(o['unit'])} 到 ({o['x']}, {o['y']})，"
                f"朝{FACING.get(o['facing'], str(o['facing']))}")
    if m == "RETIRE_UNIT":
        return f"退場 {unit(o['unit'])}"
    if m == "REVIVE_UNIT":
        return f"復歸 {unit(o['unit'])}（清狀態）"
    if m == "SET_VIEW_TILE":
        return f"視野直接設到格 ({o['x']}, {o['y']})"
    if m == "FADE_OUT":
        return f"淡出，每步 {o['step_delay_ms']} ms"
    if m == "FADE_IN":
        return f"淡入，每步 {o['step_delay_ms']} ms"
    if m == "SHAKE_VIEW":
        return (f"視野位移 {len(o['offsets'])} 步，每步 {o['frames_per_step']} 幀："
                + " ".join(f"({x},{y})" for x, y in o["offsets"]))
    if m == "SWITCH_MAP":
        return (f"切換地圖 → {o['map']}（MAP{o['map']:02d}，文字 FDETXT{o['map'] + 1:02d}）")
    if m == "SET_UNIT_TIMER":
        return (f"{unit(o['unit'])} 狀態計時 [{o['timer'] + 3}]（record +{0x22 + o['timer'] + 3:#x}）"
                f"= {o['value']}")
    if m == "TRIGGER_CELL_EVENT":
        return f"格子事件旗標 [{o['event_code']}] = {o['value']}，套用地圖變化"
    if m == "SET_MAP_CELL":
        return f"地形層 0 格 ({o['x']}, {o['y']}) = {o['value']:#06x}"
    if m == "BIAS_PALETTE":
        return f"調色盤偏移 R{o['red']} G{o['green']} B{o['blue']}"
    if m == "AWARD_XP_UNIT_3":
        return f"{unit(XP_AWARD_UNIT)} 十輪各 99 經驗，寫回名冊"
    if m == "ACTOR_BEHAVIOR_STEP":
        return f"{unit(o['unit'])} 跑一回合 AI（陣營選擇 {o['side_select']}）"
    if m == "ASK_THREE_WAY":
        return "三選一提問：" + "、".join(text(t) for t in c["texts"])
    raise AssertionError(m)


def render_markdown(report, render_text=None):
    lines = [f"# `{report.member}`", ""]
    if report.callers:
        for caller in report.callers:
            lines.append(f"- 第 {caller.chapter} 章{KIND_LABEL[caller.kind]}："
                         f"`{caller.function}`（`{caller.source}`）")
    else:
        lines.append("- 沒有呼叫端")
    if report.member in CHAINED_AFTER:
        lines.append(f"- 接在 `{CHAINED_AFTER[report.member]}` 之後播放，起始地圖沿用它的結束地圖")
    switches = " → ".join(str(m) for m in report.trace.switches) or "無"
    lines.append(f"- {report.size} byte、{len(report.steps)} 步；起始地圖 {report.initial_map}，"
                 f"切換地圖 {switches}，結束於地圖 {report.trace.final_map}")
    lines.append("- 單位編號：" + ("起始時整個單位陣列已知" if report.units_known else
                                  "起始時只知道我方 slot 與波次 0（戰鬥中追加的部分取決於戰況），"
                                  "切換地圖後整個已知"))
    for finding in report.trace.findings:
        lines.append(f"- 越界：`{finding}`")
    lines += ["", "| 偏移 | bytes | 指令 | 內容 |", "| ---: | --- | --- | --- |"]
    for traced in report.trace.steps:
        s = traced.step
        raw = s.raw.hex(" ")
        if len(raw) > 36:
            raw = raw[:33] + "…"
        lines.append(f"| `{s.offset:04x}` | `{raw}` | `{s.mnemonic}` | "
                     f"{describe(traced, render_text).replace('|', '｜')} |")
    return "\n".join(lines) + "\n"


def report_json(report):
    return {
        "member": report.member,
        "size": report.size,
        "callers": [vars(c) for c in report.callers],
        "chained_after": CHAINED_AFTER.get(report.member),
        "initial_map": report.initial_map,
        "units_known": report.units_known,
        "maps_visited": report.trace.maps_visited,
        "switches": report.trace.switches,
        "final_map": report.trace.final_map,
        "final_units": report.trace.final_units.as_json(),
        "text_refs": report.trace.text_refs,
        "findings": report.trace.findings,
        "steps": [dict(t.step.as_json(), context=t.context) for t in report.trace.steps],
    }


def text_decode_renderer(game_dir):
    """A renderer backed by tools/text_decode, the owner of FDETXTnn.TXT.

    Imported only when asked for, so this tool keeps working without it.  An
    entry whose glyphs have no character yet comes back with text_decode's
    {glyph 0xNNNN} tags.
    """
    sys.path.insert(0, str(TOOLS_DIR / "text_decode"))
    from text_decode import load_glyph_table, parse_block, render_entry

    field = read_container(Path(game_dir) / FIELD_ARCHIVE)
    table = load_glyph_table()
    blocks = {}

    def render(block_no, entry_no):
        if block_no not in blocks:
            data = field.get(f"FDETXT{block_no:02d}.TXT")
            blocks[block_no] = parse_block(data) if data is not None else []
        entries = blocks[block_no]
        return render_entry(entries[entry_no], table) if entry_no < len(entries) else None

    return render


def load_renderer(spec):
    """'path/to/module.py:function' -> the callable."""
    path, _, name = spec.rpartition(":")
    if not path or not name:
        raise ScriptError(f"--text-renderer wants path.py:function, got {spec!r}")
    module_spec = importlib.util.spec_from_file_location("text_renderer", path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return getattr(module, name)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_show(game_dir, member, renderer=None):
    member = member.upper()
    for report in decode_all(game_dir, scan_callers()):
        if report.member == member:
            print(render_markdown(report, renderer), end="")
            return 0
    raise ScriptError(f"{member} is not a script in {SCRIPT_ARCHIVE}")


def cmd_dump(game_dir, out_dir, renderer=None):
    out = Path(out_dir)
    (out / "scripts").mkdir(parents=True, exist_ok=True)
    reports = decode_all(game_dir, scan_callers())
    index = []
    for report in reports:
        stem = report.member.rsplit(".", 1)[0]
        (out / "scripts" / f"{stem}.md").write_text(render_markdown(report, renderer),
                                                     encoding="utf-8")
        data = report_json(report)
        (out / "scripts" / f"{stem}.json").write_text(
            json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        index.append({k: data[k] for k in ("member", "size", "callers", "chained_after",
                                           "initial_map", "maps_visited", "switches",
                                           "final_map", "text_refs", "findings")})
    (out / "index.json").write_text(json.dumps(index, indent=1, ensure_ascii=False) + "\n",
                                    encoding="utf-8")
    print(f"{len(reports)} scripts -> {out}")
    return cmd_verify(game_dir, reports)


def cmd_verify(game_dir, reports=None):
    reports = reports or decode_all(game_dir, scan_callers())
    failed = 0
    for report in reports:
        for failure in verify(report):
            failed += 1
            print(f"FAIL {report.member}: {failure}")
        for finding in report.trace.findings:
            print(f"out of bounds (original behaviour) {report.member}: {finding}")
    used = Counter(s.opcode for r in reports for s in r.steps)
    unused = sorted(set(OPCODES) - set(used))
    print(f"{len(reports)} scripts, {sum(len(r.steps) for r in reports)} steps, "
          f"{failed} failures")
    print("opcodes the interpreter supports and no script uses: "
          + (", ".join(f"{op:#04x} {OPCODES[op][0]}" for op in unused) or "none"))
    return 1 if failed else 0


def cmd_report(game_dir):
    """The Markdown tables resource_info/cutscene_script.md carries."""
    reports = decode_all(game_dir, scan_callers())
    used = Counter(s.opcode for r in reports for s in r.steps)
    scripts_using = Counter()
    for r in reports:
        for op in {s.opcode for s in r.steps}:
            scripts_using[op] += 1
    print("| opcode | 指令 | 出現次數 | 用到的腳本數 |")
    print("| --- | --- | ---: | ---: |")
    for op in sorted(set(OPCODES) | {0}):
        name = OPCODES[op][0] if op in OPCODES else END
        print(f"| `{op:#04x}` | `{name}` | {used.get(op, 0)} | {scripts_using.get(op, 0)} |")
    print()
    print("| 腳本 | 播放時機 | 呼叫端 | 起始地圖 | 切換到的地圖 | 結束地圖 | 用到的文字區塊與條目 |")
    print("| --- | --- | --- | ---: | --- | ---: | --- |")
    for r in reports:
        when = "；".join(f"第 {c.chapter} 章{KIND_LABEL[c.kind]}" for c in r.callers)
        if r.member in CHAINED_AFTER:
            when += f"（接在 `{CHAINED_AFTER[r.member]}` 之後）"
        funcs = "、".join(f"`{c.function}`" for c in r.callers)
        switched = " → ".join(str(m) for m in r.trace.switches) or "—"
        blocks = {}
        for ref in r.trace.text_refs:
            blocks.setdefault(ref["block"], []).append(ref["entry"])
        texts = "；".join(
            f"`FDETXT{b:02d}` " + ",".join(f"{e:#04x}" for e in sorted(set(es)))
            for b, es in sorted(blocks.items())) or "—"
        print(f"| `{r.member}` | {when} | {funcs} | {r.initial_map} | {switched} | "
              f"{r.trace.final_map} | {texts} |")
    return 0


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    args = list(argv[1:])
    renderer = None
    use_text_decode = "--text" in args
    if use_text_decode:
        args.remove("--text")
    if "--text-renderer" in args:
        i = args.index("--text-renderer")
        renderer = load_renderer(args[i + 1])
        del args[i:i + 2]
    commands = {"show": (cmd_show, 2), "dump": (cmd_dump, 2), "verify": (cmd_verify, 1),
                "report": (cmd_report, 1)}
    if not args or args[0] not in commands or len(args) != 1 + commands[args[0]][1]:
        print(__doc__, file=sys.stderr)
        return 2
    handler = commands[args[0]][0]
    try:
        if use_text_decode and renderer is None:
            renderer = text_decode_renderer(args[1])
        if handler in (cmd_show, cmd_dump):
            return handler(*args[1:], renderer=renderer)
        return handler(*args[1:])
    except (ScriptError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
