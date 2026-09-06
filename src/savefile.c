/* savefile.c -- FDE.SAV itself: the integrity checksum and the XOR stream
 * cipher that guard the image on disc, and the load path that reads a whole
 * saved game back into the live game state.
 *
 * See savefile.h for the shape of the file and for the order the checksum and
 * the cipher are applied in.  The screens that put slots in front of the
 * player are save.c and the page they draw them on is savepnl.c.
 *
 * The checksum and the cipher are hand-written assembly in the original rather
 * than compiler output, which is why neither of them touches a global or calls
 * anything: each takes its whole input from its two arguments.
 *
 * malloc and free come from <stdlib.h> along with exit, memmove from
 * <string.h>, and fopen, fread, fclose and printf from <stdio.h>, which is
 * where Watcom 10.0a declares each of them.  All of them are real calls in the
 * original -- CALL 0x0003d375 at 00023e31, CALL 0x0003d478 at 00023f68, CALL
 * 0x0003d514 at 00023f15, CALL 0x0004265e at 00023e66, CALL 0x0004270d at
 * 00023e80, CALL 0x000428be at 00023e8c, CALL 0x00042deb at 00023e48 and CALL
 * 0x00042e0f at 00023e52 -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "vfs.h"
#include "rsrc.h"
#include "text.h"
#include "msgwin.h"
#include "maptile.h"
#include "mapdraw.h"
#include "anim.h"
#include "cdaudio.h"
#include "savefile.h"

/* 00056898.  Hand-written assembly, not compiler output: ESI walks the image
   with LODSB, EBX is the accumulator and ECX is a LOOP count.  The C below is
   the same arithmetic, not the same registers (ADR-0001).

   SUB ECX,0x4 at 000568a5 is the whole point of the routine and the one thing
   that must not be tidied: the last four bytes of the image are the stored
   checksum dword this result is compared against, so summing the buffer whole
   fails every save the game ever wrote (rebuild_info/pitfalls.md).

   XOR EAX,EAX at 000568aa runs once, before the loop, and LODSB writes only
   AL -- so every byte enters the sum zero-extended and the upper 24 bits stay
   clear for the whole walk.  Reading the image as signed char instead would
   subtract for every byte over 0x7f and no real save would verify.  ADD
   EBX,EAX is a 32-bit add and the sum is allowed to wrap there; over the
   0x59c7 bytes the callers actually pass it cannot, the ceiling being
   0x59c7 * 0xff.

   The loop is LODSB / ADD / LOOP, which tests the count after the body and not
   before, so the count is spelled here as a do-while and not as a for.  The
   difference is only visible at size == 4, where the original decrements 0 to
   0xffffffff and walks four billion bytes; that is reproduced rather than
   guarded because it is what the function does, and no call site can reach it.

   Two instructions in the original have no effect and are not carried over:
   MOV EDI,ESI at 000568a0 loads a register nothing here reads, and EBX is used
   as the accumulator without being saved or restored even though the stack
   convention makes it callee-saved.  Both are shared shape with
   fdps_xor_crypt_buffer immediately after it at 000568b7, which does use EDI
   for its STOSB.  The unsaved EBX destroys the caller's copy, but all four
   callers push and pop EBX themselves and use it only as a divisor loaded one
   or two instructions before an IDIV, never across this call, so nothing
   observes it. */
unsigned int fdps_compute_save_checksum(unsigned char *save_image,
                                        unsigned int size)
{
    unsigned char *image_cursor;
    unsigned int bytes_remaining;
    unsigned int checksum;

    image_cursor = save_image;
    bytes_remaining = size - 4;
    checksum = 0;

    do {
        checksum += (unsigned int) *image_cursor;
        image_cursor++;
        bytes_remaining--;
    } while (bytes_remaining != 0);

    return checksum;
}

/* 000568b7.  Hand-written assembly like the checksum above it, and sharing its
   shape: ESI walks the buffer with LODSB and EDI writes back over it with
   STOSB, both loaded from the same `buffer` argument at 000568bc/000568bf, and
   ECX drives a LOOP.  The two registers never separate, so the C below walks
   one cursor; that is the same arithmetic and not the same registers
   (ADR-0001).

   The key lives in DX and every step of it is sixteen bits wide.  ADD DX,0x9014
   at 000568c9 wraps at 0x10000 and drops the carry, and ROL DX,3 at 000568ce
   rotates bits 15..13 back into bits 2..0.  Widening the key to an int and
   rotating 32 bits instead would change the very first keystream byte from
   0xcc to 0xc8 and every one after it, and no save the game ever wrote would
   decrypt (rebuild_info/pitfalls.md).

   The key is advanced before the XOR and not after, so the seed 0xa5 is never
   itself used as a keystream byte: the first byte of the buffer meets
   rol16(0xa5 + 0x9014, 3) & 0xff, which is 0xcc.  XOR AL,DL at 000568d2 takes
   the low half of the key alone; DH is carried forward but never applied.

   The keystream depends on the byte index and never on the data, which makes
   the routine its own inverse -- the save path calls it to encrypt and the
   load path calls the same routine, unchanged, to decrypt.  There is no
   separate decryptor anywhere in the image.

   LOOP tests the count after the body, so this is a do-while and not a for: a
   length of 0 decrements to 0xffffffff and walks four billion bytes rather
   than none.  That is reproduced rather than guarded because it is what the
   function does, and no call site can reach it -- all eight push the literal
   0x59cb. */
void fdps_xor_crypt_buffer(unsigned char *buffer, unsigned int length)
{
    unsigned char *byte_cursor;
    unsigned int bytes_remaining;
    unsigned short key;

    byte_cursor = buffer;
    bytes_remaining = length;
    key = 0x00a5;

    do {
        key = (unsigned short) (key + 0x9014);
        key = (unsigned short) ((key << 3) | (key >> 13));
        *byte_cursor = (unsigned char) (*byte_cursor ^ (unsigned char) key);
        byte_cursor++;
        bytes_remaining--;
    } while (bytes_remaining != 0);
}

/* --- fdps_load_savegame @ 00023e20 -------------------------------------- */

/* The file, and the two containers the reload pulls resources out of.  All
   four names are literals reached through a MOV EAX,<address> / PUSH pair --
   0x60118 at 00023e60, 0x61a00 at 00023e5a and 00024087, 0x60128 at 00023f3a
   and 0x61a04 at 0002408d -- and none of them can be pointed anywhere else.

   THE TWO NAMES THAT GO THROUGH fdps_vfs_load_entry ARE WRITTEN TO.  That
   routine upper-cases the MEMBER name in the caller's own storage before it
   compares (vfs.h), so "Fde.pal" is folded to "FDE.PAL" by the first call and
   stays that way; the container name is copied raw and is left as it stands
   (rebuild_info/pitfalls.md).  The same goes for the animation member at the
   end of the body. */
#define SAVE_FILE_NAME "FDE.SAV"
#define SAVE_FILE_READ_MODE "rb"
#define RESOURCE_ARCHIVE "MISC.VFS"
#define MAIN_PALETTE_MEMBER "Fde.pal"
#define UNIT_SPRITE_SHEET "ICON.CEL"
#define PLAYER_PHASE_ANIMATION "PlyPhase.saf"

/* The three out-of-memory lines, at 0x61a50, 0x61a80 and 0x61aa4.  The second
   and third really do begin with a space and the first does not; all three end
   in a newline.  Each is followed by exit(1), which is why nothing below tests
   a pointer twice. */
#define OUT_OF_MEMORY_SAVE_IMAGE "Out of Memory on get save buffer!!!\n"
#define OUT_OF_MEMORY_FIELD_BLOCK " Out of Memory at get fieldadr!!!\n"
#define OUT_OF_MEMORY_UNIT_ARRAY " Out of Memory at get ManDataSeg!!!\n"

/* The whole image and where its checksum sits, PUSH 0x59cb at 00023e2c,
   00023e75, 00023e94 and 00023eb0 and CMP EAX,[EDX + 0x59c7] at 00023ec4 --
   the same two numbers save.c writes the file with. */
#define SAVE_IMAGE_BYTES 0x59cb
#define SAVE_CHECKSUM_AT 0x59c7

/* The four blocks of the battle resume image and the scalar header behind
   them, from the ADD EAX,<offset> in front of each memmove: 0x8a3 at 00023f09,
   0x12a3 at 00024043, 0x30a3 at 0002405c and 0x30c3 at 00023ea8.  The field
   block is at offset 0 and needs no add.

   THEY ARE CONTIGUOUS AND THAT IS LOAD BEARING: 0 + 0x8a3 is the roster's
   offset, 0x8a3 + 0xa00 is the unit array's, 0x12a3 + 0x1e00 is the flags' and
   0x30a3 + 0x20 is the header's, so the image is the five blocks written end
   to end with no padding anywhere (savefile.h).  Each length below is the one
   the memmove at that site carries, not a length computed from the next
   offset. */
#define SAVE_FIELD_BLOCK_BYTES 0x8a3
#define SAVE_ROSTER_AT 0x8a3
#define SAVE_ROSTER_BYTES 0xa00
#define SAVE_UNIT_ARRAY_AT 0x12a3
#define SAVE_TRIGGERED_FLAGS_AT 0x30a3
#define SAVE_TRIGGERED_FLAGS_BYTES 0x20
#define SAVE_RESUME_HEADER_AT 0x30c3

/* The map unit array as it is allocated rather than as it is filled, PUSH
   0x1e00 at 00024005: 96 records of 0x50, which is the ceiling on the unit
   count the header can ask for.  The copy that follows moves count * 0x50
   bytes and not the whole block, IMUL EAX,[0x00060150],0x50 at 00024038, so a
   header claiming more than 96 units overruns the block it was just given --
   nothing here checks it. */
#define MAP_UNIT_ARRAY_BYTES 0x1e00
#define MAP_UNIT_RECORD_BYTES 0x50

/* Which byte of the scalar header each global comes out of, as the offset from
   the header pointer the original keeps in [EBP-0xc] -- the MOV AL,byte ptr
   [EAX + <offset>] at 00024105, 00023ff8, 00023f50, 00024114, 00024127,
   0002413a, 0002414d, 00024160, 00024170, 0002417b, 00024186, 00024191 and
   0002419c.  The header is 0x12 bytes and this is all of it: +0x07 and +0x08
   are the only two nothing here reads, and the save side writes them as the
   literal zero -- MOV byte ptr [EAX + 0x7],0x0 at 00015257 and [EAX + 0x8],0x0
   at 0001525b -- so there is no state being dropped on the way back in.

   THE SAVE SIDE STORES THE FOUR COORDINATES DIVIDED, IDIV by 0x18 at 000151fb,
   00015216, 00015231 and 0001524c, which is what makes the multiply below the
   inverse of a signed divide rather than a scale invented here.

   EVERY BYTE THAT BECOMES AN int IS WIDENED UNSIGNED, through AND EAX,0xff
   after the MOV AL and before the IMUL or the store, so a byte of 0x80 or more
   arrives as 128..255 and never as a negative (contract C in
   rebuild_info/emit_pipeline.md).  The four toggles at +0x0e..+0x11 are copied
   byte to byte with no widening at all, because the globals they land in are
   themselves single bytes.

   +0x0a is the exception in width rather than in sign: MOV EAX,dword ptr
   [EAX + 0xa] at 00024170 is a 32-bit load from an offset that is not
   four-byte aligned, and the gold it carries is signed. */
#define RESUME_TURN_COUNTER 0x00
#define RESUME_UNIT_COUNT 0x01
#define RESUME_CHAPTER 0x02
#define RESUME_VIEW_ORIGIN_TILE_X 0x03
#define RESUME_VIEW_ORIGIN_TILE_Y 0x04
#define RESUME_CURSOR_TILE_X 0x05
#define RESUME_CURSOR_TILE_Y 0x06
#define RESUME_ROSTER_MEMBERS 0x09
#define RESUME_PARTY_GOLD 0x0a
#define RESUME_BATTLE_ANIM 0x0e
#define RESUME_TERRAIN_HUD 0x0f
#define RESUME_BGM_ENABLED 0x10
#define RESUME_SFX_ENABLED 0x11

/* Which byte of the field block the two map counts come out of, MOV AL,byte
   ptr [EAX + 0x1] at 00023fbf and [EAX + 0x2] at 00023fd1 on the block this
   function has just filled.  They are read back OUT OF THE INSTALLED BLOCK and
   not out of the image, which is the same bytes by then. */
#define FIELD_PLAYER_SLOT_COUNT 0x01
#define FIELD_CHAR_SPAWN_COUNT 0x02

/* One map tile is 24 pixels square, IMUL EAX,EAX,0x18 behind each of the four
   coordinate reads: the header stores the view origin and the cursor as TILE
   indices and every global that receives them is in world pixels. */
#define MAP_TILE_SIZE 0x18

/* The corrupt-save warning: face 0 into the message panel, PUSH 0x0 at
   00023ecc, and entry 0x208 of data_fdps_all_game_text_ptr, PUSH 0x208 at
   00023ee9.  It is the last of one contiguous family: 0x1f0 through 0x207 are
   fdps_battle_system_submenu's own save and load lines, pushed at 00014fbb,
   00014fef, 0001503a, 000150a2, 00015328, 0001535d, 000153ba, 000153ee and
   0001542d, and 0x209 is what an empty save slot prints (savepnl.c).  0x208 is
   the one entry of that run reached from nowhere but here.

   0xaa44a is screen (138, 131), the pen the panel's own text
   sits at, and 0x140 is the mode 13h row stride; both are addresses inside the
   display adapter's aperture rather than the address of anything the linker
   places, so they stay literals (contract E, rebuild_info/pitfalls.md).  The
   three colours are the standard message colours, PUSH 0x6d / PUSH 0x0 /
   PUSH 0xd0 at 00023ed6. */
#define CORRUPT_SAVE_FACE_INDEX 0
#define CORRUPT_SAVE_TEXT_ID 0x208
#define CORRUPT_SAVE_TEXT_ORIGIN 0x000aa44a
#define VGA_SCREEN_PITCH 0x140
#define MESSAGE_TEXT_FG_COLOR 0xd0
#define MESSAGE_TEXT_BG_COLOR 0
#define MESSAGE_TEXT_OUTLINE_COLOR 0x6d

/* Which of the two music entries the chapter's row offers, PUSH 0x0 at
   000241c8: the field track rather than the battle one (cdaudio.h). */
#define CD_TRACK_SLOT_FIELD 0

/* 00023e20.  See savefile.h for what the two call sites use it for and for
   what the corrupt-save arm does and does not do.

   THE FILE IS NOT CHECKED FOR ANYTHING BUT ITS CHECKSUM.  fopen's answer is
   used without a test, so a missing FDE.SAV faults inside fread rather than
   telling the player, and fread's count is discarded, so a short file is
   decrypted along with whatever the fresh malloc left in the tail.  Both are
   the same shape fdps_load_game_screen has (save.h) and neither is guarded
   here.

   THE ORDER THE BLOCKS ARE INSTALLED IN IS NOT ARBITRARY.  The chapter id is
   published before fdps_field_load_chapter_resources is called, because that
   routine formats every member name it opens out of it; the field block is
   installed before the two map counts are read, because they are read back out
   of the installed block; and the unit count is published before the unit
   array is copied, because the copy's length is count * 0x50.

   THREE FREES GUARD ON A DIFFERENT THING EACH.  The palette is freed when the
   POINTER is non-null, CMP dword ptr [0x000643bc],0x0 at 00023f1d; the unit
   array when the COUNT is non-zero, CMP dword ptr [0x00060150],0x0 at
   00023fde; the sprite cache likewise on its own count at 00024070 -- and the
   field block is freed with no test at all, at 00023f62, which is safe only
   because free(NULL) is defined.  Making the four agree would change what
   happens on the first load of a process, where the counts are zero and the
   pointers are not necessarily null. */
void fdps_load_savegame(void)
{
    /* The whole file, decrypted in place and freed at the end. */
    unsigned char *save_image;
    /* The scalar header inside it, kept in [EBP-0xc] from before the checksum
       is even computed and read from thirteen times below. */
    unsigned char *resume_header;
    /* The save file, and later the sprite sheet: the original reuses one
       stack slot, [EBP-0x4], for both. */
    FILE *save_fp;
    FILE *sprite_sheet_fp;
    /* The unit the sprite-cache pass is on, and the record it names. */
    int unit_index;
    struct fdps_unit_record *map_unit;

    save_image = (unsigned char *) malloc((size_t) SAVE_IMAGE_BYTES);
    if (save_image == NULL) {
        printf(OUT_OF_MEMORY_SAVE_IMAGE);
        exit(1);
    }

    save_fp = fopen(SAVE_FILE_NAME, SAVE_FILE_READ_MODE);
    fread(save_image, 1, (size_t) SAVE_IMAGE_BYTES, save_fp);
    fclose(save_fp);
    fdps_xor_crypt_buffer(save_image, (unsigned int) SAVE_IMAGE_BYTES);

    resume_header = save_image + SAVE_RESUME_HEADER_AT;

    /* CMP EAX,dword ptr [EDX + 0x59c7] at 00023ec4 against the value
       fdps_compute_save_checksum just returned in EAX: an unsigned compare of
       two 32-bit sums, and the stored dword is read out of the image at an
       offset that is not four-byte aligned. */
    if (fdps_compute_save_checksum(save_image, (unsigned int) SAVE_IMAGE_BYTES)
        != *(unsigned int *) (save_image + SAVE_CHECKSUM_AT)) {
        fdps_message_window_open(CORRUPT_SAVE_FACE_INDEX);
        fdps_draw_text(data_fdps_all_game_text_ptr, CORRUPT_SAVE_TEXT_ID,
                       (unsigned char *) CORRUPT_SAVE_TEXT_ORIGIN,
                       VGA_SCREEN_PITCH, MESSAGE_TEXT_FG_COLOR,
                       MESSAGE_TEXT_BG_COLOR, MESSAGE_TEXT_OUTLINE_COLOR);
        fdps_message_window_close();
    }

    /* The roster goes back into the block fdps_load_global_resources allocated
       at startup and never moves; the three blocks after it are each released
       and taken again. */
    memmove(data_fdps_roster_array_ptr, save_image + SAVE_ROSTER_AT,
            (size_t) SAVE_ROSTER_BYTES);

    if (data_fdps_vga_main_palette_ptr != NULL) {
        free(data_fdps_vga_main_palette_ptr);
    }
    data_fdps_vga_main_palette_ptr = (unsigned char *)
        fdps_vfs_load_entry(RESOURCE_ARCHIVE, MAIN_PALETTE_MEMBER);

    data_fdps_chapter_current_chapter_id = (int) resume_header[RESUME_CHAPTER];
    fdps_field_load_chapter_resources();

    free(data_fdps_tile_event_data_table_ptr);
    data_fdps_tile_event_data_table_ptr = (unsigned char *)
        malloc((size_t) SAVE_FIELD_BLOCK_BYTES);
    if (data_fdps_tile_event_data_table_ptr == NULL) {
        printf(OUT_OF_MEMORY_FIELD_BLOCK);
        exit(1);
    }
    memmove(data_fdps_tile_event_data_table_ptr, save_image,
            (size_t) SAVE_FIELD_BLOCK_BYTES);
    data_fdps_map_player_slot_count =
        (int) data_fdps_tile_event_data_table_ptr[FIELD_PLAYER_SLOT_COUNT];
    data_fdps_map_char_spawn_count =
        (int) data_fdps_tile_event_data_table_ptr[FIELD_CHAR_SPAWN_COUNT];

    if (data_fdps_map_unit_count != 0) {
        free(data_fdps_map_unit_array_ptr);
    }
    data_fdps_map_unit_count = (int) resume_header[RESUME_UNIT_COUNT];
    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) MAP_UNIT_ARRAY_BYTES);
    if (data_fdps_map_unit_array_ptr == NULL) {
        printf(OUT_OF_MEMORY_UNIT_ARRAY);
        exit(1);
    }
    memmove(data_fdps_map_unit_array_ptr, save_image + SAVE_UNIT_ARRAY_AT,
            (size_t) (data_fdps_map_unit_count * MAP_UNIT_RECORD_BYTES));
    memmove(data_fdps_map_cell_event_triggered_flags,
            save_image + SAVE_TRIGGERED_FLAGS_AT,
            (size_t) SAVE_TRIGGERED_FLAGS_BYTES);

    /* The sprite cache is emptied and refilled from scratch: the count is
       cleared and every unit's group is loaded again, so the slot numbers the
       records carried in the file are overwritten by the ones this pass hands
       out.  The sheet is opened BEFORE the count is cleared, which is the
       order at 00024087 and 0002409e. */
    if (data_fdps_cel_sprite_cache_count != 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    sprite_sheet_fp = fopen(UNIT_SPRITE_SHEET, SAVE_FILE_READ_MODE);
    data_fdps_cel_sprite_cache_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        /* The original reloads the array base out of the global on both sides
           of the call, MOV EDX,[0x00069cd8] at 000240cc and MOV EBX,
           [0x00069cd8] at 000240e9.  fdps_cache_cel_sprite_group writes only
           the three cel-cache globals and never this one (rsrc.h), so one
           read is the same address as two. */
        map_unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr
                   + unit_index;
        map_unit->sprite_cache_slot = (unsigned char)
            fdps_cache_cel_sprite_group((int) map_unit->portrait_id,
                                        sprite_sheet_fp);
    }
    fclose(sprite_sheet_fp);

    data_fdps_battle_turn_counter = (int) resume_header[RESUME_TURN_COUNTER];
    data_fdps_battle_view_window_origin_x =
        (int) resume_header[RESUME_VIEW_ORIGIN_TILE_X] * MAP_TILE_SIZE;
    data_fdps_battle_view_window_origin_y =
        (int) resume_header[RESUME_VIEW_ORIGIN_TILE_Y] * MAP_TILE_SIZE;
    data_fdps_map_cursor_world_x =
        (int) resume_header[RESUME_CURSOR_TILE_X] * MAP_TILE_SIZE;
    data_fdps_map_cursor_world_y =
        (int) resume_header[RESUME_CURSOR_TILE_Y] * MAP_TILE_SIZE;
    data_fdps_roster_member_count = (int) resume_header[RESUME_ROSTER_MEMBERS];
    data_fdps_shared_party_total_gold =
        *(int *) (resume_header + RESUME_PARTY_GOLD);
    data_fdps_ui_battle_animation_enabled = resume_header[RESUME_BATTLE_ANIM];
    data_fdps_ui_terrain_hud_user_enabled = resume_header[RESUME_TERRAIN_HUD];
    data_fdps_audio_bgm_enabled_flag = resume_header[RESUME_BGM_ENABLED];
    data_fdps_audio_sfx_enabled_flag = resume_header[RESUME_SFX_ENABLED];

    free(save_image);

    fdps_map_apply_triggered_cell_changes();
    fdps_render_view_frame();
    fdps_play_vfs_animation(PLAYER_PHASE_ANIMATION);
    fdps_cd_verify_disc_and_play_track(data_fdps_chapter_current_chapter_id,
                                       CD_TRACK_SLOT_FIELD);
}
