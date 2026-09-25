// Ticket 25.17, stage 1: verify every hand-written knowledge-base document
// claim by claim, one agent per unit, with no human in the loop.
//
// A unit is one document, or one slice of a long document cut at its ##
// sections (kbverify.py decides the cut and freezes it on the first run).
// Generated content -- the three assets/text pages, the tables
// tools/data_tables writes, the generated blocks of the chapter pages and of
// chapters/_index.md, cut_content/ and the Ghidra snapshot files -- is not in
// the worklist: its own gates compare it with the data cell by cell, and the
// ticket session runs those gates.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One unit per agent.  The worklist comes from "kbverify.py pending --all"
//   and lives only in this script; every judging agent is given exactly one
//   unit id and reads that one unit through "kbverify.py show <ID>".
//
//   Verdicts go to files, agents return ~200 bytes.  The closing report is
//   built by kbverify.py from the files, so an interrupted run resumes by
//   being run again: a unit whose verdict passes the gate is done.
//
//   Judging agents write nothing but their verdict file: not the knowledge
//   base, not src/, not tools/, not Ghidra.  Every problem is a finding with an
//   exact-string edit; landing the edits is a separate, non-judging phase at
//   the end ("kbverify.py apply", then every knowledge-base gate).  Fixes
//   outside the pages (src/ comments, Ghidra, generators) are listed in the
//   closing report for the ticket session.
//
//   Two keys on every change: a second agent re-reads every finding that
//   carries an edit, a fix outside the page, low confidence or an
//   unverifiable claim, and confirms, amends or rejects it.  apply lands only
//   what the second reader confirmed or amended.
//
// args: {
//   date:       "YYYY-MM-DD"  required; names the devlog/runs files
//   roundSize:  number        units judged per round (default 10)
//   exclude:    [path, ...]   pages whose edits must wait (another ticket is
//                             changing them); apply refuses them by name and a
//                             later "kbverify.py apply" lands them
// }

export const meta = {
  name: 'kb-verify-ticket25-17',
  description: 'Verify every hand-written knowledge-base document claim by claim, one agent per unit',
  phases: [
    { title: 'Plan', detail: 'freeze the cut, read which units still need a verdict' },
    { title: 'Judge', detail: 'one agent per unit, verdict with exact-string edits written to a file' },
    { title: 'Gate', detail: 'verdict shape, first-hand evidence, edits unique and inside the unit' },
    { title: 'Rescan', detail: 'a second agent confirms, amends or rejects every flagged finding' },
    { title: 'Land', detail: 'apply the confirmed edits, then every knowledge-base gate' },
    { title: 'Report', detail: 'closing report and verdict archive in devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOL = REPO + '\\tools\\kb_verify\\kbverify.py'
const RUN = 'python ' + TOOL
const VERDICTS = REPO + '\\workspace\\kb_verify\\verdicts'
const SCRATCH = REPO + '\\workspace\\kb_verify\\scratch'

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['ids', 'todo', 'ok'],
  properties: {
    ids: { type: 'array', items: { type: 'string' }, description: 'Every id printed by pending --all, in order' },
    todo: { type: 'array', items: { type: 'string' }, description: 'Ids whose state is not done, in order' },
    ok: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'claims_checked', 'findings', 'edits', 'wrote_file', 'upstream_dead'],
  properties: {
    id: { type: 'string', description: 'The unit id, exactly as given' },
    claims_checked: { type: 'integer' },
    findings: { type: 'integer', description: 'How many problems were found' },
    edits: { type: 'integer', description: 'How many findings carry an edit' },
    wrote_file: { type: 'boolean' },
    upstream_dead: { type: 'boolean', description: 'True only if Ghidra stopped answering, or python / the repo is unusable' },
    note: { type: 'string', description: 'At most one short line, or empty' },
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
    problems: { type: 'string', description: 'Condensed, one line per kind of problem' },
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
        required: ['id', 'findings'],
        properties: { id: { type: 'string' }, findings: { type: 'array', items: { type: 'integer' } } },
      },
    },
    ok: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const RESCAN = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'confirmed', 'amended', 'rejected', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    confirmed: { type: 'integer' },
    amended: { type: 'integer' },
    rejected: { type: 'integer' },
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
    refused: { type: 'array', items: { type: 'string' }, description: 'One line per refused entry apply printed: unit/n: why' },
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

// The knowledge-base gates, run after every landing.  lint is informational:
// a finding a verifier judged to stand (a glossary entry naming devlog/) stays
// in its output, so the ticket session reviews what is left by hand.
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

function landPrompt(applyCommand, before) {
  return `Land the ticket-25.17 verdicts and run the knowledge-base gates.  You
decide nothing and you fix nothing.

Working directory: ${REPO}

Run, in this order, each exactly as written:

${before.concat([applyCommand]).map((c) => '  ' + c).join('\n')}

apply prints JSON with "applied" (a count) and "refused" (a list).  Report
applied, and one line per refused entry ("<unit or id>/<n>: <why>").  An entry
whose why is "excluded this run" is expected; list it anyway.

Then run every one of these and record, for each, whether it exited 0 and its
last line (or, when it failed, its failure lines condensed):

${GATES.map((c) => '  ' + c).join('\n')}

Finally run  python ${REPO}\\tools\\kb_verify\\kbverify.py lint  and report the
count it prints in lint_findings (it exits non-zero while any finding is left;
that is not a gate failure).

all_passed is true only if every gate exited 0.  Do not edit any file, do not
re-run apply, do not try to make a failing gate pass.`
}

// ---------------------------------------------------------------- prompts

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\ -- the complete rebuilt C (and three .asm) source of
     FDPS.LE; every function is emitted with comments and builds into a game
     the developer has played.  Primary source for behaviour.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE.  Load tools in one ToolSearch call, e.g.
       ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__get_function_by_address,mcp__ghidra__read_memory,mcp__ghidra__search_functions"
     Never call a Ghidra tool that writes (rename, comment, prototype, label,
     bookmark, tag, save).  ${REPO}\\ghidra_snapshot\\functions.txt has every
     function's address, size, calling convention, pool tag and prototype.
  3. The game's own data: ${REPO}\\fdps_game_files\\ and
     ${REPO}\\workspace\\vfs_dump\\<CONTAINER>\\ (every .VFS unpacked).  Use
     the decoders under ${REPO}\\tools\\ rather than reading bytes by hand:
     text_decode, cutscene_script, map_decode, global_text, data_tables,
     cel_decode, saf_decode, vfs_dump, save_format, chapter_docs/chapter_facts.py,
     call_graph (workspace\\call_graph\\report.md is freshly regenerated).
     Each tool's _index.md says how to run it.  Cite FILE@offset, or the
     command you ran (source "tool", location = the command).
  4. Corroboration only: the guide mirror (python
     ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps),
     the FD2 project C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\ for anything about
     FD2, and the rest of the knowledge base (source "kb").`

const VERDICT_SHAPE = `{
  "unit": "<the unit id>",
  "unit_sha1": "<the unit_sha1 printed by show, verbatim>",
  "claims_checked": <how many distinct claims you checked, an integer>,
  "summary": "<繁體中文，一句：查了什麼、幾條有問題>",
  "findings": [
    {
      "n": 1,
      "kind": "wrong_fact" | "imprecise" | "stale_count" | "broken_reference" | "narrative" |
              "forbidden_reference" | "duplicate" | "owner_violation" | "unverifiable" | "other",
      "quote": "<the claim as the page states it, copied>",
      "problem": "<繁體中文：錯在哪、正確的是什麼>",
      "evidence": [ { "source": "src" | "ghidra" | "data" | "tool" | "kb" | "guide" | "fd2",
                      "location": "src/battle.c:120 | 0x1ecc7 | FIELD.VFS/MAP27.DAT@0x53 | python tools/... | ...",
                      "observation": "<what is there>" } ],
      "confidence": "high" | "medium" | "low",
      "edit": { "old": "<exact text copied from the FILE, unique in the whole file>",
                "new": "<the replacement>" } | null,
      "outside": "<a fix this page cannot hold: a src/ comment, a Ghidra plate, a generator in tools/, another page -- say exactly where and what; else empty>",
      "developer_question": "<only when settling it needs the developer: runtime values, a playtest, a decision; else empty>"
    }
  ],
  "cross_doc": [ { "other_doc": "<repo-relative path>", "anchor": "<the address / symbol / file / number both pages talk about>",
                   "note": "<繁體中文：兩邊各怎麼說、哪邊對（若查過）>" } ],
  "pitfall_candidates": [ "<繁體中文：重建時照直覺寫就會與原版不同、而 rebuild_info/pitfalls.md 還沒收的事>" ]
}`

function judgePrompt(id) {
  return `You are verifying ONE slice of the FDPS reverse-engineering knowledge base
(炎龍騎士團外傳, a 1997 DOS game whose executable FDPS.LE has been fully
rebuilt into C).  The knowledge base states conclusions; the project's rule is
that every conclusion must hold against the code, Ghidra and the game files.
It was written by many agents in parallel and has drifted.  Find the drift.

Your unit: ${id}

Get it by running exactly:

  ${RUN} show ${id}

It prints the document path, your line range, the unit_sha1, your lines
numbered (generated regions folded into one line -- they are checked by their
own gates and are not yours), and the findings of the deterministic lint in
your lines.  This unit is the only thing you work on; other agents verify the
other slices at the same time.  Read the whole document once for context, but
judge only your own lines.

What to check, for every sentence and every table row in your lines:

  - Facts: function names and addresses (the snapshot / Ghidra), what a
    function does (src/ and Ghidra), numbers, counts, offsets, sizes, lists
    (recount them from the data or the code -- do not trust a number because
    it looks precise), file names, which chapter / map / entry, formulas.
  - Links and references: every relative link points at the page and section
    that owns the fact; the linked page really says it.
  - Ownership: README.md says each fact has exactly one owner.  A page that
    restates at length a fact another page owns (and especially one that
    restates it differently) is an owner_violation; the fix is usually to cut
    it down to a sentence and a link.
  - Conclusion style: the knowledge base states what is true, never how it
    was found -- no analysis narrative, no "後來發現", no ticket numbers, no
    dates, no "目前／尚未" status talk that only describes the project's
    progress, no citation of workspace/ or legacy/ content (kind narrative /
    forbidden_reference).  A tool's own output path in a sentence that
    describes that tool is not a citation of workspace content, but ask
    whether that sentence belongs in tools/<tool>/_index.md instead.
  - Every lint finding printed by show must be resolved by a finding of yours:
    an edit, or -- when the text is right as it stands (a glossary entry that
    names devlog/, an example symbol) -- kind other, edit null, outside empty,
    and the reason it stands in problem.  A citation written
    \`name\`（\`0xaddr\`） must name the function's entry point; an address
    inside a function is written "\`name\` 內的 \`0xaddr\`".

${SOURCES}

For each problem write one finding.  Put the fix in edit as an exact-string
replacement: old must be copied from the FILE ITSELF (not from the numbered
view), must occur exactly once in the whole file, must lie inside your lines
and outside the folded regions; make it the shortest span that is unique.
new is the corrected text in the page's own style: conclusion-style
Traditional Chinese, symbols and code in English, game proper nouns exactly as
the game writes them (assets/names.md), cite src/ files and function names and
Ghidra addresses such as \`0x21650\`, not src/ line numbers.  When the right fix
is outside this page (src/ comment, Ghidra plate, a generator, another page)
set edit to null and describe it in outside.  When a claim cannot be settled
from static evidence, use kind unverifiable with low confidence, edit null,
and write what would settle it in developer_question.  Never soften a claim
into vagueness to make it pass; if it is wrong, state what is right.

A page that is right needs no findings: an empty findings list is a valid
verdict.  Do not invent problems, and do not rewrite for style alone.

Also record in cross_doc every other page you saw saying something different
about the same thing, and in pitfall_candidates anything a rebuild would get
wrong by intuition that rebuild_info/pitfalls.md does not have yet.

Scratch scripts and outputs go under ${SCRATCH}\\${id}\\ and nowhere else.
Do not modify any file in the repository other than your verdict file.

WRITE YOUR VERDICT with the Write tool to exactly:

  ${VERDICTS}\\${id}.json

UTF-8 JSON of this shape:

${VERDICT_SHAPE}

The gate (run it yourself before you finish and fix what it reports):

  ${RUN} check --ids ${id}

It requires every field, the matching unit_sha1, first-hand evidence (src with
file:line, ghidra with an address, data with FILE@offset, or tool with the
command) for wrong_fact / imprecise / stale_count / unverifiable, and every
edit unique and inside your lines.

Set upstream_dead only if the Ghidra tools stop answering, or python or the
repo itself is unusable.  If Ghidra fails, do not guess around it: write what
you have, set upstream_dead, and stop.

Then return the summary.  Your final output is data for the workflow, not a
message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.17 knowledge-base verification gate over one round.

Round tag: ${tag}
Units (${ids.length}): ${ids.join(' ')}

Run exactly:

  ${RUN} check --ids ${ids.join(' ')} --json

It prints a JSON object with checked, ok, missing, failures and gate_passed.
Report those: every id under "failures" goes into failing, every id under
"missing" into missing.  Condense the failure reasons into problems, one short
line per kind of problem.

Do not fix anything and do not edit any verdict file.  If the command cannot
run at all, say so in problems and set gate_passed false.`
}

function rescanPrompt(id, findings) {
  return `A first agent verified ONE slice of the FDPS knowledge base and flagged
problems.  You are the second reader for that one unit.  Nothing it proposed
reaches the knowledge base unless you confirm or amend it.

Your unit:      ${id}
Findings to re-read: ${findings.join(', ')}
Get the unit:   ${RUN} show ${id}
The verdict:    ${VERDICTS}\\${id}.json

For each listed finding, in this order, because the point of a second reader
is independence:

  1. Read the quoted claim in the page and check it yourself against the
     first-hand sources BEFORE weighing the first agent's reasoning.
  2. Then compare with the finding: is the claim really wrong (or unsettled),
     is the proposed new text true, complete, conclusion-style, free of
     process words and src/ line numbers, and no vaguer than the facts allow?
     Is the outside fix, if any, right and precise?
  3. Decide: confirm (the finding and its edit / outside fix stand as
     written), amend (the problem is real but the fix is not right: give the
     corrected edit and/or outside), or reject (the page was right, or the
     problem is not worth a change).

${SOURCES}

Add to the verdict file, with the Write tool, one field and change nothing
else:

  "_reread": { "decisions": [
      { "n": <finding n>, "decision": "confirm" | "amend" | "reject",
        "why": "<繁體中文：你查了什麼、結論>",
        "edit": { "old": "...", "new": "..." } | null,      (amend only; old copied from the file, unique, inside the unit)
        "outside": "..." }                                    (amend only, when the fix is outside the page)
  ] }

One decision for every listed finding.  Run ${RUN} check --ids ${id} afterwards
and fix what it reports in your own file.  READ-ONLY on Ghidra, src/, tools/
and the knowledge base; you may read other units' verdict files in
${VERDICTS}\\ as evidence, never edit them.

A second reading is not a licence to manufacture a conclusion: if a claim
cannot be settled, amend the finding to unverifiable-style wording in why and
keep its edit null.  Set upstream_dead only if Ghidra stops answering or python
/ the repo is unusable.`
}

const RESCAN_RETRY = `

This is a second attempt: the verdict file already has a "_reread" field that
fails the gate.  Run the check command first to see why, then rewrite the
"_reread" field whole (keep every other field as it is).`

// ------------------------------------------------------------------- plan

const DATE = args && args.date
const ROUND_SIZE = (args && args.roundSize) || 10
const EXCLUDE = (args && Array.isArray(args.exclude)) ? args.exclude : []

if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { stopped: 'args.date (YYYY-MM-DD) is required; it names the devlog/runs files' }
}

phase('Plan')
const plan = await agent(
  `Prepare the ticket-25.17 knowledge-base verification worklist.  You decide nothing.

Run exactly these two commands, in this order:

  ${RUN} units --freeze
  ${RUN} pending --all

The first keeps the current cut of every document into units (only the first
time; later runs reuse it) and prints the units.  The second prints a JSON array
with one object per unit: an id and a state that is "missing", "failing" or
"done".  Return ids = every id in the order printed, and todo = every id whose
state is not "done", in the order printed.

If a command cannot run at all, set ok false and say why in note.  Do not try
to repair anything.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)

if (!plan || !plan.ok) {
  return { stopped: 'the planning agent could not read the worklist', note: plan ? plan.note : 'no response' }
}
const IDS = plan.ids
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} units, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

// ------------------------------------------------------------------ judge

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

  // 5.2 -- nothing came back at all: the cause is outside this workflow.
  if (results.length === 0 && batch.length > 0) {
    stopped = `round ${round} returned nothing at all; treating it as an upstream failure`
    unfinished.push(...todo.slice(start))
    break
  }
  // 5.5 -- an agent reported the tools dead.
  if (results.some((r) => r.upstream_dead)) {
    stopped = `an agent in round ${round} reported Ghidra or the repo unusable: `
      + results.filter((r) => r.upstream_dead).map((r) => `${r.id} ${r.note || ''}`).join('; ')
    unfinished.push(...todo.slice(start))
    break
  }
  for (const r of results) {
    log(`  ${r.id}: ${r.claims_checked} claims, ${r.findings} findings, ${r.edits} edits`)
  }

  phase('Gate')
  let gate = await agent(gatePrompt(`r${round}`, batch), { label: `gate:round${round}`, phase: 'Gate', schema: GATE })
  if (!gate) {
    gate = { checked: batch.length, ok_count: 0, failing: [], missing: batch,
             gate_passed: false, problems: 'the gate agent returned nothing' }
  }
  gateReports.push(Object.assign({ round: round }, gate))
  log(`round ${round}: gate ok=${gate.ok_count} failing=${gate.failing.length} missing=${gate.missing.length}`)

  // 5.1 -- one retry per unit; still bad after that is unfinished, never done.
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
      log(`round ${round}: the re-gate returned nothing; the retried units count as unfinished`)
      unfinished.push(...retry)
    }
  }
}

// ----------------------------------------------------------------- rescan
//
// Every finding with an edit, an outside fix, confidence below high or an
// unverifiable claim gets a second reader.  The list is derived from the
// verdict files (kbverify.py rescan), so an interrupted rescan resumes where it
// stopped; one pass covers everything, a second pass only picks up units whose
// rescan agent returned nothing.

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= 2 && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.17 rescan worklist.  You decide nothing.

Run exactly:

  ${RUN} rescan

It prints a JSON array of {id, findings}.  Return todo = that array as printed.
If the command cannot run at all, set ok false and say why in note.`,
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
        agent(rescanPrompt(t.id, t.findings), { label: `rescan:${t.id}`, phase: 'Rescan', schema: RESCAN })
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
        rescanLog.push(`${r.id}: confirmed ${r.confirmed}, amended ${r.amended}, rejected ${r.rejected}`)
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
            agent(rescanPrompt(t.id, t.findings) + RESCAN_RETRY, { label: `rescan-retry:${t.id}`, phase: 'Rescan', schema: RESCAN })
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
        log(`rescan pass ${pass}: the gate returned nothing; its units are checked again by the report`)
      }
    }
  }
}

// ------------------------------------------------------------------- land
//
// Transcription, then every knowledge-base gate.  The landing agent decides
// nothing: apply lands exactly what the second readers confirmed or amended
// and refuses (and names) everything else.  A gate that fails is reported as
// a failure, never worked around -- fixing it takes judgement, which is the
// ticket session's.  Not run after a stop (ADR-0007 5.6).

let landing = null
if (!stopped) {
  phase('Land')
  landing = await agent(landPrompt(`${RUN} apply${EXCLUDE.length ? ' --exclude ' + EXCLUDE.join(' ') : ''}`, []),
    { label: 'land:apply+gates', phase: 'Land', schema: LAND })
  if (!landing) {
    log('LAND: the landing agent returned nothing; run kbverify.py apply and the gates by hand')
  } else {
    log(`land: ${landing.applied} edits applied, ${landing.refused.length} refused, gates ${landing.all_passed ? 'all passed' : 'FAILED'}`)
    for (const g of landing.gates.filter((x) => !x.passed)) {
      log(`  GATE FAILED: ${g.command}: ${g.detail}`)
    }
  }
}

// ----------------------------------------------------------------- report
//
// The closing report is always produced, with the stop reason when there is
// one.  The verdict archive is not produced after a stop (ADR-0007 5.6).

phase('Report')
const stopArg = stopped ? ` --stopped "${stopped.replace(/[^A-Za-z0-9 .,:;()_\-]/g, ' ')}"` : ''
const report = await agent(
  `Write the ticket-25.17 knowledge-base verification closing report.  You decide nothing.

Run exactly:

  ${RUN} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-kb-verify-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-kb-verify-verdicts.json`} and prints a
one-line JSON digest.  Return written = the files it names, summary = the digest
verbatim.  If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}
if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
  log('no verdict archive was written; run again to resume')
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  units: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescan: rescanLog,
  gateReports: gateReports,
  landing: landing,
  closingReport: report ? report.summary : null,
  written: report ? report.written : [],
}
