/* btlact.c -- the commands an acting unit opens on its own turn.
 *
 * See btlact.h for what each one answers.  Everything here is a driver: the
 * ring the action menu is drawn as, the sweep animations and the cursor loop
 * all live in menu.c, and what this file contributes is which commands the
 * unit may use this turn, what each of them runs, and -- for the fall-through
 * command, which has no other caller -- the whole of the cell search.  The
 * battle screen's system menu is btlmenu.c.
 *
 * malloc and free come from <stdlib.h> and delay from <i86.h>, which is where
 * Watcom 10.0a declares each of them; all of them are real calls in the
 * original -- CALL 0x0003d375 at 00015ee7 and CALL 0x0003d478 at 00015f5f for
 * malloc and free, and CALL 0x0003d370 at 000188bd for delay -- because the
 * flag set carries no -oi (rebuild_info/build_flags.md), so the plain
 * declarations are what reproduce them.
 */
#include <stddef.h>
#include <stdlib.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "aitarget.h"
#include "audio.h"
#include "btlturn.h"
#include "chapter.h"
#include "combat.h"
#include "death.h"
#include "item.h"
#include "keybd.h"
#include "mapcur.h"
#include "mapdraw.h"
#include "maptile.h"
#include "menu.h"
#include "movegrid.h"
#include "msgwin.h"
#include "spellmnu.h"
#include "table.h"
#include "text.h"
#include "unit.h"
#include "unitatk.h"
#include "unititem.h"
#include "unitstat.h"
#include "btlact.h"

/* The descriptor value that greys an entry out: MOV dword ptr [EAX],0x1 at
   00015d40 and 00015dca, MOV dword ptr [EAX+0x8],0x1 at 00015de8, and MOV
   dword ptr [EAX+0x4],0x1 at 00015e04 and 00015e26.  The ring reads any
   non-zero as greyed (menu.h); one is what this menu writes. */
#define MENU_ENTRY_DISABLED 1

/* What fdps_menu_cursor_input_loop and fdps_prompt_two_choice answer with.
   The cursor loop's -1, tested by CMP dword ptr [EBP-0x3c],-0x1 at 00015eba,
   is what ends the action ring; the prompt's 0, tested by CMP dword ptr
   [EBP-0x1c],0x0 at 000185da, is the affirmative and both of its other
   answers decline (menu.h, msgwin.h). */
#define MENU_CURSOR_CANCELLED (-1)
#define PROMPT_ANSWER_YES 0

/* Where in the message panel a line goes, and in what colours.  0xaa44a is
   screen (138, 131), the panel's first text row, with 0x140 the mode 13h row
   stride.  It is an address inside the display adapter's aperture rather than
   the address of anything the linker places, so it stays a literal (contract
   E, rebuild_info/pitfalls.md).  The three colours are the standard message
   colours, PUSH 0x6d / PUSH 0x0 / PUSH 0xd0 in front of every one of the nine
   calls the search makes. */
#define MESSAGE_QUESTION_AT 0x000aa44a
#define VGA_SCREEN_PITCH 0x140
#define MESSAGE_TEXT_FG_COLOR 0xd0
#define MESSAGE_TEXT_BG_COLOR 0
#define MESSAGE_TEXT_OUTLINE_COLOR 0x6d

/* One map tile is 24 pixels square, MOV EBX,0x18 in front of each of the six
   IDIVs at 00018503 and 00018519, at 00015d8f and 00015da5, and at 00015f00
   and 00015f16: the two cursor globals hold world pixels and everything
   reached from here takes tile indices.

   THE DIVIDE IS SIGNED, CDQ-shaped -- the value is loaded into EAX, loaded a
   second time into EDX and SAR EDX,0x1f before the IDIV -- so a coordinate
   that ever went negative rounds toward zero rather than walking off into a
   huge quotient.  The cursor globals are signed ints (gamedata.h), which is
   what makes that the divide the compiler emits. */
#define MAP_TILE_SIZE 0x18

/* The 0x60 field of a tile's attribute byte is a four-value enumeration and
   not two independent bits -- maptile.c says so from the other side, where
   fdps_map_find_chest_cell wants exactly 0x20 and the repaint pass takes 0x20
   and 0x60.  The search below takes the two the player can be offered: the
   chest class 0x20 and the buried-treasure class 0x40, spelled as the pair of
   rejections CMP dword ptr [EBP-0xc],0x0 at 00018550 and CMP ...,0x60 at
   00018556. */
#define CELL_CLASS_MASK 0x60
#define CELL_CLASS_NONE 0x00
#define CELL_CLASS_CHEST 0x20
#define CELL_CLASS_BURIED 0x40
#define CELL_CLASS_RESERVED 0x60

/* The searchable-cell table inside the resident MAP%02d.DAT block: three
   bytes per cell event code, from the MOVSX word ptr [EAX+0x54] at 00018600
   and the MOV AL,byte ptr [EDX+0x53] at 00018617 against an index scaled by
   LEA EAX,[EAX+EAX*0x2].  It sits directly behind the two-byte per-cell event
   table maptile.h describes at 0x33: sixteen entries of two bytes end exactly
   at 0x53.

   THE KIND BYTE IS WIDENED WITHOUT SIGN AND THE PAYLOAD WORD WITH IT.  The
   kind is XOR EAX,EAX / MOV AL and the payload is MOVSX, so a payload with the
   top bit set is a negative amount of gold rather than a large one, and that
   is the arithmetic the money arm does (contract C). */
#define SEARCH_CELL_KIND_AT 0x53
#define SEARCH_CELL_PAYLOAD_AT 0x54
#define SEARCH_CELL_RECORD_BYTES 3

/* What the kind byte selects, from CMP dword ptr [EBP-0x18],0x0 at 0001861d
   and CMP ...,0x1 at 00018818.  Everything from 2 up is the scripted class and
   there is no upper test on it. */
#define SEARCH_CELL_KIND_ITEM 0
#define SEARCH_CELL_KIND_MONEY 1

/* The nine entries of data_fdps_all_game_text_ptr this function draws, pushed
   at 000185c0 (through the slot at [EBP-0x8]), 00018660, 000186b4, 0001877e,
   000187ba, 000187fb, 0001885c, 00018884 and 0001890f.  Each name says which
   branch draws the line, which is what the assembly settles; the strings
   themselves live in the chapter text block and are not read here. */
#define TEXT_SEARCH_CHEST_PROMPT 0x20a
#define TEXT_SEARCH_BURIED_PROMPT 0x20b
#define TEXT_SEARCH_DECLINED 0x20c
#define TEXT_SEARCH_ITEM_TAKEN 0x20d
#define TEXT_SEARCH_BAG_FULL 0x20e
#define TEXT_SEARCH_ITEM_SWAPPED 0x20f
#define TEXT_SEARCH_NOTHING_TAKEN 0x210
#define TEXT_SEARCH_MONEY_NONE 0x211
#define TEXT_SEARCH_MONEY_TAKEN 0x212

/* Where an item's name sits in the one text block: entry 0xc9 plus its
   ITEM.DAT id, ADD EAX,0xc9 at 0001862a and 0001875c.  The same numbering the
   shop, the status panel and the death scripts add. */
#define ITEM_NAME_TEXT_BASE 0xc9

/* What the two inventory calls answer with: fdps_unit_add_item's -1 for a full
   bag, tested by CMP EAX,-0x1 at 00018683, and the item list's -1 for a
   cancel, tested by CMP dword ptr [EBP-0x1c],-0x1 at 000186f3 (unititem.h).
   The 0 pushed at 000186e2 is the list's usable_only argument: any entry the
   cursor is on may be offered up, not only a usable one. */
#define ADD_ITEM_BAG_FULL (-1)
#define ITEM_SELECT_CANCELLED (-1)
#define ITEM_SELECT_ANY_ENTRY 0

/* How long the scripted class is left on the closed panel before the handler
   takes over, PUSH 0xc8 at 000188b8. */
#define SCRIPTED_CELL_HOLD_MS 200

/* The sound the search plays once the player has said yes, PUSH 0x61600 at
   000185e4.  It is passed as a writable string because the lookup upper-cases
   it in the caller's own storage (audio.h, rebuild_info/pitfalls.md). */
#define SEARCH_CONFIRMED_SOUND "Chess.wav"

/* 000184f0.  See btlact.h for what a cell can hold and what each answer
   does.

   THE GUARDS ARE WRITTEN AS EARLY RETURNS.  The original is one `if` around
   the whole body with a single epilogue; the arms below return instead, which
   is the same program with the nesting taken out (ADR-0001).

   EVERY LINE GOES TO THE PANEL'S FIRST TEXT ROW.  All nine calls push 0xaa44a
   -- MESSAGE_QUESTION_AT above, named for the row and not for the kind of line
   -- with the same pitch and the same three colours, so an answer replaces the
   question in place rather than appearing under it the way the system
   submenu's answers do.

   THE PANEL IS CLOSED AND REOPENED BETWEEN THE QUESTION AND THE ANSWER on
   every arm but one.  The exception is the bag-full line at 000186a1, which is
   drawn over the window the item line is already on; every other transition
   runs fdps_message_window_close followed by a fresh fdps_message_window_open
   on the same portrait.

   THE CELL RECORD IS REACHED THROUGH data_fdps_tile_event_data_table_ptr FOUR
   TIMES and the pointer is loaded from the global on each of them, so a
   handler that moved the block between two of them would be seen. */
void fdps_battle_search_cell_at_cursor(int unit_index)
{
    /* The unit doing the searching, resolved once at 00018573 and read only
       for the portrait byte every window below is opened on. */
    struct fdps_unit_record *searching_unit;
    /* The cell's event code, latched out of the tile-info block before the
       first prompt: it indexes both the searched-flag table and the cell
       record, and fdps_map_apply_triggered_cell_changes overwrites the
       tile-info globals with the map's last cell on its way past. */
    int cell_event_code;
    /* Which of the four values the attribute byte's 0x60 field holds. */
    int cell_class;
    /* Which of the two questions this class asks. */
    int prompt_text_id;
    /* What the last modal thing answered.  The original keeps both prompts'
       answers and the item list's in one stack slot, [EBP-0x1c]: 0 is the
       prompt's yes and -1 is the item list's cancel. */
    int menu_answer;
    /* The cell record's kind byte and its payload word -- an item id, an
       amount of gold or a chapter-event handler index, decided by the kind. */
    int cell_kind;
    int cell_payload;
    /* The inventory slot the player offers up when the bag is full, and the id
       of the item that was in it.  The slot is zeroed at 000184fc, before
       anything can ask for it, and the item list writes into it. */
    int offered_slot;
    int offered_item_id;

    offered_slot = 0;

    fdps_map_load_tile_info(data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                            data_fdps_map_cursor_world_y / MAP_TILE_SIZE);
    cell_event_code = (int) data_fdps_map_current_cell_event_code;
    cell_class = (int) (data_fdps_map_current_tile_attr_flags
                        & CELL_CLASS_MASK);

    if (cell_class == CELL_CLASS_NONE || cell_class == CELL_CLASS_RESERVED) {
        return;
    }
    if (data_fdps_map_cell_event_triggered_flags[cell_event_code] != 0) {
        return;
    }

    searching_unit = fdps_get_unit_record(unit_index);
    fdps_message_window_open((int) searching_unit->portrait_id);

    if ((cell_class & CELL_CLASS_MASK) == CELL_CLASS_CHEST) {
        prompt_text_id = TEXT_SEARCH_CHEST_PROMPT;
    } else {
        prompt_text_id = TEXT_SEARCH_BURIED_PROMPT;
    }
    fdps_draw_text(data_fdps_all_game_text_ptr, prompt_text_id,
                   (unsigned char *) MESSAGE_QUESTION_AT, VGA_SCREEN_PITCH,
                   MESSAGE_TEXT_FG_COLOR, MESSAGE_TEXT_BG_COLOR,
                   MESSAGE_TEXT_OUTLINE_COLOR);
    menu_answer = fdps_prompt_two_choice();

    if (menu_answer != PROMPT_ANSWER_YES) {
        fdps_message_window_close();
        fdps_message_window_open((int) searching_unit->portrait_id);
        fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SEARCH_DECLINED,
                       (unsigned char *) MESSAGE_QUESTION_AT,
                       VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                       MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
        fdps_message_window_close();
        return;
    }

    fdps_play_sfx(SEARCH_CONFIRMED_SOUND);
    cell_payload = (int) *(short *) (data_fdps_tile_event_data_table_ptr
                                     + SEARCH_CELL_PAYLOAD_AT
                                     + cell_event_code
                                       * SEARCH_CELL_RECORD_BYTES);
    cell_kind = (int) data_fdps_tile_event_data_table_ptr[
                          SEARCH_CELL_KIND_AT
                          + cell_event_code * SEARCH_CELL_RECORD_BYTES];

    if (cell_kind == SEARCH_CELL_KIND_ITEM) {
        /* The name of what was found is published before the line that reads
           it, and the line is drawn BEFORE the bag is asked whether it fits:
           the player is told the item was found even on the run where it
           turns out there is no room for it. */
        data_fdps_dialog_last_action_text_id_param = cell_payload
                                                     + ITEM_NAME_TEXT_BASE;
        fdps_message_window_close();
        fdps_message_window_open((int) searching_unit->portrait_id);
        fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SEARCH_ITEM_TAKEN,
                       (unsigned char *) MESSAGE_QUESTION_AT,
                       VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                       MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);

        if (fdps_unit_add_item(unit_index, cell_payload)
                != ADD_ITEM_BAG_FULL) {
            data_fdps_map_cell_event_triggered_flags[cell_event_code] = 1;
            fdps_message_window_close();
            fdps_map_apply_triggered_cell_changes();
            return;
        }

        fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SEARCH_BAG_FULL,
                       (unsigned char *) MESSAGE_QUESTION_AT,
                       VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                       MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
        menu_answer = fdps_prompt_two_choice();
        fdps_message_window_close();

        if (menu_answer != PROMPT_ANSWER_YES) {
            fdps_message_window_open((int) searching_unit->portrait_id);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_SEARCH_NOTHING_TAKEN,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR,
                           MESSAGE_TEXT_OUTLINE_COLOR);
            fdps_message_window_close();
            return;
        }

        menu_answer = fdps_unit_item_select_window(unit_index,
                                                   ITEM_SELECT_ANY_ENTRY,
                                                   &offered_slot);
        if (menu_answer == ITEM_SELECT_CANCELLED) {
            fdps_message_window_open((int) searching_unit->portrait_id);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_SEARCH_NOTHING_TAKEN,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR,
                           MESSAGE_TEXT_OUTLINE_COLOR);
            fdps_message_window_close();
            return;
        }

        /* The trade.  The id is read out of the slot BEFORE the entry is
           removed, because removing one packs the entries below it up
           (unititem.h), and the cell record then keeps the item that was given
           away -- data_fdps_map_cell_event_triggered_flags is NOT set on this
           path and the map is NOT repainted, so the cell can be searched again
           and hands the traded item back. */
        offered_item_id = fdps_unit_get_item_id(unit_index, offered_slot);
        fdps_unit_remove_item(unit_index, offered_slot);
        fdps_unit_add_item(unit_index, cell_payload);
        *(short *) (data_fdps_tile_event_data_table_ptr
                    + SEARCH_CELL_PAYLOAD_AT
                    + cell_event_code * SEARCH_CELL_RECORD_BYTES) =
            (short) offered_item_id;
        fdps_message_window_open((int) searching_unit->portrait_id);
        data_fdps_dialog_subst_text_id_2 = offered_item_id
                                           + ITEM_NAME_TEXT_BASE;
        fdps_draw_text(data_fdps_all_game_text_ptr, TEXT_SEARCH_ITEM_SWAPPED,
                       (unsigned char *) MESSAGE_QUESTION_AT,
                       VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                       MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
        fdps_message_window_close();
        return;
    }

    if (cell_kind == SEARCH_CELL_KIND_MONEY) {
        fdps_message_window_close();
        fdps_message_window_open((int) searching_unit->portrait_id);
        data_fdps_dialog_last_action_value_param = cell_payload;
        if (cell_payload == 0) {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_SEARCH_MONEY_NONE,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR,
                           MESSAGE_TEXT_OUTLINE_COLOR);
        } else {
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           TEXT_SEARCH_MONEY_TAKEN,
                           (unsigned char *) MESSAGE_QUESTION_AT,
                           VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                           MESSAGE_TEXT_BG_COLOR,
                           MESSAGE_TEXT_OUTLINE_COLOR);
        }
        fdps_message_window_close();

        /* The amount is added back out of the global and not out of the local
           the global was set from, MOV EAX,[0x00064038] at 0001889c: a text
           substitution that had rewritten the slot would be what the party
           received. */
        data_fdps_shared_party_total_gold +=
            data_fdps_dialog_last_action_value_param;
        data_fdps_map_cell_event_triggered_flags[cell_event_code] = 1;
        fdps_map_apply_triggered_cell_changes();
        return;
    }

    /* The scripted class.  THE HANDLER TABLE IS INDEXED WITH THE PAYLOAD WORD
       AND NOT WITH THE KIND BYTE -- LEA EDX,[EDX*0x4] on [EBP-0x14] at
       000188cd -- so the kind only says that this cell is scripted and the
       payload says which of the fifty handlers runs.  Neither the searched
       flag nor the tile is touched here: whatever the handler does about them
       is the whole of what happens to this cell. */
    delay(SCRIPTED_CELL_HOLD_MS);
    fdps_message_window_close();
    data_fdps_chapter_event_handler_table[cell_payload](unit_index);
}

/* Which slot commands what, in the ring's own order -- 0 up, 1 left, 2 right,
   3 down (menu.h).  The dispatch is a chain of equality tests on the slot the
   cursor finished on: CMP dword ptr [EBP-0x10],0x0 at 00015ecb, against 0x1 at
   0001601d and against 0x2 at 0001605c.  There is no fourth test, so the last
   arm is everything else and not slot 3 alone. */
#define ACTION_SLOT_ATTACK 0
#define ACTION_SLOT_SPELL 1
#define ACTION_SLOT_ITEM 2

/* How many slots the availability scan walks, CMP dword ptr [EBP-0x64],0x4 at
   00015e40. */
#define ACTION_SLOTS 4

/* struct fdps_unit_record's status_timers[5] at record offset 0x27, the
   封魔咒術 timer, tested as a byte by CMP byte ptr [EAX+0x27],0x0 at 00015e1d.
   A unit under it may not cast (unitstat.h). */
#define SEAL_TIMER_SLOT 5

/* fdps_unit_find_equipped_slot's want_armor argument, the 0 pushed at
   00015d26: the weapon and not the armour (unititem.h). */
#define EQUIPPED_WEAPON 0

/* fdps_collect_targets_in_range's select_mode, the 0 pushed at 00015d8d and
   again at 00015ef2: keep side 0, the enemy side (aitarget.h). */
#define TARGET_SELECT_ENEMIES 0

/* fdps_map_cursor_select_loop's select_mode, PUSH 0x0 at 00015f49.  It is none
   of the modes that function names, so a confirm needs a marked tile with an
   accepted unit inside the cursor's overlay (mapcur.h). */
#define CURSOR_SELECT_ATTACK_TARGET 0

/* The scratch block the attack's target list is collected into, PUSH 0x64 at
   00015ee5: 100 unit indices of one byte each.  malloc's answer is not tested
   -- there is no CMP between the CALL at 00015ee7 and the store at 00015eef --
   and neither is the count that fills it tested against the capacity. */
#define TARGET_LIST_BYTES 100

/* The death scripts one attack can collect, LEA EAX,[EBP-0x5c] at 00015faf and
   00015fe7 against the -0x3c the next frame slot sits at: 0x20 bytes, ten
   three-byte records with two to spare.  Nothing communicates the size to
   fdps_collect_death_scripts and the original's frame is the only bound there
   is (death.h). */
#define DEATH_SCRIPT_BUFFER_BYTES 0x20

/* What the three commands that answer more than a slot number say.  The -1 is
   shared by the target cursor, the spell command and the item command and
   means the same thing in all three -- the player backed out; the item
   command's 2 is its hand-over arm and nothing else answers it (item.h,
   mapcur.h, spellmnu.h). */
#define COMMAND_CANCELLED (-1)
#define ITEM_MENU_HANDED_OVER 2

/* The experience an attack banked is scaled by 15/10 before it is paid, IMUL
   EDX,[0x00069cec],0xf with IDIV by 0xa at 00015fc3.  Both the multiply and
   the divide are signed and the divide truncates toward zero, which is what
   the int the global is declared as gives (gamedata.h). */
#define ATTACK_EXP_NUMERATOR 15
#define ATTACK_EXP_DENOMINATOR 10

/* The value the map cursor is put into for the attack's aim, MOV dword ptr
   [0x00069cd0],0x1 at 00015f37 (gamedata.h). */
#define MAP_CURSOR_VISIBLE 1

/* What this function answers with (btlact.h). */
#define ACTION_MENU_CANCELLED (-1)
#define ACTION_MENU_DONE 1

/* 00015d00.  See btlact.h for what each command does, what the two arrays
   mean and which probe greys which slot.

   THE FIRST-SELECTABLE SCAN IS WRITTEN OUT HERE RATHER THAN CALLED.  The
   assembly at 00015e2d-00015e70 is an inline expansion of what
   fdps_menu_find_first_enabled_entry (menu.h) does -- the same walk over the
   same four ints, reached through two stack copies of the array pointer -- and
   there is no CALL at that site.  Emitting the call instead would put one in
   the rebuild that the original does not have, so the loop stays written out.

   THE EXITS ARE WRITTEN AS RETURNS.  The original stores its answer into a
   frame slot and jumps to one epilogue at 000160d3; the arms below return,
   which is the same program with the jumps taken out (ADR-0001).

   THE PROBES RUN EVERY PASS AND THE PASS COSTS A GRID RESET.  Each pass calls
   fdps_collect_targets_in_range with a NULL buffer just to count, then
   fdps_map_grid_reset to put the markers back -- and that reset is INSIDE the
   equipped-weapon arm, so a unit with no weapon never runs it and leaves the
   grid as it found it.

   THE TWO ITEM BYTES CROSS OVER.  range_min at +0x0b is the collector's
   min_dist and range_max at +0x0c is its range_code, which carries the reach
   and the shape together (aitarget.h) -- the field names are the record's, not
   the collector's.  Both are read XOR EAX,EAX / MOV AL, so both widen
   unsigned.

   THE ATTACK ARM COLLECTS THE TARGETS A SECOND TIME rather than keeping the
   count the probe took: the probe counted with a NULL buffer and this one
   fills the block.  Both are taken from the cursor's own tile, so a cursor
   that has moved between them is a different list. */
int fdps_battle_action_menu(int unit_index, int *cmd_icons, int *cmd_disabled,
                            int unit_has_moved)
{
    /* The acting unit's record, re-resolved on every pass and read for one
       byte: the 封魔咒術 timer that greys the spell command. */
    struct fdps_unit_record *acting_unit;
    /* The equipped weapon's ITEM.DAT record, and the bag slot and item id it
       was reached through. */
    struct fdps_item_effect *weapon;
    int weapon_slot;
    int weapon_item_id;
    /* The weapon's reach, as fdps_collect_targets_in_range takes it: the code
       that carries reach and shape together, and the minimum distance a code
       below 0x10 clears out of the middle. */
    int weapon_range_code;
    int weapon_min_dist;
    /* The scan's answer -- the first slot that is not greyed out, or -1 when
       all four are -- and the slot it is walking. */
    int first_enabled_slot;
    int probe_slot;
    /* Where the ring's cursor is: it goes in holding the scan's answer and
       comes back holding the slot the player finished on, which is the slot
       the dispatch below acts on. */
    int chosen_slot;
    /* The answer of whichever modal step ran last -- the ring's cursor loop,
       the attack's target cursor, the spell command or the item command.  One
       slot in the original's frame and one variable here, because every reader
       of it is the test immediately after the call that set it. */
    int step_result;
    /* What a cancelled ring answers with.  It starts at -1 and the item
       command's hand-over is the one thing that lifts it to 1, which is how a
       turn spent on a hand-over survives the player then backing out. */
    int answer_when_cancelled;
    /* The attack's target list: the block, how many indices the collector put
       in it, and the unit the player's cursor settled on. */
    unsigned char *target_list;
    int target_count;
    int target_unit_index;
    /* Where the map cursor stood before the attack's aim, in world pixels, so
       that a cancelled aim can walk it back. */
    int saved_cursor_x;
    int saved_cursor_y;
    /* What the kills the attack made owe, collected before the death animation
       marks them dead and paid out after it (death.h). */
    int death_script_count;
    unsigned char death_scripts[DEATH_SCRIPT_BUFFER_BYTES];

    answer_when_cancelled = ACTION_MENU_CANCELLED;

    for (;;) {
        cmd_disabled[ACTION_SLOT_ATTACK] = 0;
        data_fdps_battle_pending_xp_credit = 0;

        weapon_slot = fdps_unit_find_equipped_slot(unit_index,
                                                   EQUIPPED_WEAPON);
        if (weapon_slot == -1) {
            cmd_disabled[ACTION_SLOT_ATTACK] = MENU_ENTRY_DISABLED;
        } else {
            weapon_item_id = fdps_unit_get_item_id(unit_index, weapon_slot);
            weapon = fdps_get_item_record(weapon_item_id);
            weapon_min_dist = (int) weapon->range_min;
            weapon_range_code = (int) weapon->range_max;

            if (fdps_collect_targets_in_range(
                    data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                    data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                    NULL, weapon_range_code, weapon_min_dist,
                    TARGET_SELECT_ENEMIES) == 0) {
                cmd_disabled[ACTION_SLOT_ATTACK] = MENU_ENTRY_DISABLED;
            }
            fdps_map_grid_reset();
        }

        if (fdps_unit_item_count(unit_index) == 0) {
            cmd_disabled[ACTION_SLOT_ITEM] = MENU_ENTRY_DISABLED;
        }

        if (fdps_unit_collect_known_spells(unit_index, NULL) == 0) {
            cmd_disabled[ACTION_SLOT_SPELL] = MENU_ENTRY_DISABLED;
        }

        acting_unit = fdps_get_unit_record(unit_index);
        if (acting_unit->status_timers[SEAL_TIMER_SLOT] != 0) {
            cmd_disabled[ACTION_SLOT_SPELL] = MENU_ENTRY_DISABLED;
        }

        first_enabled_slot = -1;
        for (probe_slot = 0; probe_slot < ACTION_SLOTS; probe_slot++) {
            if (cmd_disabled[probe_slot] == 0) {
                first_enabled_slot = probe_slot;
                break;
            }
        }

        chosen_slot = first_enabled_slot;
        fdps_menu_animate_open(cmd_icons, cmd_disabled, chosen_slot);
        step_result = fdps_menu_cursor_input_loop(cmd_icons, cmd_disabled,
                                                  &chosen_slot);
        fdps_menu_animate_close(cmd_icons, cmd_disabled, chosen_slot);
        fdps_render_view_frame();

        if (step_result == MENU_CURSOR_CANCELLED) {
            return answer_when_cancelled;
        }

        if (chosen_slot == ACTION_SLOT_ATTACK) {
            saved_cursor_x = data_fdps_map_cursor_world_x;
            saved_cursor_y = data_fdps_map_cursor_world_y;

            target_list = (unsigned char *) malloc((size_t) TARGET_LIST_BYTES);
            target_count = fdps_collect_targets_in_range(
                data_fdps_map_cursor_world_x / MAP_TILE_SIZE,
                data_fdps_map_cursor_world_y / MAP_TILE_SIZE,
                target_list, weapon_range_code, weapon_min_dist,
                TARGET_SELECT_ENEMIES);

            data_fdps_map_cursor_draw_mode = MAP_CURSOR_VISIBLE;
            step_result = fdps_map_cursor_select_loop(
                CURSOR_SELECT_ATTACK_TARGET, target_count, target_list);
            fdps_map_grid_reset();
            free(target_list);
            fdps_flush_keyboard_queue();

            if (step_result != COMMAND_CANCELLED) {
                target_unit_index = fdps_battle_find_unit_at_cursor();
                fdps_unit_face_target(unit_index, target_unit_index);
                fdps_combat_play_attack_exchange(unit_index,
                                                 target_unit_index);

                death_script_count = fdps_collect_death_scripts(death_scripts);
                fdps_play_death_animation_and_mark_dead();
                data_fdps_battle_pending_xp_credit =
                    data_fdps_battle_pending_xp_credit * ATTACK_EXP_NUMERATOR
                    / ATTACK_EXP_DENOMINATOR;
                fdps_unit_award_exp_and_level_up(unit_index);
                fdps_run_death_scripts(unit_index, death_script_count,
                                       death_scripts);

                fdps_battle_mark_unit_done(unit_index);
                fdps_flush_keyboard_queue();
                return ACTION_MENU_DONE;
            }

            fdps_map_cursor_move_to(saved_cursor_x, saved_cursor_y);
        } else if (chosen_slot == ACTION_SLOT_SPELL) {
            step_result = fdps_battle_spell_command(unit_index);
            if (step_result != COMMAND_CANCELLED) {
                fdps_battle_mark_unit_done(unit_index);
                data_fdps_ui_play_active_flag = 1;
                return ACTION_MENU_DONE;
            }
        } else if (chosen_slot == ACTION_SLOT_ITEM) {
            step_result = fdps_battle_item_menu(unit_index);
            if (step_result == ITEM_MENU_HANDED_OVER) {
                answer_when_cancelled = ACTION_MENU_DONE;
            } else if (step_result != COMMAND_CANCELLED) {
                data_fdps_battle_pending_xp_credit = 0;
                return ACTION_MENU_DONE;
            }
        } else {
            if (unit_has_moved == 0) {
                fdps_unit_rest(unit_index);
            }
            fdps_battle_search_cell_at_cursor(unit_index);
            fdps_battle_mark_unit_done(unit_index);
            return ACTION_MENU_DONE;
        }
    }
}
