// The names build_symbol_renames.py refused to decide.
//
// Everything it could settle by transcription it already applied: a function
// whose body is byte-identical to one library function, and whose name is not
// wanted by anyone else, simply takes that library's spelling. What is left is
// the residue where the evidence disagrees with itself - two functions claiming
// one symbol, a name that no library publishes, a name nobody can derive from
// the shape - and each of those is an identity question about one function,
// which ADR-0002 says is decided one at a time by an agent that read the
// assembly.
//
// The agent decides the name only. Pool is not reopened here; a function that
// turns out to belong somewhere else is reported, not moved, because moving it
// is a different decision with different evidence.
//
// args: {
//   addresses:  explicit worklist, skipping the planning agent
//   roundSize:  items per apply round        (default 5)
//   runTag:     suffix for the run report
// }

export const meta = {
  name: 'name-disputed-symbols',
  description: 'Decide the library symbol name for each function the rename map could not settle',
  phases: [
    { title: 'Plan', detail: 'read the disputed list' },
    { title: 'Name', detail: 'one agent per function' },
    { title: 'Apply', detail: 'rename in Ghidra, then run the gate' },
    { title: 'Report', detail: 'run report for devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\pool_triage'
const FID_WS = WS + '\\fid_ail'
const DISPUTED = FID_WS + '\\renames\\needs_judgement.json'
const DECISIONS = FID_WS + '\\renames\\decisions'
const POOL_VERDICTS = WS + '\\verdicts\\pools'
const SYMBOLS = WS + '\\fid\\watcom_symbols.json'
const CRT_RESULTS = WS + '\\fid\\results'
const AIL_REPORT = FID_WS + '\\report\\ail_fid_report.json'
const AUDIT = REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java'

function arg(name, fallback) {
  return args && args[name] !== undefined ? args[name] : fallback
}

const ROUND_SIZE = arg('roundSize', 5)
const MAX_RESCAN_PASSES = arg('maxRescanPasses', 2)
const RUN_REPORT = REPO + '\\devlog\\runs\\2026-08-16-name-disputed-symbols'
  + (arg('runTag', '') ? '-' + arg('runTag', '') : '') + '.json'

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['todo', 'total'],
  properties: {
    todo: { type: 'array', items: { type: 'string' } },
    total: { type: 'integer' },
    note: { type: 'string' },
  },
}

const DECISION = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'name', 'kind', 'confidence', 'wrote_file'],
  properties: {
    addr: { type: 'string', description: '8-hex address, exactly as given' },
    name: { type: 'string', description: 'The name to apply, or empty to leave it unnamed' },
    kind: {
      type: 'string',
      enum: ['library_symbol', 'lib_static', 'crt_equivalent', 'leave_unnamed'],
      description: 'library_symbol = a real PUBDEF; lib_static = a file-static inside a '
        + 'library object; crt_equivalent = behaves like the CRT but matches no library object',
    },
    pool_doubt: { type: 'string', description: 'Empty, or why the pool now looks wrong' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'orphan_ranges', 'error_bookmarks', 'ok', 'ghidra_responding'],
  properties: {
    applied: { type: 'integer' },
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
  properties: { wrote_file: { type: 'boolean' }, note: { type: 'string' } },
}

// ---------------------------------------------------------------- prompts

const RULES = `THE NAMING RULE FOR VENDOR CODE

A function that IS a library function is called exactly what the library calls
it, with no project prefix, because the whole point of the name is that the
Watcom linker resolves it against the real library at rebuild time. Spelling,
underscores and case are copied character for character: \`_nmalloc\`, \`__CHK\`,
\`__STKOVERFLOW\`, \`IF@COS\`. The \`@\` in the \`IF@\` intrinsic-helper family is
part of the symbol and stays.

Three things are not library symbols and are named differently:

  lib_static      a file-static inside a library object, which publishes no
                  PUBDEF at all. Name it L$N_<module>_<what it does>, e.g.
                  L$1_stk_save_ss. Ghidra shows the owning module in the
                  Function ID evidence.
  crt_equivalent  behaves like a C runtime routine but matches no object in any
                  linked library, so the rebuild has to hand-write it. Name it
                  crt_equivalent_<snake_case description>.
  leave_unnamed   the evidence does not identify it. Say so and leave it; a
                  wrong name is worse than none, because it will be believed.`

function namePrompt(addr, extra) {
  return `Decide what ONE function in FDPS.LE should be called. An automated pass renamed
every vendor function whose identity was already settled by a byte-identical
library match; this one it refused, and the reason is in the file below.

Your function: ${addr}

Read these two files FIRST with the Read tool:

  ${DISPUTED}
    A JSON object keyed by address. Your entry says why the automatic pass would
    not name this function. Read only your own entry.

  ${SYMBOLS}
    Every public symbol of the libraries wlink actually links: CLIB3S,
    MATH387S, EMU387 and the DOS/4G startup object. A name you propose as
    kind "library_symbol" MUST appear in this list, character for character.

This is the only function you work on. Never name another function.

READ-ONLY on Ghidra. Do not rename, tag or comment; the workflow applies the
decision later.

${RULES}

EVIDENCE ALREADY ON DISK, if you need it:
  ${CRT_RESULTS}\\matches_10.0a.json   Function ID against the Watcom libraries
  ${AIL_REPORT}                        Function ID against the FD2 AIL library
  ${POOL_VERDICTS}\\${addr}.json        why ticket 14 put it in the pool it is in
Each match record carries the score and the source object. A score in the
hundreds with one candidate identifies a function; a score near the 14.6 default
with several candidates only says "some routine of this shape", and short
forwarders collide freely.
${extra || ''}
HOW TO DECIDE:

1. Read the disassembly of your function.
2. Weigh the library evidence against what you read. A name is only a
   library_symbol if it is in the symbol list AND the body supports it.
3. When two functions claim one symbol, the question is which one is the real
   one; read yours and say whether it is. You cannot see the other one's
   verdict, and you do not need to - describe what yours is.
4. If it is a startup or helper routine with no PUBDEF, it is lib_static or
   crt_equivalent, not a library_symbol with an invented spelling.

Tools, in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_xrefs_from"

BUDGET: about 8 tool calls.

WRITE YOUR DECISION TO A FILE. Use the Write tool to create exactly:

  ${DECISIONS}\\${addr}.json

{
  "addr": "${addr}",
  "name": "<the name to apply, or empty for leave_unnamed>",
  "kind": "library_symbol" | "lib_static" | "crt_equivalent" | "leave_unnamed",
  "evidence": "<why this name, and why not the alternatives>",
  "pool_doubt": "<empty, or why the pool looks wrong now>",
  "confidence": "high" | "medium" | "low"
}

Do not change the pool. If your reading says the function is in the wrong pool,
put that in pool_doubt and let it be picked up separately - pool is a different
question with different evidence, and this stage is not set up to weigh it.

Then return the summary. Your final output is data for the workflow.`
}

function rescanPrompt(addr) {
  return namePrompt(addr, `
THIS IS A RE-READ. An earlier pass decided this same function and left it at
less than high confidence.

This time you may also read the other decision files under ${DECISIONS} -- your
neighbours' addresses are one line apart in the disputed list, and a converter
family (itoa / _itoa / ltoa / _ltoa) only makes sense read together. Citing a
decision someone else already made is not making it for them.

A second attempt is not a licence to manufacture a conclusion. If the evidence
still does not identify it, say so again and keep the confidence low.
`)
}

function applyPrompt(tag, ids) {
  return `Apply one round of naming decisions to Ghidra. You are not deciding anything.

Round tag: ${tag}
Functions (${ids.length}): ${ids.join(' ')}

Load the tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__rename_function_by_address,mcp__ghidra__search_functions_enhanced,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

PASS program="FDPS.LE" ON EVERY CALL. Other programs are open in this session.

Steps:

1. For each address, read ${DECISIONS}\\<addr>.json.
   - kind "leave_unnamed", or an empty name: skip it, and say so in problems.
   - otherwise rename the function at that address to the decision's "name"
     with rename_function_by_address.
   Ghidra's tooling warns that names should be PascalCase. That warning is wrong
   for this project and is expected; it is not a failure.
   If a rename is rejected because the name is taken, do NOT invent a variant --
   report it in problems and move on. A clash means two decisions disagree and
   that has to be looked at, not papered over.

   **Ghidra does not reject a duplicate function name, so check for one.** After
   each rename, count how many functions hold the exact name: more than one
   means the decision collides with a name applied elsewhere, and both addresses
   have to be reported in problems. Nothing errors on its own here - an
   unchecked duplicate just sits there looking fine until a rebuild tries to
   resolve it.

   Use \`search_functions_enhanced\` with BOTH \`name_pattern\` and
   \`has_custom_name: true\`. With \`name_pattern\` alone it returns 0 for every
   renamed function - its default view only covers \`FUN_*\` names - so the check
   would read as "no duplicates" no matter what. \`search_functions\` is a
   working fallback if the result still looks wrong.

2. Mirror the name into the pool verdict file so the record and Ghidra agree:
   read ${POOL_VERDICTS}\\<addr>.json, set its "name" field to the same string,
   write it back with UTF-8 and no other change.

3. Run the gate:
     run_ghidra_script  ${AUDIT}
   Orphan code ranges and error bookmarks must both be 0. Renaming cannot create
   orphan code, so a failure means something else moved; report it.

4. Save the program with save_program.

Set ok to false and explain in problems if anything failed. Set
ghidra_responding to false only if Ghidra itself stopped answering.`
}

function planPrompt() {
  return `List the functions still waiting for a naming decision. No judgement: two file
reads.

Read ${DISPUTED} -- a JSON object keyed by address -- and return, as todo, every
key that does NOT already have a ${DECISIONS}\\<addr>.json file. Return the total
number of keys as total. Return todo even if it is empty.`
}

function reportPrompt(payload) {
  return `Write the run report for this workflow. No judgement, no knowledge base.

Write with the Write tool to:

  ${RUN_REPORT}

**If that path already exists, do not overwrite it.** Append \`-2\`, then \`-3\`,
until you find a free name, and say which you used in note.

Content: this JSON, verbatim, pretty-printed, UTF-8.

${payload}

Then return wrote_file.`
}

// ---------------------------------------------------------------- driver

const unfinished = []
const applyReports = []
const decisions = []
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
  if (report.problems) log(`${label}: PROBLEM ${report.problems}`)
  if (!report.ok) unfinished.push(`${label}: ${report.problems || 'apply reported not ok'}`)
  if (report.ghidra_responding === false) {
    halted = true
    haltReason = `${label}: Ghidra stopped responding`
    log(`STOP ${haltReason}`)
  }
  return report.ok
}

function outage(label, got, expected, remaining) {
  if (expected === 0 || got > 0) return false
  halted = true
  haltReason = `${label}: all ${expected} agent(s) in the round returned nothing, `
    + 'which puts the cause upstream of this workflow rather than in any one item'
  log(`STOP ${haltReason}`)
  if (remaining && remaining.length > 0) {
    unfinished.push(`${remaining.length} item(s) not attempted: ${remaining.join(' ')}`)
  }
  return true
}

function chunk(items, size) {
  const out = []
  for (let i = 0; i < items.length; i += size) out.push(items.slice(i, i + size))
  return out
}

phase('Plan')
let todo = arg('addresses', null)
let total = todo ? todo.length : 0
if (!todo) {
  const plan = await agent(planPrompt(), { label: 'plan:disputed', phase: 'Plan', schema: PLAN })
  if (!plan) return { error: 'the planning agent returned nothing' }
  todo = plan.todo
  total = plan.total
}
log(`${total} disputed name(s), ${todo.length} still to decide`)

const rounds = chunk(todo, ROUND_SIZE)
for (let r = 0; r < rounds.length && !halted; r++) {
  const tag = `r${r + 1}`
  phase('Name')
  const got = (await parallel(rounds[r].map((id) => () =>
    agent(namePrompt(id), { label: `name:${id}`, phase: 'Name', schema: DECISION })
  ))).filter(Boolean)

  if (outage(tag, got.length, rounds[r].length, rounds.slice(r).flat())) break

  // Done means the decision file exists, not that the agent said it went well.
  let usable = got.filter((d) => d.wrote_file)
  const lost = rounds[r].filter((id) => !usable.some((d) => d.addr === id))
  if (lost.length > 0) {
    log(`${tag}: ${lost.length} came back without a decision file, retrying`)
    const retried = (await parallel(lost.map((id) => () =>
      agent(namePrompt(id), { label: `name-retry:${id}`, phase: 'Name', schema: DECISION })
    ))).filter(Boolean).filter((d) => d.wrote_file)
    usable = usable.concat(retried)
    for (const id of lost) {
      if (!retried.some((d) => d.addr === id)) {
        unfinished.push(`${id}: no decision file after a retry`)
      }
    }
  }
  for (const d of usable) decisions.push(d)

  if (usable.length > 0) {
    phase('Apply')
    noteApply(`apply:${tag}`,
      await agent(applyPrompt(tag, usable.map((d) => d.addr)),
        { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT }))
  }
}

// ---------------------------------------------------------------- rescan
//
// ADR-0007 條件四. Every agent saw one function, and a name that turns on what
// the neighbours are called cannot be settled on the first pass -- the itoa /
// _itoa / ltoa / _ltoa run is exactly that shape. This is the second look, with
// the decisions already on disk allowed as evidence.

let rescanPass = 0
while (!halted && rescanPass < MAX_RESCAN_PASSES) {
  const open = decisions.filter((d) => d.confidence !== 'high')
  if (open.length === 0) break
  rescanPass++
  log(`rescan pass ${rescanPass}: ${open.length} name(s) to re-read`)
  const passRounds = chunk(open.map((d) => d.addr), ROUND_SIZE)
  for (let r = 0; r < passRounds.length && !halted; r++) {
    const tag = `rescan${rescanPass}r${r + 1}`
    phase('Rescan')
    const got = (await parallel(passRounds[r].map((id) => () =>
      agent(rescanPrompt(id), { label: `rescan:${id}`, phase: 'Rescan', schema: DECISION })
    ))).filter(Boolean).filter((d) => d.wrote_file)
    if (outage(tag, got.length, passRounds[r].length, passRounds[r])) break
    for (const d of got) {
      const i = decisions.findIndex((x) => x.addr === d.addr)
      if (i >= 0) decisions[i] = d; else decisions.push(d)
    }
    if (got.length > 0) {
      phase('Apply')
      noteApply(`apply:${tag}`,
        await agent(applyPrompt(tag, got.map((d) => d.addr)),
          { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT }))
    }
  }
  const stillOpen = decisions.filter((d) => d.confidence !== 'high').map((d) => d.addr).sort()
  if (stillOpen.join(' ') === open.map((d) => d.addr).sort().join(' ')) {
    log(`rescan pass ${rescanPass} settled nothing further; stopping the rescan`)
    break
  }
}

const summary = {
  ticket: '14.1 follow-up',
  workflow: 'name-disputed-symbols',
  disputed_total: total,
  decided: decisions.length,
  named: decisions.filter((d) => d.name).map((d) => ({ addr: d.addr, name: d.name, kind: d.kind })),
  left_unnamed: decisions.filter((d) => !d.name).map((d) => d.addr),
  pool_doubts: decisions.filter((d) => d.pool_doubt).map((d) => ({ addr: d.addr, why: d.pool_doubt })),
  unresolved: decisions.filter((d) => d.confidence !== 'high')
    .map((d) => ({ addr: d.addr, confidence: d.confidence, note: d.note || '' })),
  apply_rounds: applyReports,
  halted,
  halt_reason: haltReason,
  unfinished,
}

log(`decided ${decisions.length}/${total}, named ${summary.named.length}, `
  + `${summary.pool_doubts.length} pool doubt(s), ${unfinished.length} unfinished`)

if (!halted) {
  phase('Report')
  const done = await agent(reportPrompt(JSON.stringify(summary, null, 2)),
    { label: 'report:run', phase: 'Report', schema: REPORT_DONE })
  if (!done || !done.wrote_file) unfinished.push('the run report was not written')
}

return summary
