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
