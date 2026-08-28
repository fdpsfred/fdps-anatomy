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
//   The batch ends with a rescan of every equivalence concern recorded along
//   the way, now that the neighbouring functions have verdict files of their
//   own.
//
// Resuming is the normal case, not the exception: progress lives in
// tools/code_emit/data/emit_state.json and every approved function is its own
// commit, so a run that stops anywhere loses at most the function in flight.
//
// args: {
//   limit:       number   how many functions to ask next_batch.py for (default 40)
//   functions:   [{addr, name, target, body_size, stubbed_callees}]  optional,
//                         skips the worklist stage when supplied
//   batchLabel:  string
//   maxRounds:   number   fix rounds per function before giving up (default 4)
//   minBudgetPerFn: number  do not start a function without this much left
// }

export const meta = {
  name: 'fdps-emit-ticket22',
  description: 'Emit the FDPS game code function by function: three-source emit, independent review, build gate, per-function commit',
  phases: [
    { title: 'Worklist', detail: 'ask next_batch.py what is left, in callee-first order' },
    { title: 'Emit', detail: 'one emitter agent per function, three sources, writes C and tests' },
    { title: 'Review', detail: 'an independent agent verifies the diff against the assembly' },
    { title: 'Gate', detail: 'the build gate over src/ + tests/' },
    { title: 'Bookkeep', detail: 'apply Ghidra corrections, record state, commit' },
    { title: 'Split', detail: 'split a target file that has outgrown its line budget' },
    { title: 'Rescan', detail: 're-read the equivalence concerns recorded along the way' },
    { title: 'Report', detail: 'devlog and run archive' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\code_emit'
const VERDICTS = WS + '\\verdicts'

const A = (typeof args === 'string' && args.length) ? JSON.parse(args) : (args || {})
const MAX_ROUNDS = (A && A.maxRounds) || 4
// Measured cost of the first function through the pipeline was 34k output
// tokens; this is that with room for a function that needs several fix rounds.
// It is a floor for starting one more, not an estimate of what one costs.
const MIN_BUDGET_PER_FN = (A && A.minBudgetPerFn) || 120000
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
    open_issues: { type: 'integer', description: 'Equivalence concerns you recorded for the rescan' },
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

const RESCAN_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'changed', 'still_open'],
  properties: withStops({
    addr: { type: 'string' },
    changed: { type: 'boolean' },
    resolved: { type: 'integer' },
    still_open: { type: 'integer' },
    note: { type: 'string' },
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
    'yet emitted. Recording one is not a failure; the batch re-reads them all at the end.',
    'Leaving one unrecorded is how it gets lost.',
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
    '   review. It stages nothing; the bookkeeper stages for real later.',
    '   One function is in flight at a time and every approved function is its own',
    '   commit, so everything after HEAD belongs to this function. Review all of it,',
    '   including any knowledge-base or comment changes.',
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
    '',
    '# What to block on',
    '',
    'Block on anything that breaks equivalence, breaks the build, or breaks one of the',
    'rules above. Do not block on a different but equivalent way of writing something,',
    'or on style. When you are unsure, say so in the verdict and ask the emitter for',
    'evidence rather than blocking outright.',
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
    '3. Progress. In tools/code_emit/data/emit_state.json set the entry for key',
    '   "' + fn.addr + '": status "committed", emitted_against "' + (fn.body_size || '') + '",',
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
    '   not "0x...") . Create the file as {} if it does not exist. Read it back after',
    '   writing and check the encoding survived -- this is a Traditional Chinese Windows',
    '   machine and an unspecified encoding produces mojibake.',
    '',
    '5. Commit. Stage the source, the tests, the state files, and the Ghidra snapshot if',
    '   you re-exported it:',
    '     git add src tests tools/code_emit/data ghidra_snapshot',
    '   Check with  git --no-pager diff --staged --stat  that the range is this function',
    '   and nothing else, and with  git status --porcelain  that no tracked file was left',
    '   behind. Then commit with this subject:',
    '',
    '     emit: ' + fn.name + ' @ ' + fn.addr + '，reviewer 通過、build gate 通過',
    '',
    '   a blank line, one line in Traditional Chinese on what the function does, a blank',
    '   line, then:',
    '     Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>',
    '',
    '6. Report the commit as commit_line, built as  <hash> ' + fn.name + ' @ ' + fn.addr,
    '   where <hash> comes from  git --no-pager log --format=%h -1  . Do NOT pipe the',
    '   commit subject itself back: it is Traditional Chinese, this is a Traditional',
    '   Chinese Windows machine, and reading it through a console pipe turns it into',
    '   mojibake that then gets archived as the permanent record of this run.',
    '   Also confirm the tree is clean:  git status --porcelain  must print nothing.',
    '',
    '7. Report how long the target file has become:',
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
    '2. Discard the working-tree changes under src/ and tests/ only:',
    '     git checkout -- src tests',
    '   and delete any file that is new and untracked under those two directories.',
    '   Do NOT touch workspace/ -- the verdict files are the record of what went wrong',
    '   and the next run reads them.',
    '3. In tools/code_emit/data/emit_state.json set the entry for "' + fn.addr + '" to',
    '   status "' + (terminal ? 'skip' : 'failed') + '" and put the reason in note. Write it with python, UTF-8,',
    '   json.dumps(indent=2, ensure_ascii=False) plus a trailing newline.',
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

function rescanPrompt(fn) {
  return [ENV, '', RULES, '',
    '# Role: Rescan. One function, second look.',
    '',
    'Function ' + fn.addr + ' ' + fn.name + ' was emitted with equivalence concerns that could',
    'not be settled at the time. They are in',
    '  ' + VERDICTS + '\\' + fn.addr + '.emit.json      (open_issues)',
    '  ' + VERDICTS + '\\' + fn.addr + '.review.json    (open_issues)',
    'and collected in ' + REPO + '\\tools\\code_emit\\data\\emit_issues.json.',
    '',
    'The reason a second look can succeed where the first could not is that the',
    'functions around this one have verdict files now. A concern that is really about a',
    'neighbour\'s contract -- what a callee returns, what a shared table holds -- is',
    'unanswerable alone and obvious once the neighbour has been read.',
    '',
    'Read this function\'s own verdicts, then the verdict files of its callers and',
    'callees in ' + VERDICTS + '\\, and see whether they answer the question. Reading a',
    'neighbour\'s verdict is using a judgement someone else already made; it is not',
    'making one. Never rewrite another function\'s verdict file and never form an opinion',
    'about whether a neighbour\'s name is right.',
    '',
    'If a concern is settled: update emit_issues.json for key "' + fn.addr + '" to record',
    'the answer and what settled it. If the answer means the emitted C is wrong, do NOT',
    'edit the code -- record it as a blocking finding in the same entry and say so in',
    'your note, so it comes back as a real emit round.',
    '',
    'If a concern is not settled, leave it exactly as it is and say what is still',
    'missing. An honest unresolved concern is a correct outcome. A second attempt is not',
    'a reason to manufacture a conclusion.',
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
const withIssues = []
const splits = []
// Set when the run finished tidily but early -- currently only a routing
// change under its feet. Unlike `stopped` it does not suppress the closing
// documents, because nothing about the run is unaccounted for.
let endedEarly = null
let batchStart = 0
try { batchStart = budget.spent() } catch (e) { batchStart = 0 }
const spentK = (from) => { try { return Math.round((budget.spent() - from) / 1000) } catch (e) { return -1 } }

// The worklist is fetched rather than carried. This script cannot read the
// filesystem, so an agent runs next_batch.py and hands the answer back; that
// keeps a 514-function roster out of the orchestrator and makes every run
// resume from whatever is on disk rather than from what a previous run said.
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
  return { batch: LABEL, attempted: 0, committed: 0, remaining_total: remainingTotal }
}

for (let i = 0; i < fns.length && !stopped && !endedEarly; i++) {
  const fn = fns[i]
  const tag = '[' + (i + 1) + '/' + fns.length + '] ' + fn.name + ' @ ' + fn.addr
  let fnStart = 0
  try { fnStart = budget.spent() } catch (e) { fnStart = 0 }

  // Stop on a function boundary rather than half way through one: an
  // interrupted emit leaves a partial file in the tree for the next run to
  // clean up, and there is nothing to gain by starting one we cannot finish.
  let left = null
  try {
    left = budget.total ? budget.remaining() : null
  } catch (e) {
    left = null
    if (i === 0) {
      log('no budget figures available; the per-function budget guard is off this run')
    }
  }
  if (left !== null && left < MIN_BUDGET_PER_FN) {
    log('budget low: ' + Math.round(left / 1000) + 'k left, stopping before ' + tag)
    for (let j = i; j < fns.length; j++) {
      results.push({ addr: fns[j].addr, name: fns[j].name, status: 'not_started_budget' })
    }
    stopped = 'budget'
    break
  }

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
          })
          if ((book.issues_logged || 0) > 0
              || (verdict.open_issues || 0) > 0
              || (emitted.open_issues || 0) > 0) {
            withIssues.push(fn)
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
      if (cleaned && cleaned.committed) {
        log('  cleanup: tree cleaned, state recorded as ' + outcome)
      } else {
        // Never report this as done. The next function's bookkeeper stages
        // src/ and tests/ wholesale, so a half-emitted function left behind
        // gets committed as somebody else's work.
        const detail = (cleaned && cleaned.problems) || 'the cleanup agent returned nothing'
        log('  cleanup FAILED: ' + detail
          + ' -- src/ and tests/ may still hold this function, and the worklist will'
          + ' hand it out again; check git status before the next run')
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

// ------------------------------------------------------------------ rescan
//
// Everything recorded as an unsettled equivalence concern gets one more look,
// now that the functions around it have verdict files of their own.

const rescanLog = []
if (!stopped && withIssues.length > 0) {
  phase('Rescan')
  log('rescan: ' + withIssues.length + ' function(s) with recorded concerns')
  for (const fn of withIssues) {
    try {
      const r = await runAgent(rescanPrompt(fn),
        { label: 'rescan:' + fn.addr, phase: 'Rescan', schema: RESCAN_SUMMARY })
      if (r) {
        rescanLog.push({
          addr: fn.addr, name: fn.name, changed: r.changed,
          resolved: r.resolved || 0, still_open: r.still_open, note: r.note,
        })
        log('  ' + fn.addr + ': resolved ' + (r.resolved || 0)
          + ', still open ' + r.still_open + (r.note ? ' -- ' + r.note : ''))
      }
    } catch (e) {
      stopped = (e instanceof Stop) ? e.kind : 'interrupted'
      stopDetail = (e instanceof Stop) ? e.detail : String((e && e.message) || e)
      log('rescan stopped: ' + stopDetail)
      break
    }
  }
} else if (!stopped) {
  log('rescan: nothing recorded as unsettled')
}

// ------------------------------------------------------------------ report

const landed = results.filter((r) => r.status === 'committed')
const unfinished = results.filter((r) => r.status !== 'committed' && r.status !== 'skip')
const skipped = results.filter((r) => r.status === 'skip')

const stats = {
  batch: LABEL,
  attempted: fns.length,
  committed: landed.length,
  skipped: skipped.length,
  unfinished: unfinished.length,
  stopped: stopped,
  stop_detail: stopDetail,
  ended_early: endedEarly,
  splits: splits,
  // What next_batch.py said was left when this run started. The run committed
  // `landed.length` of it, so the next run has roughly that much less to go --
  // roughly, because a function can be retired and come back.
  remaining_at_start: remainingTotal,
  rescan: rescanLog,
  results: results,
  out_tok_k: spentK(batchStart),
}

log('batch ' + (stopped ? 'STOPPED (' + stopped + ')'
  : endedEarly ? 'ended early (' + endedEarly + ')' : 'complete') + ': '
  + landed.length + ' committed, ' + skipped.length + ' skipped, '
  + unfinished.length + ' unfinished of ' + fns.length)
for (const r of unfinished) {
  log('  UNFINISHED ' + r.addr + ' ' + r.name + ': ' + r.status + ' -- ' + (r.reason || ''))
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

Write ${REPO}\\devlog\\2026-08-28-emit-${LABEL}.md in TRADITIONAL
CHINESE, or append a clearly headed new section if a file of that name is
already there -- one work session, one entry.

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

Also write the run report to ${REPO}\\devlog\\runs\\2026-08-28-emit-${LABEL}.json,
verbatim from the statistics below, UTF-8.

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly what took fix rounds and why, anything the gate caught, any file
that had to be split, and every function that did not land. Do not claim anything
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
