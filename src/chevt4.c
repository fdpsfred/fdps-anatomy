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
