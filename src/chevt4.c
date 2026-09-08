/* chevt4.c -- the scripted chapter-event handlers of chapters 20 to 23.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt4.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for 8 to 14 and chevt3.c for 15 to 19.  Nothing
 * here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "text.h"
#include "deploy.h"
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

   src/chevt1.c, src/chevt2.c and src/chpost2.c spell the same slot out for the
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
