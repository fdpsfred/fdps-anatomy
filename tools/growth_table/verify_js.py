"""Check the published page's JavaScript against the Python generator, and
smoke-test that each of its three tabs renders, in headless Chrome or Edge.

The comparison runs the page's own level model -- the <script id="model">
block, cut out of the built page as it stands -- over the page's own embedded
data, for every join x route x promotion level, and compares every figure
with growth_data.json from gen_growth.  Nothing is re-implemented here.

The smoke test loads the built page once per tab (#rank, #compare, #detail)
and fails on any uncaught script error or when the tab draws nothing.  The
exercise then operates every control inside the page -- the promotion level
at 20, 30 and 40, every ranking toggle, every compare option and chip, every
character, join and route of the detail tab, and a mouse pass over each
chart -- and fails on any exception or when it stops early.

Usage:
    python tools/growth_table/verify_js.py [--browser PATH]

Run build_page.py first; this reads workspace/growth_table/.
"""
import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / "workspace" / "growth_table"
PAGE = WORK / "index.html"
FULL = WORK / "growth_data.json"
HARNESS = WORK / "verify_js.html"

BROWSERS = (
    Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Google/Chrome/Application/chrome.exe",
    Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / "Google/Chrome/Application/chrome.exe",
    Path(os.environ.get("LOCALAPPDATA", "")) / "Google/Chrome/Application/chrome.exe",
    Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / "Microsoft/Edge/Application/msedge.exe",
    Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Microsoft/Edge/Application/msedge.exe",
)


class VerifyError(Exception):
    pass


def find_browser(explicit=None):
    if explicit:
        return Path(explicit)
    for p in BROWSERS:
        if p.is_file():
            return p
    for name in ("chrome", "google-chrome", "chromium", "msedge"):
        found = shutil.which(name)
        if found:
            return Path(found)
    return None


def headless(browser, url, *extra, budget_ms=20000):
    """The page's DOM after its scripts ran, and the console lines Chrome logged."""
    with tempfile.TemporaryDirectory() as profile:
        cmd = [str(browser), "--headless=new", "--disable-gpu", "--no-first-run",
               "--no-default-browser-check", f"--user-data-dir={profile}",
               "--allow-file-access-from-files", "--enable-logging=stderr", "--v=0",
               f"--virtual-time-budget={budget_ms}", *extra, "--dump-dom", url]
        run = subprocess.run(cmd, capture_output=True, timeout=180)
    return run.stdout.decode("utf-8", "replace"), run.stderr.decode("utf-8", "replace")


def script_errors(stderr):
    return [line for line in stderr.splitlines()
            if "CONSOLE" in line and ("Uncaught" in line or "Error" in line)]


def cut(page, pattern, what):
    m = re.search(pattern, page, re.S)
    if not m:
        raise VerifyError(f"the built page has no {what}")
    return m.group(1)


COMPARE_JS = r"""
(function(){
  const D = JSON.parse(document.getElementById("growthData").textContent);
  const S = GROWTH.STATS; let values = 0; const bad = [];
  function cmp(js, py, where){
    if (js.length !== py.length){ bad.push(where + ": " + js.length + " rows vs " + py.length); return; }
    js.forEach((r, i) => {
      if (r.lv !== py[i].lv) bad.push(where + ": row " + i + " LV" + r.lv + " vs LV" + py[i].lv);
      for (const s of S) for (const v of ["min","max"]){
        values++;
        if (r[v][s] !== py[i][v][s]) bad.push(where + " LV" + r.lv + " " + s + "." + v + ": js " + r[v][s] + ", python " + py[i][v][s]);
      }
    });
  }
  const pyById = {}; PY.chars.forEach(c => pyById[c.id] = c);
  if (PY.chars.length !== D.chars.length) bad.push("character count " + D.chars.length + " vs " + PY.chars.length);
  for (const c of D.chars){
    const py = pyById[c.id];
    if (!py){ bad.push("no python rows for " + c.name); continue; }
    c.joins.forEach((j, ji) => {
      const pj = py.joins[ji];
      if (!pj || pj.level !== j.level){ bad.push(c.name + ": join " + ji + " differs"); return; }
      const base = GROWTH.baseRows(c, j.level);
      cmp(base, pj.base_rows, c.name + " join LV" + j.level + " base");
      const ps = GROWTH.promoteLevels(j.level, D.promote_min, D.cap_normal);
      if (pj.routes.length !== c.routes.length) bad.push(c.name + ": route count differs");
      c.routes.forEach((r, ri) => {
        const pr = pj.routes[ri];
        const keys = Object.keys(pr.by_promote_level).map(Number).sort((a, b) => a - b);
        if (keys.join() !== ps.join()) bad.push(c.name + " " + r.cls + ": promotion levels " + ps.join() + " vs " + keys.join());
        ps.forEach(p => {
          const carry = base.find(x => x.lv === p);
          cmp(GROWTH.promoRows(carry, r, D.cap_normal), pr.by_promote_level[p] || [], c.name + " join LV" + j.level + " " + r.cls + " at LV" + p);
        });
      });
    });
  }
  const pre = document.createElement("pre"); pre.id = "result";
  pre.textContent = JSON.stringify({values, mismatches: bad.slice(0, 50), count: bad.length});
  document.body.appendChild(pre);
})();
"""


def compare_model(browser):
    page = PAGE.read_text(encoding="utf-8")
    data = cut(page, r'<script id="growthData" type="application/json">(.*?)</script>', "embedded data")
    model = cut(page, r'<script id="model">(.*?)</script>', "model script")
    full = FULL.read_text(encoding="utf-8").replace("</", "<\\/")
    HARNESS.write_text(
        '<!doctype html><meta charset="utf-8"><body>\n'
        f'<script id="growthData" type="application/json">{data}</script>\n'
        f'<script>const PY = {full};</script>\n'
        f'<script>{model}</script>\n<script>{COMPARE_JS}</script>\n</body>\n', encoding="utf-8")
    dom, log = headless(browser, HARNESS.as_uri())
    m = re.search(r'<pre id="result">(.*?)</pre>', dom, re.S)
    if not m:
        raise VerifyError("the harness wrote no result; console:\n" + "\n".join(script_errors(log)[:10]))
    return json.loads(html.unescape(m.group(1)))


def expected_rank_rows():
    m = json.loads((WORK / "growth_compact.json").read_text(encoding="utf-8"))
    return len(m["stats"]) * sum(len(c["joins"]) * max(1, len(c["routes"])) for c in m["chars"])


SMOKE = (
    ("rank", lambda dom: dom.count('class="rrow"'), "ranking rows"),
    ("compare", lambda dom: dom.count('<svg class="chart"'), "compare chart"),
    ("detail", lambda dom: dom.count('<svg class="chart"') + dom.count('class="lvtable"'), "detail charts and table"),
)


def smoke(browser):
    failures = []
    for tab, measure, what in SMOKE:
        dom, log = headless(browser, PAGE.as_uri() + "#" + tab, "--window-size=1440,1000")
        errors = script_errors(log)
        if errors:
            failures.append(f"#{tab}: script errors: " + " | ".join(errors[:3]))
        n = measure(dom)
        want = expected_rank_rows() if tab == "rank" else (6 if tab == "detail" else 1)
        if n < want:
            failures.append(f"#{tab}: {n} {what}, expected at least {want}")
    return failures


EXERCISE_JS = r"""
<script>
(function(){
  const errors = []; let steps = 0;
  addEventListener("error", e => errors.push(String(e.message)));
  const click = el => { el.click(); steps++; };
  const all = sel => [...document.querySelectorAll(sel)];
  const hover = sel => all(sel).forEach(h => { const r = h.getBoundingClientRect();
    for (const f of [0.02, 0.5, 0.98]){ h.dispatchEvent(new MouseEvent("mousemove", {clientX: r.left + r.width * f, clientY: r.top + r.height / 2, bubbles: true})); steps++; }
    h.dispatchEvent(new MouseEvent("mouseleave")); });
  try {
    const slider = document.getElementById("promoteAt");
    for (const p of [20, 30, 40]){
      slider.value = p; slider.dispatchEvent(new Event("input")); steps++;
      click(document.getElementById("tab-rank"));
      all("#rankGrid .seg button").forEach(b => { const k = b.parentElement.dataset.kind, s = b.parentElement.dataset.stat, v = b.dataset.v;
        const again = document.querySelector('.rcol[data-stat="' + s + '"] .seg[data-kind="' + k + '"] button[data-v="' + v + '"]'); if (again) click(again); });
      all("#rankStatPick button").forEach(b => { const s = b.dataset.s; click(document.querySelector('#rankStatPick button[data-s="' + s + '"]')); });
      click(document.getElementById("tab-compare"));
      all("#pickList .chip").forEach(c => { const k = c.dataset.key; const again = document.querySelector('#pickList .chip[data-key="' + k + '"]'); if (again && !again.disabled) click(again); });
      for (const v of ["max","min","band"]){
        click(document.querySelector('#cmpVer button[data-v="' + v + '"]'));
        all("#cmpStat button").forEach(b => click(document.querySelector('#cmpStat button[data-s="' + b.dataset.s + '"]')));
        hover("#cmpChart .hit");
      }
      click(document.getElementById("cmpTable")); click(document.getElementById("cmpTable"));
      click(document.getElementById("cmpClear"));
      click(document.getElementById("tab-detail"));
      all("#roster button").forEach(r => {
        click(document.querySelector('#roster button[data-id="' + r.dataset.id + '"]'));
        hover("#multiples .hit");
        all(".joinpick button").forEach(j => { click(document.querySelector('.joinpick button[data-j="' + j.dataset.j + '"]'));
          all(".routetabs button").forEach(x => click(document.querySelector('.routetabs button[data-r="' + x.dataset.r + '"]'))); });
        all(".routetabs button").forEach(x => click(document.querySelector('.routetabs button[data-r="' + x.dataset.r + '"]')));
      });
      click(document.getElementById("themeBtn")); click(document.getElementById("themeBtn"));
    }
  } catch (e) { errors.push(String(e && e.stack || e)); }
  const pre = document.createElement("pre"); pre.id = "exercise";
  pre.textContent = JSON.stringify({steps, errors});
  document.body.appendChild(pre);
})();
</script>
"""


def exercise(browser):
    """Drive every control of the page; returns (steps taken, errors)."""
    page = PAGE.read_text(encoding="utf-8")
    target = WORK / "verify_exercise.html"
    target.write_text(page.replace("</body>", EXERCISE_JS + "</body>"), encoding="utf-8")
    dom, log = headless(browser, target.as_uri(), "--window-size=1440,1000", budget_ms=60000)
    m = re.search(r'<pre id="exercise">(.*?)</pre>', dom, re.S)
    if not m:
        return 0, ["the exercise wrote no result"] + script_errors(log)[:5]
    result = json.loads(html.unescape(m.group(1)))
    return result["steps"], result["errors"] + script_errors(log)


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--browser")
    args = parser.parse_args(argv)
    browser = find_browser(args.browser)
    if browser is None:
        print("error: no Chrome or Edge found (pass --browser)", file=sys.stderr)
        return 2
    if not PAGE.is_file() or not FULL.is_file():
        print("error: run tools/growth_table/build_page.py first", file=sys.stderr)
        return 2
    try:
        result = compare_model(browser)
        failures = smoke(browser)
        steps, errors = exercise(browser)
        # Three promotion levels over every control come to about 290 steps;
        # far fewer means the exercise stopped early.
        if errors or steps < 200:
            failures.append(f"exercise: {steps} steps, errors: " + " | ".join(errors[:3]))
    except (VerifyError, OSError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    for m in result["mismatches"]:
        print("MISMATCH " + m)
    for f in failures:
        print("SMOKE " + f)
    if result["count"] or failures:
        print(f"FAIL: {result['count']} of {result['values']} values differ; {len(failures)} smoke failures")
        return 1
    print(f"OK: page JavaScript == Python on {result['values']} values; all three tabs render, "
          f"and {steps} control interactions raise no script error")
    return 0


if __name__ == "__main__":
    sys.exit(main())
