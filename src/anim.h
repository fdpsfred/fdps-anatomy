/* anim.h -- VFS/SAF animation playback and the turn banner.
 *
 * The animations this file plays are .SAF images that live inside BaseAni.vfs,
 * the container fdps_load_global_resources reads whole into memory at startup
 * and keeps resident for the run.  Nothing here loads a file: a member is found
 * where it already lies inside that resident image (vfs.h,
 * resource_info/vfs.md), so an animation costs no allocation and no copy, and
 * every pointer this file hands out stops being valid the moment
 * fdps_shutdown_free_resources releases the image.
 */
#ifndef ANIM_H
#define ANIM_H

/* 000643ec.  The member BaseAni.vfs lookup found last, kept as a global
   although the one caller also takes the answer as a return value.  Written on
   every lookup, before the answer is tested, so it holds NULL for as long as a
   failed lookup takes to print its message and end the process; nothing ever
   clears it and nothing outside fdps_baseani_get_entry_or_exit reads it -- all
   three references in the image are that function's own store, test and load.
   It points INTO the resident archive image, so it is not a block anything may
   free. */
extern unsigned char *data_fdps_animation_baseani_entry_ptr;

/* Finds the member called name inside the resident BaseAni.vfs image and hands
   back a pointer to it, or ends the process when the archive holds no such
   member.

   The archive base is taken from data_fdps_animation_baseani_archive_ptr on
   every call -- PUSH dword ptr [0x000643a8] at 0002a254 -- and handed to
   fdps_vfs_image_get_entry as a container image, so a lookup made before
   fdps_load_global_resources has run searches through a null pointer.  Nothing
   here guards that and nothing needs to: the loader runs before the first
   animation.  The member's size is asked for because the lookup insists on
   somewhere to put it and is then discarded; no instruction in the body reads
   the slot back.

   The answer is stored in data_fdps_animation_baseani_entry_ptr before it is
   tested, and the pointer returned is that global re-read rather than the
   value the lookup gave back.  Nothing is allocated and nothing is copied: the
   pointer aims into the archive image itself and must not be freed.

   A miss does not come back.  printf("File not found: %s\n", name) goes to
   stdout and exit(1) ends the process, so the returned pointer is never NULL
   from a caller's point of view, and a caller that tests it is testing
   something that cannot happen.

   name reaches strupr inside the lookup and is upper-cased IN PLACE in the
   caller's own storage (vfs.h), which is why it cannot be const and why the
   name in the miss message is the folded spelling rather than the one the
   caller wrote.  The one call site, fdps_animate_turn_banner at 0001e880,
   pushes the literal "Turn.saf" at 0x617b0, so that literal is permanently
   rewritten to "TURN.SAF" by the first banner of the run and has to live in
   writable storage (rebuild_info/pitfalls.md). */
extern void *fdps_baseani_get_entry_or_exit(char *name);
#pragma aux fdps_baseani_get_entry_or_exit "*" parm caller [];

/* Paints the current battle turn number over the player-phase turn banner, one
   composite sprite per decimal place, through the draw request the caller has
   already built (sprite.h owns that block and what each of its nine slots
   means).

   The number is data_fdps_battle_turn_counter formatted with "%d"; nothing is
   passed in, so this always draws whatever the counter says at the moment of
   the call.  Each character becomes the request's entry index and is painted
   by fdps_draw_composite_sprite with its sound argument zero, and the
   request's x then moves on 28 pixels.

   THE ENTRY INDEX IS THE DIGIT PLUS ONE.  The character is biased by 0x2f, not
   by '0', because entry 0 of the banner's sprite bank is the word graphic the
   caller paints itself and the numerals begin at entry 1.  Writing the obvious
   digit - '0' shifts every glyph one down the bank and makes a '0' draw the
   word (rebuild_info/pitfalls.md).

   `request` IS WRITTEN IN PLACE AND IS NOT RESTORED.  Two of its nine slots
   move: the entry index, which comes back holding the last digit's, and x,
   which comes back one pitch past the last digit drawn.  The other seven --
   the destination surface, its pitch and rows, y, the sprite bank and the two
   blit slots -- are neither read nor written here; the bank in particular is
   whatever the caller loaded, and this routine never checks that it is the
   banner's.  The one caller, fdps_animate_turn_banner, rewrites both moved
   slots before it uses the block again, so nothing depends on where they are
   left.

   There is no bound on the formatted number.  It goes into an eight-byte
   buffer, which holds seven digits and a terminator, and a counter wider than
   that would run off the frame.  Nothing in the game gets near it: the counter
   is a turn number. */
extern void fdps_draw_turn_number(int *request);
#pragma aux fdps_draw_turn_number "*" parm caller [];

#endif
