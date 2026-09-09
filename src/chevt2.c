/* chevt2.c -- the scripted chapter-event handlers of chapter 8.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt2.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7 and chevt2b.c for chapters 9 to 14.  Nothing here owns
 * state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "deploy.h"
#include "icon.h"
#include "unititem.h"
#include "text.h"
#include "chevt2.h"

/* The half of the AI byte the two merges in this file keep: AND DL,0xf0 at
   0003739b in the turn handler and at 000374a3 in the guard-death one.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code
   fdps_map_actor_behavior_step isolates with AND AL,0xf and dispatches on.
   src/unit.c, src/chevt1.c, src/chevt2b.c and src/chevt3.c spell the same mask
   out for the same field; it stays file-local at every end because no header
   owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The mode 13h aperture and its row stride, PUSH 0xa0000 at 000374c1 and
   PUSH 0x140 at 000374bc.  0xa0000 stays a literal because it is where the
   display adapter answers and not the address of anything the linker places
   (rebuild_info/pitfalls.md, contract E). */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 000374b7,
   000374b5 and 000374b3: glyph fill, no cell background, and the shadow the
   outline colour becomes while the font's outline flag is clear.  Every
   ordinary line of spoken game text is drawn with these three. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The five turns chapter 8's turn handler tests for, in the order the ladder
   tests them: CMP dword ptr [0x00069ce8],0x1 at 000372e3, 0x3 at 00037314,
   0x4 at 00037330, 0xa at 000373b0 and 0xc at 000373f1.  Each test is an
   equality followed by a JNZ to the next one, and the last JNZ goes straight
   to the epilogue, so there is no default arm: a turn none of the five names
   leaves the function having done nothing.  The five are exactly the turns
   map07.dat's turn-event table names for this slot. */
#define CH08_OPENING_ORDERS_TURN 1
#define CH08_GUEST_MAGE_TURN 3
#define CH08_CELL_GUARDS_TURN 4
#define CH08_FIRST_CAVALRY_TURN 10
#define CH08_SECOND_CAVALRY_TURN 12

/* The three lines the handler speaks, PUSH 0xc at 000372ff, PUSH 0x15 at
   000373df and PUSH 0x22 at 00037420.  All three index the chapter's own
   FDETXT08.TXT block rather than the shared one. */
#define CH08_OPENING_ORDERS_TEXT_ID 0x0c
#define CH08_FIRST_CAVALRY_TEXT_ID 0x15
#define CH08_SECOND_CAVALRY_TEXT_ID 0x22

/* The two cut-scene members the middle arms play, the strings at 0x61ff8 and
   0x62004 loaded into EAX at 0003731d and 0003733d.  Icon7-1.dat brings the
   guest mage 費塔加 onto the map -- its own deploy opcode names map07.dat's
   single wave-1 record, so nothing in this handler deploys him -- and
   Icon7-2.dat is the scene at the cell block that walks one of the two guards
   away and retires him.  The pair is orderable the wrong way round and the
   turn numbers do not say which is which, so the two literals are what the
   cases in tests/chevt2.c pin. */
#define CH08_GUEST_MAGE_SCRIPT "Icon7-1.dat"
#define CH08_CELL_GUARDS_SCRIPT "Icon7-2.dat"

/* The two waves the last two arms bring on, PUSH 0x2 at 000373bc and PUSH 0x3
   at 000373fd, matched against byte 0x15 of each 0x1a-byte deployment record
   of the resident MAP%02d.DAT block.  Wave 2 is map07.dat's four level-12
   cavalry of character id 0x58 and wave 3 the level-20 knight of id 0x59 with
   eight level-30 cavalry behind it. */
#define CH08_FIRST_CAVALRY_WAVE 2
#define CH08_SECOND_CAVALRY_WAVE 3

/* How both arms place what they bring on: XOR EAX,EAX / PUSH EAX at 000373b9
   and at 000373fa, so fdps_deploy_wave settles each unit on the nearest free
   walkable tile to its placement record's coordinates rather than on the
   coordinates themselves. */
#define CH08_TURN_PLACE_EXACT 0

/* The one unit index the turn-4 arm re-aims.  0x0e is parked twice, at
   [EBP-0x20] (0003734b) and at [EBP-0x1c] (00037352), because the inline
   range walk keeps a first and a last bound and here they hold the same
   constant, so the loop runs exactly once.  Unit 0x0e is the second of the two
   soldiers posted by the cells: they are the only two of the chapter's ten
   opening enemies deployed in behaviour mode 2, the mode that holds ground,
   and Icon7-2.dat walks the first of them (0x0d) away and retires it, so 0x0e
   is the one left standing. */
#define CH08_STANDING_GUARD_UNIT_INDEX 0x0e

/* The behaviour code the turn-4 merge ORs in: the constant parked at
   [EBP-0x18] at 00037359, copied on into [EBP-0x14] at 0003736f, which is the
   slot MOV DH,byte ptr [EBP-0x14] at 000373a1 reads.  Mode 3 is the arm
   fdps_map_actor_behavior_step reaches at 00010161: it resolves the record's
   own byte 0x35 as a character id through fdps_battle_find_unit_by_character_id
   and walks the actor toward whatever unit carries it.  Unit 0x0e's byte 0x35
   is 0, out of deployment byte 0x12 of map07.dat record 18, so the guard left
   standing sets off after 蘭迪斯. */
#define AI_BEHAVIOR_MODE_CHASE_CHARACTER 3

/* 000372d0.  Chapter 8's turn-scheduled event handler: the one slot map07.dat
   names for all five of the chapter's turn events, running whichever of them
   is due for the turn the player has just finished.

   THE LADDER IS FIVE EQUALITIES AND HAS NO DEFAULT ARM.  Every test is a CMP
   against data_fdps_battle_turn_counter followed by a JNZ to the next test,
   and the fifth JNZ at 000373f8 goes to the epilogue at 00037430 that every
   arm also jumps to.  A turn the five do not name therefore leaves the
   function having done nothing, and nothing in the body tests anything else.
   What keeps the arms off the other turns is map07.dat's own turn table, which
   names this slot exactly five times: {1,10,0}, {3,10,0}, {4,10,0}, {10,10,0}
   and {12,10,0}.

   THE TURN-4 ARM'S RANGE WALK IS AN INLINE EXPANSION, NOT A CALL.  The body
   carries the same fingerprint the chapter 8 guard-death handler below and the
   chapter 13 one carry -- fdps_object_set_field34_low_nibble_range (00036b60)
   with the constant argument triple (0xe, 0xe, 3): the three constants are
   parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18] (0003734b..00037359),
   copied into a second set of slots at [EBP-0xc], [EBP-0x10] and [EBP-0x14]
   (00037360..0003736f), and only then is the counter at [EBP-0x8] seeded from
   the first of them.  There is no CALL to that helper anywhere in the body;
   the only calls are the two script runs, the two deployments, the three draws
   and fdps_get_unit_record at 0003738a.  Writing the range as a call to the
   helper would put a CALL in the rebuild that the original does not make.

   The compare at 0003737b -- CMP EAX,dword ptr [EBP-0x10] / JLE 00037387 -- is
   signed and inclusive, and both bounds hold 0x0e, so the walk touches exactly
   one record and index 0x0e is written rather than skipped.

   THE MERGE KEEPS THE HIGH NIBBLE.  MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV
   DH,[EBP-0x14] / OR DH,DL / MOV [EAX+0x34],DH at 00037398..000373a6, so the
   behaviour code goes to 3 and the 0x40 and 0x80 flag bits
   fdps_map_actor_take_best_action and fdps_score_targets_for_item read on
   their own are carried across untouched.  Assigning the whole byte instead
   sends the unit down a different AI path.

   NOTHING IS GUARDED.  There is no one-shot latch, no liveness test on either
   guard and no compare against data_fdps_map_unit_count in front of the walk:
   the instruction after the argument-slot store at 000372dc is the first turn
   compare, and the instruction after the script call's ADD ESP,0x4 at
   00037348 is the first of the three constant stores.  Turn 4 plays Icon7-2.dat and rewrites unit
   0x0e's behaviour byte whether or not the player has already cleared the two
   guards, and adding the check that looks obviously missing would suppress a
   message the original still paints (rebuild_info/pitfalls.md).

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 000373be and 000373ff is
   data_fdps_chapter_current_chapter_id, so what the arrivals are placed by is
   whichever MAP%02d.COD the loaded chapter names.

   The record pointer comes back in EAX from the CALL at 0003738a and is
   stored to [EBP-0x4] at 00037392, then re-read at 00037395 for the load and
   again at 0003739e for the store, so both halves of the merge address the
   record that iteration fetched.  fdps_icon_script_run and fdps_deploy_wave
   leave nothing this body reads.  fdps_draw_text hands back a cursor in EAX
   and all three call sites discard it: after the first two the next
   instruction is the JMP to the epilogue and after the third it is the
   epilogue itself.  Nothing sets EAX before the RET at 00037436 and no
   dispatcher reads what comes back, so the result is void.

   event_arg is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 000372dc writes zero over the incoming slot before the
   first turn compare and nothing ever reads it back, so nothing a caller
   passes can change what the handler does; the store has no observable effect
   either, because the slot belongs to the caller's outgoing argument area and
   fdps_battle_run_turn_events drops it with its own ADD ESP,0x4 at
   0002e146. */
void fdps_chapter_08_event_for_turn(int event_arg)
{
    struct fdps_unit_record *standing_guard;
    int guard_unit_index;

    event_arg = 0;

    if (data_fdps_battle_turn_counter == CH08_OPENING_ORDERS_TURN) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH08_OPENING_ORDERS_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH08_GUEST_MAGE_TURN) {
        fdps_icon_script_run(CH08_GUEST_MAGE_SCRIPT);
    } else if (data_fdps_battle_turn_counter == CH08_CELL_GUARDS_TURN) {
        fdps_icon_script_run(CH08_CELL_GUARDS_SCRIPT);

        for (guard_unit_index = CH08_STANDING_GUARD_UNIT_INDEX;
             guard_unit_index <= CH08_STANDING_GUARD_UNIT_INDEX;
             guard_unit_index++) {
            standing_guard = fdps_get_unit_record(guard_unit_index);
            standing_guard->ai_behavior = (unsigned char)
                ((standing_guard->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_CHASE_CHARACTER);
        }
    } else if (data_fdps_battle_turn_counter == CH08_FIRST_CAVALRY_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH08_FIRST_CAVALRY_WAVE, CH08_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH08_FIRST_CAVALRY_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter == CH08_SECOND_CAVALRY_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH08_SECOND_CAVALRY_WAVE, CH08_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH08_SECOND_CAVALRY_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }
}

/* The one unit index chapter 8's guard-death handler re-aims.  0x13 is parked
   twice, at [EBP-0x20] (00037453) and at [EBP-0x1c] (0003745a), because the
   inline range walk keeps a first and a last bound, and here they are the same
   constant -- so the loop runs exactly once.  Unit 0x13 is the guest mage
   費塔加: map07.dat's deployment record 19 is the file's only wave-1 record and
   it comes on as the first index past the 19 units the map opens with. */
#define CH08_GUEST_MAGE_UNIT_INDEX 0x13

/* The behaviour code the chapter 8 merge ORs in: the constant parked at
   [EBP-0x18] at 00037461, copied on into [EBP-0x14] at 00037477, which is the
   slot MOV DH,byte ptr [EBP-0x14] at 000374a9 reads.  Mode 4 is the one
   fdps_map_actor_behavior_step handles by moving the map cursor to the unit
   and then walking it toward the destination tile held in its own record
   (ai_dest_x, ai_dest_y), so the unit stops fighting and heads for a place.
   For unit 0x13 that tile is map 7's cell block on the right edge. */
#define AI_BEHAVIOR_MODE_WALK_TO_DEST 4

/* Which line the handler speaks: PUSH 0xf at 000374c6, an index into the
   chapter's own FDETXT block rather than the shared one. */
#define CH08_CELL_ORDER_TEXT_ID 0x0f

/* 00037440.  Chapter 8's guard-death event: the enemy soldier posted by the
   cell block has been killed, so the chapter's guest mage is sent walking to
   the cage and the line that goes with it is painted.

   The body is one copy of the inline expansion the chapter 2, 5 and 7
   handlers in chevt1.c and the chapter 13 one below carry --
   fdps_object_set_field34_low_nibble_range (00036b60) with
   the constant argument triple (0x13, 0x13, 4) -- and it has the same
   fingerprint: the three constants are parked at [EBP-0x20], [EBP-0x1c] and
   [EBP-0x18] (00037453..00037461), copied into a second set of slots at
   [EBP-0xc], [EBP-0x10] and [EBP-0x14] (00037468..00037477), and only then is
   the counter at [EBP-0x8] seeded from the first of them.  There is no CALL to
   that helper in the body; the only calls are fdps_get_unit_record at 00037492
   and fdps_draw_text at 000374ce, so writing the range as a call to the helper
   would put a CALL in the rebuild that the original does not make.

   The compare at 00037483 -- CMP EAX,dword ptr [EBP-0x10] / JLE 0003748f -- is
   signed and inclusive, and both bounds hold 0x13, so the walk touches exactly
   one record and index 0x13 is written rather than skipped.

   The merge is the read-modify-write of the one byte its siblings do -- MOV
   DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL / MOV
   [EAX+0x34],DH at 0003749d..000374ae -- so the behaviour code goes to 4 and
   the two AI flag bits in the high nibble are carried across untouched.
   Assigning the whole byte instead would clear the 0x40 and 0x80 bits
   fdps_map_actor_take_best_action and fdps_score_targets_for_item read, which
   sends the unit down a different AI path.

   The record pointer comes back in EAX from the CALL at 00037492 and is stored
   to [EBP-0x4] at 0003749a, then re-read at 0003749d for the load and again at
   000374a6 for the store, so both halves of the merge address the record that
   iteration fetched.

   NOTHING GUARDS THE WRITE.  There is no latch, no compare in front of the
   loop and no test against data_fdps_map_unit_count: the instruction after the
   argument-slot store at 0003744c is the first of the three constant stores.
   The unit array is sized at exactly data_fdps_map_unit_count * 0x50, and
   until chapter 8's turn-3 cutscene deploys map07.dat's single wave-1 record
   the battle holds 19 records at indices 0..0x12 -- so a soldier that dies
   before then makes the original write offset 0x13 * 0x50 + 0x34 of a
   0x5f0-byte block.  Adding the natural `if (0x13 < data_fdps_map_unit_count)`
   would remove a write the original performs, and it is not why the mage stays
   put in that case: he is not on the map to be re-aimed either way, which is
   the guide's 如果在費塔加出現前就已經清光敵人，那麼他便不會去開牢門.

   The draw is unconditional and follows the walk (CALL 0x0001ff60 at
   000374ce), so the order is re-aim first, speak second.  fdps_draw_text hands
   back a cursor in EAX and this handler discards it: nothing between the ADD
   ESP,0x1c at 000374d3 and the RET at 000374dc reads EAX, and no dispatcher
   reads what comes back, so the result is void.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003744c writes zero over the incoming slot before
   anything else and nothing ever reads it back, so which unit died cannot
   reach anything this handler does; the store has no observable effect,
   because the slot belongs to the caller's outgoing argument area and the
   death-script runner drops it with ADD ESP,0x4 at 0001dcb4. */
void fdps_chapter_08_event_send_guest_mage_to_cells(int unit_index)
{
    struct fdps_unit_record *guest_mage;
    int mage_unit_index;

    unit_index = 0;

    for (mage_unit_index = CH08_GUEST_MAGE_UNIT_INDEX;
         mage_unit_index <= CH08_GUEST_MAGE_UNIT_INDEX;
         mage_unit_index++) {
        guest_mage = fdps_get_unit_record(mage_unit_index);
        guest_mage->ai_behavior = (unsigned char)
            ((guest_mage->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_WALK_TO_DEST);
    }

    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH08_CELL_ORDER_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
}

/* The inclusive range of unit indices chapter 8's villager-escape handler
   acts for, the two literals of CMP dword ptr [EBP+0x14],0xf / JL at 00037613
   and CMP dword ptr [EBP+0x14],0x12 / JLE at 00037619.  Both compares are
   signed, so a negative index falls out at the first of them.  These are the
   four captives map07.dat deploys: the guide's LV12村民x2 and LV12村婦x2. */
#define CH08_FIRST_VILLAGER_UNIT_INDEX 0x0f
#define CH08_LAST_VILLAGER_UNIT_INDEX 0x12

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) this
   chapter counts escapes in: byte ptr [0x000640e9], element 0x11 of the
   32-entry array based at 0x000640d8, one past the shared one-shot latch.
   That the byte really is inside that array and not a global of its own is
   settled by fdps_chapter_state_reset, which memsets 0x20 bytes from 0x640d8
   at 00022782, and by fdps_load_savegame, which moves the same 0x20 bytes at
   00024062.  fdps_chapter_08_post_action reads this element to tell the
   chapter's win from its loss. */
#define CH08_ESCAPED_VILLAGER_COUNT_SLOT 0x11

/* What turns a villager's unit index into the text entry that villager speaks
   as it leaves: ADD EAX,0xd at 00037640, so 0xf..0x12 speak 0x1c..0x1f. */
#define CH08_VILLAGER_LINE_TEXT_ID_BIAS 0x0d

/* Which of the two closing lines the last villager out speaks, and the index
   that separates them: CMP dword ptr [EBP+0x14],0x11 / JGE at 00037698, with
   0x20 stored below it and 0x21 at or above.  The split falls exactly between
   the map's two 村民 at 0xf and 0x10 and its two 村婦 at 0x11 and 0x12, so the
   line matches who is speaking it. */
#define CH08_LAST_VILLAGER_IS_A_WOMAN_FROM 0x11
#define CH08_LAST_VILLAGER_MAN_TEXT_ID 0x20
#define CH08_LAST_VILLAGER_WOMAN_TEXT_ID 0x21

/* How many of the four have to be out already for the reward test to pass:
   CMP dword ptr [EBP-0x8],0x3 / JNZ at 00037681.  The count is taken before
   this villager is marked retired, so 3 means "the one that is leaving now is
   the last one still in".  The test is an equality and not a `>=`: raising the
   mark above the count without also making this a 4 puts the reward out of
   reach for good. */
#define CH08_VILLAGERS_OUT_BEFORE_THE_LAST 3

/* How many escapes the reward needs, CMP EAX,0x1 / JG at 0003768e: strictly
   more than one, so the chapter pays nothing when three of the four were
   killed and only the fourth walked out. */
#define CH08_MIN_ESCAPES_FOR_REWARD 1

/* What 費塔加 is handed, the three constants stored to [EBP-0x4] at 000376df,
   000376f4 and 000376fd against the escape counts 2, 3 and otherwise: 炎之寶石
   worth 1500, 速度藥水 worth 5000, 風精之羽 worth 12000 (assets/items.md).
   The last is the guide's 若四個村民全被救出，結束後會得到風精之羽（在費塔加
   身上）. */
#define CH08_REWARD_TWO_ESCAPED 0xc8
#define CH08_REWARD_THREE_ESCAPED 0xda
#define CH08_REWARD_ALL_FOUR_ESCAPED 0xdd

/* The escape counts the two named rewards are selected on, CMP EAX,0x2 at
   000376da and CMP EAX,0x3 at 000376ef.  Both are equality tests against the
   zero-extended byte, so a count that somehow ran past 4 would take the
   風精之羽 branch as well. */
#define CH08_TWO_ESCAPED 2
#define CH08_THREE_ESCAPED 3

/* The value written over the whole flags byte at record offset 5, MOV byte ptr
   [EAX+0x5],0x1 at 00037724.  It is an assignment of the literal and not an OR
   (the encoding is c6 40 05 01; an OR would be 80 48 05 01), so retiring a
   villager also clears bit 7, the acted-this-turn flag, along with anything
   else that byte was carrying. */
#define UNIT_FLAGS_RETIRED 1

/* 00037600.  Chapter 8's villager-escape event: one of the four captives walks
   off the battlefield and speaks its line, and once the last of them is gone
   the chapter's reward lands in the guest mage's bag.

   The whole body sits inside one range test, CMP [EBP+0x14],0xf / JL and CMP
   [EBP+0x14],0x12 / JLE at 00037613..0003761d, whose failing arm jumps to the
   epilogue at 00037728.  Both compares are signed, so this is a plain signed
   inclusive range and not the unsigned one-compare idiom.

   The escape count is bumped with a read-modify-write of one byte, INC byte
   ptr [0x000640e9] at 00037624, and is read back three times, each time as
   XOR EAX,EAX / MOV AL,[0x000640e9] -- a zero-extending byte load, so the
   comparisons that follow are unsigned in effect even though the JG at
   00037691 is a signed jump.  Declaring the array as anything wider or signed
   changes which branch a count above 0x7f takes.

   THE RETIRED COUNT IS TAKEN BEFORE THIS VILLAGER IS MARKED RETIRED, which is
   why the test that follows reads == 3 rather than == 4: the loop at
   00037652..0003767f asks fdps_unit_is_retired about all four captives
   including the one this call is for, and that one is still in the battle at
   this point.  Moving the mark at 00037724 above the loop without also making
   the 3 a 4 makes the reward unreachable.  The loop's own shape is the -od
   for-statement layout -- seed, test-and-jump, a separate increment block the
   body jumps back to -- and the MOV EAX,[EBP-0xc] in front of each INC is the
   discarded old value of a postfix increment, not a use.

   fdps_unit_is_retired's answer is tested with TEST EAX,EAX / JZ at
   00037675 and is never stored, and neither is fdps_draw_text's cursor: the
   only CALL result this function keeps is fdps_get_unit_record's, stored to
   [EBP-0x10] at 0003771e and read straight back for the flags store.

   ONE STACK SLOT CARRIES BOTH THE CLOSING LINE AND THE REWARD ID.  SUB
   ESP,0x10 makes room for exactly four locals, and [EBP-0x4] holds the text id
   0x20 or 0x21 for the draw at 000376cb and is then overwritten with the item
   id for the call at 0003770a, so one variable spans both uses rather than
   two.

   fdps_unit_add_item's answer is discarded (ADD ESP,0x8 at 0003770f and
   nothing reads EAX), so a guest mage whose eight bag entries are all full
   loses the reward without a word.  The reward goes to unit 0x13 and not to
   the villager: PUSH 0x13 at 00037708.

   Nothing latches.  Calling this twice for the same villager counts two
   escapes and draws the line twice, and the count is the number of calls made
   for indices 0xf..0x12 rather than the number of distinct villagers out. */
void fdps_chapter_08_event_villager_escapes(int unit_index)
{
    struct fdps_unit_record *escaping_villager;
    int closing_line_or_reward_id;
    int retired_villager_count;
    int villager_unit_index;

    retired_villager_count = 0;

    if (unit_index >= CH08_FIRST_VILLAGER_UNIT_INDEX &&
        unit_index <= CH08_LAST_VILLAGER_UNIT_INDEX) {

        data_fdps_map_cell_event_triggered_flags
            [CH08_ESCAPED_VILLAGER_COUNT_SLOT]++;

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       unit_index + CH08_VILLAGER_LINE_TEXT_ID_BIAS,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);

        for (villager_unit_index = CH08_FIRST_VILLAGER_UNIT_INDEX;
             villager_unit_index <= CH08_LAST_VILLAGER_UNIT_INDEX;
             villager_unit_index++) {
            if (fdps_unit_is_retired(villager_unit_index) != 0) {
                retired_villager_count++;
            }
        }

        if (retired_villager_count == CH08_VILLAGERS_OUT_BEFORE_THE_LAST &&
            data_fdps_map_cell_event_triggered_flags
                [CH08_ESCAPED_VILLAGER_COUNT_SLOT] >
                    CH08_MIN_ESCAPES_FOR_REWARD) {

            if (unit_index < CH08_LAST_VILLAGER_IS_A_WOMAN_FROM) {
                closing_line_or_reward_id = CH08_LAST_VILLAGER_MAN_TEXT_ID;
            } else {
                closing_line_or_reward_id = CH08_LAST_VILLAGER_WOMAN_TEXT_ID;
            }

            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           closing_line_or_reward_id,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

            if (data_fdps_map_cell_event_triggered_flags
                    [CH08_ESCAPED_VILLAGER_COUNT_SLOT] == CH08_TWO_ESCAPED) {
                closing_line_or_reward_id = CH08_REWARD_TWO_ESCAPED;
            } else if (data_fdps_map_cell_event_triggered_flags
                           [CH08_ESCAPED_VILLAGER_COUNT_SLOT] ==
                               CH08_THREE_ESCAPED) {
                closing_line_or_reward_id = CH08_REWARD_THREE_ESCAPED;
            } else {
                closing_line_or_reward_id = CH08_REWARD_ALL_FOUR_ESCAPED;
            }

            fdps_unit_add_item(CH08_GUEST_MAGE_UNIT_INDEX,
                               closing_line_or_reward_id);
        }

        escaping_villager = fdps_get_unit_record(unit_index);
        escaping_villager->flags = UNIT_FLAGS_RETIRED;
    }
}
