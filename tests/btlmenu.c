/* tests/btlmenu.c -- cover for src/btlmenu.c.
 *
 * One subject: fdps_battle_system_submenu at 00014ea0.  The cell search and
 * the action menu, which this file used to cover as well, moved to
 * tests/btlact.c with their subject, and took a copy of the fixture below
 * with them.
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
   entries covers every id this subject writes, 0x1f0 through 0x207, and is
   the same table tests/btlact.c stages. */
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

/* The effect pack the ring's own cue is looked up in.  fdps_menu_animate_open
   opens with fdps_play_sfx (menu.c), which reads
   data_fdps_audio_basewav_sfx_bank_buf_ptr and walks the entry table it names
   -- so the pointer has to name something, and an image whose entry count is
   zero makes the lookup miss without touching the name and without allocating,
   which is what the original does on a machine whose pack does not hold the
   member (audio.h).  It is the same empty container tests/menu.c and
   tests/statwin.c stage for the same call.

   A NULL POINTER HERE IS NOT A SILENT MISS.  The lookup copies the header from
   wherever the pointer points before it tests anything (vfs.c), so a null one
   takes its entry count out of low memory and walks that many fabricated
   entries, which is a hang and not a missed sound. */
static struct fdps_vfs_image_header menu_sfx_pack;

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
    memset(&menu_sfx_pack, 0, sizeof menu_sfx_pack);

    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) &menu_sfx_pack;
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
   tidiness: the loaders and the shutdown path free five of the pointers staged
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
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
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
}
