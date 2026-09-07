/* tests/vilshop.c -- cover for src/vilshop.c.
 *
 * Expected values come from the assembly at 00035aa0 and from nothing else:
 * the seed MOV dword ptr [EBP-0x8],0x0 at 00035aac in front of the entry test
 * CMP dword ptr [EBP-0x8],-0x1 / JNZ at 00035b79, so the first pass always
 * runs; the four-dword template copied onto the frame by the four MOVSD at
 * 00035abb -- 0x24, 0x12, 0x04, 0x10 at 00031194 -- and PUSH 0x4 at 00035b98
 * for the row's length; the unsigned bound CMP dword ptr [EBP-0x8],0x3 / JA at
 * 00035ba6 in front of JMP dword ptr CS:[EAX*4 + 0x35b84], whose four entries
 * are 00035bc1, 00035c07, 00035c15 and 00035c23; the calls those arms make --
 * two window sweeps and fdps_draw_text on the CHAPTER text block at 00035bfd,
 * then fdps_church_promote_loop, fdps_village_item_transfer_loop and
 * fdps_village_member_status_loop -- and the two-armed test CMP dword ptr
 * [EBP-0x8],-0x1 / JNZ at 00035c2f whose arms both fall to the back edge JMP
 * 0x00035b79 at 00035c90.  None of it is read off the emitted C.
 *
 * WHAT THE CASES ARE ABOUT.  Everything this function draws is somebody else's
 * behaviour and nothing it computes comes back as a value, so what is pinned
 * here is the dispatch: that the answer the command row writes selects the arm
 * the jump table selects, in that order, that the row is four entries long,
 * and that the row is reopened after every arm until the row itself is
 * cancelled.  Each arm is told from the others by something only it leaves
 * behind -- the promotion counter's own pair of substitution slots, the
 * hand-over's giver name with the item slot still untouched, and the status
 * browser's surviving picker cursor with both dialogue slots untouched, which
 * is what the reprint arm and the cancel leave as well while moving no cursor
 * at all.
 *
 * EVERY CASE NEEDS THE REAL MISC.VFS, because the backdrop is loaded by name
 * out of it with the result untested -- a container or a member that cannot be
 * found ends the process inside fdps_vfs_load_entry rather than failing an
 * assertion (vfs.h).  It is probed and the case skips itself rather than
 * dereferencing what a failed load leaves.  No case opens an inventory list,
 * so FACE.CEL is not needed: every bag staged here is empty and the hand-over
 * arm is reached on its refusal path.
 *
 * WHY THE PAGE IS NOT READ BACK.  This screen allocates its own page and frees
 * it before returning, so there is nothing left to compare afterwards; what
 * the cases assert about it instead is that the global it was published in no
 * longer holds the value they parked there, which is the observable half of
 * "it took a page of its own and published it".
 *
 * HOW THE TWO INPUT PATHS ARE PLAYED.  One interrupt handler feeds both,
 * because the callees read different ones.  The command row and every picker
 * read the auto-repeat filter's latch (data_fdps_input_last_scancode), which
 * the handler refills only once the filter has taken what is there -- an equal
 * latch and previous code is exactly that -- so one code is presented per poll
 * however many ticks a frame takes.  The promotion counter's yes/no question
 * reads the scancode ring, so the handler also appends one code per tick the
 * way fdps_keyboard_isr does; the ring is flushed by the filter on every poll,
 * so the two never cross.  Past the end of a script the latch alternates
 * Escape and the filter's no-key value, which supplies as many separate
 * cancels as a case needs and makes a runaway run back out instead of hanging
 * the test image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palcycle.h"
#include "statunit.h"
#include "statwin.h"
#include "vilmenu.h"
#include "vilshop.h"

/* The party the pickers on this screen are offered.  Six is more than any case
   walks and every roster index used is below the twelve walking-icon groups
   the cache below holds. */
#define CHR_MEMBER_COUNT 6

/* A chapter before the one that locks roster slot 3 and ghosts a member, so
   neither rule decides anything in the pickers these cases walk. */
#define CHR_CHAPTER_BEFORE_LOCK 0x16

/* The make codes the rows and pickers know, and the filter's no-key answer. */
#define CHR_KEY_ESC 0x01
#define CHR_KEY_ENTER 0x1c
#define CHR_KEY_LEFT 0x4b
#define CHR_KEY_RIGHT 0x4d
#define CHR_KEY_NONE 0xff

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the screen spins on the tick counter. */
#define CHR_TIMER_VECTOR 8

/* The adapter and the two modes a case moves between.  Mode 13h is not
   optional here: the zoom transition, the window frame, the icon row and every
   message go straight to the aperture. */
#define CHR_MODE_TEXT 0x03
#define CHR_MODE_320X200X256 0x13

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
#define CHR_CEL_SPRITES 0x48
#define CHR_CEL_TABLE_AT 0x0f
#define CHR_CEL_W 25
#define CHR_CEL_H 22
#define CHR_CEL_SKIP_CMD (0xc0 | (CHR_CEL_W - 1))
#define CHR_CEL_STREAMS_AT (CHR_CEL_TABLE_AT + CHR_CEL_SPRITES * 4)
#define CHR_CEL_BYTES (CHR_CEL_STREAMS_AT + CHR_CEL_SPRITES * CHR_CEL_H)

/* The walking-icon cache.  Unlike a loaded .CEL its offset table starts at the
   base with no header (vilmenu.c), it holds twelve streams per member, and the
   member grid indexes it by ROSTER slot. */
#define CHR_ICON_PER_SLOT 0x0c
#define CHR_ICON_SPRITES (CHR_MEMBER_COUNT * CHR_ICON_PER_SLOT)
#define CHR_ICON_SIDE 24
#define CHR_ICON_SKIP_CMD (0xc0 | (CHR_ICON_SIDE - 1))
#define CHR_ICON_STREAMS_AT (CHR_ICON_SPRITES * 4)
#define CHR_ICON_BYTES (CHR_ICON_STREAMS_AT + CHR_ICON_SPRITES * CHR_ICON_SIDE)

/* The number sheet the gold readout paints its eight digits out of: five
   colour rows of thirteen glyphs, one skip run per row six pixels wide, which
   is the width fdps_draw_number tells the decoder to cover. */
#define CHR_NUM_ROWS 5
#define CHR_NUM_GLYPHS 13
#define CHR_NUM_SPRITES (CHR_NUM_ROWS * CHR_NUM_GLYPHS)
#define CHR_NUM_W 6
#define CHR_NUM_H 8
#define CHR_NUM_TABLE_AT 0x0f
#define CHR_NUM_SKIP_CMD (0xc0 | (CHR_NUM_W - 1))
#define CHR_NUM_STREAMS_AT (CHR_NUM_TABLE_AT + CHR_NUM_SPRITES * 4)
#define CHR_NUM_BYTES (CHR_NUM_STREAMS_AT + CHR_NUM_SPRITES * CHR_NUM_H)

/* The resident text block.  fdps_draw_text takes a table of signed 16-bit byte
   offsets measured from the block's own base and walks the stream it points at
   until the token -1 (text.h), so a table whose every entry names one lone
   terminator draws nothing and needs no glyphs staged.  0x220 entries because
   the highest id anything reachable from here asks for is the promotion
   counter's 0x21c, above this screen's own 0x200 and 0x201. */
#define CHR_TEXT_ENTRIES 0x220
#define CHR_TEXT_TERMINATOR (-1)

/* The loaded chapter's own block, a different table from the resident one and
   where the reprint arm's entry 7 lives. */
#define CHR_CHAPTER_TEXT_ENTRIES 0x20

/* An empty container: the lookup walks no entries and answers NULL, so
   fdps_play_sfx finds no clip and starts no sample (audio.c). */
#define CHR_WAV_BANK_BYTES 16

/* The master palette the zoom transition uploads over the whole DAC on each of
   its nine steps (transit.h).  256 entries of three bytes; the contents decide
   only what the screen looks like. */
#define CHR_PALETTE_BYTES 768

/* The eight glyph font, which draws nothing because no text entry has a glyph
   token in it. */
#define CHR_FONT_BYTES 256
#define CHR_GLYPH_WIDTH 8
#define CHR_GLYPH_ROWS 1

/* The two data tables the promotion counter reads.  RankUp.dat is four 3-byte
   routes per character and FRILEVUP.DAT eleven bytes per form; every route of
   every character gets its own class code, so a substitution slot that read
   the wrong record or the wrong route cannot land on the value a case
   expects. */
#define CHR_CHARACTERS 16
#define CHR_FORMS 16
#define CHR_ROUTES 4
#define CHR_PROMOTION_STRIDE 0x0c
#define CHR_GROWTH_STRIDE 0x0b
#define CHR_ROUTE_BYTES 3
#define CHR_ROUTE_FORM_AT 0
#define CHR_ROUTE_CLASS_AT 1
#define CHR_ROUTE_MOVE_AT 2
#define CHR_ROUTE_CLASS_BASE 0x10
#define CHR_ROUTE_FORM_BASE 0x40
#define CHR_CLASS_OF(character, route) \
    (CHR_ROUTE_CLASS_BASE + (character) * CHR_ROUTES + (route))

/* The two biases the promotion question is composed with: the member's
   PORTRAIT id plus one for the name and the chosen ROUTE's class code plus
   0xa1 for the form (church.h). */
#define CHR_NAME_TEXT_BIAS 1
#define CHR_CLASS_TEXT_BIAS 0xa1

/* The member the promotion case stages and the level floor the counter's sweep
   applies.  Its portrait id is below the form ceiling of 9 and its character
   id is a different number, so a slot that read the wrong one of the two
   misses. */
#define CHR_PROMOTE_LEVEL 25
#define CHR_PROMOTE_PORTRAIT 4
#define CHR_PROMOTE_CHAR_ID 3

/* A portrait id inside the closed range 0x24..0x27 that
   fdps_battle_show_unit_status_window draws no window for, so the status
   browser's confirmation is a record lookup and a return. */
#define CHR_NO_WINDOW_PORTRAIT 0x25

/* An inventory entry's flag byte: bit 0x80 is "no item here" (unititem.h), so
   a bag of eight of them is a member carrying nothing. */
#define CHR_SLOTS 8
#define CHR_SLOT_EMPTY 0x80

/* Something for the window animation to release on its first frame, so that
   the free at the top of it is a free of real heap storage and not of whatever
   an earlier case left in the global. */
#define CHR_PORTRAIT_BUF_BYTES 16

/* What the three dialogue slots hold before a run.  All three are outside
   every id or figure any arm can legitimately write. */
#define CHR_TEXT_SENTINEL 0x5a5a
#define CHR_SUBST_SENTINEL 0x7e7e
#define CHR_VALUE_SENTINEL 0x3c3c

/* The purse the gold readout paints. */
#define CHR_START_GOLD 1000

/* How many codes one script can hold. */
#define CHR_SCRIPT_MAX 12

/* The container the backdrop is taken out of. */
#define CHR_BACKDROP_CONTAINER "MISC.VFS"

static struct fdps_unit_record chr_roster[CHR_MEMBER_COUNT];

static unsigned char chr_char_ids[CHR_MEMBER_COUNT] = { 0, 2, 3, 4, 5, 6 };

static unsigned char chr_cel[CHR_CEL_BYTES];
static unsigned char chr_icon_cache[CHR_ICON_BYTES];
static unsigned char chr_num_cel[CHR_NUM_BYTES];
static short chr_text[CHR_TEXT_ENTRIES + 1];
static short chr_chapter_text[CHR_CHAPTER_TEXT_ENTRIES + 1];
static unsigned char chr_wav_bank[CHR_WAV_BANK_BYTES];
static unsigned char chr_palette[CHR_PALETTE_BYTES];
static unsigned char chr_font[CHR_FONT_BYTES];
static unsigned char chr_promo[CHR_CHARACTERS * CHR_PROMOTION_STRIDE];
static unsigned char chr_growth[CHR_FORMS * CHR_GROWTH_STRIDE];

/* Parked in data_fdps_village_backdrop_page_ptr before every run.  It is the
   address of a static in this file, so a run that published its own page
   leaves the global holding something else and a run that published nothing
   leaves it holding this. */
static unsigned char chr_page_sentinel[1];

static unsigned char chr_script[CHR_SCRIPT_MAX];
static int chr_script_len;
static int chr_script_next;
static void (__interrupt __far *chr_saved_timer)();

/* Advances the game's clock, refills the auto-repeat filter's latch once the
   filter has taken what was there, and appends one Escape per tick to the
   scancode ring the way fdps_keyboard_isr does -- which is the answer that
   declines the promotion question (church.h). */
static void __interrupt __far chr_timer_isr(void)
{
    unsigned char code;
    int slot;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (chr_script_next < chr_script_len) {
            code = chr_script[chr_script_next];
            chr_script_next++;
        } else if (data_fdps_input_last_scancode == CHR_KEY_ESC) {
            code = CHR_KEY_NONE;
        } else {
            code = CHR_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    slot = data_fdps_input_scancode_queue_write_index;
    data_fdps_input_scancode_queue[slot] = (unsigned char) CHR_KEY_ESC;
    slot++;
    if (slot == SCANCODE_QUEUE_LEN) {
        slot = 0;
    }
    data_fdps_input_scancode_queue_write_index = slot;

    _chain_intr(chr_saved_timer);
}

static void chr_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void chr_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void chr_build_cel(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(chr_cel, 0, sizeof(chr_cel));
    chr_cel[0] = 'C';
    chr_cel[1] = 'E';
    chr_cel[2] = 'L';
    chr_u16(chr_cel, 0x07, (unsigned int) CHR_CEL_W);
    chr_u16(chr_cel, 0x09, (unsigned int) CHR_CEL_H);
    chr_u16(chr_cel, 0x0b, (unsigned int) CHR_CEL_SPRITES);

    for (sprite_id = 0; sprite_id < CHR_CEL_SPRITES; sprite_id++) {
        stream_at = CHR_CEL_STREAMS_AT + sprite_id * CHR_CEL_H;
        chr_u32(chr_cel, CHR_CEL_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < CHR_CEL_H; row++) {
            chr_cel[stream_at + row] = (unsigned char) CHR_CEL_SKIP_CMD;
        }
    }
}

/* The sprite cache's own table, which starts at the base and carries no header
   of any kind. */
static void chr_build_icon_cache(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(chr_icon_cache, 0, sizeof(chr_icon_cache));
    for (sprite_id = 0; sprite_id < CHR_ICON_SPRITES; sprite_id++) {
        stream_at = CHR_ICON_STREAMS_AT + sprite_id * CHR_ICON_SIDE;
        chr_u32(chr_icon_cache, sprite_id * 4, (unsigned long) stream_at);
        for (row = 0; row < CHR_ICON_SIDE; row++) {
            chr_icon_cache[stream_at + row] =
                (unsigned char) CHR_ICON_SKIP_CMD;
        }
    }
}

static void chr_build_number_sheet(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(chr_num_cel, 0, sizeof(chr_num_cel));
    chr_num_cel[0] = 'C';
    chr_num_cel[1] = 'E';
    chr_num_cel[2] = 'L';
    chr_u16(chr_num_cel, 0x07, (unsigned int) CHR_NUM_W);
    chr_u16(chr_num_cel, 0x09, (unsigned int) CHR_NUM_H);
    chr_u16(chr_num_cel, 0x0b, (unsigned int) CHR_NUM_SPRITES);

    for (sprite_id = 0; sprite_id < CHR_NUM_SPRITES; sprite_id++) {
        stream_at = CHR_NUM_STREAMS_AT + sprite_id * CHR_NUM_H;
        chr_u32(chr_num_cel, CHR_NUM_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        for (row = 0; row < CHR_NUM_H; row++) {
            chr_num_cel[stream_at + row] = (unsigned char) CHR_NUM_SKIP_CMD;
        }
    }
}

/* Overwrites one 3-byte route of one RankUp.dat record. */
static void chr_set_route(int character, int route, int form, int class_id,
                          int move_bonus)
{
    unsigned char *entry;

    entry = chr_promo + character * CHR_PROMOTION_STRIDE
            + route * CHR_ROUTE_BYTES;
    entry[CHR_ROUTE_FORM_AT] = (unsigned char) form;
    entry[CHR_ROUTE_CLASS_AT] = (unsigned char) class_id;
    entry[CHR_ROUTE_MOVE_AT] = (unsigned char) move_bonus;
}

/* Everything the screen and its three submenus draw and read through, staged
   the way the village phase leaves it.  The picker's own two globals are NOT
   reset here -- where its cursor starts is what places the member a case is
   about, so each case sets them itself. */
static void chr_stage(void)
{
    int slot;
    int entry;
    int character;
    int route;

    chr_build_cel();
    chr_build_icon_cache();
    chr_build_number_sheet();

    memset(chr_roster, 0, sizeof(chr_roster));
    for (slot = 0; slot < CHR_MEMBER_COUNT; slot++) {
        chr_roster[slot].char_id = chr_char_ids[slot];
        chr_roster[slot].portrait_id = (unsigned char) CHR_NO_WINDOW_PORTRAIT;
        chr_roster[slot].level = 1;
        for (entry = 0; entry < CHR_SLOTS; entry++) {
            chr_roster[slot].inventory_slots[entry * 2] =
                (unsigned char) CHR_SLOT_EMPTY;
            chr_roster[slot].inventory_slots[entry * 2 + 1] = 0;
        }
    }

    for (entry = 0; entry < CHR_TEXT_ENTRIES; entry++) {
        chr_text[entry] = (short) (CHR_TEXT_ENTRIES * 2);
    }
    chr_text[CHR_TEXT_ENTRIES] = (short) CHR_TEXT_TERMINATOR;

    for (entry = 0; entry < CHR_CHAPTER_TEXT_ENTRIES; entry++) {
        chr_chapter_text[entry] = (short) (CHR_CHAPTER_TEXT_ENTRIES * 2);
    }
    chr_chapter_text[CHR_CHAPTER_TEXT_ENTRIES] = (short) CHR_TEXT_TERMINATOR;

    memset(chr_promo, 0, sizeof(chr_promo));
    for (character = 0; character < CHR_CHARACTERS; character++) {
        for (route = 0; route < CHR_ROUTES; route++) {
            chr_set_route(character, route,
                          CHR_ROUTE_FORM_BASE + character * CHR_ROUTES + route,
                          CHR_CLASS_OF(character, route), route + 1);
        }
    }
    memset(chr_growth, 0, sizeof(chr_growth));

    memset(chr_wav_bank, 0, sizeof(chr_wav_bank));
    memset(chr_font, 0, sizeof(chr_font));
    memset(chr_palette, 0, sizeof(chr_palette));

    data_fdps_roster_array_ptr = (unsigned char *) chr_roster;
    data_fdps_roster_member_count = CHR_MEMBER_COUNT;

    /* The village phase points the map unit array at the roster block, which
       is what lets every fdps_unit_* accessor in the three submenus reach the
       record a picker answered with (vilmenu.h). */
    data_fdps_map_unit_array_ptr = (unsigned char *) chr_roster;

    data_fdps_village_window_sheet_ptr = chr_cel;
    data_fdps_selection_bar_sheet_ptr = chr_cel;
    data_fdps_command_sprite_sheet_ptr = chr_cel;
    data_fdps_shadow_sprite_sheet_ptr = chr_cel;
    data_fdps_cel_sprite_cache_ptr = chr_icon_cache;
    data_fdps_number_glyph_sheet_ptr = chr_num_cel;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_all_game_text_ptr = (unsigned char *) chr_text;
    data_fdps_current_chapter_text_ptr = (unsigned char *) chr_chapter_text;
    data_fdps_promotion_table_ptr = chr_promo;
    data_fdps_battle_character_growth_table_ptr = chr_growth;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = chr_wav_bank;
    data_fdps_vga_main_palette_ptr = chr_palette;
    data_fdps_vga_fight_palette_ptr = chr_palette;

    data_fdps_font_sheet_ptr = chr_font;
    data_fdps_font_glyph_width = (unsigned char) CHR_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) CHR_GLYPH_ROWS;
    data_fdps_font_glyph_stride_bytes = 1;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_glyph_advance_x = CHR_GLYPH_WIDTH;
    data_fdps_font_line_height = CHR_GLYPH_ROWS;

    /* Village mode, so the prompt puts the visible page back behind itself and
       the window close takes its picture out of the backdrop page instead of
       recomposing a battle scene (msgwin.h, statwin.h). */
    data_fdps_village_mode_flag = 1;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_village_status_window_unit_idx = -1;
    data_fdps_unit_status_window_last_tick = -1;
    data_fdps_chapter_current_chapter_id = CHR_CHAPTER_BEFORE_LOCK;

    /* Every audio gate closed, so nothing any of these rows asks for is
       played and the CD poll inside the icon row never reaches the drive. */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_cd_current_music_index = -1;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (entry = 0; entry < SFX_SAMPLE_SLOT_COUNT; entry++) {
        data_fdps_audio_sample_handle_table[entry] = NULL;
    }

    data_fdps_shared_party_total_gold = CHR_START_GOLD;
    data_fdps_dialog_last_action_text_id_param = CHR_TEXT_SENTINEL;
    data_fdps_dialog_subst_text_id_2 = CHR_SUBST_SENTINEL;
    data_fdps_dialog_last_action_value_param = CHR_VALUE_SENTINEL;
}

static void chr_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void chr_place(int cursor, int top)
{
    data_fdps_village_member_select_cursor_idx = cursor;
    data_fdps_village_member_grid_scroll_offset = top;
}

/* The one shipped file every case on this path needs. */
static int chr_backdrop_file_present(void)
{
    FILE *probe;

    probe = fopen(CHR_BACKDROP_CONTAINER, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

/* One whole visit to the church screen, with the adapter in the mode the game
   draws it in and the timer interrupt pacing the frames and playing both input
   channels.  The fixture is NOT staged here: a case calls chr_stage first and
   then edits the roster it is about, and the two steps have to stay apart for
   that. */
static void chr_go(unsigned char *codes, int count)
{
    int index;

    for (index = 0; index < count; index++) {
        chr_script[index] = codes[index];
    }
    chr_script_len = count;
    chr_script_next = 0;

    data_fdps_village_backdrop_page_ptr = chr_page_sentinel;
    data_fdps_portrait_sprite_buf_ptr =
        (unsigned char *) malloc((size_t) CHR_PORTRAIT_BUF_BYTES);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr != NULL, 1);

    data_fdps_input_last_scancode = CHR_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = CHR_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;
    data_fdps_audio_cd_repeat_last_tick = 0;
    data_fdps_audio_cd_repeat_tick_counter = 0;

    chr_set_mode(CHR_MODE_320X200X256);
    chr_saved_timer = _dos_getvect(CHR_TIMER_VECTOR);
    _dos_setvect(CHR_TIMER_VECTOR, chr_timer_isr);
    fdps_run_church_screen();
    _dos_setvect(CHR_TIMER_VECTOR, chr_saved_timer);
    chr_set_mode(CHR_MODE_TEXT);
}

/* Back to what a freshly started program has these in.  It is not tidiness:
   the chapter loaders and the shutdown path free most of these pointers
   unguarded, so a case that walked away leaving one of them naming a static in
   this file would hand a later test a free() of storage that never came from
   the heap.  The published page is freed by the screen itself, so the global
   naming it is only cleared. */
static void chr_done(void)
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
    data_fdps_input_last_scancode = CHR_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = CHR_KEY_NONE;
}

/* Backing out of the command row is the screen's only exit, and it is reached
   on the first pass because the answer slot is seeded with zero and the entry
   test is against -1.  What the run leaves behind is the page: the global no
   longer names the sentinel parked in it, so a page really was taken and
   published, and it is not put back to null on the way out.  The portrait
   buffer is gone because the window sweep releases it unconditionally
   (village.h), which is how a frame that really opened is told from one that
   was skipped.  Nothing else ran -- the picker cursor is where it was parked
   and no dialogue slot was written. */
static void chr_a_cancel_at_the_command_row_ends_the_screen(void)
{
    unsigned char script[1];

    if (chr_backdrop_file_present() == 0) {
        return;
    }

    script[0] = CHR_KEY_ESC;
    chr_stage();
    chr_place(2, 0);
    chr_go(script, 1);

    CHECK_EQ(data_fdps_village_backdrop_page_ptr != chr_page_sentinel, 1);
    CHECK_EQ(data_fdps_village_backdrop_page_ptr != NULL, 1);
    CHECK_EQ(data_fdps_portrait_sprite_buf_ptr == NULL, 1);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param, CHR_TEXT_SENTINEL);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, CHR_SUBST_SENTINEL);
    CHECK_EQ(data_fdps_shared_party_total_gold, CHR_START_GOLD);
    chr_done();
}

/* The row opens on entry 0, so a confirmation with no movement in front of it
   takes the first jump-table slot.  That arm opens no submenu of any kind: it
   sweeps the window shut and open again and writes one line, so every global
   the other three arms move is still where it was parked.  A table whose first
   slot named one of the loops would move at least one of them, and the second
   Escape is only reached because the arm fell to the back edge instead of out
   -- a run that ended on the confirmation would leave the first Escape
   unconsumed and nothing else would have to change for that to pass, which is
   why the picker cursor is asserted as well. */
static void chr_the_talk_command_opens_no_submenu(void)
{
    unsigned char script[2];

    if (chr_backdrop_file_present() == 0) {
        return;
    }

    script[0] = CHR_KEY_ENTER;
    script[1] = CHR_KEY_ESC;
    chr_stage();
    chr_place(2, 0);
    chr_go(script, 2);

    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 2);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param, CHR_TEXT_SENTINEL);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, CHR_SUBST_SENTINEL);
    CHECK_EQ(data_fdps_dialog_last_action_value_param, CHR_VALUE_SENTINEL);
    CHECK_EQ(data_fdps_shared_party_total_gold, CHR_START_GOLD);
    chr_done();
}

/* Three things at once, and each of them is only reachable if the one before
   it held.  The reprint arm falls to the back edge and not out, so the row
   opens a second time; the row's answer slot is kept, so that second row
   starts on entry 0 again and one Left wraps it against the row's length --
   which is four here and not the item screen's five, so the wrap lands on
   entry 3; and slot 3 of the jump table is the status browser, which is the
   arm that opens the member picker and writes neither dialogue slot.  The
   picker's surviving cursor is the witness -- it can only have moved inside a
   picker that was really opened.  A row of five would have wrapped to an entry
   the table does not have and fallen straight through the switch, leaving the
   cursor where it was parked. */
static void chr_the_row_is_four_wide_and_wraps_to_the_status_browser(void)
{
    unsigned char script[5];

    if (chr_backdrop_file_present() == 0) {
        return;
    }

    script[0] = CHR_KEY_ENTER;
    script[1] = CHR_KEY_LEFT;
    script[2] = CHR_KEY_ENTER;
    script[3] = CHR_KEY_RIGHT;
    script[4] = CHR_KEY_ESC;
    chr_stage();
    chr_place(0, 0);
    chr_go(script, 5);

    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 1);
    CHECK_EQ(data_fdps_dialog_last_action_text_id_param, CHR_TEXT_SENTINEL);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, CHR_SUBST_SENTINEL);
    CHECK_EQ(data_fdps_shared_party_total_gold, CHR_START_GOLD);
    chr_done();
}

/* One step right and a confirmation takes slot 1, which is the promotion
   counter -- the arm this screen has and the item screen does not.  Its
   candidate list is the only thing reachable from here that writes
   data_fdps_dialog_subst_text_id_2, and the pair of numbers it leaves is the
   staged table's own: the member's PORTRAIT id plus one for the name and route
   0 of the record his CHARACTER id names, plus 0xa1, for the form.  The
   question is then declined through the ring, the list cancelled and the row
   cancelled.  The village member picker never runs on this path, which is what
   separates this arm from the two that start with one.

   The filler between the row's confirmation and the list's is what makes the
   second one a change the repeat filter reports: an unchanged latch is a held
   key and is silenced until the repeat delay elapses (keybd.h). */
static void chr_the_promote_command_is_the_first_arm(void)
{
    unsigned char script[4];

    if (chr_backdrop_file_present() == 0) {
        return;
    }

    script[0] = CHR_KEY_RIGHT;
    script[1] = CHR_KEY_ENTER;
    script[2] = CHR_KEY_NONE;
    script[3] = CHR_KEY_ENTER;
    chr_stage();
    chr_roster[0].level = (unsigned char) CHR_PROMOTE_LEVEL;
    chr_roster[0].portrait_id = (unsigned char) CHR_PROMOTE_PORTRAIT;
    chr_roster[0].char_id = (unsigned char) CHR_PROMOTE_CHAR_ID;
    chr_place(3, 0);
    chr_go(script, 4);

    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             CHR_PROMOTE_PORTRAIT + CHR_NAME_TEXT_BIAS);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2,
             CHR_CLASS_OF(CHR_PROMOTE_CHAR_ID, 0) + CHR_CLASS_TEXT_BIAS);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 3);
    CHECK_EQ((int) chr_roster[0].level, CHR_PROMOTE_LEVEL);
    chr_done();
}

/* Two steps right and a confirmation takes slot 2, which is the item
   hand-over.  It opens the member picker for a giver, and a giver carrying
   nothing publishes his CHARACTER id plus one and is refused before any item
   list is reached (vilmenu.h) -- so the name slot moves and the item slot does
   not, which is the pair the promotion counter above cannot produce and the
   status browser produces neither half of.  The fillers between the two Rights
   and between the row's confirmation and the picker's are what make the second
   of each pair a change the repeat filter reports (keybd.h). */
static void chr_the_transfer_command_is_the_second_arm(void)
{
    unsigned char script[6];

    if (chr_backdrop_file_present() == 0) {
        return;
    }

    script[0] = CHR_KEY_RIGHT;
    script[1] = CHR_KEY_NONE;
    script[2] = CHR_KEY_RIGHT;
    script[3] = CHR_KEY_ENTER;
    script[4] = CHR_KEY_NONE;
    script[5] = CHR_KEY_ENTER;
    chr_stage();
    chr_place(1, 0);
    chr_go(script, 6);

    CHECK_EQ(data_fdps_dialog_last_action_text_id_param,
             (int) chr_char_ids[1] + CHR_NAME_TEXT_BIAS);
    CHECK_EQ(data_fdps_dialog_subst_text_id_2, CHR_SUBST_SENTINEL);
    CHECK_EQ(data_fdps_village_member_select_cursor_idx, 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, CHR_START_GOLD);
    chr_done();
}

void run_vilshop_tests(void)
{
    RUN_TEST(chr_a_cancel_at_the_command_row_ends_the_screen);
    RUN_TEST(chr_the_talk_command_opens_no_submenu);
    RUN_TEST(chr_the_row_is_four_wide_and_wraps_to_the_status_browser);
    RUN_TEST(chr_the_promote_command_is_the_first_arm);
    RUN_TEST(chr_the_transfer_command_is_the_second_arm);
}
