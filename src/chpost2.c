/* chpost2.c -- the per-chapter post-action handlers, chapters 16 to 30: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots 15 to 29 of the handler table based at 0006028c, indexed by
 * the 0-based chapter id and called only through it, so none of the four
 * battle dispatchers appears as a static caller.
 *
 * See chpost2.h for what each handler decides.  Nothing here owns state: the
 * shared default test is btlend.c's and the battle-end code it settles is
 * gamedata.h's.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "unititem.h"
#include "deploy.h"
#include "msgwin.h"
#include "text.h"
#include "btlend.h"
#include "chpost2.h"

/* 0003acb0.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003acb0..0003acb6 --
   and nothing in it is ever read, so there is no local to name.

   CALL 0x0003a2e0 at 0003acbc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET at 0003acc5, so its result
   is not used and this handler returns nothing of its own.  The verdict the
   callee leaves in data_fdps_chapter_event_or_battle_end_code is the answer,
   and the dispatchers read that global directly, immediately after the
   indirect call.

   The address reaches the dispatchers only as the dword at 000602c8, fifteen
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 15 is chapter 16.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- the enemy wiped out and 蘭迪斯's
   death -- are both the shared test's own, so a handler that adds nothing is
   the complete rule and not an omission.  The chapter's scripted business, the
   second wave that starts when the enemy knights are gone and the wandering
   smith who reforges 蘭迪斯's sword within twenty turns, is carried by
   turn-event handlers keyed on the turn counter; writing either of them here
   would fire it once per unit action instead of once per turn. */
void fdps_chapter_16_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003ad10.  The shared test, then one defeat test of this chapter's own.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003ad10..0003ad16 --
   and nothing in it is ever read, so there is no local to name.  The epilogue
   is the four bare POPs at 0003ad39..0003ad3c with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003ad3d carries no immediate.

   CALL 0x0003a2e0 at 0003ad1c has nothing pushed in front of it and no ESP
   adjustment behind it, so the shared test takes no argument, and the very
   next instruction is PUSH 0x3: EAX is not consulted between the two calls,
   so that call's result is not used here.  PUSH 0x3 / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003ad21..0003ad28 is fdps_unit_is_retired(3), the caller
   clearing its one argument, and its EAX is used -- TEST EAX,EAX / JZ
   0003ad39 at 0003ad2b is the only branch in the body, skipping the MOV
   dword ptr [0x00069da0],0x1 at 0003ad2f.

   That store looks like a duplicate and is not one.  Chapter 17's id is 0x10,
   which is one of the two ids the shared test singles out at its own CMP dword
   ptr [0x00069cf4],0x10 / JZ 0003a368, so the shared test has already asked
   fdps_unit_is_retired(3) and already stored the 1 -- but only on the path
   where the code was still 0 when it was entered.  When a chapter event has
   already recorded a verdict the shared test returns at its gate having
   examined nothing, and this store, which consults neither the code's current
   value nor what the shared test found, is then the only thing that reports
   the defeat.  So deleting it as dead, folding the two tests into an if/else,
   or copying the shared test's "only while the code is 0" guard onto it all
   change behaviour on exactly that path.  What the store does not do is
   outrank a 2 written earlier in this same call: with the code 0 on entry the
   shared test runs its whole body, and its own 0x10 arm at 0003a368 has
   already replaced that 2 with the 1 at 0003a376 before control comes back
   here, so the store then writes a 1 over a 1.  The store is distinguishable
   only when the code was already non-zero when this handler was entered.

   There is no victory test of the chapter's own, and that is not an omission:
   the guide gives 第17章 人質的危機 勝利條件 敵人全滅, which is precisely the
   sweep the shared test performs, and 失敗條件 法蓮娜死亡, which is this
   store.

   Unit index 3 is a position in this map's unit array, not a character id, and
   here it is 法蓮娜.  fdps_build_map_unit_array rebuilds unit slot i from
   roster slot i, and the roster is in join order and is never permuted, so the
   roster the chapter handlers have built by chapter 17 -- 蘭迪斯, 尤利安,
   亞克, 法蓮娜, 裘娜, 費塔加, 布蘭多, 蓋亞, 琴琴, 瑪麗安 -- puts her at 3.
   fdps_chapter_17_init parks the map cursor on the same unit 3 where
   twenty-seven of the thirty entry handlers pass 0.  Writing the argument as a
   character id, or carrying the shared test's usual slot 0 here, both reach
   蘭迪斯 instead.

   Table slot 16: the dword at 000602cc, sixteen entries into the table based
   at 0006028c, is 0003ad10, and that table entry is the function's only
   xref. */
void fdps_chapter_17_post_action(void)
{
    fdps_battle_check_default_end_conditions();
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003ad80.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003ad80..0003ad86 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003ad91..0003ad94 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003ad95 carries no immediate.  Twenty-two bytes end to end, the whole body
   size.

   CALL 0x0003a2e0 at 0003ad8c is the entire body.  Nothing is pushed in front
   of it and nothing adjusts ESP behind it, so the shared test takes no
   argument; nothing reads EAX between the CALL and the RET, so its result is
   not used and this handler returns nothing of its own.  The verdict the callee
   leaves in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   Chapter 18's id is 17, which is neither of the two ids -- 0x10 and 0x15 --
   the shared test singles out at its own CMP dword ptr [0x00069cf4],0x10 /
   CMP ...,0x15, so the slot the shared test watches for the defeat is 0,
   蘭迪斯, and not 3.  That is the whole reason this chapter needs no test of
   its own: the guide gives 第18章 咆哮的獅王 勝利條件 敵人全滅, which is the
   sweep the shared test performs, and 失敗條件 蘭迪斯死亡, which is the shared
   test's own slot-0 store.  A store here would be a second, ungated copy of a
   decision the callee has already made -- which is what chapter 17 needs and
   this chapter does not have.

   The chapter's scripted business is elsewhere and is not missing from here:
   the flyers that appear from the four upper windows on the player's fourth,
   sixth, eighth and tenth turns, the knights from the two doors on the fifth,
   seventh, tenth and eleventh, and the reinforcements from below on the
   thirteenth are all keyed on the turn counter, so they belong to turn-event
   handlers; putting any of them here would fire it once per unit action
   instead of once per turn.

   The address reaches the dispatchers only as the dword at 000602d0, seventeen
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 17 is chapter 18. */
void fdps_chapter_18_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* The mode 13h aperture every line this file draws lands on and its row
   stride, PUSH 0xa0000 and PUSH 0x140 in front of each seven-push
   fdps_draw_text call.  Both are literals in the original and both stay
   literals here: they name the adapter's frame buffer, which sits where the
   hardware puts it and not where the image is loaded. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The pen inside the open message panel, PUSH 0xaa44a at 0003afea and
   0003b084: VGA offset 0xa44a, screen (138, 131), which is where the panel's
   own text row begins. */
#define PANEL_TEXT_ORIGIN 0x000aa44a

/* The three colours every draw forwards to fdps_draw_glyph, PUSH 0x6d /
   PUSH 0x0 / PUSH 0xd0 read back in push order: the standard message
   foreground, its transparent background and its outline. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* The element of data_fdps_map_cell_event_triggered_flags (gamedata.h) this
   chapter's duel runs on: byte ptr [0x000640e9], element 0x11 of the 32-entry
   array based at 0x000640d8, read at 0003ae8c and 0003af68 and set at
   0003b099.

   The array's own indexer is a map cell's raw event code and the shipped
   M%02d.DTL event planes only use codes 0 to 15, so elements 0x10 and 0x11 are
   the first two slots no cell can reach and the chapter handlers use them as
   private storage.  0x11 is shared by several chapters -- chapters 3, 8, 21, 24
   and 25 all latch it -- which is safe only because one chapter is loaded at a
   time and fdps_chapter_state_reset clears the whole array at a chapter start.
   Chapter 19's other event, the tile-triggered wave 6, uses the neighbouring
   element 0x10.

   The only other reader of this chapter's latch is fdps_chapter_19_end, which
   keys its un-retire and full HP/MP restore of the roster on it, so declining
   the duel still buys the party that recovery.

   Element 0x11 is inside the declared 32 and not past it, so this is not a
   folded base the decompiler has attributed to the wrong symbol; the array is
   the owner of that byte. */
#define CHAPTER_19_DUEL_LATCH_SLOT 0x11
#define CHAPTER_19_DUEL_LATCH_SET 1

/* The two duellists' unit slots, PUSH 0x4 at 0003ae9f and 0003af73 and
   PUSH 0x4d at 0003aead, 0003aebf.

   4 is 裘娜's battle-map slot -- unit slot i is roster slot i and the roster is
   in join order.  0x4d is the challenger, and it is a HARD-CODED INDEX rather
   than a handle on the unit fdps_deploy_wave has just appended: it names him
   only while the array already holds exactly 0x4d units, which is after both
   the turn-6 wave-1 arrival and the tile-triggered wave 6 have deployed.
   Clear the chapter before turn 6 and the challenger is appended at 0x4c
   instead, where the retire sweep below kills him, and the settle test on 0x4d
   never fires -- the chapter can then no longer be ended at all.  That is
   shipped behaviour; resolving the challenger from the deployment would
   quietly fix a bug the original has. */
#define CHAPTER_19_JUNA_UNIT_INDEX 4
#define CHAPTER_19_CHALLENGER_UNIT_INDEX 0x4d

/* The two swords the duel trades, PUSH 0xa5 at 0003aef0 and 0003af83 and
   PUSH 0xa6 at 0003af16: 妖刀村雨 and 妖刀村正 (assets/items.md).  The first is
   both the stake -- the offer is not made to a 裘娜 who is not carrying it --
   and what is taken off her when she wins. */
#define CHAPTER_19_MURASAME_ITEM_ID 0xa5
#define CHAPTER_19_MURAMASA_ITEM_ID 0xa6

/* What fdps_unit_find_item_slot answers when the unit is not carrying the
   item, CMP dword ptr [EBP + -0xc],-0x1 at 0003af02 and CMP EAX,-0x1 at
   0003af92 (unititem.h). */
#define CHAPTER_19_ITEM_SLOT_NONE (-1)

/* The six FDETXT entries the handler speaks, each a literal in the instruction
   stream: the duel won at 0003aee0, the duel lost at 0003af3a, the challenge
   at 0003afc2, the question at 0003afef, the refusal at 0003b089 and the
   acceptance at 0003b025. */
#define CHAPTER_19_DUEL_WON_TEXT_ID 0x11
#define CHAPTER_19_DUEL_LOST_TEXT_ID 0x12
#define CHAPTER_19_CHALLENGE_TEXT_ID 0x0d
#define CHAPTER_19_QUESTION_TEXT_ID 0x0e
#define CHAPTER_19_DECLINED_TEXT_ID 0x0f
#define CHAPTER_19_ACCEPTED_TEXT_ID 0x10

/* The last turn the offer is still made on, CMP dword ptr [0x00069ce8],0x14 /
   JG 0x0003af66 at 0003af54: the branch that skips the offer is taken only for
   a turn counter strictly greater than 0x14, so turn 20 still gets the duel and
   turn 21 does not.  JG and not JA, so the comparison is signed. */
#define CHAPTER_19_DUEL_LAST_TURN 0x14

/* What fdps_deploy_wave is asked for, PUSH 0x2 at 0003af9f and the XOR EAX,EAX
   / PUSH EAX at 0003af9c: the current map's wave 2, placed on the nearest free
   tile rather than exactly on its placement record's own (deploy.h). */
#define CHAPTER_19_DUEL_WAVE 2
#define CHAPTER_19_PLACE_NEAREST_FREE_TILE 0

/* The FACE.CEL record the panel reveal carries, PUSH 0x23 at 0003afd2: the
   challenger's portrait. */
#define CHAPTER_19_DUEL_SPEAKER_FACE_INDEX 0x23

/* The answer that accepts, CMP dword ptr [EBP + -0xc],0x0 / JNZ 0x0003b076 at
   0003b00c.  fdps_prompt_two_choice answers 0 for the left cell, 1 for the
   right and -1 for a cancel (msgwin.h), and this is an equality against 0, so
   a cancel declines exactly as the right cell does. */
#define CHAPTER_19_DUEL_ANSWER_ACCEPT 0

/* How many records the accepted branch's retire sweep walks, CMP dword ptr
   [EBP + -0x8],0x4d / JL at 0003b03c: unit indices 0 through 0x4c, which is
   every unit on the map except the challenger the offer has just appended at
   0x4d. */
#define CHAPTER_19_RETIRE_SWEEP_UNIT_COUNT 0x4d

/* What each swept record's flags byte is left holding, MOV byte ptr
   [EAX + 0x5],0x1 at 0003b064.  IT IS A WHOLE-BYTE STORE AND NOT AN OR: it
   raises the retired bit 0x01 that fdps_unit_is_retired reads and drops the
   has-acted bit 0x80 along with everything else that byte was carrying. */
#define CHAPTER_19_RETIRED_FLAG_BYTE 1

/* The two codes this handler writes into
   data_fdps_chapter_event_or_battle_end_code: MOV dword ptr [0x00069da0],0x2 at
   0003af4a, the chapter cleared, and MOV dword ptr [0x00069da0],0x0 at
   0003b06a, the battle carries on. */
#define BATTLE_END_CHAPTER_CLEARED 2
#define BATTLE_END_BATTLE_CONTINUES 0

/* 0003ae80.  Chapter 19's post-action test, and the only handler in this file
   that speaks to the player and moves an item.  It is two independent halves
   run one after the other, and which one does anything is decided by the same
   latch: while the latch is down the chapter is an ordinary battle and the
   half at the top forwards to the shared test; once it is up the duel is
   running and that half settles it instead.  The half at the bottom is the
   offer, and it can only fire while the latch is still down.

   The frame is the standard four-push Watcom one with a 0xc-byte local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0xc at 0003ae80..0003ae86 -- and
   the epilogue is MOV ESP,EBP followed by the four POPs at 0003b0a0..0003b0a5,
   which is what a non-empty local area leaves behind; the RET at 0003b0a6
   carries no immediate, so the caller clears what it pushed and there is
   nothing to clear.

   THE DUEL HALF MUST NOT FALL BACK ON THE SHARED TEST, and the CMP byte ptr
   [0x000640e9],0x0 / JNZ at 0003ae8c..0003ae93 is what keeps it from doing so.
   The accepted branch below retires unit 0, 蘭迪斯, along with everyone else
   but 裘娜, so fdps_battle_check_default_end_conditions would force the defeat
   code 1 the moment the duel started.  Writing the obvious "run the default
   check first, then add the chapter's extra" turns the duel into an instant
   Game Over.

   The duel is settled by three calls to fdps_unit_is_retired and not two.
   PUSH 0x4 / CALL 0x000109b0 at 0003ae9f with TEST EAX,EAX / JNZ at 0003aea9
   and PUSH 0x4d / CALL at 0003aead with TEST EAX,EAX / JZ 0x0003af54 at
   0003aeb7 are a short-circuiting || chain: while both duellists are standing
   control leaves for the offer half and NOTHING AT ALL IS WRITTEN, which is
   what keeps the battle loop running the duel.  Once one of them is down the
   third call at 0003aebf asks about the challenger again to see which, and its
   TEST EAX,EAX / JZ 0x0003af27 at 0003aec9 picks the arm: challenger retired
   is 裘娜's win, anything else is her loss.

   Her win is the only path that moves an item.  The slot
   fdps_unit_find_item_slot answers with is kept at [EBP-0xc] and compared
   against -1 at 0003af02, so the 妖刀村雨 is taken off her only if she still
   has it, while fdps_unit_add_item hands over the 妖刀村正 either way -- the
   add is outside the branch, at 0003af16, and not its else.  Both arms then
   join at MOV dword ptr [0x00069da0],0x2 at 0003af4a, so the chapter ends
   whichever way the duel went.

   The offer half is one short-circuiting && chain of five, every failure
   jumping to the same exit at 0003b0a0: the turn counter 0x14 or less
   (signed), the battle-end code already 2, the latch still down, 裘娜 not
   retired, and the 妖刀村雨 in her bag.  The second of those is what makes
   this an exit rite rather than an event: the offer is made on the action that
   has just cleared the chapter, and on no other.

   Everything after that runs in source order with nothing conditional in it
   until the answer: the challenger is appended as the map's wave 2, the
   challenge is spoken on the visible page, the panel is revealed under FACE.CEL
   record 0x23, the question is written inside it, fdps_prompt_two_choice runs
   the modal prompt and the panel is retracted before the answer is looked at.
   Only fdps_prompt_two_choice's result is used -- it is stored at 0003b004 and
   every draw's returned cursor is discarded.

   THE ACCEPTED BRANCH LEAVES THE BATTLE OPEN, and that is the whole point of
   it.  It retires every unit index 0 through 0x4c except 裘娜 -- so 蘭迪斯 and
   the entire party go with them -- and then puts the battle-end code BACK to 0
   at 0003b06a so the phase loop resumes with only the two duellists on the map.
   fdps_get_unit_record is called for index 4 as well and its answer simply
   discarded; the CMP dword ptr [EBP + -0x8],0x4 / JZ at 0003b05b guards the
   store alone.  Declining writes its refusal line and nothing else, so the
   cleared code stands and the chapter ends.

   Either answer raises the latch at 0003b099, which is outside the if/else and
   is therefore what makes the offer one-shot.

   THE REFUSAL LINE IS DRAWN INSIDE A PANEL THAT IS NO LONGER THERE.  PUSH
   0xaa44a at 0003b084 sends it to the panel's interior pen, but
   fdps_message_window_close has already run at 0003b007 and repainted the whole
   visible page; the acceptance line at 0003b025 goes to the screen origin
   instead.  Giving the two lines the same destination is the natural tidy-up
   and it moves where the refusal appears.

   Table slot 18: the dword at 000602d4, eighteen entries into the table based
   at 0006028c, is 0003ae80, and that table entry is the function's only
   xref. */
void fdps_chapter_19_post_action(void)
{
    /* Where 裘娜 is keeping the 妖刀村雨, or -1 when she is not carrying it:
       the slot fdps_unit_find_item_slot answered with, kept at [EBP-0xc]. */
    int murasame_slot;
    /* Which cell of the offer the player committed to: 0 accepts, 1 declines
       and -1 is a cancel.  The original keeps it in the same [EBP-0xc] the
       item slot above used, the two never being live at once. */
    int duel_answer;
    /* The retire sweep's counter, [EBP-0x8]. */
    int unit_index;
    /* The record the sweep's store is made through, [EBP-0x4]. */
    struct fdps_unit_record *unit;

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_19_DUEL_LATCH_SLOT]
            == 0) {
        fdps_battle_check_default_end_conditions();
    } else if (fdps_unit_is_retired(CHAPTER_19_JUNA_UNIT_INDEX) != 0 ||
               fdps_unit_is_retired(CHAPTER_19_CHALLENGER_UNIT_INDEX) != 0) {
        if (fdps_unit_is_retired(CHAPTER_19_CHALLENGER_UNIT_INDEX) != 0) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_19_DUEL_WON_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            murasame_slot =
                fdps_unit_find_item_slot(CHAPTER_19_JUNA_UNIT_INDEX,
                                         CHAPTER_19_MURASAME_ITEM_ID);
            if (murasame_slot != CHAPTER_19_ITEM_SLOT_NONE) {
                fdps_unit_remove_item(CHAPTER_19_JUNA_UNIT_INDEX,
                                      murasame_slot);
            }
            fdps_unit_add_item(CHAPTER_19_JUNA_UNIT_INDEX,
                               CHAPTER_19_MURAMASA_ITEM_ID);
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_19_DUEL_LOST_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }
        data_fdps_chapter_event_or_battle_end_code =
            BATTLE_END_CHAPTER_CLEARED;
    }

    if (data_fdps_battle_turn_counter <= CHAPTER_19_DUEL_LAST_TURN &&
        data_fdps_chapter_event_or_battle_end_code ==
            BATTLE_END_CHAPTER_CLEARED &&
        data_fdps_map_cell_event_triggered_flags[CHAPTER_19_DUEL_LATCH_SLOT]
            == 0 &&
        fdps_unit_is_retired(CHAPTER_19_JUNA_UNIT_INDEX) == 0 &&
        fdps_unit_find_item_slot(CHAPTER_19_JUNA_UNIT_INDEX,
                                 CHAPTER_19_MURASAME_ITEM_ID)
            != CHAPTER_19_ITEM_SLOT_NONE) {

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CHAPTER_19_DUEL_WAVE,
                         CHAPTER_19_PLACE_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHAPTER_19_CHALLENGE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_message_window_open(CHAPTER_19_DUEL_SPEAKER_FACE_INDEX);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHAPTER_19_QUESTION_TEXT_ID,
                       (unsigned char *) PANEL_TEXT_ORIGIN,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        duel_answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (duel_answer == CHAPTER_19_DUEL_ANSWER_ACCEPT) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_19_ACCEPTED_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            for (unit_index = 0;
                 unit_index < CHAPTER_19_RETIRE_SWEEP_UNIT_COUNT;
                 unit_index++) {
                unit = fdps_get_unit_record(unit_index);
                if (unit_index != CHAPTER_19_JUNA_UNIT_INDEX) {
                    unit->flags = CHAPTER_19_RETIRED_FLAG_BYTE;
                }
            }
            data_fdps_chapter_event_or_battle_end_code =
                BATTLE_END_BATTLE_CONTINUES;
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_19_DECLINED_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }

        data_fdps_map_cell_event_triggered_flags[CHAPTER_19_DUEL_LATCH_SLOT] =
            CHAPTER_19_DUEL_LATCH_SET;
    }
}

/* The three unit indices chapter 20's handler releases on each of the first
   seventeen turns are turn + one of these bases: 12, 29 and 46, the three ADD
   immediates at 0003b16c, 0003b188 and 0003b1ad.  Each base is one below the
   first index of its run, since the earliest turn that adds to it is turn 1;
   the three runs are 17 units apart and are consecutive stretches of the map's
   enemy block, which the schedule walks three abreast from index 13 to index
   63. */
#define CHAPTER_20_RELEASE_BASE_1 0x0c
#define CHAPTER_20_RELEASE_BASE_2 0x1d
#define CHAPTER_20_RELEASE_BASE_3 0x2e

/* The last turn on which chapter 20 releases anything, straight off the CMP
   dword ptr [0x00069ce8],0x11 / JG 0x0003b1b9 at 0003b15c: the branch that
   skips the three calls is taken only for a turn counter strictly greater than
   0x11, so turn 17 still releases and turn 18 does not.  JG and not JA, so the
   comparison is signed. */
#define CHAPTER_20_LAST_RELEASE_TURN 0x11

/* 0003b150.  A turn-scheduled release of three held units, then the shared
   test.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b150..0003b156 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b1be..0003b1c1 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b1c2 carries no immediate.

   CMP dword ptr [0x00069ce8],0x11 / JG 0x0003b1b9 at 0003b15c is the only
   branch in the body and it guards all three calls at once; the target is the
   CALL 0x0003a2e0 at 0003b1b9, so the shared test runs on every turn whichever
   way the branch goes.  The comparison is JG rather than JA, which is the
   signed ordering data_fdps_battle_turn_counter is declared with in
   gamedata.h.

   Each of the three calls is PUSH 0x0 / MOV EAX,[0x00069ce8] / ADD EAX,<base>
   / PUSH EAX / MOV EAX,[0x00069ce8] / ADD EAX,<base> / PUSH EAX / CALL
   0x00036b60 / ADD ESP,0xc -- 0003b165, 0003b181 and 0003b19d.  The counter is
   re-loaded for each push rather than kept in a register, which is what -od
   emits and carries no meaning of its own; the two pushed indices are
   therefore always equal, so each call is an INCLUSIVE range of exactly one
   unit and no loop of the callee's runs more than once.  ADD ESP,0xc after
   each is the caller clearing three dword arguments, and nothing reads EAX
   between the CALL and the next PUSH, so none of the three results is used.

   Value 0 in the low nibble of the unit record's ai_behavior byte is the
   behaviour code that walks the unit at the nearest opposing unit; map19.dat
   deploys this map's enemies in mode 2, which fights what comes into reach but
   never advances.  So the schedule releases three held enemies per turn over
   turns 1 to 17 -- indices 13..29, 30..46 and 47..63, 51 units in all -- which
   is the batch-by-batch advance the strategy guide describes for this map.
   The callee merges rather than assigns, so the high-nibble AI flags each
   released unit carries survive.

   The three bases skip index 11 deliberately.  That index is map19.dat's
   deployment record 0, the map's only unit in behaviour mode 5, the scripted
   event walker that leaves the bottom-left chest to take the treasure at the
   top of the map; writing mode 0 over it would cancel that script, so the
   first column starts at 13 -- one above it and one above index 12 as well.
   Index 64, the last deployment record, is simply past the arithmetic's reach.
   Starting the first column at 11 or 12 to make the three columns cover the
   block evenly is the mistake this constant exists to prevent.

   The highest index the schedule ever writes is 17 + 0x2e = 63, and the map
   has 65 live units, so nothing here runs off the array.  Neither this handler
   nor the callee bounds an index against data_fdps_map_unit_count, and neither
   has to.

   CALL 0x0003a2e0 at 0003b1b9 is the shared end test, unconditional and
   argument-free, and nothing reads EAX between it and the RET, so this handler
   returns nothing of its own and adds no end condition either: chapter 20 is
   chapter id 0x13, which is neither of the two ids -- 0x10 and 0x15 -- the
   shared test singles out, so the slot it watches for the defeat is 0,
   蘭迪斯.  That matches the chapter's stated rules exactly, 勝利條件 敵人全滅
   and 失敗條件 蘭迪斯死亡.

   Table slot 19: the dword at 000602d8, nineteen entries into the table based
   at 0006028c, is 0003b150, and that table entry is the function's only
   xref. */
void fdps_chapter_20_post_action(void)
{
    if (data_fdps_battle_turn_counter <= CHAPTER_20_LAST_RELEASE_TURN) {
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_1,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_1, 0);
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_2,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_2, 0);
        fdps_object_set_field34_low_nibble_range(
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_3,
            data_fdps_battle_turn_counter + CHAPTER_20_RELEASE_BASE_3, 0);
    }
    fdps_battle_check_default_end_conditions();
}

/* 0003b210.  One CALL and a return, with no branch in the body at all.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b210..0003b216 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b221..0003b224 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b225 carries no immediate.  Twenty-two bytes end to end, the whole body
   size.

   CALL 0x0003a2e0 at 0003b21c is the entire body.  Nothing is pushed in front
   of it and nothing adjusts ESP behind it, so the shared test takes no
   argument; nothing reads EAX between the CALL and the RET, so its result is
   not used and this handler returns nothing of its own.  The verdict the callee
   leaves in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   Chapter 21's id is 20 (0x14), which is neither of the two ids -- 0x10 and
   0x15 -- the shared test singles out at its own CMP dword ptr [0x00069cf4],
   0x10 / CMP ...,0x15 at 0003a356 and 0003a35f, so the slot the shared test
   watches for the defeat is 0, 蘭迪斯, and not 3.  That is the whole reason
   this chapter needs no test of its own: the guide gives 第21章 地底神殿
   勝利條件 敵人全滅, which is the sweep the shared test performs, and
   失敗條件 蘭迪斯死亡, which is the shared test's own slot-0 store.  A store
   here would be a second, ungated copy of a decision the callee has already
   made -- which is what chapter 17 needs and this chapter does not have.

   The chapter's scripted business is elsewhere and is not missing from here:
   both reinforcement waves are position triggers -- the first when a unit
   reaches the junction two squares past the turn, the second when a unit
   reaches any of the standing enemy groups, which also switches the map to a
   general assault -- and they are carried by fdps_chapter_21_event_deploy_wave_1
   and fdps_chapter_21_event_deploy_wave_2.  Neither is keyed on a unit having
   acted, and putting either here would fire it after every action regardless
   of where anybody stood.

   The address reaches the dispatchers only as the dword at 000602dc, twenty
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 20 is chapter 21. */
void fdps_chapter_21_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003b270.  One CALL, one branch, one store -- and no forward to the shared
   end test at all, which is what makes this handler different from every one
   above it in this file.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b270..0003b276 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b294..0003b297 with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b298 carries no immediate.

   PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 at 0003b27c..0003b283 is
   fdps_unit_is_retired(3) with the caller clearing its one argument, and its
   EAX is used at once: TEST EAX,EAX / JZ 0003b294 at 0003b286 is the only
   branch in the body, skipping the MOV dword ptr [0x00069da0],0x1 at
   0003b28a.  That store is the whole of the rest of the function -- the code
   is never read before it is written and no other value is ever stored -- so
   the handler can turn the code into a 1 and can do nothing else with it.

   There is no CALL 0x0003a2e0 here, and that is deliberate rather than a
   missing line.  The shared test declares its victory by sweeping the unit
   list for a live enemy, and chapter 22 is not won that way: the guide gives
   第22章 巫湯婆婆 勝利條件 擊倒巫湯婆婆, one named boss rather than 敵人全滅,
   and the clear is written by the scripted fdps_chapter_22_event_boss_defeat
   at 000388b0, whose MOV dword ptr [0x00069da0],0x2 at 00038936 sits on every
   path out of that handler.  Adding the forward here would let the chapter
   clear itself the moment the last minion fell, with the boss still standing.

   The store carries no "only while the code is still 0" guard, unlike the two
   the shared test puts around its own writes, and copying that guard here by
   analogy changes behaviour: a defeat that lands on the same action as the
   scripted boss-defeat clear overwrites the 2 with a 1 in the original, and
   the player gets a Game Over where the guarded version would clear the
   chapter.  The value read back is the value this handler last wrote.

   Unit index 3 is 法蓮娜, and it is a position in this map's unit array rather
   than a character id: unit slot i is roster slot i, the roster is in join
   order and is never permuted, so slot 3 is hers from chapter 4 on regardless
   of who the chapter deploys.  Chapter 22's 己方 is 蘭迪斯以外的所有人 and its
   失敗條件 is 法蓮娜死亡, so slot 0 -- the slot the shared test watches in
   every chapter but 0x10 and 0x15 -- belongs to a character who is not even on
   this map, and carrying the shared test's usual index here would watch him
   instead of her.

   One consequence of there being no forward is worth recording: chapter 22's
   id, 0x15, is the second of the two the shared test singles out at its own
   CMP dword ptr [0x00069cf4],0x15, but that arm never runs.  The shared test
   has exactly twenty-one xrefs in the image, all of them direct calls from
   post-action handlers, and this handler is not one of them -- and the table
   at 0006028c reaches this chapter through slot 21 alone.  So the substitution
   the shared test makes for 0x15 is unreachable, and the whole of chapter 22's
   defeat rule is the store below.

   The address reaches the dispatchers only as the dword at 000602e0,
   twenty-one entries into the table based at 0006028c, which is why the
   function has no static caller: slot 21 is chapter 22. */
void fdps_chapter_22_post_action(void)
{
    if (fdps_unit_is_retired(3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The three unit slots chapter 25's victory test asks about, the PUSH 0xc,
   PUSH 0xd and PUSH 0xe immediates at 0003b6bc, 0003b6ca and 0003b6da.  They
   are the map's first three enemy slots and they hold the chapter's three
   魔戰將軍.

   MAP24.DAT is a 131-byte header followed by 59 deployment records of 26 bytes,
   which is the guide's enemy list for this chapter to the unit: 3 + 1 黑暗祭司
   + 11 神箭手 + 18 鎧甲武士 + 8 地獄騎士 + 18 天空騎士.  Its header byte +1
   fields twelve player slots -- the same field the chapter 4, 5, 6, 9 and 11
   handlers in chpost1.c are read for, and the field that gives map19.dat
   eleven -- and byte +2 is the record count, 59.  fdps_build_map_unit_array lays those twelve player
   slots down first and fdps_deploy_wave appends the records whose wave byte
   (+0x15) matches the wave being deployed, so the first record of wave 0 is
   unit slot 12.  Records 0, 1 and 2 are wave 0, are the file's only level-30
   units -- character ids 64, 65 and 66 -- and the guide gives exactly three
   level-30 enemies, 塞克斯, 布魯森 and 汎拉沛.  The roster is twelve deep for
   the same count: the ten of chapter 17 plus 蘭斯洛特, who arrives on chapter
   19's sixth turn, plus 珊, who joins in chapter 24.

   The map is not deployed in one go, but the split does not move these three:
   41 of the 59 records are wave 0 and land at slots 12..52 when the map opens,
   and the other 18 are wave 1 -- all character id 97, the LV16 天空騎士x18 the
   chapter's reinforcement event brings on -- appended at slots 53..70 when that
   event fires.  The chapter's eighteen 鎧甲武士 are a different group, character
   id 100, and every one of them is wave 0.  Which of the two 18-strong groups is
   which is settled by chapter 24, where their counts differ: MAP23.DAT holds id
   95 x4, id 100 x6 and id 97 x15 against that chapter's guide list 神箭手x4 /
   鎧甲武士x6 / 天空騎士x15.  The warlords are wave-0 records 0, 1 and 2 either
   way. */
#define CHAPTER_25_WARLORD_SLOT_1 0x0c
#define CHAPTER_25_WARLORD_SLOT_2 0x0d
#define CHAPTER_25_WARLORD_SLOT_3 0x0e

/* 0003b6b0.  Two end conditions of the chapter's own and no forward to the
   shared test, like chapter 22's handler and unlike every other one in this
   file -- but this one declares a victory as well as a defeat.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b6b0..0003b6b6 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is
   the four bare POPs at 0003b70c..0003b70f with no MOV ESP,EBP in front of
   them, which is what an empty local area leaves behind, and the RET at
   0003b710 carries no immediate.

   The victory test is three calls chained by their zero tests.  PUSH 0xc /
   CALL 0x000109b0 / ADD ESP,0x4 at 0003b6bc..0003b6c3 is
   fdps_unit_is_retired(0x0c) with the caller clearing its one argument, and its
   EAX is used at once: TEST EAX,EAX / JZ 0003b6d8 at 0003b6c6 leaves for the
   JMP 0x0003b6e8 that skips the store the moment a warlord is still standing.
   The 0xd call at 0003b6ca and the 0xe call at 0003b6dc repeat the shape with
   JNZ into the next test and a fall-through onto the same skip, so the three
   calls are a short-circuiting && chain in source order and no call after a
   live warlord is made at all.  Only when all three report retired does control
   reach MOV dword ptr [0x00069da0],0x2 at 0003b6ea.

   The defeat test then runs unconditionally: the store's own successor and the
   skip path's target are both 0003b6f4, where PUSH 0x0 / CALL 0x000109b0 / ADD
   ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b70c at 0003b6fe
   guards MOV dword ptr [0x00069da0],0x1 at 0003b702.  Neither store consults
   the code's current value and neither is the other's else branch, so the last
   write wins and the defeat is last: 蘭迪斯 falling on the same action that
   retires the final warlord is a Game Over in the original, where an if/else,
   an else-if, or the shared test's "only while the code is still 0" guard
   copied onto either store would clear the chapter instead.  In the other
   direction the same absence of a guard is what lets the victory overwrite a
   defeat a chapter event recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather
   than a missing line: the guide gives 第25章 魔戰將軍 勝利條件 魔戰將軍死亡,
   three named bosses and not 敵人全滅, so the shared test's sweep would clear
   the chapter as soon as the last minion fell with all three warlords still
   alive.  Because that test never runs, this handler has to carry the defeat
   itself, which is the slot-0 store -- 失敗條件 蘭迪斯死亡, and slot 0 is
   蘭迪斯 because unit slot i is roster slot i and the roster is in join order.
   Chapter 25's 己方 is 法蓮娜以外的所有人, so it is 法蓮娜 at slot 3 and not
   蘭迪斯 at slot 0 who goes undeployed here, the mirror image of chapter 22.
   Her slot is still reserved -- the map fields twelve player slots, not eleven
   -- which is what keeps the warlords at 12, 13 and 14.

   Chapter 25 is chapter id 24 and the dword at 000602ec, twenty-four entries
   into the table based at 0006028c, is 0003b6b0; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_25_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_1) != 0 &&
        fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_2) != 0 &&
        fdps_unit_is_retired(CHAPTER_25_WARLORD_SLOT_3) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The four unit slots chapter 26's victory test asks about, the PUSH 0xc, PUSH
   0xd, PUSH 0xe and PUSH 0xf immediates at 0003b76c, 0003b77a, 0003b78a and
   0003b79a.  They are the map's first four enemy slots and they hold the
   chapter's four 魔戰將軍.

   MAP25.DAT is a 131-byte header followed by 80 deployment records of 26 bytes
   -- 0x83 + 80 * 0x1a is 2211, the whole file.  Its header byte +1 fields
   twelve player slots, the same field chapter 25's map sets to twelve, and byte
   +2 is the record count, 80.  fdps_build_map_unit_array lays the twelve player
   slots down first and fdps_deploy_wave appends, in file order, the records
   whose wave byte (+0x15) equals the wave being deployed, so the 68 wave-0
   records become unit slots 0x0c..0x4f the moment the map opens.  Records 0, 1,
   2 and 3 are wave 0, are the file's only level-30 units -- their level byte
   (+4) is 0x1e where every other record on the map is 0x12 or 0x28 -- and carry
   character ids 0x40, 0x41, 0x42 and 0x43.  The guide's enemy list for this
   chapter gives exactly four level-30 enemies, 凱因巴, 塞克斯, 布魯森 and
   汎拉沛, against LV18 for the whole garrison behind them.

   The roster is twelve deep for the same count as chapter 25's: the ten of
   chapter 17 plus 蘭斯洛特, who arrives on chapter 19's sixth turn, plus 珊, who
   joins in chapter 24.  Chapter 26's 己方 is 法蓮娜以外的所有人, so slot 3 is
   reserved and empty here, which is what keeps the four warlords at 12..15
   rather than 11..14. */
#define CHAPTER_26_WARLORD_SLOT_1 0x0c
#define CHAPTER_26_WARLORD_SLOT_2 0x0d
#define CHAPTER_26_WARLORD_SLOT_3 0x0e
#define CHAPTER_26_WARLORD_SLOT_4 0x0f

/* The slot of data_fdps_map_cell_event_triggered_flags (gamedata.h) that the
   one-shot chapter-event handlers latch: byte ptr [0x000640e8], element 0x10 of
   the 32-entry array based at 0x000640d8.

   The array's own indexer is a cell's raw event code and the shipped M%02d.DTL
   event planes only ever use codes 0 to 15, so element 0x10 is the first slot no
   map cell can reach and the event handlers use it as private storage.  It is
   one slot shared by all of them, which is safe only because one chapter is
   loaded at a time and fdps_chapter_state_reset memsets the whole array when a
   chapter starts.  For this chapter the writer is
   fdps_chapter_26_event_deploy_waves_2_and_3 at 00039230, which sets it to 1 at
   0003939c as the last thing it does.

   Element 0x10 is inside the declared 32 and not past it, so this is not a
   folded base the decompiler has attributed to the wrong symbol; the array is
   the owner of that byte. */
#define CHAPTER_EVENT_ONE_SHOT_SLOT 0x10

/* The unit slot chapter 26's second defeat test asks about, the PUSH 0x5b at
   0003b7d8.  It is the LAST unit slot the map ever holds and it is the fourth
   and last of 索爾's 侍衛 -- NOT 索爾 himself, who is at 0x57.

   MAP25.DAT's 80 deployment records split 68 / 7 / 5 across waves 0, 2 and 3;
   there are no wave-1 records.  fdps_chapter_26_event_deploy_waves_2_and_3
   deploys wave 2 first (its FUN_00023830(map, 2, 0) at the top of the body) and
   wave 3 afterwards, so the seven wave-2 records -- file records 73..79, all
   side 0, character id 0x64 -- land at slots 0x50..0x56, and the five wave-3
   records -- file records 62..66, all side 1 -- land at 0x57..0x5b.  Record 62
   is character id 0x0c at level 40 and records 63..66 are four copies of
   character id 0x3b at level 40, which is the guide's 友方 LV40英雄索爾 plus
   LV40侍衛x4.  Character id 0x0c is 索爾: FRIAPRDA.DAT's row 0x0c is the
   HP960 / MP480 / AP300 / DP100 / DX160 / MV6 template and FRILEVUP.DAT's row
   0x0c grows every field by 1, so at level 40 the unit builder's
   HP = hp_base + (LV-1) * hp_min gives 999 and MP gives 519, while
   AP = ap_base + LV * ap_min gives 340 and DP 140 -- 740 and 310 once 炎龍劍
   and 大地鎧甲 are counted, and DX 200.  That is the guide's 索爾 in all six
   fields.

   So the chapter's stated 失敗條件 索爾死亡 is NOT what the code tests: it
   watches the last of his four escorts.  Writing the guide's rule as a test on
   索爾's own slot reaches 0x57 and ends the battle on a different unit's death.
   Nothing here corrects that; the original's rule is the rule.

   0x5b is also the highest slot the map ever reaches -- twelve party slots plus
   all 80 deployment records is 92 units, 0x00..0x5b -- which is why the read is
   in bounds only once the relief force has landed, and why the latch above
   guards it. */
#define CHAPTER_26_ALLIED_GUARD_LAST_SLOT 0x5b

/* 0003b760.  Three end conditions of the chapter's own and no forward to the
   shared test: a victory, a defeat, and a second defeat that only becomes
   reachable part-way through the map.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b760..0003b766 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is the
   four bare POPs at 0003b7f2..0003b7f5 with no MOV ESP,EBP in front of them,
   which is what an empty local area leaves behind, and the RET at 0003b7f6
   carries no immediate.

   The victory test is four calls chained by their zero tests.  PUSH 0xc / CALL
   0x000109b0 / ADD ESP,0x4 at 0003b76c..0003b773 is fdps_unit_is_retired(0x0c)
   with the caller clearing its one argument, and its EAX is used at once: TEST
   EAX,EAX / JZ 0003b788 at 0003b776 leaves for the JMP 0x0003b798 that skips the
   store the moment a warlord is still standing.  The 0xd call at 0003b77a, the
   0xe call at 0003b78a and the 0xf call at 0003b79a repeat the shape with JNZ
   into the next test and a fall-through onto the same skip chain, so the four
   calls are a short-circuiting && chain in source order and no call after a live
   warlord is made at all.  Only when all four report retired does control reach
   MOV dword ptr [0x00069da0],0x2 at 0003b7aa.

   The first defeat test then runs unconditionally: the store's own successor and
   the skip chain's target are both 0003b7b4, where PUSH 0x0 / CALL 0x000109b0 /
   ADD ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b7cc at 0003b7be
   guards MOV dword ptr [0x00069da0],0x1 at 0003b7c2.  Slot 0 is 蘭迪斯, the
   first half of the chapter's 失敗條件.

   The second defeat test is gated on the chapter's one-shot event latch: XOR
   EAX,EAX / MOV AL,byte ptr [0x000640e8] / CMP EAX,0x1 / JNZ 0003b7e6 at
   0003b7cc..0003b7d6 zero-extends the byte and compares it for EQUALITY with 1,
   so any other value -- including a non-zero one -- skips the test rather than
   admitting it.  Only on the equal path does PUSH 0x5b / CALL 0x000109b0 / ADD
   ESP,0x4 at 0003b7d8 run, and a retired slot 0x5b reaches MOV dword ptr
   [0x00069da0],0x1 at 0003b7e8 through the JNZ at 0003b7e4.

   That gate is load-bearing rather than defensive.  Slot 0x5b does not exist
   until fdps_chapter_26_event_deploy_waves_2_and_3 has run, and that handler
   sets the latch as the last thing it does, so the latch reading 1 is exactly
   the condition "the relief force is on the map".  fdps_unit_is_retired does not
   bound its index, so an ungated test would read the retirement bit of a record
   the map has not built yet and could end the battle before the ally ever
   appears.

   None of the three stores consults the code's current value and none is
   another's else branch, so the last write wins and the order is victory,
   蘭迪斯, escort.  An action that retires the final warlord and 蘭迪斯 together
   is a Game Over in the original, and so is one that retires the final warlord
   and slot 0x5b together once the latch is set; an if/else, an else-if, or the
   shared test's "only while the code is still 0" guard copied onto any of the
   three would clear the chapter instead.  In the other direction the same
   absence of a guard is what lets the victory overwrite a defeat a chapter event
   recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line: the guide gives 第26章 狂信人之塔 勝利條件 擊倒魔戰將軍, four
   named bosses and not 敵人全滅, so the shared test's sweep would clear the
   chapter as soon as the last of the seventy-odd garrison fell with all four
   warlords alive.  Because that test never runs, this handler has to carry both
   defeats itself.

   Chapter 26 is chapter id 25 and the dword at 000602f0, twenty-five entries
   into the table based at 0006028c, is 0003b760; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_26_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_1) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_2) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_3) != 0 &&
        fdps_unit_is_retired(CHAPTER_26_WARLORD_SLOT_4) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_EVENT_ONE_SHOT_SLOT] ==
            1 &&
        fdps_unit_is_retired(CHAPTER_26_ALLIED_GUARD_LAST_SLOT) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The one unit slot chapter 27's victory test asks about, the PUSH 0xc at
   0003b8ac.  It is the map's first enemy slot and it holds LV40 魔導王吉歐,
   the boss whose death is the chapter's 勝利條件.

   MAP26.DAT is a 131-byte header followed by 55 deployment records of 26 bytes
   -- 0x83 + 55 * 0x1a is 1561, the whole file.  Its header byte +1 fields twelve
   player slots, the same count chapters 25 and 26 field, and byte +2 is the
   record count, 55.  fdps_build_map_unit_array lays the twelve player slots down
   first and fdps_deploy_wave appends, in file order, the records whose wave byte
   (+0x15) equals the wave being deployed, so record 0 becomes unit slot 12.

   Record 0 is wave 0 -- so the slot exists from the moment the map opens and
   needs no latch of the kind chapter 26's second defeat test carries -- and it
   is the file's ONLY level-40 record: the level byte (+4) reads 0x28 once, 0x1e
   on four records and 0x12 on the other fifty.  That histogram is the guide's
   enemy list for this chapter to the unit -- LV40魔導王吉歐, four LV30 魔戰將軍,
   and LV18 神箭手x8 + 鎧甲武士x12 + 地獄騎士x10 + 天空騎士x20, fifty of them --
   so the single level-40 record is 吉歐 and it is the first record in the file.

   Its character id is 0x3f, which is over 60 and so indexes ENEMYDAT.DAT rather
   than FRIAPRDA.DAT, at row 0x3f - 60.  That table's field layout is not decoded
   (resource_info/data_tables.md), so this is a numeric match and not a field
   read: taking the little-endian word at +2 and the byte at +4 of a row as the
   HP and MP bases and multiplying each by the unit's level reproduces the
   guide's numbers for all five of this map's named enemies at once -- 300 and
   250 at level 40 give 吉歐's HP12000 and MP10000, and rows 0x40..0x43 at level
   30 give 5400/0, 4500/3000, 3900/6000 and 6300/4500, which are 塞克斯,
   布魯森, 汎拉沛 and 凱因巴.

   The four 魔戰將軍 stand at slots 13, 14, 15 and 16 here and none of them is
   tested: this chapter is won by killing 吉歐 alone.  Their defeat is what
   triggers the reinforcement event instead -- the thirty wave-1 records, which
   land at slots 0x25..0x42 when it fires -- and that event is a separate
   handler.  Carrying chapter 26's four-warlord && chain over to this chapter
   would clear it while the boss still stands. */
#define CHAPTER_27_MAGE_KING_SLOT 0x0c

/* 0003b8a0.  A victory condition and a defeat condition of the chapter's own
   and no forward to the shared test, the same shape as chapter 25's handler
   with a one-slot victory test in place of its three-slot chain.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at 0003b8a0..0003b8a6 -- and
   nothing in it is ever read, so there is no local to name.  The epilogue is the
   four bare POPs at 0003b8dc..0003b8df with no MOV ESP,EBP in front of them,
   which is what an empty local area leaves behind, and the RET at 0003b8e0
   carries no immediate.

   The victory test is one call.  PUSH 0xc / CALL 0x000109b0 / ADD ESP,0x4 at
   0003b8ac..0003b8b3 is fdps_unit_is_retired(0x0c) with the caller clearing its
   one argument, and its EAX is used at once: TEST EAX,EAX / JZ 0003b8c4 at
   0003b8b6 guards MOV dword ptr [0x00069da0],0x2 at 0003b8ba.

   The defeat test then runs unconditionally: the store's own successor and the
   skip path's target are both 0003b8c4, where PUSH 0x0 / CALL 0x000109b0 / ADD
   ESP,0x4 asks about unit slot 0 and TEST EAX,EAX / JZ 0003b8dc at 0003b8ce
   guards MOV dword ptr [0x00069da0],0x1 at 0003b8d2.  Slot 0 is 蘭迪斯 -- unit
   slot i is roster slot i and the roster is in join order -- and his death is
   the chapter's 失敗條件.  Chapter 27's 己方 is 法蓮娜以外的所有人, so it is
   slot 3 that is reserved and empty here, which is what keeps 吉歐 at 12.

   Neither store consults the code's current value and neither is the other's
   else branch, so the last write wins and the defeat is last: 蘭迪斯 falling on
   the same action that kills 吉歐 is a Game Over in the original, where an
   if/else, an else-if, or the shared test's "only while the code is still 0"
   guard copied onto either store would clear the chapter instead.  In the other
   direction the same absence of a guard is what lets the victory overwrite a
   defeat a chapter event recorded earlier in the action.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line: the guide gives 第27章 魔導士的野望 勝利條件 魔導王死亡, one
   named boss and not 敵人全滅, so the shared test's sweep would clear the
   chapter as soon as the last of the fifty-odd garrison fell with 吉歐 still
   alive.  Because that test never runs, this handler has to carry the defeat
   itself.

   Chapter 27 is chapter id 26 (0x1a) and the dword at 000602f4, twenty-six
   entries into the table based at 0006028c, is 0003b8a0; that table entry is the
   function's only xref, which is why it has no static caller. */
void fdps_chapter_27_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_27_MAGE_KING_SLOT) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 2;
    }
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* 0003b990.  One CALL and a return, with no branch in the body at all -- the
   same bare forward chapters 16, 18 and 21 have.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003b990..0003b993, MOV EBP,ESP at 0003b994, SUB
   ESP,0x0 at 0003b996 -- and nothing in it is ever read, so there is no local
   to name.  The epilogue is the four bare POPs at 0003b9a1..0003b9a4 with no
   MOV ESP,EBP in front of them, which is what an empty local area leaves
   behind, and the RET at 0003b9a5 carries no immediate: the caller cleans, and
   there is nothing to clean.

   CALL 0x0003a2e0 at 0003b99c is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- 勝利條件 敵人全滅 and 失敗條件
   蘭迪斯死亡 -- are both the shared test's own, so a handler that adds nothing
   is the complete rule and not an omission.  Chapter 28 is chapter id 27
   (0x1b), which is neither of the ids -- 0x10 and 0x15 -- the shared test
   singles out, so the slot it watches for the defeat is 0, 蘭迪斯.

   The chapter's scripted business, the three reinforcements that appear along
   the top edge at the end of the player phase on each of the nine turns the
   guide lists -- 2, 4, 6, 7, 10, 12, 14, 16, 18, which is not every even turn:
   it includes 7 and skips 8 -- is carried by a turn-event handler keyed on the
   turn counter; releasing a wave here would fire it once per unit action
   instead of once per turn.

   The dword at 000602f8, twenty-seven entries into the table based at
   0006028c, is 0003b990; that table entry is the function's only xref, which
   is why it has no static caller. */
void fdps_chapter_28_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003b9f0.  One CALL and a return, with no branch in the body at all -- the
   same bare forward chapters 16, 18, 21 and 28 have.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003b9f0..0003b9f3 (53 56 57 55), MOV EBP,ESP at
   0003b9f4, SUB ESP,0x0 at 0003b9f6 in the six-byte imm32 form 81 EC 00 00 00
   00 -- and nothing in it is ever read, so there is no local to name.  The
   epilogue is the four bare POPs at 0003ba01..0003ba04 with no MOV ESP,EBP in
   front of them, which is what an empty local area leaves behind, and the RET
   at 0003ba05 is the one-byte C3: the caller cleans, and there is nothing to
   clean.

   CALL 0x0003a2e0 at 0003b9fc is the whole body.  Nothing is pushed in front
   of it and nothing adjusts ESP after it, so the callee takes no argument;
   nothing reads EAX between the CALL and the RET, so its result is not used
   and this handler returns nothing of its own.  The verdict the callee leaves
   in data_fdps_chapter_event_or_battle_end_code is the answer, and the
   dispatchers read that global directly after the indirect call.

   There is nothing else: no store, no test of the chapter id, no unit lookup.
   The chapter's two stated conditions -- 勝利條件 敵人全滅 and 失敗條件
   蘭迪斯死亡 -- are both the shared test's own, so a handler that adds nothing
   is the complete rule and not an omission.  Chapter 29 is chapter id 28
   (0x1c), which is neither of the ids -- 0x10 and 0x15 -- the shared test
   singles out, so the slot it watches for the defeat is 0, 蘭迪斯.

   The chapter's scripted business is elsewhere and is not missing from here:
   the right, lower-right and upper-middle enemy groups that break cover once a
   player unit crosses the vertical line before the central junction, and the
   general attack that starts once one reaches the upper room's entrance, are
   fdps_chapter_29_event_activate_enemy_groups and
   fdps_chapter_29_event_activate_all_enemies (chevt6.h), the dwords 000395d0
   and 00039670 at 00060278 and 0006027c -- slots 45 and 46 of the
   chapter-script event vector based at 000601c4.  Both are keyed on where a
   unit stands and are armed by map28.dat's tile triggers, so neither can be
   reached from a postlude that runs after every action.

   The dword at 000602fc, twenty-eight entries into the table based at
   0006028c, is 0003b9f0; that table entry is the function's only xref, which
   is why it has no static caller. */
void fdps_chapter_29_post_action(void)
{
    fdps_battle_check_default_end_conditions();
}

/* 0003ba50.  One CALL, one branch, one store and no forward to the shared end
   test.  Nine of the thirty post-action handlers never call 0x0003a2e0 --
   chapters 03, 08, 10, 22, 23, 25, 26, 27 and 30, the twenty-one that do being
   the complete caller list of 0003a2e0 -- and of those nine, chapters 22
   (0003b270), 23 (0003b2e0) and 30 are the three that also never store a 2, so
   defeat is the only verdict any of the three can produce.

   The frame is the standard four-push Watcom one with an empty local area --
   PUSH EBX/ESI/EDI/EBP at 0003ba50..0003ba53 (53 56 57 55), MOV EBP,ESP at
   0003ba54, SUB ESP,0x0 at 0003ba56 in the six-byte imm32 form 81 EC 00 00 00
   00 -- and nothing in it is ever read, so there is no local to name.  The
   epilogue is the four bare POPs at 0003ba74..0003ba77 with no MOV ESP,EBP in
   front of them, which is what an empty local area leaves behind, and the RET
   at 0003ba78 is the one-byte C3: the caller cleans, and there is nothing to
   clean.

   PUSH 0x0 / CALL 0x000109b0 / ADD ESP,0x4 at 0003ba5c..0003ba65 is
   fdps_unit_is_retired(0) with the caller clearing its one argument, and its
   EAX is used at once: TEST EAX,EAX / JZ 0003ba74 at 0003ba66 is the only
   branch in the body, skipping the MOV dword ptr [0x00069da0],0x1 at 0003ba6a.
   That store is the whole of the rest of the function -- the code is never read
   before it is written and no other value is ever stored -- so this handler can
   turn the code into a 1 and can do nothing else with it.  EAX is left holding
   the callee's answer at the RET, but the table's slots are called as void
   f(void) and no dispatch site reads a result, so the handler returns nothing.

   Unit index 0 is 蘭迪斯: unit slot i is roster slot i, the roster is in join
   order and is never permuted, so slot 0 is his on every map that deploys him,
   and chapter 30 names no exclusion from its 己方.

   There is no CALL 0x0003a2e0 here, and that is the chapter's rules rather than
   a missing line.  The shared test declares its victory by sweeping the unit
   list for a live enemy, and the guide gives 第30章 最終聖戰 勝利條件 擊倒平衡
   之神 -- three named bosses, the third of which has to be beaten to end the
   chapter -- against reinforcements it describes as 敵方援軍是永遠清不完.  Two
   ghosts and two 白骨戰士 are put back on the map as fast as they are killed, so
   the sweep for a live enemy can never come up empty and forwarding to the
   shared test would be dead weight on every action rather than a second way to
   win.  The clear is written elsewhere, and the sweep of every instruction
   naming 0x00069da0 -- 59 in the image, none of them a write in any form but
   MOV with an immediate -- says where it can come from.  The fifteen stores of
   2 are the shared test's own at 0003a2f9; one in each of nine other
   post-action handlers, chapters 03 at 0003a507, 08 at 0003a7f0, 10 at
   0003a941, 15 at 0003ac06, 19 at 0003af4a, 24 at 0003b49a, 25 at 0003b6ea, 26
   at 0003b7aa and 27 at 0003b8ba; the three named boss-defeat events, chapter
   15's at 00037caf and 00037cbb, 22's at 00038936 and 23's at 00038a12; and
   the opcode-4 arm of fdps_run_death_scripts at 0001dcfb.  Chapter 30 owns
   none of the first fourteen -- a handler and the shared test are reached only
   through the table slot of the chapter being played, and the boss-defeat
   events are chapters 15's, 22's and 23's -- which leaves 0001dcfb as the only
   store of 2 a chapter-30 battle can execute.  So the third 平衡之神's death
   reaches the code as a death-script record, and this handler must not be able
   to raise a 2 at all.

   The store carries no "only while the code is still 0" guard, unlike the two
   the shared test puts around its own writes, and copying that guard here by
   analogy changes behaviour: a defeat that lands on the same action as the
   scripted clear overwrites the 2 with a 1 in the original, and the player gets
   a Game Over where the guarded version would clear the last chapter.  The
   value read back is the value this handler last wrote.

   The address reaches the dispatchers only as the dword at 00060300, twenty-nine
   entries into the table based at 0006028c, which is why the function has no
   static caller: slot 29 is chapter 30, and it is the table's last slot -- the
   dword at 00060304 is 0003a410, fdps_chapter_01_end, the first entry of the
   separate chapter-end table. */
void fdps_chapter_30_post_action(void)
{
    if (fdps_unit_is_retired(0) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}
