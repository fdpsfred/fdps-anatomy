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

/* 000205b0.  Puts the message window up over whatever is on screen and LEAVES
   IT THERE.  It slides the Message.cel panel in from the bottom over six
   steps, ends with the panel standing at screen (9, 120) at full strength, and
   draws the speaker's portrait into it.  Nothing is returned and nothing is
   saved for a caller to restore.

   THE CALLER OWNS EVERYTHING THAT COMES NEXT.  The text is written afterwards
   with fdps_draw_text, the wait is fdps_message_window_wait_key, and the
   window is taken down again by fdps_message_window_close, which runs the same
   row table backwards.  A caller that opens and never closes leaves the panel
   on the visible page.

   THE WHOLE SCREEN IS SAVED AND PUT BACK, not just the window's rectangle.
   The visible page is copied out at entry and every one of the seven frames is
   composed from that copy, so anything that was on screen is still there
   underneath, and anything drawn on the visible page WHILE this runs is lost.

   face_index selects the speaker's picture in FACE.CEL and is handed to
   fdps_load_and_draw_portrait unchanged.  ANY NEGATIVE VALUE MEANS "NO
   SPEAKER", not just -1: this routine tests face_index < 0 and releases the
   portrait buffer itself rather than calling through, because
   fdps_load_and_draw_portrait's own no-portrait test is == -1 and would treat
   any other negative index as a directory offset in front of the table.  It
   matters because fdps_message_window_open_from_tile passes a signed character
   id that stays negative when that speaker is not on the map. */
extern void fdps_message_window_open(int face_index);
#pragma aux fdps_message_window_open "*" parm caller [];

/* 00020820.  Takes the message window down again: six frames of the bare
   Message.cel panel sliding off the bottom of the screen and fading as it
   goes, over a freshly rebuilt map scene, and that scene left standing on the
   visible page.  Takes nothing and returns nothing.

   IT DOES NOT RETRACT WHAT IS ON SCREEN, IT REPLACES IT.  The visible page is
   never read.  The backdrop is recomposed from the scene layers into a
   360 x 240 page whose 312 x 192 viewport is copied into an otherwise zeroed
   screen page, so the four-pixel border comes out black, and anything a caller
   had drawn on the visible page -- the message text, a menu, a status panel --
   is gone from the first animation frame.  It is not the reverse of
   fdps_message_window_open, which composes its slide from a copy of the screen
   it found.

   THE PANEL THAT SLIDES CARRIES NEITHER TEXT NOR PORTRAIT.  It is sprite 0 of
   the sheet in data_fdps_message_window_sheet_ptr (gamedata.h) decoded into a
   scratch buffer of its own, so what the player sees drop away is an empty
   window frame.

   IT USES THE SAME SIX ROWS AS THE OPEN, READ FROM THE FAR END: 120, 127, 135,
   150, 170, 190, with the blend weight running 15, 13, 11, 9, 7, 5 out of 16.
   The first frame therefore stands exactly where the open left the panel, and
   the last is nearly transparent and mostly off the bottom edge.

   THE PORTRAIT BUFFER IS NOT RELEASED.  data_fdps_portrait_sprite_buf_ptr is
   left holding whatever fdps_load_and_draw_portrait last loaded; only the four
   buffers this routine allocates are freed.  A caller that wants the portrait
   gone has to pass a negative index to fdps_message_window_open. */
extern void fdps_message_window_close(void);
#pragma aux fdps_message_window_close "*" parm caller [];

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

/* 00017990.  The modal two-option prompt.  It runs its own frame loop over the
   panel the caller has already drawn and does not return until the player
   commits: 0 for the left cell, 1 for the right, -1 for a cancel.

   IT TAKES NOTHING AND IT ANSWERS ONLY WITH THAT NUMBER.  The question, the
   panel it is written in and the two option pictures all come from elsewhere
   -- the caller draws the panel on the visible screen first, and the pictures
   are sprites 4 to 13 of Shadow.cel (gamedata.h) -- so nothing here says what
   is being asked.  All twenty-two call sites test the result against 0, and
   the left cell is the one selected on entry, so 0 is the affirmative answer
   and both other values decline.

   A CANCEL IS NOT THE RIGHT OPTION.  Esc and keypad Del answer -1 whichever
   cell was highlighted, so -1 and 1 are different inputs with the same
   outcome at every call site that only asks whether the result is 0.

   IT CONSUMES THE KEY QUEUE ON ENTRY and reads the next key only at the
   bottom of each pass, so at least one whole frame is always drawn and shown
   before any answer can be given.  What it draws over depends on
   data_fdps_village_mode_flag (gamedata.h) exactly as the wait above does,
   and the portrait in data_fdps_portrait_sprite_buf_ptr is repainted whenever
   it is not null. */
extern int fdps_prompt_two_choice(void);
#pragma aux fdps_prompt_two_choice "*" parm caller [];

/* 00020a70.  Puts the message window up for a speaker who is standing on the
   battle map: it walks the view onto him, grows the Message.cel panel out of
   his tile over seven frames until it stands exactly where
   fdps_message_window_open would have left it, and draws his portrait.
   Returns nothing.

   IT IS THE MAP-SIDE ALTERNATIVE TO fdps_message_window_open, NOT A WRAPPER
   AROUND IT.  The two leave the same panel in the same place, so the same
   fdps_draw_text, fdps_message_window_wait_key and fdps_message_window_close
   follow either; what differs is the animation and what the screen looks like
   underneath.  tile_x == -1 means "not on the map" and hands the whole opening
   over to fdps_message_window_open instead -- an equality test, so -2 is a
   tile column and not a second way of saying no.

   THE ZOOM REPLACES THE VIEWPORT AND THE SLIDE PRESERVES THE SCREEN.  Each of
   the seven frames is composed by fdps_draw_scene_layers -- the live scrolling
   map, the units and the cursor -- and only the 312 x 192 viewport at screen
   (4, 4) is presented, so anything a caller had drawn inside it is gone and
   the four-pixel border is untouched.  fdps_message_window_open, on the -1
   arm, composes from a copy of the screen it found and moves the whole page.

   tile_x and tile_y are the speaker's tile on the battle map, 0-based; they
   are scaled by the 24-pixel tile and handed to fdps_map_cursor_move_to
   (mapcur.h), so the call also MOVES THE MAP CURSOR AND MAY SCROLL THE VIEW.
   tile_y is not read at all on the -1 arm.

   IT LEAVES data_fdps_map_cursor_draw_mode HOLDING 1 (gamedata.h) whenever the
   speaker is on the map.  The mode is cleared to 0 so that the scroll draws no
   cursor and then set to 1 for the zoom; the caller's own mode is not saved
   and not put back, so a movement range that was being outlined is showing a
   plain box afterwards.  The -1 arm does not touch it.

   face_index selects the speaker's portrait in FACE.CEL.  On the map arm it
   goes straight to fdps_load_and_draw_portrait, whose no-portrait test is
   == -1, so any OTHER negative index is scaled into a directory offset in
   front of the table; on the -1 arm fdps_message_window_open sees it first and
   releases the buffer for any negative value. */
extern void fdps_message_window_open_from_tile(int tile_x, int tile_y,
                                               int face_index);
#pragma aux fdps_message_window_open_from_tile "*" parm caller [];

#endif
