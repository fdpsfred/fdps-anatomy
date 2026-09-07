/* tests/vilbar.c -- cover for src/vilbar.c.
 *
 * THE DRAW'S CASES ARE ITS GUARD AND ONLY ITS GUARD.  fdps_run_bonus_lottery
 * opens on one date, 28 January 1998, and the four cases below drive the DOS
 * date across that boundary and put the machine's own date back afterwards.
 * Expected values come from the assembly at 00036460 and from nothing else:
 * the run-once test CMP dword ptr [0x00064110],0x0 / JNZ at 0003648a and the
 * three date compares CMP EAX,0x7ce at 00036499, CMP EAX,0x1 at 000364a7 and
 * CMP EAX,0x1c at 000364b3, every one of which falls to the same JMP 0x36ae2
 * at 000364b8, and the store MOV dword ptr [0x00064110],0x1 at 00036ad8 that
 * is the last instruction of the body and so is not reached by any of them.
 *
 * WHAT IS INSIDE THE GUARD CANNOT BE REACHED BY AN ASSERTION.  The arm that
 * awards the prize is chosen by a stack slot nothing has written (vilbar.h),
 * so which of the four prizes a run hands out is whatever the frame's storage
 * held, and pinning it would be pinning this compiler's frame layout rather
 * than the program.  The reel itself is a keyboard-stopped animation over a
 * real .SAF and belongs to the manual playtest (ADR-0003).
 *
 * WHAT A CASE ASSERTS INSTEAD is that nothing happened: the run-once flag is
 * where the case left it, the purse is untouched and every bag is still empty.
 * Those three cover all four prize arms between them -- the gold arm adds
 * 20,000 to the purse and the other three put an item into every roster
 * member's bag -- so a guard that had stopped working moves one of them
 * whichever arm the unwritten slot picked.
 *
 * NO CASE NEEDS A SHIPPED FILE, because every one of them returns at the guard
 * before the reel clip is loaded.  The fixture below is still staged in full:
 * a guard that had stopped working would run the whole draw, and it has to
 * find the village's own globals in place rather than walk off a null.
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "keybd.h"
#include "statunit.h"
#include "vilbar.h"

/* The party the draw would hand its prize to.  Six is more than any case
   needs and every roster index used is below the twelve walking-icon groups
   the cache below holds. */
#define BAR_MEMBER_COUNT 6

/* A chapter before the one that locks roster slot 3 and ghosts a member, so
   neither rule decides anything about the roster these cases stage. */
#define BAR_CHAPTER_BEFORE_LOCK 0x16

/* The one make code the handler below presents, and the filter's no-key
   answer. */
#define BAR_KEY_ESC 0x01
#define BAR_KEY_NONE 0xff

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the draw spins on the tick counter. */
#define BAR_TIMER_VECTOR 8

/* The shared .CEL.  One sheet satisfies the window frame, the selection bar
   and the command sprites: fdps_cel_blit_sprite takes the size out of the
   header's i16 pair at +0x07 and +0x09 (sprite.h) while
   fdps_blit_command_sprite passes its own fixed 25 by 22 and reads only the
   offset table.  Every stream is one skip run per row -- the op is the top two
   bits and the low six carry len-1, so 0xc0 | 24 skips exactly the 25 pixels
   the row must account for (rle.h) -- and therefore no sprite writes anything.

   THE RUN HAS TO ACCOUNT FOR EXACTLY 25 PIXELS.  A row ends when its covered
   pixels reach the width exactly and nothing bounds the stream, so a short run
   never closes the row and the decoder walks off the end of this array. */
#define BAR_CEL_SPRITES 0x48
#define BAR_CEL_TABLE_AT 0x0f
#define BAR_CEL_W 25
#define BAR_CEL_H 22
#define BAR_CEL_SKIP_CMD (0xc0 | (BAR_CEL_W - 1))
#define BAR_CEL_STREAMS_AT (BAR_CEL_TABLE_AT + BAR_CEL_SPRITES * 4)
#define BAR_CEL_BYTES (BAR_CEL_STREAMS_AT + BAR_CEL_SPRITES * BAR_CEL_H)

/* The walking-icon cache.  Unlike a loaded .CEL its offset table starts at the
   base with no header (vilmenu.c), it holds twelve streams per member, and the
   member grid indexes it by ROSTER slot. */
#define BAR_ICON_PER_SLOT 0x0c
#define BAR_ICON_SPRITES (BAR_MEMBER_COUNT * BAR_ICON_PER_SLOT)
#define BAR_ICON_SIDE 24
#define BAR_ICON_SKIP_CMD (0xc0 | (BAR_ICON_SIDE - 1))
#define BAR_ICON_STREAMS_AT (BAR_ICON_SPRITES * 4)
#define BAR_ICON_BYTES (BAR_ICON_STREAMS_AT + BAR_ICON_SPRITES * BAR_ICON_SIDE)

/* The number sheet the gold readout paints its eight digits out of: five
   colour rows of thirteen glyphs, one skip run per row six pixels wide, which
   is the width fdps_draw_number tells the decoder to cover. */
#define BAR_NUM_ROWS 5
#define BAR_NUM_GLYPHS 13
#define BAR_NUM_SPRITES (BAR_NUM_ROWS * BAR_NUM_GLYPHS)
#define BAR_NUM_W 6
#define BAR_NUM_H 8
#define BAR_NUM_TABLE_AT 0x0f
#define BAR_NUM_SKIP_CMD (0xc0 | (BAR_NUM_W - 1))
#define BAR_NUM_STREAMS_AT (BAR_NUM_TABLE_AT + BAR_NUM_SPRITES * 4)
#define BAR_NUM_BYTES (BAR_NUM_STREAMS_AT + BAR_NUM_SPRITES * BAR_NUM_H)

/* The resident text block.  fdps_draw_text takes a table of signed 16-bit byte
   offsets measured from the block's own base and walks the stream it points at
   until the token -1 (text.h), so a table whose every entry names one lone
   terminator draws nothing and needs no glyphs staged.  0x230 entries because
   the highest id anything reachable from here asks for is the draw's own
   closing line 0x229. */
#define BAR_TEXT_ENTRIES 0x230
#define BAR_TEXT_TERMINATOR (-1)

/* The loaded chapter's own block, a different table from the resident one and
   where the bar's reprint arm takes entry 6 from. */
#define BAR_CHAPTER_TEXT_ENTRIES 0x20

/* An empty container: the lookup walks no entries and answers NULL, so
   fdps_play_sfx finds no clip and starts no sample (audio.c). */
#define BAR_WAV_BANK_BYTES 16

/* The master palette the zoom transition uploads over the whole DAC on each of
   its nine steps (transit.h).  256 entries of three bytes; the contents decide
   only what the screen looks like. */
#define BAR_PALETTE_BYTES 768

/* The eight glyph font, which draws nothing because no text entry has a glyph
   token in it. */
#define BAR_FONT_BYTES 256
#define BAR_GLYPH_WIDTH 8
#define BAR_GLYPH_ROWS 1

/* The two data tables the village phase leaves published.  RankUp.dat is four
   3-byte routes per character and FRILEVUP.DAT eleven bytes per form.  Nothing
   in this file reads either of them; they are staged so that the globals
   naming them hold what a live village phase would have left there. */
#define BAR_CHARACTERS 16
#define BAR_FORMS 16
#define BAR_ROUTES 4
#define BAR_PROMOTION_STRIDE 0x0c
#define BAR_GROWTH_STRIDE 0x0b
#define BAR_ROUTE_BYTES 3
#define BAR_ROUTE_FORM_AT 0
#define BAR_ROUTE_CLASS_AT 1
#define BAR_ROUTE_MOVE_AT 2
#define BAR_ROUTE_CLASS_BASE 0x10
#define BAR_ROUTE_FORM_BASE 0x40
#define BAR_CLASS_OF(character, route) \
    (BAR_ROUTE_CLASS_BASE + (character) * BAR_ROUTES + (route))

/* A portrait id inside the closed range 0x24..0x27 that
   fdps_battle_show_unit_status_window draws no window for. */
#define BAR_NO_WINDOW_PORTRAIT 0x25

/* An inventory entry's flag byte: bit 0x80 is "no item here" (unititem.h), so
   a bag of eight of them is a member carrying nothing. */
#define BAR_SLOTS 8
#define BAR_SLOT_EMPTY 0x80

/* What the three dialogue slots hold before a run.  All three are outside
   every id or figure any arm can legitimately write. */
#define BAR_TEXT_SENTINEL 0x5a5a
#define BAR_SUBST_SENTINEL 0x7e7e
#define BAR_VALUE_SENTINEL 0x3c3c

/* The purse the gold arm of the draw would add to. */
#define BAR_START_GOLD 1000

/* How many codes one script can hold. */
#define BAR_SCRIPT_MAX 12

static struct fdps_unit_record bar_roster[BAR_MEMBER_COUNT];

static unsigned char bar_char_ids[BAR_MEMBER_COUNT] = { 0, 2, 3, 4, 5, 6 };

static unsigned char bar_cel[BAR_CEL_BYTES];
static unsigned char bar_icon_cache[BAR_ICON_BYTES];
static unsigned char bar_num_cel[BAR_NUM_BYTES];
static short bar_text[BAR_TEXT_ENTRIES + 1];
static short bar_chapter_text[BAR_CHAPTER_TEXT_ENTRIES + 1];
static unsigned char bar_wav_bank[BAR_WAV_BANK_BYTES];
static unsigned char bar_palette[BAR_PALETTE_BYTES];
static unsigned char bar_font[BAR_FONT_BYTES];
static unsigned char bar_promo[BAR_CHARACTERS * BAR_PROMOTION_STRIDE];
static unsigned char bar_growth[BAR_FORMS * BAR_GROWTH_STRIDE];

static unsigned char bar_script[BAR_SCRIPT_MAX];
static int bar_script_len;
static int bar_script_next;
static void (__interrupt __far *bar_saved_timer)();

/* Advances the game's clock, refills the auto-repeat filter's latch once the
   filter has taken what was there, and appends one Escape per tick to the
   scancode ring the way fdps_keyboard_isr does -- which is the answer that
   declines a yes/no question. */
static void __interrupt __far bar_timer_isr(void)
{
    unsigned char code;
    int slot;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (bar_script_next < bar_script_len) {
            code = bar_script[bar_script_next];
            bar_script_next++;
        } else if (data_fdps_input_last_scancode == BAR_KEY_ESC) {
            code = BAR_KEY_NONE;
        } else {
            code = BAR_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    slot = data_fdps_input_scancode_queue_write_index;
    data_fdps_input_scancode_queue[slot] = (unsigned char) BAR_KEY_ESC;
    slot++;
    if (slot == SCANCODE_QUEUE_LEN) {
        slot = 0;
    }
    data_fdps_input_scancode_queue_write_index = slot;

    _chain_intr(bar_saved_timer);
}

static void bar_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void bar_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void bar_build_cel(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(bar_cel, 0, sizeof(bar_cel));
    bar_cel[0] = 'C';
    bar_cel[1] = 'E';
    bar_cel[2] = 'L';
    bar_u16(bar_cel, 0x07, (unsigned int) BAR_CEL_W);
    bar_u16(bar_cel, 0x09, (unsigned int) BAR_CEL_H);
    bar_u16(bar_cel, 0x0b, (unsigned int) BAR_CEL_SPRITES);

    for (sprite_id = 0; sprite_id < BAR_CEL_SPRITES; sprite_id++) {
        stream_at = BAR_CEL_STREAMS_AT + sprite_id * BAR_CEL_H;
        bar_u32(bar_cel, BAR_CEL_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < BAR_CEL_H; row++) {
            bar_cel[stream_at + row] = (unsigned char) BAR_CEL_SKIP_CMD;
        }
    }
}

/* The sprite cache's own table, which starts at the base and carries no header
   of any kind. */
static void bar_build_icon_cache(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(bar_icon_cache, 0, sizeof(bar_icon_cache));
    for (sprite_id = 0; sprite_id < BAR_ICON_SPRITES; sprite_id++) {
        stream_at = BAR_ICON_STREAMS_AT + sprite_id * BAR_ICON_SIDE;
        bar_u32(bar_icon_cache, sprite_id * 4, (unsigned long) stream_at);
        for (row = 0; row < BAR_ICON_SIDE; row++) {
            bar_icon_cache[stream_at + row] =
                (unsigned char) BAR_ICON_SKIP_CMD;
        }
    }
}

static void bar_build_number_sheet(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(bar_num_cel, 0, sizeof(bar_num_cel));
    bar_num_cel[0] = 'C';
    bar_num_cel[1] = 'E';
    bar_num_cel[2] = 'L';
    bar_u16(bar_num_cel, 0x07, (unsigned int) BAR_NUM_W);
    bar_u16(bar_num_cel, 0x09, (unsigned int) BAR_NUM_H);
    bar_u16(bar_num_cel, 0x0b, (unsigned int) BAR_NUM_SPRITES);

    for (sprite_id = 0; sprite_id < BAR_NUM_SPRITES; sprite_id++) {
        stream_at = BAR_NUM_STREAMS_AT + sprite_id * BAR_NUM_H;
        bar_u32(bar_num_cel, BAR_NUM_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < BAR_NUM_H; row++) {
            bar_num_cel[stream_at + row] = (unsigned char) BAR_NUM_SKIP_CMD;
        }
    }
}

/* Overwrites one 3-byte route of one RankUp.dat record. */
static void bar_set_route(int character, int route, int form, int class_id,
                          int move_bonus)
{
    unsigned char *entry;

    entry = bar_promo + character * BAR_PROMOTION_STRIDE
            + route * BAR_ROUTE_BYTES;
    entry[BAR_ROUTE_FORM_AT] = (unsigned char) form;
    entry[BAR_ROUTE_CLASS_AT] = (unsigned char) class_id;
    entry[BAR_ROUTE_MOVE_AT] = (unsigned char) move_bonus;
}

/* Everything the bar screen and the draw it opens read and draw through,
   staged the way the village phase leaves it.  The member picker's own two
   globals are NOT reset here, because nothing in this file opens a picker. */
static void bar_stage(void)
{
    int slot;
    int entry;
    int character;
    int route;

    bar_build_cel();
    bar_build_icon_cache();
    bar_build_number_sheet();

    memset(bar_roster, 0, sizeof(bar_roster));
    for (slot = 0; slot < BAR_MEMBER_COUNT; slot++) {
        bar_roster[slot].char_id = bar_char_ids[slot];
        bar_roster[slot].portrait_id = (unsigned char) BAR_NO_WINDOW_PORTRAIT;
        bar_roster[slot].level = 1;
        for (entry = 0; entry < BAR_SLOTS; entry++) {
            bar_roster[slot].inventory_slots[entry * 2] =
                (unsigned char) BAR_SLOT_EMPTY;
            bar_roster[slot].inventory_slots[entry * 2 + 1] = 0;
        }
    }

    for (entry = 0; entry < BAR_TEXT_ENTRIES; entry++) {
        bar_text[entry] = (short) (BAR_TEXT_ENTRIES * 2);
    }
    bar_text[BAR_TEXT_ENTRIES] = (short) BAR_TEXT_TERMINATOR;

    for (entry = 0; entry < BAR_CHAPTER_TEXT_ENTRIES; entry++) {
        bar_chapter_text[entry] = (short) (BAR_CHAPTER_TEXT_ENTRIES * 2);
    }
    bar_chapter_text[BAR_CHAPTER_TEXT_ENTRIES] = (short) BAR_TEXT_TERMINATOR;

    memset(bar_promo, 0, sizeof(bar_promo));
    for (character = 0; character < BAR_CHARACTERS; character++) {
        for (route = 0; route < BAR_ROUTES; route++) {
            bar_set_route(character, route,
                          BAR_ROUTE_FORM_BASE + character * BAR_ROUTES + route,
                          BAR_CLASS_OF(character, route), route + 1);
        }
    }
    memset(bar_growth, 0, sizeof(bar_growth));

    memset(bar_wav_bank, 0, sizeof(bar_wav_bank));
    memset(bar_font, 0, sizeof(bar_font));
    memset(bar_palette, 0, sizeof(bar_palette));

    data_fdps_roster_array_ptr = (unsigned char *) bar_roster;
    data_fdps_roster_member_count = BAR_MEMBER_COUNT;

    /* The village phase points the map unit array at the roster block, which
       is what lets every fdps_unit_* accessor reach the record a roster slot
       stands for (vilmenu.h). */
    data_fdps_map_unit_array_ptr = (unsigned char *) bar_roster;

    data_fdps_village_window_sheet_ptr = bar_cel;
    data_fdps_selection_bar_sheet_ptr = bar_cel;
    data_fdps_command_sprite_sheet_ptr = bar_cel;
    data_fdps_shadow_sprite_sheet_ptr = bar_cel;
    data_fdps_cel_sprite_cache_ptr = bar_icon_cache;
    data_fdps_number_glyph_sheet_ptr = bar_num_cel;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_all_game_text_ptr = (unsigned char *) bar_text;
    data_fdps_current_chapter_text_ptr = (unsigned char *) bar_chapter_text;
    data_fdps_promotion_table_ptr = bar_promo;
    data_fdps_battle_character_growth_table_ptr = bar_growth;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = bar_wav_bank;
    data_fdps_vga_main_palette_ptr = bar_palette;
    data_fdps_vga_fight_palette_ptr = bar_palette;

    data_fdps_font_sheet_ptr = bar_font;
    data_fdps_font_glyph_width = (unsigned char) BAR_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) BAR_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_glyph_advance_x = BAR_GLYPH_WIDTH;
    data_fdps_font_line_height = BAR_GLYPH_ROWS;

    /* Village mode, so the prompt puts the visible page back behind itself and
       the window close takes its picture out of the backdrop page instead of
       recomposing a battle scene (msgwin.h, statwin.h). */
    data_fdps_village_mode_flag = 1;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_village_status_window_unit_idx = -1;
    data_fdps_unit_status_window_last_tick = -1;
    data_fdps_chapter_current_chapter_id = BAR_CHAPTER_BEFORE_LOCK;

    /* Every audio gate closed, so nothing any of these rows asks for is
       played and the CD poll inside the icon row never reaches the drive. */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (entry = 0; entry < SFX_SAMPLE_SLOT_COUNT; entry++) {
        data_fdps_audio_sample_handle_table[entry] = NULL;
    }

    data_fdps_shared_party_total_gold = BAR_START_GOLD;
    data_fdps_dialog_last_action_text_id_param = BAR_TEXT_SENTINEL;
    data_fdps_dialog_subst_text_id_2 = BAR_SUBST_SENTINEL;
    data_fdps_dialog_last_action_value_param = BAR_VALUE_SENTINEL;
}

/* Back to what a freshly started program has these in.  It is not tidiness:
   the chapter loaders and the shutdown path free most of these pointers
   unguarded, so a case that walked away leaving one of them naming a static in
   this file would hand a later test a free() of storage that never came from
   the heap.  The published page is freed by the screen itself, so the global
   naming it is only cleared. */
static void bar_done(void)
{
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    data_fdps_village_backdrop_page_ptr = NULL;
    data_fdps_roster_array_ptr = NULL;
    data_fdps_roster_member_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_promotion_table_ptr = NULL;
    data_fdps_battle_character_growth_table_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_vga_main_palette_ptr = NULL;
    data_fdps_vga_fight_palette_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_village_mode_flag = 0;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_shared_party_total_gold = 0;
    data_fdps_input_last_scancode = BAR_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = BAR_KEY_NONE;
}

static int bon_carried(int member)
{
    int slot;
    int count;

    count = 0;
    for (slot = 0; slot < BAR_SLOTS; slot++) {
        if ((bar_roster[member].inventory_slots[slot * 2] & BAR_SLOT_EMPTY)
            == 0) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------
 * fdps_run_bonus_lottery @ 00036460
 * ------------------------------------------------------------------ */

/* The one date the draw opens on, CMP EAX,0x7ce at 00036499, CMP EAX,0x1 at
   000364a7 and CMP EAX,0x1c at 000364b3, and the three dates one field off it
   -- each of which has to leave the draw shut. */
#define BON_DRAW_YEAR 1998
#define BON_DRAW_MONTH 1
#define BON_DRAW_DAY 28
#define BON_DAY_BEFORE 27
#define BON_WRONG_MONTH 2
#define BON_WRONG_YEAR 1997

/* Which roster member's bag the cases count.  Every bag bar_stage leaves is
   empty, and a draw of any class but the gold one puts something in all of
   them. */
#define BON_WATCHED_MEMBER 0

/* Runs one draw with the DOS date set to the given day and puts the machine's
   own date back afterwards.  Answers 0 when the date did not take, which every
   case asserts against before it asserts anything else: a case whose date was
   not staged has tested nothing at all, and silence about that is worse than a
   failure.

   THE TIMER INTERRUPT IS INSTALLED EVEN THOUGH A DRAW THAT STAYS SHUT NEVER
   LOOKS AT THE CLOCK.  Every frame the draw presents waits for
   data_fdps_timer_tick_counter to change, and nothing else in these cases
   moves it, so a build whose guard had stopped working would spin inside the
   reel for ever instead of failing an assertion.  With the clock running it
   runs the reel out and the assertions below catch it. */
static int bon_run_on(int year, int month, int day)
{
    struct dosdate_t machine_date;
    struct dosdate_t staged_date;
    struct dosdate_t readback;

    _dos_getdate(&machine_date);
    staged_date.day = (unsigned char) day;
    staged_date.month = (unsigned char) month;
    staged_date.year = (unsigned short) year;
    staged_date.dayofweek = 0;
    _dos_setdate(&staged_date);
    _dos_getdate(&readback);
    if (readback.year != (unsigned short) year
        || readback.month != (unsigned char) month
        || readback.day != (unsigned char) day) {
        _dos_setdate(&machine_date);
        return 0;
    }

    bar_script_len = 0;
    bar_script_next = 0;
    bar_saved_timer = _dos_getvect(BAR_TIMER_VECTOR);
    _dos_setvect(BAR_TIMER_VECTOR, bar_timer_isr);
    fdps_run_bonus_lottery();
    _dos_setvect(BAR_TIMER_VECTOR, bar_saved_timer);

    _dos_setdate(&machine_date);
    return 1;
}

/* The run-once flag is tested before the date is, and it alone shuts the draw
   on the very day it would otherwise open: CMP dword ptr [0x00064110],0x0 /
   JNZ at 0003648a jumps to the same JMP 0x36ae2 the three date tests fall to.
   Nothing is drawn, nothing is handed out and the flag is not written a second
   time.

   The gold and the bag are what say no prize was awarded: the gold arm adds
   20,000 (ADD dword ptr [0x000643a4],0x4e20 at 000369b5) and all three of the
   others put an item into every roster member's bag, so a draw that had run
   would have moved one of the two whichever arm the unwritten slot picked. */
static void bon_a_draw_already_taken_is_not_taken_again(void)
{
    int date_staged;

    bar_stage();
    data_fdps_bonus_lottery_drawn_flag = 1;

    date_staged = bon_run_on(BON_DRAW_YEAR, BON_DRAW_MONTH, BON_DRAW_DAY);
    CHECK_EQ(date_staged, 1);
    if (date_staged == 0) {
        bar_done();
        return;
    }

    CHECK_EQ(data_fdps_bonus_lottery_drawn_flag, 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, BAR_START_GOLD);
    CHECK_EQ(bon_carried(BON_WATCHED_MEMBER), 0);
    bar_done();
}

/* One day early is not the day: the day compare is the last of the three and
   the only one that separates this from the draw, and failing it leaves the
   run-once flag CLEAR -- the store MOV dword ptr [0x00064110],0x1 at 00036ad8
   is the last instruction of the body and is not reached, so tomorrow's draw
   is still available. */
static void bon_the_day_before_draws_nothing(void)
{
    int date_staged;

    bar_stage();
    data_fdps_bonus_lottery_drawn_flag = 0;

    date_staged = bon_run_on(BON_DRAW_YEAR, BON_DRAW_MONTH, BON_DAY_BEFORE);
    CHECK_EQ(date_staged, 1);
    if (date_staged == 0) {
        bar_done();
        return;
    }

    CHECK_EQ(data_fdps_bonus_lottery_drawn_flag, 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, BAR_START_GOLD);
    CHECK_EQ(bon_carried(BON_WATCHED_MEMBER), 0);
    bar_done();
}

/* The same day of the same year in the wrong month, which is the middle of the
   three compares and the one the short-circuit chain reaches second. */
static void bon_the_wrong_month_draws_nothing(void)
{
    int date_staged;

    bar_stage();
    data_fdps_bonus_lottery_drawn_flag = 0;

    date_staged = bon_run_on(BON_DRAW_YEAR, BON_WRONG_MONTH, BON_DRAW_DAY);
    CHECK_EQ(date_staged, 1);
    if (date_staged == 0) {
        bar_done();
        return;
    }

    CHECK_EQ(data_fdps_bonus_lottery_drawn_flag, 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, BAR_START_GOLD);
    CHECK_EQ(bon_carried(BON_WATCHED_MEMBER), 0);
    bar_done();
}

/* And the right day of the right month one year early, which is the first of
   the three compares and the one that reads the header's u16 rather than a
   byte.  The draw is a single day of a single year and not an anniversary. */
static void bon_the_wrong_year_draws_nothing(void)
{
    int date_staged;

    bar_stage();
    data_fdps_bonus_lottery_drawn_flag = 0;

    date_staged = bon_run_on(BON_WRONG_YEAR, BON_DRAW_MONTH, BON_DRAW_DAY);
    CHECK_EQ(date_staged, 1);
    if (date_staged == 0) {
        bar_done();
        return;
    }

    CHECK_EQ(data_fdps_bonus_lottery_drawn_flag, 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, BAR_START_GOLD);
    CHECK_EQ(bon_carried(BON_WATCHED_MEMBER), 0);
    bar_done();
}
void run_vilbar_tests(void)
{
    RUN_TEST(bon_a_draw_already_taken_is_not_taken_again);
    RUN_TEST(bon_the_day_before_draws_nothing);
    RUN_TEST(bon_the_wrong_month_draws_nothing);
    RUN_TEST(bon_the_wrong_year_draws_nothing);
}
