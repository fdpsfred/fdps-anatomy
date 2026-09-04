/* save.c -- FDE.SAV: the save and load screens, the slot panel, and the
 * checksum and XOR cipher that guard the file.
 *
 * See save.h for the image's shape on disc and for the order the checksum and
 * the cipher are applied in.
 *
 * malloc and free come from <stdlib.h>, memmove from <string.h> and inp from
 * <conio.h>, which is where Watcom 10.0a declares each of them; all four are
 * real calls in the original -- CALL 0x0003d375 at 00024759, CALL 0x0003d478 at
 * 00024800 and 00024811, CALL 0x0003d514 at 00024771 and 000247e1, and CALL
 * 0x0003d4e4 at 000247c7 -- because the flag set carries no -oi
 * (rebuild_info/build_flags.md), so the plain declarations are what reproduce
 * them.
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "gamedata.h"
#include "keybd.h"
#include "vfs.h"
#include "audio.h"
#include "sprite.h"
#include "palcycle.h"
#include "save.h"

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

/* What the loop hands back.  0 means it has not finished, so it is also the
   value the loop tests to decide whether to run another pass: MOV dword ptr
   [EBP-0x10],0x0 at 0002465c, 0xffffffff at 0002470c and 0x1 at 0002473f. */
#define SLOT_SELECT_OPEN 0
#define SLOT_SELECT_CANCELLED (-1)
#define SLOT_SELECT_CONFIRMED 1

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

/* The adapter.  0xa0000 is where the display answers in mode 13h and 0x140 is
   its row stride, PUSH 0xa0000 at 000247dc and PUSH 0x140 at 000247a4, and both
   stay literals here because neither is the address or the size of anything
   this program defines (contract E in rebuild_info/emit_pipeline.md).  0xfa00
   is 320 * 200, the whole page, PUSH 0xfa00 at 00024754, 00024764 and
   000247d3. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

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
