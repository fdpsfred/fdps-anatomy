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
 * Nothing here asserts what any global holds before a case sets it: the
 * definitions are ticket 23's and are zero-filled until then.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "deploy.h"
#include "keybd.h"
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

void run_chapter_tests(void)
{
    RUN_TEST(reset_rebuilds_the_unit_array_for_the_chapter_global);
    RUN_TEST(reset_reads_the_chapter_id_from_the_global);
    RUN_TEST(reset_installs_the_state_a_chapter_opens_in);
}
