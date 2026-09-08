/* chevt3.c -- the scripted chapter-event handlers of chapters 15 to 19.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the cell search and the death-script
 * runner -- call it indirectly, so none of them appears as a static caller.
 *
 * See chevt3.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7 and chevt2.c for chapters 8 to 14.  Nothing here owns state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "btlend.h"
#include "unit.h"
#include "unititem.h"
#include "deploy.h"
#include "msgwin.h"
#include "text.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "chevt3.h"

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 00037b53.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action at 00012c72 and 0x80 in
   fdps_score_targets_for_item at 00013380 -- while the four it drops are the
   behaviour code fdps_map_actor_behavior_step isolates with AND AL,0xf at
   00010062 and dispatches on.  src/unit.c, src/chevt1.c and src/chevt2.c spell
   the same mask out for the same field; it stays file-local at every end
   because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x18] at 00037b11 is 0x0, copied on into [EBP-0x14] at 00037b27, which
   is the slot MOV DH,byte ptr [EBP-0x14] at 00037b59 reads.  Mode 0 is the
   default chain that paths a unit toward the nearest opposing unit; mode 2,
   which is what map14.dat deploys all of its units in, holds position and only
   scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* 00037af0.  Chapter 15's death-triggered event: the nine-unit enemy group
   holding the bridgehead in the top-right corner stops standing its ground and
   starts advancing on the party.

   The body is one copy of the inline expansion the chapter 2, 5 and 7 handlers
   in chevt1.c and the chapter 13 handler in chevt2.c carry --
   fdps_object_set_field34_low_nibble_range (00036b60) with the constant
   argument triple (0x1d, 0x25, 0) -- and it has the same fingerprint: the three
   constants are parked at [EBP-0x20], [EBP-0x1c] and [EBP-0x18]
   (00037b03..00037b11), copied into a second set of slots at [EBP-0xc],
   [EBP-0x10] and [EBP-0x14] (00037b18..00037b27), and only then is the counter
   at [EBP-0x8] seeded from the first of them at 00037b2a.  There is no CALL to
   that helper in the body; the only CALL is fdps_get_unit_record at 00037b42,
   once per iteration, so writing the range as a call to the helper would put a
   CALL in the rebuild that the original does not make.

   The compare at 00037b33 -- CMP EAX,dword ptr [EBP-0x10] / JLE 00037b3f -- is
   signed and inclusive, so the range is unit indices 0x1d through 0x25 and
   0x25 is the last index written, not one past the end.  Chapter 15's map14.dat
   lays 9 player records down at 0..8 and its own wave-0 deployment records
   1..43 at 9..0x33, so unit index = deployment record index + 8 and this range
   is records 21..29: the three barbarian warriors, two ice mages and three
   archers of the top-right bridgehead block, plus record 27, the beam turret
   that is the chapter's victory condition.  The turret has no movement
   allowance, so the mode change does not alter what it does; it is inside the
   range because the range is contiguous.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; both bounds are literals in the
   instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL /
   MOV [EAX+0x34],DH at 00037b50..00037b5e -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from the CALL at 00037b42 and is stored
   to [EBP-0x4] at 00037b4a, then re-read at 00037b4d for the load and again at
   00037b56 for the store, so both halves of the merge address the record
   fetched by that iteration.

   There is no one-shot latch: the instruction after the argument-slot store at
   00037afc is the first of the three constant stores, with no compare between
   them.  The event is fired by a death script and the unit that carries it can
   only die once, so the data is what makes it happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 00037afc writes zero over the incoming slot and nothing
   ever reads it back, so which unit the event fired for cannot reach anything
   this handler does; the store has no observable effect, because the slot
   belongs to the caller's outgoing argument area and the death-script runner
   drops it with ADD ESP,0x4 at 0001dcb4.

   Nothing sets EAX before the RET at 00037b69 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_15_event_activate_enemy_group(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    for (advancing_unit_index = 0x1d;
         advancing_unit_index <= 0x25;
         advancing_unit_index++) {
        unit = fdps_get_unit_record(advancing_unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
             AI_BEHAVIOR_MODE_ADVANCE);
    }
}

/* The mode 13h pen the message panel's own text sits at and the screen's row
   stride: PUSH 0xaa44a and PUSH 0x140 in front of the panel-interior draws the
   two events below make (00037beb / 00037be6 in the boss-defeat event, and
   00037d1d / 00037d18 with the nine that repeat them in the smith event).  The
   pitch is the screen's, so it stands in front of the full-screen draws of both
   events as well.  0xaa44a is screen (138, 131), the origin fdps_draw_text puts
   the pen
   back to on a page break, and it stays a literal because it is an address
   inside the display adapter's aperture rather than the address of anything the
   linker places (rebuild_info/pitfalls.md, contract E). */
#define PANEL_TEXT_ORIGIN 0x000aa44a
#define VGA_SCREEN_PITCH 0x140

/* The standard message colours, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of
   every one of those draws (00037bb4, 00037bb2 and 00037bb0 and their three
   repeats, then 00037d13, 00037d11 and 00037d0f and their nine):
   glyph fill, no cell background, and the shadow the outline colour becomes
   while the font's outline flag is clear. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The mode 13h page the three full-screen lines are written straight onto,
   PUSH 0xa0000 at 00037bbe, 00037c25 and 00037c9a in the boss-defeat event
   below.  It is the display adapter's aperture and not the address of anything
   the linker places, so it stays a literal (rebuild_info/pitfalls.md,
   contract E). */
#define CH15_DUEL_SCREEN_TEXT_DEST 0x000a0000

/* The last battle turn the duel is still offered on: CMP dword ptr
   [0x00069ce8],0x19 / JG at 00037b81, a signed inclusive compare, so a cannon
   that falls on turn 25 still brings the challenger out and turn 26 does not.
   THE TEST IS <= 25 AND NOT < 25 -- the stricter comparison silently takes the
   妖刀村雨 away from a 25-turn clear. */
#define CH15_DUEL_LAST_TURN 0x19

/* 裘娜, the unit the challenger asks for and the only one of the party left on
   the map if the offer is taken: PUSH 0x4 at 00037b8a.  Chapter 15 lays 9
   roster records down at unit indices 0..8 and she is the fifth of them. */
#define CH15_DUEL_HEROINE_UNIT_INDEX 4

/* The wave the challenger arrives in and how his tile is chosen: PUSH 0x2 at
   00037ba0 and XOR EAX,EAX / PUSH EAX at 00037b9d..00037b9f.  Zero is the
   value that does NOT take the placement record's tile as given -- it sends
   fdps_deploy_unit off to find the nearest unoccupied walkable tile to those
   coordinates and put him there instead. */
#define CH15_DUEL_WAVE 2
#define CH15_DUEL_PLACE_NEAREST_FREE_TILE 0

/* The speaker the offer is put under, PUSH 0x3 at 00037bd3: FACE.CEL record
   3. */
#define CH15_DUEL_SPEAKER_FACE_INDEX 3

/* The four lines of the scene, each a PUSH of its id into a draw: the
   challenge at 00037bc3, the question inside the standing panel at 00037bf0,
   the acceptance at 00037c2a and the refusal at 00037c9f.  They are entries of
   the chapter's own FDETXT15.TXT block, which
   fdps_load_field_chapter_resources leaves in
   data_fdps_current_chapter_text_ptr. */
#define CH15_DUEL_CHALLENGE_TEXT_ID 0x12
#define CH15_DUEL_QUESTION_TEXT_ID 0x13
#define CH15_DUEL_ACCEPTED_TEXT_ID 0x14
#define CH15_DUEL_DECLINED_TEXT_ID 0x15

/* The answer that accepts, CMP dword ptr [EBP+0x14],0x0 / JNZ at 00037c0d:
   fdps_prompt_two_choice's 0 is the left cell.  Its 1 and its -1 both fall
   into the other arm, so a cancel declines rather than accepting
   (msgwin.h). */
#define CH15_DUEL_ANSWER_ACCEPT 0

/* How many records the retire loop walks, CMP dword ptr [EBP+0x14],0x9 / JL at
   00037c41: unit indices 0..8, the roster block map14.dat's header byte +1
   sizes at 9. */
#define CH15_DUEL_ROSTER_UNIT_COUNT 9

/* The one unit outside that block taken off the map as well, PUSH 0x34 at
   00037c6f: map14.dat's wave-1 record, the level 20 archer 瑪麗安 that
   Icon14.dat deploys during the opening cutscene, sitting behind the 9 roster
   units and the 43 wave-0 records. */
#define CH15_DUEL_ARCHER_UNIT_INDEX 0x34

/* What each of those records' flags byte is left holding, MOV byte ptr
   [EAX+0x5],0x1 at 00037c69 and 00037c7f.  IT IS A WHOLE-BYTE STORE AND NOT AN
   OR: it raises the retired bit 0x01 that fdps_unit_is_retired reads and drops
   the has-acted bit 0x80 along with everything else the byte was holding. */
#define CH15_DUEL_RETIRED_FLAG_BYTE 1

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) an
   accepted duel raises, MOV byte ptr [0x000640e8],0x1 at 00037c83 -- element
   0x10 of the 32-entry array based at 0x000640d8, the slot this family's
   one-shot handlers share.  fdps_chapter_15_post_action and
   fdps_chapter_15_end both branch on it. */
#define CH15_DUEL_LATCH_SLOT 0x10

/* What the battle-end global is left holding on the two exits that end the
   chapter, MOV dword ptr [0x00069da0],0x2 at 00037caf and 00037cbb: the code
   that means the chapter is cleared. */
#define CH15_BATTLE_END_CHAPTER_CLEARED 2

/* 00037b70.  Chapter 15's boss-death event: the fortress cannon has fallen, so
   the chapter is over -- except that when it fell on turn 25 or earlier with
   裘娜 still standing, a challenger walks onto the cleared map first and offers
   her a duel.

   fdps_battle_destroy_remaining_enemies (btlend.h) runs first, unconditionally
   and ahead of both gates, zeroing the hit points of every unit still on the
   enemy side and playing them off the map.  That is why fdps_chapter_15_end,
   alone in its family, has no destroy call of its own.

   THE TWO GATES ARE ONE SHORT-CIRCUIT CHAIN, each failure reaching the same
   store: CMP dword ptr [0x00069ce8],0x19 / JG at 00037b81 and the TEST EAX,EAX
   / JZ at 00037b94 on what fdps_unit_is_retired(4) answered, both jumping to
   00037cbb.  The turn compare is signed and inclusive, so turn 25 itself still
   qualifies.

   Both gates passing, fdps_deploy_wave brings on the current map's wave 2.  In
   map14.dat that is deployment record 44 alone, the level 17 unit of enemy id
   0x23 the guide lists as the chapter's unnamed opponent, and it is appended
   as unit 0x35: 9 roster units, then the 43 wave-0 records the chapter start
   brings on, then the wave-1 unit Icon14.dat deploys during the opening
   cutscene.

   The offer is then put to the player: the challenge is written straight onto
   the visible page, the message panel is revealed under FACE.CEL record 3, the
   question is written into the panel's interior at 0xaa44a -- VGA offset
   0xa44a, row 131 column 138 -- fdps_prompt_two_choice runs the modal prompt,
   and the panel is retracted before the answer is looked at.

   THE ACCEPTED BRANCH MUST LEAVE data_fdps_chapter_event_or_battle_end_code
   ALONE.  Two of the three exits store the cleared code into it and the
   control flow invites folding that into one store at the end; do that and
   chapter 15's duel ends the chapter the instant the player says yes.  What
   the accepted branch writes instead is the retirement of everyone who is not
   one of the two duellists -- indices 0..8 except 裘娜's, then unit 0x34 --
   and the chapter-event flag byte, which is what
   fdps_chapter_15_post_action reads to settle the duel and award the
   妖刀村雨.

   Any other answer, the -1 fdps_prompt_two_choice returns on a cancel
   included, declines: the refusal line is written and the chapter is closed
   without the duel.

   Every one of the four draws hands back a cursor and nothing between the CALL
   and the next instruction reads EAX, so all four results are discarded;
   fdps_prompt_two_choice's is the only return value used, stored at 00037c05.
   Nothing sets EAX before the RET at 00037ccb and no dispatcher reads what
   comes back, so the result is void.

   unit_index is the handler table's shared parameter: the unit the death-script
   runner was acting on when the cannon died.  IT IS NEVER READ.  The slot is
   overwritten at 00037c05 with the prompt answer and again at 00037c3a as the
   retire loop's counter, so which unit the event fired for cannot reach
   anything this handler does.

   Table slot 21, and chapter 15's map14.dat is the only shipped file that
   names it -- as the death script of deployment record 27, the level 16 unit
   of enemy id 0x80 that is the chapter's victory condition.  That file carries
   no turn events and no tile triggers at all, so the death script is the only
   route to this slot. */
void fdps_chapter_15_event_boss_defeat(int unit_index)
{
    /* The record the two stores below are made through, [EBP-0x4]. */
    struct fdps_unit_record *unit;
    /* Which cell of the offer the player committed to: 0 accepts, 1 declines
       and -1 is a cancel.  The original keeps it in the argument slot at
       [EBP+0x14]; it is a local here, because what that slot holds from
       00037c05 onwards is an answer and not a unit index. */
    int duel_answer;

    fdps_battle_destroy_remaining_enemies();

    if (data_fdps_battle_turn_counter <= CH15_DUEL_LAST_TURN &&
        fdps_unit_is_retired(CH15_DUEL_HEROINE_UNIT_INDEX) == 0) {
        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CH15_DUEL_WAVE, CH15_DUEL_PLACE_NEAREST_FREE_TILE);

        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH15_DUEL_CHALLENGE_TEXT_ID,
                       (unsigned char *) CH15_DUEL_SCREEN_TEXT_DEST,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_message_window_open(CH15_DUEL_SPEAKER_FACE_INDEX);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH15_DUEL_QUESTION_TEXT_ID,
                       (unsigned char *) PANEL_TEXT_ORIGIN,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        duel_answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (duel_answer == CH15_DUEL_ANSWER_ACCEPT) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH15_DUEL_ACCEPTED_TEXT_ID,
                           (unsigned char *) CH15_DUEL_SCREEN_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);

            /* The argument slot is the counter, as the assembly has it. */
            for (unit_index = 0;
                 unit_index < CH15_DUEL_ROSTER_UNIT_COUNT;
                 unit_index++) {
                if (unit_index != CH15_DUEL_HEROINE_UNIT_INDEX) {
                    unit = fdps_get_unit_record(unit_index);
                    unit->flags = CH15_DUEL_RETIRED_FLAG_BYTE;
                }
            }
            unit = fdps_get_unit_record(CH15_DUEL_ARCHER_UNIT_INDEX);
            unit->flags = CH15_DUEL_RETIRED_FLAG_BYTE;

            data_fdps_map_cell_event_triggered_flags[CH15_DUEL_LATCH_SLOT] = 1;
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH15_DUEL_DECLINED_TEXT_ID,
                           (unsigned char *) CH15_DUEL_SCREEN_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            data_fdps_chapter_event_or_battle_end_code =
                CH15_BATTLE_END_CHAPTER_CLEARED;
        }
    } else {
        data_fdps_chapter_event_or_battle_end_code =
            CH15_BATTLE_END_CHAPTER_CLEARED;
    }
}

/* The speaker under whose portrait the whole scene is played: FACE.CEL record
   129, PUSH 0x81 in front of every one of the ten window opens. */
#define CH16_SMITH_FACE_INDEX 0x81

/* The only unit that can spring the event, CMP dword ptr [EBP+0x14],0x0 at
   00037ce3.  Battle unit 0 is Randis, always the first unit deployed, and the
   swords the smith asks after are his. */
#define CH16_SMITH_UNIT_INDEX 0

/* The last battle turn the event still fires on: CMP dword ptr
   [0x00069ce8],0x14 / JLE at 00037cf4, a signed inclusive compare, so turn 20
   is inside the window and turn 21 is not. */
#define CH16_SMITH_LAST_TURN 0x14

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) this
   event latches: byte ptr [0x000640e8], element 0x10 of the 32-entry array
   based at 0x000640d8 -- the same slot the one-shot handlers of this family
   share, and the first the map's own event codes cannot reach. */
#define CH16_SMITH_LATCH_SLOT 0x10

/* The two swords the smith reacts to and the line he speaks for each, PUSH
   0x58 at 00037d3e with MOV [EBP-0x4],0xd at 00037d53, and PUSH 0x59 at
   00037d5c with MOV [EBP-0x4],0xe at 00037d71.  Item 0x58 is 修佩魯 and 0x59
   is 雷德 (assets/items.md); the line id doubles as which of them was found,
   because the frame slot starts at 0 and only these two writes ever change
   it. */
#define CH16_REFORGEABLE_SWORD_ITEM_ID 0x58
#define CH16_BREAKING_SWORD_ITEM_ID 0x59
#define CH16_REFORGEABLE_SWORD_REPLY 0x0d
#define CH16_BREAKING_SWORD_REPLY 0x0e

/* The state the reply slot is left in when Randis carries neither sword, MOV
   dword ptr [EBP-0x4],0x0 at 00037cdc, and the test that reads it back at
   00037d78.  Zero is not a text id here, it is "no sword found". */
#define CH16_NO_SWORD_FOUND 0

/* What fdps_unit_find_item_slot answers when the unit is not carrying the item
   at all, the -1 both compares at 00037d4d and 00037d6b are against. */
#define CH16_ITEM_NOT_CARRIED (-1)

/* The lines of the scene that are not one of the two replies above, each a
   PUSH of its id into the draw: the greeting at 00037d22, the offer question at
   00037dd9, the parting line at 00037fff and 00037fbe, the two forging lines at
   00037e20 and 00037e57, the finished sword at 00037e9e, the failure at
   00037ee7, the second question at 00037f26 and the seal at 00037f69.  They are
   entries of the chapter's own FDETXT16.TXT block, which carries 23 strings, so
   0x16 is its last. */
#define CH16_GREETING_TEXT_ID 0x0c
#define CH16_OFFER_QUESTION_TEXT_ID 0x0f
#define CH16_PARTING_TEXT_ID 0x10
#define CH16_FORGE_OPENING_TEXT_ID 0x11
#define CH16_FORGE_WORKING_TEXT_ID 0x12
#define CH16_FORGE_SUCCEEDED_TEXT_ID 0x13
#define CH16_FORGE_FAILED_TEXT_ID 0x14
#define CH16_COMPENSATION_QUESTION_TEXT_ID 0x15
#define CH16_SEAL_GIVEN_TEXT_ID 0x16

/* The answer that accepts: fdps_prompt_two_choice's 0 is the left option and
   the only value either test here matches (CMP dword ptr [EBP+0x14],0x0 / JNZ
   at 00037df6 and 00037f43).  Its 1 and its -1 both fall into the other arm, so
   a cancel declines rather than accepting (msgwin.h). */
#define CH16_ANSWER_ACCEPT 0

/* What the sword becomes, what the failed forging pays and what it hands over
   instead: PUSH 0xa0 at 00037eb3 -- 灼烈之劍 -- ADD dword ptr
   [0x000643a4],0x1388 at 00037efc, PUSH 0xa9 at 00037f7e -- 神的聖印 -- and
   PUSH 0xa3 at 00037f8f -- 金屬礦 (assets/items.md). */
#define CH16_REFORGED_SWORD_ITEM_ID 0xa0
#define CH16_COMPENSATION_GOLD 0x1388
#define CH16_SEAL_ITEM_ID 0xa9
#define CH16_ORE_ITEM_ID 0xa3

/* 00037cd0.  Chapter 16's wandering-smith event: Randis has ended his turn on
   the smith's tile, and if he is still carrying one of the two named swords
   inside the first twenty battle turns the smith offers to reforge it --
   修佩魯 comes back as 灼烈之劍, 雷德 breaks and is paid off with 5000 gold
   and the player's pick of 神的聖印 or 金屬礦.

   THE THREE GATES ARE ONE SHORT-CIRCUIT CHAIN and the whole body is skipped
   unless all three pass: CMP dword ptr [EBP+0x14],0x0 / JNZ at 00037ce3, CMP
   byte ptr [0x000640e8],0x0 / JZ at 00037ce9 and CMP dword ptr
   [0x00069ce8],0x14 / JLE at 00037cf4, each failure jumping to the same exit at
   00038014.  The turn compare is signed and inclusive.

   Every line is spoken the same way -- fdps_message_window_open(0x81),
   fdps_draw_text into the standing panel, fdps_message_window_close -- and each
   of the ten is a separate copy of those calls in the instruction stream.  The
   two questions are the exception: the panel carrying message 0xf and the one
   carrying 0x15 are left standing while fdps_prompt_two_choice runs, and are
   retracted only after the answer comes back (CALL 00017990 at 00037de9 and
   00037f36, each followed by the close at 00037df1 and 00037f3e).

   THE LATCH IS RAISED AFTER THE GREETING AND BEFORE THE INVENTORY IS SEARCHED,
   at 00037d37, so a Randis who reaches the tile carrying neither sword burns
   the encounter for the rest of the chapter (chevt3.h).

   The two lookups are an else-chain, not two independent tests: a hit on
   0x58 sets the reply to 0xd and jumps over the second lookup at 00037d5a, so
   0x59 is only asked after 0x58 has missed, and the slot the second lookup
   leaves is the one used.  With neither sword the reply is still 0 and the
   handler returns at 00037d7c.

   The three inventory calls and the stat rebuild all push a literal 0 rather
   than the argument (00037e6e, 00037eb8, 00037f83, 00037f8f and 00037fd3);
   the entry gate has already forced the two to be equal.

   Both prompt answers land in the incoming argument slot at 00037dee and
   00037f3b, which is dead from the entry test onwards; they are a local here,
   because what the slot holds after the first store is an answer and not a unit
   index.

   fdps_draw_text hands back a cursor and fdps_unit_add_item an "it fitted"
   flag; nothing between either CALL and the next instruction reads EAX, so both
   results are discarded.  Nothing sets EAX before the RET at 0003801a and no
   dispatcher reads what comes back, so the result is void.

   unit_index is the handler table's shared parameter: the battle unit that
   ended its turn on the trigger tile.  It is read once, by the first gate, and
   never again. */
void fdps_chapter_16_event_wandering_smith_forge(int unit_index)
{
    /* Which line the smith speaks about what he was handed, and which sword
       that is: 0xd for 修佩魯, 0xe for 雷德, and 0 while neither has been
       found. */
    int smith_reply_text_id;
    /* Which of the eight inventory entries that sword sits in, as
       fdps_unit_find_item_slot answers it, or -1 for not carried. */
    int sword_slot;
    /* The answer to the question just asked: 0 accepts, 1 declines, -1 is a
       cancel.  The same slot carries both questions' answers. */
    int answer;

    smith_reply_text_id = CH16_NO_SWORD_FOUND;

    if (unit_index == CH16_SMITH_UNIT_INDEX &&
        data_fdps_map_cell_event_triggered_flags[CH16_SMITH_LATCH_SLOT] == 0 &&
        data_fdps_battle_turn_counter <= CH16_SMITH_LAST_TURN) {
        fdps_message_window_open(CH16_SMITH_FACE_INDEX);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CH16_GREETING_TEXT_ID,
                       (unsigned char *) PANEL_TEXT_ORIGIN, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_message_window_close();

        data_fdps_map_cell_event_triggered_flags[CH16_SMITH_LATCH_SLOT] = 1;

        sword_slot = fdps_unit_find_item_slot(CH16_SMITH_UNIT_INDEX,
                                              CH16_REFORGEABLE_SWORD_ITEM_ID);
        if (sword_slot == CH16_ITEM_NOT_CARRIED) {
            sword_slot = fdps_unit_find_item_slot(CH16_SMITH_UNIT_INDEX,
                                                  CH16_BREAKING_SWORD_ITEM_ID);
            if (sword_slot != CH16_ITEM_NOT_CARRIED) {
                smith_reply_text_id = CH16_BREAKING_SWORD_REPLY;
            }
        } else {
            smith_reply_text_id = CH16_REFORGEABLE_SWORD_REPLY;
        }

        if (smith_reply_text_id != CH16_NO_SWORD_FOUND) {
            fdps_message_window_open(CH16_SMITH_FACE_INDEX);
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           smith_reply_text_id,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            fdps_message_window_close();

            fdps_message_window_open(CH16_SMITH_FACE_INDEX);
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH16_OFFER_QUESTION_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            answer = fdps_prompt_two_choice();
            fdps_message_window_close();

            if (answer == CH16_ANSWER_ACCEPT) {
                fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                fdps_draw_text(data_fdps_current_chapter_text_ptr,
                               CH16_FORGE_OPENING_TEXT_ID,
                               (unsigned char *) PANEL_TEXT_ORIGIN,
                               VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                               MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                fdps_message_window_close();

                fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                fdps_draw_text(data_fdps_current_chapter_text_ptr,
                               CH16_FORGE_WORKING_TEXT_ID,
                               (unsigned char *) PANEL_TEXT_ORIGIN,
                               VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                               MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                fdps_message_window_close();

                fdps_unit_remove_item(CH16_SMITH_UNIT_INDEX, sword_slot);

                if (smith_reply_text_id == CH16_REFORGEABLE_SWORD_REPLY) {
                    fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                                   CH16_FORGE_SUCCEEDED_TEXT_ID,
                                   (unsigned char *) PANEL_TEXT_ORIGIN,
                                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                                   MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                    fdps_message_window_close();

                    fdps_unit_add_item(CH16_SMITH_UNIT_INDEX,
                                       CH16_REFORGED_SWORD_ITEM_ID);
                } else {
                    fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                                   CH16_FORGE_FAILED_TEXT_ID,
                                   (unsigned char *) PANEL_TEXT_ORIGIN,
                                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                                   MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                    fdps_message_window_close();

                    data_fdps_shared_party_total_gold =
                        data_fdps_shared_party_total_gold +
                        CH16_COMPENSATION_GOLD;

                    fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                                   CH16_COMPENSATION_QUESTION_TEXT_ID,
                                   (unsigned char *) PANEL_TEXT_ORIGIN,
                                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                                   MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                    answer = fdps_prompt_two_choice();
                    fdps_message_window_close();

                    if (answer == CH16_ANSWER_ACCEPT) {
                        fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                                       CH16_SEAL_GIVEN_TEXT_ID,
                                       (unsigned char *) PANEL_TEXT_ORIGIN,
                                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                                       MESSAGE_BG_COLOR,
                                       MESSAGE_OUTLINE_COLOR);
                        fdps_message_window_close();

                        fdps_unit_add_item(CH16_SMITH_UNIT_INDEX,
                                           CH16_SEAL_ITEM_ID);
                    } else {
                        fdps_unit_add_item(CH16_SMITH_UNIT_INDEX,
                                           CH16_ORE_ITEM_ID);

                        fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                                       CH16_PARTING_TEXT_ID,
                                       (unsigned char *) PANEL_TEXT_ORIGIN,
                                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                                       MESSAGE_BG_COLOR,
                                       MESSAGE_OUTLINE_COLOR);
                        fdps_message_window_close();
                    }
                }

                fdps_unit_recompute_combat_stats(CH16_SMITH_UNIT_INDEX);
            } else {
                fdps_message_window_open(CH16_SMITH_FACE_INDEX);
                fdps_draw_text(data_fdps_current_chapter_text_ptr,
                               CH16_PARTING_TEXT_ID,
                               (unsigned char *) PANEL_TEXT_ORIGIN,
                               VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                               MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
                fdps_message_window_close();
            }
        }
    }
}

/* 00038020.  Chapter 16's turn-scheduled event: one block of the map's enemies
   stops holding position and starts advancing on the party, and which block
   depends on the turn the counter is standing at.

   The whole body is a test on data_fdps_battle_turn_counter followed by one of
   two copies of the same inline expansion the chapter 15 handler above carries
   -- fdps_object_set_field34_low_nibble_range (00036b60) with a constant
   argument triple.  The turn-5 copy stages (0x19, 0x22, 0) at [EBP-0x20],
   [EBP-0x1c] and [EBP-0x18] (0003803c..0003804a) and the other stages
   (0x0a, 0x19, 0) at [EBP-0x24], [EBP-0x28] and [EBP-0x2c]
   (0003809e..000380ac); each then copies its triple into a second set of slots
   before seeding the counter from the first of them, which is that helper's
   fingerprint.  There is no CALL to the helper in the body -- the only CALL in
   either loop is fdps_get_unit_record, at 0003807b and 000380dd, once per
   iteration -- so writing either range as a call to it would put a CALL in the
   rebuild that the original does not make.

   The test at 00038033 is CMP dword ptr [0x00069ce8],0x5 / JNZ 0003809e, an
   equality on the turn counter with the second range as the fall-through, so
   only turn 5 reaches the first loop and every other turn reaches the second.
   Both compares between the counter and the top bound -- CMP EAX,[EBP-0x10] /
   JLE at 0003806c and CMP EAX,[EBP-0x34] / JLE at 000380ce -- are signed and
   inclusive, so 0x22 and 0x19 are the last indices written in their loops, not
   one past the end.  Index 0x19 is the top of one range and the bottom of the
   other and is released by whichever branch runs.

   Chapter 16's map15.dat lays 10 player records down at 0..9 and its 25 enemy
   deployment records at 0x0a..0x22, so unit index = deployment record index +
   10 and the two ranges are records 15..24 and 0..15.  Nothing is range
   checked and data_fdps_map_unit_count is not consulted; all four bounds are
   literals in the instruction stream.

   Each merge is the same read-modify-write of the one byte as the chapter 15
   handler's -- MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x14] / OR DH,DL
   / MOV [EAX+0x34],DH at 00038089..00038097, and the same five at
   000380eb..000380f9 -- so the behaviour code goes to 0 and the two AI flag
   bits in the high nibble are carried across untouched.

   The record pointer comes back in EAX from each CALL and is stored to
   [EBP-0x4] (00038083) or [EBP-0x40] (000380e5), then re-read for the load and
   again for the store, so both halves of each merge address the record that
   iteration fetched.

   There is no one-shot latch: the only compare in the body is the one on the
   turn counter, and nothing records that a branch has run.  The map's own turn
   table is what makes each branch happen once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003802c writes zero over the incoming slot before the
   counter is read and nothing ever reads it back, so which unit the event
   fired for cannot reach anything this handler does; the store has no
   observable effect, because the slot belongs to the caller's outgoing
   argument area and the turn-event runner drops it with ADD ESP,0x4 at
   0002e146.

   Nothing sets EAX before the RET at 00038104 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_16_event_enemies_advance_for_turn(int unit_index)
{
    struct fdps_unit_record *unit;
    int advancing_unit_index;

    unit_index = 0;

    if (data_fdps_battle_turn_counter == 5) {
        for (advancing_unit_index = 0x19;
             advancing_unit_index <= 0x22;
             advancing_unit_index++) {
            unit = fdps_get_unit_record(advancing_unit_index);
            unit->ai_behavior = (unsigned char)
                ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    } else {
        for (advancing_unit_index = 0x0a;
             advancing_unit_index <= 0x19;
             advancing_unit_index++) {
            unit = fdps_get_unit_record(advancing_unit_index);
            unit->ai_behavior = (unsigned char)
                ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* What the chapter 17 handler below takes off the battle turn counter to get
   the wave it asks for: SUB EAX,0x7 at 0003812b, on the dword loaded from
   data_fdps_battle_turn_counter one instruction earlier.  The counter is
   1-based and the turn-event runner fires while it still holds the turn whose
   phase has just ended, so the map's turn 8 record asks for wave 1 and its
   turn 9 record for wave 2. */
#define CH17_WAVE_TURN_OFFSET 7

/* How that wave is placed: XOR EAX,EAX / PUSH EAX at 00038123, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves.  Wave 0, the group a map opens with, is the
   one deployed with this flag set. */
#define CH17_PLACE_EXACT 0

/* 00038110.  Chapter 17's turn-scheduled reinforcement event: brings on the
   wave of the current map's deployment table that is due for the turn just
   played.

   The whole body is one call.  The frame is the standard Watcom four-push one
   with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP /
   MOV EBP,ESP / SUB ESP,0x0 at 00038110..00038116 -- so there is no local
   here at all and the three arguments are computed straight into the pushes:
   XOR EAX,EAX / PUSH EAX, then MOV EAX,[0x00069ce8] / SUB EAX,0x7 / PUSH EAX,
   then PUSH dword ptr [0x00069cf4], at 00038123..0003812f.  The caller-cleans
   ADD ESP,0xc at 0003813a is this function's own, which is what makes the
   convention the stack one.

   The map number is data_fdps_chapter_current_chapter_id read at the call site
   and not anything this handler holds, so it is whichever chapter is loaded --
   the same way the chapter 10 ambush in chevt2.c reads it.

   The wave key is the raw subtraction with nothing on either side of it: no
   compare, no table and no lower bound.  Adding the guard that looks obvious
   would change behaviour rather than protect it, because a turn below 8 gives
   a negative key, which matches no deployment record -- fdps_deploy_wave
   compares an unsigned wave byte against this int -- while a key clamped to 0
   would match the map's whole opening army and deploy it a second time.

   Nothing guards the call and nothing records that it ran, so the handler
   fires its wave every time it is reached; what makes each wave arrive once is
   the map's turn table naming the slot once per turn.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003811c writes zero over the incoming slot before the
   counter is read and nothing ever reads it back, so which unit the event
   fired for cannot reach anything this handler does; the store has no
   observable effect, because the slot belongs to the caller's outgoing
   argument area and the turn-event runner drops it with ADD ESP,0x4 at
   0002e146.

   Nothing sets EAX between the CALL's return and the RET at 00038141, and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_17_event_deploy_wave_for_turn(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     data_fdps_battle_turn_counter - CH17_WAVE_TURN_OFFSET,
                     CH17_PLACE_EXACT);
}

/* The place_exact argument the chapter 18 handler below hands
   fdps_deploy_wave: XOR EAX,EAX / PUSH EAX at 00038163, so zero.  Zero is the
   value that does NOT take the placement record's tile as given -- it sends
   fdps_deploy_unit off to search the map for the nearest unoccupied walkable
   tile to those coordinates and put the unit there instead. */
#define CH18_PLACE_NEAREST_FREE_TILE 0

/* 00038150.  Chapter 18's turn-scheduled reinforcement event: brings on the
   wave of the current map's deployment table whose number is the battle turn
   counter's own value.

   The whole body is one call.  The frame is the standard Watcom four-push one
   with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI / PUSH EBP /
   MOV EBP,ESP / SUB ESP,0x0 at 00038150..00038156 -- so there is no local here
   at all and the three arguments are computed straight into the pushes:
   XOR EAX,EAX / PUSH EAX, then PUSH dword ptr [0x00069ce8], then PUSH dword
   ptr [0x00069cf4], at 00038163..0003816c.  The caller-cleans ADD ESP,0xc at
   00038177 is this function's own, which is what makes the convention the
   stack one.

   This is the chapter 17 handler above with the subtraction taken out, and the
   absence of it is the whole difference: the turn counter is pushed as it
   stands, with no offset, no compare, no table and no bound on either side of
   it, so the wave asked for is exactly the number the counter holds.  The
   shipped data is what that arrangement is built around.  map17.dat -- map
   index 17, the player's chapter 18 -- schedules slot 25 for side 0, the enemy
   phase, on turns 4, 5, 6, 7, 8, 9, 10, 11 and 13, and its 65 deployment
   records carry four units tagged with each of waves 4, 5, 6, 7, 8, 9, 10 and
   11 and fifteen tagged wave 13.  Wave number and turn number are the same set
   of nine values, which is why no adjustment is wanted here and why putting
   chapter 17's offset back in would deploy nothing on any of the nine.

   The map number is data_fdps_chapter_current_chapter_id read at the call site
   and not anything this handler holds, so it is whichever chapter is loaded.

   Nothing guards the call and nothing records that it ran, so the handler
   fires its wave every time it is reached; what makes each wave arrive once is
   the map's turn table naming the slot once per turn.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003815c writes zero over the incoming slot before either
   global is read and nothing ever reads it back, so which unit the event fired
   for cannot reach anything this handler does; the store has no observable
   effect, because the slot belongs to the caller's outgoing argument area and
   the turn-event runner drops it with ADD ESP,0x4 at 0002e146.

   Nothing sets EAX between the CALL's return and the RET at 0003817e, and no
   dispatcher reads what comes back, so the result is void. */
void fdps_chapter_18_event_deploy_wave_for_turn(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     data_fdps_battle_turn_counter,
                     CH18_PLACE_NEAREST_FREE_TILE);
}

/* The wave the chapter 19 handler below brings on, PUSH 0x1 at 00038196.  It
   is a literal and not the turn counter: the counter is never read anywhere in
   the body, which is the whole difference between this handler and the two
   above it.  Wave 1 of map18.dat is a single deployment record, its index 42:
   side 2, character id 0x0b, level 2, items 0x29 and 0x6a in its two equipped
   slots and nothing carried.  The wave number is how that one unit is named. */
#define CH19_LANCELOT_WAVE 1

/* The place_exact argument the handler hands fdps_deploy_wave: XOR EAX,EAX /
   PUSH EAX at 00038193, so zero.  Zero is the value that does NOT take the
   placement record's tile as given -- it sends fdps_deploy_unit off to search
   the map for the nearest unoccupied walkable tile to those coordinates and
   put the unit there instead, which is what keeps the arriving paladin off a
   tile the party is already standing on. */
#define CH19_PLACE_NEAREST_FREE_TILE 0

/* The entry of the chapter's own text block the arrival line is spoken from,
   PUSH 0xa at 000381b9. */
#define CH19_ARRIVAL_TEXT_ID 10

/* The destination the handler hands fdps_draw_text, PUSH 0xa0000 at 000381b4:
   the top-left corner of the visible page.  It stays a literal because it is
   an address inside the display adapter's aperture rather than the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E).

   Nothing is drawn there.  Entry 10 of the chapter's FDETXT19.TXT opens with
   the two tokens -0x11 and 0x0b -- speaker by character id, and the character
   id of the very record wave 1 deploys -- and that speaker token overwrites
   fdps_draw_text's own pen with the message panel's origin before a single
   glyph is painted (text.h), so this value only decides where an entry
   carrying no such token would start. */
#define CH19_ARRIVAL_TEXT_DEST 0x000a0000

/* 00038180.  Chapter 19's turn-scheduled arrival event: the paladin 蘭斯洛特
   joins the party in the middle of the battle, deployed onto the map as a
   player-side unit and then speaking his arrival line under his own portrait.

   The body is two calls and nothing else.  The frame is the standard Watcom
   four-push one with an empty local area -- PUSH EBX / PUSH ESI / PUSH EDI /
   PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 at 00038180..00038186 -- so there is no
   local here at all and every argument of both calls is either a literal
   pushed straight or a global read at the push.  The two caller-cleans, ADD
   ESP,0xc at 000381a3 and ADD ESP,0x1c at 000381c6, are this function's own,
   which is what makes the convention the stack one.

   THE DEPLOYMENT HAS TO COME FIRST AND THE ORDER IS NOT COSMETIC.  Entry 10
   of the chapter's FDETXT19.TXT begins with the tokens -0x11 and 0x0b, and the
   first of them sends fdps_draw_text off to
   fdps_battle_find_unit_by_character_id to find character 0x0b among the units
   standing on the map and raise his portrait from the record it finds --
   0x0b being the character id of the one deployment record wave 1 carries, so
   the unit the line looks for is the unit the line above brought on.  With the
   deployment not yet done there is no such unit and no portrait to raise.
   Swapping the two calls is the one rewrite of this function that compiles and
   reads the same and does not behave the same.

   This is the sibling of the two reinforcement handlers above it with the
   wave key made a literal: the turn counter is not read here at all, so the
   wave asked for is 1 on whatever turn the handler is reached.  The map
   number is data_fdps_chapter_current_chapter_id read at the call site, so it
   is whichever chapter is loaded rather than anything this handler holds, and
   a placement flag of 0 puts the unit on the nearest free walkable tile to the
   coordinates its MAP%02d.COD record names.

   fdps_draw_text hands back the cursor it stopped at.  Nothing here reads it:
   EAX is not touched between the CALL at 000381c1 and the RET at 000381cd, and
   no dispatcher reads what comes back either, so the result is void.

   There is no one-shot latch and nothing records that the handler has run.
   Calling it twice deploys wave 1 twice, because the wave walk appends and
   never checks whether those records are already on the map; the map's turn
   table naming the slot once is what makes the paladin arrive once.

   unit_index is the handler table's shared parameter.  MOV dword ptr
   [EBP+0x14],0x0 at 0003818c writes zero over the incoming slot before either
   call and nothing ever reads it back, so which unit the event fired for
   cannot reach anything this handler does; the store has no observable effect,
   because the slot belongs to the caller's outgoing argument area and the
   turn-event runner drops it with ADD ESP,0x4 at 0002e146.

   Table slot 26, and chapter 19's map18.dat is the only shipped thing that
   names it, with the single turn-event record {turn 6, slot 26, phase 2} --
   phase 2 being the one fdps_battle_advance_turn runs at the top of a player
   phase, just after it has incremented the turn counter.  No tile trigger,
   terrain cell or death script in any MAP*.DAT reaches the slot, so turn 6 of
   chapter 19 is the only way in. */
void fdps_chapter_19_event_lancelot_joins(int unit_index)
{
    unit_index = 0;

    fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                     CH19_LANCELOT_WAVE,
                     CH19_PLACE_NEAREST_FREE_TILE);
    fdps_draw_text(data_fdps_current_chapter_text_ptr,
                   CH19_ARRIVAL_TEXT_ID,
                   (unsigned char *) CH19_ARRIVAL_TEXT_DEST,
                   VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                   MESSAGE_OUTLINE_COLOR);
}

/* The element of data_fdps_map_cell_event_triggered_flags the ambush below
   latches: the byte at 0x000640e8, which is 0x10 past the block's base at
   0x000640d8 -- CMP byte ptr [0x000640e8],0x0 at 000381dc and MOV byte ptr
   [0x000640e8],0x1 at 000382d9.  It is the same slot the chapter 16 smith
   scene above and the chapter 10 ambush in chevt2.c use, because the block is
   cleared at every chapter start and only one chapter is ever loaded. */
#define CH19_AMBUSH_LATCH_SLOT 0x10

/* The wave the ambush brings on, PUSH 0x6 at 00038212, matched against byte
   0x15 of each 0x1a-byte deployment record of the resident MAP%02d.DAT block.
   Twenty-two of MAP18.DAT's records carry it: fourteen of character id 0x60 at
   level 17 and eight of character id 0x6d at level 15. */
#define CH19_AMBUSH_WAVE 6

/* How those twenty-two are placed: XOR EAX,EAX / PUSH EAX at 0003820f, so
   fdps_deploy_wave passes 0 on to fdps_deploy_unit and each unit goes on the
   nearest free walkable tile to its placement record's coordinates rather than
   on the coordinates themselves. */
#define CH19_AMBUSH_PLACE_NEAREST_FREE_TILE 0

/* The two values the handler leaves in data_fdps_map_cursor_draw_mode: 0
   before the deployment (MOV dword ptr [0x00069cd0],0x0 at 00038205), which
   matches none of fdps_draw_map_cursor's cases so nothing is painted for the
   whole sequence, and 1 after the last pan (MOV dword ptr [0x00069cd0],0x1 at
   000382ac), which is the plain box.  The second is a literal and not the mode
   the handler found on entry. */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_BOX 1

/* The three world pixels the view is walked to, in the order the pans visit
   them: PUSH 0x0 / PUSH 0x108 at 00038222, PUSH 0x210 / PUSH 0x0 at 0003824f
   and PUSH 0x210 / PUSH 0x300 at 0003827c.  fdps_map_cursor_move_to takes
   world pixels, not tiles, so at the 24-pixel tile step they are tiles
   (11, 0), (0, 22) and (32, 22) -- the top edge, the left flank and the right
   flank, the three areas map18.cod's wave-6 spawn points lie in.  The last two
   are past the far edge of chapter 19's playfield; the cursor still ends on
   them and it is the view that stops at the map's own boundary. */
#define CH19_AMBUSH_TOP_EDGE_WORLD_X 0x108
#define CH19_AMBUSH_TOP_EDGE_WORLD_Y 0
#define CH19_AMBUSH_LEFT_FLANK_WORLD_X 0
#define CH19_AMBUSH_LEFT_FLANK_WORLD_Y 0x210
#define CH19_AMBUSH_RIGHT_FLANK_WORLD_X 0x300
#define CH19_AMBUSH_RIGHT_FLANK_WORLD_Y 0x210

/* How long the view rests on each of the three, the CMP against 0xc at
   00038238, 00038265 and 00038295.  Each composed frame costs one timer tick
   inside fdps_render_view_frame, so twelve is the length of the pause and not
   a repaint count. */
#define CH19_AMBUSH_HOLD_FRAMES 0xc

/* The entry of the chapter's own text block the ambush line is spoken from,
   PUSH 0x13 at 000382c9. */
#define CH19_AMBUSH_TEXT_ID 0x13

/* The destination the handler hands fdps_draw_text, PUSH 0xa0000 at 000382c4:
   the top-left corner of the visible page.  It stays a literal because it is
   an address inside the display adapter's aperture rather than the address of
   anything the linker places (rebuild_info/pitfalls.md, contract E). */
#define CH19_AMBUSH_TEXT_DEST 0x000a0000

/* 000381d0.  Chapter 19's flank ambush: the first unit that is not on side 0
   to finish a step onto the map's trigger tile brings on the twenty-two
   enemies tagged wave 6, the view is panned over the three places they arrive
   and the chapter's line about them is spoken.

   The frame is the family's four-push one with a single 4-byte local -- PUSH
   EBX / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x4 at
   000381d0..000381d6 -- and that local is the unit record pointer at [EBP-4].
   Every caller-clean in the body is this function's own (ADD ESP,0x4 after the
   record lookup, ADD ESP,0xc after the deployment, ADD ESP,0x8 after each
   cursor move and ADD ESP,0x1c after the draw), the RET at 000382e6 carries no
   immediate, and the dispatcher pushes one dword and drops it with ADD ESP,0x4
   at 0001583f, so the convention is the stack one at both ends of the call.

   THE TWO GATES ARE ORDERED AND THE SECOND ONE COSTS A CALL.  CMP byte ptr
   [0x000640e8],0x0 / JNZ at 000381dc jumps straight to the epilogue, so a
   latch that is already up means fdps_get_unit_record is never reached; only
   when it is clear is the record fetched and CMP byte ptr [EAX+0x6],0x0 / JZ
   at 000381fb taken.  Writing the pair as one condition over a record fetched
   up front -- which is what the chapter 10 ambush in chevt2.c really does --
   would call into the unit array on a firing that is already spent.

   The side test is JZ over the whole byte, so it is a plain "not 0" and not a
   sign test: side 0 is the enemy, 1 the guest and 2 the player's own roster,
   and what the gate keeps out is an enemy springing the chapter's own ambush.

   THE CURSOR MODE IS SET TWICE AND THE SECOND STORE IS A LITERAL.  The 0 that
   goes in before the deployment hides the cursor for the whole sequence, and
   it also decides how much is drawn: fdps_map_cursor_move_to composes a frame
   at a step only when the cursor is visible or the view scrolled, so with the
   mode at 0 the three pans compose a frame only on the steps that dragged the
   view.  The 1 that goes in after the last pan is the plain box and not
   whatever the mode held on entry, so the obvious save/restore pair around the
   pans changes what the cursor wears once the ambush is over.

   THE DEPLOYMENT COMES BEFORE THE PANS and the order is what the player sees:
   the twenty-two are standing on the map before the view starts crossing the
   places they arrived at.  The map number is
   data_fdps_chapter_current_chapter_id read at the call site and not anything
   this handler holds, so it is whichever chapter is loaded.

   THE FRAME COUNTER IS THE ARGUMENT SLOT.  MOV dword ptr [EBP+0x14],0x0 at
   00038231, 0003825e and 0003828e writes zero over the incoming argument and
   each loop compares and INCs that same slot, so the counter and the parameter
   are one storage location -- the one local the frame has is spent on the unit
   record pointer.  The store has no observable effect on the caller, because
   the slot belongs to its outgoing argument area and the dispatcher drops it
   with its own ADD ESP,0x4.  All three loops are the -od shape of a for
   statement: the compare at the top, a dead MOV EAX,[EBP+0x14] ahead of the
   INC, and the body reached by a JL past the exit jump.  All three are signed
   and all three stop at 12.

   Only one value is used after a CALL: fdps_get_unit_record's, stored to
   [EBP-0x4] at 000381f5 and reloaded at 000381f8 for the one byte the gate
   reads.  fdps_deploy_wave, fdps_map_cursor_move_to and fdps_render_view_frame
   leave nothing this body reads, and fdps_draw_text's cursor is discarded --
   the next instruction is the latch store.  Nothing sets EAX before the RET
   and no dispatcher reads what comes back, so the result is void.

   The latch is raised last, after the line has been spoken, which is the
   opposite end of the body from the chapter 10 ambush's and from the chapter
   16 smith scene's: nothing this handler calls can re-enter it, so the two
   placements are indistinguishable from outside and the emitted one is where
   the original puts it. */
void fdps_chapter_19_event_deploy_wave_6(int unit_index)
{
    struct fdps_unit_record *triggering_unit;

    if (data_fdps_map_cell_event_triggered_flags[CH19_AMBUSH_LATCH_SLOT]
        == 0) {
        triggering_unit = fdps_get_unit_record(unit_index);
        if (triggering_unit->side != 0) {
            data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;

            fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                             CH19_AMBUSH_WAVE,
                             CH19_AMBUSH_PLACE_NEAREST_FREE_TILE);

            fdps_map_cursor_move_to(CH19_AMBUSH_TOP_EDGE_WORLD_X,
                                    CH19_AMBUSH_TOP_EDGE_WORLD_Y);
            /* The argument slot is the counter, as the assembly has it. */
            for (unit_index = 0;
                 unit_index < CH19_AMBUSH_HOLD_FRAMES;
                 unit_index++) {
                fdps_render_view_frame();
            }

            fdps_map_cursor_move_to(CH19_AMBUSH_LEFT_FLANK_WORLD_X,
                                    CH19_AMBUSH_LEFT_FLANK_WORLD_Y);
            for (unit_index = 0;
                 unit_index < CH19_AMBUSH_HOLD_FRAMES;
                 unit_index++) {
                fdps_render_view_frame();
            }

            fdps_map_cursor_move_to(CH19_AMBUSH_RIGHT_FLANK_WORLD_X,
                                    CH19_AMBUSH_RIGHT_FLANK_WORLD_Y);
            for (unit_index = 0;
                 unit_index < CH19_AMBUSH_HOLD_FRAMES;
                 unit_index++) {
                fdps_render_view_frame();
            }

            data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CH19_AMBUSH_TEXT_ID,
                           (unsigned char *) CH19_AMBUSH_TEXT_DEST,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            data_fdps_map_cell_event_triggered_flags[CH19_AMBUSH_LATCH_SLOT] =
                1;
        }
    }
}
