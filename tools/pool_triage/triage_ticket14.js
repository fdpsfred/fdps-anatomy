// Ticket 14, start to finish, with no human in the loop.
//
// Two stages, in this order, because the second one is only meaningful once the
// first has finished:
//
//   Stage A -- every undefined block in .object1 is judged: alignment filler,
//   data, or code the call graph never reaches. Code becomes functions. This
//   runs in passes: a large block usually holds several functions, so after
//   each round of applies the worklist is regenerated and whatever is left of
//   the block comes back as a smaller block. Passes stop when a pass creates
//   nothing new.
//
//   Stage B -- every function in the program, including the ones stage A just
//   created, is assigned to a pool: fdps, crt, ail or binary_artifact. What
//   survives as fdps is the worklist ticket 15 names.
//
// The two properties ADR-0007 asks for:
//
//   One item per agent. The worklists live in files and in this script; every
//   agent() call carries exactly one block or one function and no agent ever
//   sees the list. The evidence each agent needs -- bytes, neighbours, call
//   edges, Function ID hits against the Watcom libraries -- is prepared into a
//   per-item packet by build_worklists.py, so judging a run of alignment filler
//   costs one Read and no Ghidra calls at all.
//
//   Bounded context. Every agent writes its full judgement to a verdict file
//   and returns about 200 bytes. Neither this script nor any later stage grows
//   with the number of items processed.
//
// Resumable: an item that already has a verdict file is not judged again, so a
// run that stops on the agent budget continues where it left off.
//
// args: {
//   stage:            'blocks' | 'pools' | 'all'   (default 'all')
//   maxBlocks:        cap on blocks judged this run       (default 900)
//   maxFunctions:     cap on functions judged this run    (default 900)
//   roundSize:        items per apply round               (default 40)
//   maxBlockPasses:   how many times to regenerate the block worklist (default 4)
//   maxRescanPasses:  re-reads of unresolved verdicts     (default 2)
//   report:           false to skip the knowledge base and devlog stage,
//                     for pilot runs that are not the final pass
// }

export const meta = {
  name: 'pool-triage-ticket14',
  description: 'Judge every undefined block, then assign every function to a pool',
  phases: [
    { title: 'Plan', detail: 'refresh the worklists and read the ids' },
    { title: 'Blocks', detail: 'one agent per undefined block' },
    { title: 'ApplyBlocks', detail: 'create functions and data, then run the gate' },
    { title: 'Pools', detail: 'one agent per function' },
    { title: 'ApplyPools', detail: 'tag and comment, then run the gate' },
    { title: 'Rescan', detail: 're-read unresolved verdicts against their neighbours' },
    { title: 'Report', detail: 'game logic worklist, knowledge base, devlog' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\pool_triage'
const BLOCK_VERDICTS = WS + '\\verdicts\\blocks'
const POOL_VERDICTS = WS + '\\verdicts\\pools'
const TOOLS = REPO + '\\tools\\pool_triage'
const AUDIT = REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java'

const STAGE = (args && args.stage) || 'all'
const MAX_BLOCKS = (args && args.maxBlocks) || 900
const MAX_FUNCTIONS = (args && args.maxFunctions) || 900
const ROUND_SIZE = (args && args.roundSize) || 40
const MAX_BLOCK_PASSES = (args && args.maxBlockPasses) || 4
const MAX_RESCAN_PASSES = (args && args.maxRescanPasses) || 2

// ---------------------------------------------------------------- schemas

const WORKLIST = {
  type: 'object',
  additionalProperties: false,
  required: ['blocks_todo', 'functions_todo', 'blocks_total', 'functions_total'],
  properties: {
    blocks_todo: { type: 'array', items: { type: 'string' } },
    functions_todo: { type: 'array', items: { type: 'string' } },
    blocks_total: { type: 'integer' },
    functions_total: { type: 'integer' },
    fid_addresses: { type: 'integer' },
    note: { type: 'string' },
  },
}

const BLOCK_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['start', 'kind', 'entries', 'confidence', 'wrote_file', 'has_open_question'],
  properties: {
    start: { type: 'string', description: '8-hex block start, exactly as given' },
    kind: { type: 'string', enum: ['pad', 'data', 'code', 'mixed', 'unknown'] },
    entries: { type: 'integer', description: 'How many function entry points were identified' },
    covered_to: {
      type: 'string',
      description: 'Address after the last byte this verdict accounts for, or empty '
        + 'when the whole block is covered',
    },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    has_open_question: { type: 'boolean' },
    wrote_file: { type: 'boolean' },
    note: { type: 'string', description: 'At most one short line, or empty' },
  },
}

const POOL_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'pool', 'confidence', 'wrote_file', 'has_open_question'],
  properties: {
    addr: { type: 'string' },
    pool: { type: 'string', enum: ['fdps', 'crt', 'ail', 'binary_artifact', 'unknown'] },
    name: { type: 'string', description: 'The name written into the verdict, or empty' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    has_open_question: { type: 'boolean' },
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
    created: { type: 'integer', description: 'Functions created, 0 for the pool stage' },
    orphan_ranges: { type: 'integer', description: 'From the gate. Must be 0.' },
    error_bookmarks: { type: 'integer', description: 'From the gate. Must be 0.' },
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
  required: ['blocks_todo', 'blocks_total', 'functions_total', 'ok'],
  properties: {
    blocks_todo: { type: 'array', items: { type: 'string' } },
    blocks_total: { type: 'integer' },
    functions_total: { type: 'integer' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const RESCAN = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'changed', 'confidence', 'still_open'],
  properties: {
    id: { type: 'string' },
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

const POOL_RULES = `The four pools, from rebuild_info/naming.md:

  fdps             the game's own code. Gets a fdps_ prefix and is emitted as C
                   by a later ticket.
  crt              Watcom C runtime, the maths library and the 80x87 emulator --
                   anything that came out of CLIB3S.LIB, MATH387S.LIB or
                   EMU387.LIB. Prefix crt_.
  ail              Miles AIL sound and music library. Prefix AIL_.
  binary_artifact  code that corresponds to no C source: linker thunks, jump
                   islands, compiler-emitted helpers that are not library
                   functions.

Address ranges are NOT evidence. Library and game code are interleaved; the
0x3c000 boundary in memory_layout.md is an approximation and judging by it is
explicitly forbidden. Judge by what the code does, what it references, who
calls it, and the library match.`

const FID_NOTE = `The packet's "fid" field holds Ghidra Function ID hits against the Watcom
10.0 / 10.0a / 10.0b libraries, built from the same CLIB3S / MATH387S / EMU387 /
GRAPH the linker used. A hit with a score above about 30 that also fits the
disassembly is strong evidence for the crt pool and gives the real Watcom
symbol name. A hit is not proof on its own: short functions collide, and the
matcher scores a two-instruction stub against dozens of library stubs. Always
check that what the disassembly does matches what the named library function is
supposed to do.`

function blockPrompt(start) {
  return `You are judging ONE undefined block in FDPS.LE, a 1997 DOS game executable
(32-bit Watcom C/C++ 10.0a, DOS/4G LE module) being reverse engineered.

Your block starts at ${start}. Everything known about it is in

  ${WS}\\items\\blocks\\${start}.json

Read that file FIRST with the Read tool. It gives the range, the size, the
first and last bytes as hex, which function ends immediately before, which
function starts immediately after, and any references pointing into it.

This is the only block you work on. Never judge another block or another
function.

READ-ONLY on Ghidra. Do not create functions, do not disassemble into the
program, do not rename anything, do not set comments. The workflow applies
every change later. A Ghidra write from you is a defect.

WHAT THESE BLOCKS ARE. The linker brought in code the call graph cannot reach,
so Ghidra's analysis never touched it. Most blocks are one of three things:

  Alignment filler. A short run, usually under 16 bytes, sitting exactly
  between the end of one function body and the entry of the next. Watcom pads
  with multi-byte NOP forms: 8d 80 00 00 00 00, 8d 54 22 00, 8d 76 00, 90, cc,
  and 00 runs. If the bytes are only those forms and the block is sandwiched
  between two functions, it is padding: kind "pad". Judge this from the packet
  alone; it needs no Ghidra call.

  Code. A Watcom function prologue is 53 56 57 55 89 e5 (push ebx/esi/edi/ebp,
  mov ebp,esp), often followed by 81 ec or 83 ec (sub esp,n). Hand-written
  runtime routines look different: they may start with 55 89 e5, with a push of
  a register pair, or straight into the work. If the bytes decode as
  instructions, kind is "code" and entries lists the entry points.

  Data. Tables of pointers into the code object, jump tables, strings, or
  numeric constants. Look at the packet's refs: something referencing the block
  from a LEA or a MOV is a strong sign of data. kind "data", and describe each
  item in data_items.

  Dead code inside a function. A short run -- often exactly the three bytes of
  ADD ESP,4 -- immediately after a call to a function Ghidra knows never
  returns, such as crt_exit. The compiler emitted the cdecl stack cleanup
  anyway; nothing reaches it, so Ghidra stopped disassembling there and left a
  hole in the enclosing function's body. This is kind "code" with an EMPTY
  entries list: it is not a function entry, and the workflow folds it back into
  the function that surrounds it. Say which function that is in the comment.

A block can hold several functions one after another. Identify the entry points
you are confident about, starting at the block start. You do NOT have to
account for the whole block: set covered_to to the address after the last byte
you accounted for and the workflow will bring the remainder back as a new,
smaller block on the next pass. Guessing at a tail you cannot read is worse
than leaving it.

Tools, in ONE ToolSearch call, and only if the packet is not enough:
  ToolSearch "select:mcp__ghidra__disassemble_bytes,mcp__ghidra__read_memory,mcp__ghidra__get_xrefs_to,mcp__ghidra__get_function_by_address"

Use disassemble_bytes on the block's bytes to check that they decode cleanly.
Keep to about 10 tool calls; a padding block should need none.

${POOL_RULES}

You are NOT deciding the pool here -- a later stage judges every function with
the library evidence in hand. Record what you noticed as pool_hint and move on.

WRITE YOUR VERDICT TO A FILE. Use the Write tool to create exactly:

  ${BLOCK_VERDICTS}\\${start}.json

with this shape, UTF-8, no trailing commentary:

{
  "start": "${start}",
  "end": "<8 hex, from the packet>",
  "size": <integer, from the packet>,
  "kind": "pad" | "data" | "code" | "mixed" | "unknown",
  "entries": [{"addr": "<8 hex>", "note": "<one line: what this function looks like>"}],
  "data_items": [{"addr": "<8 hex>", "type": "byte[16]" | "dword[8]" | "string" | "word[4]",
                  "label": "<optional label, or empty>", "note": "<one line>"}],
  "covered_to": "<8 hex address after the last byte accounted for, or empty if all of it>",
  "pool_hint": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
  "comment": "<one line to record on the block itself>",
  "evidence": "<why this is what you say it is>",
  "confidence": "high" | "medium" | "low",
  "open_question": "<what you could not settle, or empty>"
}

Rules for those fields:

  entries is empty unless kind is code or mixed. data_items is empty unless
  kind is data or mixed. For kind "pad" both are empty and the workflow defines
  the range as bytes.

  Types in data_items must be one of byte[n], word[n], dword[n], ptr[n] or
  string, and consecutive items must not overlap.

  Labels follow rebuild_info/naming.md: snake_case, and prefixed by whose data
  it is -- data_fdps_ for the game's own tables, crt_ for runtime data, AIL_ for
  the sound library. No capitals, no bare descriptive words. A constant pool
  entry belonging to the maths library is crt_x87_const_two_pi, not
  X87_CONST_TWO_PI. Leave the label empty rather than inventing one you are not
  sure of.

  Never invent a purpose to fill a field. If you cannot tell what the bytes
  are, kind is "unknown", confidence is "low", and open_question says what is
  missing. An honest unknown costs the project far less than a wrong confident
  answer, which gets copied into the knowledge base and the rebuilt C source.

Then return the summary. Your final output is data for the workflow, not a
message to a human.`
}

function poolPrompt(addr) {
  return `You are assigning ONE function in FDPS.LE to a pool. FDPS.LE is a 1997 DOS game
executable (32-bit Watcom C/C++ 10.0a, DOS/4G LE module) being reverse
engineered; the question this stage answers is "is this function part of the
game, or did the linker bring it in?".

Your function: ${addr}. Everything already known about it is in

  ${WS}\\items\\functions\\${addr}.json

Read that file FIRST with the Read tool: name, size, calling convention,
caller and callee names, existing tags, the first bytes, and the Function ID
hits against the Watcom libraries.

This is the only function you work on. Never judge another function.

READ-ONLY on Ghidra. Do not rename, do not tag, do not set comments. The
workflow applies every change later.

${POOL_RULES}

${FID_NOTE}

HOW TO JUDGE, in order:

1. Read the disassembly of your function. It is the primary source.
2. Check the packet's fid hits against what you just read.
3. Look at the callers and callees in the packet. A function called only by
   named crt_ functions is almost certainly runtime; one called from named
   fdps_ code is almost certainly game logic. Startup code is the exception:
   the CRT calls main.
4. Follow references to strings and data. This executable has no import table
   and no Chinese strings, so string evidence is scarce and worth a lot when
   you find it. Miles AIL routines tend to touch the DOS interrupt path, the
   driver structures loaded from the .DIG file, or the state area at 0x70000.
5. If it is a two-or-three instruction stub that just jumps or loads a
   constant, consider binary_artifact -- but a library stub that has a real
   library symbol is crt, not an artifact.

Tools, in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_xrefs_from,mcp__ghidra__read_memory"

Keep to about 12 tool calls. Read your own function; do not disassemble the
neighbours, their names are already in the packet.

NAMING. Only propose a name when the library evidence gives you a real symbol:

  crt   "crt_" + the Watcom symbol exactly as the library spells it, keeping
        its underscores and capitals: memcpy -> crt_memcpy,
        __ExpandDGROUP -> crt___ExpandDGROUP, _nmalloc -> crt__nmalloc.
  ail   "AIL_" + the upstream name if you can identify it, casing as upstream.
  otherwise leave name empty. Do NOT name fdps functions: a later ticket does
  that with the whole call graph in view, and a guess here would be in the way.

Never propose a PascalCase name. Ghidra's tooling warns that names should be
PascalCase; that warning is wrong for this project and is expected.

WRITE YOUR VERDICT TO A FILE. Use the Write tool to create exactly:

  ${POOL_VERDICTS}\\${addr}.json

with this shape, UTF-8, no trailing commentary:

{
  "addr": "${addr}",
  "pool": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
  "name": "<symbol name, or empty>",
  "library": "<e.g. CLIB3S.LIB(memcpy) score 346, or empty>",
  "role": "<one English sentence on what the function does>",
  "evidence": "<why this pool and not the others>",
  "confidence": "high" | "medium" | "low",
  "open_question": "<what you could not settle, or empty>"
}

A wrong confident pool is expensive: crt code that gets called fdps is emitted
as hand-written C that should have come from the library, and game code marked
crt is silently dropped from the rebuild. If you cannot tell, set pool
"unknown", confidence "low", and say what is missing in open_question.

Then return the summary. Your final output is data for the workflow, not a
message to a human.`
}

function applyBlocksPrompt(tag, ids) {
  return `Transcribe one round of block-triage verdicts into Ghidra. You are not judging
anything: every verdict was made by an agent that read that one block. Your job
is to get them in without damage.

Round tag: ${tag}
Blocks (${ids.length}): ${ids.join(' ')}

Load the Ghidra tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

Steps, in order:

1. Apply the verdicts:
     run_ghidra_script  ${TOOLS}\\ApplyBlockTriage.java
     args: ${BLOCK_VERDICTS} ${ids.join(' ')}
   Read its output: blocks applied, functions created, data items, problems.
   Every line under "problems" matters -- report them, do not fix them by
   inventing a different judgement.

2. Run the gate:
     run_ghidra_script  ${AUDIT}
   Read the "# Gate" section. Orphan code ranges and error bookmarks must both
   be 0.

3. If the gate is not clean, repair it in this round.
   - Orphan code means instructions that belong to no function body. The usual
     cause here is a newly created function whose body stopped short, leaving
     the tail unowned. Repair by extending the owning function's body
     (Function.setBody through run_ghidra_script with an inline script), or by
     creating the function the orphan range really belongs to when the range
     starts at a clean prologue.
   - A Bad Instruction bookmark means something was disassembled that is not
     code. Clear the disassembly there (clearListing) and say so in problems,
     because it means a block verdict was wrong.
   Re-run the gate after repairing.

4. Save the program with save_program.

Report the numbers. Set ok to false and describe it plainly in problems if
anything is still wrong after the repair attempt -- do not paper over it. Set
ghidra_responding to false only if Ghidra itself stopped answering.`
}

function applyPoolsPrompt(tag, ids) {
  return `Transcribe one round of pool verdicts into Ghidra. You are not judging anything:
each verdict was made by an agent that read that one function.

Round tag: ${tag}
Functions (${ids.length}): ${ids.join(' ')}

Load the Ghidra tools in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"

Steps, in order:

1. Apply the verdicts:
     run_ghidra_script  ${TOOLS}\\ApplyPoolVerdicts.java
     args: ${POOL_VERDICTS} ${ids.join(' ')}
   Read its output: verdicts applied, renames, tags, pool breakdown, problems.
   A "NAME TAKEN" line is not fatal -- the script keeps both functions apart --
   but report it.

2. Run the gate:
     run_ghidra_script  ${AUDIT}
   Orphan code ranges and error bookmarks must both be 0. Tagging and renaming
   cannot create orphan code, so a failure here means something else moved;
   report it rather than working around it.

3. Save the program with save_program.

Report the numbers, with created = 0. Set ok to false and explain in problems
if anything failed. Set ghidra_responding to false only if Ghidra itself
stopped answering.`
}

function refreshPrompt(pass) {
  return `Regenerate the ticket-14 worklists from the current state of Ghidra. Nothing
here is a judgement; it is a dump plus a rebuild of the per-item packets.

Pass: ${pass}

Load the Ghidra tool in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__run_ghidra_script"

Steps:

1. run_ghidra_script  ${TOOLS}\\DumpTriageWorklist.java
   It rewrites blocks.json, functions.json and object1.bin under ${WS}.

2. Rebuild the packets and the todo lists:
     python ${TOOLS}\\build_worklists.py --blocks-dir "${BLOCK_VERDICTS}" --pools-dir "${POOL_VERDICTS}"

3. Read ${WS}\\worklist.json and return blocks_todo verbatim, plus the totals.

blocks_todo is every undefined block that still has no verdict file. After a
round of applies it contains what is left of the blocks that held more than one
function, which is exactly what the next pass has to judge. Return it even if
it is empty.`
}

function rescanBlockPrompt(id, why) {
  return `An earlier pass judged ONE undefined block in FDPS.LE and could not settle it.
You are re-reading that same block now that its neighbours have been judged too.

Your block: ${id}
Why it came back: ${why}

Read, in this order:
  ${BLOCK_VERDICTS}\\${id}.json      what was concluded, and open_question
  ${WS}\\items\\blocks\\${id}.json   the evidence packet

The reason this pass can succeed where the first could not is that the blocks
and functions around it now have verdicts of their own, in
${BLOCK_VERDICTS}\\ and ${POOL_VERDICTS}\\. A run of bytes that is
unidentifiable alone is often obvious once the function before it is known.

Reading a neighbour's verdict file is using a judgement someone else already
made; it is not re-judging that neighbour. Never rewrite another item's verdict
file.

READ-ONLY on Ghidra.

If the neighbours settle it, rewrite ${BLOCK_VERDICTS}\\${id}.json with the
Write tool, keeping the same field shape, clearing open_question and adding a
"_supersedes" field of one short paragraph saying what changed. Set changed to
true.

If they do not settle it, change nothing and set changed to false. An honest
unresolved verdict is a correct outcome. The second attempt is not a reason to
manufacture a conclusion.

Set id in your summary to exactly ${id}.`
}

function rescanPoolPrompt(id, why) {
  return `An earlier pass assigned ONE function in FDPS.LE to a pool and could not settle
it. You are re-reading that same function now that its neighbours have been
judged too.

Your function: ${id}
Why it came back: ${why}

Read, in this order:
  ${POOL_VERDICTS}\\${id}.json        what was concluded, and open_question
  ${WS}\\items\\functions\\${id}.json the evidence packet, including fid hits

The callers and callees named in the packet now have verdicts of their own in
${POOL_VERDICTS}\\. Open the ones that matter. A helper whose callers are all
crt is crt; one called from the game's main loop is fdps.

Reading a neighbour's verdict is using a judgement someone else already made;
it is not re-judging that neighbour. Never rewrite another function's verdict.

READ-ONLY on Ghidra.

${POOL_RULES}

If the neighbours settle it, rewrite ${POOL_VERDICTS}\\${id}.json with the
Write tool, keeping the same field shape, clearing open_question and adding a
"_supersedes" field of one short paragraph saying what changed. Set changed to
true.

If they do not settle it, change nothing and set changed to false. An honest
unresolved verdict is a correct outcome; the second attempt is not a reason to
manufacture a conclusion.`
}

// ------------------------------------------------------------- run state

const unfinished = []
const applyReports = []
let halted = false
let haltReason = ''

function noteApply(label, report) {
  if (!report) {
    unfinished.push(`${label}: apply agent returned nothing`)
    return false
  }
  applyReports.push(Object.assign({ round: label }, report))
  log(`${label}: applied=${report.applied} created=${report.created || 0} `
    + `orphans=${report.orphan_ranges} errors=${report.error_bookmarks} ok=${report.ok}`)
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

function chunk(items, size) {
  const out = []
  for (let i = 0; i < items.length; i += size) {
    out.push(items.slice(i, i + size))
  }
  return out
}

// ------------------------------------------------------------------ plan

// The opening worklists come in through args. They are long -- hundreds of
// addresses -- and making an agent read them out of the file and repeat them
// back costs minutes of generation for a list the caller already has. Later
// passes are short, so those still go through a refresh agent.
phase('Plan')
let plan
if (args && args.blockIds) {
  plan = {
    blocks_todo: args.blockIds,
    functions_todo: (args && args.functionIds) || [],
    blocks_total: args.blockIds.length,
    functions_total: ((args && args.functionIds) || []).length,
    fid_addresses: (args && args.fidAddresses) || 0,
  }
  log('worklists supplied through args')
}
else {
  plan = await agent(refreshPrompt('initial').replace('Pass: initial',
    `Pass: initial

Also return functions_todo: every function address in worklist.json that has no
verdict file under ${POOL_VERDICTS}. build_worklists.py computes both lists.`),
    { label: 'plan:worklists', phase: 'Plan', schema: WORKLIST })
}

if (!plan) {
  return { error: 'the planning agent returned nothing; worklists could not be read' }
}
log(`worklists: ${plan.blocks_total} blocks (${plan.blocks_todo.length} to judge), `
  + `${plan.functions_total} functions (${plan.functions_todo.length} to judge), `
  + `${plan.fid_addresses || 0} addresses with library hits`)

// --------------------------------------------------------------- stage A

const blockVerdicts = []
let blocksJudged = 0
let blockPass = 0
let pendingBlocks = plan.blocks_todo.slice(0, MAX_BLOCKS)

if (STAGE === 'blocks' || STAGE === 'all') {
  while (pendingBlocks.length > 0 && blockPass < MAX_BLOCK_PASSES && !halted) {
    blockPass++
    log(`block pass ${blockPass}: ${pendingBlocks.length} block(s) to judge`)
    const rounds = chunk(pendingBlocks, ROUND_SIZE)
    let createdThisPass = 0

    for (let r = 0; r < rounds.length && !halted; r++) {
      const tag = `p${blockPass}r${r + 1}`
      phase('Blocks')
      const summaries = (await parallel(rounds[r].map((id) => () =>
        agent(blockPrompt(id), { label: `block:${id}`, phase: 'Blocks', schema: BLOCK_SUMMARY })
      ))).filter(Boolean)

      // A missing verdict file is not a skip: retry once, then record it.
      let usable = summaries.filter((s) => s.wrote_file)
      const lost = rounds[r].filter((id) => !usable.some((s) => s.start === id))
      if (lost.length > 0) {
        log(`${tag}: ${lost.length} block(s) came back without a verdict file, retrying`)
        const retried = (await parallel(lost.map((id) => () =>
          agent(blockPrompt(id), { label: `block-retry:${id}`, phase: 'Blocks', schema: BLOCK_SUMMARY })
        ))).filter(Boolean).filter((s) => s.wrote_file)
        usable = usable.concat(retried)
        for (const id of lost) {
          if (!retried.some((s) => s.start === id)) {
            unfinished.push(`block ${id}: no verdict file after a retry`)
          }
        }
      }

      for (const s of usable) {
        blockVerdicts.push(s)
        blocksJudged++
        if (s.kind === 'code' || s.kind === 'mixed') {
          createdThisPass += s.entries || 0
        }
      }

      if (usable.length > 0) {
        phase('ApplyBlocks')
        noteApply(`applyBlocks:${tag}`,
          await agent(applyBlocksPrompt(tag, usable.map((s) => s.start)),
            { label: `apply:blocks:${tag}`, phase: 'ApplyBlocks', schema: APPLY_REPORT }))
      }
    }

    if (halted) {
      break
    }

    // Regenerate: blocks that held more than one function come back smaller.
    phase('Plan')
    const refreshed = await agent(refreshPrompt(`block pass ${blockPass}`),
      { label: `refresh:pass${blockPass}`, phase: 'Plan', schema: REFRESH })
    if (!refreshed || !refreshed.ok) {
      unfinished.push(`refresh after block pass ${blockPass} failed: `
        + (refreshed ? refreshed.problems : 'agent returned nothing'))
      break
    }
    log(`after pass ${blockPass}: ${refreshed.blocks_total} blocks remain, `
      + `${refreshed.functions_total} functions, ${createdThisPass} entry point(s) created`)
    pendingBlocks = refreshed.blocks_todo.slice(0, Math.max(0, MAX_BLOCKS - blocksJudged))
    if (createdThisPass === 0 && pendingBlocks.length === 0) {
      break
    }
  }
}

// --------------------------------------------------------------- stage B
//
// The function worklist is only complete once stage A has stopped creating
// functions, so it is re-read here rather than reusing the initial plan.

const poolVerdicts = []
let functionsTodo = plan.functions_todo

if ((STAGE === 'pools' || STAGE === 'all') && !halted) {
  if (functionsTodo.length === 0) {
    phase('Plan')
    const refreshed = await agent(refreshPrompt('before the pool stage').replace(
      'Return it even if it is empty.',
      `Return it even if it is empty. Also return functions_todo: every function
address in worklist.json with no verdict file under ${POOL_VERDICTS}.`),
      { label: 'plan:pools', phase: 'Plan', schema: WORKLIST })
    if (refreshed) {
      functionsTodo = refreshed.functions_todo
      log(`pool stage: ${refreshed.functions_total} functions, ${functionsTodo.length} to judge`)
    }
    else {
      unfinished.push('the pool-stage refresh returned nothing and no list was supplied')
    }
  }
  else {
    log(`pool stage: ${functionsTodo.length} function(s) to judge from the supplied list`)
  }

  const batch = functionsTodo.slice(0, MAX_FUNCTIONS)
  if (batch.length < functionsTodo.length) {
    log(`budget: ${functionsTodo.length - batch.length} function(s) left for a later run`)
  }
  const rounds = chunk(batch, ROUND_SIZE)
  for (let r = 0; r < rounds.length && !halted; r++) {
    const tag = `f${r + 1}`
    phase('Pools')
    const summaries = (await parallel(rounds[r].map((id) => () =>
      agent(poolPrompt(id), { label: `pool:${id}`, phase: 'Pools', schema: POOL_SUMMARY })
    ))).filter(Boolean)

    let usable = summaries.filter((s) => s.wrote_file)
    const lost = rounds[r].filter((id) => !usable.some((s) => s.addr === id))
    if (lost.length > 0) {
      log(`${tag}: ${lost.length} function(s) came back without a verdict file, retrying`)
      const retried = (await parallel(lost.map((id) => () =>
        agent(poolPrompt(id), { label: `pool-retry:${id}`, phase: 'Pools', schema: POOL_SUMMARY })
      ))).filter(Boolean).filter((s) => s.wrote_file)
      usable = usable.concat(retried)
      for (const id of lost) {
        if (!retried.some((s) => s.addr === id)) {
          unfinished.push(`function ${id}: no verdict file after a retry`)
        }
      }
    }

    for (const s of usable) {
      poolVerdicts.push(s)
    }

    if (usable.length > 0) {
      phase('ApplyPools')
      noteApply(`applyPools:${tag}`,
        await agent(applyPoolsPrompt(tag, usable.map((s) => s.addr)),
          { label: `apply:pools:${tag}`, phase: 'ApplyPools', schema: APPLY_REPORT }))
    }
  }
}

// --------------------------------------------------------------- rescan

const rescanLog = []
for (let pass = 1; pass <= MAX_RESCAN_PASSES && !halted; pass++) {
  const openBlocks = blockVerdicts.filter((s) => s.confidence === 'low' || s.has_open_question)
  const openPools = poolVerdicts.filter((s) =>
    s.confidence === 'low' || s.has_open_question || s.pool === 'unknown')
  if (openBlocks.length === 0 && openPools.length === 0) {
    log(`rescan pass ${pass}: nothing unresolved`)
    break
  }
  phase('Rescan')
  log(`rescan pass ${pass}: ${openBlocks.length} block(s) and ${openPools.length} function(s)`)

  const results = (await parallel(
    openBlocks.map((s) => () =>
      agent(rescanBlockPrompt(s.start, s.confidence === 'low'
        ? 'confidence was low' : 'an open question was recorded'),
        { label: `rescan:block:${s.start}`, phase: 'Rescan', schema: RESCAN })
    ).concat(openPools.map((s) => () =>
      agent(rescanPoolPrompt(s.addr, s.pool === 'unknown'
        ? 'the pool came back unknown'
        : (s.confidence === 'low' ? 'confidence was low' : 'an open question was recorded')),
        { label: `rescan:pool:${s.addr}`, phase: 'Rescan', schema: RESCAN })
    ))
  )).filter(Boolean)

  const changedBlocks = []
  const changedPools = []
  for (const r of results) {
    const b = blockVerdicts.find((s) => s.start === r.id)
    const p = poolVerdicts.find((s) => s.addr === r.id)
    const target = b || p
    if (!target) {
      continue
    }
    target.confidence = r.confidence
    target.has_open_question = r.still_open
    if (r.changed) {
      if (p && r.pool) {
        p.pool = r.pool
      }
      rescanLog.push(`${r.id}: ${r.what_resolved}`)
      log(`  ${r.id} resolved: ${r.what_resolved}`)
      ;(b ? changedBlocks : changedPools).push(r.id)
    }
  }

  if (changedBlocks.length === 0 && changedPools.length === 0) {
    log(`rescan pass ${pass}: nothing could be resolved, stopping`)
    break
  }
  if (changedBlocks.length > 0) {
    phase('ApplyBlocks')
    noteApply(`applyBlocks:rescan${pass}`,
      await agent(applyBlocksPrompt(`rescan${pass}`, changedBlocks),
        { label: `apply:blocks:rescan${pass}`, phase: 'ApplyBlocks', schema: APPLY_REPORT }))
  }
  if (changedPools.length > 0) {
    phase('ApplyPools')
    noteApply(`applyPools:rescan${pass}`,
      await agent(applyPoolsPrompt(`rescan${pass}`, changedPools),
        { label: `apply:pools:rescan${pass}`, phase: 'ApplyPools', schema: APPLY_REPORT }))
  }
}

// --------------------------------------------------------------- report

const byKind = {}
for (const s of blockVerdicts) {
  byKind[s.kind] = (byKind[s.kind] || 0) + 1
}
const byPool = {}
for (const s of poolVerdicts) {
  byPool[s.pool] = (byPool[s.pool] || 0) + 1
}
const stillOpen = blockVerdicts.filter((s) => s.confidence === 'low' || s.has_open_question)
  .map((s) => `block ${s.start}`)
  .concat(poolVerdicts.filter((s) => s.confidence === 'low' || s.has_open_question
    || s.pool === 'unknown').map((s) => `function ${s.addr} (${s.pool})`))

const stats = {
  blocksJudged: blocksJudged,
  blockPasses: blockPass,
  blocksByKind: byKind,
  functionsJudged: poolVerdicts.length,
  functionsByPool: byPool,
  stillOpen: stillOpen,
  unfinished: unfinished,
  halted: halted,
  haltReason: haltReason,
  applyReports: applyReports,
  rescanResolved: rescanLog,
}

const WRITE_REPORT = !(args && args.report === false)
let documents = []
if (!halted && WRITE_REPORT) {
  phase('Report')
  documents = (await parallel([
    () => agent(
      `Write the knowledge base page for the pool split of FDPS.LE. This is the
deliverable of ticket 14: after this page, "is this function part of the game?"
has a recorded answer for every function in the program.

Sources, in order of authority:
  ${POOL_VERDICTS}\\        one verdict per function: pool, name, library, evidence
  ${BLOCK_VERDICTS}\\       one verdict per undefined block
  ${WS}\\worklist.json      the id lists
  ${WS}\\items\\functions\\  the evidence packets, including the Function ID hits

Do not read every verdict. Use a short python script through the Bash tool to
roll them up into counts, address ranges and the crt/ail name lists, and open
individual verdicts only for the handful the page actually discusses.

Write ${REPO}\\program_info\\code_pools.md in TRADITIONAL CHINESE, matching the
house style of the existing pages -- read ${REPO}\\program_info\\memory_layout.md
first.

Requirements:
  - Start with a bold 驗證對象 line naming what this page owns.
  - Give the counts per pool, and how the boundary was drawn: what counts as
    evidence (library byte match through Ghidra Function ID, caller/callee
    relations, string and data references) and what explicitly does not
    (address ranges).
  - Record the CRT identification: which libraries, how many functions matched,
    and the fact that the match is against the 10.0 family libraries the
    linker used. Link to rebuild_info/build_flags.md rather than restating it.
  - Record the AIL boundary: how AIL code was told apart, and what is known
    about where it sits.
  - Say what binary_artifact covers here.
  - Say plainly what is still unknown: every function whose pool is unknown or
    low confidence, and any block that could not be judged.
  - Conclusions only. No narrative, no phases, no dates, no "we first thought".
  - Every address as 8 hex digits in backticks.
  - Do not duplicate facts owned by memory_layout.md or build_flags.md; link.

Then add a row for code_pools.md to the table in
${REPO}\\program_info\\_index.md.

The worklist ticket 15 works from is the set of functions tagged pool_fdps in
Ghidra, which travels in the versioned snapshot. Say so on the page, with the
count and how to query it, and do NOT put a copy of the list in the knowledge
base -- a second copy would go stale the moment a pool is corrected. Write the
convenience dump to ${WS}\\fdps_functions.json (a JSON array of
{addr, name, size, callers, callees} sorted by address) but never cite that
path from the knowledge base: workspace is not versioned.

Run statistics from this run, for the parts that need them:
${JSON.stringify(stats, null, 2)}

Return the list of files you wrote.`,
      { label: 'doc:code_pools', phase: 'Report', schema: DONE }
    ),
    () => agent(
      `Write the devlog entry for this ticket-14 run.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly: narrative,
rambling allowed, and the point is to record the dead ends -- successful paths
end up in the knowledge base and the code, failed ones are the only information
that would otherwise be lost.

Write ${REPO}\\devlog\\2026-08-15-pool-triage.md in TRADITIONAL CHINESE.

What this run was: a self-driving workflow for ticket 14, in two stages. Stage
A judged every undefined block in .object1 one agent at a time and turned the
code among them into functions, re-generating the worklist after each pass so
that a block holding several functions came back smaller. Stage B assigned
every function in the program to a pool, with Ghidra Function ID hits against
the Watcom 10.0 family libraries as evidence, again one agent per function.
Every agent was read-only on Ghidra and wrote its judgement to a file; a
separate apply agent per round transcribed the round and ran the baseline audit
gate.

The library evidence pipeline is worth describing: the Watcom libraries were
exploded with wlib, the Easy OMF-386 records Ghidra cannot read were patched,
820 modules were imported and analysed, and a .fidb was built per version and
queried against FDPS.LE. That approach was taken from the FD2 project rather
than rebuilt.

Sources you may read for detail:
  ${WS}\\worklist.json
  ${POOL_VERDICTS}\\   open a few, especially any listed as unresolved
  ${BLOCK_VERDICTS}\\

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly: how far each stage got, what the apply stage had to repair,
every item still unresolved, and anything left for a later run. If the run went
cleanly, say so briefly rather than padding. Do not claim anything the
statistics do not support.

Return the list of files you wrote.`,
      { label: 'doc:devlog', phase: 'Report', schema: DONE }
    ),
  ])).filter(Boolean)
}

return {
  stage: STAGE,
  blocksJudged: blocksJudged,
  blockPasses: blockPass,
  blocksByKind: byKind,
  functionsJudged: poolVerdicts.length,
  functionsByPool: byPool,
  stillOpen: stillOpen,
  unfinished: unfinished,
  halted: halted,
  haltReason: haltReason,
  applyReports: applyReports,
  documents: documents.map((d) => d.written).flat(),
}
