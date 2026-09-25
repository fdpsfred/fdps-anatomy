// Ticket 25.8, start to finish, with no human in the loop.
//
// Writes the thirty chapters/chNN.md pages and rebuilds chapters/_index.md.
// Each page is prose an agent writes around blocks tools/chapter_docs
// generates from the shipped data (deployments, treasure, events, cut-scene
// steps, the whole text block); the agent supplies the prose and the three
// judgements the blocks cannot make by themselves (which waves are ever
// deployed, who reads the text entries the source scan cannot resolve, and
// which entries nothing ever shows).
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One chapter per agent.  The chapter list lives in this script; every
//   drafting agent is handed exactly one chapter number.
//
//   Judgements go to files, agents return ~200 byte summaries.  A drafter
//   writes workspace/chapter_docs/drafts/chNN.md and chNN.meta.json; later
//   stages read collect.py's compact index, never the drafts.
//
//   Judging agents write nothing else: no chapters/, no tools/, no src/, no
//   Ghidra.  Landing is done by scripts that judge nothing: land.py fills the
//   generated blocks and copies the page (and the meta's judgement subset to
//   tools/chapter_docs/judgements/) only when the gate passes; index.py
//   rebuilds chapters/_index.md; apply_pitfalls.py applies decided rows.
//
//   Gates after every landing: check_chapter.py --landed on the chapters of
//   the round (links to chapters not landed yet only warn); index.py verify
//   after the index is rebuilt; the link check on pitfalls.md after it is
//   edited; and a final strict check_chapter.py --landed-all over every page.
//   Back-sweep: chapters left with open questions or low confidence are
//   re-read once with the other chapters' drafts and landed pages as
//   evidence, then re-landed.
//
//   Errors: one retry per chapter and per script stage; a round in which
//   every agent returns nothing stops the run (upstream failure); a drafter
//   reporting that the repository tools stopped answering stops the run.
//   After a stop no index, pitfall or devlog stage runs, every skipped
//   chapter is listed in unfinished, and the run record is still archived --
//   a failed preparation included.
//   Resume: every drafting agent first checks for its own draft and meta and
//   returns them if complete and clean, so re-running the whole script redoes
//   only what is missing; land.py, index.py and apply_pitfalls.py are
//   idempotent.  A draft is keyed to its chapter number only; if the shipped
//   data or src/ moved since a draft was written, the gate's judgement check
//   and the landed stale check refuse it, and the chapter comes back.
//
// args: {
//   date:      "YYYY-MM-DD"   required; names the devlog entry and the run record
//   only:      [3, 28]        optional; restrict drafting to these chapters
//   roundSize: 6              optional; chapters drafted per round before landing
// }

export const meta = {
  name: 'chapter-docs-ticket25-8',
  description: 'Draft, gate, land and index the thirty chapter pages (ticket 25.8)',
  phases: [
    { title: 'Prepare', detail: 'regenerate the per-chapter facts and annotated maps' },
    { title: 'Draft', detail: 'one agent per chapter, read-only, writes a draft and its meta' },
    { title: 'Land', detail: 'land.py fills and copies clean drafts; the landed gate' },
    { title: 'Rescan', detail: 're-read chapters left open, with the others as evidence' },
    { title: 'Index', detail: 'cross-chapter chains drafted; index.py rebuilds _index.md' },
    { title: 'Pitfalls', detail: 'one judge per candidate, sequential, writes a verdict file' },
    { title: 'Verify', detail: 'strict gate over every landed page and the index' },
    { title: 'Record', detail: 'devlog entry and run record' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const FD2 = 'C:\\Users\\fdpsf\\Documents\\fd2-anatomy'
const WS = REPO + '\\workspace\\chapter_docs'
const DRAFTS = WS + '\\drafts'
const FACTS = WS + '\\facts'
const MAPS = WS + '\\maps'
const VERDICTS = WS + '\\pitfalls'
const TOOLS = REPO + '\\tools\\chapter_docs'

const DATE = args && args.date
if (!DATE || !/^\d{4}-\d{2}-\d{2}$/.test(DATE)) {
  return { error: 'args.date (YYYY-MM-DD) is required: it names the devlog entry and the run record' }
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

// Candidates the parent ticket already knows about (25-kb-verification-pass.md,
// 共同要求).  Verified before anything is written, like any other.
const SEED_PITFALLS = [
  {
    id: 'ch28-reinforcement-wave-halving',
    chapter: [28],
    what: 'Chapter 28 reinforcements pick the wave as turn / 2; turn 7 truncates to wave 3, so wave 4 never deploys.',
    intuitive_wrong_way: 'Fix the arithmetic so wave 4 appears -- that fields three enemy soldiers the original never does.',
    canonical_owner: 'chapters/ch28.md',
  },
]

// ---------------------------------------------------------------- schemas

const PREPARE = {
  type: 'object',
  additionalProperties: false,
  required: ['ok', 'facts_written', 'maps_written'],
  properties: {
    ok: { type: 'boolean' },
    facts_written: { type: 'integer', description: 'How many chNN.json facts files exist afterwards' },
    maps_written: { type: 'integer', description: 'How many chNN.png maps exist afterwards' },
    problems: { type: 'string' },
  },
}

const DRAFT_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['chapter', 'wrote_file', 'gate_clean', 'confidence', 'open_questions', 'upstream_dead'],
  properties: {
    chapter: { type: 'integer', description: 'The chapter number, exactly as given' },
    wrote_file: { type: 'boolean', description: 'True once both the .md and the .meta.json exist' },
    gate_clean: { type: 'boolean', description: 'True when check_chapter.py --draft reports clean' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    open_questions: { type: 'integer', description: 'Length of open_questions in the meta file' },
    upstream_dead: { type: 'boolean', description: 'True only if Ghidra or the repository tools stopped answering' },
    note: { type: 'string', description: 'At most one short line, or empty' },
  },
}

const LAND_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['results', 'landed_gate_errors', 'pending_links', 'ok'],
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
    pending_links: { type: 'integer', description: 'Number of pending-link warnings in step 2' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const COLLECTED = {
  type: 'object',
  additionalProperties: false,
  required: ['drafts', 'pitfall_candidates', 'cut_content_candidates', 'cross_chapter', 'unreadable'],
  properties: {
    drafts: {
      type: 'array',
      items: {
        type: 'object',
        required: ['chapter', 'complete', 'confidence', 'open_questions'],
        properties: {
          chapter: { type: 'integer' },
          complete: { type: 'boolean' },
          confidence: { type: 'string' },
          open_questions: { type: 'integer' },
        },
      },
    },
    pitfall_candidates: {
      type: 'array',
      items: {
        type: 'object',
        required: ['id', 'chapter', 'what', 'intuitive_wrong_way', 'canonical_owner'],
        properties: {
          id: { type: 'string' },
          chapter: { type: 'array', items: { type: 'integer' } },
          what: { type: 'string' },
          intuitive_wrong_way: { type: 'string' },
          canonical_owner: { type: 'string' },
        },
      },
    },
    cut_content_candidates: {
      type: 'array',
      items: {
        type: 'object',
        required: ['chapter', 'what', 'why'],
        properties: { chapter: { type: 'integer' }, what: { type: 'string' }, why: { type: 'string' } },
      },
    },
    cross_chapter: {
      type: 'array',
      items: {
        type: 'object',
        required: ['chapter', 'kind', 'text'],
        properties: { chapter: { type: 'integer' }, kind: { type: 'string' }, text: { type: 'string' } },
      },
    },
    unreadable: { type: 'array', items: { type: 'string' } },
  },
}

const INDEX_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['wrote_file', 'build_errors', 'verify_errors', 'ok'],
  properties: {
    wrote_file: { type: 'boolean', description: 'True once _index_chains.md exists (chains stage) or _index.md was written (apply stage)' },
    build_errors: { type: 'integer' },
    verify_errors: { type: 'integer' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const FINAL_GATE = {
  type: 'object',
  additionalProperties: false,
  required: ['pages', 'errors', 'failing', 'index_errors', 'ok'],
  properties: {
    pages: { type: 'integer', description: 'How many chapter pages step 1 checked' },
    errors: { type: 'integer', description: 'The "errors" field of step 1' },
    failing: { type: 'array', items: { type: 'integer' }, description: 'Chapters with an error in step 1' },
    index_errors: { type: 'integer', description: 'The "errors" field of step 2' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
  },
}

const PITFALL_VERDICT = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'outcome', 'wrote_file', 'reason'],
  properties: {
    id: { type: 'string' },
    outcome: {
      type: 'string',
      enum: ['added', 'linked', 'already_covered', 'rejected_not_verified', 'rejected_below_threshold'],
    },
    wrote_file: { type: 'boolean' },
    reason: { type: 'string', description: 'One sentence' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['errors', 'link_errors', 'ok'],
  properties: {
    errors: { type: 'integer' },
    link_errors: { type: 'integer' },
    ok: { type: 'boolean' },
    problems: { type: 'string' },
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

const META_SHAPE = `{
  "chapter": <n>,
  "complete": true,
  "rescanned": false,
  "confidence": "high" | "medium" | "low",
  "open_questions": ["<anything you could not settle, one line each>"],
  "waves": [
    { "wave": <int, 255 for FF>, "deployed": true,
      "when": "<Traditional Chinese: when and by what, citing the function as \`fdps_name\`（\`0xaddr\`） or the script as \`ICONnn.DAT\` \`0xoff\`>" },
    { "wave": <int>, "deployed": false, "why": "<Traditional Chinese, one line: why nothing ever deploys it>" }
  ],
  "text_readers": [
    { "entry": "0x0f", "reader": "<Traditional Chinese: what shows this entry, citing \`fdps_name\`（\`0xaddr\`）>" }
  ],
  "never_shown": [
    { "entry": "0x00", "why": "<Traditional Chinese, one line>" }
  ],
  "cross_chapter": [
    { "kind": "recruit" | "reward" | "ending" | "flag" | "continuity" | "other",
      "text": "<Traditional Chinese: a fact that ties this chapter to another one -- a condition set here and read later, a reward whose condition spans chapters, an ending branch>" }
  ],
  "pitfall_candidates": [
    { "id": "<short-kebab-id>", "what": "<English>", "intuitive_wrong_way": "<English>",
      "canonical_owner": "<repo-relative path of the page that owns the fact>" }
  ],
  "cut_content_candidates": [ { "what": "<...>", "why": "<one line>" } ],
  "guide_disagreements": [ { "what": "<...>", "guide": "<what the site says>", "game": "<what the data/code does>" } ]
}`

const HOUSE_RULES = `Rules for the page -- the gate (check_chapter.py) enforces the mechanical
ones, the rest are the knowledge base's conventions:

  * Traditional Chinese prose.  Identifiers, addresses, file names stay as they are.
  * Start from the skeleton: python ${TOOLS}\\check_chapter.py --template <n>
    It is the title line, the header marker, and ten ## sections in a fixed
    order.  Keep every heading and every marker line exactly as it is; the
    markers (<!-- chapter_docs:KEY -->) are filled by the landing script with
    generated blocks -- you never write their content.  You may add prose
    (and ### subsections) under any heading, before its marker.
  * What goes where:
      ## 概要                 the story of the chapter in one to three
                              paragraphs, from its dialogue (the transcript and
                              the scene lines inside the scripts) -- who is where,
                              what happens, how it ends.
      ## 加入與離隊           who joins, leaves or fights as a guest; the join
                              itself is owned by ../assets/characters.md#加入 --
                              link it (the gate requires the link), do not
                              repeat its table.
      ## 勝敗條件與特殊機制   what the post-action handler and the events really
                              test to win or lose (turn limits, protected units,
                              escape cells, bosses), conditional rewards, hidden
                              events, anything a player would call "special".
                              Cite each rule's function.  The on-screen win/lose
                              strings are in the header block; say where the
                              code differs from them.
      ## 敵人配置             a short account of the opposition: what comes when,
                              the bosses, notable AI (guards that never move,
                              chest thieves, units that walk off).  The full
                              record tables are generated below it.
      ## 寶物                 anything the generated table cannot show: items a
                              handler hands out (event cells "某角色前往"),
                              rewards, a chest a thief AI goes for.
      ## 村莊                 usually nothing to add.
      ## 處理流程             one ### subsection each for the entry handler, the
                              post-action check, the victory handler and every
                              chapter event handler this chapter's data calls:
                              what each does, step by step, in order, with its
                              citation.  init / post / end and every event
                              handler the header block lists MUST be cited here
                              (the gate checks each one).
      ## 回合事件與格子事件 / ## 過場腳本 / ## 對話
                              generated; add a sentence only if a reader needs
                              it (e.g. which script branch is the good one).
  * Tie every rule to its function as  \`fdps_name\`（\`0x36bb0\`）  -- backticked
    name, then the ENTRY address from ${REPO}\\ghidra_snapshot\\functions.txt in
    backticks inside （）.  The gate checks every such pair.
  * Conclusions only.  No account of how you found anything, no dates, no
    ticket numbers, no "後來發現／一開始以為", no devlog links.  Never cite
    workspace/ or legacy/.
  * Every fact has one owner; link, do not repeat (relative links from chapters/):
      binary formats            ../resource_info/  (map.md, terrain.md, text.md, cutscene_script.md)
      table values and names    ../assets/  (characters.md, enemies.md, items.md, shops.md, ...)
      mechanisms                ../program_info/  (link a page only if it exists now)
      rebuild traps             ../rebuild_info/pitfalls.md
      other chapters            chNN.md in the same folder (a chapter not
                                written yet only warns now; the final gate
                                requires it to exist, which it will)
      content nothing reaches   (an item no one can get, an enemy that never
                                deploys, a line nothing shows): ONE line, linked
                                ONLY as ../cut_content/_index.md -- that folder is
                                built by other work and its layout is unknown,
                                so never guess a deeper path.  The generated
                                blocks already do this for waves and text entries
                                you judge unreachable.`

function draftPrompt(n) {
  const c = nn(n)
  return `You are writing ONE knowledge-base page for the FDPS reverse-engineering
project: chapters/ch${c}.md, chapter ${n} (章節索引 ${n - 1}, map ${n - 1}, text
block FDETXT${c}).  This is the only chapter you work on.  Other chapters are
written by other agents; never write their files.

RESUME CHECK FIRST.  If both ${DRAFTS}\\ch${c}.md and ${DRAFTS}\\ch${c}.meta.json
exist, the meta has "complete": true, and
  python ${TOOLS}\\check_chapter.py --draft ${n}
reports "clean", do nothing else: return the summary from the meta file.

Read first:
  ${FACTS}\\ch${c}.json          the chapter's mechanical facts and your leads:
                                 handlers, event slots, waves and their records,
                                 deploy call sites in src/ and in scripts, the
                                 text readers the scan found, the entries it did
                                 NOT find a reader for, runtime-id draws
  ${FACTS}\\ch${c}.blocks.md     the generated blocks as they will appear (before
                                 your judgements are applied)
  ${MAPS}\\ch${c}.png            the battlefield, annotated: C chest, B buried,
                                 R repaint cell, E event cell, P party start
                                 (read it as an image)
  ${REPO}\\chapters\\_index.md, ${REPO}\\resource_info\\map.md,
  ${REPO}\\assets\\characters.md (## 加入), ${REPO}\\rebuild_info\\pitfalls.md

Sources, in order of authority:
  1. The emitted C source.  Every fdps_chapter_${c}_* function, and every event
     handler the facts list under event_slots, is in src/ch*.c with comments
     that carry the semantics (grep for the name).  Follow calls into other
     src/ files when a rule lives there.
  2. Ghidra (FDPS.LE), READ-ONLY, only when the source leaves a doubt.  Load in
     ONE ToolSearch call:
       ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to"
     Never call a Ghidra tool that writes.  If Ghidra does not answer, carry on
     from src/; only if the repository's own python tools fail to run at all,
     stop and return upstream_dead=true.
  3. Decoders, for anything the facts do not show (import, never re-implement):
       python ${REPO}\\tools\\map_decode\\map_decode.py show ${n - 1}
       python ${REPO}\\tools\\cutscene_script\\cutscene_script.py show ${REPO}\\fdps_game_files <MEMBER> --text
       python ${REPO}\\tools\\chapter_docs\\chapter_facts.py block ${n} <key> --draft
  4. The mirrored strategy site, as a cross-check only (a person wrote it; the
     machine code wins, and a disagreement worth knowing goes on the page and
     into guide_disagreements):
       python ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps
  5. The predecessor's chapter page, for COVERAGE and LAYOUT only -- never a
     fact: ${FD2}\\chapters\\chapter_${c}.md

${HOUSE_RULES}

THE THREE JUDGEMENTS (they go into the meta, the generated blocks read them):

  waves -- one entry for EVERY wave other than 0 that has records (facts.waves;
    255 is wave FF).  Wave 0 is always deployed on entry and must NOT appear.
    For each, find what calls fdps_deploy_wave with it: a script DEPLOY_WAVE on
    this map (facts.script_deploys), a handler call (facts.deploy_call_sites;
    work out runtime expressions such as "turn - 2" or "turn / 2" against the
    turn events that call the handler).  deployed=true with "when" (which turn
    and phase, which cell, which death -- and by which function or script
    step), or deployed=false with "why".  A wave nothing reaches is content
    nobody sees: the block renders it as one line to cut_content.

  text_readers / never_shown -- one entry for EVERY entry in
    facts.text_entries_without_reader, and only those.  Find what shows it:
    a handler drawing an id computed at run time (facts.unresolved_draws), a
    table of ids, a script on another map whose SWITCH_MAP returns here, the
    credit roll, ...  Found -> text_readers with the citation.  Nothing can
    show it -> never_shown with the reason; the page then carries one line to
    cut_content instead of the text.  Entry 0x00 (the "第N章" string) has no
    fixed reader outside chapter 16: settle it for this chapter like any other.

Also record in the meta: facts that tie this chapter to others (cross_chapter
-- they build the index's mechanism chains), pitfall candidates (anything a
rebuild written "the natural way" would get wrong: rebuild_info/_index.md's
threshold; do not edit pitfalls.md), and content nothing reaches
(cut_content_candidates).  Empty arrays are fine; do not invent entries.

WRITE ONLY THESE TWO FILES (Write tool, UTF-8):
  ${DRAFTS}\\ch${c}.md
  ${DRAFTS}\\ch${c}.meta.json   in this shape:

${META_SHAPE}

Nothing else is written: not chapters/, not tools/, not src/, not any
_index.md, not Ghidra.  The workflow lands your page afterwards.

Then run the gate and fix every error, re-running until it is clean:
  python ${TOOLS}\\check_chapter.py --draft ${n}
It fills the markers from your meta and checks the whole page.  pending-link
warnings are expected.  Write "complete": true only once it is clean.

If something cannot be settled, say so on the page ("未確認") and in
open_questions, and lower confidence.  A confident wrong statement costs more
than an honest unknown.

Return the summary.  Your final output is data for the workflow, not a message
to a human.`
}

function rescanPrompt(n, why) {
  const c = nn(n)
  return `An earlier pass wrote ONE chapter page draft for the FDPS project and left it
unsettled.  You are re-reading that same chapter now that the others have been
drafted too.

Your chapter: ${n}.  Draft ${DRAFTS}\\ch${c}.md, metadata ${DRAFTS}\\ch${c}.meta.json.
Why it came back: ${why}

RESUME CHECK FIRST: if the meta already has "rescanned": true, change nothing
and return its summary.

Before changing anything, back up both files (python shutil.copyfile) to
${DRAFTS}\\ch${c}.md.bak and ${DRAFTS}\\ch${c}.meta.json.bak.

Read your draft and its open_questions.  Then read the OTHER chapters' drafts
in ${DRAFTS}\\ and landed pages in ${REPO}\\chapters\\ that touch the same
handlers, flags or characters -- another chapter may have settled what yours
depends on (a flag set earlier, a shared handler, a character's state).
Reading their conclusions is using evidence someone else produced, not judging
their chapter.  Never edit another chapter's files.  You may re-read src/ and
Ghidra (READ-ONLY) to confirm.

${HOUSE_RULES}

If you can settle a question, rewrite ONLY your two files (same shape): fix the
page and the judgements, remove the settled entries from open_questions, adjust
confidence.  Re-run the gate until clean:
  python ${TOOLS}\\check_chapter.py --draft ${n}
If you cannot get it clean, restore both files from the .bak copies -- the first
draft was clean and must not be lost.

If nothing can be settled, change nothing on the page.  An honest open question
is a correct outcome; the second attempt is not a reason to manufacture a
conclusion.

In every case finish by setting "rescanned": true in the meta file, then delete
the .bak files.  Return the summary (same fields as the first pass).`
}

function preparePrompt() {
  return `Regenerate the mechanical inputs of the chapter pages.  You judge nothing
and edit nothing by hand.

Run, in order:
  python ${TOOLS}\\chapter_facts.py facts all
  python ${TOOLS}\\chapter_facts.py render-maps
  python -m unittest ${TOOLS}\\test_chapter_docs.py

Then count ${FACTS}\\ch*.json and ${MAPS}\\ch??.png.  ok is true only when all
three commands exit 0 and both counts are 30.  If anything fails, put the error
in problems; do not try to repair code.`
}

function landPrompt(chapters) {
  return `Land reviewed chapter drafts into the knowledge base.  You judge nothing:
land.py fills the generated blocks and copies a draft only when the gate passes,
and refuses it otherwise.

1.  python ${TOOLS}\\land.py ${chapters.join(' ')} --json
2.  Run the landed gate over ONLY the chapters step 1 reports as new, updated or
    unchanged:
      python ${TOOLS}\\check_chapter.py --landed <those chapters> --json

Report each chapter's status from step 1 (chapter as an integer), the error
count from step 2, and the number of pending-link warnings in step 2.  If land.py
refuses or misses a chapter, report it in problems with its detail -- do NOT
edit the draft or the landed page to get it through.  ok is true only when every
chapter landed and step 2 has 0 errors.`
}

function collectPrompt() {
  return `Run this and return its JSON output unchanged, as the structured result. Do
not judge or edit anything.

  python ${TOOLS}\\collect.py`
}

function chainsPrompt() {
  return `Write the cross-chapter mechanism chains of ${REPO}\\chapters\\_index.md -- the
one prose section of that page.  You write ONLY
${DRAFTS}\\_index_chains.md; index.py assembles the page afterwards.

FIRST count ${REPO}\\chapters\\ch??.md.  If fewer than 30 exist, write nothing
and return wrote_file false, ok false, problems "only K of 30 chapter pages
landed" -- the chains are about all thirty chapters.

Evidence:
  python ${TOOLS}\\collect.py      (cross_chapter: every chapter drafter's notes)
  ${REPO}\\chapters\\chNN.md        (the landed chapter pages)
  src/ (the chapter handlers) and Ghidra READ-ONLY to confirm a link between
  chapters that the notes only suggest.  A drafter's note is a lead, not a fact.

The section starts with the exact line "## 跨章機制鏈" and has ### subsections
for what the evidence supports, at least:
  ### 條件式招募      (a character whose joining depends on something done in
                       another chapter, or on a condition checked in the victory
                       handler -- say which chapter sets it and which reads it)
  ### 隱藏獎勵        (rewards whose condition spans chapters or is not shown
                       on screen)
  ### 結局分歧        (the two ending entries in chapters 27 and 30: what
                       decides which ending is played, and where each branch
                       goes)
plus any other chain the notes reveal (flags carried between chapters, units
carried from one map to the next).  Say plainly when a subsection has nothing:
"FDPS 沒有…" is a conclusion too -- but every ### subsection must still cite
at least one function (the gate checks it): the handler that shows the
absence, e.g. the init handlers that add every character unconditionally.

Rules: Traditional Chinese; every rule tied to its function as
\`fdps_name\`（\`0xaddr\`） (entry address from ${REPO}\\ghidra_snapshot\\functions.txt);
link the chapter pages as chNN.md and other folders relatively from chapters/;
per-chapter detail stays in the chapter pages -- link, do not repeat; content
nothing reaches is one line linked ONLY as ../cut_content/_index.md.
Conclusions only: no process narrative, dates, ticket numbers, workspace/ paths.

RESUME CHECK FIRST: if ${DRAFTS}\\_index_chains.md exists and
  python ${TOOLS}\\index.py verify --json
reports 0 errors other than "stale", return at once with wrote_file true.

After writing, run  python ${TOOLS}\\index.py verify --json  and fix every error
it reports that is not "stale" (stale only means the page has not been rebuilt
yet).  Return wrote_file, build_errors 0, verify_errors = the non-stale error
count, ok = (verify_errors == 0).`
}

function indexApplyPrompt() {
  return `Rebuild ${REPO}\\chapters\\_index.md.  You judge nothing and edit nothing by hand.

Run, in order:
  python ${TOOLS}\\index.py build --json
  python ${TOOLS}\\index.py verify --json

Report build_errors and verify_errors (the "errors" field of each), wrote_file =
whether build wrote the page, and ok = both 0.  If either reports errors, copy
them into problems; do NOT fix anything by hand.`
}

function finalGatePrompt() {
  return `Run the final gate over the chapter pages.  You judge nothing and edit
nothing.

1.  python ${TOOLS}\\check_chapter.py --landed-all --json
2.  python ${TOOLS}\\index.py verify --json

Report pages = the number of chapters step 1 checked, errors = its "errors",
failing = the chapter numbers with at least one error-level finding, index_errors
= the "errors" of step 2, ok = both 0.  Copy the first few error messages into
problems.  Do NOT fix anything.`
}

function pitfallPrompt(c) {
  return `ONE candidate for ${REPO}\\rebuild_info\\pitfalls.md -- the table of things a
rebuild gets wrong when written "the natural way".  You judge this candidate
only, and you write NOTHING but your verdict file; a script applies it later.

Candidate ${c.id} (from chapter ${c.chapter.join(', ')})
  What the original does:     ${c.what}
  How a rebuild would err:    ${c.intuitive_wrong_way}
  Page that owns the fact:    ${c.canonical_owner}

Verdict file: ${VERDICTS}\\${c.id}.json
RESUME CHECK FIRST: if it exists and parses, return its outcome at once.

Steps:
  1. Verify it.  Read the owning page (chapter pages are landed under
     chapters/), the src/ function, and confirm in Ghidra READ-ONLY
     (ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function").
     Not confirmed -> rejected_not_verified.
  2. Threshold (${REPO}\\rebuild_info\\_index.md): something the natural rebuild
     gets WRONG, not merely complicated.  Below it -> rejected_below_threshold.
  3. Read pitfalls.md as it is now, AND every verdict file already in
     ${VERDICTS}\\.  If an existing row or an earlier verdict already covers
     this -> already_covered.  If an existing row covers it but its 正典 column
     does not link the owning page -> linked.
  4. Otherwise -> added: one new row for the right "## " section.

Write the verdict file (UTF-8 JSON):
  { "id": "${c.id}", "outcome": "<outcome>", "reason": "<one sentence>",
    "section": "<exact ## heading text without the ##, for added>",
    "row": "<the complete new table row, for added>",
    "old_line": "<the existing row, copied EXACTLY as it is now, for linked>",
    "new_line": "<that row with the link added and nothing else changed, for linked>" }
Rows: columns 事項 | 照直覺會怎麼寫 | 正典, Traditional Chinese, no numbers the
owning page has -- link it relatively from rebuild_info/ (e.g. ../chapters/ch28.md).
Link only a page that exists now.

Return the verdict with wrote_file true once the file exists.`
}

function applyPitfallsPrompt() {
  return `Apply decided edits to rebuild_info/pitfalls.md.  You judge nothing and edit
nothing by hand.

Run:  python ${TOOLS}\\apply_pitfalls.py

It prints {"errors", "link_errors", ...}.  Report both counts; ok is true only
when both are 0.  If anything was refused or a link is broken, say exactly which
in problems; do NOT fix it by hand.`
}

// ------------------------------------------------------------- run state

const unfinished = []
const landReports = []
let halted = false
let haltReason = ''

function halt(reason, remaining) {
  if (halted) {
    return
  }
  halted = true
  haltReason = reason
  log(`STOP ${reason}`)
  if (remaining && remaining.length > 0) {
    unfinished.push(`not done after the stop: chapters ${remaining.join(' ')}`)
  }
}

function skipped(stage) {
  unfinished.push(`${stage}: skipped because the run stopped (${haltReason})`)
}

function usable(s) {
  return s && s.wrote_file && s.gate_clean
}

function noteDead(label, s) {
  if (s && s.upstream_dead) {
    halt(`${label}: the agent reports the tools stopped answering${s.note ? ' (' + s.note + ')' : ''}`, [])
  }
}

// Draft a set of chapters: one parallel round, the outage check, one retry for
// each chapter without a clean draft, with its own outage check.
async function draftRound(chapters, label, promptOf, phaseName) {
  const first = (await parallel(chapters.map((n) => () =>
    agent(promptOf(n), { label: `${label}:ch${nn(n)}`, phase: phaseName, schema: DRAFT_SUMMARY }))))
    .filter(Boolean)
  first.forEach((s) => noteDead(`${label}:ch${nn(s.chapter)}`, s))
  if (first.length === 0 && chapters.length > 0) {
    halt(`${label}: all ${chapters.length} agent(s) returned nothing -- upstream failure `
      + '(session limit, API, host), not any one chapter', chapters)
    return []
  }
  const good = first.filter(usable)
  let lost = chapters.filter((n) => !good.some((s) => s.chapter === n))
  if (lost.length > 0 && !halted) {
    log(`${label}: ${lost.length} chapter(s) without a clean draft, retrying: ${lost.join(' ')}`)
    const retried = (await parallel(lost.map((n) => () =>
      agent(promptOf(n), { label: `${label}-retry:ch${nn(n)}`, phase: phaseName, schema: DRAFT_SUMMARY }))))
      .filter(Boolean)
    retried.forEach((s) => noteDead(`${label}-retry:ch${nn(s.chapter)}`, s))
    if (retried.length === 0) {
      halt(`${label}-retry: all ${lost.length} retried agent(s) returned nothing -- upstream failure`, [])
    }
    for (const s of retried.filter(usable)) {
      good.push(s)
    }
    lost = chapters.filter((n) => !good.some((s) => s.chapter === n))
    for (const n of lost) {
      const s = retried.find((r) => r.chapter === n)
      unfinished.push(`ch${nn(n)}: no clean draft after a retry${s && s.note ? ' (' + s.note + ')' : ''}`)
    }
  } else if (lost.length > 0) {
    unfinished.push(`not drafted after the stop: chapters ${lost.join(' ')}`)
  }
  return good
}

// A script stage (landing, index, apply, a pitfall judge): one retry when the
// agent returns nothing, as for the chapters.
async function once(prompt, opts) {
  const first = await agent(prompt, opts)
  if (first) {
    return first
  }
  log(`${opts.label}: no answer, retrying once`)
  return await agent(prompt, Object.assign({}, opts, { label: opts.label + '-retry' }))
}

// Land a set of chapters and gate them.  The meta file, not the agent's word,
// decides what is complete: land.py refuses a meta that does not say so.
async function landRound(chapters, label) {
  if (chapters.length === 0) {
    return []
  }
  const report = await once(landPrompt(chapters), { label: label, phase: 'Land', schema: LAND_REPORT })
  if (!report) {
    unfinished.push(`${label}: the landing agent returned nothing; chapters ${chapters.join(' ')} not landed`)
    return []
  }
  log(`${label}: ${report.results.map((r) => 'ch' + nn(r.chapter) + '=' + r.status).join(' ')}; `
    + `landed gate errors ${report.landed_gate_errors}, pending links ${report.pending_links}`)
  if (!report.ok) {
    unfinished.push(`${label}: ${report.problems || 'not ok'}`)
  }
  landReports.push(Object.assign({ label: label }, report))
  return report.results
    .filter((r) => ['new', 'updated', 'unchanged'].includes(r.status)).map((r) => r.chapter)
}

// ---------------------------------------------------------------- prepare

phase('Prepare')
const prep = await once(preparePrompt(), { label: 'prepare', phase: 'Prepare', schema: PREPARE })
if (!prep || !prep.ok) {
  halt('prepare failed: ' + (prep ? (prep.problems || 'not ok') : 'no response'), [])
}

// ------------------------------------------------------ draft and land

const drafted = []
const landed = []
for (let start = 0; start < CHAPTERS.length; start += ROUND_SIZE) {
  if (halted) {
    unfinished.push(`not drafted after the stop: chapters ${CHAPTERS.slice(start).join(' ')}`)
    break
  }
  const batch = CHAPTERS.slice(start, start + ROUND_SIZE)
  const round = Math.floor(start / ROUND_SIZE) + 1
  phase('Draft')
  log(`round ${round}: drafting chapters ${batch.join(' ')}`)
  const got = await draftRound(batch, `draft-r${round}`, draftPrompt, 'Draft')
  drafted.push(...got)
  if (halted) {
    if (got.length > 0) {
      unfinished.push(`drafted but not landed after the stop: chapters ${got.map((s) => s.chapter).join(' ')}`)
    }
    const rest = CHAPTERS.slice(start + ROUND_SIZE)
    if (rest.length > 0) {
      unfinished.push(`not drafted after the stop: chapters ${rest.join(' ')}`)
    }
    break
  }
  phase('Land')
  const ok = await landRound(got.map((s) => s.chapter), `land-r${round}`)
  for (const n of ok) {
    if (!landed.includes(n)) {
      landed.push(n)
    }
  }
}
log(`drafted clean: ${drafted.length}/${CHAPTERS.length}; landed: ${landed.length}`)

// ---------------------------------------------------------------- rescan

const rescanned = []
if (!halted) {
  const pending = drafted.filter((s) => s.confidence === 'low' || s.open_questions > 0)
  if (pending.length > 0) {
    phase('Rescan')
    log(`rescan: ${pending.length} chapter(s) left open: ${pending.map((s) => s.chapter).join(' ')}`)
    const again = await draftRound(pending.map((s) => s.chapter), 'rescan',
      (n) => {
        const s = pending.find((p) => p.chapter === n)
        return rescanPrompt(n, `confidence ${s.confidence}, ${s.open_questions} open question(s)`)
      }, 'Rescan')
    for (const s of again) {
      rescanned.push({ chapter: s.chapter, confidence: s.confidence, open: s.open_questions })
      drafted[drafted.findIndex((d) => d.chapter === s.chapter)] = s
    }
    if (!halted && again.length > 0) {
      phase('Land')
      for (const n of await landRound(again.map((s) => s.chapter), 'land-rescan')) {
        if (!landed.includes(n)) {
          landed.push(n)
        }
      }
    }
  }
} else {
  skipped('rescan')
}

// ------------------------------------------------------------ collect

let collected = null
if (!halted && landed.length > 0) {
  collected = await once(collectPrompt(), { label: 'collect', phase: 'Index', schema: COLLECTED })
  if (!collected) {
    unfinished.push('collect: could not read the draft metadata; index chains and pitfalls skipped')
  } else if (collected.unreadable.length > 0) {
    unfinished.push(`unreadable meta files: ${collected.unreadable.join(' ')}`)
  }
}

// ------------------------------------------------------------------ index
//
// The chains need every chapter: the chains agent writes nothing unless all
// thirty pages are landed (this run or an earlier one), and a partial state
// leaves _index.md as it was.

let chains = null
let indexApply = null
if (!halted && collected) {
  phase('Index')
  chains = await once(chainsPrompt(), { label: 'index-chains', phase: 'Index', schema: INDEX_REPORT })
  if (!chains || !chains.wrote_file || !chains.ok) {
    unfinished.push(`index chains: ${chains ? (chains.problems || 'not ok') : 'the agent returned nothing'}`)
  } else {
    indexApply = await once(indexApplyPrompt(), { label: 'index-build', phase: 'Index', schema: INDEX_REPORT })
    if (!indexApply || !indexApply.ok) {
      unfinished.push(`index build: ${indexApply ? (indexApply.problems || 'not ok') : 'the agent returned nothing'}`)
    }
  }
} else if (halted) {
  skipped('index')
} else {
  unfinished.push('index: not rebuilt, no chapter metadata could be collected')
}

// ---------------------------------------------------------------- pitfalls

const verdicts = []
let applyReport = null
if (!halted && collected && landed.length > 0) {
  phase('Pitfalls')
  // Two chapters may report the same id: judge it once, as reported by both.
  const candidates = []
  for (const c of SEED_PITFALLS.concat(collected.pitfall_candidates)) {
    const known = candidates.find((x) => x.id === c.id)
    if (!known) {
      candidates.push(Object.assign({}, c, { chapter: c.chapter.slice() }))
      continue
    }
    for (const n of c.chapter) {
      if (!known.chapter.includes(n)) {
        known.chapter.push(n)
      }
    }
    if (c.what !== known.what) {
      log(`pitfalls: ${c.id} reported again with a different description by chapter ${c.chapter.join(' ')}; `
        + 'judged once with the first description')
      unfinished.push(`pitfall:${c.id}: a second description from chapter ${c.chapter.join(' ')} was not `
        + `judged separately: ${c.what}`)
    }
  }
  log(`pitfalls: ${candidates.length} candidate(s)`)
  let emptyInARow = 0
  for (let i = 0; i < candidates.length; i++) {
    if (halted) {
      unfinished.push(`not judged after the stop: ${candidates.slice(i).map((c) => c.id).join(' ')}`)
      break
    }
    const v = await once(pitfallPrompt(candidates[i]),
      { label: `pitfall:${candidates[i].id}`, phase: 'Pitfalls', schema: PITFALL_VERDICT })
    if (!v || !v.wrote_file) {
      unfinished.push(`pitfall:${candidates[i].id}: no verdict file`)
      emptyInARow = v ? 0 : emptyInARow + 1
      if (emptyInARow >= 2) {
        halt('pitfalls: two judges in a row returned nothing -- upstream failure', [])
        unfinished.push(`not judged after the stop: ${candidates.slice(i + 1).map((c) => c.id).join(' ')}`)
        break
      }
      continue
    }
    emptyInARow = 0
    verdicts.push(v)
    log(`  ${v.id}: ${v.outcome} -- ${v.reason}`)
  }
  if (!halted && verdicts.some((v) => v.outcome === 'added' || v.outcome === 'linked')) {
    applyReport = await once(applyPitfallsPrompt(), { label: 'apply-pitfalls', phase: 'Pitfalls', schema: APPLY_REPORT })
    if (!applyReport || !applyReport.ok) {
      unfinished.push(`apply pitfalls: ${applyReport ? (applyReport.problems || 'not ok') : 'the agent returned nothing'}`)
    }
  }
} else if (halted) {
  skipped('pitfalls')
}

// ------------------------------------------------------------ final gate
//
// Strict: every landed page, links to other chapters included, and the index.

let finalGate = null
if (!halted && landed.length > 0) {
  phase('Verify')
  finalGate = await once(finalGatePrompt(), { label: 'final-gate', phase: 'Verify', schema: FINAL_GATE })
  if (!finalGate) {
    unfinished.push('final gate: the agent returned nothing; the landed pages are not verified as a whole')
  } else {
    log(`final gate: ${finalGate.pages} page(s), ${finalGate.errors} error(s), index ${finalGate.index_errors}`)
    if (!finalGate.ok) {
      unfinished.push(`final gate: chapters ${finalGate.failing.join(' ') || '-'} failing, index errors `
        + `${finalGate.index_errors}${finalGate.problems ? ' (' + finalGate.problems + ')' : ''}`)
    }
  }
} else if (halted) {
  skipped('final gate')
}

// ----------------------------------------------------------------- record

const stats = {
  date: DATE,
  halted: halted,
  haltReason: haltReason,
  chaptersRequested: CHAPTERS,
  drafted: drafted.map((s) => ({ chapter: s.chapter, confidence: s.confidence, open: s.open_questions })),
  landed: landed.slice().sort((a, b) => a - b),
  rescanned: rescanned,
  land: landReports,
  index: { chains: chains, build: indexApply },
  pitfallVerdicts: verdicts,
  applyPitfalls: applyReport,
  finalGate: finalGate,
  // For the cut_content/ work (tickets 25.11-25.14): what the chapter drafters
  // judged unreachable.  The judgements themselves are in
  // tools/chapter_docs/judgements/chNN.json (never_shown, waves deployed=false).
  cutContentCandidates: collected ? collected.cut_content_candidates : [],
  crossChapter: collected ? collected.cross_chapter.length : 0,
  unfinished: unfinished,
}

phase('Record')
const record = await agent(`Record this workflow run.

1. Write the run record, verbatim, to
     ${REPO}\\devlog\\runs\\${DATE}-chapters-t258-NN.json
   where NN is 01, or the next free two-digit number if that name is taken
   (UTF-8, pretty-printed):
${JSON.stringify(stats, null, 2)}

2. ${halted
    ? 'The run STOPPED early (see haltReason). Do NOT write a devlog entry or touch the knowledge base; the record above is the whole report.'
    : `Write the devlog entry ${REPO}\\devlog\\${DATE}-chapter-docs-run.md (if that
   name is taken, ${DATE}-chapter-docs-run-2.md, -3 ...) in TRADITIONAL CHINESE,
   following ${REPO}\\devlog\\_conventions.md exactly (narrative; the point is
   the dead ends).  Sources: the record above, the draft metadata in
   ${DRAFTS}\\ch*.meta.json (open questions, guide_disagreements) and the
   verdicts in ${VERDICTS}\\*.json.  Cover honestly what was drafted, what came
   back unsettled and whether the rescan settled it, what land.py refused and
   why, which pitfall candidates were rejected and why, where the strategy site
   disagreed with the game, and every item in unfinished.  Do not claim
   anything the record does not support.`}

Return the files you wrote.`, { label: 'record', phase: 'Record', schema: DONE })
if (!record) {
  unfinished.push('record: the run record was not written -- the caller must archive the return value')
}

return Object.assign(stats, { recordFiles: record ? record.written : [] })
