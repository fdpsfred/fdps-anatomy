/* tests/ending.c -- cover for src/ending.c.
 *
 * One entry point, fdps_play_ending_credit_roll @ 0001ba40, with its own
 * fixture and its own block of cases.  The block sets out what it asserts
 * against and why its run is bounded the way it is, above itself.
 *
 * The machine fence at the top of this file is the one tests/title.c puts
 * around fdps_play_movie, duplicated here rather than shared: the credit roll
 * ends by calling that function, so a run of it takes vector 09h down and
 * brings it back, and the two test units are separate compilations with no
 * header of their own to hold the probes.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testharn.h"
#include "ailv3.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "cd.h"
#include "palette.h"
#include "text.h"
#include "ending.h"

/* The aperture and the frame inside it.  The frame stops at 64000, which is
   what the closing clear covers and what the last card is read back out of. */
#define MOVIE_BASE 0x000a0000
#define MOVIE_FRAME_BYTES 0xfa00

#define MOVIE_MODE_TEXT 0x03
#define MOVIE_MODE_320X200X256 0x13

/* What the frame is filled with before the run.  The clear writes zero, so the
   sentinel is a value the clear cannot produce. */
#define MOVIE_SENTINEL 0x5a

/* INT 21h AH=35h for vector 09h: offset as the result, selector through the
   pointer.  Separate in-line assembly from src/keybd.c's own probes on
   purpose -- this file reads the vector back independently and compares it
   against what the function under test filed away. */
extern unsigned int movie_read_int9_vector(unsigned short *selector_out);
#pragma aux movie_read_int9_vector =    \
    "push es"                           \
    "push esi"                          \
    "mov  eax,3509h"                    \
    "int  21h"                          \
    "mov  ax,es"                        \
    "pop  esi"                          \
    "mov  [esi],ax"                     \
    "pop  es"                           \
    parm [esi]                          \
    value [ebx]                         \
    modify [eax ebx ecx edx];

/* INT 21h AH=25h for vector 09h with an arbitrary selector:offset, which is
   what putting the original handler back needs. */
extern void movie_write_int9_vector(unsigned short handler_selector,
                                    unsigned int handler_offset);
#pragma aux movie_write_int9_vector =   \
    "push ds"                           \
    "mov  eax,2509h"                    \
    "mov  ds,cx"                        \
    "int  21h"                          \
    "pop  ds"                           \
    parm [cx] [edx]                     \
    modify [eax ebx ecx edx];

/* Set bit 1 of the master 8259's interrupt mask register so IRQ1 cannot be
   delivered, and hand the mask back as it was so it can be put back byte for
   byte.  Masking stops the interrupt rather than deferring it the way CLI
   would, and deferral would not hold: DOS re-enables interrupts inside the
   very INT 21h calls the function under test makes. */
extern unsigned char movie_mask_irq1(void);
#pragma aux movie_mask_irq1 =           \
    "in   al,21h"                       \
    "mov  ah,al"                        \
    "or   al,2"                         \
    "out  21h,al"                       \
    "mov  al,ah"                        \
    value [al]                          \
    modify [eax];

extern void movie_restore_irq_mask(unsigned char mask);
#pragma aux movie_restore_irq_mask = "out 21h,al" parm [al] modify [eax];

static void movie_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* ---- fdps_play_ending_credit_roll @ 0001ba40 -----------------------------
 *
 * Run whole, twice, against the real game files, because there is nothing
 * smaller to run: the function takes no argument, returns nothing and reads
 * everything it needs out of globals, so the only way to reach the two
 * branches that decide the ending is to play one.  FMER1.TMP, FMER2.TMP,
 * MER1.TMP, MER2.TMP, FIGHT.VFS, FIGACT.VFS and BACKGRND.VFS are already
 * staged beside the executable for tests/combat.c (tests/gamefile.lst) and
 * this file needs no other real file: the seven member names are composed
 * inside the function out of the roster index and a record byte, so what is
 * asserted is that the composed names found the members the shipped
 * containers hold.
 *
 * WHAT THE CAPTION IS READ THROUGH.  The one thing the assembly decides that
 * nothing else in the run shows is WHICH chapter text entry each card is
 * captioned with -- base 0x20 or 0x19, plus the roster index, with slot 3
 * skipped on the chapter 27 ending.  So the chapter text global is pointed at
 * a table built here in which entry 0x19 + s is a run of s + 29 blank glyphs,
 * one solid glyph and the end marker: the caption's origin is screen row 2
 * column 0 (page + 0x2578 against the window at page + 0x2298, a difference of
 * two rows), the staged glyph advance is eight, and so entry 0x19 + s paints
 * exactly one eight-pixel block of colour 0xd0 at screen column 232 + 8s.  The
 * timer handler reads those rows back and records which slots it saw.
 *
 * A slot counts only when the eight pixels of the caption colour are there AND
 * the eight of the shadow colour that the font puts one row below them and one
 * column to the right are there too.  Requiring the pair is what makes the
 * reading safe over a screen that still has a backdrop or a character on it:
 * the caption is composed after both, so it is always on top, and a clip would
 * have to reproduce a two-colour signature to be counted as one.
 *
 * The font is staged to match: one byte per glyph, eight pixels wide, one row
 * high, glyph 0 blank and glyph 1 solid, with the outline flag clear so that
 * the single shadow at (1, 1) is drawn and not the four-way outline.
 *
 * THE TWO RUNS.  The chapter 30 ending is played with an empty roster, so the
 * loop's one pass is the pass past the end -- the card that takes the fixed
 * sprite id -- and the chapter 27 ending is played with four members, which is
 * the shortest roster that reaches the slot-3 skip: cards 0, 1 and 2, then the
 * counter jumps to 4 and the pass at 4 is the pass past the end.  Between them
 * every branch in the body is taken.
 *
 * WHY THE SECOND RUN GETS A FASTER CLOCK.  Every frame of every stage waits
 * for the tick counter to change, and a card is 9 + 50 + the action clip + 251
 * + 9 frames, so four cards are about 1,400 ticks -- eighty seconds at the
 * BIOS rate, which the host's stall detector would call a hang.  The second
 * run therefore reprograms channel 0 of the interval timer to about a
 * kilohertz and chains to the BIOS handler every 55th interrupt so the
 * time-of-day clock keeps its rate; the frames are then paced by the vertical
 * retrace alone.  The first run is left at the BIOS rate precisely so that one
 * assertion can count ticks and get frames.
 *
 * WHAT IS LEFT TO THE PLAYTEST.  Where each layer lands on the page, which
 * frame of the standing clip each stage draws, and that the two dissolves run
 * in the direction they should are all matters of pixels the real clips
 * produce; nothing below asserts a picture. */

/* The four staged blend tables, and the window compared out of each.  The
   fight file and the map file first disagree at 1028 and at 2, from the
   measurement in tests/combat.c, so a 128-byte window at 1024 and at 0
   separates them. */
#define CR_RAMP_BYTES 0x4800
#define CR_CUBE_BYTES 0x1000
#define CR_RAMP_WINDOW_AT 1024
#define CR_CUBE_WINDOW_AT 0
#define CR_WINDOW_BYTES 128
#define CR_ENDING_RAMP_FILE "FMER1.TMP"
#define CR_ENDING_CUBE_FILE "FMER2.TMP"
#define CR_MAP_RAMP_FILE "MER1.TMP"
#define CR_MAP_CUBE_FILE "MER2.TMP"

/* The page, the window taken out of it and the caption's origin inside it, all
   from src/ending.c's own constants.  The caption is two page rows below the
   window's first row and in its first column. */
#define CR_PAGE_BYTES 0x16480
#define CR_SCREEN_PITCH 0x140
#define CR_CAPTION_SCREEN_ROW 2

/* The mark zone: ten eight-pixel slots ending at the right edge of the 320
   pixel row, far enough right that no card's sprite reaches them. */
#define CR_MARK_ZONE_FIRST 232
#define CR_MARK_SLOTS 10
#define CR_MARK_WIDTH 8
#define CR_CAPTION_COLOR 0xd0
#define CR_CAPTION_SHADOW_COLOR 0x6d

/* The staged font: one byte to a glyph, eight pixels by one row, glyph 0 blank
   and glyph 1 solid. */
#define CR_GLYPH_WIDTH 8
#define CR_GLYPH_ROWS 1
#define CR_GLYPH_STRIDE 1
#define CR_GLYPH_ADVANCE 8
#define CR_GLYPH_BLANK 0
#define CR_GLYPH_SOLID 1
#define CR_GLYPH_COUNT 256
#define CR_SHADOW_ROW 1
#define CR_SHADOW_COLUMN 1
#define CR_LINE_HEIGHT 8

/* The chapter text fixture: 48 entries, of which 0x19 through 0x22 carry a
   mark and every other one is empty.  -1 is fdps_draw_text's end marker
   (src/text.c). */
#define CR_TEXT_ENTRIES 0x30
#define CR_TEXT_SHORTS 512
#define CR_TEXT_END (-1)
#define CR_FIRST_MARK_TEXT_ID 0x19

/* The two chapter indices and the two roster sizes the runs use.  Four is the
   smallest member count that reaches the slot-3 skip and still ends on the
   pass past the roster. */
#define CR_CHAPTER_30 0x1d
#define CR_CHAPTER_27 0x1a
#define CR_CHAPTER_30_MEMBERS 0
#define CR_CHAPTER_27_MEMBERS 4
#define CR_ROSTER_SLOTS 8
#define CR_ROSTER_STRIDE 0x50
/* STAND000.SAF and ACT000.SAF exist in the shipped containers, so a card for a
   staged member finds its clips. */
#define CR_MEMBER_PORTRAIT 0

/* The caption entries each run should paint, as slot numbers off 0x19.  The
   chapter 30 run has one card, roster index 0, so entry 0x20 -- slot 7.  The
   chapter 27 run has cards 0, 1, 2 and 4, so entries 0x19, 0x1a, 0x1b and
   0x1d: slots 0, 1, 2 and 4, and NOT slot 3. */
#define CR_CHAPTER_30_MARKS (1 << 7)
#define CR_CHAPTER_27_MARKS ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 4))

/* One card is 9 slide frames, 50 hold frames, the action clip, 251 caption
   frames and 9 fade frames.  ACT012.SAF -- the clip the pass past the roster
   loads -- is 32 frames whose durations add up to 56 ticks, so the chapter 30
   run is 375 frames and, at one tick a frame with the first frame's wait
   skipped, at least 374 ticks.  ACT000.SAF adds up to 25, so a run that had
   loaded the member's clip instead of the fixed one could not reach this. */
#define CR_ONE_CARD_TICKS 374

/* The interval timer, and the divisor the second run drives it with: 2386
   ticks of the 1.193 MHz input is five hundred a second, and chaining to the
   BIOS handler every 27th of them keeps the time-of-day clock within a fifth
   of a percent.  Five hundred is enough and no more, because a frame also
   waits out a vertical retrace: at a fourteenth of a second that is seven
   times the tick period, so the tick wait has stopped being what paces the
   run.  Divisor 0 is 65536, the rate the BIOS programs. */
#define CR_TIMER_VECTOR 8
#define CR_PIT_CHANNEL0 0x40
#define CR_PIT_COMMAND 0x43
#define CR_PIT_MODE3 0x36
#define CR_PIT_FAST_DIVISOR 2386
#define CR_PIT_BIOS_DIVISOR 0
#define CR_SUBTICKS_PER_BIOS_TICK 27
#define CR_PIC_COMMAND 0x20
#define CR_PIC_EOI 0x20

/* Six DAC readings: the three channels of entry 0 and of entry 255. */
#define CR_DAC_READ_INDEX 0x3c7
#define CR_DAC_WRITE_INDEX 0x3c8
#define CR_DAC_DATA 0x3c9
#define CR_DAC_ENTRIES 256
#define CR_DAC_SENTINEL 63
#define CR_PALETTE_SPAN 61
#define CR_READING_SLOTS 6

/* A block the size of the composing page, so a leaked page can be counted
   without any other allocation being mistaken for one.  Watcom rounds a
   request up to a multiple of eight and 0x16480 is already one, but the window
   allows for a header the walker might report inside the size. */
#define CR_PAGE_BLOCK_SLACK 32

static struct fdps_unit_record cr_roster[CR_ROSTER_SLOTS];
static unsigned char cr_font[CR_GLYPH_COUNT * CR_GLYPH_STRIDE];
static short cr_text[CR_TEXT_SHORTS];
static struct fdps_palette_entry cr_ending_pal[CR_DAC_ENTRIES];
static struct fdps_palette_entry cr_map_pal[CR_DAC_ENTRIES];

static unsigned char cr_ending_ramp_file[CR_WINDOW_BYTES];
static unsigned char cr_ending_cube_file[CR_WINDOW_BYTES];
static unsigned char cr_map_ramp_file[CR_WINDOW_BYTES];
static unsigned char cr_map_cube_file[CR_WINDOW_BYTES];
static int cr_files_read;

/* What one run left behind.  Every case below reads one of these rather than
   running again. */
static int cr_marks;
static int cr_snapshot_taken;
static unsigned char cr_ramp_during[CR_WINDOW_BYTES];
static unsigned char cr_cube_during[CR_WINDOW_BYTES];
static unsigned char cr_ramp_after[CR_WINDOW_BYTES];
static unsigned char cr_cube_after[CR_WINDOW_BYTES];
static int cr_dac_during[CR_READING_SLOTS];
static int cr_dac_after[CR_READING_SLOTS];
static int cr_screen_non_zero;
static int cr_pages_leaked;
static unsigned int cr_ticks_used;
static int cr_chapter30_done;
static int cr_chapter27_done;

static void (__interrupt __far *cr_saved_timer)();
static int cr_fast_clock;
static int cr_subticks;

/* Which marks are standing in the zone right now.  A mark is not just eight
   pixels of the caption colour: it is those eight AND the eight of the shadow
   colour that the staged font puts one row below and one column right of them,
   which is a signature the clips cannot be mistaken for.  That pairing is what
   lets this run over a screen that still has a backdrop or a character on it,
   because the caption is composed last and is therefore on top of both. */
static void cr_scan_marks(void)
{
    unsigned char *glyph_row;
    unsigned char *shadow_row;
    int slot;
    int column;
    int matched;

    glyph_row = (unsigned char *) (MOVIE_BASE
                                   + CR_CAPTION_SCREEN_ROW * CR_SCREEN_PITCH
                                   + CR_MARK_ZONE_FIRST);
    shadow_row = glyph_row + CR_SHADOW_ROW * CR_SCREEN_PITCH + CR_SHADOW_COLUMN;
    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        matched = 0;
        for (column = 0; column < CR_MARK_WIDTH; column++) {
            if (glyph_row[slot * CR_MARK_WIDTH + column] == CR_CAPTION_COLOR
                && shadow_row[slot * CR_MARK_WIDTH + column]
                   == CR_CAPTION_SHADOW_COLOR) {
                matched++;
            }
        }
        if (matched == CR_MARK_WIDTH) {
            cr_marks |= 1 << slot;
        }
    }
}

static void cr_read_dac(int *into)
{
    outp(CR_DAC_READ_INDEX, 0);
    into[0] = (int) inp(CR_DAC_DATA);
    into[1] = (int) inp(CR_DAC_DATA);
    into[2] = (int) inp(CR_DAC_DATA);
    outp(CR_DAC_READ_INDEX, CR_DAC_ENTRIES - 1);
    into[3] = (int) inp(CR_DAC_DATA);
    into[4] = (int) inp(CR_DAC_DATA);
    into[5] = (int) inp(CR_DAC_DATA);
}

/* The three things that only exist while the roll is running, taken the first
   time a caption reaches the screen.  That moment is past the blend-table
   reads and past the ending palette upload, and it is a long way before the
   restores, which are after the last card. */
static void cr_take_snapshot(void)
{
    memmove(cr_ramp_during,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CR_RAMP_WINDOW_AT, (size_t) CR_WINDOW_BYTES);
    memmove(cr_cube_during,
            data_fdps_inverse_palette_cube + CR_CUBE_WINDOW_AT,
            (size_t) CR_WINDOW_BYTES);
    cr_read_dac(cr_dac_during);
}

static void __interrupt __far cr_timer_isr(void)
{
    int before;

    ++data_fdps_timer_tick_counter;

    before = cr_marks;
    cr_scan_marks();
    if (cr_marks != before && cr_snapshot_taken == 0) {
        cr_take_snapshot();
        cr_snapshot_taken = 1;
    }

    if (cr_fast_clock == 0) {
        _chain_intr(cr_saved_timer);
    }
    cr_subticks++;
    if (cr_subticks >= CR_SUBTICKS_PER_BIOS_TICK) {
        cr_subticks = 0;
        _chain_intr(cr_saved_timer);
    }
    outp(CR_PIC_COMMAND, CR_PIC_EOI);
}

static void cr_set_timer_divisor(int divisor)
{
    outp(CR_PIT_COMMAND, CR_PIT_MODE3);
    outp(CR_PIT_CHANNEL0, divisor & 0xff);
    outp(CR_PIT_CHANNEL0, (divisor >> 8) & 0xff);
}

/* Used heap entries the size of one composing page, which is how a leaked page
   is told from every other allocation the run makes and gives back. */
static int cr_page_blocks(void)
{
    struct _heapinfo entry;
    int pages;

    pages = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY
            && entry._size >= (size_t) CR_PAGE_BYTES
            && entry._size <= (size_t) (CR_PAGE_BYTES + CR_PAGE_BLOCK_SLACK)) {
            pages++;
        }
    }
    return pages;
}

/* One window out of a staged blend table, read here so that what the run put
   in the global can be compared against the file rather than against a
   number. */
static int cr_read_file_window(char *name, long at, unsigned char *into)
{
    FILE *f;
    int got;

    memset(into, 0, (size_t) CR_WINDOW_BYTES);
    f = fopen(name, "rb");
    if (f == NULL) {
        return 0;
    }
    got = 0;
    if (fseek(f, at, SEEK_SET) == 0) {
        got = (int) fread(into, 1, (size_t) CR_WINDOW_BYTES, f);
    }
    fclose(f);
    return got == CR_WINDOW_BYTES;
}

static void cr_read_files(void)
{
    if (cr_files_read != 0) {
        return;
    }
    cr_files_read = 1;
    cr_files_read &= cr_read_file_window(CR_ENDING_RAMP_FILE,
                                         (long) CR_RAMP_WINDOW_AT,
                                         cr_ending_ramp_file);
    cr_files_read &= cr_read_file_window(CR_ENDING_CUBE_FILE,
                                         (long) CR_CUBE_WINDOW_AT,
                                         cr_ending_cube_file);
    cr_files_read &= cr_read_file_window(CR_MAP_RAMP_FILE,
                                         (long) CR_RAMP_WINDOW_AT,
                                         cr_map_ramp_file);
    cr_files_read &= cr_read_file_window(CR_MAP_CUBE_FILE,
                                         (long) CR_CUBE_WINDOW_AT,
                                         cr_map_cube_file);
}

/* Entry 0x19 + s is s + 29 blank glyphs, one solid glyph and the end marker;
   every other entry is the end marker alone. */
static void cr_build_text(void)
{
    int id;
    int slot;
    int blanks;
    int glyph;
    int cursor;

    memset(cr_text, 0, sizeof(cr_text));
    cursor = CR_TEXT_ENTRIES + 1;
    for (id = 0; id < CR_TEXT_ENTRIES; id++) {
        cr_text[id] = (short) (CR_TEXT_ENTRIES * 2);
    }
    cr_text[CR_TEXT_ENTRIES] = (short) CR_TEXT_END;

    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        id = CR_FIRST_MARK_TEXT_ID + slot;
        cr_text[id] = (short) (cursor * 2);
        blanks = CR_MARK_ZONE_FIRST / CR_GLYPH_WIDTH + slot;
        for (glyph = 0; glyph < blanks; glyph++) {
            cr_text[cursor] = CR_GLYPH_BLANK;
            cursor++;
        }
        cr_text[cursor] = CR_GLYPH_SOLID;
        cursor++;
        cr_text[cursor] = (short) CR_TEXT_END;
        cursor++;
    }
}

/* Everything the run reads that is not the machine: the roster, the font, the
   chapter text and the two palettes.  No hardware is touched here, so the
   premise case can use it too. */
static void cr_stage_fixture(int chapter_id, int member_count)
{
    int index;

    memset(cr_roster, 0, sizeof(cr_roster));
    for (index = 0; index < CR_ROSTER_SLOTS; index++) {
        cr_roster[index].portrait_id = (unsigned char) CR_MEMBER_PORTRAIT;
    }
    data_fdps_roster_array_ptr = (unsigned char *) cr_roster;
    data_fdps_roster_member_count = member_count;
    data_fdps_chapter_current_chapter_id = chapter_id;

    memset(cr_font, 0, sizeof(cr_font));
    cr_font[CR_GLYPH_SOLID * CR_GLYPH_STRIDE] = 0xff;
    data_fdps_font_sheet_ptr = cr_font;
    data_fdps_font_glyph_stride_bytes = CR_GLYPH_STRIDE;
    data_fdps_font_glyph_width = (unsigned char) CR_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = (unsigned char) CR_GLYPH_ROWS;
    data_fdps_glyph_advance_x = CR_GLYPH_ADVANCE;
    data_fdps_font_line_height = CR_LINE_HEIGHT;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_glyph_shadow_row_offset = CR_SHADOW_ROW;
    data_fdps_font_shadow_offset_x = CR_SHADOW_COLUMN;

    cr_build_text();
    data_fdps_current_chapter_text_ptr = (unsigned char *) cr_text;

    for (index = 0; index < CR_DAC_ENTRIES; index++) {
        cr_ending_pal[index].red = (unsigned char) (index % CR_PALETTE_SPAN);
        cr_ending_pal[index].green =
            (unsigned char) ((index + 7) % CR_PALETTE_SPAN);
        cr_ending_pal[index].blue =
            (unsigned char) ((index + 14) % CR_PALETTE_SPAN);
        cr_map_pal[index].red =
            (unsigned char) ((index + 21) % CR_PALETTE_SPAN);
        cr_map_pal[index].green =
            (unsigned char) ((index + 28) % CR_PALETTE_SPAN);
        cr_map_pal[index].blue =
            (unsigned char) ((index + 35) % CR_PALETTE_SPAN);
    }
    data_fdps_vga_fight_palette_ptr = (unsigned char *) cr_ending_pal;
    data_fdps_vga_main_palette_ptr = (unsigned char *) cr_map_pal;

    /* Both audio gates closed, so the sound ids the shipped clips carry play
       nothing, and eight empty voices for the teardown. */
    data_fdps_audio_bgm_enabled_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (index = 0; index < SFX_SAMPLE_SLOT_COUNT; index++) {
        data_fdps_audio_sample_handle_table[index] = NULL;
    }
}

/* One whole ending, fenced the way tests/title.c fences its own call to
   fdps_play_movie and for the same reason -- the run ends inside
   fdps_play_movie, so it leaves vector 09h on the build's stub keyboard
   handler and brings the audio stack up, and both have to be put back
   here. */
static void cr_run(int chapter_id, int member_count, int fast_clock)
{
    void (__interrupt __far *entry_timer)();
    unsigned char saved_irq_mask;
    unsigned int vector_offset;
    unsigned short vector_selector;
    unsigned int ticks_before;
    int blocks_before;
    int index;

    cr_read_files();
    cr_stage_fixture(chapter_id, member_count);

    cr_marks = 0;
    cr_snapshot_taken = 0;
    cr_subticks = 0;
    cr_fast_clock = fast_clock;
    memset(cr_ramp_during, 0, (size_t) CR_WINDOW_BYTES);
    memset(cr_cube_during, 0, (size_t) CR_WINDOW_BYTES);
    for (index = 0; index < CR_READING_SLOTS; index++) {
        cr_dac_during[index] = -1;
        cr_dac_after[index] = -1;
    }

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    /* An empty prefix, so the movie player the run ends on is looked for at
       "\fd.exe": certainly absent, on a drive that certainly exists. */
    data_fdps_cdrom_path[0] = '\0';

    /* What vector 08h held before the audio stack took it, which is what it
       has to be given back at the end: the hook below displaces AIL's own
       timer service, fdps_play_movie takes that service down and brings a
       fresh one up before it returns, and putting the displaced handler back
       would leave a shut-down library's dispatcher on the vector for the next
       AIL_startup to save as its own predecessor and chain to. */
    entry_timer = _dos_getvect(CR_TIMER_VECTOR);
    AIL_startup();

    saved_irq_mask = movie_mask_irq1();
    vector_offset = movie_read_int9_vector(&vector_selector);

    movie_set_mode(MOVIE_MODE_320X200X256);
    memset((void *) MOVIE_BASE, MOVIE_SENTINEL, (size_t) MOVIE_FRAME_BYTES);
    for (index = 0; index < CR_DAC_ENTRIES; index++) {
        outp(CR_DAC_WRITE_INDEX, index);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
        outp(CR_DAC_DATA, CR_DAC_SENTINEL);
    }

    blocks_before = cr_page_blocks();
    data_fdps_timer_tick_counter = 0;
    ticks_before = data_fdps_timer_tick_counter;

    cr_saved_timer = _dos_getvect(CR_TIMER_VECTOR);
    _dos_setvect(CR_TIMER_VECTOR, cr_timer_isr);
    if (fast_clock != 0) {
        cr_set_timer_divisor(CR_PIT_FAST_DIVISOR);
    }

    fdps_play_ending_credit_roll();

    cr_ticks_used = data_fdps_timer_tick_counter - ticks_before;
    cr_pages_leaked = cr_page_blocks() - blocks_before;
    cr_read_dac(cr_dac_after);
    cr_screen_non_zero = 0;
    for (index = 0; index < MOVIE_FRAME_BYTES; index++) {
        if (((unsigned char *) MOVIE_BASE)[index] != 0) {
            cr_screen_non_zero++;
        }
    }
    movie_set_mode(MOVIE_MODE_TEXT);

    memmove(cr_ramp_after,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CR_RAMP_WINDOW_AT, (size_t) CR_WINDOW_BYTES);
    memmove(cr_cube_after,
            data_fdps_inverse_palette_cube + CR_CUBE_WINDOW_AT,
            (size_t) CR_WINDOW_BYTES);

    /* The audio stack the run brought up goes down first, because putting it
       down is what gives the interrupt vector and the timer's rate back to
       whoever held them before it took them. */
    fdps_audio_shutdown();
    cr_set_timer_divisor(CR_PIT_BIOS_DIVISOR);
    _dos_setvect(CR_TIMER_VECTOR, entry_timer);
    cr_fast_clock = 0;

    movie_write_int9_vector(vector_selector, vector_offset);
    movie_restore_irq_mask(saved_irq_mask);
}

static void cr_chapter30_run(void)
{
    if (cr_chapter30_done != 0) {
        return;
    }
    cr_chapter30_done = 1;
    cr_run(CR_CHAPTER_30, CR_CHAPTER_30_MEMBERS, 0);
}

static void cr_chapter27_run(void)
{
    if (cr_chapter27_done != 0) {
        return;
    }
    cr_chapter27_done = 1;
    cr_run(CR_CHAPTER_27, CR_CHAPTER_27_MEMBERS, 1);
}

/* The premise every caption assertion rests on: each of the ten staged
   entries, drawn through fdps_draw_text with the colours and the pitch the
   credit roll hands it, paints its own slot and nothing else.  A fixture that
   did not do that would make the readings below meaningless rather than
   false.  The scratch page is the width of the screen so the slot number is
   the screen column divided by eight, which is what the timer handler
   assumes. */
static void credits_premise_each_caption_entry_marks_its_own_slot(void)
{
    unsigned char *page;
    int slot;
    int column;
    int painted;
    int shadowed;
    int elsewhere;

    cr_stage_fixture(CR_CHAPTER_30, CR_CHAPTER_30_MEMBERS);
    page = (unsigned char *) malloc((size_t) (CR_SCREEN_PITCH * 4));
    painted = 0;
    shadowed = 0;
    elsewhere = 0;

    for (slot = 0; slot < CR_MARK_SLOTS; slot++) {
        memset(page, 0, (size_t) (CR_SCREEN_PITCH * 4));
        fdps_draw_text((unsigned char *) cr_text,
                       CR_FIRST_MARK_TEXT_ID + slot, page, CR_SCREEN_PITCH,
                       CR_CAPTION_COLOR, 0, CR_CAPTION_SHADOW_COLOR);
        for (column = 0; column < CR_SCREEN_PITCH; column++) {
            if (column >= CR_MARK_ZONE_FIRST + slot * CR_MARK_WIDTH
                && column < CR_MARK_ZONE_FIRST + (slot + 1) * CR_MARK_WIDTH) {
                if (page[column] == CR_CAPTION_COLOR) {
                    painted++;
                }
                if (page[CR_SHADOW_ROW * CR_SCREEN_PITCH + CR_SHADOW_COLUMN
                         + column] == CR_CAPTION_SHADOW_COLOR) {
                    shadowed++;
                }
            } else if (page[column] != 0) {
                elsewhere++;
            }
        }
    }
    free(page);

    CHECK_EQ(painted, CR_MARK_SLOTS * CR_MARK_WIDTH);
    CHECK_EQ(shadowed, CR_MARK_SLOTS * CR_MARK_WIDTH);
    CHECK_EQ(elsewhere, 0);
}

/* And the premise the blend-table assertions rest on: the four staged files
   were read, and the ending pair really does differ from the map pair inside
   the window compared, so "the table holds the ending file" and "the table
   holds the map file" are different statements. */
static void credits_premise_the_two_blend_pairs_differ(void)
{
    cr_read_files();

    CHECK_EQ(cr_files_read, 1);
    CHECK_EQ(memcmp(cr_ending_ramp_file, cr_map_ramp_file,
                    (size_t) CR_WINDOW_BYTES) != 0, 1);
    CHECK_EQ(memcmp(cr_ending_cube_file, cr_map_cube_file,
                    (size_t) CR_WINDOW_BYTES) != 0, 1);
}

/* MOV dword ptr [EBP-0x30],0x20 at 0001ba66, taken because the chapter index
   is 0x1d, and ADD EAX,[EBP-0x2c] at 0001c040 for the card's own index.  The
   one card of this run is roster index 0, so the entry drawn is 0x20 and the
   slot seen is 7.  A run that had taken the other arm would have marked slot 0
   instead, and a run that added something other than the index would have
   marked a different one again. */
static void credits_the_chapter_30_ending_captions_from_entry_0x20(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_marks, CR_CHAPTER_30_MARKS);
}

/* The two fread calls at 0001bab2 and 0001baee land the ending pair over the
   globals every alpha blitter reads, and they are still there while the cards
   play: the snapshot is taken the first time a caption reaches the screen,
   which is inside the first card. */
static void credits_composites_through_the_ending_blend_tables(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_snapshot_taken, 1);
    CHECK_EQ(memcmp(cr_ramp_during, cr_ending_ramp_file,
                    (size_t) CR_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(cr_cube_during, cr_ending_cube_file,
                    (size_t) CR_WINDOW_BYTES), 0);
}

/* And the two at 0001c20d and 0001c249 put the map pair back before the movie,
   so the map is composited through its own tables again afterwards. */
static void credits_puts_the_map_blend_tables_back(void)
{
    cr_chapter30_run();

    CHECK_EQ(memcmp(cr_ramp_after, cr_map_ramp_file,
                    (size_t) CR_WINDOW_BYTES), 0);
    CHECK_EQ(memcmp(cr_cube_after, cr_map_cube_file,
                    (size_t) CR_WINDOW_BYTES), 0);
}

/* PUSH [0x000643e4] with 0, 0xff and three zero biases at 0001bb10 uploads the
   ending palette before the first card, and the same shape over [0x000643bc]
   at 0001c1ca puts the main one back.  Both fixtures are inside the DAC's six
   bits, so an unbiased upload is the identity and neither is clamped. */
static void credits_uploads_the_ending_palette_and_restores_the_main_one(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_dac_during[0], (int) cr_ending_pal[0].red);
    CHECK_EQ(cr_dac_during[1], (int) cr_ending_pal[0].green);
    CHECK_EQ(cr_dac_during[2], (int) cr_ending_pal[0].blue);
    CHECK_EQ(cr_dac_during[3],
             (int) cr_ending_pal[CR_DAC_ENTRIES - 1].red);
    CHECK_EQ(cr_dac_during[5],
             (int) cr_ending_pal[CR_DAC_ENTRIES - 1].blue);
    CHECK_EQ(cr_dac_after[0], (int) cr_map_pal[0].red);
    CHECK_EQ(cr_dac_after[2], (int) cr_map_pal[0].blue);
    CHECK_EQ(cr_dac_after[3], (int) cr_map_pal[CR_DAC_ENTRIES - 1].red);
}

/* CALL fdps_play_movie at 0001c263 is the last thing in the body, and the
   movie clears the whole mode 13h frame on its way back (00030ff2).  So a
   frame that comes back with a byte in it says the roll did not reach the
   movie -- which is also the only evidence a unit test has that the roster
   loop terminated at all. */
static void credits_hands_the_screen_to_the_ending_movie(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_screen_non_zero, 0);
}

/* Every frame of every stage ends waiting for data_fdps_timer_tick_counter to
   change, so a run costs at least one tick a frame less the first, whose latch
   is uninitialised and may not wait at all.  This run's single card is 9 + 50 +
   56 + 251 + 9 frames: the 56 is ACT012.SAF's own total duration, and the fixed
   sprite id of the pass past the roster is what loads it -- the staged member's
   ACT000.SAF would be 25 and could not reach this floor.  No ceiling is
   asserted: nothing stops the counter advancing more than once while a frame is
   composed. */
static void credits_paces_every_frame_on_the_tick(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_ticks_used >= (unsigned int) CR_ONE_CARD_TICKS, 1);
}

/* CALL free at 0001c1c2 is past the end of the roster loop and CALL malloc at
   0001bc60 is inside it, so a run of one card allocates one page and gives it
   back.  This is the case that says the leak below is a leak and not a page
   that was never released. */
static void credits_a_single_card_leaves_no_page_behind(void)
{
    cr_chapter30_run();

    CHECK_EQ(cr_pages_leaked, 0);
}

/* CMP [0x00069cf4],0x1a / CMP [EBP-0x2c],0x3 / MOV [EBP-0x2c],0x4 at 0001bb4a:
   on the chapter 27 ending the counter is moved from 3 to 4, so slot 3 plays
   no card.  With four members the cards are 0, 1, 2 and then the pass past the
   roster at 4, and the captions are entries 0x19, 0x1a, 0x1b and 0x1d.  Entry
   0x1c is the one that says the skip happened: a body that treated the skip as
   "leave slot 3 out of the roster" and closed the gap would have drawn it. */
static void credits_the_chapter_27_ending_skips_roster_slot_three(void)
{
    cr_chapter27_run();

    CHECK_EQ(cr_marks, CR_CHAPTER_27_MARKS);
}

/* The page is allocated once per card and released once for the whole run, so
   four cards leave three pages behind -- about 89 KB each.  That is the
   original's behaviour and moving the free into the loop, which is what a
   rewrite naturally does, would make this zero
   (rebuild_info/pitfalls.md). */
static void credits_leaks_one_page_for_every_card_but_the_last(void)
{
    cr_chapter27_run();

    CHECK_EQ(cr_pages_leaked, 3);
}

void run_ending_tests(void)
{
    RUN_TEST(credits_premise_each_caption_entry_marks_its_own_slot);
    RUN_TEST(credits_premise_the_two_blend_pairs_differ);
    RUN_TEST(credits_the_chapter_30_ending_captions_from_entry_0x20);
    RUN_TEST(credits_composites_through_the_ending_blend_tables);
    RUN_TEST(credits_puts_the_map_blend_tables_back);
    RUN_TEST(credits_uploads_the_ending_palette_and_restores_the_main_one);
    RUN_TEST(credits_hands_the_screen_to_the_ending_movie);
    RUN_TEST(credits_paces_every_frame_on_the_tick);
    RUN_TEST(credits_a_single_card_leaves_no_page_behind);
    RUN_TEST(credits_the_chapter_27_ending_skips_roster_slot_three);
    RUN_TEST(credits_leaks_one_page_for_every_card_but_the_last);
}
