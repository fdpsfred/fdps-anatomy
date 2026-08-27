export const meta = {
  name: 'global-data-ticket17',
  description: 'Name and type every global data symbol of FDPS.LE, then lay out the structs they hold',
  whenToUse: 'Ticket 17. Turn offset arithmetic in the decompiled C into field accesses.',
  phases: [
    { title: 'Plan', detail: 'refresh the dump and work out what is still unjudged' },
    { title: 'Judge', detail: 'one agent reads one anchor and writes one verdict file' },
    { title: 'Apply', detail: 'transcribe the round into Ghidra, re-dump, run both gates' },
    { title: 'Arbitrate', detail: 'two verdicts claiming one symbol, or one boundary' },
    { title: 'Rescan', detail: 're-read the verdicts that stayed open, neighbours now settled' },
    { title: 'Struct', detail: 'one agent lays out one struct from every site that touches it' },
    { title: 'StructApply', detail: 'transcribe the layouts and run the gates' },
    { title: 'Report', detail: 'completed, failed, and what is still unfinished' },
  ],
}

// Ticket 17 -- the global data half of making FDPS.LE readable.
//
// Ticket 15 named all 514 game functions, so the decompilation now says what
// each routine is for. It still says it in offsets: DAT_00069cd8 + i * 0x50 +
// 0x40 rather than units[i].hp_current. This workflow closes that gap in two
// stages, because the two halves of the job have genuinely different shapes.
//
// Stage one is per-item and follows ADR-0007 exactly: 1,167 anchors, one agent
// each, verdict to a file, transcription in its own phase, both gates every
// round, a rescan at the end.
//
// Stage two is not per-item, and the ticket says so in its own words. What a
// struct field means comes from every site that touches it, so cutting it into
// one-agent-per-field would produce a set of mutually contradictory answers
// about one layout. The unit of work there is the struct: still one item per
// agent, just a coarser item. It runs after the anchors are settled, because
// the struct list is discovered from their verdicts rather than known upfront.
//
// The five rules this is built to (ADR-0007):
//
//  1. The verdict goes in a file; the agent returns about 200 bytes. Nothing in
//     this script or any later phase grows with the number of anchors handled.
//  2. Judging agents never write to Ghidra. Transcription is its own phase and
//     makes no judgements.
//  3. Every round runs both gates -- the structural baseline audit and this
//     ticket's global audit -- and a round that breaks one fixes it before the
//     next starts.
//  4. A rescan phase re-reads whatever stayed open, this time allowed to read
//     the neighbours' verdict files.
//  5. Errors are handled, never swallowed: a lost verdict is retried once then
//     recorded, a round where every agent died stops the run, an apply that
//     cannot land something reports it, and the closing report lists what was
//     left undone.
//
// Three things about this ticket's shape that are not in the earlier ones:
//
//  * An anchor is not the same thing as a variable. The dump reports where
//    references land; whether four consecutive referenced dwords are four ints
//    or one struct is the judgement being asked for. So a verdict may say "this
//    is the interior of the object that starts at X", and the transcription
//    step leaves those alone for their owner's type to cover.
//  * Because of that, two verdicts can disagree about a boundary as well as
//    about a name. Both come back as reports from the transcription step and
//    both get arbitrated, and build_worklist.py puts an anchor that lost a
//    boundary argument back on the list so it can be re-judged as an interior.
//  * The predecessor project already solved the layouts this game inherits.
//    FD2's runtime_char is 0x50 bytes and so is this game's unit record. The
//    vocabulary page carries FD2's types.h verbatim for exactly that reason,
//    with the warning that a matching size is not a matching layout.
//
// Run it:
//   python tools/global_data/build_worklist.py
//   Workflow({ scriptPath: 'tools/global_data/globals_ticket17.js',
//              args: { roundSize: 16, maxItems: 120 } })
//
// It is meant to be run several times. Anything with a current verdict file is
// skipped, so each call picks up where the last one stopped -- which matters,
// because 1,167 anchors do not fit in one session. Pass { structs: true } to
// run the struct stage on its own once the anchors are through.

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOLS = `${REPO}\\tools\\global_data`
const WORK = `${REPO}\\workspace\\global_data`
const VERDICTS = `${WORK}\\verdicts`
const STRUCTS = `${WORK}\\structs`
const DUMP = `${WORK}\\dump`
const LOGIC_DUMP = `${REPO}\\workspace\\logic_naming\\dump`
const BASELINE_AUDIT = `${REPO}\\tools\\ghidra_baseline\\AuditGhidraBaseline.java`
const FD2 = 'C:\\Users\\fdpsf\\Documents\\fd2-anatomy'

const cfg = args || {}
const ROUND_SIZE = cfg.roundSize || 16
const MAX_ITEMS = cfg.maxItems || 120
const MAX_RESCAN_PASSES = cfg.maxRescanPasses === undefined ? 2 : cfg.maxRescanPasses
const SKIP_RESCAN = !!cfg.skipRescan
const STRUCTS_ONLY = !!cfg.structs
const MAX_STRUCTS = cfg.maxStructs || 12

// ---------------------------------------------------------------- schemas

// About 200 bytes. Everything else the agent decided is in the verdict file.
const VERDICT_SUMMARY = {
  type: 'object',
  required: ['addr', 'classification', 'confidence', 'wrote_file'],
  properties: {
    addr: { type: 'string' },
    name: { type: 'string', description: 'the symbol name the verdict settled on, empty for an interior' },
    classification: {
      type: 'string',
      enum: ['variable', 'array', 'pointer', 'interior', 'padding', 'unknown'],
    },
    pool: { type: 'string' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean', description: 'the verdict file exists on disk' },
    has_open_question: { type: 'boolean' },
    needs_rescan: {
      type: 'boolean',
      description: 'this verdict could change once the neighbouring anchors are judged',
    },
    struct_candidate: { type: 'string', description: 'the struct name proposed, empty if none' },
    has_pitfall: {
      type: 'boolean',
      description: 'the verdict recorded something a rebuild would get wrong by writing the obvious C',
    },
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
    retyped: { type: 'integer' },
    violations: { type: 'integer', description: 'AuditGlobals violations, target 0' },
    pending: { type: 'integer', description: 'anchors still wearing a default or loader label' },
    orphan_ranges: { type: 'integer' },
    error_bookmarks: { type: 'integer' },
    name_conflicts: {
      type: 'array',
      items: { type: 'string', description: '<addr> wanted <name>, held by <addr>' },
    },
    overlaps: {
      type: 'array',
      items: { type: 'string', description: 'an OVERLAP line, verbatim' },
    },
    segment_issues: { type: 'array', items: { type: 'string' } },
    problems: { type: 'array', items: { type: 'string' } },
    edata_consistent: { type: 'boolean' },
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
    retired: { type: 'integer' },
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
  required: ['addr', 'wrote_file'],
  properties: {
    addr: { type: 'string' },
    name: { type: 'string' },
    wrote_file: { type: 'boolean' },
    reason: { type: 'string' },
  },
  additionalProperties: false,
}

const STRUCT_LIST = {
  type: 'object',
  required: ['todo'],
  properties: {
    todo: { type: 'array', items: { type: 'string' } },
    settled: { type: 'integer' },
    note: { type: 'string' },
  },
  additionalProperties: false,
}

const STRUCT_SUMMARY = {
  type: 'object',
  required: ['handed_name', 'name', 'wrote_file', 'confidence'],
  properties: {
    handed_name: {
      type: 'string',
      description: 'the candidate name this agent was given, echoed back verbatim. '
        + 'It is how the workflow knows which item was worked on when the layout '
        + 'ended up under a different name.',
    },
    name: { type: 'string', description: 'the name the layout was actually written under' },
    size: { type: 'integer' },
    fields: { type: 'integer' },
    exists_in_image: {
      type: 'boolean',
      description: 'false when the answer is that this record has no layout in the executable',
    },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean' },
    is_game_data: {
      type: 'boolean',
      description: 'the record holds game content -- stats, prices, ranges, class ids -- '
        + 'so the strategy guide can confirm a field. False for CRT and sound-library '
        + 'structures, which the guide says nothing about.',
    },
    guide_checked: { type: 'boolean' },
    has_open_question: { type: 'boolean' },
    has_pitfall: { type: 'boolean' },
    ghidra_responding: { type: 'boolean' },
  },
  additionalProperties: false,
}

const STRUCT_APPLY = {
  type: 'object',
  required: ['ok', 'structs_applied', 'violations', 'ghidra_responding'],
  properties: {
    ok: { type: 'boolean' },
    structs_applied: { type: 'integer' },
    fields_placed: { type: 'integer' },
    globals_typed: { type: 'integer' },
    variables_typed: { type: 'integer' },
    violations: { type: 'integer' },
    orphan_ranges: { type: 'integer' },
    error_bookmarks: { type: 'integer' },
    problems: {
      type: 'array',
      items: { type: 'string', description: 'the layout is wrong about the program' },
    },
    target_problems: {
      type: 'array',
      items: { type: 'string', description: 'a TARGET line: one call site could not take the type' },
    },
    deferred: {
      type: 'array',
      items: { type: 'string', description: 'a DEFERRED line: a field waiting on a struct not yet laid out' },
    },
    ghidra_responding: { type: 'boolean' },
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

// Shared preamble. Every judging agent gets the same picture of the job so that
// a thousand of them do not each invent their own dialect.
const BRIEF = `You are working on FDPS.LE, the 32-bit DOS/4G executable of the
Traditional Chinese game 炎龍騎士團外傳 (Flame Dragon Plus), built with Watcom
C/C++ 10.0a. Ticket 14.2 settled which pool every function belongs to; ticket 15
named all 514 game-logic functions and wrote a plate comment on each. Your
ticket is the next one: the code says what it does, but it says it in offsets.
Give the data it touches names and types so that it says it in fields.

Read these before you decide anything (they are short and binding):
  ${REPO}\\rebuild_info\\naming.md          the naming canon
  ${REPO}\\CONTEXT.md                       project vocabulary
  ${WORK}\\vocabulary.md                    the canon applied, what this run has
                                            already named, and the names and
                                            solved layouts the predecessor
                                            project FD2 settled on

Rules that come out of them and are not negotiable:
  * Game globals are data_fdps_ + snake_case. Globals reached only by the sound
    library are data_ail_ or the library's own AIL_ name. CRT globals carry the
    library's own symbol where you can identify it, L$N_<module>_<use> where it
    is a file-static you cannot, and no other prefix. Compiler and linker output
    is binary_artifact_ + description + _<address>.
  * No address may appear in a name, and binary_artifact_ is the only exception.
    "data_fdps_word_69cd8" is not a name, it is a refusal to decide.
  * The Ghidra name and the future C name are the same string, so it has to be a
    legal C identifier and it has to still make sense in a .c file.
  * Reach for a word already in the vocabulary page before inventing one, and
    prefer FD2's word where FDPS has the same concept.

Domain facts you will need. The game is a tactical RPG: units stand on a tile
map, take turns, move along traced paths and attack. The 0x50-byte unit records
live in an array whose base pointer is at 00069cd8. Chapters are numbered 1..30
to the player and 0..29 inside the program. Resources live in a VFS container;
CEL is the sprite format, SAF the animation format. The memory map is settled
and you should not contradict it without saying so: 00060000-0006392f is
initialised data, 00063930-0006a3bb is _BSS which the CRT zeroes at startup,
0006a3c0 upward is the stack, command-line copy and near heap, and 00070000 is a
separate object holding the low-level input state. Read-only tables also sit
among the code in .object1.

When a name would turn on a game fact you cannot get from the code -- what an
item does, what a class is called -- the strategy guide mirror is searchable:
  python ${REPO}\\tools\\guide_scrape\\... search "<term>"      (see its _index.md)
and the fdps-data skill answers questions about items, spells, classes and
characters.`

const EVIDENCE = `Your evidence is on disk, in one file:

  ${DUMP}\\ev\\<addr>.json

What is in it, and how much each part is worth:

  xrefs            every instruction that reads or writes this address, with the
                   function it sits in, that function's pool, and four
                   instructions of context either side. THIS IS THE PRIMARY
                   EVIDENCE. What a global is for is what the code does with it,
                   and the addressing mode around the reference is usually the
                   whole answer: IMUL by 0x50 then an indexed load says array of
                   0x50-byte records; a MOV of a dword followed by dereferencing
                   it says pointer.
  bytes            the initial value, for anything in initialised memory. Absent
                   for _BSS, where the value is zero by definition -- the CRT
                   zeroes 00063930 to 0006a3bc at startup.
  decompiled       the lines of Ghidra's C that mention this symbol, taken from
                   functions ticket 15 already named and commented. Useful for
                   shape; never authoritative on types.
  segment          which part of the memory map this falls in.
  referencing_pools  which pools reach it. A global only pool_ail touches is the
                   sound library's, whatever it looks like.
  span_to_next_anchor  bytes to the next address anything references. A
                   MEASUREMENT, NOT A BOUNDARY. It says where reference density
                   stops. Deciding where the variable stops is your job.
  inside_defined_data  present when this address already sits inside a larger
                   object somebody defined. Strong evidence you are looking at
                   an interior.
  prev_anchor / next_anchor  the neighbours, so you can see whether you are at
                   the start of a run or in the middle of one.

You may also read ${LOGIC_DUMP}\\asm\\<addr>.txt and \\dec\\<addr>.c for any
referencing function if the context window in the evidence file is not enough.

Most anchors are settled by their own evidence file plus the vocabulary page,
and a good many are settled by the first two xrefs alone -- a scalar with two
references is not a research project. Reach into the function dumps when the
addressing genuinely does not tell you what the value is for, not as a matter of
routine. There are over a thousand of these and the ones that deserve an hour
are the tables and the pointers, not the flags.`

function judgePrompt(addr) {
  return `${BRIEF}

${EVIDENCE}

Your anchor this time, and the only one you may write a verdict for:

  ${addr}

Decide four things, each on its own evidence:

WHAT IT IS. Before anything else: is this address a variable in its own right,
or the interior of one that starts lower down? An anchor is where a reference
lands, and code references the middle of arrays and structs all the time. Look
at prev_anchor, at inside_defined_data, and above all at the addressing in the
xrefs: a reference of the form base + index * stride reaching this address means
the variable is at base, not here. If it is an interior, say so, name the owner,
and stop -- do not invent a name for it. The owner's type is what will give
these bytes a name, as a field or an array element. The same goes for alignment
padding between two objects.

If it is a variable, is it a scalar, an array, or a pointer? The commonest
mistake available here is calling a pointer an integer: a dword in _BSS that
every use dereferences is a pointer, and typing it as a pointer is most of what
makes the decompilation readable.

POOL. Which of fdps, crt, ail, binary_artifact does this belong to?
referencing_pools usually settles it outright. Where more than one pool reaches
it, decide who owns it and who is merely a visitor -- a CRT variable the game
pokes is still the CRT's, and it keeps the library's name.

NAME. What is the shortest name that says what this holds? Name it for what it
means to the code, not for its representation: data_fdps_battle_turn_counter,
not data_fdps_word_at_63a10. If it is a flag, say which condition it means. If
it is a table, say what the rows are. Check the vocabulary page for FD2's word
for the same thing first.

TYPE. What is it really? The transcription step understands a type name, any
number of pointer stars, and at most one array dimension. Nothing more
elaborate, and a spelling it cannot resolve is reported and NOT substituted, so
the anchor stays untyped. Stay inside this list:

  byte, word, dword, qword       the sizes, when the meaning is just bits
  char, uchar, short, ushort, int, uint, long, ulong    when you know the sense
  float, double
  char *, void *, byte *, <anything above> *            pointers
  <anything above>[N]                                   one dimension
  func_ptr, func_ptr[N]          a code pointer, and a table of them. Use this
                                 for a dispatch table -- a slot only ever
                                 reached by CALL or JMP through the table. Do
                                 NOT write "code *", Ghidra has no such name.
                                 The signature behind func_ptr is a placeholder
                                 (void (void)); what each entry really takes is
                                 an emit-stage question, and typing it as a code
                                 pointer is what makes Ghidra follow the slots
                                 at all.

If the honest answer is a struct you have not defined yet, say so in
struct_candidate and give the type as the pointer or byte array it is until
then. Getting the SIZE right matters more than getting the signedness right: a
size that is too big swallows the neighbour and comes back as an overlap
dispute.

WRITE THE VERDICT to ${VERDICTS}\\${addr}.json, exactly this shape:

{
  "addr": "${addr}",
  "covers": { "start": "${addr}", "size": <bytes this object occupies>, "region_sha": "<region_sha from the evidence file>" },
  "pool": "fdps|crt|ail|binary_artifact|unknown",
  "classification": "variable|array|pointer|interior|padding|unknown",
  "belongs_to": "<owner address, only when classification is interior>",
  "name": { "verdict": "<the name, empty for interior or padding>", "evidence": "<why this name and not another>", "confidence": "high|medium|low" },
  "type": { "verdict": "<the type, empty for interior or padding>", "evidence": "<the instructions that show it: the addressing mode, the operand size, what the value is used as>", "confidence": "high|medium|low" },
  "size": <bytes>,
  "element_count": <for arrays, how many elements; 0 otherwise>,
  "comment": { "text": "<the plate comment, see below>", "confidence": "high|medium|low" },
  "struct_candidate": { "name": "<a game record is fdps_ + snake_case, e.g. fdps_unit_record; a CRT or AIL record carries the LIBRARY'S OWN name with no prefix, e.g. tm, FILE, _iobuf -- there is no crt_ prefix in this project; no _t suffix; empty if none>", "stride": <bytes per record, 0 if none>, "evidence": "<what shows the stride>" },
  "segment_check": "",
  "needs_rescan": false,
  "open_question": "",
  "pitfall": ""
}

The plate comment is what a reader who has never seen this global needs:

    <one sentence saying what it holds>

    <what writes it and when, what reads it and why. Name the functions. Say
    what the values mean when you can tell -- which bit is which flag, what
    range an index runs over, what a sentinel value means.>

    Pool: <pool>  [<one line of why>]

    Rebuild note:
    <only when you have a pitfall -- same text as the field below>

Refer to functions by the name they carry right now, or by address if they are
still FUN_. Do not describe your own investigation; the comment says what the
data is.

Notes on the fields:
  * size and covers.size are the same number and both must be right. The
    transcription step uses the type's length to decide whether this object
    swallows the next anchor, and reports an overlap rather than guessing.
  * struct_candidate is how the struct stage gets its worklist. Fill it when you
    find a table of fixed-size records or a pointer to one, even if you cannot
    say what the fields are -- naming the record and its stride is enough to put
    it on the list. Leave it empty otherwise.
  * segment_check: non-empty only if what you found contradicts the memory map
    quoted above -- initialised-looking data above _edata, a variable straddling
    a segment boundary, something in the stack region that is not stack. Say
    what you saw. It gets reported, not acted on.
  * open_question: what you could not settle and what would settle it. Leave it
    empty only when it is empty. It is a record, not a request.
  * needs_rescan: true when judging the NEIGHBOURING ANCHORS could change THIS
    verdict -- you think you may be an interior but the candidate owner has no
    verdict yet, or the stride only makes sense if the next few anchors turn out
    to be fields. Set it false when the open question is real but no amount of
    neighbour-judging would answer it.
  * pitfall: normally empty. Fill it when you found something that would make a
    faithful C rewrite behave differently from the original -- a variable the
    CRT zeroes that the game reads before writing, a table whose last entry is a
    sentinel the loop depends on, an initial value that looks like padding and
    is not, a signedness the code depends on. One or two sentences saying what
    would go wrong and how someone would write it wrong. The threshold is
    "writing the obvious thing gives the wrong answer", not "this is intricate".

Confidence is about the evidence, not about how you feel. A type inferred from a
single operand size with no clue what the value means is medium at best.

Then return the summary object. Do not put the comment, the evidence or the type
rationale in what you return -- they are in the file. Set wrote_file only if you
actually wrote it, and ghidra_responding false only if you tried a Ghidra MCP
call and it failed (you should not need one; everything is in the dump).`
}

function rescanPrompt(addr) {
  return `${BRIEF}

${EVIDENCE}

Your anchor this time, and the only one you may write a verdict for:

  ${addr}

This is a re-read. The first pass left this verdict open -- low confidence, or
it said judging the neighbours could change it -- and since then the neighbours
have been judged. Two things changed for you:

  * ${DUMP} was re-exported after every round, so the symbol names, types and
    inside_defined_data in ev/${addr}.json are current. What was DAT_ last time
    may be a named field of something now.
  * You may read other anchors' verdict files in ${VERDICTS} as evidence. Start
    with prev_anchor and next_anchor, and with any address your first pass named
    as a possible owner. Quoting a judgement someone else already made is not
    making it for them.

The existing verdict is at ${VERDICTS}\\${addr}.json. Read the evidence again
first, then it, then decide whether the new evidence changes anything.

The commonest correct outcome of this pass is discovering you are an interior:
the neighbour below you now has a type that covers this address. If so, rewrite
the verdict as an interior with belongs_to set, and drop the name -- a stranded
label inside another object is a gate violation, not a harmless leftover.

A second attempt is not a licence to manufacture a conclusion. If it is still
not settled, say so: keep the verdict, keep the open question, and write down
what specifically is still missing and what would answer it. An honest low is
worth more than a confident invention, and this is the last pass.

If the evidence does change the verdict, rewrite the file completely in the same
shape it already has, and add one field:

  "_supersedes": "<what the first pass concluded and what changed it>"

Return the summary object either way: the name in it must be the name in the
file after you are done.`
}

function arbitrateNamePrompt(conflict) {
  return `${BRIEF}

Two verdicts claim the same symbol, and only one can have it. The transcription
step reported:

  ${conflict}

The address named second was left unchanged, so right now it still wears its
default label and its verdict file says something Ghidra refused.

Read both anchors' evidence and both verdict files:
  ${DUMP}\\ev\\<addr>.json for each
  ${VERDICTS}\\<addr>.json for each

Decide which global the name actually describes. Then fix the loser's verdict
file so it names what that global holds, in a way that does not collide -- a
different, accurate name, not the same name with a suffix. A suffix is what you
write when you have decided not to decide.

There is one case where the answer is neither: the two addresses are parts of
one object, and the loser should be an interior of the winner rather than a
variable with its own name. If that is what the evidence says, rewrite the
loser's verdict as an interior with belongs_to set.

Rewrite only the loser's verdict file, keeping its shape. Return which address
you changed and to what.`
}

function arbitrateOverlapPrompt(overlap) {
  return `${BRIEF}

Two verdicts disagree about where a variable ends. The transcription step
reported:

  ${overlap}

One agent judged an object big enough to cover an address that another agent
judged to be a variable of its own. Neither could see the other. The type was
NOT applied, so nothing is wrong in the database yet -- but nothing is right
either, and leaving it means both anchors stay unfinished.

Read both anchors' evidence and both verdict files:
  ${DUMP}\\ev\\<addr>.json for each
  ${VERDICTS}\\<addr>.json for each

The question is only this: is the covered address a field or element of the
covering object, or is the covering object smaller than its verdict claims?

Look at the addressing in the xrefs of both. A reference that computes
base + index * stride and lands on the second address settles it in favour of
the big object. A reference that loads the second address as an absolute
immediate, with no arithmetic from the first, is evidence the two are separate
and the big object's size is wrong.

Rewrite whichever verdict file is wrong, keeping its shape:
  * if the covered address is inside, rewrite ITS verdict as an interior with
    belongs_to set and no name;
  * if the covering object is too big, correct ITS size, type and
    element_count.

Add to whichever you changed:

  "_supersedes": "<what it said before and what changed it>"

Change one of them, not both. Return the address you changed.`
}

function structApplyPrompt(names) {
  return `Transcribe ticket 17 struct layouts into Ghidra, then check it.

You are not judging anything. Each layout was decided by an agent that read
every site touching that struct; your job is to put it in and report honestly.

  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program"

Run these in order, all through run_ghidra_script with the absolute path:

  1. ${TOOLS}\\ApplyStructDefs.java
     args: ${STRUCTS} all

     "all" and not just this round's ${names.length} layout(s), on purpose. Two
     layouts claiming the same bytes is only visible when both are in the same
     invocation, and the layouts that disagree are usually written rounds apart
     -- an earlier round's struct and this one's. Applying only the new names
     means the collision never surfaces during the run and is discovered later
     with one of the two already destroyed. Re-applying a layout that is already
     in the database costs nothing and changes nothing.

  2. ${BASELINE_AUDIT}
     args: ${REPO}\\workspace\\ghidra_baseline
     Orphan code ranges, error bookmarks and undefined bytes must all be 0.

  3. ${TOOLS}\\AuditGlobals.java
     args: ${WORK}
     violations must be 0. Applying a struct over a region that had named labels
     inside it can strand those labels, and the audit will say so -- that is a
     real breakage and it is yours to fix in this round, by clearing the stranded
     label whose bytes the struct now owns.

  4. save_program()

ApplyStructDefs separates three kinds of outcome and so must you:

  problems         the LAYOUT is wrong about the program -- a field that will
                   not fit, a struct that grew past its declared size, two
                   layouts claiming the same bytes, a type spelling nothing can
                   resolve. These are gate failures.
  DEFERRED lines   a field whose type is another struct that is on the candidate
                   list but has no layout yet. fdps_save_slot holds an array of
                   fdps_unit_record; whichever is written first names the other
                   before it exists. Not a failure and not done -- it lands by
                   itself once the other layout is written, because every round
                   re-applies the whole set.
  TARGET lines     a place the layout said to apply the type could not take it:
                   a variable that is not there under that name, a storage
                   conflict the script could not clear. The struct itself is in
                   the database and correct; one call site simply reads no
                   better than before. These are NOT gate failures.

Report structs_applied, fields_placed, globals_typed, variables_typed, every
line under "problems" verbatim in problems, every DEFERRED line verbatim in
deferred, every TARGET line verbatim in target_problems, the violation and gate
counts, and ok -- true when violations is 0, orphan ranges 0, error bookmarks 0,
and problems is empty. DEFERRED and TARGET lines do not affect ok.

Do not work around either kind. They must reach the report intact.

If a Ghidra call fails outright or times out, set ghidra_responding false and
stop.`
}

function arbitrateStructCollisionPrompt(collision) {
  return `${BRIEF}

Two struct layouts claim the same bytes, and only one can have them. The
transcription step reported:

  ${collision}

Neither was applied, so the database is unchanged and nothing is wrong in it
yet -- but nothing is right either, and leaving it means both layouts stay
half-landed.

Read both layout files in ${STRUCTS}, the anchors that proposed them in
${VERDICTS}, and the evidence for the addresses in dispute in ${DUMP}\\ev. The
assembly is in ${LOGIC_DUMP}\\asm.

The question is which of these the code actually says:

  * TWO NAMES FOR ONE TYPE. Check this first, because it is the cheapest to
    confirm and the easiest to miss. If the two layouts have the same size and
    the same fields and claim the SAME address ranges rather than overlapping
    ones, they are one record that two agents named differently -- neither could
    see what the other proposed. Then one name survives, chosen for describing
    the record better, and the loser is rewritten as exists_in_image false with
    alias_of set to the winner. Do not merge their field lists; pick the better
    layout whole.
  * ONE OBJECT. The bigger layout is right and the smaller one is describing a
    field of it. The code reaches those bytes from the record's base with a
    fixed displacement, never as a thing of its own. Then the smaller layout
    must drop that address from its apply_to -- the field it describes is
    already inside the bigger struct, and if the bigger struct's field list is
    missing it, add it there.
  * TWO OBJECTS. The bigger layout's size is wrong, or it is a family of
    separate variables that happen to sit together. The code loads the disputed
    address as an absolute immediate with no arithmetic from the other base.
    Then the bigger layout's size, fields and apply_to must be corrected.

A tie-break that is NOT available: applying whichever one is more convenient, or
leaving both in apply_to and letting the transcription order decide. That is
what produced this collision -- the last one written destroyed the first, and
the script reported success.

Rewrite whichever layout file is wrong, keeping its shape, and add to it:

  "_supersedes": "<what it said before and what changed it>"

Change one of them, not both, unless the correct answer genuinely requires a
field added to one and an apply_to entry removed from the other -- in which case
do exactly that and say so. Return the struct name you changed in "name", and
set wrote_file.`
}

function structPrompt(name) {
  return `${BRIEF}

Your job this time is one struct layout, and it is the only one you may write:

  ${name}

This is not the per-anchor job. A struct field means what every site that
touches it treats it as, so you are reading across functions rather than at one
address. Still one item per agent -- the item is just the whole struct.

WHERE THE EVIDENCE IS.

  ${WORK}\\structs.json         the candidate list, with the anchors whose
                                verdicts proposed this struct and the strides
                                they measured
  ${VERDICTS}\\<addr>.json      those verdicts, for what the base is and how it
                                is indexed
  ${DUMP}\\ev\\<addr>.json      the anchors themselves, with every reference
  ${LOGIC_DUMP}\\dec\\<addr>.c  the decompilation of any function, named and
                                commented by ticket 15
  ${LOGIC_DUMP}\\asm\\<addr>.txt the assembly of any function, which is where a
                                field's WIDTH comes from -- the decompiler
                                guesses, the operand size does not
  ${WORK}\\vocabulary.md        includes FD2's types.h verbatim

You may also use read-only Ghidra MCP calls if you need something the dumps do
not have:
  ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__read_memory"
Do NOT write anything to Ghidra. Transcription is a separate phase.

FD2 IS THE STARTING POINT AND NOT THE ANSWER. The predecessor project solved
these layouts for the previous game on the same engine, and its runtime_char is
0x50 bytes -- exactly this game's unit record. Read
${FD2}\\src\\include\\types.h and ${FD2}\\program_info\\ for the subsystem.
Then confirm every field against FDPS's own code before you carry it over. A
matching size is not a matching layout, and a field that quietly moved by two
bytes between the two games is precisely the kind of thing that produces a build
that runs and plays wrong. In the evidence for each field, say whether you
confirmed it in FDPS's code or assumed it from FD2 -- an assumed field is medium
confidence at best.

CROSS-CHECK AGAINST THE GAME, IF IT IS GAME DATA. Set is_game_data in what you
return: true when the record holds game content a player can see -- stats,
prices, ranges, class ids, chapter contents -- and false for a CRT or
sound-library structure, which the guide says nothing about and which owes no
cross-check. Where it is true, the strategy guide mirror has the ground truth
the players see:
  python ${REPO}\\tools\\guide_scrape\\... search "<term>"    (see its _index.md)
  the fdps-data skill for items, spells, classes and characters
Reading a few records out of the table and checking them against what the guide
says that item costs, or that class's growth is, turns a guess about field order
into a fact. Do it for at least one field of any table that holds game data, and
say in the file what you checked and what you found.

CHECK FIRST WHETHER YOU ARE A SECOND SPELLING OF SOMETHING ELSE ON THE LIST.
The candidates came from agents who each saw one anchor and could not see what
the others proposed, so one record arrives twice under two names. Your entry in
structs.json carries a "similar" array naming the candidates that resemble
yours. Look at each of them -- which anchors proposed it, what stride it
measured -- before you lay anything out. That array is a hint from a crude
string comparison, not the boundary of what to check: the whole candidate list
is in the same file and it is short, so read it. Two names can be one record
without looking alike at all. If one of them is the same record, only
one name survives, and the survivor is the one that describes the record better,
not the one that happens to sort first. If yours is the loser, write your file
with exists_in_image false and "alias_of" set to the winner, and stop; the
collector then folds your name away instead of a second layout being built for
the same bytes. If yours is the winner, or they are genuinely different records,
say so in the evidence and carry on.

THE ANSWER MAY BE THAT THERE IS NO STRUCT. Some of these candidates are named in
the ticket rather than discovered in the code, and the honest answer for some of
them will be "this game reads that table out of a resource file; there is no
such layout in the executable". That is a real finding, not a failure. Write the
file with exists_in_image false, say where the data does live, and give no
fields.

WRITE THE LAYOUT to ${STRUCTS}\\${name}.json, exactly this shape:

{
  "name": "${name}",
  "exists_in_image": true,
  "alias_of": "<only when this candidate turned out to be another one's record>",
  "size": <total bytes; must equal the stride the code indexes by>,
  "description": "<one line, goes on the type in Ghidra>",
  "evidence": "<what fixes the size: the IMUL constant, the array bound, the file record length>",
  "confidence": "high|medium|low",
  "fields": [
    { "offset": 0, "name": "<field>", "type": "<byte|word|dword|int|uint|char|byte[8]|...>",
      "comment": "<what it means, what range, what the values mean>",
      "evidence": "<the instruction or the caller that shows it, or 'from FD2, unconfirmed'>",
      "confidence": "high|medium|low" }
  ],
  "apply_to": [
    { "kind": "global", "addr": "<8 hex digits>", "type": "${name} *" },
    { "kind": "variable", "function": "<entry point, 8 hex digits>", "name": "<variable name>", "stack_offset": <optional, negative, e.g. -80>, "type": "${name} *" }
  ],
  "fd2_correspondence": "<which FD2 type this matches, field by field where they differ, or 'none'>",
  "guide_crosscheck": "<what you read out of the table and what the guide says it should be>",
  "open_question": "",
  "pitfall": ""
}

Rules on the layout itself:
  * The type's NAME follows naming.md, which applies the same two iron rules to
    types as to symbols. A game structure is fdps_ + snake_case. A LIBRARY
    structure -- CRT or AIL -- carries the library's own name with NO PREFIX:
    tm, FILE, _iobuf, rt_init. There is no crt_ prefix in this project; the only
    legal form is the compound crt_equivalent_, which means something hand
    written because it could not be matched to a library object, and that is not
    what a struct layout is. Writing crt_tm or crt_file is wrong, for the same
    reason memcpy is not called crt_memcpy: the rebuilt .c gets the type from
    <time.h> or <stdio.h>, and the Ghidra name and the C name must be the same
    string. Where you genuinely cannot identify the library's name for a
    structure that lives inside one object, use L$N_<module>_<use>, the same
    form the canon gives file-static symbols -- but look in the Watcom 10.0a
    headers first, because "could not identify" and "did not look" are different
    answers. No _t suffix, no address in the name.

    THE NAME YOU WERE HANDED MAY BREAK THIS. The candidate list was proposed by
    agents working from an earlier, wrong version of this rule, so a good many
    of the crt_-prefixed candidates are misnamed rather than misjudged. If yours
    is one, do BOTH of these, and the second is not optional:
      1. write the layout to ${STRUCTS}\\<correct name>.json;
      2. write ${STRUCTS}\\${name}.json -- the handed name -- with
         exists_in_image false and alias_of set to the correct name.
    Without the second file the collector keeps handing this candidate out for
    ever. Say in the evidence which library header gave you the name, and set
    handed_name to "${name}" in what you return whatever you decide.

A NOTE ON SELF-REFERENCE. A field that points at the struct being defined -- a
linked list's next, a parent pointer -- is spelled the ordinary way,
"<name> *". The transcription step declares every struct before it places any
field, so the type resolves.
  * Every field must fit inside size and must not overlap another. The
    transcription step refuses a struct that would grow past its declared size,
    because a struct that silently grew past the stride its callers index by is
    a layout that looks applied and is wrong.
  * Gaps are fine and honest. A byte nothing touches gets no field; do not
    invent unknown_2f padding fields to fill space you have not explained.
  * Name fields for what they mean. If FD2 called it hp_current, call it
    hp_current.
  * apply_to is where the readability actually comes from. List the globals that
    hold this type, and the local variables in the functions that walk it.
    Which names actually work is worth knowing before you write them:
      - a name ticket 15 gave a parameter or local is stored and will be found;
      - local_30 and auStack_50 are display names Ghidra regenerates on every
        decompile, but they encode the frame offset, so the transcription step
        resolves them anyway -- and you can give stack_offset instead to be
        explicit;
      - puVar1, iVar3, pvVar2 are decompiler temporaries. They exist only in the
        printed C, nothing in the database can carry a type there, and listing
        one buys a line in the report and no readability. If the only handle on
        a site is a temporary like that, leave it out and say so in the
        evidence.
    A target the transcription step cannot find is reported, so an entry that
    misses costs a line in the report, not silence.

Return the summary object. Do not put the field list in it -- it is in the file.`
}

// --------------------------------------------------------------- helpers

const unfinished = []
const deferrals = []
const pitfalls = []
const stopped = { yes: false, why: '' }

function stop(why) {
  stopped.yes = true
  stopped.why = why
  log(`STOP: ${why}`)
}

/**
 * The same line, once. Every struct round now transcribes the whole layout set
 * rather than its own additions, which is what makes cross-round collisions
 * visible -- but it also means an unfixable target miss is reported again on
 * every round after the one that found it. A closing report where one finding
 * appears eight times is a report nobody reads to the end.
 */
function distinct(list) {
  return list.filter((line, i) => list.indexOf(line) === i)
}

function chunk(list, size) {
  const out = []
  for (let i = 0; i < list.length; i += size) {
    out.push(list.slice(i, i + size))
  }
  return out
}

// 5.5, with the threshold measured rather than assumed. Judging agents are told
// they should not need Ghidra at all, so a single one reporting it dead is far
// more likely to be that agent making an off-script call than an outage -- the
// first full run produced exactly one such report while the transcription agent
// either side of it talked to Ghidra without trouble. Half a round saying it is
// an outage; one agent saying it is a mistake, and stopping on it throws away a
// working run. The transcription phase, which genuinely cannot proceed without
// Ghidra, remains a single-report stop.
/**
 * Collapse the transcription step's collision lines to one per pair of struct
 * names, keeping every line for that pair as context so the arbitrating agent
 * sees the full extent of the disagreement rather than one address of it.
 */
function dedupeCollisions(lines) {
  const byPair = new Map()
  for (const line of lines) {
    const names = line.match(/TARGET COLLISION (\S+) .* and (\S+) wants/)
    const key = names ? [names[1], names[2]].sort().join(' vs ') : line
    if (!byPair.has(key)) {
      byPair.set(key, [])
    }
    byPair.get(key).push(line)
  }
  const out = []
  for (const [key, group] of byPair) {
    out.push(group.length === 1
      ? group[0]
      : `${key} -- ${group.length} addresses in dispute:\n  ${group.join('\n  ')}`)
  }
  return out
}

function ghidraLooksDead(summaries, tag) {
  const dead = summaries.filter((s) => s.ghidra_responding === false).length
  if (dead === 0) {
    return false
  }
  // A majority alone is not enough, because the rounds are not all the same
  // size: a rescan batch of four trips a half-of-the-round test on two
  // misreports, and that is exactly what stopped an otherwise clean run whose
  // transcription agent then talked to Ghidra without trouble. Three agents
  // independently failing to reach it is the point where an outage is more
  // likely than a coincidence.
  if (dead >= 3 && dead * 2 >= summaries.length) {
    return true
  }
  log(`${tag}: ${dead} of ${summaries.length} agents claimed Ghidra is down; `
    + 'too few to be an outage, continuing')
  return false
}

// Transcribe one round, refresh the dump and the vocabulary, run both gates.
// The dump refresh is the reason this is worth a whole agent: an anchor judged
// next round needs to see that the neighbour below it now has a type covering
// it, and that only shows up in a re-export.
async function applyRound(tag, addrs) {
  if (!addrs.length) {
    return { ok: true, applied: 0, violations: 0, pending: -1, ghidra_responding: true }
  }
  return agent(
    `Transcribe one round of ticket 17 global-data verdicts into Ghidra, then check it.

You are not judging anything. Every name, type and comment in these verdict
files was decided by an agent that read that one anchor's references; your job
is to put them in and report honestly on what happened.

  ToolSearch "select:mcp__ghidra__run_ghidra_script,mcp__ghidra__save_program"

Run these in order, all through run_ghidra_script with the absolute path:

  1. ${TOOLS}\\ApplyGlobalVerdicts.java
     args: ${VERDICTS} ${addrs.join(' ')}

  2. ${TOOLS}\\DumpGlobalState.java
     args: ${DUMP}
     Re-exports the evidence so the next round's agents see the types this round
     produced. Read-only, about twenty-five seconds.

  3. ${TOOLS}\\build_vocabulary.py -- run it with python from ${REPO}, no args.
     Adds this round's names to the page every judging agent reads.

  4. ${BASELINE_AUDIT}
     args: ${REPO}\\workspace\\ghidra_baseline
     The structural gate. Read only its "# Gate" section: orphan code ranges,
     error bookmarks and undefined bytes must all be 0.

  5. ${TOOLS}\\AuditGlobals.java
     args: ${WORK}
     This ticket's gate. violations must be 0. pending is how many anchors still
     wear a default or loader-generated label -- it counts down as the ticket
     proceeds and is not a failure. Also report edata_consistent, which says
     whether the last non-zero initialised byte still falls below _edata.

  6. save_program()

Then report:

  applied / renamed / retyped   from the ApplyGlobalVerdicts output
  name_conflicts                every "NAME TAKEN" line, verbatim
  overlaps                      every "OVERLAP" line, verbatim
  segment_issues                every "SEGMENT" line, verbatim
  problems                      every other line under "problems", verbatim
  violations / pending          from AuditGlobals
  edata_consistent              from AuditGlobals
  orphan_ranges / error_bookmarks   from the baseline audit's Gate section
  ok                            true only when violations is 0, orphan ranges 0,
                                error bookmarks 0, and ApplyGlobalVerdicts
                                reported no problems other than NAME TAKEN and
                                OVERLAP lines

A NAME TAKEN or OVERLAP line is not yours to fix -- report it and the workflow
will arbitrate. What IS yours to fix, in this round, before you report:
  * a type that failed to resolve, where the verdict spelled a type Ghidra does
    not have. Report it; do not substitute one.
  * a stranded label the audit flags, where a rename landed inside an object a
    later verdict in the same round typed over it. Re-run the apply for the
    outer address after the inner one has been dealt with.
If you cannot get a gate clean, report ok false with the reason in problems; do
not paper over it.

If a Ghidra call fails outright or times out, set ghidra_responding false and
stop -- do not retry a dead database.`,
    { label: `apply:${tag}`, phase: 'Apply', schema: APPLY_REPORT })
}

// ------------------------------------------------------------------ Plan

phase('Plan')

// The worklist lives on disk, and the script cannot read disk. One agent runs
// the builder and hands back the addresses -- which also re-checks every verdict
// against the current dump, so an anchor whose bytes changed, or which a
// neighbour's type has since swallowed, comes back onto the list instead of
// being skipped for ever.
async function refreshWorklist(tag) {
  return agent(
    `Refresh the ticket 17 worklist.

Run, from ${REPO}:

  python tools\\global_data\\build_worklist.py

It rebuilds ${WORK}\\worklist.json from the dump and the verdict files, retiring
any verdict whose anchor no longer matches what the dump says about it.

Read the resulting worklist.json and return:
  todo      the "full" array, in the order the file has it -- referenced anchors
            first in address order, then the unreferenced ones. Do not re-sort.
  settled   counts.settled
  total     counts.in_scope
  retired   counts.retired_this_run

If the script fails, return an empty todo, settled 0, and ghidra_responding
false, with the error visible in your final message.`,
    { label: `worklist:${tag}`, phase: 'Plan', schema: WORKLIST })
}

let queue = []
let plan = null

if (!STRUCTS_ONLY) {
  if (cfg.addrs && cfg.addrs.length) {
    plan = { todo: cfg.addrs, settled: -1, total: -1, ghidra_responding: true }
    log(`worklist from args: ${plan.todo.length} anchor(s)`)
  }
  else {
    plan = await refreshWorklist('initial')
    if (!plan) {
      stop('the worklist agent did not return')
    }
    else {
      log(`worklist: ${plan.todo.length} to do, ${plan.settled} settled of ${plan.total}`
        + (plan.retired ? `, ${plan.retired} verdict(s) retired as stale` : ''))
    }
  }
  queue = stopped.yes ? [] : plan.todo.slice(0, MAX_ITEMS)
  if (!stopped.yes && plan.todo.length > queue.length) {
    log(`taking ${queue.length} this call; ${plan.todo.length - queue.length} left for the next one`)
  }
}
else {
  log('structs only: skipping the per-anchor stage')
}

// ----------------------------------------------------------- Judge/Apply

const done = []
const openVerdicts = []
const structCandidates = []
let roundNo = 0

for (const round of chunk(queue, ROUND_SIZE)) {
  if (stopped.yes) {
    break
  }
  roundNo++
  const tag = `r${roundNo}`
  log(`${tag}: judging ${round.length} anchor(s)`)

  const summaries = (await parallel(round.map((addr) => () =>
    agent(judgePrompt(addr), { label: `judge:${addr}`, phase: 'Judge', schema: VERDICT_SUMMARY })
  ))).filter(Boolean)

  // 5.2 -- when a whole round comes back empty the cause is outside this script
  // (session limit, API, machine) and retrying is guaranteed waste.
  if (summaries.length === 0 && round.length > 0) {
    for (const addr of queue.slice(queue.indexOf(round[0]))) {
      unfinished.push(`anchor ${addr}: not attempted, run stopped`)
    }
    stop(`round ${tag}: every one of ${round.length} agents came back empty -- upstream failure`)
    break
  }

  // 5.5 -- an agent that reached Ghidra and found it dead is a stop signal too,
  // and unlike 5.2 it can fire while agents are still returning.
  if (ghidraLooksDead(summaries, tag)) {
    stop(`round ${tag}: agents report Ghidra is not responding`)
  }

  // 5.1 -- done is decided by the file, not by the agent saying it went well.
  let usable = summaries.filter((s) => s.wrote_file)
  const lost = round.filter((addr) => !usable.some((s) => s.addr === addr))
  if (lost.length && !stopped.yes) {
    log(`${tag}: ${lost.length} anchor(s) came back without a verdict file, retrying once`)
    const retried = (await parallel(lost.map((addr) => () =>
      agent(judgePrompt(addr), { label: `judge-retry:${addr}`, phase: 'Judge', schema: VERDICT_SUMMARY })
    ))).filter(Boolean).filter((s) => s.wrote_file)
    usable = usable.concat(retried)
    for (const addr of lost) {
      if (!retried.some((s) => s.addr === addr)) {
        unfinished.push(`anchor ${addr}: no verdict file after a retry`)
      }
    }
  }

  if (stopped.yes) {
    for (const addr of round) {
      if (!usable.some((s) => s.addr === addr)) {
        unfinished.push(`anchor ${addr}: judged but not landed, run stopped`)
      }
    }
    break
  }

  const report = await applyRound(tag, usable.map((s) => s.addr))
  if (!report) {
    stop(`round ${tag}: the transcription agent did not return`)
    for (const s of usable) {
      unfinished.push(`anchor ${s.addr}: verdict written but not transcribed`)
    }
    break
  }
  if (report.ghidra_responding === false) {
    stop(`round ${tag}: Ghidra stopped responding during transcription`)
    break
  }

  // Two verdicts contradicting each other -- about a name, or about where an
  // object ends. Settle both now: leaving either means the loser stays
  // unfinished while the run reports itself complete.
  const disputes = []
  for (const c of (report.name_conflicts || [])) {
    disputes.push({ kind: 'name', text: c })
  }
  for (const o of (report.overlaps || [])) {
    disputes.push({ kind: 'overlap', text: o })
  }
  if (disputes.length) {
    log(`${tag}: ${disputes.length} dispute(s), arbitrating`)
    const fixed = (await parallel(disputes.map((d) => () =>
      agent(d.kind === 'name' ? arbitrateNamePrompt(d.text) : arbitrateOverlapPrompt(d.text),
        { label: `arbitrate:${tag}`, phase: 'Arbitrate', schema: ARBITRATION })
    ))).filter(Boolean).filter((a) => a.wrote_file)
    if (fixed.length) {
      const second = await applyRound(`${tag}-arb`, fixed.map((a) => a.addr))
      if (!second || !second.ok) {
        unfinished.push(`round ${tag}: disputes still unresolved after arbitration`)
      }
    }
    if (!fixed.length) {
      for (const d of disputes) {
        unfinished.push(`${d.kind} dispute unresolved: ${d.text}`)
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

  for (const s of (report.segment_issues || [])) {
    unfinished.push(`segment issue reported, not acted on: ${s}`)
  }
  if (report.edata_consistent === false) {
    unfinished.push('the audit says initialised data now runs past _edata -- the '
      + 'memory map in program_info/memory_layout.md and this run disagree')
  }

  for (const s of usable) {
    done.push(s)
    if (s.confidence === 'low' || s.needs_rescan) {
      openVerdicts.push(s.addr)
    }
    if (s.has_pitfall) {
      pitfalls.push(s.addr)
    }
    if (s.struct_candidate) {
      structCandidates.push(s.struct_candidate)
    }
  }
  log(`${tag}: landed ${report.applied}, ${report.pending} anchors still unnamed program-wide`)
}

// ---------------------------------------------------------------- Rescan

// Every agent sees exactly one anchor, so a verdict that depends on a
// neighbour -- above all "am I an interior of the thing below me" -- is
// unanswerable until that neighbour is judged. This is the pass where those
// come back, with the neighbours settled and their verdict files readable.
if (!STRUCTS_ONLY && !stopped.yes && !SKIP_RESCAN && MAX_RESCAN_PASSES > 0) {
  phase('Rescan')

  // The in-memory list only has this call's rounds in it. Earlier calls left
  // open verdicts on disk too, and they are just as unfinished.
  const collected = await agent(
    `List the ticket 17 verdicts that are still open.

Read every ${VERDICTS}\\*.json (ignore .superseded-* files -- those are retired)
and return the addr of each one where any of these holds:

  * needs_rescan is true
  * name.confidence is "low"
  * type.confidence is "low"
  * classification is "unknown"

A non-empty open_question on its own is NOT a reason to re-read. Most of them
record something no amount of neighbour-judging will answer -- what a magic
value means, what units a field is in -- and re-reading those spends an agent to
write the same sentence again. The agent that wrote the verdict said with
needs_rescan whether judging the neighbours could actually move it; trust that
field.

Return just the addresses. Do not read any evidence and do not judge anything;
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
    const batch = open.slice(0, MAX_ITEMS)
    log(`rescan pass ${pass}: re-reading ${batch.length}`)
    const results = []
    for (const group of chunk(batch, ROUND_SIZE)) {
      const got = (await parallel(group.map((addr) => () =>
        agent(rescanPrompt(addr), { label: `rescan:${addr}`, phase: 'Rescan', schema: VERDICT_SUMMARY })
      ))).filter(Boolean)
      if (got.length === 0 && group.length > 0) {
        stop(`rescan pass ${pass}: every agent came back empty -- upstream failure`)
        break
      }
      if (ghidraLooksDead(got, `rescan${pass}`)) {
        stop(`rescan pass ${pass}: agents report Ghidra is not responding`)
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
    unfinished.push(`anchor ${addr}: verdict still open after the rescan`)
  }
}

// ---------------------------------------------------------------- Struct

// The second stage, and the one the ticket singles out as not being per-item.
// It runs when the anchors are through, because the struct list is discovered
// from their verdicts -- or on its own, with { structs: true }.
const structsRun = []
const anchorsLeft = plan && !STRUCTS_ONLY
  ? Math.max(0, plan.todo.length - queue.length)
  : 0

if (!stopped.yes && (STRUCTS_ONLY || anchorsLeft === 0)) {
  phase('Struct')

  const list = await agent(
    `Refresh the ticket 17 struct worklist.

Run, from ${REPO}:

  python tools\\global_data\\collect_structs.py

It gathers the struct candidates the per-anchor verdicts proposed, folds in the
layouts the ticket names explicitly, and writes ${WORK}\\structs.json.

Read the result and return "todo" and the count of "settled". Do not judge
anything; this is a listing.`,
    { label: 'structs:collect', phase: 'Struct', schema: STRUCT_LIST })

  const todo = (list ? list.todo : []).slice(0, MAX_STRUCTS)
  if (!list) {
    unfinished.push('the struct worklist agent did not return; no struct was laid out')
  }
  else {
    log(`structs: ${list.todo.length} to lay out, ${list.settled || 0} settled`
      + (list.todo.length > todo.length ? `; taking ${todo.length} this call` : ''))
  }

  for (const group of chunk(todo, Math.min(ROUND_SIZE, 6))) {
    if (stopped.yes) {
      break
    }
    let got = (await parallel(group.map((name) => () =>
      agent(structPrompt(name), { label: `struct:${name}`, phase: 'Struct', schema: STRUCT_SUMMARY })
    ))).filter(Boolean)

    if (got.length === 0 && group.length > 0) {
      stop(`struct round: every one of ${group.length} agents came back empty -- upstream failure`)
      for (const name of todo) {
        unfinished.push(`struct ${name}: not attempted, run stopped`)
      }
      break
    }
    if (ghidraLooksDead(got, 'struct')) {
      stop('struct round: agents report Ghidra is not responding')
      break
    }

    // 5.1 again -- one retry, then it goes on the unfinished list.
    // Matched on handed_name, not name. The canon lets an agent conclude the
    // candidate was misnamed and write the layout under the right name, and
    // matching on the name it produced would then read as "this item was never
    // done", retry it, and get the same rename again.
    const worked = (s) => s.handed_name || s.name
    let usable = got.filter((s) => s.wrote_file)
    const lost = group.filter((name) => !usable.some((s) => worked(s) === name))
    if (lost.length) {
      log(`structs: ${lost.length} came back without a layout file, retrying once`)
      const retried = (await parallel(lost.map((name) => () =>
        agent(structPrompt(name), { label: `struct-retry:${name}`, phase: 'Struct', schema: STRUCT_SUMMARY })
      ))).filter(Boolean).filter((s) => s.wrote_file)
      usable = usable.concat(retried)
      for (const name of lost) {
        if (!retried.some((s) => worked(s) === name)) {
          unfinished.push(`struct ${name}: no layout file after a retry`)
        }
      }
    }
    for (const s of usable) {
      if (s.handed_name && s.name && s.handed_name !== s.name) {
        log(`structs: ${s.handed_name} was laid out as ${s.name}`)
      }
    }

    // A struct the agent found does not exist in the image is settled, not
    // applied. Nothing goes into the data type manager for it.
    const toApply = usable.filter((s) => s.exists_in_image !== false)
    for (const s of usable) {
      structsRun.push(s)
      if (s.has_pitfall) {
        pitfalls.push(`struct ${s.name}`)
      }
      if (s.exists_in_image === false) {
        log(`structs: ${s.name} has no layout in the executable; recorded, not applied`)
      }
      // The guide is the players' view of the game's content. It has nothing to
      // say about a FILE or a sound driver, so only a game record owes it an
      // answer.
      if (s.guide_checked === false && s.exists_in_image !== false && s.is_game_data) {
        unfinished.push(`struct ${s.name}: game record laid out without the strategy-guide cross-check`)
      }
    }

    if (toApply.length) {
      const applied = await agent(structApplyPrompt(toApply.map((s) => s.name)),
        { label: 'struct-apply', phase: 'StructApply', schema: STRUCT_APPLY })

      if (!applied) {
        stop('the struct transcription agent did not return')
        for (const s of toApply) {
          unfinished.push(`struct ${s.name}: layout written but not transcribed`)
        }
        break
      }
      if (applied.ghidra_responding === false) {
        stop('Ghidra stopped responding during struct transcription')
        break
      }
      // A call site that could not take the type is a loss of readability at
      // that one site, not a broken database. It goes on the list and the run
      // carries on; only a layout that is wrong about the program stops it.
      for (const p of (applied.target_problems || [])) {
        unfinished.push(`struct apply, type not carried to one site: ${p}`)
      }
      // Deferred fields are only worth reporting if they are still deferred at
      // the end, so they are noted per round and filtered out of the closing
      // list once the struct they wait for has landed.
      for (const d of (applied.deferred || [])) {
        deferrals.push(d)
      }
      // A collision is two layouts contradicting each other about what lives at
      // an address. It is settleable from the evidence, so settle it rather
      // than stopping the run -- the same treatment the anchor stage gives a
      // boundary dispute.
      // One pair of layouts that disagree everywhere produces one collision
      // line per address -- the emu387 round produced fourteen from two real
      // disagreements. Arbitrating each line separately would spend agents on
      // the same question repeatedly and, worse, have several of them rewriting
      // the same two files at once. One agent per PAIR.
      const collisions = dedupeCollisions(
        (applied.problems || []).filter((p) => p.includes('TARGET COLLISION')))
      if (collisions.length) {
        log(`structs: ${collisions.length} layout disagreement(s) to arbitrate`)
        const fixed = (await parallel(collisions.map((c) => () =>
          agent(arbitrateStructCollisionPrompt(c),
            { label: 'arbitrate:struct', phase: 'Arbitrate', schema: ARBITRATION })
        ))).filter(Boolean).filter((a) => a.wrote_file)
        if (fixed.length) {
          const second = await agent(structApplyPrompt(toApply.map((s) => s.name)),
            { label: 'struct-apply-arb', phase: 'StructApply', schema: STRUCT_APPLY })
          if (second && second.ok) {
            log(`structs: collisions settled, ${second.structs_applied} applied`)
            for (const p of (second.target_problems || [])) {
              unfinished.push(`struct apply, type not carried to one site: ${p}`)
            }
            continue
          }
          unfinished.push('struct collisions still unresolved after arbitration')
        }
        else {
          for (const c of collisions) {
            unfinished.push(`struct collision unresolved: ${c}`)
          }
        }
      }

      if (!applied.ok) {
        stop(`struct transcription: gate failed -- ${applied.violations} violation(s), `
          + `${applied.orphan_ranges} orphan range(s), ${applied.error_bookmarks} error bookmark(s), `
          + `${(applied.problems || []).length} layout problem(s)`)
        for (const p of (applied.problems || [])) {
          unfinished.push(`struct apply: ${p}`)
        }
        break
      }
      log(`structs: applied ${applied.structs_applied}, `
        + `${applied.fields_placed} field(s), ${applied.variables_typed} variable(s) retyped`)
    }
  }

  if (list && list.todo.length > todo.length) {
    for (const name of list.todo.slice(MAX_STRUCTS)) {
      unfinished.push(`struct ${name}: not attempted this call, budget reached`)
    }
  }
}
else if (!stopped.yes) {
  log(`structs: skipped, ${anchorsLeft} anchor(s) still unjudged -- `
    + 'the struct list is discovered from their verdicts')
}

// ---------------------------------------------------------------- Report

phase('Report')

const left = distinct(unfinished)
// Deferrals are deliberately NOT carried into the closing list from here. A
// field deferred in round three because its struct had not been written yet is
// satisfied the moment round five writes it, and this script cannot tell which
// of the two happened. The report agent re-runs the transcription -- it is
// idempotent -- and whatever it still calls DEFERRED is the honest answer.
log(`${deferrals.length} deferral(s) seen during the run; the report re-measures them`)

// 5.6 -- nothing that looks like a finished deliverable gets written after a
// stop. A half-populated knowledge base page is indistinguishable from a
// complete one, which is exactly what makes it dangerous.
const status = await agent(
  `Report where ticket 17 stands, from the files -- do not take anything on trust.

  ToolSearch "select:mcp__ghidra__run_ghidra_script"

  1. python ${REPO}\\tools\\global_data\\build_worklist.py     (from ${REPO})
  2. python ${REPO}\\tools\\global_data\\collect_structs.py     (from ${REPO})
  3. run_ghidra_script ${TOOLS}\\AuditGlobals.java  args: ${WORK}
  4. run_ghidra_script ${TOOLS}\\ApplyStructDefs.java  args: ${STRUCTS} all
     Re-applying the whole layout set changes nothing that is already right; it
     is here to re-measure the DEFERRED lines. A field deferred mid-run because
     the struct it names had not been written yet is satisfied once that struct
     lands, and only this final pass can tell which deferrals survived.

Write ${WORK}\\run_report.md with:
  * how many anchors are in scope, how many have a verdict, how many are still
    to do, and how the verdicts split across classification and pool
  * the violation count and every violation line if there are any
  * whether the audit still finds the initialised data ending below _edata
    (0x63930), which is this ticket's answer on the data/BSS boundary
  * how many verdicts are still open (low confidence, needs_rescan, or an
    unknown classification)
  * the struct list: which have a layout file, which of those say the record has
    no layout in the executable and where the data lives instead, which are
    folded away as a second spelling of another (the aliases), and which are
    still to do
  * every DEFERRED line the final ApplyStructDefs run still reports, as
    "<struct>.<field> waits on <type>". An empty list here is the good outcome
    and worth saying so explicitly
  * every verdict and every struct layout whose "pitfall" field is non-empty, as
    a list of "<addr or struct name> <name> -- <the pitfall text>". These are
    the findings that say a faithful-looking C rewrite would behave differently
    from the original, and they are the reason to read this report at all. Do
    NOT write them into rebuild_info/pitfalls.md yourself -- they are collected
    across every run of this ticket and folded in once, and one run writing its
    own would duplicate what another run already put there.
  * ${stopped.yes ? `THE RUN STOPPED: ${stopped.why}` : 'the run completed its queue'}
  * this unfinished list, verbatim:
${left.length ? left.map((u) => `      - ${u}`).join('\n') : '      (nothing)'}

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
  structs_laid_out: structsRun.map((s) => s.name),
  struct_candidates_found: structCandidates.filter((v, i, a) => a.indexOf(v) === i),
  pitfalls_found: pitfalls,
  unfinished: left,
  report: status ? status.files : [],
}
