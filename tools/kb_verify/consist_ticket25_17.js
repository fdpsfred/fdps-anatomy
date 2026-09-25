// Ticket 25.17, stage 2: cross-document consistency, one agent per item.
//
// Stage 1 (verify_ticket25_17.js) verifies each document on its own.  What it
// cannot see by construction: a fact corrected in one page but still wrong in
// another, the same fact owned by two pages, and contradictions a verifier
// only noticed in passing.  kbconsist.py finds the candidates mechanically --
// the anchors (addresses, fdps_ symbols, file names) of every edit stage 1
// landed, near-duplicate passages by character 4-grams, the verifiers'
// cross_doc notes -- plus the cut_content/ reclassifications the ticket lists,
// and freezes them into an item list.  Each item goes to one agent.
//
// Run it after stage 1's edits are landed ("kbverify.py apply" and the gates),
// because the propagation items are built from what was landed.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One item per agent; the worklist lives only in this script (from
//   "kbconsist.py pending --all"); an agent reads its item through
//   "kbconsist.py show <ID>".
//
//   Verdicts go to files, agents return ~200 bytes; the report is built from
//   the files, so a rerun resumes.
//
//   Judging agents write nothing but their verdict file.  Edits are
//   exact-string replacements in named pages; the Land phase runs
//   "kbconsist.py apply", which lands only what a second reader confirmed or
//   amended, then every knowledge-base gate.
//
// args: {
//   date:       "YYYY-MM-DD"  required; names the devlog/runs files
//   roundSize:  number        items judged per round (default 10)
//   exclude:    [path, ...]   pages whose edits must wait; an item touching one
//                             is refused by name and landed by a later apply
// }

export const meta = {
  name: 'kb-consist-ticket25-17',
  description: 'Cross-document consistency of the knowledge base, one agent per candidate group',
  phases: [
    { title: 'Plan', detail: 'freeze the candidate items, read which still need a verdict' },
    { title: 'Judge', detail: 'one agent per item, verdict with exact-string edits written to a file' },
    { title: 'Gate', detail: 'verdict shape, evidence, edits unique and outside generated regions' },
    { title: 'Rescan', detail: 'a second agent confirms, amends or rejects every change' },
    { title: 'Land', detail: 'apply the confirmed edits, regenerate the cut_content summary, run every gate' },
    { title: 'Report', detail: 'closing report and verdict archive in devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOL = REPO + '\\tools\\kb_verify\\kbconsist.py'
const RUN = 'python ' + TOOL
const VERDICTS = REPO + '\\workspace\\kb_consist\\verdicts'
const SCRATCH = REPO + '\\workspace\\kb_consist\\scratch'

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['ids', 'todo', 'ok'],
  properties: {
    ids: { type: 'array', items: { type: 'string' } },
    todo: { type: 'array', items: { type: 'string' } },
    ok: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'verdict', 'edits', 'wrote_file', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    verdict: { type: 'string', enum: ['consistent', 'fixed', 'cannot_settle'] },
    edits: { type: 'integer' },
    wrote_file: { type: 'boolean' },
    upstream_dead: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const GATE = {
  type: 'object',
  additionalProperties: false,
  required: ['checked', 'ok_count', 'failing', 'missing', 'gate_passed'],
  properties: {
    checked: { type: 'integer' },
    ok_count: { type: 'integer' },
    failing: { type: 'array', items: { type: 'string' } },
    missing: { type: 'array', items: { type: 'string' } },
    gate_passed: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const RESCAN_PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['todo', 'ok'],
  properties: {
    todo: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['id', 'why'],
        properties: { id: { type: 'string' }, why: { type: 'string' } },
      },
    },
    ok: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const RESCAN = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'decision', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    decision: { type: 'string', enum: ['confirm', 'amend', 'reject'] },
    upstream_dead: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const DONE = {
  type: 'object',
  additionalProperties: false,
  required: ['written', 'summary'],
  properties: {
    written: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
  },
}

const LAND = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'refused', 'gates', 'all_passed'],
  properties: {
    applied: { type: 'integer', description: 'The "applied" count apply printed' },
    refused: { type: 'array', items: { type: 'string' }, description: 'One line per refused entry: id: why' },
    gates: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['command', 'passed', 'detail'],
        properties: {
          command: { type: 'string' },
          passed: { type: 'boolean', description: 'True when the command exited 0' },
          detail: { type: 'string', description: 'Its last line, or the failure lines condensed' },
        },
      },
    },
    lint_findings: { type: 'integer', description: 'The count printed by kbverify.py lint (informational)' },
    all_passed: { type: 'boolean' },
  },
}

// Run after the landing.  cut_content.py index comes first: a reclassified
// entry moves between sections and the summary table is regenerated from the
// topic pages, never edited by hand.
const AFTER_APPLY = ['python ' + REPO + '\\tools\\cut_content\\cut_content.py index']
const GATES = [
  'python ' + REPO + '\\tools\\chapter_docs\\check_chapter.py --landed-all',
  'python ' + REPO + '\\tools\\chapter_docs\\index.py verify',
  'python ' + REPO + '\\tools\\data_tables\\data_tables.py check',
  'python ' + REPO + '\\tools\\global_text\\global_text.py verify',
  'python ' + REPO + '\\tools\\cut_content\\cut_content.py check',
  'python ' + REPO + '\\tools\\cut_content\\story.py check --final',
  'python ' + REPO + '\\tools\\data_skill\\build.py',
  'python ' + REPO + '\\tools\\kb_verify\\kbverify.py indexes',
]

function landPrompt(applyCommand) {
  return `Land the ticket-25.17 consistency verdicts and run the knowledge-base
gates.  You decide nothing and you fix nothing.

Working directory: ${REPO}

Run, in this order, each exactly as written:

  ${applyCommand}
${AFTER_APPLY.map((c) => '  ' + c).join('\n')}

apply prints JSON with "applied" (a count) and "refused" (a list).  Report
applied, and one line per refused entry ("<id>: <why>"); an entry whose why is
"excluded this run" is expected, list it anyway.

Then run every one of these and record, for each, whether it exited 0 and its
last line (or, when it failed, its failure lines condensed):

${GATES.map((c) => '  ' + c).join('\n')}

Finally run  python ${REPO}\\tools\\kb_verify\\kbverify.py lint  and report the
count it prints in lint_findings (a non-zero exit there is not a gate failure).

all_passed is true only if every gate exited 0.  Do not edit any file, do not
re-run apply, do not try to make a failing gate pass.`
}

const RESCAN_RETRY = `

This is a second attempt: the verdict file already has a "_reread" field that
fails the gate.  Run the check command first to see why, then rewrite the
"_reread" field whole (keep every other field as it is).`

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\ -- the complete rebuilt C source of FDPS.LE.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE (load tools with one ToolSearch call,
     e.g. "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__read_memory").
     Never call a Ghidra tool that writes.  ${REPO}\\ghidra_snapshot\\functions.txt
     lists every function with its address, pool tag and prototype.
  3. The game's data: ${REPO}\\fdps_game_files\\ and ${REPO}\\workspace\\vfs_dump\\,
     through the decoders under ${REPO}\\tools\\ (each tool's _index.md says how).
     Cite FILE@offset, or the command you ran (source "tool").
  4. Corroboration only: the guide mirror (python ${REPO}\\tools\\guide_scrape\\guide_scrape.py
     search <pattern> --game fdps), the FD2 project C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\,
     and the knowledge base itself (source "kb").

Ownership: README.md says each fact has exactly one owner -- the page whose
folder _index.md and whose own scope say so (program_info/ for what the
program does, resource_info/ for file formats, assets/ for the numbers,
chapters/ for per-chapter content, chapters/_index.md for cross-chapter facts,
cut_content/ for cut and unused content with cut_content/_index.md "與其他正典的分工",
rebuild_info/pitfalls.md for "a rebuild would get this wrong" with a link to
the owner).  Generated pages (assets/text/*.md except _index.md) and the
<!-- chapter_docs:... --> / <!-- story:... --> regions are written by tools;
a fix there goes into outside, naming the generator.`

const VERDICT_SHAPE = `{
  "id": "<the item id>",
  "item_sha1": "<the item_sha1 printed by show, verbatim>",
  "verdict": "consistent" | "fixed" | "cannot_settle",
  "conclusion": "<繁體中文：查證後的事實，結論式，一到三句>",
  "evidence": [ { "source": "src" | "ghidra" | "data" | "tool" | "kb" | "guide" | "fd2",
                  "location": "...", "observation": "..." } ],
  "edits": [ { "doc": "<repo-relative page path>", "old": "<exact text copied from that file, unique in it>",
               "new": "<replacement>", "why": "<繁體中文，一句>" } ],
  "outside": "<fixes a page edit cannot make: a generator in tools/, a src/ comment, Ghidra; say exactly where and what; else empty>",
  "confidence": "high" | "medium" | "low",
  "open_question": "<what stays unsettled, else empty>",
  "developer_question": "<only when it needs the developer (runtime, playtest, a decision), else empty>"
}`

function judgePrompt(id) {
  return `You are settling ONE cross-document consistency item in the FDPS
reverse-engineering knowledge base (炎龍騎士團外傳, a 1997 DOS game whose
executable FDPS.LE has been fully rebuilt into C).  Every page was verified on
its own; this item is about how pages agree with each other.

Your item: ${id}

Get it by running exactly:

  ${RUN} show ${id}

It prints the item_sha1 and what the item is: a corrected fact whose anchors
appear in other places (propagation), passages that read alike (duplicate), a
contradiction a verifier noticed (conflict), a pitfall candidate (pitfall), or
a cut_content/ placement to look at again (reclass).  Work on this item only;
other agents work on other items at the same time.

  - propagation: for each listed line (and any other place you find by
    grepping the knowledge base for the same anchors), decide whether it
    carries the same wrong fact or contradicts the corrected one.  Fix those;
    leave lines that are right.
  - duplicate: decide whether it is one fact written in several places.  If
    so, keep it in the owner and cut the others down to a sentence with a
    link; if the passages say different things, make them agree with the
    evidence.  Passages that are each right and say different things stay.
  - conflict: settle which statement is true from first-hand evidence and fix
    the other page(s).
  - pitfall: check the candidate against the code; add it to
    rebuild_info/pitfalls.md only if it clears the bar in rebuild_info/_index.md
    and is not there already (show says how the edit is written).
  - reclass: apply CONTEXT.md "刪減與未用" and cut_content/_index.md "分類" to
    the entry and decide its class from the evidence.  If it changes, the edits
    move the entry to the right section of its topic page (or to the exclusion
    list) and change its 分類 line; the summary table is regenerated by the
    ticket session (cut_content.py index), so do not edit it; an OWNERS change in
    tools/cut_content/story.py goes into outside.

${SOURCES}

Edits are exact-string replacements: old copied from the page file itself,
occurring exactly once in it, outside generated regions; new in the page's
own style (conclusion-style Traditional Chinese, no process narrative, no src/
line numbers, game names as the game writes them).  An item whose pages
already agree is verdict consistent with no edits.  Do not rewrite for style.

Scratch files go under ${SCRATCH}\\${id}\\ and nowhere else.  Do not modify any
file in the repository other than your verdict file.  READ-ONLY on Ghidra.

WRITE YOUR VERDICT with the Write tool to exactly:

  ${VERDICTS}\\${id}.json

UTF-8 JSON of this shape:

${VERDICT_SHAPE}

The gate (run it yourself before you finish and fix what it reports):

  ${RUN} check --ids ${id}

Set upstream_dead only if Ghidra stops answering or python / the repo is
unusable.  Then return the summary; your final output is data for the
workflow, not a message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.17 consistency gate over one round.

Round tag: ${tag}
Items (${ids.length}): ${ids.join(' ')}

Run exactly:

  ${RUN} check --ids ${ids.join(' ')} --json

Report checked, ok (as ok_count), every id under "failures" in failing, every
id under "missing" in missing, and gate_passed; condense the failure reasons
into problems.  Do not fix anything.  If the command cannot run, say so and set
gate_passed false.`
}

function rescanPrompt(id, why) {
  return `A first agent settled ONE cross-document consistency item in the FDPS
knowledge base.  You are the second reader; none of its edits reaches the
knowledge base unless you confirm or amend them.

Your item:   ${id}
Why:         ${why}
The item:    ${RUN} show ${id}
The verdict: ${VERDICTS}\\${id}.json

First check the item yourself against first-hand sources, then read the
verdict and judge it: is the conclusion true, does every edit fix a real
problem, is every new text true, conclusion-style and no vaguer than the facts,
does the ownership come out with exactly one owner, is the outside fix right?

${SOURCES}

Add to the verdict file, with the Write tool, one field and change nothing else:

  "_reread": { "decision": "confirm" | "amend" | "reject",
               "why": "<繁體中文：你查了什麼、結論>",
               "edits": [ ... ] }      (amend only: the full corrected edit list, same shape as edits)

confirm = everything stands; amend = the problem is real but the edits need
changing (give the complete list that should land); reject = nothing should
change.  Run ${RUN} check --ids ${id} afterwards and fix what it reports in
your own file.  READ-ONLY on everything else.  A second reading is not a
licence to manufacture a conclusion.  Set upstream_dead only if Ghidra stops
answering or python / the repo is unusable.`
}

const DATE = args && args.date
const ROUND_SIZE = (args && args.roundSize) || 10
const EXCLUDE = (args && Array.isArray(args.exclude)) ? args.exclude : []

if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { stopped: 'args.date (YYYY-MM-DD) is required; it names the devlog/runs files' }
}

phase('Plan')
const plan = await agent(
  `Prepare the ticket-25.17 consistency worklist.  You decide nothing.

Run exactly these two commands, in this order:

  ${RUN} freeze
  ${RUN} pending --all

The first builds the item list the first time and keeps it afterwards; it
prints a JSON digest.  The second prints a JSON array of {id, state}.  Return
ids = every id in order, todo = every id whose state is not "done", in order.
If a command cannot run at all, set ok false and say why in note.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)
if (!plan || !plan.ok) {
  return { stopped: 'the planning agent could not read the worklist', note: plan ? plan.note : 'no response' }
}
const IDS = plan.ids
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} items, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

const unfinished = []
const gateReports = []
let stopped = null

for (let start = 0; start < todo.length && !stopped; start += ROUND_SIZE) {
  const round = Math.floor(start / ROUND_SIZE) + 1
  const batch = todo.slice(start, start + ROUND_SIZE)

  phase('Judge')
  log(`round ${round}: judging ${batch.join(' ')}`)
  const results = (await parallel(batch.map((id) => () =>
    agent(judgePrompt(id), { label: `judge:${id}`, phase: 'Judge', schema: SUMMARY })
  ))).filter(Boolean)

  if (results.length === 0 && batch.length > 0) {
    stopped = `round ${round} returned nothing at all; treating it as an upstream failure`
    unfinished.push(...todo.slice(start))
    break
  }
  if (results.some((r) => r.upstream_dead)) {
    stopped = `an agent in round ${round} reported Ghidra or the repo unusable: `
      + results.filter((r) => r.upstream_dead).map((r) => `${r.id} ${r.note || ''}`).join('; ')
    unfinished.push(...todo.slice(start))
    break
  }

  phase('Gate')
  let gate = await agent(gatePrompt(`r${round}`, batch), { label: `gate:round${round}`, phase: 'Gate', schema: GATE })
  if (!gate) {
    gate = { checked: batch.length, ok_count: 0, failing: [], missing: batch,
             gate_passed: false, problems: 'the gate agent returned nothing' }
  }
  gateReports.push(Object.assign({ round: round }, gate))
  log(`round ${round}: gate ok=${gate.ok_count} failing=${gate.failing.length} missing=${gate.missing.length}`)

  const retry = [].concat(gate.failing || [], gate.missing || []).filter((i) => batch.includes(i))
  if (retry.length > 0) {
    if (gate.problems) {
      log(`round ${round}: gate said: ${gate.problems}`)
    }
    log(`round ${round}: retrying ${retry.join(' ')}`)
    const second = (await parallel(retry.map((id) => () =>
      agent(judgePrompt(id), { label: `retry:${id}`, phase: 'Judge', schema: SUMMARY })
    ))).filter(Boolean)
    if (second.length === 0) {
      stopped = `the retries of round ${round} returned nothing at all; treating it as an upstream failure`
      unfinished.push(...retry, ...todo.slice(start + ROUND_SIZE))
      break
    }
    if (second.some((r) => r.upstream_dead)) {
      stopped = `a retry in round ${round} reported Ghidra or the repo unusable`
      unfinished.push(...retry, ...todo.slice(start + ROUND_SIZE))
      break
    }
    const regate = await agent(gatePrompt(`r${round}b`, retry), { label: `gate:round${round}b`, phase: 'Gate', schema: GATE })
    if (regate) {
      gateReports.push(Object.assign({ round: round + 'b' }, regate))
      const stillBad = [].concat(regate.failing || [], regate.missing || [])
      if (stillBad.length > 0) {
        log(`round ${round}: UNFINISHED after retry: ${stillBad.join(' ')}`)
        unfinished.push(...stillBad)
      }
    } else {
      unfinished.push(...retry)
    }
  }
}

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= 2 && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.17 consistency rescan worklist.  You decide nothing.

Run exactly:

  ${RUN} rescan

It prints a JSON array of {id, why}.  Return todo = that array as printed.  If
the command cannot run at all, set ok false and say why in note.`,
      { label: `rescanplan:pass${pass}`, phase: 'Plan', schema: RESCAN_PLAN }
    )
    if (!plan2 || !plan2.ok) {
      stopped = `rescan pass ${pass}: could not read the rescan worklist`
      break
    }
    const pending = plan2.todo.filter((t) => IDS.includes(t.id))
    if (pending.length === 0) {
      log(`rescan pass ${pass}: nothing to re-read`)
      break
    }
    for (let start = 0; start < pending.length && !stopped; start += ROUND_SIZE) {
      const batch = pending.slice(start, start + ROUND_SIZE)
      phase('Rescan')
      log(`rescan pass ${pass}: re-reading ${batch.map((t) => t.id).join(' ')}`)
      const results = (await parallel(batch.map((t) => () =>
        agent(rescanPrompt(t.id, t.why), { label: `rescan:${t.id}`, phase: 'Rescan', schema: RESCAN })
      ))).filter(Boolean)
      if (results.length === 0) {
        stopped = `a rescan round of pass ${pass} returned nothing at all; treating it as an upstream failure`
        break
      }
      if (results.some((r) => r.upstream_dead)) {
        stopped = `a rescan agent in pass ${pass} reported Ghidra or the repo unusable`
        break
      }
      for (const r of results) {
        rescanLog.push(`${r.id}: ${r.decision}${r.note ? ' -- ' + r.note : ''}`)
      }
      phase('Gate')
      const gate = await agent(gatePrompt(`rescan${pass}.${start}`, batch.map((t) => t.id)), {
        label: `gate:rescan${pass}.${start}`, phase: 'Gate', schema: GATE,
      })
      if (gate) {
        gateReports.push(Object.assign({ round: `rescan${pass}.${start}` }, gate))
        const bad = [].concat(gate.failing || [], gate.missing || [])
        if (bad.length > 0) {
          // 5.1 -- one retry for a second reading that broke the gate.
          log(`rescan pass ${pass}: failing the gate after the second reading, retrying: ${bad.join(' ')}`)
          const again = batch.filter((t) => bad.includes(t.id))
          const second = (await parallel(again.map((t) => () =>
            agent(rescanPrompt(t.id, t.why) + RESCAN_RETRY, { label: `rescan-retry:${t.id}`, phase: 'Rescan', schema: RESCAN })
          ))).filter(Boolean)
          const regate = second.length > 0
            ? await agent(gatePrompt(`rescan${pass}.${start}b`, again.map((t) => t.id)), {
              label: `gate:rescan${pass}.${start}b`, phase: 'Gate', schema: GATE })
            : null
          const stillBad = regate ? [].concat(regate.failing || [], regate.missing || []) : bad
          if (regate) {
            gateReports.push(Object.assign({ round: `rescan${pass}.${start}b` }, regate))
          }
          if (stillBad.length > 0) {
            log(`rescan pass ${pass}: UNFINISHED after retry: ${stillBad.join(' ')}`)
            unfinished.push(...stillBad.filter((i) => !unfinished.includes(i)))
          }
        }
      } else {
        log(`rescan pass ${pass}: the gate returned nothing; its items are checked again by the report`)
      }
    }
  }
}

// Transcription and every knowledge-base gate; not run after a stop (5.6).
let landing = null
if (!stopped) {
  phase('Land')
  landing = await agent(landPrompt(`${RUN} apply${EXCLUDE.length ? ' --exclude ' + EXCLUDE.join(' ') : ''}`),
    { label: 'land:apply+gates', phase: 'Land', schema: LAND })
  if (!landing) {
    log('LAND: the landing agent returned nothing; run kbconsist.py apply and the gates by hand')
  } else {
    log(`land: ${landing.applied} edits applied, ${landing.refused.length} refused, gates ${landing.all_passed ? 'all passed' : 'FAILED'}`)
    for (const g of landing.gates.filter((x) => !x.passed)) {
      log(`  GATE FAILED: ${g.command}: ${g.detail}`)
    }
  }
}

phase('Report')
const stopArg = stopped ? ` --stopped "${stopped.replace(/[^A-Za-z0-9 .,:;()_\-]/g, ' ')}"` : ''
const report = await agent(
  `Write the ticket-25.17 consistency closing report.  You decide nothing.

Run exactly:

  ${RUN} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-kb-consist-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-kb-consist-verdicts.json`} and prints a one-line
JSON digest.  Return written = the files it names, summary = the digest
verbatim.  If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}
if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  items: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescan: rescanLog,
  gateReports: gateReports,
  landing: landing,
  closingReport: report ? report.summary : null,
  written: report ? report.written : [],
}
