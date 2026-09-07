/* tests/vilmenu.c -- cover for src/vilmenu.c.
 *
 * Expected values come from the assembly at 000336d0 -- the cancel arm's two
 * codes 0x01 and 0x53 at 000336fa and 00033700; the confirm arm's 0x1c and
 * 0x39 at 00033712 and 00033718 behind the chapter/slot gate CMP dword ptr
 * [0x00069cf4],0x17 / JL and CMP dword ptr [0x000601bc],0x3 / JZ at 0003371e;
 * the four guarded arrow arms at 0003373c (0x4d, DEC EAX / CMP / JG against
 * the member count), 00033782 (0x4b, CMP ...,0x0 / JG), 000337c0 (0x48,
 * CMP ...,0x2 / JG) and 000337fc (0x50, SUB EAX,0x3 / CMP / JG); the window
 * steps ADD [0x000601b8],0x3 behind a compare of top + 6 against the cursor
 * and SUB [0x000601b8],0x3 behind a compare of the cursor against the top;
 * and the epilogue CMP dword ptr [EBP-0x10],0x0 / JLE at 00033b56 that turns
 * anything but a confirmation into -1.  None of it is read off the emitted C.
 *
 * HOW A MODAL LOOP IS RUN FROM OUTSIDE.  fdps_village_select_member takes
 * nothing, answers one number, and everything between is drawing and
 * keystrokes.  So every case stages the sheets and tables the frame is drawn
 * through, installs a timer interrupt that both advances the game's clock and
 * plays the case's keys, calls, and asserts on the number that came back and
 * on the two globals the grid leaves behind.  The scaffolding is the same one
 * tests/shop.c uses on the shop's picker, for the same reasons.
 *
 * WHY THE KEYS ARE PLAYED INTO THE SCANCODE LATCH AND NOT THE RING.  The grid
 * reads through fdps_read_scancode_auto_repeat, which answers from
 * data_fdps_input_last_scancode alone and throws the ring away on every poll
 * (keybd.h).  The ISR therefore writes the latch, and only when the filter has
 * already picked up what is there -- data_fdps_input_key_repeat_prev_scancode
 * equal to the latch is exactly that -- so one code is presented per poll
 * however many ticks a frame happens to take.
 *
 * WHY TWO EQUAL KEYS IN A ROW NEED A FILLER BETWEEN THEM.  The filter reports
 * a code because it CHANGED; an unchanged latch is a held key and is silenced
 * until the repeat delay elapses.  VIL_KEY_NONE is the filter's own 0xff
 * no-key answer, which the grid's chain ignores, so a script puts one between
 * two identical presses and the second arrives as a change.
 *
 * PAST THE END OF A SCRIPT THE ISR PLAYS ESCAPE FOREVER, so a run that got
 * away from its script cancels and fails an assertion instead of hanging the
 * whole test image.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Nothing reads the frame back.  Every sprite
 * staged below is a skip-only RLE stream (rle.h) that steps its destination
 * and writes no pixel, which is deliberate twice over: the drawing is not this
 * function's behaviour, and the down arrow's fixed 25x22 block at page offset
 * 0x4e96 runs off the end of the 0x5ca0 page the pass allocated (vilmenu.c),
 * so a sprite that really painted would corrupt this test image's heap rather
 * than draw a wrong picture.  The ghosted-icon cases still go down the mode-9
 * arm of the draw and so still exercise the branch and its descriptor; what
 * they assert is that the answer is unaffected, which is the observable half.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palcycle.h"
#include "statunit.h"
#include "vilmenu.h"

/* The party this file offers the grid.  Eight members is longer than the six
   visible cells, which is what makes the window steps and the scroll arrows
   reachable, and it is not a multiple of three, which is what makes the down
   guard's "a whole row still ahead" observable.

   THE CHARACTER IDS ARE CHOSEN TO SEPARATE THE TWO CHAPTER-24 RULES.  Roster
   slot 3 is the slot the confirm is refused on and it deliberately does NOT
   carry character id 1; roster slot 5 does carry it and is deliberately not
   slot 3.  A build that folded the two tests into one would refuse slot 5 or
   accept slot 3, and both cases below would catch it. */
#define VIL_MEMBER_COUNT 8
#define VIL_LOCKED_SLOT 3
#define VIL_GHOSTED_SLOT 5
#define VIL_GHOSTED_CHAR_ID 1

static struct fdps_unit_record vil_roster[VIL_MEMBER_COUNT];

static unsigned char vil_char_ids[VIL_MEMBER_COUNT] = {
    0, 2, 3, 4, 5, VIL_GHOSTED_CHAR_ID, 6, 7
};

/* The two chapter indices the gate is tested either side of: 0x16 is the last
   one that accepts slot 3 and draws nobody ghosted, 0x17 the first that
   refuses it (CMP dword ptr [0x00069cf4],0x17 / JL at 0003371e). */
#define VIL_CHAPTER_BEFORE_LOCK 0x16
#define VIL_CHAPTER_AT_LOCK 0x17

/* The make codes the grid's chain knows, and the filter's no-key answer. */
#define VIL_KEY_ESC 0x01
#define VIL_KEY_ENTER 0x1c
#define VIL_KEY_SPACE 0x39
#define VIL_KEY_UP 0x48
#define VIL_KEY_LEFT 0x4b
#define VIL_KEY_RIGHT 0x4d
#define VIL_KEY_DOWN 0x50
#define VIL_KEY_DELETE 0x53
#define VIL_KEY_NONE 0xff

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the grid spins on the tick counter. */
#define VIL_TIMER_VECTOR 8

/* The adapter and the two modes a case moves between. */
#define VIL_MODE_TEXT 0x03
#define VIL_MODE_320X200X256 0x13

/* The shared .CEL.  One sheet satisfies the window frame, the selection bar
   and the command sprites: fdps_cel_blit_sprite takes the size out of the
   header's i16 pair at +0x07 and +0x09 (sprite.h) while
   fdps_blit_command_sprite passes its own fixed 25 by 22 and reads only the
   offset table.  Every stream is one skip run per row -- the op is the top two
   bits and the low six carry len-1, so 0xc0 | 24 skips exactly the 25 pixels
   the row must account for (rle.h) -- and therefore no sprite writes anything.

   THE RUN HAS TO ACCOUNT FOR EXACTLY 25 PIXELS.  A row ends when its covered
   pixels reach the width exactly and nothing bounds the stream, so a short run
   never closes the row and the decoder walks off the end of this array.

   0x48 entries because the highest index asked for is 0x47, the lit frame of
   the down arrow. */
#define VIL_CEL_SPRITES 0x48
#define VIL_CEL_TABLE_AT 0x0f
#define VIL_CEL_W 25
#define VIL_CEL_H 22
#define VIL_CEL_SKIP_CMD (0xc0 | (VIL_CEL_W - 1))
#define VIL_CEL_STREAMS_AT (VIL_CEL_TABLE_AT + VIL_CEL_SPRITES * 4)
#define VIL_CEL_BYTES (VIL_CEL_STREAMS_AT + VIL_CEL_SPRITES * VIL_CEL_H)

/* The walking-icon cache.  Unlike a loaded .CEL its offset table starts at the
   base with no header (vilmenu.c), it holds twelve streams per member, and the
   grid indexes it by ROSTER slot, so it needs twelve entries per member of the
   party above.  Each stream is a 24-row skip of 24 pixels, which is the 0x18
   by 0x18 fdps_blit_dispatch is told to draw. */
#define VIL_ICON_SPRITES_PER_SLOT 0x0c
#define VIL_ICON_SPRITES (VIL_MEMBER_COUNT * VIL_ICON_SPRITES_PER_SLOT)
#define VIL_ICON_SIDE 24
#define VIL_ICON_SKIP_CMD (0xc0 | (VIL_ICON_SIDE - 1))
#define VIL_ICON_STREAMS_AT (VIL_ICON_SPRITES * 4)
#define VIL_ICON_BYTES (VIL_ICON_STREAMS_AT + VIL_ICON_SPRITES * VIL_ICON_SIDE)

/* The text block.  fdps_draw_text takes a table of signed 16-bit byte offsets
   measured from the block's own base and walks the stream it points at until
   the token -1 (text.h), so a table whose every entry names one lone
   terminator draws nothing and needs no font staged.  The entry the grid asks
   for is char_id + 1 and the highest character id above is 7. */
#define VIL_TEXT_ENTRIES 0x100
#define VIL_TEXT_TERMINATOR (-1)

/* An empty container: the lookup walks no entries and answers NULL, so
   fdps_play_sfx finds no clip and starts no sample (audio.c). */
#define VIL_WAV_BANK_BYTES 16

/* How many codes one script can hold. */
#define VIL_SCRIPT_MAX 8

static unsigned char vil_cel[VIL_CEL_BYTES];
static unsigned char vil_icon_cache[VIL_ICON_BYTES];
static short vil_text[VIL_TEXT_ENTRIES + 1];
static unsigned char vil_wav_bank[VIL_WAV_BANK_BYTES];

static unsigned char vil_script[VIL_SCRIPT_MAX];
static int vil_script_len;
static int vil_script_next;
static void (__interrupt __far *vil_saved_timer)();

/* Advances the game's clock the way fdps_timer_tick_handler does, and presents
   the next code of the script to the auto-repeat filter -- but only once the
   filter has taken the one already there, which is what an equal previous
   scancode and latch mean (keybd.h).  One code per poll, however long a frame
   takes.  Past the end of the script escape and the no-key value alternate, so
   a runaway run cancels rather than hangs. */
static void __interrupt __far vil_timer_isr(void)
{
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (vil_script_next < vil_script_len) {
            code = vil_script[vil_script_next];
            vil_script_next++;
        } else if (data_fdps_input_last_scancode == VIL_KEY_ESC) {
            code = VIL_KEY_NONE;
        } else {
            code = VIL_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    _chain_intr(vil_saved_timer);
}

static void vil_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void vil_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void vil_build_cel(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(vil_cel, 0, sizeof(vil_cel));
    vil_cel[0] = 'C';
    vil_cel[1] = 'E';
    vil_cel[2] = 'L';
    vil_u16(vil_cel, 0x03, 1);
    vil_u16(vil_cel, 0x05, 0);
    vil_u16(vil_cel, 0x07, (unsigned int) VIL_CEL_W);
    vil_u16(vil_cel, 0x09, (unsigned int) VIL_CEL_H);
    vil_u16(vil_cel, 0x0b, (unsigned int) VIL_CEL_SPRITES);
    vil_u16(vil_cel, 0x0d, 2);

    for (sprite_id = 0; sprite_id < VIL_CEL_SPRITES; sprite_id++) {
        stream_at = VIL_CEL_STREAMS_AT + sprite_id * VIL_CEL_H;
        vil_u32(vil_cel, VIL_CEL_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < VIL_CEL_H; row++) {
            vil_cel[stream_at + row] = (unsigned char) VIL_CEL_SKIP_CMD;
        }
    }
}

/* The sprite cache's own table, which starts at the base and carries no
   header of any kind. */
static void vil_build_icon_cache(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(vil_icon_cache, 0, sizeof(vil_icon_cache));
    for (sprite_id = 0; sprite_id < VIL_ICON_SPRITES; sprite_id++) {
        stream_at = VIL_ICON_STREAMS_AT + sprite_id * VIL_ICON_SIDE;
        vil_u32(vil_icon_cache, sprite_id * 4, (unsigned long) stream_at);
        for (row = 0; row < VIL_ICON_SIDE; row++) {
            vil_icon_cache[stream_at + row] =
                (unsigned char) VIL_ICON_SKIP_CMD;
        }
    }
}

static void vil_stage(void)
{
    int slot;
    int entry;

    vil_build_cel();
    vil_build_icon_cache();

    memset(vil_roster, 0, sizeof(vil_roster));
    for (slot = 0; slot < VIL_MEMBER_COUNT; slot++) {
        vil_roster[slot].char_id = vil_char_ids[slot];
    }

    for (entry = 0; entry < VIL_TEXT_ENTRIES; entry++) {
        vil_text[entry] = (short) (VIL_TEXT_ENTRIES * 2);
    }
    vil_text[VIL_TEXT_ENTRIES] = VIL_TEXT_TERMINATOR;

    memset(vil_wav_bank, 0, sizeof(vil_wav_bank));

    data_fdps_roster_array_ptr = (unsigned char *) vil_roster;
    data_fdps_roster_member_count = VIL_MEMBER_COUNT;
    data_fdps_village_window_sheet_ptr = vil_cel;
    data_fdps_selection_bar_sheet_ptr = vil_cel;
    data_fdps_command_sprite_sheet_ptr = vil_cel;
    data_fdps_cel_sprite_cache_ptr = vil_icon_cache;
    data_fdps_all_game_text_ptr = (unsigned char *) vil_text;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = vil_wav_bank;

    /* Both halves of fdps_cd_music_repeat_poll's inner guard, so the poll the
       grid makes on every pass never reaches the drive (cdaudio.c). */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
}

/* Back to the state a freshly started program has these in.  It is not
   tidiness: the chapter loaders and the shutdown path free most of these
   pointers unguarded, so a case that walked away leaving one of them naming a
   static in this file would hand a later test a free() of storage that never
   came from the heap. */
static void vil_unstage(void)
{
    data_fdps_roster_array_ptr = NULL;
    data_fdps_roster_member_count = 0;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_input_last_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = VIL_KEY_NONE;
}

static void vil_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole run of the grid, with the adapter in the mode the game draws it in
   and the timer interrupt both pacing the frames and playing the keys.  The
   two grid globals are NOT reset here: where the cursor and the window start
   is what several cases are about, so each case sets them itself. */
static int vil_run(int chapter, unsigned char *codes, int count)
{
    int answer;
    int index;

    vil_stage();
    data_fdps_chapter_current_chapter_id = chapter;

    for (index = 0; index < count; index++) {
        vil_script[index] = codes[index];
    }
    vil_script_len = count;
    vil_script_next = 0;

    data_fdps_input_last_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    vil_set_mode(VIL_MODE_320X200X256);
    vil_saved_timer = _dos_getvect(VIL_TIMER_VECTOR);
    _dos_setvect(VIL_TIMER_VECTOR, vil_timer_isr);
    answer = fdps_village_select_member();
    _dos_setvect(VIL_TIMER_VECTOR, vil_saved_timer);
    vil_set_mode(VIL_MODE_TEXT);

    return answer;
}

static void vil_place(int cursor, int top)
{
    data_fdps_village_member_select_cursor_idx = cursor;
    data_fdps_village_member_grid_scroll_offset = top;
}

/* Escape stores -1 in the loop result at 00033706 and the epilogue's JLE hands
   that same -1 back.  It moves neither global: the cancel arm is a single
   store and a jump to the frame. */
static void vil_escape_cancels(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ESC;
    vil_place(2, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), -1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 0);
    vil_unstage();
}

/* 0x53 is the second cancel code, tested at 00033700 and joining escape's
   store, so Delete backs out of the grid exactly as Escape does.  A chain that
   only knew Escape would leave the grid open on Delete. */
static void vil_delete_cancels_like_escape(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_DELETE;
    vil_place(2, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), -1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    vil_unstage();
}

/* Enter stores 1 and the epilogue answers with the cursor itself -- the roster
   index, straight out of the global at 00033b5c. */
static void vil_enter_answers_the_cursor(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ENTER;
    vil_place(4, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), 4);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 4);
    vil_unstage();
}

/* 0x39 is the second confirm code, tested at 00033718 and joining Enter's
   store. */
static void vil_space_confirms_like_enter(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_SPACE;
    vil_place(1, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), 1);
    vil_unstage();
}

/* Right steps one entry and left steps one back, so two rights and a left
   leave the cursor on entry 1.  The filler between the two rights is what
   makes the second one a change the repeat filter reports. */
static void vil_right_and_left_step_one(void)
{
    unsigned char script[5];

    script[0] = VIL_KEY_RIGHT;
    script[1] = VIL_KEY_NONE;
    script[2] = VIL_KEY_RIGHT;
    script[3] = VIL_KEY_LEFT;
    script[4] = VIL_KEY_ENTER;
    vil_place(0, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 5), 1);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 0);
    vil_unstage();
}

/* The right guard is count-1 > cursor, so the last member is where right
   stops: nothing moves and nothing is played. */
static void vil_right_stops_on_the_last_member(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_RIGHT;
    script[1] = VIL_KEY_ENTER;
    vil_place(VIL_MEMBER_COUNT - 1, 3);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2),
             VIL_MEMBER_COUNT - 1);
    vil_unstage();
}

/* The left guard is cursor > 0, so the first member is where left stops. */
static void vil_left_stops_on_the_first_member(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_LEFT;
    script[1] = VIL_KEY_ENTER;
    vil_place(0, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 0);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 0);
    vil_unstage();
}

/* Down is a whole row and the grid is three columns wide, so it adds three. */
static void vil_down_steps_three(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_DOWN;
    script[1] = VIL_KEY_ENTER;
    vil_place(1, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 4);
    vil_unstage();
}

/* Up subtracts the same three. */
static void vil_up_steps_three(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_UP;
    script[1] = VIL_KEY_ENTER;
    vil_place(4, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 1);
    vil_unstage();
}

/* The up guard is cursor > 2, so the top row of three is where up stops --
   and the boundary is the cursor and not the window: entry 2 is on the first
   row and cannot go up. */
static void vil_up_stops_on_the_top_row(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_UP;
    script[1] = VIL_KEY_ENTER;
    vil_place(2, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 2);
    vil_unstage();
}

/* The down guard is count-3 > cursor, which needs a whole row still ahead --
   with eight members and the cursor on 5 there is none, so down is blocked
   even though entries 6 and 7 exist and right would reach them. */
static void vil_down_needs_a_whole_row_ahead(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_DOWN;
    script[1] = VIL_KEY_ENTER;
    vil_place(5, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 5);
    vil_unstage();
}

/* The window follows only when the cursor reaches top + 6, and it moves a
   whole row when it does: a single right off entry 5 puts the cursor on 6 and
   steps the window from 0 to 3. */
static void vil_window_follows_the_cursor_forward(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_RIGHT;
    script[1] = VIL_KEY_ENTER;
    vil_place(5, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 6);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 3);
    vil_unstage();
}

/* And back only when the cursor drops below the top: from entry 3 with the
   window at 3, one left puts the cursor on 2 and the window back to 0. */
static void vil_window_follows_the_cursor_back(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_LEFT;
    script[1] = VIL_KEY_ENTER;
    vil_place(3, 3);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 2);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 0);
    vil_unstage();
}

/* A vertical move carries the window with it on the same rule: down off entry
   4 lands on 7, which is top + 6 or more, so the window steps a row. */
static void vil_down_can_scroll_the_window(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_DOWN;
    script[1] = VIL_KEY_ENTER;
    vil_place(4, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 2), 7);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 3);
    vil_unstage();
}

/* From chapter index 0x17 on, a confirm with the cursor on roster slot 3 is
   dropped: the loop result is left at 0 and the grid keeps running, so the
   escape behind it is what ends the call and the answer is a cancel. */
static void vil_locked_slot_cannot_be_confirmed(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_ESC;
    vil_place(VIL_LOCKED_SLOT, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_AT_LOCK, script, 2), -1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, VIL_LOCKED_SLOT);
    vil_unstage();
}

/* Space is refused on the same slot for the same reason -- both confirm codes
   share the one gate at 0003371e. */
static void vil_locked_slot_refuses_space_too(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_SPACE;
    script[1] = VIL_KEY_ESC;
    vil_place(VIL_LOCKED_SLOT, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_AT_LOCK, script, 2), -1);
    vil_unstage();
}

/* The gate is JL against 0x17, so the chapter before it still accepts slot 3
   and the refusal really is a chapter boundary and not a permanent rule. */
static void vil_locked_slot_is_confirmable_before_the_chapter(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ENTER;
    vil_place(VIL_LOCKED_SLOT, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), VIL_LOCKED_SLOT);
    vil_unstage();
}

/* The refusal is on the SLOT and nothing else, so every other slot confirms
   normally in the same chapter -- including slot 5, which is the one carrying
   character id 1 and so the one drawn ghosted.  A build that gated the confirm
   on the character id instead would refuse this. */
static void vil_ghosted_member_is_still_confirmable(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ENTER;
    vil_place(VIL_GHOSTED_SLOT, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_AT_LOCK, script, 1), VIL_GHOSTED_SLOT);
    vil_unstage();
}

/* And the cursor still stops on the refused slot: it is not skipped over, so
   right off slot 2 lands on 3 rather than 4, and a following right leaves it
   on 4. */
static void vil_locked_slot_is_still_walked_over(void)
{
    unsigned char script[4];

    script[0] = VIL_KEY_RIGHT;
    script[1] = VIL_KEY_ENTER;
    script[2] = VIL_KEY_NONE;
    script[3] = VIL_KEY_ESC;
    vil_place(2, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_AT_LOCK, script, 4), -1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, VIL_LOCKED_SLOT);
    vil_unstage();
}

/* Neither global is seeded on entry, so the grid reopens where the last visit
   left it: a run that moves the cursor to 1 and confirms is followed by a run
   that only confirms, and the second answers the same member. */
static void vil_cursor_survives_between_visits(void)
{
    unsigned char first[2];
    unsigned char second[1];

    first[0] = VIL_KEY_RIGHT;
    first[1] = VIL_KEY_ENTER;
    second[0] = VIL_KEY_ENTER;

    vil_place(0, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, first, 2), 1);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, second, 1), 1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 1);
    vil_unstage();
}

/* A window past the end of the party is not clamped: the grid draws whatever
   the six cells resolve to and leaves the entries past the member count out,
   without touching the window.  It is also the case that runs
   fdps_get_roster_record over slots the party does not have -- the call sits
   ahead of the count test at 000338a8 -- and the pointer it forms must stay
   undereferenced. */
static void vil_window_past_the_party_is_left_alone(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ENTER;
    vil_place(7, 6);
    CHECK_EQ(vil_run(VIL_CHAPTER_BEFORE_LOCK, script, 1), 7);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 6);
    vil_unstage();
}

/* Nothing in the grid writes a roster record -- the only field it reads is
   char_id at +8, twice, and both reads are loads -- so the party comes out of
   a run exactly as it went in. */
static void vil_does_not_modify_the_roster(void)
{
    unsigned char script[3];

    script[0] = VIL_KEY_RIGHT;
    script[1] = VIL_KEY_DOWN;
    script[2] = VIL_KEY_ENTER;
    vil_place(0, 0);
    CHECK_EQ(vil_run(VIL_CHAPTER_AT_LOCK, script, 3), 4);
    CHECK_EQ(vil_roster[VIL_LOCKED_SLOT].char_id,
             vil_char_ids[VIL_LOCKED_SLOT]);
    CHECK_EQ(vil_roster[VIL_GHOSTED_SLOT].char_id, VIL_GHOSTED_CHAR_ID);
    CHECK_EQ(vil_roster[0].hp_current, 0);
    vil_unstage();
}

/* ---- fdps_village_member_status_loop, 00033f80 --------------------------
 *
 * Expected values come from the assembly at 00033f80 and from nothing else:
 * the seed MOV dword ptr [EBP-0x4],0x0 at 00033f8c in front of the entry test
 * CMP dword ptr [EBP-0x4],-0x1 / JZ at 00033f93, so the first pass always
 * runs; the two zoom calls with 0 and 1 as their second argument at 00033f99
 * and 00033fb0, either side of CALL fdps_village_select_member at 00033fa8
 * whose EAX is stored to the loop slot at 00033fad; the inner test CMP dword
 * ptr [EBP-0x4],-0x1 / JZ 0x00033fea at 00033fc2, whose taken arm jumps to the
 * back edge and not out of the loop; and the back edge JMP 0x00033f93 at
 * 00033fea, which is the only path out of a confirmation.  None of it is read
 * off the emitted C.
 *
 * HOW A LOOP AROUND A MODAL PICKER IS RUN FROM OUTSIDE.  The loop answers
 * nothing and its two visible effects -- the frame it puts on the adapter and
 * the window the status call draws -- are both other functions' behaviour.
 * What is left, and what the cases below assert, is the shape of the loop
 * itself: that a cancel ends it, that a confirmation does not, and that the
 * caller's page comes back untouched however many passes ran.  The picker's
 * surviving cursor is what counts the passes: a case confirms, then moves the
 * cursor one cell, then cancels, and where the cursor ends up says how many
 * times the picker was reopened.
 *
 * WHY THE STATUS WINDOW IS SAFE TO REACH HERE.
 * fdps_battle_show_unit_status_window resolves the index and returns without
 * drawing anything for portrait ids 0x24..0x27 (statwin.c), so a party staged
 * inside that range makes every confirmation a record lookup and a return.
 * That is a real call into the real function and not a stand-in: the arm it
 * takes is the one the game itself takes for those four ids.
 *
 * WHY THE UNIT ARRAY IS POINTED AT THE ROSTER.  The picker answers a roster
 * index and the status window resolves it through fdps_get_unit_record against
 * data_fdps_map_unit_array_ptr, which is the alias
 * fdps_load_field_chapter_resources leaves in place for the whole village
 * phase (vilmenu.h).  Staging the two apart would be staging a state the game
 * cannot be in when this loop runs.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Which index reached the status window, and
 * that the backdrop reached the adapter before it did, are not observable from
 * outside: the refusing arm has no effect to read, and every zoom frame writes
 * the same page to the adapter afterwards anyway.  Both are settled by reading
 * 00033fc8-00033fe7, where the memmove of 0xfa00 bytes from the page to
 * 0xa0000 stands between the confirmation test and the call.
 */

/* The backdrop page the three village screens hand this loop: a whole mode 13h
   frame, which is the count the memmove at 00033fc8 carries and the count the
   window animation copies out of it every frame.  It is filled with one value
   so that "the page was only read" is a single comparison. */
#define VST_PAGE_BYTES 0xfa00
#define VST_PAGE_FILL 0x5a

/* A portrait id inside the closed range 0x24..0x27 that
   fdps_battle_show_unit_status_window draws no window for. */
#define VST_NO_WINDOW_PORTRAIT 0x25

/* Something for the window animation to release on its first frame, so that
   the free at the top of it is a free of real heap storage and not of whatever
   an earlier case left in the global. */
#define VST_PORTRAIT_BUF_BYTES 16

static unsigned char *vst_page;

/* One whole visit to the status browser, staged the way the village phase
   leaves things: the picker's sheets and tables as every case above stages
   them, the unit array aliased onto the roster, and every member carrying a
   portrait id the status window refuses.  The two grid globals are NOT reset
   here -- where the cursor starts is what the cases are about. */
static void vst_run(int chapter, unsigned char *codes, int count)
{
    int index;
    int slot;

    vil_stage();
    data_fdps_chapter_current_chapter_id = chapter;
    for (slot = 0; slot < VIL_MEMBER_COUNT; slot++) {
        vil_roster[slot].portrait_id = VST_NO_WINDOW_PORTRAIT;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) vil_roster;
    data_fdps_portrait_sprite_buf_ptr =
        (unsigned char *) malloc((size_t) VST_PORTRAIT_BUF_BYTES);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);

    vst_page = (unsigned char *) malloc((size_t) VST_PAGE_BYTES);
    CHECK_EQ(vst_page != NULL, 1);
    if (vst_page == NULL) {
        return;
    }
    memset(vst_page, VST_PAGE_FILL, (size_t) VST_PAGE_BYTES);

    for (index = 0; index < count; index++) {
        vil_script[index] = codes[index];
    }
    vil_script_len = count;
    vil_script_next = 0;

    data_fdps_input_last_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    vil_set_mode(VIL_MODE_320X200X256);
    vil_saved_timer = _dos_getvect(VIL_TIMER_VECTOR);
    _dos_setvect(VIL_TIMER_VECTOR, vil_timer_isr);
    fdps_village_member_status_loop(vst_page);
    _dos_setvect(VIL_TIMER_VECTOR, vil_saved_timer);
    vil_set_mode(VIL_MODE_TEXT);
}

/* 1 when every byte of the page still holds what the run filled it with. */
static int vst_page_untouched(void)
{
    int index;

    if (vst_page == NULL) {
        return 0;
    }
    for (index = 0; index < VST_PAGE_BYTES; index++) {
        if (vst_page[index] != (unsigned char) VST_PAGE_FILL) {
            return 0;
        }
    }
    return 1;
}

static void vst_done(void)
{
    if (vst_page != NULL) {
        free(vst_page);
        vst_page = NULL;
    }
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    data_fdps_map_unit_array_ptr = NULL;
    vil_unstage();
}

/* The seed is zero and the entry test is against -1, so the picker is opened
   before anything has been picked; the cancel that comes back out of it is the
   loop's exit and nothing else runs.  The cancel arm of the picker moves
   neither of its globals, so the cursor is still where the case put it. */
static void vst_a_cancel_ends_the_first_pass(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ESC;
    vil_place(2, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    CHECK_EQ(data_fdps_village_member_grid_scroll_offset, 0);
    CHECK_EQ(vst_page_untouched(), 1);
    vst_done();
}

/* The confirmation arm jumps to the back edge and not out of the frame, so a
   confirmed member is followed by another picker.  The second one moves the
   cursor one cell before cancelling, and that move is only reachable if the
   picker really was opened a second time. */
static void vst_a_confirmation_reopens_the_picker(void)
{
    unsigned char script[3];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_RIGHT;
    script[2] = VIL_KEY_ESC;
    vil_place(0, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 3);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 1);
    vst_done();
}

/* And it is a loop and not a second pass: two confirmations in a row each get
   their own picker, so the cursor is two cells along by the time the cancel
   arrives. */
static void vst_it_keeps_reopening_until_a_cancel(void)
{
    unsigned char script[5];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_RIGHT;
    script[2] = VIL_KEY_ENTER;
    script[3] = VIL_KEY_RIGHT;
    script[4] = VIL_KEY_ESC;
    vil_place(0, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 5);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    vst_done();
}

/* The page is a source and never a destination, on the pass that memmoves it
   to the adapter as much as on the pass that does not: the animation composes
   on a page of its own and the memmove reads this one.  A run with a
   confirmation in it exercises both. */
static void vst_the_backdrop_page_is_only_read(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_ESC;
    vil_place(0, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 2);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 0);
    CHECK_EQ(vst_page_untouched(), 1);
    vst_done();
}

/* The window animation is really entered and not skipped past: it releases the
   portrait buffer unconditionally on its way to the first frame (village.h),
   and nothing else in this loop or in the picker touches that global. */
static void vst_the_window_animation_runs(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ESC;
    vil_place(0, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 1);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    vst_done();
}

/* Nothing on this path writes a unit record.  The picker only reads char_id
   and the status window's refusing arm only reads portrait_id, so the party
   comes out of a run with a confirmation in it exactly as it went in. */
static void vst_it_does_not_modify_the_party(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_ESC;
    vil_place(0, 0);
    vst_run(VIL_CHAPTER_BEFORE_LOCK, script, 2);
    CHECK_EQ(vil_roster[0].portrait_id, VST_NO_WINDOW_PORTRAIT);
    CHECK_EQ(vil_roster[0].char_id, vil_char_ids[0]);
    CHECK_EQ(vil_roster[0].hp_current, 0);
    CHECK_EQ(data_fdps_roster_member_count, VIL_MEMBER_COUNT);
    vst_done();
}

/* ---- fdps_village_item_sell_loop, 00034000 -------------------------------
 *
 * Expected values come from the assembly at 00034000 and from nothing else:
 * the seed MOV dword ptr [EBP-0x14],0x0 at 0003400c in front of the entry test
 * CMP dword ptr [EBP-0x14],-0x1 / JZ at 00034025, so the first pass always
 * runs; the four zoom calls with 1, 0, 1 and 0 as their second argument at
 * 00034013, 0003402f, 00034050 and 00034072; CALL fdps_unit_item_count / TEST
 * EAX,EAX / JNZ at 00034066 for the empty-bag split; MOV AL,byte ptr [EAX+0x8]
 * / AND EAX,0xff / INC EAX / MOV [0x00064030],EAX at 00034093 for the name
 * substitution and PUSH 0x1fb at 000340b4 for the message it is printed in;
 * MOV dword ptr [EBP-0x10],0x0 at 000340e2 for the slot seed; CMP EAX,-0x1 /
 * JZ at 00034105 for the list's cancel; ADD EAX,0xc9 / MOV [0x00064030],EAX at
 * 00034145 for the item substitution and PUSH 0x1fc at 0003418e for its
 * message; MOV DX,word ptr [EDX+0x13] / AND EDX,0xffff / LEA EDX,[EDX+EDX*0x2]
 * / SAR EDX,0x1f / SHL EDX,0x2 / SBB EAX,EDX / SAR EAX,0x2 / MOV
 * [0x00064038],EAX at 00034161 for the offer; TEST EAX,EAX / JNZ at 000341ab
 * for the prompt's one selling answer; and MOV EAX,[0x00064038] / ADD dword
 * ptr [0x000643a4],EAX at 000341cd for the purse taking the figure back out of
 * the global rather than recomputing it.  None of it is read off the emitted C.
 *
 * HOW A LOOP THIS DEEP IS RUN FROM OUTSIDE.  The function answers nothing:
 * everything it does is in the purse, in two dialogue globals and in one
 * member's inventory, and everything it asks is asked through two modal
 * pickers and one modal prompt.  So every case stages what all of those draw
 * through, places the member picker's cursor on the member the case is about,
 * installs a timer interrupt that advances the game's clock and answers both
 * input channels, calls, and then reads the purse, the globals and the record.
 *
 * THE TWO INPUT CHANNELS ARE SEPARATE AND THE INTERRUPT SERVES BOTH, exactly
 * as tests/shop.c's cover for fdps_shop_buy_loop describes.  The member picker
 * and the inventory list both read through fdps_read_scancode_auto_repeat,
 * which answers from data_fdps_input_last_scancode and THROWS THE RING AWAY on
 * every poll; fdps_prompt_two_choice reads the ring itself and flushes it once
 * on entry (keybd.h, msgwin.h).  The handler plays the picker keys into the
 * latch the way vil_timer_isr above does and the prompt answers into the ring,
 * advancing the answer list only when the ring's READ index has moved since
 * the push -- which is what tells a prompt's read from a picker poll's flush.
 *
 * PAST THE END OF A SCRIPT THE HANDLER ALTERNATES ESCAPE AND THE NO-KEY VALUE
 * on the latch and answers Escape on the ring, so a run that got away from its
 * script backs out of everything and fails an assertion instead of hanging the
 * test image.
 *
 * THE THREE CASES THAT REACH THE INVENTORY LIST NEED THE REAL MISC.VFS AND
 * FACE.CEL.  fdps_unit_item_select_window composes its frame out of Status.cel
 * and the unit's portrait, and both loaders name their file with a literal and
 * take no argument, so there is nothing to point at a fixture (unititem.h).
 * Those cases probe for the two files and skip themselves rather than
 * dereferencing what a failed load leaves behind; the cases that stop at the
 * refusal do not need them and always run.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Nothing is read back off the screen.  Which
 * of the two messages was drawn, and where, is fdps_draw_text's behaviour over
 * a text block staged here as empty entries; the gold readout is
 * fdps_draw_party_gold's.  What the messages are ABOUT is still pinned,
 * because each stands on its own arm and the arms leave different things
 * behind: the refusal publishes a NAME id and no figure, the offer publishes
 * an ITEM id and a figure.
 */

/* The text block, sized past 0x1fc -- the offer, which is the highest entry
   this loop asks for -- and past 0xc9 + the highest item id staged below.
   Every entry names one lone terminator, so drawing any of them paints nothing
   and no font is needed.  It replaces vil_text, which is only big enough for
   the member names the grid draws. */
#define SELL_TEXT_ENTRIES 0x220
#define SELL_TEXT_TERMINATOR (-1)

/* The ITEM.DAT records the cases sell.  The sold item's price is deliberately
   not a multiple of four -- 101 * 3 / 4 is 75.75 -- so the offer pins the
   truncation and not just the ratio. */
#define SELL_ITEM_RECORDS 16
#define SELL_ITEM_SOLD 5
#define SELL_ITEM_SOLD_PRICE 101
#define SELL_ITEM_SOLD_OFFER 75

/* One unit record's inventory: eight two-byte entries, a flag byte and an id
   byte each, with 0x80 for empty (unititem.h). */
#define SELL_SLOTS 8
#define SELL_SLOT_EMPTY 0x80
#define SELL_SLOT_CARRIED 0

/* The purse every run starts with, and the two members the cases use: one
   carrying the item in its first entry, one carrying nothing.  Neither is
   roster slot 3, so the picker's locked-slot rule decides nothing here. */
#define SELL_START_GOLD 1000
#define SELL_SELLER 1
#define SELL_EMPTY_HANDED 2

/* Values parked in the two dialogue globals before a run, so that "this arm
   never wrote it" is a single comparison.  Neither is a value any arm below
   could leave: the name ids are small and the offer is 75. */
#define SELL_TEXT_SENTINEL 0x5a5a
#define SELL_VALUE_SENTINEL 0x3c3c

/* The number sheet the gold readout and the status panel draw figures
   through: five colour rows of thirteen 6 by 8 glyphs, because the panel
   selects rows other than 0 and fdps_draw_number reaches a glyph's offset at
   (row * 13 + glyph) * 4 + 0x0f (text.c), so a sheet with one row's worth of
   table would read a stream byte as an offset.  Every glyph is one skip run
   per row and paints nothing. */
#define SELL_NUM_ROWS 5
#define SELL_NUM_GLYPHS 13
#define SELL_NUM_SPRITES (SELL_NUM_ROWS * SELL_NUM_GLYPHS)
#define SELL_NUM_W 6
#define SELL_NUM_H 8
#define SELL_NUM_TABLE_AT 0x0f
#define SELL_NUM_SKIP_CMD (0xc0 | (SELL_NUM_W - 1))
#define SELL_NUM_STREAMS_AT (SELL_NUM_TABLE_AT + SELL_NUM_SPRITES * 4)
#define SELL_NUM_BYTES (SELL_NUM_STREAMS_AT + SELL_NUM_SPRITES * SELL_NUM_H)

/* The status panel's two gauges: three graphics of 0x75 by 8 raw pixels,
   0x3a8 bytes apart (gauge.h). */
#define SELL_BAR_STRIDE 0x3a8
#define SELL_BAR_BYTES (3 * SELL_BAR_STRIDE)

/* One glyph of the font the panel would draw with if any staged text entry
   held anything. */
#define SELL_GLYPH_WIDTH 8
#define SELL_GLYPH_ROWS 1
#define SELL_FONT_BYTES 256

/* The backdrop page the three village screens hand this loop, filled with one
   value so that "the page was only read" is a single comparison.  It is also
   what the loop publishes as the village backdrop, which is where the
   inventory window's close takes the picture it puts back (statwin.c) -- the
   same page in both places, as the callers leave it. */
#define SELL_PAGE_BYTES 0xfa00
#define SELL_PAGE_FILL 0x5a

/* How many picker keys and how many prompt answers one case may script. */
#define SELL_SCRIPT_MAX 8
#define SELL_REPLY_MAX 4

/* The two containers the inventory window cannot be opened without. */
#define SELL_WINDOW_CONTAINER "MISC.VFS"
#define SELL_WINDOW_PORTRAIT "FACE.CEL"

static short sell_text[SELL_TEXT_ENTRIES + 1];
static struct fdps_item_effect sell_items[SELL_ITEM_RECORDS];
static unsigned char sell_num_cel[SELL_NUM_BYTES];
static unsigned char sell_bar_sheet[SELL_BAR_BYTES];
static unsigned char sell_font[SELL_FONT_BYTES];
static unsigned char *sell_page;

static unsigned char sell_replies[SELL_REPLY_MAX];
static int sell_reply_len;
static int sell_reply_next;
static int sell_reply_pending;
static int sell_reply_head_at_push;

/* Advances the game's clock and answers both input channels.  The latch half
   is vil_timer_isr's, unchanged.  The ring half pushes an answer whenever the
   ring is empty and moves the answer list on only when the read index has
   changed since the push, which is what separates a prompt's read from a
   picker poll's flush. */
static void __interrupt __far sell_timer_isr(void)
{
    int slot;
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (vil_script_next < vil_script_len) {
            code = vil_script[vil_script_next];
            vil_script_next++;
        } else if (data_fdps_input_last_scancode == VIL_KEY_ESC) {
            code = VIL_KEY_NONE;
        } else {
            code = VIL_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        if (sell_reply_pending != 0
            && data_fdps_input_scancode_queue_head != sell_reply_head_at_push) {
            sell_reply_next++;
        }
        if (sell_reply_next < sell_reply_len) {
            code = sell_replies[sell_reply_next];
        } else {
            code = VIL_KEY_ESC;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = code;
        sell_reply_head_at_push = data_fdps_input_scancode_queue_head;
        sell_reply_pending = 1;
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(vil_saved_timer);
}

/* Both shipped files the inventory window needs, probed rather than
   assumed. */
static int sell_window_files_present(void)
{
    FILE *probe;

    probe = fopen(SELL_WINDOW_CONTAINER, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);

    probe = fopen(SELL_WINDOW_PORTRAIT, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* A skip-only number sheet: one run per row, six pixels wide, which is the
   width fdps_draw_number tells the decoder to cover. */
static void sell_build_number_sheet(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(sell_num_cel, 0, sizeof(sell_num_cel));
    sell_num_cel[0] = 'C';
    sell_num_cel[1] = 'E';
    sell_num_cel[2] = 'L';
    vil_u16(sell_num_cel, 0x07, (unsigned int) SELL_NUM_W);
    vil_u16(sell_num_cel, 0x09, (unsigned int) SELL_NUM_H);
    vil_u16(sell_num_cel, 0x0b, (unsigned int) SELL_NUM_SPRITES);

    for (sprite_id = 0; sprite_id < SELL_NUM_SPRITES; sprite_id++) {
        stream_at = SELL_NUM_STREAMS_AT + sprite_id * SELL_NUM_H;
        vil_u32(sell_num_cel, SELL_NUM_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < SELL_NUM_H; row++) {
            sell_num_cel[stream_at + row] = (unsigned char) SELL_NUM_SKIP_CMD;
        }
    }
}

/* Everything the four screens on this path draw through, on top of the picker
   fixture every case above already uses: the bigger text block the two
   messages are in, the item table the offer is worked out of, the number and
   gauge sheets the readout and the panel need, the village branch of the
   prompt and of the window close, and a bag for each member.  The picker's own
   two globals are NOT reset -- where the cursor starts is what places the
   case's member. */
static void sell_stage(void)
{
    int entry;
    int member;
    int slot;

    vil_stage();
    sell_build_number_sheet();

    for (entry = 0; entry < SELL_TEXT_ENTRIES; entry++) {
        sell_text[entry] = (short) (SELL_TEXT_ENTRIES * 2);
    }
    sell_text[SELL_TEXT_ENTRIES] = SELL_TEXT_TERMINATOR;
    data_fdps_all_game_text_ptr = (unsigned char *) sell_text;

    memset(sell_items, 0, sizeof(sell_items));
    sell_items[SELL_ITEM_SOLD].price = (unsigned short) SELL_ITEM_SOLD_PRICE;
    data_fdps_item_effect_table_ptr = (unsigned char *) sell_items;

    memset(sell_bar_sheet, 0x01, sizeof(sell_bar_sheet));
    memset(sell_font, 0, sizeof(sell_font));

    data_fdps_number_glyph_sheet_ptr = sell_num_cel;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_status_gauge_bar_sheet_ptr = sell_bar_sheet;
    data_fdps_shadow_sprite_sheet_ptr = vil_cel;
    data_fdps_font_sheet_ptr = sell_font;
    data_fdps_font_glyph_width = (unsigned char) SELL_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) SELL_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_advance_x = SELL_GLYPH_WIDTH;
    data_fdps_font_line_height = SELL_GLYPH_ROWS;

    /* Every bag empty; the case that sells puts one entry back. */
    for (member = 0; member < VIL_MEMBER_COUNT; member++) {
        for (slot = 0; slot < SELL_SLOTS; slot++) {
            vil_roster[member].inventory_slots[slot * 2] =
                (unsigned char) SELL_SLOT_EMPTY;
            vil_roster[member].inventory_slots[slot * 2 + 1] = 0;
        }
    }

    /* The village phase points the map unit array at the roster block, which
       is what lets every fdps_unit_* accessor here reach the record the picker
       answered with (vilmenu.h). */
    data_fdps_map_unit_array_ptr = (unsigned char *) vil_roster;

    /* Village mode, so the prompt puts the visible page back behind itself and
       the window close takes its picture out of the backdrop page instead of
       recomposing a battle scene (msgwin.h, statwin.h). */
    data_fdps_village_mode_flag = 1;
    data_fdps_portrait_sprite_buf_ptr = NULL;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_village_status_window_unit_idx = -1;
    data_fdps_unit_status_window_last_tick = -1;

    data_fdps_shared_party_total_gold = SELL_START_GOLD;
    data_fdps_dialog_last_action_text_id_param = SELL_TEXT_SENTINEL;
    data_fdps_dialog_last_action_value_param = SELL_VALUE_SENTINEL;
}

static void sell_put(int member, int slot, int flag, int item_id)
{
    vil_roster[member].inventory_slots[slot * 2] = (unsigned char) flag;
    vil_roster[member].inventory_slots[slot * 2 + 1] = (unsigned char) item_id;
}

/* One whole visit to the sell counter, with the adapter in the mode the game
   draws it in and the timer interrupt pacing the frames and answering both
   channels.  The fixture is NOT staged here: a case calls sell_stage first and
   then fills the bag it is about, and the two steps have to stay apart for
   that. */
static void sell_go(unsigned char *codes, int count,
                    unsigned char *replies, int reply_count)
{
    int index;

    sell_page = (unsigned char *) malloc((size_t) SELL_PAGE_BYTES);
    CHECK_EQ(sell_page != NULL, 1);
    if (sell_page == NULL) {
        return;
    }
    memset(sell_page, SELL_PAGE_FILL, (size_t) SELL_PAGE_BYTES);
    data_fdps_village_backdrop_page_ptr = sell_page;

    for (index = 0; index < count; index++) {
        vil_script[index] = codes[index];
    }
    vil_script_len = count;
    vil_script_next = 0;

    for (index = 0; index < reply_count; index++) {
        sell_replies[index] = replies[index];
    }
    sell_reply_len = reply_count;
    sell_reply_next = 0;
    sell_reply_pending = 0;
    sell_reply_head_at_push = 0;

    data_fdps_input_last_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = VIL_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    vil_set_mode(VIL_MODE_320X200X256);
    vil_saved_timer = _dos_getvect(VIL_TIMER_VECTOR);
    _dos_setvect(VIL_TIMER_VECTOR, sell_timer_isr);
    fdps_village_item_sell_loop(sell_page);
    _dos_setvect(VIL_TIMER_VECTOR, vil_saved_timer);
    vil_set_mode(VIL_MODE_TEXT);
}

/* 1 when every byte of the page still holds what the run filled it with. */
static int sell_page_untouched(void)
{
    int index;

    if (sell_page == NULL) {
        return 0;
    }
    for (index = 0; index < SELL_PAGE_BYTES; index++) {
        if (sell_page[index] != (unsigned char) SELL_PAGE_FILL) {
            return 0;
        }
    }
    return 1;
}

/* How many of a member's eight entries still hold something, counted here
   rather than through fdps_unit_item_count so that the assertion does not lean
   on the same accessor the code under test branches on. */
static int sell_carried(int member)
{
    int slot;
    int count;

    count = 0;
    for (slot = 0; slot < SELL_SLOTS; slot++) {
        if ((vil_roster[member].inventory_slots[slot * 2]
             & SELL_SLOT_EMPTY) == 0) {
            count++;
        }
    }
    return count;
}

static void sell_done(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    if (sell_page != NULL) {
        free(sell_page);
        sell_page = NULL;
    }
    data_fdps_village_backdrop_page_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_status_gauge_bar_sheet_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_village_mode_flag = 0;
    data_fdps_shared_party_total_gold = 0;
    vil_unstage();
}

/* The seed is zero and the entry test is against -1, so the picker is opened
   before anything has been picked; the cancel that comes back out of it is the
   loop's exit and nothing else runs.  Neither dialogue global is written on
   that path, which is what the two sentinels show. */
static void sell_a_cancel_ends_the_first_pass(void)
{
    unsigned char script[1];

    script[0] = VIL_KEY_ESC;
    vil_place(SELL_EMPTY_HANDED, 0);
    sell_stage();
    sell_go(script, 1, NULL, 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SELL_START_GOLD);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param, SELL_TEXT_SENTINEL);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SELL_VALUE_SENTINEL);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, SELL_EMPTY_HANDED);
    CHECK_EQ(sell_page_untouched(), 1);
    sell_done();
}

/* A member carrying nothing is refused: the name substitution is that member's
   character id plus one -- the message table's bias for a character name --
   the figure slot is never written, and the purse does not move.  A body that
   published the id itself, or that took the bias the item names use, would
   miss here by exactly one and by exactly 0xc8. */
static void sell_an_empty_bag_publishes_the_member_name(void)
{
    unsigned char script[2];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_ESC;
    vil_place(SELL_EMPTY_HANDED, 0);
    sell_stage();
    sell_go(script, 2, NULL, 0);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             (int) vil_char_ids[SELL_EMPTY_HANDED] + 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SELL_VALUE_SENTINEL);
    CHECK_EQ(data_fdps_shared_party_total_gold, SELL_START_GOLD);
    CHECK_EQ(sell_carried(SELL_EMPTY_HANDED), 0);
    CHECK_EQ(sell_page_untouched(), 1);
    sell_done();
}

/* And the refusal is not the end of the call: the arm falls to the back edge,
   so the picker opens again.  The second visit moves the cursor one cell
   before cancelling, and that move is only reachable if the picker really was
   reopened. */
static void sell_a_refusal_reopens_the_picker(void)
{
    unsigned char script[3];

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_RIGHT;
    script[2] = VIL_KEY_ESC;
    vil_place(SELL_EMPTY_HANDED, 0);
    sell_stage();
    sell_go(script, 3, NULL, 0);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx,
             SELL_EMPTY_HANDED + 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, SELL_START_GOLD);
    sell_done();
}

/* A settled sale.  The offer is three quarters of the listed price truncated
   -- 101 becomes 75 and not 76 -- the name substitution is the item id plus
   0xc9 and not the character bias, the entry leaves the bag, and the purse
   grows by the offer that was published, taken back out of the figure global
   rather than worked out a second time.  The cursor moving after the sale is
   the loop going round again rather than back into the same bag. */
static void sell_a_settled_sale_pays_three_quarters(void)
{
    unsigned char script[5];
    unsigned char replies[1];

    if (sell_window_files_present() == 0) {
        return;
    }

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_NONE;
    script[2] = VIL_KEY_ENTER;
    script[3] = VIL_KEY_RIGHT;
    script[4] = VIL_KEY_ESC;
    replies[0] = VIL_KEY_ENTER;

    vil_place(SELL_SELLER, 0);
    sell_stage();
    sell_put(SELL_SELLER, 0, SELL_SLOT_CARRIED, SELL_ITEM_SOLD);
    sell_go(script, 5, replies, 1);

    CHECK_EQ(data_fdps_dialog_last_action_value_param, SELL_ITEM_SOLD_OFFER);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             SELL_ITEM_SOLD + 0xc9);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             SELL_START_GOLD + SELL_ITEM_SOLD_OFFER);
    CHECK_EQ(sell_carried(SELL_SELLER), 0);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, SELL_SELLER + 1);
    CHECK_EQ(sell_page_untouched(), 1);
    sell_done();
}

/* The prompt's other answers do not sell.  A cancel out of it leaves the entry
   in the bag and the purse where it was -- but the offer it was asked about
   still stands in the figure global, because that is written before the prompt
   runs and no arm puts it back. */
static void sell_a_declined_offer_leaves_the_bag_alone(void)
{
    unsigned char script[5];
    unsigned char replies[1];

    if (sell_window_files_present() == 0) {
        return;
    }

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_NONE;
    script[2] = VIL_KEY_ENTER;
    script[3] = VIL_KEY_NONE;
    script[4] = VIL_KEY_ESC;
    replies[0] = VIL_KEY_ESC;

    vil_place(SELL_SELLER, 0);
    sell_stage();
    sell_put(SELL_SELLER, 0, SELL_SLOT_CARRIED, SELL_ITEM_SOLD);
    sell_go(script, 5, replies, 1);

    CHECK_EQ(data_fdps_shared_party_total_gold, SELL_START_GOLD);
    CHECK_EQ(sell_carried(SELL_SELLER), 1);
    CHECK_EQ(vil_roster[SELL_SELLER].inventory_slots[1], SELL_ITEM_SOLD);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SELL_ITEM_SOLD_OFFER);
    sell_done();
}

/* Backing out of the inventory list skips the whole sale arm, offer included:
   the -1 the list answers with jumps to the back edge ahead of the gold
   readout, so neither dialogue global is written and the picker simply opens
   again. */
static void sell_a_cancelled_list_publishes_nothing(void)
{
    unsigned char script[5];

    if (sell_window_files_present() == 0) {
        return;
    }

    script[0] = VIL_KEY_ENTER;
    script[1] = VIL_KEY_NONE;
    script[2] = VIL_KEY_ESC;
    script[3] = VIL_KEY_NONE;
    script[4] = VIL_KEY_ESC;

    vil_place(SELL_SELLER, 0);
    sell_stage();
    sell_put(SELL_SELLER, 0, SELL_SLOT_CARRIED, SELL_ITEM_SOLD);
    sell_go(script, 5, NULL, 0);

    CHECK_EQ(data_fdps_shared_party_total_gold, SELL_START_GOLD);
    CHECK_EQ(sell_carried(SELL_SELLER), 1);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param, SELL_TEXT_SENTINEL);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, SELL_VALUE_SENTINEL);
    sell_done();
}

void run_vilmenu_tests(void)
{
    RUN_TEST(vil_escape_cancels);
    RUN_TEST(vil_delete_cancels_like_escape);
    RUN_TEST(vil_enter_answers_the_cursor);
    RUN_TEST(vil_space_confirms_like_enter);
    RUN_TEST(vil_right_and_left_step_one);
    RUN_TEST(vil_right_stops_on_the_last_member);
    RUN_TEST(vil_left_stops_on_the_first_member);
    RUN_TEST(vil_down_steps_three);
    RUN_TEST(vil_up_steps_three);
    RUN_TEST(vil_up_stops_on_the_top_row);
    RUN_TEST(vil_down_needs_a_whole_row_ahead);
    RUN_TEST(vil_window_follows_the_cursor_forward);
    RUN_TEST(vil_window_follows_the_cursor_back);
    RUN_TEST(vil_down_can_scroll_the_window);
    RUN_TEST(vil_locked_slot_cannot_be_confirmed);
    RUN_TEST(vil_locked_slot_refuses_space_too);
    RUN_TEST(vil_locked_slot_is_confirmable_before_the_chapter);
    RUN_TEST(vil_ghosted_member_is_still_confirmable);
    RUN_TEST(vil_locked_slot_is_still_walked_over);
    RUN_TEST(vil_cursor_survives_between_visits);
    RUN_TEST(vil_window_past_the_party_is_left_alone);
    RUN_TEST(vil_does_not_modify_the_roster);
    RUN_TEST(vst_a_cancel_ends_the_first_pass);
    RUN_TEST(vst_a_confirmation_reopens_the_picker);
    RUN_TEST(vst_it_keeps_reopening_until_a_cancel);
    RUN_TEST(vst_the_backdrop_page_is_only_read);
    RUN_TEST(vst_the_window_animation_runs);
    RUN_TEST(vst_it_does_not_modify_the_party);
    RUN_TEST(sell_a_cancel_ends_the_first_pass);
    RUN_TEST(sell_an_empty_bag_publishes_the_member_name);
    RUN_TEST(sell_a_refusal_reopens_the_picker);
    RUN_TEST(sell_a_settled_sale_pays_three_quarters);
    RUN_TEST(sell_a_declined_offer_leaves_the_bag_alone);
    RUN_TEST(sell_a_cancelled_list_publishes_nothing);
}
