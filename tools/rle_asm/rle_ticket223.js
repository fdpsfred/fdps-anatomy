export const meta = {
  name: 'rle-ticket223',
  description: 'Ticket 22.3: transcribe the 15 RLE routines back to the original assembly, land them, port their tests through the dispatcher',
  phases: [
    { title: 'Preflight', detail: 'tools answer, resume state read off the files' },
    { title: 'Transcribe', detail: 'one agent per routine writes a standalone WASM module and checks it against FDPS.LE' },
    { title: 'Sweep', detail: 'routines left with open concerns are re-read with the other verdicts as evidence' },
    { title: 'Land', detail: 'join the fragments into src/*.asm, full build gate, commit' },
    { title: 'Tests', detail: 'one agent per routine, in sequence: dispatcher-based cases for every C-translation case' },
    { title: 'Final', detail: 'coverage of the old cases, full build gate, report' },
  ],
}

// Ticket 22.3 (.scratch/fdps-rebuild/issues/22.3-rle-blit-keep-original-asm.md).
//
// The work list is the fifteen routines and it lives here, in the script: every
// agent() call carries exactly one routine (CLAUDE.md, ADR-0002).  Everything the
// script knows about progress is read off files by tools/rle_asm/wf_state.py,
// never taken from what an agent says (ADR-0007 5.1), so a run that is killed at
// any point is resumed by calling it again (5.7).
//
// The transcription agents write only under workspace/rle_asm/; nothing reaches
// src/ until the Land phase, which is a script (land.py) and does not judge
// (ADR-0007 principle 2).  The test-port agents do write tests/ and commit, so
// they run one at a time.

const REPO = 'C:\\Users\\fdpsf\\Documents\\fdps-anatomy'
const LABEL = (args && args.label) || 't223'

const ROUTINES = [
  { addr: '000568db', name: 'fdps_blit_dispatch', file: 'rledisp', prefix: 'disp', mode: null, cfile: 'src/blit.c', ctest: null },
  { addr: '00056a0d', name: 'fdps_rle_blit_passthrough', file: 'rlebase', prefix: 'pass', mode: 0, cfile: 'src/rle.c', ctest: 'tests/rle.c' },
  { addr: '00056a8d', name: 'fdps_rle_blit_remap_sprite_and_backdrop', file: 'rlepal', prefix: 'rmsb', mode: 1, cfile: 'src/rlecolor.c', ctest: 'tests/rlecolor.c' },
  { addr: '00056b25', name: 'fdps_rle_blit_with_palette_remap', file: 'rlepal', prefix: 'rmpal', mode: 2, cfile: 'src/rlecolor.c', ctest: 'tests/rlecolor.c' },
  { addr: '00056bb7', name: 'fdps_rle_blit_recolor', file: 'rlepal', prefix: 'rcol', mode: 3, cfile: 'src/rlecolor.c', ctest: 'tests/rlecolor.c' },
  { addr: '00056c5e', name: 'fdps_rle_blit_scaled', file: 'rlebase', prefix: 'scal', mode: 4, cfile: 'src/rle.c', ctest: 'tests/rle.c' },
  { addr: '00056dc9', name: 'fdps_rle_skip_row', file: 'rlebase', prefix: 'skip', mode: null, cfile: 'src/rle.c', ctest: 'tests/rle.c' },
  { addr: '00056e2a', name: 'fdps_rle_blit_rotated', file: 'rleturn', prefix: 'rot', mode: 5, cfile: 'src/rlerot.c', ctest: 'tests/rlerot.c' },
  { addr: '00057114', name: 'fdps_rle_blit_rotated_scaled', file: 'rleturn', prefix: 'rots', mode: 6, cfile: 'src/rlerot.c', ctest: 'tests/rlerot.c' },
  { addr: '00057551', name: 'fdps_rle_blit_mirrored_horizontal', file: 'rlebase', prefix: 'mirh', mode: 7, cfile: 'src/rle.c', ctest: 'tests/rle.c' },
  { addr: '000575ed', name: 'fdps_rle_blit_mirrored_vertical', file: 'rlebase', prefix: 'mirv', mode: 8, cfile: 'src/rle.c', ctest: 'tests/rle.c' },
  { addr: '0005761b', name: 'fdps_rle_blit_translucent', file: 'rlemix', prefix: 'tran', mode: 9, cfile: 'src/rleblend.c', ctest: 'tests/rleblend.c' },
  { addr: '00057793', name: 'fdps_rle_blit_tint_sprite_and_backdrop', file: 'rlemix', prefix: 'tsb', mode: 10, cfile: 'src/rleblend.c', ctest: 'tests/rleblend.c' },
  { addr: '00057916', name: 'fdps_rle_blit_tint', file: 'rlemix', prefix: 'tint', mode: 11, cfile: 'src/rleblend.c', ctest: 'tests/rleblend.c' },
  { addr: '00057a74', name: 'fdps_rle_blit_translucent_color_range', file: 'rlemix', prefix: 'tcr', mode: 12, cfile: 'src/rleblend.c', ctest: 'tests/rleblend.c' },
]

// Files the Land phase expects to find modified or new, and nothing else: the
// C side of the switch was staged by hand before this run and goes into the
// same commit as the assembly, so no commit ever links a routine twice or not
// at all.
const LANDING_SET = [
  'src/blit.c', 'src/rle.c', 'src/rle.h', 'src/rlecolor.c', 'src/rlecolor.h',
  'src/rlerot.c', 'src/rlerot.h', 'src/rleblend.c', 'src/rleblend.h',
  'tests/blit.c', 'tests/rle.c', 'tests/rlecolor.c', 'tests/rlerot.c', 'tests/rleblend.c',
  'tests/rledisp.c', 'tests/rlebase.c', 'tests/rlepal.c', 'tests/rleturn.c', 'tests/rlemix.c',
  'tools/build_gate/gate.py',
  'src/rledisp.asm', 'src/rlebase.asm', 'src/rlepal.asm', 'src/rleturn.asm', 'src/rlemix.asm',
]

const unfinished = []
const report = { label: LABEL, transcribed: [], swept: [], landed: false, tests: [], final: null, stopped: null }

// ------------------------------------------------------------------ schemas

const RUN_SCHEMA = {
  type: 'object',
  properties: {
    ok: { type: 'boolean', description: 'exit code 0 and the printed JSON says ok' },
    exit_code: { type: 'integer' },
    output: { type: 'string', description: 'the printed JSON, verbatim, at most 3000 characters' },
    toolchain_unreachable: { type: 'boolean', description: 'the command could not run at all' },
  },
  required: ['ok', 'exit_code', 'output', 'toolchain_unreachable'],
}

const TRANSCRIBE_SCHEMA = {
  type: 'object',
  properties: {
    address: { type: 'string' },
    status: { type: 'string', enum: ['done', 'failed', 'toolchain_unreachable'] },
    instructions: { type: 'integer' },
    open_concerns: { type: 'integer' },
    note: { type: 'string', description: 'one line, at most 200 characters' },
  },
  required: ['address', 'status', 'instructions', 'open_concerns', 'note'],
}

const SWEEP_SCHEMA = {
  type: 'object',
  properties: {
    address: { type: 'string' },
    resolved: { type: 'integer' },
    still_open: { type: 'integer' },
    fragment_changed: { type: 'boolean' },
    note: { type: 'string' },
  },
  required: ['address', 'resolved', 'still_open', 'fragment_changed', 'note'],
}

const LAND_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['committed', 'not_ready', 'gate_failed', 'unexpected_tree', 'toolchain_unreachable'] },
    commit: { type: 'string' },
    failures: { type: 'array', items: { type: 'string' }, description: 'at most 20 lines, verbatim from the tools' },
    note: { type: 'string' },
  },
  required: ['status', 'commit', 'failures', 'note'],
}

const TESTS_SCHEMA = {
  type: 'object',
  properties: {
    address: { type: 'string' },
    status: { type: 'string', enum: ['committed', 'failed', 'toolchain_unreachable'] },
    ported: { type: 'integer', description: 'C-translation cases mapped to dispatcher cases' },
    uncovered: { type: 'integer' },
    commit: { type: 'string' },
    note: { type: 'string' },
  },
  required: ['address', 'status', 'ported', 'uncovered', 'commit', 'note'],
}

// ------------------------------------------------------------------ prompts

function runPrompt(cmd, why) {
  return [
    'Run exactly this command from ' + REPO + ' in the foreground and report what it printed.',
    'Do not edit anything, do not fix anything, do not interpret beyond the fields asked for.',
    '',
    '  ' + cmd,
    '',
    'Why it is run: ' + why,
    '',
    'Use the PowerShell tool with a timeout of 600000. `ok` is true only when the exit code is 0.',
    'If the command cannot run at all (python missing, DOSBox-X or Watcom missing, an',
    'exception before any result), set toolchain_unreachable.',
  ].join('\n')
}

function transcribePrompt(r, retryWhy) {
  const f = 'workspace\\rle_asm\\frag\\' + r.prefix + '.asm'
  const v = 'workspace\\rle_asm\\verdicts\\' + r.addr + '.json'
  return [
    '# Transcribe ONE routine of FDPS.LE into WASM 10.0a assembly',
    '',
    'Routine: ' + r.name + ' at ' + r.addr + (r.mode !== null ? ' (blit mode ' + r.mode + ')' : '') + '.',
    'This is the only routine you work on. Do not write any other routine\'s fragment or verdict.',
    '',
    'Ticket 22.3 puts the fifteen hand-written RLE routines back into the rebuild as the',
    'original\'s own assembly. Read the ticket first:',
    '  .scratch\\fdps-rebuild\\issues\\22.3-rle-blit-keep-original-asm.md',
    'The bar is the ticket\'s "execution efficiency identical" definition: the same instruction',
    'sequence (every NOP included), every instruction the same length, bytes differing only in',
    'an equal-length alternative encoding or inside a relocated field. Byte identity is NOT the',
    'goal and must not be chased.',
    retryWhy ? '\nA previous attempt at this routine was rejected by the checker:\n' + retryWhy + '\nStart from the existing fragment if it is there and fix what the checker names.\n' : '',
    '## What to read',
    '',
    '1. The original, as the transcriber needs it -- every instruction with its offset, length,',
    '   bytes, branch targets as instruction numbers, and every relocated operand already named',
    '   with its symbol:',
    '     python tools\\rle_asm\\asm_match.py listing ' + r.addr,
    '2. The C translation, for what the routine means (it is under #if 0 and is reference only):',
    '   the section for ' + r.addr + ' in ' + r.cfile + ', and its header\'s prototype comment.',
    '3. Ghidra (read-only, optional): the plate comment at ' + r.addr + '. Do not write to Ghidra.',
    '',
    '## What to write',
    '',
    '`' + f + '` -- a standalone WASM module holding this routine and nothing else, in exactly',
    'this shape (a later script joins fragments mechanically and rejects any other shape):',
    '',
    '    ; optional comment lines',
    '            .386p',
    '',
    '            extrn   <symbol>:<byte|word|dword|near>     (one per line, as needed)',
    '',
    "    _TEXT   segment byte public use32 'CODE'",
    '            assume  cs:_TEXT',
    '',
    '            public  ' + r.name,
    '',
    '    ; <the routine\'s header comment block, English>',
    '    ' + r.name + ' proc near',
    '            <instructions>',
    '    ' + r.name + ' endp',
    '',
    '    _TEXT   ends',
    '',
    '            end',
    '',
    'Header comment: the original address, what the routine does in two or three sentences,',
    'its register contract on entry and exit (which registers carry what; which it destroys),',
    'which slots of fdps_blit_dispatch\'s EBP frame it reads or writes, and a pointer to the C',
    'translation (' + r.cfile + ') for the long explanation. Inline comments only where an',
    'instruction is not self-explanatory. Everything in English (CLAUDE.md language rule).',
    '',
    '## Measured WASM 10.0a facts (ticket 22.3) -- follow them',
    '',
    '- Every absolute address in the original is a relocated operand; the listing names its',
    '  symbol. Write the symbol, declared `extrn sym:word` (or byte/dword by the access size),',
    '  never the number: a number inside the original image\'s ranges fails the check. Use',
    '  `word ptr` / `dword ptr` / `byte ptr` where the operand size would be ambiguous.',
    '- A branch to another of the fifteen routines is to its name, declared `extrn name:near`.',
    '- A FORWARD branch the listing shows as 2 bytes is followed in the original by NOP padding',
    '  (4 NOPs after a conditional jump, 3 after JMP). Write it as `jcc short label` /',
    '  `jmp short label` and then the NOPs, one `nop` per line, exactly as many as the listing.',
    '- Every other branch: write no size keyword and let WASM choose. Backward branches and',
    '  forward branches out of short range come out the original\'s length.',
    '- EXCEPTION: a forward long branch that the listing marks "would reach as a short branch',
    '  once shortened". WASM shortens a conditional jump there. Write `jcc near ptr label`.',
    '  WASM\'s `jcc near ptr` is known to compute the displacement one byte too far when the',
    '  32-bit displacement is below 127 forward (or short-reachable backward); at 127 and above',
    '  it is right. The checker compares every branch by the instruction it lands on, so a',
    '  wrong displacement cannot pass. For JMP in that situation WASM keeps the long form by',
    '  itself; `jmp near ptr` is always encoded correctly if you ever need to force it.',
    '- WASM labels are module-wide: every label you define starts with `' + r.prefix + '_`',
    '  (e.g. ' + r.prefix + '_next_row). Give them names that say what is there where you can.',
    '- WASM picks the other direction bit for reg,reg MOV/ADD/SUB/XOR/AND/OR/CMP and the imm8',
    '  form of CMP AX,0. Those are equal-length and allowed: write the plain instruction.',
    '- NO db / dw / dd anywhere. If an instruction genuinely cannot be written with the same',
    '  length, stop and record it as a concern -- do not hand-encode it.',
    '- The module must define no data and no public other than ' + r.name + '.',
    '',
    '## Check and record',
    '',
    'Write the verdict skeleton `' + v + '`:',
    '  {"address": "' + r.addr + '", "name": "' + r.name + '", "labels": <number of labels>,',
    '   "notes": ["<non-obvious things a reader of the assembly should know>"],',
    '   "concerns": [{"what": "...", "needs": "...", "resolved": false}]}',
    'then stamp it (this assembles the fragment in DOSBox-X, runs the checker, and writes the',
    'result and the fragment\'s hash into the verdict):',
    '  python tools\\rle_asm\\wf_state.py stamp ' + r.addr,
    'Fix and re-stamp until it prints "ok": true. `python tools\\rle_asm\\asm_match.py frag ' + f + '`',
    'gives the same check in readable form. A verdict stamped before your last edit is stale.',
    '',
    'Concerns are for things you could not settle: a contract with another routine you could',
    'not confirm from this routine alone, anything in the original that looks wrong. Do not',
    'invent concerns; do not bury real ones in notes.',
    '',
    'Write nothing under src\\ or tests\\, do not commit, do not touch Ghidra. If DOSBox-X or',
    'WASM cannot run at all, stop and report toolchain_unreachable.',
    '',
    'Your final message is the summary object: status done only when the stamp printed ok.',
  ].join('\n')
}

function sweepPrompt(r) {
  return [
    '# Second reading of ONE transcribed routine',
    '',
    'Routine: ' + r.name + ' at ' + r.addr + '. Its transcription passed the instruction check',
    'but its verdict still carries open concerns:',
    '  workspace\\rle_asm\\verdicts\\' + r.addr + '.json',
    '',
    'The first reader saw only this routine. You may now also read the other routines\'',
    'verdicts and fragments (workspace\\rle_asm\\verdicts\\*.json, workspace\\rle_asm\\frag\\*.asm)',
    'as evidence -- a contract between the dispatcher and its kernels is often visible only',
    'from the other side. Quoting another routine\'s settled finding is allowed; deciding',
    'anything about another routine is not.',
    '',
    'For each open concern: if the evidence settles it, set "resolved": true and add',
    '"resolution" with the evidence. If it does not, leave it open and say in "resolution" what',
    'is still missing. A second attempt is not a reason to force a conclusion.',
    'If settling a concern requires changing the fragment (workspace\\rle_asm\\frag\\' + r.prefix + '.asm),',
    'change it and re-stamp: python tools\\rle_asm\\wf_state.py stamp ' + r.addr,
    'The stamp must end "ok": true. Write nothing else; no src\\, tests\\, Ghidra or commits.',
    '',
    'Final message: the summary object.',
  ].join('\n')
}

function landPrompt(fixRound) {
  return [
    '# Land the transcribed RLE routines (ticket 22.3). You transcribe; you do not judge.',
    '',
    'Work in ' + REPO + '. Use the PowerShell tool.',
    '',
    '1. `git status --porcelain`. Every modified or new path must be in this set:',
    '     ' + LANDING_SET.join(' '),
    '   plus anything under workspace\\ (ignored). If anything else is dirty, stop: unexpected_tree.',
    '2. `python tools\\rle_asm\\land.py` -- joins workspace\\rle_asm\\frag\\*.asm into the five',
    '   src\\*.asm files. If it prints NOT READY, stop: not_ready, with its lines as failures.',
    '3. `python tools\\rle_asm\\switch_impl.py status` must print `asm`.',
    '4. `python tools\\rle_asm\\asm_match.py check --fresh` must end PASS (it assembles the five',
    '   files as landed and checks every routine against FDPS.LE -- in the joined files a branch',
    '   between two routines of one file is no longer a fixup, so this can differ from the',
    '   fragment checks).',
    '5. The full build gate for the unit-test image. It takes 20-30 minutes and prints nothing',
    '   until it ends, so run it in the background with its output to a file:',
    '     python tools\\build_gate\\gate.py check --target emittest *> workspace\\rle_asm\\gate_land.log',
    '   (run_in_background: true). You are notified when it exits. Sleeps are blocked, so do',
    '   not poll with them; if you must wait on it explicitly, use the Monitor tool with an',
    '   until-loop on the log. Do NOT send your final message while it runs.',
    '   Then read workspace\\build_gate\\result.json: its "verdict" must be "PASS", with the',
    '   rle_asm.check and code_emit.run suites passing.',
    '6. Only if 3, 4 and 5 all passed: stage exactly the landing set (`git add` each path that',
    '   exists) and commit with the message',
    '     rle: 票 22.3 落地 15 支 RLE 組語（原版指令序列），C 譯本以 #if 0 保留作參考',
    '   followed by a blank line and the two attribution lines:',
    '     Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>',
    '     Claude-Session: https://claude.ai/code/session_01SFBoWG1kt6hda9Cvf4BUr9',
    '   Never use --no-verify.',
    '7. `python tools\\rle_asm\\wf_state.py verify-land` must print "ok": true.',
    '',
    'Do not edit any source to make a step pass. If a step fails, stop, leave the tree as it is',
    'and report gate_failed with the failing lines verbatim (from result.json for the gate).',
    fixRound ? '\nThis is the second landing attempt after the fragments were corrected; the steps are the same.' : '',
    '',
    'Final message: the summary object.',
  ].join('\n')
}

function landFixPrompt(failures) {
  return [
    '# The landing of the RLE assembly failed. Diagnose, and fix only the transcription.',
    '',
    'Work in ' + REPO + '. The landing step reported:',
    failures.map(s => '  ' + s).join('\n'),
    '',
    'Find the cause. Read workspace\\build_gate\\result.json, workspace\\code_emit\\ build',
    'transcripts, workspace\\rle_asm\\check.json, the landed src\\*.asm and the fragments.',
    '',
    'You may change ONLY fragments under workspace\\rle_asm\\frag\\ (then re-stamp each one you',
    'change: python tools\\rle_asm\\wf_state.py stamp <address>, which must end "ok": true), and',
    'you may delete the five src\\rle*.asm files the failed landing wrote (`git status` shows',
    'them untracked) so the next landing starts clean. Nothing else: no C sources, no tests,',
    'no tools, no commits, no Ghidra.',
    '',
    'If the cause is outside the fragments -- a C test that is wrong, a tool defect, a data',
    'definition -- do not work around it. Report it precisely; the run stops and a person',
    'looks at it.',
    '',
    'Final message: plain text, at most 1200 characters: the cause, what you changed, and',
    'whether a second landing can be expected to pass. Start it with FIXED or NOT FIXABLE HERE.',
  ].join('\n')
}

function testsPrompt(r, retryWhy) {
  const ctestNote = r.name === 'fdps_rle_skip_row'
    ? ['This routine has no mode of its own: the dispatcher reaches it only from the scaled',
       'kernel (mode 4) and the rotated-and-scaled kernel (mode 6), once per source row a',
       'vertical shrink drops. Every C case that calls it directly has to be restated as a',
       'dispatch through one of those modes whose observable outcome -- which source rows',
       'end up drawn, where, and how many stream bytes each skipped row consumed (seen through',
       'which row is drawn next) -- depends on exactly the property the C case asserts.'].join('\n')
    : 'Its dispatcher mode is ' + r.mode + ' (see the mode table in src\\blit.h).'
  return [
    '# Port ONE routine\'s tests to go through fdps_blit_dispatch',
    '',
    'Routine: ' + r.name + ' at ' + r.addr + '. This is the only routine you work on.',
    '',
    'Ticket 22.3 replaced the C translation of the RLE kernels with the original\'s assembly',
    '(src\\' + r.file + '.asm, already landed and committed). The kernels have no C interface any',
    'more; the only way in is fdps_blit_dispatch. The C translation\'s tests call the kernels',
    'directly and now sit under #if 0 in ' + r.ctest + ' (reference only -- do not edit them).',
    '',
    ctestNote,
    '',
    '## The job',
    '',
    '1. Find every RUN_TEST case in ' + r.ctest + ' that exercises ' + r.name + ' (read each case;',
    '   do not go by name prefix alone). Those are "your" cases. A case exercising another',
    '   routine is not yours even if it sits next to yours.',
    '2. For each of your cases, write one or more cases in tests\\' + r.file + '.c that call',
    '   fdps_blit_dispatch with this routine\'s mode and verify the same situation -- the same',
    '   stream ops, the same edge (row width reached exactly, a zero count, a signed value, a',
    '   global consumed to zero, ...), asserting the same observable facts. Register each in',
    '   run_' + r.file + '_tests. Name them with a prefix that says which routine they cover.',
    '   Put shared fixtures near the top of the file; other routines\' agents will add their',
    '   own cases to the same file after you, so keep yours self-contained and do not rename or',
    '   reorganise what is already there.',
    '3. Expected values come from the assembly (python tools\\rle_asm\\asm_match.py listing',
    '   ' + r.addr + ') and the reasoning already written in the C case. NEVER run the code and',
    '   copy what it produced into an expectation.',
    '4. Translating a direct call into a dispatch: the dispatcher publishes width, row count',
    '   and pitch (each truncated to 16 bits) into the rectangle globals and passes the kernels',
    '   that take one a row advance of pitch - width computed in 32 bits. So a C case that set',
    '   the globals by hand and passed an advance becomes a dispatch with width, rows and',
    '   pitch = advance + width. Modes 4-8 get no advance. The sixth argument is each mode\'s',
    '   operand (blit.h). If a C case asserts something no dispatch can produce -- a state the',
    '   dispatcher always overwrites, an argument it cannot pass -- record it under "uncovered"',
    '   with the reason; do not fake it.',
    '5. Build and run the unit-test image in the FOREGROUND (it takes several minutes; use the',
    '   PowerShell tool with timeout 600000; if one call is not enough, run it with',
    '   run_in_background: true and its output to a file -- you are notified when it exits,',
    '   sleeps are blocked, and the Monitor tool can wait on a condition -- and never send',
    '   your final message while it runs):',
    '     python tools\\code_emit\\build_emit.py all --only ' + r.file,
    '   (--only still compiles and links everything but calls only run_' + r.file + '_tests, so a',
    '   run takes the build time plus seconds; the final gate of this workflow runs everything.)',
    '   It must end [result] PASS with zero failed checks. A failure in YOUR new case means your',
    '   expectation or your translation is wrong, or the assembly differs from what the C',
    '   translation claimed -- work out which from the listing. Never weaken an assertion to',
    '   pass; if the assembly really does something else than the old case asserted, that is a',
    '   finding: record it in the verdict\'s "findings" and assert what the assembly does.',
    '6. Write workspace\\rle_asm\\tests\\' + r.addr + '.json:',
    '     {"address": "' + r.addr + '", "name": "' + r.name + '", "test_file": "tests/' + r.file + '.c",',
    '      "cases": [{"c_case": "<RUN_TEST name in ' + r.ctest + '>", "dispatch_cases": ["<new name>", ...]}],',
    '      "uncovered": [{"c_case": "...", "why": "..."}], "findings": ["..."]}',
    '7. Commit only tests\\' + r.file + '.c:',
    '     git add tests/' + r.file + '.c',
    '     git commit -m "tests: 票 22.3 ' + r.name + ' 經分派者的測試" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>" -m "Claude-Session: https://claude.ai/code/session_01SFBoWG1kt6hda9Cvf4BUr9"',
    '8. python tools\\rle_asm\\wf_state.py verify-tests ' + r.addr + '  must print "ok": true.',
    retryWhy ? '\nA previous attempt was rejected:\n' + retryWhy + '\nContinue from what is in the tree.\n' : '',
    'Touch nothing but tests\\' + r.file + '.c and your verdict file: no src\\, no other tests,',
    'no #if 0 region, no Ghidra. If DOSBox-X or the build cannot run at all, report',
    'toolchain_unreachable.',
    '',
    'Final message: the summary object.',
  ].join('\n')
}

// ------------------------------------------------------------------ helpers

async function run(cmd, why, phaseName, label) {
  const r = await agent(runPrompt(cmd, why), { label: label, phase: phaseName, schema: RUN_SCHEMA, effort: 'low' })
  return r
}

function parseJson(s) {
  try { return JSON.parse(s) } catch (e) { return null }
}

function stop(why) {
  report.stopped = why
  log('STOPPED: ' + why)
}

// ---------------------------------------------------------------- Preflight

phase('Preflight')
const self = await run('python tools\\rle_asm\\asm_match.py selftest',
  'the checker must be proven to catch every failure before it judges anything', 'Preflight', 'selftest')
if (!self) {
  stop('preflight agent returned nothing')
  return report
}
if (self.toolchain_unreachable || !self.ok) {
  stop('asm_match selftest: ' + self.output.slice(0, 500))
  return report
}
const st0 = await run('python tools\\rle_asm\\wf_state.py status', 'read resume state', 'Preflight', 'status')
const status0 = st0 && parseJson(st0.output)
if (!status0) {
  stop('could not read the resume state')
  return report
}
const byAddr = {}
for (const s of status0.routines) byAddr[s.address] = s
log('resume state: ' + status0.routines.filter(s => s.frag === 'stamped').length + '/15 stamped, landed=' + status0.landed)

// --------------------------------------------------------------- Transcribe

phase('Transcribe')
let upstreamDown = false
const todo = status0.landed ? [] : ROUTINES.filter(r => byAddr[r.addr].frag !== 'stamped')

async function verifyFrag(r) {
  const v = await run('python tools\\rle_asm\\wf_state.py verify-frag ' + r.addr,
    'is the transcription of ' + r.name + ' done, judged from the files', 'Transcribe', 'verify ' + r.prefix)
  if (!v) return { ok: false, why: 'verifier returned nothing', dead: true }
  if (v.toolchain_unreachable) return { ok: false, why: 'toolchain unreachable', down: true }
  const j = parseJson(v.output)
  return { ok: v.ok && j && j.ok, why: (j && j.problems ? j.problems.slice(0, 8).join('\n') : v.output.slice(0, 800)), open: j ? j.open_concerns : 0 }
}

// Stamped routines from an earlier run are re-verified, not trusted.
const already = status0.landed ? [] : ROUTINES.filter(r => byAddr[r.addr].frag === 'stamped')

const results = await pipeline(todo.concat(already),
  async (r) => {
    if (upstreamDown) return { r, skipped: true }
    if (byAddr[r.addr].frag === 'stamped') {
      const v = await verifyFrag(r)
      if (v.ok) return { r, ok: true, open: v.open }
      // A stale or broken earlier result goes back through transcription.
      return { r, ok: false, retryWhy: v.why }
    }
    return { r, ok: false, retryWhy: null }
  },
  async (prev) => {
    const r = prev.r
    if (prev.skipped || prev.ok) return prev
    let why = prev.retryWhy
    for (let attempt = 0; attempt < 2; attempt++) {
      if (upstreamDown) return { r, skipped: true }
      const t = await agent(transcribePrompt(r, why), { label: 'transcribe ' + r.prefix, phase: 'Transcribe', schema: TRANSCRIBE_SCHEMA })
      if (t && t.status === 'toolchain_unreachable') {
        upstreamDown = true
        stop('toolchain unreachable while transcribing ' + r.name)
        return { r, skipped: true }
      }
      const v = await verifyFrag(r)
      if (v.down) {
        upstreamDown = true
        stop('toolchain unreachable while verifying ' + r.name)
        return { r, skipped: true }
      }
      if (v.ok) return { r, ok: true, open: v.open, returned: !!t }
      why = (t ? '' : '(the transcriber returned nothing)\n') + v.why
      if (!t && attempt === 0) continue
    }
    return { r, ok: false, why, returned: false }
  })

const done = results.filter(Boolean)
const sent = todo.length
const returned = done.filter(x => x.returned).length
if (sent > 0 && returned === 0 && done.every(x => !x.ok)) {
  // ADR-0007 5.2: every agent of the round came back empty -- upstream, not us.
  stop('no transcriber of ' + sent + ' returned anything: upstream failure')
}
for (const x of done) {
  if (x.ok) report.transcribed.push({ address: x.r.addr, open_concerns: x.open || 0 })
  else unfinished.push({ address: x.r.addr, name: x.r.name, stage: 'transcribe', why: x.skipped ? 'not attempted: run stopped' : (x.why || '').slice(0, 600) })
}
log('transcribed ' + report.transcribed.length + '/15')

// -------------------------------------------------------------------- Sweep

if (!report.stopped && report.transcribed.length === 15 && !status0.landed) {
  phase('Sweep')
  const withConcerns = report.transcribed.filter(t => t.open_concerns > 0)
  if (withConcerns.length) log(withConcerns.length + ' routine(s) carry open concerns; second reading')
  const swept = await parallel(withConcerns.map(t => async () => {
    const r = ROUTINES.find(x => x.addr === t.address)
    const s = await agent(sweepPrompt(r), { label: 'sweep ' + r.prefix, phase: 'Sweep', schema: SWEEP_SCHEMA })
    const v = await verifyFrag(r)
    return { r, s, v }
  }))
  for (const x of swept.filter(Boolean)) {
    report.swept.push({ address: x.r.addr, still_open: x.v.open, note: x.s ? x.s.note : 'sweep agent returned nothing' })
    if (!x.v.ok) {
      unfinished.push({ address: x.r.addr, name: x.r.name, stage: 'sweep', why: 'fragment no longer passes after the sweep: ' + x.v.why.slice(0, 400) })
    }
  }
}

// --------------------------------------------------------------------- Land

const readyToLand = !report.stopped && (status0.landed || (report.transcribed.length === 15 && !unfinished.length))
if (readyToLand && !status0.landed) {
  phase('Land')
  let land = await agent(landPrompt(false), { label: 'land', phase: 'Land', schema: LAND_SCHEMA })
  if (!land) land = await agent(landPrompt(false), { label: 'land (retry)', phase: 'Land', schema: LAND_SCHEMA })
  if (!land) {
    stop('landing agent returned nothing twice')
  } else if (land.status === 'toolchain_unreachable' || land.status === 'unexpected_tree' || land.status === 'not_ready') {
    stop('landing: ' + land.status + ': ' + land.failures.slice(0, 5).join(' | '))
  } else if (land.status === 'gate_failed') {
    log('landing gate failed; one diagnosis-and-fix round')
    const fix = await agent(landFixPrompt(land.failures), { label: 'land fix', phase: 'Land' })
    if (fix && fix.startsWith('FIXED')) {
      const land2 = await agent(landPrompt(true), { label: 'land (2)', phase: 'Land', schema: LAND_SCHEMA })
      if (land2 && land2.status === 'committed') { report.landed = true; report.landCommit = land2.commit }
      else stop('second landing failed: ' + (land2 ? land2.status + ': ' + land2.failures.slice(0, 5).join(' | ') : 'no reply'))
    } else {
      stop('landing gate failed and was not fixable in the fragments: ' + (fix || 'no reply').slice(0, 800))
    }
  } else {
    report.landed = true
    report.landCommit = land.commit
  }
  if (report.landed) {
    const vl = await run('python tools\\rle_asm\\wf_state.py verify-land', 'is the landing done, judged from the files', 'Land', 'verify land')
    if (!vl || !vl.ok) {
      report.landed = false
      stop('landing reported committed but verify-land says: ' + (vl ? vl.output.slice(0, 800) : 'no reply'))
    }
  }
} else if (status0.landed) {
  report.landed = true
}

// -------------------------------------------------------------------- Tests

if (report.landed && !report.stopped) {
  phase('Tests')
  for (const r of ROUTINES) {
    if (r.file === 'rledisp') continue
    if (byAddr[r.addr].tests === 'verdict') {
      const v = await run('python tools\\rle_asm\\wf_state.py verify-tests ' + r.addr, 'resume: is this port already done', 'Tests', 'verify tests ' + r.prefix)
      if (v && v.ok) { report.tests.push({ address: r.addr, resumed: true }); continue }
    }
    let why = null
    let ok = false
    let summary = null
    for (let attempt = 0; attempt < 2 && !ok; attempt++) {
      summary = await agent(testsPrompt(r, why), { label: 'tests ' + r.prefix, phase: 'Tests', schema: TESTS_SCHEMA })
      if (summary && summary.status === 'toolchain_unreachable') { stop('toolchain unreachable while porting tests for ' + r.name); break }
      const v = await run('python tools\\rle_asm\\wf_state.py verify-tests ' + r.addr, 'is the test port of ' + r.name + ' done, judged from the files', 'Tests', 'verify tests ' + r.prefix)
      if (!v && !summary) { stop('test port of ' + r.name + ': agent and verifier both returned nothing: upstream failure'); break }
      ok = !!(v && v.ok)
      if (!ok) why = v ? v.output.slice(0, 1500) : 'verifier returned nothing'
    }
    if (report.stopped) {
      unfinished.push({ address: r.addr, name: r.name, stage: 'tests', why: 'run stopped' })
      break
    }
    if (ok) report.tests.push({ address: r.addr, ported: summary ? summary.ported : null, uncovered: summary ? summary.uncovered : null, note: summary ? summary.note : '' })
    else unfinished.push({ address: r.addr, name: r.name, stage: 'tests', why: (why || '').slice(0, 600) })
  }
}

// -------------------------------------------------------------------- Final

if (report.landed && !report.stopped && !unfinished.length) {
  phase('Final')
  const cov = await run('python tools\\rle_asm\\wf_state.py coverage', 'every C-translation case must be claimed by some routine\'s port', 'Final', 'coverage')
  const gate = await agent([
    'Run the full build gate for the unit-test image from ' + REPO + ' and report its verdict. Do not edit anything.',
    'It takes 20-30 minutes and prints nothing until it ends. Run it in the background:',
    '  python tools\\build_gate\\gate.py check --target emittest *> workspace\\rle_asm\\gate_final.log',
    '(run_in_background: true). You are notified when it exits; sleeps are blocked, and the',
    'Monitor tool can wait on a condition if you need it. Never send your final message while',
    'it runs. Then read workspace\\build_gate\\result.json.',
    'Final message: plain text, at most 1500 characters, starting with PASS or FAIL, then every',
    'failing suite or check verbatim.',
  ].join('\n'), { label: 'final gate', phase: 'Final' })
  report.final = { coverage: cov ? cov.output.slice(0, 1500) : 'no reply', coverage_ok: !!(cov && cov.ok), gate: gate || 'no reply' }
}

report.unfinished = unfinished
report.complete = report.landed && !report.stopped && !unfinished.length && report.final
  && report.final.coverage_ok && String(report.final.gate).startsWith('PASS')
log(report.complete ? 'ticket 22.3 workflow complete' : 'ticket 22.3 workflow ended with ' + unfinished.length + ' unfinished item(s)')
return report
