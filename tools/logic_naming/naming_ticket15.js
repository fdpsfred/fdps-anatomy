export const meta = {
  name: 'logic-naming-ticket15',
  description: 'Name every pool_fdps function of FDPS.LE, with parameters, convention and a behaviour plate comment',
  whenToUse: 'Ticket 15. Give the game-logic half of FDPS.LE names a human can read.',
  phases: [
    { title: 'Plan', detail: 'refresh the dump and work out what is still unnamed' },
    { title: 'Name', detail: 'one agent reads one function and writes one verdict file' },
    { title: 'Apply', detail: 'transcribe the round into Ghidra, re-dump, run both gates' },
    { title: 'Arbitrate', detail: 'two verdicts claiming one symbol' },
    { title: 'Rescan', detail: 're-read the verdicts that stayed open, neighbours now settled' },
    { title: 'Report', detail: 'completed, failed, and what is still unfinished' },
  ],
}

// Ticket 15 -- naming the game logic.
//
// Ticket 14.2 left 514 functions tagged pool_fdps, of which 417 still wear the
// name Ghidra generated for them. This workflow gives each one a name, semantic
// parameter names, a confirmed calling convention and a plate comment saying
// what it does, one function per agent, from the assembly.
//
// The five rules this is built to (ADR-0007):
//
//  1. The verdict goes in a file; the agent returns about 200 bytes. Nothing in
//     this script or any later phase grows with the number of functions handled.
//  2. Naming agents never write to Ghidra. Transcription is its own phase and
//     makes no judgements.
//  3. Every round runs both gates -- the structural baseline audit and this
//     ticket's naming audit -- and a round that breaks one fixes it before the
//     next starts.
//  4. A rescan phase re-reads whatever stayed open, this time allowed to read
//     the neighbours' verdict files.
//  5. Errors are handled, never swallowed: a lost verdict is retried once then
//     recorded, a round where every agent died stops the run, an apply that
//     cannot land something reports it, and the closing report lists what was
//     left undone.
//
// Two things about this ticket's shape that are not in the earlier ones:
//
//  * The worklist is fixed and ordered leaves-first. A function that calls
//    nothing explains itself; one that calls twenty others is mostly a summary
//    of what those twenty do, so it reads better after them. build_worklist.py
//    sorts by callee count for exactly that.
//  * The evidence dump goes stale every round. The single most useful clue for
//    naming a function is what its already-named neighbours are called, so the
//    Apply phase re-runs DumpNamingState.java after landing, and the next round
//    reads names this round produced.
//
// Run it:
//   python tools/logic_naming/build_worklist.py       # take "full" from the output
//   Workflow({ scriptPath: 'tools/logic_naming/naming_ticket15.js',
//              args: { addrs: [...], roundSize: 16, maxFunctions: 120 } })
//
// It is meant to be run several times. Anything with a current verdict file is
// skipped, so each call picks up where the last one stopped -- which matters,
// because 417 functions do not fit in one session.

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOLS = `${REPO}\\tools\\logic_naming`
const WORK = `${REPO}\\workspace\\logic_naming`
const VERDICTS = `${WORK}\\verdicts`
const DUMP = `${WORK}\\dump`
const BASELINE_AUDIT = `${REPO}\\tools\\ghidra_baseline\\AuditGhidraBaseline.java`

const cfg = args || {}
const ROUND_SIZE = cfg.roundSize || 16
const MAX_FUNCTIONS = cfg.maxFunctions || 120
const MAX_RESCAN_PASSES = cfg.maxRescanPasses === undefined ? 2 : cfg.maxRescanPasses
const SKIP_RESCAN = !!cfg.skipRescan

// ---------------------------------------------------------------- schemas

// About 200 bytes. Everything else the agent decided is in the verdict file.
const NAME_SUMMARY = {
  type: 'object',
  required: ['addr', 'name', 'confidence', 'wrote_file', 'has_open_question'],
  properties: {
    addr: { type: 'string' },
    name: { type: 'string', description: 'the symbol name the verdict settled on' },
    subsystem: { type: 'string' },
    cc: { type: 'string' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean', description: 'the verdict file exists on disk' },
    has_open_question: { type: 'boolean' },
    needs_rescan: {
      type: 'boolean',
      description: 'this verdict could change once the neighbours are named',
    },
    has_pitfall: {
      type: 'boolean',
      description: 'the verdict recorded something a rebuild would get wrong by writing the obvious C',
    },
    boundary_issue: { type: 'boolean' },
    ghidra_responding: { type: 'boolean' },
  },
  additionalProperties: false,
}

const APPLY_REPORT = {
  type: 'object',
  required: ['ok', 'applied', 'violations', 'pending', 'ghidra_responding'],
  properties: {
    ok: { type: 'boolean', description: 'both gates clean and nothing left unapplied' },
    applied: { type: 'integer' },
    renamed: { type: 'integer' },
    violations: { type: 'integer', description: 'AuditNaming violations, target 0' },
    pending: { type: 'integer', description: 'pool_fdps functions still wearing FUN_' },
    orphan_ranges: { type: 'integer' },
    error_bookmarks: { type: 'integer' },
    name_conflicts: {
      type: 'array',
      items: { type: 'string', description: '<addr> wanted <name>, held by <addr>' },
    },
    boundary_issues: { type: 'array', items: { type: 'string' } },
    problems: { type: 'array', items: { type: 'string' } },
    ghidra_responding: { type: 'boolean' },
  },
  additionalProperties: false,
}

const WORKLIST = {
  type: 'object',
  required: ['todo', 'settled', 'ghidra_responding'],
  properties: {
    todo: { type: 'array', items: { type: 'string' } },
    settled: { type: 'integer' },
    total: { type: 'integer' },
    ghidra_responding: { type: 'boolean' },
  },
  additionalProperties: false,
}

const OPEN_LIST = {
  type: 'object',
  required: ['addrs'],
  properties: {
    addrs: { type: 'array', items: { type: 'string' } },
    note: { type: 'string' },
  },
  additionalProperties: false,
}

const ARBITRATION = {
  type: 'object',
  required: ['addr', 'name', 'wrote_file'],
  properties: {
    addr: { type: 'string' },
    name: { type: 'string' },
    wrote_file: { type: 'boolean' },
    reason: { type: 'string' },
  },
  additionalProperties: false,
}

const DONE = {
  type: 'object',
  required: ['files'],
  properties: {
    files: { type: 'array', items: { type: 'string' } },
    note: { type: 'string' },
  },
  additionalProperties: false,
}

// ---------------------------------------------------------------- prompts

// Shared preamble. Every naming agent gets the same picture of the job so that
// five hundred of them do not each invent their own dialect.
const BRIEF = `You are working on FDPS.LE, the 32-bit DOS/4G executable of the
Traditional Chinese game 炎龍騎士團外傳 (Flame Dragon Plus), built with Watcom
C/C++ 10.0a. Ticket 14.2 already settled which pool every function belongs to,
where its body starts and ends, and whether it is game code. Your ticket is the
next one: make the game half readable.

Read these before you decide anything (they are short and binding):
  ${REPO}\\rebuild_info\\naming.md         the naming canon
  ${REPO}\\CONTEXT.md                      project vocabulary
  ${WORK}\\vocabulary.md                   how the canon looks applied, plus the
                                           wording the predecessor project FD2
                                           settled on for the same subsystems

Rules that come out of them and are not negotiable:
  * pool_fdps functions are named fdps_ + snake_case. The single exception in
    the whole program is main, and it is already named.
  * No address may appear in a name. "fdps_helper_12ab" is not a name, it is a
    refusal to decide.
  * The Ghidra name and the future C name are the same string, so it has to be a
    legal C identifier and it has to still make sense in a .c file.
  * Reach for a word already in use before inventing one. If the program already
    says blit, do not say draw for the same operation.

Domain facts you will need. The game is a tactical RPG: units stand on a tile
map, take turns, move along traced paths and attack. A "unit record" is 0x50
bytes in the array at DAT_00069cd8, indexed by unit index. Chapters are numbered
1..30 to the player and 0..29 inside the program. Resources live in a VFS
container; CEL is the sprite format, SAF the animation format. When a name would
turn on a game fact you cannot get from the code -- what an item does, what a
class is called -- the strategy guide mirror is searchable:
  python ${REPO}\\tools\\guide_scrape\\... search "<term>"      (see its _index.md)
and the fdps-data skill answers questions about items, spells, classes and
characters.`

const EVIDENCE = `Your evidence is on disk. Read it in this order:

  1. ${DUMP}\\asm\\<addr>.txt   the disassembly, annotated with the names and
                                string literals its operands resolve to. This is
                                the primary evidence and it outranks everything
                                else in this list.
  2. ${DUMP}\\dec\\<addr>.c     Ghidra's decompilation. Useful for seeing the
                                shape of the control flow; never authoritative
                                on its own, and wrong about types often enough
                                that a claim resting only on it is not settled.
  3. ${DUMP}\\ctx\\<addr>.json  the neighbourhood: callers and callees with their
                                current names, prototypes and the first line of
                                their plate comments; every data and string
                                reference the body makes; anything that points at
                                the entry without calling it; the gap to the next
                                function. It also carries current_plate, which is
                                what ticket 14.2 concluded about this function.

current_plate is evidence, not an answer. Ticket 14.2 was asking a different
question -- which pool is this, is the boundary right -- and its behaviour
summary was a by-product of that. Where it and the assembly disagree, the
assembly wins and you say so in your own evidence.

The string literals are usually the fastest way in: a filename, an asset name or
a message tells you what the code around it is for. So does an already-named
caller or callee -- that is why the dump is refreshed every round.`

function namePrompt(addr) {
  return `${BRIEF}

${EVIDENCE}

Your function this time, and the only one you may write a verdict for:

  ${addr}

Decide four things, each on its own evidence:

NAME. What does this function do, and what is the shortest fdps_ name that says
so? Name what it does for its callers, not how it does it. A function that walks
a table looking for a free slot is fdps_..._find_free_slot, not
fdps_..._loop_over_table. If it is a predicate returning 0/1, say so with is_ or
has_. If every caller ignores the return value, do not name it get_.

CALLING CONVENTION. This binary is compiled with wcc386 -4s, so the default is
__cdecl: arguments on the stack from [EBP+0x14] upward after the standard four
pushes, the caller cleaning up with ADD ESP,n after the call, and a plain RET.
Ghidra labels almost everything __watcall from its own analysis; that label is a
guess, not a fact, and for this binary it is usually wrong. __watcall is real
only for hand-written assembly that takes its arguments in EAX/EDX/EBX/ECX. A
function with no arguments is indistinguishable between the two, and takes
__cdecl to match the binary's default. Say which one you saw and where.

PARAMETERS. Name every parameter for what it is used for, from what the body
does with it and from what the callers pass. A parameter used only as an index
into the unit record array is unit_index; one that is a tile coordinate is x or
tile_x; one passed straight through to a named callee takes that callee's name
for it. If you genuinely cannot tell what one is for, name it for its observable
role (flag, mode, count) rather than leaving it param_N -- but say in the
evidence that you could not settle it, and set the open question.

PLATE COMMENT. What a reader who has never seen this function needs. Format:

    <one sentence saying what it is for>

    Algorithm:
    <what the body actually does, in the order it does it. Name the globals and
    the callees. Say what the magic numbers mean when you can tell.>

    Parameters:
      <name> -- <what it is, what range, where it comes from>

    Returns:
      <what, and what the values mean>

    Pool: fdps  [<one line of why, carried over from the current plate>]

    Rebuild note:
    <only when you have a pitfall -- see the field below. Same text.>

The rebuild note is in the plate as well as in the field because the verdict
file is working state and the plate is what survives into the repository. The
person it is written for is whoever writes this function in C, months from now,
looking at the plate comment.

Two constraints on the text. Refer to other functions by the name they carry
right now, or by their address if that name is still FUN_ -- writing a name you
expect a later round to give them leaves a comment that points at nothing. And
do not describe your own investigation; the plate says what the code does.

WRITE THE VERDICT to ${VERDICTS}\\${addr}.json, exactly this shape:

{
  "addr": "${addr}",
  "covers": { "start": "<body start>", "end": "<body end>", "body_sha": "<body_sha from ${DUMP}\\\\functions.json for this address>" },
  "subsystem": "<battle|field|menu|graphics|audio|input|file_io|save|script|cd|memory|string|startup|main_loop|unknown>",
  "name": { "verdict": "fdps_...", "evidence": "<why this name and not another>", "confidence": "high|medium|low" },
  "signature": {
    "cc": "__cdecl|__watcall",
    "prototype": "<return type> <name>(<typed, named parameters>)",
    "assumed": false,
    "evidence": "<the instructions that show it: where arguments are read, how the stack is cleaned, what RET carries>",
    "confidence": "high|medium|low"
  },
  "params": [ { "name": "<param>", "evidence": "<what the body does with it and what callers pass>" } ],
  "plate": { "text": "<the full plate comment>", "confidence": "high|medium|low" },
  "boundary_issue": "",
  "needs_rescan": false,
  "open_question": "",
  "pitfall": ""
}

Notes on the fields:
  * The prototype carries the parameter names -- that is how they reach Ghidra.
    Write it without the calling convention (it has its own field) and without
    const (Ghidra's parser rejects both). Use void f(void) for no parameters.
  * assumed: true means you are guessing the convention. A guessed convention is
    kept out of Ghidra and lives in the plate comment instead, so use it when
    you mean it.
  * boundary_issue: non-empty only if you found something wrong with the body --
    a jump table whose targets are outside it, a fall-through into the next
    function, dead bytes in the middle. Ticket 14.2 settled boundaries and this
    ticket does not move them, so describe what you saw and let it be reported.
  * open_question: what you could not settle and what would settle it. Leave it
    empty only when it is empty. It is a record, not a request -- a question that
    only a later ticket can answer belongs here and does not need a re-read.
  * needs_rescan: true when naming the neighbours could change THIS verdict --
    the name rests on a caller that is still FUN_, or a sibling family needs one
    consistent wording and the siblings are unnamed. That is what the rescan pass
    is for. Set it false when the open question is real but no amount of naming
    would answer it; those get reported, not re-read.
  * pitfall: normally empty. Fill it when you found something that would make a
    faithful C rewrite behave differently from the original -- a register the
    routine destroys that its convention says it should preserve, an off-by-one
    the callers depend on, a bug you must not fix, a comparison that is
    case-sensitive where you would expect otherwise. One or two sentences saying
    what would go wrong and how someone would write it wrong. The threshold is
    "writing the obvious thing gives the wrong answer", not "this is intricate".

Confidence is about the evidence, not about how you feel. If the name rests on
one unnamed caller, it is low, and saying so is what gets it re-read later.

Then return the summary object. Do not put the plate comment, the evidence or
the prototype in what you return -- they are in the file. Set wrote_file only if
you actually wrote it, and ghidra_responding false only if you tried a Ghidra
MCP call and it failed (you should not need one; everything is in the dump).`
}

function rescanPrompt(addr) {
  return `${BRIEF}

${EVIDENCE}

Your function this time, and the only one you may write a verdict for:

  ${addr}

This is a re-read. The first pass left this verdict open -- low confidence, or
an unanswered question -- and since then the neighbours have been named. Two
things changed for you:

  * ${DUMP} was re-exported after every round, so the caller and callee names in
    ctx/${addr}.json are current. What was FUN_ last time may be named now.
  * You may read other functions' verdict files in ${VERDICTS} as evidence. Start
    with this function's own callers and callees. Quoting a judgement someone
    else already made is not making it for them.

The existing verdict is at ${VERDICTS}\\${addr}.json. Read the assembly again
first, then it, then decide whether the new evidence changes anything.

A second attempt is not a licence to manufacture a conclusion. If it is still
not settled, say so: keep the verdict, keep the open question, and write down
what specifically is still missing and what would answer it. An honest low is
worth more than a confident invention, and this is the last pass -- whatever the
open question says here is what goes in the ticket's closing report.

If the evidence does change the verdict, rewrite the file completely in the same
shape it already has, and add one field:

  "_supersedes": "<what the first pass concluded and what changed it>"

Return the summary object either way: the name in it must be the name in the
file after you are done.`
}

function arbitratePrompt(conflict) {
  return `${BRIEF}

Two verdicts claim the same symbol, and only one can have it. The transcription
step reported:

  ${conflict}

The address named second was left unchanged, so right now it still wears its
FUN_ name and its verdict file says something Ghidra refused.

Read both functions' assembly and both verdict files:
  ${DUMP}\\asm\\<addr>.txt for each
  ${VERDICTS}\\<addr>.json for each

Decide which function the name actually describes. Then fix the loser's verdict
file so it names what that function does, in a way that does not collide -- a
different, accurate name, not the same name with a suffix. A suffix is what you
write when you have decided not to decide, and a name is going to be resolved by
wlink against a real library, so two spellings of one idea is a bug.

If the two really are the same function reached two ways -- a thunk and its body
-- naming.md has the rule: the thunk keeps the plain name, the body takes the
address suffix. That is the one case where a suffix is correct, and it is rare;
ticket 14.2 found no thunks in pool_fdps at all, so treat it as a surprise that
needs saying rather than a convenient way out.

Rewrite only the loser's verdict file, keeping its shape. Return which address
you changed and to what.`
}

// --------------------------------------------------------------- helpers

const unfinished = []
const pitfalls = []
const stopped = { yes: false, why: '' }

function stop(why) {
  stopped.yes = true
  stopped.why = why
  log(`STOP: ${why}`)
}

function chunk(list, size) {
  const out = []
  for (let i = 0; i < list.length; i += size) {
    out.push(list.slice(i, i + size))
  }
  return out
}

// ------------------------------------------------------------------ Plan

phase('Plan')

// The worklist lives on disk, and the script cannot read disk. One agent runs
// the builder and hands back the addresses -- which also re-checks every verdict
// against the current body, so a function whose body moved comes back onto the
// list instead of being skipped forever.
async function refreshWorklist(tag) {
  return agent(
    `Refresh the ticket 15 worklist.

Run, from ${REPO}:

  python tools\\logic_naming\\build_worklist.py

It rebuilds ${WORK}\\worklist.json from the dump and the verdict files, retiring
any verdict whose function's body no longer matches the one it was written for.

Read the resulting worklist.json and return:
  todo     the "full" array, in the order the file has it -- it is sorted
           leaves-first on purpose, do not re-sort it
  settled  counts.settled
  total    fdps_count

If the script fails, return an empty todo, settled 0, and ghidra_responding
false, with the error visible in your final message.`,
    { label: `worklist:${tag}`, phase: 'Plan', schema: WORKLIST })
}

let plan = null
if (cfg.addrs && cfg.addrs.length) {
  plan = { todo: cfg.addrs, settled: -1, total: -1, ghidra_responding: true }
  log(`worklist from args: ${plan.todo.length} function(s)`)
}
else {
  plan = await refreshWorklist('initial')
  if (!plan) {
    stop('the worklist agent did not return')
  }
  else {
    log(`worklist: ${plan.todo.length} to do, ${plan.settled} already settled of ${plan.total}`)
  }
}

let queue = stopped.yes ? [] : plan.todo.slice(0, MAX_FUNCTIONS)
if (!stopped.yes && plan.todo.length > queue.length) {
  log(`taking ${queue.length} this call; ${plan.todo.length - queue.length} left for the next one`)
}

// Transcribe one round, refresh the dump, run both gates. The dump refresh is
// the reason this is worth a whole agent: without it every later round would be
// naming functions against a neighbourhood snapshot from before this ticket
// started, and the names its neighbours got would be invisible.
async function applyRound(tag, addrs) {
  if (!addrs.length) {
    return { ok: true, applied: 0, violations: 0, pending: -1, ghidra_responding: true }
  }
  return agent(
    `Transcribe one round of ticket 15 naming verdicts into Ghidra, then check it.

You are not judging anything. Every name, prototype and plate comment in these
verdict files was decided by an agent that read that one function's assembly;
your job is to put them in and report honestly on what happened.

  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program"

Run these in order, all through run_ghidra_script with the absolute path:

  1. ${TOOLS}\\ApplyNamingVerdicts.java
     args: ${VERDICTS} ${addrs.join(' ')}

  2. ${TOOLS}\\DumpNamingState.java
     args: ${DUMP}
     Re-exports the evidence so the next round's agents see the names this round
     produced. It is read-only and takes about twenty seconds.

  3. ${TOOLS}\\build_vocabulary.py -- run it with python from ${REPO}, no args.
     Adds this round's names to the page every naming agent reads.

  4. ${BASELINE_AUDIT}
     args: ${REPO}\\workspace\\ghidra_baseline
     The structural gate. Read only its "# Gate" section: orphan code ranges,
     error bookmarks and undefined bytes must all be 0.

  5. ${TOOLS}\\AuditNaming.java
     args: ${WORK}
     This ticket's gate. violations must be 0. pending is how many pool_fdps
     functions still wear a FUN_ name -- it counts down as the ticket proceeds
     and is not a failure.

  6. save_program()

Then report:

  applied / renamed        from the ApplyNamingVerdicts output
  name_conflicts           every "NAME TAKEN" line, verbatim
  boundary_issues          every "BOUNDARY" line, verbatim
  problems                 every other line under "problems", verbatim
  violations / pending     from AuditNaming
  orphan_ranges / error_bookmarks   from the baseline audit's Gate section
  ok                       true only when violations is 0, orphan ranges 0,
                           error bookmarks 0, and ApplyNamingVerdicts reported
                           no problems other than NAME TAKEN lines

If a gate comes back dirty, fix it in this round before reporting. The two
failures this ticket can actually cause are a rename that left a param_N behind
(re-run the apply for that address after correcting its verdict file's
prototype) and a plate comment that did not land. A NAME TAKEN line is not
yours to fix -- report it and the workflow will arbitrate. If you cannot get a
gate clean, report ok false with the reason in problems; do not paper over it.

If a Ghidra call fails outright or times out, set ghidra_responding false and
stop -- do not retry a dead database.`,
    { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT })
}


// ------------------------------------------------------------ Name/Apply

const done = []
const openVerdicts = []
let roundNo = 0

for (const round of chunk(queue, ROUND_SIZE)) {
  if (stopped.yes) {
    break
  }
  roundNo++
  const tag = `r${roundNo}`
  log(`${tag}: naming ${round.length} function(s)`)

  const summaries = (await parallel(round.map((addr) => () =>
    agent(namePrompt(addr), { label: `name:${addr}`, phase: 'Name', schema: NAME_SUMMARY })
  ))).filter(Boolean)

  // 5.2 -- when a whole round comes back empty the cause is outside this script
  // (session limit, API, machine) and retrying is guaranteed waste.
  if (summaries.length === 0 && round.length > 0) {
    for (const addr of queue.slice(queue.indexOf(round[0]))) {
      unfinished.push(`function ${addr}: not attempted, run stopped`)
    }
    stop(`round ${tag}: every one of ${round.length} agents came back empty -- upstream failure`)
    break
  }

  // 5.5 -- an agent that reached Ghidra and found it dead is a stop signal too,
  // and unlike 5.2 it can fire while agents are still returning.
  if (summaries.some((s) => s.ghidra_responding === false)) {
    stop(`round ${tag}: an agent reported Ghidra is not responding`)
  }

  // 5.1 -- done is decided by the file, not by the agent saying it went well.
  let usable = summaries.filter((s) => s.wrote_file)
  const lost = round.filter((addr) => !usable.some((s) => s.addr === addr))
  if (lost.length && !stopped.yes) {
    log(`${tag}: ${lost.length} function(s) came back without a verdict file, retrying once`)
    const retried = (await parallel(lost.map((addr) => () =>
      agent(namePrompt(addr), { label: `name-retry:${addr}`, phase: 'Name', schema: NAME_SUMMARY })
    ))).filter(Boolean).filter((s) => s.wrote_file)
    usable = usable.concat(retried)
    for (const addr of lost) {
      if (!retried.some((s) => s.addr === addr)) {
        unfinished.push(`function ${addr}: no verdict file after a retry`)
      }
    }
  }

  if (stopped.yes) {
    for (const addr of round) {
      if (!usable.some((s) => s.addr === addr)) {
        unfinished.push(`function ${addr}: judged but not landed, run stopped`)
      }
    }
    break
  }

  const report = await applyRound(tag, usable.map((s) => s.addr))
  if (!report) {
    stop(`round ${tag}: the transcription agent did not return`)
    for (const addr of usable.map((s) => s.addr)) {
      unfinished.push(`function ${addr}: verdict written but not transcribed`)
    }
    break
  }
  if (report.ghidra_responding === false) {
    stop(`round ${tag}: Ghidra stopped responding during transcription`)
    break
  }

  // A name collision is two verdicts contradicting each other about what a
  // function is. Settle it now: leaving it means the loser keeps a FUN_ name
  // and the run reports itself complete with a hole in it.
  const conflicts = report.name_conflicts || []
  if (conflicts.length) {
    log(`${tag}: ${conflicts.length} name collision(s), arbitrating`)
    const fixed = (await parallel(conflicts.map((c) => () =>
      agent(arbitratePrompt(c), { label: `arbitrate:${tag}`, phase: 'Arbitrate', schema: ARBITRATION })
    ))).filter(Boolean).filter((a) => a.wrote_file)
    if (fixed.length) {
      const second = await applyRound(`${tag}-arb`, fixed.map((a) => a.addr))
      if (!second || !second.ok) {
        unfinished.push(`round ${tag}: name collisions still unresolved after arbitration`)
      }
    }
    for (const c of conflicts) {
      if (!fixed.length) {
        unfinished.push(`name collision unresolved: ${c}`)
      }
    }
  }

  if (!report.ok) {
    // 5.4 -- a gate that will not come clean stops the run rather than letting
    // later rounds pile on top of a broken database.
    stop(`round ${tag}: gate failed -- ${report.violations} violation(s), `
      + `${report.orphan_ranges} orphan range(s), ${report.error_bookmarks} error bookmark(s)`)
    for (const p of (report.problems || [])) {
      unfinished.push(`round ${tag}: ${p}`)
    }
    break
  }

  for (const b of (report.boundary_issues || [])) {
    unfinished.push(`boundary issue reported, not acted on: ${b}`)
  }

  for (const s of usable) {
    done.push(s)
    if (s.confidence === 'low' || s.needs_rescan) {
      openVerdicts.push(s.addr)
    }
    if (s.has_pitfall) {
      pitfalls.push(s.addr)
    }
  }
  log(`${tag}: landed ${report.applied}, ${report.pending} still unnamed program-wide`)
}

// ---------------------------------------------------------------- Rescan

// Every agent sees exactly one function, so a verdict that depends on a
// neighbour is unanswerable until that neighbour is named. This is the pass
// where those come back, with the neighbours settled and their verdict files
// readable.
if (!stopped.yes && !SKIP_RESCAN && MAX_RESCAN_PASSES > 0) {
  phase('Rescan')

  // The in-memory list only has this call's rounds in it. Earlier calls left
  // open verdicts on disk too, and they are just as unfinished.
  const collected = await agent(
    `List the ticket 15 verdicts that are still open.

Read every ${VERDICTS}\\*.json (ignore .superseded-*, .orphan and .unparseable
files -- those are retired) and return the addr of each one where any of these
holds:

  * needs_rescan is true
  * name.confidence is "low"
  * signature.confidence is "low"
  * signature.assumed is true

A non-empty open_question on its own is NOT a reason to re-read. Most of them
record something no amount of naming will answer -- what a global is for, what a
value's units are -- and re-reading those spends an agent to write the same
sentence again. The agent that wrote the verdict said with needs_rescan whether
naming the neighbours could actually move it; trust that field.

Return just the addresses. Do not read the assembly and do not judge anything;
this is a listing.`,
    { label: 'rescan:collect', phase: 'Rescan', schema: OPEN_LIST })

  let open = collected ? collected.addrs : []
  for (const addr of openVerdicts) {
    if (!open.includes(addr)) {
      open.push(addr)
    }
  }
  log(`rescan: ${open.length} verdict(s) still open`)

  for (let pass = 1; pass <= MAX_RESCAN_PASSES && open.length && !stopped.yes; pass++) {
    const batch = open.slice(0, MAX_FUNCTIONS)
    log(`rescan pass ${pass}: re-reading ${batch.length}`)
    const results = []
    for (const group of chunk(batch, ROUND_SIZE)) {
      const got = (await parallel(group.map((addr) => () =>
        agent(rescanPrompt(addr), { label: `rescan:${addr}`, phase: 'Rescan', schema: NAME_SUMMARY })
      ))).filter(Boolean)
      if (got.length === 0 && group.length > 0) {
        stop(`rescan pass ${pass}: every agent came back empty -- upstream failure`)
        break
      }
      results.push(...got.filter((s) => s.wrote_file))
    }
    if (stopped.yes) {
      break
    }

    const report = await applyRound(`rescan${pass}`, results.map((s) => s.addr))
    if (!report || !report.ok) {
      stop(`rescan pass ${pass}: transcription or gate failed`)
      break
    }

    const stillOpen = results.filter((s) => s.confidence === 'low' || s.needs_rescan)
      .map((s) => s.addr)
    // A pass that resolved nothing will not resolve anything next time either.
    if (stillOpen.length === batch.length) {
      log(`rescan pass ${pass} settled none of ${batch.length}; stopping the rescan here`)
      open = stillOpen
      break
    }
    open = stillOpen.concat(open.slice(batch.length))
  }

  for (const addr of open) {
    unfinished.push(`function ${addr}: verdict still open after the rescan`)
  }
}

// ---------------------------------------------------------------- Report

phase('Report')

// 5.6 -- nothing that looks like a finished deliverable gets written after a
// stop. A half-populated knowledge base page is indistinguishable from a
// complete one, which is exactly what makes it dangerous.
const status = await agent(
  `Report where ticket 15 stands, from the files -- do not take anything on trust.

  ToolSearch "select:mcp__ghidra__run_ghidra_script"

  1. python ${REPO}\\tools\\logic_naming\\build_worklist.py  (from ${REPO})
  2. run_ghidra_script ${TOOLS}\\AuditNaming.java  args: ${WORK}

Write ${WORK}\\run_report.md with:
  * how many pool_fdps functions there are, how many are named, how many are
    still pending
  * the violation count and every violation line if there are any
  * how many verdict files exist, and how many of them are still open (low
    confidence, needs_rescan, or an assumed convention)
  * every verdict whose "pitfall" field is non-empty, as a list of
    "<addr> <name> -- <the pitfall text>". These are the findings that say a
    faithful-looking C rewrite would behave differently from the original, and
    they are the reason to read this report at all. Do NOT write them into
    rebuild_info/pitfalls.md yourself -- they are collected across every run of
    this ticket and folded in once, and one run writing its own would duplicate
    what another run already put there.
  * ${stopped.yes ? `THE RUN STOPPED: ${stopped.why}` : 'the run completed its queue'}
  * this unfinished list, verbatim:
${unfinished.length ? unfinished.map((u) => `      - ${u}`).join('\n') : '      (nothing)'}

Return the path you wrote. Do not write any knowledge-base page and do not
update any _index.md -- that happens once the whole ticket is through, not per
run.`,
  { label: 'report', phase: 'Report', schema: DONE })

return {
  stopped: stopped.yes,
  stop_reason: stopped.why,
  attempted: queue.length,
  landed: done.length,
  open_after_rescan: openVerdicts.length,
  pitfalls_found: pitfalls,
  unfinished,
  report: status ? status.files : [],
}
