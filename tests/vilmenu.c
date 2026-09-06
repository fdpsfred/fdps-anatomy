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
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palcycle.h"
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
}
