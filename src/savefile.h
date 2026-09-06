/* savefile.h -- FDE.SAV itself: reading a saved game back into the live game
 * state, and the two primitives that guard the image on disc -- the integrity
 * checksum and the XOR stream cipher.
 *
 * The file is one 0x59cb-byte image in two regions, with a 32-bit checksum
 * dword at +0x59c7 that overlaps the tail of the unreachable fourth slot.
 *
 * The first 0x312b bytes are the BATTLE RESUME IMAGE, one battle in progress
 * written out whole and read back whole, and they are a straight concatenation
 * with no padding between the pieces:
 *
 *   +0x0000  0x08a3  the chapter's resident MAP%02d.DAT block
 *   +0x08a3  0x0a00  the party roster, 32 struct fdps_unit_record
 *   +0x12a3  0x1e00  the map unit array, 96 struct fdps_unit_record
 *   +0x30a3  0x0020  the per-cell event triggered flags
 *   +0x30c3  0x0012  the scalar header: turn, unit count, chapter, the view
 *                    origin and cursor in TILES, roster size, gold and the
 *                    four option toggles
 *
 * From +0x312b on are the four 0xa28-byte chapter slots the save and load
 * screens read and write (struct fdps_save_slot in fdpstype.h).  The two
 * regions are written by different code and neither reads the other: the
 * resume image is fdps_battle_system_submenu's and this file's, the slots are
 * save.c's.
 *
 * Everything on disc is ciphertext: the read path decrypts the whole image in
 * place and then verifies the checksum over the plaintext, and the write path
 * computes the checksum first and encrypts afterwards.
 *
 * The screens that put slots in front of the player are save.h; the page they
 * draw the slots on is savepnl.h.
 */
#ifndef SAVEFILE_H
#define SAVEFILE_H

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

/* 00023e20.  Reads FDE.SAV back and installs the battle it holds as the live
   game state, then puts the player back in front of it.  Takes nothing and
   returns nothing: the file name is a literal and everything it produces is a
   global.  Both call sites are the two places a battle can be resumed from --
   fdps_battle_system_submenu's load entry and fdps_title_screen's continue.

   IT INSTALLS ONLY THE RESUME IMAGE, NOT A SLOT.  The four chapter slots from
   +0x312b on are never looked at; what this reads is the region above, and the
   screens in save.c are what move a slot into it.  The two are separate saves
   with separate lifetimes.

   A CORRUPT IMAGE IS REPORTED AND THEN LOADED ANYWAY.  When the recomputed
   checksum disagrees with the stored one the routine opens the message panel,
   draws entry 0x208 and closes the panel again -- with no wait for a key in
   between, so the warning is on screen for the length of the close animation
   -- and then falls straight into the same load it would have done.  There is
   no return, no retry and no second file.

   THE WARNING PATH NEEDS A CHAPTER ALREADY LOADED.  fdps_message_window_close
   recomposes the map scene from the scene-layer globals (msgwin.h), and those
   still hold the PREVIOUS chapter's layers at that point -- the load of this
   save's chapter happens further down.  Reached from
   fdps_battle_system_submenu, where a battle is already on screen, that is
   simply the old scene; reached from fdps_title_screen with nothing loaded
   there are no layers to compose from.

   IT ENDS BY PUTTING THE GAME BACK ON SCREEN AND ON THE DISC: the triggered
   cell changes are reapplied, one view frame is composed, the "PlyPhase.saf"
   player-phase banner plays, and the chapter's music is started through
   fdps_cd_verify_disc_and_play_track, which holds the game still until the
   right disc is in the drive (cdaudio.h). */
extern void fdps_load_savegame(void);
#pragma aux fdps_load_savegame "*" parm caller [];

#endif
