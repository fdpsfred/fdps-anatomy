// Ticket 22's closing sweep: every concern in emit_issues.json, looked at once,
// after all 514 functions have landed.
//
// The per-batch rescan was taken out of emit_ticket22.js by ticket 22.1, and the
// work it did moves here. ADR-0007 4 asks for a rescan before the WORK ends; the
// work ended when the last function landed, and this is that rescan. It can do
// three things the per-batch one could not:
//
//   Every neighbour exists. No concern is unanswerable because the function it
//   was waiting on has not been emitted: all 514 verdict files are on disk.
//
//   Each concern is asked once. Nothing is re-asked because a neighbour moved.
//
//   Concerns are grouped by root cause first. One investigation settles a whole
//   family -- the same index-bound question asked about five functions, the same
//   inline-expansion reading asked twice -- instead of five agents each deriving
//   it from nothing.
//
// Shape:
//
//   Recover      a killed landing leaves src/ and tests/ dirty; clear it, bounded
//                by path, before anything else runs
//   Plan         freeze the concern list (sweep.py items) and read what is done
//   Cluster      one agent groups the open concerns by root cause, only when no
//                valid clusters.json exists yet. Grouping is not ruling: every
//                concern still gets its own ruling from the investigator
//   Investigate  one agent per cluster, read-only, in parallel. Writes a finding
//                file and returns two hundred bytes
//   Rescan       clusters that came back with a concern still open get one more
//                look, this time allowed to read every other cluster's finding.
//                A second attempt is not a reason to force a conclusion
//   Land         serial. A cluster whose finding needs code goes through fix,
//                independent review and the build gate before its commit; one
//                that needs Ghidra corrected goes through a bookkeeper; the rest
//                is pure transcription, done in one mechanical commit
//   Knowledge    the notes the findings raised for the knowledge base, integrated
//                by one writer
//
// Rulings are transcribed into emit_issues.json by sweep.py apply, never by an
// agent's hand, so what "handed to ticket 24" means is defined in one place.
//
// Stops (ADR-0007 5): a Ghidra or toolchain failure an agent reports; a round in
// which every agent returned nothing; three agent calls in a row returning
// nothing. After a stop nothing downstream runs -- no landing of work that was
// never checked, no knowledge pages -- and the report says why and what is left.
// Resuming is running this again: finding files are the progress record, the
// landing commits are the rest, and sweep.py plan reads both.
//
// args: { label: string }   run label for the report, e.g. "sweep-01"

export const meta = {
  name: 'fdps-sweep-ticket22',
  description: 'Closing sweep over emit_issues.json: cluster by root cause, investigate each cluster read-only, rescan, land rulings serially behind review and the build gate',
  phases: [
    { title: 'Recover', detail: 'clear what a killed landing left in src/ and tests/' },
    { title: 'Plan', detail: 'freeze the concern list, read progress off disk' },
    { title: 'Cluster', detail: 'group the open concerns by root cause' },
    { title: 'Investigate', detail: 'one read-only agent per cluster' },
    { title: 'Rescan', detail: 'second look at clusters left open, with every other finding as evidence' },
    { title: 'Land', detail: 'fix, review, gate, commit -- one cluster at a time' },
    { title: 'Knowledge', detail: 'integrate the knowledge-base notes the findings raised' },
  ],
}

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const SW = REPO + '\\workspace\\code_emit\\sweep'
const PY = 'python tools/code_emit/sweep.py'
const A = (typeof args === 'string' && args.length) ? JSON.parse(args) : (args || {})
const LABEL = A.label || 'sweep'
const MAX_FIX_ROUNDS = A.maxFixRounds || 3
const TRAILER = 'Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>'

// ---------------------------------------------------------------- schemas

const STOP_FIELDS = {
  ghidra_unreachable: {
    type: 'boolean',
    description: 'True only when a Ghidra MCP call still fails after one quick retry.',
  },
  toolchain_unreachable: {
    type: 'boolean',
    description: 'True only when the build cannot run at all (DOSBox-X, Watcom, preflight). '
      + 'A build that runs and reports errors is not this.',
  },
  stop_detail: { type: 'string', description: 'The error you saw when a stop flag is set.' },
}
function withStops(props) { return Object.assign({}, props, STOP_FIELDS) }

const RECOVER = {
  type: 'object', additionalProperties: false,
  required: ['out_of_bounds', 'clean_now'],
  properties: withStops({
    tree_was_dirty: { type: 'boolean' },
    out_of_bounds: { type: 'boolean' },
    out_of_bounds_paths: { type: 'array', items: { type: 'string' } },
    discarded_paths: { type: 'array', items: { type: 'string' } },
    clean_now: { type: 'boolean' },
    note: { type: 'string' },
  }),
}

const PLAN = {
  type: 'object', additionalProperties: false,
  required: ['clusters_valid', 'clusters'],
  properties: withStops({
    clusters_valid: {
      type: 'boolean',
      description: 'check-clusters printed ok true. False also when clusters.json does not exist.',
    },
    cluster_errors: { type: 'array', items: { type: 'string' } },
    clusters: {
      type: 'array',
      description: 'The clusters array sweep.py plan printed, verbatim',
      items: {
        type: 'object', additionalProperties: false,
        required: ['id', 'kind', 'first_valid', 'finding_valid', 'needs_rescan', 'landed'],
        properties: {
          id: { type: 'string' }, kind: { type: 'string' }, size: { type: 'integer' },
          first_valid: { type: 'boolean' }, rescanned: { type: 'boolean' },
          rescan_valid: { type: 'boolean' }, finding_valid: { type: 'boolean' },
          needs_rescan: { type: 'boolean' }, has_code_change: { type: 'boolean' },
          has_ghidra_fixes: { type: 'boolean' }, landed: { type: 'boolean' },
        },
      },
    },
  }),
}

const CLUSTER = {
  type: 'object', additionalProperties: false,
  required: ['written', 'valid', 'clusters'],
  properties: {
    written: { type: 'boolean' },
    valid: { type: 'boolean', description: 'check-clusters printed ok true on the file as you left it' },
    clusters: { type: 'integer' },
    largest: { type: 'integer' },
    note: { type: 'string' },
  },
}

const FINDING = {
  type: 'object', additionalProperties: false,
  required: ['cluster', 'finding_ok'],
  properties: withStops({
    cluster: { type: 'string' },
    finding_ok: { type: 'boolean', description: 'check-finding reported ok for the file you wrote' },
    settled: { type: 'integer' }, code_change: { type: 'integer' },
    handoff_t23: { type: 'integer' }, handoff_t24: { type: 'integer' },
    still_open: { type: 'integer' },
    label_ok: { type: 'integer' }, label_corrected: { type: 'integer' },
    ghidra_fixes: { type: 'integer' }, kb_notes: { type: 'integer' },
    note: { type: 'string', description: 'One line for the orchestrator' },
  }),
}

const FIX = {
  type: 'object', additionalProperties: false,
  required: ['cluster', 'build_pass'],
  properties: withStops({
    cluster: { type: 'string' },
    files_touched: { type: 'array', items: { type: 'string' } },
    build_pass: { type: 'boolean' },
    build_detail: { type: 'string' },
    cannot_apply: {
      type: 'boolean',
      description: 'The finding\'s change cannot be made as written. Say why in note; do not improvise another change.',
    },
    note: { type: 'string' },
  }),
}

const REVIEW = {
  type: 'object', additionalProperties: false,
  required: ['cluster', 'approved', 'wrote_review'],
  properties: withStops({
    cluster: { type: 'string' },
    approved: { type: 'boolean' },
    wrote_review: { type: 'boolean' },
    blocking_count: { type: 'integer' },
    note: { type: 'string' },
  }),
}

const GATE = {
  type: 'object', additionalProperties: false,
  required: ['gate_pass'],
  properties: withStops({
    gate_pass: { type: 'boolean' },
    tests_total: { type: 'integer' },
    tests_failed: { type: 'integer' },
    detail: { type: 'string' },
  }),
}

const BOOK = {
  type: 'object', additionalProperties: false,
  required: ['committed', 'applied', 'tree_clean'],
  properties: withStops({
    committed: { type: 'boolean' },
    commit_line: { type: 'string' },
    applied: { type: 'array', items: { type: 'string' }, description: 'Cluster ids sweep.py apply accepted' },
    apply_failed: { type: 'array', items: { type: 'string' } },
    ghidra_applied: { type: 'integer' },
    ghidra_gate_clean: { type: 'boolean' },
    tree_clean: { type: 'boolean' },
    problems: { type: 'string' },
  }),
}

const DISCARD = {
  type: 'object', additionalProperties: false,
  required: ['clean_now'],
  properties: { clean_now: { type: 'boolean' }, note: { type: 'string' } },
}

const KNOW = {
  type: 'object', additionalProperties: false,
  required: ['committed', 'notes_seen'],
  properties: {
    committed: { type: 'boolean' },
    notes_seen: { type: 'integer' },
    integrated: { type: 'integer' },
    already_present: { type: 'integer' },
    rejected: { type: 'integer' },
    files: { type: 'array', items: { type: 'string' } },
    tree_clean: { type: 'boolean' },
    note: { type: 'string' },
  },
}

// ---------------------------------------------------------------- prompts

const ENV = [
  'FDPS.LE is a 1997 Traditional Chinese DOS game (Watcom C/C++ 10.0a, DOS/4G LE',
  'module). All 514 of its game functions have been rebuilt into C under src/ of',
  REPO + ', each reviewed from the assembly and gated',
  'by the build. Ghidra has FDPS.LE open as the only program; leave the program',
  'parameter empty on every MCP call.',
  '',
  'Load the Ghidra tools you need in ONE ToolSearch call:',
  '  ToolSearch "select:mcp__ghidra__get_plate_comment,mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_function_callees,mcp__ghidra__get_xrefs_to,mcp__ghidra__get_function_signature,mcp__ghidra__read_memory,mcp__ghidra__search_instructions,mcp__ghidra__get_assembly_context,mcp__ghidra__get_function_by_address"',
  '',
  'If a Ghidra call fails, retry it ONCE. If the retry fails too, stop, set',
  'ghidra_unreachable and put the error in stop_detail. Never guess what Ghidra',
  'would have said.',
  '',
  'Shell: use the Bash tool. Do not read or write UTF-8 files through PowerShell',
  'Get-Content/Set-Content: on this Traditional Chinese Windows machine they turn',
  'Chinese text into mojibake without an error. Do not judge a file by printing',
  'Chinese to the console either -- the console mangles it even when the file is',
  'fine.',
].join('\n')

const EVIDENCE = [
  '# Evidence you have, all of it, right now',
  '',
  '- Ghidra, read-only: every function\'s disassembly, every caller and callee,',
  '  get_xrefs_to over a global, search_instructions over the whole image. The',
  '  ORIGINAL IMAGE\'S DATA IS IN GHIDRA TOO: read_memory gives you the shipped',
  '  initial value of any global or table, so "what does this table hold" is not a',
  '  ticket 23 question. Ticket 23 is about how the REBUILD will define a symbol.',
  '- The emit and review verdict of EVERY function, including all the neighbours a',
  '  concern was waiting on: workspace/code_emit/verdicts/<addr>.emit.json and',
  '  <addr>.review.json. They are other agents\' judgements -- cite them, check',
  '  them against the assembly where it matters, do not just believe them.',
  '- The rebuilt source and tests, src/ and tests/ (read them, do not edit them).',
  '- The objects the last build produced, workspace/code_emit/out/objs/*.OBJ, and',
  '  the Watcom disassembler that reads them, a native Windows binary:',
  '    C:\\Users\\fdpsf\\Documents\\WATCOM_10_series\\WATCOM_10.0a\\BINNT\\WDISASM.EXE',
  '- The shipped game files in fdps_game_files/ and the knowledge base',
  '  (program_info/, resource_info/, assets/, chapters/, rebuild_info/).',
  '',
  'You may NOT run the build (build_emit.py, gate.py): other investigators are',
  'running at the same time and the build owns shared directories. If a question',
  'can only be answered by compiling something new, say exactly what to compile',
  'and leave that concern open.',
].join('\n')

const STANDARD = [
  '# The standard (ADR-0001)',
  '',
  'The rebuild must be FUNCTIONALLY equivalent to the original: the same externally',
  'visible behaviour. Register allocation, instruction choice, stack layout, and how',
  'the original source happened to be spelled are all outside that standard. So:',
  '',
  '- A concern is BLOCKING only if the shipped src/ BEHAVES differently from the',
  '  original on some input the game can actually produce. "The original probably',
  '  wrote this with _inline" or "open-coded where the original called" is not',
  '  blocking -- both spellings behave the same. A preference is a note, never a',
  '  code change.',
  '- Findings record findings; they do not legislate. Do not write conditional',
  '  obligations ("if X is proven, the code should be changed to Y") -- someone',
  '  downstream will execute them without checking the rule. Decide it now against',
  '  this standard, or leave it open.',
  '- Faithful to the original includes its bugs. A read past the end of a table,',
  '  an unbounded cursor, stack garbage sent to a device: if the original does it,',
  '  the rebuild must do the SAME thing, and a "fix" is a behaviour change. What the',
  '  rebuild must not do is behave differently -- e.g. read a different neighbour',
  '  than the original because the rebuild lays its globals out differently. That',
  '  is the kind of thing that IS blocking.',
].join('\n')

const RULINGS = [
  '# Rulings, one per concern',
  '',
  '  settled       the question is answered, with evidence, and src/ is right as it',
  '                is. The answer says what the evidence is.',
  '  code_change   answered, and src/ behaves differently from the original.',
  '                Requires blocking true, and a code_changes entry naming the file,',
  '                the function, the exact change, the behaviour difference it',
  '                removes, and what test pins it (expected values from the',
  '                assembly or the Ghidra emulator, never from the C itself).',
  '  handoff_t23   genuinely cannot be answered until ticket 23 defines the game\'s',
  '                data in the rebuild (what type or initialiser the rebuild\'s',
  '                symbol gets, where it lands relative to its neighbours). The',
  '                answer says precisely what ticket 23 must decide or check.',
  '  handoff_t24   genuinely needs the game running -- an observation only play in',
  '                DOSBox-X can make. The answer says precisely what to do in the',
  '                game and what to look for, so ticket 24 can check it without',
  '                re-reading any of this.',
  '  open          none of the above: the evidence does not exist anywhere you can',
  '                reach and the question is not about data definitions or play.',
  '                Say what would settle it.',
  '',
  'blocking is true or false on every ruling, judged by the standard above.',
  '',
  'Handing off is not a way out of work. If read_memory, a caller\'s assembly or a',
  'neighbour\'s verdict answers it, it is settled, not handed off. Equally, do not',
  'force a conclusion the evidence does not support -- an honest "open" is worth',
  'more than an invented "settled".',
].join('\n')

const FINDING_FILE = [
  '# The finding file',
  '',
  'Write it with the Write tool (UTF-8) to',
  '  ' + SW + '\\findings\\<cluster>.json',
  'with this shape:',
  '  {',
  '    "cluster": "<id>",',
  '    "root_cause": "what the concerns in this cluster actually have in common, as',
  '                   established -- not the hypothesis you were given",',
  '    "investigation": "what you looked at and what it showed, with addresses",',
  '    "rulings": [ { "id": "<addr>#<n>", "ruling": "...", "blocking": false,',
  '                   "answer": "the conclusion, self-contained" } ],',
  '    "code_changes": [ { "ids": ["<addr>#<n>"], "file": "src/x.c", "function": "...",',
  '                        "change": "...", "behaviour_difference": "...", "test": "..." } ],',
  '    "ghidra_fixes": [ { "kind": "rename_function|set_plate_comment|set_function_prototype|rename_data|set_data_type|other",',
  '                        "target": "<addr>", "value": "exactly what to write", "why": "..." } ],',
  '    "kb_notes": [ { "file": "rebuild_info/pitfalls.md", "note": "the fact, as a conclusion" } ]',
  '  }',
  'One ruling per concern id in the cluster, no more, no fewer. Mirrors (entries',
  'another agent wrote about the same concern) are part of the evidence and follow',
  'their root automatically; they get no ruling of their own.',
  '',
  'kb_notes: a conclusion the knowledge base does not have yet. In particular, if',
  'you find something where writing the rebuild the intuitive way would diverge from',
  'the original, that belongs in rebuild_info/pitfalls.md. Check the page first so',
  'you do not propose what is already there. Written in Traditional Chinese, as a',
  'conclusion, not as a story of how you got there.',
  '',
  'Then validate it and fix it until it says ok:',
  '  cd ' + REPO + ' && ' + PY + ' check-finding <cluster>',
  'finding_ok in your summary is what that command said, not what you intended.',
].join('\n')

function investigatePrompt(c) {
  return [ENV, '', '# Role: investigate cluster ' + c.id + '. Read-only.', '',
    'Every concern in emit_issues.json that was still open when the last function',
    'landed has been grouped by a suspected common root cause. This cluster has ' + c.size + '.',
    'Print them in full, with every mirror:',
    '  cd ' + REPO + ' && ' + PY + ' show ' + c.id,
    '',
    'The grouping is a hypothesis. Investigate the root cause once, then rule on',
    'each concern individually -- concerns in one cluster can end up with different',
    'rulings, and if the hypothesis was wrong, say so in root_cause.',
    '',
    'You change nothing: not src/, not tests/, not Ghidra, not tools/code_emit/data/.',
    'The only file you write is the finding file. Code changes and Ghidra corrections',
    'you conclude are needed go INTO the finding; a separate landing stage applies',
    'them after review.',
    '',
    EVIDENCE, '', STANDARD, '', RULINGS, '', FINDING_FILE, '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function labelPrompt(c) {
  return [ENV, '', '# Role: label review for ' + c.id + '. Read-only.', '',
    'These concerns were marked resolved during emit, most of them by the per-batch',
    'rescan that has since been removed. Their CONCLUSIONS are not being re-derived.',
    'What is being checked is the LABEL on each: whether what it calls blocking,',
    'required, or "must change" actually holds under the standard below, and',
    'whether what the answer says the source does is what src/ does now.',
    '',
    '  cd ' + REPO + ' && ' + PY + ' show ' + c.id,
    '',
    'This is where the one known wrong label lives: 0002af60 was marked BLOCKING',
    'because the open-coded RGB packing in src/palette.c is really an inline',
    'expansion of fdps_pack_rgb and "should therefore be a call". Both behave the',
    'same, and a plain call would add a CALL the original does not have; the',
    'open-coded source is correct and stays. If you are reviewing 0002af60, that',
    'label is corrected, with that reason. Every other label gets the same test on',
    'its own merits.',
    '',
    STANDARD, '',
    '# Rulings',
    '  label_ok         the label holds as written. answer says why, briefly.',
    '  label_corrected  it does not. label_correction says what the label should be',
    '                   and why; answer says what you checked. The original answer',
    '                   is kept as it is -- your correction sits beside it.',
    'Label rulings take no blocking field. If you notice src/ genuinely behaving',
    'differently from the original while you look, put it in the investigation text',
    'plainly: it is not something a label review can change, and the orchestrator',
    'will pick it up.',
    '',
    FINDING_FILE, '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function rescanPrompt(c) {
  return [ENV, '', '# Role: rescan cluster ' + c.id + '. Read-only.', '',
    'This cluster was investigated once and came back with at least one concern',
    'still ruled open. Every other cluster has been investigated as well, and you',
    'may now use their findings as evidence: citing another agent\'s finished',
    'judgement is not making it for them.',
    '',
    '  cd ' + REPO + ' && ' + PY + ' show ' + c.id + '           the concerns',
    '  ' + SW + '\\findings\\' + c.id + '.json        the first pass',
    '  cd ' + REPO + ' && ' + PY + ' findings-index      every cluster\'s conclusions in brief',
    '  ' + SW + '\\findings\\<other>.json     any of them in full',
    '',
    'A SECOND ATTEMPT IS NOT A REASON TO FORCE A CONCLUSION. If the evidence still',
    'does not exist, the concern stays open, and saying so plainly is the right',
    'result. What you are looking for is the evidence the first pass could not',
    'see: another cluster that settled the same underlying question, a neighbour',
    'function\'s verdict, a caller that bounds the value.',
    '',
    'Write the complete finding again -- every ruling, including the ones the first',
    'pass settled, copied through unless you have reason to change them -- to',
    '  ' + SW + '\\findings\\' + c.id + '.rescan.json',
    'in the same shape, and validate it with check-finding ' + c.id + '.',
    '',
    EVIDENCE, '', STANDARD, '', RULINGS, '', FINDING_FILE.replace('<cluster>.json', '<cluster>.rescan.json'), '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function clusterPrompt(errors) {
  return [ENV, '', '# Role: group concerns by root cause. You rule on nothing.', '',
    'Read ' + SW + '\\index.json. It holds every open equivalence concern from the',
    'emit of 514 functions -- each with its id, function, file, what and needs, and',
    'the ids of the mirrors that repeat it.',
    '',
    'Group them by ROOT CAUSE, so that one investigation can settle a whole family.',
    'Families already named in the emit devlogs, as seeds (not as a closed list):',
    '  - the terrain modifier table read at index 6, past its end (0001c520,',
    '    00019f80, fdps_draw_cursor_info_panel)',
    '  - PROEQU.DAT covers class codes 0x00-0x23 but codes run to 0x27 (00025fe0,',
    '    00018ba0)',
    '  - CD request headers with bytes that are never written, so stack residue is',
    '    sent (0003be36, 0003c51c, 0003c7aa, 0003c0c8)',
    '  - the floating-indicator queue cursor has no upper bound (0001f510)',
    '  - uninitialised tick variables / stack residue deciding behaviour (00031780,',
    '    000232b0)',
    '  - whether an index can run past a buffer (00011da0, 00023050, 00014550)',
    '  - how an _inline expansion is read (0002af60, 00014550)',
    '  - waiting on a neighbour that has since been emitted -- group these by the',
    '    neighbour they wait on',
    '  - waiting on ticket 23 data, or on play (ticket 24) -- group by WHAT they need',
    '    (the same table, the same scene), not merely by the fact that they wait',
    '',
    'Rules:',
    '  - Every id in index.json in exactly one cluster. Mirrors are NOT listed; they',
    '    follow their root.',
    '  - A cluster is a set of concerns one investigator can settle together.',
    '    At most 8 per cluster. A concern with nothing in common with any other is a',
    '    cluster of one, and that is fine.',
    '  - root_cause is your one-paragraph hypothesis, for the investigator. You are',
    '    not answering anything.',
    '  - Cluster ids c01, c02, ... in any order.',
    '',
    'Write ' + SW + '\\clusters.json with the Write tool:',
    '  { "clusters": [ { "id": "c01", "root_cause": "...", "items": ["0001c520#0", ...] } ] }',
    'then check it and fix it until it prints ok true:',
    '  cd ' + REPO + ' && ' + PY + ' check-clusters',
    (errors && errors.length
      ? '\nA previous clusters.json failed that check with:\n  ' + errors.slice(0, 20).join('\n  ') + '\nRewrite it from scratch.'
      : ''),
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function planPrompt() {
  return ['# Role: Plan. Mechanical. Run commands, report output, judge nothing.', '',
    'cd ' + REPO + ' and run, in the foreground:',
    '  ' + PY + ' items',
    '     (builds the frozen concern list the first time; afterwards it only says frozen)',
    '  ' + PY + ' check-clusters',
    '     (fails with a missing-file error when no clustering has been done yet; that',
    '      is clusters_valid false with the error in cluster_errors, not a problem)',
    '  ' + PY + ' plan',
    '',
    'Report clusters_valid and cluster_errors from check-clusters, and the clusters',
    'array from plan VERBATIM -- every entry, same order, same values. plan lists the',
    'label-review clusters even when clusters.json is missing; report those too.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function recoverPrompt() {
  return ['# Role: Recovery. Mechanical.', '',
    'A previous run of this sweep may have been killed in the middle of landing a',
    'code change. Clear what it left before anything starts.',
    '',
    '1.  cd ' + REPO + ' && git status --porcelain',
    '    Nothing printed: set clean_now true and return.',
    '2.  These paths are the landing stage\'s own and may be cleaned:',
    '      src/  tests/  tools/code_emit/data/  ghidra_snapshot/',
    '    ANY OTHER dirty path is out of bounds: change nothing, set out_of_bounds true,',
    '    list the paths verbatim, return. The run stops for a human.',
    '3.  Otherwise discard, all three commands, in this order:',
    '      git reset -q -- src tests tools/code_emit/data ghidra_snapshot',
    '      git checkout -- src tests tools/code_emit/data ghidra_snapshot',
    '      git clean -fd src tests',
    '    The reset matters: the reviewer runs git add -N, and an intent-to-add file',
    '    survives checkout as a zero-byte file otherwise.',
    '    Never touch workspace/ (the finding files are the progress record) and never',
    '    anything committed -- no revert, no amend, no reset that moves HEAD.',
    '4.  If ghidra_snapshot/ was dirty, Ghidra itself may have been changed and saved',
    '    before the kill, so re-export instead of trusting the reverted text:',
    '      ToolSearch "select:mcp__ghidra__run_ghidra_script"',
    '      run_ghidra_script ' + REPO + '\\tools\\ghidra_snapshot\\ExportGhidraSnapshot.java',
    '    If that produces a diff, commit it alone:',
    '      git add ghidra_snapshot && git commit -m "sweep: 前一輪中斷，重新匯出 Ghidra 快照" -m "' + TRAILER + '"',
    '5.  git status --porcelain must print nothing now. Report clean_now.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

const C89 = [
  'Code rules (the same the emit ran under): C89 -- declarations first in every',
  'block, no // comments; symbol names byte-identical to Ghidra; every local and',
  'parameter named for what it holds (the build refuses decompiler names like',
  'iVar1 with E9001); tests in tests/<stem>.c registered in run_<stem>_tests;',
  'expected values from the assembly or the Ghidra emulator, never from running',
  'the C you just wrote; no test hooks in src/. rebuild_info/emit_pipeline.md is',
  'the canon for all of it.',
].join('\n')

function fixPrompt(c, round, why) {
  return [ENV, '', '# Role: apply the code change of cluster ' + c.id + ' (round ' + round + ').', '',
    'The sweep ruled that src/ behaves differently from the original here. The',
    'change to make is decided already, in the code_changes array of the cluster\'s',
    'final finding; this prints which file that is:',
    '  cd ' + REPO + ' && ' + PY + ' final-path ' + c.id,
    'Read that finding and the concerns it answers (' + PY + ' show ' + c.id + ').',
    '',
    'Make exactly that change and the test it names, nothing else. Before you edit,',
    'read the function\'s disassembly yourself: the change must make the rebuild do',
    'what the ORIGINAL does, and if the finding\'s description turns out not to',
    'match the assembly, do not improvise another fix -- set cannot_apply, say why,',
    'change nothing (or undo what you changed).',
    (round > 1
      ? '\nThe previous round did not pass: ' + why + '\nIf ' + SW + '\\review\\' + c.id + '.json exists, fix what it lists as blocking.'
      : ''),
    '',
    C89, '',
    'Build and run the tests, in the foreground:',
    '  cd ' + REPO + ' && python tools/code_emit/build_emit.py all',
    'Report build_pass from what it printed. Do not commit, do not touch Ghidra.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function reviewPrompt(c, round) {
  return [ENV, '', '# Role: independent review of the code change for cluster ' + c.id + ' (round ' + round + ').', '',
    'Another agent changed src/ and tests/ to carry out a sweep ruling. You check',
    'it from the assembly. You do not trust the fixer and you do not trust the',
    'finding: the finding is a claim, the assembly is the evidence.',
    '',
    '1. See the change, new files included:',
    '     cd ' + REPO + ' && git add -N -- src tests && git --no-pager diff HEAD -- src tests',
    '2. Read the finding (' + PY + ' final-path ' + c.id + ' prints which file) and the',
    '   concerns (' + PY + ' show ' + c.id + ').',
    '3. For each changed function, read its disassembly yourself and confirm:',
    '   - the change makes the rebuild BEHAVE like the original where it did not;',
    '   - nothing else in the function\'s behaviour moved;',
    '   - it is a behaviour change at all -- a change that only re-spells equivalent',
    '     code is not what the sweep authorises (ADR-0001) and is blocking here;',
    '   - the new test\'s expected values come from the assembly or the emulator,',
    '     and it would fail on the old code.',
    '   ' + C89.split('\n').join('\n   '),
    '4. Write ' + SW + '\\review\\' + c.id + '.json (Write tool):',
    '     { "cluster": "' + c.id + '", "approved": true|false, "blocking": ["..."], "notes": ["..."] }',
    '',
    'You change no code and no Ghidra. Your final message is the summary object and',
    'nothing else.',
  ].join('\n')
}

function gatePrompt(c) {
  return ['# Role: Gate. You judge, you do not fix.', '',
    'The code change for sweep cluster ' + c.id + ' passed review. Run the build gate over',
    'the working tree. It takes well over ten minutes, longer than one foreground',
    'command may run, so start it detached and wait for its exit file:',
    '',
    '  cd ' + REPO + ' && rm -f workspace/code_emit/sweep/gate.rc && (python tools/build_gate/gate.py check --target emittest > workspace/code_emit/sweep/gate.log 2>&1; echo $? > workspace/code_emit/sweep/gate.rc) &',
    '',
    'then, repeatedly until the rc file exists (each call waits at most nine minutes):',
    '',
    '  cd ' + REPO + ' && for i in $(seq 1 54); do [ -f workspace/code_emit/sweep/gate.rc ] && break; sleep 10; done; cat workspace/code_emit/sweep/gate.rc 2>/dev/null || echo still-running',
    '',
    'Then read workspace/build_gate/result.json for the verdict and the detail.',
    'Report what it said. Do not edit anything to make it pass. If the gate cannot',
    'run at all, that is toolchain_unreachable, which is different from a build that',
    'ran and failed.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function discardPrompt(c, why) {
  return ['# Role: discard the unlanded code change of cluster ' + c.id + '. Mechanical.', '',
    'It will not land: ' + why,
    '',
    '  cd ' + REPO,
    '  git reset -q -- src tests',
    '  git checkout -- src tests',
    '  git clean -fd src tests',
    '  git status --porcelain',
    'Touch nothing else -- not workspace/, not anything committed. Report whether the',
    'last command printed nothing.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function bookkeepPrompt(c, codeState) {
  const withCode = codeState === 'landed'
  const failed = codeState && codeState !== 'landed'
  return [ENV, '', '# Role: Bookkeeper for sweep cluster ' + c.id + '. No judgement.', '',
    (withCode
      ? 'Its code change passed independent review and the build gate and is in the working tree.'
      : failed
        ? 'Its code change did NOT land (' + codeState + '). The rulings are transcribed anyway, marked as such.'
        : 'It needs no code change.'),
    '',
    '1. Ghidra corrections. Read ghidra_fixes in the finding',
    '   (cd ' + REPO + ' && ' + PY + ' final-path ' + c.id + '  prints which file).',
    '   Apply each exactly as written, with the matching MCP call (rename_function_by_address,',
    '   set_plate_comment, set_function_prototype, rename_data, apply_data_type, ...). You',
    '   transcribe decisions; if one cannot be applied, say so in problems and move on --',
    '   do not adjust it to make it fit. If there are none, skip to step 3.',
    '2. Only if Ghidra changed:',
    '     run_ghidra_script ' + REPO + '\\tools\\ghidra_baseline\\AuditGhidraBaseline.java',
    '       (its "# Gate" section: orphan code ranges 0, error bookmarks 0)',
    '     list_bookmarks category "Bad Instruction" must be empty',
    '     save_program',
    '     run_ghidra_script ' + REPO + '\\tools\\ghidra_snapshot\\ExportGhidraSnapshot.java',
    '3. Transcribe the rulings -- this is the only way they reach emit_issues.json:',
    '     cd ' + REPO + ' && ' + PY + ' apply ' + c.id
      + (failed ? ' --code-failed "' + String(codeState).replace(/"/g, '\'') + '"' : ''),
    '   If it refuses, report the cluster in apply_failed with what it printed, commit',
    '   nothing, and restore tools/code_emit/data with git checkout.',
    '4. git status --porcelain. Anything dirty outside src/, tests/,',
    '   tools/code_emit/data/ and ghidra_snapshot/ must be committed ALONE first, with',
    '   its own honest subject:  git add <path> && git commit -m ... -- <path>',
    '5. Stage and commit:',
    '     git add src tests tools/code_emit/data ghidra_snapshot',
    '     git --no-pager diff --staged --stat',
    '   Write the message into a UTF-8 file with the Write tool and commit with',
    '   git commit -F <file>. Subject:',
    '     sweep: ' + c.id + ' ' + (withCode ? '改碼落地，reviewer 通過、build gate 通過' : '裁決落地'),
    '   a blank line, one line in Traditional Chinese on the root cause, a blank line,',
    '     ' + TRAILER,
    '6. commit_line is  <git log --format=%h -1> ' + c.id + '  -- do not pipe the Chinese',
    '   subject back through the console. git status --porcelain must print nothing.',
    '',
    'Load Ghidra tools only if step 1 has work, in ONE ToolSearch call.',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function transcribePrompt(ids) {
  return ['# Role: transcribe rulings. Mechanical, no judgement.', '',
    'These sweep clusters need neither code nor Ghidra changes; their rulings only',
    'have to reach emit_issues.json. For each id, in order:',
    '  cd ' + REPO + ' && ' + PY + ' apply <id>',
    'ids: ' + ids.join(' '),
    '',
    'An id that apply refuses goes into apply_failed with what it printed; carry on',
    'with the rest. Then:',
    '  git status --porcelain      only tools/code_emit/data/emit_issues.json may be dirty;',
    '                              anything else, stop and report it in problems',
    '  git add tools/code_emit/data/emit_issues.json',
    'Write the message into a UTF-8 file with the Write tool, git commit -F it. Subject:',
    '  sweep: ' + ids.length + ' 群的裁決轉錄進 emit_issues.json',
    'a blank line, one line naming the ids, a blank line,',
    '  ' + TRAILER,
    'commit_line is  <git log --format=%h -1> transcribe  . git status --porcelain must',
    'print nothing afterwards.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

function knowledgePrompt() {
  return ['# Role: integrate the sweep\'s knowledge-base notes.', '',
    '  cd ' + REPO + ' && ' + PY + ' kb-notes',
    'prints every note the findings raised, each with the file it was meant for.',
    '',
    'The knowledge base records conclusions; CLAUDE.md and each folder\'s _index.md',
    'say how. Every fact has one owner: before adding a note, find whether the',
    'owning page already says it -- then it is already_present. A note that is',
    'really process narrative ("we found that...", "during the sweep...") or that',
    'contradicts the canon without evidence is rejected. rebuild_info/pitfalls.md',
    'takes things where writing the rebuild the intuitive way diverges from the',
    'original, in its own format (read rebuild_info/_index.md for the bar). Written',
    'in Traditional Chinese. Update any _index.md your additions change.',
    'Do not touch src/, tests/, Ghidra or tools/code_emit/data/.',
    '',
    'Re-read what you changed for narrative (when something was found, what it used',
    'to say, phases) and remove it. Then commit only the knowledge-base files, with a',
    'message written to a UTF-8 file and git commit -F:',
    '  sweep: 疑慮總掃的結論整合進知識庫',
    'a blank line, a one-line summary, a blank line,',
    '  ' + TRAILER,
    'git status --porcelain must print nothing afterwards. If there were no notes',
    'worth adding, commit nothing and say so.',
    '',
    'Your final message is the summary object and nothing else.',
  ].join('\n')
}

// ------------------------------------------------------------ agent driver

let stopped = null
let nullStreak = 0

function stop(kind, detail) {
  if (!stopped) {
    stopped = { kind: kind, detail: detail }
    log('STOP (' + kind + '): ' + detail)
  }
}

// One retry, then null. Three nulls in a row anywhere is upstream failure
// (ADR-0007 5.2): agents die one at a time, not in runs.
async function run(prompt, opts) {
  if (stopped) return null
  let out = null
  for (let attempt = 1; attempt <= 2 && !stopped; attempt++) {
    out = await agent(prompt, opts)
    if (out) break
    log('  ' + opts.label + ': no return (attempt ' + attempt + ')')
  }
  if (!out) {
    nullStreak++
    if (nullStreak >= 3) stop('upstream_failure', 'three agent calls in a row returned nothing')
    return null
  }
  nullStreak = 0
  if (out.ghidra_unreachable === true) stop('ghidra_disconnect', out.stop_detail || '(no detail)')
  if (out.toolchain_unreachable === true) stop('toolchain_unreachable', out.stop_detail || '(no detail)')
  return out
}

// A fan-out where every agent came back empty is upstream failure regardless of
// the streak count, which parallel completion order can interleave.
function checkRound(name, results, sent) {
  if (sent > 0 && results.filter(Boolean).length === 0) {
    stop('upstream_failure', 'every agent of the ' + name + ' round returned nothing (' + sent + ' sent)')
  }
}

async function plan() {
  const p = await run(planPrompt(), { label: 'plan', phase: 'Plan', schema: PLAN })
  if (!p) stop('plan_failed', 'the plan stage returned nothing')
  return p
}

const report = {
  label: LABEL, stopped: null,
  investigated: [], rescanned: [], landed: [], code_landed: [], code_failed: [],
  unfinished: [], transcribed: [], knowledge: null,
}
function unfinished(id, why) { report.unfinished.push({ id: id, why: why }) }

// ---------------------------------------------------------------- recover

phase('Recover')
const rec = await run(recoverPrompt(), { label: 'recover', phase: 'Recover', schema: RECOVER })
if (!rec) {
  return { error: 'recovery returned nothing; refusing to work on a tree nobody looked at', label: LABEL }
}
if (rec.out_of_bounds) {
  return { error: 'working tree dirty outside the sweep\'s paths', paths: rec.out_of_bounds_paths || [], label: LABEL }
}
if (!rec.clean_now) {
  return { error: 'tree not clean after recovery', detail: rec.note || '', label: LABEL }
}

// ------------------------------------------------------------ plan/cluster

phase('Plan')
let p = await plan()
if (p && !p.clusters_valid) {
  phase('Cluster')
  let errors = p.cluster_errors || []
  for (let attempt = 1; attempt <= 2 && !stopped; attempt++) {
    const cl = await run(clusterPrompt(attempt > 1 ? errors : null),
      { label: 'cluster#' + attempt, phase: 'Cluster', schema: CLUSTER })
    phase('Plan')
    p = await plan()
    if (p && p.clusters_valid) {
      log('clustered: ' + (cl ? cl.clusters + ' clusters, largest ' + cl.largest : '(agent returned nothing, file validates)'))
      break
    }
    errors = (p && p.cluster_errors) || ['(no detail)']
  }
  if (p && !p.clusters_valid) stop('clustering_failed', (p.cluster_errors || []).slice(0, 5).join('; '))
}
if (stopped || !p) {
  report.stopped = stopped
  return report
}
log(p.clusters.length + ' clusters: ' + p.clusters.filter((c) => c.landed).length + ' already landed')

// ------------------------------------------------------------ investigate

phase('Investigate')
const toInvestigate = p.clusters.filter((c) => !c.landed && !c.first_valid)
for (let pass = 1; pass <= 2 && toInvestigate.length && !stopped; pass++) {
  const batch = pass === 1 ? toInvestigate : toInvestigate.filter((c) => !c.first_valid)
  if (!batch.length) break
  log('investigate pass ' + pass + ': ' + batch.length + ' clusters')
  const res = await parallel(batch.map((c) => () =>
    run(c.kind === 'label' ? labelPrompt(c) : investigatePrompt(c),
      { label: 'inv:' + c.id + (pass > 1 ? '#2' : ''), phase: 'Investigate', schema: FINDING })))
  checkRound('investigate', res, batch.length)
  res.filter(Boolean).forEach((s) => report.investigated.push(s.cluster))
  if (stopped) break
  // The file decides, not the agent's summary (ADR-0007 5.1).
  p = await plan()
  if (!p) break
  const byId = {}
  p.clusters.forEach((c) => { byId[c.id] = c })
  toInvestigate.forEach((c) => { c.first_valid = byId[c.id] ? byId[c.id].first_valid : false })
}
if (!stopped) toInvestigate.filter((c) => !c.first_valid).forEach((c) => unfinished(c.id, 'no valid finding after two attempts'))

// ---------------------------------------------------------------- rescan
//
// Only after every first pass is in, because a rescan's evidence is the other
// clusters' findings. This is the one barrier the sweep needs.

if (!stopped) {
  phase('Rescan')
  const toRescan = p.clusters.filter((c) => !c.landed && c.needs_rescan)
  log('rescan: ' + toRescan.length + ' clusters with a concern still open')
  for (let pass = 1; pass <= 2 && toRescan.length && !stopped; pass++) {
    const batch = pass === 1 ? toRescan : toRescan.filter((c) => c.needs_rescan)
    if (!batch.length) break
    const res = await parallel(batch.map((c) => () =>
      run(rescanPrompt(c), { label: 'rescan:' + c.id + (pass > 1 ? '#2' : ''), phase: 'Rescan', schema: FINDING })))
    checkRound('rescan', res, batch.length)
    res.filter(Boolean).forEach((s) => report.rescanned.push(s.cluster))
    if (stopped) break
    p = await plan()
    if (!p) break
    const byId = {}
    p.clusters.forEach((c) => { byId[c.id] = c })
    toRescan.forEach((c) => { c.needs_rescan = byId[c.id] ? byId[c.id].needs_rescan : true })
  }
  // A cluster whose rescan never produced a valid file still lands on its first
  // pass -- its open rulings stay open -- but the report names it.
  if (!stopped) toRescan.filter((c) => c.needs_rescan).forEach((c) => unfinished(c.id, 'rescan did not produce a valid file; landing the first pass'))
}

// ------------------------------------------------------------------ land
//
// Serial: the reviewer reads the working tree against HEAD, so exactly one code
// change may be in flight, and emit_issues.json has one writer at a time.

if (!stopped && p) {
  phase('Land')
  const ready = p.clusters.filter((c) => !c.landed && c.finding_valid)
  const code = ready.filter((c) => c.has_code_change)
  const ghidra = ready.filter((c) => !c.has_code_change && c.has_ghidra_fixes)
  const plain = ready.filter((c) => !c.has_code_change && !c.has_ghidra_fixes)
  log('land: ' + code.length + ' with code, ' + ghidra.length + ' with Ghidra fixes, ' + plain.length + ' transcription only')

  for (const c of code) {
    if (stopped) break
    let approved = false
    let why = ''
    for (let round = 1; round <= MAX_FIX_ROUNDS && !stopped; round++) {
      const fx = await run(fixPrompt(c, round, why), { label: 'fix:' + c.id + '#' + round, phase: 'Land', schema: FIX })
      if (!fx) { why = 'fixer returned nothing'; break }
      if (fx.cannot_apply) { why = 'fixer: cannot apply as written -- ' + (fx.note || ''); break }
      if (!fx.build_pass) { why = 'build failed: ' + (fx.build_detail || ''); continue }
      const rv = await run(reviewPrompt(c, round), { label: 'review:' + c.id + '#' + round, phase: 'Land', schema: REVIEW })
      if (!rv || !rv.wrote_review) { why = 'reviewer returned nothing or wrote no review'; break }
      if (!rv.approved) { why = 'review rejected: ' + (rv.note || ''); continue }
      const gt = await run(gatePrompt(c), { label: 'gate:' + c.id + '#' + round, phase: 'Land', schema: GATE })
      if (!gt) { why = 'gate returned nothing'; break }
      if (!gt.gate_pass) { why = 'gate failed: ' + (gt.detail || ''); continue }
      approved = true
      break
    }
    if (stopped) break
    if (!approved) {
      const d = await run(discardPrompt(c, why), { label: 'discard:' + c.id, phase: 'Land', schema: DISCARD })
      if (!d || !d.clean_now) { stop('dirty_tree', 'could not discard the failed change of ' + c.id); break }
      report.code_failed.push({ id: c.id, why: why })
    }
    const bk = await run(bookkeepPrompt(c, approved ? 'landed' : why.slice(0, 200)),
      { label: 'book:' + c.id, phase: 'Land', schema: BOOK })
    if (!bk || !bk.committed) {
      unfinished(c.id, 'bookkeeping did not commit: ' + ((bk && bk.problems) || 'no return'))
      if (!bk || !bk.tree_clean) { stop('dirty_tree', 'bookkeeping for ' + c.id + ' left the tree dirty'); break }
      continue
    }
    report.landed.push(c.id)
    if (approved) report.code_landed.push(c.id)
  }

  for (const c of ghidra) {
    if (stopped) break
    const bk = await run(bookkeepPrompt(c, null), { label: 'book:' + c.id, phase: 'Land', schema: BOOK })
    if (!bk || !bk.committed) {
      unfinished(c.id, 'bookkeeping did not commit: ' + ((bk && bk.problems) || 'no return'))
      if (!bk || !bk.tree_clean) { stop('dirty_tree', 'bookkeeping for ' + c.id + ' left the tree dirty'); break }
      continue
    }
    report.landed.push(c.id)
  }

  if (!stopped && plain.length) {
    const ids = plain.map((c) => c.id)
    const tr = await run(transcribePrompt(ids), { label: 'transcribe', phase: 'Land', schema: BOOK })
    if (!tr || !tr.committed) {
      ids.forEach((id) => unfinished(id, 'transcription did not commit'))
      if (!tr || !tr.tree_clean) stop('dirty_tree', 'transcription left the tree dirty')
    } else {
      const failed = new Set(tr.apply_failed || [])
      ids.forEach((id) => {
        if (failed.has(id)) unfinished(id, 'sweep.py apply refused it')
        else { report.landed.push(id); report.transcribed.push(id) }
      })
    }
  }
}

// -------------------------------------------------------------- knowledge
//
// Not after a stop (ADR-0007 5.6): pages written from half the findings read
// exactly like pages written from all of them.

if (!stopped) {
  phase('Knowledge')
  report.knowledge = await run(knowledgePrompt(), { label: 'knowledge', phase: 'Knowledge', schema: KNOW })
  if (!report.knowledge) unfinished('knowledge', 'the knowledge stage returned nothing')
}

// ----------------------------------------------------------------- report

if (!stopped) {
  const fin = await plan()
  if (fin) {
    report.not_landed = fin.clusters.filter((c) => !c.landed).map((c) => c.id)
    report.clusters_total = fin.clusters.length
  }
}
report.stopped = stopped
return report
