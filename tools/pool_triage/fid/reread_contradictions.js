// Ticket 14.1, the part that needs a judgement per function.
//
// Comparing FDPS.LE against the AIL library FD2 was carved out of FD2.LE is a
// whole-binary fact and needs no agents. What does need them is the fallout:
// every function where the library match and the ticket-14 pool verdict
// disagree has to be re-decided, and a pool verdict is exactly the kind of
// per-function judgement ADR-0002 forbids deciding in bulk.
//
// So each disagreement gets one agent, one function, with the ticket-14
// verdict and the library evidence laid side by side in a prepared packet. The
// agent writes a full verdict to a file and returns about 200 bytes; the list
// of disagreements lives here and in the packet directory, never inside an
// agent's context.
//
// A disagreement is not by itself a correction. Two of the shapes here are
// known to be noise: a fourteen-byte forwarder matches every other fourteen-byte
// forwarder in the program, and the library and FDPS.LE do not have to agree on
// where Ghidra put a function boundary. The prompt says so, and an agent that
// keeps the ticket-14 pool is doing its job, not failing to find something.
//
// Resumable: an address that already has a 14.1 verdict file is not judged
// again, so a run stopped by the agent budget continues where it left off.
//
// args: {
//   addresses:       explicit worklist, skipping the planning agent
//   roundSize:       items judged per apply round        (default 8)
//   maxRescanPasses: re-reads of unresolved verdicts     (default 2)
//   report:          false to skip the run-report stage
// }

export const meta = {
  name: 'ail-fid-contradictions',
  description: 'Re-decide every function where the AIL library match and the ticket-14 pool verdict disagree',
  phases: [
    { title: 'Plan', detail: 'rebuild the evidence packets and read the pending list' },
    { title: 'Judge', detail: 'one agent per disagreeing function' },
    { title: 'Apply', detail: 'transcribe the round into Ghidra, then run the gate' },
    { title: 'Rescan', detail: 're-read unresolved verdicts against their neighbours' },
    { title: 'Report', detail: 'run report for devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\pool_triage'
const FID_WS = WS + '\\fid_ail'
const PACKETS = FID_WS + '\\contradictions'
const VERDICTS = FID_WS + '\\verdicts'
const POOL_VERDICTS = WS + '\\verdicts\\pools'
const TOOLS = REPO + '\\tools\\pool_triage'
const AUDIT = REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java'
function arg(name, fallback) {
  return args && args[name] !== undefined ? args[name] : fallback
}

// One report file per run. runTag names it; when it is omitted the report agent
// is told to step aside rather than overwrite an existing file, because the
// script itself cannot see the filesystem to check.
const RUN_REPORT = REPO + '\\devlog\\runs\\2026-08-16-ail-fid-contradictions'
  + (arg('runTag', '') ? '-' + arg('runTag', '') : '') + '.json'

const ROUND_SIZE = arg('roundSize', 8)
const MAX_RESCAN_PASSES = arg('maxRescanPasses', 2)
const WRITE_REPORT = arg('report', true)

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['todo', 'total'],
  properties: {
    todo: { type: 'array', items: { type: 'string' } },
    total: { type: 'integer', description: 'How many disagreements exist in all' },
    note: { type: 'string' },
  },
}

const UNRESOLVED = {
  type: 'object',
  additionalProperties: false,
  required: ['addresses'],
  properties: {
    addresses: { type: 'array', items: { type: 'string' } },
    checked: { type: 'integer', description: 'How many verdict files were read' },
    note: { type: 'string' },
  },
}

const VERDICT_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'pool', 'changed', 'confidence', 'wrote_file'],
  properties: {
    addr: { type: 'string', description: '8-hex address, exactly as given' },
    pool: { type: 'string', enum: ['fdps', 'crt', 'ail', 'binary_artifact', 'unknown'] },
    changed: { type: 'boolean', description: 'true when this differs from the ticket-14 pool' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    has_open_question: { type: 'boolean' },
    wrote_file: { type: 'boolean' },
    note: { type: 'string', description: 'At most one short line, or empty' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'orphan_ranges', 'error_bookmarks', 'ok', 'ghidra_responding'],
  properties: {
    applied: { type: 'integer' },
    renamed: { type: 'integer' },
    tag_totals: { type: 'string', description: 'pool tag counts as ReconcilePoolTags reports them' },
    orphan_ranges: { type: 'integer' },
    error_bookmarks: { type: 'integer' },
    ok: { type: 'boolean' },
    ghidra_responding: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const REPORT_DONE = {
  type: 'object',
  additionalProperties: false,
  required: ['wrote_file'],
  properties: {
    wrote_file: { type: 'boolean' },
    note: { type: 'string' },
  },
}

// ---------------------------------------------------------------- prompts

const POOL_RULES = `THE FOUR POOLS

  fdps             the game's own code, to be restored to C
  crt              Watcom C runtime and the 80x87 emulator
  ail              Miles Audio Interface Library 3.02
  binary_artifact  what the linker or compiler produced with no source of its
                   own: jump islands, merged epilogues, calling-convention
                   shims

The decisive evidence for a library boundary is link direction. A third-party
library arrives already compiled, so its objects cannot call symbols defined in
the game's source. A function called only by settled AIL code is AIL. The
converse does not hold: the game calling a public AIL entry point is the normal
use of the library and says nothing about the caller.`

const FID_CAUTION = `WHAT THE LIBRARY MATCH IS AND IS NOT

ailv3.lib was assembled by the FD2 project out of FD2.LE's own bytes. It is not
a Miles release. A hit therefore means "this stretch of FDPS.LE is the same code
as that stretch of FD2.LE", which is strong evidence of shared vendor origin and
nothing more.

Two ways a hit misleads, both present in this worklist:

  Shape collision. Function ID hashes instructions with relocated operands
  masked out, so every fourteen-byte "load one stack argument, call, clean up,
  return" forwarder in the program hashes identically. The packet's
  module_matched_n_functions field says how many different FDPS functions the
  same library module answered to; anything above 1 with a low score is a
  collision, not an identification. Scores below the Ghidra default of 14.6 are
  weak on their own.

  Boundary difference. Ghidra decided where each function ends independently in
  the two programs, so a mismatch in size between your function and the library
  one can be a boundary artefact rather than different code.

A match at a three-figure score, one candidate, and full_hash_equal true is a
different matter: that is a long stretch of identical instructions and it takes
a concrete reason to explain away.`

function judgePrompt(addr, extra) {
  return `Re-decide the pool of ONE function in FDPS.LE. Ticket 14 already judged it, and
the AIL library comparison from ticket 14.1 disagrees. Your job is to settle
which is right for this one function.

Your function: ${addr}. The evidence is prepared in

  ${PACKETS}\\${addr}.json

Read that file FIRST with the Read tool. It holds the ticket-14 pool with the
reasoning that produced it, the library match with its score and collision
count, and the pools of the four neighbouring functions.

This is the only function you work on. Never judge another function.

READ-ONLY on Ghidra. Do not rename, tag, or comment. The workflow transcribes
every change later.

${POOL_RULES}

${FID_CAUTION}
${extra || ''}
HOW TO JUDGE, in order:

1. Read the packet.
2. Read the disassembly of your function. It is the primary source, and the
   ticket-14 reasoning in the packet is a claim about it that you can check.
3. Decide whether the library match is a real identification or one of the two
   noise shapes above.
4. Look at what calls your function and what it calls. Link direction settles
   most of these: a function only reachable from settled AIL code is AIL
   whatever its body looks like.
5. Data references are the other decisive kind. AIL keeps its runtime state in
   known places -- the last-error buffer at 0x69ef0, the debug logger counters
   at 0x69e6c-0x69e84, the timer registry at 0x604a0-0x605ee, the mixer
   parameters at 0x61500-0x61518. A function that reads or writes those is AIL
   even with no calls at all.

Tools, in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_xrefs_from"

BUDGET: about 8 tool calls. Keeping the ticket-14 pool is a perfectly good
outcome and usually the quick one. If eight calls have not settled it, write a
medium- or low-confidence verdict with an open question and let the rescan take
it -- do not keep digging.

WRITE YOUR VERDICT TO A FILE. Use the Write tool to create exactly:

  ${VERDICTS}\\${addr}.json

with this shape, UTF-8, no trailing commentary:

{
  "addr": "${addr}",
  "pool": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
  "name": "<symbol name, or empty>",
  "library": "<e.g. ailv3.lib(AIL_internal_filesize_path) score 128, or empty>",
  "role": "<one English sentence on what the function does>",
  "evidence": "<why this pool and not the others, and what you made of the library match>",
  "confidence": "high" | "medium" | "low",
  "open_question": "<what you could not settle, or empty>"
}

Fill "name" only when the library match is a real identification and you are
moving the function into that pool: use the library symbol exactly as ailv3.lib
spells it. Otherwise leave it empty; renaming on the strength of a collision
would be worse than leaving the function unnamed. Never propose a PascalCase
name -- Ghidra's tooling warns about that and the warning is wrong here.

Then return the summary. Your final output is data for the workflow, not a
message to a human.`
}

function rescanPrompt(addr, why) {
  return judgePrompt(addr, `
THIS IS A RE-READ. An earlier pass judged this same function and left it
unsettled: ${why}

This time you may also read the verdict files of the neighbouring functions
under ${POOL_VERDICTS} -- their addresses are in your packet. Citing a
judgement someone else already made is not making it for them.

A second attempt is not a licence to manufacture a conclusion. If it is still
unsettled, say so again and keep the open question.
`)
}

function applyPrompt(tag, ids) {
  return `Transcribe one round of ticket-14.1 pool verdicts into Ghidra. You are not
judging anything: each verdict was made by an agent that read that one function.

Round tag: ${tag}
Functions (${ids.length}): ${ids.join(' ')}

Load the tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

PASS program="FDPS.LE" ON EVERY run_ghidra_script AND save_program CALL. The
same session has the AIL library modules open for the comparison this ticket is
built on, so the current program is often one of those and not FDPS.LE.
ApplyPoolVerdicts refuses to run against anything else -- that guard is correct
and fires before any edit, so nothing is damaged, but relying on the current
program wastes a round trip every time.

Steps, in order:

1. Promote this round's verdicts into the ticket-14 verdict directory, which is
   the set ApplyPoolVerdicts and ReconcilePoolTags read. For each address above,
   using PowerShell:
     - if ${POOL_VERDICTS}\\<addr>.json.t14 does not exist, copy
       ${POOL_VERDICTS}\\<addr>.json to it first. That keeps the ticket-14
       decision on disk for the write-up; it must never be overwritten by a
       second run.
     - then copy ${VERDICTS}\\<addr>.json over ${POOL_VERDICTS}\\<addr>.json.
   Report in problems any address whose 14.1 verdict file is missing, and skip
   that address for the rest of this round rather than inventing a verdict.

2. Apply them:
     run_ghidra_script  ${TOOLS}\\ApplyPoolVerdicts.java
     args: ${POOL_VERDICTS} ${ids.join(' ')}
   Read its output: verdicts applied, renames, tags, problems. A "NAME TAKEN"
   line is not fatal but must be reported.

3. Reconcile the tag set, which is the only place the pool tags are checked
   against the verdicts:
     run_ghidra_script  ${TOOLS}\\ReconcilePoolTags.java
     args: ${POOL_VERDICTS}
   The four pool tag counts must add up to the program's function count (1347).
   Put the counts in tag_totals.

4. Run the gate:
     run_ghidra_script  ${AUDIT}
   Orphan code ranges and error bookmarks must both be 0. Tagging and renaming
   cannot create orphan code, so a failure here means something else moved:
   report it rather than working around it.

5. Save the program with save_program.

Set ok to false and describe it in problems if anything is still wrong after a
repair attempt -- do not paper over it. Set ghidra_responding to false only if
Ghidra itself stopped answering.`
}

function planPrompt() {
  return `Rebuild the ticket-14.1 disagreement worklist. Nothing here is a judgement; it
is a script run plus a directory listing.

Steps:

1. Rebuild the evidence packets from the current query results:
     python ${REPO}\\tools\\pool_triage\\fid\\build_contradiction_worklist.py
   It prints the full address list and writes one packet per address under
   ${PACKETS}.

2. List ${VERDICTS} and return, as todo, every address from step 1 that does
   NOT already have a <addr>.json verdict file there. Return the full count from
   step 1 as total.

Return todo even if it is empty.`
}

function unresolvedPrompt() {
  return `List the ticket-14.1 verdicts that are not settled. No judgement: this is a
directory read plus a field check.

Read every ${VERDICTS}\\<addr>.json file. Return, as addresses, the "addr" of
each one where any of these holds:

  - "confidence" is not "high"
  - "open_question" is a non-empty string
  - "pool" is "unknown"

Return the total number of files you read as checked. Return an empty addresses
list if every verdict is settled; that is the normal outcome and not a failure.

Read the files themselves rather than trusting any summary: a verdict written by
an earlier, interrupted run counts exactly as much as one written a minute ago.`
}

function reportPrompt(payload) {
  return `Write the run report for this workflow. No judgement, no knowledge base: the
ticket owner writes those. This is the machine-readable record devlog/_conventions.md
asks for.

Write with the Write tool to:

  ${RUN_REPORT}

**If that path already exists, do not overwrite it** — a previous run's record
is not yours to destroy. Append \`-2\`, then \`-3\`, and so on before the \`.json\`
until you find a free name, and say which name you used in note.

Content: this JSON, verbatim, pretty-printed, UTF-8.

${payload}

Then return wrote_file.`
}

// ---------------------------------------------------------------- driver

const unfinished = []
const applyReports = []
const verdicts = []
let halted = false
let haltReason = ''

function noteApply(label, report) {
  if (!report) {
    unfinished.push(`${label}: apply agent returned nothing`)
    return false
  }
  applyReports.push(Object.assign({ round: label }, report))
  log(`${label}: applied=${report.applied} orphans=${report.orphan_ranges} `
    + `errors=${report.error_bookmarks} ok=${report.ok}`)
  if (report.problems) {
    log(`${label}: PROBLEM ${report.problems}`)
  }
  if (!report.ok) {
    unfinished.push(`${label}: ${report.problems || 'apply reported not ok'}`)
  }
  if (report.ghidra_responding === false) {
    halted = true
    haltReason = `${label}: Ghidra stopped responding`
    log(`STOP ${haltReason}`)
  }
  return report.ok
}

// One agent failing is a per-item problem and gets a retry. Every agent in a
// round failing is not: the cause is then upstream of this workflow -- session
// limit, API, host -- and retrying is guaranteed waste.
function outage(label, got, expected, remaining) {
  if (expected === 0 || got > 0) {
    return false
  }
  halted = true
  haltReason = `${label}: all ${expected} agent(s) in the round returned nothing, `
    + 'which means the failure is upstream of this workflow rather than in any one item'
  log(`STOP ${haltReason}`)
  if (remaining && remaining.length > 0) {
    unfinished.push(`${remaining.length} item(s) not attempted after the run stopped: `
      + remaining.join(' '))
  }
  return true
}

function chunk(items, size) {
  const out = []
  for (let i = 0; i < items.length; i += size) {
    out.push(items.slice(i, i + size))
  }
  return out
}

async function judgeRound(tag, ids, promptFor, phaseName) {
  const summaries = (await parallel(ids.map((id) => () =>
    agent(promptFor(id), { label: `${phaseName.toLowerCase()}:${id}`, phase: phaseName,
      schema: VERDICT_SUMMARY })
  ))).filter(Boolean)

  if (outage(tag, summaries.length, ids.length, ids)) {
    return []
  }

  // Whether an item is done is decided by the verdict file, not by the agent
  // saying it went well.
  let usable = summaries.filter((s) => s.wrote_file)
  const lost = ids.filter((id) => !usable.some((s) => s.addr === id))
  if (lost.length > 0) {
    log(`${tag}: ${lost.length} function(s) came back without a verdict file, retrying`)
    const retried = (await parallel(lost.map((id) => () =>
      agent(promptFor(id), { label: `${phaseName.toLowerCase()}-retry:${id}`, phase: phaseName,
        schema: VERDICT_SUMMARY })
    ))).filter(Boolean).filter((s) => s.wrote_file)
    usable = usable.concat(retried)
    for (const id of lost) {
      if (!retried.some((s) => s.addr === id)) {
        unfinished.push(`function ${id}: no verdict file after a retry`)
      }
    }
  }
  return usable
}

phase('Plan')
let todo = arg('addresses', null)
let total = todo ? todo.length : 0
if (!todo) {
  const plan = await agent(planPrompt(), { label: 'plan:worklist', phase: 'Plan', schema: PLAN })
  if (!plan) {
    return { error: 'the planning agent returned nothing; the worklist could not be read' }
  }
  todo = plan.todo
  total = plan.total
}
log(`${total} disagreement(s) in all, ${todo.length} still to judge`)

const rounds = chunk(todo, ROUND_SIZE)
for (let r = 0; r < rounds.length && !halted; r++) {
  const tag = `r${r + 1}`
  phase('Judge')
  const usable = await judgeRound(tag, rounds[r], judgePrompt, 'Judge')
  if (halted) {
    // outage() already recorded this round; record the rounds never started.
    const never = rounds.slice(r + 1).flat()
    if (never.length > 0) {
      unfinished.push(`${never.length} item(s) in later rounds not attempted: ${never.join(' ')}`)
    }
    break
  }
  for (const s of usable) {
    verdicts.push(s)
  }
  if (usable.length > 0) {
    phase('Apply')
    noteApply(`apply:${tag}`,
      await agent(applyPrompt(tag, usable.map((s) => s.addr)),
        { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT }))
  }
}

// ---------------------------------------------------------------- rescan
//
// Every agent saw one function, so a verdict that turns on what the neighbours
// are cannot be settled on the first pass. This is the second look, with the
// neighbours' verdict files allowed as evidence.
//
// The list comes off disk rather than out of this run's summaries. Items that
// already had a verdict file were never judged here, so an in-run list would
// skip exactly the unresolved verdicts a previous interrupted run left behind,
// silently dropping the ADR-0007 rescan in the case the header advertises as
// resumable.

let rescanPass = 0
let openOnDisk = null
while (!halted && rescanPass < MAX_RESCAN_PASSES) {
  openOnDisk = await agent(unresolvedPrompt(), {
    label: `rescan:scan${rescanPass + 1}`, phase: 'Rescan', schema: UNRESOLVED })
  if (!openOnDisk) {
    unfinished.push('the unresolved-verdict scan returned nothing; the rescan was skipped')
    break
  }
  const unresolved = openOnDisk.addresses
  if (unresolved.length === 0) {
    break
  }
  rescanPass++
  log(`rescan pass ${rescanPass}: ${unresolved.length} verdict(s) to re-read`)
  const passRounds = chunk(unresolved, ROUND_SIZE)
  for (let r = 0; r < passRounds.length && !halted; r++) {
    const tag = `rescan${rescanPass}r${r + 1}`
    phase('Rescan')
    const why = 'the first pass left it at less than high confidence, or with an open question'
    const usable = await judgeRound(tag, passRounds[r], (id) => rescanPrompt(id, why), 'Rescan')
    if (halted) {
      break
    }
    for (const s of usable) {
      const i = verdicts.findIndex((v) => v.addr === s.addr)
      if (i >= 0) verdicts[i] = s; else verdicts.push(s)
    }
    if (usable.length > 0) {
      phase('Apply')
      noteApply(`apply:${tag}`,
        await agent(applyPrompt(tag, usable.map((s) => s.addr)),
          { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT }))
    }
  }
  // A pass that settles nothing will not settle anything next time either.
  // Compared as sets, not as counts: a pass that settles one verdict while a
  // re-read downgrades another leaves the counts equal, and stopping there
  // would deny the newly-unsettled one its second look.
  const after = await agent(unresolvedPrompt(), {
    label: `rescan:recheck${rescanPass}`, phase: 'Rescan', schema: UNRESOLVED })
  if (!after) {
    unfinished.push(`the unresolved-verdict recheck after rescan pass ${rescanPass} returned nothing`)
    break
  }
  const before = unresolved.slice().sort().join(' ')
  openOnDisk = after
  if (after.addresses.slice().sort().join(' ') === before) {
    log(`rescan pass ${rescanPass} settled nothing further; stopping the rescan`)
    break
  }
}

// ---------------------------------------------------------------- report

const changed = verdicts.filter((s) => s.changed)
const summary = {
  ticket: '14.1',
  workflow: 'ail-fid-contradictions',
  disagreements_total: total,
  judged: verdicts.length,
  pool_changed: changed.length,
  changes: changed.map((s) => ({ addr: s.addr, pool: s.pool, confidence: s.confidence })),
  kept: verdicts.filter((s) => !s.changed).map((s) => s.addr),
  // From the last on-disk scan, so a verdict left open by an earlier run shows
  // up here too rather than only the ones this run happened to touch.
  unresolved: openOnDisk ? openOnDisk.addresses : [],
  apply_rounds: applyReports,
  halted,
  halt_reason: haltReason,
  unfinished,
}

log(`judged ${verdicts.length}/${total}, pool changed on ${changed.length}, `
  + `${unfinished.length} unfinished entr(ies)`)

// 5.6: a halted run must not leave a finished-looking artefact behind.
if (WRITE_REPORT && !halted) {
  phase('Report')
  const done = await agent(reportPrompt(JSON.stringify(summary, null, 2)),
    { label: 'report:run', phase: 'Report', schema: REPORT_DONE })
  if (!done || !done.wrote_file) {
    unfinished.push('the run report was not written')
  }
}
else if (halted) {
  log('run halted: the run report is returned but not written to devlog/runs')
}

return summary
