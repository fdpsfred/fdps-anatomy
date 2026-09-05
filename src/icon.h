/* icon.h -- the IconAni cut-scene script interpreter and the opcode handlers
 * it dispatches to.
 *
 * A cut-scene in this game is a byte-coded script read out of the ICON
 * container: fdps_icon_script_run walks it one instruction at a time, an
 * opcode byte followed by that opcode's operand bytes, and hands each one to
 * a handler in this file.  The handlers do the visible work -- showing a
 * picture, playing a sample, moving a unit, fading the screen -- and the
 * interpreter only sequences them.
 *
 * The fade handlers here own no state.  They read the game's master palette
 * through data_fdps_vga_main_palette_ptr (gamedata.h) and write the VGA DAC
 * through fdps_set_palette_range (palette.h); the screen itself is left
 * alone, so whatever picture is on it stays there and only its colours move.
 */
#ifndef ICON_H
#define ICON_H

/* Script opcode 0x0e.  Fades the screen to black over sixteen steps, each one
   paced by a wait for the VGA vertical retrace and then held for
   step_delay_ms milliseconds through the CRT's delay().

   Every step re-uploads all 256 DAC entries from the master palette with the
   step's darkening bias applied, so the ramp always runs from the master
   palette and never from whatever happens to be on the DAC when the opcode is
   reached -- a picture shown under some other palette blacks out along the
   master palette's ramp, not its own.

   The last step's bias is -60, not -64, so an entry brighter than 60 finishes
   at 1..3 rather than at 0: the screen ends very dark rather than completely
   black.  The counterpart handler, opcode 0x0f, brings it back by running the
   same ramp from -64 up to 0.

   step_delay_ms is the operand byte the interpreter read out of the script, so
   0..255, and it is the only thing that sets the fade's speed beyond the one
   retrace each step already waits for. */
extern void fdps_icon_script_fade_to_black(int step_delay_ms);
#pragma aux fdps_icon_script_fade_to_black "*" parm caller [];

/* Script opcode 0x0f.  Brings the screen back out of black over seventeen
   steps, each one paced by a wait for the VGA vertical retrace and then held
   for step_delay_ms milliseconds through the CRT's delay().

   Like the fade out, every step re-uploads all 256 DAC entries from the master
   palette with that step's darkening bias applied, so the ramp is derived from
   the master palette and never from what is on the DAC.

   The biases are -64, -60, ... -4, 0.  Both ends are load-bearing: -64 clamps
   every component to 0, so the picture always comes up from true black no
   matter what the DAC held on entry, and the final 0 uploads the master
   palette untouched, so the handler ends with the screen at full brightness.
   That is one step more than the fade out's sixteen, and the pair is
   deliberately asymmetric -- the fade out stops at -60, this one runs all the
   way home.

   step_delay_ms is the operand byte the interpreter read out of the script, so
   0..255. */
extern void fdps_icon_script_fade_in(int step_delay_ms);
#pragma aux fdps_icon_script_fade_in "*" parm caller [];

/* Script opcode 5.  Scrolls the map view from wherever it is to the tile the
   script names, one tile-step per rendered frame, and parks it there.

   script is the base of the loaded IconAni .DAT image and offset the byte
   offset of this opcode inside it, so script[offset + 1] is the target tile x
   and script[offset + 2] the target tile y.  Both operands are unsigned bytes:
   the original masks the loaded byte with 0xff before it multiplies, so a tile
   number of 200 is 200 and not -56.

   Returns offset + 3, the position of the next opcode, which the interpreter
   stores as its new script position.

   The handler leaves four globals set (all gamedata.h): the view origin
   data_fdps_battle_view_window_origin_x / _y hold the target tile in map
   pixels exactly, and the map cursor data_fdps_map_cursor_world_x / _y hold
   that same point one tile further in, so the cursor sits just inside the new
   view's top-left corner.  Nothing is clamped against the map extents -- the
   tile the script names becomes the view origin as written, which is the
   difference between this and fdps_map_cursor_move_to. */
extern int fdps_icon_script_scroll_view_to_tile(unsigned char *script,
                                                int offset);
#pragma aux fdps_icon_script_scroll_view_to_tile "*" parm caller [];

/* Script opcode 1.  Walks a list of battle units a scripted number of tiles,
   each unit in its own scripted direction, playing the six sub-step walk
   animation as they go.

   script is the base of the loaded IconAni .DAT image and offset the byte
   offset of this opcode inside it.  The three operand bytes are all unsigned:
   script[offset + 1] is how many rendered frames each of the six walk
   sub-steps is held for, script[offset + 2] is how many tiles the group
   walks, and script[offset + 3] is how many units are listed.  The list
   follows at script[offset + 4] as that many two-byte pairs, a unit index
   then a facing code (0 down, 1 left, 2 up, anything else right).

   Returns offset + 4 + 2 * unit_count, the position of the next opcode, which
   the interpreter stores as its new script position.

   Every listed unit ends up on the tile its facing code names, one tile
   further along per scripted tile, with its sub-tile step counter back at 0.
   Nothing is clamped, no terrain is consulted and no arrival event is raised:
   the listed units walk straight through whatever is on the map, which is
   what separates a scripted walk from the interactive movement playback.

   A frames-per-sub-step operand of 0 moves nothing at all -- the units are
   updated once per rendered frame, so with no frames there is no update.

   The handler leaves two globals set (both gamedata.h):
   data_fdps_map_cursor_draw_mode and data_fdps_ui_play_active_flag are both 0
   for the duration of the walk, so no cursor and no info panel are drawn over
   it, and both are 1 on return.  They are set to those constants rather than
   put back to what they held on entry. */
extern int fdps_icon_script_walk_units(unsigned char *script, int offset);
#pragma aux fdps_icon_script_walk_units "*" parm caller [];

#endif
