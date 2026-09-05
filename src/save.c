/* save.c -- the save and load screens and the modal slot cursor they share.
 *
 * See save.h for what each screen leaves behind.  The page the slots are drawn
 * on is savepnl.c's, and the FDE.SAV image the save screen writes -- its shape
 * on disc, and the checksum and cipher that guard it -- is savefile.c's.
 *
 * malloc and free come from <stdlib.h>, memmove and memset from <string.h>,
 * fopen, fclose, fread and fwrite from <stdio.h>, inp from <conio.h> and
 * _dos_getdate and _dos_gettime with their two structures from <dos.h>, which
 * is where Watcom 10.0a declares each of them; all of them are real calls in
 * the original -- CALL 0x0003d375 at 00024759, CALL 0x0003d478 at 00024800 and
 * 00024811, CALL 0x0003d514 at 00024771 and 000247e1, and CALL 0x0003d4e4 at
 * 000247c7 -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <conio.h>
#include <dos.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "vfs.h"
#include "audio.h"
#include "sprite.h"
#include "palcycle.h"
#include "transit.h"
#include "save.h"
#include "savepnl.h"
#include "savefile.h"

/* The adapter.  0xa0000 is where the display answers in mode 13h and 0x140 is
   its row stride, PUSH 0xa0000 at 000247dc and PUSH 0x140 at 000247a4, and both
   stay literals here because neither is the address or the size of anything
   this program defines (contract E in rebuild_info/emit_pipeline.md).  0xfa00
   is 320 * 200, the whole page, PUSH 0xfa00 at 00024754, 00024764 and
   000247d3. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

/* What fdps_save_slot_select_loop hands back, and the value both screens spin
   their outer loop on.  0 means it has not finished, so it is also the value
   the loop tests to decide whether to run another pass: MOV dword ptr
   [EBP-0x10],0x0 at 0002465c, 0xffffffff at 0002470c and 0x1 at 0002473f. */
#define SLOT_SELECT_OPEN 0
#define SLOT_SELECT_CANCELLED (-1)
#define SLOT_SELECT_CONFIRMED 1

/* The whole FDE.SAV image and where the four slots sit inside it, PUSH 0x59cb
   at 0002488c, 000248d0 and 000248ef for the size, ADD EDX,0x312b at 00024929
   for the base and IMUL EAX,[EBP-0x8],0xa28 at 0002491f for the stride -- and
   the same three numbers again in fdps_save_game_screen at 0002426b, 000242ee
   and 000242e4.  The stride is sizeof(struct fdps_save_slot) exactly
   (fdpstype.h), and 0x312b + 4 * 0xa28 is the 0x59cb the file holds -- the
   fourth slot is on disc and is not reachable from either screen (save.h). */
#define SAVE_IMAGE_BYTES 0x59cb
#define SAVE_IMAGE_SLOTS_AT 0x312b
#define SAVE_SLOT_STRIDE 0xa28

/* The chapter byte of a slot that has never been written, CMP EAX,0xff at
   00024942 on the byte the AND EAX,0xff at 0002493d just widened -- the byte
   value 255 and not -1 (contract C in rebuild_info/emit_pipeline.md).

   IT IS ALSO THE FILL BYTE, PUSH 0xff at 000248f4 and at 000242d3, and that is
   not a coincidence: filling the whole image with it is exactly how a missing
   FDE.SAV makes every slot read as never written -- for the screen that draws
   the slots and for the screen that is about to write one.
   fdps_draw_save_slot_panel tests the same byte for the same thing under its
   own name, because it is reached with the record already in hand and never
   sees the fill (savepnl.c). */
#define SAVE_SLOT_UNWRITTEN_CHAPTER 0xff

/* Which of the two screens is running, MOV dword ptr [0x00064104],0x0 at
   000241fa.  Zero is the save screen, and it is what makes
   fdps_save_slot_select_loop accept a confirm on a slot that has never been
   written (save.h). */
#define SAVELOAD_MODE_SAVE 0

/* The member the page is composed from, PUSH 0x61adc at 00024204 and the same
   address again at 00024431: ONE copy of the name reaches both calls in the
   original, and identical literals in one translation unit are pooled, which
   is that same single copy.

   IT IS WRITTEN TO.  The name reaches strupr inside fdps_vfs_load_entry, which
   upper-cases the caller's own storage in place (vfs.h), so it is folded to
   "SAVE.CEL" by the first build and stays that way
   (rebuild_info/pitfalls.md). */
#define SAVE_SCREEN_BACKGROUND "Save.cel"

/* How much live game state one save carries, PUSH 0xa00 at 00024311: the
   0xa00 bytes at data_fdps_roster_array_ptr, which is the roster block and the
   60 bytes that follow it (struct fdps_save_slot in fdpstype.h).  The five
   stamp fields are written back over its last 0x14 bytes AFTERWARDS and are
   part of it, not appended to it -- see the note on the function below. */
#define SAVE_LIVE_STATE_BYTES 0xa00

/* Where the image carries its own checksum, MOV dword ptr [EDX + 0x59c7],EAX
   at 000243df: a dword four bytes from the end, which is exactly the four
   bytes fdps_compute_save_checksum leaves out of the sum (save.h). */
#define SAVE_CHECKSUM_AT 0x59c7

/* The mosaic reveal both ends of the screen use, the eleven pushes at 00024215
   through 00024233 and the identical set at 00024453 through 00024476: the
   whole 320x200 picture at a stride of 0x140 onto the adapter, cut into 4x3
   blocks over a 16x16 phase grid, one delay() tick between cells (transit.h).
   Every one of the eleven is the same at both call sites; only the source page
   differs. */
#define SAVE_TRANSITION_WIDTH 0x140
#define SAVE_TRANSITION_HEIGHT 0xc8
#define SAVE_TRANSITION_GRID_COLS 0x10
#define SAVE_TRANSITION_GRID_ROWS 0x10
#define SAVE_TRANSITION_BLOCK_W 4
#define SAVE_TRANSITION_BLOCK_H 3
#define SAVE_TRANSITION_DELAY 1

/* 000241e0.  The save screen: it reveals the slot panel, lets the player pick
   a slot, writes the live game state into that slot of FDE.SAV and offers the
   panel again, until the player backs out.  See save.h for the argument and
   for what the screen leaves behind.

   THE OUTER LOOP IS A while AND ITS CONDITION IS SEEDED WITH THE CONFIRM
   VALUE.  MOV dword ptr [EBP-0x10],0x1 at 000241f3 puts the "confirmed"
   answer into the variable before anything has been selected, and the test at
   00024244 is at the top: that is what runs the first pass.  The 1 is not an
   answer, it is the value that means "go round", so the panel is shown once
   for free and then once more after every save.

   THE SELECTION IS TESTED TWICE AGAINST THE SAME VALUE and the two tests are
   different questions: 00024244 asks whether to run another pass, 00024261
   asks whether this pass picked a slot.  Cancelling answers -1, which fails
   both.

   THE CURSOR IS CLEARED ONCE, at 000241ec, and never again: the loop hands the
   same variable to fdps_save_slot_select_loop on every pass, so the cursor the
   player left it on is where the next pass starts.

   THE STAMP LANDS INSIDE THE COPIED BLOCK AND NOT AFTER IT.  ADD dword ptr
   [EBP-0x18],0xa00 at 00024328 walks the record pointer to the end of the
   0xa00 bytes memmove has just written, and the five stores that follow it are
   at -0x4, -0x8, -0xc, -0x10 and -0x14 from there: the month, the day, the
   hour, the minute and the lottery flag overwrite the last 0x14 bytes of the
   live state that was copied a moment ago.  Appending them after the block
   instead shifts every header field by 0x14 and neither fdps_load_savegame nor
   fdps_draw_save_slot_panel would find anything where it looks
   (rebuild_info/pitfalls.md).

   THE DATE AND THE TIME ARE READ BEFORE THE COPY, at 000242fd and 00024309,
   and the copy cannot disturb them: both land in this frame's own structures.

   EACH TIMESTAMP FIELD IS ONE BYTE WIDENED INTO A DWORD.  XOR EAX,EAX /
   MOV AL,byte ptr [...] in front of each of the four stores at 0002432f
   through 00024358: the record's fields are 32 bits, the DOS structures'
   members are 8, and the widening is unsigned.  So is the chapter byte, which
   goes the other way -- MOV DL,byte ptr [0x00069cf4] at 00024367 takes the low
   byte of an int global (contract C in rebuild_info/emit_pipeline.md).

   A MISSING FDE.SAV IS FILLED WITH 0xff AND IS NOT DECRYPTED.  The fread arm
   runs the cipher over what it read and the memset arm does not, because the
   fill is already plaintext; running the cipher over it would turn every
   untouched slot's chapter byte into something that is not 0xff and the panel
   would show three slots of garbage rather than three empty ones.

   THE CHECKSUM IS STORED BEFORE THE CIPHER RUNS, in this order: fopen, then
   fdps_compute_save_checksum over the plaintext at 000243d4, the store at
   000243df, then fdps_xor_crypt_buffer at 000243ee and only then the fwrite.
   The read path is the mirror image and there is no second cipher (save.h).

   THE WRITE STREAM IS NOT TESTED.  fopen's answer at 000243c0 goes straight
   into fwrite and fclose; only the READ fopen at 00024287 is checked.  A disc
   that cannot be written to therefore faults inside the CRT rather than
   telling the player anything, and the screen carries on as though the save
   had worked.

   THE PAGE IS REBUILT AFTER EVERY SAVE, at 00024437, which is the only reason
   the panel shows the slot that was just written: fdps_saveload_screen_build
   reads FDE.SAV every time and caches nothing (save.h).

   THREE CALLS' ANSWERS ARE READ.  fdps_saveload_screen_build's is the page,
   stored at 00024212 and 0002443f and used as the select loop's background and
   as the mosaic's source; malloc's is the save image, stored at 00024278 and
   used without a test, as everywhere else in this file; fopen's is tested once
   as described above.  fdps_compute_save_checksum's answer is the dword that
   goes into the image.  fread's count, fwrite's count, memmove, memset,
   fclose, free, fdps_xor_crypt_buffer and fdps_transition_random_blocks all
   return values the original never looks at.

   NOTHING IS REPORTED TO THE CALLER.  The function is void and the screen
   leaves data_fdps_ui_saveload_is_load_mode at 0 behind it. */
void fdps_save_game_screen(unsigned char *restore_page)
{
    /* The date and the time the stamp is taken from, filled by DOS and read
       one byte at a time.  Both are this frame's own storage. */
    struct dosdate_t saved_on_date;
    struct dostime_t saved_at_time;
    /* The whole FDE.SAV image, decrypted in place, or 0xff throughout when
       there is no file to read. */
    unsigned char *save_image;
    /* The save file: read once to pick the other slots up, written once to put
       this one down.  Only the read is tested. */
    FILE *save_fp;
    /* What the last pass of the slot cursor answered, and the loop's own
       condition.  Seeded with the confirm value so the first pass runs. */
    int selection;
    /* Which of the three panel slots the cursor is on, cleared once before the
       loop and carried across passes. */
    int slot_index;
    /* The 0xa28-byte record inside the image that this save is written into. */
    struct fdps_save_slot *slot_record;
    /* The composed 320x200 panel page, rebuilt after every save and freed on
       the way out. */
    unsigned char *page;

    slot_index = 0;
    selection = SLOT_SELECT_CONFIRMED;
    data_fdps_ui_saveload_is_load_mode = SAVELOAD_MODE_SAVE;

    page = fdps_saveload_screen_build(SAVE_SCREEN_BACKGROUND);
    fdps_transition_random_blocks((unsigned int) page, VGA_SCREEN_PITCH,
                                  (unsigned char *) VGA_SCREEN_BASE,
                                  VGA_SCREEN_PITCH, SAVE_TRANSITION_WIDTH,
                                  SAVE_TRANSITION_HEIGHT,
                                  SAVE_TRANSITION_GRID_COLS,
                                  SAVE_TRANSITION_GRID_ROWS,
                                  SAVE_TRANSITION_BLOCK_W,
                                  SAVE_TRANSITION_BLOCK_H,
                                  SAVE_TRANSITION_DELAY);

    while (selection == SLOT_SELECT_CONFIRMED) {
        selection = fdps_save_slot_select_loop(page, &slot_index);
        if (selection == SLOT_SELECT_CONFIRMED) {
            save_image = (unsigned char *) malloc((size_t) SAVE_IMAGE_BYTES);
            save_fp = fopen("FDE.SAV", "rb");
            if (save_fp != NULL) {
                fread(save_image, 1, (size_t) SAVE_IMAGE_BYTES, save_fp);
                fdps_xor_crypt_buffer(save_image,
                                      (unsigned int) SAVE_IMAGE_BYTES);
                fclose(save_fp);
            } else {
                memset(save_image, SAVE_SLOT_UNWRITTEN_CHAPTER,
                       (size_t) SAVE_IMAGE_BYTES);
            }

            slot_record = (struct fdps_save_slot *)
                          (save_image + SAVE_IMAGE_SLOTS_AT
                           + slot_index * SAVE_SLOT_STRIDE);
            _dos_getdate(&saved_on_date);
            _dos_gettime(&saved_at_time);
            memmove(slot_record, data_fdps_roster_array_ptr,
                    (size_t) SAVE_LIVE_STATE_BYTES);

            slot_record->save_month = (unsigned int) saved_on_date.month;
            slot_record->save_day = (unsigned int) saved_on_date.day;
            slot_record->save_hour = (unsigned int) saved_at_time.hour;
            slot_record->save_minute = (unsigned int) saved_at_time.minute;
            slot_record->bonus_lottery_drawn_flag =
                (unsigned int) data_fdps_bonus_lottery_drawn_flag;
            slot_record->chapter_index =
                (unsigned char) data_fdps_chapter_current_chapter_id;
            slot_record->roster_member_count =
                (unsigned char) data_fdps_roster_member_count;
            slot_record->party_gold = data_fdps_shared_party_total_gold;
            slot_record->terrain_hud_user_enabled =
                data_fdps_ui_terrain_hud_user_enabled;
            slot_record->battle_animation_enabled =
                data_fdps_ui_battle_animation_enabled;
            slot_record->bgm_enabled_flag = data_fdps_audio_bgm_enabled_flag;
            slot_record->sfx_enabled_flag = data_fdps_audio_sfx_enabled_flag;

            save_fp = fopen("FDE.SAV", "wb");
            *(unsigned int *) (save_image + SAVE_CHECKSUM_AT) =
                fdps_compute_save_checksum(save_image,
                                           (unsigned int) SAVE_IMAGE_BYTES);
            fdps_xor_crypt_buffer(save_image, (unsigned int) SAVE_IMAGE_BYTES);
            fwrite(save_image, 1, (size_t) SAVE_IMAGE_BYTES, save_fp);
            fclose(save_fp);
            free(save_image);

            free(page);
            page = fdps_saveload_screen_build(SAVE_SCREEN_BACKGROUND);
        }
    }

    free(page);
    fdps_transition_random_blocks((unsigned int) restore_page,
                                  VGA_SCREEN_PITCH,
                                  (unsigned char *) VGA_SCREEN_BASE,
                                  VGA_SCREEN_PITCH, SAVE_TRANSITION_WIDTH,
                                  SAVE_TRANSITION_HEIGHT,
                                  SAVE_TRANSITION_GRID_COLS,
                                  SAVE_TRANSITION_GRID_ROWS,
                                  SAVE_TRANSITION_BLOCK_W,
                                  SAVE_TRANSITION_BLOCK_H,
                                  SAVE_TRANSITION_DELAY);
}

/* The three names this screen pulls out of the game's data, all of them
   literals the original holds in its writable data segment: "MISC.VFS" at
   0x60128 and "LoadKon.cel" at 0x61af8 are the two pushes at 0002466e and
   00024668, "Beep.wav" at 0x61b04 is pushed at 000246c5 and 000246fb and
   "Sure.wav" at 0x61b10 at 00024746.

   ALL THREE ARE WRITTEN TO AND CANNOT LIVE IN READ-ONLY STORAGE.  The member
   name reaches strupr inside fdps_vfs_load_entry and the cue name reaches it
   inside fdps_play_sfx, both of which upper-case the caller's own storage in
   place (vfs.h, audio.h), so these literals are folded to upper case by the
   first pass through the loop and stay that way (rebuild_info/pitfalls.md). */
#define SLOT_PANEL_ARCHIVE "MISC.VFS"
#define SLOT_CURSOR_SHEET "LoadKon.cel"
#define SLOT_MOVE_SFX "Beep.wav"
#define SLOT_CONFIRM_SFX "Sure.wav"

/* The make codes the loop acts on, straight off the CMP immediates at 0002469b,
   000246a1, 000246d3, 000246d9, 00024706, 00024715 and 0002471b.  They are set
   1 scancodes as fdps_keyboard_isr queues them (keybd.h), not ASCII, so Enter
   is 0x1c and not '\r'.

   THERE IS NO KEYPAD DEL HERE.  0x53 cancels the ring menu and the message
   prompt; this loop does not compare against it at all, so Escape is the whole
   of the way out. */
#define SLOT_KEY_ESC 0x01
#define SLOT_KEY_ENTER 0x1c
#define SLOT_KEY_SPACE 0x39
#define SLOT_KEY_UP 0x48
#define SLOT_KEY_LEFT 0x4b
#define SLOT_KEY_RIGHT 0x4d
#define SLOT_KEY_DOWN 0x50

/* The lowest code the loop throws away.  CMP dword ptr [EBP-0x1c],0x7f / JGE at
   00024691 and 00024695, a SIGNED compare on the byte the reader widened, so
   0x7f itself is ignored along with everything above it -- which is what drops
   the 0xff fdps_read_keyboard_queue answers with when the ring is empty
   (keybd.h). */
#define SLOT_SCANCODE_IGNORED_FROM 0x7f

/* Where the highlight goes, PUSH 0xa at 000247a2 for the column and
   IMUL EAX,dword ptr [EAX],0x34 / ADD EAX,0x19 at 0002479b for the row: slot 0
   at row 25 and 52 rows to the next one, which puts the three highlights at
   rows 25, 77 and 129 of a 200-row page. */
#define SLOT_CURSOR_X 0x0a
#define SLOT_CURSOR_Y_FIRST 0x19
#define SLOT_CURSOR_Y_PITCH 0x34

/* How the highlight animates.  SHR EAX,0x2 / AND EAX,0x3 at 0002477e, then
   CMP dword ptr [EBP-0x14],0x3 / MOV 0x1 at 00024787: four timer ticks to a
   frame, four frames to a cycle and the fourth folded onto the second, so the
   visible order is 0, 1, 2, 1 over a sheet of exactly three sprites.  The shift
   is SHR and not SAR because data_fdps_timer_tick_counter is unsigned, which is
   what makes the cycle survive the counter wrapping. */
#define SLOT_CURSOR_FRAME_TICKS_SHIFT 2
#define SLOT_CURSOR_FRAME_MASK 3
#define SLOT_CURSOR_FRAME_ABSENT 3
#define SLOT_CURSOR_FRAME_FOLDED_TO 1

/* The mode pair the highlight is drawn with, PUSH 0x0 twice at 00024794 and
   00024796: mode 0, the opaque pass-through, with no operand (sprite.h). */
#define SLOT_CURSOR_BLIT_OPERAND 0
#define SLOT_CURSOR_BLIT_MODE 0

/* VGA input status register 1.  Bit 3 is set while the vertical retrace is in
   progress. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* 00024650.  The modal slot cursor both save/load screens run.  See save.h for
   the arguments, the answer, and the four behaviours a caller has to know
   about; what follows is how the assembly maps onto the C.

   THE KEY HANDLING IS A CHAIN AND NOT A SWITCH, and the order is the order the
   compares appear in: back, forward, cancel, confirm.  Every arm falls into the
   frame below it, so no key ever costs more or fewer than one frame and the
   loop reads the ring exactly once per frame.  Ten codes queued between two
   passes take ten more frames to drain, one each, which is what makes a held
   arrow key walk the cursor at the panel's frame rate.

   THE CURSOR WRAPS BY MODULUS AND THE MODULUS IS SIGNED.  Back is +2 and
   forward is +1, both taken mod 3 with IDIV after SAR EDX,0x1f -- 000246ac
   through 000246be and 000246e4 through 000246f4 -- and the remainder is stored
   back through the caller's pointer.  Adding 2 rather than subtracting 1 is what
   keeps the back step off the negative side of a signed remainder for any
   cursor already in 0..2; a cursor the caller started outside that range keeps a
   negative remainder and indexes the occupied flags below zero, and nothing here
   guards that.

   TWO CALLS' ANSWERS ARE READ.  fdps_vfs_load_entry's is the cursor sheet,
   stored at 00024677 and read on every frame as the drawer's first argument at
   000247b1, and freed at 00024811; malloc's is the compose page, stored at
   00024761 and used without a test, exactly as the animations use theirs
   (anim.c).  inp's is tested for bit 3 at 000247cf.  fdps_play_sfx,
   fdps_cel_blit_sprite, fdps_cycle_ui_palette, memmove and free all return
   values the original never looks at.

   THERE IS ONE RETRACE SPIN AND IT WAITS FOR THE RETRACE TO END, not to begin:
   TEST AL,0x8 / JNZ at 000247cf loops while bit 3 is SET.  So the page copy
   starts the moment the retrace finishes and runs into the visible frame, which
   is the opposite of what the fades in anim.c do -- they straddle the retrace
   with a spin on either side.  Inverting this one to wait for the retrace to
   begin would tear differently, not better.

   THE FRAME WAIT'S LATCH IS DELIBERATELY LEFT UNINITIALISED.  last_tick is read
   at 000247e9 before anything has written it, so the first frame does not wait
   and the panel appears without a tick of delay; giving it an initialiser adds
   one (rebuild_info/pitfalls.md).  data_fdps_timer_tick_counter is volatile at
   its declaration (gamedata.h) because of that wait: nothing inside it writes
   the counter, so a build allowed to hoist the load would spin here forever.
   The retrace spin reads a port and cannot be hoisted for the same reason.

   THE PAGE IS TAKEN AND GIVEN BACK EVERY FRAME, malloc at 00024759 and free at
   00024800 inside the loop, rather than once around it.  The sheet is the other
   way round -- loaded once at 0002466f and freed once at 00024811 -- so a run
   of n frames makes n allocations and one load.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
int fdps_save_slot_select_loop(void *background, int *slot)
{
    /* The three-sprite highlight sheet, LoadKon.cel, held for the whole run. */
    unsigned char *cursor_sheet;
    /* Cancelled, confirmed, or still open.  It is both the loop's condition and
       the answer, which is why the loop cannot end on the pass that reads
       nothing. */
    int result;
    /* The make code this pass took out of the ring, widened from the byte
       fdps_read_keyboard_queue sets in AL -- the AND EAX,0xff at 00024689.
       0xff means the ring was empty and is ignored by the threshold. */
    int scancode;
    /* The private page this frame is composed on, taken and released inside the
       loop.  malloc's answer is not tested, the same as the original. */
    unsigned char *compose_page;
    /* Which of the sheet's three sprites the highlight shows this frame. */
    int cursor_frame;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    result = SLOT_SELECT_OPEN;
    cursor_sheet = (unsigned char *) fdps_vfs_load_entry(SLOT_PANEL_ARCHIVE,
                                                         SLOT_CURSOR_SHEET);

    while (result == SLOT_SELECT_OPEN) {
        scancode = fdps_read_keyboard_queue();

        if (scancode < SLOT_SCANCODE_IGNORED_FROM) {
            if (scancode == SLOT_KEY_UP || scancode == SLOT_KEY_LEFT) {
                *slot = (*slot + (SAVE_SLOT_COUNT - 1)) % SAVE_SLOT_COUNT;
                fdps_play_sfx(SLOT_MOVE_SFX);
            } else if (scancode == SLOT_KEY_RIGHT
                       || scancode == SLOT_KEY_DOWN) {
                *slot = (*slot + 1) % SAVE_SLOT_COUNT;
                fdps_play_sfx(SLOT_MOVE_SFX);
            } else if (scancode == SLOT_KEY_ESC) {
                result = SLOT_SELECT_CANCELLED;
            } else if ((scancode == SLOT_KEY_SPACE
                        || scancode == SLOT_KEY_ENTER)
                       && (data_fdps_ui_saveload_is_load_mode == 0
                           || data_fdps_ui_save_slot_occupied_flags[*slot]
                              != 0)) {
                result = SLOT_SELECT_CONFIRMED;
                fdps_play_sfx(SLOT_CONFIRM_SFX);
            }
        }

        compose_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
        memmove(compose_page, background, (size_t) VGA_SCREEN_BYTES);

        cursor_frame = (int) ((data_fdps_timer_tick_counter
                              >> SLOT_CURSOR_FRAME_TICKS_SHIFT)
                             & SLOT_CURSOR_FRAME_MASK);
        if (cursor_frame == SLOT_CURSOR_FRAME_ABSENT) {
            cursor_frame = SLOT_CURSOR_FRAME_FOLDED_TO;
        }

        fdps_cel_blit_sprite(cursor_sheet, cursor_frame, compose_page,
                             VGA_SCREEN_PITCH, SLOT_CURSOR_X,
                             *slot * SLOT_CURSOR_Y_PITCH + SLOT_CURSOR_Y_FIRST,
                             SLOT_CURSOR_BLIT_OPERAND, SLOT_CURSOR_BLIT_MODE);
        fdps_cycle_ui_palette();

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* Spin until the retrace ends, and only until then -- there is no
               second spin waiting for it to begin. */
        }
        memmove((void *) VGA_SCREEN_BASE, compose_page,
                (size_t) VGA_SCREEN_BYTES);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* Hold the frame until the timer interrupt moves the counter. */
        }
        last_tick = data_fdps_timer_tick_counter;

        free(compose_page);
    }

    free(cursor_sheet);
    return result;
}
