/* chevt4.c -- the scripted chapter-event handlers of chapters 20 to 23.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt4.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for chapter 8, chevt2b.c for 9 to 14 and
 * chevt3.c for 15 to 19.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "text.h"
#include "deploy.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "chevt4.h"

/* The one unit index the chapter 20 event answers to, CMP dword ptr
   [EBP+0x14],0x0 / JNZ at 00038310.  Battle unit 0 is Randis: the deployment
   lays the player's roster down first and he is always its first record, so no
   chapter can present him at another index. */
#define RANDIS_UNIT_INDEX 0

/* The last turn the sword upgrade still happens on, CMP dword ptr
   [0x00069ce8],0x14 / JLE at 00038316.  The compare is signed and the jump is
   JLE, so twenty is inclusive and turn 21 is the first that is too late.  The
   counter starts a battle at 1. */
#define CH20_UPGRADE_LAST_TURN 0x14

/* What fdps_unit_find_item_slot reports when the item is not in the bag, CMP
   dword ptr [EBP-0x4],-0x1 / JNZ at 00038321 (unititem.h). */
#define CH20_SWORD_NOT_CARRIED (-1)

/* The sword that is taken and the sword that is given, PUSH 0xa0 at 000382fc
   and PUSH 0xa1 at 0003835c: 灼烈之劍, which the chapter 16 smith forges, and
   火光之劍 (assets/items.md). */
#define CH20_BLAZING_SWORD_ITEM_ID 0xa0
#define CH20_FLAME_SWORD_ITEM_ID 0xa1

/* The entry of the chapter's own text block the fire god's line is spoken from,
   PUSH 0x13 at 0003833c.  FDETXT20.TXT carries exactly twenty strings, so this
   is its last one. */
#define CH20_UPGRADE_TEXT_ID 0x13

/* The destination the handler hands fdps_draw_text and the screen's row stride,
   PUSH 0xa0000 at 00038337 and PUSH 0x140 at 00038332: the top-left corner of
   the visible page.  0xa0000 stays a literal because it is an address inside
   the display adapter's aperture rather than the address of anything the linker
   places (rebuild_info/pitfalls.md, contract E). */
#define CH20_UPGRADE_TEXT_DEST 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 0003832d,
   0003832b and 00038329: glyph fill, no cell background, and the shadow the
   outline colour becomes while the font's outline flag is clear. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* 000382f0.  Chapter 20's sword-upgrade event: Randis finishes a step onto the
   map's trigger tile still carrying 灼烈之劍 and the fire god swaps it for
   火光之劍.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   000382f0..000382f6 -- and that local is the slot number at [EBP-0x4].  Every
   caller-clean in the body is this function's own (ADD ESP,0x8 after the slot
   search, ADD ESP,0x1c after the draw, ADD ESP,0x8 after each of the two
   inventory calls and ADD ESP,0x4 after the stat rebuild), the RET at 0003837d
   carries no immediate, and the dispatcher pushes one dword and drops it with
   ADD ESP,0x4 at 0001583f, so the convention is the stack one at both ends of
   the call.

   THE SEARCH RUNS BEFORE ANY OF THE GATES.  fdps_unit_find_item_slot is called
   at 00038305, unconditionally, and only then are the three tests made at
   00038310..00038325 in this order: the unit index, the turn counter and the
   search result.  So every firing -- including a firing by a unit that is not
   Randis, and every firing after the deadline -- costs one walk of that unit's
   inventory.  Folding the search into the condition, which is what the
   short-circuit spelling of the same test would do, moves the call inside the
   gates and skips it on those firings.

   THE DEADLINE IS INCLUSIVE AND IT IS THE ONLY THING THAT EVER CLOSES THE
   EVENT.  CMP dword ptr [0x00069ce8],0x14 / JLE at 00038316 is a signed test on
   a counter that starts the battle at 1, so turn 20 still upgrades the sword.
   Writing the obvious < 20 costs the player 真炎龍劍 in chapter 25, which
   fdps_chapter_25_event_upgrade_randis_sword hands out only to a Randis already
   carrying 火光之劍.

   THERE IS NO ONE-SHOT LATCH.  Nothing in the body writes
   data_fdps_map_cell_event_triggered_flags and nothing marks the cell consumed;
   the swapped item is what stops the event repeating, because the search misses
   once the sword has become 火光之劍.  Adding the latch the sibling chapter
   events use would be wrong in the other direction: a unit that is not Randis
   has to be able to walk over the tile and leave it armed for him.

   THE THIRD CALL TAKES A LITERAL AND NOT THE PARAMETER.  PUSH 0x0 at 00038361
   is what fdps_unit_add_item is given, while the removal at 00038350 and the
   stat rebuild at 0003836b both reload [EBP+0x14].  The gate above has already
   forced the two to be equal, so this is a spelling and not a behaviour, and
   both spellings are kept where the assembly has them.

   Only one value is used after a CALL: fdps_unit_find_item_slot's, stored to
   [EBP-0x4] at 0003830d and reloaded at 00038321 for the gate and at 0003834c
   for the removal.  fdps_draw_text's cursor is discarded -- the next
   instruction is the load of the slot -- fdps_unit_add_item's result is
   discarded likewise, and fdps_unit_recompute_combat_stats returns nothing.
   Nothing sets EAX before the RET and the dispatcher reads nothing back, so the
   result is void. */
void fdps_chapter_20_event_upgrade_randis_sword(int unit_index)
{
    int sword_slot;

    sword_slot = fdps_unit_find_item_slot(unit_index,
                                          CH20_BLAZING_SWORD_ITEM_ID);

    if (unit_index == RANDIS_UNIT_INDEX
            && data_fdps_battle_turn_counter <= CH20_UPGRADE_LAST_TURN
            && sword_slot != CH20_SWORD_NOT_CARRIED) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH20_UPGRADE_TEXT_ID,
                       (unsigned char *) CH20_UPGRADE_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_unit_remove_item(unit_index, sword_slot);
        /* The literal the assembly pushes, not unit_index. */
        fdps_unit_add_item(RANDIS_UNIT_INDEX, CH20_FLAME_SWORD_ITEM_ID);
        fdps_unit_recompute_combat_stats(unit_index);
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   one-shot chapter events latch, MOV byte ptr [0x000640e8],0x1 at 000383ec
   against the array's base 0x000640d8.  It is the first element the map's own
   per-cell event codes cannot reach, and it is shared: twelve chapter handlers
   across different chapters latch this one byte and three post-action handlers
   read it back.

   Being inside that array is what keeps the latch honest.
   fdps_chapter_state_reset clears all 0x20 bytes when a chapter starts and the
   save and load paths move the whole array to and from the slot image, so a
   function-local static in its place would leave the ambush spent across a
   chapter restart and across a reload (rebuild_info/pitfalls.md).

   src/chevt1.c, src/chevt2b.c and src/chpost3.c spell the same slot out for the
   same reason; it stays file-local at every end because no header owns it. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The side byte value the gate at 000383af tests for.  2 is the player's own
   roster, 0 the enemy and 1 the guest; fdps_roster_add_character writes 2 into
   struct fdps_unit_record's side at record offset 6 and fdps_deploy_unit copies
   it out of the deployment record.  src/chevt6.c, src/combat.c, src/deploy.c
   and src/unitatk.c spell the same constant out; it stays file-local at every
   end because no header owns it. */
#define PLAYER_SIDE 2

/* The wave the ambush brings on, PUSH 0x1 at 000383b9: matched by
   fdps_deploy_wave against byte 0x15 of each 0x1a-byte deployment record of the
   resident MAP%02d.DAT block.  Sixteen of MAP20.DAT's records carry it. */
#define CH21_WAVE_1 1

/* How the sixteen are placed, XOR EAX,EAX / PUSH EAX at 000383b6: zero, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival settles on
   the nearest free walkable tile to its placement record's coordinates rather
   than on those coordinates themselves. */
#define CH21_WAVE_1_PLACE_EXACT 0

/* The entry of the chapter's own text block the ambush line is spoken from,
   PUSH 0x14 at 000383dc, and where it is drawn: PUSH 0xa0000 at 000383d7, the
   top-left corner of the visible mode-13h page.  0xa0000 stays a literal
   because it is an address inside the display adapter's aperture rather than
   the address of anything the linker places (rebuild_info/pitfalls.md,
   contract E). */
#define CH21_WAVE_1_TEXT_ID 0x14
#define CH21_WAVE_1_TEXT_DEST 0x000a0000

/* 00038380.  Chapter 21's first ambush: the first unit of the player's own side
   to finish a step onto the map's trigger tile brings on the sixteen enemies
   tagged wave 1 and the chapter's line about them is spoken.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   00038380..00038386 -- and that local is the unit record pointer at [EBP-0x4].
   Every caller-clean in the body is this function's own (ADD ESP,0x4 after the
   record lookup, ADD ESP,0xc after the deployment and ADD ESP,0x1c after the
   draw), the RET at 000383f9 carries no immediate, and the dispatcher pushes
   one dword and drops it with ADD ESP,0x4 at 0001583f, so the convention is the
   stack one at both ends of the call.

   THE RECORD IS FETCHED BEFORE EITHER GATE.  CALL 0x0002d210 at 00038390 is
   unconditional and its result is stored to [EBP-0x4] at 00038398; only then is
   CMP byte ptr [0x000640e8],0x0 / JNZ made at 0003839b.  So a firing on a latch
   that is already up still costs one call into the unit array.  The chapter 19
   ambush at 000381d0 really does put the fetch inside the latch gate, and the
   two shapes are not interchangeable spellings of each other.

   THE SIDE TEST IS AN EQUALITY ON 2 AND NOT THE FAMILY'S "NOT 0".  MOV AL,byte
   ptr [EAX+0x6] / AND EAX,0xff / CMP EAX,0x2 / JZ at 000383a7..000383b2 is an
   unsigned compare of the whole byte against one value, so side 1, the guest
   side, is refused here where the chapter 10 and chapter 19 ambushes admit it.
   Writing this gate as the neighbours' side != 0 lets a guest unit spring the
   ambush.

   THE LATCH IS RAISED LAST, after the deployment and after the line, where the
   chapter 10 ambush raises it before its deployment.  Nothing either handler
   calls can re-enter it, so the two placements are indistinguishable from
   outside and the emitted one is where the original puts it.  The test is
   against 0 rather than against 1, so any non-zero value in the slot blocks the
   body.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at the call site (PUSH dword ptr
   [0x00069cf4] at 000383bb) and not out of anything this handler holds, so it
   is whichever chapter is loaded -- 20 for this one, which is the map20 the
   handler's table slot is only ever named from.

   Only one value is used after a CALL: fdps_get_unit_record's, stored to
   [EBP-0x4] at 00038398 and reloaded at 000383a4 for the one byte the gate
   reads.  fdps_deploy_wave leaves nothing this body reads, and fdps_draw_text's
   cursor is discarded -- the next instruction is the latch store.  Nothing sets
   EAX before the RET and no dispatcher reads what comes back, so the result is
   void. */
void fdps_chapter_21_event_deploy_wave_1(int unit_index)
{
    struct fdps_unit_record *triggering_unit;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT]
            == 0
            && triggering_unit->side == PLAYER_SIDE) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH21_WAVE_1,
                         CH21_WAVE_1_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH21_WAVE_1_TEXT_ID,
                       (unsigned char *) CH21_WAVE_1_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags the SECOND one-shot
   chapter event latches, MOV byte ptr [0x000640e9],0x1 at 0003846f against the
   array's base 0x000640d8.  It is the slot next to the one the handler above
   uses, and the two have to be different bytes: map20.dat's tile-event table
   names slot 29 and slot 30 back to back, so both ambushes are armed on the
   same map at the same time and a shared latch would let whichever fired first
   suppress the other.  Codes 0 to 15 are all the shipped M%02d.DTL event planes
   reach, so 0x11 is as unreachable by a cell of the map's own event layer as
   0x10 is.

   Being inside that array is what keeps the latch honest here too:
   fdps_chapter_state_reset clears all 0x20 bytes when a chapter starts and the
   save and load paths move the whole array to and from the slot image, so a
   function-local static in its place would leave the ambush spent across a
   chapter restart and across a reload (rebuild_info/pitfalls.md).

   src/chevt1.c spells the same slot out for the same reason; it stays
   file-local at both ends because no header owns it. */
#define CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT 0x11

/* The wave the second ambush brings on, PUSH 0x2 at 0003843c: matched by
   fdps_deploy_wave against byte 0x15 of each 0x1a-byte deployment record of the
   resident MAP%02d.DAT block.  Sixteen of MAP20.DAT's records carry it, table
   indices 54 to 69, and they are the sixteen the wave-1 ambush above leaves
   behind. */
#define CH21_WAVE_2 2

/* How the sixteen are placed, XOR EAX,EAX / PUSH EAX at 00038439: zero, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival settles on
   the nearest free walkable tile to its placement record's coordinates rather
   than on those coordinates themselves. */
#define CH21_WAVE_2_PLACE_EXACT 0

/* The entry of the chapter's own text block the second ambush line is spoken
   from, PUSH 0x15 at 0003845f -- one past the 0x14 the wave-1 handler above
   asks for out of the same FDETXT21.TXT block -- and where it is drawn: PUSH
   0xa0000 at 0003845a, the top-left corner of the visible mode-13h page.
   0xa0000 stays a literal because it is an address inside the display adapter's
   aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define CH21_WAVE_2_TEXT_ID 0x15
#define CH21_WAVE_2_TEXT_DEST 0x000a0000

/* The mask the merge keeps and the behaviour code it ORs in.  MOV DL,byte ptr
   [EAX+0x34] / AND DL,0xf0 at 000384c3 keeps the two AI flag bits 0x40 and 0x80
   that the scorers read out of the high nibble, and the value merged in is the
   0x0 parked at [EBP-0x1c] at 00038484 and copied on to [EBP-0x18], which is
   the byte the OR reads.  Mode 0 is the default chain that paths a unit toward
   the nearest opposing unit; mode 2, which is what MAP20.DAT deploys the
   chapter's enemies in, holds position until the unit's threat weights cross a
   threshold.  src/chevt1.c, src/chevt3.c and src/chevt6.c spell the same two
   out for the same field; they stay file-local at every end because no header
   owns them. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* The first and the last unit index the advance runs over, the 0xb parked at
   [EBP-0x24] at 00038476 and the 0x50 parked at [EBP-0x20] at 0003847d.  Both
   are exact for chapter 21's map and neither is read from a count: 0xb is the
   eleven party members MAP20.DAT's header seeds the unit array with, and 0x50
   is the last index the array ever reaches once wave 0's 38 units, wave 1's 16
   and this handler's own 16 have all been appended. */
#define CH21_ADVANCE_FIRST_UNIT_INDEX 0x0b
#define CH21_ADVANCE_LAST_UNIT_INDEX 0x50

/* 00038400.  Chapter 21's second ambush: the first unit of the player's own
   side to finish a step onto the map's second trigger tile brings on the
   sixteen enemies tagged wave 2, the chapter's line about them is spoken, and
   then every unit on the map from index 0xb up is put back on the default
   advance so the whole garrison starts moving at once.

   The frame is the family's four-push one -- PUSH EBX / PUSH ESI / PUSH EDI /
   PUSH EBP / MOV EBP,ESP at 00038400..00038404 -- over a 0x24-byte local area,
   which is larger than the wave-1 handler's 4 because the inline expansion at
   the end brings its own argument and parameter slots with it.  Every
   caller-clean in the body is this function's own (ADD ESP,0x4 after each
   record lookup, ADD ESP,0xc after the deployment and ADD ESP,0x1c after the
   draw), the RET at 000384dc carries no immediate, and the dispatcher pushes
   one dword and drops it with ADD ESP,0x4 at 0001583f, so the convention is the
   stack one at both ends of the call.

   THE RECORD IS FETCHED BEFORE EITHER GATE, exactly as in the wave-1 handler:
   CALL 0x0002d210 at 00038410 is unconditional and its result is stored to
   [EBP-0x4] at 00038418; only then is CMP byte ptr [0x000640e9],0x0 / JNZ made
   at 0003841b.  So a firing on a latch that is already up still costs one call
   into the unit array.

   THE LATCH IS NOT THE ONE THE WAVE-1 HANDLER USES.  This one is 0x000640e9,
   element 0x11 of the array, where the handler above latches 0x000640e8.  Both
   ambushes are named by the same map's tile-event table, so copying the
   neighbour's slot here would make the first unit to trip either tile disarm
   both.

   THE SIDE TEST IS AN EQUALITY ON 2 AND NOT THE FAMILY'S "NOT 0".  MOV AL,byte
   ptr [EAX+0x6] / AND EAX,0xff / CMP EAX,0x2 / JZ at 00038424..00038432 is an
   unsigned compare of the whole byte against one value, so side 1, the guest
   side, is refused here as it is at the wave-1 tile.

   THE ADVANCE IS AN INLINE EXPANSION AND NOT A CALL.  The tail is
   fdps_object_set_field34_low_nibble_range (00036b60) with the constant
   argument triple (0xb, 0x50, 0), and it carries that helper's whole
   fingerprint: the three constants are parked at [EBP-0x24], [EBP-0x20] and
   [EBP-0x1c] (00038476..0003848a), copied into a second set of slots at
   [EBP-0x10], [EBP-0x14] and [EBP-0x18] (0003848b..0003849c), and only then is
   the counter at [EBP-0xc] seeded from the first of them at 0003849d.  There is
   no CALL to that helper anywhere in the body -- the only CALLs are
   fdps_get_unit_record, fdps_deploy_wave, fdps_draw_text and then
   fdps_get_unit_record once per iteration -- so writing the range as a call to
   it would put a CALL in the rebuild that the original does not make.

   THE RANGE IS INCLUSIVE AT BOTH ENDS.  CMP EAX,dword ptr [EBP-0x14] / JLE at
   000384a6 is a signed compare and the jump is taken to the body, so unit index
   0x50 is the last index WRITTEN and not one past the end; the idiomatic
   half-open loop leaves the map's last unit holding position.  Nothing bounds
   the two ends against data_fdps_map_unit_count: they are literals in the
   instruction stream and the array is re-resolved through fdps_get_unit_record
   on every iteration, so the base is re-read per record.

   THE MERGE IS A READ-MODIFY-WRITE AND NOT AN ASSIGNMENT: MOV DL,byte ptr
   [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP-0x18] / OR DH,DL / MOV byte
   ptr [EAX+0x34],DH at 000384c3..000384d1.  Storing the mode whole would clear
   the two AI flag bits the scorers read out of the high nibble.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at the call site (PUSH dword ptr
   [0x00069cf4] at 0003843e) and not out of anything this handler holds, so it
   is whichever chapter is loaded.

   Two values are used after a CALL and both are fdps_get_unit_record's: the
   first stored to [EBP-0x4] at 00038418 and reloaded at 00038424 for the one
   byte the gate reads, the second stored to [EBP-0x8] at 000384bd and reloaded
   twice at 000384c0 and 000384c9 for the read and the write of the same byte.
   fdps_deploy_wave leaves nothing this body reads, and fdps_draw_text's cursor
   is discarded -- the next instruction is the latch store.  Nothing sets EAX
   before the RET and no dispatcher reads what comes back, so the result is
   void. */
void fdps_chapter_21_event_deploy_wave_2(int unit_index)
{
    struct fdps_unit_record *triggering_unit;
    struct fdps_unit_record *advancing_unit;
    int advancing_unit_index;

    triggering_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT] == 0
            && triggering_unit->side == PLAYER_SIDE) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH21_WAVE_2,
                         CH21_WAVE_2_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH21_WAVE_2_TEXT_ID,
                       (unsigned char *) CH21_WAVE_2_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_SECOND_ONE_SHOT_SLOT] = 1;

        for (advancing_unit_index = CH21_ADVANCE_FIRST_UNIT_INDEX;
             advancing_unit_index <= CH21_ADVANCE_LAST_UNIT_INDEX;
             advancing_unit_index++) {
            advancing_unit = fdps_get_unit_record(advancing_unit_index);
            advancing_unit->ai_behavior = (unsigned char)
                ((advancing_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* The five turn numbers the ladder at 000384ec..000387d5 tests
   data_fdps_battle_turn_counter against, in the order it tests them: CMP
   dword ptr [0x00069ce8],0x1 / 0x3 / 0x5 / 0x8 / 0x9, each with a JNZ on to
   the next.  Every one of the five is an equality, so a turn the ladder does
   not name -- and every turn past 9 -- falls out of the whole body doing
   nothing.  MAP21.DAT's turn-event table names this one handler slot five
   times, once per turn in this list, and the counter it is read off starts a
   battle at 1. */
#define CH22_WAVE_1_TURN 1
#define CH22_WAVE_2_TURN 3
#define CH22_WAVE_3_AND_5_TURN 5
#define CH22_WAVE_4_TURN 8
#define CH22_BEHAVIOR_SWITCH_TURN 9

/* The waves each of those turns brings on, PUSH 0x1 / 0x2 / 0x3 / 0x5 / 0x4 at
   0003851b, 0003855f, 000385a7, 00038648 and 0003871e: matched by
   fdps_deploy_wave against byte 0x15 of each 0x1a-byte deployment record of the
   resident MAP%02d.DAT block.

   THE TURN-TO-WAVE MAPPING IS NOT THE IDENTITY.  Turn 5 deploys wave 3 and then
   wave 5, and turn 8 deploys wave 4, so putting the waves on in numeric order
   lands wave 4's twenty-two units on the map three turns early and holds wave
   5's four back to turn 8. */
#define CH22_WAVE_1 1
#define CH22_WAVE_2 2
#define CH22_WAVE_3 3
#define CH22_WAVE_4 4
#define CH22_WAVE_5 5

/* How every one of the five waves is placed, XOR EAX,EAX / PUSH EAX before each
   deployment call: zero, so fdps_deploy_wave passes 0 on to fdps_deploy_unit
   and each arrival settles on the nearest free walkable tile to its placement
   record's coordinates rather than on those coordinates themselves. */
#define CH22_PLACE_ON_NEAREST_FREE_TILE 0

/* The entries of the chapter's own text block the six lines are spoken from,
   PUSH 0x0c / 0x0d / 0x0e / 0x0f / 0x10 / 0x11 at 00038508, 0003854c, 00038594,
   00038635, 000386d6 and 0003870b, out of the FDETXT22.TXT block
   data_fdps_current_chapter_text_ptr holds.  Turn 5 speaks three of them and
   every other firing turn speaks one; turn 9 speaks none. */
#define CH22_WAVE_1_TEXT_ID 0x0c
#define CH22_WAVE_2_TEXT_ID 0x0d
#define CH22_WAVE_3_TEXT_ID 0x0e
#define CH22_WAVE_5_TEXT_ID 0x0f
#define CH22_AFTER_WAVE_5_TEXT_ID 0x10
#define CH22_WAVE_4_TEXT_ID 0x11

/* Where the six lines are drawn, PUSH 0xa0000 before each draw: the top-left
   corner of the visible mode-13h page.  It stays a literal because it is an
   address inside the display adapter's aperture rather than the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH22_TEXT_DEST 0x000a0000

/* What data_fdps_map_cursor_draw_mode is parked at while the view is panned and
   what it is put back to afterwards, MOV dword ptr [0x00069cd0],0x0 and ,0x1 at
   000385b7 / 00038618 / 00038658 / 000386b9 and 0003872e / 000387bf.  Zero is
   the mode fdps_draw_map_cursor draws nothing in, so the cursor is off the
   screen for the whole pan; 1 is the ordinary battle cursor that
   fdps_chapter_state_reset leaves a chapter running in. */
#define CH22_MAP_CURSOR_BLANK 0
#define CH22_MAP_CURSOR_NORMAL 1

/* The map pixel positions the view is panned to, PUSH 0x0 / PUSH 0x60 at
   000385c3, PUSH 0x1b0 / PUSH 0x60 at 000385ed and PUSH 0x1b0 / PUSH 0x1f8 at
   00038794, and they are pixels rather than tiles: fdps_map_cursor_move_to
   walks the view at 0x18 pixels per tile, so 0x1b0 is tile column 18 and 0x1f8
   is tile row 21.  Turn 5 pans to the first two, twice over, and turn 8 pans to
   all three. */
#define CH22_PAN_LEFT_X 0
#define CH22_PAN_RIGHT_X 0x1b0
#define CH22_PAN_NORTH_Y 0x60
#define CH22_PAN_SOUTH_Y 0x1f8

/* How long the view is held at each of those positions, CMP dword ptr
   [EBP+0x14],0xc / JL at every one of the seven hold loops: twelve calls into
   fdps_render_view_frame, each of which spins until the timer tick moves, so
   the count IS the dwell and not an instruction budget
   (rebuild_info/pitfalls.md, contract D).  Shortening it shortens the pause the
   player gets to read the map by exactly that many ticks. */
#define CH22_PAN_HOLD_FRAMES 0xc

/* The behaviour mode the boss is moved to on turn 9, the 0xb parked at
   [EBP-0x18] at 000387e9 and merged in at 00038831: the branch
   fdps_map_actor_behavior_step takes at 00010652.  The mask the merge keeps and
   the mode every reinforcement is handed back to are the
   AI_BEHAVIOR_FLAG_NIBBLE and AI_BEHAVIOR_MODE_ADVANCE the chapter 21 handler
   above already spells out. */
#define CH22_BOSS_BEHAVIOR_MODE 0x0b

/* The two inclusive index ranges turn 9 walks: the 0xb parked at [EBP-0x20] and
   [EBP-0x1c] at 000387db and 000387e2, then the 0xc and 0x41 parked at
   [EBP-0x24] and [EBP-0x28] at 0003883b and 00038842.  The first range's ends
   are the same index, so it is one record: MAP21.COD places 66 units and
   MAP21.DAT holds 55 deployment records, so 0..0x0a are the roster and the
   map's single wave-0 record -- the level-20 boss 巫湯婆婆 -- lands at 0x0b.
   0x0c..0x41 is then all 54 units the five waves have put on the map by the
   time turn 9 comes round.  Both are literals in the instruction stream and
   neither is bounded against data_fdps_map_unit_count. */
#define CH22_BOSS_FIRST_UNIT_INDEX 0x0b
#define CH22_BOSS_LAST_UNIT_INDEX 0x0b
#define CH22_REINFORCEMENT_FIRST_UNIT_INDEX 0x0c
#define CH22_REINFORCEMENT_LAST_UNIT_INDEX 0x41

/* 000384e0.  Chapter 22's turn-scheduled event handler: the one slot MAP21.DAT
   names for all five of the chapter's turn events, running whichever of them is
   due for the turn the player has just finished.

   The frame is the family's four-push one -- PUSH EBX / PUSH ESI / PUSH EDI /
   PUSH EBP / MOV EBP,ESP / SUB ESP,0x40 at 000384e0..000384e6 -- so the one
   incoming dword sits at [EBP+0x14].  Every caller-clean in the body is this
   function's own (ADD ESP,0x1c after each draw, ADD ESP,0xc after each
   deployment, ADD ESP,0x8 after each pan and ADD ESP,0x4 after each record
   lookup), the RET at 000388a1 carries no immediate, and the dispatcher pushes
   one dword and drops it with ADD ESP,0x4 at 0002e146, so the convention is the
   stack one at both ends of the call.

   THE LADDER HAS NO DEFAULT BRANCH.  Five equality tests on
   data_fdps_battle_turn_counter, each falling through a JNZ into the next, and
   the last JNZ at 000387d5 goes straight to the epilogue at 0003889b.  So the
   handler is called on every scheduled turn and simply does nothing on a turn
   it does not name -- there is nothing here to fold into a table or a switch
   default.

   THE INCOMING ARGUMENT IS NEVER READ.  Nothing loads [EBP+0x14] before the
   turn-5 branch stores 0 over it at 000385cd, and the whole 0x40 of locals is
   accounted for by the two inline expansions at the end (eight dword slots
   each), so the pan loops have no counter of their own: they really do count in
   the caller's argument slot.  fdps_battle_run_turn_events, the only dispatcher
   that reaches this table slot, pushes a literal 0 at 0002e13e, so no value is
   lost by that.

   THE TURN-TO-WAVE MAPPING IS NOT THE IDENTITY -- see CH22_WAVE_1 above.  Turn
   5 puts on wave 3 and then wave 5, and turn 8 puts on wave 4.

   THE TURN-9 RANGES ARE INLINE EXPANSIONS AND NOT CALLS.  Both carry
   fdps_object_set_field34_low_nibble_range's whole fingerprint: three constants
   parked in one set of slots, copied into a second set, and only then the
   counter seeded from the first of them (000387db..00038802 and
   0003883b..00038865).  There is no CALL to that helper anywhere in the body --
   the only CALLs are the five callees this file's other handlers already use --
   so writing either range as a call to it would put a CALL in the rebuild that
   the original does not make.

   BOTH RANGES ARE INCLUSIVE AT BOTH ENDS.  CMP EAX,dword ptr [EBP-0x10] / JLE
   at 0003880e and CMP EAX,dword ptr [EBP-0x34] / JLE at 0003886e are signed
   compares jumping into the body, so 0x0b and 0x41 are the last indices
   WRITTEN.  The first range's two ends are the same index, so its loop body
   runs exactly once, on the boss.

   THE MERGE IS A READ-MODIFY-WRITE AND NOT AN ASSIGNMENT: MOV DL,byte ptr
   [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP-0x14] / OR DH,DL / MOV byte
   ptr [EAX+0x34],DH at 00038828..00038836 and the same shape at
   00038888..00038896.  Storing the mode whole would clear the two AI flag bits
   the scorers read out of the high nibble.

   THE CURSOR IS BLANKED PER PAN PAIR AND RESTORED PER PAIR.  Turn 5 blanks it
   at 000385b7, restores it at 00038618, blanks it again at 00038658 and
   restores it at 000386b9, so the line spoken between the two pan pairs is
   spoken with the ordinary cursor on the map; turn 8 blanks it once at 0003872e
   and restores it once at 000387bf across all three of its pans.  Both branches
   leave it on 1.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at each call site (PUSH dword ptr
   [0x00069cf4]) and not out of anything this handler holds, so it is whichever
   chapter is loaded -- 21 for this one, which is the map21 the handler's table
   slot is only ever named from.

   Only one call's value is used afterwards, and it is fdps_get_unit_record's:
   stored to [EBP-0x4] at 00038822 and reloaded at 00038825 and 0003882e for the
   read and the write of the one byte, and the same shape through [EBP-0x40] at
   00038882.  Each of the six fdps_draw_text cursors is discarded -- the next
   instruction is a PUSH or a store -- fdps_deploy_wave leaves nothing this body
   reads, and fdps_map_cursor_move_to and fdps_render_view_frame return nothing.
   Nothing sets EAX before the RET and no dispatcher reads what comes back, so
   the result is void. */
void fdps_chapter_22_event_for_turn(int event_arg)
{
    struct fdps_unit_record *boss_unit;
    struct fdps_unit_record *reinforcement_unit;
    int boss_unit_index;
    int reinforcement_unit_index;

    if (data_fdps_battle_turn_counter == CH22_WAVE_1_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_WAVE_1_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH22_WAVE_1,
                         CH22_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH22_WAVE_2_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_WAVE_2_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH22_WAVE_2,
                         CH22_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH22_WAVE_3_AND_5_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_WAVE_3_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH22_WAVE_3,
                         CH22_PLACE_ON_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_BLANK;
        fdps_map_cursor_move_to(CH22_PAN_LEFT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        fdps_map_cursor_move_to(CH22_PAN_RIGHT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_WAVE_5_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH22_WAVE_5,
                         CH22_PLACE_ON_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_BLANK;
        fdps_map_cursor_move_to(CH22_PAN_LEFT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        fdps_map_cursor_move_to(CH22_PAN_RIGHT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_AFTER_WAVE_5_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH22_WAVE_4_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_WAVE_4_TEXT_ID,
                       (unsigned char *) CH22_TEXT_DEST, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH22_WAVE_4,
                         CH22_PLACE_ON_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_BLANK;
        fdps_map_cursor_move_to(CH22_PAN_LEFT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        fdps_map_cursor_move_to(CH22_PAN_RIGHT_X, CH22_PAN_NORTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        fdps_map_cursor_move_to(CH22_PAN_RIGHT_X, CH22_PAN_SOUTH_Y);
        for (event_arg = 0; event_arg < CH22_PAN_HOLD_FRAMES; event_arg++) {
            fdps_render_view_frame();
        }
        data_fdps_map_cursor_draw_mode = CH22_MAP_CURSOR_NORMAL;
    } else if (data_fdps_battle_turn_counter == CH22_BEHAVIOR_SWITCH_TURN) {
        for (boss_unit_index = CH22_BOSS_FIRST_UNIT_INDEX;
             boss_unit_index <= CH22_BOSS_LAST_UNIT_INDEX;
             boss_unit_index++) {
            boss_unit = fdps_get_unit_record(boss_unit_index);
            boss_unit->ai_behavior = (unsigned char)
                ((boss_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 CH22_BOSS_BEHAVIOR_MODE);
        }

        for (reinforcement_unit_index = CH22_REINFORCEMENT_FIRST_UNIT_INDEX;
             reinforcement_unit_index <= CH22_REINFORCEMENT_LAST_UNIT_INDEX;
             reinforcement_unit_index++) {
            reinforcement_unit =
                fdps_get_unit_record(reinforcement_unit_index);
            reinforcement_unit->ai_behavior = (unsigned char)
                ((reinforcement_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* Who the reward is for, MOV AL,byte ptr [EAX+0x8] / AND EAX,0xff / CMP EAX,0x1
   / JNZ at 000388ce..000388d9.  The byte read is struct fdps_unit_record's
   char_id at record offset 8 -- the id fdps_roster_add_character stamps in --
   and character id 1 is 法蓮娜 (assets/characters.md), so the gate follows her
   whatever index the deployment gave her rather than naming a position. */
#define CH22_CONTRACT_CHAR_ID 1

/* The bag count that refuses the award, CMP EAX,0x8 / JNZ at 000388e7.
   fdps_unit_item_count answers 8 when none of the unit's eight inventory
   entries carries the empty bit 0x80 (unititem.h), so 8 is "no room".  The test
   is an inequality against that one value and not a "< 8", which is the same
   answer only because 8 is the count's maximum. */
#define CH22_BAG_FULL_COUNT 8

/* The dead boss's own unit index, PUSH 0xb at 000388ee: 巫湯婆婆, the map's
   single wave-0 record.  MAP21.COD places 66 units against MAP21.DAT's 55
   deployment records, so unit indices 0..0x0a are the player's roster and the
   boss is appended behind it at 0x0b -- the same index the turn-9 branch above
   walks as the first of its two ranges. */
#define CH22_BOSS_UNIT_INDEX 0x0b

/* The entry of the chapter's own text block the dying boss speaks, PUSH 0x12 at
   00038915, out of the FDETXT22.TXT block data_fdps_current_chapter_text_ptr
   holds: the entry after the six the turn handler above speaks.  It is drawn at
   the top-left corner of the visible mode-13h page, PUSH 0xa0000 at 00038910,
   which stays a literal because it is an address inside the display adapter's
   aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define CH22_BOSS_DEFEAT_TEXT_ID 0x12
#define CH22_BOSS_DEFEAT_TEXT_DEST 0x000a0000

/* What she is handed, PUSH 0xba at 00038925: 死神契約 (assets/items.md).  It is
   carried and never used -- chapter 23's 死神 gives up 反禁制器 for a killing
   blow struck by a 法蓮娜 who has this in her bag, and 反禁制器 is what opens
   the hidden chapter (docs/guide/fdps/walkthrough.txt). */
#define CH22_REAPER_CONTRACT_ITEM_ID 0xba

/* 000388b0.  Chapter 22's boss-death event: the chapter is won, and if 法蓮娜
   struck the killing blow with room in her bag the dying 巫湯婆婆 speaks her
   line and 死神契約 changes hands.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   000388b0..000388b6 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x4 after each of
   the two record lookups and after the bag count, ADD ESP,0x1c after the draw
   and ADD ESP,0x8 after the award), the RET at 00038946 carries no immediate,
   and the dispatcher pushes one dword and drops it with ADD ESP,0x4 at
   0001dcb4, so the convention is the stack one at both ends of the call.

   THE VICTORY IS UNCONDITIONAL AND THE GIFT IS NOT.  Both gates jump to
   00038936, which is the store of the end code, so a firing that hands nothing
   over still clears the chapter.  This store is chapter 22's whole victory
   condition: fdps_chapter_22_post_action only ever tests for defeat, so moving
   the store inside the branch leaves a won chapter running with its boss dead.

   THE SILENT SKIP ON A FULL BAG IS THE ORIGINAL BEHAVIOUR.  With all eight
   entries occupied the item is simply not given and the player is told nothing
   -- and that is what shuts the hidden chapter, because chapter 23's reward is
   only handed to a 法蓮娜 carrying this one.  A swap prompt, a forced grant or
   a drop on the floor in its place all change what the run can reach.

   THE STORE INTO THE BOSS'S RECORD IS LOAD-BEARING AND MUST STAY AHEAD OF THE
   LINE.  MOV byte ptr [EAX+0x5],0x0 at 000388fe clears the WHOLE flags byte of
   unit 0x0b, and it looks like a pointless poke at a unit that is already dead:
   the death sequence has just set bit 0, the removed bit fdps_unit_is_retired
   reads.  Text entry 0x12 opens with the portrait token, which raises its
   speaker through fdps_battle_find_unit_by_character_id -- and that search
   skips retired units.  So clearing the byte is what gives the line a face, and
   narrowing the store to bit 0, or moving it after the draw, silently renders
   the dying line with no speaker.

   Two values are used after a CALL and both are fdps_get_unit_record's: the
   acting unit's record, stored to [EBP-0x4] at 000388c8 and reloaded at
   000388cb for the char_id byte, and the boss's, stored to the same slot at
   000388f8 and reloaded at 000388fb for the flags store.  fdps_unit_item_count's
   EAX is the bag count and is compared where it lands, at 000388e7;
   fdps_draw_text's cursor is discarded -- the next instruction is a PUSH -- and
   fdps_unit_add_item's 1-or-(-1) is discarded likewise, the bag count having
   already answered the question it reports.  Nothing sets EAX before the RET
   and the dispatcher reads nothing back, so the result is void.

   Table slot 32, reached only through the table: the boss's deployment record
   in MAP21.DAT carries the death script (opcode 2, operand 32),
   fdps_collect_defeated_unit_events collects it once her HP reaches 0, and the
   death-script runner calls the slot with the index of the unit that was
   acting. */
void fdps_chapter_22_event_boss_defeat(int unit_index)
{
    /* The unit that struck the killing blow, and then the boss it killed -- two
       names here for the one slot [EBP-0x4] the original keeps them in. */
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *boss_unit;

    acting_unit = fdps_get_unit_record(unit_index);

    if (acting_unit->char_id == CH22_CONTRACT_CHAR_ID
            && fdps_unit_item_count(unit_index) != CH22_BAG_FULL_COUNT) {
        boss_unit = fdps_get_unit_record(CH22_BOSS_UNIT_INDEX);
        /* The whole byte, and before the line is spoken. */
        boss_unit->flags = 0;
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH22_BOSS_DEFEAT_TEXT_ID,
                       (unsigned char *) CH22_BOSS_DEFEAT_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_unit_add_item(unit_index, CH22_REAPER_CONTRACT_ITEM_ID);
    }

    /* 2 is the chapter cleared (gamedata.h), written on the skipped path too. */
    data_fdps_chapter_event_or_battle_end_code = 2;
}

/* Who the reward is for, MOV AL,byte ptr [EAX+0x8] / AND EAX,0xff / CMP EAX,0x1
   / JNZ at 000389b9..000389c4.  The byte read is struct fdps_unit_record's
   char_id at record offset 8 and character id 1 is 法蓮娜
   (assets/characters.md), so the gate follows her whatever index the deployment
   gave her.  The read is zero-extended before the compare, so the id is an
   unsigned byte and no value of it can be mistaken for a negative. */
#define CH23_REWARD_CHAR_ID 1

/* What she has to be carrying, PUSH 0xba at 000389a2: 死神契約
   (assets/items.md), which chapter 22's boss hands her for the same kind of
   killing blow.  fdps_unit_find_item_slot answers -1 when the item is not in
   the bag (unititem.h), and that is the second gate, CMP dword ptr
   [EBP-0x4],-0x1 / JNZ at 000389c6. */
#define CH23_REAPER_CONTRACT_ITEM_ID 0xba
#define CH23_CONTRACT_NOT_CARRIED (-1)

/* What she gets in exchange, PUSH 0xdc at 00038a01: 反禁制器
   (assets/items.md), the item the game wants carried before it will open its
   hidden chapters (docs/guide/fdps/walkthrough.txt). */
#define CH23_SEAL_BREAKER_ITEM_ID 0xdc

/* The dead boss's own unit index, PUSH 0x20 at 0003895c: 死神.  MAP22.DAT holds
   eighty 0x1a-byte deployment records behind a 131-byte header and record 52 --
   enemy id 0x48 at level 23 -- is the only one of the eighty whose death script
   at record +0x16 is opcode 2 with operand 33, this handler's table slot.  The
   same index is what fdps_chapter_23_event_deploy_wave_for_turn drives into
   behaviour mode 0x0b on turn 15, MOV dword ptr [EBP-0x20],0x20 at 00038af4. */
#define CH23_BOSS_UNIT_INDEX 0x20

/* The two entries of the chapter's own text block this handler speaks, PUSH
   0x1a at 00038983 and PUSH 0x1b at 000389e1, out of the FDETXT23.TXT block
   data_fdps_current_chapter_text_ptr holds: the last two of that file's
   twenty-eight.  0x1a opens with the portrait token -0x11 followed by character
   id 0x48, the boss's own; 0x1b raises 0x48, 4 (亞克) and 1 (法蓮娜) in turn.
   Both are drawn at the top-left corner of the visible mode-13h page, PUSH
   0xa0000 at 0003897e and 000389dc, which stays a literal because it is an
   address inside the display adapter's aperture rather than the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH23_BOSS_DEFEAT_TEXT_ID 0x1a
#define CH23_EXCHANGE_TEXT_ID 0x1b
#define CH23_BOSS_DEFEAT_TEXT_DEST 0x000a0000

/* 00038950.  Chapter 23's boss-death event: the chapter is won and the dying
   死神 speaks its line, and if 法蓮娜 struck the killing blow carrying
   死神契約 the contract is taken off her and 反禁制器 put in its place.

   The frame is the family's four-push one with two 4-byte locals -- PUSH EBX /
   PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x8 at
   00038950..00038956 -- so the one incoming dword sits at [EBP+0x14].  Every
   caller-clean in the body is this function's own (ADD ESP,0x4 after each
   record lookup, ADD ESP,0x8 after the slot search and after each of the two
   inventory calls, ADD ESP,0x1c after each draw), the RET at 00038a22 carries
   no immediate, and the death-script dispatcher pushes one dword and drops it
   with ADD ESP,0x4 at 0001dcb4, so the convention is the stack one at both ends
   of the call.

   THE LINE AND THE VICTORY ARE UNCONDITIONAL AND THE EXCHANGE IS NOT.  Unlike
   chapter 22's handler, which puts its draw inside the branch, this one clears
   the boss's flags byte and speaks entry 0x1a before either gate is tested, and
   both gates jump to 00038a12, the store of the end code.  So every firing
   plays the dying line and clears the chapter; only the second draw, the
   removal and the award sit behind the gates.  The store is chapter 23's whole
   victory condition -- killing the 死神 is what wins the map.

   THE STORE INTO THE BOSS'S RECORD IS LOAD-BEARING AND MUST STAY AHEAD OF THE
   LINE.  MOV byte ptr [EAX+0x5],0x0 at 0003896c clears the WHOLE flags byte of
   unit 0x20, and it looks like a pointless poke at a unit that is already dead:
   the death sequence has just set bit 0, the removed bit fdps_unit_is_retired
   reads.  Text entry 0x1a opens with the portrait token, which raises its
   speaker through fdps_battle_find_unit_by_character_id -- and that search
   skips retired units.  So clearing the byte is what gives the dying line a
   face, and narrowing the store to bit 0, or moving it after the draw, silently
   renders the line with no speaker.

   THE SEARCH RUNS WHETHER OR NOT THE CHARACTER MATCHES.  CALL 0x00034520 at
   000389ab is reached unconditionally and its result is parked in [EBP-0x4]
   before either compare; only then does the char_id test at 000389c4 run.
   Writing the two gates as a short-circuit that calls the search second would
   skip a call the original always makes.

   THE EXCHANGE IS A REMOVE THEN AN ADD, NOT AN IN-PLACE SWAP.
   fdps_unit_remove_item packs the entries above the vacated one down and empties
   the last, and fdps_unit_add_item then takes the first empty entry, so with a
   full bag 反禁制器 lands back in the contract's old slot and with a gappy one
   it lands in the first hole instead.  Storing 0xdc over the slot the search
   returned would give the player a different slot order.

   Three values are used after a CALL.  fdps_get_unit_record's record pointer is
   stored to [EBP-0x8] at 00038966 and reloaded at 00038969 for the flags store,
   then the second lookup overwrites the same slot at 0003899f and is reloaded
   at 000389b6 for the char_id byte.  fdps_unit_find_item_slot's EAX is the slot
   and is stored to [EBP-0x4] at 000389b3.  fdps_draw_text's cursor is discarded
   both times -- the next instruction is an ADD ESP -- and fdps_unit_add_item's
   1-or-(-1) is discarded likewise; nothing sets EAX after it before the RET and
   the dispatcher reads nothing back, so the result is void.

   Table slot 33 at 00060248, reached only through the table: the boss's
   deployment record in MAP22.DAT carries the death script (opcode 2, operand
   33), fdps_collect_defeated_unit_events collects it once its HP reaches 0, and
   the death-script runner calls the slot with the index of the unit that was
   acting. */
void fdps_chapter_23_event_boss_defeat(int unit_index)
{
    /* The boss whose death fired the event, and then the unit that killed it --
       two names here for the one slot [EBP-0x8] the original keeps them in. */
    struct fdps_unit_record *boss_unit;
    struct fdps_unit_record *acting_unit;
    /* Where 死神契約 sits in the killer's bag, or -1. */
    int contract_slot;

    boss_unit = fdps_get_unit_record(CH23_BOSS_UNIT_INDEX);
    /* The whole byte, and before the line is spoken. */
    boss_unit->flags = 0;
    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH23_BOSS_DEFEAT_TEXT_ID,
                   (unsigned char *) CH23_BOSS_DEFEAT_TEXT_DEST,
                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                   MESSAGE_OUTLINE_COLOR);

    acting_unit = fdps_get_unit_record(unit_index);
    /* Searched before either gate is tested, as the original searches it. */
    contract_slot = fdps_unit_find_item_slot(unit_index,
                                            CH23_REAPER_CONTRACT_ITEM_ID);

    if (acting_unit->char_id == CH23_REWARD_CHAR_ID
            && contract_slot != CH23_CONTRACT_NOT_CARRIED) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_EXCHANGE_TEXT_ID,
                       (unsigned char *) CH23_BOSS_DEFEAT_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_unit_remove_item(unit_index, contract_slot);
        fdps_unit_add_item(unit_index, CH23_SEAL_BREAKER_ITEM_ID);
    }

    /* 2 is the chapter cleared (gamedata.h), written on the skipped path too. */
    data_fdps_chapter_event_or_battle_end_code = 2;
}

/* The turn the first test closes at, CMP dword ptr [0x00069ce8],0x5 / JG at
   00038a43: the compare is signed and the jump leaves the body, so turn 5 still
   brings a wave on and turn 6 is the first that does not.  The counter starts a
   battle at 1, which is what fdps_chapter_state_reset writes. */
#define CH23_LAST_ARRIVAL_TURN 5

/* What that test turns the turn number into, ADD EAX,0x4 at 00038a54 on the
   counter it has just loaded.  So turns 1 to 5 ask for waves 5, 6, 7, 8 and 9
   in that order and the mapping is an offset rather than the identity: asking
   for wave N on turn N would put MAP22.DAT's waves 1 to 5 on instead, which are
   not the records these turns bring. */
#define CH23_ARRIVAL_WAVE_BIAS 4

/* The two turns the chain below names for a wave of their own, CMP dword ptr
   [0x00069ce8],0x3 / JNZ at 00038a66 and CMP ...,0x7 / JNZ at 00038aaa, and the
   turn it names for the behaviour switch, CMP ...,0xf / JNZ at 00038aeb.  All
   three are equalities and the last JNZ goes straight to the epilogue, so the
   chain has no default branch. */
#define CH23_WAVE_3_TURN 3
#define CH23_WAVE_4_TURN 7
#define CH23_BEHAVIOR_SWITCH_TURN 0x0f

/* The waves the chain's own two branches ask for, PUSH 0x3 at 00038a95 and PUSH
   0x4 at 00038ad9: matched by fdps_deploy_wave against byte 0x15 of each
   0x1a-byte deployment record of the resident MAP%02d.DAT block. */
#define CH23_WAVE_3 3
#define CH23_WAVE_4 4

/* How every wave here is placed, XOR EAX,EAX / PUSH EAX before each of the
   three deployment calls: zero, so fdps_deploy_wave passes 0 on to
   fdps_deploy_unit and each arrival settles on the nearest free walkable tile
   to its placement record's coordinates rather than on those coordinates
   themselves. */
#define CH23_TURN_PLACE_ON_NEAREST_FREE_TILE 0

/* The entries of the chapter's own text block the two lines are spoken from,
   PUSH 0x15 at 00038a82 and PUSH 0x16 at 00038ac6, out of the FDETXT23.TXT
   block data_fdps_current_chapter_text_ptr holds.  They are drawn at the
   top-left corner of the visible mode-13h page, PUSH 0xa0000 at 00038a7d and
   00038ac1, which stays a literal because it is an address inside the display
   adapter's aperture rather than the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define CH23_WAVE_3_TEXT_ID 0x15
#define CH23_WAVE_4_TEXT_ID 0x16
#define CH23_TURN_TEXT_DEST 0x000a0000

/* The behaviour mode turn 15 drives the boss into, the 0xb parked at [EBP-0x18]
   at 00038b02 and merged in at 00038b4a: one of the two driven branches
   fdps_map_actor_behavior_step dispatches on the low nibble of the same byte.
   The mask the merge keeps is the AI_BEHAVIOR_FLAG_NIBBLE the chapter 21 and 22
   handlers above already spell out. */
#define CH23_BOSS_BEHAVIOR_MODE 0x0b

/* The inclusive index range turn 15 walks, the 0x20 parked at [EBP-0x20] and
   [EBP-0x1c] at 00038af4 and 00038afb.  Both ends are the same index, so the
   loop body runs exactly once, on unit 0x20 -- the 死神 whose own death script
   is fdps_chapter_23_event_boss_defeat above.  Both are literals in the
   instruction stream and neither is bounded against data_fdps_map_unit_count. */
#define CH23_BOSS_FIRST_UNIT_INDEX 0x20
#define CH23_BOSS_LAST_UNIT_INDEX 0x20

/* 00038a30.  Chapter 23's turn-scheduled event handler: the one slot MAP22.DAT
   names for all seven of the chapter's turn events, running whichever of them
   is due for the turn that has just been finished.

   The frame is the family's four-push one -- PUSH EBX / PUSH ESI / PUSH EDI /
   PUSH EBP / MOV EBP,ESP / SUB ESP,0x20 at 00038a30..00038a36 -- so the one
   incoming dword sits at [EBP+0x14].  Every caller-clean in the body is this
   function's own (ADD ESP,0xc after each deployment, ADD ESP,0x1c after each
   draw and ADD ESP,0x4 after the record lookup), the RET at 00038b5a carries no
   immediate, and the dispatcher pushes one dword and drops it with ADD ESP,0x4
   at 0002e146, so the convention is the stack one at both ends of the call.

   THE TWO TESTS ARE NOT AN IF/ELSE AND TURN 3 DEPLOYS TWICE.  The JG at
   00038a4a on the turn <= 5 test jumps to 00038a66, which is the first compare
   of the chain, and the tail of the taken path falls into the same address; the
   chain is a separate statement and not the else of the first test.  So turn 3
   brings on wave 7 from the first test and then wave 3 from the chain, two
   deployments in that order, and folding the two into one ladder loses wave 3's
   records on the chapter's third turn.

   THE WAVE KEY IS THE TURN PLUS FOUR AND NOT THE TURN.  See
   CH23_ARRIVAL_WAVE_BIAS above.

   THE CHAIN HAS NO DEFAULT BRANCH.  Three equality tests, each falling through
   a JNZ into the next, and the last JNZ at 00038af2 goes to the epilogue.  So a
   turn past 5 that the chain does not name leaves the whole body having done
   nothing.

   THE INCOMING ARGUMENT IS NEVER READ.  MOV dword ptr [EBP+0x14],0x0 at
   00038a3c is the only access to the slot in the whole body and nothing loads
   it afterwards -- unlike the chapter 22 handler, which counts its pan holds
   there, this one just clears it.  fdps_battle_run_turn_events, the only
   dispatcher that reaches this table slot, pushes a literal 0 at 0002e13e, so
   no value is lost by that.

   THE TURN-15 RANGE IS AN INLINE EXPANSION AND NOT A CALL.  It carries
   fdps_object_set_field34_low_nibble_range's whole fingerprint: the three
   constants parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18]
   (00038af4..00038b02), copied into a second set of slots at [EBP-0xc],
   [EBP-0x10] and [EBP-0x14] (00038b09..00038b18), and only then the counter at
   [EBP-0x8] seeded from the first of them at 00038b1b.  There is no CALL to
   that helper anywhere in the body -- the only CALLs are fdps_deploy_wave,
   fdps_draw_text and fdps_get_unit_record -- so writing the range as a call to
   it would put a CALL in the rebuild that the original does not make.

   THE RANGE IS INCLUSIVE AT BOTH ENDS.  CMP EAX,dword ptr [EBP-0x10] / JLE at
   00038b24 is a signed compare and the jump is taken into the body, so unit
   index 0x20 is written and is not one past the end; the idiomatic half-open
   spelling leaves the 死神 on the behaviour MAP22.DAT authored it in.

   THE MERGE IS A READ-MODIFY-WRITE AND NOT AN ASSIGNMENT: MOV DL,byte ptr
   [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP-0x14] / OR DH,DL / MOV byte
   ptr [EAX+0x34],DH at 00038b41..00038b4f.  Storing the mode whole would clear
   the two AI flag bits the scorers read out of the high nibble.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at each call site (PUSH dword ptr
   [0x00069cf4] at 00038a58, 00038a97 and 00038adb) and not out of anything this
   handler holds, so it is whichever chapter is loaded -- 22 for this one, which
   is the map this handler's table slot is only ever named from.

   Only one value is used after a CALL and it is fdps_get_unit_record's: stored
   to [EBP-0x4] at 00038b3b and reloaded at 00038b3e and 00038b47 for the read
   and the write of the one byte.  fdps_deploy_wave leaves nothing this body
   reads, and each fdps_draw_text cursor is discarded -- the next instruction is
   the ADD ESP that cleans its arguments.  Nothing sets EAX before the RET and
   no dispatcher reads what comes back, so the result is void.

   Table slot 34 at 0006024c, reached only through the table: all seven live
   entries of MAP22.DAT's turn-event table route to this slot, on turns 1 to 5,
   7 and 15. */
void fdps_chapter_23_event_deploy_wave_for_turn(int event_arg)
{
    struct fdps_unit_record *boss_unit;
    int boss_unit_index;

    /* The store the original makes over its own argument slot and never reads
       back; nothing here counts in it. */
    event_arg = 0;

    if (data_fdps_battle_turn_counter <= CH23_LAST_ARRIVAL_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         data_fdps_battle_turn_counter
                         + CH23_ARRIVAL_WAVE_BIAS,
                         CH23_TURN_PLACE_ON_NEAREST_FREE_TILE);
    }

    /* A separate statement, not the else of the test above: turn 3 arrives
       here having already deployed wave 7. */
    if (data_fdps_battle_turn_counter == CH23_WAVE_3_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_WAVE_3_TEXT_ID,
                       (unsigned char *) CH23_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH23_WAVE_3,
                         CH23_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH23_WAVE_4_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_WAVE_4_TEXT_ID,
                       (unsigned char *) CH23_TURN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH23_WAVE_4,
                         CH23_TURN_PLACE_ON_NEAREST_FREE_TILE);
    } else if (data_fdps_battle_turn_counter == CH23_BEHAVIOR_SWITCH_TURN) {
        for (boss_unit_index = CH23_BOSS_FIRST_UNIT_INDEX;
             boss_unit_index <= CH23_BOSS_LAST_UNIT_INDEX;
             boss_unit_index++) {
            boss_unit = fdps_get_unit_record(boss_unit_index);
            boss_unit->ai_behavior = (unsigned char)
                ((boss_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 CH23_BOSS_BEHAVIOR_MODE);
        }
    }
}

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) the ring
   event latches: byte ptr [0x000640e8], element 0x10 of the 32-entry array
   based at 0x000640d8, which is the slot the one-shot handlers of every chapter
   share.  It is NOT this handler's own byte and it must not become a static:
   fdps_chapter_state_reset memsets the array at the head of a chapter and the
   save image carries it, which is what makes the event repeatable across a
   restart and a reload. */
#define CH23_RING_LATCH_SLOT 0x10

/* The character id the gate at 00038b8f admits: 7, 琴琴, the party's 武道家 and
   index 07 of FRIAPRDA.DAT (assets/characters.md).  The byte is read as a whole
   unsigned char -- MOV AL,byte ptr [EAX+0x8] / AND EAX,0xff -- and compared for
   equality, so no other member of the party can trip the scene. */
#define CH23_RING_MARTIAL_ARTIST_CHAR_ID 7

/* The bag count that refuses the scene, CMP EAX,0x8 / JNZ at 00038ba2: eight
   is every entry of an eight-entry bag occupied.  The compare is an equality
   and the refusal is the equal side, so anything below eight passes. */
#define CH23_RING_BAG_FULL 8

/* What she is handed, PUSH 0xb1 at 00038ca0: 形見指環, the ring that restores
   HP and MP every turn (assets/items.md). */
#define CH23_RING_ITEM_ID 0xb1

/* The three entries of the chapter's own FDETXT23.TXT block the scene speaks,
   PUSH 0x17 at 00038bbf, PUSH 0x18 at 00038c36 and PUSH 0x19 at 00038c90: the
   opening line, the exchange between the two of them and the closing line.
   They are drawn at the top-left corner of the visible mode-13h page, PUSH
   0xa0000 at 00038bba, 00038c31 and 00038c8b, which stays a literal because it
   is an address inside the display adapter's aperture rather than the address
   of anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH23_RING_OPENING_TEXT_ID 0x17
#define CH23_RING_EXCHANGE_TEXT_ID 0x18
#define CH23_RING_CLOSING_TEXT_ID 0x19
#define CH23_RING_TEXT_DEST 0x000a0000

/* The wave the spirit arrives with, PUSH 0xa at 00038bd2, and how it is placed,
   XOR EAX,EAX / PUSH EAX at 00038bcf: fdps_deploy_wave matches the key against
   byte 0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT
   block, and MAP22.DAT holds exactly one record with key 10 -- record 0,
   character id 13, side 1 -- so exactly one unit is appended.  Zero placement
   settles it on the nearest free walkable tile to its record's coordinates
   rather than on those coordinates themselves. */
#define CH23_RING_SPIRIT_WAVE 10
#define CH23_RING_PLACE_ON_NEAREST_FREE_TILE 0

/* What data_fdps_map_cursor_draw_mode is parked at for the arrival and what it
   is put back to afterwards, MOV dword ptr [0x00069cd0],0x0 at 00038be2 and
   ,0x1 at 00038c19.  Zero is the mode fdps_draw_map_cursor paints nothing in,
   so no cursor sits over the spirit walking on; 1 is the ordinary battle cursor
   fdps_chapter_state_reset leaves a chapter running in. */
#define CH23_RING_MAP_CURSOR_BLANK 0
#define CH23_RING_MAP_CURSOR_NORMAL 1

/* Where the view is walked to for the arrival, PUSH 0x0 / PUSH 0x2a0 at
   00038bec: map pixels, not tiles.  fdps_map_cursor_move_to steps the cursor at
   0x18 pixels per tile and drags the viewport with it, so 0x2a0 is tile column
   28 and the row is the top edge of the map. */
#define CH23_RING_SHRINE_VIEW_X 0x2a0
#define CH23_RING_SHRINE_VIEW_Y 0

/* How long each of the two holds runs, CMP dword ptr [EBP-0x4],0xc / JL at
   00038c02 and 00038c66: twelve calls into fdps_render_view_frame, each of
   which spins until the timer tick moves, so the count IS the dwell and not an
   instruction budget (rebuild_info/pitfalls.md, contract D).  Shortening either
   loop shortens the pause by exactly that many ticks. */
#define CH23_RING_HOLD_FRAMES 0xc

/* What the spirit's flags byte is left holding, MOV byte ptr [EAX+0x5],0x1 at
   00038c5b.  The store is the whole byte and not an OR, and it is the same form
   fdps_unit_mark_retired writes: bit 0 is the bit fdps_unit_is_retired reads,
   and the record is one this handler's own deployment created a moment ago, so
   there is nothing else in the byte to preserve. */
#define CH23_RING_SPIRIT_RETIRED 1

/* 00038b60.  Chapter 23's keepsake-ring event: 琴琴 ends her turn on the shrine
   tile, a spirit walks on, the two of them speak, it leaves, and she is handed
   形見指環.

   The frame is the family's four-push one -- PUSH EBX / PUSH ESI / PUSH EDI /
   PUSH EBP / MOV EBP,ESP / SUB ESP,0x8 at 00038b60..00038b66 -- so the one
   incoming dword sits at [EBP+0x14].  Every caller-clean in the body is this
   function's own (ADD ESP,0x4 after each record lookup and after the bag count,
   ADD ESP,0x1c after each draw, ADD ESP,0xc after the deployment and ADD
   ESP,0x8 after the cursor move), the RET at 00038cbe carries no immediate, and
   the dispatcher pushes one dword and drops it with an ADD ESP,0x4 of its own,
   so the convention is the stack one at both ends of the call.

   THE THREE GATES SHORT-CIRCUIT IN THIS ORDER AND THE BAG COUNT IS LAST.  The
   latch test at 00038b7b jumps out before the record is touched, the character
   test at 00038b8f jumps out before fdps_unit_item_count is called, and only a
   unit that passed both is counted.  Reordering them costs an inventory walk on
   every firing that the original does not make.

   THE BAG TEST GATES THE WHOLE SCENE AND THE LATCH IS RAISED LAST.  A full bag
   leaves the JMP at 00038ba7 straight to the epilogue, so nothing is drawn,
   nothing is deployed and byte [0x000640e8] is still 0 -- the player drops
   something and comes back.  Raising the latch at the top of the body, or
   letting the scene play and only skipping fdps_unit_add_item, spends the event
   and loses the ring for that save.

   THE LATCH IS THE SHARED ARRAY.  See CH23_RING_LATCH_SLOT above.

   BOTH HOLD LOOPS COUNT AND DO NOTHING ELSE.  [EBP-0x4] is seeded at 0, tested
   against 0xc and stepped, and the MOV EAX,dword ptr [EBP-0x4] at 00038c0a and
   00038c6e that precedes each INC is the -od increment shape reading a value
   nothing consumes.  fdps_render_view_frame takes no arguments and returns
   none, so the counter is the dwell and nothing else.

   Three values are used after a CALL.  fdps_get_unit_record's record pointer is
   stored to [EBP-0x8] at 00038b78 and reloaded at 00038b84 for the character
   byte; the second lookup overwrites the same slot at 00038c55 and is reloaded
   at 00038c58 for the flags store, so the two pointers share one frame slot and
   the first is dead by then.  fdps_unit_item_count's EAX is compared at
   00038ba2 and never stored.  Each fdps_draw_text cursor is discarded -- the
   next instruction is the ADD ESP that cleans its arguments -- and so is
   fdps_unit_add_item's 1-or-(-1), which the entry gate has already made a 1.
   Nothing sets EAX before the RET and the dispatcher reads nothing back, so the
   result is void.

   Table slot 35 at 00060250, reached only through the table: MAP22.DAT's
   tile-event entry 0 names it with occasion 1, the occasion
   fdps_map_set_pending_tile_event is given straight after
   fdps_battle_mark_unit_done. */
void fdps_chapter_23_event_give_martial_artist_ring(int unit_index)
{
    /* The unit that ended its turn on the shrine tile, and then the spirit the
       deployment has just appended -- two names for the one slot [EBP-0x8] the
       original keeps them in. */
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *spirit_unit;
    /* The counter of each twelve-frame hold, [EBP-0x4].  It paces the hold and
       nothing reads it. */
    int hold_frame;

    acting_unit = fdps_get_unit_record(unit_index);

    if (data_fdps_map_cell_event_triggered_flags[CH23_RING_LATCH_SLOT] == 0
            && acting_unit->char_id == CH23_RING_MARTIAL_ARTIST_CHAR_ID
            && fdps_unit_item_count(unit_index) != CH23_RING_BAG_FULL) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_RING_OPENING_TEXT_ID,
                       (unsigned char *) CH23_RING_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH23_RING_SPIRIT_WAVE,
                         CH23_RING_PLACE_ON_NEAREST_FREE_TILE);

        data_fdps_map_cursor_draw_mode = CH23_RING_MAP_CURSOR_BLANK;
        fdps_map_cursor_move_to(CH23_RING_SHRINE_VIEW_X,
                                CH23_RING_SHRINE_VIEW_Y);
        for (hold_frame = 0;
             hold_frame < CH23_RING_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }
        data_fdps_map_cursor_draw_mode = CH23_RING_MAP_CURSOR_NORMAL;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_RING_EXCHANGE_TEXT_ID,
                       (unsigned char *) CH23_RING_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        /* The newest record: the deployment above appended exactly one, so the
           live count minus one is the spirit. */
        spirit_unit = fdps_get_unit_record(data_fdps_map_unit_count - 1);
        spirit_unit->flags = CH23_RING_SPIRIT_RETIRED;
        for (hold_frame = 0;
             hold_frame < CH23_RING_HOLD_FRAMES;
             hold_frame++) {
            fdps_render_view_frame();
        }

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH23_RING_CLOSING_TEXT_ID,
                       (unsigned char *) CH23_RING_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        fdps_unit_add_item(unit_index, CH23_RING_ITEM_ID);
        data_fdps_map_cell_event_triggered_flags[CH23_RING_LATCH_SLOT] = 1;
    }
}
