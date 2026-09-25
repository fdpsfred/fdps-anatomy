// Ticket 25.9, start to finish, with no human in the loop.
//
// The cut-content survey left 53 findings -- claims that some content, slot or
// code path in FDPS is residual, a stub, sealed off by a bug, or left over from
// FD2.  Each one is a claim, not a conclusion.  This verifies every one of them
// independently, one agent per finding, and leaves a verdict file per finding
// that tickets 25.10-25.14 transcribe from.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One finding per agent.  The worklist is the IDS array below; every agent()
//   call carries exactly one ID, and the agent reads that one claim through
//   "cutverify.py show <ID>".  No judging agent sees the list.
//
//   Verdicts go to files, agents return ~200 bytes.  The closing report is built
//   by cutverify.py from the files, never from this script's memory, so an
//   interrupted run resumes by simply being run again: a finding with a verdict
//   that passes the gate and matches the current wording of its claim is done.
//
//   Nothing is written into src/, the knowledge base or Ghidra by a judging
//   agent.  The only landing this ticket does is the closing report and verdict
//   archive in devlog/runs/, the devlog entry and pitfalls.md, all in the last
//   phase and only when the run was not stopped.
//
// args: {
//   date:            "YYYY-MM-DD"  required; names the devlog and devlog/runs files
//   roundSize:       number        findings judged per round (default 8)
//   maxRescanPasses: number        re-reading passes (default 2)
//   report:          boolean       false skips the devlog/pitfalls stage
// }

export const meta = {
  name: 'cut-verify-ticket25-9',
  description: 'Independently verify each of the 53 cut-content findings, one agent per finding',
  phases: [
    { title: 'Plan', detail: 'read which findings still need a verdict' },
    { title: 'Judge', detail: 'one agent per finding, verdict written to a file' },
    { title: 'Gate', detail: 'every verdict must carry first-hand evidence' },
    { title: 'Rescan', detail: 'a second agent re-judges corrections and disputed classes' },
    { title: 'Report', detail: 'closing report and verdict archive in devlog/runs' },
    { title: 'Document', detail: 'devlog entry, pitfalls, ticket checkboxes' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOL = REPO + '\\tools\\cut_verify\\cutverify.py'
const WS = REPO + '\\workspace\\cut_verify'
const VERDICTS = WS + '\\verdicts'
const SURVEY = REPO + '\\workspace\\cut_content\\investigation'

// The fixed worklist.  cutverify.py reads the same IDs from the ticket; the
// Plan phase stops the run if the two ever disagree.
const IDS = [
  'C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8', 'C9', 'C10', 'C11', 'C12', 'C13',
  'C14', 'C15', 'C16',
  'U01', 'U02', 'U03', 'U04', 'U05', 'U06', 'U07', 'U08', 'U09', 'U10',
  'I01', 'I02', 'I03', 'I04', 'I05', 'I06', 'I07', 'I08',
  'B1', 'B2', 'B3', 'B4', 'B5', 'B6',
  'S01', 'S02', 'S03', 'S04', 'S05', 'S06', 'S07', 'S08', 'S09', 'S10', 'S11', 'S12', 'S13',
]

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['ids', 'todo', 'ok'],
  properties: {
    ids: { type: 'array', items: { type: 'string' }, description: 'Every id printed, in order' },
    todo: { type: 'array', items: { type: 'string' }, description: 'Ids whose state is not done' },
    ok: { type: 'boolean', description: 'False if the command could not run at all' },
    note: { type: 'string' },
  },
}

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'verdict', 'category', 'confidence', 'wrote_file', 'has_open_question',
             'upstream_dead'],
  properties: {
    id: { type: 'string', description: 'The finding id, exactly as given' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    category: { type: 'string', enum: ['residual', 'stub', 'sealed', 'predecessor_leftover',
                                        'excluded', 'negative'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean', description: 'True once the verdict file exists' },
    has_open_question: { type: 'boolean' },
    upstream_dead: {
      type: 'boolean',
      description: 'True only if Ghidra stopped answering, or python / the repo is unusable',
    },
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
  required: ['id', 'changed', 'verdict', 'category', 'confidence', 'still_open', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    changed: { type: 'boolean', description: 'True if the verdict file was rewritten' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    category: { type: 'string', enum: ['residual', 'stub', 'sealed', 'predecessor_leftover',
                                        'excluded', 'negative'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    still_open: { type: 'boolean' },
    upstream_dead: { type: 'boolean' },
    what_changed: { type: 'string', description: 'One line, or empty' },
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

// ---------------------------------------------------------------- prompts

const CATEGORIES = `The classes, from CONTEXT.md "刪減與未用" (read that section once).  A trace
belongs to exactly one:

  residual              殘留內容  finished content (assets, numbers or script complete
                                  enough to work) that no game path reaches
  stub                  空殼      the mechanism, field or slot exists but nothing was
                                  put in it.  Residual lacks a path; a stub lacks content
  sealed                被封住的內容  content AND the path to it both exist, but an original
                                  bug keeps the player from getting or seeing it
  predecessor_leftover  前作遺留  code or data inherited from FD2 that does nothing in
                                  FDPS.  It says where the code came from, not that FDPS
                                  planned and dropped something
  excluded              排除      real, but belongs to no class: compiler output, unused
                                  API of a general library, unreachable defensive
                                  branches, a data-entry error, a feature that is in use
  negative              否定性結論  the claim is that something does NOT exist ("there is
                                  no debug key"); there is no trace to classify`

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\  -- the complete rebuilt C source of FDPS.LE, every function
     emitted with comments.  This is the primary source.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE.  Use it to confirm an address, a
     cross-reference count, or that nothing writes a global.  Load tools in one
     ToolSearch call, e.g.
       ToolSearch "select:mcp__ghidra__get_xrefs_to,mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__read_memory,mcp__ghidra__search_byte_patterns"
     Never call a Ghidra tool that writes (rename, comment, prototype, label,
     bookmark, save).
  3. The game's own data: ${REPO}\\fdps_game_files\\ (the original files) and
     ${REPO}\\workspace\\vfs_dump\\<CONTAINER>\\ (every .VFS already unpacked).
     Decoders exist under ${REPO}\\tools\\ (vfs_dump, saf_decode, cel_decode,
     data_skill); the fdps-data skill answers "what is item 0x0E".  Cite
     FILE@offset.  CD audio tracks are on the disc images named in CLAUDE.md;
     only read them if the claim is about CD audio.
  4. Corroboration only, never enough on its own:
     - the survey that produced the claim: ${SURVEY}\\ (four subfolders of
       scripts and outputs, and reports\\ with the full text).  You may reuse its
       scripts, but a number you take from its output must be re-derived from
       src/, Ghidra or the data before it counts;
     - the guide mirror: python ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps
     - the FD2 knowledge base: C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\ (for
       predecessor_leftover claims, what FD2 did with the same code).`

const VERDICT_SHAPE = `{
  "id": "<the id>",
  "claim_sha1": "<the claim_sha1 printed by show, verbatim>",
  "target_ticket": "<the target_ticket printed by show, verbatim>",
  "verdict": "holds" | "refuted" | "needs_correction",
  "category": "residual" | "stub" | "sealed" | "predecessor_leftover" | "excluded" | "negative",
  "suggested_category": "<the class the claim itself proposes, same codes>",
  "category_uncertain": true | false,
  "confidence": "high" | "medium" | "low",
  "conclusion": "<繁體中文。驗證後的結論，一到三句，照結論式寫，不寫過程>",
  "evidence": [
    { "source": "src", "location": "src/village.c:123", "observation": "<what is there>" },
    { "source": "ghidra", "location": "000357a0", "observation": "..." },
    { "source": "data", "location": "SHOP25.DAT@0x1a0", "observation": "..." },
    { "source": "guide" | "fd2" | "investigation", "location": "...", "observation": "..." }
  ],
  "sub_claims": [ { "claim": "<繁中>", "verdict": "holds|refuted|needs_correction", "note": "<繁中>" } ],
  "differences_from_claim": "<繁體中文。清單原文與實際不同的每一處；完全相符則為空字串>",
  "corrected_claim": "<繁體中文。needs_correction 時寫出修正後的完整主張，否則空字串>",
  "new_traces": ["<繁體中文。驗證途中看到、但不屬於本條主張的新痕跡，一條一句，不判定>"],
  "pitfall_candidate": "<繁體中文。若驗證揭露「重建時照直覺寫就會與原版不同」的事，寫出來；否則空字串>",
  "open_question": "<繁體中文。沒能確定的部分；沒有則空字串>"
}`

function judgePrompt(id) {
  return `You are verifying ONE finding of a survey of cut and unused content in FDPS
(炎龍騎士團外傳, a 1997 DOS game) whose executable FDPS.LE has been fully
reverse engineered into C.

Your finding: ${id}

Get its claim by running exactly:

  python ${TOOL} show ${id}

That prints the id, the ticket it will be transcribed to, the claim text and its
claim_sha1.  This is the only finding you work on.  Do not open the ticket file
or look at any other finding's claim.  Other findings may be verified by other
agents at the same time; never write anything but your own verdict file.

The claim was produced by a single pass that was only spot-checked.  Treat it as
an assertion to test, not a fact.  Your job is to find out, from first-hand
sources, whether each part of it is true.

${SOURCES}

How to judge:

  - Check every factual detail: counts, addresses, file names, offsets, record
    numbers, chapter and map numbers, names.  A claim that "nothing writes X" is
    tested by looking for a writer, in src/ (grep) and in Ghidra (xrefs), not by
    reading the survey.  A claim that some content is never reached is tested by
    looking for the path that would reach it.
  - A negative claim ("there is no debug key", "every string has an xref") is
    tested by actively trying to find a counterexample.
  - Some findings bundle several sub-claims (a list of maps, a list of items, a
    list of exclusions).  Check each one and record it in sub_claims.  Leave
    sub_claims as [] when the finding is a single claim.
  - "holds": every detail checks out.  "needs_correction": the trace is real but
    some detail is wrong, overstated or missing -- write the full corrected claim.
    "refuted": the core of the claim is false (the path exists, the writer
    exists, the content is in use).
  - The class is part of what you judge.  The claim proposes one (the words in
    （…） such as 殘留內容); put that in suggested_category.  When the claim names
    two or asks whether it belongs to any class, put the first one it names, or
    "excluded" if it names none, and decide category yourself.  A class that
    differs from the suggestion is not by itself a reason for needs_correction.
    Set category_uncertain when the line between two classes is genuinely
    unclear; say why in open_question.

${CATEGORIES}

Be thorough but bounded.  If some part cannot be settled with reasonable effort,
say so in open_question and lower confidence; never mark it holds on the
survey's word.  A wrong confident verdict is copied into the knowledge base; an
honest "could not settle" is not.

If you need scratch scripts or outputs, put them under ${WS}\\scratch\\${id}\\.
Do not write anywhere else except the verdict file.

WRITE YOUR VERDICT with the Write tool to exactly:

  ${VERDICTS}\\${id}.json

UTF-8 JSON of this shape:

${VERDICT_SHAPE}

Field rules, enforced by a gate (python ${TOOL} check --ids ${id}; run it
yourself before you finish and fix what it reports):

  - at least one evidence item must be first-hand: src with file:line, ghidra
    with an address, or data with FILE@offset.  guide / fd2 / investigation
    items are corroboration only;
  - refuted and needs_correction need differences_from_claim;
    needs_correction also needs corrected_claim;
  - prose fields are in Traditional Chinese; game proper nouns stay as the game
    writes them; identifiers, addresses and file names stay as they are.

new_traces is for anything you notice that is NOT part of this claim.  Record it
in one sentence and do not judge it; tickets 25.10-25.14 decide what to do with
it.

Set upstream_dead only if the Ghidra tools stop answering, or python or the repo
itself is unusable.  If Ghidra fails, do not guess around it: write what you
have, set upstream_dead, and stop.

Then return the summary.  Your final output is data for the workflow, not a
message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.9 verdict gate over one round.

Round tag: ${tag}
Findings (${ids.length}): ${ids.join(' ')}

Run exactly:

  python ${TOOL} check --ids ${ids.join(' ')} --json

It prints a JSON object with checked, ok, missing, failures and gate_passed.
Report those: every id under "failures" goes into failing, every id under
"missing" into missing.  Condense the failure reasons into problems, one short
line per kind of problem.

Do not fix anything and do not edit any verdict file.  The workflow re-runs the
judging agent for whatever you report; a repair made here would hide which
reading was wrong.  If the command cannot run at all, say so in problems and set
gate_passed false.`
}

function rescanPrompt(id, why) {
  return `A first agent verified ONE finding of the FDPS cut-content survey and its
verdict needs a second reading.  You are that second reader.  You work on this
one finding only.

Your finding: ${id}
Why it came back: ${why}

Get the claim:  python ${TOOL} show ${id}
The first verdict: ${VERDICTS}\\${id}.json

Work in this order, because the point of a second reader is independence:

  1. Read the claim, then form your own view from first-hand sources BEFORE
     reading the first verdict closely -- at minimum re-check the evidence the
     disputed part rests on.
  2. Then read the first verdict and compare.
  3. You may read other findings' verdict files in ${VERDICTS}\\ as evidence --
     for example when this finding's class depends on how a neighbouring
     trace was judged.  That is using a judgement someone else made, not making
     one for them.  Never edit another verdict file.

${SOURCES}

${CATEGORIES}

If your reading changes anything -- verdict, category, confidence, a corrected
detail, the corrected claim -- rewrite ${VERDICTS}\\${id}.json with the Write
tool, keeping exactly the same field shape and the same claim_sha1 and
target_ticket, and add a "_supersedes" field: one short Traditional Chinese
paragraph saying what the first reading concluded and what changed it.  Set
changed to true.

If it does not, leave every judgement field exactly as it is and add one field,
"_reread": a single Traditional Chinese line saying you confirmed it and what
you checked.  Set changed to false.  That field is bookkeeping: it is how a later
run knows this verdict already had its second reading.

Run python ${TOOL} check --ids ${id} afterwards and fix what it reports in your
own file.  READ-ONLY on Ghidra, src/ and the knowledge base throughout.

A second attempt is not a licence to manufacture a conclusion.  If the question
stays open, keep it open, say so in open_question, and keep confidence honest.
Set upstream_dead only if Ghidra stops answering or python / the repo is unusable.`
}

// ------------------------------------------------------------------- plan

const DATE = args && args.date
const ROUND_SIZE = (args && args.roundSize) || 8
const MAX_RESCAN_PASSES = (args && args.maxRescanPasses) || 2
const WRITE_DOCS = !(args && args.report === false)

if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { stopped: 'args.date (YYYY-MM-DD) is required; it names the devlog and run files' }
}

phase('Plan')
const plan = await agent(
  `Read the ticket-25.9 worklist.  You decide nothing; you only report what is left.

Run exactly:

  python ${TOOL} pending --all

It prints a JSON array with one object per finding: an id and a state that is
"missing", "failing" or "done".  Return ids = every id in the order printed, and
todo = every id whose state is not "done", in the order printed.

If the command cannot run at all, set ok false and say why in note.  Do not try
to repair anything.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)

if (!plan || !plan.ok) {
  return { stopped: 'the planning agent could not read the worklist', note: plan ? plan.note : 'no response' }
}
const drift = IDS.filter((i) => !plan.ids.includes(i)).concat(plan.ids.filter((i) => !IDS.includes(i)))
if (drift.length > 0) {
  return {
    stopped: 'the ticket and this script disagree on the finding ids; update IDS before running',
    drift: drift,
  }
}
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} findings, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

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

  phase('Gate')
  let gate = await agent(gatePrompt(`r${round}`, batch), {
    label: `gate:round${round}`, phase: 'Gate', schema: GATE,
  })
  if (!gate) {
    gate = { checked: batch.length, ok_count: 0, failing: [], missing: batch,
             gate_passed: false, problems: 'the gate agent returned nothing' }
  }
  gateReports.push(Object.assign({ round: round }, gate))
  log(`round ${round}: gate ok=${gate.ok_count} failing=${gate.failing.length} missing=${gate.missing.length}`)

  // 5.1 -- one retry per finding; still bad after that is unfinished, never done.
  const retry = [].concat(gate.failing || [], gate.missing || []).filter((i) => batch.includes(i))
  if (retry.length > 0) {
    if (gate.problems) {
      log(`round ${round}: gate said: ${gate.problems}`)
    }
    log(`round ${round}: retrying ${retry.join(' ')}`)
    const second = (await parallel(retry.map((id) => () =>
      agent(judgePrompt(id), { label: `retry:${id}`, phase: 'Judge', schema: SUMMARY })
    ))).filter(Boolean)
    if (second.some((r) => r.upstream_dead)) {
      stopped = `a retry in round ${round} reported Ghidra or the repo unusable`
      unfinished.push(...retry, ...todo.slice(start + ROUND_SIZE))
      break
    }
    const regate = await agent(gatePrompt(`r${round}b`, retry), {
      label: `gate:round${round}b`, phase: 'Gate', schema: GATE,
    })
    if (regate) {
      gateReports.push(Object.assign({ round: round + 'b' }, regate))
      const stillBad = [].concat(regate.failing || [], regate.missing || [])
      if (stillBad.length > 0) {
        log(`round ${round}: UNFINISHED after retry: ${stillBad.join(' ')}`)
        unfinished.push(...stillBad)
      }
    } else {
      log(`round ${round}: the re-gate returned nothing; the retried findings count as unfinished`)
      unfinished.push(...retry)
    }
  }
}

// ----------------------------------------------------------------- rescan
//
// Each judge saw one claim.  The ticket asks for a second agent on every
// verdict that corrects the claim or disputes its class; low confidence and
// open questions come back too.  The list is derived from the verdict files
// (cutverify.py rescan), so an interrupted rescan resumes where it stopped.

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= MAX_RESCAN_PASSES && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.9 rescan worklist.  You decide nothing.

Run exactly:

  python ${TOOL} rescan

It prints a JSON array of {id, why}.  Return todo = that array as printed.  If the
command cannot run at all, set ok false and say why in note.`,
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

    phase('Rescan')
    log(`rescan pass ${pass}: re-reading ${pending.map((t) => t.id).join(' ')}`)
    const results = (await parallel(pending.map((t) => () =>
      agent(rescanPrompt(t.id, t.why), { label: `rescan:${t.id}`, phase: 'Rescan', schema: RESCAN })
    ))).filter(Boolean)

    if (results.length === 0) {
      stopped = `rescan pass ${pass} returned nothing at all; treating it as an upstream failure`
      break
    }
    if (results.some((r) => r.upstream_dead)) {
      stopped = `a rescan agent in pass ${pass} reported Ghidra or the repo unusable`
      break
    }
    for (const r of results) {
      if (r.changed) {
        rescanLog.push(`${r.id} -> ${r.verdict}/${r.category} (${r.confidence}): ${r.what_changed || ''}`)
        log(`  ${r.id} changed -> ${r.verdict}/${r.category} (${r.confidence})`)
      }
    }
    const missed = pending.map((t) => t.id).filter((i) => !results.some((r) => r.id === i))
    if (missed.length > 0) {
      log(`rescan pass ${pass}: no answer for ${missed.join(' ')}; the next pass picks them up`)
    }

    phase('Gate')
    const gate = await agent(gatePrompt(`rescan${pass}`, pending.map((t) => t.id)), {
      label: `gate:rescan${pass}`, phase: 'Gate', schema: GATE,
    })
    if (gate) {
      gateReports.push(Object.assign({ round: 'rescan' + pass }, gate))
      const bad = [].concat(gate.failing || [], gate.missing || [])
      if (bad.length > 0) {
        log(`rescan pass ${pass}: verdicts now failing the gate: ${bad.join(' ')}`)
        unfinished.push(...bad.filter((i) => !unfinished.includes(i)))
      }
    }
  }
}

// ----------------------------------------------------------------- report
//
// The closing report is always produced, with the stop reason when there is
// one.  The verdict archive and everything after it are not produced after a
// stop (ADR-0007 5.6): a partial set reads exactly like a complete one.

phase('Report')
const stopArg = stopped ? ` --stopped "${stopped.replace(/"/g, "'")}"` : ''
const report = await agent(
  `Write the ticket-25.9 closing report.  You decide nothing.

Run exactly:

  python ${TOOL} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-cut-verify-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-cut-verify-verdicts.json`} and prints a one-line JSON
digest.  Return written = the files it names, summary = the digest verbatim.
If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}

const stats = {
  findings: IDS.length,
  judgedThisRun: todo.length,
  unfinishedThisRun: unfinished,
  stopped: stopped,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  closingReport: report ? report.summary : null,
}

let docs = []
if (!stopped && WRITE_DOCS && report && report.written.length >= 2) {
  phase('Document')
  docs = (await parallel([
    () => agent(
      `Write the devlog entry for ticket 25.9 and tick its checkboxes.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly: narrative,
rambling allowed, and the point is the dead ends -- the successful path ends up
in the knowledge base, the failed ones are the only thing otherwise lost.

Sources:
  ${REPO}\\devlog\\runs\\${DATE}-cut-verify-summary.json    counts and lists
  ${REPO}\\devlog\\runs\\${DATE}-cut-verify-verdicts.json   every verdict; open the
                                                          refuted and corrected ones
  ${REPO}\\devlog\\2026-09-25-cut-content-survey.md        the survey this verified

Write ${REPO}\\devlog\\${DATE}-cut-verify.md in TRADITIONAL CHINESE.  Cover: what
was verified and how (one agent per finding, a second agent on corrections and
disputed classes); how many held, were refuted, needed correction, per target
ticket; every refuted finding and what the survey got wrong, because those are
the survey's dead ends; classes that moved; what stayed open; and the new traces
handed to 25.10-25.14.  Do not claim anything the files do not support.

Then edit ${REPO}\\.scratch\\fdps-rebuild\\issues\\25.9-cut-findings-verification.md:
tick a checkbox only when the summary supports it (every finding has a verdict
means unfinished is empty), set Status to done if all three are ticked, and add
one line under the checkboxes naming the two devlog/runs files as where
25.10-25.14 read the verified findings.  Do not change the 發現清單 section: the
verdicts are keyed to its exact wording.

Return the list of files you wrote.`,
      { label: 'doc:devlog', phase: 'Document', schema: DONE }
    ),
    () => agent(
      `Add ticket 25.9's rebuild pitfalls to ${REPO}\\rebuild_info\\pitfalls.md.

Read ${REPO}\\rebuild_info\\_index.md for the threshold -- a pitfall is something
a rebuild gets wrong by following intuition, not something merely interesting --
and read pitfalls.md in full for its structure and for what is already there.
Other agents may be editing pitfalls.md at the same time: re-read it right before
each Edit and keep each Edit small.

Candidates: the pitfall_candidates list in
${REPO}\\devlog\\runs\\${DATE}-cut-verify-summary.json, each naming the finding
it came from; open that finding's verdict in
${REPO}\\devlog\\runs\\${DATE}-cut-verify-verdicts.json for the evidence.  Only
use candidates whose finding's verdict is holds or needs_correction (for the
latter, the corrected_claim is the fact).  Three candidates were already named by
ticket 25: chapter 28 reinforcements computed as turn / 2, the 24-row secret
code table read with chapter index minus 1, and script opcode 0x61 being a real
reward rather than debug code.  Check whether each is already in pitfalls.md.

Write in TRADITIONAL CHINESE, conclusion-style, matching the house style.  Say
what goes wrong and what the intuitive version would be, and link to the page
that owns the fact when one exists; do not repeat format details.  Do not cite
workspace/ or devlog/.  If pitfalls.md is listed in an _index.md whose
description would now be wrong, fix that description.

Return the list of files you wrote and, in summary, which candidates you added
and which you rejected and why.`,
      { label: 'doc:pitfalls', phase: 'Document', schema: DONE }
    ),
  ])).filter(Boolean)
}

if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
  log('no verdict archive, devlog or pitfalls were written; run again to resume')
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  findings: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  closingReport: report ? report.summary : null,
  documents: docs.map((d) => d.written).flat(),
  docSummaries: docs.map((d) => d.summary),
  stats: stats,
}
