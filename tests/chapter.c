/* tests/chapter.c -- cover for src/chapter.c.
 *
 * fdps_chapter_state_reset takes no argument and returns nothing, so
 * everything it does is a change to a global, and every case here stages those
 * globals with values the function has to overwrite and reads them back
 * afterwards.
 *
 * It is run whole, against the shipped containers, for the reason
 * tests/deploy.c's chapter cases give: its middle line is
 * fdps_build_map_unit_array, whose own first line is
 * fdps_field_load_chapter_resources, and a resource member that is not there
 * ends the process or spins in fdps_wait_any_key rather than failing a check.
 * ICON.CEL, FIELD.VFS, FIELD1.VFS and FIELD2.VFS are staged next to the
 * executable through tests/gamefile.lst; every case skips itself unless all
 * four are there.
 *
 * Expected values come from the assembly at 00022750 and from the shipped
 * resources:
 *
 *   MOV dword ptr [0x00069cd0],0x0 at 0002275c and ,0x1 at 000227b8
 *     the overlay is switched off and then back on to mode 1
 *   MOV dword ptr [0x00069da0],0x0 at 00022766     battle still running
 *   PUSH dword ptr [0x00069cf4] / CALL 0x00022be0 at 00022770
 *     the chapter the rebuild is given is the global, not a constant
 *   PUSH 0x20 / PUSH 0x0 / MOV EAX,0x640d8 / PUSH EAX / CALL memset at
 *     0002277e                                      all 32 flags, not fewer
 *   MOV dword ptr [0x00069ce4],0x0 and [0x00069ce0],0x0 at 00022790, 0002279a
 *   MOV dword ptr [0x00069cd4],0x0 and [0x00069ccc],0x0 at 000227a4, 000227ae
 *   MOV dword ptr [0x00069ce8],0x1 at 000227c2       turn 1, not turn 0
 *   CALL 0x000567b3 at 000227cc                      the ring is emptied
 *
 * The two map facts the rebuild is measured by are the ones tests/deploy.c
 * already pins and documents, read out of the shipped containers with
 * tools/vfs_dump: MAP00.DAT fields one player slot behind 22 scripted
 * deployments and tags none of them with wave 0, so chapter 0 ends with one
 * unit standing on MAP00.COD record 22 at (16, 22); MAP05.DAT fields four
 * player slots behind 32 scripted deployments and tags two with wave 0, so
 * chapter 5 ends with six, the first on MAP05.COD record 32 at (4, 8) and the
 * fifth being character 98 at (15, 4).
 *
 * fdps_show_chapter_title_card is run whole too, and for a related reason:
 * it loads Chapter.saf and Chapter.pal out of MISC.VFS through
 * fdps_vfs_load_entry, which ends the process rather than returning when the
 * container is not there.  MISC.VFS is staged through tests/gamefile.lst and
 * the three cases skip themselves unless it is present.  What they assert
 * against is set out above those cases.
 *
 * Nothing here asserts what any global holds before a case sets it: the
 * definitions are ticket 23's and are zero-filled until then.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "deploy.h"
#include "keybd.h"
#include "sprite.h"
#include "vfs.h"
#include "chapter.h"

#define CH_ONE_SLOT 0
#define CH_FOUR_SLOT 5

#define CH0_PLAYER_SLOTS 1
#define CH0_CHAR_SPAWNS 22
#define CH0_UNITS 1
#define CH0_PARTY_TILE_X 16
#define CH0_PARTY_TILE_Y 22

#define CH5_PLAYER_SLOTS 4
#define CH5_CHAR_SPAWNS 32
#define CH5_WAVE0_UNITS 2
#define CH5_PARTY0_TILE_X 4
#define CH5_PARTY0_TILE_Y 8
#define CH5_WAVE0_CHAR_A 98
#define CH5_WAVE0_TILE_A_X 15
#define CH5_WAVE0_TILE_A_Y 4

#define CEL_NAME "ICON.CEL"
#define FIELD_NAME "FIELD.VFS"
#define FIELD1_NAME "FIELD1.VFS"
#define FIELD2_NAME "FIELD2.VFS"

/* The reader takes a fixed 0x2970-byte bite out of the sheet's offset table,
   so a shorter file is not a smaller fixture but one it runs off the end of.
   Same guard tests/deploy.c and tests/rsrc.c use. */
#define CEL_MIN_SIZE (15L + 0x2970L)

/* Wide enough for every character and enemy id chapter 5's wave-0 records
   name: character 12 indexes the roster tables directly and character 98 the
   enemy table at 98 - 60. */
#define TABLE_ROWS 64

/* Every item id is a valid index into the staged table, 0xff included:
   fdps_unit_recompute_combat_stats follows the equipped flag into
   fdps_get_item_record with no bound of any kind. */
#define ITEM_TABLE_ROWS 256

#define PARTY_SLOTS 8
#define PARTY_PORTRAIT 3
#define PARTY_CHAR_ID_BASE 0x30
#define PARTY_HP_MAX_BASE 50
#define PARTY_MP_MAX_BASE 20

#define UNIT_RECORD_STRIDE 0x50

/* How many entries the per-cell event flag table has, and so how many the
   PUSH 0x20 at 0002277e clears. */
#define CELL_EVENT_FLAGS 32

/* The values the cases stage into the scalars the reset overwrites.  Each is
   picked so that the value the function installs cannot be mistaken for the
   one that was already there: the turn counter is staged above 1 rather than
   at 0, so "installs 1" and "clears to 0" are different answers, and the
   overlay mode is staged at 6, the top of fdps_draw_map_cursor's dispatch
   chain, so a body that only performed the store of 0 at 0002275c is a
   different answer again. */
#define STALE_END_CODE 2
#define STALE_TURN 7
#define STALE_DRAW_MODE 6
#define STALE_VIEW_X 7
#define STALE_VIEW_Y 9
#define STALE_CURSOR_X 48
#define STALE_CURSOR_Y 72
#define STALE_QUEUE_HEAD 3
#define STALE_QUEUE_WRITE 9

static struct fdps_unit_record stage_party[PARTY_SLOTS];
static struct fdps_character_base_record stage_char[TABLE_ROWS];
static struct fdps_character_growth stage_growth[TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];

static int containers_ready = 0;
static int containers_checked = 0;

static void zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static void ensure_containers(void)
{
    FILE *fp;
    long size;

    if (containers_checked) {
        return;
    }
    containers_checked = 1;

    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < CEL_MIN_SIZE) {
        return;
    }

    fp = fopen(FIELD_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    fp = fopen(FIELD1_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    fp = fopen(FIELD2_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    containers_ready = 1;
}

/* Nulling, not freeing.  fdps_field_load_chapter_resources frees all of these
   on entry, and with a layer count of zero and null everywhere else it frees
   nothing -- the state a freshly started process is in.  Handing a static
   array or a block another test file still owns to free() is not something a
   later check would get to report. */
static void clear_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < 6; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_spawn_pos_table_ptr = NULL;
}

/* The blocks the load left behind, released, and the globals put back the way
   a fresh process has them, so whatever runs after a case sees what it would
   have seen if the case had never run. */
static void free_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    free(data_fdps_current_chapter_text_ptr);
    free(data_fdps_tile_event_data_table_ptr);
    free(data_fdps_map_cell_event_code_layer_ptr);
    free(data_fdps_battle_move_grid_ptr);
    clear_chapter_globals();

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The record the rebuild left at unit_index.  Read through the global, because
   the array is reallocated on every call. */
static struct fdps_unit_record *deployed(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * UNIT_RECORD_STRIDE);
}

/* Blank staging for a whole-chapter reset: the four data tables wide enough
   for the wave-0 records the shipped maps carry, a party of roster_members
   distinguishable records, an empty unit array, and every scalar the reset
   writes set to a value it is not allowed to leave behind.  The two map counts
   are deliberately staged at numbers no map has, so a case that reads the
   right ones back has proved the rebuild really republished them. */
static void stage_chapter(int roster_members)
{
    int member;
    int flag_index;

    clear_chapter_globals();

    zero_bytes(stage_char, (int) sizeof(stage_char));
    zero_bytes(stage_growth, (int) sizeof(stage_growth));
    zero_bytes(stage_enemy, (int) sizeof(stage_enemy));
    zero_bytes(stage_items, (int) sizeof(stage_items));
    zero_bytes(stage_party, (int) sizeof(stage_party));

    data_fdps_battle_character_base_table_ptr = (unsigned char *) stage_char;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) stage_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) stage_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) stage_items;

    for (member = 0; member < PARTY_SLOTS; member++) {
        stage_party[member].portrait_id = PARTY_PORTRAIT;
        stage_party[member].char_id =
            (unsigned char) (PARTY_CHAR_ID_BASE + member);
        stage_party[member].hp_max = (short) (PARTY_HP_MAX_BASE + member);
        stage_party[member].mp_max = (short) (PARTY_MP_MAX_BASE + member);
    }

    data_fdps_roster_array_ptr = (unsigned char *) stage_party;
    data_fdps_roster_member_count = roster_members;

    data_fdps_map_player_slot_count = 99;
    data_fdps_map_char_spawn_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;

    for (flag_index = 0; flag_index < CELL_EVENT_FLAGS; flag_index++) {
        data_fdps_map_cell_event_triggered_flags[flag_index] = 0xff;
    }

    data_fdps_chapter_event_or_battle_end_code = STALE_END_CODE;
    data_fdps_battle_turn_counter = STALE_TURN;
    data_fdps_map_cursor_draw_mode = STALE_DRAW_MODE;
    data_fdps_battle_view_window_origin_x = STALE_VIEW_X;
    data_fdps_battle_view_window_origin_y = STALE_VIEW_Y;
    data_fdps_map_cursor_world_x = STALE_CURSOR_X;
    data_fdps_map_cursor_world_y = STALE_CURSOR_Y;
    data_fdps_input_scancode_queue_head = STALE_QUEUE_HEAD;
    data_fdps_input_scancode_queue_write_index = STALE_QUEUE_WRITE;
}

/* The rebuild really runs, and it runs on the chapter the GLOBAL names.
 *
 * Both map counts are staged at numbers MAP00.DAT does not carry -- 99 player
 * slots and 0 scripted deployments -- so reading 1 and 22 back says the
 * resource load inside fdps_build_map_unit_array republished them.  The unit
 * that ends up in the array stands on MAP00.COD record 22, the placement
 * behind the map's 22 scripted ones, at (16, 22).
 *
 * The chapter id itself comes back unchanged: the reset writes ten globals and
 * this is not one of them, and a body that cleared the chapter it had just
 * loaded would be a different function.  The roster is untouched for the same
 * reason -- the party between battles is not per-chapter state. */
static void reset_rebuilds_the_unit_array_for_the_chapter_global(void)
{
    ensure_containers();
    CHECK_EQ(containers_ready, 1);
    if (!containers_ready) {
        return;
    }
    stage_chapter(1);
    data_fdps_chapter_current_chapter_id = CH_ONE_SLOT;

    fdps_chapter_state_reset();

    CHECK_EQ(data_fdps_map_player_slot_count, CH0_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH0_CHAR_SPAWNS);
    CHECK_EQ(data_fdps_map_unit_count, CH0_UNITS);
    CHECK_EQ((int) deployed(0)->pos_x, CH0_PARTY_TILE_X);
    CHECK_EQ((int) deployed(0)->pos_y, CH0_PARTY_TILE_Y);
    CHECK_EQ((int) deployed(0)->char_id, PARTY_CHAR_ID_BASE);

    CHECK_EQ(data_fdps_chapter_current_chapter_id, CH_ONE_SLOT);
    CHECK_EQ(data_fdps_roster_member_count, 1);

    free_chapter_globals();
}

/* A second chapter, and it is the only input that changed.
 *
 * Nothing is passed to fdps_chapter_state_reset, so the map it rebuilds can
 * only have come from data_fdps_chapter_current_chapter_id -- and moving that
 * global from 0 to 5 moves every number below.  Chapter 5 fields four player
 * slots behind 32 scripted deployments and tags two of the 32 with wave 0, so
 * the array ends at six records: the party on MAP05.COD records 32 upwards,
 * then the two wave-0 units on the records their own deployments name.
 *
 * A body that passed a literal 0, or that passed some other global, would give
 * chapter 0's 1 and 22 here. */
static void reset_reads_the_chapter_id_from_the_global(void)
{
    ensure_containers();
    if (!containers_ready) {
        return;
    }
    stage_chapter(2);
    data_fdps_chapter_current_chapter_id = CH_FOUR_SLOT;

    fdps_chapter_state_reset();

    CHECK_EQ(data_fdps_map_player_slot_count, CH5_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH5_CHAR_SPAWNS);
    CHECK_EQ(data_fdps_map_unit_count, CH5_PLAYER_SLOTS + CH5_WAVE0_UNITS);
    CHECK_EQ((int) deployed(0)->pos_x, CH5_PARTY0_TILE_X);
    CHECK_EQ((int) deployed(0)->pos_y, CH5_PARTY0_TILE_Y);
    CHECK_EQ((int) deployed(4)->char_id, CH5_WAVE0_CHAR_A);
    CHECK_EQ((int) deployed(4)->pos_x, CH5_WAVE0_TILE_A_X);
    CHECK_EQ((int) deployed(4)->pos_y, CH5_WAVE0_TILE_A_Y);

    CHECK_EQ(data_fdps_chapter_current_chapter_id, CH_FOUR_SLOT);

    free_chapter_globals();
}

/* The state the battle opens in, one assertion per store.
 *
 * The overlay mode is the one that needs two of them.  It is staged at 6, so
 * ending at 1 says the store at 000227b8 happened; a body that kept only the
 * store of 0 at 0002275c would leave 0 here and a body that kept only the
 * later one would pass this and still not be the function.  What no assertion
 * can separate is the two stores from a single store of 1, because nothing in
 * the call tree under fdps_build_map_unit_array reads the global between them.
 *
 * The turn counter is staged at 7 and comes back at 1 rather than 0: the
 * battle opens on turn one, and the counter is what fdps_draw_turn_number
 * shows the player unadjusted, so a zero here would put "turn 0" on the
 * screen.
 *
 * The end code is the reverse -- staged at 2, chapter cleared, and cleared to
 * 0, still running.  The two are the only pair in the body where 0 and 1 are
 * not interchangeable answers.
 *
 * The view window and the cursor go to the map origin, all four, and they are
 * four separate globals rather than two pairs: the assembly names each by its
 * own absolute address.
 *
 * All 32 per-cell event flags are cleared, staged at 0xff.  Entry 31 is the
 * one that says the length really is the 0x20 the assembly pushes -- a shorter
 * memset would leave the tail of the table saying every high-numbered event of
 * the previous chapter had already fired.
 *
 * The scancode ring is emptied by rewinding the write index onto the read
 * index, which is the store fdps_flush_keyboard_queue makes: staged 3 and 9,
 * both come back 3.  A body that had dequeued instead would leave the read
 * index at 9, and one that zeroed both would leave 0. */
static void reset_installs_the_state_a_chapter_opens_in(void)
{
    int flag_index;

    ensure_containers();
    if (!containers_ready) {
        return;
    }
    stage_chapter(1);
    data_fdps_chapter_current_chapter_id = CH_ONE_SLOT;

    fdps_chapter_state_reset();

    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(data_fdps_battle_turn_counter, 1);
    CHECK_EQ((long) data_fdps_chapter_event_or_battle_end_code, 0L);

    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    CHECK_EQ(data_fdps_battle_view_window_origin_y, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 0);

    for (flag_index = 0; flag_index < CELL_EVENT_FLAGS; flag_index++) {
        CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[flag_index], 0);
    }

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, STALE_QUEUE_HEAD);
    CHECK_EQ(data_fdps_input_scancode_queue_head, STALE_QUEUE_HEAD);

    free_chapter_globals();
}

/* --- fdps_show_chapter_title_card @ 00020c60 ---------------------------- */

/* WHAT THESE CASES ASSERT AGAINST.  This routine draws on the real adapter
   and then wipes what it drew: it composes the card on a private page, puts
   the page's 320x200 window at 0xa0000, fades the DAC up and down around it,
   and clears the aperture again before it returns.  So the picture is not
   there to be read afterwards, and the cases below put the machine in mode
   13h, hook the timer, run the whole thing once, and have the interrupt
   compare the live aperture against a reference image on every tick.
   Everything all three cases look at comes from that single run.

   THE REFERENCE IS BUILT, NOT GUESSED.  It is Chapter.saf entry 5 -- the
   chapter data_fdps_chapter_current_chapter_id is staged with -- drawn by
   fdps_draw_composite_sprite at (0x18, 0x18) on a 0x170 x 0xf8 page, with the
   0x140 x 0xc8 window at page + 0x2298 taken out of it.  Every one of those
   numbers is read off the assembly and not off the emitted C: MOV
   [EBP-0x2c],0x170 and MOV [EBP-0x28],0xf8 at 00020cbe and 00020cc5, MOV
   [EBP-0x24],0x18 and MOV [EBP-0x20],0x18 at 00020cda and 00020ce1, PUSH
   0x16480 at 00020cae, and ADD EAX,0x2298 at 00020d3c.  A build that sized
   the page differently, drew at a different origin, took a different window
   out of it or handed the drawer a different entry index produces a different
   320x200 image, and no tick matches.

   THE PAGE IS NOT CLEARED BEFORE IT IS DRAWN ON -- malloc's block goes to the
   drawer as it comes -- so the reference is only well defined if the card
   covers every pixel of the window.  That is not assumed: the reference is
   built twice, from a page pre-filled with 0x00 and from one pre-filled with
   0xff, and the first assertion is that the two agree.  CHAPTER.SAF's frames
   are one 14x9 tilemap of 24x24 cells at offset (0, 0), which is 336x216 over
   a 320x200 window, and every cell is opaque.

   THE CHAPTER IS THE GLOBAL AND NOT A CONSTANT.  Entry 5's card differs from
   entry 0's in 18,306 of its 64,000 bytes (tools/saf_decode over MISC.VFS's
   CHAPTER.SAF), so a body that ignored
   data_fdps_chapter_current_chapter_id and drew entry 0 fails the match.

   WHAT IS LEFT TO THE PLAYTEST.  How bright the card is at any moment, how
   many steps each fade takes and how long each step is held are properties of
   the DAC over time.  The interrupt cannot read the DAC to check them: the
   fades are uploading it through the same index and data ports 33 times over,
   and an interrupt that read those ports mid-upload would corrupt the upload
   it was trying to observe.  The one part of the fade that survives into a
   readable state is the last upload of all, which is what the third case
   below reads. */

#define MISC_NAME "MISC.VFS"

/* The chapter staged into the global before the run.  Any of the thirty would
   do; 5 is one whose card differs from entry 0's, so the case can tell a body
   that read the global from one that did not. */
#define CARD_CHAPTER 5

/* The off-screen page and the window taken out of it, from the assembly cited
   in the note above. */
#define CARD_PAGE_PITCH 0x170
#define CARD_PAGE_ROWS 0xf8
#define CARD_PAGE_BYTES 0x16480
#define CARD_PAGE_MARGIN 0x18
#define CARD_PAGE_WINDOW_AT 0x2298

/* The visible screen and the two BIOS modes the run moves between. */
#define CARD_VGA_BASE 0x000a0000
#define CARD_SCREEN_W 0x140
#define CARD_SCREEN_H 0xc8
#define CARD_SCREEN_BYTES (CARD_SCREEN_W * CARD_SCREEN_H)
#define CARD_MODE_TEXT 0x03
#define CARD_MODE_320X200X256 0x13

/* The BIOS timer, the interrupt the aperture is sampled from. */
#define CARD_TIMER_VECTOR 8

/* Painted over the whole aperture before the run.  No byte of the card is
   this value, so a screen still holding it is one the routine never wrote. */
#define CARD_SENTINEL 0x5a

/* How many ticks have to have seen the finished card on the adapter.  The two
   fades and the hold between them are 33 delays of 80 ms plus one of 1000 ms,
   so the card stands on the adapter for at least 3.64 seconds -- around 66
   ticks of the 18.2 Hz timer -- and the bound is set at less than half of
   that so a slow host cannot fail the case for the wrong reason. */
#define CARD_MIN_MATCH_TICKS 30

/* The DAC read port pair.  Writing an entry number to 0x3c7 arms a read of
   that entry's three components from 0x3c9. */
#define CARD_DAC_READ_INDEX 0x3c7
#define CARD_DAC_DATA 0x3c9
#define CARD_DAC_ENTRIES 256

/* The master palette staged behind data_fdps_vga_main_palette_ptr for the
   run.  Every component is inside the DAC's 0..63 range, so an upload at bias
   0 has to reproduce it exactly, and the three channels step by different
   amounts so a build that uploaded one channel's bytes three times over does
   not pass. */
#define CARD_MASTER_RED_STEP 7
#define CARD_MASTER_GREEN_STEP 11
#define CARD_MASTER_BLUE_STEP 13
#define CARD_DAC_RANGE 64

static unsigned char card_reference[CARD_SCREEN_BYTES];
static unsigned char card_alt_reference[CARD_SCREEN_BYTES];
static unsigned char card_final_screen[CARD_SCREEN_BYTES];
static unsigned char card_master_palette[CARD_DAC_ENTRIES * 3];
static unsigned char card_final_dac[CARD_DAC_ENTRIES * 3];

static void (__interrupt __far *card_saved_timer)();
static volatile long card_matched_ticks;
static volatile long card_best_diff;

static int card_ran = 0;
static int card_ready = 0;
static long card_prefill_disagreements = 0;

/* Sampled once per timer tick while the routine runs.  The aperture holds the
   finished card from the blit until the closing memset, which is the whole of
   both fades and the hold, so a correct build is seen matching on most ticks;
   the smallest byte difference seen on any tick is kept as well, so a failure
   says how far off the picture was rather than only that it was off. */
static void __interrupt __far card_timer_isr(void)
{
    unsigned char *aperture;
    long diff;
    long at;

    aperture = (unsigned char *) CARD_VGA_BASE;
    if (memcmp(aperture, card_reference, (size_t) CARD_SCREEN_BYTES) == 0) {
        card_matched_ticks++;
        card_best_diff = 0;
    } else if (card_best_diff != 0) {
        diff = 0;
        for (at = 0; at < CARD_SCREEN_BYTES; at++) {
            if (aperture[at] != card_reference[at]) {
                diff++;
            }
        }
        if (diff < card_best_diff) {
            card_best_diff = diff;
        }
    }
    _chain_intr(card_saved_timer);
}

static void card_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Draws Chapter.saf entry CARD_CHAPTER onto a page pre-filled with `prefill`
   and copies the window the routine blits into `out`. */
static void card_build_reference(void *bank, int prefill, unsigned char *out)
{
    int request[DRAW_REQUEST_DWORDS];
    unsigned char *page;
    int row;

    page = (unsigned char *) malloc((size_t) CARD_PAGE_BYTES);
    if (page == NULL) {
        return;
    }
    memset(page, prefill, (size_t) CARD_PAGE_BYTES);

    request[DRAW_REQUEST_DEST_BASE] = (int) page;
    request[DRAW_REQUEST_DEST_PITCH] = CARD_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = CARD_PAGE_ROWS;
    request[DRAW_REQUEST_X] = CARD_PAGE_MARGIN;
    request[DRAW_REQUEST_Y] = CARD_PAGE_MARGIN;
    request[DRAW_REQUEST_IMAGE] = (int) bank;
    request[DRAW_REQUEST_ITEM_INDEX] = CARD_CHAPTER;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;
    fdps_draw_composite_sprite(request, 0);

    for (row = 0; row < CARD_SCREEN_H; row++) {
        memmove(out + row * CARD_SCREEN_W,
                page + CARD_PAGE_WINDOW_AT + row * CARD_PAGE_PITCH,
                (size_t) CARD_SCREEN_W);
    }
    free(page);
}

/* The one run all three cases read.  MISC.VFS has to be there: a container
   fdps_vfs_load_entry cannot open ends the process rather than failing a
   check. */
static void card_run(void)
{
    FILE *fp;
    void *bank;
    unsigned char *saved_master;
    int saved_chapter;
    int entry;
    long at;

    if (card_ran) {
        return;
    }
    card_ran = 1;

    fp = fopen(MISC_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    for (entry = 0; entry < CARD_DAC_ENTRIES; entry++) {
        card_master_palette[entry * 3] =
            (unsigned char) ((entry * CARD_MASTER_RED_STEP) % CARD_DAC_RANGE);
        card_master_palette[entry * 3 + 1] =
            (unsigned char) ((entry * CARD_MASTER_GREEN_STEP) % CARD_DAC_RANGE);
        card_master_palette[entry * 3 + 2] =
            (unsigned char) ((entry * CARD_MASTER_BLUE_STEP) % CARD_DAC_RANGE);
    }

    saved_master = data_fdps_vga_main_palette_ptr;
    saved_chapter = data_fdps_chapter_current_chapter_id;
    data_fdps_vga_main_palette_ptr = card_master_palette;
    data_fdps_chapter_current_chapter_id = CARD_CHAPTER;

    bank = fdps_vfs_load_entry(MISC_NAME, "Chapter.saf");
    card_build_reference(bank, 0x00, card_reference);
    card_build_reference(bank, 0xff, card_alt_reference);
    free(bank);

    card_prefill_disagreements = 0;
    for (at = 0; at < CARD_SCREEN_BYTES; at++) {
        if (card_reference[at] != card_alt_reference[at]) {
            card_prefill_disagreements++;
        }
    }

    card_matched_ticks = 0;
    card_best_diff = CARD_SCREEN_BYTES;

    card_set_mode(CARD_MODE_320X200X256);
    memset((void *) CARD_VGA_BASE, CARD_SENTINEL, (size_t) CARD_SCREEN_BYTES);

    card_saved_timer = _dos_getvect(CARD_TIMER_VECTOR);
    _dos_setvect(CARD_TIMER_VECTOR, card_timer_isr);
    fdps_show_chapter_title_card();
    _dos_setvect(CARD_TIMER_VECTOR, card_saved_timer);

    memmove(card_final_screen, (void *) CARD_VGA_BASE,
            (size_t) CARD_SCREEN_BYTES);
    for (entry = 0; entry < CARD_DAC_ENTRIES; entry++) {
        outp(CARD_DAC_READ_INDEX, entry);
        card_final_dac[entry * 3] = (unsigned char) (inp(CARD_DAC_DATA) & 0x3f);
        card_final_dac[entry * 3 + 1] =
            (unsigned char) (inp(CARD_DAC_DATA) & 0x3f);
        card_final_dac[entry * 3 + 2] =
            (unsigned char) (inp(CARD_DAC_DATA) & 0x3f);
    }
    card_set_mode(CARD_MODE_TEXT);

    data_fdps_vga_main_palette_ptr = saved_master;
    data_fdps_chapter_current_chapter_id = saved_chapter;
    card_ready = 1;
}

/* The card the chapter global names reaches the adapter, pixel for pixel, and
   stays there.  card_best_diff is the smallest number of bytes any tick saw
   between the aperture and the reference, so 0 means some tick saw the exact
   image; the tick count then says it was held rather than flashed. */
static void title_card_puts_the_chapter_the_global_names_on_the_adapter(void)
{
    card_run();
    if (!card_ready) {
        return;
    }

    CHECK_EQ(card_prefill_disagreements, 0L);
    CHECK_EQ(card_best_diff, 0L);
    CHECK_EQ(card_matched_ticks >= CARD_MIN_MATCH_TICKS, 1);
}

/* The routine wipes what it drew.  Every byte of CHAPTER.SAF's cards is
   non-zero, so an aperture that still held any of the picture -- or the
   sentinel it was filled with beforehand -- would show up here. */
static void title_card_leaves_the_frame_buffer_cleared(void)
{
    long still_set;
    long at;

    card_run();
    if (!card_ready) {
        return;
    }

    still_set = 0;
    for (at = 0; at < CARD_SCREEN_BYTES; at++) {
        if (card_final_screen[at] != 0) {
            still_set++;
        }
    }
    CHECK_EQ(still_set, 0L);
}

/* The last upload of all is the master palette at bias 0, not the black the
   fade down ended on.  A body that dropped the closing upload, or that
   carried the fade's -0x40 into it, leaves the DAC extinguished; the second
   check is what tells those two apart from a build that got it right. */
static void title_card_leaves_the_master_palette_installed(void)
{
    long wrong;
    long lit;
    long at;

    card_run();
    if (!card_ready) {
        return;
    }

    wrong = 0;
    lit = 0;
    for (at = 0; at < CARD_DAC_ENTRIES * 3; at++) {
        if (card_final_dac[at] != card_master_palette[at]) {
            wrong++;
        }
        if (card_final_dac[at] != 0) {
            lit++;
        }
    }
    CHECK_EQ(wrong, 0L);
    CHECK_EQ(lit > 0, 1);
}

void run_chapter_tests(void)
{
    RUN_TEST(reset_rebuilds_the_unit_array_for_the_chapter_global);
    RUN_TEST(reset_reads_the_chapter_id_from_the_global);
    RUN_TEST(reset_installs_the_state_a_chapter_opens_in);
    RUN_TEST(title_card_puts_the_chapter_the_global_names_on_the_adapter);
    RUN_TEST(title_card_leaves_the_frame_buffer_cleared);
    RUN_TEST(title_card_leaves_the_master_palette_installed);
}
