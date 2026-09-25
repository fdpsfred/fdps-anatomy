// Ticket 25.5, start to finish, with no human in the loop.
//
// Writes the program_info/ pages that answer "how does this game work":
// battle arithmetic, the map AI, movement, spell and item effects, the chapter
// lifecycle, the cut-scene interpreter, dialogue, the village, the save/load
// flow, and the catalogue of original bugs.
//
// Load-bearing properties (ADR-0002, ADR-0007):
//
//   One item per agent.  The page list and the candidate lists live in this
//   script; every judging agent is handed exactly one page or one candidate.
//
//   Judgements go to files, agents return ~200 byte summaries.  A drafter
//   writes workspace/game_mechanics/drafts/<doc>.md and <doc>.meta.json; a
//   pitfall judge writes workspace/game_mechanics/pitfalls/<id>.json; the index
//   proposer writes workspace/game_mechanics/index_rows.json.  Later stages
//   read collect.py's compact index, never the pages themselves.
//
//   Judging agents write nothing else: no Ghidra, no src/, no knowledge-base
//   page.  Landing is done by scripts that judge nothing: land.py copies a
//   clean draft byte for byte, apply_kb.py applies the decided pitfalls.md and
//   _index.md edits verbatim and idempotently.
//
//   Gates after every landing: check_mechanics.py --landed on the pages,
//   --links on the shared pages apply_kb.py edited.  Back-sweep: drafts left
//   with open questions or low confidence are re-read once with the other
//   drafts as evidence.
//
//   Errors: one retry per page; a round in which every agent returns nothing,
//   or two sequential judges in a row returning nothing, stops the run
//   (upstream failure); a drafter reporting that Ghidra stopped answering stops
//   the run.  After a stop no knowledge-base stage runs, every skipped item is
//   listed in unfinished, and the run record is still archived.
//   Resume: every judging agent first checks for its own judgement file and
//   returns it if it is complete and clean, so re-running the whole script
//   redoes only what is missing; both landing scripts are idempotent.
//   Not detected: a src/ change after a draft was written (src/ is not
//   expected to move while this runs).
//
// args: {
//   date:  "YYYY-MM-DD"   required; names the devlog entry and the run record
//   only:  ["battle", ...] optional; restrict the drafting stage to these pages
// }

export const meta = {
  name: 'game-mechanics-ticket25-5',
  description: 'Draft, gate, land and index the program_info game-mechanics pages (ticket 25.5)',
  phases: [
    { title: 'Draft', detail: 'one agent per subsystem page, read-only, writes a draft file' },
    { title: 'Rescan', detail: 're-read drafts left open, using the other drafts as evidence' },
    { title: 'Bugs', detail: 'known_bugs.md from every drafter\'s bug notes' },
    { title: 'Land', detail: 'land.py copies complete, clean drafts; then the landed gate' },
    { title: 'Pitfalls', detail: 'one judge per candidate, sequential, writes a verdict file' },
    { title: 'Index', detail: 'propose program_info/_index.md rows' },
    { title: 'Apply', detail: 'apply_kb.py lands pitfall and index edits; link gate' },
    { title: 'Record', detail: 'devlog entry and run record' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const FD2 = 'C:\\Users\\fdpsf\\Documents\\fd2-anatomy'
const WS = REPO + '\\workspace\\game_mechanics'
const DRAFTS = WS + '\\drafts'
const VERDICTS = WS + '\\pitfalls'
const TOOLS = REPO + '\\tools\\game_mechanics'

const DATE = args && args.date
if (!DATE) {
  return { error: 'args.date (YYYY-MM-DD) is required: it names the devlog entry and the run record' }
}

// ------------------------------------------------------------------ pages
//
// scope: what the page must answer (Traditional Chinese, from the ticket).
// src:   the emitted modules the answer lives in -- the primary source.
// fd2:   the predecessor's page(s) to borrow coverage and layout from, never content.

const PAGES = [
  {
    doc: 'battle',
    title: '戰鬥數值與回合',
    scope: '物理攻擊的傷害、命中、爆擊、連擊與反擊的公式；經驗值的取得量與升級時的屬性成長；'
      + '狀態異常的施加、持續與解除；共用亂數（Watcom rand，從不 srand）在哪些判定被抽、抽幾次；'
      + '回合的各階段（玩家、敵方、友軍的順序，單位記錄 +5 狀態 byte 的意義，回合開始與結束時做的事）；'
      + '單位死亡與撤退的流程。',
    src: ['combat.c', 'cmbblow.c', 'unitatk.c', 'unitstat.c', 'btlturn.c', 'death.c', 'unit.c', 'btlact.c'],
    fd2: ['program_info\\battle.md'],
  },
  {
    doc: 'map_ai',
    title: '敵方 AI',
    scope: '電腦控制單位的行為種類（每一種怎麼選行動、怎麼選目標、何時移動何時原地）；'
      + '攻擊、道具、法術候選的評分公式；目標收集（範圍、直線、區域）與反擊可行性；行動的執行。',
    src: ['mapai.c', 'aiscore.c', 'aitarget.c', 'aiact.c'],
    fd2: ['program_info\\battle.md'],
  },
  {
    doc: 'movement',
    title: '移動範圍與地形',
    scope: '移動範圍怎麼算（地形消耗表、職業差異、zone of control、友軍與敵軍格的通行）；'
      + '路徑怎麼決定；地圖格的地形屬性怎麼讀；單位沿路徑行走。',
    src: ['movegrid.c', 'walk.c', 'maptile.c', 'mapcur.c'],
    fd2: ['program_info\\pathfind.md'],
  },
  {
    doc: 'spell',
    title: '法術與道具效果',
    scope: '施法的流程與 MP 消耗；法術效果怎麼分派（效果代碼到處理函式的對應）與每一種效果做什麼'
      + '（傷害、回復、狀態、召喚、特殊）；法術的命中與範圍；道具使用的效果套用與消耗；裝備。',
    src: ['spell.c', 'spellmnu.c', 'item.c', 'cmbspell.c', 'unititem.c'],
    fd2: ['program_info\\spell.md'],
  },
  {
    doc: 'chapter',
    title: '章節生命週期與事件分派',
    scope: '一章從進入到結束的流程；四張處理表（進入、結束、行動後、腳本事件）各自的位址、索引方式與呼叫時機；'
      + '回合事件與格子事件怎麼觸發；戰鬥勝敗判定；部署與援軍波次的機制。'
      + '這頁寫機制，不寫各章內容——各章的事件內容屬於 chapters/，連過去即可。',
    src: ['chapter.c', 'btlend.c', 'deploy.c', 'chinit1.c', 'chinit1b.c', 'chinit2.c', 'chinit2b.c',
      'chend1.c', 'chend1b.c', 'chend2.c', 'chend2b.c', 'chpost1.c', 'chpost2.c', 'chpost3.c',
      'chevt1.c', 'chevt2.c', 'chevt2b.c', 'chevt3.c', 'chevt4.c', 'chevt5.c', 'chevt5b.c', 'chevt6.c',
      'main.c'],
    fd2: ['program_info\\field.md'],
  },
  {
    doc: 'cutscene',
    title: '過場腳本直譯器',
    scope: '過場腳本（ICONnn、WINnn 等）在執行期怎麼被播放：誰在什麼時機呼叫直譯器、直譯迴圈的形狀、'
      + '每個 opcode 在執行期對遊戲狀態做了什麼（切換地圖、部署波次、顯示文字、音樂、動畫、等待、分支）。'
      + '腳本的位元組格式本身屬於 resource_info/ 的過場腳本格式文件（另一張票撰寫，看 resource_info/_index.md）；'
      + '存在就連過去，不重複 opcode 長度表。',
    src: ['icon.c', 'anim.c', 'transit.c'],
    fd2: [],
  },
  {
    doc: 'dialog',
    title: '對話系統',
    scope: '對話訊息視窗的開關、頭像、二選一提示、等待按鍵；文字的來源（哪個文字區塊、第幾條）與繪製；'
      + '對話由誰觸發。',
    src: ['msgwin.c', 'text.c'],
    fd2: ['program_info\\dialog.md'],
  },
  {
    doc: 'village',
    title: '村莊',
    scope: '兩章之間的村莊階段流程；武器店與秘密商店（庫存怎麼決定、買價與賣價）；教會（轉職的條件與結果、'
      + '復活的費用公式）；酒館（存讀檔入口、抽獎）；暗號表的查法；隊伍成員選單。',
    src: ['village.c', 'vilbar.c', 'vilmenu.c', 'vilshop.c', 'shop.c', 'shopdraw.c', 'church.c', 'roster.c'],
    fd2: ['program_info\\town_menu.md'],
  },
  {
    doc: 'save',
    title: '存讀檔流程',
    scope: '什麼時候能存檔與讀檔（含標題選單的「讀檔」「繼續」何時可用）、slot 的選擇流程、存檔時寫出哪些遊戲狀態、'
      + '讀檔後怎麼把遊戲狀態重建回去並從哪裡繼續。'
      + 'FDE.SAV 的位元組佈局、checksum 與加密算法屬於 resource_info/save.md；連過去，不重複。',
    src: ['save.c', 'savefile.c', 'savepnl.c', 'title.c'],
    fd2: ['program_info\\save.md'],
  },
]

const KNOWN_BUGS = { doc: 'known_bugs', title: '已知原版 bug', fd2: 'program_info\\known_bugs.md' }

const ALL_DOCS = PAGES.map((p) => p.doc).concat([KNOWN_BUGS.doc])

// Leads other tickets handed over.  Each is verified like any other.
const KNOWN_BUG_LEADS = [
  'From the save-format work (resource_info/save.md, and its row in rebuild_info/pitfalls.md): '
    + 'the title menu\'s load and continue entries both require the checksum to match AND the '
    + 'live-state chapter byte (+0x30c5) to differ from 0xff.  When FDE.SAV does not exist, the '
    + 'save/load screen creates it filled with 0xff, so a player who only ever saved slots between '
    + 'chapters and never saved during a battle cannot reach the load screen on the next start, '
    + 'even though the slots hold saves.',
]

// Candidates the parent ticket already knows about (25-kb-verification-pass.md,
// 共同要求).  Each is verified before it is written, like any other.
const SEED_PITFALLS = [
  {
    id: 'ch28-reinforcement-wave-halving',
    doc: ['chapter'],
    what: 'Chapter 28 reinforcements pick the wave as turn / 2; turn 7 truncates to wave 3, so wave 4 never deploys.',
    intuitive_wrong_way: 'Fix the arithmetic so wave 4 appears -- that adds three enemy soldiers the original never fields.',
    canonical_owner: 'chapters/ch28.md (per-chapter content, written by the chapter-pages work); '
      + 'if that page does not exist yet, the handler\'s comment in src/chevt6.c',
  },
  {
    id: 'password-table-index-minus-one',
    doc: ['village'],
    what: 'The village before chapter 26 looks up a 24-row password table with chapter index - 1 and reads past its end onto the stack.',
    intuitive_wrong_way: 'Add the missing row or clamp the index -- either changes what the game does.',
    canonical_owner: 'program_info/village.md',
  },
  {
    id: 'cutscene-opcode-0x61-not-debug',
    doc: ['cutscene'],
    what: 'Cut-scene opcode 0x61 has a debug-sounding name but is the real reward of chapter 27\'s hidden route.',
    intuitive_wrong_way: 'Drop it as a debug code -- the hidden-route reward disappears.',
    canonical_owner: 'program_info/cutscene.md',
  },
]

// ---------------------------------------------------------------- schemas

const DRAFT_SUMMARY = {
  type: 'object',
  additionalProperties: false,
  required: ['doc', 'wrote_file', 'gate_clean', 'confidence', 'open_questions', 'bugs', 'pitfalls',
    'ghidra_responding'],
  properties: {
    doc: { type: 'string', description: 'The page name, exactly as given' },
    wrote_file: { type: 'boolean', description: 'True once both the .md and the .meta.json exist' },
    gate_clean: { type: 'boolean', description: 'True when check_mechanics.py --draft reports no error' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    open_questions: { type: 'integer', description: 'Length of open_questions in the meta file' },
    bugs: { type: 'integer', description: 'Length of original_bugs in the meta file' },
    pitfalls: { type: 'integer', description: 'Length of pitfall_candidates in the meta file' },
    ghidra_responding: { type: 'boolean', description: 'False only if Ghidra stopped answering' },
    note: { type: 'string', description: 'At most one short line, or empty' },
  },
}

const COLLECTED = {
  type: 'object',
  additionalProperties: false,
  required: ['drafts', 'pitfall_candidates', 'original_bugs', 'cut_content_candidates', 'unreadable'],
  properties: {
    drafts: {
      type: 'array',
      items: {
        type: 'object',
        required: ['doc', 'complete', 'confidence', 'open_questions'],
        properties: {
          doc: { type: 'string' },
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
        required: ['id', 'doc', 'what', 'intuitive_wrong_way', 'canonical_owner'],
        properties: {
          id: { type: 'string' },
          doc: { type: 'array', items: { type: 'string' } },
          what: { type: 'string' },
          intuitive_wrong_way: { type: 'string' },
          canonical_owner: { type: 'string' },
        },
      },
    },
    original_bugs: {
      type: 'array',
      items: {
        type: 'object',
        required: ['id', 'doc', 'title', 'blocks_content'],
        properties: {
          id: { type: 'string' },
          doc: { type: 'string' },
          title: { type: 'string' },
          blocks_content: { type: 'boolean' },
        },
      },
    },
    cut_content_candidates: {
      type: 'array',
      items: {
        type: 'object',
        required: ['doc', 'what', 'why'],
        properties: { doc: { type: 'string' }, what: { type: 'string' }, why: { type: 'string' } },
      },
    },
    unreadable: { type: 'array', items: { type: 'string' } },
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
        required: ['doc', 'status'],
        properties: {
          doc: { type: 'string' },
          status: { type: 'string', enum: ['new', 'updated', 'unchanged', 'refused', 'missing'] },
          detail: { type: 'string' },
        },
      },
    },
    landed_gate_errors: { type: 'integer', description: 'Error count from check_mechanics.py --landed over the landed pages only' },
    pending_links: {
      type: 'array',
      items: { type: 'string' },
      description: 'Every pending-link warning, as "doc: target"',
    },
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
    wrote_file: { type: 'boolean', description: 'True once the verdict file exists' },
    reason: { type: 'string', description: 'One sentence' },
  },
}

const APPLY_REPORT = {
  type: 'object',
  additionalProperties: false,
  required: ['pitfalls_errors', 'index_errors', 'link_gate_errors', 'ok'],
  properties: {
    pitfalls_errors: { type: 'integer' },
    index_errors: { type: 'integer' },
    link_gate_errors: { type: 'integer' },
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

const HOUSE_RULES = `Rules for the page itself -- the gate (check_mechanics.py) enforces the
mechanical ones, the rest are the knowledge base's conventions:

  * Traditional Chinese prose.  Identifiers, addresses and code stay as they are.
  * First line "# <title>".  Then, as every program_info/ page does, a paragraph
    starting "**驗證對象**：" stating what this page is the sole canon for: the
    FDPS.LE address ranges / functions it covers and the src/ files they live
    in (see program_info/architecture.md and cd_audio.md for the form).
  * Tie every formula and rule to the function that implements it, written
    exactly as  \`fdps_name\`（\`0x1ecc7\`）  -- backticked name, then the
    function's ENTRY address in backticks inside （）.  The gate checks every such
    pair against ghidra_snapshot/functions.txt, and every ## section must
    contain at least one.  When you need to point at an instruction inside a
    function, write "\`fdps_name\` 內的 \`0x1ed02\`", never the （） form.
  * Conclusions only.  No account of how you found anything, no dates, no ticket
    numbers, no "後來發現／一開始以為", no devlog links.  Never cite workspace/ or
    legacy/.
  * Every fact has one owner.  Do not repeat what another page owns -- link to it
    with a relative link from program_info/:
      - binary formats            ../resource_info/  (see its _index.md)
      - table values and names    ../assets/
      - per-chapter content       ../chapters/
      - rebuild traps             ../rebuild_info/pitfalls.md
      - the other mechanics pages, by file name in the same folder:
        ${ALL_DOCS.map((d) => d + '.md').join(', ')}
      - original bugs             known_bugs.md  (a one-line mention plus link;
                                  the mechanism is written there, not here)
      - content an original bug makes unreachable: the cut_content/ folder,
                                  linked ONLY as ../cut_content/_index.md -- the
                                  folder is built by other work and its layout
                                  is not known yet, so never guess a deeper path
    Existing program_info/ pages (architecture.md, memory_layout.md,
    data_structures.md, code_pools.md, cd_audio.md) own what they own; link.
  * Formulas are written as formulas: operand order, integer division and its
    rounding, signedness, clamps, and the exact constants, as the machine code
    does them.  A formula "that is basically right" is wrong for a rebuild.`

const META_SHAPE = `{
  "doc": "<page name>",
  "complete": true,
  "rescanned": false,
  "confidence": "high" | "medium" | "low",
  "open_questions": ["<anything you could not settle, one line each>"],
  "original_bugs": [
    { "id": "<short-kebab-id>",
      "title": "<Traditional Chinese, one line>",
      "functions": ["fdps_name@0x1ecc7"],
      "mechanism": "<Traditional Chinese paragraph: what the code does wrong and why>",
      "player_visible": "<what a player sees>",
      "blocks_content": true | false,
      "blocked_content": "<what becomes unreachable, or empty>" }
  ],
  "pitfall_candidates": [
    { "id": "<short-kebab-id>",
      "what": "<English: what the original does that looks wrong>",
      "intuitive_wrong_way": "<English: how a rebuild would naturally write it, and what changes>",
      "canonical_owner": "<repo-relative path of the page that owns the fact>" }
  ],
  "cut_content_candidates": [
    { "what": "<content that exists but no path reaches, or a bug blocks>", "why": "<one line>" }
  ]
}`

function draftPrompt(p) {
  const srcList = p.src.map((f) => `${REPO}\\src\\${f}`).join('\n  ')
  const fd2List = p.fd2.length ? p.fd2.map((f) => `${FD2}\\${f}`).join('\n  ') : '(none -- FD2 has no counterpart)'
  return `You are writing ONE knowledge-base page for the FDPS reverse-engineering
project: program_info/${p.doc}.md, "${p.title}".  This is the only page you work
on.  Other pages are written by other agents; never write them.

What the page must answer:
  ${p.scope}

Sources, in order of authority:
  1. The emitted C source -- every function is already in C and its comments
     carry the semantics:
  ${srcList}
     Follow calls into other src/ files when a rule lives there.
  2. Ghidra (FDPS.LE), READ-ONLY, to confirm each formula against the machine
     code: constants, signedness, rounding, order of operations.  Load tools in
     ONE ToolSearch call:
       ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_xrefs_to,mcp__ghidra__read_memory"
     Never call a Ghidra tool that writes (rename, set comment, set prototype,
     create/delete anything).  If Ghidra does not answer, stop and return with
     ghidra_responding=false.
  3. The predecessor project's page, for COVERAGE and LAYOUT only -- never copy
     a number or a rule from it; FDPS differs:
  ${fd2List}
  4. Concrete table values: python ${REPO}\\.claude\\skills\\fdps-data\\query.py
     (show / where / find).  Player-visible numbers can be cross-checked
     against the mirrored strategy site:
       python ${REPO}\\tools\\guide_scrape\\guide_scrape.py search <pattern> --game fdps
     The site is written by a person and is sometimes wrong; the machine code
     wins, and a disagreement worth knowing goes on the page.

Also read ${REPO}\\program_info\\_index.md, ${REPO}\\resource_info\\_index.md and
${REPO}\\rebuild_info\\pitfalls.md so you link to what already exists instead of
repeating it.

${HOUSE_RULES}

RESUME CHECK FIRST.  If both ${DRAFTS}\\${p.doc}.md and
${DRAFTS}\\${p.doc}.meta.json already exist, the meta file has "complete": true,
and
  python ${TOOLS}\\check_mechanics.py --draft ${p.doc}
reports "clean", do nothing else: return the summary from the meta file.

WRITE ONLY THESE TWO FILES (Write tool, UTF-8):

  ${DRAFTS}\\${p.doc}.md          the page
  ${DRAFTS}\\${p.doc}.meta.json   what the rest of the workflow needs:

${META_SHAPE}

An original bug is behaviour the original executable gets wrong in a way a
player can notice.  A pitfall candidate is anything where writing the rebuild
"the natural way" would change behaviour (rebuild_info/_index.md's threshold);
do not edit pitfalls.md yourself -- a later stage does, one candidate at a time.
Empty arrays are fine.  Do not invent entries to fill them.

Nothing else is written: not program_info/, not src/, not rebuild_info/, not any
_index.md, not Ghidra.  The workflow lands your page afterwards.

Then run the gate on your draft and fix every error it reports, re-running
until it is clean:
  python ${TOOLS}\\check_mechanics.py --draft ${p.doc}
pending-link warnings are expected and need no fix.  Write "complete": true
only once the gate is clean.

If something cannot be settled, say so on the page ("未確認") and in
open_questions, and lower confidence.  A confident wrong formula costs far more
than an honest unknown: it is copied into the rebuild.

Return the summary.  Your final output is data for the workflow, not a message
to a human.`
}

function rescanPrompt(doc, why) {
  return `An earlier pass wrote ONE knowledge-base draft for the FDPS project and left
it unsettled.  You are re-reading that same page now that the other pages have
been drafted too.

Your page: ${DRAFTS}\\${doc}.md   (metadata: ${DRAFTS}\\${doc}.meta.json)
Why it came back: ${why}

RESUME CHECK FIRST: if the meta file already has "rescanned": true, this page
has had its second look; change nothing and return its summary.

Before changing anything, back up both files (python shutil.copyfile) to
${DRAFTS}\\${doc}.md.bak and ${DRAFTS}\\${doc}.meta.json.bak.

Read your page and its open_questions.  Then read the OTHER drafts in
${DRAFTS}\\ that touch the same functions -- another page may already have
settled a formula or a table your page depends on.  Reading their conclusions is
using evidence someone else produced, not re-judging their page.  Never edit
another page's files.  You may re-read src/ and Ghidra (READ-ONLY) to confirm.

${HOUSE_RULES}

If you can settle a question, rewrite ONLY ${DRAFTS}\\${doc}.md and
${DRAFTS}\\${doc}.meta.json (same shape as now): fix the page, remove the
settled entries from open_questions, adjust confidence.  Re-run the gate until
clean:
  python ${TOOLS}\\check_mechanics.py --draft ${doc}
If you cannot get it clean, restore both files from the .bak copies -- the first
draft was clean and must not be lost.

If nothing can be settled, change nothing on the page.  An honest open question
is a correct outcome; the second attempt is not a reason to manufacture a
conclusion.

In every case finish by setting "rescanned": true in the meta file, then delete
the .bak files.  Return the summary (same fields as the first pass).`
}

function bugsPrompt() {
  return `You are writing ONE knowledge-base page for the FDPS reverse-engineering
project: program_info/known_bugs.md, "${KNOWN_BUGS.title}" -- the catalogue of
bugs in the original executable FDPS.LE that a player can notice.

Every other mechanics page has been drafted; each drafter recorded the original
bugs it met in its metadata.  Start from those:
  python ${TOOLS}\\collect.py            (compact index; original_bugs lists them)
  ${DRAFTS}\\<doc>.meta.json             (each bug's mechanism, functions, blocked content)

Leads handed over by other work, to verify like any other:
  - ${KNOWN_BUG_LEADS.join('\n  - ')}

Also read:
  ${REPO}\\rebuild_info\\pitfalls.md, section "## 不能修的原版 bug" -- rows there
     that describe a player-visible bug belong in this catalogue too;
  ${REPO}\\CONTEXT.md, section "刪減與未用" -- the vocabulary for 被封住的內容;
  ${FD2}\\${KNOWN_BUGS.fd2} -- the predecessor's catalogue, for layout only.

Verify every bug yourself before it goes in: read the src/ function and confirm
in Ghidra (READ-ONLY; ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function,mcp__ghidra__get_xrefs_to,mcp__ghidra__read_memory").
A drafter's note is a lead, not a fact.  A bug you cannot confirm stays out of
the page and goes into open_questions.  If Ghidra does not answer, stop and
return with ghidra_responding=false.

One entry per bug, each with: 現象 (what the player sees), 成因 (the mechanism,
with every function as \`fdps_name\`（\`0xaddr\`）), and 重建 (one line: the
rebuild keeps it, linking to the matching row of ../rebuild_info/pitfalls.md
when there is one).  Ownership, strictly:
  * The MECHANISM is owned here.  pitfalls.md owns "how a rebuild would get it
    wrong"; link to it, do not repeat it.  A format fact owned by
    ../resource_info/ is linked, not repeated.
  * When the bug makes content unreachable (被封住的內容), that content is owned
    by cut_content/ -- write one line and link ../cut_content/_index.md, never
    a deeper guessed path.
  * Mechanics pages mention the bug in one line and link here; you do not edit
    them.

${HOUSE_RULES}

RESUME CHECK FIRST: if ${DRAFTS}\\known_bugs.md and ${DRAFTS}\\known_bugs.meta.json
both exist, the meta has "complete": true and the gate below is clean, return
at once.

Write ONLY ${DRAFTS}\\known_bugs.md and ${DRAFTS}\\known_bugs.meta.json, the
meta in this shape (original_bugs: exactly one per entry on the page):

${META_SHAPE}

Run the gate until clean, then set "complete": true:
  python ${TOOLS}\\check_mechanics.py --draft known_bugs

Return the summary.`
}

function collectPrompt() {
  return `Run this and return its JSON output unchanged, as the structured result. Do
not judge or edit anything.

  python ${TOOLS}\\collect.py`
}

function landPrompt(docs) {
  return `Land reviewed drafts into the knowledge base.  You judge nothing: land.py
copies a draft byte for byte when the gate passes and refuses it otherwise.

1.  python ${TOOLS}\\land.py ${docs.join(' ')} --json
2.  Run the landed gate over ONLY the pages step 1 reports as new, updated or
    unchanged:
      python ${TOOLS}\\check_mechanics.py --landed <those pages> --json

Report each page's status from step 1, the error count from step 2, and every
pending-link warning from step 2 as "doc: target".  If land.py refuses or
misses a page, report it in problems with its detail -- do NOT edit the draft or
the landed page to get it through.  ok is true only when every page landed and
step 2 has 0 errors.`
}

function pitfallPrompt(c) {
  return `ONE candidate for ${REPO}\\rebuild_info\\pitfalls.md -- the table of things a
rebuild gets wrong when written "the natural way".  You judge this candidate
only, and you write NOTHING but your verdict file; a script applies it later.

Candidate ${c.id} (reported by: ${c.doc.join(', ')})
  What the original does:     ${c.what}
  How a rebuild would err:    ${c.intuitive_wrong_way}
  Page that owns the fact:    ${c.canonical_owner}

Verdict file: ${VERDICTS}\\${c.id}.json
RESUME CHECK FIRST: if it exists and parses, return its outcome at once.

Steps:
  1. Verify it.  Read the owning page (mechanics pages are landed under
     program_info/), the src/ function, and confirm in Ghidra READ-ONLY
     (ToolSearch "select:mcp__ghidra__decompile_function,mcp__ghidra__disassemble_function").
     Not confirmed -> rejected_not_verified.
  2. Threshold (rebuild_info/_index.md): something the natural rebuild gets
     WRONG, not merely complicated.  Below it -> rejected_below_threshold.
  3. Read pitfalls.md as it is now, AND every verdict file already in
     ${VERDICTS}\\ (earlier candidates decided in this run, not yet applied).
     If an existing row or an earlier verdict already covers this ->
     already_covered.  If an existing row covers it but its 正典 column does
     not link the owning page -> linked.
  4. Otherwise -> added: one new row for the right "## " section.

Write the verdict file (UTF-8 JSON):
  { "id": "${c.id}", "outcome": "<outcome>", "reason": "<one sentence>",
    "section": "<exact ## heading text without the ##, for added>",
    "row": "<the complete new table row, for added>",
    "old_line": "<the existing row, copied EXACTLY as it is now, for linked>",
    "new_line": "<that row with the link added and nothing else changed, for linked>" }
Rows: columns 事項 | 照直覺會怎麼寫 | 正典, Traditional Chinese, no numbers or
layouts the owning page has -- link it, relative from rebuild_info/ (e.g.
../program_info/village.md).  Link only a page that exists now; if the owning
page does not exist, point at the src/ file's comment in plain text instead.

Return the verdict with wrote_file true once the file exists.`
}

function bugLinkPrompt(b) {
  const id = 'link-' + b.id
  return `ONE cross-link between ${REPO}\\rebuild_info\\pitfalls.md and
${REPO}\\program_info\\known_bugs.md.  You write NOTHING but your verdict file; a
script applies it later.

known_bugs.md catalogues the original bug "${b.title}" (id ${b.id}).  Read its
entry.  The two pages must point at each other without repeating each other:
known_bugs.md owns the mechanism, pitfalls.md owns how a rebuild would get it
wrong.

Verdict file: ${VERDICTS}\\${id}.json
RESUME CHECK FIRST: if it exists and parses, return its outcome at once.

Read pitfalls.md as it is now and every verdict file already in ${VERDICTS}\\.
  * A row there (or an earlier verdict's new row) is about this bug:
      - its 正典 column already links ../program_info/known_bugs.md -> already_covered;
      - otherwise -> linked, with old_line = the row copied EXACTLY and new_line =
        the same row with "[已知原版 bug](../program_info/known_bugs.md)" added to
        the 正典 column and nothing else changed.  If the row re-explains the
        mechanism at length, leave its text alone and say so in reason.
  * No row is about this bug: would the natural rebuild "fix" it (threshold in
    rebuild_info/_index.md)?  Yes -> added, a row in "不能修的原版 bug" whose 正典
    links ../program_info/known_bugs.md.  No -> rejected_below_threshold.

Write the verdict file (UTF-8 JSON):
  { "id": "${id}", "outcome": "<outcome>", "reason": "<one sentence>",
    "section": "不能修的原版 bug", "row": "<for added>",
    "old_line": "<for linked>", "new_line": "<for linked>" }

Return the verdict with id ${id} and wrote_file true once the file exists.`
}

function indexPrompt(docs) {
  return `Propose rows for ${REPO}\\program_info\\_index.md, one per page:
  ${docs.join(', ')}

Read each landed page (${REPO}\\program_info\\<doc>.md) and the existing rows of
_index.md.  For each page write one table row in the existing style:
  | [\`<doc>.md\`](<doc>.md) | <one line, Traditional Chinese, what the page answers> |

Write ONLY ${WS}\\index_rows.json as {"<doc>": "<row>", ...} (UTF-8).  Do not
edit _index.md; a script adds each row unless the page already has one.

Also check ${REPO}\\README.md and ${REPO}\\rebuild_info\\_index.md for a
statement these pages make false, and report it in summary; do not edit them.

Return the files you wrote.`
}

function applyPrompt(doPitfalls, doIndex, bugsLanded) {
  const steps = []
  if (doPitfalls) {
    steps.push(`python ${TOOLS}\\apply_kb.py pitfalls`)
  }
  if (doIndex) {
    steps.push(`python ${TOOLS}\\apply_kb.py index`)
  }
  steps.push(`python ${TOOLS}\\check_mechanics.py --links rebuild_info/pitfalls.md program_info/_index.md`
    + (bugsLanded ? ' program_info/known_bugs.md' : '') + ' --json')
  return `Apply decided edits to shared knowledge-base pages, then gate them.  You judge
nothing and edit nothing by hand.

Run, in order:
  ${steps.join('\n  ')}

apply_kb.py prints {"errors": n, "results": [...]}; report each run's error
count (0 for a step not run).  The last command's "errors" is
link_gate_errors.  If anything was refused or a link is broken, say exactly which in
problems; do NOT fix it by hand.  ok is true only when all three counts are 0.`
}

// ------------------------------------------------------------- run state

const unfinished = []
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
    unfinished.push(`not done after the stop: ${remaining.join(' ')}`)
  }
}

function skipped(stage) {
  unfinished.push(`${stage}: skipped because the run stopped (${haltReason})`)
}

function usable(s) {
  return s && s.wrote_file && s.gate_clean
}

function noteGhidra(label, s) {
  if (s && s.ghidra_responding === false) {
    halt(`${label}: Ghidra stopped responding`, [])
  }
}

// Draft a set of pages: one round, the outage check, then one retry for each
// page that came back without a clean draft, with its own outage check.  A
// single page cannot tell an outage from a one-off failure, so it gets its
// retry first; two empty answers in a row stop the run.
async function draftRound(pages, label, promptOf, phaseName) {
  const first = (await parallel(pages.map((p) => () =>
    agent(promptOf(p), { label: `${label}:${p.doc}`, phase: phaseName, schema: DRAFT_SUMMARY }))))
    .filter(Boolean)
  first.forEach((s) => noteGhidra(`${label}:${s.doc}`, s))
  if (first.length === 0 && pages.length > 1) {
    halt(`${label}: all ${pages.length} agent(s) returned nothing -- upstream failure `
      + '(session limit, API, host), not any one page', [])
  }
  const good = first.filter(usable)
  let lost = pages.filter((p) => !good.some((s) => s.doc === p.doc))
  if (lost.length > 0 && !halted) {
    log(`${label}: ${lost.length} page(s) without a clean draft, retrying: ${lost.map((p) => p.doc).join(' ')}`)
    const retried = (await parallel(lost.map((p) => () =>
      agent(promptOf(p), { label: `${label}-retry:${p.doc}`, phase: phaseName, schema: DRAFT_SUMMARY }))))
      .filter(Boolean)
    retried.forEach((s) => noteGhidra(`${label}-retry:${s.doc}`, s))
    if (retried.length === 0 && (lost.length > 1 || first.length === 0)) {
      halt(`${label}-retry: all ${lost.length} retried agent(s) returned nothing -- upstream failure`, [])
    }
    for (const s of retried.filter(usable)) {
      good.push(s)
    }
    lost = pages.filter((p) => !good.some((s) => s.doc === p.doc))
    for (const p of lost) {
      const s = retried.find((r) => r.doc === p.doc)
      unfinished.push(`${p.doc}: no clean draft after a retry${s && s.note ? ' (' + s.note + ')' : ''}`)
    }
  } else if (lost.length > 0) {
    unfinished.push(`not drafted after the stop: ${lost.map((p) => p.doc).join(' ')}`)
  }
  return good
}

// Second look at drafts left open.  The first draft stays if the second look
// returns nothing or breaks the gate (the rescanner restores its backup).
async function rescan(drafts, label) {
  const pending = drafts.filter((s) => s.confidence === 'low' || s.open_questions > 0)
  if (pending.length === 0 || halted) {
    return []
  }
  log(`${label}: ${pending.length} draft(s) left open`)
  const got = (await parallel(pending.map((s) => () =>
    agent(rescanPrompt(s.doc, `confidence ${s.confidence}, ${s.open_questions} open question(s)`),
      { label: `${label}:${s.doc}`, phase: 'Rescan', schema: DRAFT_SUMMARY })))).filter(Boolean)
  got.forEach((s) => noteGhidra(`${label}:${s.doc}`, s))
  if (got.length === 0 && pending.length > 1) {
    halt(`${label}: all ${pending.length} agent(s) returned nothing -- upstream failure`, [])
  }
  const notes = []
  for (const s of pending) {
    const r = got.find((x) => x.doc === s.doc)
    if (!r) {
      unfinished.push(`${s.doc}: the rescan returned nothing; the first draft stands with its open questions`)
      continue
    }
    if (usable(r)) {
      drafts[drafts.findIndex((d) => d.doc === r.doc)] = r
    } else {
      unfinished.push(`${r.doc}: the rescan reports the draft failing the gate`)
    }
    notes.push(`${r.doc}: confidence ${r.confidence}, ${r.open_questions} open`)
  }
  return notes
}

// Sequential judges on the same shared page.  Two empty answers in a row mean
// the failure is upstream, not in the candidates.
async function judgeInSequence(items, labelOf, promptOf, phaseName) {
  const verdicts = []
  let emptyInARow = 0
  for (let i = 0; i < items.length; i++) {
    if (halted) {
      unfinished.push(`not judged after the stop: ${items.slice(i).map(labelOf).join(' ')}`)
      break
    }
    const v = await agent(promptOf(items[i]), { label: labelOf(items[i]), phase: phaseName, schema: PITFALL_VERDICT })
    if (!v || !v.wrote_file) {
      unfinished.push(`${labelOf(items[i])}: no verdict file`)
      emptyInARow = v ? 0 : emptyInARow + 1
      if (emptyInARow >= 2) {
        halt(`${phaseName}: two judges in a row returned nothing -- upstream failure`,
          items.slice(i + 1).map(labelOf))
        break
      }
      continue
    }
    emptyInARow = 0
    verdicts.push(v)
    log(`  ${v.id}: ${v.outcome} -- ${v.reason}`)
  }
  return verdicts
}

// ------------------------------------------------------------------ draft

phase('Draft')
const only = (args && args.only) || null
const pages = only ? PAGES.filter((p) => only.includes(p.doc)) : PAGES
log(`drafting ${pages.length} page(s): ${pages.map((p) => p.doc).join(' ')}`)
const drafted = await draftRound(pages, 'draft', draftPrompt, 'Draft')
log(`drafted clean: ${drafted.length}/${pages.length}`)

phase('Rescan')
const rescanned = await rescan(drafted, 'rescan')

// ------------------------------------------------------------ known bugs
//
// The one barrier with a reason: this page is built from every other page's
// bug notes, so it waits for all of them.

let bugsDraft = null
if (!halted) {
  phase('Bugs')
  const got = await draftRound([KNOWN_BUGS], 'bugs', () => bugsPrompt(), 'Bugs')
  bugsDraft = got[0] || null
  if (bugsDraft) {
    const bugList = [bugsDraft]
    rescanned.push(...await rescan(bugList, 'rescan-bugs'))
    bugsDraft = bugList[0]
  }
} else {
  skipped('known_bugs draft')
}

// ------------------------------------------------------------------- land
//
// The judgement file decides what is done, not the agent's word: a page lands
// only if its meta file says complete.

let collected = null
let landReport = null
let landed = []
const candidatesToLand = drafted.map((s) => s.doc).concat(bugsDraft ? ['known_bugs'] : [])
if (!halted && candidatesToLand.length > 0) {
  phase('Land')
  collected = await agent(collectPrompt(), { label: 'collect', phase: 'Land', schema: COLLECTED })
  if (!collected) {
    unfinished.push('land: could not read the draft metadata, nothing landed')
  } else {
    if (collected.unreadable.length > 0) {
      unfinished.push(`unreadable meta files: ${collected.unreadable.join(' ')}`)
    }
    const complete = collected.drafts.filter((d) => d.complete).map((d) => d.doc)
    const toLand = candidatesToLand.filter((d) => complete.includes(d))
    for (const d of candidatesToLand.filter((x) => !complete.includes(x))) {
      unfinished.push(`${d}: the agent reported a clean draft but its meta file is not complete`)
    }
    if (toLand.length > 0) {
      landReport = await agent(landPrompt(toLand), { label: 'land', phase: 'Land', schema: LAND_REPORT })
    }
    if (toLand.length === 0) {
      unfinished.push('land: no page had a complete meta file, nothing landed')
    } else if (!landReport) {
      unfinished.push('land: the landing agent returned nothing')
    } else {
      log(`land: ${landReport.results.map((r) => r.doc + '=' + r.status).join(' ')}; `
        + `landed gate errors ${landReport.landed_gate_errors}`)
      if (!landReport.ok) {
        unfinished.push(`land: ${landReport.problems || 'not ok'}`)
      }
      landed = landReport.results
        .filter((r) => ['new', 'updated', 'unchanged'].includes(r.status)).map((r) => r.doc)
    }
  }
} else if (halted) {
  skipped('land')
}

// ----------------------------------------------------- pitfalls and links

let pitfallVerdicts = []
if (!halted && landed.length > 0 && collected) {
  phase('Pitfalls')
  const candidates = []
  for (const c of SEED_PITFALLS.concat(collected.pitfall_candidates)) {
    if (!candidates.some((x) => x.id === c.id)) {
      candidates.push(c)
    }
  }
  log(`pitfalls: ${candidates.length} candidate(s)`)
  pitfallVerdicts = await judgeInSequence(candidates, (c) => `pitfall:${c.id}`, pitfallPrompt, 'Pitfalls')
  if (landed.includes('known_bugs')) {
    const catalogued = collected.original_bugs.filter((b) => b.doc === 'known_bugs')
    log(`bug cross-links: ${catalogued.length}`)
    pitfallVerdicts = pitfallVerdicts.concat(
      await judgeInSequence(catalogued, (b) => `buglink:${b.id}`, bugLinkPrompt, 'Pitfalls'))
  }
} else if (halted) {
  skipped('pitfalls')
}

// ------------------------------------------------------------------ index

let indexReport = null
if (!halted && landed.length > 0) {
  phase('Index')
  indexReport = await agent(indexPrompt(landed), { label: 'index', phase: 'Index', schema: DONE })
  if (!indexReport) {
    unfinished.push('index: the proposer returned nothing')
  }
} else if (halted) {
  skipped('index')
}

// ------------------------------------------------------------------ apply

let applyReport = null
const applyPitfalls = pitfallVerdicts.some((v) => v.outcome === 'added' || v.outcome === 'linked')
if (!halted && (applyPitfalls || indexReport)) {
  phase('Apply')
  applyReport = await agent(applyPrompt(applyPitfalls, !!indexReport, landed.includes('known_bugs')), { label: 'apply', phase: 'Apply', schema: APPLY_REPORT })
  if (!applyReport) {
    unfinished.push('apply: the agent returned nothing; pitfall and index edits are decided but not applied')
  } else {
    log(`apply: pitfalls errors ${applyReport.pitfalls_errors}, index errors ${applyReport.index_errors}, `
      + `link gate errors ${applyReport.link_gate_errors}`)
    if (!applyReport.ok) {
      unfinished.push(`apply: ${applyReport.problems || 'not ok'}`)
    }
  }
} else if (halted) {
  skipped('apply')
}

// ----------------------------------------------------------------- record

const stats = {
  date: DATE,
  halted: halted,
  haltReason: haltReason,
  pagesRequested: pages.map((p) => p.doc),
  drafted: drafted.map((s) => ({ doc: s.doc, confidence: s.confidence, open: s.open_questions,
    bugs: s.bugs, pitfalls: s.pitfalls })),
  knownBugs: bugsDraft,
  rescanned: rescanned,
  land: landReport,
  pitfallVerdicts: pitfallVerdicts,
  apply: applyReport,
  originalBugs: collected ? collected.original_bugs : [],
  // For the cut_content/ work: what the drafters saw blocked or unreachable,
  // and every page that links cut_content/_index.md ahead of its existence.
  cutContentCandidates: collected ? collected.cut_content_candidates : [],
  pendingLinks: landReport ? landReport.pending_links : [],
  index: indexReport,
  unfinished: unfinished,
}

phase('Record')
const record = await agent(`Record this workflow run.

1. Write the run record, verbatim, to
     ${REPO}\\devlog\\runs\\${DATE}-mechanics-t255-NN.json
   where NN is 01, or the next free two-digit number if that name is taken
   (UTF-8, pretty-printed):
${JSON.stringify(stats, null, 2)}

2. ${halted
    ? 'The run STOPPED early (see haltReason). Do NOT write a devlog entry or touch the knowledge base; the record above is the whole report.'
    : `Write the devlog entry ${REPO}\\devlog\\${DATE}-game-mechanics.md (if that
   name is taken, ${DATE}-game-mechanics-2.md, -3 ...) in TRADITIONAL CHINESE,
   following ${REPO}\\devlog\\_conventions.md exactly (narrative; the point is
   the dead ends).  Sources: the record above and the draft metadata in
   ${DRAFTS}\\*.meta.json and the verdicts in ${VERDICTS}\\*.json (rejected
   pitfall candidates and why).  Cover honestly what was drafted, what came
   back unsettled and whether the rescan settled it, what land.py or
   apply_kb.py refused, which pitfall candidates were rejected and why, and
   every item in unfinished.  Do not claim anything the record does not
   support.`}

Return the files you wrote.`, { label: 'record', phase: 'Record', schema: DONE })
if (!record) {
  unfinished.push('record: the run record was not written -- the caller must archive the return value')
}

return Object.assign(stats, { recordFiles: record ? record.written : [] })
