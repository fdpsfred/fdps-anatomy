// Ticket 25.11: judge the new traces that ticket 25.9 handed over, one per agent.
//
// While verifying the 53 cut-content findings, ticket 25.9's agents wrote down
// things they noticed outside their own claim ("new traces"), one sentence
// each, deliberately unjudged.  28 of them were addressed to ticket 25.11
// (units, enemies and classes), with many duplicates among them: the same
// MAP31.DAT fact alone was reported four times.  Before any of them may enter
// cut_content/, each one has to be verified and placed on its own.  This
// workflow does exactly that and nothing else.
//
// It is the ticket-25.10 workflow (traces_ticket25_10.js) with the ticket, the
// worklist, the topic page and the placement prompts changed; cuttrace.py is
// shared and every call to it carries --ticket 25.11.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One trace per agent.  The worklist is the IDS array below; every agent()
//   call carries exactly one id and reads that one trace through
//   "cuttrace.py show <ID>".  No judging agent sees the list.
//
//   Judgements go to files, agents return ~200 bytes.  The closing report is
//   built by cuttrace.py from the files, so an interrupted run resumes by
//   simply being run again: a trace whose judgement passes the gate is done.
//
//   Judging agents are read-only on src/, Ghidra and the knowledge base.  The
//   landing (transcribing addenda, new entries and exclusions into
//   cut_content/, pitfalls, hand-offs) is done afterwards by the ticket's own
//   session from the archived judgements, followed by
//   "cut_content.py check" as the landing gate.
//
// args: {
//   date:            "YYYY-MM-DD"  required; names the devlog/runs files
//   roundSize:       number        traces judged per round (default 8)
//   maxRescanPasses: number        re-reading passes (default 2)
// }

export const meta = {
  name: 'cut-traces-ticket25-11',
  description: 'Verify and place each of the 28 new unit, enemy and class traces from ticket 25.9, one agent per trace',
  phases: [
    { title: 'Plan', detail: 'read which traces still need a judgement' },
    { title: 'Judge', detail: 'one agent per trace, judgement written to a file' },
    { title: 'Gate', detail: 'every judgement must carry first-hand evidence and a valid placement' },
    { title: 'Rescan', detail: 'a second agent re-reads everything that would land, and every doubt' },
    { title: 'Report', detail: 'closing report and judgement archive in devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
// Every cuttrace.py call must carry --ticket 25.11 (its default is 25.10), so
// TOOL includes it: "python ${TOOL} show T11-01" runs
// "python ...\cuttrace.py --ticket 25.11 show T11-01".
const TOOL = REPO + '\\tools\\cut_traces\\cuttrace.py --ticket 25.11'
const WS = REPO + '\\workspace\\cut_traces\\25.11'
const JUDGEMENTS = WS + '\\judgements'
const VERDICTS_2509 = REPO + '\\devlog\\runs\\2026-09-25-cut-verify-verdicts.json'
const CUT = REPO + '\\cut_content'

const IDS = [
  'T11-01', 'T11-02', 'T11-03', 'T11-04', 'T11-05', 'T11-06', 'T11-07', 'T11-08',
  'T11-09', 'T11-10', 'T11-11', 'T11-12', 'T11-13', 'T11-14', 'T11-15', 'T11-16',
  'T11-17', 'T11-18', 'T11-19', 'T11-20', 'T11-21', 'T11-22', 'T11-23', 'T11-24',
  'T11-25', 'T11-26', 'T11-27', 'T11-28',
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
  required: ['id', 'verdict', 'disposition', 'confidence', 'wrote_file', 'upstream_dead'],
  properties: {
    id: { type: 'string', description: 'The trace id, exactly as given' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    disposition: { type: 'string', enum: ['absorbed', 'addendum', 'new_entry', 'exclude', 'route', 'drop'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean', description: 'True once the judgement file exists' },
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
  required: ['id', 'changed', 'verdict', 'disposition', 'confidence', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    changed: { type: 'boolean', description: 'True if the judgement file was rewritten' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    disposition: { type: 'string', enum: ['absorbed', 'addendum', 'new_entry', 'exclude', 'route', 'drop'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
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

const CATEGORIES = `The classes, from CONTEXT.md "刪減與未用" and the rules table in
${CUT}\\_index.md (read both once).  A trace belongs to exactly one:

  residual              殘留內容  finished content (code, assets, numbers or script
                                  complete enough to work) that no game path reaches
  stub                  空殼      the mechanism, field or slot exists but nothing was
                                  put in it.  Residual lacks a path; a stub lacks content
  sealed                被封住的內容  content AND the path to it both exist, but an original
                                  bug keeps the player from getting or seeing it
  predecessor_leftover  前作遺留  code or data inherited UNCHANGED from FD2 that does
                                  nothing in FDPS; needs an FD2-side comparison.  If
                                  FDPS rewrote it and still never enabled it, it is
                                  residual, not predecessor_leftover
  negative              否定性結論  a verified statement that something does NOT exist
  excluded              排除      real, but belongs to no class: compiler output, unused
                                  API of a THIRD-PARTY library (Watcom CRT, Miles AIL),
                                  unreachable defensive branches, a data-entry error, a
                                  feature or a data row in use.  Finished but unused
                                  members of the game's own modules and data tables
                                  are residual, not excluded
  none                  not a cut-content matter at all (a bug's mechanism, a
                        documentation error, a plain fact about how the game works)

"Different from FD2" is not the same as "FDPS rewrote it".  Only claim a rewrite
when the FD2 side shows the same thing and FDPS demonstrably changed it; data
that FDPS simply made anew (its own maps, tables and art) is FDPS content, and
FD2 having nothing like it does not make it predecessor_leftover either.`

const DISPOSITIONS = `Where the trace goes -- "disposition".  Pick exactly one:

  absorbed   Its fact is ALREADY stated by an existing entry on a cut_content/
             page (read ${CUT}\\units.md; the other pages code.md, items.md,
             battle_assets.md, story.md may exist too) or in the exclusion list
             of ${CUT}\\_index.md.  entry = that id (e.g. "U01", "U09").
             Nothing to land.
  addendum   It is part of an existing units.md entry's subject, the entry does
             not say it yet, and it is worth saying.  entry = that id; kb_text =
             the one or two sentences to add to that entry.
  new_entry  It is a cut-content trace of its own that belongs on the UNITS,
             ENEMIES AND CLASSES page (units.md: characters and enemies never
             fielded, portraits / map icons / battle animations nothing shows,
             classes nobody has, growth or spell-learning rows nothing reaches,
             FRIAPRDA / FRILEVUP / GETMGTAB / RANKUP / ENEMYDAT / PROMAP /
             PROEQU rows) and no entry covers it.  topic = "units"; category =
             one of residual / stub / sealed / predecessor_leftover / negative;
             title = the one-line heading; kb_text = the entry body in that
             page's format (the bullets 是什麼 / 為什麼到不了 or 為什麼沒有內容 or
             為什麼沒作用 / 暗示 or 不能確定的部分).
  exclude    Real, but excluded (see the classes).  topic = "units";
             category = "excluded"; title = what it is (the 內容 cell);
             kb_text = the one-sentence reason (the 理由 cell).
  route      Not for units.md: set route to one of
               known_bugs  an original bug's mechanism (program_info/known_bugs.md),
                           e.g. a crash path or an out-of-range read
               pitfalls    only a rebuild pitfall (rebuild_info/pitfalls.md)
               kb_fix      an error or a gap in the knowledge base, a src/
                           comment or a Ghidra label/comment (the fixing ticket
                           25.15).  Also use kb_fix, saying so in route_note,
                           for a plain fact about how the game works that a
                           program_info/ or assets/ page lacks, and for a
                           program trace that belongs on code.md (ticket 25.10
                           has no route value of its own)
               25.12 25.13 25.14   a cut-content trace for another topic page:
                           items / battle assets / story & scenes.  Deployment
                           records that are never deployed (the records past a
                           map's count, waves nothing deploys) and maps nothing
                           loads are story & scenes (25.14)
             Never route to 25.11: that is this page.  Say in route_note, in one
             Traditional Chinese sentence, what the receiver has to do (for
             kb_fix: which file or address says what, and what is right).
  drop       The trace is refuted, or true but says nothing worth recording
             anywhere.  Explain in conclusion.

A pitfall ("a rebuild that follows intuition gets this wrong") goes into
pitfall_candidate in addition to any disposition.  The data files are read by
the rebuilt executable unchanged, so a fact about a data file is a pitfall only
if some code would plausibly be written to treat that data differently.`

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\  -- the complete rebuilt C source of FDPS.LE, every function
     emitted with comments.  Primary source.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE.  Load tools in one ToolSearch call, e.g.
       ToolSearch "select:mcp__ghidra__get_xrefs_to,mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__read_memory,mcp__ghidra__search_byte_patterns,mcp__ghidra__search_instructions"
     Never call a Ghidra tool that writes (rename, comment, prototype, label,
     bookmark, save).
  3. The game's own data: ${REPO}\\fdps_game_files\\ (the original files) and
     ${REPO}\\workspace\\vfs_dump\\<CONTAINER>\\ (every .VFS unpacked).  Decoders
     under ${REPO}\\tools\\ (vfs_dump, map_decode, cutscene_script, text_decode,
     save_format, saf_decode, cel_decode, data_tables).  Cite FILE@offset.
     Unit names are entry (character id + 1) of FDETXT00.TXT, class names
     entry (0xA1 + class code); the decoded tables are in
     ${REPO}\\assets\\characters.md, enemies.md, classes.md and
     assets\\text\\global_text.md.
  4. Corroboration only: the guide mirror
     (python ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps),
     the FD2 project C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\ (src/, knowledge
     base, fd2_game_files/) for anything about FD2, and the knowledge base.

Context you may read (read-only): the verdict of the ticket-25.9 finding your
trace came from -- in ${VERDICTS_2509}, the object whose "id" equals the trace's
"from" (extract just that one object with a short python command; do not read
the others).  It tells you what that agent was looking at.  It is context, not
evidence: re-derive anything you rely on.`

const JUDGEMENT_SHAPE = `{
  "id": "<the id>",
  "trace_sha1": "<the trace_sha1 printed by show, verbatim>",
  "from": "<the from printed by show, verbatim>",
  "verdict": "holds" | "refuted" | "needs_correction",
  "disposition": "absorbed" | "addendum" | "new_entry" | "exclude" | "route" | "drop",
  "entry": "<existing entry id for absorbed/addendum, else empty>",
  "topic": "<units for new_entry/exclude, else empty>",
  "category": "residual" | "stub" | "sealed" | "predecessor_leftover" | "negative" | "excluded" | "none",
  "route": "none" | "known_bugs" | "pitfalls" | "kb_fix" | "25.12" | "25.13" | "25.14",
  "title": "<繁體中文。new_entry 的標題或 exclude 的內容欄；否則空字串>",
  "kb_text": "<繁體中文。addendum/new_entry/exclude 要落地的文字；否則空字串>",
  "route_note": "<繁體中文。route 時接手的人要做什麼；否則空字串>",
  "confidence": "high" | "medium" | "low",
  "conclusion": "<繁體中文。驗證後的事實，一到三句，結論式>",
  "evidence": [
    { "source": "src", "location": "src/village.c:123", "observation": "<what is there>" },
    { "source": "ghidra", "location": "000357a0", "observation": "..." },
    { "source": "data", "location": "MAP31.DAT@0x83", "observation": "..." },
    { "source": "guide" | "fd2" | "investigation", "location": "...", "observation": "..." }
  ],
  "corrected_trace": "<繁體中文。needs_correction 時寫出修正後的完整陳述；否則空字串>",
  "pitfall_candidate": "<繁體中文。重建時照直覺寫就會與原版不同的事；否則空字串>",
  "open_question": "<繁體中文。沒能確定的部分；沒有則空字串>"
}`

const KB_STYLE = `kb_text and title are transcribed into the knowledge base as they are, so:
conclusion-style Traditional Chinese (state what it is, never how you found
it -- no "驗證時", "第一位讀者", "調查"); cite src/ file and function name and a
Ghidra address such as \`0x357a0\`, never a line number (line numbers drift);
game proper nouns exactly as the game writes them; no workspace/ or devlog/
paths.  Data files are cited by name and offset (\`FRILEVUP.DAT\` \`+0x176\`).
Match the tone of ${CUT}\\units.md.`

function judgePrompt(id) {
  return `You are judging ONE trace for the cut-and-unused-content knowledge base of
FDPS (炎龍騎士團外傳, a 1997 DOS game) whose executable FDPS.LE has been fully
reverse engineered into C.

Your trace: ${id}

Get it by running exactly:

  python ${TOOL} show ${id}

That prints the id, the ticket-25.9 finding it came from ("from"), the trace
sentence and its trace_sha1.  This is the only trace you work on.  Other traces
are judged by other agents at the same time, and several traces may describe
the same fact; judge yours on its own and never write anything but your own
judgement file.

The trace was written in passing by an agent busy with something else, and was
never checked.  Treat it as an assertion to test.  You have two jobs:

  1. VERIFY it from first-hand sources: verdict holds / needs_correction (true
     but some detail wrong or overstated -- write corrected_trace) / refuted.
  2. PLACE it: decide its disposition, as defined below.  To decide absorbed /
     addendum / new_entry you must read ${CUT}\\units.md and the exclusion
     list at the end of ${CUT}\\_index.md (read-only), and glance at the
     other topic pages there for an entry that already owns the fact.

${SOURCES}

${CATEGORIES}

${DISPOSITIONS}

${KB_STYLE}

Be thorough but bounded.  If something cannot be settled with reasonable
effort, say so in open_question and lower confidence; never call it settled on
the trace's word.  A wrong confident judgement is copied into the knowledge
base; an honest "could not settle" is not.

Scratch scripts and outputs go under ${WS}\\scratch\\${id}\\ and nowhere else.

WRITE YOUR JUDGEMENT with the Write tool to exactly:

  ${JUDGEMENTS}\\${id}.json

UTF-8 JSON of this shape:

${JUDGEMENT_SHAPE}

The gate (run it yourself before you finish and fix what it reports):

  python ${TOOL} check --ids ${id}

It requires at least one first-hand evidence item (src with file:line, ghidra
with an address, data with FILE@offset); refuted -> disposition drop;
needs_correction -> corrected_trace; absorbed/addendum -> an entry id that is
really on a cut_content/ page; addendum/new_entry/exclude -> kb_text;
new_entry/exclude -> topic "units" and a title; new_entry -> one of the five
classes; exclude -> category excluded; route -> a target and a route_note.

Set upstream_dead only if the Ghidra tools stop answering, or python or the repo
itself is unusable.  If Ghidra fails, do not guess around it: write what you
have, set upstream_dead, and stop.

Then return the summary.  Your final output is data for the workflow, not a
message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.11 trace-judgement gate over one round.

Round tag: ${tag}
Traces (${ids.length}): ${ids.join(' ')}

Run exactly:

  python ${TOOL} check --ids ${ids.join(' ')} --json

It prints a JSON object with checked, ok, missing, failures and gate_passed.
Report those: every id under "failures" goes into failing, every id under
"missing" into missing.  Condense the failure reasons into problems, one short
line per kind of problem.

cuttrace.py accepts "25.11" as a route, but for this ticket that is its own
page and not a valid target.  So also run, for the same ids:

  python -c "import json,os,sys; d=sys.argv[1]; print([i for i in sys.argv[2:] if os.path.exists(os.path.join(d,i+'.json')) and json.load(open(os.path.join(d,i+'.json'),encoding='utf-8')).get('route')=='25.11'])" "${JUDGEMENTS}" ${ids.join(' ')}

and add every id it prints to failing (problem: "routed to 25.11, its own
page"), with gate_passed false.

Do not fix anything and do not edit any judgement file.  The workflow re-runs
the judging agent for whatever you report.  If the command cannot run at all,
say so in problems and set gate_passed false.`
}

function rescanPrompt(id, why) {
  return `A first agent judged ONE trace for the FDPS cut-content knowledge base and
its judgement needs a second reading.  You are that second reader, for this one
trace only.

Your trace: ${id}
Why it came back: ${why}

Get the trace:        python ${TOOL} show ${id}
The first judgement:  ${JUDGEMENTS}\\${id}.json

Work in this order, because the point of a second reader is independence:

  1. Read the trace, then form your own view from first-hand sources BEFORE
     reading the first judgement closely -- at minimum re-check the evidence the
     verdict and the placement rest on.
  2. Then read the first judgement and compare.  Check the placement as hard as
     the facts: is the kb_text true, conclusion-style, free of process words and
     line numbers?  Is an addendum really missing from its entry, a new entry
     really not covered, a route really outside units.md?
  3. You may read other traces' judgement files in ${JUDGEMENTS}\\ as evidence
     (for example when the same fact was judged there).  That is using a
     judgement someone else made, not making one for them.  Never edit another
     judgement file.

${SOURCES}

${CATEGORIES}

${DISPOSITIONS}

${KB_STYLE}

If your reading changes anything -- verdict, disposition, category, entry,
title, kb_text, route, confidence, a corrected detail -- rewrite
${JUDGEMENTS}\\${id}.json with the Write tool, keeping exactly the same field
shape, trace_sha1 and from, and add a "_supersedes" field: one short
Traditional Chinese paragraph saying what the first reading concluded and what
changed it.  Set changed to true.

If it does not, leave every field exactly as it is and add one field,
"_reread": a single Traditional Chinese line saying you confirmed it and what
you checked.  Set changed to false.  That field is how a later run knows this
judgement already had its second reading.

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

if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { stopped: 'args.date (YYYY-MM-DD) is required; it names the devlog/runs files' }
}

phase('Plan')
const plan = await agent(
  `Read the ticket-25.11 trace worklist.  You decide nothing; you only report what is left.

Run exactly:

  python ${TOOL} pending --all

It prints a JSON array with one object per trace: an id and a state that is
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
    stopped: 'cuttrace.py and this script disagree on the trace ids; update IDS before running',
    drift: drift,
  }
}
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} traces, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

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

  // 5.1 -- one retry per trace; still bad after that is unfinished, never done.
  const retry = [].concat(gate.failing || [], gate.missing || []).filter((i) => batch.includes(i))
  if (retry.length > 0) {
    if (gate.problems) {
      log(`round ${round}: gate said: ${gate.problems}`)
    }
    log(`round ${round}: retrying ${retry.join(' ')}`)
    const second = (await parallel(retry.map((id) => () =>
      agent(judgePrompt(id), { label: `retry:${id}`, phase: 'Judge', schema: SUMMARY })
    ))).filter(Boolean)
    if (second.length === 0) {
      stopped = `the retries of round ${round} returned nothing at all; treating it as an upstream failure`
      unfinished.push(...retry, ...todo.slice(start + ROUND_SIZE))
      break
    }
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
      log(`round ${round}: the re-gate returned nothing; the retried traces count as unfinished`)
      unfinished.push(...retry)
    }
  }
}

// ----------------------------------------------------------------- rescan
//
// Every judgement that would land in the knowledge base (addendum, new entry,
// exclusion) gets a second reader, and so does every verdict other than holds,
// every confidence below high and every open question.  The list is derived
// from the judgement files (cuttrace.py rescan), so an interrupted rescan
// resumes where it stopped.

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= MAX_RESCAN_PASSES && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.11 rescan worklist.  You decide nothing.

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
        rescanLog.push(`${r.id} -> ${r.verdict}/${r.disposition} (${r.confidence}): ${r.what_changed || ''}`)
        log(`  ${r.id} changed -> ${r.verdict}/${r.disposition} (${r.confidence})`)
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
        log(`rescan pass ${pass}: judgements now failing the gate: ${bad.join(' ')}`)
        unfinished.push(...bad.filter((i) => !unfinished.includes(i)))
      }
    } else {
      log(`rescan pass ${pass}: the gate returned nothing; its traces are checked again by the report`)
    }
  }
}

// ----------------------------------------------------------------- report
//
// The closing report is always produced, with the stop reason when there is
// one.  The judgement archive is not produced after a stop (ADR-0007 5.6): a
// partial set reads exactly like a complete one.

phase('Report')
// The reason may quote an agent's note; keep only characters no shell expands.
const stopArg = stopped ? ` --stopped "${stopped.replace(/[^A-Za-z0-9 .,:;()_\-]/g, ' ')}"` : ''
const report = await agent(
  `Write the ticket-25.11 trace closing report.  You decide nothing.

Run exactly:

  python ${TOOL} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-cut-traces-25.11-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-cut-traces-25.11-judgements.json`} and prints a
one-line JSON digest.  Return written = the files it names, summary = the digest
verbatim.  If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}

if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
  log('no judgement archive was written; run again to resume')
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  traces: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  closingReport: report ? report.summary : null,
  written: report ? report.written : [],
}
