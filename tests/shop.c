/* tests/shop.c -- cover for src/shop.c.
 *
 * Expected values come from the assembly at 00031700 and from the SHOP%02d.DAT
 * members the shipped FIELD.VFS actually holds; none of them is read off the
 * emitted C.
 *
 * The stock table is not a file the reader opens -- it is a VFS member that
 * fdps_load_field_chapter_resources parks in data_fdps_shop_stock_table_ptr --
 * so a test installs the member's own 36 bytes behind that pointer rather than
 * staging a file.  The byte rows below are transcribed from the unpacked
 * FIELD.VFS members and are the real ones, not invented stand-ins.
 *
 * ---- fdps_shop_select_item, 00032710 -------------------------------------
 *
 * The picker cases at the end come from the assembly at 00032710 -- the entry
 * test CMP EAX,dword ptr [EBP-0x24] / JL at 0003273b that clears BOTH globals
 * only when the saved cursor is at or past the stock count; the four guarded
 * arrow arms at 00032795 (0x4d, DEC EAX / CMP / JG against the count),
 * 000327d9 (0x4b, CMP ...,0x0 / JG), 00032817 (0x48, CMP ...,0x1 / JG) and
 * 00032853 (0x50, SUB EAX,0x2 / CMP / JG); the window steps ADD
 * [0x000601a8],0x2 behind a compare of top + 6 against the cursor and SUB
 * [0x000601a8],0x2 behind a compare of the cursor against the top; the two
 * confirm codes 0x1c and 0x39 at 0003277d and 00032783; and the epilogue CMP
 * dword ptr [EBP-0x10],0x0 / JLE at 00032a35 that turns anything but a
 * confirmation into -1.  None of it is read off the emitted C.
 *
 * HOW A MODAL LOOP IS RUN FROM OUTSIDE.  fdps_shop_select_item takes one
 * number, answers one number, and everything between is drawing and
 * keystrokes.  So every picker case stages the sheets and tables the frame is
 * drawn through, installs a timer interrupt that both advances the game's
 * clock and plays the case's keys, calls, and asserts on the number that came
 * back and on the two globals the picker leaves behind.
 *
 * WHY THE KEYS ARE PLAYED INTO THE SCANCODE LATCH AND NOT THE RING.  The
 * picker reads through fdps_read_scancode_auto_repeat, which answers from
 * data_fdps_input_last_scancode alone and throws the ring away on every poll
 * (keybd.h).  The ISR therefore writes the latch, and only when the filter has
 * already picked up what is there -- data_fdps_input_key_repeat_prev_scancode
 * equal to the latch is exactly that -- so one code is presented per poll
 * however many ticks a frame happens to take.
 *
 * WHY TWO EQUAL KEYS IN A ROW NEED A FILLER BETWEEN THEM.  The filter reports
 * a code because it CHANGED; an unchanged latch is a held key and is silenced
 * until the repeat delay elapses.  PICKER_KEY_NONE is the filter's own 0xff
 * no-key answer, which the picker's chain ignores, so a script puts one
 * between two identical presses and the second arrives as a change.
 *
 * PAST THE END OF A SCRIPT THE ISR PLAYS ESCAPE FOREVER, so a run that got
 * away from its script cancels and fails an assertion instead of hanging the
 * whole test image.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Nothing reads the frame back.  Every sprite
 * staged below is a skip-only RLE stream (rle.h) that steps its destination
 * and writes no pixel, which is deliberate twice over: the drawing is not this
 * function's behaviour, and the down arrow's fixed 25x22 block at page offset
 * 0x4e96 runs off the end of the 0x5ca0 page the pass allocated (shop.c), so a
 * sprite that really painted would corrupt this test image's heap rather than
 * draw a wrong picture.  Which entry the selection bar sits on is still
 * pinned, because the picker answers with the id under the cursor.
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
#include "shop.h"

/* SHOP01.DAT: row 0 the item shop, row 1 the weapon shop, row 2 the secret
   shop.  The weapon row is the canonical "hole between two stocked entries"
   case and the item row is the canonical "id above 0x7f" case. */
static unsigned char shop01_table[36] = {
    0xb4, 0xde, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x02, 0x71, 0xff, 0xff, 0x72, 0x73, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xb5, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* SHOP03.DAT: its weapon row opens ON an empty slot, so a scan that treated
   0xff as the terminator would report this shop as stocking nothing at all. */
static unsigned char shop03_table[36] = {
    0xb4, 0xb5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x03, 0xff, 0x1e, 0x1f, 0x30, 0x64, 0x65, 0x74, 0x75, 0x82, 0x83,
    0xb5, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* SHOP00.DAT: the item row runs to nine ids before its holes, and both the
   weapon and secret rows are twelve bytes of 0x00 -- a live item id, not a
   terminator and not an empty slot. */
static unsigned char shop00_table[36] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x64, 0xc8, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Every slot empty: the loop still runs its twelve iterations and writes
   nothing. */
static unsigned char empty_table[36] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* -2 is not a byte the row can produce, so an untouched element is
   distinguishable from anything the function could have written. */
static void fill_unwritten(int *out_item_ids)
{
    int slot;

    for (slot = 0; slot < 12; slot++) {
        out_item_ids[slot] = -2;
    }
}

/* The weapon row 02 71 FF FF 72 73 FF FF FF FF FF FF: the pair of holes at
   slots 2 and 3 is stepped over, not stopped on, and the two ids behind them
   land at destination indices 2 and 3.  The destination index is the write
   counter and not the slot, so the answer is packed. */
static void shop_skips_holes_between_stock(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
    CHECK_EQ(stock[0], 0x02);
    CHECK_EQ(stock[1], 0x71);
    CHECK_EQ(stock[2], 0x72);
    CHECK_EQ(stock[3], 0x73);
    /* Nothing is written past the count: the store at 0003175b is inside the
       taken branch and the counter never reached 4 for a fifth slot. */
    CHECK_EQ(stock[4], -2);
}

/* SHOP03.DAT's weapon row FF 03 FF 1E 1F 30 64 65 74 75 82 83: a leading hole
   and a second one at slot 2, then ten stocked slots.  Treating 0xff as the
   row terminator answers 0 here. */
static void shop_leading_hole_does_not_end_row(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop03_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 10);
    CHECK_EQ(stock[0], 0x03);
    CHECK_EQ(stock[1], 0x1e);
    CHECK_EQ(stock[9], 0x83);
}

/* SHOP01.DAT's item row B4 DE: the byte is read zero-extended (XOR EAX,EAX /
   MOV AL,byte ptr [EDX] at 00031739), so the ids are 180 and 222.  Read
   through a signed char pointer they would be -76 and -34, and the 0xff slots
   after them would compare as -1 and be sold as stock. */
static void shop_ids_above_7f_are_unsigned(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 2);
    CHECK_EQ(stock[0], 180);
    CHECK_EQ(stock[1], 222);
}

/* The row base is shop_index * 0xc (IMUL EAX,dword ptr [EBP+0x14],0xc at
   0003172a), so index 2 reads the third row and not the third byte. */
static void shop_index_selects_a_row_of_twelve(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(2, stock), 2);
    CHECK_EQ(stock[0], 0xb5);
    CHECK_EQ(stock[1], 0xb7);
}

/* An all-0xff row writes nothing and returns 0 -- the count the picker at
   00032710 compares its cursor against. */
static void shop_empty_row_returns_zero(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = empty_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 0);
    CHECK_EQ(stock[0], -2);
}

/* SHOP00.DAT's weapon row is twelve 0x00 bytes.  0x00 is a live item id and
   the only value the loop treats specially is 0xff, so all twelve are written
   and the caller's array needs room for twelve ints. */
static void shop_full_row_writes_twelve(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop00_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 12);
    CHECK_EQ(stock[0], 0);
    CHECK_EQ(stock[11], 0);
}

/* SHOP00.DAT's item row 00 01 02 03 04 05 06 64 C8 FF FF FF: nine ids, the
   last two of them 100 and 200, then trailing holes that contribute nothing. */
static void shop_trailing_holes_contribute_nothing(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop00_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 9);
    CHECK_EQ(stock[7], 100);
    CHECK_EQ(stock[8], 200);
    CHECK_EQ(stock[9], -2);
}

/* The table is only ever loaded from -- there is no store through the row
   pointer anywhere in the function -- so the chapter's stock image survives a
   collect and the three shops can be walked in any order. */
static void shop_does_not_modify_the_table(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
    CHECK_EQ(shop01_table[12], 0x02);
    CHECK_EQ(shop01_table[14], 0xff);
    CHECK_EQ(shop01_table[17], 0x73);
    /* And a second call over the same row answers the same thing. */
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
}

/* ---- fdps_shop_select_item ---------------------------------------------- */

/* A stock table built for the picker rather than transcribed from a shipped
   one: the three rows stock 8, 4 and 2 entries, which are the three list
   lengths the guards need -- longer than the six visible cells, an even list
   that fits on one page, and a list shorter than a saved cursor.  The ids are
   this file's own inputs; what the picker does with them is index arithmetic
   and it never looks at what an id means. */
#define PICKER_LONG_SHOP 0
#define PICKER_LONG_COUNT 8
#define PICKER_MID_SHOP 1
#define PICKER_MID_COUNT 4
#define PICKER_SHORT_SHOP 2

static unsigned char picker_table[36] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xff, 0xff, 0xff, 0xff,
    0x11, 0x12, 0x13, 0x14, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x21, 0x22, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* The make codes the picker's chain knows, and the filter's no-key answer. */
#define PICKER_KEY_ESC 0x01
#define PICKER_KEY_ENTER 0x1c
#define PICKER_KEY_SPACE 0x39
#define PICKER_KEY_UP 0x48
#define PICKER_KEY_LEFT 0x4b
#define PICKER_KEY_RIGHT 0x4d
#define PICKER_KEY_DOWN 0x50
#define PICKER_KEY_NONE 0xff

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the picker spins on the tick counter. */
#define PICKER_TIMER_VECTOR 8

/* The adapter and the two modes a case moves between. */
#define PICKER_MODE_TEXT 0x03
#define PICKER_MODE_320X200X256 0x13

/* The shared .CEL.  One sheet satisfies all three sprite globals the frame is
   drawn through: fdps_cel_blit_sprite takes the size out of the header's i16
   pair at +0x07 and +0x09 (sprite.h) for the window frame and the selection
   bar, while fdps_blit_command_sprite passes its own fixed 25 by 22 and reads
   only the offset table (sprite.h).  Every stream is one skip run per row --
   the op is the top two bits and the low six carry len-1, so 0xc0 | 24 skips
   exactly the 25 pixels the row must account for (rle.h) -- and therefore no
   sprite writes anything anywhere.

   THE RUN HAS TO ACCOUNT FOR EXACTLY 25 PIXELS.  A row ends when its covered
   pixels reach the width exactly and nothing bounds the stream, so a short run
   never closes the row and the decoder walks off the end of this array.

   0x48 entries because the highest index asked for is 0x47, the lit frame of
   the down arrow. */
#define PICKER_CEL_SPRITES 0x48
#define PICKER_CEL_TABLE_AT 0x0f
#define PICKER_CEL_W 25
#define PICKER_CEL_H 22
#define PICKER_CEL_SKIP_CMD (0xc0 | (PICKER_CEL_W - 1))
#define PICKER_CEL_STREAMS_AT (PICKER_CEL_TABLE_AT + PICKER_CEL_SPRITES * 4)
#define PICKER_CEL_BYTES \
    (PICKER_CEL_STREAMS_AT + PICKER_CEL_SPRITES * PICKER_CEL_H)

/* Number.cel's shape: thirteen glyphs of 6 by 8 in the colour row
   fdps_draw_number selects with a colour row of zero (text.h), skip-only for
   the same reason as above -- 6 pixels to a row means 0xc0 | 5. */
#define PICKER_NUM_SPRITES 13
#define PICKER_NUM_W 6
#define PICKER_NUM_H 8
#define PICKER_NUM_SKIP_CMD (0xc0 | (PICKER_NUM_W - 1))
#define PICKER_NUM_STREAMS_AT (PICKER_CEL_TABLE_AT + PICKER_NUM_SPRITES * 4)
#define PICKER_NUM_BYTES \
    (PICKER_NUM_STREAMS_AT + PICKER_NUM_SPRITES * PICKER_NUM_H)

/* The text block.  fdps_draw_text takes a table of signed 16-bit byte offsets
   measured from the block's own base and walks the stream it points at until
   the token -1 (text.h), so a table whose every entry names one lone
   terminator draws nothing and needs no font staged.  The entry the shop's
   item name lives at is 0xc9 + item_id (shopdraw.c) and the highest id in the
   table above is 0x22. */
#define PICKER_TEXT_ENTRIES 0x100
#define PICKER_TEXT_TERMINATOR (-1)

/* ITEM.DAT records, 0x17 bytes each (table.c), covering every id the stock
   table names.  They are left zeroed: a zero type takes the entry painter's
   armour arm and every figure it draws is 0, which is a picture nothing here
   reads.  Nothing asserts what any of these fields holds -- ticket 23 owns the
   real table. */
#define PICKER_ITEM_STRIDE 0x17
#define PICKER_ITEM_RECORDS 0x24

/* An empty container: the lookup walks no entries and answers NULL, so
   fdps_play_sfx finds no clip and starts no sample (audio.c). */
#define PICKER_WAV_BANK_BYTES 16

/* How many codes one script can hold. */
#define PICKER_SCRIPT_MAX 8

static unsigned char picker_cel[PICKER_CEL_BYTES];
static unsigned char picker_num_cel[PICKER_NUM_BYTES];
static short picker_text[PICKER_TEXT_ENTRIES + 1];
static unsigned char picker_items[PICKER_ITEM_STRIDE * PICKER_ITEM_RECORDS];
static unsigned char picker_wav_bank[PICKER_WAV_BANK_BYTES];

static unsigned char picker_script[PICKER_SCRIPT_MAX];
static int picker_script_len;
static int picker_script_next;
static void (__interrupt __far *picker_saved_timer)();

/* Advances the game's clock the way fdps_timer_tick_handler does, and presents
   the next code of the script to the auto-repeat filter -- but only once the
   filter has taken the one already there, which is what an equal previous
   scancode and latch mean (keybd.h).  One code per poll, however long a frame
   takes.  Past the end of the script escape and the no-key value alternate, so
   a runaway run cancels rather than hangs. */
static void __interrupt __far picker_timer_isr(void)
{
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (picker_script_next < picker_script_len) {
            code = picker_script[picker_script_next];
            picker_script_next++;
        } else if (data_fdps_input_last_scancode == PICKER_KEY_ESC) {
            code = PICKER_KEY_NONE;
        } else {
            code = PICKER_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    _chain_intr(picker_saved_timer);
}

static void picker_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void picker_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void picker_build_cel(unsigned char *image, int sprites, int width,
                             int height, int streams_at, int skip_cmd)
{
    int sprite_id;
    int row;
    int stream_at;

    image[0] = 'C';
    image[1] = 'E';
    image[2] = 'L';
    picker_u16(image, 0x03, 1);
    picker_u16(image, 0x05, 0);
    picker_u16(image, 0x07, (unsigned int) width);
    picker_u16(image, 0x09, (unsigned int) height);
    picker_u16(image, 0x0b, (unsigned int) sprites);
    picker_u16(image, 0x0d, 2);

    for (sprite_id = 0; sprite_id < sprites; sprite_id++) {
        stream_at = streams_at + sprite_id * height;
        picker_u32(image, PICKER_CEL_TABLE_AT + sprite_id * 4,
                   (unsigned long) stream_at);
        for (row = 0; row < height; row++) {
            image[stream_at + row] = (unsigned char) skip_cmd;
        }
    }
}

static void picker_stage(void)
{
    int entry;

    memset(picker_cel, 0, sizeof(picker_cel));
    picker_build_cel(picker_cel, PICKER_CEL_SPRITES, PICKER_CEL_W,
                     PICKER_CEL_H, PICKER_CEL_STREAMS_AT,
                     PICKER_CEL_SKIP_CMD);
    memset(picker_num_cel, 0, sizeof(picker_num_cel));
    picker_build_cel(picker_num_cel, PICKER_NUM_SPRITES, PICKER_NUM_W,
                     PICKER_NUM_H, PICKER_NUM_STREAMS_AT,
                     PICKER_NUM_SKIP_CMD);

    for (entry = 0; entry < PICKER_TEXT_ENTRIES; entry++) {
        picker_text[entry] = (short) (PICKER_TEXT_ENTRIES * 2);
    }
    picker_text[PICKER_TEXT_ENTRIES] = PICKER_TEXT_TERMINATOR;

    memset(picker_items, 0, sizeof(picker_items));
    memset(picker_wav_bank, 0, sizeof(picker_wav_bank));

    data_fdps_shop_stock_table_ptr = picker_table;
    data_fdps_village_window_sheet_ptr = picker_cel;
    data_fdps_selection_bar_sheet_ptr = picker_cel;
    data_fdps_command_sprite_sheet_ptr = picker_cel;
    data_fdps_number_glyph_sheet_ptr = picker_num_cel;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_all_game_text_ptr = (unsigned char *) picker_text;
    data_fdps_item_effect_table_ptr = picker_items;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = picker_wav_bank;

    /* Both halves of fdps_cd_music_repeat_poll's inner guard, so the poll the
       picker makes on every pass never reaches the drive (cdaudio.c). */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
}

/* Back to the state a freshly started program has these in.  It is not
   tidiness: the chapter loaders and the shutdown path free most of these
   pointers unguarded, so a case that walked away leaving one of them naming a
   static in this file would hand a later test a free() of storage that never
   came from the heap. */
static void picker_unstage(void)
{
    data_fdps_shop_stock_table_ptr = NULL;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
}

static void picker_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole run of the picker, with the adapter in the mode the game draws it
   in and the timer interrupt both pacing the frames and playing the keys.  The
   two picker globals are NOT reset here: where the cursor and the window start
   is what several cases are about, so each case sets them itself. */
static int picker_run(int shop_index, unsigned char *codes, int count)
{
    int answer;
    int index;

    picker_stage();

    for (index = 0; index < count; index++) {
        picker_script[index] = codes[index];
    }
    picker_script_len = count;
    picker_script_next = 0;

    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    picker_set_mode(PICKER_MODE_320X200X256);
    picker_saved_timer = _dos_getvect(PICKER_TIMER_VECTOR);
    _dos_setvect(PICKER_TIMER_VECTOR, picker_timer_isr);
    answer = fdps_shop_select_item(shop_index);
    _dos_setvect(PICKER_TIMER_VECTOR, picker_saved_timer);
    picker_set_mode(PICKER_MODE_TEXT);

    return answer;
}

static void picker_place(int cursor, int top)
{
    data_fdps_shop_item_picker_cursor_idx = cursor;
    data_fdps_shop_item_list_scroll_offset = top;
}

/* Escape stores -1 in the loop result at 00032771 and the epilogue's JLE hands
   that same -1 back.  It moves neither global: the cancel arm is a single
   store and a jump to the frame. */
static void picker_escape_cancels(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ESC;
    picker_place(1, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 1), -1);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 0);
    picker_unstage();
}

/* Enter stores 1 and the epilogue answers with the id the cursor stands on,
   read out of the picker's own packed copy of the row and not out of the table
   -- the weapon row's third stocked id is 0x13. */
static void picker_enter_answers_the_entry_under_the_cursor(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    picker_place(2, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 1), 0x13);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 2);
    picker_unstage();
}

/* 0x39 is the second confirm code, tested at 00032783 and joining Enter's
   store: space picks the entry exactly as Enter does. */
static void picker_space_confirms_like_enter(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_SPACE;
    picker_place(1, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 1), 0x12);
    picker_unstage();
}

/* Right steps one entry and left steps one back, so two rights and a left
   leave the cursor on entry 1.  The filler between the two rights is what
   makes the second one a change the repeat filter reports. */
static void picker_right_and_left_step_one(void)
{
    unsigned char script[5];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_NONE;
    script[2] = PICKER_KEY_RIGHT;
    script[3] = PICKER_KEY_LEFT;
    script[4] = PICKER_KEY_ENTER;
    picker_place(0, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 5), 0x12);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    picker_unstage();
}

/* The right guard is count-1 > cursor, so the last entry of the four is where
   right stops: nothing moves and nothing is played. */
static void picker_right_stops_on_the_last_entry(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_ENTER;
    picker_place(PICKER_MID_COUNT - 1, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x14);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, PICKER_MID_COUNT - 1);
    picker_unstage();
}

/* The left guard is cursor > 0, so the first entry is where left stops. */
static void picker_left_stops_on_the_first_entry(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_LEFT;
    script[1] = PICKER_KEY_ENTER;
    picker_place(0, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x11);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 0);
    picker_unstage();
}

/* Down is a whole row and the list is two columns wide, so it adds two. */
static void picker_down_steps_a_row(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_DOWN;
    script[1] = PICKER_KEY_ENTER;
    picker_place(0, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x13);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 2);
    picker_unstage();
}

/* Up subtracts the same two. */
static void picker_up_steps_a_row(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_UP;
    script[1] = PICKER_KEY_ENTER;
    picker_place(3, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x12);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    picker_unstage();
}

/* The down guard is count-2 > cursor, which needs a whole row still ahead --
   with four entries and the cursor on 2 there is none, so down is blocked even
   though entry 3 exists and right would reach it. */
static void picker_down_needs_a_whole_row_ahead(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_DOWN;
    script[1] = PICKER_KEY_ENTER;
    picker_place(2, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x13);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 2);
    picker_unstage();
}

/* The up guard is cursor > 1, so the top row of two is where up stops. */
static void picker_up_stops_on_the_top_row(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_UP;
    script[1] = PICKER_KEY_ENTER;
    picker_place(1, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 2), 0x12);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    picker_unstage();
}

/* The window follows only when the cursor reaches top + 6: over the eight-entry
   row the first two downs leave it at 0 and the third, which puts the cursor on
   entry 6, steps it to 2. */
static void picker_window_follows_the_cursor_forward(void)
{
    unsigned char script[6];

    script[0] = PICKER_KEY_DOWN;
    script[1] = PICKER_KEY_NONE;
    script[2] = PICKER_KEY_DOWN;
    script[3] = PICKER_KEY_NONE;
    script[4] = PICKER_KEY_DOWN;
    script[5] = PICKER_KEY_ENTER;
    picker_place(0, 0);
    CHECK_EQ(picker_run(PICKER_LONG_SHOP, script, 6), 0x07);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 6);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 2);
    picker_unstage();
}

/* And back only when the cursor drops below the top: from entry 6 with the
   window at 2 the first two ups leave it there and the third, which puts the
   cursor on entry 0, steps it back to 0. */
static void picker_window_follows_the_cursor_back(void)
{
    unsigned char script[6];

    script[0] = PICKER_KEY_UP;
    script[1] = PICKER_KEY_NONE;
    script[2] = PICKER_KEY_UP;
    script[3] = PICKER_KEY_NONE;
    script[4] = PICKER_KEY_UP;
    script[5] = PICKER_KEY_ENTER;
    picker_place(6, 2);
    CHECK_EQ(picker_run(PICKER_LONG_SHOP, script, 6), 0x01);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 0);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 0);
    picker_unstage();
}

/* The window step is not the arrow key's: a single right that crosses top + 6
   moves the window a whole row, so entry 6 becomes the top left cell's
   neighbour rather than sitting one line off the bottom. */
static void picker_right_can_scroll_the_window(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_ENTER;
    picker_place(5, 0);
    CHECK_EQ(picker_run(PICKER_LONG_SHOP, script, 2), 0x07);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 2);
    picker_unstage();
}

/* And a single left that drops below the top moves it back the same row. */
static void picker_left_can_scroll_the_window(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_LEFT;
    script[1] = PICKER_KEY_ENTER;
    picker_place(2, 2);
    CHECK_EQ(picker_run(PICKER_LONG_SHOP, script, 2), 0x02);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 0);
    picker_unstage();
}

/* The entry test is the one thing that clears the pair, and it fires on a
   saved cursor at or past this shop's stock count: entering the two-entry
   secret shop with the cursor left on 6 opens it on the first entry with the
   window home. */
static void picker_cursor_past_the_stock_resets_both(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    picker_place(6, 2);
    CHECK_EQ(picker_run(PICKER_SHORT_SHOP, script, 1), 0x21);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 0);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 0);
    picker_unstage();
}

/* The test is on the cursor alone.  A cursor of 1 is inside the four-entry
   weapon row, so nothing is reset and a window left at 2 -- which shows
   entries 2 through 5 and so does not show the cursor at all -- survives the
   entry untouched. */
static void picker_window_is_not_clamped_on_its_own(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    picker_place(1, 2);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, script, 1), 0x12);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    CHECK_EQ(data_fdps_shop_item_list_scroll_offset, 2);
    picker_unstage();
}

/* Neither global is seeded on entry, so the picker reopens where the last
   visit left it: a run that moves the cursor to 1 and confirms is followed by
   a run that only confirms, and the second answers the same id. */
static void picker_cursor_survives_between_visits(void)
{
    unsigned char first[2];
    unsigned char second[1];

    first[0] = PICKER_KEY_RIGHT;
    first[1] = PICKER_KEY_ENTER;
    second[0] = PICKER_KEY_ENTER;

    picker_place(0, 0);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, first, 2), 0x12);
    CHECK_EQ(picker_run(PICKER_MID_SHOP, second, 1), 0x12);
    CHECK_EQ(data_fdps_shop_item_picker_cursor_idx, 1);
    picker_unstage();
}

/* ---- fdps_shop_select_buy_target, 00032c40 -------------------------------
 *
 * Expected values come from the assembly at 00032c40 -- the type test XOR
 * EAX,EAX / MOV AL,byte ptr [EDX] at 00032c6e with CMP ...,0x0 / JZ at
 * 00032c73 and CMP ...,0x27 / JLE at 00032c79, and the CALL 0x000336d0 / MOV
 * dword ptr [EBP-0x4],EAX at 00032c7f behind them; the two cancel codes 0x01
 * and 0x53 at 00032ca3 and 00032ca9; the two confirm codes 0x1c and 0x39 at
 * 00032cbb and 00032cc1 behind the gate CMP dword ptr [0x00069cf4],0x17 / JL
 * and CMP dword ptr [0x000601b4],0x3 / JZ at 00032cc7; the four guarded arrow
 * arms at 00032ce5 (0x4d, DEC EAX / CMP / JG against the member count),
 * 00032da5 (0x4b, CMP ...,0x0 / JG), 00032e5d (0x48, CMP ...,0x2 / JG) and
 * 00032f05 (0x50, SUB EAX,0x3 / CMP / JG); the window steps ADD
 * [0x000601b0],0x3 behind a compare of top + 3 against the cursor and SUB
 * [0x000601b0],0x3 behind a compare of the cursor against the top, with the
 * vertical arms moving both globals unconditionally; and the epilogue CMP
 * dword ptr [EBP-0x1c],0x0 / JLE at 000330b2 that turns anything but a
 * confirmation into -1.  What the village grid answers for a non-equipment
 * item is vilmenu.h's contract.  None of it is read off the emitted C.
 *
 * THE TIMER SCRIPT AND THE FRAME STAGING ARE THE ONES THE ITEM PICKER ABOVE
 * USES, for the same reasons: the ISR both advances the game's clock and feeds
 * the auto-repeat filter one code per poll, and every sprite staged is a
 * skip-only RLE stream so no draw writes a pixel anywhere.
 *
 * EVERY SCRIPT IS PLAYED BEHIND ONE FILLER CODE, and that is not tidiness.  A
 * move that changes the visible row draws its slide from the entry grid the
 * PREVIOUS pass allocated, and on the very first pass there is no previous one
 * -- the original reads an uninitialised stack slot there and gets away with it
 * because the real game reaches this screen with a matching block already on
 * the near heap (shop.c).  A test image has no such block, so the filler buys
 * the loop one pass to allocate a real grid before any case's first key can
 * reach a slide.
 *
 * WHY NO MEMBER CAN EQUIP THE ITEM.  The class equipment table is left zeroed
 * and the offered item's type is 3, so fdps_unit_can_equip_item answers 0 for
 * every member (unititem.h) and each entry takes the cannot-equip arm, which
 * draws two messages and no figures.  That is one branch of
 * fdps_shop_draw_member_entry rather than both, and it is deliberate: which
 * branch the entry painter takes is shopdraw.c's behaviour and is covered
 * there, while here it only has to be harmless.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Nothing reads the grid or the screen back.
 * The picker answers one number and leaves two globals behind, and those three
 * are what every case below is about.
 */

/* Eight members is one more than two full rows of three, which is what the
   guards need: a last member right can stop on, a row up cannot leave, and a
   cursor with entries ahead of it but not a whole row. */
#define BT_MEMBER_COUNT 8

/* The slot the confirm gate locks out and the chapter index that starts it,
   with one chapter either side of the boundary. */
#define BT_LOCKED_SLOT 3
#define BT_CHAPTER_BEFORE_LOCK 0x16
#define BT_CHAPTER_AT_LOCK 0x17

/* Delete, the cancel code the item picker above does not take. */
#define BT_KEY_DELETE 0x53

/* The ITEM.DAT records the cases offer.  Type 3 is inside the 0x01-0x27
   equipment band; 0x27 and 0x28 are the two sides of the upper bound; and type
   0 is the value that leaves on the zero test before the bound is reached. */
#define BT_ITEM_COUNT 8
#define BT_EQUIP_ITEM 5
#define BT_EQUIP_TYPE 3
#define BT_LAST_EQUIP_ITEM 6
#define BT_LAST_EQUIP_TYPE 0x27
#define BT_CONSUMABLE_ITEM 7
#define BT_CONSUMABLE_TYPE 0x28
#define BT_TYPE_ZERO_ITEM 4

/* Where the village grid is left standing for the non-equipment cases.  It is
   deliberately not where the shop's own cursor is: the answer is what says
   which of the two pickers ran. */
#define BT_VILLAGE_CURSOR 5
#define BT_VILLAGE_SCROLL 3

/* The class equipment table.  Four rows is more than the zeroed roster's class
   0 needs, and every row is left at zero so no class may wear type 3. */
#define BT_CLASS_COUNT 4

/* The walking-icon cache.  Its offset table starts at the block's own base with
   no header (rsrc.h), it holds twelve streams per member, and the entry painter
   indexes it by roster slot.  Each stream is a 24-row skip of 24 pixels, the
   0x18 by 0x18 fdps_blit_dispatch is told to draw. */
#define BT_ICON_SPRITES_PER_SLOT 0x0c
#define BT_ICON_SPRITES (BT_MEMBER_COUNT * BT_ICON_SPRITES_PER_SLOT)
#define BT_ICON_SIDE 24
#define BT_ICON_SKIP_CMD (0xc0 | (BT_ICON_SIDE - 1))
#define BT_ICON_STREAMS_AT (BT_ICON_SPRITES * 4)
#define BT_ICON_BYTES (BT_ICON_STREAMS_AT + BT_ICON_SPRITES * BT_ICON_SIDE)

/* The text block, sized past 0x1f7 -- the cannot-equip line, which is the
   highest message the entry painter asks for.  Every entry names one lone
   terminator, so drawing any of them paints nothing and no font is needed. */
#define BT_TEXT_ENTRIES 0x200
#define BT_TEXT_TERMINATOR (-1)

/* How many codes one buy-target script holds, the filler included. */
#define BT_SCRIPT_MAX 8

static unsigned char bt_icon_cache[BT_ICON_BYTES];
static short bt_text[BT_TEXT_ENTRIES + 1];
static struct fdps_unit_record bt_roster[BT_MEMBER_COUNT];
static struct fdps_item_effect bt_items[BT_ITEM_COUNT];
static struct fdps_class_equip_record bt_class_equip[BT_CLASS_COUNT];

/* The sprite cache's own table, which starts at the base and carries no header
   of any kind. */
static void bt_build_icon_cache(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(bt_icon_cache, 0, sizeof(bt_icon_cache));
    for (sprite_id = 0; sprite_id < BT_ICON_SPRITES; sprite_id++) {
        stream_at = BT_ICON_STREAMS_AT + sprite_id * BT_ICON_SIDE;
        picker_u32(bt_icon_cache, sprite_id * 4, (unsigned long) stream_at);
        for (row = 0; row < BT_ICON_SIDE; row++) {
            bt_icon_cache[stream_at + row] = (unsigned char) BT_ICON_SKIP_CMD;
        }
    }
}

static void bt_stage(void)
{
    int slot;
    int entry;

    memset(picker_cel, 0, sizeof(picker_cel));
    picker_build_cel(picker_cel, PICKER_CEL_SPRITES, PICKER_CEL_W,
                     PICKER_CEL_H, PICKER_CEL_STREAMS_AT,
                     PICKER_CEL_SKIP_CMD);
    bt_build_icon_cache();

    /* No member carries character id 1, so no entry takes the ghosting arm and
       the chapter the case picks decides the confirm gate and nothing else. */
    memset(bt_roster, 0, sizeof(bt_roster));
    for (slot = 0; slot < BT_MEMBER_COUNT; slot++) {
        bt_roster[slot].char_id = (unsigned char) (slot + 2);
    }

    memset(bt_items, 0, sizeof(bt_items));
    bt_items[BT_EQUIP_ITEM].type = (unsigned char) BT_EQUIP_TYPE;
    bt_items[BT_LAST_EQUIP_ITEM].type = (unsigned char) BT_LAST_EQUIP_TYPE;
    bt_items[BT_CONSUMABLE_ITEM].type = (unsigned char) BT_CONSUMABLE_TYPE;
    memset(bt_class_equip, 0, sizeof(bt_class_equip));

    for (entry = 0; entry < BT_TEXT_ENTRIES; entry++) {
        bt_text[entry] = (short) (BT_TEXT_ENTRIES * 2);
    }
    bt_text[BT_TEXT_ENTRIES] = BT_TEXT_TERMINATOR;

    memset(picker_wav_bank, 0, sizeof(picker_wav_bank));

    /* The village points the map unit array at the roster block, which is what
       lets fdps_unit_can_equip_item reach the same record the entry painter
       does (unititem.h). */
    data_fdps_roster_array_ptr = (unsigned char *) bt_roster;
    data_fdps_map_unit_array_ptr = (unsigned char *) bt_roster;
    data_fdps_roster_member_count = BT_MEMBER_COUNT;
    data_fdps_item_effect_table_ptr = (unsigned char *) bt_items;
    data_fdps_class_equip_table_ptr = (unsigned char *) bt_class_equip;
    data_fdps_village_window_sheet_ptr = picker_cel;
    data_fdps_selection_bar_sheet_ptr = picker_cel;
    data_fdps_command_sprite_sheet_ptr = picker_cel;
    data_fdps_cel_sprite_cache_ptr = bt_icon_cache;
    data_fdps_all_game_text_ptr = (unsigned char *) bt_text;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = picker_wav_bank;

    /* Both halves of fdps_cd_music_repeat_poll's inner guard, so the poll the
       picker makes on every pass never reaches the drive (cdaudio.c). */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
}

/* Back to the state a freshly started program has these in: the chapter loaders
   and the shutdown path free most of these pointers unguarded, so a case that
   walked away leaving one of them naming a static in this file would hand a
   later test a free() of storage that never came from the heap. */
static void bt_unstage(void)
{
    data_fdps_roster_array_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_roster_member_count = 0;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_class_equip_table_ptr = NULL;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
}

/* One whole run of the buy-target picker, with the adapter in the mode the game
   draws it in and the timer interrupt both pacing the frames and playing the
   keys.  The case's codes go behind one filler for the reason at the head of
   this section.  Neither picker's globals are reset here: where the cursor and
   the window start is what several cases are about. */
static int bt_run(int chapter, int item_id, unsigned char *codes, int count)
{
    int answer;
    int index;

    bt_stage();
    data_fdps_chapter_current_chapter_id = chapter;

    picker_script[0] = PICKER_KEY_NONE;
    for (index = 0; index < count; index++) {
        picker_script[index + 1] = codes[index];
    }
    picker_script_len = count + 1;
    picker_script_next = 0;

    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    picker_set_mode(PICKER_MODE_320X200X256);
    picker_saved_timer = _dos_getvect(PICKER_TIMER_VECTOR);
    _dos_setvect(PICKER_TIMER_VECTOR, picker_timer_isr);
    answer = fdps_shop_select_buy_target(item_id);
    _dos_setvect(PICKER_TIMER_VECTOR, picker_saved_timer);
    picker_set_mode(PICKER_MODE_TEXT);

    return answer;
}

static void bt_place(int cursor, int top)
{
    data_fdps_shop_buy_target_cursor_idx = cursor;
    data_fdps_shop_buy_target_scroll_offset = top;
}

/* And where the OTHER picker is left standing, so a non-equipment case can tell
   which of the two answered. */
static void bt_place_village(int cursor, int top)
{
    data_fdps_village_member_select_cursor_idx = cursor;
    data_fdps_village_member_grid_scroll_offset = top;
}

/* An item of type 3 is equipment, so this file's own picker runs: escape
   stores -1 at 00032caf and the epilogue's JLE hands that same -1 back.  It
   moves neither global. */
static void bt_escape_cancels(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ESC;
    bt_place(2, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 1), -1);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 2);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* 0x53 is the second cancel code and joins escape's store at 00032caf, which is
   the one difference from the item picker's key chain.  The enter behind it is
   what makes the case discriminating: 0x53 ends the loop on its own pass, so
   the enter is never read and the answer is -1.  Were the 0x53 test at 00032ca9
   absent the code would match no arm, the next pass would take the enter, and
   the confirm would answer the cursor. */
static void bt_delete_cancels_like_escape(void)
{
    unsigned char script[2];

    script[0] = BT_KEY_DELETE;
    script[1] = PICKER_KEY_ENTER;
    bt_place(2, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), -1);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 2);
    bt_unstage();
}

/* Enter stores 1 and the epilogue answers with the cursor itself -- a roster
   index, not a position in the visible row. */
static void bt_enter_answers_the_cursor(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(4, 3);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 1), 4);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 4);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 3);
    bt_unstage();
}

/* 0x39 is the second confirm code, tested at 00032cc1 and joining enter's
   store. */
static void bt_space_confirms_like_enter(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_SPACE;
    bt_place(1, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 1), 1);
    bt_unstage();
}

/* Type 0x27 is the last equipment type -- the bound is JLE -- so it still gets
   this picker, and the answer is the SHOP's cursor while the village grid's is
   left where it stands. */
static void bt_type_27_is_still_equipment(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(1, 0);
    bt_place_village(BT_VILLAGE_CURSOR, BT_VILLAGE_SCROLL);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_LAST_EQUIP_ITEM, script, 1), 1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, BT_VILLAGE_CURSOR);
    bt_unstage();
}

/* Type 0x28 is one past the bound, so the whole screen is handed to the village
   grid and its answer comes back unchanged: the reply is the VILLAGE cursor and
   the shop's own cursor is never read. */
static void bt_type_28_goes_to_the_village_grid(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(1, 0);
    bt_place_village(BT_VILLAGE_CURSOR, BT_VILLAGE_SCROLL);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_CONSUMABLE_ITEM, script, 1),
             BT_VILLAGE_CURSOR);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 1);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* Type 0 leaves on the zero test, which is the branch BEFORE the bound, and
   lands on the same village grid. */
static void bt_type_zero_goes_to_the_village_grid(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(1, 0);
    bt_place_village(BT_VILLAGE_CURSOR, BT_VILLAGE_SCROLL);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_TYPE_ZERO_ITEM, script, 1),
             BT_VILLAGE_CURSOR);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 1);
    bt_unstage();
}

/* A cancel on the village grid is this function's cancel: the answer is stored
   and returned unchanged, with no test of its value on the way through. */
static void bt_village_grid_cancel_is_passed_through(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ESC;
    bt_place(1, 0);
    bt_place_village(BT_VILLAGE_CURSOR, BT_VILLAGE_SCROLL);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_CONSUMABLE_ITEM, script, 1),
             -1);
    bt_unstage();
}

/* Right steps one entry and left steps one back, and neither moves the window
   while the cursor stays inside the three entries on show. */
static void bt_right_and_left_step_one(void)
{
    unsigned char script[5];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_NONE;
    script[2] = PICKER_KEY_RIGHT;
    script[3] = PICKER_KEY_LEFT;
    script[4] = PICKER_KEY_ENTER;
    bt_place(0, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 5), 1);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* The right guard is count-1 > cursor, so the last member is where right stops:
   nothing moves and nothing is played. */
static void bt_right_stops_on_the_last_member(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_ENTER;
    bt_place(BT_MEMBER_COUNT - 1, 6);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2),
             BT_MEMBER_COUNT - 1);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 6);
    bt_unstage();
}

/* The left guard is cursor > 0, so the first member is where left stops. */
static void bt_left_stops_on_the_first_member(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_LEFT;
    script[1] = PICKER_KEY_ENTER;
    bt_place(0, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 0);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* The window follows only when the cursor reaches top + 3, and it moves a whole
   row when it does: a single right off entry 2 puts the cursor on 3 and steps
   the window from 0 to 3. */
static void bt_right_can_scroll_the_window(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_ENTER;
    bt_place(2, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 3);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 3);
    bt_unstage();
}

/* And back only when the cursor drops below the top: from entry 3 with the
   window at 3, one left puts the cursor on 2 and the window back to 0. */
static void bt_left_can_scroll_the_window(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_LEFT;
    script[1] = PICKER_KEY_ENTER;
    bt_place(3, 3);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 2);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* Down is a whole row and the row is three wide, so it adds three -- and it
   carries the window with it unconditionally, without the test the horizontal
   moves make. */
static void bt_down_steps_three_and_the_window_with_it(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_DOWN;
    script[1] = PICKER_KEY_ENTER;
    bt_place(1, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 4);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 3);
    bt_unstage();
}

/* Up subtracts the same three from both. */
static void bt_up_steps_three_and_the_window_with_it(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_UP;
    script[1] = PICKER_KEY_ENTER;
    bt_place(4, 3);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 1);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* The up guard is cursor > 2, so the top row of three is where up stops -- and
   the boundary is the cursor and not the window. */
static void bt_up_stops_on_the_top_row(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_UP;
    script[1] = PICKER_KEY_ENTER;
    bt_place(2, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 2);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 0);
    bt_unstage();
}

/* The down guard is count-3 > cursor, which needs a whole row still ahead --
   with eight members and the cursor on 5 there is none, so down is blocked even
   though entries 6 and 7 exist and right would reach them. */
static void bt_down_needs_a_whole_row_ahead(void)
{
    unsigned char script[2];

    script[0] = PICKER_KEY_DOWN;
    script[1] = PICKER_KEY_ENTER;
    bt_place(5, 3);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 2), 5);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 3);
    bt_unstage();
}

/* From chapter index 0x17 on, a confirm with the cursor on roster slot 3 is
   dropped: the loop result is left at 0 and the picker keeps running, so the
   escape the ISR plays past the end of the script is what ends the call and the
   answer is a cancel.  The cursor is still standing on the slot. */
static void bt_locked_slot_cannot_be_confirmed(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(BT_LOCKED_SLOT, BT_LOCKED_SLOT);
    CHECK_EQ(bt_run(BT_CHAPTER_AT_LOCK, BT_EQUIP_ITEM, script, 1), -1);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, BT_LOCKED_SLOT);
    bt_unstage();
}

/* Space is refused on the same slot: both confirm codes share the one gate. */
static void bt_locked_slot_refuses_space_too(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_SPACE;
    bt_place(BT_LOCKED_SLOT, BT_LOCKED_SLOT);
    CHECK_EQ(bt_run(BT_CHAPTER_AT_LOCK, BT_EQUIP_ITEM, script, 1), -1);
    bt_unstage();
}

/* One chapter earlier the same slot confirms normally: the gate is the chapter
   index and not the member. */
static void bt_locked_slot_is_confirmable_before_the_chapter(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(BT_LOCKED_SLOT, BT_LOCKED_SLOT);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 1),
             BT_LOCKED_SLOT);
    bt_unstage();
}

/* And in the locked chapter every other slot confirms: the gate is one slot
   compared for equality, not a range. */
static void bt_locked_chapter_blocks_only_slot_three(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(BT_LOCKED_SLOT + 1, BT_LOCKED_SLOT);
    CHECK_EQ(bt_run(BT_CHAPTER_AT_LOCK, BT_EQUIP_ITEM, script, 1),
             BT_LOCKED_SLOT + 1);
    bt_unstage();
}

/* The locked slot is still walked over: right off entry 2 stops on it and right
   again leaves it, so only the confirm is refused. */
static void bt_locked_slot_is_still_walked_over(void)
{
    unsigned char script[4];

    script[0] = PICKER_KEY_RIGHT;
    script[1] = PICKER_KEY_NONE;
    script[2] = PICKER_KEY_RIGHT;
    script[3] = PICKER_KEY_ENTER;
    bt_place(2, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_AT_LOCK, BT_EQUIP_ITEM, script, 4), 4);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 3);
    bt_unstage();
}

/* Neither global is seeded on entry, so the picker reopens where the last visit
   left it: a run that moves the cursor to 1 and confirms is followed by a run
   that only confirms, and the second answers the same member. */
static void bt_cursor_survives_between_visits(void)
{
    unsigned char first[2];
    unsigned char second[1];

    first[0] = PICKER_KEY_RIGHT;
    first[1] = PICKER_KEY_ENTER;
    second[0] = PICKER_KEY_ENTER;

    bt_place(0, 0);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, first, 2), 1);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, second, 1), 1);
    CHECK_EQ(data_fdps_shop_buy_target_cursor_idx, 1);
    bt_unstage();
}

/* There is no entry test of any kind, so a window left past the cursor is not
   tidied up on the way in -- unlike the item picker above, which at least
   clears both when its cursor is off the end of the stock. */
static void bt_window_is_not_seeded_on_entry(void)
{
    unsigned char script[1];

    script[0] = PICKER_KEY_ENTER;
    bt_place(1, 6);
    CHECK_EQ(bt_run(BT_CHAPTER_BEFORE_LOCK, BT_EQUIP_ITEM, script, 1), 1);
    CHECK_EQ(data_fdps_shop_buy_target_scroll_offset, 6);
    bt_unstage();
}

/* ---- fdps_shop_buy_loop, 00033b80 ----------------------------------------
 *
 * Expected values come from the assembly at 00033b80 and from nothing else:
 * the price store MOV AX,word ptr [EAX+0x13] / AND EAX,0xffff / MOV
 * [0x00064038],EAX at 00033cb1; the trade-in guard CMP dword ptr
 * [EBP-0x24],-0x1 / JZ at 00033cc2 with the SECOND CALL to
 * fdps_unit_can_equip_item behind it at 00033cd0; the want_armor choice CMP
 * dword ptr [EBP-0x2c],0x15 / JG at 00033c84; the credit LEA
 * EDX,[EDX+EDX*0x2] / SAR EDX,0x1f / SHL EDX,0x2 / SBB EAX,EDX / SAR EAX,0x2
 * and the SUB dword ptr [0x00064038],EAX at 00033d83; the bag test CMP
 * byte ptr [EBP-0x4],0x0 / JNZ at 00033d89 in FRONT of CALL
 * fdps_unit_item_count / CMP EAX,0x8 / JZ; the balance chain CMP dword ptr
 * [0x00064038],0x0 / JLE at 00033dd9 and CMP EAX,dword ptr [0x000643a4] / JG
 * at 00033deb; the two purses SUB dword ptr [0x000643a4],EAX at 00033e3d and
 * ADD dword ptr [0x000643a4],EAX at 00033ee8 behind NEG dword ptr
 * [0x00064038] at 00033e98; the two substitution stores MOV AL,byte ptr
 * [EAX+0x8] / INC EAX / MOV [0x00064030],EAX at 00033d19 and ADD EAX,0xc9 /
 * MOV [0x00064034],EAX at 00033d2a; and the settled-payment block at 00033f11,
 * whose fdps_unit_equip_slot is handed fdps_unit_item_count(target) - 1.
 * None of it is read off the emitted C.
 *
 * HOW A LOOP THIS DEEP IS RUN FROM OUTSIDE.  fdps_shop_buy_loop answers
 * nothing at all: everything it does is in the purse, in three dialogue
 * globals and in one member's inventory record, and everything it asks is
 * asked through two modal pickers and up to two modal prompts.  So every case
 * below stages the sheets and tables all four of those screens draw through,
 * places both pickers' cursors on the entries the case wants chosen, installs
 * a timer interrupt that advances the game's clock and answers both input
 * channels, calls, and then reads the purse, the globals and the record.
 *
 * THE TWO INPUT CHANNELS ARE SEPARATE AND THE INTERRUPT SERVES BOTH.  The two
 * pickers read through fdps_read_scancode_auto_repeat, which answers from
 * data_fdps_input_last_scancode and THROWS THE RING AWAY on every poll;
 * fdps_prompt_two_choice reads the ring itself and flushes it once on entry
 * (keybd.h, msgwin.h).  The handler therefore plays the picker keys into the
 * latch exactly as tests above do, and the prompt answers into the ring -- but
 * it may only advance the answer list when an answer was really READ, never
 * when a picker's poll merely flushed it.  It can tell the two apart because a
 * read moves the ring's read index and a flush moves the write index back onto
 * it: the handler remembers the read index it pushed at, and advances only
 * when that index has since changed.  With that one test the answer list holds
 * one entry per prompt and needs no fillers, and the many hundreds of picker
 * polls in between cost it nothing.
 *
 * WHY EVERY PICKER KEY HAS A FILLER BEHIND IT.  The auto-repeat filter reports
 * a code because it CHANGED, so two confirms in a row need something between
 * them; 0xff is the filter's own no-key answer and neither picker has an arm
 * for it.  Past the end of a script the handler alternates Escape and 0xff, so
 * a run that got away from its script cancels out of the item list and fails
 * an assertion instead of hanging the test image.
 *
 * NO CASE PRESSES AN ARROW IN THE BUY-TARGET PICKER, and that is not laziness.
 * A move that changes the visible row draws its slide out of the entry grid
 * the PREVIOUS pass allocated, and on the first pass there is no previous one
 * (shop.c).  Both cursors are placed before the call instead, which is what
 * the two pickers' surviving globals are for.
 *
 * THE WINDOW SHEET IS A REAL PICTURE HERE AND THE OTHER SHEETS ARE NOT.
 * fdps_village_animate_window_zoom scales sprite 0 of the village window sheet
 * into a full 320x200 page nine times per sweep, so that one sheet is a flat
 * 312 x 76 fill of the size the opened window is -- the shape tests/village.c
 * proves the scaler handles.  Every other sheet staged is a skip-only RLE
 * stream that steps its destination and writes no pixel, which keeps the item
 * picker's deliberately overrunning down arrow (shop.c) from writing past the
 * page it is drawn into.
 *
 * WHAT IS NOT ASSERTED, AND WHY.  Nothing is read back off the screen: which
 * of the five messages was drawn, and where the 0x26-row shift put it, is
 * fdps_draw_text's behaviour over a text block this file stages as empty
 * entries.  What the messages are ABOUT is still pinned, because each of them
 * stands on its own arm of the balance chain and the arms have different
 * effects on the purse and the bag.
 */

/* Four members is one more than the buy-target picker's row of three, so a
   cursor can be placed anywhere in the list without a slide ever being asked
   for. */
#define BUY_MEMBER_COUNT 4

/* Chapter index below fdps_shop_select_buy_target's locked-slot boundary
   (0x17), so every roster slot can be confirmed and the chapter decides
   nothing else here. */
#define BUY_CHAPTER 0x10

/* The two class rows of the equipment table.  Row 0 names the two types the
   cases offer, so a member of that class may wear either; row 1 is left at
   0xff, which no item type is, so a member of that class may wear nothing. */
#define BUY_CLASS_COUNT 2
#define BUY_CLASS_EQUIPS_BOTH 0
#define BUY_CLASS_EQUIPS_NOTHING 1

/* Which roster slot carries the class that can wear nothing.  It is what makes
   the auto-equip flag and the trade-in guard observable: both are the answer
   of fdps_unit_can_equip_item for the member being bought for. */
#define BUY_UNSKILLED_MEMBER 1
#define BUY_SKILLED_MEMBER 0

/* The ITEM.DAT records the cases trade in and buy.  The old weapon's price is
   deliberately not a multiple of four -- 101 * 3 / 4 is 75.75 -- so the credit
   pins the truncation and not just the ratio.  The old armour's credit, 750,
   is larger than the new armour's 300 price, which is the only way to reach
   the negate-and-pay-out arm. */
#define BUY_ITEM_RECORDS 16
#define BUY_NEW_WEAPON 5
#define BUY_NEW_WEAPON_TYPE 3
#define BUY_NEW_WEAPON_PRICE 500
#define BUY_OLD_WEAPON 6
#define BUY_OLD_WEAPON_TYPE 3
#define BUY_OLD_WEAPON_PRICE 101
#define BUY_OLD_WEAPON_CREDIT 75
#define BUY_NEW_ARMOUR 7
#define BUY_NEW_ARMOUR_TYPE 0x18
#define BUY_NEW_ARMOUR_PRICE 300
#define BUY_OLD_ARMOUR 8
#define BUY_OLD_ARMOUR_TYPE 0x18
#define BUY_OLD_ARMOUR_PRICE 1000
#define BUY_OLD_ARMOUR_CREDIT 750
#define BUY_FILLER_ITEM 9
#define BUY_FILLER_TYPE 3
#define BUY_FILLER_PRICE 10

/* Where those ids sit in the shop's stock row, which is what the item picker's
   cursor selects between. */
#define BUY_SHOP 0
#define BUY_STOCK_NEW_WEAPON 0
#define BUY_STOCK_NEW_ARMOUR 2

/* One unit record's inventory: eight two-byte entries, a flag byte and an id
   byte each, with 0x80 for empty and 0x40 for equipped (unititem.h). */
#define BUY_SLOTS 8
#define BUY_SLOT_EMPTY 0x80
#define BUY_SLOT_CARRIED 0
#define BUY_SLOT_EQUIPPED 0x40

/* The text block, sized past 0x213 -- the cannot-afford line, which is the
   highest entry this loop asks for.  Every entry names one lone terminator, so
   drawing any of them paints nothing and no font is needed. */
#define BUY_TEXT_ENTRIES 0x220
#define BUY_TEXT_TERMINATOR (-1)

/* The number sheet needs four colour rows and not one: the buy-target picker's
   entry painter stores 0, 2 and 3 into data_fdps_number_glyph_color_row before
   its figures, and fdps_draw_number reaches the glyph's offset at
   (row * 13 + glyph) * 4 + 0x0f (text.c), so a sheet with one row's worth of
   table would read a stream byte as an offset. */
#define BUY_NUM_ROWS 4
#define BUY_NUM_GLYPHS 13
#define BUY_NUM_SPRITES (BUY_NUM_ROWS * BUY_NUM_GLYPHS)
#define BUY_NUM_STREAMS_AT (PICKER_CEL_TABLE_AT + BUY_NUM_SPRITES * 4)
#define BUY_NUM_BYTES (BUY_NUM_STREAMS_AT + BUY_NUM_SPRITES * PICKER_NUM_H)

/* The village window sheet: one sprite the size of the opened window, a flat
   fill, spelled as four 64-pixel runs and one of 56 because a run carries at
   most 64 (resource_info/cel.md).  It is the only sheet here that paints. */
#define BUY_WIN_SPRITES 1
#define BUY_WIN_W 0x138
#define BUY_WIN_H 0x4c
#define BUY_WIN_TABLE_AT 0x0f
#define BUY_WIN_STREAM_AT (BUY_WIN_TABLE_AT + (BUY_WIN_SPRITES + 1) * 4)
#define BUY_WIN_CMD_64 0x3f
#define BUY_WIN_CMD_56 0x37
#define BUY_WIN_FULL_RUNS 4
#define BUY_WIN_ROW_BYTES ((BUY_WIN_FULL_RUNS + 1) * 2)
#define BUY_WIN_BYTES (BUY_WIN_STREAM_AT + BUY_WIN_H * BUY_WIN_ROW_BYTES)
#define BUY_WIN_COLOR 0x51

/* The caller's own screen page, which the sweeps only ever read. */
#define BUY_SCREEN_BYTES 0xfa00

/* How many picker keys and how many prompt answers one case may script. */
#define BUY_SCRIPT_MAX 12
#define BUY_REPLY_MAX 4

/* A value parked in the dialogue figure global before a run that must not
   reach the store at 00033cb1. */
#define BUY_VALUE_SENTINEL 0x5a5a

static unsigned char buy_table[36] = {
    BUY_NEW_WEAPON, BUY_OLD_WEAPON, BUY_NEW_ARMOUR, BUY_OLD_ARMOUR,
    BUY_FILLER_ITEM, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static unsigned char buy_num_cel[BUY_NUM_BYTES];
static unsigned char buy_win_sheet[BUY_WIN_BYTES];
static short buy_text[BUY_TEXT_ENTRIES + 1];
static struct fdps_unit_record buy_roster[BUY_MEMBER_COUNT];
static struct fdps_item_effect buy_items[BUY_ITEM_RECORDS];
static struct fdps_class_equip_record buy_class_equip[BUY_CLASS_COUNT];
static unsigned char *buy_screen_page;

static unsigned char buy_keys[BUY_SCRIPT_MAX];
static int buy_key_len;
static int buy_key_next;
static unsigned char buy_replies[BUY_REPLY_MAX];
static int buy_reply_len;
static int buy_reply_next;
static int buy_reply_pending;
static int buy_reply_head_at_push;
static void (__interrupt __far *buy_saved_timer)();

/* Advances the game's clock and answers both input channels.
   The latch half is the picker script, presented only once the filter has
   taken the code already there.  The ring half is the prompt answer list: an
   answer is pushed whenever the ring is empty, and the list only moves on when
   the read index has changed since the push, which is what separates a
   prompt's read from a picker poll's flush (see the section head). */
static void __interrupt __far buy_timer_isr(void)
{
    int slot;
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (buy_key_next < buy_key_len) {
            code = buy_keys[buy_key_next];
            buy_key_next++;
        } else if (data_fdps_input_last_scancode == PICKER_KEY_ESC) {
            code = PICKER_KEY_NONE;
        } else {
            code = PICKER_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        if (buy_reply_pending != 0
            && data_fdps_input_scancode_queue_head != buy_reply_head_at_push) {
            buy_reply_next++;
        }
        if (buy_reply_next < buy_reply_len) {
            code = buy_replies[buy_reply_next];
        } else {
            code = PICKER_KEY_ESC;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = code;
        buy_reply_head_at_push = data_fdps_input_scancode_queue_head;
        buy_reply_pending = 1;
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(buy_saved_timer);
}

/* One flat sprite of 0x138 by 0x4c behind a header that states that size, and
   an offset table whose entries are measured from the start of the file. */
static void buy_build_window_sheet(void)
{
    struct fdps_cel_header *header;
    int row;
    int run;
    int at;

    memset(buy_win_sheet, 0, sizeof(buy_win_sheet));
    header = (struct fdps_cel_header *) buy_win_sheet;
    header->magic[0] = 'C';
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->sprite_width = BUY_WIN_W;
    header->sprite_height = BUY_WIN_H;
    header->sprite_count = BUY_WIN_SPRITES;

    at = BUY_WIN_STREAM_AT;
    picker_u32(buy_win_sheet, BUY_WIN_TABLE_AT, (unsigned long) at);
    picker_u32(buy_win_sheet, BUY_WIN_TABLE_AT + 4,
               (unsigned long) BUY_WIN_BYTES);
    for (row = 0; row < BUY_WIN_H; row++) {
        for (run = 0; run <= BUY_WIN_FULL_RUNS; run++) {
            if (run < BUY_WIN_FULL_RUNS) {
                buy_win_sheet[at + row * BUY_WIN_ROW_BYTES + run * 2] =
                    BUY_WIN_CMD_64;
            } else {
                buy_win_sheet[at + row * BUY_WIN_ROW_BYTES + run * 2] =
                    BUY_WIN_CMD_56;
            }
            buy_win_sheet[at + row * BUY_WIN_ROW_BYTES + run * 2 + 1] =
                BUY_WIN_COLOR;
        }
    }
}

/* Everything the four screens draw through, with every member's bag empty and
   both pickers' globals left for the case to place. */
static void buy_stage(void)
{
    int entry;
    int member;
    int slot;

    memset(picker_cel, 0, sizeof(picker_cel));
    picker_build_cel(picker_cel, PICKER_CEL_SPRITES, PICKER_CEL_W,
                     PICKER_CEL_H, PICKER_CEL_STREAMS_AT,
                     PICKER_CEL_SKIP_CMD);
    memset(buy_num_cel, 0, sizeof(buy_num_cel));
    picker_build_cel(buy_num_cel, BUY_NUM_SPRITES, PICKER_NUM_W,
                     PICKER_NUM_H, BUY_NUM_STREAMS_AT, PICKER_NUM_SKIP_CMD);
    bt_build_icon_cache();
    buy_build_window_sheet();

    /* No member carries character id 1, so no entry takes the buy-target
       painter's ghosting arm and the chapter decides nothing. */
    memset(buy_roster, 0, sizeof(buy_roster));
    for (member = 0; member < BUY_MEMBER_COUNT; member++) {
        buy_roster[member].char_id = (unsigned char) (member + 2);
        if (member == BUY_UNSKILLED_MEMBER) {
            buy_roster[member].clazz = (unsigned char) BUY_CLASS_EQUIPS_NOTHING;
        } else {
            buy_roster[member].clazz = (unsigned char) BUY_CLASS_EQUIPS_BOTH;
        }
        for (slot = 0; slot < BUY_SLOTS; slot++) {
            buy_roster[member].inventory_slots[slot * 2] =
                (unsigned char) BUY_SLOT_EMPTY;
            buy_roster[member].inventory_slots[slot * 2 + 1] = 0;
        }
    }

    memset(buy_items, 0, sizeof(buy_items));
    buy_items[BUY_NEW_WEAPON].type = (unsigned char) BUY_NEW_WEAPON_TYPE;
    buy_items[BUY_NEW_WEAPON].price = (unsigned short) BUY_NEW_WEAPON_PRICE;
    buy_items[BUY_OLD_WEAPON].type = (unsigned char) BUY_OLD_WEAPON_TYPE;
    buy_items[BUY_OLD_WEAPON].price = (unsigned short) BUY_OLD_WEAPON_PRICE;
    buy_items[BUY_NEW_ARMOUR].type = (unsigned char) BUY_NEW_ARMOUR_TYPE;
    buy_items[BUY_NEW_ARMOUR].price = (unsigned short) BUY_NEW_ARMOUR_PRICE;
    buy_items[BUY_OLD_ARMOUR].type = (unsigned char) BUY_OLD_ARMOUR_TYPE;
    buy_items[BUY_OLD_ARMOUR].price = (unsigned short) BUY_OLD_ARMOUR_PRICE;
    buy_items[BUY_FILLER_ITEM].type = (unsigned char) BUY_FILLER_TYPE;
    buy_items[BUY_FILLER_ITEM].price = (unsigned short) BUY_FILLER_PRICE;

    memset(buy_class_equip, 0xff, sizeof(buy_class_equip));
    buy_class_equip[BUY_CLASS_EQUIPS_BOTH].allowed_item_type[0] =
        (unsigned char) BUY_NEW_WEAPON_TYPE;
    buy_class_equip[BUY_CLASS_EQUIPS_BOTH].allowed_item_type[1] =
        (unsigned char) BUY_NEW_ARMOUR_TYPE;

    for (entry = 0; entry < BUY_TEXT_ENTRIES; entry++) {
        buy_text[entry] = (short) (BUY_TEXT_ENTRIES * 2);
    }
    buy_text[BUY_TEXT_ENTRIES] = BUY_TEXT_TERMINATOR;

    memset(picker_wav_bank, 0, sizeof(picker_wav_bank));

    data_fdps_shop_stock_table_ptr = buy_table;
    data_fdps_village_window_sheet_ptr = buy_win_sheet;
    data_fdps_selection_bar_sheet_ptr = picker_cel;
    data_fdps_command_sprite_sheet_ptr = picker_cel;
    data_fdps_shadow_sprite_sheet_ptr = picker_cel;
    data_fdps_number_glyph_sheet_ptr = buy_num_cel;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_all_game_text_ptr = (unsigned char *) buy_text;
    data_fdps_item_effect_table_ptr = (unsigned char *) buy_items;
    data_fdps_class_equip_table_ptr = (unsigned char *) buy_class_equip;
    data_fdps_cel_sprite_cache_ptr = bt_icon_cache;

    /* The village phase points the map unit array at the roster block, which
       is what lets the fdps_unit_* accessors reach the record
       fdps_get_roster_record hands back (shop.c). */
    data_fdps_roster_array_ptr = (unsigned char *) buy_roster;
    data_fdps_map_unit_array_ptr = (unsigned char *) buy_roster;
    data_fdps_roster_member_count = BUY_MEMBER_COUNT;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = picker_wav_bank;
    data_fdps_chapter_current_chapter_id = BUY_CHAPTER;

    /* Village mode, so the prompt puts the visible page back behind itself
       instead of recomposing a battle scene (msgwin.h), and no portrait, so
       the sweep's unconditional release has nothing to give back. */
    data_fdps_village_mode_flag = 1;
    data_fdps_portrait_sprite_buf_ptr = NULL;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;

    /* Both halves of fdps_cd_music_repeat_poll's inner guard, so the poll the
       pickers make on every pass never reaches the drive (cdaudio.c). */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
}

/* Back to the state a freshly started program has these in: the chapter
   loaders and the shutdown path free most of these pointers unguarded, so a
   case that walked away leaving one of them naming a static here would hand a
   later test a free of storage that never came from the heap. */
static void buy_unstage(void)
{
    data_fdps_shop_stock_table_ptr = NULL;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_shadow_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_class_equip_table_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_roster_array_ptr = NULL;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_roster_member_count = 0;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_village_mode_flag = 0;
    data_fdps_portrait_sprite_buf_ptr = NULL;
    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
}

static void buy_put(int member, int slot, int flag, int item_id)
{
    buy_roster[member].inventory_slots[slot * 2] = (unsigned char) flag;
    buy_roster[member].inventory_slots[slot * 2 + 1] = (unsigned char) item_id;
}

static int buy_flag(int member, int slot)
{
    return (int) buy_roster[member].inventory_slots[slot * 2];
}

static int buy_id(int member, int slot)
{
    return (int) buy_roster[member].inventory_slots[slot * 2 + 1];
}

/* One whole visit to a shop, with the adapter in the mode the game runs it in,
   both pickers standing where the case put them, and the timer interrupt
   answering both channels. */
static void buy_go(int item_cursor, int target_cursor,
                   unsigned char *keys, int key_count,
                   unsigned char *replies, int reply_count)
{
    int index;

    buy_screen_page = (unsigned char *) malloc((size_t) BUY_SCREEN_BYTES);
    CHECK_EQ(buy_screen_page != NULL, 1);
    if (buy_screen_page == NULL) {
        return;
    }
    memset(buy_screen_page, 0, (size_t) BUY_SCREEN_BYTES);

    data_fdps_shop_item_picker_cursor_idx = item_cursor;
    data_fdps_shop_item_list_scroll_offset = 0;
    data_fdps_shop_buy_target_cursor_idx = target_cursor;
    data_fdps_shop_buy_target_scroll_offset =
        (target_cursor / 3) * 3;

    for (index = 0; index < key_count; index++) {
        buy_keys[index] = keys[index];
    }
    buy_key_len = key_count;
    buy_key_next = 0;
    for (index = 0; index < reply_count; index++) {
        buy_replies[index] = replies[index];
    }
    buy_reply_len = reply_count;
    buy_reply_next = 0;
    buy_reply_pending = 0;
    buy_reply_head_at_push = 0;

    data_fdps_input_last_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = PICKER_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    picker_set_mode(PICKER_MODE_320X200X256);
    buy_saved_timer = _dos_getvect(PICKER_TIMER_VECTOR);
    _dos_setvect(PICKER_TIMER_VECTOR, buy_timer_isr);
    fdps_shop_buy_loop(buy_screen_page, (unsigned char) BUY_SHOP);
    _dos_setvect(PICKER_TIMER_VECTOR, buy_saved_timer);
    picker_set_mode(PICKER_MODE_TEXT);

    free(buy_screen_page);
    buy_screen_page = NULL;
}

/* The picker script for one purchase and then a cancel out of the item list:
   confirm the item, confirm the member, then Escape the item list.  The 0xff
   between each pair is the filler the auto-repeat filter needs to see a
   change. */
static unsigned char buy_keys_one_buy[5] = {
    PICKER_KEY_ENTER, PICKER_KEY_NONE,
    PICKER_KEY_ENTER, PICKER_KEY_NONE,
    PICKER_KEY_ESC
};

/* ---------------------------------------------------------------------- */

/* The price the pass publishes is the offered item's own 16-bit price field,
   and an accepted purchase charges exactly that: the store at 00033cb1 and the
   SUB at 00033e3d are one number.  The bought item lands in the first free
   entry and the auto-equip flag, latched from the first
   fdps_unit_can_equip_item at 00033c4d, puts the equipped bit on it. */
static void buy_charges_the_price_and_hands_over_the_item(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000 - BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_NEW_WEAPON);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 1), BUY_SLOT_EMPTY);
    buy_unstage();
}

/* A declined prompt leaves the flag at 00033f11 clear, so not one of the four
   inventory calls behind it runs and the purse is untouched.  The price is
   still published, because that store is in front of the whole chain. */
static void buy_declined_offer_moves_nothing(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ESC;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EMPTY);
    buy_unstage();
}

/* CMP EAX,dword ptr [0x000643a4] / JG at 00033deb refuses only when the price
   is GREATER than the purse, so a purse of exactly the price buys and leaves
   nothing behind. */
static void buy_a_purse_equal_to_the_price_is_enough(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = BUY_NEW_WEAPON_PRICE;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, 0);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_NEW_WEAPON);
    buy_unstage();
}

/* One coin short takes the other side of that branch: message 0x213 is drawn
   and NO prompt is put up at all, so the answer list is never touched and
   nothing moves. */
static void buy_a_purse_one_short_refuses(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = BUY_NEW_WEAPON_PRICE - 1;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, BUY_NEW_WEAPON_PRICE - 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EMPTY);
    buy_unstage();
}

/* The trade-in.  101 * 3 / 4 is 75 and not 76, so the balance is 425 and the
   purse pays 425; the traded entry is dropped by fdps_unit_remove_item before
   the bought one is added, so the bag holds one thing at the end.  The two
   substitution globals are the offer's own arithmetic: the member's name is
   its character id plus one and the old item's is its id plus 0xc9. */
static void buy_trade_in_credits_three_quarters_truncated(void)
{
    unsigned char replies[2];

    replies[0] = PICKER_KEY_ENTER;
    replies[1] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_SKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_WEAPON);
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 2);
    CHECK_EQ(data_fdps_dialog_last_action_value_param,
             BUY_NEW_WEAPON_PRICE - BUY_OLD_WEAPON_CREDIT);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             1000 - (BUY_NEW_WEAPON_PRICE - BUY_OLD_WEAPON_CREDIT));
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             (int) buy_roster[BUY_SKILLED_MEMBER].char_id + 1);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, BUY_OLD_WEAPON + 0xc9);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_NEW_WEAPON);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 1), BUY_SLOT_EMPTY);
    buy_unstage();
}

/* Declining the trade-in leaves the balance at the full price and leaves the
   old item in the bag: the credit is subtracted inside the accepted arm at
   00033d66 and nowhere else.  The old weapon is still taken OFF, because
   fdps_unit_equip_slot unequips whatever is worn in the same category
   (unititem.h) -- that is the auto-equip's doing and not the trade-in's. */
static void buy_declined_trade_in_leaves_the_price_alone(void)
{
    unsigned char replies[2];

    replies[0] = PICKER_KEY_ESC;
    replies[1] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_SKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_WEAPON);
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 2);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000 - BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_OLD_WEAPON);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 1), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 1), BUY_NEW_WEAPON);
    buy_unstage();
}

/* A credit larger than the price sends the balance below zero, which NEG dword
   ptr [0x00064038] at 00033e98 turns into what the shop pays out: 1000 * 3 / 4
   is 750, the armour costs 300, and an accepted prompt ADDS the 450 difference
   to the purse instead of taking anything out of it. */
static void buy_a_credit_above_the_price_is_paid_out(void)
{
    unsigned char replies[2];

    replies[0] = PICKER_KEY_ENTER;
    replies[1] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_SKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_ARMOUR);
    buy_go(BUY_STOCK_NEW_ARMOUR, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 2);
    CHECK_EQ(data_fdps_dialog_last_action_value_param,
             BUY_OLD_ARMOUR_CREDIT - BUY_NEW_ARMOUR_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             1000 + (BUY_OLD_ARMOUR_CREDIT - BUY_NEW_ARMOUR_PRICE));
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_NEW_ARMOUR);
    buy_unstage();
}

/* want_armor is chosen from the OFFERED item's type and nothing else: a weapon
   asks fdps_unit_find_equipped_slot for the equipped weapon, so a member
   wearing armour and no weapon gets no offer and pays the full price.  Were
   the test the other way round the armour would be offered, the credit would
   be 750 and the purse would go UP rather than down. */
static void buy_a_weapon_ignores_the_armour_being_worn(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_SKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_ARMOUR);
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000 - BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_OLD_ARMOUR);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 1), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 1), BUY_NEW_WEAPON);
    buy_unstage();
}

/* A member already holding eight things and trading nothing in is refused with
   0x1fa and the pass ends there: no prompt is put up and the purse is not
   touched.  The price is still published, because the bag test is behind that
   store. */
static void buy_a_full_bag_refuses_without_a_trade_in(void)
{
    unsigned char replies[1];
    int slot;

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    for (slot = 0; slot < BUY_SLOTS; slot++) {
        buy_put(BUY_SKILLED_MEMBER, slot, BUY_SLOT_CARRIED, BUY_FILLER_ITEM);
    }
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_FILLER_ITEM);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 7), BUY_FILLER_ITEM);
    buy_unstage();
}

/* And the same full bag DOES buy once the trade-in is accepted, because the
   traded item leaves the bag first: CMP byte ptr [EBP-0x4],0x0 / JNZ at
   00033d89 jumps over the count entirely.  The equipped entry is slot 0, the
   shift packs the other seven down, and the bought item lands in the entry the
   shift emptied -- slot 7, which is where fdps_unit_item_count - 1 points. */
static void buy_a_full_bag_still_buys_after_a_trade_in(void)
{
    unsigned char replies[2];
    int slot;

    replies[0] = PICKER_KEY_ENTER;
    replies[1] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_SKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_WEAPON);
    for (slot = 1; slot < BUY_SLOTS; slot++) {
        buy_put(BUY_SKILLED_MEMBER, slot, BUY_SLOT_CARRIED, BUY_FILLER_ITEM);
    }
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 2);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             1000 - (BUY_NEW_WEAPON_PRICE - BUY_OLD_WEAPON_CREDIT));
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 0), BUY_FILLER_ITEM);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 7), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_SKILLED_MEMBER, 7), BUY_NEW_WEAPON);
    buy_unstage();
}

/* A member whose class may wear nothing gets neither half of what
   fdps_unit_can_equip_item decides: the trade-in guard's second call refuses
   the offer although the member IS wearing a weapon, and the auto-equip flag
   latched from the first call leaves the bought item merely carried.  The
   worn item is left exactly as it was. */
static void buy_a_member_who_cannot_equip_gets_neither_half(void)
{
    unsigned char replies[1];

    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    buy_put(BUY_UNSKILLED_MEMBER, 0, BUY_SLOT_EQUIPPED, BUY_OLD_WEAPON);
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_UNSKILLED_MEMBER, buy_keys_one_buy, 5,
           replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000 - BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_UNSKILLED_MEMBER, 0), BUY_SLOT_EQUIPPED);
    CHECK_EQ(buy_id(BUY_UNSKILLED_MEMBER, 0), BUY_OLD_WEAPON);
    CHECK_EQ(buy_flag(BUY_UNSKILLED_MEMBER, 1), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_UNSKILLED_MEMBER, 1), BUY_NEW_WEAPON);
    buy_unstage();
}

/* ONE CALL IS A WHOLE VISIT.  The loop only ends when the item picker cancels,
   so a script that confirms twice buys twice out of one call, and the picker's
   surviving cursor means the second purchase is the same item.  The member
   here is the one that can wear nothing, so neither pass is offered a
   trade-in and both charge the full price. */
static void buy_the_loop_buys_again_until_the_list_is_cancelled(void)
{
    unsigned char keys[9];
    unsigned char replies[2];

    keys[0] = PICKER_KEY_ENTER;
    keys[1] = PICKER_KEY_NONE;
    keys[2] = PICKER_KEY_ENTER;
    keys[3] = PICKER_KEY_NONE;
    keys[4] = PICKER_KEY_ENTER;
    keys[5] = PICKER_KEY_NONE;
    keys[6] = PICKER_KEY_ENTER;
    keys[7] = PICKER_KEY_NONE;
    keys[8] = PICKER_KEY_ESC;
    replies[0] = PICKER_KEY_ENTER;
    replies[1] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1200;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_UNSKILLED_MEMBER, keys, 9, replies, 2);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             1200 - 2 * BUY_NEW_WEAPON_PRICE);
    CHECK_EQ(buy_flag(BUY_UNSKILLED_MEMBER, 0), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_UNSKILLED_MEMBER, 0), BUY_NEW_WEAPON);
    CHECK_EQ(buy_flag(BUY_UNSKILLED_MEMBER, 1), BUY_SLOT_CARRIED);
    CHECK_EQ(buy_id(BUY_UNSKILLED_MEMBER, 1), BUY_NEW_WEAPON);
    CHECK_EQ(buy_flag(BUY_UNSKILLED_MEMBER, 2), BUY_SLOT_EMPTY);
    buy_unstage();
}

/* A cancel out of the BUY-TARGET picker ends that pass alone and the item list
   comes back: CMP dword ptr [EBP-0x30],-0x1 / JZ at 00033c3b jumps to the
   loop's back edge and not out of it.  Everything after that test is skipped,
   the price store included, so the figure global still holds what was parked
   in it before the call. */
static void buy_cancelling_the_target_ends_only_the_pass(void)
{
    unsigned char keys[5];
    unsigned char replies[1];

    keys[0] = PICKER_KEY_ENTER;
    keys[1] = PICKER_KEY_NONE;
    keys[2] = PICKER_KEY_ESC;
    keys[3] = PICKER_KEY_NONE;
    keys[4] = PICKER_KEY_ESC;
    replies[0] = PICKER_KEY_ENTER;
    buy_stage();
    data_fdps_shared_party_total_gold = 1000;
    data_fdps_dialog_last_action_value_param = BUY_VALUE_SENTINEL;
    buy_go(BUY_STOCK_NEW_WEAPON, BUY_SKILLED_MEMBER, keys, 5, replies, 1);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, BUY_VALUE_SENTINEL);
    CHECK_EQ(data_fdps_shared_party_total_gold, 1000);
    CHECK_EQ(buy_flag(BUY_SKILLED_MEMBER, 0), BUY_SLOT_EMPTY);
    buy_unstage();
}

void run_shop_tests(void)
{
    RUN_TEST(shop_skips_holes_between_stock);
    RUN_TEST(shop_leading_hole_does_not_end_row);
    RUN_TEST(shop_ids_above_7f_are_unsigned);
    RUN_TEST(shop_index_selects_a_row_of_twelve);
    RUN_TEST(shop_empty_row_returns_zero);
    RUN_TEST(shop_full_row_writes_twelve);
    RUN_TEST(shop_trailing_holes_contribute_nothing);
    RUN_TEST(shop_does_not_modify_the_table);

    RUN_TEST(picker_escape_cancels);
    RUN_TEST(picker_enter_answers_the_entry_under_the_cursor);
    RUN_TEST(picker_space_confirms_like_enter);
    RUN_TEST(picker_right_and_left_step_one);
    RUN_TEST(picker_right_stops_on_the_last_entry);
    RUN_TEST(picker_left_stops_on_the_first_entry);
    RUN_TEST(picker_down_steps_a_row);
    RUN_TEST(picker_up_steps_a_row);
    RUN_TEST(picker_down_needs_a_whole_row_ahead);
    RUN_TEST(picker_up_stops_on_the_top_row);
    RUN_TEST(picker_window_follows_the_cursor_forward);
    RUN_TEST(picker_window_follows_the_cursor_back);
    RUN_TEST(picker_right_can_scroll_the_window);
    RUN_TEST(picker_left_can_scroll_the_window);
    RUN_TEST(picker_cursor_past_the_stock_resets_both);
    RUN_TEST(picker_window_is_not_clamped_on_its_own);
    RUN_TEST(picker_cursor_survives_between_visits);

    RUN_TEST(bt_escape_cancels);
    RUN_TEST(bt_delete_cancels_like_escape);
    RUN_TEST(bt_enter_answers_the_cursor);
    RUN_TEST(bt_space_confirms_like_enter);
    RUN_TEST(bt_type_27_is_still_equipment);
    RUN_TEST(bt_type_28_goes_to_the_village_grid);
    RUN_TEST(bt_type_zero_goes_to_the_village_grid);
    RUN_TEST(bt_village_grid_cancel_is_passed_through);
    RUN_TEST(bt_right_and_left_step_one);
    RUN_TEST(bt_right_stops_on_the_last_member);
    RUN_TEST(bt_left_stops_on_the_first_member);
    RUN_TEST(bt_right_can_scroll_the_window);
    RUN_TEST(bt_left_can_scroll_the_window);
    RUN_TEST(bt_down_steps_three_and_the_window_with_it);
    RUN_TEST(bt_up_steps_three_and_the_window_with_it);
    RUN_TEST(bt_up_stops_on_the_top_row);
    RUN_TEST(bt_down_needs_a_whole_row_ahead);
    RUN_TEST(bt_locked_slot_cannot_be_confirmed);
    RUN_TEST(bt_locked_slot_refuses_space_too);
    RUN_TEST(bt_locked_slot_is_confirmable_before_the_chapter);
    RUN_TEST(bt_locked_chapter_blocks_only_slot_three);
    RUN_TEST(bt_locked_slot_is_still_walked_over);
    RUN_TEST(bt_cursor_survives_between_visits);
    RUN_TEST(bt_window_is_not_seeded_on_entry);

    RUN_TEST(buy_charges_the_price_and_hands_over_the_item);
    RUN_TEST(buy_declined_offer_moves_nothing);
    RUN_TEST(buy_a_purse_equal_to_the_price_is_enough);
    RUN_TEST(buy_a_purse_one_short_refuses);
    RUN_TEST(buy_trade_in_credits_three_quarters_truncated);
    RUN_TEST(buy_declined_trade_in_leaves_the_price_alone);
    RUN_TEST(buy_a_credit_above_the_price_is_paid_out);
    RUN_TEST(buy_a_weapon_ignores_the_armour_being_worn);
    RUN_TEST(buy_a_full_bag_refuses_without_a_trade_in);
    RUN_TEST(buy_a_full_bag_still_buys_after_a_trade_in);
    RUN_TEST(buy_a_member_who_cannot_equip_gets_neither_half);
    RUN_TEST(buy_the_loop_buys_again_until_the_list_is_cancelled);
    RUN_TEST(buy_cancelling_the_target_ends_only_the_pass);
}
