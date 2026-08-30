// Ticket 22: emit the whole game, one function at a time, with no human in the
// loop -- ticket 21's pipeline at the scale it was built for.
//
// What ticket 21 proved on one function this runs on all 514, and the three
// things that had to change to survive the scale are:
//
//   The worklist is fetched, not carried. A run asks next_batch.py what is
//   left, so a batch of any size is described by two numbers in args instead of
//   a hundred kilobytes of roster through the orchestrator, and an interrupted
//   run resumes by asking the same question again.
//
//   The order is callee-before-caller, not by address (emit_order.py). A
//   function emitted ahead of its callees links against generated stubs, and a
//   test written against a stub asserts the stub. The call graph gives an order
//   where that happens 11 times out of roughly 500 instead of constantly.
//
//   The build links twice. The first link's undefined symbols are the data
//   ticket 23 has not emitted yet; the second link fills them with a generated
//   zero-filled module (gen_stubs.py) so the gate can still run. Nothing about
//   that module is written into src/.
//
// The shape is serial -- concurrency one -- and that is the point rather than a
// limitation. The reviewer decides what to look at by reading the working tree
// against HEAD, so exactly one function may be in flight; two would put each
// other's edits in the other's diff. Everything else follows ADR-0007:
//
//   Judgements are written to files, summaries are returned. An emitter's full
//   reasoning and a reviewer's full checklist run to several kilobytes each. At
//   514 functions, returning them to this script would put a megabyte of prose
//   through the orchestrator. Each agent writes its verdict to
//   workspace/code_emit/verdicts/ and returns about two hundred bytes.
//
//   Nobody writes Ghidra during judgement. Corrections to plate comments and
//   names are proposed in the verdict files and applied by the bookkeeper, in
//   one place, after the review has passed.
//
//   Every landing is followed by the gate. The gate here is the build gate's
//   emittest target: zero errors, zero unresolved symbols, no new warning,
//   every test green (rebuild_info/build_gate.md).
//
//   Nothing is skipped in silence. A missing return is retried once; a second
//   miss is recorded as unfinished, never as done. Two consecutive agent calls
//   returning nothing means the failure is upstream of this workflow, so the
//   batch stops instead of spending the rest of its budget on doomed retries.
//
//   Equivalence concerns are recorded and left where they are. ADR-0007 4 asks
//   for a rescan before the WORK ends, and a batch is not the work: ticket 22's
//   work ends when all 514 have landed, and the rescan is a separate sweep over
//   emit_issues.json at that point (ticket 22.1). Doing it per batch was
//   measured on t22-01 and did not pay -- 30 of that batch's 196 agents, and 21
//   of the 30 conclusions they reached used evidence the emitter already had in
//   hand: Ghidra, the shipped data files, or the .OBJ its own build had just
//   produced. What that bought was not the sweep, which costs the same wherever
//   it happens, but a whole agent context rebuilt from nothing to do it. The
//   convergence rule in RULES moves that work to where the context already
//   exists; what genuinely needs a neighbour waits for the sweep.
//
// Resuming is the normal case, not the exception: progress lives in
// tools/code_emit/data/emit_state.json and every approved function is its own
// commit, so a run that stops anywhere loses at most the function in flight.
//
// "At most the function in flight" is a claim about the NEXT run, and it is only
// true because of the Recover stage (ticket 21.6). A usage limit does not raise
// an exception this script can catch -- it kills the session where it stands, so
// the tidy-up that failure paths do here never runs, and the half-emitted files
// stay in the tree. The next function's bookkeeper stages src/ and tests/
// wholesale, so without a recovery pass the corpse gets committed under a live
// function's name. The one moment that is guaranteed to execute after a session
// has already died is the start of the next one, so that is where the tidying
// lives: before any worklist is fetched, this run looks at the working tree,
// discards what is under src/ and tests/, and hands the interrupted address back
// to the worklist. What it may discard is bounded by path -- src/, tests/, and
// the in-flight marker in emit_state.json, nothing else -- and anything dirty
// outside that bound stops the run instead of being cleaned up on a guess.
//
// The in-flight marker is the other half. Each function is written into
// emit_state.json as `in_flight` before its emitter touches anything, and that
// write is deliberately left uncommitted: the landing commit carries it forward
// to `committed`, and a session that dies leaves it behind as the one line that
// says which function was flying. `in_flight` is not a terminal status, so
// next_batch.py hands the address out again exactly as it would a `pending` one.
//
// This workflow does not watch its own budget. It runs the list the caller gave
// it and stops for three reasons only: the list ran out, something upstream is
// broken (ADR-0007 5.2/5.5), or it was killed. How much to attempt is the
// caller's decision, made per batch; a workflow that quietly stopped after 12 of
// the 40 it was asked for would be answering a question nobody asked.
//
// args: {
//   limit:       number   how many functions to ask next_batch.py for (default 40)
//   functions:   [{addr, name, target, body_size, stubbed_callees}]  optional,
//                         skips the worklist stage when supplied
//   batchLabel:  string
//   maxRounds:   number   fix rounds per function before giving up (default 4)
// }

export const meta = {
  name: 'fdps-emit-ticket22',
  description: 'Emit the FDPS game code function by function: three-source emit, independent review, build gate, per-function commit',
  phases: [
    { title: 'Recover', detail: 'clear the wreckage a killed run left in src/ and tests/' },
    { title: 'Worklist', detail: 'ask next_batch.py what is left, in callee-first order' },
    { title: 'Emit', detail: 'one emitter agent per function, three sources, writes C and tests' },
    { title: 'Review', detail: 'an independent agent verifies the diff against the assembly' },
    { title: 'Gate', detail: 'the build gate over src/ + tests/' },
    { title: 'Bookkeep', detail: 'apply Ghidra corrections, record state, commit' },
    { title: 'Split', detail: 'split a target file that has outgrown its line budget' },
    { title: 'Report', detail: 'devlog and run archive' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\code_emit'
const VERDICTS = WS + '\\verdicts'

const A = (typeof args === 'string' && args.length) ? JSON.parse(args) : (args || {})
const MAX_ROUNDS = (A && A.maxRounds) || 4
const LABEL = (A && A.batchLabel) || 'emit'
const LIMIT = (A && A.limit) || 40
// A target .c that has outgrown this while functions are still routed into it
// gets split before the next one lands (rebuild_info/code_layout.md).
const LINE_BUDGET = (A && A.lineBudget) || 1000

// ---------------------------------------------------------------- schemas

const STOP_FIELDS = {
  ghidra_unreachable: {
    type: 'boolean',
    description: 'True only when a Ghidra MCP call still fails after one quick retry. '
      + 'This is the batch-wide stop signal; do not set it for a call that worked on retry.',
  },
  toolchain_unreachable: {
    type: 'boolean',
    description: 'True only when the build itself cannot run at all -- DOSBox-X missing, '
      + 'the Watcom install unusable, preflight refusing. A build that runs and reports '
      + 'errors is NOT this: that is an ordinary failing gate.',
  },
  stop_detail: {
    type: 'string',
    description: 'The error you actually saw, when either stop flag is set. Empty otherwise.',
  },
}

function withStops(props) {
  return Object.assign({}, props, STOP_FIELDS)
}

const EMIT_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'wrote_verdict', 'status'],
  properties: withStops({
    addr: { type: 'string' },
    wrote_verdict: { type: 'boolean', description: 'True once the verdict file exists on disk' },
    files_touched: { type: 'array', items: { type: 'string' } },
    test_cases: { type: 'integer', description: 'How many assertions the new test makes' },
    build_pass: { type: 'boolean', description: 'The emitter\'s own build_emit.py run came back clean' },
    build_detail: { type: 'string', description: 'One line: what failed, or empty when clean' },
    open_issues: { type: 'integer', description: 'Equivalence concerns recorded in the verdict file' },
    status: { type: 'string', enum: ['done', 'skip', 'blocked'] },
    note: { type: 'string', description: 'At most one short line for the orchestrator' },
  }),
}

const REVIEW_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'approved', 'wrote_verdict', 'emit_verdict_present', 'blocking_count'],
  properties: withStops({
    addr: { type: 'string' },
    approved: { type: 'boolean' },
    wrote_verdict: { type: 'boolean' },
    emit_verdict_present: {
      type: 'boolean',
      description: 'Whether the emitter\'s verdict file is actually on disk and complete. '
        + 'Judge the file, not the emitter\'s claim about it.',
    },
    three_source_done: { type: 'boolean', description: 'You read plate, disassembly and decompilation yourself' },
    diff_reviewed: { type: 'boolean', description: 'You read the working-tree diff against HEAD' },
    blocking_count: { type: 'integer' },
    open_issues: { type: 'integer', description: 'Equivalence concerns you recorded in your verdict file' },
    ghidra_fixes: { type: 'integer', description: 'Ghidra corrections proposed in your verdict file' },
    note: { type: 'string' },
  }),
}

const GATE_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['gate_pass'],
  properties: withStops({
    gate_pass: { type: 'boolean' },
    errors: { type: 'integer' },
    warnings: { type: 'integer' },
    undefined_symbols: { type: 'integer' },
    tests_total: { type: 'integer' },
    tests_failed: { type: 'integer' },
    detail: { type: 'string', description: 'What failed, verbatim enough to act on. Empty when clean.' },
  }),
}

const BOOK_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'committed'],
  properties: withStops({
    addr: { type: 'string' },
    committed: { type: 'boolean' },
    commit_line: { type: 'string' },
    ghidra_applied: { type: 'integer' },
    ghidra_gate_clean: { type: 'boolean', description: 'Orphan ranges and error bookmarks both 0' },
    snapshot_exported: { type: 'boolean' },
    issues_logged: { type: 'integer' },
    tree_clean: { type: 'boolean', description: 'git status is clean afterwards' },
    target_lines: {
      type: 'integer',
      description: 'Line count of the target .c after the commit, from wc -l',
    },
    problems: { type: 'string' },
  }),
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

const RECOVER_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['tree_was_dirty', 'out_of_bounds', 'clean_now'],
  properties: withStops({
    tree_was_dirty: {
      type: 'boolean',
      description: 'True when anything at all was uncommitted before you started',
    },
    out_of_bounds: {
      type: 'boolean',
      description: 'True when something was dirty outside src/, tests/ and '
        + 'tools/code_emit/data/emit_state.json. You then change NOTHING and this '
        + 'run stops.',
    },
    out_of_bounds_paths: {
      type: 'array', items: { type: 'string' },
      description: 'The offending paths, verbatim from git status --porcelain',
    },
    discarded_paths: {
      type: 'array', items: { type: 'string' },
      description: 'What you actually threw away under src/ and tests/',
    },
    interrupted: {
      type: 'array',
      description: 'The functions the state file had marked in_flight, now recorded '
        + 'as interrupted. Usually zero or one.',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['addr'],
        properties: {
          addr: { type: 'string' },
          name: { type: 'string' },
          since: { type: 'string', description: 'The in_flight timestamp, if one was recorded' },
        },
      },
    },
    residue_without_marker: {
      type: 'boolean',
      description: 'True when src/ or tests/ was dirty but no in_flight entry named an '
        + 'owner for it. The residue is still discarded; the report says so.',
    },
    clean_now: { type: 'boolean', description: 'git status --porcelain prints nothing' },
    note: { type: 'string' },
  }),
}

const WORKLIST = {
  type: 'object',
  additionalProperties: false,
  required: ['functions', 'remaining_total'],
  properties: withStops({
    functions: {
      type: 'array',
      description: 'The worklist exactly as next_batch.py printed it, in order',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['addr', 'name', 'target'],
        properties: {
          addr: { type: 'string' },
          name: { type: 'string' },
          target: { type: 'string' },
          body_size: { type: 'string' },
          stubbed_callees: { type: 'array', items: { type: 'string' } },
        },
      },
    },
    remaining_total: {
      type: 'integer',
      description: 'How many functions are still not committed, from --stats',
    },
    conflicts: {
      type: 'string',
      description: 'Anything next_batch.py printed on stderr. Empty when clean.',
    },
  }),
}

const SPLIT_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['file', 'split', 'committed'],
  properties: withStops({
    file: { type: 'string' },
    split: { type: 'boolean', description: 'True if the file was actually split' },
    committed: { type: 'boolean' },
    new_files: { type: 'array', items: { type: 'string' } },
    gate_pass: { type: 'boolean' },
    note: { type: 'string' },
  }),
}

// ---------------------------------------------------------------- prompts

const ENV = `FDPS.LE is a 1997 Traditional Chinese DOS game (Watcom C/C++ 10.0a, DOS/4G LE
module) being rebuilt into functionally equivalent C. Ghidra has it open as the
only program, so leave the \`program\` parameter empty on every MCP call.

Load the Ghidra tools you need in ONE ToolSearch call:
  ToolSearch "select:mcp__ghidra__get_plate_comment,mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_function_callees,mcp__ghidra__get_xrefs_to,mcp__ghidra__get_function_signature,mcp__ghidra__read_memory"

If a Ghidra call fails or times out, retry it ONCE. If the retry works, carry on
and set no flags. If it fails again, stop work on this function, set
ghidra_unreachable to true and put what you saw in stop_detail. That flag is the
only signal the workflow has that Ghidra is gone; never set it for a blip that
the retry cleared, and never guess at an answer Ghidra would have given.`

const RULES = `# Rules for this pipeline. Not suggestions.

ONE FUNCTION. You work on the function you were given and no other. Do not emit,
fix, rename or tidy anything else, however tempting the neighbour looks.

THREE SOURCES, always. Plate comment, disassembly and decompiled C, every time,
including for a function that looks trivial. The disassembly is the primary
source; the decompiled C is a second opinion that invents parameters and is
routinely wrong about which value a CALL returned. Every place the C uses a value
after a CALL must be checked against the assembly.

READ-ONLY ON GHIDRA. Do not rename, do not set plate comments, do not set
prototypes, do not create labels. If you find something in Ghidra that is wrong,
write the correction into your verdict file under ghidra_fixes; the bookkeeper
applies it after the review passes. A Ghidra write from you is a defect.

C89. Declarations come before statements in every block; wcc386 reports a mixed
block as a cascade of "E1077: Missing '}'" that points nowhere near the cause.
No // comments, no declarations in for-statements.

FILE NAMES 8.3. Every .c and .h basename is at most 8 characters. The DOS-hosted
toolchain has no long-filename support and truncates silently into a different
file.

SYMBOL NAMES ARE BYTE-IDENTICAL TO GHIDRA. Same spelling, same underscores, same
case, character for character (rebuild_info/naming.md). That correspondence is
the only thing that lets the C and the database be checked against each other
later.

EVERY LOCAL AND EVERY PARAMETER HAS A NAME THAT SAYS WHAT IT IS. The decompiler's
own names -- iVar1, uVar3, param_1, local_8, puVar2, in_EAX, DAT_00069cd8,
CONCAT44 -- do not survive into src/, and iVar1_index is not a fix. The name says
the value's role in this function (tile_index, remaining_mp, cursor_row), never
its type (int_var) or the register it came out of (eax_val). A loop counter
called i or j is a convention, not a default name, and is fine.

src/ is what the rest of this project reads to find out what the game does. A
function full of iVar1 reads exactly like the decompiler output it came from,
which means this step captured nothing. And the name is the evidence: not being
able to say what a local holds almost always means that stretch of control flow
has not actually been read yet.

WHEN YOU CANNOT WORK OUT WHAT A VALUE IS FOR, THAT IS AN open_issues ENTRY, NOT A
NAME YOU INVENT. A confident wrong name is worse than iVar1, because iVar1 at
least tells the next reader that nobody knows. Say what you could establish, say
what would settle it, and leave the honest gap.

The build refuses to compile a source carrying a decompiler default name and says
which line, so the mechanical half of this costs you nothing to check
(rebuild_info/naming.md). What it cannot check is whether a name that is not a
default is actually true, and that half is reviewed.

A CONCERN IS FOR EVIDENCE THAT DOES NOT EXIST YET, NOT FOR A LOOKUP YOU DID NOT
DO. Before you write an open_issues entry, ask what would answer it and whether
that thing is reachable from where you are sitting. These are, always:

    Ghidra, read-only -- get_xrefs_to over every call site, the disassembly of
      any caller or callee, search_instructions across the image
    the shipped game files and resources under fdps_game_files/ and the
      unpacked resource dumps
    the knowledge base -- program_info/, resource_info/, assets/, chapters/,
      rebuild_info/
    THE .OBJ YOUR OWN BUILD JUST PRODUCED, under
      workspace/code_emit/out/objs/, disassembled read-only with
      WATCOM_10.0a/BINNT/WDISASM.EXE

That last one is the one people forget: the object file holding the function you
just wrote is sitting on disk, and comparing it against the original's bytes
answers "did the compiler really do that" without guessing.

If the answer is reachable, GO AND GET IT. Sweep every call site rather than
sampling; read the caller's disassembly rather than assuming what it passes. It
costs the same tokens now as it would later, and later it costs a whole fresh
context to rebuild everything you already have in front of you.

Record a concern when the evidence genuinely does not exist yet: a callee that
has not been emitted, a data symbol ticket 23 has not written, behaviour only a
real machine settles. "I did not go and look" is not one of those.

This does not narrow the rule above about a value whose purpose you cannot work
out. That one stays exactly as written: an honest gap beats an invented name.

YOUR VERDICT RECORDS FINDINGS, IT DOES NOT LEGISLATE. Do not write a conditional
obligation into the needs field -- "if X turns out to be true then this should be
rewritten as Y". A later stage will read that as an instruction and carry it out,
and nothing between here and there checks whether the rule you invented was ever
sound. This has already happened once: an emitter wrote "if this is an inline
expansion then the call site should become a real call", it was proven to be one,
and the resulting change would have put a CALL in the rebuild that the original
does not have.

ADR-0001 is the standard and it is functional equivalence: observable behaviour
and function, explicitly NOT register allocation, instruction selection or
anything else at the binary level. Recovering the exact source text the original
author typed is not the standard. Two spellings that compile to equivalent
behaviour are BOTH correct, and preferring one is a note, never a blocking
finding. State what you found and what it means; leave the obligation out.

CALLING CONVENTION IS DECLARED IN THE CODE, never inherited from the flag set.
The binary was built with -4s, the stack convention, and all 503 compiler-emitted
game functions use it (rebuild_info/build_flags.md). Declare it as:

    #pragma aux <exact symbol name> "*" parm caller [];

next to the function's prototype. This was measured to produce byte-identical
code to the plain default, and "*" is what keeps the symbol undecorated.

NEVER write __cdecl or __watcall in the C. Watcom's __cdecl prefixes the symbol
with an underscore, which breaks the name rule above. The pragma is how a
convention is stated here.

NO HALF MEASURES. No renamed stand-in, no empty shell, no _impl suffix, no
"static for now". If you cannot finish it honestly, report blocked with the
reason.

NEVER BEND THE CODE TO PASS A TEST. If the test disagrees with the emitted C,
one of them is wrong about the assembly; find out which. Fixing a test by
weakening the assertion is the same defect as fixing code by weakening it.

FIX CAUSES, NOT SYMPTOMS. No workarounds.

DECLARATIONS HAVE ONE OWNER. A global's extern belongs in the .h of the .c that
routing.json says owns it -- gamedata.h for anything read from more than one
file -- and nowhere else. Never write a private extern into the .c you are
working on: a second copy of a declaration does not follow the definition when
its type changes, and the linker will not say a word.

GAME RECORD LAYOUTS COME FROM src/fdpstype.h. It is generated from ticket 17's
settled layouts and it is complete. Include it; do not restate a struct, do not
declare a local look-alike, and do not "fix" a field. If a layout there really
is wrong, that is a ghidra_fixes entry against the struct, not an edit here.

NOTHING OUTSIDE THE PIPELINE'S OWN PATHS MAY BE LEFT UNCOMMITTED. The recovery
stage that starts every run reads the working tree to decide what a killed
session left behind, and it can only tell wreckage from work by the path. These
six, and nothing else, may be dirty at any moment:

    src/  tests/  tools/code_emit/data/  ghidra_snapshot/
    tools/code_emit/build_routing.py  rebuild_info/code_layout.md

Every one of them is produced by a stage of this pipeline and can be thrown away
and made again. Anything dirty outside that set stops the next run and calls for
a human, which is exactly what this pipeline exists not to need.

So if the work you were given makes you change something else -- a build script,
the gate, another knowledge-base page -- COMMIT THAT CHANGE BY ITSELF,
immediately, before you carry on, with its own subject saying what it is and why.
Do not leave it sitting in the tree and do not let it ride into the function's
landing commit: that commit says "this function, reviewed and gated", and a gate
change inside it is neither reviewed as one nor findable afterwards. Two honest
commits, never one dishonest one.

This is about a change INCIDENTAL to your task. A stage whose given task is
those files -- the split stage owns routing and code_layout.md -- commits its own
work in its own commit, which is the same rule and not an exception to it.

THE DATA IS NOT EMITTED YET, AND THAT IS FINE. Ticket 23 writes the real
contents of every data_fdps_* global; until then the build defines them
zero-filled so the program links. So: declare what you read, read it, and never
assert in a test that one of them holds a particular value. The same goes for a
callee that has not been emitted -- it is a stub returning 0 while it waits.`

const PLAYTEST_BUGS = `# The eight implicit contracts (from the FD2 rebuild; canon in rebuild_info/emit_pipeline.md)

These are the bug classes that compile clean and pass unit tests and still behave
differently on real hardware, because they are contracts the function has with
its environment rather than arithmetic it gets wrong. Check each one against the
function in front of you and say in your verdict which ones apply and why not
when they do not.

A  Register clobber across a vendor call. What a vendor function really destroys
   is part of its ABI. Under the stack convention EBX is callee-saved, so a
   partial "modify" list moves the corruption rather than fixing it -- all four
   of eax ebx ecx edx go in the list or none do.
B  Adjacency and order of uninitialised globals. Watcom does not promise the
   order or the neighbourliness of tentative definitions, and has been measured
   putting them in reverse. Anything that indexes across neighbouring globals as
   though they were one array must be emitted AS one array or struct.
C  Signedness of data. Signedness is behaviour, not representation: it decides
   the branch the moment a value is compared. Read the assembly's JGE/JLE versus
   JAE/JB.
D  Instruction count in a timing-sensitive hot loop. Sound effects run in real
   time while drawing scales with the emulator's cycle setting, so a loop that
   is equivalent but shorter can break a timing balance the original relied on.
E  Hard-coded absolute addresses. The rebuild does not place anything where the
   original placed it, so an address written as a literal points at nothing.
   Always reference the symbol.
F  The call form of a math intrinsic. Whether sqrt/sin/cos compile to an
   intrinsic or to a named CRT call is codegen, and the header's pragmas are
   part of it. Getting it wrong pulls vendor code into the runtime that the
   original never executed.
G  Where stack probes are. Which translation units carry __CHK is a load-bearing
   contract: all-on gets units killed by an interrupt's private stack, all-off
   loses the overflow guard. The original built the game code with -s.
H  A folded base attributed to the wrong symbol. When the compiler folds a
   constant index into the displacement, Ghidra attributes the base to the
   PREVIOUS symbol. The tell is an index whose maximum exceeds the declared
   element count. The build gate cannot see this class at all.`

function emitterPrompt(fn, mode, reviewNote) {
  const head = [ENV, '', RULES, '', PLAYTEST_BUGS, '',
    '# Role: Emitter' + (mode === 'fix' ? ' (fix round)' : ''),
    '',
    'Your function:',
    '  address     ' + fn.addr,
    '  name        ' + fn.name,
    '  target file ' + REPO + '\\src\\' + fn.target,
    '  test file   ' + REPO + '\\tests\\' + fn.target,
    '  Ghidra body size ' + (fn.body_size || 'unknown'),
    ((fn.stubbed_callees && fn.stubbed_callees.length)
      ? '  callees that were not emitted when this batch started, so they may\n'
        + '  still be zero-returning stubs: ' + fn.stubbed_callees.join(', ') + '\n'
        + '  Check src/ before you believe that list -- one of them may have\n'
        + '  landed earlier in this same batch, in which case it is real code and\n'
        + '  a test may rely on it. For the ones that are still stubs, your test\n'
        + '  may assert only what does not depend on what they return.'
      : '  every callee it has is already emitted'),
  ].join('\n')

  const body = mode === 'fix' ? [
    '',
    '# This round: fix what the reviewer blocked on. Change nothing else.',
    '',
    'The reviewer wrote its full findings to ' + VERDICTS + '\\' + fn.addr + '.review.json.',
    'Read that file. It is the work item for this round; the summary below is only a pointer.',
    '  ' + (reviewNote || '(no note)'),
    '',
    'For each blocking issue: either fix it and say how in your verdict file, or',
    'disagree and record the assembly evidence for why no change is needed. Silence',
    'on an issue is not an answer.',
  ].join('\n') : [
    '',
    '# This round: emit the function from nothing.',
    '',
    '0. Before you read anything or write any code, leave the footprint. In',
    '   tools/code_emit/data/emit_state.json set the entry for key "' + fn.addr + '" to',
    '   status "in_flight" with in_flight_since set to the current local time as',
    '   YYYY-MM-DD HH:MM. Write it with python, json.dumps(indent=2, ensure_ascii=False)',
    '   plus a trailing newline, UTF-8. Touch no other entry and no other field.',
    '   Do NOT commit it. Leaving it uncommitted is the whole design: if this session is',
    '   killed -- a usage limit does not give anyone a chance to tidy up -- that',
    '   uncommitted line is the only thing left saying which function was in the air, and',
    '   the next run reads it, throws away whatever you had half-written, and puts this',
    '   address back on the worklist. The bookkeeper carries the entry on to "committed"',
    '   in the landing commit, so it never survives a function that finished.',
    '',
    'A. Read all three sources for ' + fn.addr + '. Work out the calling convention from',
    '   the prologue shape and from what the callers do after the CALL, not from what',
    '   Ghidra\'s signature claims -- Ghidra\'s automatic analysis labels this whole',
    '   binary __watcall and the measurement says otherwise. Mark every point where a',
    '   value is used after a CALL, and check each against the assembly.',
    '   If the plate comment says the function is a decompiler fragment or a linker',
    '   artefact, do not emit it: report status "skip" with the reason.',
    '',
    'B. Write the C into src/' + fn.target + '. If the file does not exist yet, create it',
    '   with a header comment naming what the file holds, and create src/' +
      fn.target.replace(/\.c$/, '.h') + ' with the prototype and the calling-convention',
    '   pragma. Nothing needs wiring into the build: tools/code_emit/build_emit.py',
    '   compiles whatever src/ and tests/ contain.',
    '',
    '   What to include, and where a declaration comes from:',
    '     src/fdpstype.h    every game record layout, generated from ticket 17.',
    '     src/<owner>.h     the prototype of a function in another file, and the',
    '                       extern of a global that file owns. Which file owns a',
    '                       symbol is in tools/code_emit/data/routing.json, and',
    '                       rebuild_info/code_layout.md explains the rule.',
    '     src/gamedata.h    the externs of globals read from more than one file.',
    '   If the owning header does not exist yet, create it with just the extern',
    '   you need; the file that owns the definition will fill it in when it is',
    '   emitted. Adding an extern to somebody else\'s header is right; adding one',
    '   to your own .c is not.',
    '',
    '   Name every local and every parameter for what it holds. This is not a',
    '   tidying pass to do at the end: you worked out what each value is in step A,',
    '   when you followed it through the assembly, and the name is where that work',
    '   gets written down. Carrying Ghidra\'s iVar1 through to the C and renaming it',
    '   afterwards is how a name ends up describing what the C looks like rather',
    '   than what the assembly does. If step A left you unable to say what a value',
    '   is for, that is an open_issues entry and the honest gap stays -- do not',
    '   invent a plausible name to fill it.',
    '',
    'C. Write the test into tests/' + fn.target + '. The file must define',
    '   void run_' + fn.target.replace(/\.c$/, '') + '_tests(void) -- that exact name is how the',
    '   generated entry point finds it -- and register each case with RUN_TEST. Use',
    '   CHECK_EQ for assertions; see tests/testharn.h. Coverage is risk-driven:',
    '   arithmetic, branch structure, state transitions and anything touching a value',
    '   that came back from a CALL must be asserted. Expected values come from the',
    '   assembly or from the strategy-guide knowledge base, never from a guess. A test',
    '   that reads a real game file must read the real file, staged by listing its 8.3',
    '   name in tests/gamefile.lst; fabricating a stand-in file proves nothing.',
    '',
    'D. Build it, in the foreground, and read what comes back:',
    '     python ' + REPO + '\\tools\\code_emit\\build_emit.py all',
    '   Never run it in the background: you end the moment you send your final',
    '   message, so a background build has nobody left to read it. Zero errors, zero',
    '   warnings, every test green. Red means fix it or report blocked with the real',
    '   cause.',
    '',
    '   The build links twice on purpose. The first link reports every symbol',
    '   nothing defines yet and writes them to workspace/code_emit/undefined.json;',
    '   the second link supplies them zero-filled and must come out clean. So an',
    '   "undefined symbol" in the first link\'s output is expected and is not your',
    '   failure -- unless it is a symbol that must never be stubbed, which the',
    '   build says in as many words and fails on. Read undefined.json after your',
    '   build and check that every symbol of yours in it is one you meant to',
    '   borrow. A name routing.json has never heard of fails the build, but a name',
    '   that is real and simply not the one the assembly reads gets stubbed like',
    '   any other, and zero is a plausible-looking answer.',
  ].join('\n')

  const tail = [
    '',
    '# Write your verdict to a file, then return the summary',
    '',
    'Use the Write tool to create exactly ' + VERDICTS + '\\' + fn.addr + '.emit.json,',
    'UTF-8, this shape (overwrite it on a fix round, keeping the history in rounds):',
    '',
    '{',
    '  "addr": "' + fn.addr + '",',
    '  "name": "' + fn.name + '",',
    '  "target": "' + fn.target + '",',
    '  "calling_convention": "stack (-4s, caller cleans)" or what you measured,',
    '  "cc_evidence": "<the instructions that decide it>",',
    '  "signature": "<the C prototype as emitted>",',
    '  "control_flow": "<how the assembly\'s branches map onto the C>",',
    '  "call_returns": "<every use of a value after a CALL and what the assembly says it is>",',
    '  "playtest_contracts": {"A": "<applies / does not apply, and why>", ... "H": "..."},',
    '  "tests": ["<what each assertion pins down and where its expected value came from>"],',
    '  "build": {"errors": 0, "warnings": 0, "undefined": 0, "tests_total": N, "tests_failed": 0},',
    '  "ghidra_fixes": [{"kind": "plate|rename|prototype", "address": "...", "what": "...", "why": "..."}],',
    '  "open_issues": [{"what": "<equivalence concern you could not settle>", "needs": "<what would settle it>"}],',
    '  "rounds": [{"round": N, "did": "<what changed and why>"}]',
    '}',
    '',
    'open_issues is for concerns that only a later function, or a real machine, can',
    'settle -- FPU rounding, a width you could not pin down, a table whose owner is not',
    'yet emitted. Nothing in this batch will look at them again: they are settled in one',
    'sweep over emit_issues.json after all 514 functions have landed. So an entry has to',
    'stand on its own months from now -- say what you established, and say in `needs`',
    'exactly what evidence would settle it. Leaving one unrecorded is how it gets lost;',
    'recording one that YOU COULD HAVE SETTLED is the failure the convergence rule above',
    'is about.',
    '',
    'Your final message is the summary object and nothing else. It is data for a',
    'script, not a report for a person.',
  ].join('\n')

  return head + '\n' + body + '\n' + tail
}

function reviewerPrompt(fn, round) {
  return [ENV, '', RULES, '', PLAYTEST_BUGS, '',
    '# Role: Reviewer, round ' + round + '. Independent.',
    '',
    'You do not trust the emitted C, you do not trust the emitter\'s verdict file, and',
    'you do not trust Ghidra\'s decompiled C. Every conclusion you reach, you reach from',
    'the assembly yourself. Confirming that a name exists is not review.',
    '',
    'Your function: ' + fn.addr + '  ' + fn.name + '  ->  src/' + fn.target,
    '',
    '# Steps',
    '',
    'A. See exactly what this round changed. Run all three, in this order:',
    '     cd ' + REPO + ' && git status --porcelain',
    '     cd ' + REPO + ' && git add -N -- src tests',
    '     cd ' + REPO + ' && git --no-pager diff HEAD',
    '   The middle command is not optional and it is not a mistake. A plain diff does',
    '   not show untracked files, and the first function to land in a new module',
    '   creates every one of its files new -- without the intent-to-add, your view of',
    '   "what changed this round" would be empty for exactly the code you are here to',
    '   review. It stages no content -- the bookkeeper stages for real later -- but it',
    '   does put the path in the index, which is why the cleanup and recovery stages',
    '   unstage before they discard anything.',
    '   One function is in flight at a time and every approved function is its own',
    '   commit, so everything after HEAD belongs to this function. Review all of it.',
    '   One modified file is expected and is not the emitter\'s work to review:',
    '   tools/code_emit/data/emit_state.json, whose entry for ' + fn.addr + ' now says',
    '   in_flight. That is the marker the pipeline leaves so a killed session can be',
    '   recovered from. Confirm it says in_flight for this address and nothing else in',
    '   that file changed; if it names a different address, or if some other function\'s',
    '   entry moved, block on it -- somebody is standing in somebody else\'s tree.',
    '   Anything else uncommitted -- a build script, the gate, a knowledge-base page --',
    '   is a blocking finding on its own. The emitter is required to commit such a change',
    '   by itself before carrying on, precisely so it does not end up inside this',
    '   function\'s commit and so the next run\'s recovery stage is not left guessing',
    '   whose it was. Block on it and name the path.',
    '',
    'B. Read the three sources for ' + fn.addr + ' yourself.',
    '',
    'C. Read the emitter\'s verdict at ' + VERDICTS + '\\' + fn.addr + '.emit.json.',
    '   Judge whether the file is actually there and complete, and report that as',
    '   emit_verdict_present -- not what the emitter said about it.',
    '',
    'D. Work the checklist. Every item gets a verdict and evidence quoting the',
    '   instruction or the source line it rests on:',
    '   1. Control flow: the assembly\'s branches, loops and call order against the C.',
    '   2. Values used after a CALL: each one checked against the assembly, because',
    '      Ghidra systematically mis-attributes them.',
    '   3. Calling convention and parameter count, from the callers. Declaring fewer',
    '      parameters than the ABI passes makes the callee read stack garbage.',
    '   4. Widths and signedness, and any floating-point rounding.',
    '   5. The calling convention is declared in the code with the pragma, and no',
    '      __cdecl or __watcall keyword appears.',
    '   6. Symbol names byte-identical to Ghidra; C89 declaration placement; 8.3.',
    '   7. The eight implicit contracts above, each one answered for this function.',
    '   8. Test quality: does every risky path have a real assertion, and did every',
    '      expected value come from the assembly or the knowledge base rather than from',
    '      the emitted C itself? A test that asserts what the code happens to do is',
    '      worth nothing. Block on a fabricated game file or a weakened assertion.',
    '   9. No half measures, and nothing bent to make a test pass.',
    '  10. Ghidra\'s own facts: does the plate comment describe what the assembly does,',
    '      does the name mean what the function means? Propose corrections in your',
    '      verdict file; do not apply them.',
    '  11. Declarations: every global it reads is declared in the header of the file',
    '      routing.json says owns that symbol, and every record layout comes from',
    '      src/fdpstype.h. A private extern in the .c, or a struct restated by hand,',
    '      is a blocking finding -- both survive a type change in silence.',
    '  12. Stubs: the data is zero-filled until ticket 23 and an unemitted callee',
    '      returns 0. Read workspace/code_emit/undefined.json for what is currently',
    '      stubbed, and block on any assertion whose expected value depends on one of',
    '      them. Such a test passes today and turns red the moment the real value',
    '      lands, which is the worst possible time to find out it was never a test.',
    '  13. Naming inside the function. Do NOT sweep every local: the build already',
    '      refuses a Ghidra default name and names the line, so a regex-detectable',
    '      one cannot reach you, and re-checking for it is spent effort. What only',
    '      you can judge is whether a name that is not a default is TRUE. You have',
    '      already had to work out what several of these values are -- items 1 to 4',
    '      cannot be answered without it -- so this item is about those values and',
    '      no others: for each one you formed an opinion about, does its name say',
    '      what you concluded? A name that says something the assembly does not',
    '      support is a blocking finding, and it is a worse defect than a default',
    '      name, because it reads as settled knowledge. A vague but not wrong name',
    '      is a note, not a block. If the emitter recorded an open_issues entry',
    '      instead of naming a value it could not pin down, that is the rule working',
    '      -- say so and do not push for a name.',
    '',
    '# What to block on',
    '',
    'Block on anything that breaks equivalence, breaks the build, or breaks one of the',
    'rules above. Do not block on a different but equivalent way of writing something,',
    'or on style. When you are unsure, say so in the verdict and ask the emitter for',
    'evidence rather than blocking outright.',
    '',
    'Equivalence means ADR-0001: observable behaviour and function, explicitly not',
    'register allocation, instruction selection, or which of two spellings the original',
    'author typed. So separate the two questions before you block. "The rebuilt code',
    'behaves differently" is blocking. "The original source probably said this another',
    'way" is a note, however good the evidence -- an open-coded expression against an',
    'inlined call, a byte offset against a struct field. Both compile to the same',
    'behaviour, so both are correct, and a rewrite to chase the original\'s wording can',
    'easily land further away than it started.',
    '',
    'Check the emitter\'s open_issues against the same standard. Two failures to catch:',
    'a concern it could have settled with a read it did not do (RULES says which sources',
    'are always reachable, and the .OBJ its own build produced is one of them), and a',
    'conditional obligation in `needs` -- "if X then rewrite as Y" -- which is the',
    'emitter legislating for a later stage. Both are blocking findings. An honest concern',
    'about evidence that does not exist yet is the rule working; say so and move on.',
    '',
    '# Write your verdict to a file, then return the summary',
    '',
    'Write ' + VERDICTS + '\\' + fn.addr + '.review.json, UTF-8:',
    '',
    '{',
    '  "addr": "' + fn.addr + '", "round": ' + round + ', "approved": true|false,',
    '  "emit_verdict_present": true|false,',
    '  "checklist": [{"item": 1, "verdict": "pass|fail", "evidence": "..."}],',
    '  "blocking_issues": [{"item": N, "where": "<file and function>", "claim": "...", "evidence": "...", "fix": "<what would satisfy you>"}],',
    '  "notes": ["<non-blocking observations>"],',
    '  "ghidra_fixes": [{"kind": "plate|rename|prototype", "address": "...", "what": "...", "why": "..."}],',
    '  "open_issues": [{"what": "...", "needs": "..."}]',
    '}',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function gatePrompt(fn) {
  return [
    '# Role: Gate. You judge, you do not fix.',
    '',
    'Function ' + fn.name + ' @ ' + fn.addr + ' has passed review. Run the build gate over',
    'the working tree before it is committed.',
    '',
    'Run this in the foreground and wait for it:',
    '  cd ' + REPO + ' && python tools/build_gate/gate.py check --target emittest',
    '',
    'It rebuilds src/ and tests/ inside DOSBox-X, runs the test image, and runs the',
    'registered self-tests. It prints a verdict and writes',
    'workspace/build_gate/result.json. Read the result file for the detail rather than',
    'scraping the console.',
    '',
    'Report what it said. Do not edit any source to make it pass -- that is the',
    'emitter\'s job on the next round, and it needs to know exactly what failed. If the',
    'gate cannot run at all (DOSBox-X missing, the Watcom install unusable, preflight',
    'refusing), that is toolchain_unreachable, and it is a different thing from a build',
    'that ran and reported errors.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function bookkeepPrompt(fn, hasGhidraFixes, rounds) {
  return [
    '# Role: Bookkeeper. No judgement, no code changes.',
    '',
    'Function ' + fn.name + ' @ ' + fn.addr + ' passed review and passed the gate. Land it.',
    '',
    '1. Ghidra corrections. Read the ghidra_fixes arrays in',
    '     ' + VERDICTS + '\\' + fn.addr + '.emit.json',
    '     ' + VERDICTS + '\\' + fn.addr + '.review.json',
    (hasGhidraFixes
      ? '   Apply each one exactly as written -- rename_function_by_address, '
        + 'set_plate_comment, set_function_prototype as the kind says. You are '
        + 'transcribing decisions, not making them. If a fix cannot be applied, say so '
        + 'in problems and leave it; do not adjust the decision to make it fit.'
      : '   Both are expected to be empty this time. If they are not, apply them as '
        + 'written and say so.'),
    '',
    '2. If and only if you changed Ghidra:',
    '   - run the baseline audit and check the "# Gate" section: orphan code ranges 0,',
    '     error bookmarks 0.',
    '       run_ghidra_script ' + REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java',
    '   - save the program with save_program',
    '   - re-export the text snapshot so the commit carries the database change:',
    '       run_ghidra_script ' + REPO + '\\tools\\ghidra_snapshot\\ExportGhidraSnapshot.java',
    '   Load these in ONE ToolSearch call:',
    '     ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__set_plate_comment,mcp__ghidra__rename_function_by_address,mcp__ghidra__set_function_prototype,mcp__ghidra__save_program,mcp__ghidra__list_bookmarks"',
    '',
    '3. Progress. In tools/code_emit/data/emit_state.json the entry for key "' + fn.addr + '"',
    '   currently says in_flight -- the emitter wrote that before it started, deliberately',
    '   uncommitted, so a killed session leaves a trace. Carry it forward now: status',
    '   "committed", emitted_against "' + (fn.body_size || '') + '", drop in_flight_since,',
    '   rounds ' + rounds + ' (that is the count the workflow observed -- use it verbatim,',
    '   do not recount). Write it with python, json.dumps(indent=2, ensure_ascii=False)',
    '   plus a trailing newline, UTF-8. Touch no other entry and no other field.',
    '   There is deliberately no commit hash in this file: a hash cannot be recorded',
    '   inside the commit that creates it, and amending to add it afterwards produces',
    '   a hash that points at the object the amend threw away. The commit subject',
    '   carries the address instead, so the landing commit is found with',
    '   git log --oneline --grep "@ ' + fn.addr + '".',
    '',
    '4. Equivalence concerns. Append every open_issues entry from both verdict files to',
    '   tools/code_emit/data/emit_issues.json, keyed by the 8-hex address ("' + fn.addr + '",',
    '   not "0x...") . Create the file as {} if it does not exist. Give every entry',
    '   "status": "open" and "from": "emit" or "review". This file is the ENTIRE input',
    '   to the closing sweep that runs once every function has landed, and that sweep',
    '   selects on status, so an entry written without the field drops silently out of',
    '   every future search for outstanding concerns -- nothing else in this pipeline',
    '   will ever look at it again. Read it back after writing and check the encoding',
    '   survived: this is a Traditional Chinese Windows machine and an unspecified',
    '   encoding produces mojibake.',
    '   Report the number of entries you appended as issues_logged.',
    '',
    '5. Strays FIRST, before you stage anything. Run  git status --porcelain  and look',
    '   for anything outside src/, tests/, tools/code_emit/data/ and ghidra_snapshot/ --',
    '   a build script, the gate, a knowledge-base page an earlier stage had to touch.',
    '',
    '   Do NOT sweep it into the function\'s commit. That commit says "this function,',
    '   reviewed and gated", and something that was neither reviewed nor gated as part of',
    '   it does not belong under that subject, where nobody will ever find it again.',
    '   Leaving it uncommitted is not an option either: the next run\'s recovery stage',
    '   would see a dirty path outside the pipeline\'s own and stop for a human.',
    '',
    '   So commit it by itself, NOW, while nothing else is staged, naming the file',
    '   explicitly so the index cannot smuggle anything in:',
    '     git add <path> && git commit -- <path>',
    '   with an honest subject in Traditional Chinese saying what it is and why it had to',
    '   change, and the same Co-Authored-By trailer. The pathspec on the commit is not',
    '   decoration: git commits the index, not your intention, so a bare  git commit',
    '   after the function is staged would land the whole function under the stray file\'s',
    '   subject and leave nothing for the real commit. Doing this before any  git add',
    '   src  is the other half of the same protection. Report the path in problems.',
    '',
    '6. Now stage the function -- the source, the tests, the state files, and the Ghidra',
    '   snapshot if you re-exported it -- and NOTHING ELSE:',
    '     git add src tests tools/code_emit/data ghidra_snapshot',
    '   Check with  git --no-pager diff --staged --stat  that the range is this function',
    '   and nothing else, and with  git status --porcelain  that nothing is left over.',
    '   Then commit with this subject:',
    '',
    '     emit: ' + fn.name + ' @ ' + fn.addr + '，reviewer 通過、build gate 通過',
    '',
    '   a blank line, one line in Traditional Chinese on what the function does, a blank',
    '   line, then:',
    '     Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>',
    '',
    '7. Report the commit as commit_line, built as  <hash> ' + fn.name + ' @ ' + fn.addr,
    '   where <hash> comes from  git --no-pager log --format=%h -1  . Do NOT pipe the',
    '   commit subject itself back: it is Traditional Chinese, this is a Traditional',
    '   Chinese Windows machine, and reading it through a console pipe turns it into',
    '   mojibake that then gets archived as the permanent record of this run.',
    '   Also confirm the tree is clean:  git status --porcelain  must print nothing.',
    '',
    '8. Report how long the target file has become:',
    '     wc -l src/' + fn.target,
    '   as target_lines. It decides nothing here; the workflow watches the budget.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function abandonPrompt(fn, why, terminal) {
  return [
    '# Role: Bookkeeper, ' + (terminal ? 'recording one function as not emittable' : 'abandoning one function') + '.',
    '',
    'Function ' + fn.name + ' @ ' + fn.addr + ' will not be landed: ' + why,
    '',
    'The next function needs a clean tree, and a half-emitted function left behind',
    'would land inside somebody else\'s diff and be committed as theirs.',
    '',
    '1. Look at what is there first:  cd ' + REPO + ' && git status --porcelain && git --no-pager diff --stat HEAD -- src tests',
    '2. Discard the working-tree changes under src/ and tests/ only, all three commands:',
    '     git reset -q -- src tests',
    '     git checkout -- src tests',
    '     git clean -fd src tests            (the files the emitter created new)',
    '   The reset is not optional. The reviewer runs  git add -N -- src tests  so its',
    '   diff shows new files, and an intent-to-add file is in the index: checkout',
    '   truncates it to zero bytes rather than removing it and clean skips it as tracked,',
    '   leaving a zero-byte .c for the next function\'s bookkeeper to commit as its own.',
    '   Do NOT touch workspace/ -- the verdict files are the record of what went wrong',
    '   and the next run reads them. Do not touch anything already committed: this reset',
    '   takes a pathspec and only unstages, and there is no other reset here.',
    '3. In tools/code_emit/data/emit_state.json the entry for "' + fn.addr + '" says',
    '   in_flight, uncommitted, left there by the emitter. Set it to status',
    '   "' + (terminal ? 'skip' : 'failed') + '", drop in_flight_since, and put the reason in note. Write it with',
    '   python, UTF-8, json.dumps(indent=2, ensure_ascii=False) plus a trailing newline.',
    '   This step is the whole point of this stage: without it the worklist hands the',
    '   same address out again on every future run, forever.',
    '4. Commit only that one state file:',
    '     git add tools/code_emit/data/emit_state.json && git commit',
    '   subject:  emit: ' + fn.name + ' @ ' + fn.addr
      + (terminal ? ' 不 emit，記入工作狀態' : ' 未完成，記入工作狀態'),
    '   then a blank line, one line on why, a blank line, and',
    '     Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>',
    '5. Confirm the tree is clean and report it.',
    '',
    'Report committed=true only if that state commit was made. Your final message is',
    'the summary object and nothing else.',
  ].join('\n')
}

function recoverPrompt() {
  return [
    '# Role: Recovery. Mechanical. You judge no code and you emit nothing.',
    '',
    'A previous run of this pipeline may have been killed outright -- a usage limit',
    'ends the session where it stands, so the failure paths that normally tidy up',
    'never ran. Anything they left behind is in the working tree right now, and the',
    'first function of this run would otherwise have somebody else\'s half-written',
    'code inside its review diff and inside its commit. Clear it before anything',
    'starts.',
    '',
    '1. Look, before you touch anything:',
    '     cd ' + REPO + ' && git status --porcelain',
    '   If it prints nothing, the tree is clean. Skip to step 4 -- there may still be',
    '   an in_flight entry from a run that died before it wrote any code.',
    '',
    '2. Classify every line it printed. These six paths, and only these, are the ones',
    '   this pipeline\'s own stages write, so they are the ones you may clean:',
    '     src/                              emitted code from the dead run',
    '     tests/                            its test',
    '     tools/code_emit/data/             the in-flight marker and the issue log',
    '     ghidra_snapshot/                  a re-export the bookkeeper had started',
    '     tools/code_emit/build_routing.py  the split stage\'s routing edit',
    '     rebuild_info/code_layout.md       the split stage\'s file table',
    '   Ignored, because git does not track them at all: workspace/ and anything else',
    '   already covered by .gitignore. They will not appear in --porcelain; if one',
    '   somehow does, leave it alone and do not count it as out of bounds.',
    '',
    '   ANY OTHER PATH IS OUT OF BOUNDS. A dirty devlog/, docs/, another rebuild_info/',
    '   page, another tools/ script -- that is not this pipeline\'s wreckage, and',
    '   guessing what it was for is how somebody\'s unrelated afternoon gets deleted. In',
    '   that case: change NOTHING, discard NOTHING, set out_of_bounds true, list the',
    '   paths verbatim, and return. The run stops.',
    '',
    '3. Read tools/code_emit/data/emit_state.json BEFORE you discard anything -- step 4',
    '   needs what is in it now, and step 3 is about to revert it.',
    '',
    '   Then discard the residue, across exactly those six paths and no others:',
    '     git reset -q -- src tests tools/code_emit/data ghidra_snapshot tools/code_emit/build_routing.py rebuild_info/code_layout.md',
    '     git checkout -- src tests tools/code_emit/data ghidra_snapshot tools/code_emit/build_routing.py rebuild_info/code_layout.md',
    '     git clean -fd src tests',
    '   The  git reset  is not optional and it is not redundant. The reviewer stage runs',
    '   git add -N -- src tests  so that untracked new files show up in its diff, and a',
    '   file that is intent-to-add is IN THE INDEX: checkout truncates it to zero bytes',
    '   instead of removing it, clean skips it as tracked, and you are left with a',
    '   zero-byte .c that the next function\'s bookkeeper commits as its own. Unstage',
    '   first and both commands work as expected.',
    '',
    '   Never workspace/: the verdict files there are the record of what the dead run',
    '   had worked out, and the re-run reads them. Never anything already committed --',
    '   no revert, no amend, and no reset that moves HEAD (the reset above is a pathspec',
    '   reset, which only unstages). Every landed function is somebody\'s finished work',
    '   and this stage has no opinion about any of it.',
    '',
    '   If ghidra_snapshot/ was among the dirty paths, the Ghidra database itself may',
    '   have been changed and saved before the kill, in which case reverting the text',
    '   snapshot leaves it describing a database that no longer exists. So after the',
    '   revert, re-export it rather than trusting either version:',
    '     run_ghidra_script ' + REPO + '\\tools\\ghidra_snapshot\\ExportGhidraSnapshot.java',
    '     ToolSearch "select:mcp__ghidra__run_ghidra_script"',
    '   Whatever comes out is the truth about the database and goes into the commit in',
    '   step 6. Byte-identical output means nothing had been changed, and there will',
    '   simply be no diff. Only do this if ghidra_snapshot/ was actually dirty.',
    '',
    '4. In the copy of tools/code_emit/data/emit_state.json you read in step 3 -- the',
    '   pre-revert one -- find every entry whose status is',
    '   "in_flight". That is the marker each function writes before its emitter starts,',
    '   and it is left uncommitted on purpose, so an entry still saying it is the one',
    '   line that survives a kill saying which function was flying. Normally there is',
    '   exactly one; zero and several are both possible and neither is an error.',
    '',
    '   For each one, write it back into the reverted file with status "interrupted",',
    '   keeping the in_flight timestamp in a field named in_flight_since if it was there,',
    '   and a one-line note saying the run was killed mid-flight and the work was',
    '   discarded. Report addr, name and timestamp.',
    '   "interrupted" is not a terminal status, so next_batch.py hands the address',
    '   straight back out on the worklist below -- which is the intent. Do not mark it',
    '   failed: nothing about the function was judged, it just never finished.',
    '',
    '   Write the file with python, json.dumps(indent=2, ensure_ascii=False) plus a',
    '   trailing newline, UTF-8. Touch no other entry and no other field.',
    '',
    '5. If src/ or tests/ was dirty but no entry said in_flight, discard it all the same',
    '   -- the boundary is the path, not who owns it -- and set residue_without_marker',
    '   true so the report says the wreckage had no name on it.',
    '',
    '6. Commit, if step 4 changed the state file or the re-export in step 3 changed the',
    '   snapshot:',
    '     git add tools/code_emit/data/emit_state.json ghidra_snapshot && git commit',
    '   subject:  emit: 前一輪中斷的殘留已清除，<n> 支重回工作清單',
    '   then a blank line, one line in Traditional Chinese naming the functions and what',
    '   was discarded, a blank line, and',
    '     Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>',
    '   If nothing was dirty and nothing was in flight, commit nothing.',
    '',
    '7. Confirm with  git status --porcelain  that the tree is clean now, and report it.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function worklistPrompt(limit) {
  return [
    '# Role: Worklist. Read what is left, report it, judge nothing.',
    '',
    'Run both of these in the foreground and read what they print:',
    '  cd ' + REPO + ' && python tools/code_emit/next_batch.py --stats',
    '  cd ' + REPO + ' && python tools/code_emit/next_batch.py --limit ' + limit
      + ' --label ' + LABEL,
    '',
    'The second one prints JSON. Return its "functions" array verbatim -- same',
    'order, same fields, nothing added, nothing dropped, nothing reordered. The',
    'order is callee-before-caller and it is load-bearing: a function emitted ahead',
    'of its callees gets tested against generated stubs.',
    '',
    'remaining_total is the count of everything not yet committed, from --stats',
    '(pending + failed + stale, not just the ones in this batch).',
    '',
    'If either command prints anything on stderr, put it in conflicts and return an',
    'empty functions array: a disagreement between routing.json and emit_state.json',
    'means the same function is about to be written into two different files, and',
    'guessing which one is right is not your job.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function splitPrompt(file, lines, pending) {
  return [ENV, '', RULES, '',
    '# Role: Split one oversized source file. No new emit, no behaviour change.',
    '',
    'src/' + file + ' is ' + lines + ' lines and ' + pending + ' more function(s) are still',
    'routed into it. The budget is ' + LINE_BUDGET + ' lines, and the reason is the model rather',
    'than the program: past that, every one-line change drags the whole file through',
    'somebody\'s context.',
    '',
    'rebuild_info/code_layout.md owns the rules and you follow them exactly:',
    '  - Split now, before the next function lands in it. Moving code that is already',
    '    committed pollutes the next function\'s review diff, and the whole point of',
    '    doing it here is that this run\'s diff is empty.',
    '  - Split along a cohesive sub-function, never alphabetically and never by',
    '    address. Each new file must be describable in one sentence; if you cannot',
    '    write that sentence, the cut is in the wrong place.',
    '  - New basenames are at most 8 characters and must not collide with any',
    '    existing one, case-insensitively.',
    '',
    'Do all of this:',
    '1. Edit the routing table at its source -- the decision table in',
    '   tools/code_emit/build_routing.py -- then regenerate:',
    '     python tools/code_emit/build_routing.py',
    '     python tools/code_emit/build_routing.py --check',
    '   Never hand-edit routing.json or routing.md; they are outputs.',
    '2. Move the already-emitted functions to the file routing now names, taking',
    '   their tests with them (tests/<stem>.c mirrors src/<stem>.c, and the runner',
    '   inside it must be renamed to run_<new stem>_tests or the generated entry',
    '   point will not find it). Move the code as it is: this is not a chance to',
    '   improve anything.',
    '3. Update the file table in rebuild_info/code_layout.md with the new files and',
    '   what each one is for.',
    '4. Update tools/code_emit/data/emit_state.json for every function whose target',
    '   changed, or next_batch.py will refuse to hand out a worklist at all.',
    '5. Run the gate and require it green:',
    '     python tools/build_gate/gate.py check --target emittest',
    '6. Commit everything in one commit:',
    '     split: src/' + file + ' 超出行數預算，依內聚子功能拆檔',
    '   a blank line, one line in Traditional Chinese on where the cut went and why,',
    '   a blank line, then',
    '     Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>',
    '',
    'If you look at it and conclude the file should not be split -- nothing is routed',
    'into it any more, or there is no honest cohesive cut -- say so in note, report',
    'split=false, change nothing and commit nothing. A bad cut is worse than a long',
    'file.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

// ------------------------------------------------------------ agent driver

let nullStreak = 0
let stopped = null
let stopDetail = ''

class Stop extends Error {
  constructor(kind, detail) {
    super(kind + ': ' + detail)
    this.kind = kind
    this.detail = detail
  }
}

// One retry, then the item is recorded as unfinished -- never as done. Two
// consecutive calls returning nothing is the upstream-failure signal: at
// concurrency one, "every agent in the round returned nothing" is exactly this,
// and retrying past it only burns the rest of the budget (ADR-0007 5.2).
async function runAgent(prompt, opts) {
  let out = null
  for (let attempt = 1; attempt <= 2; attempt++) {
    out = await agent(prompt, opts)
    if (out) {
      break
    }
    log('  ' + opts.label + ': no return (attempt ' + attempt + ')')
  }
  if (!out) {
    nullStreak++
    if (nullStreak >= 2) {
      throw new Stop('upstream_failure',
        'two consecutive agent calls returned nothing; the cause is outside this workflow')
    }
    return null
  }
  nullStreak = 0
  if (out.ghidra_unreachable === true) {
    throw new Stop('ghidra_disconnect', out.stop_detail || '(no detail given)')
  }
  if (out.toolchain_unreachable === true) {
    throw new Stop('toolchain_unreachable', out.stop_detail || '(no detail given)')
  }
  return out
}

// ------------------------------------------------------------------- batch

const results = []
// How many equivalence concerns this batch added to emit_issues.json. Counted,
// not collected: nothing in this run reads them back, and the closing sweep
// over the whole file (ticket 22.1) picks them up off disk when every function
// has landed. A number that climbs faster than the batch size is the signal
// that the convergence rule in RULES is not biting.
let concernsLogged = 0
const splits = []
// Set when the run finished tidily but early -- currently only a routing
// change under its feet. Unlike `stopped` it does not suppress the closing
// documents, because nothing about the run is unaccounted for.
let endedEarly = null
let batchStart = 0
try { batchStart = budget.spent() } catch (e) { batchStart = 0 }
const spentK = (from) => { try { return Math.round((budget.spent() - from) / 1000) } catch (e) { return -1 } }

// ---------------------------------------------------------------- recover
//
// Before anything else, and before the worklist is even asked for. A run that
// was killed outright -- a usage limit ends the session where it stands -- never
// got to run any of the tidy-up below, so its half-written files are sitting in
// src/ and tests/ right now. The first function of this run would find them
// inside its own review diff and its bookkeeper would commit them under its
// name. This is the only moment that is guaranteed to execute after a session
// has already died, so this is where the tidying goes.
//
// The boundary is by path and it is not negotiable: src/, tests/, and the
// in-flight marker in emit_state.json. Anything else dirty means this is not our
// wreckage, and the run stops rather than deleting somebody's unrelated work.
phase('Recover')
const rec = await agent(recoverPrompt(),
  { label: 'recover', phase: 'Recover', schema: RECOVER_SUMMARY })
if (!rec) {
  log('the recovery stage returned nothing; refusing to emit into a tree nobody has looked at')
  return { error: 'recovery returned nothing', batch: LABEL }
}
if (rec.out_of_bounds) {
  const paths = (rec.out_of_bounds_paths || []).join(', ')
  log('STOP: uncommitted changes outside src/, tests/ and emit_state.json: ' + paths)
  return { error: 'working tree dirty outside this pipeline', batch: LABEL,
    out_of_bounds_paths: rec.out_of_bounds_paths || [], detail: rec.note || '' }
}
if (!rec.clean_now) {
  log('STOP: the tree is still not clean after recovery: ' + (rec.note || '(no detail)'))
  return { error: 'recovery did not leave a clean tree', batch: LABEL, detail: rec.note || '' }
}
const recovered = rec.interrupted || []
if (recovered.length) {
  log('recovery: ' + recovered.length + ' function(s) were killed mid-flight last run and '
    + 'go back on the worklist: '
    + recovered.map((r) => r.addr + ' ' + (r.name || '')).join(', '))
}
if (rec.residue_without_marker) {
  log('recovery: src/ or tests/ was dirty with no in_flight entry naming an owner; '
    + 'discarded anyway -- ' + ((rec.discarded_paths || []).join(', ') || '(no list)'))
} else if (!rec.tree_was_dirty && !recovered.length) {
  log('recovery: nothing to clean up')
}

// The worklist is fetched rather than carried. This script cannot read the
// filesystem, so an agent runs next_batch.py and hands the answer back; that
// keeps a 514-function roster out of the orchestrator and makes every run
// resume from whatever is on disk rather than from what a previous run said.
// Recovery ran first on purpose: an address it just put back to `interrupted`
// has to be in the list this asks for, not in the one after it.
let fns = (A && A.functions) || []
let remainingTotal = null
if (!fns.length) {
  phase('Worklist')
  const wl = await agent(worklistPrompt(LIMIT),
    { label: 'worklist', phase: 'Worklist', schema: WORKLIST })
  if (!wl) {
    log('the worklist stage returned nothing; nothing was attempted')
    return { error: 'no worklist', batch: LABEL }
  }
  if (wl.conflicts) {
    log('next_batch.py refused: ' + wl.conflicts)
    return { error: 'worklist conflict', detail: wl.conflicts, batch: LABEL }
  }
  fns = wl.functions || []
  remainingTotal = wl.remaining_total
  log('worklist: ' + fns.length + ' function(s) this run, '
    + remainingTotal + ' still to go overall')
}

if (!fns.length) {
  log('nothing left to emit')
  return { batch: LABEL, attempted: 0, committed: 0, remaining_total: remainingTotal,
    recovered: recovered }
}

for (let i = 0; i < fns.length && !stopped && !endedEarly; i++) {
  const fn = fns[i]
  const tag = '[' + (i + 1) + '/' + fns.length + '] ' + fn.name + ' @ ' + fn.addr
  let fnStart = 0
  try { fnStart = budget.spent() } catch (e) { fnStart = 0 }

  // No budget guard here, deliberately (ticket 21.6). How many functions to
  // attempt is the caller's decision, made per batch; a workflow that stopped
  // after 12 of the 40 it was handed, on a criterion the caller never asked it
  // to apply, is answering its own question. Running out of budget shows up as
  // being killed, and the Recover stage above is what makes that survivable.
  log(tag + ' -- start')
  let rounds = 0
  let outcome = null
  let why = ''

  try {
    phase('Emit')
    const emitted = await runAgent(emitterPrompt(fn, 'emit', null),
      { label: 'emit:' + fn.addr, phase: 'Emit', schema: EMIT_SUMMARY })

    if (!emitted) {
      outcome = 'no_emitter_return'
      why = 'the emitter returned nothing twice'
    } else if (emitted.status === 'skip') {
      outcome = 'skip'
      why = emitted.note || 'the emitter judged this not to be emittable code'
      log(tag + ' -- SKIP: ' + why)
    } else if (!emitted.wrote_verdict) {
      outcome = 'no_verdict_file'
      why = 'the emitter reported no verdict file'
    } else {
      phase('Review')
      let verdict = await runAgent(reviewerPrompt(fn, 1),
        { label: 'review:' + fn.addr, phase: 'Review', schema: REVIEW_SUMMARY })

      // The reviewer looked at the file itself; the emitter only claimed it.
      if (verdict && verdict.emit_verdict_present === false) {
        outcome = 'no_verdict_file'
        why = 'the reviewer found no complete emit verdict on disk'
        verdict = null
      }

      // One loop, two ways in. A function leaves it only by being both approved
      // and green; a red gate goes back through the emitter AND through the
      // reviewer, not straight back to the gate. A failing gate usually means
      // the emitted C is wrong, which is exactly when a second reviewer pass is
      // worth having -- and the commit subject says the reviewer passed, so it
      // had better be the reviewer's current opinion.
      let gate = null
      while (verdict && rounds < MAX_ROUNDS) {
        let why_fix = null
        if (!verdict.approved) {
          why_fix = 'The reviewer blocked on ' + verdict.blocking_count + ' issue(s).'
        } else {
          phase('Gate')
          gate = await runAgent(gatePrompt(fn),
            { label: (rounds ? 'regate:' : 'gate:') + fn.addr + ':' + rounds,
              phase: 'Gate', schema: GATE_SUMMARY })
          if (!gate) {
            outcome = 'gate_lost'
            why = 'the gate stage stopped returning'
            break
          }
          if (gate.gate_pass) {
            break
          }
          why_fix = 'The build gate failed: ' + (gate.detail || '(no detail)')
            + ' Read workspace/build_gate/result.json for the full report.'
        }

        rounds++
        log(tag + ' -- fix round ' + rounds + ': ' + why_fix)
        phase('Emit')
        const fixed = await runAgent(emitterPrompt(fn, 'fix', why_fix),
          { label: 'fix:' + fn.addr + ':' + rounds, phase: 'Emit', schema: EMIT_SUMMARY })
        if (!fixed) {
          outcome = 'emit_lost'
          why = 'the emitter stopped returning during fix round ' + rounds
          verdict = null
          break
        }
        phase('Review')
        gate = null
        verdict = await runAgent(reviewerPrompt(fn, rounds + 1),
          { label: 'rereview:' + fn.addr + ':' + rounds, phase: 'Review', schema: REVIEW_SUMMARY })
        if (!verdict) {
          outcome = 'review_lost'
          why = 'the review stage stopped returning after fix round ' + rounds
        }
      }

      if (outcome) {
        // one of the loop's own exits already said what went wrong
      } else if (!verdict) {
        outcome = 'review_lost'
        why = 'the review stage stopped returning'
      } else if (!verdict.approved) {
        outcome = 'not_approved'
        why = 'still ' + verdict.blocking_count + ' blocking issue(s) after ' + rounds + ' fix round(s)'
      } else if (!gate || !gate.gate_pass) {
        outcome = 'gate_failed'
        why = (gate && gate.detail) || 'the gate stayed red after ' + rounds + ' fix round(s)'
      } else {
        phase('Bookkeep')
        const book = await runAgent(
          bookkeepPrompt(fn, (verdict.ghidra_fixes || 0) > 0, rounds),
          { label: 'commit:' + fn.addr, phase: 'Bookkeep', schema: BOOK_SUMMARY })
        if (!book || !book.committed) {
          outcome = 'commit_failed'
          why = (book && book.problems) || 'the bookkeeper did not report a commit'
        } else {
          outcome = 'committed'
          log(tag + ' -- COMMITTED after ' + rounds + ' fix round(s): ' + (book.commit_line || ''))
          results.push({
            addr: fn.addr, name: fn.name, status: 'committed', rounds: rounds,
            commit: book.commit_line, ghidra_applied: book.ghidra_applied || 0,
            issues: book.issues_logged || 0, out_tok_k: spentK(fnStart),
            // Carried on the success row too. This is where the bookkeeper
            // reports a change it had to commit out of band -- a gate fix, a
            // build script -- and that commit is invisible in this function's
            // own commit by design, so if the note is dropped here nothing
            // anywhere records that it happened.
            problems: book.problems || undefined,
          })
          if (book.problems) {
            log('  out of band: ' + book.problems)
          }
          // The bookkeeper's own count is the one that matters: it counts what
          // reached emit_issues.json, where the closing sweep will look, rather
          // than what the emitter and the reviewer each claim to have raised.
          if ((book.issues_logged || 0) > 0) {
            concernsLogged += book.issues_logged
            log('  ' + book.issues_logged + ' concern(s) recorded for the closing sweep')
          }

          // A target file that has outgrown the budget is split now, while the
          // tree is clean, rather than later: moving committed code into a new
          // file after the next function has started would put the move inside
          // that function's review diff. Splitting a file nothing else is
          // routed into buys nothing, so the count of what is still coming
          // decides it (rebuild_info/code_layout.md).
          const stillRouted = fns.slice(i + 1)
            .filter((f) => f.target === fn.target).length
          if ((book.target_lines || 0) > LINE_BUDGET && stillRouted > 0) {
            phase('Split')
            log(tag + ' -- src/' + fn.target + ' is ' + book.target_lines
              + ' lines with ' + stillRouted + ' function(s) still routed into it')
            const sp = await runAgent(
              splitPrompt(fn.target, book.target_lines, stillRouted),
              { label: 'split:' + fn.target, phase: 'Split', schema: SPLIT_SUMMARY })
            if (sp && sp.split && sp.committed) {
              splits.push({ file: fn.target, lines: book.target_lines,
                new_files: sp.new_files || [], note: sp.note })
              log('  split: ' + fn.target + ' -> '
                + (sp.new_files || []).join(', '))
              // Everything still queued for that file now belongs somewhere
              // else, and this run's copy of the worklist says otherwise. End
              // here rather than emit into a file routing no longer names; the
              // next run asks next_batch.py again and gets the new answer.
              // This is a clean finish, not a failure -- the closing documents
              // still get written, because everything they describe did happen.
              endedEarly = 'src/' + fn.target + ' was split; the worklist this run '
                + 'is holding predates the new routing'
            } else {
              splits.push({ file: fn.target, lines: book.target_lines,
                new_files: [], note: (sp && sp.note) || 'the split stage returned nothing' })
              log('  split NOT done for ' + fn.target + ': '
                + ((sp && sp.note) || 'no return'))
            }
          }
        }
      }
    }
  } catch (e) {
    if (e instanceof Stop) {
      stopped = e.kind
      stopDetail = e.detail
      outcome = outcome || 'interrupted'
      why = e.detail
      log(tag + ' -- STOP (' + e.kind + '): ' + e.detail)
    } else {
      // agent() also throws on a token or usage limit, which is the same
      // situation from this workflow's point of view: stop, keep what was
      // committed, and let the re-run pick the rest up from emit_state.json.
      stopped = 'interrupted'
      stopDetail = String((e && e.message) || e)
      outcome = 'interrupted'
      why = stopDetail
      log(tag + ' -- INTERRUPTED: ' + stopDetail)
    }
  }

  if (outcome !== 'committed') {
    log(tag + ' -- NOT LANDED (' + outcome + '): ' + why)
    results.push({
      addr: fn.addr, name: fn.name, status: outcome, rounds: rounds,
      reason: why, out_tok_k: spentK(fnStart),
    })
    // Clean the tree so the next function's diff is its own, and record the
    // outcome so the worklist stops handing this address out. A skip needs the
    // recording just as much as a failure does -- more, in fact, since a skip is
    // a conclusion rather than an accident. Only a dead machine excuses it.
    if (stopped !== 'upstream_failure') {
      let cleaned = null
      try {
        cleaned = await agent(abandonPrompt(fn, why, outcome === 'skip'),
          { label: 'abandon:' + fn.addr, phase: 'Bookkeep', schema: BOOK_SUMMARY })
      } catch (e) {
        log('  cleanup THREW: ' + String((e && e.message) || e))
      }
      // Both halves, not just the commit. A cleanup that recorded the state but
      // left the tree dirty is the failure this stage exists to prevent: the
      // next function's bookkeeper stages src/ and tests/ wholesale.
      if (cleaned && cleaned.committed && cleaned.tree_clean !== false) {
        log('  cleanup: tree cleaned, state recorded as ' + outcome)
      } else {
        // Never report this as done. The next function's bookkeeper stages
        // src/ and tests/ wholesale, so a half-emitted function left behind
        // gets committed as somebody else's work.
        const detail = (cleaned && cleaned.problems)
          || (cleaned && cleaned.tree_clean === false ? 'the cleanup left the tree dirty' : null)
          || 'the cleanup agent returned nothing'
        log('  cleanup FAILED: ' + detail
          + ' -- src/ and tests/ may still hold this function. Nothing is lost: the'
          + ' in_flight marker is still in emit_state.json, so the next run\'s Recover'
          + ' stage discards the residue and puts the address back on the worklist.'
          + ' This run stops here rather than emit the next function into that tree.')
        stopped = stopped || 'cleanup_failed'
        stopDetail = stopDetail || detail
        const row = results[results.length - 1]
        if (row) {
          row.cleanup_failed = detail
        }
      }
    }
  }

  if (stopped || endedEarly) {
    for (let j = i + 1; j < fns.length; j++) {
      results.push({ addr: fns[j].addr, name: fns[j].name,
        status: stopped ? 'not_started_after_stop' : 'not_started_routing_changed' })
    }
  }
}

// ------------------------------------------------------------------ report

const landed = results.filter((r) => r.status === 'committed')
const unfinished = results.filter((r) => r.status !== 'committed' && r.status !== 'skip')
const skipped = results.filter((r) => r.status === 'skip')
// Two kinds of "did not land" that read the same in a count and mean opposite
// things to whoever calls the next batch. Never reached: the run stopped before
// its turn, nothing was attempted, nothing to clean up. Killed in flight: work
// was in the tree when the run ended, and the next run's Recover stage is what
// deals with it. Reporting them as one number is how the second kind gets
// mistaken for the first and nobody looks at the tree.
const neverReached = results.filter((r) => r.status === 'not_started_after_stop'
  || r.status === 'not_started_routing_changed')
const cutShort = results.filter((r) => r.status === 'interrupted')

const stats = {
  batch: LABEL,
  attempted: fns.length,
  committed: landed.length,
  skipped: skipped.length,
  unfinished: unfinished.length,
  never_reached: neverReached.length,
  cut_short: cutShort.map((r) => ({ addr: r.addr, name: r.name, reason: r.reason })),
  // What the Recover stage found in the tree when this run started -- the
  // function the PREVIOUS run was killed in the middle of, if there was one.
  recovered: recovered,
  stopped: stopped,
  stop_detail: stopDetail,
  ended_early: endedEarly,
  splits: splits,
  // What next_batch.py said was left when this run started. The run committed
  // `landed.length` of it, so the next run has roughly that much less to go --
  // roughly, because a function can be retired and come back.
  remaining_at_start: remainingTotal,
  // Concerns this batch added to emit_issues.json. They are not settled here --
  // the closing sweep over the whole file does that once every function has
  // landed (ticket 22.1).
  concerns_logged: concernsLogged,
  results: results,
  out_tok_k: spentK(batchStart),
}

log('batch ' + (stopped ? 'STOPPED (' + stopped + ')'
  : endedEarly ? 'ended early (' + endedEarly + ')' : 'complete') + ': '
  + landed.length + ' committed, ' + skipped.length + ' skipped, '
  + unfinished.length + ' unfinished of ' + fns.length)
for (const r of unfinished) {
  const kind = r.status === 'interrupted' ? 'CUT SHORT MID-FLIGHT'
    : (r.status === 'not_started_after_stop' || r.status === 'not_started_routing_changed')
      ? 'never reached' : 'UNFINISHED'
  log('  ' + kind + ' ' + r.addr + ' ' + r.name + ': ' + r.status
    + (r.reason ? ' -- ' + r.reason : ''))
}
if (cutShort.length) {
  log('the tree may hold work for ' + cutShort.map((r) => r.addr).join(', ')
    + '; the next run\'s Recover stage discards it and puts them back on the worklist')
}

// A run that stopped does not get closing documents. A knowledge-base page
// written from half a batch is indistinguishable from one written from a whole
// one (ADR-0007 5.6); the report below is produced either way, and says why.
let documents = []
if (!stopped) {
  phase('Report')
  const docs = await agent(
    `Write the devlog entry for this emit run and archive its report.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly. It is narrative,
rambling is allowed, and the point is the dead ends -- the paths that worked end
up in the code and the knowledge base, the ones that did not are the only thing
that would otherwise be lost.

Write ${REPO}\\devlog\\<today>-emit-${LABEL}.md in TRADITIONAL CHINESE, where
<today> is today's date as YYYY-MM-DD -- read it off the machine, do not assume
it. Append a clearly headed new section if a file of that name is already there
-- one work session, one entry.

What this run was: a batch of ticket 22, the emit of the game's own functions.
Each one goes emitter (three sources, writes C and a test) -> independent
reviewer (works from the assembly and from the working-tree diff) -> fix loop ->
build gate over src/ and tests/ -> its own commit. Serial, because the
reviewer's view of "this round's changes" is the working tree itself. The order
is callee-before-caller so a function is tested against real callees rather than
against generated stubs, and the build links twice so that data ticket 23 has
not emitted yet can be filled in zero-filled without hiding anything.

Sources you may read for detail:
  ${VERDICTS}\\      the per-function emit and review verdicts
  ${REPO}\\tools\\code_emit\\data\\emit_issues.json

Also write the run report to ${REPO}\\devlog\\runs\\<today>-emit-${LABEL}.json,
same date, verbatim from the statistics below, UTF-8.

Then COMMIT BOTH FILES, naming them explicitly:

  git add devlog && git commit -- devlog

subject  devlog: emit ${LABEL} 一批的記錄  , a blank line, one line in Traditional
Chinese on what the batch did, a blank line, and the
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com> trailer.

This is not tidiness. The next run of this workflow starts by looking at the
working tree to work out what a killed session left behind, and it can only tell
wreckage from work by the path -- devlog/ is outside the paths it owns, so an
uncommitted devlog stops the next batch dead and calls for a human. A batch that
ends by leaving the tree dirty has broken the thing this pipeline is for. Check
with  git status --porcelain  that it prints nothing, and say so in your summary.

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly what took fix rounds and why, anything the gate caught, any file
that had to be split, and every function that did not land. If the statistics'
recovered list is not empty, the previous run was killed mid-flight and this one
threw its wreckage
away before starting -- say which function that was and that its work was
discarded, because that is the one thing a reader cannot reconstruct from the
commits. Do not claim anything
the statistics do not support. With dozens of functions in a run a per-function
recital is noise: what is worth writing down is what went wrong, what it cost,
and anything a later batch would waste a day rediscovering.

Return the list of files you wrote.`,
    { label: 'doc:devlog', phase: 'Report', schema: DONE })
  if (docs) {
    documents = docs.written || []
    log('devlog: ' + docs.summary)
  }
} else {
  log('stopped, so no closing documents were written; the report below is the record')
}

return Object.assign({ documents: documents }, stats)
