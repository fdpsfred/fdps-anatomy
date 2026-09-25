"""index.py -- chapters/_index.md, the owner of cross-chapter facts (ticket 25.8).

The page is assembled, not edited by hand: fixed introductory text, generated
tables (chapter_facts.py's sources), and one prose section -- the
cross-chapter mechanism chains -- that an agent drafts after every chapter
page exists.  The generated tables sit between markers like the chapter
pages' blocks, so `verify` can regenerate and compare them.

    <!-- chapter_docs:index_KEY --> ... <!-- /chapter_docs:index_KEY -->

The chains section comes from workspace/chapter_docs/drafts/_index_chains.md
when that draft exists, otherwise it is carried over from the landed page.

Usage: python tools/chapter_docs/index.py build  [--json]
       python tools/chapter_docs/index.py verify [--json]
Exit : 0 when the page is written (build) or consistent (verify).
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import chapter_facts as facts  # noqa: E402
import check_chapter as gate  # noqa: E402
import map_decode  # noqa: E402  (on the path chapter_facts set up)
from check_mechanics import citations, load_idents, load_names  # noqa: E402

INDEX = gate.LANDED / "_index.md"
CHAINS_DRAFT = facts.DRAFTS / "_index_chains.md"
CHAINS_HEADING = "## 跨章機制鏈"

INTRO = """# chapters — 關卡視角

回答「每一章的關卡內容與事件流程是什麼」。遊戲的關卡單位是章，共 30 章，沒有序章或終章（依據：攻略站「遊戲攻略」頁，鏡像見 [`docs/guide/`](../docs/guide/_index.md)）。一章一檔 `chNN.md`（章號補零兩位），每章檔寫該章專屬的內容：劇情、加入角色、敵人與波次、寶物、勝敗條件、處理流程、回合與格子事件、過場腳本與全部對白。

章號 1 起算，`FDPS.LE` 內部的章節索引 0 起算，兩者差 1（見 [`CONTEXT.md`](../CONTEXT.md) 的詞條）。下列各表併列兩種編號，從程式側的常數表取值再寫進章節檔時，以此換算。

本索引是**跨章事實的唯一擁有者**：四張章節處理表的逐章總表與共用處理函式、跨章機制鏈、章號與資源檔的對照、額外場景地圖的歸屬、村莊與連戰區段都只寫在這裡，章節檔不重複。處理表怎麼被呼叫、回合事件與格子事件怎麼觸發是機制，屬於 `program_info/`；各章頁與本索引只寫內容。

章節裡到不了的內容——永遠不部署的波次、永遠不顯示的對白、額外場景裡的殘留——由 [`cut_content/story.md`](../cut_content/story.md) 擁有，各章頁只寫一行連過去；沒有格子引用的寶物記錄由 [`cut_content/items.md`](../cut_content/items.md) 擁有。

每章頁裡夾在 `<!-- chapter_docs:… -->` 標記之間的區塊（含 `maps/` 的戰場全圖）與本頁的表，由 [`tools/chapter_docs/`](../tools/chapter_docs/_index.md) 從出貨資料與 `src/` 產生，不直接編輯。"""


def _mark(key, body):
    return f"{gate.OPEN.format('index_' + key)}\n{body}\n{gate.CLOSE.format('index_' + key)}"


def _page_link(n):
    name = f"ch{n:02d}.md"
    return f"[`{name}`]({name})" if (gate.LANDED / name).exists() else "（尚無）"


def table_chapters():
    rows = ["| 章號 | 章節索引 | 章名 | 勝利條件 | 敗北條件 | 文件 |",
            "| ---: | ---: | --- | --- | --- | --- |"]
    for n in facts.CHAPTERS:
        rows.append(f"| {n} | {n - 1} | {facts.cell(facts.chapter_title(n))} | "
                    f"{facts.cell(facts.one_line(facts.entry_line(n, 2)))} | "
                    f"{facts.cell(facts.one_line(facts.entry_line(n, 3)))} | {_page_link(n)} |")
    return "\n".join(rows)


def table_handlers():
    t = facts.handler_tables()
    distinct = all(len(set(t[k])) == len(t[k]) for k in ("init", "post", "end"))
    rows = [f"進入處理 `0x{facts.table_address('init'):x}`、行動後檢查 `0x{facts.table_address('post'):x}`、"
            f"勝利處理 `0x{facts.table_address('end'):x}` 都是 30 格、以章節索引索引"
            + ("，每章各有自己的一支，沒有兩章共用同一支。" if distinct else "。"), "",
            "| 章號 | 進入處理 | 行動後檢查 | 勝利處理 |", "| ---: | --- | --- | --- |"]
    for n in facts.CHAPTERS:
        i = n - 1
        rows.append(f"| {n} | {facts.cite(t['init'][i])} | {facts.cite(t['post'][i])} | "
                    f"{facts.cite(t['end'][i])} |")
    return "\n".join(rows)


def table_event_slots():
    t = facts.handler_tables()["event"]
    users = {}
    for n in facts.CHAPTERS:
        for slot, where in facts.event_slots_used(facts.battle_map(n - 1)).items():
            users.setdefault(slot, []).append((n, where))
    rows = [f"章節事件處理表 `0x{facts.table_address('event'):x}` 有 50 格，以地圖資料裡的 slot 編號"
            "索引（回合事件、格子事件、種類 2 以上的可搜尋格記錄、死亡腳本 opcode 2）。下表是每個 slot "
            "的處理函式與出貨資料裡呼叫它的章。", "",
            "| slot | 處理函式 | 呼叫它的章（來源） |", "| ---: | --- | --- |"]
    for slot, name in enumerate(t):
        if slot in users:
            who = "、".join(f"[第 {n} 章](ch{n:02d}.md)（{'、'.join(w)}）"
                           if (gate.LANDED / f"ch{n:02d}.md").exists()
                           else f"第 {n} 章（{'、'.join(w)}）" for n, w in users[slot])
        else:
            who = f"出貨資料沒有任何一筆呼叫這個 slot，見 [刪減與未用]({facts.CUT})"
        rows.append(f"| {slot} | {facts.cite(name)} | {who} |")
    shared = {slot: u for slot, u in users.items() if len({n for n, _ in u}) > 1}
    if shared:
        rows += ["", "共用處理函式（出貨資料裡被兩章以上呼叫的 slot）：" + "、".join(
            f"slot {slot} {facts.cite(t[slot])}（第 {'、'.join(str(n) for n in sorted({n for n, _ in u}))} 章）"
            for slot, u in sorted(shared.items())) + "。"]
    return "\n".join(rows)


def _scripts_of(n, kind):
    return [member for member, _, mine in facts.chapter_scripts(n)
            if any(c.kind == kind for c in mine)]


def table_resources():
    rows = ["| 章號 | 戰場地圖 | 文字區塊 | 開場腳本 | 勝利腳本 | 戰鬥中事件腳本 | 本章之前的村莊 | 光碟 |",
            "| ---: | --- | --- | --- | --- | --- | --- | ---: |"]

    def names(xs):
        return "、".join(f"`{x}`" for x in xs) or "—"

    for n in facts.CHAPTERS:
        v = facts.village_before(n)
        rows.append(f"| {n} | `MAP{n - 1:02d}` | `FDETXT{n:02d}` | {names(_scripts_of(n, 'init'))} | "
                    f"{names(_scripts_of(n, 'end'))} | {names(_scripts_of(n, 'event'))} | "
                    f"{f'`SHOP{v[0]:02d}`' if v else '—'} | "
                    f"{facts.disc_of(n - 1)} |")
    rows += ["", "地形層（`M%02d.DTL`／`.MPL`／`ATTR`／`DSC`）與戰場地圖同號，見 "
             "[`resource_info/terrain.md`](../resource_info/terrain.md)。每章的 CD 音軌由程式內嵌的"
             "常數表以章節索引查，見 [`program_info/cd_audio.md`](../program_info/cd_audio.md)。"]
    return "\n".join(rows)


def table_scenes():
    switched = {}
    for member, r in sorted(facts.scripts().items()):
        for target in r.trace.switches:
            if target >= facts.FIRST_SCENE_MAP:
                who = "、".join(sorted({f"第 {c.chapter} 章{facts.KIND_LABEL[c.kind]}"
                                       for c in r.callers}))
                label = f"`{member}`（{who}）"
                switched.setdefault(target, [])
                if label not in switched[target]:
                    switched[target].append(label)
    maps = [n for n in map_decode.map_numbers(facts.DUMP) if n >= facts.FIRST_SCENE_MAP]
    rows = ["地圖編號 31 以後是不對應章節的額外場景，只由過場腳本的 `SWITCH_MAP` 切過去；切換時一併換掉"
            "文字區塊（`FDETXT` 地圖 + 1）與單位。這些區塊的文字由 "
            "[`assets/text/scene_text.md`](../assets/text/scene_text.md) 擁有。", "",
            "| 地圖 | 文字區塊 | 切到它的腳本 |", "| ---: | --- | --- |"]
    for n in maps:
        who = "、".join(switched.get(n, [])) or f"沒有腳本切過去，見 [刪減與未用]({facts.CUT})"
        rows.append(f"| {n} | `FDETXT{n + 1:02d}` | {who} |")
    return "\n".join(rows)


def table_villages():
    rows = ["一章勝利後，章節結束處理把章節索引改成下一章，接著依 `CHAPTER_HAS_NO_VILLAGE`（`src/village.c`）"
            "決定要不要進村莊；不進村莊的兩章之間直接到存檔畫面，中間買不到東西，是連戰。", "",
            "| 兩章之間 | 村莊 |", "| --- | --- |"]
    for n in range(2, 31):
        v = facts.village_before(n)
        rows.append(f"| 第 {n - 1} → {n} 章 | {f'有，`SHOP{v[0]:02d}.DAT`' if v else '無'} |")
    runs, start = [], None
    for n in range(2, 31):
        if facts.village_before(n) is None:
            start = n - 1 if start is None else start
        elif start is not None:
            runs.append((start, n - 1))
            start = None
    if start is not None:
        runs.append((start, 30))
    rows += ["", "連戰區段（中間沒有村莊的連續章）：" + "、".join(
        f"第 {a}–{b} 章" for a, b in runs) + "。"]
    return "\n".join(rows)


TABLES = (
    ("章節總表", "chapters", table_chapters),
    ("章節處理表", "handlers", table_handlers),
    ("章節事件處理表與共用處理函式", "event_slots", table_event_slots),
    ("章號與資源檔的對照", "resources", table_resources),
    ("額外場景地圖的歸屬", "scenes", table_scenes),
    ("村莊與連戰", "villages", table_villages),
)


class ChainsDraftError(Exception):
    """The chains draft is not usable."""


def chains_section(current):
    """The chains prose: the draft if there is one, else what the page has."""
    if CHAINS_DRAFT.exists():
        text = CHAINS_DRAFT.read_text(encoding="utf-8").strip()
    else:
        m = re.search(re.escape(CHAINS_HEADING) + r"\n(.*)", current or "", re.S)
        text = (CHAINS_HEADING + "\n" + m.group(1)).strip() if m else ""
    if not text:
        return None
    if not text.startswith(CHAINS_HEADING + "\n"):
        raise ChainsDraftError(f"{CHAINS_DRAFT.name}: must start with '{CHAINS_HEADING}'")
    return text


def build_text(current, chains=None):
    """The whole page; chains, when given, is used instead of chains_section."""
    parts = [INTRO]
    for heading, key, fn in TABLES:
        parts.append(f"## {heading}\n\n{_mark(key, fn())}")
    chains = chains if chains is not None else chains_section(current)
    if chains:
        parts.append(chains)
    return "\n\n".join(parts) + "\n"


def check(text):
    """The knowledge-base rules on the whole page, and a source for every
    chain: each ### subsection of the chains section cites a function."""
    lines = text.splitlines()
    out = gate.check_kb_rules(lines, load_names(), load_idents(), draft=False)
    if CHAINS_HEADING not in lines:
        out.append(("error", "structure", 1, "no %s section" % CHAINS_HEADING))
        return out
    start = lines.index(CHAINS_HEADING)
    sub, sub_line, cited = None, 0, False
    for no, line in enumerate(lines[start + 1:] + ["### end"], start + 2):
        if line.startswith("### "):
            if sub is not None and not cited:
                out.append(("error", "uncited-chain", sub_line,
                            "chain '%s' cites no function as `name`（`0xaddr`）" % sub))
            sub, sub_line, cited = line[4:].strip(), no, False
        elif any(True for _ in citations(line)):
            cited = True
    return out


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("build", "verify"))
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    current = INDEX.read_text(encoding="utf-8") if INDEX.exists() else None
    try:
        fresh = build_text(current)
        findings = check(fresh)
    except ChainsDraftError as e:
        fresh, findings = None, [("error", "chains-draft", 1, str(e))]
    if a.cmd == "verify" and fresh is not None and current != fresh:
        findings.append(("error", "stale", 1, "chapters/_index.md differs from a fresh build"))
    errors = [f for f in findings if f[0] == "error"]
    written = a.cmd == "build" and not errors and fresh != current
    if written:
        INDEX.write_bytes(fresh.encode("utf-8"))
    report = {"errors": len(errors), "written": written,
              "findings": [{"level": f[0], "code": f[1], "line": f[2], "message": f[3]}
                           for f in findings]}
    if a.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        for f in findings:
            print("  %s %s line %d: %s" % f)
        print("%s: %d error(s)" % (a.cmd, len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
