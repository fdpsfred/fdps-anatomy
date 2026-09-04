/* msgwin.h -- the dialogue message window, the portrait it shows and the
 * two-choice prompt.
 *
 * See msgwin.c for the file's own note.  Everything here draws into an 8bpp
 * surface addressed by a byte pointer and a pitch, the same convention blit.h
 * and text.c use: 0x140 for the visible mode 13h screen.
 */
#ifndef MSGWIN_H
#define MSGWIN_H

/* 000177d0.  Loads one 125x100 portrait out of FACE.CEL into the portrait
   buffer data_fdps_portrait_sprite_buf_ptr (gamedata.h) and blits it once into
   the caller's surface.

   dest is the destination 8bpp surface ALREADY OFFSET to the portrait's
   top-left pixel -- this routine adds no origin of its own -- and dest_pitch
   is that surface's pitch in bytes.  Both go straight to fdps_blit_dispatch,
   with width 0x7d, height 0x64 and mode 0, the plain RLE decode.  Every caller
   in the game passes pitch 0x140.

   portrait_index selects the record of FACE.CEL, 0..159, through the sheet's
   u32 offset directory at file offset 0x0f.  It is a portrait id and not a
   character id: fdps_draw_unit_status_panel takes it from unit record + 0x07,
   not from the char_id at + 0x08.

   THE BUFFER IS FREED ON EVERY CALL AND THE RECORD DELIBERATELY OUTLIVES THE
   ONE BLIT.  The entry sequence frees whatever the global still points at and
   nulls it, so at most one portrait is ever resident; the record loaded here
   is then left allocated when the routine returns, because
   fdps_prompt_two_choice, fdps_message_window_wait_key and the message window
   each repaint the same 125x100 image straight out of the global whenever it
   is non-NULL.  Reading the record into a local, or freeing it after the blit,
   draws correctly once and loses the portrait on the next redraw.

   portrait_index == -1 IS "NO PORTRAIT", NOT AN ERROR AND NOT AN INDEX.  The
   free and the null store still happen and the routine then returns, so it is
   the way a caller releases the buffer and draws nothing.  Any other index is
   used unchecked: nothing tests it against the sheet's 160 records.

   A FACE.CEL that will not open ends the process.  The failure path is
   printf("File not found: 'FACE.CEL'\n") followed by exit(1), so there is no
   return value to test and no way for a caller to recover.  Nothing else is
   checked either -- neither malloc, nor either fread's count. */
extern void fdps_load_and_draw_portrait(unsigned char *dest, int dest_pitch,
                                        int portrait_index);
#pragma aux fdps_load_and_draw_portrait "*" parm caller [];

/* 000203d0.  Holds the message window that fdps_message_window_open left
   standing on the visible screen, repainting it over a live background once
   per game tick, until the player presses a key or the tick budget runs out.
   Returns nothing and writes nothing back.

   IT NEITHER OPENS NOR CLOSES THE WINDOW.  What it draws is a copy of the
   screen it lifts on entry from 0xa9609 -- 302 x 73 at screen (9, 120), the
   exact rectangle fdps_message_window_open puts the Message.cel panel in --
   so the window and whatever text has already been written into it have to be
   on screen before the call, and they are still on screen after it.

   show_wait_indicator nonzero draws the blinking four-phase prompt indicator,
   Command.cel sprites 0x48 to 0x4b, in the window's bottom right corner at
   screen (280, 166), one phase every three ticks; zero leaves it out.  All
   nine call sites in the game pass 1.

   timeout_ticks is the most passes to make, one per game tick, decremented at
   the bottom of the loop and tested against 0 there.  Callers pass 0x1e, 0x32
   or 0x64.  ZERO IS NOT "DO NOT WAIT": the decrement happens before the test,
   so 0 wraps and spins for 2^32 ticks.  Nothing in the game passes it.

   THE BACKGROUND IT REPAINTS OVER DEPENDS ON data_fdps_village_mode_flag
   (gamedata.h).  Clear, on the battle map, fdps_draw_scene_layers recomposes
   the whole scrolling scene every pass so the map keeps animating behind the
   window; set, in the village, the visible page's 312 x 192 viewport is copied
   into the composition page instead and the backdrop stays still.

   The portrait is drawn from data_fdps_portrait_sprite_buf_ptr whenever that
   is not null, at screen (12, 90) and over the window rather than under it,
   which is what makes fdps_load_and_draw_portrait's deliberately outliving
   buffer worth keeping.

   A KEY ALREADY IN THE RING ENDS IT BEFORE ANYTHING IS DRAWN.  The scancode is
   read at the top of each pass, so a make code found on the first read returns
   with the screen untouched.  The code itself is discarded either way: a
   caller that needs to know which key was pressed has to read the ring
   itself. */
extern void fdps_message_window_wait_key(int show_wait_indicator,
                                         int timeout_ticks);
#pragma aux fdps_message_window_wait_key "*" parm caller [];

#endif
