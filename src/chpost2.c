/* chpost2.c -- the per-chapter post-action handlers, chapters 16 to 24: the
 * win/lose test the battle loop runs after a unit has acted.
 *
 * These are slots 15 to 23 of the handler table based at 0006028c, indexed by
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
   path out of that handler.  The boss is herself a side-0 record, MAP21.DAT
   record 7, so while she stands the shared sweep never clears the chapter
   either; a forward added here changes the outcome only when she is retired
   without her death script running -- a poison death
   (program_info/known_bugs.md item 23) -- which the original can then never
   clear and the forwarded version clears once every side-0 unit is down.

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

/* Chapter 23's post-action test.  Two CALLs to fdps_unit_is_retired, one
   message and two stores -- and, like chapter 22's handler above it, no forward
   to the shared end test at all.

   0003b2e0.  The frame is the standard four-push Watcom one with an empty
   local area -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 at
   0003b2e0..0003b2e6 -- and nothing in it is ever read, so there is no local to
   name.  No argument is read from [EBP+8] or above; the epilogue is the four
   bare POPs at 0003b341..0003b344 with no MOV ESP,EBP in front of them, which
   is what an empty local area leaves behind, and the RET at 0003b345 carries
   no immediate.  Both of this body's own calls push their arguments and clear
   them afterwards -- ADD ESP,0x4 at 0003b2f3 and 0003b30d, ADD ESP,0x1c at
   0003b334 -- which is the stack convention, caller cleans.

   The two tests are nested, not sequential, and that is the whole shape of the
   function.  PUSH 0x3 / CALL 0x000109b0 / ADD ESP,0x4 at 0003b2ec..0003b2f3 is
   fdps_unit_is_retired(3) and its EAX is used at once: TEST EAX,EAX / JZ
   0003b306 at 0003b2f6 goes to the second test only when slot 3 is standing.
   Where it falls through, MOV dword ptr [0x00069da0],0x1 at 0003b2fa stores the
   defeat and JMP 0x0003b341 at 0003b304 leaves over the top of everything else
   -- so a retired slot 3 draws nothing.  The second test, PUSH 0x1f / CALL
   0x000109b0 / ADD ESP,0x4 at 0003b306..0003b30d, has its EAX tested the same
   way at 0003b310, and its JZ 0003b341 skips the message AND the store
   together: the only path that paints is the one where slot 3 is standing and
   slot 0x1f is not.

   The message is the seven-argument full-screen form of fdps_draw_text
   (text.h): the pushes at 0003b314..0003b329 are, in reverse, the chapter text
   block at 0x00060124, entry 0x14, the mode 13h aperture at 0xa0000, a pitch of
   0x140 and the three standard message colours 0xd0, 0 and 0x6d.  The trailing
   three are glyph colours forwarded to fdps_draw_glyph, not a rectangle.  The
   cursor fdps_draw_text hands back in EAX is discarded -- the ADD ESP,0x1c at
   0003b334 is followed straight by the store at 0003b337, and nothing between
   there and the RET reads EAX.

   Writing the natural "lose if A or B" here -- two independent ifs, or one if
   with ||, with the message after them -- changes what the player sees on the
   losing turn: with slot 3 already retired the original declares the defeat in
   silence even when slot 0x1f is gone too, and the flattened form paints entry
   0x14 over the map first.

   Neither store carries a "only while the code is still 0" guard, unlike the
   two the shared test at 0003a2e0 puts around its own writes, and adding one by
   analogy changes behaviour: a defeat found on the same action as the scripted
   boss-defeat clear overwrites the 2 with a 1 in the original, and the player
   gets a Game Over where the guarded version would clear the chapter.

   There is no CALL 0x0003a2e0 here and that is deliberate rather than a missing
   line.  The guide gives 第23章 死神冥河 勝利條件 擊倒死神 -- one named boss,
   not 敵人全滅 -- so the clear is the scripted fdps_chapter_23_event_boss_defeat
   at 00038950 to write.  A forward added here would not clear the chapter
   early: the 死神 is a side-0 record, MAP22.DAT record 52, so the shared sweep
   finds him standing until he falls.  It would lose it instead: chapter 23's
   id, 0x16, is not one the shared test substitutes slot 3 for, so it would ask
   about slot 0, and the 蘭迪斯 there is retired by ICON22.DAT's opening
   RETIRE_UNIT -- the first action would record a defeat.

   Unit slot 3 is 法蓮娜, the first of the chapter's two stated 失敗條件: unit
   slot i is roster slot i, the roster is in join order and is never permuted,
   and chapter 23 deploys 蘭迪斯以外的所有人, so the slot 0 the shared test
   watches in most chapters holds a 蘭迪斯 the opening cut-scene has already
   retired.  fdps_chapter_23_init parking the map cursor on unit 3 is the same
   reading of the chapter.

   Slot 0x1f is NOT a roster slot.  The eleven player slots come first and
   map22.dat's wave-0 records 1 to 21 follow them in record order, so slot 0x1f
   is record 21: side 1, character 0x24, AI behaviour 7 with destination
   (24, 3), starting from MAP22.COD's (24, 24) -- the last of the column of the
   dead walking to the gate of hell.  It retires when it reaches the gate, as
   it does when an enemy strikes its one hit point down, and that is the
   chapter's second stated 失敗條件, 蘭迪斯從戰場上方消失: entry 0x14 of
   FDETXT23.TXT is 尤利安's 蘭迪斯！！！ and 法蓮娜's 不～要～～～！.

   The address reaches the dispatchers only as the dword at 000602e4,
   twenty-two entries into the table based at 0006028c, which is why the
   function has no static caller: slot 22 is chapter 23. */

/* 法蓮娜, the chapter's first 失敗條件. */
#define CHAPTER_23_FARLENA_UNIT_INDEX 3

/* The map unit whose retirement is the chapter's second defeat, announced with
   entry 0x14 before the code is written: map22.dat's deployment record 21, the
   last of the dead walking to the gate (see the note above). */
#define CHAPTER_23_SECOND_LOSS_UNIT_INDEX 0x1f

/* Entry 0x14 of the chapter text block, the line the second defeat paints. */
#define CHAPTER_23_SECOND_LOSS_TEXT_ID 0x14

void fdps_chapter_23_post_action(void)
{
    if (fdps_unit_is_retired(CHAPTER_23_FARLENA_UNIT_INDEX) != 0) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    } else if (fdps_unit_is_retired(CHAPTER_23_SECOND_LOSS_UNIT_INDEX) != 0) {
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHAPTER_23_SECOND_LOSS_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE, VGA_SCREEN_PITCH,
                       MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* The latch element, byte ptr [0x000640e9] at 0003b3dc, 0003b4b8 and 0003b5ee
   -- element 0x11 of the 32-entry per-cell event flag array based at 000640d8
   (gamedata.h), which fdps_chapter_state_reset clears at 00022782 when a
   chapter starts.  Chapter 19's duel uses the same element; the two chapters
   never run together and that reset is what keeps them apart. */
#define CHAPTER_24_DUEL_LATCH_SLOT 0x11
#define CHAPTER_24_DUEL_LATCH_SET 1

/* The two duellists, PUSH 0x4 at 0003b3ef, 0003b445, 0003b4c3 and 0003b4d8 and
   PUSH 0x52 at 0003b3fd and 0003b40f.

   4 is 裘娜's battle-map slot -- unit slot i is roster slot i and the roster is
   in join order.  0x52 is the challenger, and it is a HARD-CODED INDEX rather
   than a handle on the unit fdps_deploy_wave has just appended: it names him
   only while the array already holds exactly 0x52 units at the moment the offer
   is accepted, which is after all five of this map's turn waves have arrived.

   The retire sweep is NOT what an early clear breaks.  Its bound is the live
   count less one and the challenger is the record the deployment has just put
   at the end, so he is spared wherever he lands.  What breaks is the settle
   test: with fewer units on the map the challenger is appended below 0x52 and
   fdps_unit_is_retired(0x52) then asks about a slot past the array's last
   record rather than about him.  That is shipped behaviour; resolving the
   challenger from what the deployment appended would quietly change it.

   The guide records both chapters' early-clear results and they differ exactly
   as the two handlers do.  Chapter 19, whose sweep bound IS a literal and so
   does reach its challenger: 若第五回合以前便結束，則狂戰士會消失，造成本章無法
   結束 -- the challenger disappears and the chapter can no longer be ended.
   Chapter 24: 若第七回合以前便結束，則兩人對戰的第一回合己方結束時，狂戰士便
   自動認輸 -- he is still there and forfeits on the first turn, which is what
   the settle test answering about a slot he never occupied looks like from the
   player's side. */
#define CHAPTER_24_JUNA_UNIT_INDEX 4
#define CHAPTER_24_CHALLENGER_UNIT_INDEX 0x52

/* The two swords the duel trades, PUSH 0xa6 at 0003b440 and 0003b4d3 and PUSH
   0xa7 at 0003b466: 妖刀村正 and 妖刀正宗 (assets/items.md).  The 妖刀村正 is
   both the stake -- the offer is not made to a 裘娜 who is not carrying it --
   and what is taken off her when she wins; chapter 19's duel is where she got
   it.  The guide's chapter 24 entry describes exactly that trade,
   如果之前有取得妖刀村正 ... 打贏他就可換到他手中的妖刀正宗. */
#define CHAPTER_24_MURAMASA_ITEM_ID 0xa6
#define CHAPTER_24_MASAMUNE_ITEM_ID 0xa7

/* What fdps_unit_find_item_slot answers when the unit is not carrying the item,
   CMP dword ptr [EBP + -0xc],-0x1 at 0003b452 and CMP EAX,-0x1 at 0003b4e2
   (unititem.h). */
#define CHAPTER_24_ITEM_SLOT_NONE (-1)

/* The six FDETXT entries the handler speaks, each a literal in the instruction
   stream: the duel won at 0003b430, the duel lost at 0003b48a, the challenge at
   0003b512, the question at 0003b53f, the acceptance at 0003b575 and the
   refusal at 0003b5de. */
#define CHAPTER_24_DUEL_WON_TEXT_ID 0x19
#define CHAPTER_24_DUEL_LOST_TEXT_ID 0x1a
#define CHAPTER_24_CHALLENGE_TEXT_ID 0x15
#define CHAPTER_24_QUESTION_TEXT_ID 0x16
#define CHAPTER_24_ACCEPTED_TEXT_ID 0x17
#define CHAPTER_24_DECLINED_TEXT_ID 0x18

/* The last turn the offer is still made on, CMP dword ptr [0x00069ce8],0x19 /
   JG 0x0003b4b6 at 0003b4a4: the branch that skips the offer is taken only for
   a turn counter strictly greater than 0x19, so turn 25 still gets the duel and
   turn 26 does not.  JG and not JA, so the comparison is signed.  The guide
   gives the same number in words -- 本章務必在25回合內結束. */
#define CHAPTER_24_DUEL_LAST_TURN 0x19

/* What fdps_deploy_wave is asked for, PUSH 0x7 at 0003b4ef and the XOR EAX,EAX
   / PUSH EAX at 0003b4ec: the current map's wave 7, placed on the nearest free
   tile rather than exactly on its placement record's own (deploy.h).  Wave 7 is
   the one wave fdps_chapter_24_event_deploy_wave_for_turn never brings on --
   its five calls at 00038d0a, 00038d5c, 00038da0, 00038e07 and 00038e48 push
   waves 2, 6, 4, 3 and 5 -- so the challenger reaches the map through this
   handler alone.  MAP23.DAT carries exactly one wave-7 deployment record, its
   record 71: enemy side, character record 0x23 at level 40, which is the one
   unit on the map built from a FRIAPRDA.DAT form rather than an ENEMYDAT.DAT
   one. */
#define CHAPTER_24_DUEL_WAVE 7
#define CHAPTER_24_PLACE_NEAREST_FREE_TILE 0

/* The FACE.CEL record the panel reveal carries, PUSH 0x23 at 0003b522: the
   challenger's portrait, the same record chapter 19's duel uses.  It is also
   his character record index -- MAP23.DAT's wave-7 record names form 0x23 --
   which is what a portrait index under 60 means (assets/characters.md). */
#define CHAPTER_24_DUEL_SPEAKER_FACE_INDEX 0x23

/* The answer that accepts, CMP dword ptr [EBP + -0xc],0x0 / JNZ 0x0003b5cb at
   0003b55c.  fdps_prompt_two_choice answers 0 for the left cell, 1 for the
   right and -1 for a cancel (msgwin.h), and this is an equality against 0, so a
   cancel declines exactly as the right cell does. */
#define CHAPTER_24_DUEL_ANSWER_ACCEPT 0

/* What each swept record's flags byte is left holding, MOV byte ptr
   [EAX + 0x5],0x1 at 0003b5b9.  IT IS A WHOLE-BYTE STORE AND NOT AN OR: it
   raises the retired bit 0x01 that fdps_unit_is_retired reads and drops the
   has-acted bit 0x80 along with everything else that byte was carrying. */
#define CHAPTER_24_RETIRED_FLAG_BYTE 1

/* 0003b3d0.  Chapter 24's post-action test: the same two-halves shape as
   chapter 19's above, with a second duel at the chapter's exit.

   The frame is the standard four-push Watcom one with a 0xc-byte local area --
   PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0xc at 0003b3d0..0003b3d6 -- and
   the epilogue is MOV ESP,EBP followed by the four POPs at 0003b5f5..0003b5fa,
   which is what a non-empty local area leaves behind; the RET at 0003b5fb
   carries no immediate.  No argument is read from [EBP+8] or above, and every
   call in the body pushes its arguments and clears them itself -- ADD ESP,0x4,
   0x8, 0xc and 0x1c -- which is the stack convention, caller cleans.

   THE DUEL HALF MUST NOT FALL BACK ON THE SHARED TEST, and the CMP byte ptr
   [0x000640e9],0x0 / JNZ at 0003b3dc..0003b3e3 is what keeps it from doing so.
   The accepted branch below retires unit 0, 蘭迪斯, along with everyone else
   but 裘娜, so fdps_battle_check_default_end_conditions would force the defeat
   code 1 the moment the duel started.  Writing the obvious "run the default
   check first, then add the chapter's extra" turns the duel into an instant
   Game Over.

   The duel is settled by three calls to fdps_unit_is_retired and not two.  PUSH
   0x4 / CALL 0x000109b0 at 0003b3ef with TEST EAX,EAX / JNZ at 0003b3f9 and
   PUSH 0x52 / CALL at 0003b3fd with TEST EAX,EAX / JZ 0x0003b4a4 at 0003b407
   are a short-circuiting || chain: while both duellists are standing control
   leaves for the offer half and NOTHING AT ALL IS WRITTEN, which is what keeps
   the battle loop running the duel.  Once one of them is down the third call at
   0003b411 asks about the challenger again to see which, and its TEST EAX,EAX /
   JZ 0x0003b477 at 0003b419 picks the arm: challenger retired is 裘娜's win,
   anything else is her loss.

   Her win is the only path that moves an item.  The slot
   fdps_unit_find_item_slot answers with is kept at [EBP-0xc] and compared
   against -1 at 0003b452, so the 妖刀村正 is taken off her only if she still
   has it, while fdps_unit_add_item hands over the 妖刀正宗 either way -- the
   add is outside the branch, at 0003b46d, and not its else.  Both arms then
   join at MOV dword ptr [0x00069da0],0x2 at 0003b49a, so the chapter ends
   whichever way the duel went.

   The offer half is one short-circuiting && chain of five, every failure
   jumping to the same exit at 0003b5f5: the turn counter 0x19 or less (signed),
   the battle-end code already 2, the latch still down, 裘娜 not retired, and
   the 妖刀村正 in her bag.  The second of those is what makes this an exit rite
   rather than an event: the offer is made on the action that has just cleared
   the chapter, and on no other.

   Everything after that runs in source order with nothing conditional in it
   until the answer: the challenger is appended as the map's wave 7, the
   challenge is spoken on the visible page, the panel is revealed under FACE.CEL
   record 0x23, the question is written inside it, fdps_prompt_two_choice runs
   the modal prompt and the panel is retracted before the answer is looked at.
   Only fdps_prompt_two_choice's result is used -- it is stored at 0003b554 and
   every draw's returned cursor is discarded.

   THE ACCEPTED BRANCH LEAVES THE BATTLE OPEN, and that is the whole point of
   it.  It retires every unit index the sweep reaches except 裘娜 -- so 蘭迪斯
   and the entire party go with them -- and then puts the battle-end code BACK
   to 0 at 0003b5bf so the phase loop resumes with only the two duellists on the
   map.  fdps_get_unit_record is called for index 4 as well and its answer is
   simply discarded; the CMP dword ptr [EBP + -0x8],0x4 / JZ at 0003b5b0 guards
   the store alone.  Declining writes its refusal line and nothing else, so the
   cleared code stands and the chapter ends.

   THE SWEEP'S BOUND IS READ FROM THE LIVE UNIT COUNT, NOT FROM A LITERAL, which
   is the difference from chapter 19's otherwise identical branch: MOV
   EAX,[0x00060150] / DEC EAX / CMP EAX,dword ptr [EBP + -0x8] / JG at
   0003b58c..0003b595 re-reads data_fdps_map_unit_count on every iteration and
   stops one short of the end of the array, which is where fdps_deploy_wave has
   just put the challenger.  The comparison is JG and not JA, so it is signed.

   Either answer raises the latch at 0003b5ee, which is outside the if/else and
   is therefore what makes the offer one-shot; fdps_chapter_24_end reads the
   same element at 0003b60c.

   THE REFUSAL LINE IS DRAWN INSIDE A PANEL THAT IS NO LONGER THERE.  PUSH
   0xaa44a at 0003b5d9 sends it to the panel's interior pen, but
   fdps_message_window_close has already run at 0003b557 and repainted the whole
   visible page; the acceptance line at 0003b570 goes to the screen origin
   instead.  Giving the two lines the same destination is the natural tidy-up
   and it moves where the refusal appears.

   Table slot 23: the dword at 000602e8, twenty-three entries into the table
   based at 0006028c, is 0003b3d0, and that table entry is the function's only
   xref. */
void fdps_chapter_24_post_action(void)
{
    /* Where 裘娜 is keeping the 妖刀村正, or -1 when she is not carrying it:
       the slot fdps_unit_find_item_slot answered with, kept at [EBP-0xc]. */
    int muramasa_slot;
    /* Which cell of the offer the player committed to: 0 accepts, 1 declines
       and -1 is a cancel.  The original keeps it in the same [EBP-0xc] the
       item slot above used, the two never being live at once. */
    int duel_answer;
    /* The retire sweep's counter, [EBP-0x8]. */
    int unit_index;
    /* The record the sweep's store is made through, [EBP-0x4]. */
    struct fdps_unit_record *unit;

    if (data_fdps_map_cell_event_triggered_flags[CHAPTER_24_DUEL_LATCH_SLOT]
            == 0) {
        fdps_battle_check_default_end_conditions();
    } else if (fdps_unit_is_retired(CHAPTER_24_JUNA_UNIT_INDEX) != 0 ||
               fdps_unit_is_retired(CHAPTER_24_CHALLENGER_UNIT_INDEX) != 0) {
        if (fdps_unit_is_retired(CHAPTER_24_CHALLENGER_UNIT_INDEX) != 0) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_24_DUEL_WON_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            muramasa_slot =
                fdps_unit_find_item_slot(CHAPTER_24_JUNA_UNIT_INDEX,
                                         CHAPTER_24_MURAMASA_ITEM_ID);
            if (muramasa_slot != CHAPTER_24_ITEM_SLOT_NONE) {
                fdps_unit_remove_item(CHAPTER_24_JUNA_UNIT_INDEX,
                                      muramasa_slot);
            }
            fdps_unit_add_item(CHAPTER_24_JUNA_UNIT_INDEX,
                               CHAPTER_24_MASAMUNE_ITEM_ID);
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_24_DUEL_LOST_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }
        data_fdps_chapter_event_or_battle_end_code =
            BATTLE_END_CHAPTER_CLEARED;
    }

    if (data_fdps_battle_turn_counter <= CHAPTER_24_DUEL_LAST_TURN &&
        data_fdps_chapter_event_or_battle_end_code ==
            BATTLE_END_CHAPTER_CLEARED &&
        data_fdps_map_cell_event_triggered_flags[CHAPTER_24_DUEL_LATCH_SLOT]
            == 0 &&
        fdps_unit_is_retired(CHAPTER_24_JUNA_UNIT_INDEX) == 0 &&
        fdps_unit_find_item_slot(CHAPTER_24_JUNA_UNIT_INDEX,
                                 CHAPTER_24_MURAMASA_ITEM_ID)
            != CHAPTER_24_ITEM_SLOT_NONE) {

        fdps_deploy_wave(data_fdps_chapter_current_chapter_id,
                         CHAPTER_24_DUEL_WAVE,
                         CHAPTER_24_PLACE_NEAREST_FREE_TILE);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHAPTER_24_CHALLENGE_TEXT_ID,
                       (unsigned char *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        fdps_message_window_open(CHAPTER_24_DUEL_SPEAKER_FACE_INDEX);
        fdps_draw_text(data_fdps_current_chapter_text_ptr,
                       CHAPTER_24_QUESTION_TEXT_ID,
                       (unsigned char *) PANEL_TEXT_ORIGIN,
                       VGA_SCREEN_PITCH, MESSAGE_FG_COLOR, MESSAGE_BG_COLOR,
                       MESSAGE_OUTLINE_COLOR);
        duel_answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (duel_answer == CHAPTER_24_DUEL_ANSWER_ACCEPT) {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_24_ACCEPTED_TEXT_ID,
                           (unsigned char *) VGA_SCREEN_BASE,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            for (unit_index = 0;
                 unit_index < data_fdps_map_unit_count - 1;
                 unit_index++) {
                unit = fdps_get_unit_record(unit_index);
                if (unit_index != CHAPTER_24_JUNA_UNIT_INDEX) {
                    unit->flags = CHAPTER_24_RETIRED_FLAG_BYTE;
                }
            }
            data_fdps_chapter_event_or_battle_end_code =
                BATTLE_END_BATTLE_CONTINUES;
        } else {
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHAPTER_24_DECLINED_TEXT_ID,
                           (unsigned char *) PANEL_TEXT_ORIGIN,
                           VGA_SCREEN_PITCH, MESSAGE_FG_COLOR,
                           MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
        }

        data_fdps_map_cell_event_triggered_flags[CHAPTER_24_DUEL_LATCH_SLOT] =
            CHAPTER_24_DUEL_LATCH_SET;
    }
}
