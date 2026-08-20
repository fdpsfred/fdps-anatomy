// Ticket 14.2, start to finish, with no human in the loop.
//
// Ticket 14 gave every function in FDPS.LE a pool, a name and a plate comment.
// This workflow reads all of them again, with a second pair of eyes, and fixes
// what is wrong. The reason it exists is that every error ticket 14 made was
// found only where a second line of evidence happened to exist -- the AIL
// library comparison, the mixer dispatch table, the sprintf/spawnlp clash --
// and the largest block of judgements, the 517 functions left over as fdps,
// never had one at all.
//
// One agent, one function, four independent axes: pool, name, boundary and
// signature, plate comment. Nothing is judged in batches (ADR-0002); the
// worklist lives in this script and in files, and no agent ever sees it.
//
// The shape that is specific to this ticket:
//
//   Read the evidence before reading the answer. The dump is split into
//   items/facts/ (bytes, call edges, references, library hits) and
//   items/current/ (name, tags, signature, plate comment). The prompt orders
//   the two reads and the verdict records the independent conclusion, so
//   "re-read" cannot quietly degrade into "find reasons for the existing tag".
//
//   A boundary fix retires its neighbours. Changing where a function ends
//   changes what the functions around it are, so ApplyRereviewVerdicts logs
//   every boundary change and build_packets.py retires the verdicts inside the
//   affected range -- they go back on the worklist and get judged again.
//
// ADR-0007's five rules, and where they are:
//   1 verdict files, ~200 byte summaries          reviewPrompt, REVIEW_SUMMARY
//   2 review agents never write to Ghidra          reviewPrompt, applyPrompt
//   3 a gate after every apply round               applyPrompt, APPLY_REPORT
//   4 a rescan pass that may read neighbours       rescanPrompt
//   5 error handling, all seven clauses            outage(), retries, halted
//
// args: {
//   functionIds:   the worklist for this run; when absent the plan stage reads
//                  it off disk (long, so the caller normally supplies it)
//   maxFunctions:  cap on functions judged this run     (default 900)
//   roundSize:     functions per apply round            (default 30)
//   refresh:       false to skip the Ghidra re-dump at the start (default true)
//   maxRescanPasses: re-reads of unresolved verdicts    (default 2)
//   report:        false to skip the knowledge base and devlog stage
// }

export const meta = {
  name: 'pool-rereview-ticket14-2',
  description: 'Re-read every function in FDPS.LE on four axes and fix what ticket 14 got wrong',
  phases: [
    { title: 'Plan', detail: 're-dump the program state and rebuild the packets' },
    { title: 'Review', detail: 'one agent per function, four axes' },
    { title: 'Apply', detail: 'transcribe a round, then run both gates' },
    { title: 'Rescan', detail: 're-read what stayed unresolved, neighbours allowed' },
    { title: 'Report', detail: 'recount the pools, update the knowledge base, devlog' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\pool_rereview'
const VERDICTS = WS + '\\verdicts'
const TOOLS = REPO + '\\tools\\pool_rereview'
const BASELINE_AUDIT = REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java'

function arg(name, fallback) {
  return args && args[name] !== undefined ? args[name] : fallback
}

const MAX_FUNCTIONS = arg('maxFunctions', 900)
const ROUND_SIZE = arg('roundSize', 30)
const MAX_RESCAN_PASSES = arg('maxRescanPasses', 2)

// ---------------------------------------------------------------- schemas

const REVIEW_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'pool', 'confidence', 'wrote_file', 'has_open_question', 'changed_axes'],
  properties: {
    addr: { type: 'string', description: '8-hex entry point, exactly as given' },
    pool: { type: 'string', enum: ['fdps', 'crt', 'ail', 'binary_artifact', 'unknown'] },
    name: { type: 'string', description: 'The name the verdict settles on, or empty' },
    changed_axes: {
      type: 'array',
      items: { type: 'string', enum: ['pool', 'name', 'boundary', 'signature', 'plate'] },
      description: 'Which axes the re-read changed. Empty means the first pass was right.',
    },
    boundary_fix: { type: 'string', enum: ['none', 'set_body', 'split', 'delete'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    has_open_question: { type: 'boolean' },
    wrote_file: { type: 'boolean' },
    note: { type: 'string', description: 'At most one short line, or empty' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'orphan_ranges', 'error_bookmarks', 'name_collisions', 'ok',
    'ghidra_responding'],
  properties: {
    applied: { type: 'integer' },
    renamed: { type: 'integer' },
    boundary_changes: { type: 'integer' },
    orphan_ranges: { type: 'integer', description: 'From the baseline gate. Must be 0.' },
    error_bookmarks: { type: 'integer', description: 'From the baseline gate. Must be 0.' },
    undefined_bytes: { type: 'integer', description: 'From the baseline gate. Must be 0.' },
    name_collisions: { type: 'integer', description: 'From AuditNames. Must be 0.' },
    collision_addrs: {
      type: 'array',
      items: { type: 'string' },
      description: 'Every address involved in a name collision, so the rescan can re-read them',
    },
    ok: { type: 'boolean' },
    ghidra_responding: {
      type: 'boolean',
      description: 'False only when Ghidra itself stopped answering, which stops the run',
    },
    problems: { type: 'string' },
  },
}

const REFRESH = {
  type: 'object',
  additionalProperties: false,
  required: ['functions_total', 'ok'],
  properties: {
    functions_total: { type: 'integer' },
    todo_count: { type: 'integer' },
    retired: {
      type: 'array',
      items: { type: 'string' },
      description: 'Verdicts retired because the function moved or a neighbour did',
    },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const RESCAN = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'changed', 'confidence', 'still_open'],
  properties: {
    addr: { type: 'string' },
    changed: { type: 'boolean' },
    pool: { type: 'string' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    still_open: { type: 'boolean' },
    what_resolved: { type: 'string' },
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

const POOL_RULES = `THE FOUR POOLS

  fdps             the game's own code, to be rebuilt as C.
  crt              Watcom C runtime, maths library, 80x87 emulator: whatever came
                   out of CLIB3S.LIB, MATH387S.LIB or EMU387.LIB.
  ail              Miles Audio Interface Library 3.02.
  binary_artifact  code no source line produces: linker jump islands, merged
                   epilogues, convention shims THIS link emitted.

WHAT COUNTS AS EVIDENCE, strongest first:

  1 Library byte match. A Function ID hit names the library module the bytes came
    from. The packet carries two sets: "watcom" against the real Watcom 10.0x
    libraries, and "ail" against ailv3.lib, which the FD2 project synthesised
    from FD2.LE -- an ail hit therefore proves "the same bytes as FD2", not "the
    same as a Miles release".
  2 Link direction. A third-party library is linked as compiled objects, so its
    modules cannot call symbols defined in the game's source. A function called
    only by AIL modules is AIL. The reverse does not hold: the game calling a
    public AIL entry point is the normal way to use it.
  3 String references. The AIL debug build prints its own API names; a function
    reached by such a format string is named by its own library's data.
  4 Shared data. Writers and readers of one runtime state area are one module.

THE PREDECESSOR PROJECT ALREADY ANSWERED SOME OF THIS. FD2 is the same
developer's earlier game, reverse engineered in full at
C:\Users\fdpsf\Documents\fd2-anatomy, and it shares the AIL build and much of
the runtime. Before concluding, check whether it already settled your function:
grep its knowledge base (rebuild_info/, program_info/) for the symbol or the
behaviour. Its rebuild_info/ail/calling_convention.md is the one to read when
link direction is your only evidence -- it has an "fd2common pool" section
listing the helpers THE GAME defines and the AIL vendor objects reference
through EXTDEF, which is the documented exception to "only AIL calls it, so it
is AIL". A recorded answer there outranks an inference you make here.

WHAT IS NOT EVIDENCE:

  Address ranges. The three pools are interleaved: crt runs from 00010000 to
  00056786 and fdps from 00010010 to 00057a74, completely overlapping. Judging
  by "this is above 0x3c000 so it is library" is explicitly forbidden.

  The body's shape on its own. A PCM conversion loop and a sprite loop look
  alike. If the function has no static caller it is probably reached through a
  table of function pointers, and then THE DISPATCHER decides the pool, not what
  the body computes -- refs_to in the packet shows the table slot that holds this
  address.

  A library hit with a low score or many candidates. Fourteen-byte stubs collide:
  malloc, free, close, remove, _strupr, _toupper and _tolower all hash the same
  "push an argument, call, clean up, return" shape. Check fid.same_hash_in_image
  and the candidate count -- a hash shared by a dozen functions identifies a
  shape, not a symbol.`

const NAMING_RULES = `NAMING, from rebuild_info/naming.md (read it if you need the full text):

  Vendor functions carry the library's own name with NO project prefix, spelled
  exactly as the library spells it: memcpy, _nmalloc, __CHK, IF@COS, AIL_startup.
  The name is what wlink resolves against the real .LIB, so a prefix would break
  it. crt_ as a prefix is WRONG and any surviving crt_ name is a defect.

  The game's own functions get fdps_ + snake_case. Do not invent one here: a
  later ticket names them with the whole call graph in view. A function that is
  fdps and still called FUN_xxxxxxxx is correct at this stage.

  A file-static inside a library object gets L$N_<module>_<purpose> -- but only
  after you have tried to get its real name back. Watcom emits translation-unit
  statics as OMF LPUBDEF (local public) records, not PUBDEF, so wlib listings and
  the Function ID database are both blind to them, yet the name IS in the .LIB.
  Pull the module out with wlib, match the LPUBDEF offset against the body, and
  the real symbol comes back -- convDec, calc_yday, forcedecpt and FixedPoint_Format
  were all recovered this way. The listing of the linked CLIB3S is already
  extracted at C:\Users\fdpsf\Documents\fdps-anatomy\workspace\pool_rereview\CLIB3S.lst. Fall back to L$ only when the record genuinely
  does not name this offset, and then the middle token is the OBJECT BASENAME
  (L$1_spve_...), never the caller's public symbol. A routine
  that behaves like a library function but matches no library object gets
  crt_equivalent_ + snake_case. Linker and compiler products get
  binary_artifact_ + description + _<address>.

  AIL internals get AIL_internal_ + description, or AIL_internal_<public>_inner.
  Where one helper is linked twice, the thunk keeps the plain name and the body
  carries the address suffix as ailv3.lib spells it -- that suffix is the FD2
  address, not this program's.

  NEVER INVENT A SUFFIX to get out of a clash. _thunk, _stub, _impl, _2 and the
  like are names no library publishes, and a manufactured name is exactly what
  the rules forbid -- it will not resolve at link time and it hides which address
  the real symbol belongs to. When a Function ID hit lands on a full body and a
  five-byte jump to that body wears the same public name, the body keeps the
  symbol and the jump gets what the rules give a jump: FUN_xxxxxxxx if it is
  library code with no symbol of its own, binary_artifact_<what it does>_<addr>
  if this link emitted it. The one address-suffixed form that is allowed is the
  AIL one, and only because ailv3.lib itself publishes the name that way.

  DO NOT MANUFACTURE A NAME. If you cannot point at a public symbol, leave the
  name as it is. "It looks like strlen" is not a name; a Function ID hit at a
  real score, or a caller that passes the arguments the documented function
  takes, is. A name taken from a shape collision is exactly the error this
  ticket exists to catch: 000435a2 wore "sprintf" for a whole ticket and is
  actually spawnlp.`

function reviewPrompt(addr) {
  return `You are re-reading ONE function in FDPS.LE and answering four questions about it
independently. FDPS.LE is a 1997 DOS game executable (32-bit Watcom C/C++ 10.0a,
DOS/4G LE module) being reverse engineered.

An earlier ticket already judged this function. Its answers are on disk, and you
will read them -- but only after you have formed your own. That order is the
whole point of this pass: a re-read that starts from the existing tag is not a
re-read, it is a search for reasons to agree, and it would confirm the original
judgement a thousand times out of a thousand.

Your function: ${addr}

STEP 1 -- FORM YOUR OWN CONCLUSION. Read, in this order, and nothing else yet:

  ${WS}\\items\\facts\\${addr}.json   bytes, body ranges and holes, the gap after
                                     the body, callers, callees, every reference
                                     to the entry, data and string references,
                                     and the Function ID hits
  ${WS}\\asm\\${addr}.txt             the disassembly, plus a few instructions
                                     past the end of the body

Decide, from those two alone:
  - which pool this function belongs to, and why
  - what it should be called
  - whether the boundary and the signature are right
  - what an accurate one-line description of it would say

STEP 2 -- COMPARE WITH WHAT IS RECORDED.

  ${WS}\\items\\current\\${addr}.json  the current name, pool tags, calling
                                      convention, prototype and plate comment

Now, and only now, compare. Where you agree, say so. Where you disagree, the
verdict changes and you say what the earlier reading missed. Agreement is the
expected outcome for most functions and is a real result -- do not manufacture a
disagreement to look thorough.

${POOL_RULES}

${NAMING_RULES}

THE FOUR AXES, each judged separately. A right name does not make the pool
right, and a right pool does not make the boundary right.

1 POOL. As above. Say which of the four kinds of evidence you are using.

2 NAME. Against the naming rules. Look especially for a name that came from a
  shape collision: check fid.same_hash_in_image, fid.same_hash_in_ail_lib and
  the candidate counts, and check that what the disassembly does is what the
  named library function is supposed to do. A wrong vendor name is worse than no
  name, because the rebuild will link against the real library.

3 BOUNDARY AND SIGNATURE.
  - Does the body start at the real entry? Watcom prologues are 53 56 57 55
    89 e5 (push ebx/esi/edi/ebp; mov ebp,esp), often with sub esp,n after.
  - Does it end where the function ends? Look at gap_after in the packet: a
    non-empty gap holding instructions usually means a tail that should be part
    of this body. body_holes are ranges inside the body Ghidra does not own,
    normally dead stack cleanup after a no-return call, and they belong here.
  - Has it swallowed the next function? The tail printed after the body in the
    asm file shows what comes next.
  - Is the calling convention what the assembly does? Watcom's default here is
    register calling (__watcall): up to four arguments in EAX, EDX, EBX, ECX.
    A function that reads its arguments from [ESP+n] and leaves the caller to
    clean up is __cdecl, whatever Ghidra says. A hand-written assembly routine
    that takes its arguments in ESI/EDI/ECX and reuses the caller's frame has no
    C convention at all: record what it really does and mark it as assumed.
  - Ghidra guessed every signature whose sig_source is ANALYSIS. Saying "the
    convention is Ghidra's guess and the assembly does not confirm it" is a
    correct, useful verdict.

4 PLATE COMMENT. Does what it claims match the code? Reasons that a later
  finding overturned must go. The comment you write is the one that stays.

TOOLS. The packets are built so most functions need no Ghidra call at all. If
you do need one, load them in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__read_memory"

BUDGET: about 6 tool calls. You may read another function's asm file or facts
packet when a caller or a dispatcher decides your answer -- that is reading
evidence, not judging that function. You may NOT read another function's verdict
file in this pass; a later rescan pass is allowed to.

READ-ONLY ON GHIDRA. Do not rename, tag, comment, or change any boundary. The
workflow applies everything later, in one place. A write from you is a defect.

WRITE YOUR VERDICT TO A FILE. Use the Write tool to create exactly:

  ${VERDICTS}\\${addr}.json

UTF-8, this exact shape, no trailing commentary:

{
  "addr": "${addr}",
  "covers": {
    "start": "<8 hex>", "end": "<8 hex>", "body_sha": "<copy body_sha from the facts packet>"
  },
  "independent": {
    "pool": "<what you concluded in step 1, before reading current/>",
    "name": "<likewise, or empty>",
    "role": "<one English sentence on what the function does>",
    "boundary_ok": true,
    "signature": "<the convention the assembly actually implements>"
  },
  "pool": {
    "verdict": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
    "agrees_with_current": true,
    "evidence": "<why this pool and not the others>",
    "confidence": "high" | "medium" | "low"
  },
  "name": {
    "verdict": "<the name to record; keep the current one if it is right>",
    "agrees_with_current": true,
    "evidence": "<what the name rests on, or why there is no name to give>",
    "confidence": "high" | "medium" | "low"
  },
  "boundary": {
    "fix": "none" | "set_body" | "split" | "delete",
    "start": "<8 hex, only for set_body>",
    "end": "<8 hex, only for set_body>",
    "split_at": "<8 hex, only for split>",
    "evidence": "<why the body ends where you say it does>",
    "confidence": "high" | "medium" | "low"
  },
  "signature": {
    "cc": "<a Ghidra convention name: __watcall, __cdecl, __stdcall, or empty to leave it>",
    "prototype": "<a C prototype WITHOUT the convention, or empty>",
    "assumed": false,
    "agrees_with_current": true,
    "evidence": "<what the assembly shows about arguments and cleanup>",
    "confidence": "high" | "medium" | "low"
  },
  "plate": {
    "text": "<the full replacement plate comment, English, in the format below>",
    "agrees_with_current": true,
    "confidence": "high" | "medium" | "low"
  },
  "open_question": "<what you could not settle, or empty>"
}

agrees_with_current, on every axis, means "what was already recorded was right".
For the plate that means the old comment's claims still hold, even where you
reworded it; set it false when the old comment said something the code does not
do. These four flags are what the closing report counts as the result of the
whole ticket, so they have to be honest in both directions.

SPELL THE FUNCTION'S OWN NAME IN THE PROTOTYPE exactly as the name axis settled
it -- "int FUN_0001c520(int attacker, int target)", not "int f(int attacker,
int target)". Ghidra applies the whole signature including the name, and a
placeholder there would rename the function behind the name axis's back.

SET signature.assumed TO TRUE when the convention on record is a guess the
assembly does not confirm -- a hand-written routine taking its arguments in
ESI/EDI/ECX has no C convention at all. When assumed is true, LEAVE cc AND
prototype EMPTY: only what the code actually showed you may go into the
database, and the guess belongs in the plate comment where a reader can see that
it is one. The workflow drops cc and prototype whenever assumed is true, so
filling them in anyway only wastes your work.

THE PLATE COMMENT FORMAT, exactly these lines, in this order:

  <one sentence: what the function does>
  Pool: <pool>  [<the library match, or the evidence in a few words>]
  Evidence: <why this pool and not the others>
  Name: <what the name rests on, or "no library symbol identified">
  Boundary: <start>-<end>, <why it ends there>
  Signature: <the convention and arguments; say "Ghidra's guess, unconfirmed" when it is>
  Open question: <only when there is one>

BOUNDARY FIXES ARE EXPENSIVE AND RARE. Every one retires the verdicts of the
functions around it and sends them back to the worklist, so propose one only
when the assembly leaves no doubt. "set_body" gives the body a new start and
end; "split" cuts a second function out at split_at; "delete" removes a function
that is really part of its neighbour. When in doubt, fix nothing and write what
you saw into open_question.

A wrong confident answer is far more expensive than an honest unknown: it gets
copied into the knowledge base and into the rebuilt C source, and nobody looks
at it again. If you cannot settle an axis, say so in open_question and set that
axis's confidence to low. The rescan pass will pick it up.

Then return the summary. Your final output is data for the workflow, not a
message to a human. changed_axes lists only the axes you actually changed.`
}

function applyPrompt(tag, ids) {
  return `Transcribe one round of ticket-14.2 re-review verdicts into Ghidra, then prove the
program is still clean. You are not judging anything: every verdict was written
by an agent that read that one function.

Round tag: ${tag}
Functions (${ids.length}): ${ids.join(' ')}

Load the Ghidra tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

Steps, in order:

1. Apply the verdicts:
     run_ghidra_script  ${TOOLS}\\ApplyRereviewVerdicts.java
     args: ${VERDICTS} ${ids.join(' ')}
   Read the output: verdicts applied, renames, signatures, plate comments,
   bodies changed, boundary changes, problems. Every "problems" line matters.
   A "NAME TAKEN" line means two verdicts claim one symbol: report it, do not
   pick a winner and do not add a suffix. Report the boundary change count.

2. Run the baseline gate:
     run_ghidra_script  ${BASELINE_AUDIT}
   In its "# Gate" section, orphan code ranges, undefined bytes in the code
   object and error bookmarks must all be 0.

3. Run the naming gate:
     run_ghidra_script  ${TOOLS}\\AuditNames.java
   collisions, prefix_mismatches and tag_problems must all be 0. Collisions on
   a generated name (AUTO-COLLISION) do not count and need no action, and
   no_pool_tag is an honest "unknown" verdict waiting for the rescan -- report
   the number, do not treat it as a failure.

4. If either gate is dirty, repair it in this round.
   - Orphan code after a boundary fix means the tail of a shrunken body belongs
     to nobody. Either extend the owning function's body or create the function
     the range starts, whichever the code says -- through run_ghidra_script with
     an inline script.
   - A Bad Instruction bookmark means something was disassembled that is not
     code. Clear it and say so, because it means a verdict was wrong.
   - A name collision means two addresses claim one symbol. Do NOT resolve it by
     renaming one of them: report every address involved in collision_addrs and
     leave them alone. Deciding which one is right is a judgement, and judgements
     are made one function at a time by the review agents, not here. The workflow
     sends those addresses back through the rescan.
   Re-run the gate you repaired.

5. Save the program with save_program.

DO NOT WRITE A NEW SCRIPT to get a rejected verdict in. ApplyRereviewVerdicts
already strips the qualifiers Ghidra's DataTypeManager cannot model; whatever it
still refuses is reported in problems and left for the review agents to settle.
Working around a rejection here is the one thing this stage must not do, because
it turns a visible failure into a silent judgement made by the wrong agent.

Report the numbers. Set ok to false and describe it plainly in problems if
anything is still wrong after the repair attempt -- do not paper over it. Set
ghidra_responding to false only if Ghidra itself stopped answering.`
}

function refreshPrompt(why) {
  return `Refresh the ticket-14.2 worklist from the current state of Ghidra. Nothing here
is a judgement; it is a dump plus a rebuild of the per-function packets.

Why: ${why}

Load the Ghidra tool in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script"

Steps:

1. run_ghidra_script  ${TOOLS}\\DumpRereviewState.java
   It rewrites functions.json, current.json and asm/ under ${WS}.

2. Rebuild the packets and recompute what is left:
     python ${TOOLS}\\build_packets.py

3. Read the printed summary and return the totals. "retired" is the union of the
   retired_stale, retired_orphan and retired_boundary lists in
   ${WS}\\worklist.json -- those are functions whose verdict no longer describes
   the code that is there, so they are back on the worklist. Return their
   addresses; if there are more than 40, return the first 40 and say so in
   problems.

Set ok to true if both steps ran.`
}

function rescanPrompt(addr, why) {
  return `An earlier pass re-read ONE function in FDPS.LE and could not settle it. You are
reading it again now that the functions around it have been re-read too.

Your function: ${addr}
Why it came back: ${why}

Read, in this order:
  ${VERDICTS}\\${addr}.json            what was concluded, and open_question
  ${WS}\\items\\facts\\${addr}.json     the evidence packet
  ${WS}\\asm\\${addr}.txt              the disassembly

What this pass has that the first did not: the callers, callees and dispatchers
named in the packet now have verdicts of their own under ${VERDICTS}\\. Open the
ones that matter. Reading a neighbour's verdict is using a judgement someone else
already made -- it is not re-judging that neighbour, and you must never rewrite
another function's verdict file.

If the packet shows no callers at all, the function is reached through a table of
function pointers, and WHO DISPATCHES THE TABLE decides the pool. refs_to holds
the slot address; find what reads that table and read its verdict. A routine
sitting in a table with a hundred routines already judged ail is ail, however
much its body looks like game code.

IF YOU CAME BACK BECAUSE OF A NAME CLASH, another address settled on the same
symbol and only one of you can be right. Find the other one -- grep the name in
${VERDICTS}\\ -- and read its verdict and its facts packet. Then decide which
body actually is that symbol. The loser is usually the shorter one: a five-byte
jump island wearing a library name is a thunk to the real body, and its own name
belongs to whatever the naming rules say a jump island is called. If your
function is the one that should give up the name, change it here. If you are
sure yours is right and the other is wrong, say so in what_resolved and leave
your verdict alone -- never rewrite the other function's verdict file.

${POOL_RULES}

${NAMING_RULES}

READ-ONLY ON GHIDRA.

If the neighbours settle it, rewrite ${VERDICTS}\\${addr}.json with the Write
tool, keeping the same field shape, clearing open_question, and adding a
"_supersedes" field of one short paragraph saying what changed and why. Set
changed to true.

If they do not settle it, change nothing and set changed to false. An honest
unresolved verdict is a correct outcome. A second attempt is not a licence to
manufacture a conclusion.

Set addr in your summary to exactly ${addr}.`
}

// ------------------------------------------------------------- run state

const unfinished = []
const applyReports = []
// Addresses the naming gate found claiming one symbol between them. Neither
// verdict is wrong on its face -- both agents were confident -- so the clash is
// only visible from outside, and the rescan is where it gets settled.
const disputedNames = new Set()
let halted = false
let haltReason = ''

function noteApply(label, report) {
  if (!report) {
    unfinished.push(`${label}: apply agent returned nothing`)
    return false
  }
  applyReports.push(Object.assign({ round: label }, report))
  for (const id of report.collision_addrs || []) {
    disputedNames.add(id)
  }
  log(`${label}: applied=${report.applied} renamed=${report.renamed || 0} `
    + `boundary=${report.boundary_changes || 0} orphans=${report.orphan_ranges} `
    + `errors=${report.error_bookmarks} collisions=${report.name_collisions} ok=${report.ok}`)
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

// A whole round coming back empty is not a per-item failure. Individual agents
// fail one at a time; when every agent in a round dies the cause is upstream --
// the session token limit, the API, the host -- and retrying is guaranteed
// waste. Stop, and record what was never attempted.
function outage(label, got, expected, remaining) {
  if (expected === 0 || got > 0) {
    return false
  }
  halted = true
  haltReason = `${label}: all ${expected} agent(s) in the round returned nothing, so the `
    + 'failure is upstream of this workflow (session limit, API, host) rather than in any '
    + 'one function'
  log(`STOP ${haltReason}`)
  if (remaining && remaining.length > 0) {
    unfinished.push(`${remaining.length} function(s) not attempted after the run stopped: `
      + remaining.slice(0, 12).join(' ') + (remaining.length > 12 ? ' ...' : ''))
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

// ------------------------------------------------------------------ plan

phase('Plan')
let todo = arg('functionIds', null)

if (arg('refresh', true)) {
  const refreshed = await agent(refreshPrompt('start of the run'),
    { label: 'plan:refresh', phase: 'Plan', schema: REFRESH })
  if (!refreshed || !refreshed.ok) {
    return {
      error: 'the plan stage could not refresh the packets',
      detail: refreshed ? refreshed.problems : 'agent returned nothing',
    }
  }
  log(`program has ${refreshed.functions_total} functions, `
    + `${refreshed.todo_count || 0} still to re-read`)
  if (todo === null) {
    todo = refreshed.retired || []
    log('no worklist supplied; using what the refresh reported as outstanding')
  }
}

if (!todo || todo.length === 0) {
  log('nothing to re-read from the supplied worklist')
  todo = []
}

// --------------------------------------------------------------- review

const verdicts = []
const batch = todo.slice(0, MAX_FUNCTIONS)
if (batch.length < todo.length) {
  log(`budget: ${todo.length - batch.length} function(s) left for a later run`)
  unfinished.push(`${todo.length - batch.length} function(s) beyond this run's budget`)
}

const rounds = chunk(batch, ROUND_SIZE)
for (let r = 0; r < rounds.length && !halted; r++) {
  const tag = `r${r + 1}`
  phase('Review')
  const summaries = (await parallel(rounds[r].map((id) => () =>
    agent(reviewPrompt(id), { label: `review:${id}`, phase: 'Review', schema: REVIEW_SUMMARY })
  ))).filter(Boolean)

  if (outage(tag, summaries.length, rounds[r].length, rounds.slice(r).flat())) {
    break
  }

  // A missing verdict file is not a skip: retry once, then record it.
  let usable = summaries.filter((s) => s.wrote_file)
  const lost = rounds[r].filter((id) => !usable.some((s) => s.addr === id))
  if (lost.length > 0) {
    log(`${tag}: ${lost.length} function(s) came back without a verdict file, retrying`)
    const retried = (await parallel(lost.map((id) => () =>
      agent(reviewPrompt(id), { label: `review-retry:${id}`, phase: 'Review', schema: REVIEW_SUMMARY })
    ))).filter(Boolean).filter((s) => s.wrote_file)
    usable = usable.concat(retried)
    for (const id of lost) {
      if (!retried.some((s) => s.addr === id)) {
        unfinished.push(`function ${id}: no verdict file after a retry`)
      }
    }
  }

  for (const s of usable) {
    verdicts.push(s)
  }

  if (usable.length === 0) {
    continue
  }

  phase('Apply')
  const report = await agent(applyPrompt(tag, usable.map((s) => s.addr)),
    { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT })
  noteApply(`apply:${tag}`, report)

  // A boundary change invalidates the neighbours, and the only way to find out
  // which is to re-dump and let build_packets.py compare. The retired ones join
  // the end of this run's worklist rather than waiting for the next invocation.
  if (!halted && report && (report.boundary_changes || 0) > 0) {
    phase('Plan')
    const refreshed = await agent(
      refreshPrompt(`${report.boundary_changes} boundary change(s) landed in round ${tag}`),
      { label: `refresh:${tag}`, phase: 'Plan', schema: REFRESH })
    if (refreshed && refreshed.ok) {
      const back = (refreshed.retired || []).filter((id) =>
        !rounds.slice(r + 1).flat().includes(id))
      if (back.length > 0) {
        log(`${tag}: ${back.length} neighbour verdict(s) retired, back on the worklist`)
        rounds.push(...chunk(back, ROUND_SIZE))
      }
    }
    else {
      unfinished.push(`refresh after ${tag} failed: `
        + (refreshed ? refreshed.problems : 'agent returned nothing'))
    }
  }
}

// --------------------------------------------------------------- rescan
//
// Every agent saw one function, so anything whose identity depends on a
// neighbour was unanswerable while that neighbour was unjudged. This pass is
// the only one allowed to read another function's verdict.

const rescanLog = []
const changedFromRescan = []

// A run that stopped on the budget left unresolved verdicts this invocation
// never saw. The final run collects them off disk so the rescan covers the
// whole ticket rather than the last slice of it.
if (arg('rescanFromDisk', false) && !halted) {
  phase('Rescan')
  const leftovers = await agent(
    `List the ticket-14.2 verdicts that are still unresolved. This is a listing job, not a
judgement: do not open Ghidra and do not form an opinion about any verdict.

Run exactly this with the Bash tool:

  python -c "import json,glob,os;print(json.dumps([os.path.basename(p)[:-5] for p in sorted(glob.glob(r'${VERDICTS}\\*.json')) if (lambda v: any((v.get(k) or {}).get('confidence') not in ('high',None) for k in ('pool','name','boundary','signature','plate')) or (v.get('pool') or {}).get('verdict') in (None,'','unknown'))(json.load(open(p,encoding='utf-8')))]))"

The test is "an axis below high confidence, or no pool" on purpose. A
high-confidence verdict can still carry an open question -- what an argument
means, which type code selects what -- and those are real, but they belong to
the naming and emit tickets, not this one. What comes back here is every verdict
whose own four axes are still in doubt.

Return what it printed as retired, and set functions_total to the number of
verdict files in that directory. If it printed more than 200 addresses, return
the first 200 and say so in problems. Set ok to true if the command ran.`,
    { label: 'rescan:collect', phase: 'Rescan', schema: REFRESH })
  if (leftovers && leftovers.ok) {
    for (const id of leftovers.retired || []) {
      if (!verdicts.some((s) => s.addr === id)) {
        verdicts.push({ addr: id, pool: 'unknown', confidence: 'low',
          has_open_question: true, wrote_file: true, changed_axes: [], fromDisk: true })
      }
    }
    log(`rescan: picked up ${(leftovers.retired || []).length} verdict(s) left unresolved earlier`)
  }
  else {
    unfinished.push('could not collect unresolved verdicts from disk for the rescan')
  }
}

for (const id of arg('rescanIds', [])) {
  const existing = verdicts.find((s) => s.addr === id)
  if (existing) {
    existing.confidence = 'low'
    existing.has_open_question = true
  }
  else {
    verdicts.push({ addr: id, pool: 'unknown', confidence: 'low', has_open_question: true,
      wrote_file: true, changed_axes: [], fromDisk: true })
  }
}

for (let pass = 1; pass <= MAX_RESCAN_PASSES && !halted; pass++) {
  for (const id of disputedNames) {
    const existing = verdicts.find((s) => s.addr === id)
    if (existing) {
      existing.disputed_name = true
    }
    else {
      verdicts.push({ addr: id, pool: 'unknown', confidence: 'high', has_open_question: false,
        wrote_file: true, changed_axes: [], disputed_name: true })
    }
  }
  // What comes back is what THIS ticket could not settle: an axis below high
  // confidence, a pool that stayed unknown, or a symbol two addresses claim.
  // A high-confidence verdict often still carries an open question -- what a
  // parameter means, what a type code selects -- and those are real, but they
  // are questions for the naming and emit tickets. Re-reading several hundred
  // settled functions to chase them would cost more than it returns; the
  // closing report lists them instead.
  const open = verdicts.filter((s) =>
    s.confidence !== 'high' || s.pool === 'unknown' || s.disputed_name)
  if (open.length === 0) {
    log(`rescan pass ${pass}: nothing unresolved`)
    break
  }
  phase('Rescan')
  log(`rescan pass ${pass}: ${open.length} function(s)`)

  const results = (await parallel(open.map((s) => () =>
    agent(rescanPrompt(s.addr, s.disputed_name
      ? 'the naming gate found another address claiming the same symbol'
      : (s.pool === 'unknown' ? 'the pool came back unknown'
        : (s.confidence === 'low' ? 'confidence was low' : 'an open question was recorded'))),
      { label: `rescan:${s.addr}`, phase: 'Rescan', schema: RESCAN })
  ))).filter(Boolean)

  if (outage(`rescan pass ${pass}`, results.length, open.length, open.map((s) => s.addr))) {
    break
  }

  const changed = []
  for (const res of results) {
    const target = verdicts.find((s) => s.addr === res.addr)
    if (!target) {
      continue
    }
    target.confidence = res.confidence
    target.has_open_question = res.still_open
    // A clash that survived its rescan stays visible in the closing report
    // instead of being retried forever.
    if (target.disputed_name && !res.changed) {
      target.has_open_question = true
    }
    target.disputed_name = false
    if (res.changed) {
      if (res.pool) {
        target.pool = res.pool
      }
      rescanLog.push(`${res.addr}: ${res.what_resolved}`)
      log(`  ${res.addr} resolved: ${res.what_resolved}`)
      changed.push(res.addr)
      changedFromRescan.push(res.addr)
    }
  }

  disputedNames.clear()
  if (changed.length === 0) {
    log(`rescan pass ${pass}: nothing could be resolved, stopping`)
    break
  }
  phase('Apply')
  noteApply(`apply:rescan${pass}`,
    await agent(applyPrompt(`rescan${pass}`, changed),
      { label: `apply:rescan${pass}`, phase: 'Apply', schema: APPLY_REPORT }))
}

// --------------------------------------------------------------- report

const byPool = {}
const changedByAxis = {}
for (const s of verdicts) {
  byPool[s.pool] = (byPool[s.pool] || 0) + 1
  for (const axis of s.changed_axes || []) {
    changedByAxis[axis] = (changedByAxis[axis] || 0) + 1
  }
}
const stillOpen = verdicts
  .filter((s) => s.confidence !== 'high' || s.pool === 'unknown')
  .map((s) => `${s.addr} (${s.pool})`)
// Questions a confident verdict left behind for a later ticket -- argument
// meanings, type codes, struct fields. Counted, not chased.
const deferredQuestions = verdicts
  .filter((s) => s.confidence === 'high' && s.has_open_question).map((s) => s.addr)

const stats = {
  reviewed: verdicts.length,
  byPool: byPool,
  changedByAxis: changedByAxis,
  changedFunctions: verdicts.filter((s) => (s.changed_axes || []).length > 0).map((s) => s.addr),
  boundaryFixes: verdicts.filter((s) => s.boundary_fix && s.boundary_fix !== 'none')
    .map((s) => `${s.addr}:${s.boundary_fix}`),
  stillOpen: stillOpen,
  deferredQuestions: deferredQuestions.length,
  rescanResolved: rescanLog,
  unfinished: unfinished,
  halted: halted,
  haltReason: haltReason,
  applyReports: applyReports,
}

const WRITE_REPORT = !(args && args.report === false)
let documents = []
if (!halted && WRITE_REPORT) {
  phase('Report')
  documents = (await parallel([
    () => agent(
      `Update the knowledge base after the ticket-14.2 re-review of every function in
FDPS.LE. The pool sizes moved, so every number on the page has to be recounted
rather than edited by hand.

Sources, in order of authority:
  ${VERDICTS}\\                one verdict per function, four axes each
  ${WS}\\worklist.json         the id lists
  ${WS}\\items\\facts\\         the evidence packets, including the library hits

Do not read every verdict. Use a short python script through the Bash tool to
roll them up into counts and lists, and open individual verdicts only for the
handful the page actually discusses.

What to write, all in TRADITIONAL CHINESE, matching the house style of the
existing pages:

1. ${REPO}\\program_info\\code_pools.md -- rewrite the numbers, not the shape:
   the four pool sizes, the AIL functional areas' member counts, the confidence
   distribution, and what the re-review changed. State plainly how many
   judgements the second reading overturned and on which axis; that number is
   the ticket's own result. Conclusions only -- no narrative, no dates, no "we
   first thought". Every address as 8 hex digits in backticks.

2. The ticket 14.1 comparison numbers, wherever they appear on that page: a pool
   change moves both the numerator and the denominator of the AIL Function ID
   hit rate, so recompute them from the verdicts rather than copying.

3. ${REPO}\\docs\\adr\\0004-reuse-fd2-ail-library.md -- its premise is that
   FDPS and FD2 share one AIL build. Say explicitly whether the re-review still
   supports that. If it does, add one short line recording that it was checked
   and holds; if it does not, say what broke it and that the fallback has to be
   considered. Do not rewrite the decision itself.

4. Anything the re-review found that means "write it the obvious way and it will
   differ from the original" goes into ${REPO}\\rebuild_info\\pitfalls.md, in the
   format that file already uses.

Then make sure every _index.md that indexes a file you touched still describes
it correctly.

Run statistics from this run:
${JSON.stringify(stats, null, 2)}

Return the list of files you wrote.`,
      { label: 'doc:knowledge-base', phase: 'Report', schema: DONE }
    ),
    () => agent(
      `Write the devlog entry for this ticket-14.2 run.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly: narrative,
rambling allowed, and the point is to record the dead ends -- successful paths
end up in the knowledge base and in the code, failed ones are the only
information that would otherwise be lost.

Write ${REPO}\\devlog\\2026-08-16-pool-rereview.md in TRADITIONAL CHINESE.

What this run was: a self-driving workflow for ticket 14.2, a second reading of
every function ticket 14 judged. One agent per function, four independent axes --
pool, name, boundary and signature, plate comment. The structural point is that
the evidence and the existing answer were dumped into separate files and the
prompt ordered the reads, so an agent had to reach its own conclusion before it
could see the one already recorded. Boundary fixes retire the verdicts of the
functions around them, which is why the worklist can grow while the run is going.

Sources you may read for detail:
  ${WS}\\worklist.json
  ${VERDICTS}\\   open a few, especially the ones that changed a judgement

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly: what the re-review overturned and what it confirmed, what the
apply stage had to repair, everything still unresolved, and anything left for a
later run. If the run went cleanly, say so briefly rather than padding. Do not
claim anything the statistics do not support.

Return the list of files you wrote.`,
      { label: 'doc:devlog', phase: 'Report', schema: DONE }
    ),
  ])).filter(Boolean)
}

return {
  reviewed: verdicts.length,
  byPool: byPool,
  changedByAxis: changedByAxis,
  changedCount: verdicts.filter((s) => (s.changed_axes || []).length > 0).length,
  boundaryFixes: stats.boundaryFixes,
  stillOpen: stillOpen,
  rescanResolved: rescanLog.length,
  unfinished: unfinished,
  halted: halted,
  haltReason: haltReason,
  applyReports: applyReports,
  documents: documents.map((d) => d.written).flat(),
}
