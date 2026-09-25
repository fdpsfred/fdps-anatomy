"""The non-judging half of the ticket-25.14 verification workflow.

Ticket 25.14's own session placed a number of things on cut_content/story.md
and in the exclusion list one by one while writing the page: the entries and
exclusions S14-S17, the owner of every never-shown text entry that 25.9 had not
already settled (S13b drafts, S15 copies), the four chapter-30 lines put under
S4, the headers of the S5 blocks and the 16:0x00 override.  Every one of those
is a per-item judgement, so each is verified again by an agent of its own
(verify_story_ticket25_14.js).  This script is everything around that which
needs no judgement:

    python tools/cut_content/story_verify.py show <ID>      ONE claim (the agent's only view of it)
    python tools/cut_content/story_verify.py mechanical     verdicts for the claims a string compare settles
    python tools/cut_content/story_verify.py pending [--all]
    python tools/cut_content/story_verify.py check [--ids ...] [--json]
    python tools/cut_content/story_verify.py rescan
    python tools/cut_content/story_verify.py report --date YYYY-MM-DD [--stopped REASON]

The claims are generated from story.py (OWNERS, the game text) and the texts
below, so a change to either makes an old verdict stale: every verdict records
the SHA-1 of the claim it judged.

Two kinds of item:
    judged      one agent each.  Ids: E-<entry> for an entry or exclusion,
                B<block>-<entry> for one text entry's placement, H<block> for
                the header of one S5 block.
    mechanical  a never-shown entry owned by S13b or S15 that is a verbatim copy
                of an entry that IS shown: a string compare settles it, so
                `mechanical` writes its verdict (id M<block>-<entry>).
"""

import argparse
import hashlib
import json
import sys
from functools import lru_cache
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tools" / "cut_traces"))
import story  # noqa: E402  (OWNERS and the text)
import cuttrace  # noqa: E402  (the evidence rule shared by every cut-content gate)

WORK = REPO / "workspace" / "story_verify"
VERDICTS = WORK / "verdicts"
RUNS = REPO / "devlog" / "runs"

VERDICT_VALUES = ("holds", "refuted", "needs_correction")
CATEGORY_VALUES = ("residual", "stub", "sealed", "predecessor_leftover", "negative",
                   "excluded", "none")
CONFIDENCE_VALUES = ("high", "medium", "low")
REQUIRED = ("id", "claim_sha1", "verdict", "category", "owner", "confidence", "conclusion",
            "evidence", "corrected_claim", "open_question", "pitfall_candidate")
MIN_CONCLUSION = 20

OWNER_MEANING = {
    "S4": "S4（殘留內容：寫好卻沒被顯示的對白）",
    "S5": "S5（殘留內容：過場區塊裡某時期章節區塊的複本，開頭 9 條整組帶著）",
    "S13a": "S13a（排除：章節區塊的章名文字，章名卡改畫 CHAPTER.SAF 的圖）",
    "S13b": "S13b（排除：被取代的舊稿——同一段台詞顯示中的版本在另一個區塊）",
    "S15": "S15（排除：與顯示中的條目相同或只差幾個字的複本）",
}

ENTRY_CLAIMS = {
    "E-S14": (
        "`FIELD.VFS` `FDETXT00.TXT` 的條目 `0x000` 是一整列數字與大寫英文字母"
        "「0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ」（字模索引 0–0x23 依序），沒有任何讀取端"
        "（名稱表的起點都大於 0，系統訊息沒有寫死條目 0）。前作 FD2 的共用文字（FDTXT.DAT 第 0 項）"
        "第 0 頁是同一列字、在 FD2 裡同樣沒有讀取端，FDPS 原封不動帶了過來，所以歸前作遺留（S14）。"),
    "E-S16-ICON00": (
        "過場腳本 `ICON00.DAT` 在 `0x454`、`0x48C` 的兩條 `FACE_UNITS` 對當時還不存在的單位（1、3）轉向，"
        "寫到單位陣列尾端之外；那兩個單位由同一支腳本稍後的 `DEPLOY_WAVE` 部署。沒有做好或被封住的內容，"
        "歸排除（資料填錯，S16）。"),
    "E-S16-ICON11": (
        "過場腳本 `ICON11.DAT` 在 `0x008` 的 `FACE_UNITS` 對當時還不存在的單位 10 轉向（當時 9 個單位），"
        "那個單位由 `0x019` 的 `DEPLOY_WAVE` 部署。沒有做好或被封住的內容，歸排除（資料填錯，S16）。"),
    "E-S16-ICON23": (
        "過場腳本 `ICON23.DAT` 在 `0x108`、`0x10D` 的兩條 `FACE_UNITS` 對單位 38 轉向，而當時只有 21 個單位、"
        "38 號在那張地圖上從未部署；原本指的是誰判斷不出來。沒有做好或被封住的內容，歸排除（資料填錯，S16）。"),
    "E-S16-WIN24": (
        "過場腳本 `WIN24.DAT` 切到地圖 22 之後以 35 條 `RETIRE_UNIT` 退場 32–66 號（當時只有 32 個單位），"
        "切回地圖 59 之後的 `PLACE_UNIT 22`（當時只有 2 個單位），這 36 條都被邊界檢查整條略過。退場清單比當時的"
        "單位數長，沒有做好或被封住的內容，歸排除（資料填錯，S16）。"),
    "E-S17": (
        "`FIELD2.VFS` 的 `ATTR151.DAT` 是地圖 15（第 16 章戰場）第 1 層的圖磚屬性表：785 byte、192 列，"
        "118 列的地形類別是 6，其餘 74 列全 0，檔頭與 `ATTR071.DAT`、`ATTR291.DAT` 相同。場景圖層依 `DSC` "
        "宣告的層數逐層載入，而 `DSC15.DAT` 只宣告一層，所以它永遠不會載入；同一層的 `M151.MPL`、`M151.CEL` "
        "也不存在。只有屬性表、沒有圖磚與圖面，所以是空殼（S17），不是殘留內容。"),
    "E-16-00": (
        "`FDETXT16` 的 `0x00`（「第十六章」）永遠不會顯示：第 16 章流浪工匠事件 "
        "`fdps_chapter_16_event_wandering_smith_forge` 以變數 `smith_reply_text_id` 畫字，初值是 0，"
        "但那次 `fdps_draw_text` 只在它不等於 0 時執行，所以只會畫 `0x0d` 或 `0x0e`。它與其他 29 章的 `0x00` "
        "一樣屬於 S13a。"),
}

CH30_CLAIMS = {
    (30, 0x0D): "第二型態平衡之神（角色 61）的叫陣；部署它的 `fdps_chapter_30_event_deploy_wave_3` 不畫字，"
                "也沒有死亡腳本或過場畫這一條",
    (30, 0x0E): "第三型態平衡之神（角色 62）喊出鬼動死靈陣；第三型態的死亡腳本運算元是 0xFF（不畫字），"
                "也沒有處理函式或過場畫這一條",
    (30, 0x13): "另一版的道別（前半）：前半與 `WIN29` 在地圖 63 畫的 `FDETXT64` `0x11` 相同，接著蘭迪斯問"
                "法蓮娜是不是真的不願意同行；劇情走向與顯示中的版本不同，所以不是被取代的舊稿",
    (30, 0x14): "另一版的道別（後半）：法蓮娜沉默，蘭迪斯說「我會永遠記得妳的。我走了！」；顯示中的版本改成"
                "兩人約定再會，劇情走向不同，所以不是被取代的舊稿",
}


def sha1(text):
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def ref(block, entry):
    return f"`FDETXT{block:02d}` `0x{entry:02x}`"


@lru_cache(maxsize=None)
def context():
    game = story.Game(story.DEFAULT_GAME)
    never, unsettled = story.never_shown_text(game)
    shown = story.all_shown(game, never, unsettled)
    return game, never, unsettled, shown


@lru_cache(maxsize=None)
def items():
    """[{id, kind, claim}] in a fixed order."""
    game, never, unsettled, shown = context()
    out = [{"id": i, "kind": "judged", "claim": c} for i, c in ENTRY_CLAIMS.items()]
    for key, what in CH30_CLAIMS.items():
        out.append({"id": f"B{key[0]:02d}-{key[1]:02x}", "kind": "judged", "claim":
                    f"{ref(*key)}（全文：{game.line(*key)}）是{what}。它是寫好卻永遠不會顯示的對白，"
                    f"屬於 {OWNER_MEANING['S4']}。"})
    for block in story.S5_BLOCKS:
        heads = "；".join(f"`0x{e:02x}`：{game.line(block, e)}" for e in range(4))
        out.append({"id": f"H{block}", "kind": "judged", "claim":
                    f"`FDETXT{block:02d}` 的 `0x00`–`0x03`（{heads}）永遠不會顯示；它們是這個區塊從章節區塊"
                    f"複製時連同 `0x04`–`0x08` 的村莊台詞一起帶來的章名與勝敗條件，屬於 {OWNER_MEANING['S5']}，"
                    "而不是另列的排除項。"})
    for key, owner in sorted(story.OWNERS.items()):
        if owner not in ("S13b", "S15") or key[0] in unsettled:
            continue
        kind, twin = story.twin(game, key, shown)
        if kind == "same":
            out.append({"id": f"M{key[0]:02d}-{key[1]:02x}", "kind": "mechanical", "claim":
                        f"{ref(*key)} 永遠不會顯示，與顯示中的 {ref(*twin)} 逐字相同，屬於 "
                        f"{OWNER_MEANING[owner]}。", "twin": twin})
            continue
        out.append({"id": f"B{key[0]:02d}-{key[1]:02x}", "kind": "judged", "claim":
                    f"{ref(*key)}（全文：{game.line(*key)}）永遠不會顯示，屬於 {OWNER_MEANING[owner]}。"
                    f"顯示中最接近的是 {ref(*twin)}（全文：{game.line(*twin)}；以字元計相符 "
                    f"{kind:.0%}）。"})
    return out


def by_id():
    return {i["id"]: i for i in items()}


# ---------------------------------------------------------------------- gate

def check_verdict(v, item):
    if not isinstance(v, dict):
        return ["the verdict file is not a JSON object"]
    problems = ["missing field %s" % k for k in REQUIRED if k not in v]
    if problems:
        return problems
    text_fields = ("id", "claim_sha1", "verdict", "category", "owner", "confidence",
                   "conclusion", "corrected_claim", "open_question", "pitfall_candidate")
    problems += ["%s must be a string" % k for k in text_fields if not isinstance(v[k], str)]
    if problems:
        return problems
    if v["id"] != item["id"]:
        problems.append("id %r does not match %s" % (v["id"], item["id"]))
    if v["claim_sha1"] != sha1(item["claim"]):
        problems.append("stale: judged a different wording of the claim")
    for field, allowed in (("verdict", VERDICT_VALUES), ("category", CATEGORY_VALUES),
                           ("confidence", CONFIDENCE_VALUES)):
        if v[field] not in allowed:
            problems.append("%s %r not one of %s" % (field, v[field], allowed))
    if len(v["conclusion"].strip()) < MIN_CONCLUSION:
        problems.append("conclusion is shorter than %d characters" % MIN_CONCLUSION)
    if v["verdict"] != "holds" and not v["corrected_claim"].strip():
        problems.append("%s without corrected_claim" % v["verdict"])
    if not v["owner"].strip():
        problems.append("owner is empty: name the entry or exclusion id, or none")
    if item["kind"] == "judged":
        problems += cuttrace.check_evidence(v["evidence"])
    return problems


def _load(path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except Exception as exc:                                        # noqa: BLE001
        return None, "unreadable: %s" % exc


def states(wanted):
    rows = []
    for item in wanted:
        path = VERDICTS / ("%s.json" % item["id"])
        if not path.is_file():
            rows.append({"id": item["id"], "kind": item["kind"], "state": "missing"})
            continue
        v, err = _load(path)
        problems = [err] if err else check_verdict(v, item)
        rows.append({"id": item["id"], "kind": item["kind"],
                     "state": "failing" if problems else "done",
                     **({"problems": problems} if problems else {})})
    return rows


# ------------------------------------------------------------ mechanical ones

def mechanical():
    """Write the verdict of every verbatim-copy item; returns (written, failed)."""
    game, never, unsettled, shown = context()
    VERDICTS.mkdir(parents=True, exist_ok=True)
    written, failed = 0, []
    for item in items():
        if item["kind"] != "mechanical":
            continue
        key = (int(item["id"][1:3]), int(item["id"][4:], 16))
        twin = tuple(item["twin"])
        ok = key in never and twin in shown and game.line(*key) == game.line(*twin)
        if not ok:
            failed.append(item["id"])
            continue
        verdict = {
            "id": item["id"], "claim_sha1": sha1(item["claim"]), "verdict": "holds",
            "category": "excluded", "owner": story.OWNERS[key], "confidence": "high",
            "conclusion": f"{ref(*key)} 在永遠不會顯示的集合裡，{ref(*twin)} 有讀取端，兩者解碼後逐字相同。",
            "evidence": [{"source": "data", "location": f"FIELD.VFS/FDETXT{key[0]:02d}.TXT@entry 0x{key[1]:02x}",
                          "observation": "text_decode 解碼後與 twin 的字串完全相同（story_verify.py mechanical）"}],
            "corrected_claim": "", "open_question": "", "pitfall_candidate": "",
            "method": "mechanical",
        }
        (VERDICTS / ("%s.json" % item["id"])).write_text(
            json.dumps(verdict, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        written += 1
    return written, failed


# ------------------------------------------------------------ rescan / report

def rescan_todo():
    todo = []
    for row in states([i for i in items() if i["kind"] == "judged"]):
        if row["state"] != "done":
            continue
        v, _ = _load(VERDICTS / ("%s.json" % row["id"]))
        why = []
        if v["verdict"] != "holds":
            why.append("verdict is %s" % v["verdict"])
        if v["confidence"] != "high":
            why.append("confidence is %s" % v["confidence"])
        if v["open_question"].strip():
            why.append("open question recorded")
        if why and not (v.get("_supersedes") or v.get("_reread")):
            todo.append({"id": row["id"], "why": "; ".join(why)})
    return todo


def summarize(stopped):
    rows = states(items())
    done = [r["id"] for r in rows if r["state"] == "done"]
    out = {"total": len(rows), "judged": sum(r["kind"] == "judged" for r in rows),
           "mechanical": sum(r["kind"] == "mechanical" for r in rows),
           "complete": len(done), "unfinished": [r["id"] for r in rows if r["state"] != "done"],
           "unfinished_detail": [r for r in rows if r["state"] != "done"],
           "by_verdict": {k: [] for k in VERDICT_VALUES}, "owner_changed": [],
           "still_open": [], "reread": [], "pitfall_candidates": [], "stopped": stopped}
    for i in done:
        v, _ = _load(VERDICTS / ("%s.json" % i))
        out["by_verdict"][v["verdict"]].append(i)
        if v["open_question"].strip() or v["confidence"] != "high":
            out["still_open"].append({"id": i, "confidence": v["confidence"],
                                      "open_question": v["open_question"]})
        if v.get("_supersedes") or v.get("_reread"):
            out["reread"].append({"id": i, "changed": bool(v.get("_supersedes"))})
        if v["pitfall_candidate"].strip():
            out["pitfall_candidates"].append({"id": i, "pitfall": v["pitfall_candidate"]})
        if v["verdict"] != "holds":
            out["owner_changed"].append({"id": i, "owner": v["owner"],
                                         "corrected_claim": v["corrected_claim"]})
    return out


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("show").add_argument("id")
    sub.add_parser("mechanical")
    sub.add_parser("pending").add_argument("--all", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("--ids", nargs="*", default=None)
    p.add_argument("--json", action="store_true")
    sub.add_parser("rescan")
    p = sub.add_parser("report")
    p.add_argument("--date", required=True)
    p.add_argument("--stopped", default="")
    args = ap.parse_args()
    table = by_id()

    if args.cmd == "show":
        item = table.get(args.id)
        if item is None or item["kind"] != "judged":
            print("no judged item %s" % args.id, file=sys.stderr)
            return 1
        print(json.dumps({"id": item["id"], "claim": item["claim"],
                          "claim_sha1": sha1(item["claim"]),
                          "verdict_file": str(VERDICTS / ("%s.json" % item["id"]))},
                         ensure_ascii=False, indent=1))
        return 0
    if args.cmd == "mechanical":
        written, failed = mechanical()
        print(json.dumps({"written": written, "failed": failed}, ensure_ascii=False))
        return 1 if failed else 0
    if args.cmd == "pending":
        rows = states(items())
        judged = [r for r in rows if r["kind"] == "judged"]
        if args.all:
            print(json.dumps(judged, ensure_ascii=False, indent=1))
        else:
            print(json.dumps([r["id"] for r in judged if r["state"] != "done"], ensure_ascii=False))
        mech_bad = [r["id"] for r in rows if r["kind"] == "mechanical" and r["state"] != "done"]
        if mech_bad:
            print("mechanical items not done: " + " ".join(mech_bad), file=sys.stderr)
        return 0
    if args.cmd == "check":
        wanted = args.ids or list(table)
        unknown = [i for i in wanted if i not in table]
        rows = states([table[i] for i in wanted if i in table])
        result = {"checked": len(wanted), "ok": sum(r["state"] == "done" for r in rows),
                  "missing": [r["id"] for r in rows if r["state"] == "missing"] + unknown,
                  "failures": {r["id"]: r["problems"] for r in rows if r["state"] == "failing"}}
        result["gate_passed"] = not result["missing"] and not result["failures"]
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=1))
        else:
            print("checked=%d ok=%d missing=%d failing=%d" % (
                result["checked"], result["ok"], len(result["missing"]), len(result["failures"])))
            for i in result["missing"]:
                print("  MISSING  %s" % i)
            for i, problems in result["failures"].items():
                print("  FAIL     %s: %s" % (i, "; ".join(problems)))
            print("GATE %s" % ("PASSED" if result["gate_passed"] else "FAILED"))
        return 0 if result["gate_passed"] else 1
    if args.cmd == "rescan":
        print(json.dumps(rescan_todo(), ensure_ascii=False, indent=1))
        return 0
    if args.cmd == "report":
        summary = summarize(args.stopped)
        RUNS.mkdir(parents=True, exist_ok=True)
        stem = "%s-story-verify" % args.date
        written = [RUNS / ("%s-summary.json" % stem)]
        written[0].write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n",
                              encoding="utf-8")
        # A stopped run leaves a partial set that reads like a complete one (ADR-0007 5.6).
        if not args.stopped:
            archive = []
            for item in items():
                path = VERDICTS / ("%s.json" % item["id"])
                if path.is_file():
                    v, _ = _load(path)
                    if v is not None:
                        archive.append(dict(v, claim=item["claim"]))
            written.append(RUNS / ("%s-verdicts.json" % stem))
            written[1].write_text(json.dumps(archive, ensure_ascii=False, indent=1) + "\n",
                                  encoding="utf-8")
        print(json.dumps({"written": [str(p) for p in written], "complete": summary["complete"],
                          "total": summary["total"], "unfinished": summary["unfinished"],
                          "by_verdict": {k: len(v) for k, v in summary["by_verdict"].items()}},
                         ensure_ascii=False))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
