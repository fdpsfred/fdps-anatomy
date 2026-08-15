// Backbone walk -- one agent per function, read-only.
//
// Ticket 12 needs the path from the entry point to the main loop walked by
// hand and named. This script is the structural enforcement of ADR-0002: the
// worklist lives here, each agent() call carries exactly one function, and no
// agent is ever handed a list. Agents only read Ghidra; every write to the
// program database is applied by the orchestrator afterwards, one at a time,
// which also removes any chance of concurrent writers colliding.
//
// Invoke with args = [{addr, name, size, callers: [...], callees: [...]}, ...]
// taken from workspace/call_graph/backbone_queue.json.

export const meta = {
  name: 'backbone-walk',
  description: 'Read one backbone function per agent and propose its identity',
  phases: [
    { title: 'Read', detail: 'one agent per function, read-only' },
  ],
}

const VERDICT = {
  type: 'object',
  additionalProperties: false,
  required: [
    'addr', 'proposed_name', 'pool', 'role', 'subsystem',
    'plate_comment', 'params', 'evidence', 'confidence', 'walk_next',
  ],
  properties: {
    addr: { type: 'string', description: '8-hex address, exactly as given' },
    proposed_name: {
      type: 'string',
      description:
        'Symbol name following rebuild_info/naming.md: fdps_ prefix + snake_case for ' +
        'game logic, crt_ for Watcom runtime, AIL_ for Miles audio (upstream casing), ' +
        'data_fdps_ never applies here. "main" is the only unprefixed name allowed.',
    },
    pool: { type: 'string', enum: ['fdps', 'crt', 'ail', 'binary_artifact', 'unknown'] },
    role: { type: 'string', description: 'One English sentence: what this function does.' },
    subsystem: {
      type: 'string',
      description:
        'One of: startup, main_loop, battle, menu, graphics, input, file_io, audio, ' +
        'cd, math, memory, string, unknown. Pick the closest; "unknown" is allowed.',
    },
    plate_comment: {
      type: 'string',
      description:
        'English plate comment, plain text, no decorative borders. Must contain the ' +
        'sections Algorithm, Parameters and Returns.',
    },
    params: {
      type: 'string',
      description:
        'What the disassembly shows about parameter passing: which registers are read ' +
        'before being written, whether arguments come off the stack, the stack purge.',
    },
    cc_note: {
      type: 'string',
      description:
        'Anything suggesting the calling convention deviates from the __watcall default, ' +
        'or empty when it looks like every other function.',
    },
    evidence: {
      type: 'string',
      description:
        'Why the name and pool are right: referenced strings, called CRT primitives, ' +
        'data addresses touched, caller context, recognisable algorithm shape.',
    },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    walk_next: {
      type: 'array',
      items: { type: 'string' },
      description:
        'Addresses of this function callees that are worth walking next because they ' +
        'carry the backbone forward (subsystem entry points, the main loop). Leave ' +
        'empty for leaves and for calls into runtime helpers.',
    },
    open_question: {
      type: 'string',
      description: 'Anything that could not be settled by reading, or empty.',
    },
  },
}

function promptFor(fn) {
  return `You are identifying ONE function in FDPS.LE, a 1997 DOS game executable
(32-bit Watcom C/C++ 10.0a, DOS/4G LE module) that is being reverse engineered.

Your function: ${fn.addr}  (current name ${fn.name}, body ${fn.size} bytes)

This is the only function you work on. Do not read, judge, name or report on any
other function. If a neighbouring function looks interesting, put its address in
walk_next and move on.

READ-ONLY. You must not call any Ghidra tool that writes: no rename, no
set_plate_comment, no set_function_prototype, no create_label, nothing. The
orchestrator applies every change. A write from you is a defect.

Load the Ghidra tools you need in ONE ToolSearch call, for example:
  ToolSearch "select:mcp__ghidra__disassemble_function,mcp__ghidra__decompile_function,mcp__ghidra__get_function_callers,mcp__ghidra__get_function_callees,mcp__ghidra__get_xrefs_from,mcp__ghidra__get_function_signature"

How to judge:

1. Read the full disassembly. This is the primary source.
2. Read the decompiled C as a second opinion. Do not trust it on its own --
   it invents parameters, and unaff_EBX in a signature usually means Ghidra
   guessed wrong, not that the function really reads EBX.
3. Follow references out of the function: strings, data addresses, imported
   constants. In this binary there are no Chinese strings in the executable
   and no import table, so string evidence is scarce and precious.
4. Look at who calls it (${fn.callers.length} callers) and what it calls
   (${fn.callees.length} callees). A function called from one place during
   startup is a different thing from one called from forty places.

Naming, from rebuild_info/naming.md -- these are project rules, not suggestions:

  game logic    fdps_ prefix, snake_case      fdps_rle_blit_sprite
  Watcom CRT    crt_ prefix, snake_case       crt_memcpy
  Miles AIL     AIL_ prefix, upstream casing  AIL_startup
  C entry point main                          the only name with no prefix

Do NOT propose a PascalCase name. The Ghidra tooling will warn that the name
should be PascalCase; that warning is wrong for this project and is expected.

If you cannot tell what the function does, say so: set confidence to low and
put what is missing in open_question. A wrong confident name costs far more
than an honest "unknown" -- it will be copied into the knowledge base and the
rebuilt C source. Never invent a purpose to fill the field.

Return the structured verdict. Your final output is data for the orchestrator,
not a message to a human.`
}

phase('Read')

const work = Array.isArray(args) ? args : []
log(`walking ${work.length} function(s), one agent each`)

const results = await parallel(work.map((fn) => () =>
  agent(promptFor(fn), {
    label: `read:${fn.addr}`,
    phase: 'Read',
    schema: VERDICT,
  })
))

const ok = results.filter(Boolean)
log(`${ok.length}/${work.length} returned a verdict`)

return {
  verdicts: ok,
  missing: work.filter((fn, i) => !results[i]).map((fn) => fn.addr),
}
