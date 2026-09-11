/* tests/btlmenu.c -- cover for src/btlmenu.c.
 *
 * Three subjects: fdps_battle_system_submenu at 00014ea0,
 * fdps_battle_search_cell_at_cursor at 000184f0 and fdps_battle_action_menu
 * at 00015d00.  The notes below belong to the first; the other two have their
 * own block in front of their cases.
 *
 * Expected values come from the assembly, never from the emitted C: the four
 * command icon ids of the template at 00014700 and the all-zero descriptor at
 * 00014710; the MOV dword ptr [EBP-0x24],0x1 at 00014ed8 that greys the LOAD
 * entry when access() answered non-zero and the MOV dword ptr [EBP-0x28],0x1
 * at 00014f2e that greys SAVE when a unit's flags byte has bit 0 clear and bit
 * 7 set; the MOV dword ptr [EBP-0xc],0x0 at 00014f37 that is INSIDE the loop
 * and so puts the cursor back on the first entry every time the menu reopens;
 * the CMP dword ptr [EBP-0x14],-0x1 at 00014f82 that turns a cancelled ring
 * into the -1 this function returns; the dispatch chain CMP [EBP-0xc] against
 * 3, 0, 1 and 2 at 00014f94, 00015064, 0001507b and 00015393; and the MOV byte
 * ptr [0x000643eb],0x1 with MOV [EBP-0x4],0x1 at 00015014 that is the whole of
 * the confirmed-quit arm.
 *
 * HOW A WHOLE MENU IS RUN FROM OUTSIDE.  This function is a modal driver: it
 * takes nothing, returns one number, and everything between is drawing and
 * keystrokes.  So every case below puts the adapter in the mode the game runs
 * it in, stages the four sheets and the text block the drawing reads through,
 * installs a timer interrupt that both advances the game's clock and plays the
 * case's keys into the scancode ring, calls, and asserts on the number that
 * came back and on the one global the function writes.
 *
 * WHY THE KEYS ARE FED FROM THE ISR AND WHY EVERY REAL KEY HAS A FILLER IN
 * FRONT OF IT.  fdps_prompt_two_choice empties the ring on entry and the save
 * and load arms end in fdps_flush_keyboard_queue, so a code staged before the
 * call, or pushed at the wrong moment, is thrown away rather than read.  The
 * ISR therefore pushes the next code only when the ring is EMPTY -- one code
 * per read, however long a frame takes -- and each script puts a code the
 * menus ignore (0x20 is none of the eight the cursor loop knows and none of
 * the six the prompt knows) in front of each key that follows a flush.  The
 * filler is then either eaten by the flush or read and ignored, and the real
 * key survives either way, which is what makes the scripts independent of how
 * many ticks a frame happens to take.
 *
 * PAST THE END OF A SCRIPT THE ISR PUSHES ESCAPE FOREVER, so a run that got
 * away from its script ends in a cancel and fails an assertion instead of
 * hanging the whole test image.
 *
 * WHAT IS NOT ASSERTED, AND WHY.
 *
 * The descriptor the ring is drawn with is not read back off the screen.  It
 * is consumed by the eight opening frames, the cursor loop's repaints and the
 * six closing frames, and every one of those is overwritten before this
 * function returns: CALL 0x0002beb0 at 00014f7d presents a whole fresh 312x192
 * view over the window on every pass.  What the greyed entries do IS still
 * observable, because a blocked direction leaves the cursor where it was
 * (menu.h) and the cursor decides which arm runs -- which is exactly what the
 * pair of save cases below reads it through.
 *
 * That the cursor goes back to the first entry when the menu reopens is not
 * asserted either, and it cannot be from out here: the cursor loop's arrow
 * keys name their slot absolutely rather than stepping (menu.c), so a Right on
 * the reopened menu lands on slot 2 whether the pass began on 0 or on the 3
 * the player had been standing on.  The only script that could tell them apart
 * is one that presses Enter with no arrow in front of it, and that selects the
 * objectives entry -- which no case here may run, for the reason below.
 *
 * The objectives entry is not chosen by any case.  Slot 0 runs
 * fdps_battle_show_win_fail_window, which reloads the nine data tables out of
 * MISC.VFS and draws through the number glyph sheet, so a case that selected
 * it would be exercising tests/btlend.c's subject and would leave those nine
 * globals reloaded for every test file after this one.
 *
 * NO CASE CONFIRMS THE SAVE, and that is the one arm of the four this file
 * cannot cover.  Confirming it writes FDE.SAV -- the shipped save
 * tests/savefile.c decrypts and checksums, staged next to this executable by
 * tests/gamefile.lst -- and a restore afterwards is not crash safe: the file
 * this function writes is exactly 0x59cb bytes long, so build_emit.py's
 * staging, which re-copies only when the size differs, would carry a damaged
 * one into every later run.  What the save arm builds is recorded as a
 * playtest contract in the emit issues instead.  The DECLINED save is run, and
 * the file's opening bytes are read before and after it to say that the
 * declining arm opens nothing.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "chapter.h"
#include "menu.h"
#include "msgwin.h"
#include "testharn.h"
#include "btlmenu.h"

/* The adapter and the two modes a case moves between. */
#define MENU_VGA_BASE 0x000a0000
#define MENU_SCREEN_PITCH 320
#define MENU_SCREEN_ROWS 200
#define MENU_SCREEN_BYTES (MENU_SCREEN_PITCH * MENU_SCREEN_ROWS)
#define MENU_MODE_TEXT 0x03
#define MENU_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the drawing spins on the tick counter. */
#define MENU_TIMER_VECTOR 8

/* The make codes the ring's cursor loop knows (src/menu.c) and the two the
   prompt answers on (src/msgwin.c).  0x20 is in neither set. */
#define KEY_ESC 0x01
#define KEY_ENTER 0x1c
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DOWN 0x50
#define KEY_IGNORED 0x20

/* What fdps_battle_system_submenu answers with. */
#define SUBMENU_CANCELLED (-1)
#define SUBMENU_DONE 0
#define SUBMENU_QUIT_CONFIRMED 1

/* The synthetic .CEL every sheet global is pointed at.  Its streams are read
   through two different paths and one 25 x 22 sheet satisfies both:
   fdps_cel_blit_sprite takes the size out of the header's i16 pair at +0x07
   and +0x09 (sprite.h), which is how the message panel and the prompt's cells
   are drawn, while fdps_render_ring_menu_frame passes the plate's 25 x 22
   itself and never looks at the header at all (menu.c).  Every stream is one
   transparent skip run per row -- an RLE command byte is the length minus one
   in its low six bits with the op in the top two, and 0xc0 is the skip op
   (rle.h) -- so no sprite writes a pixel and no case can be reading artwork
   this file invented.

   THE ROW HAS TO ACCOUNT FOR EXACTLY 25 PIXELS.  A row ends when the pixels it
   has covered reach the width EXACTLY, and nothing bounds the stream (rle.h),
   so a 24-pixel run against the ring's 25-pixel row never closes the row: the
   decoder reads on past the end of this array and treats whatever follows as
   commands, which is heap corruption and not a wrong picture.

   0x40 entries because the highest id the ring can form is the greyed-out
   bank's copy of icon 0x17, which is 0x17 + 0x24 = 0x3b. */
#define CEL_SPRITES 0x40
#define CEL_TABLE_AT 0x0f
#define CEL_SPRITE_W 25
#define CEL_SPRITE_H 22
#define CEL_SKIP_CMD (0xc0 | (CEL_SPRITE_W - 1))
#define CEL_STREAMS_AT (CEL_TABLE_AT + CEL_SPRITES * 4)
#define CEL_BYTES (CEL_STREAMS_AT + CEL_SPRITES * CEL_SPRITE_H)

/* The synthetic text block.  fdps_draw_text takes a table of signed 16-bit
   byte offsets measured from the block's own base and walks the stream it
   points at until the token -1 (text.h), so a table whose every entry names
   one lone terminator draws nothing at all and needs no font staged.  0x213
   entries covers every id either subject writes: 0x1f0 through 0x207 for the
   system submenu and 0x20a through 0x212 for the cell search. */
#define TEXT_ENTRIES 0x213
#define TEXT_TERMINATOR (-1)

/* How many codes a script can hold. */
#define SCRIPT_MAX 12

/* One unit record's flags byte at +5 (fdpstype.h): bit 0 retires the unit and
   bit 7 says it has already acted this turn. */
#define UNIT_FLAG_RETIRED 0x01
#define UNIT_FLAG_ACTED 0x80

/* Portrait id 0x80 is the record that has no map sprite: fdps_draw_map_unit
   returns on it before it reads anything else off the record (mapdraw.c), so
   units staged with it are walked by this function's availability loop and
   drawn by nothing. */
#define UNIT_PORTRAIT_NO_SPRITE 0x80

/* Where the ring is hung, the same world pixel tests/menu.c uses so the four
   buttons land inside the presented window. */
#define MENU_CURSOR_X 144
#define MENU_CURSOR_Y 120

/* The shipped save, and how much of its head is compared across a run. */
#define SAVE_NAME "FDE.SAV"
#define SAVE_HEAD_BYTES 16

/* The portrait sheet fdps_message_window_open reaches through
   fdps_load_and_draw_portrait.  A missing one is printf plus exit(1), so every
   case that opens the window stands down without it rather than taking the
   whole run with it. */
#define FACE_NAME "FACE.CEL"

static unsigned char menu_cel[CEL_BYTES];
static short menu_text[TEXT_ENTRIES + 1];
static struct fdps_unit_record menu_units[4];

static unsigned char menu_script[SCRIPT_MAX];
static int menu_script_len;
static int menu_script_next;
static void (__interrupt __far *menu_saved_timer)();

static unsigned char save_head_before[SAVE_HEAD_BYTES];
static unsigned char save_head_after[SAVE_HEAD_BYTES];

/* Advances the game's clock the way fdps_timer_tick_handler does, and plays
   the script into the scancode ring the way fdps_keyboard_isr does -- one code
   at a time, and only into an empty ring, so a code is pushed only after the
   one before it has been read or flushed.  Equal indices are the ring's only
   emptiness test (keybd.h). */
static void __interrupt __far menu_timer_isr(void)
{
    int slot;
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        if (menu_script_next < menu_script_len) {
            code = menu_script[menu_script_next];
            menu_script_next++;
        } else {
            code = KEY_ESC;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = code;
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(menu_saved_timer);
}

static void menu_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void menu_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void menu_build_cel(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(menu_cel, 0, (size_t) CEL_BYTES);
    menu_cel[0] = 'C';
    menu_cel[1] = 'E';
    menu_cel[2] = 'L';
    menu_u16(menu_cel, 0x03, 1);
    menu_u16(menu_cel, 0x05, 0);
    menu_u16(menu_cel, 0x07, CEL_SPRITE_W);
    menu_u16(menu_cel, 0x09, CEL_SPRITE_H);
    menu_u16(menu_cel, 0x0b, CEL_SPRITES);
    menu_u16(menu_cel, 0x0d, 2);

    for (sprite_id = 0; sprite_id < CEL_SPRITES; sprite_id++) {
        stream_at = CEL_STREAMS_AT + sprite_id * CEL_SPRITE_H;
        menu_u32(menu_cel, CEL_TABLE_AT + sprite_id * 4,
                 (unsigned long) stream_at);
        for (row = 0; row < CEL_SPRITE_H; row++) {
            menu_cel[stream_at + row] = (unsigned char) CEL_SKIP_CMD;
        }
    }
}

static void menu_build_text(void)
{
    int entry;

    for (entry = 0; entry < TEXT_ENTRIES; entry++) {
        menu_text[entry] = (short) (TEXT_ENTRIES * 2);
    }
    menu_text[TEXT_ENTRIES] = TEXT_TERMINATOR;
}

/* Nothing on the map: no scene layers, no cursor overlay, and units that draw
   nothing.  unit_count is what the availability walk reads, so it is set by
   the caller and not here. */
static void menu_stage(void)
{
    menu_build_cel();
    menu_build_text();

    data_fdps_command_sprite_sheet_ptr = menu_cel;
    data_fdps_shadow_sprite_sheet_ptr = menu_cel;
    data_fdps_message_window_sheet_ptr = menu_cel;
    data_fdps_all_game_text_ptr = (unsigned char *) menu_text;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_array_ptr = (unsigned char *) menu_units;
    data_fdps_map_unit_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = MENU_CURSOR_X;
    data_fdps_map_cursor_world_y = MENU_CURSOR_Y;
    data_fdps_village_mode_flag = 0;
    data_fdps_shared_quit_game_requested = 0;

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* Back to the state a freshly started program has these in.  It is not
   tidiness: the loaders and the shutdown path free four of the pointers staged
   above unguarded, so a case that walked away leaving one of them naming a
   static in this file would hand a later test a free() of storage that never
   came from the heap.  The portrait buffer is the one block that really is on
   the heap -- fdps_message_window_open leaves it loaded on purpose (msgwin.h)
   -- so it is released rather than dropped. */
static void menu_unstage(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_message_window_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_shared_quit_game_requested = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* One unit that is still in the battle.  `acted` sets bit 7, which is the half
   of the save test that greys the entry out; bit 0 stays clear, which is the
   other half. */
static void menu_stage_unit(int slot, int acted)
{
    memset(&menu_units[slot], 0, sizeof(struct fdps_unit_record));
    menu_units[slot].portrait_id = UNIT_PORTRAIT_NO_SPRITE;
    menu_units[slot].flags = (unsigned char) (acted ? UNIT_FLAG_ACTED : 0);
}

static void menu_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void menu_load_script(unsigned char *codes, int count)
{
    int index;

    for (index = 0; index < count; index++) {
        menu_script[index] = codes[index];
    }
    menu_script_len = count;
    menu_script_next = 0;
}

/* One whole run of the submenu, with the adapter in the mode the game draws it
   in and the timer interrupt both pacing the frames and playing the keys. */
static int menu_run(unsigned char *codes, int count)
{
    int answer;

    menu_load_script(codes, count);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_view_frame_last_tick = 0;

    menu_set_mode(MENU_MODE_320X200X256);
    memset((void *) MENU_VGA_BASE, 0, (size_t) MENU_SCREEN_BYTES);

    menu_saved_timer = _dos_getvect(MENU_TIMER_VECTOR);
    _dos_setvect(MENU_TIMER_VECTOR, menu_timer_isr);
    answer = fdps_battle_system_submenu();
    _dos_setvect(MENU_TIMER_VECTOR, menu_saved_timer);

    menu_set_mode(MENU_MODE_TEXT);
    return answer;
}

/* Is the named file staged next to this executable? */
static int menu_file_present(char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int menu_read_save_head(unsigned char *into)
{
    FILE *fp;
    size_t got;

    fp = fopen(SAVE_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(into, 1, (size_t) SAVE_HEAD_BYTES, fp);
    fclose(fp);
    return got == (size_t) SAVE_HEAD_BYTES;
}

/* ---------------------------------------------------------------------- */

/* A cancel on the very first pass of the ring is the whole call: CMP dword ptr
   [EBP-0x14],-0x1 / JNZ at 00014f82 stores 0xffffffff into the result slot and
   jumps to the epilogue, so no entry runs, the message window is never opened
   and the quit flag is not touched.

   This is also the one case that needs nothing staged but the ring, which is
   why it is the case that says the harness itself can drive the menu. */
static void a_cancelled_ring_answers_minus_one(void)
{
    unsigned char codes[2];

    menu_stage();
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 2), SUBMENU_CANCELLED);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* The availability walk runs over the unit array whether or not it changes
   anything, and a battle whose units have all still to act leaves the whole
   descriptor zero.  Three units are staged so that the walk is more than one
   pass, and the answer is the same cancel as above: what this case says is
   that the walk over records does not disturb the cancel path. */
static void the_availability_walk_leaves_a_cancel_alone(void)
{
    unsigned char codes[2];

    menu_stage();
    menu_stage_unit(0, 0);
    menu_stage_unit(1, 0);
    menu_stage_unit(2, 0);
    data_fdps_map_unit_count = 3;

    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 2), SUBMENU_CANCELLED);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* Slot 3 confirmed and the prompt answered on its left cell: MOV byte ptr
   [0x000643eb],0x1 at 00015014 and the 1 stored into the result slot at
   0001501b.  Enter is the prompt's affirmative (msgwin.h), and 1 is the only
   answer of the three this function ever gives that is not shared with another
   arm. */
static void a_confirmed_quit_raises_the_flag_and_answers_one(void)
{
    unsigned char codes[4];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    menu_stage();
    codes[0] = KEY_DOWN;
    codes[1] = KEY_ENTER;
    codes[2] = KEY_IGNORED;
    codes[3] = KEY_ENTER;

    CHECK_EQ(menu_run(codes, 4), SUBMENU_QUIT_CONFIRMED);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 1);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* Declining the quit does not end the call: the arm falls through to the JMP
   at 0001505f, which lands on the JMP at 00015460 back to the top of the loop
   at 00014f37 -- so the menu is opened again and the second cancel is what
   ends it.  The flag stays clear, which is what separates this from the case
   above.

   Escape is the prompt's cancel and answers -1, which is not the 0 the
   affirmative arm tests for, so it takes the declining arm exactly as the
   right-hand cell would (msgwin.h). */
static void a_declined_quit_reopens_the_menu(void)
{
    unsigned char codes[6];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    menu_stage();
    codes[0] = KEY_DOWN;
    codes[1] = KEY_ENTER;
    codes[2] = KEY_IGNORED;
    codes[3] = KEY_ESC;
    codes[4] = KEY_IGNORED;
    codes[5] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 6), SUBMENU_CANCELLED);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* A second menu really is a second menu: the reopened one takes its own
   arrows.  The script declines the quit and then walks Down and Right on the
   menu that comes back, confirms the load entry there and declines that
   prompt, so the answer is the load arm's 0 rather than the cancel a run that
   had fallen out of the loop would give. */
static void the_reopened_menu_takes_its_own_arrows(void)
{
    unsigned char codes[9];

    if (!menu_file_present(FACE_NAME) || !menu_file_present(SAVE_NAME)) {
        return;
    }

    menu_stage();
    codes[0] = KEY_DOWN;
    codes[1] = KEY_ENTER;
    codes[2] = KEY_IGNORED;
    codes[3] = KEY_ESC;
    codes[4] = KEY_DOWN;
    codes[5] = KEY_RIGHT;
    codes[6] = KEY_ENTER;
    codes[7] = KEY_IGNORED;
    codes[8] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 9), SUBMENU_DONE);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* The load entry with FDE.SAV staged next to the executable: access() answers
   0, the descriptor's slot 2 is left selectable, and the arrow reaches it.
   Declining the prompt takes the arm at 0001541a and returns 0 without ever
   calling fdps_load_savegame -- which is what the unit count says, because a
   load installs the shipped save's own twenty-three units over it.

   THE SCRIPT WALKS DOWN BEFORE IT WALKS RIGHT so that a wrongly greyed load
   entry cannot be mistaken for a working one: a refused Right would leave the
   cursor on slot 3 and the run would end in the quit prompt and, after the
   trailing escapes, in -1. */
static void the_load_entry_is_selectable_when_the_save_file_is_there(void)
{
    unsigned char codes[5];

    if (!menu_file_present(FACE_NAME) || !menu_file_present(SAVE_NAME)) {
        return;
    }

    menu_stage();
    menu_stage_unit(0, 0);
    data_fdps_map_unit_count = 1;

    codes[0] = KEY_DOWN;
    codes[1] = KEY_RIGHT;
    codes[2] = KEY_ENTER;
    codes[3] = KEY_IGNORED;
    codes[4] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 5), SUBMENU_DONE);
    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* The save entry with no unit that has acted: slot 1 stays selectable, Left
   reaches it, and declining the prompt takes the arm at 0001534a and returns
   0.  FDE.SAV's opening bytes are compared across the run because that arm
   must open nothing at all -- the malloc, both fopens and the fwrite are all
   inside the confirmed arm at 000150c7 onward. */
static void the_save_entry_is_selectable_when_no_unit_has_acted(void)
{
    unsigned char codes[5];

    if (!menu_file_present(FACE_NAME) || !menu_read_save_head(save_head_before)) {
        return;
    }

    menu_stage();
    menu_stage_unit(0, 0);
    menu_stage_unit(1, 0);
    data_fdps_map_unit_count = 2;

    codes[0] = KEY_DOWN;
    codes[1] = KEY_LEFT;
    codes[2] = KEY_ENTER;
    codes[3] = KEY_IGNORED;
    codes[4] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 5), SUBMENU_DONE);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(menu_read_save_head(save_head_after), 1);
    CHECK_EQ(memcmp(save_head_before, save_head_after,
                    (size_t) SAVE_HEAD_BYTES), 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* The same script against a battle where one unit has already acted -- flags
   bit 0 clear and bit 7 set, the two halves of the test at 00014f10 and
   00014f1e -- so slot 1 is greyed out at 00014f2e and the Left is refused.
   The cursor stays on the 3 the Down put it on, Enter opens the QUIT prompt
   instead, the escape declines it, the menu reopens and the trailing escapes
   cancel it: the answer is -1 where the case above answered 0.

   That difference is the only way the descriptor is visible from outside this
   function, and it pins both halves of the condition at once -- a test that
   read the flag byte as "acted OR retired", or that dropped the retired half,
   would grey the entry here as well and both cases would answer -1. */
static void the_save_entry_is_greyed_when_a_unit_has_acted(void)
{
    unsigned char codes[7];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    menu_stage();
    menu_stage_unit(0, 0);
    menu_stage_unit(1, 1);
    data_fdps_map_unit_count = 2;

    codes[0] = KEY_DOWN;
    codes[1] = KEY_LEFT;
    codes[2] = KEY_ENTER;
    codes[3] = KEY_IGNORED;
    codes[4] = KEY_ESC;
    codes[5] = KEY_IGNORED;
    codes[6] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 7), SUBMENU_CANCELLED);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* A retired unit that has also acted does not grey the entry: the first half
   of the test is (flags & 1) == 0 and it short-circuits the second, so a
   record carrying both bits is skipped entirely.  Same script and same
   staging as the pair above, and the answer has to be the selectable one. */
static void a_retired_unit_does_not_grey_the_save_entry(void)
{
    unsigned char codes[5];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    menu_stage();
    menu_stage_unit(0, 1);
    menu_units[0].flags = (unsigned char) (UNIT_FLAG_ACTED | UNIT_FLAG_RETIRED);
    data_fdps_map_unit_count = 1;

    codes[0] = KEY_DOWN;
    codes[1] = KEY_LEFT;
    codes[2] = KEY_ENTER;
    codes[3] = KEY_IGNORED;
    codes[4] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 5), SUBMENU_DONE);
    CHECK_EQ(data_fdps_shared_quit_game_requested, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}

/* A unit count of zero walks nothing, which is the state every menu opened
   before a battle has begun is in.  The save entry is selectable and the
   script reaches it, so this says the loop's bound is the count and not a
   fixed four the way the descriptor's own length is. */
static void a_zero_unit_count_walks_nothing(void)
{
    unsigned char codes[5];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    menu_stage();
    menu_stage_unit(0, 1);
    data_fdps_map_unit_count = 0;

    codes[0] = KEY_DOWN;
    codes[1] = KEY_LEFT;
    codes[2] = KEY_ENTER;
    codes[3] = KEY_IGNORED;
    codes[4] = KEY_ESC;

    CHECK_EQ(menu_run(codes, 5), SUBMENU_DONE);
    CHECK_EQ(_heapchk(), _HEAPOK);
    menu_unstage();
}


/* ---- fdps_battle_search_cell_at_cursor @ 000184f0 -------------------------
 *
 * The second subject of this file, and it is driven the same way as the first:
 * the adapter in mode 13h, the timer interrupt above pacing the frames and
 * playing the make codes, and one call per case.  It returns nothing, so every
 * assertion is on state that outlives the call -- the searched-flag table, the
 * party gold, the three dialog argument globals, the unit's own inventory
 * bytes, the cell record inside the map block, and the two map layers that
 * fdps_map_apply_triggered_cell_changes edits.
 *
 * THE MAP IS A FIXTURE AND NOT A FILE.  Four tiles by three, staged into the
 * four layer globals fdps_map_load_tile_info reads (maptile.h): a terrain layer
 * whose tile id picks an attribute row, an attribute table whose four rows hold
 * the four values of the 0x60 class field one each, a movement grid that is
 * read and never used here, and an event-code layer that names one cell.  The
 * searchable-cell records go into a block of the same shape as the resident
 * MAP%02d.DAT, which is what data_fdps_tile_event_data_table_ptr points at.
 * None of it stands in for a shipped file: it is the local fixture table the
 * emit pipeline asks for, and every number in it is chosen by the case.
 *
 * Expected values come from the assembly at 000184f0: the two rejections CMP
 * dword ptr [EBP-0xc],0x0 at 00018550 and CMP ...,0x60 at 00018556 for which
 * classes are searchable; CMP byte ptr [EAX+0x640d8],0x0 at 00018561 for the
 * already-searched gate; CMP dword ptr [EBP-0x1c],0x0 at 000185da for the
 * prompt's yes; MOVSX EAX,word ptr [EAX+0x54] at 00018600 for the payload
 * being read signed and MOV AL,byte ptr [EDX+0x53] at 00018617 for the kind
 * being read unsigned; ADD EAX,0xc9 at 0001862a and 0001875c for the item-name
 * bias; CMP EAX,-0x1 at 00018683 for a full bag; MOV byte ptr [EAX+0x640d8],
 * 0x1 at 0001868b and 000188aa for the two arms that consume the cell; MOV
 * EAX,[0x00064038] / ADD [0x000643a4],EAX at 0001889c for the gold coming out
 * of the global; and LEA EDX,[EDX*0x4 + 0x0] on [EBP-0x14] at 000188cd for the
 * handler table being indexed by the payload word.
 *
 * WHAT IS NOT ASSERTED, AND WHY.
 *
 * The accepted item-for-item trade is not run.  Reaching it means answering yes
 * to the bag-full prompt, which opens the whole unit status window over the
 * real Status.cel inside MISC.VFS and draws the panel through the number sheet,
 * the gauge sheet, the sprite cache, the font and the item table
 * (unititem.h, statunit.h) -- a second copy of tests/unititem.c's window
 * fixture, in a test image that has a documented headroom problem
 * (rebuild_info/emit_pipeline.md).  The DECLINED trade is run instead, and it
 * is what pins the bag-full branch and the cell being left alone by it.  What
 * the accepted trade writes -- the discarded id into the cell record, the
 * second dialog argument, and the searched flag deliberately NOT being set --
 * is recorded as a playtest contract in the emit issues.
 *
 * Which of the nine text ids each arm draws is not asserted either.  Every one
 * of them goes through fdps_draw_text into the adapter and the frames that
 * follow overwrite it; the fixture text block draws no pixels at all by
 * construction, which is what keeps these cases from depending on a font.
 */
/* The fixture map.  Small on purpose: fdps_map_apply_triggered_cell_changes
   walks every cell of it and calls the tile reader on each. */
#define SEARCH_MAP_W 4
#define SEARCH_MAP_H 3
#define SEARCH_MAP_CELLS (SEARCH_MAP_W * SEARCH_MAP_H)

/* Where in that map the cursor stands, and the cell index it names.  The
   function divides the cursor's world pixels by the 24-pixel tile size. */
#define SEARCH_TILE_X 1
#define SEARCH_TILE_Y 1
#define SEARCH_TILE_SIZE 24
#define SEARCH_CELL_INDEX (SEARCH_TILE_Y * SEARCH_MAP_W + SEARCH_TILE_X)

/* The event code that cell carries.  It indexes both the searched-flag table
   and the three-byte cell record, and it is deliberately not the cell's own
   position in the map so that a body that confused the two would be caught. */
#define SEARCH_CELL_CODE 9

/* The terrain layer: signed 16-bit width at +7, height at +9, and 16-bit tile
   ids at +0xb in row-major order (maptile.h). */
#define SEARCH_TERRAIN_W_AT 7
#define SEARCH_TERRAIN_H_AT 9
#define SEARCH_TERRAIN_IDS_AT 0x0b
#define SEARCH_TERRAIN_BYTES 64

/* The tileset attribute table: 4-byte rows from +0x11, indexed by tile id.
   One row per value of the 0x60 class field, so a case picks a class by
   putting that row's tile id in the cell. */
#define SEARCH_ATTR_ROWS_AT 0x11
#define SEARCH_ATTR_BYTES 64
#define SEARCH_TILE_PLAIN 0
#define SEARCH_TILE_CHEST 1
#define SEARCH_TILE_BURIED 2
#define SEARCH_TILE_RESERVED 3
#define SEARCH_ATTR_CLASS_NONE 0x00
#define SEARCH_ATTR_CLASS_CHEST 0x20
#define SEARCH_ATTR_CLASS_BURIED 0x40
#define SEARCH_ATTR_CLASS_RESERVED 0x60

/* The movement grid: a four-byte header then one two-byte cell each.  The
   reader latches its marker byte and nothing here looks at it. */
#define SEARCH_GRID_BYTES 64

/* The event-code layer is written through struct fdps_map_cell_code_layer, so
   only its total size is named here. */
#define SEARCH_EVENT_LAYER_BYTES 64

/* The resident map block: the three-byte searchable-cell records begin at
   0x53, so a block that covers every code the 32-entry flag table can hold
   needs 0x53 + 3 * 32 bytes. */
#define SEARCH_MAP_BLOCK_BYTES 256
#define SEARCH_RECORD_AT 0x53
#define SEARCH_RECORD_BYTES 3
#define SEARCH_RECORD_KIND 0
#define SEARCH_RECORD_PAYLOAD 1

/* The two kinds of cell record that are named by a number of their own; the
   scripted class is every kind from 2 up and is named with the case below. */
#define SEARCH_KIND_ITEM 0
#define SEARCH_KIND_MONEY 1

/* The item the item cases put in the cell, and where its name sits in the
   text block once the 0xc9 bias is added. */
#define SEARCH_ITEM_ID 0x2a
#define SEARCH_ITEM_NAME_TEXT (SEARCH_ITEM_ID + 0xc9)

/* The gold cases: the party's balance before the call, the amount in the cell,
   and the payload whose top bit is set -- read through MOVSX it is -1 and not
   65535, which is the whole point of that case. */
#define SEARCH_GOLD_BEFORE 100
#define SEARCH_GOLD_IN_CELL 250
#define SEARCH_GOLD_NEGATIVE_WORD 0xffff
#define SEARCH_GOLD_NEGATIVE_VALUE (-1)

/* The scripted case.  Two handlers are installed, one at the slot the kind
   byte would name and one at the slot the payload word names, so a body that
   indexed the table with the wrong one would run the wrong handler rather than
   dereferencing a null slot. */
#define SEARCH_SCRIPT_KIND 2
#define SEARCH_SCRIPT_PAYLOAD 7
#define SEARCH_SCRIPT_UNIT 2

/* One unit's inventory: eight two-byte entries, flag byte then id byte, and
   the 0x80 that marks an entry empty (unititem.h). */
#define SEARCH_INVENTORY_ENTRIES 8
#define SEARCH_ENTRY_EMPTY 0x80

/* A value no arm of the function writes, so a global still holding it after a
   call is one the call never touched. */
#define SEARCH_UNTOUCHED (-999)

/* The unit every case but the scripted one searches with. */
#define SEARCH_UNIT 0

static unsigned char search_terrain[SEARCH_TERRAIN_BYTES];
static unsigned char search_attr[SEARCH_ATTR_BYTES];
static unsigned char search_grid[SEARCH_GRID_BYTES];
static unsigned char search_event_layer[SEARCH_EVENT_LAYER_BYTES];
static unsigned char search_map_block[SEARCH_MAP_BLOCK_BYTES];
static struct fdps_vfs_image_header search_sfx_pack;

/* What the two installed handlers saw. */
static int search_kind_handler_calls;
static int search_payload_handler_calls;
static int search_kind_handler_unit;
static int search_payload_handler_unit;

static void search_kind_slot_handler(int unit_index)
{
    search_kind_handler_calls++;
    search_kind_handler_unit = unit_index;
}

static void search_payload_slot_handler(int unit_index)
{
    search_payload_handler_calls++;
    search_payload_handler_unit = unit_index;
}

/* Points the cell the cursor stands on at one attribute row and gives it one
   event code; every other cell of the map is the plain row and code 0, so
   nothing else in the repaint walk can match. */
static void search_build_map(int cell_tile_id)
{
    struct fdps_map_cell_code_layer *layer;
    int cell;

    memset(search_terrain, 0, sizeof search_terrain);
    *(short *) (search_terrain + SEARCH_TERRAIN_W_AT) = (short) SEARCH_MAP_W;
    *(short *) (search_terrain + SEARCH_TERRAIN_H_AT) = (short) SEARCH_MAP_H;
    for (cell = 0; cell < SEARCH_MAP_CELLS; cell++) {
        *(short *) (search_terrain + SEARCH_TERRAIN_IDS_AT + cell * 2) =
            (short) SEARCH_TILE_PLAIN;
    }
    *(short *) (search_terrain + SEARCH_TERRAIN_IDS_AT
                + SEARCH_CELL_INDEX * 2) = (short) cell_tile_id;

    memset(search_attr, 0, sizeof search_attr);
    ((struct fdps_tile_attr_entry *) (search_attr + SEARCH_ATTR_ROWS_AT))
        [SEARCH_TILE_PLAIN].flags = (unsigned char) SEARCH_ATTR_CLASS_NONE;
    ((struct fdps_tile_attr_entry *) (search_attr + SEARCH_ATTR_ROWS_AT))
        [SEARCH_TILE_CHEST].flags = (unsigned char) SEARCH_ATTR_CLASS_CHEST;
    ((struct fdps_tile_attr_entry *) (search_attr + SEARCH_ATTR_ROWS_AT))
        [SEARCH_TILE_BURIED].flags = (unsigned char) SEARCH_ATTR_CLASS_BURIED;
    ((struct fdps_tile_attr_entry *) (search_attr + SEARCH_ATTR_ROWS_AT))
        [SEARCH_TILE_RESERVED].flags =
            (unsigned char) SEARCH_ATTR_CLASS_RESERVED;

    memset(search_grid, 0, sizeof search_grid);

    memset(search_event_layer, 0, sizeof search_event_layer);
    layer = (struct fdps_map_cell_code_layer *) search_event_layer;
    layer->width = (short) SEARCH_MAP_W;
    layer->height = (short) SEARCH_MAP_H;
    layer->cells[SEARCH_CELL_INDEX] = (unsigned char) SEARCH_CELL_CODE;
}

/* The three bytes of one searchable-cell record. */
static void search_set_record(int kind, int payload_word)
{
    unsigned char *record;

    record = search_map_block + SEARCH_RECORD_AT
             + SEARCH_CELL_CODE * SEARCH_RECORD_BYTES;
    record[SEARCH_RECORD_KIND] = (unsigned char) kind;
    *(short *) (record + SEARCH_RECORD_PAYLOAD) = (short) payload_word;
}

static int search_record_payload(void)
{
    return (int) *(short *) (search_map_block + SEARCH_RECORD_AT
                             + SEARCH_CELL_CODE * SEARCH_RECORD_BYTES
                             + SEARCH_RECORD_PAYLOAD);
}

static int search_cell_tile_id(void)
{
    return (int) *(short *) (search_terrain + SEARCH_TERRAIN_IDS_AT
                             + SEARCH_CELL_INDEX * 2);
}

static int search_cell_event_code(void)
{
    return (int) ((struct fdps_map_cell_code_layer *)
                  search_event_layer)->cells[SEARCH_CELL_INDEX];
}

/* Every entry of one unit's bag either empty or occupied.  An occupied entry
   is what makes fdps_unit_add_item answer -1 (unititem.h). */
static void search_fill_inventory(int unit_slot, int occupied)
{
    int entry;

    for (entry = 0; entry < SEARCH_INVENTORY_ENTRIES; entry++) {
        menu_units[unit_slot].inventory_slots[entry * 2] =
            (unsigned char) (occupied ? 0 : SEARCH_ENTRY_EMPTY);
        menu_units[unit_slot].inventory_slots[entry * 2 + 1] = 0;
    }
}

/* Everything one call reads that this file can set: the ring menu staging
   above for the panel and the prompt, the four map layers, the map block, an
   effect pack whose entry count is zero so the search sound is looked up and
   missed without touching the heap (audio.h), and sentinels in the three
   dialog argument globals. */
static void search_stage(int cell_tile_id, int kind, int payload_word)
{
    int slot;

    menu_stage();
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    menu_stage_unit(SEARCH_UNIT, 0);
    menu_stage_unit(SEARCH_SCRIPT_UNIT, 0);
    search_fill_inventory(SEARCH_UNIT, 0);
    search_fill_inventory(SEARCH_SCRIPT_UNIT, 0);

    data_fdps_map_cursor_world_x = SEARCH_TILE_X * SEARCH_TILE_SIZE;
    data_fdps_map_cursor_world_y = SEARCH_TILE_Y * SEARCH_TILE_SIZE;

    search_build_map(cell_tile_id);
    memset(search_map_block, 0, sizeof search_map_block);
    search_set_record(kind, payload_word);

    data_fdps_scene_layer_tile_map_ptrs[0] = search_terrain;
    data_fdps_scene_layer_tile_attr_ptr[0] = search_attr;
    data_fdps_battle_move_grid_ptr = search_grid;
    data_fdps_map_cell_event_code_layer_ptr = search_event_layer;
    data_fdps_tile_event_data_table_ptr = search_map_block;

    memset(&search_sfx_pack, 0, sizeof search_sfx_pack);
    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) &search_sfx_pack;

    for (slot = 0; slot < 32; slot++) {
        data_fdps_map_cell_event_triggered_flags[slot] = 0;
    }

    data_fdps_shared_party_total_gold = SEARCH_GOLD_BEFORE;
    data_fdps_dialog_last_action_text_id_param = SEARCH_UNTOUCHED;
    data_fdps_dialog_subst_text_id_2 = SEARCH_UNTOUCHED;
    data_fdps_dialog_last_action_value_param = SEARCH_UNTOUCHED;

    search_kind_handler_calls = 0;
    search_payload_handler_calls = 0;
    search_kind_handler_unit = SEARCH_UNTOUCHED;
    search_payload_handler_unit = SEARCH_UNTOUCHED;
}

/* Back to the state a freshly started program has these in.  The four layer
   pointers and the map block are all released by the chapter loader and by the
   shutdown path without a null test, so a case that walked away leaving one of
   them naming a static here would hand a later test a free() of storage that
   never came from the heap (rebuild_info/emit_pipeline.md). */
static void search_unstage(void)
{
    int slot;

    menu_unstage();
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;

    data_fdps_chapter_event_handler_table[SEARCH_SCRIPT_KIND] = NULL;
    data_fdps_chapter_event_handler_table[SEARCH_SCRIPT_PAYLOAD] = NULL;

    for (slot = 0; slot < 32; slot++) {
        data_fdps_map_cell_event_triggered_flags[slot] = 0;
    }
    data_fdps_shared_party_total_gold = 0;
    data_fdps_dialog_last_action_text_id_param = 0;
    data_fdps_dialog_subst_text_id_2 = 0;
    data_fdps_dialog_last_action_value_param = 0;
}

/* One whole search, with the adapter in the mode the panels are drawn in and
   the timer interrupt both pacing the frames and playing the keys. */
static void search_run(int unit_index, unsigned char *codes, int count)
{
    menu_load_script(codes, count);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_view_frame_last_tick = 0;

    menu_set_mode(MENU_MODE_320X200X256);
    memset((void *) MENU_VGA_BASE, 0, (size_t) MENU_SCREEN_BYTES);

    menu_saved_timer = _dos_getvect(MENU_TIMER_VECTOR);
    _dos_setvect(MENU_TIMER_VECTOR, menu_timer_isr);
    fdps_battle_search_cell_at_cursor(unit_index);
    _dos_setvect(MENU_TIMER_VECTOR, menu_saved_timer);

    menu_set_mode(MENU_MODE_TEXT);
}

/* ---------------------------------------------------------------------- */

/* A cell whose class field is 0 is not searchable: CMP dword ptr [EBP-0xc],0x0
   / JZ at 00018550 leaves through the epilogue before the unit record is even
   fetched.  Nothing is drawn, so this case needs no portrait sheet, and the
   portrait buffer staying null is what says no window was opened. */
static void a_plain_cell_is_not_searched(void)
{
    unsigned char codes[2];

    search_stage(SEARCH_TILE_PLAIN, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SEARCH_GOLD_BEFORE);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SEARCH_UNTOUCHED);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_PLAIN);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* The fourth value of the class field is refused as well, and it is refused by
   its own test: CMP dword ptr [EBP-0xc],0x60 / JZ at 00018556.  A body that
   read the field as the bit test (attr & 0x20) would accept this cell, which
   is exactly what the repaint pass in maptile.c does and what this function
   must not do. */
static void a_reserved_class_cell_is_not_searched(void)
{
    unsigned char codes[2];

    search_stage(SEARCH_TILE_RESERVED, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SEARCH_GOLD_BEFORE);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_RESERVED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A searchable cell whose event code is already flagged is refused too, by the
   third half of the same guard: CMP byte ptr [EAX+0x640d8],0x0 / JZ at
   00018561.  The flag is indexed by the cell's event code and not by its
   position, so the case flags exactly that code and leaves the rest zero. */
static void an_already_searched_cell_is_refused(void)
{
    unsigned char codes[2];

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE] = 1;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, SEARCH_GOLD_BEFORE);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SEARCH_UNTOUCHED);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* Declining the prompt ends the call: JNZ at 000185de skips the whole of the
   body and takes the arm at 000188e3, which draws one line and closes.  The
   cell keeps everything it had -- the flag stays clear and the tile is not
   turned over -- and the money global is never written, which is what says the
   record was not even read.

   Escape is the prompt's cancel and answers -1, which is not the 0 the
   affirmative arm tests for (msgwin.h). */
static void a_declined_search_leaves_the_cell_alone(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SEARCH_GOLD_BEFORE);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SEARCH_UNTOUCHED);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST);
    CHECK_EQ(search_cell_event_code(), SEARCH_CELL_CODE);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A kind 0 cell accepted into a bag that has room: the item id goes into the
   first empty entry with its flag byte zeroed (unititem.h), the item's name id
   is published as the payload plus 0xc9, the cell is flagged searched and
   fdps_map_apply_triggered_cell_changes bumps the cell's tile id by one and
   clears its event code (maptile.h).  Those last two are the only way from out
   here to say the repaint really ran. */
static void an_item_cell_fills_the_bag_and_consumes_the_cell(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_ITEM, SEARCH_ITEM_ID);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ((int) menu_units[SEARCH_UNIT].inventory_slots[1],
             SEARCH_ITEM_ID);
    CHECK_EQ((int) menu_units[SEARCH_UNIT].inventory_slots[0], 0);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             SEARCH_ITEM_NAME_TEXT);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             1);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST + 1);
    CHECK_EQ(search_cell_event_code(), 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* The buried-treasure class is searchable and takes the same arms: the cell is
   accepted at 00018556 and its money is taken.  Its tile is NOT turned over,
   because the repaint pass takes only the classes 0x20 and 0x60 and leaves
   0x40 alone (maptile.c) -- the flag is still set, so the cell is spent, and
   the picture simply does not change. */
static void a_buried_treasure_cell_is_searchable(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_BURIED, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_shared_party_total_gold,
             SEARCH_GOLD_BEFORE + SEARCH_GOLD_IN_CELL);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             1);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_BURIED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A kind 1 cell on the chest class: the payload lands in the numeric dialog
   argument, the party's gold goes up by it, the cell is flagged and the tile
   is turned over.  The gold is added back out of the global and not out of the
   local it was set from (0001889c), so the two have to agree afterwards. */
static void a_money_cell_pays_the_party_and_consumes_the_cell(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_MONEY, SEARCH_GOLD_IN_CELL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_dialog_last_action_value_param, SEARCH_GOLD_IN_CELL);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             SEARCH_GOLD_BEFORE + SEARCH_GOLD_IN_CELL);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             1);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST + 1);
    CHECK_EQ(search_cell_event_code(), 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A money cell holding zero is still a cell: it draws its own line rather than
   the other one (0x211 against 0x212 at 00018847), and it is spent all the
   same -- the flag is set and the tile turned over on both sides of that
   branch.  The party is no richer, which is the only part of the pair that is
   visible from out here. */
static void a_money_cell_holding_zero_is_still_spent(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_MONEY, 0);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_dialog_last_action_value_param, 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SEARCH_GOLD_BEFORE);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             1);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST + 1);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* The payload word is read signed, MOVSX word ptr [EAX+0x54] at 00018600, so a
   record holding 0xffff pays the party minus one gold and not plus 65535.  The
   record is written as the raw word and the expectation is the sign-extended
   value, which is the whole of what this case separates. */
static void the_payload_word_is_read_signed(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_MONEY,
                 SEARCH_GOLD_NEGATIVE_WORD);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_UNIT, codes, 2);

    CHECK_EQ(data_fdps_dialog_last_action_value_param,
             SEARCH_GOLD_NEGATIVE_VALUE);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             SEARCH_GOLD_BEFORE + SEARCH_GOLD_NEGATIVE_VALUE);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A kind 0 cell against a bag with all eight entries occupied: fdps_unit_add_
   item answers -1 (CMP EAX,-0x1 at 00018683) and the bag-full prompt goes up.
   Declining it takes the arm at 000187d4, and NOTHING is spent -- the cell
   keeps its item, the flag stays clear, the tile is not turned over and the
   bag is untouched.

   The script answers the first prompt yes and the second no, and each key has
   a filler in front of it because fdps_prompt_two_choice empties the ring on
   entry. */
static void a_full_bag_that_declines_the_trade_spends_nothing(void)
{
    unsigned char codes[4];
    int entry;

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_KIND_ITEM, SEARCH_ITEM_ID);
    search_fill_inventory(SEARCH_UNIT, 1);
    for (entry = 0; entry < SEARCH_INVENTORY_ENTRIES; entry++) {
        menu_units[SEARCH_UNIT].inventory_slots[entry * 2 + 1] =
            (unsigned char) (entry + 1);
    }

    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;
    codes[2] = KEY_IGNORED;
    codes[3] = KEY_ESC;

    search_run(SEARCH_UNIT, codes, 4);

    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             SEARCH_ITEM_NAME_TEXT);
    CHECK_EQ(search_record_payload(), SEARCH_ITEM_ID);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             0);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST);
    CHECK_EQ(search_cell_event_code(), SEARCH_CELL_CODE);
    CHECK_EQ((int) menu_units[SEARCH_UNIT].inventory_slots[1], 1);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, SEARCH_UNTOUCHED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* A kind of 2 or more is the scripted class, and the handler it runs is named
   by the PAYLOAD word and not by the kind byte -- LEA EDX,[EDX*0x4 + 0x0] on
   [EBP-0x14] at 000188cd.  Both slots carry a handler here, so the wrong index
   would run the other one rather than fault, and the unit index the handler is
   called with is the one this function was given (PUSH EAX from [EBP+0x14] at
   000188d7).

   Nothing else happens on this path: the flag is not set and the tile is not
   turned over, because both are left to the handler. */
static void a_scripted_cell_dispatches_on_the_payload(void)
{
    unsigned char codes[2];

    if (!menu_file_present(FACE_NAME)) {
        return;
    }

    search_stage(SEARCH_TILE_CHEST, SEARCH_SCRIPT_KIND, SEARCH_SCRIPT_PAYLOAD);
    data_fdps_chapter_event_handler_table[SEARCH_SCRIPT_KIND] =
        search_kind_slot_handler;
    data_fdps_chapter_event_handler_table[SEARCH_SCRIPT_PAYLOAD] =
        search_payload_slot_handler;

    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    search_run(SEARCH_SCRIPT_UNIT, codes, 2);

    CHECK_EQ(search_payload_handler_calls, 1);
    CHECK_EQ(search_kind_handler_calls, 0);
    CHECK_EQ(search_payload_handler_unit, SEARCH_SCRIPT_UNIT);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[SEARCH_CELL_CODE],
             0);
    CHECK_EQ(search_cell_tile_id(), SEARCH_TILE_CHEST);
    CHECK_EQ(search_cell_event_code(), SEARCH_CELL_CODE);
    CHECK_EQ(_heapchk(), _HEAPOK);
    search_unstage();
}

/* ---------------------------------------------------------------------- *
 * 00015d00 fdps_battle_action_menu
 *
 * The third subject of this file, and the one the other two are opened from.
 * It is a modal driver like the system submenu above, so the cases below are
 * run through the same fixture: the adapter in mode 13h, the four sheets and
 * the text block staged, and the timer interrupt both advancing the clock and
 * playing one scancode at a time into the ring.  What is new is that this
 * function writes an array the caller owns, so most of what a case asserts is
 * read back out of that array after the call rather than off the screen.
 *
 * Expected values come from the assembly at 00015d00: the MOV dword ptr
 * [EAX],0x0 at 00015d16 and MOV dword ptr [0x00069cec],0x0 at 00015d1c that
 * open every pass; the CMP dword ptr [EBP-0x38],-0x1 at 00015d37 that greys
 * the attack entry when no weapon is equipped and the TEST EAX,EAX at 00015dc3
 * that greys it when the collector counted nothing; the CALL 0x00010b20 at
 * 00015dd0, which is inside the equipped-weapon arm and not after it; the TEST
 * EAX,EAX at 00015de1 and the MOV dword ptr [EAX+0x8],0x1 at 00015de8 for the
 * item entry; the same pair at 00015dfd / 00015e04 and the independent CMP
 * byte ptr [EAX+0x27],0x0 at 00015e1d for the two tests that grey the spell
 * entry; the four-slot scan at 00015e40 with its MOV dword ptr [EBP-0x60],
 * 0xffffffff at 00015e69; the CMP dword ptr [EBP-0x3c],-0x1 at 00015eba that
 * returns the accumulator; the dispatch chain CMP [EBP-0x10] against 0, 1 and
 * 2 at 00015ecb, 0001601d and 0001605c with no fourth test; and the CMP dword
 * ptr [EBP+0x20],0x0 at 0001609b that is the whole of what the fourth argument
 * does.
 *
 * WHAT IS NOT ASSERTED, AND WHY.
 *
 * Three of the four arms are not driven: attack, spell and item all hand the
 * player a whole second modal machine -- the target cursor and the attack
 * exchange, the spell window, the item ring -- and each of those is the
 * subject of its own test file with its own fixture.  What this file covers of
 * them is the half that belongs to this function: whether the entry is offered
 * at all, which is what the probe cases below read out of the descriptor.
 *
 * So the item command's 2 is not covered either.  It is the one value that
 * survives a pass -- it lifts the accumulator at [EBP-0x8] so that a later
 * cancel answers 1 instead of -1 -- and reaching it means driving the item
 * ring's hand-over arm from in here.  It is recorded as a playtest contract in
 * the emit issues instead.
 *
 * The ring's own picture is not read back off the screen, for the reason the
 * system submenu's block above gives: every frame of it is overwritten by the
 * CALL 0x0002beb0 at 00015eb5 before this function returns.
 */

/* The acting unit, the enemy the two attack-probe cases place, and the tile
   the cursor stands on -- the same (1, 1) the search cases use, so the cell
   under it is the plain one that the fall-through arm's search refuses. */
#define ACTION_UNIT 0
#define ACTION_ENEMY 1

/* The unit array index the acting unit's record is at, as tiles. */
#define ACTION_UNIT_TILE_X SEARCH_TILE_X
#define ACTION_UNIT_TILE_Y SEARCH_TILE_Y

/* The sides the two units are on.  The acting unit is on the player's side so
   that the enemy sweep, which keeps side 0 alone, does not count it as a
   target of its own attack. */
#define ACTION_SIDE_ENEMY 0
#define ACTION_SIDE_PLAYER 2

/* The movement grid the attack probe sweeps: the same extents as the fixture
   map, a four-byte header of two 16-bit tile counts and then one two-byte cell
   per tile with the marker second (aitarget.h, movegrid.h). */
#define ACTION_GRID_W SEARCH_MAP_W
#define ACTION_GRID_H SEARCH_MAP_H
#define ACTION_GRID_UNREACHED 0xff

/* A marker value the game never leaves behind, so a grid still holding it
   after a call is one that fdps_map_grid_reset did not run over. */
#define ACTION_GRID_SENTINEL 0x33

/* The equipped weapon.  Only three bytes of an ITEM.DAT record matter here:
   the type at +0 has to be a weapon type for fdps_unit_find_equipped_slot to
   accept the slot (unititem.h), and the two reach bytes at +0x0b and +0x0c are
   what this function copies into the collector's min_dist and range_code.
   0x11 is a straight-line cross with arms of one tile and a range_code of 0x10
   or more ignores min_dist entirely (aitarget.h), which is what keeps this
   fixture off the flood fill and off the class table. */
#define ACTION_ITEM_STRIDE 0x17
#define ACTION_ITEM_TYPE_AT 0x00
#define ACTION_ITEM_RANGE_MIN_AT 0x0b
#define ACTION_ITEM_RANGE_MAX_AT 0x0c
#define ACTION_WEAPON_ID 1
#define ACTION_WEAPON_TYPE 1
#define ACTION_WEAPON_RANGE_MIN 0
#define ACTION_WEAPON_RANGE_CODE 0x11
#define ACTION_ITEM_TABLE_BYTES (ACTION_ITEM_STRIDE * 4)

/* One inventory entry: flag byte then id byte, 0x80 empty and 0x40 equipped
   (unititem.h).  An entry that is neither is an ordinary carried item, which
   is what keeps the bag non-empty without equipping anything. */
#define ACTION_ENTRY_EQUIPPED 0x40
#define ACTION_ENTRY_CARRIED 0x00

/* The spell bitmap byte that makes fdps_unit_collect_known_spells answer 1:
   any one bit set is one known spell (unitstat.h). */
#define ACTION_ONE_SPELL 0x01

/* status_timers[5], record offset 0x27 -- the 封魔咒術 timer the spell entry's
   second test reads -- and a number of turns for it to be running. */
#define ACTION_SEAL_TIMER_SLOT 5
#define ACTION_SEAL_TURNS 3

/* The unit the rest cases heal: below its maximum and free of both timers, so
   fdps_unit_rest accepts it and gives back a fifth of the maximum
   (unitatk.h). */
#define ACTION_HP_BEFORE 10
#define ACTION_HP_MAX 20
#define ACTION_HP_AFTER_REST (ACTION_HP_BEFORE + ACTION_HP_MAX / 5)

/* Where the view window is put for the rest cases.  fdps_blit_unit_sprite
   draws the rest flash only when the sprite's page row is strictly inside the
   page -- sprite_y = pos_y * 0x18 - origin_y + 0x12 and the test is
   sprite_y > 0 (sprite.c) -- so an origin that puts that row on exactly 0
   leaves the flash undrawn and the case needs no sprite cache staged.  Nothing
   else in these cases reads the origin: the map has no scene layers and both
   units draw nothing. */
#define ACTION_REST_VIEW_ORIGIN_Y (ACTION_UNIT_TILE_Y * SEARCH_TILE_SIZE + 0x12)

/* What the fourth argument means to the caller and to the branch it drives. */
#define ACTION_UNIT_STAYED_PUT 0
#define ACTION_UNIT_MOVED 1

/* What this function answers with. */
#define ACTION_MENU_CANCELLED (-1)
#define ACTION_MENU_DONE 1

/* The descriptor value that greys an entry out.  The ring reads any non-zero
   as greyed (menu.h) and 1 is what this function writes -- MOV dword ptr
   [EAX],0x1 at 00015d40 and the three stores like it. */
#define ACTION_ENTRY_GREYED 1

/* The descriptor slots, in the ring's order. */
#define ACTION_SLOT_ATTACK 0
#define ACTION_SLOT_SPELL 1
#define ACTION_SLOT_ITEM 2
#define ACTION_SLOT_FALLTHROUGH 3

/* A pending experience haul no arm of this function leaves behind, so a global
   still holding it would be one the top of the pass never cleared. */
#define ACTION_XP_SENTINEL 777

static int action_icons[4];
static int action_disabled[4];
static unsigned char action_item_table[ACTION_ITEM_TABLE_BYTES];

/* The grid the attack probe sweeps, laid over the map the search fixture
   builds.  Every marker starts at the 0xff the game hands this function
   (aitarget.h) unless a case asks for the sentinel. */
static void action_build_grid(int marker)
{
    int cell;

    memset(search_grid, 0, sizeof search_grid);
    *(short *) search_grid = (short) ACTION_GRID_W;
    *(short *) (search_grid + 2) = (short) ACTION_GRID_H;
    for (cell = 0; cell < ACTION_GRID_W * ACTION_GRID_H; cell++) {
        search_grid[4 + cell * 2 + 1] = (unsigned char) marker;
    }
}

static int action_grid_marker(int tile_x, int tile_y)
{
    return (int) search_grid[4 + (tile_y * ACTION_GRID_W + tile_x) * 2 + 1];
}

/* Everything one call reads that is not already staged by the search fixture:
   the acting unit on the cursor's own tile and on the player's side, an empty
   descriptor, an item table with one weapon in it, and a grid in the state the
   caller hands over. */
static void action_stage(void)
{
    int slot;

    search_stage(SEARCH_TILE_PLAIN, SEARCH_KIND_MONEY, 0);

    menu_units[ACTION_UNIT].pos_x = (unsigned char) ACTION_UNIT_TILE_X;
    menu_units[ACTION_UNIT].pos_y = (unsigned char) ACTION_UNIT_TILE_Y;
    menu_units[ACTION_UNIT].side = (unsigned char) ACTION_SIDE_PLAYER;
    menu_units[ACTION_UNIT].hp_current = (short) ACTION_HP_BEFORE;
    menu_units[ACTION_UNIT].hp_max = (short) ACTION_HP_MAX;

    for (slot = 0; slot < 4; slot++) {
        action_icons[slot] = 0;
        action_disabled[slot] = 0;
    }

    memset(action_item_table, 0, sizeof action_item_table);
    action_item_table[ACTION_WEAPON_ID * ACTION_ITEM_STRIDE
                      + ACTION_ITEM_TYPE_AT] = (unsigned char) ACTION_WEAPON_TYPE;
    action_item_table[ACTION_WEAPON_ID * ACTION_ITEM_STRIDE
                      + ACTION_ITEM_RANGE_MIN_AT] =
        (unsigned char) ACTION_WEAPON_RANGE_MIN;
    action_item_table[ACTION_WEAPON_ID * ACTION_ITEM_STRIDE
                      + ACTION_ITEM_RANGE_MAX_AT] =
        (unsigned char) ACTION_WEAPON_RANGE_CODE;
    data_fdps_item_effect_table_ptr = action_item_table;

    action_build_grid(ACTION_GRID_UNREACHED);
    data_fdps_battle_pending_xp_credit = ACTION_XP_SENTINEL;
}

static void action_unstage(void)
{
    search_unstage();
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_battle_pending_xp_credit = 0;
    data_fdps_battle_view_window_origin_y = 0;
}

/* Puts one weapon in the acting unit's hands. */
static void action_equip_weapon(void)
{
    menu_units[ACTION_UNIT].inventory_slots[0] =
        (unsigned char) ACTION_ENTRY_EQUIPPED;
    menu_units[ACTION_UNIT].inventory_slots[1] =
        (unsigned char) ACTION_WEAPON_ID;
}

/* Puts one enemy on the map, on the tile named, still in the battle. */
static void action_place_enemy(int tile_x, int tile_y)
{
    menu_stage_unit(ACTION_ENEMY, 0);
    menu_units[ACTION_ENEMY].pos_x = (unsigned char) tile_x;
    menu_units[ACTION_ENEMY].pos_y = (unsigned char) tile_y;
    menu_units[ACTION_ENEMY].side = (unsigned char) ACTION_SIDE_ENEMY;
    data_fdps_map_unit_count = ACTION_ENEMY + 1;
}

/* One whole run of the action menu. */
static int action_run(int unit_index, int unit_has_moved, unsigned char *codes,
                      int count)
{
    int answer;

    menu_load_script(codes, count);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_view_frame_last_tick = 0;

    menu_set_mode(MENU_MODE_320X200X256);
    memset((void *) MENU_VGA_BASE, 0, (size_t) MENU_SCREEN_BYTES);

    menu_saved_timer = _dos_getvect(MENU_TIMER_VECTOR);
    _dos_setvect(MENU_TIMER_VECTOR, menu_timer_isr);
    answer = fdps_battle_action_menu(unit_index, action_icons, action_disabled,
                                     unit_has_moved);
    _dos_setvect(MENU_TIMER_VECTOR, menu_saved_timer);

    menu_set_mode(MENU_MODE_TEXT);
    return answer;
}

/* ---------------------------------------------------------------------- */

/* A unit with nothing: no weapon equipped, an empty bag and no spell known.
   All three probes fire, the fall-through entry is the only one left, and the
   cancel walks straight back out with the -1 the accumulator was initialised
   to at 00015d0c.  The turn is not spent -- no arm ran, so nothing ORed 0x80
   into the flags byte -- and the experience global is the 0 the top of the
   pass wrote over the sentinel.

   The grid is the other half of this case.  fdps_map_grid_reset is INSIDE the
   equipped-weapon arm, so a unit with no weapon leaves the markers exactly as
   it found them; a body that ran the reset after the arm rather than inside it
   would have put every one of them back to 0xff. */
static void a_unit_with_nothing_greys_three_entries_and_cancels(void)
{
    unsigned char codes[2];

    action_stage();
    action_build_grid(ACTION_GRID_SENTINEL);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ATTACK], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_SPELL], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ITEM], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_FALLTHROUGH], 0);
    CHECK_EQ(data_fdps_battle_pending_xp_credit, 0);
    CHECK_EQ((int) (menu_units[ACTION_UNIT].flags & UNIT_FLAG_ACTED), 0);
    CHECK_EQ(action_grid_marker(ACTION_UNIT_TILE_X, ACTION_UNIT_TILE_Y),
             ACTION_GRID_SENTINEL);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* One bit in the spell bitmap is one known spell, so the count is not zero and
   the entry is left selectable.  The bag is still empty, which is the control:
   the two probes are independent and only the one whose subject is missing
   greys its entry. */
static void a_known_spell_keeps_the_spell_entry(void)
{
    unsigned char codes[2];

    action_stage();
    menu_units[ACTION_UNIT].spells_known_bitmap[0] =
        (unsigned char) ACTION_ONE_SPELL;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_SPELL], 0);
    CHECK_EQ(action_disabled[ACTION_SLOT_ITEM], ACTION_ENTRY_GREYED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* The same unit with its 封魔咒術 timer running.  The second test at 00015e1d
   is a plain zero test on record byte 0x27 and it greys the entry the first
   test had just left alone, which is the whole reason there are two of them. */
static void a_running_seal_timer_greys_the_spell_entry(void)
{
    unsigned char codes[2];

    action_stage();
    menu_units[ACTION_UNIT].spells_known_bitmap[0] =
        (unsigned char) ACTION_ONE_SPELL;
    menu_units[ACTION_UNIT].status_timers[ACTION_SEAL_TIMER_SLOT] =
        (unsigned char) ACTION_SEAL_TURNS;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_SPELL], ACTION_ENTRY_GREYED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* One carried item is a bag that is not empty.  The entry it leaves alone is
   the item entry and not the attack entry: the item is not equipped, so the
   weapon probe still answers -1. */
static void a_carried_item_keeps_the_item_entry(void)
{
    unsigned char codes[2];

    action_stage();
    menu_units[ACTION_UNIT].inventory_slots[0] =
        (unsigned char) ACTION_ENTRY_CARRIED;
    menu_units[ACTION_UNIT].inventory_slots[1] =
        (unsigned char) ACTION_WEAPON_ID;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ITEM], 0);
    CHECK_EQ(action_disabled[ACTION_SLOT_ATTACK], ACTION_ENTRY_GREYED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* An equipped weapon and an enemy one tile away along the cursor's own row.
   Both halves of the attack probe pass -- a slot came back from the equipment
   walk and the collector counted one unit -- so the entry is left selectable,
   which is the one path on which the MOV dword ptr [EAX],0x0 at 00015d16
   survives to the end of the pass.

   The grid is asserted too, from the other side of the case above: the arm
   that ran the collector runs fdps_map_grid_reset after it, so the marking the
   sweep left behind is back at the 0xff sentinel by the time this returns. */
static void an_enemy_in_reach_keeps_the_attack_entry(void)
{
    unsigned char codes[2];

    action_stage();
    action_equip_weapon();
    action_place_enemy(ACTION_UNIT_TILE_X + 1, ACTION_UNIT_TILE_Y);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ATTACK], 0);
    CHECK_EQ(action_grid_marker(ACTION_UNIT_TILE_X, ACTION_UNIT_TILE_Y),
             ACTION_GRID_UNREACHED);
    CHECK_EQ(action_grid_marker(ACTION_UNIT_TILE_X + 1, ACTION_UNIT_TILE_Y),
             ACTION_GRID_UNREACHED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* The same weapon with the enemy two tiles off the cross the 0x11 reach marks.
   The equipment walk still answers a slot, so this case separates the two
   halves of the probe: it is the collector's zero that greys the entry. */
static void an_enemy_out_of_reach_greys_the_attack_entry(void)
{
    unsigned char codes[2];

    action_stage();
    action_equip_weapon();
    action_place_enemy(ACTION_UNIT_TILE_X + 2, ACTION_UNIT_TILE_Y - 1);
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ATTACK], ACTION_ENTRY_GREYED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* A descriptor that arrives with everything greyed out is not rebuilt.  The
   unit staged here knows a spell and carries an item, so both of those probes
   would leave their entries selectable if the pass cleared them first -- and
   they stay greyed, because the only slot written to zero at the top of a pass
   is the attack slot and the other three are only ever set.

   The attack slot ends greyed as well, by its own probe: this unit has no
   weapon.  What the case says about slot 0 is therefore only that clearing it
   does not put back what the probe refuses. */
static void the_greyed_entries_are_never_put_back(void)
{
    unsigned char codes[2];

    action_stage();
    menu_units[ACTION_UNIT].spells_known_bitmap[0] =
        (unsigned char) ACTION_ONE_SPELL;
    menu_units[ACTION_UNIT].inventory_slots[0] =
        (unsigned char) ACTION_ENTRY_CARRIED;
    action_disabled[ACTION_SLOT_ATTACK] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_SPELL] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_ITEM] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_FALLTHROUGH] = ACTION_ENTRY_GREYED;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ESC;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_CANCELLED);
    CHECK_EQ(action_disabled[ACTION_SLOT_SPELL], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ITEM], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_FALLTHROUGH], ACTION_ENTRY_GREYED);
    CHECK_EQ(action_disabled[ACTION_SLOT_ATTACK], ACTION_ENTRY_GREYED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* Every entry greyed out, including the fall-through one, and the player
   confirms anyway.  The scan answers -1 and the ring's cursor loop confirms on
   whatever it is holding, so the dispatch is entered with -1 -- which is not
   0, not 1 and not 2, and so runs the last arm.  That arm ends the call: the
   turn is spent and the answer is 1.

   This is what says the last arm is the ELSE of three equality tests and not a
   fourth test against 3. */
static void a_confirm_with_every_entry_greyed_runs_the_last_arm(void)
{
    unsigned char codes[2];

    action_stage();
    action_disabled[ACTION_SLOT_ATTACK] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_SPELL] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_ITEM] = ACTION_ENTRY_GREYED;
    action_disabled[ACTION_SLOT_FALLTHROUGH] = ACTION_ENTRY_GREYED;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_DONE);
    CHECK_EQ((int) (menu_units[ACTION_UNIT].flags & UNIT_FLAG_ACTED),
             UNIT_FLAG_ACTED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* The fall-through arm on a unit that walked before opening the menu.  The
   fourth argument is non-zero, so the rest is skipped and the unit's hit
   points are exactly what they were; the search and the turn still happen. */
static void a_unit_that_moved_does_not_rest(void)
{
    unsigned char codes[2];

    action_stage();
    data_fdps_battle_view_window_origin_y = ACTION_REST_VIEW_ORIGIN_Y;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_MOVED, codes, 2),
             ACTION_MENU_DONE);
    CHECK_EQ((int) menu_units[ACTION_UNIT].hp_current, ACTION_HP_BEFORE);
    CHECK_EQ((int) (menu_units[ACTION_UNIT].flags & UNIT_FLAG_ACTED),
             UNIT_FLAG_ACTED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

/* The same arm on a unit that stayed where it was.  The fourth argument is
   zero, fdps_unit_rest runs, and a fifth of the maximum comes back -- 20 / 5
   on the fixture's unit, a signed divide that truncates (unitatk.h).  The
   hit-point figure is the whole of what separates this case from the one
   above. */
static void a_unit_that_stayed_put_rests(void)
{
    unsigned char codes[2];

    action_stage();
    data_fdps_battle_view_window_origin_y = ACTION_REST_VIEW_ORIGIN_Y;
    codes[0] = KEY_IGNORED;
    codes[1] = KEY_ENTER;

    CHECK_EQ(action_run(ACTION_UNIT, ACTION_UNIT_STAYED_PUT, codes, 2),
             ACTION_MENU_DONE);
    CHECK_EQ((int) menu_units[ACTION_UNIT].hp_current, ACTION_HP_AFTER_REST);
    CHECK_EQ((int) (menu_units[ACTION_UNIT].flags & UNIT_FLAG_ACTED),
             UNIT_FLAG_ACTED);
    CHECK_EQ(_heapchk(), _HEAPOK);
    action_unstage();
}

void run_btlmenu_tests(void)
{
    RUN_TEST(a_cancelled_ring_answers_minus_one);
    RUN_TEST(the_availability_walk_leaves_a_cancel_alone);
    RUN_TEST(a_confirmed_quit_raises_the_flag_and_answers_one);
    RUN_TEST(a_declined_quit_reopens_the_menu);
    RUN_TEST(the_reopened_menu_takes_its_own_arrows);
    RUN_TEST(the_load_entry_is_selectable_when_the_save_file_is_there);
    RUN_TEST(the_save_entry_is_selectable_when_no_unit_has_acted);
    RUN_TEST(the_save_entry_is_greyed_when_a_unit_has_acted);
    RUN_TEST(a_retired_unit_does_not_grey_the_save_entry);
    RUN_TEST(a_zero_unit_count_walks_nothing);
    RUN_TEST(a_plain_cell_is_not_searched);
    RUN_TEST(a_reserved_class_cell_is_not_searched);
    RUN_TEST(an_already_searched_cell_is_refused);
    RUN_TEST(a_declined_search_leaves_the_cell_alone);
    RUN_TEST(an_item_cell_fills_the_bag_and_consumes_the_cell);
    RUN_TEST(a_buried_treasure_cell_is_searchable);
    RUN_TEST(a_money_cell_pays_the_party_and_consumes_the_cell);
    RUN_TEST(a_money_cell_holding_zero_is_still_spent);
    RUN_TEST(the_payload_word_is_read_signed);
    RUN_TEST(a_full_bag_that_declines_the_trade_spends_nothing);
    RUN_TEST(a_scripted_cell_dispatches_on_the_payload);
    RUN_TEST(a_unit_with_nothing_greys_three_entries_and_cancels);
    RUN_TEST(a_known_spell_keeps_the_spell_entry);
    RUN_TEST(a_running_seal_timer_greys_the_spell_entry);
    RUN_TEST(a_carried_item_keeps_the_item_entry);
    RUN_TEST(an_enemy_in_reach_keeps_the_attack_entry);
    RUN_TEST(an_enemy_out_of_reach_greys_the_attack_entry);
    RUN_TEST(the_greyed_entries_are_never_put_back);
    RUN_TEST(a_confirm_with_every_entry_greyed_runs_the_last_arm);
    RUN_TEST(a_unit_that_moved_does_not_rest);
    RUN_TEST(a_unit_that_stayed_put_rests);
}
