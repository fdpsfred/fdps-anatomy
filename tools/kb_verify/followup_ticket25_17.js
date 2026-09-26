// Ticket 25.17, stage 5: the open questions the four passes left behind.
//
// Besides the fixes, the closing reports list things a verifier noticed but
// could not settle inside its own item: a claim in a page it was not
// verifying, a comment outside its group, a count that needs re-measuring.
// followups_25_17.json writes each down as a request; kbfollowup.py runs them
// through kboutside.py's machinery (grouping, gate, second reading, landing)
// under workspace/kb_followup/.  Same properties as stage 4: one group per
// agent, verdicts in files, judging agents write nothing else, the Land phase
// applies what a second reader confirmed and runs every gate including the
// full build gate; a rerun resumes.
//
// args: {
//   date:       "YYYY-MM-DD"  required; names the devlog/runs files
//   roundSize:  number        groups judged per round (default 8)
// }

export const meta = {
  name: 'kb-followup-ticket25-17',
  description: 'Settle and land the open questions the verification passes left behind, one agent per group',
  phases: [
    { title: 'Plan', detail: 'group the follow-ups, read which still need a verdict' },
    { title: 'Judge', detail: 'one agent per group, verdict with exact-string edits written to a file' },
    { title: 'Gate', detail: 'verdict shape, evidence, comment-only C edits, allowed files and regenerations' },
    { title: 'Rescan', detail: 'a second agent confirms, amends or rejects every change' },
    { title: 'Land', detail: 'apply, regenerate, knowledge-base gates, touched unit tests, full build gate' },
    { title: 'Report', detail: 'closing report and verdict archive in devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOL = REPO + '\\tools\\kb_verify\\kbfollowup.py'
const RUN = 'python ' + TOOL
const VERDICTS = REPO + '\\workspace\\kb_followup\\verdicts'
const SCRATCH = REPO + '\\workspace\\kb_followup\\scratch'

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['ids', 'todo', 'ok'],
  properties: {
    ids: { type: 'array', items: { type: 'string' } },
    todo: { type: 'array', items: { type: 'string' } },
    ok: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'edits', 'ghidra', 'wrote_file', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    edits: { type: 'integer' },
    ghidra: { type: 'integer' },
    wrote_file: { type: 'boolean' },
    upstream_dead: { type: 'boolean' },
    note: { type: 'string' },
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
    problems: { type: 'string' },
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
  required: ['id', 'decision', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    decision: { type: 'string', enum: ['confirm', 'amend', 'reject'] },
    upstream_dead: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const LAND = {
  type: 'object',
  additionalProperties: false,
  required: ['applied', 'refused', 'regenerated', 'gates', 'all_passed'],
  properties: {
    applied: { type: 'integer' },
    refused: { type: 'array', items: { type: 'string' }, description: 'One line per refused entry: id: why' },
    regenerated: { type: 'array', items: { type: 'string' }, description: 'One line per regeneration: command -> exit code' },
    gates: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['command', 'passed', 'detail'],
        properties: {
          command: { type: 'string' },
          passed: { type: 'boolean' },
          detail: { type: 'string', description: 'Its verdict line, or the failure lines condensed' },
        },
      },
    },
    all_passed: { type: 'boolean' },
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

const KB_GATES = [
  'python ' + REPO + '\\tools\\chapter_docs\\check_chapter.py --landed-all',
  'python ' + REPO + '\\tools\\chapter_docs\\index.py verify',
  'python ' + REPO + '\\tools\\data_tables\\data_tables.py check',
  'python ' + REPO + '\\tools\\global_text\\global_text.py verify',
  'python ' + REPO + '\\tools\\cut_content\\cut_content.py check',
  'python ' + REPO + '\\tools\\cut_content\\story.py check --final',
  'python ' + REPO + '\\tools\\data_skill\\build.py',
  'python ' + REPO + '\\tools\\kb_verify\\kbverify.py indexes',
]
const BUILD_GATE = 'python ' + REPO + '\\tools\\build_gate\\gate.py check'

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\ -- the complete rebuilt C source of FDPS.LE.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE (load tools with one ToolSearch call,
     e.g. "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__get_plate_comment,mcp__ghidra__read_memory").
     Never call a Ghidra tool that writes.
  3. The game's data: ${REPO}\\fdps_game_files\\ and ${REPO}\\workspace\\vfs_dump\\,
     through the decoders under ${REPO}\\tools\\ (each tool's _index.md says how).
     Cite FILE@offset, or the command you ran (source "tool").
  4. Corroboration: the knowledge base (source "kb"), the FD2 project
     C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\ for anything about FD2.`

const VERDICT_SHAPE = `{
  "id": "<the item id>",
  "item_sha1": "<the item_sha1 printed by show, verbatim>",
  "members": [ { "ref": "<each fix's ref, e.g. V:program_info__dialog#5 or C:X129>",
                 "status": "done_already" | "fixed" | "declined" | "developer",
                 "note": "<繁體中文，一句：為什麼>" } ],
  "edits": [ { "file": "<repo-relative path>", "old": "<exact text from that file, unique in it>",
               "new": "<replacement>", "why": "<which refs it settles>" } ],
  "ghidra": [ { "address": "0x...", "change": "plate_append" | "plate_replace" | "rename_param" | "rename_symbol" | "comment",
                "detail": "<exactly what to write or rename, in English>" } ],
  "regenerate": [ "global_text" | "story" | "cut_content_index" | "chapters_index" | "data_skill" | "chapter_refill:N[,N]" ],
  "conclusion": "<繁體中文：這一組定案了什麼>",
  "evidence": [ { "source": "src" | "ghidra" | "data" | "tool" | "kb" | "fd2", "location": "...", "observation": "..." } ],
  "confidence": "high" | "medium" | "low",
  "open_question": "<else empty>",
  "developer_question": "<a decision only the developer can make, else empty>"
}`

function judgePrompt(id) {
  return `You are settling ONE group of fixes in the FDPS reverse-engineering project
(炎龍騎士團外傳, a 1997 DOS game whose executable FDPS.LE has been fully rebuilt
into C).  While verifying the knowledge base, agents noticed problems they could not
settle inside their own item -- a claim in another page, a comment outside
their group, a count that needs re-measuring -- and the ticket wrote each
down as a request.  They may concern pages, src/ or tests/ comments, tools/,
or Ghidra.  When a request turns out to need the developer (a playtest, a
debugger, a decision), say so in developer_question with status developer.  The group holds every request that names the same
files, so its edits can be written once and coherently.

Your item: ${id}

Get it by running exactly:

  ${RUN} show ${id}

Work on this group only; other agents settle other groups at the same time.

For each request, in order:

  1. Check whether it still applies: the knowledge base has since been
     corrected by three passes, and another request in your group (or the
     page itself) may already have done it.  If so: status done_already.
  2. Check that it is TRUE, against first-hand sources.  A request is a claim
     by one agent, not an instruction.  If it is wrong, status declined with
     the reason.
  3. If it applies and is true, write the edit(s): status fixed.

Rules for the edits:

  - Exact-string replacements: old copied from the file as it is now,
    occurring exactly once in it.  Several requests on one sentence become one
    edit.
  - src/ and tests/ C source: comments only.  A renamed macro or identifier is
    allowed only when every use is renamed in the same edits and nothing else
    in the code changes -- the landing checks the whole file token for token,
    and the full build gate then proves the image unchanged.  Never change
    what the code does.  Comments in English, in the file's existing style.
  - A generated page (assets/text/*.md, chapter pages' <!-- chapter_docs --> and
    cut_content/story.md's <!-- story --> blocks, cut_content/_index.md's summary
    table) is fixed in its generator or hand data (tools/global_text/global_text.py,
    tools/chapter_docs/judgements/chNN.json, tools/cut_content/story.py, the
    topic pages' headings) and then regenerated: name it in regenerate
    (global_text, story, chapter_refill:N, chapters_index, cut_content_index,
    data_skill).  Keep a changed judgement or OWNERS consistent with the
    chapter's and cut_content's own gates.
  - Knowledge-base pages: conclusion-style Traditional Chinese, no process
    narrative, no src/ line numbers.
  - Not edited by this pass: docs/adr/ (a decision record says what was
    decided and why at the time -- status declined, or developer if you think
    the developer should amend it), devlog/, README.md, workspace/.  A ticket
    file under .scratch/ may be edited only to correct a statement of fact,
    never to change its status or scope on an agent's word -- that is
    status developer.
  - Ghidra: list the change in ghidra (address, kind, exact English text),
    never call a Ghidra tool that writes.  The ticket session makes them.

${SOURCES}

Scratch files go under ${SCRATCH}\\${id}\\ and nowhere else.  Do not modify any
file in the repository other than your verdict file.

WRITE YOUR VERDICT with the Write tool to exactly:

  ${VERDICTS}\\${id}.json

UTF-8 JSON of this shape (one members entry for every ref in the group):

${VERDICT_SHAPE}

The gate (run it yourself before you finish and fix what it reports):

  ${RUN} check --ids ${id}

Set upstream_dead only if Ghidra stops answering or python / the repo is
unusable.  Then return the summary; your final output is data for the
workflow, not a message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.17 follow-up gate over one round.

Round tag: ${tag}
Items (${ids.length}): ${ids.join(' ')}

Run exactly:

  ${RUN} check --ids ${ids.join(' ')} --json

Report checked, ok (as ok_count), every id under "failures" in failing, every
id under "missing" in missing, and gate_passed; condense the failure reasons
into problems.  Do not fix anything.  If the command cannot run, say so and set
gate_passed false.`
}

function rescanPrompt(id, why) {
  return `A first agent settled ONE group of outside fixes in the FDPS project.  You
are the second reader; none of its edits or Ghidra changes happens unless you
confirm or amend them.

Your item:   ${id}
Why:         ${why}
The item:    ${RUN} show ${id}
The verdict: ${VERDICTS}\\${id}.json

First check the requests yourself against first-hand sources, then judge the
verdict: is each status right, is every edit true, a comment-only change in C
source (or a complete, behaviour-neutral rename), in the file's style, and no
vaguer than the facts?  Are the regenerations the right ones?  Is every Ghidra
change true and exactly worded?

${SOURCES}

Add to the verdict file, with the Write tool, one field and change nothing else:

  "_reread": { "decision": "confirm" | "amend" | "reject",
               "why": "<繁體中文：你查了什麼、結論>",
               "edits": [ ... ], "ghidra": [ ... ], "regenerate": [ ... ] }   (amend only: the complete corrected lists)

Run ${RUN} check --ids ${id} afterwards and fix what it reports in your own
file.  READ-ONLY on everything else.  A second reading is not a licence to
manufacture a conclusion.  Set upstream_dead only if Ghidra stops answering or
python / the repo is unusable.`
}

const RESCAN_RETRY = `

This is a second attempt: the verdict file already has a "_reread" field that
fails the gate.  Run the check command first to see why, then rewrite the
"_reread" field whole (keep every other field as it is).`

function landPrompt() {
  return `Land the ticket-25.17 follow-up verdicts and run every gate.  You decide
nothing and you fix nothing.

Working directory: ${REPO}

1. Run exactly:  ${RUN} apply
   It prints JSON: "applied" (a count), "refused" (a list), "regenerated" (the
   regenerations it ran with their exit codes), "files", "tests_to_run" (a
   list of commands) and "src_changed".  Report applied, one line per refused
   entry ("<id>: <why>"), and one line per regeneration ("<command> -> <exit>").

2. Run each of these and record whether it exited 0 and its last line (or the
   failure lines condensed):

${KB_GATES.map((c) => '     ' + c).join('\n')}

3. Run every command listed in "tests_to_run", recording each the same way.

4. Run the full build gate, and record its verdict the same way:

     ${BUILD_GATE}

   It builds in DOSBox-X and takes a while; let it finish.  It passes when
   every target's equivalence is identical, strict, reloc or pad and every
   suite passes.

all_passed is true only if every command in steps 2-4 exited 0 and every
regeneration exited 0.  Do not edit any file, do not re-run apply, do not try to
make a failing gate pass.`
}

const DATE = args && args.date
const ROUND_SIZE = (args && args.roundSize) || 8

if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { stopped: 'args.date (YYYY-MM-DD) is required; it names the devlog/runs files' }
}

phase('Plan')
const plan = await agent(
  `Prepare the ticket-25.17 follow-up worklist.  You decide nothing.

Run exactly these two commands, in this order:

  ${RUN} freeze
  ${RUN} pending --all

The first groups the fixes the first time and keeps the groups afterwards; it
prints a JSON digest.  The second prints a JSON array of {id, state}.  Return
ids = every id in order, todo = every id whose state is not "done", in order.
If a command cannot run at all, set ok false and say why in note.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)
if (!plan || !plan.ok) {
  return { stopped: 'the planning agent could not read the worklist', note: plan ? plan.note : 'no response' }
}
const IDS = plan.ids
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} groups, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

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

  if (results.length === 0 && batch.length > 0) {
    stopped = `round ${round} returned nothing at all; treating it as an upstream failure`
    unfinished.push(...todo.slice(start))
    break
  }
  if (results.some((r) => r.upstream_dead)) {
    stopped = `an agent in round ${round} reported Ghidra or the repo unusable: `
      + results.filter((r) => r.upstream_dead).map((r) => `${r.id} ${r.note || ''}`).join('; ')
    unfinished.push(...todo.slice(start))
    break
  }

  phase('Gate')
  let gate = await agent(gatePrompt(`r${round}`, batch), { label: `gate:round${round}`, phase: 'Gate', schema: GATE })
  if (!gate) {
    gate = { checked: batch.length, ok_count: 0, failing: [], missing: batch,
             gate_passed: false, problems: 'the gate agent returned nothing' }
  }
  gateReports.push(Object.assign({ round: round }, gate))
  log(`round ${round}: gate ok=${gate.ok_count} failing=${gate.failing.length} missing=${gate.missing.length}`)

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
    const regate = await agent(gatePrompt(`r${round}b`, retry), { label: `gate:round${round}b`, phase: 'Gate', schema: GATE })
    if (regate) {
      gateReports.push(Object.assign({ round: round + 'b' }, regate))
      const stillBad = [].concat(regate.failing || [], regate.missing || [])
      if (stillBad.length > 0) {
        log(`round ${round}: UNFINISHED after retry: ${stillBad.join(' ')}`)
        unfinished.push(...stillBad)
      }
    } else {
      unfinished.push(...retry)
    }
  }
}

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= 2 && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.17 follow-up rescan worklist.  You decide nothing.

Run exactly:

  ${RUN} rescan

It prints a JSON array of {id, why}.  Return todo = that array as printed.  If
the command cannot run at all, set ok false and say why in note.`,
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
    for (let start = 0; start < pending.length && !stopped; start += ROUND_SIZE) {
      const batch = pending.slice(start, start + ROUND_SIZE)
      phase('Rescan')
      log(`rescan pass ${pass}: re-reading ${batch.map((t) => t.id).join(' ')}`)
      const results = (await parallel(batch.map((t) => () =>
        agent(rescanPrompt(t.id, t.why), { label: `rescan:${t.id}`, phase: 'Rescan', schema: RESCAN })
      ))).filter(Boolean)
      if (results.length === 0) {
        stopped = `a rescan round of pass ${pass} returned nothing at all; treating it as an upstream failure`
        break
      }
      if (results.some((r) => r.upstream_dead)) {
        stopped = `a rescan agent in pass ${pass} reported Ghidra or the repo unusable`
        break
      }
      for (const r of results) {
        rescanLog.push(`${r.id}: ${r.decision}${r.note ? ' -- ' + r.note : ''}`)
      }
      phase('Gate')
      const gate = await agent(gatePrompt(`rescan${pass}.${start}`, batch.map((t) => t.id)), {
        label: `gate:rescan${pass}.${start}`, phase: 'Gate', schema: GATE,
      })
      if (gate) {
        gateReports.push(Object.assign({ round: `rescan${pass}.${start}` }, gate))
        const bad = [].concat(gate.failing || [], gate.missing || [])
        if (bad.length > 0) {
          log(`rescan pass ${pass}: failing the gate after the second reading, retrying: ${bad.join(' ')}`)
          const again = batch.filter((t) => bad.includes(t.id))
          const second = (await parallel(again.map((t) => () =>
            agent(rescanPrompt(t.id, t.why) + RESCAN_RETRY, { label: `rescan-retry:${t.id}`, phase: 'Rescan', schema: RESCAN })
          ))).filter(Boolean)
          const regate = second.length > 0
            ? await agent(gatePrompt(`rescan${pass}.${start}b`, again.map((t) => t.id)), {
              label: `gate:rescan${pass}.${start}b`, phase: 'Gate', schema: GATE })
            : null
          const stillBad = regate ? [].concat(regate.failing || [], regate.missing || []) : bad
          if (regate) {
            gateReports.push(Object.assign({ round: `rescan${pass}.${start}b` }, regate))
          }
          if (stillBad.length > 0) {
            log(`rescan pass ${pass}: UNFINISHED after retry: ${stillBad.join(' ')}`)
            unfinished.push(...stillBad.filter((i) => !unfinished.includes(i)))
          }
        }
      } else {
        log(`rescan pass ${pass}: the gate returned nothing; its items are checked again by the report`)
      }
    }
  }
}

let landing = null
if (!stopped) {
  phase('Land')
  landing = await agent(landPrompt(), { label: 'land:apply+gates', phase: 'Land', schema: LAND })
  if (!landing) {
    log('LAND: the landing agent returned nothing; run kbfollowup.py apply and the gates by hand')
  } else {
    log(`land: ${landing.applied} edits applied, ${landing.refused.length} refused, gates ${landing.all_passed ? 'all passed' : 'FAILED'}`)
    for (const g of landing.gates.filter((x) => !x.passed)) {
      log(`  GATE FAILED: ${g.command}: ${g.detail}`)
    }
  }
}

phase('Report')
const stopArg = stopped ? ` --stopped "${stopped.replace(/[^A-Za-z0-9 .,:;()_\-]/g, ' ')}"` : ''
const report = await agent(
  `Write the ticket-25.17 follow-up closing report.  You decide nothing.

Run exactly:

  ${RUN} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-kb-followup-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-kb-followup-verdicts.json`} and prints a one-line
JSON digest.  Return written = the files it names, summary = the digest
verbatim.  If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}
if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  items: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescan: rescanLog,
  gateReports: gateReports,
  landing: landing,
  closingReport: report ? report.summary : null,
  written: report ? report.written : [],
}
