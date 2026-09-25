// Ticket 25.14: verify, one agent per item, the placements the ticket's own
// session made while writing cut_content/story.md.
//
// Writing the story page, the ticket's session placed a number of things one by
// one on its own: the entries and exclusions S14-S17, the owner of every
// never-shown text entry that ticket 25.9 had not already settled (S13b drafts,
// S15 copies), four chapter-30 lines put under S4, the headers of the S5 blocks,
// and the 16:0x00 override.  Each of those is a per-item judgement, so under
// ADR-0002 each is verified again here by an independent agent that sees only
// its own item.  A verbatim copy of an entry that is shown is settled by a
// string compare and needs no agent: "story_verify.py mechanical" writes those
// verdicts in the plan phase.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One item per agent.  The worklist is the IDS array below; every agent()
//   call carries exactly one id and reads that one claim through
//   "story_verify.py show <ID>".  No judging agent sees the list.
//
//   Verdicts go to files, agents return ~200 bytes.  The closing report is
//   built by story_verify.py from the files, so an interrupted run resumes by
//   simply being run again: an item whose verdict passes the gate is done.
//
//   Judging agents are read-only on src/, Ghidra, tools/ and the knowledge
//   base.  Changing story.md, the exclusion list or story.py's OWNERS after a
//   refuted or corrected verdict is done afterwards by the ticket's session,
//   followed by "story.py check" and "cut_content.py check".
//
// args: {
//   date:            "YYYY-MM-DD"  required; names the devlog/runs files
//   roundSize:       number        items judged per round (default 8)
//   maxRescanPasses: number        re-reading passes (default 2)
// }

export const meta = {
  name: 'story-verify-ticket25-14',
  description: 'Verify each placement ticket 25.14 made on cut_content/story.md, one agent per item',
  phases: [
    { title: 'Plan', detail: 'settle the verbatim copies mechanically, read what still needs a verdict' },
    { title: 'Judge', detail: 'one agent per item, verdict written to a file' },
    { title: 'Gate', detail: 'every verdict must carry first-hand evidence and a valid shape' },
    { title: 'Rescan', detail: 'a second agent re-reads every doubt' },
    { title: 'Report', detail: 'closing report and verdict archive in devlog/runs' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const TOOL = REPO + '\\tools\\cut_content\\story_verify.py'
const RUN = 'python ' + TOOL
const STORY_TOOL = REPO + '\\tools\\cut_content\\story.py'
const VERDICTS = REPO + '\\workspace\\story_verify\\verdicts'
const SCRATCH = REPO + '\\workspace\\story_verify\\scratch'
const CUT = REPO + '\\cut_content'

const IDS = [
  'E-S14', 'E-S16-ICON00', 'E-S16-ICON11', 'E-S16-ICON23', 'E-S16-WIN24', 'E-S17', 'E-16-00',
  'B30-0d', 'B30-0e', 'B30-13', 'B30-14',
  'H53', 'H54', 'H57', 'H58', 'H59', 'H60', 'H61',
  'B01-04', 'B01-05', 'B01-06', 'B01-0e', 'B06-0c', 'B07-0a', 'B08-09', 'B09-14',
  'B10-09', 'B10-0c', 'B10-0d', 'B10-0f', 'B18-0f', 'B19-0c', 'B20-0c', 'B21-0f',
  'B21-13', 'B23-13', 'B25-0e', 'B27-13', 'B30-1e', 'B59-0e', 'B62-0b', 'B64-20',
]

// ---------------------------------------------------------------- schemas

const PLAN = {
  type: 'object',
  additionalProperties: false,
  required: ['ids', 'todo', 'mechanical_failed', 'ok'],
  properties: {
    ids: { type: 'array', items: { type: 'string' }, description: 'Every id printed by pending --all, in order' },
    todo: { type: 'array', items: { type: 'string' }, description: 'Ids whose state is not done' },
    mechanical_failed: { type: 'array', items: { type: 'string' }, description: 'The "failed" list printed by mechanical' },
    ok: { type: 'boolean', description: 'False if a command could not run at all' },
    note: { type: 'string' },
  },
}

const SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'verdict', 'owner', 'confidence', 'wrote_file', 'upstream_dead'],
  properties: {
    id: { type: 'string', description: 'The item id, exactly as given' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    owner: { type: 'string', description: 'The owner the verdict settles on (an entry or exclusion id, or none)' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    wrote_file: { type: 'boolean', description: 'True once the verdict file exists' },
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
  required: ['id', 'changed', 'verdict', 'owner', 'confidence', 'upstream_dead'],
  properties: {
    id: { type: 'string' },
    changed: { type: 'boolean', description: 'True if the verdict file was rewritten' },
    verdict: { type: 'string', enum: ['holds', 'refuted', 'needs_correction'] },
    owner: { type: 'string' },
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

const CLASSES = `The classes, from CONTEXT.md "刪減與未用" and the rules table in
${CUT}\\_index.md (read both once).  A thing belongs to exactly one:

  residual              殘留內容  finished content (text, assets, numbers, script or code
                                  complete enough to work) that no game path reaches
  stub                  空殼      the mechanism, field or slot exists but nothing was
                                  put in it.  Residual lacks a path; a stub lacks content
  sealed                被封住的內容  content AND the path both exist, but an original bug
                                  keeps the player from getting or seeing it
  predecessor_leftover  前作遺留  code or data inherited UNCHANGED from FD2 that does
                                  nothing in FDPS; needs an FD2-side comparison.  If
                                  FDPS rewrote it and never enabled it, it is residual.
                                  "Different from FD2" is not "rewritten by FDPS"
  negative              否定性結論  a verified statement that something does NOT exist
  excluded              排除      real, but in no class: compiler output, unused API of
                                  a third-party library, unreachable defensive branches,
                                  a data-entry error, a feature in use -- which includes
                                  a text entry whose content IS shown through another
                                  entry (a verbatim or near-verbatim copy, or a draft
                                  superseded by the version that is shown)
  none                  not a cut-content matter at all

The owners a text entry can have on this page:
  S4    residual: dialogue written in full that nothing ever draws, with no shown
        version elsewhere (a different line, not a rewording of a shown one)
  S5    residual: the seven cut-scene blocks that are copies of a chapter block
        (FDETXT53 54 57 58 59 60 61), their first 9 entries carried along
  S13a  excluded: a chapter block's 0x00 chapter-title text (the title card is a
        CHAPTER.SAF picture)
  S13b  excluded: a superseded draft -- the same scene's lines, reworded or
        trimmed, are shown from another block
  S15   excluded: a copy, identical or differing in a few characters or control
        codes, of an entry that is shown
Every owner and its exact wording is on ${CUT}\\story.md (entries) and in the
exclusion list at the end of ${CUT}\\_index.md.`

const SOURCES = `Where the truth is, in order of authority:

  1. ${REPO}\\src\\  -- the complete rebuilt C source of FDPS.LE, every function
     emitted with comments.  Primary source.  Cite file:line.
  2. Ghidra, READ-ONLY, program FDPS.LE.  Load tools in one ToolSearch call, e.g.
       ToolSearch "select:mcp__ghidra__get_xrefs_to,mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__read_memory,mcp__ghidra__search_strings"
     Never call a Ghidra tool that writes (rename, comment, prototype, label,
     bookmark, save).
  3. The game's own data: ${REPO}\\fdps_game_files\\ and
     ${REPO}\\workspace\\vfs_dump\\<CONTAINER>\\ (every .VFS unpacked).  Decoders
     under ${REPO}\\tools\\: text_decode (FDETXTnn), cutscene_script (which script
     draws which entry on which map: "cutscene_script.py show fdps_game_files
     WIN29.DAT --text"), map_decode, global_text, cel_decode, saf_decode,
     vfs_dump.  Cite FILE@offset.
  4. Corroboration only: the chapter pages' per-chapter judgements
     ${REPO}\\tools\\chapter_docs\\judgements\\chNN.json (which entries of a chapter
     block have no reader, and why), the guide mirror (python
     ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps), the
     FD2 project C:\\Users\\fdpsf\\Documents\\fd2-anatomy\\ (src/, knowledge base,
     fd2_game_files/) for anything about FD2, and the knowledge base.

${STORY_TOOL} (read-only) holds the owner table OWNERS and the helpers the
page is generated with; reading it tells you how the claim was produced, not
whether it is true.`

const VERDICT_SHAPE = `{
  "id": "<the id>",
  "claim_sha1": "<the claim_sha1 printed by show, verbatim>",
  "verdict": "holds" | "refuted" | "needs_correction",
  "category": "residual" | "stub" | "sealed" | "predecessor_leftover" | "negative" | "excluded" | "none",
  "owner": "<the entry or exclusion id the thing belongs to after your verdict (S4, S5, S13a, S13b, S14, S15, S16, S17 ...), or none>",
  "confidence": "high" | "medium" | "low",
  "conclusion": "<繁體中文。驗證後的事實，一到三句，結論式>",
  "evidence": [
    { "source": "src", "location": "src/icon.c:958", "observation": "<what is there>" },
    { "source": "ghidra", "location": "00021650", "observation": "..." },
    { "source": "data", "location": "FIELD.VFS/FDETXT30.TXT@0x3c4", "observation": "..." },
    { "source": "guide" | "fd2" | "investigation", "location": "...", "observation": "..." }
  ],
  "corrected_claim": "<繁體中文。refuted／needs_correction 時寫出正確的完整陳述（含歸屬）；否則空字串>",
  "open_question": "<繁體中文。沒能確定的部分；沒有則空字串>",
  "pitfall_candidate": "<繁體中文。重建時照直覺寫就會與原版不同的事；否則空字串>"
}`

function judgePrompt(id) {
  return `You are verifying ONE claim on the cut-and-unused-content page of FDPS
(炎龍騎士團外傳, a 1997 DOS game) whose executable FDPS.LE has been fully
reverse engineered into C.

Your item: ${id}

Get it by running exactly:

  ${RUN} show ${id}

That prints the claim and its claim_sha1.  This is the only item you work on.
Other items are verified by other agents at the same time; never write anything
but your own verdict file.

The claim was written by one session while it built the page, and has not been
checked by anyone else.  Treat it as an assertion to test, in two parts:

  1. THE FACTS: is the thing really as the claim says -- is the text entry
     really never shown (no script DRAW_TEXT / ASK_THREE_WAY on the map whose
     block it is, no fdps_draw_text in src/ that can reach that entry, no death
     script opcode 3 and up naming it, not a village or save-panel header entry
     that is read), are the quoted file facts and code facts right?
  2. THE PLACEMENT: does it belong to the class and owner the claim names?  For
     a text entry this is the question "is its content shown through another
     entry (excluded as a copy or a superseded draft), or is it content that is
     nowhere shown (residual)?" -- decide it from the texts themselves, not
     from a similarity number.

verdict: holds / needs_correction (true but a detail or the placement is wrong
-- write corrected_claim, and put the right owner in owner) / refuted (the
thing is not as claimed at all -- corrected_claim says what is true).

${SOURCES}

${CLASSES}

corrected_claim and conclusion are copied into the knowledge base, so:
conclusion-style Traditional Chinese (state what it is, never how you found it
-- no "驗證時", "調查", "這次"); cite src/ file and function name and a Ghidra
address such as \`0x21650\`, never a line number there; text entries as block
and entry (\`FDETXT30\` \`0x0d\`); game proper nouns exactly as the game writes
them; no workspace/ or devlog/ paths.

Be thorough but bounded.  If something cannot be settled with reasonable
effort, say so in open_question and lower confidence; never call it settled on
the claim's word.

Scratch scripts and outputs go under ${SCRATCH}\\${id}\\ and nowhere else.

WRITE YOUR VERDICT with the Write tool to exactly:

  ${VERDICTS}\\${id}.json

UTF-8 JSON of this shape:

${VERDICT_SHAPE}

The gate (run it yourself before you finish and fix what it reports):

  ${RUN} check --ids ${id}

It requires every field, a matching claim_sha1, at least one first-hand
evidence item (src with file:line, ghidra with an address, data with
FILE@offset), a non-empty owner, and corrected_claim whenever the verdict is
not holds.

Set upstream_dead only if the Ghidra tools stop answering, or python or the repo
itself is unusable.  If Ghidra fails, do not guess around it: write what you
have, set upstream_dead, and stop.

Then return the summary.  Your final output is data for the workflow, not a
message to a human.`
}

function gatePrompt(tag, ids) {
  return `Run the ticket-25.14 story-verification gate over one round.

Round tag: ${tag}
Items (${ids.length}): ${ids.join(' ')}

Run exactly:

  ${RUN} check --ids ${ids.join(' ')} --json

It prints a JSON object with checked, ok, missing, failures and gate_passed.
Report those: every id under "failures" goes into failing, every id under
"missing" into missing.  Condense the failure reasons into problems, one short
line per kind of problem.

Do not fix anything and do not edit any verdict file.  The workflow re-runs the
judging agent for whatever you report.  If the command cannot run at all, say
so in problems and set gate_passed false.`
}

function rescanPrompt(id, why) {
  return `A first agent verified ONE claim on the FDPS cut-content page and its
verdict needs a second reading.  You are that second reader, for this one item
only.

Your item: ${id}
Why it came back: ${why}

Get the claim:        ${RUN} show ${id}
The first verdict:    ${VERDICTS}\\${id}.json

Work in this order, because the point of a second reader is independence:

  1. Read the claim, then form your own view from first-hand sources BEFORE
     reading the first verdict closely -- at minimum re-check the evidence the
     verdict and the placement rest on.
  2. Then read the first verdict and compare.  Check the placement as hard as
     the facts, and check that corrected_claim is true, conclusion-style and
     free of process words and line numbers.
  3. You may read other items' verdict files in ${VERDICTS}\\ as evidence (for
     example the verdict on a neighbouring entry of the same block).  That is
     using a judgement someone else made, not making one for them.  Never edit
     another verdict file.

${SOURCES}

${CLASSES}

If your reading changes anything -- verdict, category, owner, confidence, a
corrected detail -- rewrite ${VERDICTS}\\${id}.json with the Write tool, keeping
exactly the same field shape and claim_sha1, and add a "_supersedes" field: one
short Traditional Chinese paragraph saying what the first reading concluded and
what changed it.  Set changed to true.

If it does not, leave every field exactly as it is and add one field,
"_reread": a single Traditional Chinese line saying you confirmed it and what
you checked.  Set changed to false.  That field is how a later run knows this
verdict already had its second reading.

Run ${RUN} check --ids ${id} afterwards and fix what it reports in your own
file.  READ-ONLY on Ghidra, src/, tools/ and the knowledge base throughout.

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
  `Prepare the ticket-25.14 story-verification worklist.  You decide nothing.

Run exactly these two commands, in this order:

  ${RUN} mechanical
  ${RUN} pending --all

The first settles the verbatim-copy items by string compare and prints a JSON
object {"written": N, "failed": [...]}; return its "failed" list as
mechanical_failed (an empty list when it printed none).  The second prints a
JSON array with one object per judged item: an id and a state that is
"missing", "failing" or "done".  Return ids = every id in the order printed,
and todo = every id whose state is not "done", in the order printed.

If a command cannot run at all, set ok false and say why in note.  Do not try
to repair anything.`,
  { label: 'plan:worklist', phase: 'Plan', schema: PLAN }
)

if (!plan || !plan.ok) {
  return { stopped: 'the planning agent could not read the worklist', note: plan ? plan.note : 'no response' }
}
if (plan.mechanical_failed.length > 0) {
  return {
    stopped: 'some verbatim-copy items are no longer verbatim copies of a shown entry; '
      + 'the ticket session has to re-place them before this runs',
    mechanicalFailed: plan.mechanical_failed,
  }
}
const drift = IDS.filter((i) => !plan.ids.includes(i)).concat(plan.ids.filter((i) => !IDS.includes(i)))
if (drift.length > 0) {
  return {
    stopped: 'story_verify.py and this script disagree on the item ids; update IDS before running',
    drift: drift,
  }
}
const todo = IDS.filter((i) => plan.todo.includes(i))
log(`worklist: ${IDS.length} items, ${IDS.length - todo.length} already settled, ${todo.length} to judge`)

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

  // 5.1 -- one retry per item; still bad after that is unfinished, never done.
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
      log(`round ${round}: the re-gate returned nothing; the retried items count as unfinished`)
      unfinished.push(...retry)
    }
  }
}

// ----------------------------------------------------------------- rescan
//
// Every verdict other than holds, every confidence below high and every open
// question gets a second reader.  The list is derived from the verdict files
// (story_verify.py rescan), so an interrupted rescan resumes where it stopped.

const rescanLog = []
if (!stopped) {
  for (let pass = 1; pass <= MAX_RESCAN_PASSES && !stopped; pass++) {
    const plan2 = await agent(
      `Read the ticket-25.14 story-verification rescan worklist.  You decide nothing.

Run exactly:

  ${RUN} rescan

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
        rescanLog.push(`${r.id} -> ${r.verdict}/${r.owner} (${r.confidence}): ${r.what_changed || ''}`)
        log(`  ${r.id} changed -> ${r.verdict}/${r.owner} (${r.confidence})`)
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
        log(`rescan pass ${pass}: verdicts now failing the gate: ${bad.join(' ')}`)
        unfinished.push(...bad.filter((i) => !unfinished.includes(i)))
      }
    } else {
      log(`rescan pass ${pass}: the gate returned nothing; its items are checked again by the report`)
    }
  }
}

// ----------------------------------------------------------------- report
//
// The closing report is always produced, with the stop reason when there is
// one.  The verdict archive is not produced after a stop (ADR-0007 5.6): a
// partial set reads exactly like a complete one.

phase('Report')
// The reason may quote an agent's note; keep only characters no shell expands.
const stopArg = stopped ? ` --stopped "${stopped.replace(/[^A-Za-z0-9 .,:;()_\-]/g, ' ')}"` : ''
const report = await agent(
  `Write the ticket-25.14 story-verification closing report.  You decide nothing.

Run exactly:

  ${RUN} report --date ${DATE}${stopArg}

It writes ${REPO}\\devlog\\runs\\${DATE}-story-verify-summary.json${stopped ? '' : ` and
${REPO}\\devlog\\runs\\${DATE}-story-verify-verdicts.json`} and prints a
one-line JSON digest.  Return written = the files it names, summary = the digest
verbatim.  If it fails, say so in summary and return written = [].`,
  { label: 'report:closing', phase: 'Report', schema: DONE }
)
if (report) {
  log(`closing report: ${report.summary}`)
}

if (stopped) {
  log(`RUN STOPPED: ${stopped}`)
  log('no verdict archive was written; run again to resume')
}
if (unfinished.length > 0) {
  log(`UNFINISHED (${unfinished.length}): ${unfinished.join(' ')}`)
}

return {
  stopped: stopped,
  items: IDS.length,
  judgedThisRun: todo.length,
  unfinished: unfinished,
  rescanChanged: rescanLog,
  gateReports: gateReports,
  closingReport: report ? report.summary : null,
  written: report ? report.written : [],
}
