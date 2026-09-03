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

#endif
