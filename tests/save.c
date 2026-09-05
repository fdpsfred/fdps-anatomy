/* tests/save.c -- cover for src/save.c.
 *
 * Expected values come from the assembly at 00056898 and 000568b7 -- SUB
 * ECX,0x4, XOR EAX,EAX before a LODSB that writes only AL, ADD EBX,EAX, and
 * for the cipher MOV DX,0xa5 / ADD DX,0x9014 / ROL DX,3 / XOR AL,DL / STOSB,
 * both loops closed by LOOP -- and from the shipped FDE.SAV itself.  None of
 * them is read off the emitted C.
 *
 * The checksum's buffers are staged here rather than read from a game file
 * because that function takes its entire input from its two arguments: it
 * reads no global and opens nothing.  The cipher is the routine that makes the
 * real file usable as a witness, and the last case does exactly that -- it
 * decrypts the shipped FDE.SAV and hands the plaintext to the checksum, which
 * has to agree with the dword the file itself stores at +0x59c7.  That single
 * assertion exercises the whole keystream over 22,987 bytes against a file the
 * game wrote, and neither routine can be wrong for it to hold.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "vfs.h"
#include "sprite.h"
#include "blit.h"
#include "rsrc.h"
#include "testharn.h"
#include "save.h"

/* 300 summed bytes plus the four skipped ones: 300 * 0xff overflows 16 bits,
   which is what the width case needs. */
#define WIDE_BYTES 304

static unsigned char stage_small[12];
static unsigned char stage_wide[WIDE_BYTES];

/* SUB ECX,0x4 at 000568a5.  Eight bytes summing to 36 followed by four 0xff
   bytes: the trailing four are the stored checksum field and must not enter
   the sum that gets compared against them.  Summing the buffer whole would
   give 36 + 4 * 0xff = 1056. */
static void trailing_four_bytes_are_not_summed(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        stage_small[i] = (unsigned char) (i + 1);
    }
    for (i = 8; i < 12; i++) {
        stage_small[i] = 0xff;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 36);
}

/* The boundary itself: the byte at size - 4 is the first one left out, and the
   byte at size - 5 is the last one taken in.  One 0x7f planted on either side
   of the line separates a count of size - 4 from a count of size - 3 or
   size - 5, which the case above cannot. */
static void the_skip_starts_exactly_at_size_minus_four(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[8] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0);

    stage_small[8] = 0x00;
    stage_small[7] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0x7f);
}

/* XOR EAX,EAX at 000568aa followed by a LODSB that writes AL alone: bytes
   enter the sum zero-extended.  0x80 + 0xff + 0x80 + 0xff is 766 unsigned and
   -258 if the image were read as signed char. */
static void bytes_are_summed_zero_extended(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[0] = 0x80;
    stage_small[1] = 0xff;
    stage_small[2] = 0x80;
    stage_small[3] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 766);
}

/* ADD EBX,EAX is a 32-bit add into a 32-bit accumulator.  300 bytes of 0xff
   sum to 76500, which does not fit in 16 bits: a narrower accumulator would
   report 76500 - 65536 = 10964. */
static void the_accumulator_is_thirty_two_bits_wide(void)
{
    int i;

    for (i = 0; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0xff;
    }
    for (i = WIDE_BYTES - 4; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0x00;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_wide, WIDE_BYTES), 76500L);
}

/* The shortest walk the loop can take without wrapping its count: size 5 sums
   exactly one byte.  LOOP tests after the body, so a size of 4 would walk 2^32
   bytes instead of none -- that one is described in save.c and deliberately
   not exercised here. */
static void size_five_sums_exactly_one_byte(void)
{
    stage_small[0] = 0x2a;
    stage_small[1] = 0xff;
    stage_small[2] = 0xff;
    stage_small[3] = 0xff;
    stage_small[4] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 5), 0x2a);
}

/* LODSB alone: there is no STOSB in this routine, unlike fdps_xor_crypt_buffer
   next to it, so the image comes back untouched.  A caller that computed the
   checksum over a buffer it was about to write out would otherwise be shipping
   whatever the routine had scribbled. */
static void the_image_is_not_written_to(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = (unsigned char) (0x11 * (i + 1));
    }

    fdps_compute_save_checksum(stage_small, 12);

    CHECK_EQ(stage_small[0], 0x11);
    CHECK_EQ(stage_small[7], 0x88);
    CHECK_EQ(stage_small[11], (unsigned char) (0x11 * 12));
}

/* ------------------------------------------------------ fdps_xor_crypt_buffer

   The first eight keystream bytes, worked out by hand from the four
   instructions that make one and confirmed byte for byte against the shipped
   file: FDE.SAV opens with cc 01 b7 53 on disc and 00 01 16 ff once decrypted,
   so its first four keystream bytes are cc 00 a1 ac.

   The very first byte is the load-bearing one.  0xcc is rol16(0xa5 + 0x9014,
   3) & 0xff, and it pins two things at once that no later byte can separate:
   that the key is advanced before the XOR rather than after -- the 0xa5 seed
   is never applied to anything -- and that the rotate is sixteen bits wide.  A
   32-bit key rotating a preserved carry would give 0xc8 here. */
#define KEYSTREAM_LENGTH 8

static const unsigned char keystream[KEYSTREAM_LENGTH] = {
    0xcc, 0x00, 0xa1, 0xac, 0x06, 0xd1, 0x2c, 0x04
};

static unsigned char crypt_stage[16];

/* Crypting a run of zeroes copies the keystream out where it can be read. */
static void the_keystream_is_the_low_half_of_the_rotating_key(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0x00;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], keystream[i]);
    }
}

/* The keystream is a function of the byte index alone -- nothing in the loop
   feeds a data byte back into DX.  A buffer of 0xff must therefore come out as
   the same keystream inverted, byte for byte, as the buffer of zeroes above.
   If the cipher were chained on its own output the two would diverge from the
   second byte on. */
static void the_keystream_does_not_depend_on_the_data(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0xff;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0xff ^ keystream[i]));
    }
}

/* The one property the game depends on: the same routine encrypts and
   decrypts, because the key is reseeded to 0xa5 on entry and the keystream
   restarts with it.  Both save call sites and both load call sites call this
   function and there is no second one. */
static void crypting_twice_restores_the_buffer(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        crypt_stage[i] = (unsigned char) (0x37 * i + 0x5a);
    }

    fdps_xor_crypt_buffer(crypt_stage, 16);
    fdps_xor_crypt_buffer(crypt_stage, 16);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0x37 * i + 0x5a));
    }
}

/* ECX is the byte count, and STOSB writes one byte per pass: a length of 1
   touches the first byte and stops.  The neighbour is checked because the
   LOOP-after-body shape makes an off-by-one here write past the end rather
   than fall short. */
static void the_length_is_a_byte_count(void)
{
    crypt_stage[0] = 0x00;
    crypt_stage[1] = 0x00;
    crypt_stage[2] = 0x00;

    fdps_xor_crypt_buffer(crypt_stage, 1);

    CHECK_EQ(crypt_stage[0], keystream[0]);
    CHECK_EQ(crypt_stage[1], 0x00);
    CHECK_EQ(crypt_stage[2], 0x00);
}

/* The shipped save, and the only witness that covers the whole keystream.
   FDE.SAV is 0x59cb bytes, which is the length all eight call sites push. */
#define SAVE_NAME "FDE.SAV"
#define SAVE_IMAGE_SIZE 0x59cbL
#define SAVE_CHECKSUM_OFFSET 0x59c7L
#define SAVE_STORED_CHECKSUM 0x002dedc4L

static unsigned char save_image[0x59cb];

static int load_shipped_save(void)
{
    FILE *fp;
    size_t got;

    fp = fopen(SAVE_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(save_image, 1, (size_t) SAVE_IMAGE_SIZE, fp);
    fclose(fp);
    return got == (size_t) SAVE_IMAGE_SIZE;
}

/* Decrypt the file the game wrote and let it check itself.  The dword at
   +0x59c7 is the checksum the game stored when it saved, and
   fdps_compute_save_checksum over the plaintext has to reproduce it: 22,983
   keystream bytes all have to be right for that sum to land, and the stored
   dword is four more.  The opening bytes are asserted separately so that a
   failure says whether the keystream went wrong at the start or somewhere in
   the middle.

   The case skips itself when the file is not staged, the way the container
   cases in tests/rsrc.c do: there is nothing to assert against and a
   fabricated stand-in would only be asserting against bytes this test
   wrote. */
static void the_shipped_save_decrypts_to_its_own_checksum(void)
{
    unsigned long stored_checksum;

    if (!load_shipped_save()) {
        return;
    }

    fdps_xor_crypt_buffer(save_image, (unsigned int) SAVE_IMAGE_SIZE);

    stored_checksum =
        (unsigned long) save_image[SAVE_CHECKSUM_OFFSET]
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 1] << 8)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 2] << 16)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 3] << 24);

    CHECK_EQ(save_image[0], 0x00);
    CHECK_EQ(save_image[1], 0x01);
    CHECK_EQ(save_image[2], 0x16);
    CHECK_EQ(save_image[3], 0xff);
    CHECK_EQ(stored_checksum, SAVE_STORED_CHECKSUM);
    CHECK_EQ(fdps_compute_save_checksum(save_image,
                                        (unsigned int) SAVE_IMAGE_SIZE),
             stored_checksum);
}

/* --------------------------------------------- fdps_save_slot_select_loop

   HOW THE LOOP IS WATCHED.  It composes every frame on a page it allocates and
   frees itself and copies that page whole to 0xa0000, so the adapter is the
   only place its output can be read back from.  Each case therefore sets mode
   13h, fills the screen with a sentinel, calls, snapshots the 64,000 bytes and
   returns to text mode -- the same way tests/menu.c watches the ring menu.
   Because the page is reseeded from the caller's background on every pass, the
   screen at the end is exactly the LAST frame and nothing accumulates.

   WHY A TIMER INTERRUPT IS INSTALLED, AND WHY IT STEPS BY SIXTEEN.  Every frame
   ends waiting for data_fdps_timer_tick_counter to change, so nothing returns at
   all unless something advances it.  The handler here adds sixteen rather than
   one, which is what makes the cursor frame predictable: the loop picks it as
   (tick >> 2) & 3, and sixteen is four times the four ticks a frame lasts, so
   any number of interrupts landing anywhere inside the call leaves that
   expression at the value the case seeded the counter with.  With a step of one
   the answer would depend on how long the container took to open.

   WHY THE SOUND PACK IS AN EMPTY IMAGE.  The move and confirm cues go to
   fdps_play_sfx, and an image whose entry count is zero makes the lookup miss
   without touching the name and without allocating anything.  Whether a cue
   reaches the mixer is fdps_play_sfx's own contract and is covered where it
   lives; here it only has to not disturb the frame or the heap.

   WHY THE CONTAINER IS THE REAL ONE.  The member name and the container name
   are both literals inside the function, so nothing can point it at a smaller
   fixture, and a container that will not open or a member that is not in it
   ends the process inside fdps_vfs_load_entry rather than failing an assertion.
   The cases skip themselves when MISC.VFS is not staged.  LOADKON.CEL's own
   header is what says the sheet holds three sprites, which is the reason the
   fourth animation frame is folded onto the second. */

#define SLOT_ARCHIVE "MISC.VFS"
#define SLOT_SHEET "LoadKon.cel"

#define SLOT_SCREEN_W 320
#define SLOT_SCREEN_H 200
#define SLOT_SCREEN_BYTES (SLOT_SCREEN_W * SLOT_SCREEN_H)
#define SLOT_VGA_BASE 0x000a0000
#define SLOT_MODE_TEXT 0x03
#define SLOT_MODE_320X200X256 0x13

/* The value the screen is filled with before every call: nothing the function
   draws can leave it in place, because the first thing every frame does is
   copy the whole background over the page. */
#define SLOT_SENTINEL 0xee

/* The hardware timer, and the step that keeps (tick >> 2) & 3 invariant. */
#define SLOT_TIMER_VECTOR 8
#define SLOT_TICK_STEP 16

/* The make codes, off the CMP immediates at 0002469b, 000246a1, 000246d3,
   000246d9, 00024706, 00024715 and 0002471b, plus three codes the loop does not
   know: 0x53 is the keypad Del that cancels the ring menu, 0xc8 is the break
   code of the up arrow, and 0xff is what an empty ring answers with. */
#define SLOT_KEY_ESC 0x01
#define SLOT_KEY_ENTER 0x1c
#define SLOT_KEY_SPACE 0x39
#define SLOT_KEY_UP 0x48
#define SLOT_KEY_LEFT 0x4b
#define SLOT_KEY_RIGHT 0x4d
#define SLOT_KEY_DOWN 0x50
#define SLOT_KEY_KEYPAD_DEL 0x53
#define SLOT_KEY_THRESHOLD 0x7f
#define SLOT_KEY_UP_BREAK 0xc8
#define SLOT_KEY_NONE 0xff

/* MOV dword ptr [EBP-0x10],0xffffffff at 0002470c and 0x1 at 0002473f. */
#define SLOT_CANCELLED (-1)
#define SLOT_CONFIRMED 1

/* PUSH 0xa at 000247a2, and IMUL EAX,dword ptr [EAX],0x34 / ADD EAX,0x19 at
   0002479b. */
#define SLOT_CURSOR_X 10
#define SLOT_CURSOR_Y_FIRST 25
#define SLOT_CURSOR_Y_PITCH 52

/* Where LOADKON.CEL states how many sprites it holds: the i16 at +0x0b of a
   .CEL header (resource_info/cel.md). */
#define SLOT_CEL_COUNT_AT 0x0b

static unsigned char slot_background[SLOT_SCREEN_BYTES];
static unsigned char slot_screen[SLOT_SCREEN_BYTES];
static unsigned char slot_reference[SLOT_SCREEN_BYTES];
/* The sprite-0 frame, kept aside so the frame cycle can be checked against
   itself.  A static and not a local: 64,000 bytes is more stack than the test
   runner has. */
static unsigned char slot_frame_zero[SLOT_SCREEN_BYTES];

static struct fdps_vfs_image_header slot_sfx_pack;

static void (__interrupt __far *slot_saved_timer)();

static int slot_blocks_before;
static int slot_blocks_after;
static unsigned int slot_ticks_used;

static void __interrupt __far slot_timer_isr(void)
{
    data_fdps_timer_tick_counter += SLOT_TICK_STEP;
    _chain_intr(slot_saved_timer);
}

static int slot_container_present(void)
{
    FILE *fp;

    fp = fopen(SLOT_ARCHIVE, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* The page the caller owns.  Every byte is different from the sentinel, so a
   screen that still holds the sentinel anywhere says the background did not
   reach it. */
static void slot_stage_background(void)
{
    int index;

    for (index = 0; index < SLOT_SCREEN_BYTES; index++) {
        slot_background[index] = (unsigned char) ((index * 7 + index / 320) & 0x7f);
    }
}

static void slot_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static int slot_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

/* The ring loaded with a burst and the two indices put where fdps_keyboard_isr
   would have left them.  EVERY BURST HAS TO END IN A KEY THAT ENDS THE LOOP:
   a drained ring answers 0xff, which the threshold ignores, and the pass costs
   a frame and goes round again, so a burst without a cancel or an accepted
   confirm in it hangs the run. */
static void slot_queue(unsigned char *codes, int count)
{
    int index;

    for (index = 0; index < count; index++) {
        data_fdps_input_scancode_queue[index] = codes[index];
    }
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = count;
}

static void slot_stage(void)
{
    slot_stage_background();
    memset(&slot_sfx_pack, 0, sizeof(slot_sfx_pack));
    data_fdps_audio_basewav_sfx_bank_buf_ptr = (unsigned char *) &slot_sfx_pack;
    data_fdps_ui_saveload_is_load_mode = 0;
    data_fdps_ui_save_slot_occupied_flags[0] = 0;
    data_fdps_ui_save_slot_occupied_flags[1] = 0;
    data_fdps_ui_save_slot_occupied_flags[2] = 0;
}

/* fdps_shutdown_free_resources frees data_fdps_audio_basewav_sfx_bank_buf_ptr
   unguarded and the pack above is a static, so it goes back to null; the ring
   goes back to empty so a burst left half drained is not read by the next case
   that polls the keyboard. */
static void slot_unstage(void)
{
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_ui_saveload_is_load_mode = 0;
    data_fdps_ui_save_slot_occupied_flags[0] = 0;
    data_fdps_ui_save_slot_occupied_flags[1] = 0;
    data_fdps_ui_save_slot_occupied_flags[2] = 0;
}

/* One whole run, with the adapter in the mode the screen uses, the ring loaded
   and a timer interrupt going, leaving the last frame in slot_screen[]. */
static int slot_run(int *slot, unsigned char *codes, int count,
                    unsigned int tick_base)
{
    int result;

    slot_blocks_before = slot_used_heap_blocks();
    slot_queue(codes, count);

    slot_set_mode(SLOT_MODE_320X200X256);
    memset((void *) SLOT_VGA_BASE, SLOT_SENTINEL, (size_t) SLOT_SCREEN_BYTES);

    data_fdps_timer_tick_counter = tick_base;
    slot_saved_timer = _dos_getvect(SLOT_TIMER_VECTOR);
    _dos_setvect(SLOT_TIMER_VECTOR, slot_timer_isr);
    result = fdps_save_slot_select_loop(slot_background, slot);
    _dos_setvect(SLOT_TIMER_VECTOR, slot_saved_timer);
    slot_ticks_used = (data_fdps_timer_tick_counter - tick_base)
                      / SLOT_TICK_STEP;

    memmove(slot_screen, (void *) SLOT_VGA_BASE, (size_t) SLOT_SCREEN_BYTES);
    slot_set_mode(SLOT_MODE_TEXT);
    slot_blocks_after = slot_used_heap_blocks();
    return result;
}

/* The sheet the loop draws from, loaded the same way it loads it.  The names go
   in through writable storage because the loader upper-cases the caller's own
   copy in place (vfs.h). */
static unsigned char *slot_load_sheet(void)
{
    char archive[16];
    char member[16];

    strcpy(archive, SLOT_ARCHIVE);
    strcpy(member, SLOT_SHEET);
    return (unsigned char *) fdps_vfs_load_entry(archive, member);
}

/* The frame the loop should have left on the adapter, composed by hand out of
   the same background and the same drawer. */
static void slot_build_reference(int frame, int slot_index)
{
    unsigned char *sheet;

    sheet = slot_load_sheet();
    memmove(slot_reference, slot_background, (size_t) SLOT_SCREEN_BYTES);
    fdps_cel_blit_sprite(sheet, frame, slot_reference, SLOT_SCREEN_W,
                         SLOT_CURSOR_X,
                         slot_index * SLOT_CURSOR_Y_PITCH + SLOT_CURSOR_Y_FIRST,
                         0, 0);
    free(sheet);
}

/* MOV dword ptr [EBP-0x10],0xffffffff at 0002470c: Escape alone ends the loop
   and the arm writes nothing else, so the caller's cursor comes back untouched.
   Escape is also the ONLY way out -- keypad Del, which cancels the ring menu
   and the message prompt, is not one of the codes this loop compares against,
   so the burst that starts with it needs the Escape behind it to end at all and
   the cursor still has not moved. */
static void slot_escape_is_the_only_cancel(void)
{
    int slot;
    unsigned char keys[2];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();

    slot = 1;
    keys[0] = SLOT_KEY_ESC;
    CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    slot = 1;
    keys[0] = SLOT_KEY_KEYPAD_DEL;
    keys[1] = SLOT_KEY_ESC;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(slot_blocks_after - slot_blocks_before, 0);
    slot_unstage();
}

/* CMP dword ptr [0x00064104],0x0 / JZ straight to the accept at 00024721: with
   the save screen's mode flag the occupied flags are never looked at, so both
   confirm keys take an empty slot.  All three flags are staged empty here, so a
   confirm that consulted them would leave the loop running until the ring drained
   and then hang. */
static void slot_save_screen_confirms_an_empty_slot(void)
{
    int slot;
    unsigned char keys[1];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    data_fdps_ui_saveload_is_load_mode = 0;

    slot = 0;
    keys[0] = SLOT_KEY_SPACE;
    CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CONFIRMED);
    CHECK_EQ(slot, 0);

    slot = 2;
    keys[0] = SLOT_KEY_ENTER;
    CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CONFIRMED);
    CHECK_EQ(slot, 2);

    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(slot_blocks_after - slot_blocks_before, 0);
    slot_unstage();
}

/* The other arm of the same test: with the load screen's mode flag set, the
   confirm has to find data_fdps_ui_save_slot_occupied_flags[*slot] non-zero --
   CMP dword ptr [EAX + 0x640f8],0x0 / JZ back into the frame at 00024736.  Only
   slot 1 is staged occupied, so the Enter on slot 0 has to be swallowed whole:
   the loop runs a frame and reads the next code.  Right then walks the cursor
   onto slot 1 and the second Enter is taken.  If the empty-slot confirm were
   accepted the run would answer with the cursor still on 0. */
static void slot_load_screen_needs_an_occupied_slot(void)
{
    int slot;
    unsigned char keys[3];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    data_fdps_ui_saveload_is_load_mode = 1;
    data_fdps_ui_save_slot_occupied_flags[1] = 1;

    slot = 0;
    keys[0] = SLOT_KEY_ENTER;
    keys[1] = SLOT_KEY_RIGHT;
    keys[2] = SLOT_KEY_ENTER;
    CHECK_EQ(slot_run(&slot, keys, 3, 0), SLOT_CONFIRMED);
    CHECK_EQ(slot, 1);

    /* And Space is refused on an empty slot exactly as Enter is: this run can
       only end on the Escape behind it. */
    slot = 2;
    keys[0] = SLOT_KEY_SPACE;
    keys[1] = SLOT_KEY_ESC;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 2);

    CHECK_EQ(_heapchk(), _HEAPOK);
    slot_unstage();
}

/* ADD EDX,0x2 / IDIV 3 at 000246ac: up and left are one arm and it steps the
   cursor BACK, wrapping 0 round to 2.  Adding two rather than subtracting one
   is what keeps the signed remainder off the negative side. */
static void slot_up_and_left_step_back(void)
{
    int slot;
    unsigned char keys[2];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    keys[1] = SLOT_KEY_ESC;

    slot = 2;
    keys[0] = SLOT_KEY_UP;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    slot = 2;
    keys[0] = SLOT_KEY_LEFT;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    slot = 0;
    keys[0] = SLOT_KEY_UP;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 2);

    CHECK_EQ(_heapchk(), _HEAPOK);
    slot_unstage();
}

/* INC EDX / IDIV 3 at 000246e4: right and down are the other arm and step
   FORWARD, wrapping 2 round to 0. */
static void slot_right_and_down_step_forward(void)
{
    int slot;
    unsigned char keys[2];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    keys[1] = SLOT_KEY_ESC;

    slot = 0;
    keys[0] = SLOT_KEY_RIGHT;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    slot = 0;
    keys[0] = SLOT_KEY_DOWN;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);

    slot = 2;
    keys[0] = SLOT_KEY_DOWN;
    CHECK_EQ(slot_run(&slot, keys, 2, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 0);

    CHECK_EQ(_heapchk(), _HEAPOK);
    slot_unstage();
}

/* CMP dword ptr [EBP-0x1c],0x7f / JGE at 00024691: everything at or above 0x7f
   is thrown away, and the compare is >= and not the <= that ends the message
   prompt's wait, so 0x7f itself is ignored here.  0xc8 is the break code of the
   up arrow and is the code with the teeth: a reader that masked the top bit
   off, or a threshold spelled against 0x80, would step the cursor.  Each
   ignored code still costs a whole frame, which is what the tick count says --
   four codes go through four passes and every pass but possibly the first waits
   out a tick. */
static void slot_codes_from_seven_f_up_are_ignored(void)
{
    int slot;
    unsigned char keys[4];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();

    slot = 1;
    keys[0] = SLOT_KEY_THRESHOLD;
    keys[1] = SLOT_KEY_NONE;
    keys[2] = SLOT_KEY_UP_BREAK;
    keys[3] = SLOT_KEY_ESC;
    CHECK_EQ(slot_run(&slot, keys, 4, 0), SLOT_CANCELLED);
    CHECK_EQ(slot, 1);
    CHECK_EQ(slot_ticks_used >= 3, 1);

    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(slot_blocks_after - slot_blocks_before, 0);
    slot_unstage();
}

/* PUSH 0xa for the column and IMUL by 0x34 / ADD 0x19 for the row: the
   highlight lands at column 10, row 25 + 52 * slot, on a page whose stride is
   0x140.  Byte-equal against a hand-composed frame is the strongest statement
   available here -- a different column, a different row, a different stride or
   a different sprite all change the picture -- and it also says the frame after
   the decision is drawn at all: the run below ends on Escape and its only pass
   is the pass that cancelled, so a loop that returned out of the branch would
   leave the sentinel on the adapter.

   The wrong-row check is what gives the row arithmetic teeth: slot 1's frame
   must not match slot 0's, so a pitch of anything but 52 fails. */
static void slot_cursor_lands_at_ten_and_twenty_five_plus_fifty_two(void)
{
    int slot;
    int slot_index;
    unsigned char keys[1];

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    keys[0] = SLOT_KEY_ESC;

    for (slot_index = 0; slot_index < SAVE_SLOT_COUNT; slot_index++) {
        slot = slot_index;
        CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CANCELLED);
        slot_build_reference(0, slot_index);
        CHECK_EQ(memcmp(slot_screen, slot_reference,
                        (size_t) SLOT_SCREEN_BYTES), 0);
    }

    slot = 0;
    CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CANCELLED);
    slot_build_reference(0, 1);
    CHECK_EQ(memcmp(slot_screen, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES) != 0, 1);

    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(slot_blocks_after - slot_blocks_before, 0);
    slot_unstage();
}

/* SHR EAX,0x2 / AND EAX,0x3 at 0002477e and the fold at 00024787: four ticks to
   a frame and the fourth frame folded onto the second, so a counter seeded at
   0, 4, 8 and 12 has to draw sprites 0, 1, 2 and 1.  The seeds are exact
   because the handler steps by sixteen, which cannot move (tick >> 2) & 3.

   The fold is not cosmetic and LOADKON.CEL says why: its header declares three
   sprites, so index 3 is past the end of an offset table the drawer does not
   range check (sprite.h).  The three references are also checked against each
   other, because "seed 12 draws sprite 1" says nothing unless sprite 1 is a
   different picture from sprites 0 and 2. */
static void slot_cursor_frame_cycles_and_folds_three_to_one(void)
{
    int slot;
    unsigned char keys[1];
    unsigned char *sheet;

    if (!slot_container_present()) {
        return;
    }
    slot_stage();
    keys[0] = SLOT_KEY_ESC;

    sheet = slot_load_sheet();
    CHECK_EQ((int) sheet[SLOT_CEL_COUNT_AT]
                 | ((int) sheet[SLOT_CEL_COUNT_AT + 1] << 8), 3);
    free(sheet);

    slot = 0;
    CHECK_EQ(slot_run(&slot, keys, 1, 0), SLOT_CANCELLED);
    slot_build_reference(0, 0);
    CHECK_EQ(memcmp(slot_screen, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES), 0);
    memmove(slot_frame_zero, slot_reference, (size_t) SLOT_SCREEN_BYTES);

    slot = 0;
    CHECK_EQ(slot_run(&slot, keys, 1, 4), SLOT_CANCELLED);
    slot_build_reference(1, 0);
    CHECK_EQ(memcmp(slot_screen, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES), 0);
    CHECK_EQ(memcmp(slot_frame_zero, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES) != 0, 1);

    slot = 0;
    CHECK_EQ(slot_run(&slot, keys, 1, 8), SLOT_CANCELLED);
    slot_build_reference(2, 0);
    CHECK_EQ(memcmp(slot_screen, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES), 0);
    CHECK_EQ(memcmp(slot_frame_zero, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES) != 0, 1);

    slot = 0;
    CHECK_EQ(slot_run(&slot, keys, 1, 12), SLOT_CANCELLED);
    slot_build_reference(1, 0);
    CHECK_EQ(memcmp(slot_screen, slot_reference,
                    (size_t) SLOT_SCREEN_BYTES), 0);

    CHECK_EQ(_heapchk(), _HEAPOK);
    CHECK_EQ(slot_blocks_after - slot_blocks_before, 0);
    slot_unstage();
}

/* ------------------------------------------------- fdps_draw_save_slot_panel

   HOW THE PANEL IS WATCHED.  It draws into a page the caller owns and at a
   pitch the caller chooses, so unlike the cursor loop above nothing has to
   touch the adapter: the cases hand it a page filled with a sentinel and read
   the page back.  Every sheet it draws out of is a global, so the fixture
   below fabricates three of them -- the Command.cel captions, the Number.cel
   figures and the font -- with each sprite painted a colour that says which
   sprite it was.  Reading one pixel then answers three questions at once:
   whether anything was drawn there, which sheet entry it came from, and, for a
   figure, which of Number.cel's colour rows it was taken from.

   THE FACE AND THE CHAPTER TITLE COME FROM THE REAL GAME FILES AND CANNOT BE
   STOOD IN FOR.  The function holds "ICON.CEL" and "Field.vfs" as literals and
   takes no file argument, so nothing can point it at a fixture; a missing
   member ends the process inside fdps_vfs_load_entry rather than failing an
   assertion.  The cases skip themselves when the two files are not staged.

   WHY THE FONT IS SOLID.  Every glyph in the fabricated sheet has all its bits
   set, so a run of n glyphs paints exactly n * advance columns of foreground
   and the column the run stops at is readable off the page.  That is what lets
   a case count the glyphs of a text entry it never has to decode -- which is
   how the chapter title is pinned to the member the name was formatted from. */

#define PANEL_PITCH 0x140
#define PANEL_PAGE_ROWS 200
#define PANEL_PAGE_BYTES (PANEL_PITCH * PANEL_PAGE_ROWS)

/* A pitch that is NOT the 0x140 the one call site passes.  The two message
   destinations are raw byte offsets added to `dest` with no multiply against
   the pitch, and at 0x140 that is indistinguishable from row 21 times pitch
   plus a column -- 21 * 320 + 100 is exactly 0x1aa4.  At 0x100 the two
   readings fall 1344 bytes apart, which is the only way to tell them. */
#define PANEL_ODD_PITCH 0x100

#define PANEL_SENTINEL 0xee

/* The fabricated Command.cel: 58 sprites, which is one more than the highest
   index the panel asks for.  A .CEL's offset table starts at +0x0f and holds
   one base-relative dword per sprite (resource_info/cel.md), and
   fdps_blit_command_sprite draws 25 by 22 of whatever the entry points at
   (sprite.h), so each stream is 22 rows of one fill op with a run of 25 --
   command byte 0x18 is op 00 with a run of (0x18 & 0x3f) + 1. */
#define PANEL_CEL_TABLE_AT 0x0f
#define PANEL_CMD_SPRITES 0x3a
#define PANEL_CMD_WIDTH_CMD 0x18
#define PANEL_CMD_ROWS 22
#define PANEL_CMD_STREAM_BYTES (PANEL_CMD_ROWS * 2)
#define PANEL_CMD_STREAMS_AT (PANEL_CEL_TABLE_AT + PANEL_CMD_SPRITES * 4)
#define PANEL_CMD_SHEET_BYTES \
    (PANEL_CMD_STREAMS_AT + PANEL_CMD_SPRITES * PANEL_CMD_STREAM_BYTES)
/* Caption colours are 0x80 + sprite index, which keeps them clear of the
   figure colours below and of the sentinel. */
#define PANEL_CMD_COLOR_BASE 0x80

/* The fabricated Number.cel.  fdps_draw_number picks its sprite as
   colour_row * 13 + glyph and reads the same +0x0f table (text.c), and draws 6
   by 8 -- command byte 0x05 is a run of 6.  Six colour rows is one more than
   the four the panel uses.  A sprite's colour is its index plus one, so no
   figure cell can hold zero and every (row, digit) pair is a different byte. */
#define PANEL_NUM_ROWS 6
#define PANEL_NUM_GLYPHS 13
#define PANEL_NUM_SPRITES (PANEL_NUM_ROWS * PANEL_NUM_GLYPHS)
#define PANEL_NUM_WIDTH_CMD 0x05
#define PANEL_NUM_CELL_W 6
#define PANEL_NUM_CELL_H 8
#define PANEL_NUM_STREAM_BYTES (PANEL_NUM_CELL_H * 2)
#define PANEL_NUM_STREAMS_AT (PANEL_CEL_TABLE_AT + PANEL_NUM_SPRITES * 4)
#define PANEL_NUM_SHEET_BYTES \
    (PANEL_NUM_STREAMS_AT + PANEL_NUM_SPRITES * PANEL_NUM_STREAM_BYTES)

/* The fabricated font: 1024 solid glyphs of 8 by 8, one byte a row.  1024 is
   above the highest glyph index the chapter titles this file reads reach --
   FDETXT04.TXT's title tops out at 903 -- so no entry indexes past the
   sheet. */
#define PANEL_GLYPHS 1024
#define PANEL_GLYPH_W 8
#define PANEL_GLYPH_H 8
#define PANEL_GLYPH_STRIDE 8
#define PANEL_FONT_BYTES (PANEL_GLYPHS * PANEL_GLYPH_STRIDE)

/* The fabricated global text block, laid out the way text.h describes: a table
   of signed 16-bit byte offsets from the block's own base, then the token
   streams.  Entry 0x209 -- the one an unwritten slot prints -- is a single
   glyph and every other entry is three, so the width of the run on the page
   says which entry was drawn. */
#define PANEL_EMPTY_TEXT_ID 0x209
#define PANEL_TEXT_ENTRIES (PANEL_EMPTY_TEXT_ID + 1)
#define PANEL_TEXT_TABLE_BYTES (PANEL_TEXT_ENTRIES * 2)
#define PANEL_TEXT_SHORT_AT PANEL_TEXT_TABLE_BYTES
#define PANEL_TEXT_LONG_AT (PANEL_TEXT_SHORT_AT + 4)
#define PANEL_TEXT_BYTES (PANEL_TEXT_LONG_AT + 8)
#define PANEL_TEXT_GLYPH 70
#define PANEL_TEXT_END (-1)
#define PANEL_TEXT_SHORT_GLYPHS 1
#define PANEL_TEXT_LONG_GLYPHS 3

/* The colours the two messages are drawn in, and the offsets they are drawn
   at: PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d and ADD EAX,0x1aa4 / ADD EAX,0x1ab5. */
#define PANEL_TEXT_FG 0xd0
#define PANEL_EMPTY_TEXT_AT 0x1aa4
#define PANEL_TITLE_TEXT_AT 0x1ab5

/* The record the cases hand in.  Every field is a value that formats to two
   distinct digits, so no two figures on the panel can be confused for each
   other and a month printed where the day belongs fails. */
#define PANEL_LEVEL 42
#define PANEL_MONTH 7
#define PANEL_DAY 25
#define PANEL_HOUR 13
#define PANEL_MINUTE 59

/* The chapter byte the panel is given, and what its title costs on the page.
   The member name is formatted from the byte PLUS ONE, so index 0 loads
   FDETXT01.TXT and index 3 loads FDETXT04.TXT; entry 1 of those two -- the
   chapter title -- is five tokens and three tokens long in the shipped
   FIELD.VFS, and neither holds a control code.  The two lengths are what
   separate "the name took the index plus one" from "the name took the index":
   FDETXT00.TXT's title is a different length again. */
#define PANEL_CHAPTER_FIRST 0
#define PANEL_CHAPTER_FIRST_TITLE_GLYPHS 5
#define PANEL_CHAPTER_FOURTH 3
#define PANEL_CHAPTER_FOURTH_TITLE_GLYPHS 3

/* The icon group the leader's face comes out of, and which sprite of the
   twelve the panel takes: the dword at cache + 4, which is slot 0's
   sprite_offset[1]. */
#define PANEL_PORTRAIT_GROUP 1
#define PANEL_PORTRAIT_SPRITE 1
#define PANEL_PORTRAIT_ALT_SPRITE 0
#define PANEL_PORTRAIT_ROW 10
#define PANEL_PORTRAIT_COL 8
#define PANEL_PORTRAIT_SIZE 0x18
/* The rows of the face that nothing else reaches: the lower caption starts at
   row 26 and overlaps the face's own box, and it is drawn first, so only the
   rows above it can be compared byte for byte. */
#define PANEL_PORTRAIT_CLEAN_ROWS 16

#define PANEL_ICON_SHEET "ICON.CEL"
#define PANEL_CHAPTER_ARCHIVE "FIELD.VFS"

static unsigned char panel_page[PANEL_PAGE_BYTES];
static unsigned char panel_reference[PANEL_PAGE_BYTES];
static unsigned char panel_command_sheet[PANEL_CMD_SHEET_BYTES];
static unsigned char panel_number_sheet[PANEL_NUM_SHEET_BYTES];
static unsigned char panel_font[PANEL_FONT_BYTES];
static unsigned char panel_text[PANEL_TEXT_BYTES];
static struct fdps_save_slot panel_slot;

/* Both game files, or neither: a case that ran with one of them missing would
   not fail, it would end the process. */
static int panel_files_present(void)
{
    FILE *fp;
    int found;

    found = 0;
    fp = fopen(PANEL_ICON_SHEET, "rb");
    if (fp != NULL) {
        found++;
        fclose(fp);
    }
    fp = fopen(PANEL_CHAPTER_ARCHIVE, "rb");
    if (fp != NULL) {
        found++;
        fclose(fp);
    }
    return found == 2;
}

static unsigned char panel_caption_color(int sprite_index)
{
    return (unsigned char) (PANEL_CMD_COLOR_BASE + sprite_index);
}

static unsigned char panel_figure_color(int color_row, int digit)
{
    return (unsigned char) (color_row * PANEL_NUM_GLYPHS + digit + 1);
}

static unsigned char panel_at(int row, int column)
{
    return panel_page[row * PANEL_PITCH + column];
}

static void panel_build_sheets(void)
{
    int sprite;
    int row;
    int stream_at;
    int index;

    for (sprite = 0; sprite < PANEL_CMD_SPRITES; sprite++) {
        stream_at = PANEL_CMD_STREAMS_AT + sprite * PANEL_CMD_STREAM_BYTES;
        *(int *) (panel_command_sheet + PANEL_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < PANEL_CMD_ROWS; row++) {
            panel_command_sheet[stream_at + row * 2] = PANEL_CMD_WIDTH_CMD;
            panel_command_sheet[stream_at + row * 2 + 1] =
                panel_caption_color(sprite);
        }
    }

    for (sprite = 0; sprite < PANEL_NUM_SPRITES; sprite++) {
        stream_at = PANEL_NUM_STREAMS_AT + sprite * PANEL_NUM_STREAM_BYTES;
        *(int *) (panel_number_sheet + PANEL_CEL_TABLE_AT + sprite * 4) =
            stream_at;
        for (row = 0; row < PANEL_NUM_CELL_H; row++) {
            panel_number_sheet[stream_at + row * 2] = PANEL_NUM_WIDTH_CMD;
            panel_number_sheet[stream_at + row * 2 + 1] =
                (unsigned char) (sprite + 1);
        }
    }

    for (index = 0; index < PANEL_FONT_BYTES; index++) {
        panel_font[index] = 0xff;
    }

    for (index = 0; index < PANEL_TEXT_ENTRIES; index++) {
        *(short *) (panel_text + index * 2) = (short) PANEL_TEXT_LONG_AT;
    }
    *(short *) (panel_text + PANEL_EMPTY_TEXT_ID * 2) =
        (short) PANEL_TEXT_SHORT_AT;
    *(short *) (panel_text + PANEL_TEXT_SHORT_AT) = PANEL_TEXT_GLYPH;
    *(short *) (panel_text + PANEL_TEXT_SHORT_AT + 2) = PANEL_TEXT_END;
    *(short *) (panel_text + PANEL_TEXT_LONG_AT) = PANEL_TEXT_GLYPH;
    *(short *) (panel_text + PANEL_TEXT_LONG_AT + 2) = PANEL_TEXT_GLYPH;
    *(short *) (panel_text + PANEL_TEXT_LONG_AT + 4) = PANEL_TEXT_GLYPH;
    *(short *) (panel_text + PANEL_TEXT_LONG_AT + 6) = PANEL_TEXT_END;
}

/* The page back to its sentinel, the sheets published, and the record loaded
   with the five fields the panel prints. */
static void panel_reset(int chapter_index)
{
    int index;

    memset(panel_page, PANEL_SENTINEL, (size_t) PANEL_PAGE_BYTES);
    panel_build_sheets();

    data_fdps_command_sprite_sheet_ptr = panel_command_sheet;
    data_fdps_number_glyph_sheet_ptr = panel_number_sheet;
    data_fdps_font_sheet_ptr = panel_font;
    data_fdps_all_game_text_ptr = panel_text;
    data_fdps_font_glyph_width = (unsigned char) PANEL_GLYPH_W;
    data_fdps_glyph_cell_height = (unsigned char) PANEL_GLYPH_H;
    data_fdps_font_glyph_stride_bytes = PANEL_GLYPH_STRIDE;
    data_fdps_font_outline_enabled_flag = (unsigned char) 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = PANEL_GLYPH_W;
    data_fdps_font_line_height = PANEL_GLYPH_H;

    for (index = 0; index < (int) sizeof(struct fdps_save_slot); index++) {
        ((unsigned char *) &panel_slot)[index] = 0;
    }
    panel_slot.roster[0].portrait_id = (unsigned char) PANEL_PORTRAIT_GROUP;
    panel_slot.roster[0].level = (unsigned char) PANEL_LEVEL;
    panel_slot.save_month = PANEL_MONTH;
    panel_slot.save_day = PANEL_DAY;
    panel_slot.save_hour = PANEL_HOUR;
    panel_slot.save_minute = PANEL_MINUTE;
    panel_slot.chapter_index = (unsigned char) chapter_index;
}

/* The cache the panel leaves behind: one group, allocated by the loader. */
static void panel_release_cache(void)
{
    if (data_fdps_cel_sprite_cache_ptr != NULL) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_buffer_used = 0;
}

static void panel_draw(int chapter_index, int color_row_on_entry)
{
    panel_reset(chapter_index);
    data_fdps_number_glyph_color_row = color_row_on_entry;
    panel_release_cache();
    fdps_draw_save_slot_panel(panel_page, PANEL_PITCH, &panel_slot);
}

/* One sprite of the leader's icon group, drawn onto the reference page at the
   place the panel puts the face.  Used to say which of the twelve the panel
   took. */
static void panel_build_reference_face(int sprite)
{
    FILE *sheet;
    unsigned char *face;

    memset(panel_reference, PANEL_SENTINEL, (size_t) PANEL_PAGE_BYTES);
    panel_release_cache();

    sheet = fopen(PANEL_ICON_SHEET, "rb");
    fdps_cache_cel_sprite_group(PANEL_PORTRAIT_GROUP, sheet);
    fclose(sheet);
    face = data_fdps_cel_sprite_cache_ptr
        + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
              ->sprite_offset[sprite];
    fdps_blit_dispatch(face,
                       panel_reference + PANEL_PITCH * PANEL_PORTRAIT_ROW
                           + PANEL_PORTRAIT_COL,
                       PANEL_PORTRAIT_SIZE, PANEL_PORTRAIT_SIZE, PANEL_PITCH,
                       0, 0);
    panel_release_cache();
}

/* The rows of the face box that only the face reaches, compared byte for
   byte.  A non-zero answer means the two pages differ somewhere in it. */
static int panel_face_differs(void)
{
    int row;

    for (row = PANEL_PORTRAIT_ROW;
         row < PANEL_PORTRAIT_ROW + PANEL_PORTRAIT_CLEAN_ROWS; row++) {
        if (memcmp(panel_page + row * PANEL_PITCH + PANEL_PORTRAIT_COL,
                   panel_reference + row * PANEL_PITCH + PANEL_PORTRAIT_COL,
                   (size_t) PANEL_PORTRAIT_SIZE) != 0) {
            return 1;
        }
    }
    return 0;
}

/* ADD EAX,0x1aa4 at 00024a71 with no IMUL against the pitch in front of it.
   Drawn at a pitch of 0x100, the message has to land at byte 0x1aa4 of the
   page and NOT at row 21 times that pitch plus column 100.  The run is one
   glyph wide, which is entry 0x209's stream and not the three-glyph stream
   every other entry of the block carries. */
static void panel_empty_slot_prints_at_a_raw_byte_offset(void)
{
    panel_reset(0);
    panel_slot.chapter_index = 0xff;
    data_fdps_number_glyph_color_row = 0;
    panel_release_cache();

    fdps_draw_save_slot_panel(panel_page, PANEL_ODD_PITCH, &panel_slot);

    CHECK_EQ(panel_page[PANEL_EMPTY_TEXT_AT], PANEL_TEXT_FG);
    CHECK_EQ(panel_page[PANEL_EMPTY_TEXT_AT
                        + PANEL_TEXT_SHORT_GLYPHS * PANEL_GLYPH_W - 1],
             PANEL_TEXT_FG);
    CHECK_EQ(panel_page[PANEL_EMPTY_TEXT_AT
                        + PANEL_TEXT_SHORT_GLYPHS * PANEL_GLYPH_W],
             PANEL_SENTINEL);
    CHECK_EQ(panel_page[21 * PANEL_ODD_PITCH + 100], PANEL_SENTINEL);
}

/* The JMP at 00024a8a goes straight to the epilogue: an unwritten slot draws
   the message and returns.  No caption, no face, no figure, and neither the
   colour-row selector nor the sprite cache is touched -- the cache count is
   left at a value the teardown would have zeroed and the pointer at a null the
   teardown would have handed to free. */
static void panel_empty_slot_draws_nothing_else(void)
{
    panel_reset(0);
    panel_slot.chapter_index = 0xff;
    data_fdps_number_glyph_color_row = 5;
    panel_release_cache();
    data_fdps_cel_sprite_cache_count = 5;

    fdps_draw_save_slot_panel(panel_page, PANEL_PITCH, &panel_slot);

    CHECK_EQ(data_fdps_number_glyph_color_row, 5);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 5);
    CHECK_EQ(data_fdps_cel_sprite_cache_ptr == NULL, 1);
    CHECK_EQ(panel_at(0x1a, 0x06), PANEL_SENTINEL);
    CHECK_EQ(panel_at(0x13, 0x27), PANEL_SENTINEL);
    CHECK_EQ(panel_at(0x13, 0x43), PANEL_SENTINEL);
    CHECK_EQ(panel_at(0x1d, 0x43), PANEL_SENTINEL);
    CHECK_EQ(panel_at(PANEL_PORTRAIT_ROW, PANEL_PORTRAIT_COL),
             PANEL_SENTINEL);

    data_fdps_cel_sprite_cache_count = 0;
}

/* Every figure at its own row and column, carrying its own field, in its own
   colour row.  The six positions are the IMUL/ADD pairs in front of the six
   fdps_draw_number calls -- rows 0x13 and 0x1d, columns 0x43, 0x51 and 0x64,
   two digits at six pixels each -- and the colours are the writes to
   data_fdps_number_glyph_color_row between them: nothing before the level, 3
   before the chapter, 1 before the date and 2 before the time.  A month drawn
   where the day belongs, or a chapter drawn in the date's colour row, fails
   here. */
static void panel_figures_carry_their_own_fields(void)
{
    if (!panel_files_present()) {
        return;
    }

    panel_draw(PANEL_CHAPTER_FIRST, 0);

    /* Level 42 at row 0x13, column 0x43, in the colour row the global held on
       entry. */
    CHECK_EQ(panel_at(0x13, 0x43), panel_figure_color(0, 4));
    CHECK_EQ(panel_at(0x13, 0x43 + PANEL_NUM_CELL_W), panel_figure_color(0, 2));

    /* Chapter 0 + 1 at row 0x1d, column 0x43, in colour row 3. */
    CHECK_EQ(panel_at(0x1d, 0x43), panel_figure_color(3, 0));
    CHECK_EQ(panel_at(0x1d, 0x43 + PANEL_NUM_CELL_W), panel_figure_color(3, 1));

    /* Month 7 at row 0x13, column 0x64 and day 25 at column 0x51, both in
       colour row 1. */
    CHECK_EQ(panel_at(0x13, 0x64), panel_figure_color(1, 0));
    CHECK_EQ(panel_at(0x13, 0x64 + PANEL_NUM_CELL_W), panel_figure_color(1, 7));
    CHECK_EQ(panel_at(0x13, 0x51), panel_figure_color(1, 2));
    CHECK_EQ(panel_at(0x13, 0x51 + PANEL_NUM_CELL_W), panel_figure_color(1, 5));

    /* Minute 59 at row 0x1d, column 0x64 and hour 13 at column 0x51, both in
       colour row 2. */
    CHECK_EQ(panel_at(0x1d, 0x64), panel_figure_color(2, 5));
    CHECK_EQ(panel_at(0x1d, 0x64 + PANEL_NUM_CELL_W), panel_figure_color(2, 9));
    CHECK_EQ(panel_at(0x1d, 0x51), panel_figure_color(2, 1));
    CHECK_EQ(panel_at(0x1d, 0x51 + PANEL_NUM_CELL_W), panel_figure_color(2, 3));

    panel_release_cache();
}

/* The level is drawn BEFORE the first write to the colour-row selector, so it
   comes out in whatever row the caller left behind, and the 0 written at
   00024d30 is what the next panel's level then inherits.  Handing the panel a
   colour row of 4 has to move the level's colour and nothing else's. */
static void panel_level_takes_the_incoming_colour_row(void)
{
    if (!panel_files_present()) {
        return;
    }

    panel_draw(PANEL_CHAPTER_FIRST, 4);

    CHECK_EQ(panel_at(0x13, 0x43), panel_figure_color(4, 4));
    CHECK_EQ(panel_at(0x13, 0x43 + PANEL_NUM_CELL_W), panel_figure_color(4, 2));
    CHECK_EQ(panel_at(0x1d, 0x43), panel_figure_color(3, 0));
    CHECK_EQ(data_fdps_number_glyph_color_row, 0);

    panel_release_cache();
}

/* The five captions -- the scroll's two halves, the "LV"/"MAP" labels, the
   arrow markers and the "/" and ":" separators -- each read at a pixel nothing
   later draws over.  The sample for sprite 0x38 is column 0x3c, which sprite
   0x37's 25-wide cell also covers: 0x37 is drawn first, so finding 0x38's
   colour there is also what says the two went down in the order the assembly
   has them. */
static void panel_captions_land_at_their_five_places(void)
{
    if (!panel_files_present()) {
        return;
    }

    panel_draw(PANEL_CHAPTER_FIRST, 0);

    CHECK_EQ(panel_at(0x1a, 0x06), panel_caption_color(0x2a));
    CHECK_EQ(panel_at(0x28, 0x1f), panel_caption_color(0x2b));
    CHECK_EQ(panel_at(0x13, 0x27), panel_caption_color(0x37));
    CHECK_EQ(panel_at(0x13, 0x3c), panel_caption_color(0x38));
    CHECK_EQ(panel_at(0x23, 0x5d), panel_caption_color(0x39));

    panel_release_cache();
}

/* MOV EAX,dword ptr [EAX + 0x4] at 00024af6: the face is sprite 1 of cache
   slot 0, and it is the right face only because the cache was emptied first.
   The comparison is against the same group's sprite 1 drawn independently, and
   against its sprite 0 to show the comparison can tell the two apart. */
static void panel_face_is_sprite_one_of_slot_zero(void)
{
    if (!panel_files_present()) {
        return;
    }

    panel_draw(PANEL_CHAPTER_FIRST, 0);

    panel_build_reference_face(PANEL_PORTRAIT_SPRITE);
    CHECK_EQ(panel_face_differs(), 0);

    panel_build_reference_face(PANEL_PORTRAIT_ALT_SPRITE);
    CHECK_EQ(panel_face_differs(), 1);

    panel_release_cache();
}

/* CMP dword ptr [0x00069cf0],0x0 / free / MOV 0x0 at 00024a8f through
   00024aa6.  Two runs, one with an empty cache and one with a cache of a
   single allocated block: the second has to give the block back, so its net
   change in used heap blocks is one lower than the first's.  The difference is
   what makes the assertion independent of whatever else the load allocates.

   The count afterwards is 1 and not 2, which is the other half of the same
   behaviour: the count was zeroed, so the loader seeded the cache rather than
   appending to it. */
static void panel_empties_the_cache_before_it_loads(void)
{
    int before;
    int without_old_cache;
    int with_old_cache;

    if (!panel_files_present()) {
        return;
    }

    panel_reset(PANEL_CHAPTER_FIRST);
    data_fdps_number_glyph_color_row = 0;
    panel_release_cache();
    before = slot_used_heap_blocks();
    fdps_draw_save_slot_panel(panel_page, PANEL_PITCH, &panel_slot);
    without_old_cache = slot_used_heap_blocks() - before;
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);
    panel_release_cache();

    panel_reset(PANEL_CHAPTER_FIRST);
    data_fdps_number_glyph_color_row = 0;
    data_fdps_cel_sprite_cache_ptr = (unsigned char *) malloc((size_t) 64);
    data_fdps_cel_sprite_cache_count = 1;
    before = slot_used_heap_blocks();
    fdps_draw_save_slot_panel(panel_page, PANEL_PITCH, &panel_slot);
    with_old_cache = slot_used_heap_blocks() - before;
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);
    panel_release_cache();

    CHECK_EQ(without_old_cache - with_old_cache, 1);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* sprintf(name, "fdetxt%02d.txt", chapter_index + 1) at 00024bf9, after the
   INC EAX at 00024be7.  The title is entry 1 of that member and the fixture's
   font paints eight columns a glyph, so the width of the run at 0x1ab5 counts
   the tokens the member holds: five for FDETXT01.TXT and three for
   FDETXT04.TXT, which are the shipped file's own.  A name built without the
   plus one would load FDETXT00.TXT for the first case and a title of a
   different length. */
static void panel_title_comes_from_the_chapter_after_the_index(void)
{
    if (!panel_files_present()) {
        return;
    }

    panel_draw(PANEL_CHAPTER_FIRST, 0);
    CHECK_EQ(panel_page[PANEL_TITLE_TEXT_AT
                        + PANEL_CHAPTER_FIRST_TITLE_GLYPHS * PANEL_GLYPH_W
                        - 1],
             PANEL_TEXT_FG);
    CHECK_EQ(panel_page[PANEL_TITLE_TEXT_AT
                        + PANEL_CHAPTER_FIRST_TITLE_GLYPHS * PANEL_GLYPH_W],
             PANEL_SENTINEL);
    panel_release_cache();

    panel_draw(PANEL_CHAPTER_FOURTH, 0);
    CHECK_EQ(panel_page[PANEL_TITLE_TEXT_AT
                        + PANEL_CHAPTER_FOURTH_TITLE_GLYPHS * PANEL_GLYPH_W
                        - 1],
             PANEL_TEXT_FG);
    CHECK_EQ(panel_page[PANEL_TITLE_TEXT_AT
                        + PANEL_CHAPTER_FOURTH_TITLE_GLYPHS * PANEL_GLYPH_W],
             PANEL_SENTINEL);
    CHECK_EQ(panel_at(0x1d, 0x43 + PANEL_NUM_CELL_W), panel_figure_color(3, 4));
    panel_release_cache();
}

/* ---- fdps_saveload_screen_build ------------------------------------------
 *
 * The whole screen, run against the real files: MISC.VFS for the background,
 * the shipped FDE.SAV for the slot contents, and ICON.CEL and FIELD.VFS for
 * what the three panels draw.  Nothing here can be stood in for -- the
 * function holds "FDE.SAV", "ICON.CEL" and "MISC.VFS" as literals and takes
 * only the background's member name -- so the cases skip themselves when the
 * files are not staged, the way the panel cases above do.
 *
 * The fixture sheets, font and text block the panels paint through are the
 * fabricated ones panel_reset publishes, so a figure's colour still says which
 * of Number.cel's rows it came from and a caption's colour still says which
 * Command.cel sprite it was.  Only the background, the save image and the icon
 * groups are real.
 *
 * Expected values come from the assembly at 00024830 -- ADD EDX,0x312b /
 * IMUL 0xa28 / byte [EAX+0xa00] / CMP 0xff for the slot marker, ADD EDX,0xd
 * with IMUL 0x34 / ADD 0x1a / IMUL 0x140 for where the panels go, and PUSH
 * 0x140 with six PUSH 0x0 for the background blit -- and from the shipped
 * FDE.SAV's own bytes.
 */

#define BUILD_ARCHIVE_NAME "MISC.VFS"
#define BUILD_SAVE_FILE "FDE.SAV"
/* Where the save file is moved to for the one case that has to run without
   it.  It is moved back before the case returns. */
#define BUILD_SAVE_FILE_ASIDE "FDE.BAK"

#define BUILD_PITCH 0x140
#define BUILD_PAGE_ROWS 200
#define BUILD_PAGE_BYTES (BUILD_PITCH * BUILD_PAGE_ROWS)

/* ADD EDX,0xd at 0002498c and IMUL EAX,[EBP-0x8],0x34 / ADD EAX,0x1a at
   0002497c: panel 0's top-left byte is pixel (13, 26) and the panels are 52
   rows apart. */
#define BUILD_PANEL_COL 0x0d
#define BUILD_PANEL_FIRST_ROW 0x1a
#define BUILD_PANEL_ROW_PITCH 0x34

/* The rows of the page no panel reaches.  The first thing any panel draws is
   the leader's face at panel row 10, which is page row 36, so everything above
   that is background and nothing else. */
#define BUILD_CLEAN_ROWS 36
#define BUILD_CLEAN_BYTES (BUILD_CLEAN_ROWS * BUILD_PITCH)

/* The first caption an occupied panel lays down, ADD EAX,0x1aa4 aside: sprite
   0x2a of Command.cel at panel row 0x1a, column 6, which is page (19, 52) for
   panel 0.  Column 19 is clear of the face, which is drawn afterwards and
   starts at column 21. */
#define BUILD_CAPTION_ROW 0x1a
#define BUILD_CAPTION_COL 0x06
#define BUILD_CAPTION_SPRITE 0x2a

/* What the shipped FDE.SAV holds: slot 0 was written in chapter index 1 and
   slots 1 and 2 have never been written.  The three chapter markers are 0x01,
   0xff and 0xff at 0x312b + slot * 0xa28 + 0xa00 of the decrypted image, which
   is what makes this file a witness with a mixed answer rather than a uniform
   one. */
#define BUILD_SHIPPED_FLAG_SLOT0 1
#define BUILD_SHIPPED_FLAG_SLOT1 0
#define BUILD_SHIPPED_FLAG_SLOT2 0

/* The roster the refill loop walks.  Three members with three DIFFERENT icon
   groups: fdps_cache_cel_sprite_group returns an existing slot without adding
   one for a group it already holds, so equal ids would not tell a loop that
   ran three times from one that ran once. */
#define BUILD_ROSTER_MEMBERS 3
#define BUILD_ROSTER_GROUP_FIRST 1

static unsigned char build_reference[BUILD_PAGE_BYTES];
static struct fdps_unit_record build_roster[BUILD_ROSTER_MEMBERS];

/* Both names are upper-cased in the caller's own storage by
   fdps_vfs_load_entry, so they are arrays and not pointers to literals. */
static char build_archive[] = BUILD_ARCHIVE_NAME;
static char build_save_bg[] = "Save.cel";
static char build_load_bg[] = "Load.cel";

/* All four files, or none: a case that ran with one of them missing would not
   fail, it would end the process inside the loader. */
static int build_files_present(void)
{
    FILE *fp;
    int found;

    found = 0;
    fp = fopen(BUILD_ARCHIVE_NAME, "rb");
    if (fp != NULL) {
        found++;
        fclose(fp);
    }
    fp = fopen(BUILD_SAVE_FILE, "rb");
    if (fp != NULL) {
        found++;
        fclose(fp);
    }
    return found == 2 && panel_files_present();
}

/* The fixture sheets published, the colour row put somewhere known and a
   roster of `members` staged.  panel_reset does the sheets, the font and the
   text block; the record it also loads is not used here. */
static void build_stage(int members)
{
    int index;

    panel_reset(PANEL_CHAPTER_FIRST);
    data_fdps_number_glyph_color_row = 0;
    panel_release_cache();

    for (index = 0; index < BUILD_ROSTER_MEMBERS; index++) {
        build_roster[index].portrait_id =
            (unsigned char) (BUILD_ROSTER_GROUP_FIRST + index);
    }
    data_fdps_roster_array_ptr = (unsigned char *) build_roster;
    data_fdps_roster_member_count = members;
}

/* What the run left in the sprite cache.  A run with an empty roster empties
   the cache and loads nothing, so the global still points at the block the
   last panel's loader allocated and this function has already freed -- that
   pointer must be dropped and not freed a second time. */
static void build_drop_cache(int members)
{
    if (members > 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_buffer_used = 0;
}

/* The background alone, laid into build_reference exactly the way the function
   lays it into its page: sprite 0 of the named member at the page origin,
   pitch 0x140, no operand and blit mode 0.  SAVE.CEL and LOAD.CEL are both a
   single 320x200 sprite that covers every byte of the page, so the comparison
   does not depend on what malloc handed the function. */
static void build_background_reference(char *cel_name)
{
    unsigned char *cel;

    cel = (unsigned char *) fdps_vfs_load_entry(build_archive, cel_name);
    fdps_cel_blit_sprite(cel, 0, build_reference, BUILD_PITCH, 0, 0, 0, 0);
    free(cel);
}

/* Where an unwritten slot's message lands on the page: the panel's origin plus
   the raw byte offset 0x1aa4 the drawer adds without touching the pitch, which
   is page (113, 47), (113, 99) and (113, 151) for the three slots. */
static long build_empty_text_at(int slot)
{
    return (long) BUILD_PANEL_COL
           + (long) (slot * BUILD_PANEL_ROW_PITCH + BUILD_PANEL_FIRST_ROW)
                 * BUILD_PITCH
           + (long) PANEL_EMPTY_TEXT_AT;
}

/* The three flags the screen publishes, against the file the game shipped.
   0x01 in slot 0 is not 0xff, so that slot is flagged occupied; slots 1 and 2
   are 0xff and are flagged empty.  A build that read the slots at the wrong
   base or with the wrong stride would not land on this pattern.

   The pixel at slot 0's message position is the other half of the same fact:
   an occupied slot draws figures there and never the "empty" message, so the
   foreground colour that message is drawn in must NOT be what is on the
   page. */
static void build_flags_the_slots_the_shipped_save_holds(void)
{
    unsigned char *page;

    if (!build_files_present()) {
        return;
    }

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_save_bg);

    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[0], BUILD_SHIPPED_FLAG_SLOT0);
    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[1], BUILD_SHIPPED_FLAG_SLOT1);
    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[2], BUILD_SHIPPED_FLAG_SLOT2);
    CHECK_EQ(page[build_empty_text_at(0)] == PANEL_TEXT_FG, 0);

    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);
}

/* The background, and that the argument is what picks it.  The top 36 rows of
   the page are the ones no panel reaches, so they are the blit's output and
   nothing else: against "Save.cel" they have to equal a reference blitted the
   same way, and against "Load.cel" they have to differ from it -- the two
   members disagree from their very first byte. */
static void build_blits_the_background_the_name_asks_for(void)
{
    unsigned char *page;

    if (!build_files_present()) {
        return;
    }

    build_background_reference(build_save_bg);

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_save_bg);
    CHECK_EQ(memcmp(page, build_reference, (size_t) BUILD_CLEAN_BYTES), 0);
    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_load_bg);
    CHECK_EQ(memcmp(page, build_reference, (size_t) BUILD_CLEAN_BYTES) != 0, 1);
    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);
}

/* Where the three panels go.  Panel 0 is occupied, so its first caption is on
   the page at (19, 52) in that sprite's own colour; panels 1 and 2 are empty,
   so each prints its message at its own raw offset, 52 rows below the one
   before it.  A column other than 13, a first row other than 26 or a spacing
   other than 52 moves all three. */
static void build_places_the_panels_thirteen_across_and_fifty_two_apart(void)
{
    unsigned char *page;
    long caption_at;

    if (!build_files_present()) {
        return;
    }

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_save_bg);

    caption_at = (long) BUILD_PANEL_COL
                 + (long) BUILD_PANEL_FIRST_ROW * BUILD_PITCH
                 + (long) BUILD_CAPTION_ROW * BUILD_PITCH
                 + (long) BUILD_CAPTION_COL;
    CHECK_EQ(page[caption_at], panel_caption_color(BUILD_CAPTION_SPRITE));
    CHECK_EQ(page[build_empty_text_at(1)], PANEL_TEXT_FG);
    CHECK_EQ(page[build_empty_text_at(2)], PANEL_TEXT_FG);

    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);
}

/* The cache is emptied and refilled from the roster, and the emptying is
   unconditional.  Each panel leaves the cache holding exactly one group, so a
   run over a three-member roster has to end at three -- not four, which is
   what appending to the panel's leftover would give -- and a run over an empty
   roster has to end at zero, which is only reachable if the emptying happens
   whether or not there is anything to put back. */
static void build_refills_the_cache_from_the_roster(void)
{
    unsigned char *page;

    if (!build_files_present()) {
        return;
    }

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_save_bg);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, BUILD_ROSTER_MEMBERS);
    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);

    build_stage(0);
    page = fdps_saveload_screen_build(build_save_bg);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 0);
    free(page);
    build_drop_cache(0);
}

/* Everything the run took is given back except the page.  The cel, the save
   image and every buffer the three panels used are freed inside the call, so
   once the caller frees the page and drops the cache the run has to be
   heap-neutral. */
static void build_gives_back_everything_but_the_page(void)
{
    unsigned char *page;
    int before;

    if (!build_files_present()) {
        return;
    }

    build_stage(BUILD_ROSTER_MEMBERS);
    before = slot_used_heap_blocks();
    page = fdps_saveload_screen_build(build_save_bg);
    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);

    CHECK_EQ(slot_used_heap_blocks() - before, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* No FDE.SAV at all.  The image is filled with 0xff instead of read, so every
   slot's chapter marker reads 0xff: all three flags come out empty and all
   three panels print the message, including slot 0, which the shipped file
   has occupied.  The file is moved aside and moved straight back; the case
   skips itself if either move fails rather than running against a state it
   does not understand. */
static void build_without_a_save_file_reads_every_slot_empty(void)
{
    unsigned char *page;

    if (!build_files_present()) {
        return;
    }

    remove(BUILD_SAVE_FILE_ASIDE);
    if (rename(BUILD_SAVE_FILE, BUILD_SAVE_FILE_ASIDE) != 0) {
        return;
    }

    build_stage(BUILD_ROSTER_MEMBERS);
    page = fdps_saveload_screen_build(build_save_bg);

    CHECK_EQ(rename(BUILD_SAVE_FILE_ASIDE, BUILD_SAVE_FILE), 0);

    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[0], 0);
    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[1], 0);
    CHECK_EQ(data_fdps_ui_save_slot_occupied_flags[2], 0);
    CHECK_EQ(page[build_empty_text_at(0)], PANEL_TEXT_FG);
    CHECK_EQ(page[build_empty_text_at(1)], PANEL_TEXT_FG);
    CHECK_EQ(page[build_empty_text_at(2)], PANEL_TEXT_FG);

    free(page);
    build_drop_cache(BUILD_ROSTER_MEMBERS);
}

void run_save_tests(void)
{
    RUN_TEST(trailing_four_bytes_are_not_summed);
    RUN_TEST(the_skip_starts_exactly_at_size_minus_four);
    RUN_TEST(bytes_are_summed_zero_extended);
    RUN_TEST(the_accumulator_is_thirty_two_bits_wide);
    RUN_TEST(size_five_sums_exactly_one_byte);
    RUN_TEST(the_image_is_not_written_to);
    RUN_TEST(the_keystream_is_the_low_half_of_the_rotating_key);
    RUN_TEST(the_keystream_does_not_depend_on_the_data);
    RUN_TEST(crypting_twice_restores_the_buffer);
    RUN_TEST(the_length_is_a_byte_count);
    RUN_TEST(the_shipped_save_decrypts_to_its_own_checksum);
    RUN_TEST(slot_escape_is_the_only_cancel);
    RUN_TEST(slot_save_screen_confirms_an_empty_slot);
    RUN_TEST(slot_load_screen_needs_an_occupied_slot);
    RUN_TEST(slot_up_and_left_step_back);
    RUN_TEST(slot_right_and_down_step_forward);
    RUN_TEST(slot_codes_from_seven_f_up_are_ignored);
    RUN_TEST(slot_cursor_lands_at_ten_and_twenty_five_plus_fifty_two);
    RUN_TEST(slot_cursor_frame_cycles_and_folds_three_to_one);
    RUN_TEST(panel_empty_slot_prints_at_a_raw_byte_offset);
    RUN_TEST(panel_empty_slot_draws_nothing_else);
    RUN_TEST(panel_figures_carry_their_own_fields);
    RUN_TEST(panel_level_takes_the_incoming_colour_row);
    RUN_TEST(panel_captions_land_at_their_five_places);
    RUN_TEST(panel_face_is_sprite_one_of_slot_zero);
    RUN_TEST(panel_empties_the_cache_before_it_loads);
    RUN_TEST(panel_title_comes_from_the_chapter_after_the_index);
    RUN_TEST(build_flags_the_slots_the_shipped_save_holds);
    RUN_TEST(build_blits_the_background_the_name_asks_for);
    RUN_TEST(build_places_the_panels_thirteen_across_and_fifty_two_apart);
    RUN_TEST(build_refills_the_cache_from_the_roster);
    RUN_TEST(build_gives_back_everything_but_the_page);
    RUN_TEST(build_without_a_save_file_reads_every_slot_empty);
}
