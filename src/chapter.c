/* chapter.c -- the chapter frame.
 *
 * See chapter.h for what the frame is.  Nothing here holds state: every value
 * this file writes is a game-state global gamedata.c owns, and the chapter it
 * acts on is data_fdps_chapter_current_chapter_id.
 */
#include <string.h>
#include "gamedata.h"
#include "deploy.h"
#include "keybd.h"
#include "chapter.h"

/* How many bytes of the per-cell event flag table are cleared: PUSH 0x20 at
   0002277e, which is the whole of data_fdps_map_cell_event_triggered_flags's
   32 entries (gamedata.h).  Written as the literal length the original pushes
   rather than as sizeof, so a later change to the declared size cannot quietly
   change how much of it a chapter start clears. */
#define MAP_CELL_EVENT_FLAG_COUNT 0x20

/* 00022750.  Zero-sized frame and no locals at all: PUSH EBX / PUSH ESI /
   PUSH EDI / PUSH EBP / MOV EBP,ESP / SUB ESP,0x0 and a matching POP sequence
   at 000227d1, with nothing addressed off EBP anywhere in between.  Every
   value the body touches is a global at an absolute address, which is why
   there is no name in the C below that is not one of them.

   Stack calling convention, and this body shows it from both ends: it takes no
   argument and reads no register or stack slot before writing one, and the one
   call it passes something to pushes the value and cleans up itself -- PUSH
   dword ptr [0x00069cf4] / CALL 0x00022be0 / ADD ESP,0x4 at
   00022770..0002277b, with fdps_build_map_unit_array reading it at [EBP+0x14].

   No value is used after any CALL.  memset's return is discarded (ADD ESP,0xc
   and then a store of a literal), and both game callees return void: the
   32 call sites of fdps_flush_keyboard_queue push nothing and read nothing
   back (src/keybd.c), and fdps_build_map_unit_array's result is not read here
   either.

   Straight line, no branches: the disassembly has no conditional jump and no
   loop, so the C is the ten statements in the order the stores appear.

   THE TWO STORES TO data_fdps_map_cursor_draw_mode ARE BOTH REAL.  0 goes in
   at 0002275c, before the unit array is rebuilt, and 1 at 000227b8 after it;
   collapsing them into the single store to 1 that the final state would
   suggest is the obvious tidy-up.  Nothing in the call tree under
   fdps_build_map_unit_array reads that global -- its callees are the resource
   loader, the wave deployer, the record accessors, the sprite cache and the
   CRT, and the four functions in the image that read it
   (fdps_draw_map_cursor, fdps_map_cursor_move_to, fdps_map_cursor_select_loop
   and fdps_battle_spell_command) are in none of their callee lists -- so the
   intermediate 0 is not observable inside one call.  It is emitted because the
   original stores it, not because a reader of it is known.

   The chapter id is read from the global and NOT from anything this function
   was handed, because it was handed nothing.  fdps_build_map_unit_array itself
   ignores the argument when it loads the chapter's resources and uses the same
   global (src/deploy.c), so the two agree only because this call site passes
   that global in; the argument is what names the map%02d.cod placement file.

   The order matters at one point and only one: the array rebuild runs BEFORE
   the counters below it are cleared.  fdps_build_map_unit_array reaches
   fdps_deploy_wave, which deploys the map's wave-0 units, and those go down on
   the tiles the placement table names -- so a reset that cleared the view
   window and the cursor first would still end with the same numbers in them.
   The per-cell event memset is the one that is not interchangeable: it has to
   follow the load, because the chapter whose flags are being cleared is the
   one the load just brought in.

   Contract E: the memset destination is MOV EAX,0x640d8 / PUSH EAX, an
   absolute address the rebuild will not reproduce, so it is written as the
   symbol.  Every other operand here is already a dword reference to a named
   global. */
void fdps_chapter_state_reset(void)
{
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_chapter_event_or_battle_end_code = 0;

    fdps_build_map_unit_array(data_fdps_chapter_current_chapter_id);

    memset(data_fdps_map_cell_event_triggered_flags, 0,
           MAP_CELL_EVENT_FLAG_COUNT);

    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = 1;
    data_fdps_battle_turn_counter = 1;

    fdps_flush_keyboard_queue();
}
