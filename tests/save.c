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
}
