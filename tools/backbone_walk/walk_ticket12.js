// Ticket 12, start to finish, with no human in the loop.
//
// Walks the backbone from main outwards, names every function it reaches,
// applies the results to Ghidra, tags the clusters, and writes the knowledge
// base page and the devlog entry.
//
// Two properties are load-bearing:
//
//   One function per agent. The worklist lives in this script; every agent()
//   call carries exactly one address and no agent ever sees the list
//   (ADR-0002). Rounds are discovered, not scheduled: each reader reports which
//   of its callees carry the backbone forward, and that is the next frontier.
//
//   Bounded context everywhere. A reader writes its full judgement -- plate
//   comment, evidence, prototype -- to verdicts/<addr>.json and returns only a
//   ~200 byte summary. So neither this script nor any later stage grows with
//   the number of functions walked; the synthesis step reads a compact index,
//   never a hundred plate comments.
//
// args: {
//   seeds:          ["0002bae0", ...]   addresses to start from
//   alreadyDone:    ["00029220", ...]   walked in an earlier run, do not revisit
//   maxFunctions:   number              hard cap on newly walked functions
//   maxRounds:      number              hard cap on BFS depth
// }

export const meta = {
  name: 'backbone-walk-ticket12',
  description: 'Walk, name, apply, tag and document the FDPS.LE backbone end to end',
  phases: [
    { title: 'Walk', detail: 'BFS from the seeds, one reader agent per function' },
    { title: 'Arbitrate', detail: 'resolve duplicate names, one agent per clash' },
    { title: 'Apply', detail: 'transcribe each round into Ghidra' },
    { title: 'Tag', detail: 'pool, subsystem and shared-helper tags' },
    { title: 'Document', detail: 'architecture page and devlog' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\backbone_walk'
const VERDICTS = WS + '\\verdicts'

// ---------------------------------------------------------------- schemas

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'name', 'pool', 'subsystem', 'confidence', 'walk_next', 'wrote_file'],
  properties: {
    addr: { type: 'string', description: '8-hex address, exactly as given' },
    name: { type: 'string', description: 'The symbol name written into the verdict file' },
    pool: { type: 'string', enum: ['fdps', 'crt', 'ail', 'binary_artifact', 'unknown'] },
    subsystem: { type: 'string' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    walk_next: {
      type: 'array',
      items: { type: 'string' },
      description: 'Callee addresses that carry the backbone forward. Empty for leaves.',
    },
    wrote_file: { type: 'boolean', description: 'True once verdicts/<addr>.json exists' },
    note: { type: 'string', description: 'At most one short line for the orchestrator, or empty' },
  },
}

const ARBITRATION = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'final_name', 'reason'],
  properties: {
    addr: { type: 'string' },
    final_name: { type: 'string' },
    reason: { type: 'string', description: 'One sentence on why this name and not the other' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'prototypes_set', 'orphan_ranges', 'error_bookmarks', 'ok'],
  properties: {
    applied: { type: 'integer' },
    prototypes_set: { type: 'integer' },
    orphan_ranges: { type: 'integer', description: 'From the baseline audit gate. Must be 0.' },
    error_bookmarks: { type: 'integer', description: 'From the baseline audit gate. Must be 0.' },
    ok: { type: 'boolean' },
    problems: { type: 'string', description: 'What failed and what was done about it, or empty' },
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

const NAMING_RULES = `Naming, from rebuild_info/naming.md -- project rules, not suggestions:

  game logic    fdps_ prefix, snake_case      fdps_rle_blit_sprite
  Watcom CRT    crt_ prefix, snake_case       crt_memcpy
  Miles AIL     AIL_ prefix, upstream casing  AIL_startup
  C entry point main                          the only name with no prefix

Never propose a PascalCase name. Ghidra's tooling warns that names should be
PascalCase; that warning is wrong for this project and is expected. Do not
comply with it.`

function readerPrompt(addr) {
  return `You are identifying ONE function in FDPS.LE, a 1997 DOS game executable
(32-bit Watcom C/C++ 10.0a, DOS/4G LE module) being reverse engineered.

Your function: ${addr}

This is the only function you work on. Never read, judge or name another
function. If a neighbour looks interesting, put its address in walk_next.

READ-ONLY on Ghidra. Do not call any Ghidra tool that writes: no rename, no
set_plate_comment, no set_function_prototype, no create_label. The workflow
applies every change later. A Ghidra write from you is a defect.

Keep your work bounded. Aim for at most about 15 tool calls. Read your own
function's disassembly and decompilation, look at the data and strings it
references, and get the NAMES of its callers and callees -- do not disassemble
them. If the answer is not clear after that, say so rather than digging further.

Load what you need in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_function_callees,mcp__ghidra__get_xrefs_from,mcp__ghidra__read_memory"

How to judge:
1. The disassembly is the primary source.
2. The decompiled C is a second opinion only. It invents parameters, and
   unaff_EBX in a signature almost always means Ghidra guessed wrong rather
   than that the function really reads EBX.
3. Follow references out: strings, data addresses, constants. This executable
   contains no Chinese strings and has no import table, so string evidence is
   scarce and worth a lot when you find it.
4. Caller and callee counts matter. A function called once during startup is a
   different thing from one called from forty places.

${NAMING_RULES}

If you cannot tell what the function does, set confidence to "low" and write
what is missing into open_question. A wrong confident name costs far more than
an honest unknown: it gets copied into the knowledge base and into the rebuilt
C source. Never invent a purpose to fill a field.

WRITE YOUR VERDICT TO A FILE. Use the Write tool to create exactly:

  ${VERDICTS}\\${addr}.json

with this shape, UTF-8, no trailing commentary:

{
  "addr": "${addr}",
  "name": "<symbol name per the rules above>",
  "pool": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
  "subsystem": "startup|main_loop|battle|menu|graphics|input|file_io|audio|cd|math|memory|string|unknown",
  "role": "<one English sentence>",
  "plate_comment": "<English plate comment, plain text, no decorative borders, containing the sections Algorithm, Parameters and Returns>",
  "prototype": "<C signature WITHOUT any calling convention token, e.g. void fdps_draw_frame(int x, int y)>",
  "calling_convention": "__watcall" | "__cdecl",
  "no_return": true | false,
  "confidence": "high" | "medium" | "low",
  "evidence": "<why the name and pool are right>",
  "open_question": "<what could not be settled, or empty>",
  "walk_next": ["<callee addresses worth walking>"]
}

Two hard rules for those fields:

  prototype must NOT contain __watcall or __cdecl, and must NOT use const.
  Ghidra's parser rejects the signature outright on a convention token
  ("Can't resolve return type") and cannot resolve "const char *" in this
  program. Write "char *" and put the convention in its own field.

  calling_convention: this binary was built with wcc386 -4s, the STACK
  convention, so the default for game code is __cdecl -- arguments on the
  stack starting at [ebp+0x14], caller cleans up, prologue PUSH EBX/ESI/EDI/EBP
  (53 56 57 55 89 e5). Use __watcall only when the disassembly actually shows
  an argument arriving in a register, which in practice means hand-written
  assembly in the runtime. For a function that takes no arguments at all the
  two are indistinguishable; pick __cdecl to match the binary's default.
  See rebuild_info/build_flags.md.

  walk_next carries the backbone forward. Include callees that are subsystem
  entry points or that the main loop dispatches to. Exclude runtime helpers,
  leaf utilities, and anything in the crt or ail pools -- those are named by a
  different ticket and walking into them wastes the budget.

Then return the summary. Your final output is data for the workflow, not a
message to a human.`
}

function arbiterPrompt(addr, myName, otherAddr, otherName) {
  return `Two functions in FDPS.LE ended up with the same proposed symbol name.
Names must be unique, so one of them has to change. You are deciding for ONE of
them.

Your function:  ${addr}, currently proposing "${myName}"
The other one:  ${otherAddr}, proposing "${otherName}"

Read both verdict files to see what each one actually is:
  ${VERDICTS}\\${addr}.json
  ${VERDICTS}\\${otherAddr}.json

You may also look at the disassembly of your own function to break a tie. Do
not re-judge what the functions do -- both readings were done carefully. You
are only choosing a name.

${NAMING_RULES}

Decide the final name for ${addr} ONLY. Prefer keeping the more specific or
more canonical name on the function that earns it, and give the other a name
that stays honest about what it is: a suffix like _body, _impl or a term drawn
from its actual role, never a bare number.

Then use Edit or Write to update ONLY the "name" field of
${VERDICTS}\\${addr}.json, leaving every other field untouched. If you change
the name away from what the reader proposed, also prepend one short paragraph
to that file's plate_comment explaining why this function does not carry the
other name. Do not touch the other function's file.

Return the final name.`
}

function applyPrompt(tag, addrs) {
  return `Transcribe one round of reviewed backbone-walk verdicts into Ghidra. You are
not judging anything: every name, plate comment and prototype was decided by an
agent that read that one function. Your job is to get them in without damage.

Round tag: ${tag}
Addresses (${addrs.length}): ${addrs.join(' ')}

Load the Ghidra tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__set_function_prototype,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

Steps, in order:

1. Collect the verdicts:
     python ${REPO}\\tools\\backbone_walk\\collect_verdicts.py "${VERDICTS}" "${WS}" ${tag} ${addrs.join(' ')}
   If it reports NO VERDICT FILE for an address, carry on without it and say so
   in problems. If it reports DUPLICATE NAME, stop and report that -- do not
   invent a name to break the tie.

2. Apply names, plate comments and no-return flags:
     run_ghidra_script  ${REPO}\\tools\\backbone_walk\\ApplyBackboneWalk.java
     args: ${WS}\\apply_${tag}.json

3. Set the prototypes. ${WS}\\protos_${tag}.txt has one per line, tab
   separated: address, calling convention, signature. For each line call
   set_function_prototype with function_address = the address, prototype = the
   signature, calling_convention = the convention.

   IMPORTANT: never put __watcall inside the prototype string. It fails with
   "Can't resolve return type". It only works through the calling_convention
   parameter.

4. Run the gate:
     run_ghidra_script  ${REPO}\\tools\\ghidra_baseline\\AuditGhidraBaseline.java
   Read the "# Gate" section. Both orphan code ranges and error bookmarks must
   be 0.

5. If orphan code ranges is not 0, fix it. The known cause is step 2 marking a
   function no-return: Ghidra then drops the fall-through at each of its call
   sites, and any instructions after such a call fall out of the enclosing
   function's body. When those instructions are reached by a jump from
   somewhere else they are real code that just lost its owner. Repair by
   restoring the enclosing function's body rather than by undoing the no-return
   flag, using run_ghidra_script with an inline script that calls
   Function.setBody with the missing range added. Re-run the gate afterwards.

6. Save the program with save_program.

Report the numbers. If anything is still wrong after your repair attempt, say
so plainly in problems and set ok to false -- do not paper over it.`
}

// ------------------------------------------------------------------ walk

const seeds = (args && args.seeds) || []
const alreadyDone = new Set(((args && args.alreadyDone) || []).map((a) => a.toLowerCase()))
const MAX_FUNCTIONS = (args && args.maxFunctions) || 100
const MAX_ROUNDS = (args && args.maxRounds) || 8

const visited = new Set(alreadyDone)
const walked = []
const nameOwner = new Map()
const lowConfidence = []
const applyReports = []

let frontier = seeds.map((a) => a.toLowerCase()).filter((a) => !visited.has(a))
let round = 0

while (frontier.length > 0 && round < MAX_ROUNDS && walked.length < MAX_FUNCTIONS) {
  round++
  const budget = MAX_FUNCTIONS - walked.length
  const batch = frontier.slice(0, budget)
  const deferred = frontier.slice(budget)
  if (deferred.length > 0) {
    log(`round ${round}: budget reached, ${deferred.length} address(es) not walked: ${deferred.join(' ')}`)
  }
  batch.forEach((a) => visited.add(a))

  phase('Walk')
  log(`round ${round}: reading ${batch.length} function(s)`)
  const summaries = (await parallel(batch.map((addr) => () =>
    agent(readerPrompt(addr), { label: `read:${addr}`, phase: 'Walk', schema: SUMMARY })
  ))).filter(Boolean)

  const usable = summaries.filter((s) => s.wrote_file)
  const lost = summaries.filter((s) => !s.wrote_file)
  if (lost.length > 0) {
    log(`round ${round}: ${lost.length} reader(s) reported no verdict file: ${lost.map((s) => s.addr).join(' ')}`)
  }

  // Duplicate names are a program-wide problem, so check against every name
  // taken so far, not just this round's.
  phase('Arbitrate')
  const clashes = []
  for (const s of usable) {
    const owner = nameOwner.get(s.name)
    if (owner && owner !== s.addr) {
      clashes.push({ addr: s.addr, name: s.name, otherAddr: owner })
    } else {
      nameOwner.set(s.name, s.addr)
    }
  }
  if (clashes.length > 0) {
    log(`round ${round}: ${clashes.length} duplicate name(s) to arbitrate`)
    const decided = (await parallel(clashes.map((c) => () =>
      agent(arbiterPrompt(c.addr, c.name, c.otherAddr, c.name), {
        label: `arbitrate:${c.addr}`,
        phase: 'Arbitrate',
        schema: ARBITRATION,
      })
    ))).filter(Boolean)
    for (const d of decided) {
      const s = usable.find((u) => u.addr === d.addr)
      if (s) {
        s.name = d.final_name
      }
      nameOwner.set(d.final_name, d.addr)
      log(`  ${d.addr} -> ${d.final_name}: ${d.reason}`)
    }
  }

  phase('Apply')
  const applied = usable.map((s) => s.addr)
  if (applied.length > 0) {
    const report = await agent(applyPrompt(`r${round}`, applied), {
      label: `apply:round${round}`,
      phase: 'Apply',
      schema: APPLY_REPORT,
    })
    if (report) {
      applyReports.push(Object.assign({ round: round }, report))
      log(`round ${round}: applied=${report.applied} protos=${report.prototypes_set} `
        + `orphans=${report.orphan_ranges} errors=${report.error_bookmarks} ok=${report.ok}`)
      if (!report.ok) {
        log(`round ${round}: PROBLEM ${report.problems}`)
      }
    }
  }

  for (const s of usable) {
    walked.push(s)
    if (s.confidence === 'low') {
      lowConfidence.push(s)
    }
  }

  const next = []
  for (const s of usable) {
    for (const a of s.walk_next || []) {
      const norm = a.toLowerCase()
      if (!visited.has(norm) && !next.includes(norm)) {
        next.push(norm)
      }
    }
  }
  frontier = next
  log(`round ${round}: ${walked.length} walked in total, next frontier ${frontier.length}`)
}

const stoppedBecause = frontier.length === 0 ? 'the frontier emptied'
  : walked.length >= MAX_FUNCTIONS ? `the ${MAX_FUNCTIONS} function budget ran out`
  : `the ${MAX_ROUNDS} round limit was reached`
log(`walk finished after ${round} round(s): ${stoppedBecause}`)
if (frontier.length > 0) {
  log(`UNWALKED FRONTIER (${frontier.length}): ${frontier.join(' ')}`)
}

// ------------------------------------------------------------------- tag

phase('Tag')
const tagReport = await agent(
  `Tag the walked functions in FDPS.LE so the clusters are queryable.

Load tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program,mcp__ghidra__list_function_tags"

1. Refresh the compact index over every verdict written so far:
     python ${REPO}\\tools\\backbone_walk\\collect_verdicts.py "${VERDICTS}" "${WS}" all
   Note whether it reports any duplicate names.

2. Apply the tags:
     run_ghidra_script  ${REPO}\\tools\\backbone_walk\\TagBackbone.java
     args: ${WS}\\index.json ${REPO}\\workspace\\call_graph\\graph.json

3. Save the program.

Report what it printed: how many functions got backbone, pool, subsystem and
shared_helper tags, and the pool breakdown. If the script fails, report the
error rather than working around it.`,
  { label: 'tag:clusters', phase: 'Tag', schema: DONE }
)
if (tagReport) {
  log(`tags: ${tagReport.summary}`)
}

// -------------------------------------------------------------- document

phase('Document')

const bySubsystem = {}
for (const s of walked) {
  bySubsystem[s.subsystem] = (bySubsystem[s.subsystem] || 0) + 1
}
const byPool = {}
for (const s of walked) {
  byPool[s.pool] = (byPool[s.pool] || 0) + 1
}
const stats = {
  walked: walked.length,
  rounds: round,
  stoppedBecause: stoppedBecause,
  unwalkedFrontier: frontier,
  bySubsystem: bySubsystem,
  byPool: byPool,
  lowConfidence: lowConfidence.map((s) => `${s.addr} ${s.name}`),
  applyReports: applyReports,
}

const docs = await parallel([
  () => agent(
    `Write the architecture overview for the FDPS.LE reverse engineering project.
This is the knowledge base deliverable of ticket 12: the reader should come away
knowing how this program is put together.

Sources, in order of authority:
  ${WS}\\index.json                      every walked function: address, name, pool, subsystem, role
  ${REPO}\\workspace\\call_graph\\report.md   reachability, clusters, shared helpers, pointer tables
  ${VERDICTS}\\                          full per-function verdicts; open specific ones when you
                                         need detail, do not read them all

Read index.json and the call graph report in full. Open individual verdict
files only for the functions the overview actually discusses -- the startup
chain, main, the main loop, and each subsystem entry point.

Write ${REPO}\\program_info\\architecture.md in TRADITIONAL CHINESE, following
the conventions of the existing pages in that folder -- read
${REPO}\\program_info\\memory_layout.md first to match the house style.

Requirements:
  - Start with a bold 驗證對象 line naming what this page owns.
  - Cover: the startup chain from the LE entry point to main; what main does;
    the shape of the main loop and how it dispatches; the entry point of each
    subsystem that was identified; and which clusters are shared helper code
    that must stay together when work is partitioned.
  - Conclusions only. No narrative of how the analysis went, no "we first
    thought", no phases, no dates. That belongs in the devlog.
  - Every address as 8 hex digits in backticks.
  - Do not duplicate facts owned by memory_layout.md (pointer tables, object
    layout, BSS boundaries). Link to it instead.
  - Say plainly what is still unknown, including any function whose identity
    was only low confidence and any part of the backbone that was not walked.

Then add a row for architecture.md to the table in
${REPO}\\program_info\\_index.md.

Statistics from this run, for the parts of the page that need them:
${JSON.stringify(stats, null, 2)}

Return the list of files you wrote.`,
    { label: 'doc:architecture', phase: 'Document', schema: DONE }
  ),
  () => agent(
    `Write the devlog entry for this backbone walk.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly. In short:
narrative, rambling is allowed, and the point is to record the dead ends --
successful paths end up in the knowledge base and the code, failed ones are the
only information that would otherwise be lost.

Write ${REPO}\\devlog\\2026-08-15-backbone-walk-auto.md in TRADITIONAL CHINESE.

What this run was: a self-driving workflow for ticket 12. It walks the backbone
breadth first from a set of seed addresses, one reader agent per function, each
agent read-only and writing its full judgement to a file so that neither the
workflow script nor any later stage grows with the number of functions walked.
Duplicate names are arbitrated by a separate agent per clash. Each round is
transcribed into Ghidra by one apply agent that also runs the baseline audit
gate. Then the clusters are tagged and the knowledge base page written.

Sources you may read for detail:
  ${WS}\\index.json
  ${VERDICTS}\\        open a few, especially any listed as low confidence

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly: how far the walk actually got and why it stopped; anything the
apply stage had to repair and how; every function that came back low
confidence and what was missing; and any part of the frontier left unwalked.
If the run went cleanly, say so briefly rather than padding.

Do not claim anything the statistics do not support.

Return the list of files you wrote.`,
    { label: 'doc:devlog', phase: 'Document', schema: DONE }
  ),
]).then((r) => r.filter(Boolean))

return {
  walked: walked.length,
  rounds: round,
  stoppedBecause: stoppedBecause,
  unwalkedFrontier: frontier,
  byPool: byPool,
  bySubsystem: bySubsystem,
  lowConfidence: stats.lowConfidence,
  applyReports: applyReports,
  documents: docs.map((d) => d.written).flat(),
}
