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

#endif
