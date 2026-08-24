// Ticket 16, start to finish, with no human in the loop.
//
// Every CRT function in FDPS.LE is one measurement of which Watcom release the
// executable was linked against.  This runs that measurement once per function,
// one agent per function, then folds the results into a single version
// statement and writes the knowledge base.
//
// Three properties are load-bearing:
//
//   One function per agent.  The worklist lives in this script and in
//   pending.py; every agent() call carries exactly one address and no judging
//   agent ever sees the list (ADR-0002).  The convergence stage is different in
//   kind: it decides nothing about any single function, it only intersects what
//   the per-function verdicts already say.
//
//   Bounded context.  The sweep has already done the byte comparison and left a
//   packet per function; the agent reads that one packet, writes its full
//   judgement to verdicts/<addr>.json, and returns about 200 bytes.  Neither
//   this script nor the convergence stage grows with the number of functions.
//
//   The gate is on the evidence, not on Ghidra.  This ticket writes nothing
//   into the program, so there is no orphan-code or bookmark gate to run.  What
//   check_verdicts.py enforces instead is that a verdict names what was
//   compared -- a bare conclusion is a gate failure.
//
// args: {
//   roundSize:        number   functions judged per round (default 12)
//   maxFunctions:     number   hard cap on functions judged this run (default 500)
//   maxRescanPasses:  number   how many times to re-read unsettled verdicts (default 2)
//   report:           boolean  set false to skip the knowledge base stage
// }

export const meta = {
  name: 'crt-version-ticket16',
  description: 'Compare every confirmed CRT function against every Watcom release and converge on one version',
  phases: [
    { title: 'Plan', detail: 'refresh the worklist and read what is still to do' },
    { title: 'Judge', detail: 'one agent per CRT function, one evidence packet each' },
    { title: 'Gate', detail: 'every verdict must show what it compared' },
    { title: 'Rescan', detail: 're-read the discriminating and the unsettled ones' },
    { title: 'Converge', detail: 'intersect the verdicts into one version statement' },
    { title: 'Report', detail: 'knowledge base and devlog' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOLS = REPO + '\\tools\\crt_version'
const WS = REPO + '\\workspace\\crt_version'
const PACKETS = WS + '\\packets'
const VERDICTS = WS + '\\verdicts'

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['todo', 'total', 'done', 'ok'],
  properties: {
    todo: { type: 'array', items: { type: 'string' }, description: 'Addresses with no usable verdict yet' },
    total: { type: 'integer', description: 'Packets on disk' },
    done: { type: 'integer', description: 'Verdicts already usable' },
    ok: { type: 'boolean', description: 'False if the tooling could not be run at all' },
    note: { type: 'string' },
  },
}

const VERDICT = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'basis', 'power', 'confidence', 'compatible_count', 'splits_10_0_family',
             'wrote_file', 'has_open_question'],
  properties: {
    addr: { type: 'string', description: '8-hex address, exactly as given' },
    basis: { type: 'string', enum: ['byte_match', 'ambiguous_match', 'no_match', 'too_short', 'not_comparable'] },
    power: { type: 'string', enum: ['discriminating', 'family_only', 'none'] },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    compatible_count: { type: 'integer', description: 'How many releases the function is compatible with' },
    splits_10_0_family: {
      type: 'boolean',
      description: 'True when compatible includes at least one and excludes at least one of '
        + '10.0, 10.0a, 10.0a_infobase, 10.0b. These are the verdicts the ticket turns on.',
    },
    wrote_file: { type: 'boolean', description: 'True once verdicts/<addr>.json exists' },
    has_open_question: { type: 'boolean', description: 'True when open_questions is non-empty' },
    upstream_dead: { type: 'boolean', description: 'Set only if the packet directory or Python is unusable' },
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
    failing: { type: 'array', items: { type: 'string' }, description: 'Addresses that failed the gate' },
    missing: { type: 'array', items: { type: 'string' }, description: 'Addresses with no verdict file' },
    gate_passed: { type: 'boolean' },
    problems: { type: 'string', description: 'What the gate reported, condensed, or empty' },
  },
}

const RESCAN = {
  type: 'object',
  additionalProperties: false,
  required: ['addr', 'changed', 'confidence', 'still_open'],
  properties: {
    addr: { type: 'string' },
    changed: { type: 'boolean' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    still_open: { type: 'boolean' },
    what_resolved: { type: 'string' },
  },
}

const CONVERGE = {
  type: 'object',
  additionalProperties: false,
  required: ['version', 'intersection', 'agrees_with_previous', 'summary'],
  properties: {
    version: { type: 'string', description: 'The single release concluded, or "" if none survives' },
    intersection: { type: 'array', items: { type: 'string' } },
    agrees_with_previous: { type: 'boolean', description: 'Whether this matches the standing 10.0a verdict' },
    excluded_10_0: { type: 'string', description: 'Which function excludes 10.0, or empty' },
    excluded_10_0b: { type: 'string', description: 'Which function excludes 10.0b, or empty' },
    contradictions: { type: 'string', description: 'Any function whose verdict fights the rest, or empty' },
    summary: { type: 'string' },
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

const BACKGROUND = `Background you need, and nothing more:

FDPS.LE is a 1997 DOS game executable built with Watcom C/C++ for DOS/4G.  Its
runtime code came out of the release's static libraries -- CLIB3S, MATH387S,
EMU387 -- so a runtime function in the image is a copy of a library module, with
the linker's patches applied.

Twelve candidate installs are on this machine and every one of their library modules
has been reassembled from its .obj records, together with a mask of the byte
positions that release's own FIXUPP records say the linker patches.  The sweep
(tools/crt_version/sweep_versions.py) has already compared your function against
every module of every release, ignoring exactly those masked bytes.  Its result
is in your packet.  You are reading that result, not producing it.

Two of the twelve labels are the same release: 10.0a and 10.0a_infobase are two
installs of Watcom 10.0a.  Their compiler, MATH387S, EMU387, GRAPH and startup
object are byte-identical; only their CLIB3S.LIB differ, and the copy under
WATCOM_10.0a is missing the stk386 module that the other has.  So a function
compatible with 10.0a_infobase but not 10.0a is telling you about a damaged
library file on this machine, not about the release.  Say so when you see it.`

function judgePrompt(addr) {
  return `You are deciding, for ONE function in FDPS.LE, which Watcom releases could have
supplied it.

Your function: ${addr}
Its evidence packet: ${PACKETS}\\${addr}.json

This is the only function you work on.  Never judge another one.

${BACKGROUND}

Read the packet first, in full.  Its fields:

  name, body_size, dump_len       what Ghidra has for this function
  contiguous, ranges              false means the body has holes; only the range
                                  holding the entry point was compared
  masked_bytes, comparable_bytes  how many bytes the comparison could use
  anchors                         the unmasked byte runs used to find candidates
  listing                         the disassembly, one instruction per line
  versions.<release>.match        true when some module of that release matched
  versions.<release>.hits         lib, module, segment, offset and the module's
                                  own public symbol at that offset
  versions.<release>.nearest      best near miss when nothing matched exactly
  compatible_versions             the releases whose match is true

What you decide, and how:

1. Is the comparison meaningful at all?  comparable_bytes is the whole of the
   evidence.  A five byte JMP thunk with four relocated bytes has one byte to
   compare and identifies nothing; a 500 byte body with 450 comparable bytes
   identifies a module beyond doubt.  Somewhere between those, judgement is
   needed -- that is why this is a per-function decision and not a script.
   Set basis to too_short when there is not enough to say anything, and say in
   the evidence how many bytes there were.

2. Does the match land somewhere sensible?  A hit whose module and public
   symbol agree with the function's name in Ghidra is worth far more than a hit
   in an unrelated module at a strange offset.  Several hits in one release
   usually means a short body that appears in many modules; note it.

3. Does a non-match mean the release is excluded?  It means the release has no
   module containing this code.  That is real evidence when the body is long
   and the other releases matched cleanly.  It is not evidence when nothing
   matched anywhere -- then the comparison itself failed, and basis is no_match
   with an empty compatible list.  A body flagged contiguous:false was compared
   only up to its first hole, so treat its result with care and say so.

4. Look at the listing when the numbers are ambiguous.  Hand-written assembly,
   a jump table, or a body that is mostly relocated pointers all explain a weak
   result.  You may also read the function in Ghidra if you need to, READ-ONLY:
   never rename, never set a prototype, never set a comment.  This ticket writes
   nothing into the program.  Keep to about 10 tool calls in total.

Then write the verdict file with the Write tool, exactly:

  ${VERDICTS}\\${addr}.json

{
  "address": "${addr}",
  "name": "<the packet's name field, verbatim>",
  "library": "<CLIB3S | MATH387S | EMU387 | GRAPH | STARTUP_... | empty>",
  "module": "<library module the match landed in, or empty>",
  "public_symbol": "<the module's public symbol at the match offset, or empty>",
  "dump_len": <the packet's dump_len, verbatim>,
  "sweep_compatible": <the packet's compatible_versions array, verbatim>,
  "comparable_bytes": <the packet's comparable_bytes, verbatim>,
  "compatible": ["<release>", ...],
  "excluded": ["<release>", ...],
  "basis": "byte_match" | "ambiguous_match" | "no_match" | "too_short" | "not_comparable",
  "power": "discriminating" | "family_only" | "none",
  "evidence": "<English. What was compared and what came of it. Name the module, the offset, the byte counts, and for anything you excluded, why the miss counts.>",
  "confidence": "high" | "medium" | "low",
  "open_questions": ["<anything unsettled>"]
}

Rules for those fields, all enforced by the gate:

  compatible and excluded together must list all twelve labels exactly once,
  with none in both: 9.5, 9.5a, 9.5b, 9.5c, 10.0, 10.0a, 10.0a_infobase, 10.0b,
  10.5, 10.5a, 10.6, 10.6a.  They are the keys of the packet's versions object;
  use every one of them and nothing else.

  dump_len, sweep_compatible and comparable_bytes are copied from the packet
  unchanged.  They are how a later run tells a fresh verdict from one written
  against an older sweep.

  basis byte_match means the match pins down one library module, and then module
  must name it.  A short generic body -- a one argument thunk, a two instruction
  stub -- often matches cleanly in fifteen or twenty different modules of the
  same release; that is basis ambiguous_match, with the number of hits per
  release stated in the evidence.  The release sets are still worth recording in
  that case: which releases contain the shape at all is real information even
  when which module is not.

  ambiguous_match is about what the bytes settle, not about what you know.  If
  the module is identified some other way -- most often by placement, because
  the neighbouring functions were transplanted from the same module and their
  offsets still line up -- put it in module and say in the evidence that the
  identification comes from the layout rather than from these bytes.

  power is discriminating when compatible is a proper non-empty subset of the
  releases, family_only when it is compatible with every release, and none when
  compatible is empty.

  evidence must show your working.  "Matches 10.0a" is a gate failure.  At least
  one number has to appear in it.  If you excluded a release, the reason has to
  be there too.

  Do not force a conclusion.  A function that identifies nothing is a correct
  and useful outcome; there are hundreds of others.  Never widen or narrow the
  compatible set beyond what the packet supports to make a tidier answer.

Then return the summary.  Your final output is data for the workflow, not a
message to a human.`
}

function gatePrompt(tag, addrs) {
  return `Run the ticket-16 verdict gate over one round.

Round tag: ${tag}
Addresses (${addrs.length}): ${addrs.join(' ')}

Run exactly:

  python ${TOOLS}\\check_verdicts.py --addresses ${addrs.join(' ')} --json

It prints a JSON object with checked, ok, missing, failures and gate_passed.
Report those numbers back.  Put every address under "failures" into failing and
every one under "missing" into missing.

Condense the failure reasons into problems -- one short line per distinct kind
of problem, not one per address.

Do not fix anything.  Do not edit any verdict file.  The workflow re-runs the
judging agent for whatever you report; a repair made here would hide which
function's reading was wrong.  If the command itself cannot run at all -- Python
missing, script missing -- say that in problems and set gate_passed false.`
}

function rescanPrompt(addr, why) {
  return `An earlier pass judged ONE function in FDPS.LE against the installed Watcom
releases and left something unsettled, or reached a conclusion strong enough to
be worth a second reading.  You are re-reading that same function.

Your function: ${addr}
Why it came back: ${why}

${BACKGROUND}

Read, in this order:

  ${VERDICTS}\\${addr}.json    what was concluded and what was left open
  ${PACKETS}\\${addr}.json     the comparison it was based on
  ${WS}\\aggregate.json        what every other verdict adds up to

The reason this pass can do better than the first is the third file.  The first
pass saw one function with no idea whether its answer was normal or strange.
You can see the whole distribution: how many functions are compatible with each
release, which releases anything at all excludes, and which functions carry the
exclusions.  A lone function excluding a release that four hundred others accept
is either the best evidence in the ticket or a mistake, and now you can tell
which by checking whether its match is clean and its module the expected one.

You may read other verdict files as evidence.  That is using a judgement someone
else already made, not making one for them.  Never rewrite another function's
verdict file and never form an opinion about whether another function's reading
is right -- if one looks wrong, say so in what_resolved and leave it.

READ-ONLY on Ghidra throughout.

If the second reading changes the answer, rewrite ${VERDICTS}\\${addr}.json with
the Write tool, keeping exactly the same field shape, and add a "_supersedes"
field saying in one short paragraph what the first reading concluded and what
changed it.  Set changed to true.

If it does not, leave every judgement field exactly as it is and add one field,
"_reread", holding a single line saying you confirmed it and what you checked.
Set changed to false.  That field is bookkeeping, not a judgement: it is how a
later run knows this verdict has already had its second reading and does not
spend another agent on it.  A second look that confirms the first is a real
result.  **The second attempt is not a licence to manufacture a conclusion.**
An honest unresolved verdict stays unresolved.`
}

// ------------------------------------------------------------------- plan

const ROUND_SIZE = (args && args.roundSize) || 12
const MAX_FUNCTIONS = (args && args.maxFunctions) || 500
const MAX_RESCAN_PASSES = (args && args.maxRescanPasses) || 2

phase('Plan')
const plan = await agent(
  `Read the ticket-16 worklist.  You decide nothing; you only report what is left.

Run exactly:

  python ${TOOLS}\\pending.py --all

It prints one JSON object per CRT function with an address and a state:
"missing" (no verdict yet), "failing" (a verdict that does not pass the gate),
"stale" (a verdict written against an older sweep) or "done".

Return todo = every address whose state is not "done", in the order printed;
total = how many objects there were; done = how many are "done".

If the command cannot run at all, set ok to false and say why in note.  Do not
try to repair anything.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)

if (!plan || !plan.ok) {
  return {
    stopped: 'the planning agent could not read the worklist',
    note: plan ? plan.note : 'no response',
  }
}
log(`worklist: ${plan.total} functions, ${plan.done} already settled, ${plan.todo.length} to judge`)

// ------------------------------------------------------------------ judge

const todo = plan.todo.slice(0, MAX_FUNCTIONS)
const deferred = plan.todo.slice(MAX_FUNCTIONS)
if (deferred.length > 0) {
  log(`budget reached: ${deferred.length} function(s) not judged this run`)
}

const judged = []
const unfinished = []
const gateReports = []
let stopped = null

for (let start = 0; start < todo.length && !stopped; start += ROUND_SIZE) {
  const round = Math.floor(start / ROUND_SIZE) + 1
  const batch = todo.slice(start, start + ROUND_SIZE)

  phase('Judge')
  log(`round ${round}: judging ${batch.length} function(s)`)
  let results = (await parallel(batch.map((addr) => () =>
    agent(judgePrompt(addr), { label: `judge:${addr}`, phase: 'Judge', schema: VERDICT })
  ))).filter(Boolean)

  // 5.2 -- when nothing at all comes back the cause is outside this workflow,
  // and retrying just burns the rest of the budget on doomed calls.
  if (results.length === 0 && batch.length > 0) {
    stopped = `round ${round} returned nothing at all; treating it as an upstream failure`
    log(`STOP: ${stopped}`)
    unfinished.push(...todo.slice(start))
    break
  }
  if (results.some((r) => r.upstream_dead)) {
    stopped = `an agent in round ${round} reported the packet directory or Python unusable`
    log(`STOP: ${stopped}`)
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
  log(`round ${round}: gate ok=${gate.ok_count} failing=${gate.failing.length} `
    + `missing=${gate.missing.length}`)

  // 5.1 -- one retry per item, then it is recorded as unfinished, never as done.
  const retry = [].concat(gate.failing || [], gate.missing || [])
  if (retry.length > 0) {
    log(`round ${round}: retrying ${retry.length} function(s): ${retry.join(' ')}`)
    if (gate.problems) {
      log(`round ${round}: gate said: ${gate.problems}`)
    }
    const second = (await parallel(retry.map((addr) => () =>
      agent(judgePrompt(addr), { label: `retry:${addr}`, phase: 'Judge', schema: VERDICT })
    ))).filter(Boolean)
    results = results.filter((r) => !retry.includes(r.addr)).concat(second)

    const regate = await agent(gatePrompt(`r${round}b`, retry), {
      label: `gate:round${round}b`, phase: 'Gate', schema: GATE,
    })
    if (regate) {
      gateReports.push(Object.assign({ round: round + 'b' }, regate))
      const stillBad = [].concat(regate.failing || [], regate.missing || [])
      if (stillBad.length > 0) {
        log(`round ${round}: UNFINISHED after retry: ${stillBad.join(' ')}`)
        unfinished.push(...stillBad)
        results = results.filter((r) => !stillBad.includes(r.addr))
      }
    } else {
      log(`round ${round}: the re-gate returned nothing; treating the retried set as unfinished`)
      unfinished.push(...retry)
      results = results.filter((r) => !retry.includes(r.addr))
    }
  }

  judged.push(...results)
  log(`round ${round}: ${judged.length}/${todo.length} settled so far`)
}

// ----------------------------------------------------------------- rescan
//
// A judging agent sees one packet and cannot tell an exceptional result from an
// ordinary one, so anything the conclusion leans on gets read twice.
//
// "Leans on" is narrower than "discriminating". Almost every function that
// matches anything at all excludes the 9.5 family, and re-reading four hundred
// verdicts that agree with each other buys nothing. What the ticket turns on is
// the far smaller set that splits the 10.0 family -- those decide between 10.0,
// 10.0a and 10.0b -- plus whatever any agent left unsettled.

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= MAX_RESCAN_PASSES; pass++) {
    // The list comes from the verdict files, not from this run's results, so a
    // rescan cut short by an API outage resumes instead of being lost, and
    // verdicts judged by an earlier run still get their second reading.
    const plan2 = await agent(
      `Read the ticket-16 rescan worklist.  You decide nothing.

Run exactly:

  python ${TOOLS}\\rescan_list.py

It prints a JSON array of {address, why} for every verdict that carries weight
or carries doubt and has not had a second reading yet.  Return todo = those
addresses in the order printed, total = how many there were, done = 0.

If the command cannot run at all, set ok false and say why in note.`,
      { label: `rescanplan:pass${pass}`, phase: 'Plan', schema: PLAN }
    )
    if (!plan2 || !plan2.ok) {
      log(`rescan pass ${pass}: could not read the worklist, skipping the rescan`)
      break
    }
    const pending = plan2.todo.map((addr) => {
      const known = judged.find((j) => j.addr === addr)
      return known || { addr: addr, splits_10_0_family: false,
                        confidence: 'high', has_open_question: false }
    })
    if (pending.length === 0) {
      log(`rescan pass ${pass}: nothing to re-read`)
      break
    }
    phase('Converge')
    const agg = await agent(
      `Refresh the aggregate the rescan agents read.  Run exactly:

  python ${TOOLS}\\aggregate.py

Then return, in summary, the intersection line and the per-release counts it
printed.  Decide nothing.`,
      { label: `aggregate:pass${pass}`, phase: 'Converge', schema: DONE }
    )
    if (agg) {
      log(`aggregate before rescan pass ${pass}: ${agg.summary}`)
    }

    phase('Rescan')
    log(`rescan pass ${pass}: re-reading ${pending.length} verdict(s)`)
    const results = (await parallel(pending.map((r) => () =>
      agent(
        rescanPrompt(r.addr, r.splits_10_0_family
          ? `it splits the 10.0 family, so the ticket's conclusion leans on it`
          : r.confidence && r.confidence !== 'high' ? `confidence was ${r.confidence}`
          : r.has_open_question ? 'an open question was recorded'
          : 'rescan_list.py selected it -- your verdict either splits the 10.0 family, '
            + 'is not high confidence, or has an open question; read it to see which'),
        { label: `rescan:${r.addr}`, phase: 'Rescan', schema: RESCAN }
      )
    ))).filter(Boolean)

    if (results.length === 0 && pending.length > 0) {
      stopped = `rescan pass ${pass} returned nothing at all; treating it as an upstream failure`
      log(`STOP: ${stopped}`)
      break
    }

    for (const res of results) {
      const r = judged.find((j) => j.addr === res.addr)
      if (!r) {
        continue
      }
      r.confidence = res.confidence
      r.has_open_question = res.still_open
      if (res.changed) {
        rescanLog.push(`${res.addr} (${res.confidence}): ${res.what_resolved}`)
        log(`  ${res.addr} changed (${res.confidence}): ${res.what_resolved}`)
      }
    }

    const changed = results.filter((r) => r.changed)
    phase('Gate')
    const gate = await agent(gatePrompt(`rescan${pass}`, pending.map((p) => p.addr)), {
      label: `gate:rescan${pass}`, phase: 'Gate', schema: GATE,
    })
    if (gate) {
      gateReports.push(Object.assign({ round: 'rescan' + pass }, gate))
      log(`rescan pass ${pass}: gate ok=${gate.ok_count} failing=${gate.failing.length}`)
      const bad = [].concat(gate.failing || [], gate.missing || [])
      if (bad.length > 0) {
        log(`rescan pass ${pass}: verdicts now failing the gate: ${bad.join(' ')}`)
        unfinished.push(...bad.filter((a) => !unfinished.includes(a)))
      }
    }
    if (changed.length === 0) {
      log(`rescan pass ${pass}: nothing changed, stopping`)
      break
    }
  }
}

// --------------------------------------------------------------- converge

let conclusion = null
if (!stopped) {
  phase('Converge')
  conclusion = await agent(
    `Fold the ticket-16 verdicts into one statement about which Watcom release
FDPS.LE was built with.  This is the one stage that is allowed to reason across
functions -- and it still judges no individual function; it only reads what the
per-function verdicts already concluded.

1. Run:  python ${TOOLS}\\aggregate.py
   Read ${WS}\\aggregate.json for the detail.

2. The answer is the intersection of every informative verdict's compatible set.
   Check it against the standing conclusion in
   ${REPO}\\rebuild_info\\build_flags.md, which says Watcom 10.0a with 10.0b not
   excludable.  Whether this run agrees or not, say so plainly.

3. 10.0a and 10.0a_infobase are two installs of the same release.  If the
   intersection separates them, that is a statement about a damaged library file
   on this machine, not about the release; resolve it and explain.

4. If the intersection is empty, do not pick a winner.  Find which verdicts
   conflict, report them by address, and say the conclusion does not converge.
   An empty intersection is a finding, not a failure to be smoothed over.

5. Name the specific functions that carry the exclusions: which one excludes
   10.0, which one excludes 10.0b, how many comparable bytes each rests on, and
   which library module each is.  A conclusion resting on one 5 byte function is
   worth saying out loud.

Write your reasoning to ${WS}\\conclusion.md in English, with the numbers, and
return the summary.  Do not write into the knowledge base -- a later stage does
that.`,
    { label: 'converge:version', phase: 'Converge', schema: CONVERGE }
  )
  if (conclusion) {
    log(`converged on: ${conclusion.version || '(no single release)'} `
      + `[${conclusion.intersection.join(', ')}]`)
    log(`agrees with the standing verdict: ${conclusion.agrees_with_previous}`)
    if (conclusion.contradictions) {
      log(`contradictions: ${conclusion.contradictions}`)
    }
  }
}

// ----------------------------------------------------------------- report
//
// 5.6 -- nothing that looks like a finished deliverable is produced after a
// stop. The run report below is still produced, with the reason and the list.

const stats = {
  totalFunctions: plan.total,
  alreadySettled: plan.done,
  judgedThisRun: judged.length,
  deferredForBudget: deferred,
  unfinished: unfinished,
  stopped: stopped,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  conclusion: conclusion,
}

let docs = []
const WRITE_REPORT = !(args && args.report === false)
if (!stopped && WRITE_REPORT && conclusion) {
  phase('Report')
  docs = (await parallel([
    () => agent(
      `Write the ticket-16 conclusion into the knowledge base.

Sources:
  ${WS}\\conclusion.md                        the convergence stage's reasoning
  ${WS}\\aggregate.json                       counts, exclusions and which function carries each
  ${WS}\\compiler_diff\\report.json            same corpus compiled by each release's own toolchain
  ${WS}\\compiler_diff\\report_h10.0a.json     the same, with every release pinned to 10.0a's headers
  ${VERDICTS}\\                               per-function verdicts; open the discriminating ones only

Read ${REPO}\\rebuild_info\\_index.md and ${REPO}\\rebuild_info\\build_flags.md
first, and follow the house style of that folder exactly.

What to change, in TRADITIONAL CHINESE:

1. ${REPO}\\rebuild_info\\build_flags.md, the 工具鏈 section.  It currently says
   Watcom 10.0a with "10.0b 無法排除" and rests the 10.0-family claim on four
   pieces of evidence, the weakest being two floating point routines.  Replace
   the version reasoning with what this ticket measured: how many CRT functions
   were compared against how many releases, what the intersection is, and which
   functions carry each exclusion by address, module and comparable byte count.
   Keep it conclusion-style -- no narrative, no "originally we thought", no
   dates, no phases.

2. In the same file, answer whether the compiler version follows from the
   library version.  The measurement is in the two compiler_diff reports: the
   same corpus compiled by 10.0, 10.0a and 10.0b.  State plainly what it shows
   and what therefore remains an inference rather than a measurement.  If it
   cannot be verified from the binary, say that it cannot, and say what it costs
   in practice.

3. ${REPO}\\rebuild_info\\pitfalls.md: add whatever this ticket found that a
   rebuild would get wrong by following intuition.  Judge that against the
   folder's own threshold, which is in ${REPO}\\rebuild_info\\_index.md -- do not
   add anything that is merely interesting.

4. ${REPO}\\tools\\crt_version\\_index.md: a new index page for this tool
   directory, in the style of ${REPO}\\tools\\pool_triage\\fid\\_index.md, and a
   row for it in ${REPO}\\tools\\_index.md.

5. If ${REPO}\\program_info\\code_pools.md states anything about CRT versions
   that this ticket has now settled differently, correct it.  Do not duplicate
   the version facts there; build_flags.md owns them.

Every address as 8 hex digits in backticks.  Do not duplicate a fact that
another page already owns -- link instead.  Return the list of files you wrote.`,
      { label: 'doc:knowledge', phase: 'Report', schema: DONE }
    ),
    () => agent(
      `Write the devlog entry for this run.

Read ${REPO}\\devlog\\_conventions.md first and follow it exactly: narrative,
rambling allowed, and the point is the dead ends -- the successful path ends up
in the knowledge base, the failed ones are the only thing that would otherwise
be lost.

Write ${REPO}\\devlog\\2026-08-24-crt-version-hardening.md in TRADITIONAL
CHINESE.

What this run was: ticket 16.  Every function tagged pool_crt in FDPS.LE was
compared against every installed Watcom release's libraries, one agent per
function, each agent reading a pre-computed evidence packet and writing its
verdict to a file.  The comparison ignores each library module's own FIXUPP
fields so that only bytes the linker did not patch count.

Sources you may read:
  ${WS}\\conclusion.md
  ${WS}\\aggregate.json
  ${WS}\\compiler_diff\\report.json and report_h10.0a.json
  ${VERDICTS}\\        open a few, especially the discriminating ones

Run statistics:
${JSON.stringify(stats, null, 2)}

Cover honestly: what the sweep had to be corrected for before it gave sane
answers, the damaged 10.0a CLIB3S.LIB on this machine and how it first showed up
as a contradiction, how many functions turned out to carry no information at
all, and anything the rescan overturned.  If the run went cleanly, say so
briefly rather than padding.  Do not claim anything the statistics do not
support.

Return the list of files you wrote.`,
      { label: 'doc:devlog', phase: 'Report', schema: DONE }
    ),
  ])).filter(Boolean)
}

if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
  log(`no knowledge base or devlog was written; ${unfinished.length} function(s) left unfinished`)
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  totalFunctions: plan.total,
  alreadySettled: plan.done,
  judgedThisRun: judged.length,
  unfinished: unfinished,
  deferredForBudget: deferred,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  conclusion: conclusion,
  documents: docs.map((d) => d.written).flat(),
}
