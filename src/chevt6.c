/* chevt6.c -- the scripted chapter-event handlers of chapters 28 to 30.
 *
 * Most are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the tile-trigger hand-off slot and the
 * death-script runner -- call it indirectly, so none of them appears as a
 * static caller.  The undead top-up at the bottom of the file is the one that
 * is not: fdps_map_actor_behavior_step calls it directly and by name.
 *
 * See chevt6.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for 8 to 14 and chevt3.c for 15 to 19.  Nothing
 * here owns state.
 */
#include <stdlib.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "anim.h"
#include "mapcur.h"
#include "maptile.h"
#include "movegrid.h"
#include "palette.h"
#include "unit.h"
#include "deploy.h"
#include "mapdraw.h"
#include "text.h"
#include "chevt6.h"

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 0003964b.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code fdps_map_actor_behavior_step
   isolates with AND AL,0xf and dispatches on.  src/unit.c, src/chevt1.c,
   src/chevt2.c and src/chevt3.c spell the same mask out for the same field; it
   stays file-local at every end because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x1c] at 00039609 is 0x0, copied on into [EBP-0x18] at 0003961f, which
   is the slot MOV DH,byte ptr [EBP-0x18] at 00039651 reads.  Mode 0 is the
   default chain that paths a unit toward the nearest opposing unit; mode 2,
   which is what map28.dat deploys these units in, holds position and only
   scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* The other behaviour code this file's handlers OR in, and it is 0x0a: the
   constant parked at [EBP-0x30] at 0003970d, copied on into [EBP-0x3c] at
   00039723, which is the slot MOV DH,byte ptr [EBP-0x3c] at 00039755 reads.
   Mode 0x0a scores an item and uses it when the score reaches 6, then takes an
   attack of its own in the same step, and it never reaches the raw-distance
   pathing mode 0 falls through to, so a unit in it works from where it stands.
   It stays file-local because no header owns it. */
#define AI_BEHAVIOR_MODE_ITEM_THEN_ATTACK 0x0a

/* The side byte value the gate at 000395f6 tests for.  2 is the player side, 0
   the enemy and 1 the neutral one; fdps_roster_add_character writes 2 into the
   byte at record offset 6 and fdps_deploy_spawn_unit copies it from byte 0 of
   the scenario deployment record.  src/combat.c, src/deploy.c and src/unitatk.c
   spell the same constant out; it stays file-local at every end because no
   header owns it. */
#define PLAYER_SIDE 2

/* What the battle turn counter is divided by to name the wave, and it is a
   plain signed halving: MOV EAX,[0x00069ce8] / MOV EDX,[0x00069ce8] /
   SAR EDX,0x1f / SUB EAX,EDX / SAR EAX,0x1 at 0003955f..00039571, the -od
   expansion of a signed divide by two, which truncates toward zero.  The bias
   by the sign is the whole difference from an unsigned shift and it is
   behaviour: it is what makes turn 7 come out at wave 3 rather than at wave 4,
   and map27.dat schedules turn 7.  Rounding it, or writing it as a shift of an
   unsigned counter, changes which wave arrives (rebuild_info/pitfalls.md,
   contract C). */
#define CH28_WAVE_TURN_DIVISOR 2

/* How the wave is placed: XOR EAX,EAX / PUSH EAX at 0003955c, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes to the
   nearest unoccupied walkable tile around the coordinates map%02d.cod names for
   it, rather than onto them exactly.  All three of chapter 28's spawn tiles are
   on the top edge and the wave is three units wide, so a placement flag of 1
   here would stack the wave's units on top of one another whenever the tile the
   script picked is already taken. */
#define CH28_PLACE_ON_NEAREST_FREE_TILE 0

/* What data_fdps_map_cursor_draw_mode is parked at while the view is panned and
   what it is left on afterwards, MOV dword ptr [0x00069cd0],0x0 at 00039580 and
   MOV dword ptr [0x00069cd0],0x1 at 000395b7.  Zero is the mode
   fdps_draw_map_cursor paints nothing in, so the cursor is off the screen for
   the whole pan; 1 is the ordinary battle cursor.  The 1 is a literal store and
   not the mode the call found, so the entry value is lost either way. */
#define CH28_MAP_CURSOR_BLANK 0
#define CH28_MAP_CURSOR_NORMAL 1

/* Where the view is panned to, PUSH 0x0 / PUSH 0x120 at 0003958a, and they are
   map pixels rather than tiles: fdps_map_cursor_move_to walks at 0x18 pixels per
   tile, so 0x120 is tile column 12 and 0 is row 0.  That is the rightmost of the
   three top-edge tiles -- (10, 0), (11, 0) and (12, 0) -- that map27.cod gives
   every one of chapter 28's wave records. */
#define CH28_PAN_SPAWN_X 0x120
#define CH28_PAN_SPAWN_Y 0

/* How long the view is held there, CMP dword ptr [EBP+0x14],0xc / JL at
   000395a0: twelve calls into fdps_render_view_frame, each of which spins until
   the timer tick moves, so the count IS the dwell and not an instruction budget
   (rebuild_info/pitfalls.md, contract D).  Shortening it shortens the pause the
   player gets to see the reinforcements by exactly that many ticks. */
#define CH28_PAN_HOLD_FRAMES 0xc

/* 00039550.  Chapter 28's turn-scheduled reinforcement event: the wave the turn
   just played is due arrives, and the view pans to the top edge to show it.

   The frame is the standard Watcom four-push one with an empty local area --
   PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00039550..00039556 -- so there is no local in this function at all.  Both
   calls that take arguments push them and clean them themselves, ADD ESP,0xc at
   0003957d and ADD ESP,0x8 at 00039596, which is what makes the convention the
   stack one.

   THE WAVE KEY IS THE TURN COUNTER HALVED, AND IT IS READ BEFORE THE COUNTER
   MOVES.  fdps_battle_advance_turn dispatches the phase-0 turn events at
   0001e5bc and only increments data_fdps_battle_turn_counter afterwards at
   0001e5fd, so the value halved here still names the turn whose player phase has
   just ended.  Making the counter 0-based, or moving the increment ahead of the
   dispatch, shifts every one of chapter 28's reinforcement waves by a turn.

   WHICH WAVES THAT ACTUALLY DEPLOYS.  map27.dat schedules this slot on turns 2,
   4, 6, 7, 10, 12, 14, 16 and 18, and halving those gives waves 1, 2, 3, 3, 5,
   6, 7, 8 and 9.  Turn 7 truncates onto wave 3 a second time, so the shipped
   game deploys map27.dat's wave 3 twice and never deploys its wave 4 -- three
   enemies the chapter has data for and never spawns.  That is the original's
   behaviour and not a defect to correct here.

   THE PAN IS NOT CONDITIONAL ON ANYTHING.  There is no test of the turn, of the
   wave, or of whether the deployment appended anything, so a turn whose wave
   number matches no record still blanks the cursor, walks the view to the top
   edge and holds it there for twelve frames.

   THE ARGUMENT SLOT IS THE HOLD COUNTER.  MOV dword ptr [EBP+0x14],0x0 at
   00039599 writes zero over the incoming argument before the loop and the slot
   is only ever the counter after that, so nothing about the acting unit reaches
   the wave asked for, the map asked for or the pan.  The store has no observable
   effect on the caller, because the slot belongs to the caller's outgoing
   argument area and fdps_battle_run_turn_events drops it after the call.

   NO VALUE IS USED AFTER A CALL.  fdps_deploy_wave, fdps_map_cursor_move_to and
   fdps_render_view_frame all return nothing, and the instruction after each of
   the first two CALLs is the ADD ESP that cleans its arguments.  The MOV
   EAX,[EBP+0x14] at 000395a8 loads the counter into EAX and nothing reads it --
   it is the -od expansion of the loop's own increment, not a use of anything the
   CALL left behind.  Nothing sets EAX before the RET and no dispatcher reads
   what comes back, so the result is void. */
void fdps_chapter_28_event_deploy_wave_for_turn(int unit_index)
{
    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     data_fdps_battle_turn_counter / CH28_WAVE_TURN_DIVISOR,
                     CH28_PLACE_ON_NEAREST_FREE_TILE);

    data_fdps_map_cursor_draw_mode = CH28_MAP_CURSOR_BLANK;

    fdps_map_cursor_move_to(CH28_PAN_SPAWN_X, CH28_PAN_SPAWN_Y);

    /* The argument slot is the counter, as the assembly has it. */
    for (unit_index = 0;
         unit_index < CH28_PAN_HOLD_FRAMES;
         unit_index++) {
        fdps_render_view_frame();
    }

    data_fdps_map_cursor_draw_mode = CH28_MAP_CURSOR_NORMAL;
}

/* 000395d0.  Chapter 29's mid-map ambush trigger: the enemy groups holding the
   right, lower-right and upper-middle of the map stop guarding their ground and
   start hunting the party, but only when the unit that walked over the trigger
   tile is one of the party's own.

   The body is a side gate in front of one copy of the inline expansion the
   chapter 2, 5, 7, 13, 15 and 16 handlers in chevt1.c, chevt2.c and chevt3.c
   carry -- fdps_object_set_field34_low_nibble_range (00036b60) with the
   constant argument triple (0x24, 0x59, 0) -- and it has the same fingerprint:
   the three constants are parked at [EBP-0x24], [EBP-0x20] and [EBP-0x1c]
   (000395fb..00039609), copied into a second set of slots at [EBP-0x10],
   [EBP-0x14] and [EBP-0x18] (00039610..0003961f), and only then is the counter
   at [EBP-0xc] seeded from the first of them at 00039622.  There is no CALL to
   that helper in the body; the only CALLs are the two to fdps_get_unit_record,
   at 000395e0 for the acting unit and at 0003963a once per iteration, so
   writing the range as a call to the helper would put a CALL in the rebuild
   that the original does not make.

   The gate is MOV EAX,[EBP-0x4] / MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff /
   CMP EAX,0x2 / JNZ 0003965b at 000395eb..000395f9: the side byte is
   zero-extended to a full word before the compare, so it is an unsigned
   equality on the byte and nothing but the value 2 reaches the loop.  The jump
   target is the epilogue, so a non-player unit crossing the same tile leaves
   the map exactly as it was.  This is what separates this handler from the rest
   of the family: its argument is not overwritten with 0 on entry, it is pushed
   at 000395df and the record that comes back decides whether anything happens.

   The record pointer for the acting unit comes back in EAX from the CALL at
   000395e0 and is stored to [EBP-0x4] at 000395e8; the loop's record pointer
   comes back in EAX from the CALL at 0003963a and is stored to a different slot
   at [EBP-0x8] (00039642), so the loop does not disturb the acting unit's
   pointer -- two separate locals, not one reused.

   The compare at 0003962b -- CMP EAX,dword ptr [EBP-0x14] / JLE 00039637 -- is
   signed and inclusive, so the range is unit indices 0x24 through 0x59 and 0x59
   is the last index written, not one past the end.  map28.dat declares 12
   player slots in byte 1 and 80 scenario units in byte 2, so unit indices
   0x00..0x0b are the party and 0x0c..0x5b are deployment records 0..79; the
   range is records 24..77.  Records 24..59 carry behaviour byte 2 at deployment
   record offset 0x11 and are the 36 units this event releases, records 60..77
   already carry 0 and advanced from the first turn, and the top bound stops one
   short of unit index 0x5a: deployment records 78 and 79, the two level 30
   Guardian Dragons the chapter is named for, also carry behaviour 2 and are
   left holding their ground for the map's second trigger, slot 46, to release.
   Nothing is range checked and data_fdps_map_unit_count is not consulted; both
   bounds are literals in the instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x18] / OR DH,DL /
   MOV [EAX+0x34],DH at 00039648..00039656 -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   There is no one-shot latch: the instruction after the gate's JNZ is the first
   of the three constant stores, with no compare between them, and nothing
   records that the loop ran.  What makes it fire once is that the tile-trigger
   hand-off arms the slot and the turn driver resets it to 0xff before the next
   unit acts.

   Nothing sets EAX before the RET at 00039661 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_29_event_activate_enemy_groups(int unit_index)
{
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *released_unit;
    int released_unit_index;

    acting_unit = fdps_get_unit_record(unit_index);
    if (acting_unit->side == PLAYER_SIDE) {
        for (released_unit_index = 0x24;
             released_unit_index <= 0x59;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* 00039670.  Chapter 29's final ambush trigger, and the twin of the handler
   above: the same side gate, but behind it two of the family's inline range
   merges back to back instead of one, and between them they cover every unit
   map28.dat deploys.

   The gate is MOV EAX,[EBP-0x4] / MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff /
   CMP EAX,0x2 / JNZ 0003975f at 0003968b..00039699 -- the record's side byte is
   zero-extended to a full word before the compare, so it is an unsigned
   equality on the byte and only the value 2 reaches the loops.  The jump target
   is the epilogue, so an enemy or neutral unit crossing the same tile leaves
   the map exactly as it was.  As in the handler above the argument is not
   overwritten on entry: it is pushed at 0003967f and the record that comes back
   decides whether anything happens.

   Both bodies are inline expansions of fdps_object_set_field34_low_nibble_range
   (00036b60) with the same fingerprint the rest of the family carries -- the
   three constants parked at one set of slots and copied into a second set
   before the counter is seeded from the first of them.  The first expansion
   parks (0x0c, 0x59, 0) at [EBP-0x24], [EBP-0x20] and [EBP-0x1c]
   (0003969f..000396ad), copies them to [EBP-0x10], [EBP-0x14] and [EBP-0x18]
   (000396b4..000396c3) and seeds [EBP-0xc] at 000396c6.  The second parks
   (0x5a, 0x5b, 0x0a) at [EBP-0x28], [EBP-0x2c] and [EBP-0x30]
   (000396ff..0003970d), copies them to [EBP-0x34], [EBP-0x38] and [EBP-0x3c]
   (00039714..00039723) and seeds [EBP-0x40] at 00039726.  There is no CALL to
   that helper anywhere in the body; the only CALLs are the three to
   fdps_get_unit_record, at 00039680 for the acting unit and at 000396de and
   0003973e once per iteration of each loop, so writing either range as a call
   to the helper would put a CALL in the rebuild the original does not make.

   The three record pointers come back in EAX and go to three different slots --
   [EBP-0x4] at 00039688, [EBP-0x8] at 000396e6 and [EBP-0x44] at 00039746 -- so
   neither loop disturbs the acting unit's pointer.  Both compares are signed
   and inclusive: CMP EAX,[EBP-0x14] / JLE at 000396cc..000396d2 and CMP
   EAX,[EBP-0x38] / JLE at 0003972c..00039732, so 0x59 and 0x5b are the last
   indices written rather than one past the end.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; all four bounds are literals in
   the instruction stream.

   Both merges are the same read-modify-write of the one byte -- MOV
   DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x18 or -0x3c] / OR DH,DL / MOV
   [EAX+0x34],DH at 000396ec..000396fa and 0003974c..0003975a -- so only the
   behaviour code in the low nibble moves and the two AI flag bits above it are
   carried across untouched.

   What the two ranges mean comes from map28.dat: 12 player slots in byte 1 and
   80 scenario units in byte 2, so unit indices 0x00..0x0b are the party and
   0x0c..0x5b are deployment records 0..79.  The two ranges are disjoint: the
   first is 0x0c..0x59, deployment records 0..77, on mode 0, and the second is
   0x5a..0x5b, records 78 and 79 -- the two level 30 Guardian Dragons -- on mode
   0x0a.  Together they are all 80 records with each written exactly once, and
   running the two loops in the other order would leave the same state.

   There is no one-shot latch: the instruction after the gate's JNZ is the first
   of the first expansion's constant stores, with no compare between them, and
   nothing records that the loops ran.  What makes it fire once is that the
   tile-trigger hand-off arms the slot and the turn driver resets it to 0xff
   before the next unit acts.

   Nothing sets EAX before the RET at 00039765 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_29_event_activate_all_enemies(int unit_index)
{
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *released_unit;
    struct fdps_unit_record *guardian_unit;
    int released_unit_index;
    int guardian_unit_index;

    acting_unit = fdps_get_unit_record(unit_index);
    if (acting_unit->side == PLAYER_SIDE) {
        for (released_unit_index = 0x0c;
             released_unit_index <= 0x59;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
        for (guardian_unit_index = 0x5a;
             guardian_unit_index <= 0x5b;
             guardian_unit_index++) {
            guardian_unit = fdps_get_unit_record(guardian_unit_index);
            guardian_unit->ai_behavior = (unsigned char)
                ((guardian_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ITEM_THEN_ATTACK);
        }
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags the ambush below
   latches: the byte at 0x000640e8, which is 0x10 past the block's base at
   0x000640d8 -- CMP byte ptr [0x000640e8],0x0 at 0003977c and MOV byte ptr
   [0x000640e8],0x1 at 00039830.  It is the same slot the chapter 10, 16 and 19
   handlers in chevt2.c and chevt3.c latch, because the block is cleared at every
   chapter start and only one chapter is ever loaded. */
#define CH30W4_LATCH_SLOT 0x10

/* The wave the ambush brings on, PUSH 0x4 at 0003978c, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block.
   Four of map29.dat's records carry it: two of character id 0x55 and two of
   character id 0x6a, all at level 0x14 -- the LV20 死靈 x2 and LV20 白骨戰士 x2
   the guide's chapter 30 entry lists.  It is a literal and not the battle turn
   counter the turn-scheduled handlers of this family push, so the same four
   arrive whenever the trigger tile is crossed. */
#define CH30W4_WAVE 4

/* How those four are placed: XOR EAX,EAX / PUSH EAX at 00039789, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  Both of map29.cod's wave-4 spawn points carry
   two units, so a placement flag of 1 here would stack each pair on one tile. */
#define CH30W4_PLACE_NEAREST_FREE_TILE 0

/* The two values the handler leaves in data_fdps_map_cursor_draw_mode: 0 before
   the first pan (MOV dword ptr [0x00069cd0],0x0 at 0003979c), which matches none
   of fdps_draw_map_cursor's cases so nothing is painted for the whole sequence,
   and 1 after the second pan (MOV dword ptr [0x00069cd0],0x1 at 00039803), which
   is the plain box.  The second is a literal and not the mode the handler found
   on entry, so the obvious save/restore pair around the pans changes what the
   cursor wears once the ambush is over. */
#define CH30W4_MAP_CURSOR_BLANK 0
#define CH30W4_MAP_CURSOR_NORMAL 1

/* The two world pixels the view is walked to, in the order the pans visit them:
   PUSH 0x150 / PUSH 0x60 at 000397a6 and PUSH 0x150 / PUSH 0x198 at 000397d3.
   fdps_map_cursor_move_to takes world pixels, not tiles, so at the 24-pixel tile
   step they are tiles (4, 14) and (17, 14) -- beside the two clusters map29.cod
   gives chapter 30's wave-4 records, whose own spawn tiles are (5, 12) for the
   first pair and (15, 13) and (16, 13) for the second. */
#define CH30W4_PAN_LEFT_SPAWN_X 0x60
#define CH30W4_PAN_LEFT_SPAWN_Y 0x150
#define CH30W4_PAN_RIGHT_SPAWN_X 0x198
#define CH30W4_PAN_RIGHT_SPAWN_Y 0x150

/* How long the view rests on each of the two, the CMP against 0xc at 000397bc
   and 000397ec.  Each composed frame costs one timer tick inside
   fdps_render_view_frame, so twelve is the length of the pause and not an
   instruction budget (rebuild_info/pitfalls.md, contract D): shortening it
   shortens the look the player gets at the reinforcements by that many ticks. */
#define CH30W4_PAN_HOLD_FRAMES 0xc

/* The entry of the chapter's own text block the arrival line is spoken from,
   PUSH 0x8 at 00039820. */
#define CH30W4_ARRIVAL_TEXT_ID 8

/* The mode 13h aperture and its row stride, PUSH 0xa0000 and PUSH 0x140 at
   0003981b and 00039816.  0xa0000 stays a literal because it is where the
   display adapter answers and not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 00039811,
   0003980f and 0003980d: glyph fill, no cell background, and the shadow the
   outline colour becomes while the font's outline flag is clear.  Every ordinary
   line of spoken game text is drawn with these three. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* 00039770.  Chapter 30's reinforcement ambush: the first unit to finish a step
   onto the map's trigger tile brings on the map's four wave-4 enemies, the view
   is panned over both of the places they arrive and the chapter's line about
   them is spoken.

   The frame is the family's four-push one with an empty local area -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00039770..00039776 -- so there is no local in this function at all.  Every
   caller-clean in the body is this function's own (ADD ESP,0xc after the
   deployment, ADD ESP,0x8 after each cursor move and ADD ESP,0x1c after the
   draw) and the RET at 0003983b carries no immediate, so the convention is the
   stack one.

   THE ONE GATE IS THE SHARED LATCH AND THERE IS NO SIDE TEST.  CMP byte ptr
   [0x000640e8],0x0 / JNZ at 0003977c jumps straight to the epilogue and nothing
   else in the body is conditional, so unlike the chapter 19 ambush this one is
   sprung by whichever unit crosses the tile -- an enemy's step springs it as
   readily as one of the party's.  fdps_get_unit_record is never called and the
   argument never reaches a record.

   THE LATCH IS THE SHARED CHAPTER-EVENT ARRAY AND NOT A PRIVATE STATIC.  Element
   0x10 of data_fdps_map_cell_event_triggered_flags is cleared for the whole
   block by fdps_chapter_state_reset at every chapter start and restored by
   fdps_load_savegame, and a dozen handlers of other chapters latch the same
   byte.  A function-local static would fire the ambush once per process, so
   replaying chapter 30 after a Game Over, or loading a save made before the
   trigger, would silently skip the reinforcements.

   THE LATCH IS RAISED LAST, after the line has been spoken, which is the
   opposite end of the body from the chapter 10 ambush's in chevt2.c.  Nothing
   this handler calls can re-enter it, so the two placements are
   indistinguishable from outside and the emitted one is where the original puts
   it.

   THE DEPLOYMENT COMES BEFORE THE PANS and the order is what the player sees:
   the four are standing on the map before the view starts crossing the places
   they arrived at.  Nothing tests whether the deployment appended anything, so a
   map whose wave 4 matches no record still blanks the cursor, runs both pans and
   speaks the line.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   000397b5 and 000397e5 writes zero over the incoming argument at the head of
   each hold loop and each loop compares and INCs that same slot, so the counter
   and the parameter are one storage location -- which is what the empty local
   area leaves room for.  The store has no observable effect on the caller,
   because the slot belongs to its outgoing argument area and the dispatcher
   drops it with its own ADD ESP,0x4.  Both loops are the -od shape of a for
   statement: the compare at the top, a dead MOV EAX,[EBP+0x14] ahead of the INC,
   and the body reached by a JL past the exit jump.  Both are signed and both
   stop at 12.

   NO VALUE IS USED AFTER A CALL.  fdps_deploy_wave, fdps_map_cursor_move_to and
   fdps_render_view_frame all return nothing, and the instruction after each of
   the first two CALLs is the ADD ESP that cleans its arguments.  fdps_draw_text
   does hand back the cursor it stopped at and it is discarded: the instructions
   after its CALL at 00039828 are the ADD ESP,0x1c and the latch store, neither
   of which reads EAX.  The MOV EAX,[EBP+0x14] at 000397c4 and 000397f4 loads the
   counter and nothing reads it -- it is the -od expansion of the loop's own
   increment, not a use of anything a CALL left behind.  Nothing sets EAX before
   the RET and no dispatcher reads what comes back, so the result is void. */
void fdps_chapter_30_event_deploy_wave_4(int unit_index)
{
    if (data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT] == 0) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH30W4_WAVE,
                         CH30W4_PLACE_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH30W4_MAP_CURSOR_BLANK;

        fdps_map_cursor_move_to(CH30W4_PAN_LEFT_SPAWN_X,
                                CH30W4_PAN_LEFT_SPAWN_Y);
        /* The argument slot is the counter, as the assembly has it. */
        for (unit_index = 0;
             unit_index < CH30W4_PAN_HOLD_FRAMES;
             unit_index++) {
            fdps_render_view_frame();
        }

        fdps_map_cursor_move_to(CH30W4_PAN_RIGHT_SPAWN_X,
                                CH30W4_PAN_RIGHT_SPAWN_Y);
        for (unit_index = 0;
             unit_index < CH30W4_PAN_HOLD_FRAMES;
             unit_index++) {
            fdps_render_view_frame();
        }

        data_fdps_map_cursor_draw_mode = CH30W4_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH30W4_ARRIVAL_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                       MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

        data_fdps_map_cell_event_triggered_flags[CH30W4_LATCH_SLOT] = 1;
    }
}

/* The two entries of the chapter's own text block the handler below speaks:
   PUSH 0xf at 00039866 and PUSH 0x10 at 000398ab.  Both are drawn at the same
   aperture origin in the same colours, so the second lands over the first. */
#define CH30_WAVE2_FALL_TEXT_ID 0x0f
#define CH30_WAVE2_ARRIVAL_TEXT_ID 0x10

/* The map cell event code the scripted terrain change is keyed on.  MOV byte
   ptr [0x000640da],0x1 at 00039876 reads like a flag of its own, because Ghidra
   labels the target DAT_000640da, but 0x000640da is element 2 of
   data_fdps_map_cell_event_triggered_flags, whose base is 0x000640d8: the table
   fdps_map_apply_triggered_cell_changes indexes with each cell's event code
   (CMP byte ptr [EAX + 0x640d8],0x0 at 0002e9b0) and that
   fdps_map_actor_behavior_step, fdps_battle_search_cell_at_cursor and
   fdps_icon_script_run address as base + index.  Written as a separate flag the
   table stays untouched and the call on the next line then changes no terrain at
   all, so chapter 30's map would keep its old tiles when the first form dies
   (rebuild_info/pitfalls.md, contract H). */
#define CH30_WAVE2_TERRAIN_CELL_CODE 2

/* The wave the handler asks for, PUSH 0x2 at 00039888.  A literal, like the
   wave-3 handler's below and unlike the turn-scheduled handlers of chapters 17,
   18, 23, 24 and 28, which push the battle turn counter instead. */
#define CH30_WAVE2_WAVE_NO 2

/* How that wave is placed: MOV EAX,0x1 / PUSH EAX at 00039882, the same
   exact-tile flag the wave-3 handler passes, so the arriving form is put on the
   tile its own placement record names rather than beside it. */
#define CH30_WAVE2_PLACE_EXACT 1

/* 00039840.  Chapter 30's "the first form has fallen" event: the chapter's line
   is spoken, the map's scripted terrain change is applied, the second form of
   平衡之神 is deployed and a second line is spoken.

   The frame is the family's four-push one with an empty local area -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00039840..00039846 -- so there is no local in this function at all.  Every
   caller-clean in the body is this function's own (ADD ESP,0x1c after each draw
   and ADD ESP,0xc after the deployment) and the RET at 000398bf carries no
   immediate, so the convention is the stack one.

   THERE IS NO GATE AND NO LATCH.  The body has no compare and no conditional
   jump in it, and it is the one handler of this family whose trigger-table write
   is not the shared one-shot slot: 0x000640da is element 2 and the one-shot
   handlers all use 0x000640e8 or 0x000640e9.  So this handler runs its whole
   body every time it is reached, and reaching it twice speaks both lines twice
   and appends a second copy of the wave.

   THE STORE AND THE CALL AFTER IT ARE ONE OPERATION.  Element 2 of the trigger
   table is the terrain change's key and
   fdps_map_apply_triggered_cell_changes is what makes it visible; the mark is
   the state that persists across a save and the call is the view of it.  See
   CH30_WAVE2_TERRAIN_CELL_CODE above for why it cannot be a variable of its own.

   THE ARGUMENT SLOT IS OVERWRITTEN FIRST.  MOV dword ptr [EBP+0x14],0x0 at
   0003984c is the first instruction after the frame and nothing reads the slot
   afterwards, so which unit died cannot reach the entry drawn, the cell code
   marked, the map asked for, the wave asked for or the placement flag.  The
   store has no observable effect on the caller, because the slot belongs to its
   outgoing argument area and fdps_run_death_scripts drops it with its own ADD
   ESP,0x4.

   NO VALUE IS USED AFTER A CALL.  fdps_map_apply_triggered_cell_changes and
   fdps_deploy_wave return nothing, and the instruction after the deployment's
   CALL is the ADD ESP that cleans its arguments.  fdps_draw_text does hand back
   the cursor it stopped at, and both times it is discarded: the instruction
   after the CALL at 0003986e is the ADD ESP,0x1c and then the trigger store,
   and after the CALL at 000398b3 it is the ADD ESP,0x1c and then the epilogue,
   none of which reads EAX.  The MOV EAX,0x1 at 00039882 is the placement flag
   being loaded for its PUSH and not a use of anything a CALL left behind.
   Nothing sets EAX before the RET and no dispatcher reads what comes back, so
   the result is void. */
void fdps_chapter_30_event_deploy_wave_2(int unit_index)
{
    unit_index = 0;

    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH30_WAVE2_FALL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE,
                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                   MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

    data_fdps_map_cell_event_triggered_flags[CH30_WAVE2_TERRAIN_CELL_CODE] = 1;
    fdps_map_apply_triggered_cell_changes();

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     CH30_WAVE2_WAVE_NO,
                     CH30_WAVE2_PLACE_EXACT);

    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH30_WAVE2_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE,
                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                   MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
}

/* The wave the chapter 30 handler below asks for: PUSH 0x3 at 000398d9, a
   literal and not a value read from anywhere.  That is the whole difference
   between this handler and the turn-scheduled ones of chapters 17, 18, 23 and
   24, which push the battle turn counter instead, and it is what makes this one
   deploy the same group whenever it is reached rather than a group that depends
   on when it was reached. */
#define CH30_WAVE3_WAVE_NO 3

/* How that wave is placed: MOV EAX,0x1 / PUSH EAX at 000398d3, so
   fdps_deploy_wave passes 1 on to fdps_deploy_unit and every unit is put on the
   tile its MAP%02d.COD placement record names, exactly, with no search and no
   test of what is standing there or what the terrain is.  A 0 here -- what the
   turn-scheduled reinforcement handlers push -- would send each unit to the
   nearest unoccupied walkable tile to those coordinates instead, which for a
   scripted single-unit arrival is a different tile whenever the tile the script
   picked is occupied or blocked. */
#define CH30_WAVE3_PLACE_EXACT 1

/* 000398c0.  Chapter 30's "the second form has fallen" event: brings on the
   third and final form of the boss by deploying the current map's wave-3 units.

   The whole body is one call.  The frame is the standard Watcom four-push one
   with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP /
   MOV EBP,ESP / SUB ESP,0x0 at 000398c0..000398c6 -- so there is no local here
   at all and the three arguments go straight into the pushes: MOV EAX,0x1 /
   PUSH EAX, PUSH 0x3, PUSH dword ptr [0x00069cf4] at 000398d3..000398db.  The
   caller-cleans ADD ESP,0xc at 000398e6 is this function's own, which is what
   makes the convention the stack one.

   The map number is data_fdps_chapter_current_chapter_id read at the call site
   and not anything this handler holds, so it is whichever chapter is loaded --
   the same way the chapter 10 ambush in chevt2.c and the chapter 17 and 18
   handlers in chevt3.c read it.

   What wave 3 means comes from the shipped map: this slot is named exactly once
   in the whole game, by the death script of map29.dat's deployment record 1 --
   chapter 30, the level 40 character id 61 that is the second form of 平衡之神
   -- and wave 3 of that same file is the single record carrying character id 62,
   its third form.  So the event is the boss's second form dying and its third
   arriving, and the placement flag of 1 is why that arrival lands on the tile
   the script chose for it rather than beside it.  The third form's own death
   script is opcode 4, the battle-cleared verdict, so nothing deploys after it.

   Nothing guards the call and nothing records that it ran, so the handler
   deploys wave 3 every time it is reached; what makes the form arrive once is
   that fdps_collect_death_scripts takes a unit's death script off it before
   fdps_play_death_animation_and_mark_dead marks it removed, so the record is
   collected once per death.  Deploying twice would append a second copy of the
   wave rather than being refused.

   unit_index is the handler table's shared parameter, and here it is the index
   of the unit whose death ran the script.  MOV dword ptr [EBP+0x14],0x0 at
   000398cc writes zero over the incoming slot before anything else happens and
   nothing ever reads it back, so which unit died cannot reach the wave asked
   for, the map asked for or the placement flag; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   fdps_run_death_scripts drops it with ADD ESP,0x4 at 0001dcb4.

   Nothing sets EAX between the CALL's return and the RET at 000398ed, and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_30_event_deploy_wave_3(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     CH30_WAVE3_WAVE_NO,
                     CH30_WAVE3_PLACE_EXACT);
}

/* The two character ids the revival accepts, out of CMP EAX,0x55 at 000107a5
   and CMP EAX,0x6a at 000107b5.  0x55 is 死靈 and 0x6a is 白骨戰士, the two
   undead types map29.dat tags as chapter 30's wave 4; the assets/ tables name
   them.  Both loads are MOV AL,byte ptr [EAX+0x7] followed by AND EAX,0xff, so
   the byte is widened UNSIGNED and the compare is an equality on 0..255. */
#define CH30_REVIVE_WRAITH_CHAR_ID   0x55
#define CH30_REVIVE_SKELETON_CHAR_ID 0x6a

/* Where each type is put back, out of the four literals at 0001080f..00010826.
   They are the spawn coordinates map29.cod gives the two types' own placement
   records, and they are hard-coded here rather than read from the record the
   unit was deployed from -- a revived unit therefore heads back to the type's
   scripted corner of the map and not to where it died. */
#define CH30_REVIVE_WRAITH_ANCHOR_X    5
#define CH30_REVIVE_WRAITH_ANCHOR_Y    12
#define CH30_REVIVE_SKELETON_ANCHOR_X  15
#define CH30_REVIVE_SKELETON_ANCHOR_Y  13

/* The movement grid's two-byte cells start after its 4-byte header, and byte 0
   of a cell carries 0x40 when a unit stands on the tile: ADD EAX,EDX / MOV
   AL,byte ptr [EAX + 0x4] / AND AL,0x40 at 0001087e.  The same two numbers are
   spelled out in src/deploy.c for the same search; neither header owns them,
   because the grid's layout is movegrid.h's prose and not a C declaration. */
#define CH30_REVIVE_GRID_CELL_BASE 4
#define CH30_REVIVE_GRID_OCCUPIED  0x40

/* What the best-so-far distance starts at, MOV dword ptr [EBP-0x10],0xff at
   0001082d.  It is a distance and not a sentinel, so a map whose every free
   walkable cell is further than 255 tiles from the anchor finds nothing. */
#define CH30_REVIVE_START_DISTANCE 0xff

/* The terrain movement cost the search will still stand a unit on: CMP EAX,0x5
   / JGE at 000108ce, so 0..4 are accepted and 5 and above are not.  Same bound
   as fdps_deploy_unit's own search. */
#define CH30_REVIVE_TERRAIN_LIMIT 5

/* The white flash: the bias starts at 0x40 (MOV dword ptr [EBP-0xc],0x40 at
   0001092b) and steps down to 0 inclusive, which is 65 uploads of the palette,
   each followed by delay(4) (PUSH 0x4 at 00010963).  fdps_set_palette_range
   clamps every biased channel to 63, so a bias of 0x40 forces all 256 entries
   to white however dark the palette under it was, and the fade back is what the
   next 64 steps do.  The whole DAC is rewritten every step: 0 and 0xff are the
   first and last entry pushed at 00010953 and 0001094e. */
#define CH30_REVIVE_FLASH_BIAS_START 0x40
#define CH30_REVIVE_FLASH_STEP_MS    4
#define CH30_REVIVE_DAC_FIRST_ENTRY  0
#define CH30_REVIVE_DAC_LAST_ENTRY   0xff

/* The effect that plays over the unit that came back, the string at 0006155c,
   and the one unit it plays over -- PUSH 0x1 at 0001097f.  The container the
   member is pulled from is MISC.VFS, a literal inside
   fdps_play_vfs_animation_over_units and not named here.  The same clip is the
   healing effect src/item.c plays; the misspelling is the shipped member's
   name. */
#define CH30_REVIVE_EFFECT_CLIP  "Posion.saf"
#define CH30_REVIVE_EFFECT_UNITS 1

/* 00010760.  See chevt6.h for what this does to a battle.  The frame is the
   standard Watcom four-push one with SUB ESP,0x34, no argument slot is read and
   nothing sets EAX before the RET at 000109ae, so it is void(void); the one
   call site at 0001067e pushes nothing and adjusts nothing afterwards.

   THE NEAREST-TILE SEARCH KEEPS THE LAST TIE, NOT THE FIRST.  CMP EAX,[EBP-0x10]
   / JLE at 000108b0 accepts a candidate whose distance merely EQUALS the best so
   far, so in a row-major scan the winning cell is the last one at the winning
   distance.  Writing the obvious "distance < best_distance" moves the revived
   unit to a different tile on any map where more than one free cell ties, which
   on map29 is the normal case because the anchor tile itself is usually
   occupied.  fdps_deploy_unit's search carries the identical quirk.

   THE RECOMPUTE INSIDE THE ACCEPT BRANCH IS NOT REPRODUCED.  The original calls
   abs twice more at 000108da and 000108eb to rebuild the distance it already has
   in EAX; abs is a pure function of two values nothing between the two
   computations changes, so assigning the distance in hand is the same arithmetic
   (ADR-0001).  Same call for the same reason in src/deploy.c.

   THE CHOSEN CELL IS LEFT UNINITIALISED WHEN THE SEARCH FINDS NOTHING.  The two
   slots at [EBP-0x18] and [EBP-0x14] are written only from inside the accept
   branch at 000108f8, and the stores into the record at 0001090e run whatever
   happened, so an entirely occupied or entirely unwalkable map puts stack
   residue into the unit's tile bytes.  Seeding them with the anchor is the
   obvious repair and it would place a unit the original places elsewhere.

   THE RECORD POINTER IS NOT RE-RESOLVED after the four calls in the middle of
   the body: [EBP-0x8] is loaded once at 00010797 and still used at 00010989.
   Nothing in this body moves the unit array, which is what makes that safe here;
   fdps_get_unit_record's own note says why a pointer held across
   fdps_relocate_unit_array would not be.

   THE GRID IS REBUILT PER REVIVAL AND LEFT BLANK AFTERWARDS.  Everything from
   the reset at 000107cf to the reset at 0001099e runs once for each unit that
   qualifies, so two dead undead cost two white flashes and two effect
   animations, and the second search sees the first unit's new tile marked.  The
   trailing reset is what the movement code expects to find.

   THE STATUS BYTE IS ASSIGNED 0, NOT MASKED.  MOV byte ptr [EAX + 0x5],0x0 at
   0001098c, the whole byte, so the per-turn redraw bit 0x80 goes down with the
   retired bit 0.  A "&= ~1" would leave a revived unit carrying it.

   Contract C is live on the two grid dimensions: both arrive through MOVSX at
   000107ed and 000107f8 and both loop bounds are the signed JL, so a header word
   of 0xffff is -1 and the scan runs zero rows.  Read unsigned it is 65535 and
   the scan walks off the block.

   The character id, the tile bytes and the terrain code all arrive zero-extended
   out of bytes (AND EAX,0xff at 000107a0 and 000107b0, XOR EAX,EAX / MOV AL at
   000108c7), which is what struct fdps_unit_record's unsigned char fields and
   data_fdps_map_tile_terrain_type's unsigned char give without a cast. */
void fdps_chapter_30_revive_wave_4_undead(void)
{
    /* The record being considered, resolved once per index and then held
       across the whole revival. */
    struct fdps_unit_record *unit;
    /* The movement-grid cell the scan is standing on. */
    struct fdps_move_grid_cell *cell;
    /* Which unit of the live array the sweep has reached. */
    int unit_index;
    /* The grid header's two dimensions, in tiles. */
    int grid_width;
    int grid_height;
    /* The type's scripted spawn tile, which the search measures from. */
    int anchor_x;
    int anchor_y;
    /* Where the scan is. */
    int scan_x;
    int scan_y;
    /* This cell's Manhattan distance from the anchor, and the shortest one any
       accepted cell has had so far. */
    int distance;
    int best_distance;
    /* The tile the search settled on. */
    int chosen_x;
    int chosen_y;
    /* How much white this step of the flash adds to every DAC channel. */
    int flash_bias;
    /* The one-entry unit list the effect animation is played over.  Its address
       is what the call takes, which is why it is a variable and not the index
       itself. */
    unsigned char revived_unit_id;

    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->portrait_id != CH30_REVIVE_WRAITH_CHAR_ID
            && unit->portrait_id != CH30_REVIVE_SKELETON_CHAR_ID) {
            continue;
        }
        if (fdps_unit_is_retired(unit_index) == 0) {
            continue;
        }

        /* The occupancy this search reads is rebuilt rather than trusted: the
           reset clears both zone bits off every cell and the two marking passes
           -- one per side select -- put 0x40 back on the tile of every unit
           still in play.  The unit being revived is retired, so neither pass
           marks it and its own old tile is a candidate again. */
        fdps_map_grid_reset();
        fdps_move_grid_mark_opposing_zones_of_control(0);
        fdps_move_grid_mark_opposing_zones_of_control(1);

        grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
        grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

        if (unit->portrait_id == CH30_REVIVE_WRAITH_CHAR_ID) {
            anchor_x = CH30_REVIVE_WRAITH_ANCHOR_X;
            anchor_y = CH30_REVIVE_WRAITH_ANCHOR_Y;
        } else {
            anchor_x = CH30_REVIVE_SKELETON_ANCHOR_X;
            anchor_y = CH30_REVIVE_SKELETON_ANCHOR_Y;
        }

        best_distance = CH30_REVIVE_START_DISTANCE;
        for (scan_y = 0; scan_y < grid_height; scan_y++) {
            for (scan_x = 0; scan_x < grid_width; scan_x++) {
                cell = (struct fdps_move_grid_cell *)
                       (data_fdps_battle_move_grid_ptr +
                        CH30_REVIVE_GRID_CELL_BASE) +
                       (scan_y * grid_width + scan_x);
                if ((cell->flags & CH30_REVIVE_GRID_OCCUPIED) != 0) {
                    continue;
                }
                distance = abs(scan_x - anchor_x) + abs(scan_y - anchor_y);
                if (distance > best_distance) {
                    continue;
                }
                fdps_map_load_tile_info(scan_x, scan_y);
                if (data_fdps_map_tile_terrain_type
                        < CH30_REVIVE_TERRAIN_LIMIT) {
                    best_distance = distance;
                    chosen_x = scan_x;
                    chosen_y = scan_y;
                }
            }
        }

        unit->pos_x = (unsigned char) chosen_x;
        unit->pos_y = (unsigned char) chosen_y;
        fdps_map_cursor_move_to_unit(unit_index);

        for (flash_bias = CH30_REVIVE_FLASH_BIAS_START;
             flash_bias >= 0;
             flash_bias--) {
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                CH30_REVIVE_DAC_FIRST_ENTRY, CH30_REVIVE_DAC_LAST_ENTRY,
                flash_bias, flash_bias, flash_bias);
            delay((unsigned int) CH30_REVIVE_FLASH_STEP_MS);
        }

        revived_unit_id = (unsigned char) unit_index;
        fdps_play_vfs_animation_over_units(CH30_REVIVE_EFFECT_UNITS,
                                           &revived_unit_id,
                                           CH30_REVIVE_EFFECT_CLIP);

        unit->flags = 0;
        unit->hp_current = unit->hp_max;
        fdps_map_grid_reset();
    }
}
