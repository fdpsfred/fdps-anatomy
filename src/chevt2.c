/* chevt2.c -- the scripted chapter-event handlers of chapters 8 to 14.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt2.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "deploy.h"
#include "unititem.h"
#include "text.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "chevt2.h"

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) the
   one-shot handlers latch: byte ptr [0x000640e8], element 0x10 of the 32-entry
   array based at 0x000640d8.  The array's own indexer is a cell's raw event
   code and the shipped M%02d.DTL event planes only reach codes 0 to 15, so
   element 0x10 is the first slot no map cell can name and the handlers keep
   their latch in it -- one slot shared by all of them, which is safe only
   because one chapter is loaded at a time and fdps_chapter_state_reset memsets
   the whole array when a chapter starts.  Being inside that array is also what
   makes the latch survive a save, because the save and load paths move all
   0x20 bytes to and from the slot image.

   src/chevt1.c and src/chpost2.c spell the same slot out for the same reason;
   it stays file-local at all three ends because no header owns it. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The half of the AI byte the two merges in this file keep: AND DL,0xf0 at
   000374a3 in the chapter 8 handler and at 00037a33 in the chapter 13 one.
   The four bits it preserves are flags other code reads on their own -- 0x40
   in fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item
   -- while the four it drops are the behaviour code
   fdps_map_actor_behavior_step isolates with AND AL,0xf and dispatches on.
   src/unit.c, src/chevt1.c and src/chevt3.c spell the same mask out for the
   same field; it stays file-local at every end because no header owns it. */
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

/* The wave chapter 9's reinforcements carry: PUSH 0x1 at 00037746, matched
   against byte 0x15 of each 0x1a-byte deployment record of the resident
   MAP%02d.DAT block.  It is a literal with no arithmetic and no read of
   data_fdps_battle_turn_counter anywhere in the body, so this handler always
   asks for wave 1 whenever it is run.  Six of map08.dat's 31 records carry it
   -- character ids 0x4c, 0x56 and 0x5d, two of each at level 13 and all on
   side 0 -- against the 24 wave-0 records the map opens with. */
#define CH09_ARRIVAL_WAVE 1

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00037743..00037745, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each arrival goes on
   the nearest free walkable tile to its placement record's coordinates rather
   than on the coordinates themselves. */
#define CH09_PLACE_EXACT 0

/* The line spoken over the arrivals: PUSH 0x17 at 00037769, the last of the 24
   entries of chapter 9's own FDETXT%02d.TXT block -- its 24-word offset table
   ends at 0x742, which is that entry.  The entry opens with the portrait
   control code -0x11 followed by 0x4c, so it is spoken by character 0x4c, one
   of the six units the deployment on the line above has just brought on. */
#define CH09_ARRIVAL_TEXT_ID 0x17

/* 00037730.  Chapter 9's scheduled reinforcement wave: the map's six wave-1
   enemies march on and the line announcing them is painted over them.

   The frame is the family's standard Watcom four-push one with an empty local
   area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB
   ESP,0x0 at 00037730..00037736 -- so there is no local here at all and every
   argument is computed straight into the pushes.  The two caller-cleans, ADD
   ESP,0xc after the deployment and ADD ESP,0x1c after the draw, are this
   function's own, and the RET at 0003777d carries no immediate, so the
   convention is the stack one at both ends of the call.

   THERE IS NO GUARD OF ANY KIND IN THE BODY: no one-shot latch, no test of
   data_fdps_battle_turn_counter and no compare anywhere -- the instruction
   after the argument-slot store at 0003773c is the XOR that builds the
   deployment's third argument.  What makes the event happen once is the data:
   map08.dat's turn-event table, the sixteen 3-byte entries
   fdps_battle_run_turn_events walks from offset 3 of the chapter script, names
   handler 0x0e in one entry only -- turn 0x0f, side 0 -- so a second call
   would append the same six records a second time and nothing here would stop
   it.

   The two calls are unconditional and in this order -- CALL 0x00023830 at
   0003774e, CALL 0x0001ff60 at 00037771 -- so the enemies are on the map
   before the line is spoken, the opposite of chapter 3's ambush handler, which
   speaks first.  It is also what makes the portrait in the message name a unit
   that is already standing on the field.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 00037748 is data_fdps_chapter_current_chapter_id, the same
   argument the chapter 4, 6 and 10 handlers read and the opposite of the four
   chapter 3 handlers, which push the literal 2.  The deployment records still
   come from whichever MAP%02d.DAT is resident; what the number chooses is the
   MAP%02d.COD coordinates the arrivals are put down at.

   fdps_deploy_wave leaves nothing this body reads, and the cursor
   fdps_draw_text hands back in EAX is discarded: the ADD ESP,0x1c at 00037776
   is followed straight by the four POPs and the RET, with nothing in between
   that touches EAX.  Nothing sets EAX before that RET and no dispatcher reads
   what comes back, so the result is void.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003773c writes zero over the incoming slot before either
   call and nothing ever reads it back, so which unit the event fired for
   cannot reach anything this handler does; the store has no observable effect,
   because the slot belongs to the caller's outgoing argument area and the
   turn-event dispatcher drops it with its own stack cleanup. */
void fdps_chapter_09_event_deploy_wave_1(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH09_ARRIVAL_WAVE,
                     CH09_PLACE_EXACT);
    fdps_draw_text(data_fdps_current_chapter_text_ptr, CH09_ARRIVAL_TEXT_ID,
                   (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                   MESSAGE_FG_COLOR, MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
}

/* The one turn the body compares for equality, CMP dword ptr
   [0x00069ce8],0x3 / JNZ at 0003778c, and the wave that turn brings on, PUSH
   0x1 at 00037798.  map09.dat carries eight records tagged wave 1, all
   spawning at tile (13, 2) -- the door the chapter opens behind. */
#define CH10_ANNOUNCED_WAVE_TURN 3
#define CH10_ANNOUNCED_WAVE 1

/* The upper bound of the middle arm, CMP dword ptr [0x00069ce8],0xd / JG at
   000377d0.  It is a SIGNED compare and it is the only test the arm has: there
   is no lower bound, so every turn from 13 downwards that is not 3 runs this
   path and asks for whatever wave the subtraction below produces. */
#define CH10_LAST_DOOR_TURN 0xd

/* What the turn counter is reduced by to name the wave, SUB EAX,0x4 at
   000377e5.  The scheduled turns 6 through 13 map onto waves 2 through 9, the
   eight pairs of records map09.dat puts at the two mid-map doors. */
#define CH10_DOOR_WAVE_TURN_BIAS 4

/* The wave every turn past 13 brings on, PUSH 0xb at 00037859: map09.dat's six
   wave-11 records, which spawn at tiles (3..5, 1..2).  Wave 10 is not this
   handler's -- it belongs to the same map's tile trigger above. */
#define CH10_LAST_WAVE 0xb

/* How all three deployments are placed: XOR EAX,EAX / PUSH EAX at 00037795,
   000377dd and 00037856, so fdps_deploy_wave passes 0 on to fdps_deploy_unit
   and each arrival goes on the nearest free walkable tile to its placement
   record's coordinates rather than on the coordinates themselves. */
#define CH10_TURN_PLACE_EXACT 0

/* The two lines this handler speaks: PUSH 0x12 at 000377bb for the wave-1
   arrival and PUSH 0x13 at 0003787c for the wave-11 one, both entries of the
   chapter's own FDETXT%02d.TXT block.  The middle arm speaks neither. */
#define CH10_ANNOUNCED_WAVE_TEXT_ID 0x12
#define CH10_LAST_WAVE_TEXT_ID 0x13

/* The two world pixels the cursor is walked to, PUSH 0x48 / PUSH 0xd8 at
   000377f7 and PUSH 0x168 / PUSH 0xd8 at 00037824.  At the 24-pixel tile step
   they are tiles (3, 9) and (15, 9), the two doors every record of waves 2
   through 9 spawns at.  fdps_map_cursor_move_to takes world pixels, not
   tiles. */
#define CH10_LEFT_DOOR_WORLD_X 0x48
#define CH10_RIGHT_DOOR_WORLD_X 0x168
#define CH10_DOOR_WORLD_Y 0xd8

/* How long the view rests on each door: the CMP against 0xc at 0003780d and
   0003783d.  Each frame costs one timer tick inside fdps_render_view_frame, so
   twelve is the length of the pause and not a repaint count. */
#define CH10_DOOR_HOLD_FRAMES 0xc

/* 00037780.  Chapter 10's turn-scheduled reinforcements: the wave-1 group with
   its announcement on turn 3, one enemy at each of the two mid-map doors with
   the view panning onto both of them on every turn from 6 to 13, and the
   wave-11 group with its own line on turn 19.

   The frame is the family's four-push one with an empty local area -- PUSH EBX
   / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at
   00037780..00037786 -- so there is no local here at all and the frame counter
   has nowhere to live but the incoming argument slot.  Every caller-clean in
   the body is this function's own (ADD ESP,0xc after each deployment, ADD
   ESP,0x1c after each draw, ADD ESP,0x8 after each cursor move), the RET at
   00037890 carries no immediate, and the turn-event runner pushes one dword
   and drops it with ADD ESP,0x4 at 0002e146, so the convention is the stack
   one at both ends of the call.

   THE THREE ARMS ARE TWO TESTS, NOT A RANGE.  CMP dword ptr [0x00069ce8],0x3 /
   JNZ at 0003778c picks the announced arm, and CMP dword ptr [0x00069ce8],0xd
   / JG at 000377d0 picks between the door arm and the last-wave arm.  The
   second compare is signed and has no partner below it, so the door arm is
   every turn of 13 or less except 3 -- turn 0 asks for wave -4 and turn 2 for
   wave -2.  Neither matches a deployment record, so nothing arrives, but the
   cursor still crosses both doors and twenty-four frames are still composed.
   What confines the arm to waves 2 through 9 is map09.dat's turn table, which
   names this slot on turns 3, 6..13 and 19 only.

   THE WAVE THE MIDDLE ARM ASKS FOR IS SIGNED ARITHMETIC ON A SIGNED COUNTER.
   MOV EAX,[0x00069ce8] / SUB EAX,0x4 at 000377e0 pushes the difference as a
   full dword, so a counter below 4 sends a negative wave into
   fdps_deploy_wave, which compares it against each record's unsigned wave byte
   and matches nothing.  Widening either side to unsigned would turn that into
   a very large wave number instead, which still matches nothing -- but it also
   turns the JG into an unsigned test, and then a negative counter would take
   the last-wave arm and bring wave 11 on early.

   THE ORDER WITHIN EACH ARM IS WHAT THE PLAYER SEES.  The announced arms
   deploy first and draw second (CALL 0x00023830 then CALL 0x0001ff60), so the
   line is spoken over enemies already standing on the field; the door arm
   deploys first as well, then walks the cursor onto each door in turn and
   holds the view there, so the arrivals are on the map before the pan starts.

   THE MAP NUMBER IS THE CHAPTER GLOBAL AND NOT A LITERAL.  PUSH dword ptr
   [0x00069cf4] at 0003779a, 000377e9 and 0003785b is
   data_fdps_chapter_current_chapter_id, the same argument the chapter 4, 6 and
   9 handlers read.  The deployment records still come from whichever
   MAP%02d.DAT is resident; what the number chooses is the MAP%02d.COD
   coordinates the arrivals are put down at.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   00037806 and again at 00037836 writes zero over the incoming argument, and
   each loop compares and INCs that same slot, so the counter and the parameter
   are one storage location and there is no local frame to hold anything else.
   The other two arms never touch the slot.  The store has no observable effect
   on the caller either, because the slot belongs to its outgoing argument area
   and the turn-event runner drops it with its own ADD ESP,0x4.

   Both loops are the -od shape of a for statement: the compare at the top, a
   dead MOV EAX,[EBP+0x14] ahead of the INC, and the body reached by a JL past
   the exit jump.  Both are signed (JL) and both stop at 12.

   THERE IS NO LATCH AND NO OTHER GUARD.  Nothing in the body tests or writes
   the family's one-shot slot and nothing records that the handler ran, so
   every call the turn table makes fires in full.

   fdps_deploy_wave, fdps_map_cursor_move_to and fdps_render_view_frame all
   leave nothing this body reads.  fdps_draw_text hands back a cursor in EAX
   and both call sites discard it: after the first the next instruction is the
   JMP to the epilogue at 000377cb and after the second it is the epilogue
   itself.  Nothing sets EAX before the RET and no dispatcher reads what comes
   back, so the result is void.

   event_arg is the handler table's shared parameter.  The turn-event runner is
   the only dispatcher that reaches this slot and it pushes a literal 0 at
   0002e13e, and no arm reads the incoming value before overwriting it, so
   nothing a caller passes can change what the handler does. */
void fdps_chapter_10_event_deploy_wave_for_turn(int event_arg)
{
    if (data_fdps_battle_turn_counter == CH10_ANNOUNCED_WAVE_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH10_ANNOUNCED_WAVE, CH10_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH10_ANNOUNCED_WAVE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    } else if (data_fdps_battle_turn_counter <= CH10_LAST_DOOR_TURN) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         data_fdps_battle_turn_counter -
                             CH10_DOOR_WAVE_TURN_BIAS,
                         CH10_TURN_PLACE_EXACT);

        fdps_map_cursor_move_to(CH10_LEFT_DOOR_WORLD_X, CH10_DOOR_WORLD_Y);
        /* The argument slot is the counter, as the assembly has it. */
        for (event_arg = 0;
             event_arg < CH10_DOOR_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }

        fdps_map_cursor_move_to(CH10_RIGHT_DOOR_WORLD_X, CH10_DOOR_WORLD_Y);
        for (event_arg = 0;
             event_arg < CH10_DOOR_HOLD_FRAMES;
             event_arg++) {
            fdps_render_view_frame();
        }
    } else {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, CH10_LAST_WAVE,
                         CH10_TURN_PLACE_EXACT);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH10_LAST_WAVE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
    }
}

/* The wave the ambush brings on: PUSH 0xa at 000378d9, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block.
   Ten of MAP09.DAT's records carry it. */
#define AMBUSH_WAVE 10

/* How the ten are placed: XOR EAX,EAX / PUSH EAX at 000378d6, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  Wave 0, the group a map opens with, is the
   one deployed with this flag set. */
#define AMBUSH_PLACE_EXACT 0

/* 000378a0.  Chapter 10's stairway ambush: the first unit that is not on side
   0 to trigger the map's event tile brings on the ten reinforcements of wave
   10.

   fdps_get_unit_record is called before either test and unconditionally (CALL
   at 000378b0, with the result stored to [EBP-0x4] at 000378b8), and the
   record it hands back is read for one byte only, the side at record+6.  The
   two tests are the JNZ at 000378c2 over the latch and the JA at 000378cb over
   that byte, both jumping to the same epilogue, which is the short circuit
   written here: a non-zero latch means the side byte is never loaded.

   The side test is CMP byte ptr [EAX+0x6],0x0 / JA -- unsigned, so it is a
   plain "not 0" over the whole byte range and not a sign test.  Side 0 is the
   enemy, 1 the guest/neutral one and 2 the player's own roster, so what the
   test really keeps out is an enemy unit walking over the tile: the player's
   units and the guests spring the chapter's own ambush alike.

   The latch is the shared one-shot slot and it is written before the deploy
   call (MOV byte ptr [0x000640e8],0x1 at 000378cf), not after it, so a handler
   re-entered from inside fdps_deploy_wave could not fire twice either.  It is
   tested against 0 rather than against 1: any non-zero value in the slot
   blocks the body.

   The map number handed to fdps_deploy_wave is read out of
   data_fdps_chapter_current_chapter_id at the call site (PUSH dword ptr
   [0x00069cf4] at 000378db) and not out of anything this handler holds, so it
   is whatever chapter is loaded -- 9 for this one, which is the map09 the
   handler's table slot is only ever named from.

   Nothing sets EAX before the RET at 000378ef and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_10_event_deploy_wave_10(int unit_index)
{
    struct fdps_unit_record *triggering_unit;

    triggering_unit = fdps_get_unit_record(unit_index);

    if ((data_fdps_map_cell_event_triggered_flags[
             CHAPTER_EVENT_ONE_SHOT_SLOT] == 0) &&
        (triggering_unit->side != 0)) {
        data_fdps_map_cell_event_triggered_flags[
            CHAPTER_EVENT_ONE_SHOT_SLOT] = 1;
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id, AMBUSH_WAVE,
                         AMBUSH_PLACE_EXACT);
    }
}

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x18] at 000379f1 is 0x0, copied on into [EBP-0x14], which is the slot
   the OR reads.  Mode 0 is the default chain that paths a unit toward the
   nearest opposing unit; mode 2, which is what map12.dat deploys all 37 of its
   units in, holds position and only scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* 000379d0.  Chapter 13's death-triggered event: 36 of the map's 37 enemies
   stop holding position and start advancing on the player.

   The body is one copy of the inline expansion the chapter 2, 5 and 7 handlers
   in chevt1.c carry -- fdps_object_set_field34_low_nibble_range (00036b60)
   with the constant argument triple (9, 0x2c, 0) -- and it has the same
   fingerprint: the three constants are parked at [EBP-0x20], [EBP-0x1c] and
   [EBP-0x18] (000379e3..000379f1), copied into a second set of slots at
   [EBP-0xc], [EBP-0x10] and [EBP-0x14] (000379f8..00037a07), and only then is
   the counter at [EBP-0x8] seeded from the first of them.  There is no CALL to
   that helper in the body; the only CALL is fdps_get_unit_record at 00037a22,
   once per iteration, so writing the range as a call to the helper would put a
   CALL in the rebuild that the original does not make.

   The compare at 00037a13 -- CMP EAX,dword ptr [EBP-0x10] / JLE 00037a1f -- is
   signed and inclusive, so the range is unit indices 9 through 0x2c and 0x2c is
   the last index written, not one past the end.  Chapter 13's map12.dat lays 9
   player records down at 0..8 and its own 37 deployment records at 9..0x2d, so
   the walk stays inside the unit array but stops one record short of it: unit
   index 0x2d keeps the behaviour the map gave it.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; both bounds are literals in the
   instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL /
   MOV [EAX+0x34],DH at 00037a30..00037a3e -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL at 00037a22 and is stored
   to [EBP-0x4] at 00037a2a, then re-read at 00037a2d for the load and again at
   00037a36 for the store, so both halves of the merge address the record
   fetched by that iteration.

   There is no one-shot latch, unlike the chapter 5 handler: the instruction
   after the argument-slot store at 000379dc is the first of the three constant
   stores, with no compare between them.  The event is fired by a death script
   rather than a tile trigger, and the unit can only die once, so the data is
   what makes it happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 000379dc writes zero over the incoming slot and nothing
   ever reads it back, so which unit died cannot reach anything this handler
   does; the store has no observable effect, because the slot belongs to the
   caller's outgoing argument area and the death-script runner drops it with
   ADD ESP,0x4 at 0001dcb4.

   Nothing sets EAX before the RET at 00037a49 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_13_event_enemies_advance(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 9;
         advancing_unit_index <= 0x2c;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}
