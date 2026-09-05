/* save.h -- FDE.SAV: the save and load screens, the slot panel they draw, and
 * the two primitives that guard the file itself -- the integrity checksum and
 * the XOR stream cipher the image is stored under
 * (rebuild_info/code_layout.md).
 *
 * The file on disc is one 0x59cb-byte image holding a fixed header followed by
 * four 0xa28-byte chapter slots (struct fdps_save_slot in fdpstype.h), with a
 * 32-bit checksum dword at +0x59c7 that overlaps the tail of the unreachable
 * fourth slot.  Everything on disc is ciphertext; the read path decrypts the
 * whole image in place and then verifies the checksum over the plaintext, and
 * the write path computes the checksum first and encrypts afterwards.
 */
#ifndef SAVE_H
#define SAVE_H

#include "fdpstype.h"

/* How many slots the save and load panel offers, and the modulus the slot
   cursor wraps on.  MOV EBX,0x3 before each of the two IDIVs at 000246b9 and
   000246ef, and CMP dword ptr [EBP-0x8],0x3 / JGE at 0002490c bounds the loop
   in fdps_saveload_screen_build that fills the flags below.  It is three and
   not the four chapter slots the file on disc carries: the fourth slot of the
   image is not reachable from this screen. */
#define SAVE_SLOT_COUNT 3

/* 000640f8.  Whether each of the three panel slots holds a game that can be
   loaded, one int per slot, written by fdps_saveload_screen_build as it draws
   the panel -- MOV dword ptr [EAX + 0x640f8],0x0 at 00024953 for an empty slot
   and 0x1 at 00024969 for an occupied one -- and read by
   fdps_save_slot_select_loop, which is the only reader in the image.

   The flags gate confirmation on the load screen alone.  On the save screen
   the mode flag below is zero and the whole test is short-circuited, so an
   empty slot confirms exactly as an occupied one does.

   Indexed with LEA EAX,[EAX*0x4 + 0x0] / [EAX + 0x640f8] from the slot cursor,
   whose value the modulus above keeps in 0..2, so the array is exactly three
   entries and the mode flag that follows it in memory is never reached through
   it. */
extern int data_fdps_ui_save_slot_occupied_flags[SAVE_SLOT_COUNT];

/* 00064104.  Which of the two screens is running: zero for the save screen,
   non-zero for the load screen.  Its two writers are the screens themselves --
   MOV dword ptr [0x00064104],0x0 at 000241fa in fdps_save_game_screen and 0x1
   at 000244a3 in fdps_load_game_screen -- and its one reader is
   fdps_save_slot_select_loop, where it decides whether an empty slot may be
   confirmed.

   It is a mode, not a count, and it is never cleared on the way out: whichever
   screen ran last leaves its value behind.  Nothing else in the image looks at
   it, so that costs nothing. */
extern int data_fdps_ui_saveload_is_load_mode;

/* 00024650.  The modal loop both save/load screens run once the slot panel is
   on the page: it drives a three-entry slot cursor from the keyboard, animates
   the cursor highlight over a background the caller composed, and answers
   whether a slot was picked or the screen was backed out of.

   `background` is a whole 320x200 page the caller owns -- the panel with the
   three slots already drawn on it.  It is never written to: each frame copies
   it into a page of its own and draws over the copy.  `slot` points at the
   caller's cursor, which is read AND written: the arrow keys move it and the
   caller reads it back to find out which slot was picked.  Nothing validates it
   on entry, and a starting value outside 0..2 stays outside it -- the modulus
   only closes over the range once a key has moved the cursor at least once, and
   the highlight is drawn at 25 + 52 * whatever it holds.  It never happens:
   both call sites clear their cursor immediately before the call, MOV dword ptr
   [EBP-0xc],0x0 at 0002449c in fdps_load_game_screen and [EBP-0x14],0x0 at
   000241ec in fdps_save_game_screen, and both then index the save image with
   that same value times 0xa28.

   The answer is 1 for a confirmed slot and -1 for a cancelled screen; 0 is the
   value the loop tests to decide whether to run another pass, so it is never
   returned.

   ESCAPE IS THE ONLY CANCEL KEY.  The ring menu's second cancel key, keypad Del
   at 0x53, is not one of the six codes this loop knows (menu.c), so a player
   who backs out of every other modal screen with it finds it does nothing here.
   Space and Enter both confirm; up and left both step back; right and down both
   step forward.

   A CONFIRM ON AN EMPTY SLOT IS SILENTLY IGNORED ON THE LOAD SCREEN and
   accepted on the save screen: the test is
   data_fdps_ui_saveload_is_load_mode == 0 first, and only then the occupied
   flag.  A rejected confirm plays no sound and leaves no trace -- the pass
   costs a frame and the loop goes round again.

   ONE MORE FRAME IS ALWAYS DRAWN AFTER THE DECISION.  The arms that set the
   answer only store it; the frame below them runs regardless and the loop test
   is at the top, so the screen the caller inherits is a full frame drawn with
   the cursor on the slot that was picked.  Returning out of the branch instead
   would leave the previous frame's highlight on the adapter.

   The cursor animation is four ticks per frame, folded: ((tick >> 2) & 3) with
   3 mapped to 1, so the highlight cycles 0, 1, 2, 1 through a sheet that holds
   exactly three sprites.  Dropping the fold asks LoadKon.cel for a sprite it
   does not have and the drawer, which range checks nothing, reads a stream
   address from past the offset table (sprite.h).

   The frames are paced by the vertical retrace and by
   data_fdps_timer_tick_counter, so the loop does not return until the timer
   interrupt is running.  A container or a member that cannot be found ends the
   process inside fdps_vfs_load_entry rather than coming back (vfs.h). */
extern int fdps_save_slot_select_loop(void *background, int *slot);
#pragma aux fdps_save_slot_select_loop "*" parm caller [];

/* 00024a40.  Draws one slot's summary panel: the leader's face and level, the
   chapter the slot holds and that chapter's title, and the date and time the
   slot was written.  fdps_saveload_screen_build calls it once per slot and it
   is the only caller.

   `dest` is the top-left byte of this slot's panel inside the page being
   composed -- the caller forms it as page + 0xd + (slot * 0x34 + 0x1a) * 0x140,
   so the three panels sit 52 rows apart -- and `pitch` is that page's row
   stride, 0x140 at the one call site.  `slot_record` is one 0xa28-byte slot of
   the decrypted FDE.SAV image, save_buffer + 0x312b + slot * 0xa28 at the call
   site; its first 0x9b0 bytes are the roster copy, so roster[0] is the party
   leader.

   A SLOT WHOSE chapter_index IS 0xff HAS NEVER BEEN WRITTEN.  It prints entry
   0x209 of data_fdps_all_game_text_ptr across the panel and draws nothing else
   -- no captions, no face, no figures.

   IT EMPTIES THE GLOBAL SPRITE CACHE AND LEAVES IT HOLDING ONE GROUP.  Drawing
   a written slot frees the cache buffer, zeroes the count and loads the
   leader's icon group into the slot that frees up, so on return the cache holds
   that one group and nothing else.  Every panel does it again, and the caller
   has to reload the party's groups after its slot loop or the rest of the game
   paints out of a one-entry cache (rebuild_info/pitfalls.md).

   IT MOVES data_fdps_number_glyph_color_row AND LEAVES IT AT 0.  The level is
   drawn in whatever row the global already held on entry; the chapter, the date
   and the time each set their own row, and 0 is written on the way out.  An
   unwritten slot does not touch it at all.

   The two messages are placed at raw byte offsets into the page rather than at
   a row times `pitch`, so they only land where they are meant to at a pitch of
   0x140.  Nothing is clipped and no pointer is checked: a missing ICON.CEL
   faults inside the cache loader and a missing chapter member ends the process
   inside fdps_vfs_load_entry (vfs.h). */
extern void fdps_draw_save_slot_panel(unsigned char *dest, int pitch,
                                      struct fdps_save_slot *slot_record);
#pragma aux fdps_draw_save_slot_panel "*" parm caller [];

/* 00056898.  The FDE.SAV integrity checksum: the sum of every byte of the
   save image except the trailing four, which are the stored checksum field
   itself and must not take part in the sum that is compared against them.

   `save_image` points at the plaintext image and `size` is its whole length
   counting that field; all four call sites pass the buffer and the literal
   0x59cb.  Bytes are summed zero-extended, and the sum wraps at 32 bits.

   `size` is a raw loop count, not a checked length: a size below 4 wraps the
   count and walks the heap, and a size of exactly 4 walks 2^32 bytes.  Neither
   is reachable from the four call sites and neither is guarded against. */
extern unsigned int fdps_compute_save_checksum(unsigned char *save_image,
                                               unsigned int size);
#pragma aux fdps_compute_save_checksum "*" parm caller [];

/* 000568b7.  The FDE.SAV stream cipher, applied in place over `length` bytes
   starting at `buffer`.  A sixteen-bit key seeded with 0xa5 is advanced once
   per byte -- add 0x9014, rotate left by three, both modulo 0x10000 -- and its
   low half is XORed into the byte.

   The keystream is a function of the byte's index alone, so the routine is its
   own inverse and there is no separate decryptor: the write path calls it
   after computing the checksum, the read path calls it before verifying one.

   `length` is a raw loop count and is not checked: a length of 0 wraps it and
   walks 2^32 bytes.  All eight call sites pass the whole 0x59cb-byte image. */
extern void fdps_xor_crypt_buffer(unsigned char *buffer, unsigned int length);
#pragma aux fdps_xor_crypt_buffer "*" parm caller [];

#endif
