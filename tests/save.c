/* tests/save.c -- cover for src/save.c.
 *
 * Both the slot cursor and the save screen can only be watched on the adapter:
 * each composes every frame on a page it allocates and frees itself and copies
 * that page whole to 0xa0000.  Every case therefore sets mode 13h, fills the
 * screen with a sentinel, calls, snapshots the 64,000 bytes and returns to text
 * mode, the same way tests/menu.c watches the ring menu.
 *
 * Expected values come from the assembly at 00024650 and 000241e0 -- the CMP
 * immediates the key chain tests, IDIV 3 for the cursor wrap, SHR 2 / AND 3
 * with the fold at 00024787 for the highlight frame, and ADD [EBP-0x18],0xa00
 * with the five stores below it for the stamp -- and from the shipped FDE.SAV,
 * MISC.VFS, FIELD.VFS and ICON.CEL.  None of them is read off the emitted C.
 *
 * The screen cases run end to end against those real files, because nothing
 * about the screen can be stood in for: it names them as literals and takes no
 * argument that could point it anywhere else.  Each of them skips itself when
 * the files are not staged, and the save file is moved aside and put back
 * around the cases that write it.
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
#include "rsrc.h"
#include "testharn.h"
#include "save.h"
#include "savefile.h"

/* The checksum the shipped FDE.SAV stores at +0x59c7, which is what says the
   cases that must not write the file did not write it.  tests/savefile.c
   checks the same dword against the plaintext it decrypts. */
#define SAVE_STORED_CHECKSUM 0x002dedc4L

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

/* THE PANEL FIXTURE, THE SAME ONE tests/savepnl.c STAGES.  The save screen
   redraws the whole page through fdps_saveload_screen_build and
   fdps_draw_save_slot_panel, so every sheet, font and text block those two
   paint out of has to be published before the screen is called -- there is no
   argument that could point them anywhere else.  Only what screen_stage()
   below reaches is carried here; the assertions that read the fixture back
   belong to tests/savepnl.c. */
#define PANEL_PITCH 0x140
#define PANEL_PAGE_ROWS 200
#define PANEL_PAGE_BYTES (PANEL_PITCH * PANEL_PAGE_ROWS)

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

/* The record the cases hand in.  Every field is a value that formats to two
   distinct digits, so no two figures on the panel can be confused for each
   other and a month printed where the day belongs fails. */
#define PANEL_LEVEL 42
#define PANEL_MONTH 7
#define PANEL_DAY 25
#define PANEL_HOUR 13
#define PANEL_MINUTE 59

/* The chapter byte the panel is given.  The member name is formatted from the
   byte PLUS ONE, so index 0 loads FDETXT01.TXT. */
#define PANEL_CHAPTER_FIRST 0

/* The icon group the leader's face comes out of. */
#define PANEL_PORTRAIT_GROUP 1

#define PANEL_ICON_SHEET "ICON.CEL"
#define PANEL_CHAPTER_ARCHIVE "FIELD.VFS"

static unsigned char panel_page[PANEL_PAGE_BYTES];
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

#define BUILD_ARCHIVE_NAME "MISC.VFS"
#define BUILD_SAVE_FILE "FDE.SAV"

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

/* ---- fdps_save_game_screen -----------------------------------------------
 *
 * The whole save screen, run end to end against the real files, because
 * nothing about it can be stood in for: it names "Save.cel", "FDE.SAV", "rb"
 * and "wb" itself, takes only the page it restores on the way out, and its
 * one visible product is the file it leaves on disc.  So each case stages the
 * game state the screen copies, drives it with a burst of make codes through
 * the same ring fdps_save_slot_select_loop reads, and then reads FDE.SAV back
 * and decrypts it.
 *
 * THE SHIPPED SAVE IS MOVED ASIDE AND PUT BACK.  The screen rewrites the whole
 * file, so every case that confirms a slot renames FDE.SAV to FDE.BK2 first
 * and renames it back at the end; the cases that only cancel leave it where it
 * is.  Running from no file at all is also the arm that shows the 0xff fill:
 * the two slots the player did not pick have to come back reading as never
 * written.
 *
 * THE FOUR TIMESTAMP FIELDS ARE BRACKETED RATHER THAN STAGED.  They are the
 * only part of the record whose value does not come from a global -- the
 * screen takes them from _dos_getdate and _dos_gettime and nothing else -- and
 * the DOS clock is not this test's to set: DOSBox-X does not take the setting,
 * so a case that pinned it would skip itself and assert nothing.  Each field is
 * therefore read against the clock as it stood immediately before and
 * immediately after the run, which are one call apart, AND against the range
 * its DOS structure member can hold.  Between them a field stored at another
 * field's offset fails: a day in 13..31 cannot pass as a month and a minute in
 * 24..59 cannot pass as an hour, and on a value low enough for the range to
 * allow it the bracket still has to agree.  What the pair cannot separate is a
 * swap on the rare stroke where the two members happen to hold the same
 * number.
 *
 * EVERY OFFSET BELOW IS A RAW BYTE OFFSET AND NOT A STRUCT MEMBER.  The whole
 * point of these cases is where the fields land inside the record, so reading
 * them back through struct fdps_save_slot would only be asserting that the
 * emitted C agrees with itself.  They come off the assembly: ADD dword ptr
 * [EBP-0x18],0xa00 at 00024328 and the five stores at -0x4, -0x8, -0xc, -0x10
 * and -0x14 behind it, then +0x0, +0x1, +0x2, +0x6, +0x7, +0x8 and +0x9 in
 * front of it.
 *
 * A timer interrupt is installed for the length of every run for the same
 * reason the slot-cursor cases install one: the frames inside the cursor loop
 * do not end without it.
 */

#define SCREEN_SAVE_FILE "FDE.SAV"
#define SCREEN_SAVE_ASIDE "FDE.BK2"

/* PUSH 0x59cb at 0002426b, ADD EDX,0x312b at 000242ee, IMUL EAX,[EBP-0x14],
   0xa28 at 000242e4 and MOV dword ptr [EDX + 0x59c7],EAX at 000243df. */
#define SCREEN_IMAGE_BYTES 0x59cbL
#define SCREEN_SLOTS_AT 0x312bL
#define SCREEN_SLOT_STRIDE 0xa28L
#define SCREEN_CHECKSUM_AT 0x59c7L

/* PUSH 0xa00 at 00024311: how much live state the memmove copies, which is
   also where the stamp begins to overwrite it. */
#define SCREEN_LIVE_BYTES 0xa00
#define SCREEN_STAMP_AT 0x9ec

/* The five stamp fields, at -0x14 through -0x4 from the end of the copied
   block, and the seven header fields at +0x0 through +0x9 past it. */
#define SCREEN_AT_LOTTERY 0x9ec
#define SCREEN_AT_MINUTE 0x9f0
#define SCREEN_AT_HOUR 0x9f4
#define SCREEN_AT_DAY 0x9f8
#define SCREEN_AT_MONTH 0x9fc
#define SCREEN_AT_CHAPTER 0xa00
#define SCREEN_AT_MEMBERS 0xa01
#define SCREEN_AT_GOLD 0xa02
#define SCREEN_AT_TERRAIN 0xa06
#define SCREEN_AT_BATTLE_ANIM 0xa07
#define SCREEN_AT_BGM 0xa08
#define SCREEN_AT_SFX 0xa09

/* PUSH 0xff at 000242d3: the fill a missing file is stood up with, and the
   byte fdps_saveload_screen_build reads as "never written". */
#define SCREEN_UNWRITTEN 0xff

/* What each timestamp member of the two DOS structures can hold, straight off
   the comments in Watcom 10.0a's own <dos.h>: day 1-31, month 1-12, hour 0-23
   and minute 0-59.  Half of the field-to-offset check is that the value found
   at an offset is one its own member could have produced. */
#define SCREEN_MONTH_LOW 1
#define SCREEN_MONTH_HIGH 12
#define SCREEN_DAY_LOW 1
#define SCREEN_DAY_HIGH 31
#define SCREEN_HOUR_LOW 0
#define SCREEN_HOUR_HIGH 23
#define SCREEN_MINUTE_LOW 0
#define SCREEN_MINUTE_HIGH 59

/* The game state the two runs stage.  Every value is distinct so a field read
   out of the wrong global fails, and the two chapters are ones FIELD.VFS holds
   a text member for -- fdetxt01.txt and fdetxt04.txt -- because the panel the
   screen redraws after a save loads the saved chapter's title. */
#define SCREEN_CHAPTER_A 0
#define SCREEN_CHAPTER_B 3
#define SCREEN_GOLD_A 0x0001e240L
#define SCREEN_GOLD_B 0x00003039L
#define SCREEN_MEMBERS_A 3
#define SCREEN_MEMBERS_B 2
#define SCREEN_LOTTERY_A 1
#define SCREEN_LOTTERY_B 0
#define SCREEN_TERRAIN_A 1
#define SCREEN_TERRAIN_B 0
#define SCREEN_BATTLE_ANIM_A 0
#define SCREEN_BATTLE_ANIM_B 1
#define SCREEN_BGM_A 1
#define SCREEN_BGM_B 0
#define SCREEN_SFX_A 0
#define SCREEN_SFX_B 1

/* The 0xa00 bytes at data_fdps_roster_array_ptr that the save copies, and the
   image read back off disc. */
static unsigned char screen_live_state[SCREEN_LIVE_BYTES];
static unsigned char screen_image[0x59cb];
static unsigned char screen_restore[SLOT_SCREEN_BYTES];

/* The clock either side of a run, read one call before it starts and one call
   after it ends. */
static struct dosdate_t screen_date_before;
static struct dosdate_t screen_date_after;
static struct dostime_t screen_time_before;
static struct dostime_t screen_time_after;

/* Whether a value the record carries is one of the two readings that bracket
   the run, and inside the range its own DOS member can hold. */
static int screen_stamp_holds(unsigned long stored, int before, int after,
                              int low, int high)
{
    if (stored < (unsigned long) low || stored > (unsigned long) high) {
        return 0;
    }
    return stored == (unsigned long) before || stored == (unsigned long) after;
}

/* One byte of the staged live state: a function of the byte's own index and of
   which of the two states this is, so a record written at the wrong offset
   inside the image cannot match it and the two runs cannot be confused. */
static unsigned char screen_pattern_byte(int seed, int index)
{
    return (unsigned char) ((index * 5 + seed * 0x51 + 3) & 0xff);
}

/* The live game state the save copies, and the roster the redraw walks.  The
   last 0x14 bytes carry a value the stamp cannot leave in place. */
static void screen_stage_live_state(int seed, int members)
{
    struct fdps_unit_record *member;
    int index;

    for (index = 0; index < SCREEN_LIVE_BYTES; index++) {
        screen_live_state[index] = screen_pattern_byte(seed, index);
    }
    for (index = SCREEN_STAMP_AT; index < SCREEN_LIVE_BYTES; index++) {
        screen_live_state[index] = (unsigned char) 0xaa;
    }
    for (index = 0; index < members; index++) {
        member = (struct fdps_unit_record *) screen_live_state + index;
        member->portrait_id = (unsigned char) (index + 1);
        member->level = (unsigned char) (index + 4);
    }
    data_fdps_roster_array_ptr = screen_live_state;
    data_fdps_roster_member_count = members;
}

/* Everything the screen reads out of a global, staged as one of the two sets
   above.  panel_reset publishes the fixture sheets and the font the redrawn
   panels paint through; slot_stage publishes the empty sound pack and clears
   the ring. */
static void screen_stage(int set)
{
    panel_reset(PANEL_CHAPTER_FIRST);
    data_fdps_number_glyph_color_row = 0;
    panel_release_cache();
    slot_stage();

    if (set == 0) {
        screen_stage_live_state(0, SCREEN_MEMBERS_A);
        data_fdps_chapter_current_chapter_id = SCREEN_CHAPTER_A;
        data_fdps_shared_party_total_gold = (int) SCREEN_GOLD_A;
        data_fdps_bonus_lottery_drawn_flag = SCREEN_LOTTERY_A;
        data_fdps_ui_terrain_hud_user_enabled = (unsigned char) SCREEN_TERRAIN_A;
        data_fdps_ui_battle_animation_enabled =
            (unsigned char) SCREEN_BATTLE_ANIM_A;
        data_fdps_audio_bgm_enabled_flag = (unsigned char) SCREEN_BGM_A;
        data_fdps_audio_sfx_enabled_flag = (unsigned char) SCREEN_SFX_A;
    } else {
        screen_stage_live_state(1, SCREEN_MEMBERS_B);
        data_fdps_chapter_current_chapter_id = SCREEN_CHAPTER_B;
        data_fdps_shared_party_total_gold = (int) SCREEN_GOLD_B;
        data_fdps_bonus_lottery_drawn_flag = SCREEN_LOTTERY_B;
        data_fdps_ui_terrain_hud_user_enabled = (unsigned char) SCREEN_TERRAIN_B;
        data_fdps_ui_battle_animation_enabled =
            (unsigned char) SCREEN_BATTLE_ANIM_B;
        data_fdps_audio_bgm_enabled_flag = (unsigned char) SCREEN_BGM_B;
        data_fdps_audio_sfx_enabled_flag = (unsigned char) SCREEN_SFX_B;
    }
}

/* The page the screen puts back on the adapter when it ends.  Every byte is
   different from the sentinel the screen is filled with beforehand. */
static void screen_stage_restore_page(void)
{
    int index;

    for (index = 0; index < SLOT_SCREEN_BYTES; index++) {
        screen_restore[index] = (unsigned char) ((index * 3 + 1) & 0x7f);
    }
}

/* One whole run of the screen, driven by a burst of make codes.  EVERY BURST
   HAS TO END IN AN ESCAPE: the outer loop only leaves on a cancelled
   selection, and a drained ring answers 0xff, which the cursor loop ignores.
   The adapter is left holding the last thing the screen put there. */
static void screen_run(unsigned char *codes, int count)
{
    slot_queue(codes, count);

    slot_set_mode(SLOT_MODE_320X200X256);
    memset((void *) SLOT_VGA_BASE, SLOT_SENTINEL, (size_t) SLOT_SCREEN_BYTES);

    data_fdps_timer_tick_counter = 0;
    slot_saved_timer = _dos_getvect(SLOT_TIMER_VECTOR);
    _dos_setvect(SLOT_TIMER_VECTOR, slot_timer_isr);
    _dos_getdate(&screen_date_before);
    _dos_gettime(&screen_time_before);
    fdps_save_game_screen(screen_restore);
    _dos_getdate(&screen_date_after);
    _dos_gettime(&screen_time_after);
    _dos_setvect(SLOT_TIMER_VECTOR, slot_saved_timer);

    memmove(slot_screen, (void *) SLOT_VGA_BASE, (size_t) SLOT_SCREEN_BYTES);
    slot_set_mode(SLOT_MODE_TEXT);
}

/* The sprite cache the redraw left holding the party, given back. */
static void screen_drop_cache(int members)
{
    if (members > 0) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_buffer_used = 0;
}

static long screen_file_size(void)
{
    FILE *fp;
    long size;

    fp = fopen(SCREEN_SAVE_FILE, "rb");
    if (fp == NULL) {
        return -1L;
    }
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    return size;
}

/* FDE.SAV as the screen left it, decrypted in place.  The same routine that
   wrote it undoes it, which is the whole of the file's cipher (save.h). */
static int screen_reload(void)
{
    FILE *fp;
    size_t got;

    fp = fopen(SCREEN_SAVE_FILE, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(screen_image, 1, (size_t) SCREEN_IMAGE_BYTES, fp);
    fclose(fp);
    if (got != (size_t) SCREEN_IMAGE_BYTES) {
        return 0;
    }
    fdps_xor_crypt_buffer(screen_image, (unsigned int) SCREEN_IMAGE_BYTES);
    return 1;
}

static unsigned char *screen_slot(int slot)
{
    return screen_image + SCREEN_SLOTS_AT + (long) slot * SCREEN_SLOT_STRIDE;
}

static unsigned long screen_dword(unsigned char *at)
{
    return (unsigned long) at[0]
           | ((unsigned long) at[1] << 8)
           | ((unsigned long) at[2] << 16)
           | ((unsigned long) at[3] << 24);
}

/* The whole record, field by field, written from no save file at all.
 *
 * The 0xa00 bytes of live state have to arrive whole and at the record's own
 * base -- 0x312b for slot 0 -- with only the last 0x14 of them replaced: the
 * pattern is compared byte for byte up to 0x9eb, and the 0xaa the staging left
 * across 0x9ec..0x9ff has to be gone, which is what says the stamp landed
 * INSIDE the copied block and not after it.  Then every header field is read at
 * its own raw offset against the global it was staged in and each of the four
 * timestamp fields against the bracket described above, so a field taken out of
 * the wrong global or written at the wrong offset fails on its own.
 *
 * The two slots nobody picked have to read 0xff at their chapter byte, which
 * is only true if a missing file was stood up with the memset fill and that
 * fill was NOT put through the cipher; and the checksum the screen stored at
 * +0x59c7 has to be the sum of the plaintext, which is only true if it was
 * computed before the encryption and not after.
 */
static void screen_save_writes_the_record_it_staged(void)
{
    unsigned char keys[2];
    unsigned char *record;

    if (!build_files_present()) {
        return;
    }
    remove(SCREEN_SAVE_ASIDE);
    CHECK_EQ(rename(SCREEN_SAVE_FILE, SCREEN_SAVE_ASIDE), 0);

    screen_stage_restore_page();
    screen_stage(0);
    data_fdps_ui_saveload_is_load_mode = 1;

    keys[0] = SLOT_KEY_SPACE;
    keys[1] = SLOT_KEY_ESC;
    screen_run(keys, 2);
    screen_drop_cache(SCREEN_MEMBERS_A);

    CHECK_EQ(screen_file_size(), SCREEN_IMAGE_BYTES);
    CHECK_EQ(screen_reload(), 1);
    CHECK_EQ(data_fdps_ui_saveload_is_load_mode, 0);

    record = screen_slot(0);
    CHECK_EQ(memcmp(record, screen_live_state, (size_t) SCREEN_STAMP_AT), 0);
    CHECK_EQ(record[SCREEN_STAMP_AT] == 0xaa, 0);
    CHECK_EQ(record[SCREEN_LIVE_BYTES - 1] == 0xaa, 0);

    CHECK_EQ(screen_stamp_holds(screen_dword(record + SCREEN_AT_MONTH),
                                screen_date_before.month,
                                screen_date_after.month,
                                SCREEN_MONTH_LOW, SCREEN_MONTH_HIGH), 1);
    CHECK_EQ(screen_stamp_holds(screen_dword(record + SCREEN_AT_DAY),
                                screen_date_before.day, screen_date_after.day,
                                SCREEN_DAY_LOW, SCREEN_DAY_HIGH), 1);
    CHECK_EQ(screen_stamp_holds(screen_dword(record + SCREEN_AT_HOUR),
                                screen_time_before.hour,
                                screen_time_after.hour,
                                SCREEN_HOUR_LOW, SCREEN_HOUR_HIGH), 1);
    CHECK_EQ(screen_stamp_holds(screen_dword(record + SCREEN_AT_MINUTE),
                                screen_time_before.minute,
                                screen_time_after.minute,
                                SCREEN_MINUTE_LOW, SCREEN_MINUTE_HIGH), 1);
    CHECK_EQ(screen_dword(record + SCREEN_AT_LOTTERY), SCREEN_LOTTERY_A);

    CHECK_EQ(record[SCREEN_AT_CHAPTER], SCREEN_CHAPTER_A);
    CHECK_EQ(record[SCREEN_AT_MEMBERS], SCREEN_MEMBERS_A);
    CHECK_EQ(screen_dword(record + SCREEN_AT_GOLD), SCREEN_GOLD_A);
    CHECK_EQ(record[SCREEN_AT_TERRAIN], SCREEN_TERRAIN_A);
    CHECK_EQ(record[SCREEN_AT_BATTLE_ANIM], SCREEN_BATTLE_ANIM_A);
    CHECK_EQ(record[SCREEN_AT_BGM], SCREEN_BGM_A);
    CHECK_EQ(record[SCREEN_AT_SFX], SCREEN_SFX_A);

    CHECK_EQ(screen_slot(1)[SCREEN_AT_CHAPTER], SCREEN_UNWRITTEN);
    CHECK_EQ(screen_slot(2)[SCREEN_AT_CHAPTER], SCREEN_UNWRITTEN);

    CHECK_EQ(fdps_compute_save_checksum(screen_image,
                                        (unsigned int) SCREEN_IMAGE_BYTES),
             screen_dword(screen_image + SCREEN_CHECKSUM_AT));

    remove(SCREEN_SAVE_FILE);
    CHECK_EQ(rename(SCREEN_SAVE_ASIDE, SCREEN_SAVE_FILE), 0);
}

/* The second save reads the file the first one wrote.
 *
 * Two runs from no file at all: the first confirms slot 0, the second walks the
 * cursor one step right and confirms slot 1 with a different game state
 * staged.  Slot 0 has to come back holding exactly what the first run put
 * there -- which is only possible if the second run read FDE.SAV and decrypted
 * it before replacing one record -- and slot 1 has to hold the second run's
 * state at 0x312b + 0xa28, which is what pins the stride.  Slot 2 is still
 * 0xff from the first run's fill.
 *
 * It also pins the outer loop: the cursor is cleared once, before the loop, so
 * the second run starts on slot 0 again and the Right code is what moves it.
 */
static void screen_second_save_keeps_the_first_one(void)
{
    unsigned char keys[3];
    unsigned char *record;

    if (!build_files_present()) {
        return;
    }
    remove(SCREEN_SAVE_ASIDE);
    CHECK_EQ(rename(SCREEN_SAVE_FILE, SCREEN_SAVE_ASIDE), 0);

    screen_stage_restore_page();

    screen_stage(0);
    keys[0] = SLOT_KEY_SPACE;
    keys[1] = SLOT_KEY_ESC;
    screen_run(keys, 2);
    screen_drop_cache(SCREEN_MEMBERS_A);

    screen_stage(1);
    keys[0] = SLOT_KEY_RIGHT;
    keys[1] = SLOT_KEY_SPACE;
    keys[2] = SLOT_KEY_ESC;
    screen_run(keys, 3);
    screen_drop_cache(SCREEN_MEMBERS_B);

    CHECK_EQ(screen_reload(), 1);

    record = screen_slot(0);
    CHECK_EQ(record[SCREEN_AT_CHAPTER], SCREEN_CHAPTER_A);
    CHECK_EQ(record[SCREEN_AT_MEMBERS], SCREEN_MEMBERS_A);
    CHECK_EQ(screen_dword(record + SCREEN_AT_GOLD), SCREEN_GOLD_A);
    CHECK_EQ(record[SCREEN_AT_BGM], SCREEN_BGM_A);
    CHECK_EQ(record[0], screen_pattern_byte(0, 0));

    record = screen_slot(1);
    CHECK_EQ(memcmp(record, screen_live_state, (size_t) SCREEN_STAMP_AT), 0);
    CHECK_EQ(record[SCREEN_AT_CHAPTER], SCREEN_CHAPTER_B);
    CHECK_EQ(record[SCREEN_AT_MEMBERS], SCREEN_MEMBERS_B);
    CHECK_EQ(screen_dword(record + SCREEN_AT_GOLD), SCREEN_GOLD_B);
    CHECK_EQ(record[SCREEN_AT_BGM], SCREEN_BGM_B);

    CHECK_EQ(screen_slot(2)[SCREEN_AT_CHAPTER], SCREEN_UNWRITTEN);
    CHECK_EQ(fdps_compute_save_checksum(screen_image,
                                        (unsigned int) SCREEN_IMAGE_BYTES),
             screen_dword(screen_image + SCREEN_CHECKSUM_AT));

    remove(SCREEN_SAVE_FILE);
    CHECK_EQ(rename(SCREEN_SAVE_ASIDE, SCREEN_SAVE_FILE), 0);
}

/* Escape on the first pass writes nothing and hands the adapter back.
 *
 * The mosaic on the way out reveals `restore_page` over the whole 320x200
 * screen -- 80 block columns cover the columns exactly and the bands reach
 * every row (transit.h) -- so the adapter has to end up holding that page byte
 * for byte, with none of the sentinel left anywhere.  The save file must not
 * have been touched: its length and its stored checksum are still the shipped
 * file's, which is also what says no fopen("wb") ran.
 *
 * The mode flag is staged non-zero and has to come back 0, because that store
 * is the first thing the screen does and it is what lets an empty slot be
 * confirmed at all.
 *
 * The run has to be heap-neutral once the cache the redraw filled is given
 * back: the page is built inside the call and freed inside it.
 */
static void screen_cancel_writes_nothing_and_restores_the_page(void)
{
    unsigned char keys[1];
    long size_before;
    int blocks_before;

    if (!build_files_present()) {
        return;
    }

    screen_stage_restore_page();
    screen_stage(0);
    data_fdps_ui_saveload_is_load_mode = 1;
    size_before = screen_file_size();
    CHECK_EQ(screen_reload(), 1);
    CHECK_EQ(screen_dword(screen_image + SCREEN_CHECKSUM_AT),
             SAVE_STORED_CHECKSUM);

    blocks_before = slot_used_heap_blocks();
    keys[0] = SLOT_KEY_ESC;
    screen_run(keys, 1);
    screen_drop_cache(SCREEN_MEMBERS_A);

    CHECK_EQ(data_fdps_ui_saveload_is_load_mode, 0);
    CHECK_EQ(memcmp(slot_screen, screen_restore, (size_t) SLOT_SCREEN_BYTES),
             0);
    CHECK_EQ(screen_file_size(), size_before);
    CHECK_EQ(screen_reload(), 1);
    CHECK_EQ(screen_dword(screen_image + SCREEN_CHECKSUM_AT),
             SAVE_STORED_CHECKSUM);
    CHECK_EQ(slot_used_heap_blocks() - blocks_before, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);

    slot_unstage();
}

void run_save_tests(void)
{
    RUN_TEST(slot_escape_is_the_only_cancel);
    RUN_TEST(slot_save_screen_confirms_an_empty_slot);
    RUN_TEST(slot_load_screen_needs_an_occupied_slot);
    RUN_TEST(slot_up_and_left_step_back);
    RUN_TEST(slot_right_and_down_step_forward);
    RUN_TEST(slot_codes_from_seven_f_up_are_ignored);
    RUN_TEST(slot_cursor_lands_at_ten_and_twenty_five_plus_fifty_two);
    RUN_TEST(slot_cursor_frame_cycles_and_folds_three_to_one);
    RUN_TEST(screen_save_writes_the_record_it_staged);
    RUN_TEST(screen_second_save_keeps_the_first_one);
    RUN_TEST(screen_cancel_writes_nothing_and_restores_the_page);
}
