// Ticket 25.8, second pass: the items the first run (chapters_ticket25_8.js,
// devlog/runs/2026-09-26-chapters-t258-01.json) left, and the follow-ups other
// tickets handed back.  Start to finish, no human in the loop.
//
//   Pitfalls   three judgements, one agent each, in sequence (one shared page):
//              the two verdicts apply_pitfalls.py refused because their row
//              changed under them (re-judged against pitfalls.md as it is now),
//              and chapter 12's second description of the poison-death
//              candidate, which the first run merged without judging.
//   Relink     one agent per chapter: the prose links that still point at
//              cut_content/_index.md or at the AI table that moved out of
//              resource_info/map.md are pointed at the entry that owns the
//              content; chapter 16 also takes 25.14's verdict that entry 0x00
//              is never shown (the source scan no longer counts the smith).
//              Drafts only; the generated blocks already link the entries.
//
// Load-bearing properties (ADR-0002, ADR-0007): one item per agent; agents
// write only their verdict file or their chapter's two draft files; landing by
// land.py / apply_pitfalls.py / index.py, which judge nothing; the landed gate
// after every round and a strict final gate; one retry per item and per script
// stage; a round with no answer at all stops the run; every skipped item is in
// unfinished; the run record is always archived.  Resume: each agent returns
// at once when its file already carries the marker this pass writes
// ("rejudged" / "relinked"), so re-running the script redoes only what is
// missing.
//
// args: {
//   date:      "YYYY-MM-DD"   required; names the run record
//   only:      [16]           optional; restrict the relink pass to these chapters
//   roundSize: 6              optional
// }

export const meta = {
  name: 'chapter-docs-ticket25-8-followup',
  description: 'Re-judge the refused pitfall rows, judge the merged candidate, relink the chapter pages (ticket 25.8)',
  phases: [
    { title: 'Pitfalls', detail: 'one judge per item, sequential; then apply_pitfalls.py' },
    { title: 'Relink', detail: 'one agent per chapter, drafts only' },
    { title: 'Land', detail: 'land.py and the landed gate after every round' },
    { title: 'Verify', detail: 'index rebuild and the strict gate over every page' },
    { title: 'Record', detail: 'run record' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const WS = REPO + '\\workspace\\chapter_docs'
const DRAFTS = WS + '\\drafts'
const VERDICTS = WS + '\\pitfalls'
const SUPERSEDED = WS + '\\pitfalls_superseded'
const TOOLS = REPO + '\\tools\\chapter_docs'
const FIRST_RUN = REPO + '\\devlog\\runs\\2026-09-26-chapters-t258-01.json'
const STORY_VERDICTS = REPO + '\\devlog\\runs\\2026-09-26-story-verify-verdicts.json'

const DATE = args && args.date
if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { error: 'args.date (YYYY-MM-DD) is required: it names the run record' }
}
const ROUND_SIZE = (args && args.roundSize) || 6
const ALL = []
for (let n = 1; n <= 30; n++) {
  ALL.push(n)
}
const ONLY = (args && args.only) || null
const CHAPTERS = ONLY ? ALL.filter((n) => ONLY.includes(n)) : ALL

function nn(n) {
  return n < 10 ? '0' + n : '' + n
}

// ------------------------------------------------------------------ items

const REJUDGE = [
  { id: 'ch23-time-limit-is-a-walk', chapter: [23] },
  { id: 'ch28-reinforcement-wave-halving', chapter: [28] },
]

const NEW_CANDIDATE = {
  id: 'poison-death-ch12-guardian-weapons',
  chapter: [12],
  what: 'A unit killed by poison in fdps_battle_tick_status_effects (0x1fa30) is retired without collecting '
    + 'death scripts. In chapter 12 a guardian that dies of poison still ends the battle as a win, but its '
    + 'sword (death-script opcode 0: 58/59/5A) is never dropped and cannot be obtained any other way.',
  intuitive_wrong_way: 'Route every death through one on-death hook that runs death scripts, which hands out '
    + 'the guardian weapon on a poison kill.',
  canonical_owner: 'program_info/battle.md',
  related: 'The first run judged the same id from chapter 6 as "added" (verdict '
    + VERDICTS + '\\poison-death-skips-death-scripts.json, row landed in rebuild_info/pitfalls.md '
    + 'under 不能修的原版 bug, beginning 中毒致死不跑死亡腳本).',
}

// ---------------------------------------------------------------- schemas

const VERDICT = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'outcome', 'wrote_file', 'reason', 'blocks_content'],
  properties: {
    id: { type: 'string' },
    outcome: {
      type: 'string',
      enum: ['added', 'linked', 'already_covered', 'rejected_not_verified', 'rejected_below_threshold'],
    },
    wrote_file: { type: 'boolean' },
    reason: { type: 'string', description: 'One sentence' },
    blocks_content: { type: 'string', description: 'Content the behaviour makes unreachable (被封住的內容), one line, or empty' },
  },
}

const RELINK = {
  type: 'object',
  additionalProperties: false,
  required: ['chapter', 'wrote_file', 'gate_clean', 'links_changed', 'upstream_dead'],
  properties: {
    chapter: { type: 'integer' },
    wrote_file: { type: 'boolean', description: 'True once the draft and the meta carry "relinked": true' },
    gate_clean: { type: 'boolean' },
    links_changed: { type: 'integer' },
    upstream_dead: { type: 'boolean' },
    note: { type: 'string' },
  },
}

const LAND_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['results', 'landed_gate_errors', 'ok'],
  properties: {
    results: {
      type: 'array',
      items: {
        type: 'object',
        required: ['chapter', 'status'],
        properties: {
          chapter: { type: 'integer' },
          status: { type: 'string', enum: ['new', 'updated', 'unchanged', 'refused', 'missing'] },
          detail: { type: 'string' },
        },
      },
    },
    landed_gate_errors: { type: 'integer' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const SCRIPT_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['errors', 'ok'],
  properties: {
    errors: { type: 'integer' },
    failing: { type: 'array', items: { type: 'integer' } },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const DONE = {
  type: 'object',
  additionalProperties: false,
  required: ['written'],
  properties: { written: { type: 'array', items: { type: 'string' } } },
}

// ---------------------------------------------------------------- prompts

const PITFALL_RULES = `Steps:
  1. Verify the behaviour.  Read the owning page, the src/ function, and confirm
     in Ghidra READ-ONLY (ToolSearch
     "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function").
     Not confirmed -> rejected_not_verified.
  2. Threshold (${REPO}\\rebuild_info\\_index.md): something the natural rebuild
     gets WRONG, not merely complicated.  Below it -> rejected_below_threshold.
  3. Read ${REPO}\\rebuild_info\\pitfalls.md AS IT IS NOW.  If an existing row
     already covers this and already links the page(s) a reader would need ->
     already_covered.  If a row covers it but its 正典 column lacks a link the
     reader needs -> linked.
  4. Otherwise -> added: one new row for the right "## " section.

Write the verdict file (UTF-8 JSON):
  { "id": "<id>", "outcome": "<outcome>", "reason": "<one sentence>",
    "rejudged": true,
    "section": "<exact ## heading text without the ##, for added>",
    "row": "<the complete new table row, for added>",
    "old_line": "<the existing row, copied EXACTLY as it is in the file now, for linked>",
    "new_line": "<that row with the link added and nothing else changed, for linked>" }
Rows: columns 事項 | 照直覺會怎麼寫 | 正典, Traditional Chinese, linking
relatively from rebuild_info/ (chapter pages are ../chapters/chNN.md, the
cut-content entries ../cut_content/<page>.md#<anchor>).  Link only pages that
exist now.  Copy old_line byte for byte (read the file with the Read tool) --
the script refuses a line that is not in the file exactly.

Also report blocks_content: if the behaviour makes content unreachable (an item
no one can obtain, an event that never fires), one line saying what; else "".`

function rejudgePrompt(item) {
  return `ONE item for ${REPO}\\rebuild_info\\pitfalls.md.  You judge this item only
and write NOTHING but its verdict file; a script applies it.

Candidate ${item.id} (chapter ${item.chapter.join(', ')}).  An earlier judge
decided it and wrote ${VERDICTS}\\${item.id}.json, but the script refused to
apply it: "the row changed since it was read" -- other work edited that row of
pitfalls.md in between.  Decide it again against the page as it is now.

RESUME CHECK FIRST: if ${VERDICTS}\\${item.id}.json parses and has
"rejudged": true, return its outcome at once.

Before writing, copy the existing verdict file to
${SUPERSEDED}\\${item.id}.json (create the folder; overwrite a previous copy).
Read the old verdict for its evidence (what was confirmed, which link it wanted
to add), then follow the steps below; the old verdict's old_line is stale and
must not be reused.

${PITFALL_RULES}

The verdict file is ${VERDICTS}\\${item.id}.json (replace it).  Return the
verdict with wrote_file true once the file exists.`
}

function newCandidatePrompt(c) {
  return `ONE candidate for ${REPO}\\rebuild_info\\pitfalls.md.  You judge this
candidate only and write NOTHING but its verdict file; a script applies it.

Candidate ${c.id} (from chapter ${c.chapter.join(', ')})
  What the original does:     ${c.what}
  How a rebuild would err:    ${c.intuitive_wrong_way}
  Page that owns the fact:    ${c.canonical_owner}
  Related:                    ${c.related}

This is chapter 12's own description of a behaviour an earlier judge decided
from chapter 6's description.  Judge whether chapter 12's case (the guardian's
dropped weapon) is covered by what is on the page now, needs a link added
(e.g. ../chapters/ch12.md or the cut-content entry that owns the weapon, if
one exists -- read ${REPO}\\cut_content\\items.md), or is a separate row.
Also check the claim itself: which records in MAP11.DAT carry those death
scripts, whether poison can actually kill them, and whether the weapons are
obtainable any other way (python ${REPO}\\tools\\map_decode\\map_decode.py show 11,
${REPO}\\chapters\\ch12.md).

RESUME CHECK FIRST: if ${VERDICTS}\\${c.id}.json parses and has "rejudged": true,
return its outcome at once.

${PITFALL_RULES}

The verdict file is ${VERDICTS}\\${c.id}.json.  Return the verdict with
wrote_file true once the file exists.`
}

function relinkPrompt(n) {
  const c = nn(n)
  const ch16 = n !== 16 ? '' : `

CHAPTER 16 ONLY.  Ticket 25.14 verified independently (item E-16-00 in
${STORY_VERDICTS}) that entry 0x00 of FDETXT16 is never shown: the source scan
counted the wandering smith's reply draw as a reader of 0, but that draw only
runs when the reply id is not 0 (src/chevt3.c,
fdps_chapter_16_event_wandering_smith_forge).  The scan has been fixed, so the
gate now asks for a judgement of 0x00.  Confirm it in src/chevt3.c yourself,
then add { "entry": "0x00", "why": "<one line>" } to never_shown in the meta,
and remove from the page (and from open_questions) the sentence that corrected
the old reader -- it is no longer needed.  Adjust confidence if that was the
only open question.`
  return `You are revising ONE chapter page draft of the FDPS project: chapter ${n},
${DRAFTS}\\ch${c}.md with its metadata ${DRAFTS}\\ch${c}.meta.json.  Other
chapters are handled by other agents; never touch their files.

RESUME CHECK FIRST: if the meta has "relinked": true and
  python ${TOOLS}\\check_chapter.py --draft ${n}
reports clean, return at once (links_changed 0).

What to change -- links in the PROSE only (the text outside the
<!-- chapter_docs:KEY --> ... <!-- /chapter_docs:KEY --> markers; the draft keeps
the markers bare and the landing script fills them, and the generated blocks
already link the right entries):

  1. Every link to ../cut_content/_index.md.  The cut_content/ folder now
     exists.  Point each such link at the entry that owns what the sentence is
     about: ../cut_content/<page>.md#<anchor>, where the anchor is the entry
     heading's GitHub anchor (lower-case, backticks and punctuation other than
     - and _ dropped, spaces to -; e.g. "### S11 18 筆波次 \`0xFF\` 的部署記錄"
     is #s11-18-筆波次-0xff-的部署記錄).  Read the entry to make sure it is
     about the same thing.  Pages: story.md (S: dialogue, scenes, waves),
     items.md (I), units.md (U), battle_assets.md (B), code.md (C).  Owners
     already settled for this chapter:
       python ${REPO}\\tools\\chapter_docs\\chapter_facts.py block ${n} dialogue --draft
       python ${REPO}\\tools\\chapter_docs\\chapter_facts.py block ${n} deployments --draft
     show which entry owns each never-shown line and unreachable wave.
     Something listed only in the exclusion list (排除清單 in
     ../cut_content/_index.md) links ../cut_content/_index.md#排除清單.  If no
     entry and no exclusion covers it, leave the link as it is and add one line
     to open_questions saying what is not covered -- do not guess.
  2. Links about the AI behaviour codes that point into ../resource_info/map.md
     (the table moved): point them at ../program_info/map_ai.md#行為代碼.
     Links to map.md about anything else stay.

Change nothing else: no wording, no facts, no judgements${n === 16 ? ' (except the chapter-16 item below)' : ''}.${ch16}

Back up both files first (python shutil.copyfile) to .bak copies.  Then edit
with the Edit tool, set "relinked": true in the meta, and run the gate until it
is clean:
  python ${TOOLS}\\check_chapter.py --draft ${n}
If you cannot get it clean, restore both files from the .bak copies and return
gate_clean false with the reason.  Delete the .bak files at the end.  If the
repository's python tools do not run at all, return upstream_dead true.

Return the summary: links_changed = how many links you repointed.`
}

function landPrompt(chapters) {
  return `Land chapter drafts.  You judge nothing and edit nothing by hand.

1.  python ${TOOLS}\\land.py ${chapters.join(' ')} --json
2.  python ${TOOLS}\\check_chapter.py --landed <the chapters step 1 reports new, updated or unchanged> --json

Report each chapter's status (chapter as an integer), the error count of step 2,
and ok = every chapter landed and step 2 has 0 errors.  Put land.py's detail of
any refusal in problems.`
}

function applyPitfallsPrompt() {
  return `Run:  python ${TOOLS}\\apply_pitfalls.py
It prints {"errors", "link_errors", "results"}.  Report errors = errors +
link_errors, ok = both 0, and in problems every result whose status is "error"
with its id and detail.  Do not fix anything by hand.`
}

function finalPrompt() {
  return `Run, in order, and report; do not fix anything.
  python ${TOOLS}\\index.py build --json
  python ${TOOLS}\\check_chapter.py --landed-all --json
  python ${TOOLS}\\chapter_facts.py verify-maps
  python -m unittest ${TOOLS}\\test_chapter_docs.py
errors = the sum of: index build "errors", check_chapter "errors", 1 if
verify-maps reports any problem, 1 if the tests fail.  failing = chapter numbers
with an error in the second command.  ok = errors == 0.  Copy the first few
error messages into problems.`
}

// ------------------------------------------------------------- run state

const unfinished = []
let halted = false
let haltReason = ''

function halt(reason) {
  if (!halted) {
    halted = true
    haltReason = reason
    log(`STOP ${reason}`)
  }
}

async function once(prompt, opts) {
  const first = await agent(prompt, opts)
  if (first) {
    return first
  }
  log(`${opts.label}: no answer, retrying once`)
  return await agent(prompt, Object.assign({}, opts, { label: opts.label + '-retry' }))
}

// --------------------------------------------------------------- pitfalls

phase('Pitfalls')
const verdicts = []
const items = REJUDGE.map((it) => ({ label: it.id, prompt: rejudgePrompt(it) }))
  .concat([{ label: NEW_CANDIDATE.id, prompt: newCandidatePrompt(NEW_CANDIDATE) }])
for (let i = 0; i < items.length; i++) {
  const v = await once(items[i].prompt, { label: `pitfall:${items[i].label}`, phase: 'Pitfalls', schema: VERDICT })
  if (!v) {
    halt(`pitfall:${items[i].label}: no answer after a retry -- upstream failure`)
    unfinished.push(`pitfalls not judged: ${items.slice(i).map((x) => x.label).join(' ')}`)
    break
  }
  if (!v.wrote_file) {
    unfinished.push(`pitfall:${items[i].label}: no verdict file`)
    continue
  }
  verdicts.push(v)
  log(`  ${v.id}: ${v.outcome} -- ${v.reason}`)
}

let applyReport = null
if (!halted && verdicts.some((v) => v.outcome === 'added' || v.outcome === 'linked')) {
  applyReport = await once(applyPitfallsPrompt(), { label: 'apply-pitfalls', phase: 'Pitfalls', schema: SCRIPT_REPORT })
  if (!applyReport || !applyReport.ok) {
    unfinished.push(`apply pitfalls: ${applyReport ? (applyReport.problems || 'not ok') : 'no answer'}`)
  }
}

// ------------------------------------------------------ relink and land

const relinked = []
const landReports = []
for (let start = 0; start < CHAPTERS.length; start += ROUND_SIZE) {
  if (halted) {
    unfinished.push(`not relinked after the stop: chapters ${CHAPTERS.slice(start).join(' ')}`)
    break
  }
  const batch = CHAPTERS.slice(start, start + ROUND_SIZE)
  const round = Math.floor(start / ROUND_SIZE) + 1
  phase('Relink')
  const run = async (list, tag) => (await parallel(list.map((n) => () =>
    agent(relinkPrompt(n), { label: `${tag}:ch${nn(n)}`, phase: 'Relink', schema: RELINK })))).filter(Boolean)
  const first = await run(batch, `relink-r${round}`)
  if (first.length === 0) {
    halt(`relink round ${round}: no agent answered -- upstream failure`)
    unfinished.push(`not relinked after the stop: chapters ${CHAPTERS.slice(start).join(' ')}`)
    break
  }
  if (first.some((s) => s.upstream_dead)) {
    halt(`relink round ${round}: an agent reports the repository tools stopped answering`)
  }
  const good = first.filter((s) => s.wrote_file && s.gate_clean)
  let lost = batch.filter((n) => !good.some((s) => s.chapter === n))
  if (lost.length > 0 && !halted) {
    const again = await run(lost, `relink-r${round}-retry`)
    if (again.length === 0) {
      halt(`relink round ${round} retry: no agent answered -- upstream failure`)
    }
    good.push(...again.filter((s) => s.wrote_file && s.gate_clean))
    lost = batch.filter((n) => !good.some((s) => s.chapter === n))
  }
  for (const n of lost) {
    unfinished.push(`ch${nn(n)}: not relinked (no clean draft after a retry)`)
  }
  relinked.push(...good.map((s) => ({ chapter: s.chapter, links: s.links_changed, note: s.note || '' })))
  if (halted) {
    const rest = CHAPTERS.slice(start + ROUND_SIZE)
    if (rest.length > 0) {
      unfinished.push(`not relinked after the stop: chapters ${rest.join(' ')}`)
    }
    if (good.length > 0) {
      unfinished.push(`relinked but not landed after the stop: chapters ${good.map((s) => s.chapter).join(' ')}`)
    }
    break
  }
  if (good.length > 0) {
    phase('Land')
    const report = await once(landPrompt(good.map((s) => s.chapter)),
      { label: `land-r${round}`, phase: 'Land', schema: LAND_REPORT })
    if (!report) {
      unfinished.push(`land-r${round}: no answer; chapters ${good.map((s) => s.chapter).join(' ')} not landed`)
    } else {
      landReports.push(Object.assign({ round: round }, report))
      if (!report.ok) {
        unfinished.push(`land-r${round}: ${report.problems || 'not ok'}`)
      }
    }
  }
}

// ----------------------------------------------------------------- verify

let finalReport = null
if (!halted) {
  phase('Verify')
  finalReport = await once(finalPrompt(), { label: 'final-gate', phase: 'Verify', schema: SCRIPT_REPORT })
  if (!finalReport || !finalReport.ok) {
    unfinished.push(`final gate: ${finalReport ? (finalReport.problems || 'not ok') : 'no answer'}`)
  }
} else {
  unfinished.push(`final gate: skipped because the run stopped (${haltReason})`)
}

// ----------------------------------------------------------------- record

const stats = {
  date: DATE,
  firstRun: FIRST_RUN,
  halted: halted,
  haltReason: haltReason,
  pitfallVerdicts: verdicts,
  applyPitfalls: applyReport,
  // For the cut_content/ work: behaviour a judge found to block content.
  blockedContent: verdicts.filter((v) => v.blocks_content).map((v) => ({ id: v.id, what: v.blocks_content })),
  relinked: relinked,
  land: landReports,
  finalGate: finalReport,
  unfinished: unfinished,
}

phase('Record')
const record = await agent(`Write this run record, verbatim, to
  ${REPO}\\devlog\\runs\\${DATE}-chapters-t258-NN.json
where NN is the next free two-digit number (01 and up; UTF-8, pretty-printed).
Do not write anything else.

${JSON.stringify(stats, null, 2)}

Return the file you wrote.`, { label: 'record', phase: 'Record', schema: DONE })
if (!record) {
  unfinished.push('record: not written -- the caller must archive the return value')
}

return Object.assign(stats, { recordFiles: record ? record.written : [] })
