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
}
