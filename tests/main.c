/* tests/main.c -- cover for src/main.c.
 *
 * Two functions, one pair of globals-shaped contracts.  fdps_load_data_tables
 * fills the nine data-table pointers out of a container and
 * fdps_free_global_resource_buffers releases them, and neither takes or returns
 * anything that a test can look at directly, so both are measured through the
 * nine globals and the heap.
 *
 * The loader's half is pinned against the shipped MISC.VFS, which is the
 * container the game opens for these nine members; the sizes quoted below come
 * out of that container's own entry table, read the same way tests/vfs.c reads
 * FIELD2.VFS's.  A fabricated container would prove nothing here, and a
 * container missing one of the nine would not fail an assertion at all: the
 * loader's wrapper calls exit(1) on a miss rather than returning.
 *
 * The releaser's half: expected values come from the assembly at 00018a20 --
 * nine PUSH dword ptr
 * [global] / CALL free / ADD ESP,4 groups, no branch anywhere in the body and
 * no store back into any of the nine globals -- and from the CRT's own free()
 * at 0003d478, whose near-heap worker begins OR EAX,EAX / JZ and so returns at
 * once on a null pointer.  None of them is read off the emitted C.
 *
 * The function takes no argument and returns nothing: everything it does is
 * done to the heap, through globals.  So the fixture stages real malloc()
 * blocks into those globals and measures the heap on either side of the call,
 * counting used heap entries with _heapwalk.  Freeing a used block turns it
 * into a free entry, so the used count drops by exactly one per free() that
 * was handed a real block -- which is what lets a test say "this pointer, and
 * this one, and not a tenth" rather than only "it did not crash".
 *
 * Nothing below asserts what any of the nine globals holds on its own; they
 * are null until ticket 23 and the loader, and every value read back here is
 * one this file put there.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "testharn.h"
#include "gamedata.h"
#include "vfs.h"
#include "main.h"

/* How many table pointers the function frees.  Nine, from the nine CALL
   0x0003d478 groups between 00018a2c and 00018aa7. */
#define TABLE_COUNT 9

/* Big enough that the heap cannot be tempted to serve two of them out of one
   entry, small enough that nine of them are nothing. */
#define BLOCK_BYTES 64

/* The nine globals the assembly names, in the order it frees them. */
static unsigned char **const table_slots[TABLE_COUNT] = {
    &data_fdps_battle_character_base_table_ptr,   /* 00063fd8 */
    &data_fdps_battle_character_growth_table_ptr, /* 00063fec */
    &data_fdps_battle_enemy_data_table_ptr,       /* 00063fd4 */
    &data_fdps_class_table_ptr,                   /* 00063fd0 */
    &data_fdps_item_effect_table_ptr,             /* 00063fe0 */
    &data_fdps_class_equip_table_ptr,             /* 00063fe4 */
    &data_fdps_battle_spell_effect_table_ptr,     /* 00063ff0 */
    &data_fdps_spell_learning_table_ptr,          /* 00063fe8 */
    &data_fdps_promotion_table_ptr                /* 00063fdc */
};

/* Used entries currently in the heap.  A used entry becomes a free entry the
   moment it is released -- possibly merged with a neighbour, which is why the
   free entries are not counted and the used ones are. */
static int used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

static void clear_all(void)
{
    int i;

    for (i = 0; i < TABLE_COUNT; i++) {
        *table_slots[i] = NULL;
    }
}

/* A fresh block in every slot.  The function under test frees all nine, so a
   staged run leaks nothing. */
static void stage_all(void)
{
    int i;

    for (i = 0; i < TABLE_COUNT; i++) {
        *table_slots[i] = (unsigned char *) malloc(BLOCK_BYTES);
    }
}

/* A fresh block in one slot and null in the other eight, so the drop in used
   entries counts that slot alone. */
static void stage_only(int index)
{
    clear_all();
    *table_slots[index] = (unsigned char *) malloc(BLOCK_BYTES);
}

/* The measurement the single-slot tests all make: hand the function one live
   block in slot `index` and nothing else, and report how many used entries it
   released.  One means that slot's global reached free(); zero means the
   emitted C is reading some other symbol there. */
static int blocks_released_from_slot(int index)
{
    int before;
    int after;

    stage_only(index);
    before = used_heap_blocks();
    fdps_free_global_resource_buffers();
    after = used_heap_blocks();
    return before - after;
}

/* Nine CALLs to free, so nine blocks go back when all nine slots are live.
   The count is the point: eight would mean a group was dropped in the
   transcription, ten would mean one was written twice. */
static void releases_all_nine_table_buffers(void)
{
    int before;
    int after;

    stage_all();
    before = used_heap_blocks();
    fdps_free_global_resource_buffers();
    after = used_heap_blocks();
    CHECK_EQ(before - after, TABLE_COUNT);
}

/* And the heap survives it: nine well-formed blocks in, a walkable heap out.
   A free() aimed at something that was never malloc'd -- an interior pointer,
   a global that is not a heap block -- shows up here rather than as a wrong
   count. */
static void heap_is_intact_after_the_release(void)
{
    stage_all();
    fdps_free_global_resource_buffers();
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* One per PUSH in the assembly.  Each pins that this particular global is one
   of the nine the function hands to free() -- the check a stubbed-but-wrong
   symbol name fails, since a name nothing else touches would leave its block
   allocated and read zero here. */
static void frees_the_character_base_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(0), 1);
}

static void frees_the_character_growth_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(1), 1);
}

static void frees_the_enemy_data_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(2), 1);
}

static void frees_the_class_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(3), 1);
}

static void frees_the_item_effect_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(4), 1);
}

static void frees_the_class_equip_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(5), 1);
}

static void frees_the_spell_effect_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(6), 1);
}

static void frees_the_spell_learning_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(7), 1);
}

static void frees_the_promotion_table_pointer(void)
{
    CHECK_EQ(blocks_released_from_slot(8), 1);
}

/* Nothing in the body stores into any of the nine globals, so each still holds
   the address it held on the way in -- now dangling.  This is the contract
   that makes a second call a double free, and the only part of it a test can
   see. */
static void leaves_every_table_pointer_uncleared(void)
{
    unsigned char *staged[TABLE_COUNT];
    int i;

    stage_all();
    for (i = 0; i < TABLE_COUNT; i++) {
        staged[i] = *table_slots[i];
    }
    fdps_free_global_resource_buffers();
    for (i = 0; i < TABLE_COUNT; i++) {
        CHECK_EQ(*table_slots[i] == staged[i], 1);
    }
}

/* There is no CMP/JZ in front of any of the nine pushes, so a table that was
   never loaded is handed to free() like any other.  Nothing is released and
   the heap is untouched, because the CRT's free() returns on null -- not
   because the game checked. */
static void passes_a_null_pointer_to_free_unguarded(void)
{
    int before;
    int after;

    clear_all();
    before = used_heap_blocks();
    fdps_free_global_resource_buffers();
    after = used_heap_blocks();
    CHECK_EQ(before - after, 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* The container fdps_load_data_tables is always handed in the shipped program:
   fdps_load_global_resources opens "MISC.VFS" at 000296d0 and passes what comes
   back straight into the call at 000296fd. */
#define CONTAINER_NAME "MISC.VFS"

/* Longest member name is 12 characters; the loader upper-cases the query in
   place, so it has to be a buffer and not a literal read out of this file. */
#define QUERY_MAX 16

/* The nine members, in the order the assembly loads them, with the byte count
   MISC.VFS's own entry table records for each.  The nine counts are all
   different, which is what lets a memcmp against an independently loaded copy
   say "this global holds THIS member" rather than only "this global holds
   something". */
#define FRIAPRDA_BYTES 1440
#define FRILEVUP_BYTES 660
#define ITEM_BYTES 5773
#define ENEMYDAT_BYTES 910
#define PROMAP_BYTES 410
#define PROEQU_BYTES 216
#define MAGICDAT_BYTES 280
#define GETMGTAB_BYTES 720
#define RANKUP_BYTES 108

/* The container the fixture opened, kept for the whole loader half: opening it
   once and loading once is not an optimisation but the contract -- a second
   fdps_load_data_tables over the same globals would overwrite nine live blocks
   and leak them. */
static void *container;

/* Opens MISC.VFS and runs the loader over the nine globals, once.  Answers 1
   when the tables are in place, so a test can say so and then stop instead of
   dereferencing nothing. */
static int tables_are_loaded(void)
{
    if (container != NULL) {
        return 1;
    }
    clear_all();
    container = fdps_vfs_open(CONTAINER_NAME);
    if (container == NULL) {
        return 0;
    }
    fdps_load_data_tables(container);
    return 1;
}

/* Gives back everything the fixture took: the nine loaded blocks and the
   handle.  free() on the globals rather than the function under test, so the
   releaser's own tests still start from a heap this file controls. */
static void release_loaded_tables(void)
{
    int i;

    for (i = 0; i < TABLE_COUNT; i++) {
        free(*table_slots[i]);
    }
    clear_all();
    free(container);
    container = NULL;
}

/* The one assertion each of the nine calls in the body makes: the global at
   `index` came back holding the member called `name`.  The reference copy is
   loaded through fdps_vfs_load_file directly from the same container, so the
   comparison is against what that member's bytes are, and a call whose
   filename and destination were paired up wrongly in the transcription puts
   some other member's bytes in the slot and fails here. */
static void check_slot_holds_member(int index, char *name, unsigned int bytes)
{
    char query[QUERY_MAX];
    void *reference;

    CHECK_EQ(tables_are_loaded(), 1);
    if (container == NULL) {
        return;
    }
    CHECK_EQ(*table_slots[index] != NULL, 1);
    strcpy(query, name);
    reference = fdps_vfs_load_file(query, container);
    CHECK_EQ(reference != NULL, 1);
    if (reference != NULL && *table_slots[index] != NULL) {
        CHECK_EQ(memcmp(*table_slots[index], reference, bytes), 0);
    }
    free(reference);
}

/* Nine calls, nine globals written.  A slot still null would mean a group was
   dropped or aimed at some other symbol -- the loader writes through every
   destination it is given, and its wrapper does not return on a failed load. */
static void fills_every_table_pointer(void)
{
    int i;

    CHECK_EQ(tables_are_loaded(), 1);
    if (container == NULL) {
        return;
    }
    for (i = 0; i < TABLE_COUNT; i++) {
        CHECK_EQ(*table_slots[i] != NULL, 1);
    }
}

/* Nine separate loads, so nine separate malloc'd blocks: no two globals may
   hold the same address.  This is what a body that passed one destination
   twice would fail, whatever the filenames were. */
static void fills_them_with_nine_distinct_blocks(void)
{
    int i;
    int j;

    CHECK_EQ(tables_are_loaded(), 1);
    if (container == NULL) {
        return;
    }
    for (i = 0; i < TABLE_COUNT; i++) {
        for (j = i + 1; j < TABLE_COUNT; j++) {
            CHECK_EQ(*table_slots[i] != *table_slots[j], 1);
        }
    }
}

/* One per call site, in the order of the calls: 0001893c Friaprda.dat into
   00063fd8, 00018954 FriLevUp.dat into 00063fec, and so on to 000189fc
   RankUp.dat into 00063fdc. */
static void loads_friaprda_into_the_character_base_table(void)
{
    check_slot_holds_member(0, "FRIAPRDA.DAT", FRIAPRDA_BYTES);
}

static void loads_frilevup_into_the_character_growth_table(void)
{
    check_slot_holds_member(1, "FRILEVUP.DAT", FRILEVUP_BYTES);
}

static void loads_enemydat_into_the_enemy_data_table(void)
{
    check_slot_holds_member(2, "ENEMYDAT.DAT", ENEMYDAT_BYTES);
}

static void loads_promap_into_the_class_table(void)
{
    check_slot_holds_member(3, "PROMAP.DAT", PROMAP_BYTES);
}

static void loads_item_into_the_item_effect_table(void)
{
    check_slot_holds_member(4, "ITEM.DAT", ITEM_BYTES);
}

static void loads_proequ_into_the_class_equip_table(void)
{
    check_slot_holds_member(5, "PROEQU.DAT", PROEQU_BYTES);
}

static void loads_magicdat_into_the_spell_effect_table(void)
{
    check_slot_holds_member(6, "MAGICDAT.DAT", MAGICDAT_BYTES);
}

static void loads_getmgtab_into_the_spell_learning_table(void)
{
    check_slot_holds_member(7, "GETMGTAB.DAT", GETMGTAB_BYTES);
}

static void loads_rankup_into_the_promotion_table(void)
{
    check_slot_holds_member(8, "RANKUP.DAT", RANKUP_BYTES);
}

/* The argument is forwarded and not consumed: the handle the caller passed is
   still a working container after all nine loads, which is what lets
   fdps_load_global_resources go on using the same handle for another fourteen
   members at 00029705 and after.  Nothing in the body writes through it -- it
   is only ever pushed. */
static void leaves_the_container_handle_usable(void)
{
    char query[QUERY_MAX];
    void *again;

    CHECK_EQ(tables_are_loaded(), 1);
    if (container == NULL) {
        return;
    }
    strcpy(query, "RANKUP.DAT");
    again = fdps_vfs_load_file(query, container);
    CHECK_EQ(again != NULL, 1);
    free(again);
}

void run_main_tests(void)
{
    RUN_TEST(fills_every_table_pointer);
    RUN_TEST(fills_them_with_nine_distinct_blocks);

    RUN_TEST(loads_friaprda_into_the_character_base_table);
    RUN_TEST(loads_frilevup_into_the_character_growth_table);
    RUN_TEST(loads_enemydat_into_the_enemy_data_table);
    RUN_TEST(loads_promap_into_the_class_table);
    RUN_TEST(loads_item_into_the_item_effect_table);
    RUN_TEST(loads_proequ_into_the_class_equip_table);
    RUN_TEST(loads_magicdat_into_the_spell_effect_table);
    RUN_TEST(loads_getmgtab_into_the_spell_learning_table);
    RUN_TEST(loads_rankup_into_the_promotion_table);

    RUN_TEST(leaves_the_container_handle_usable);

    /* Everything the loader half put on the heap goes back before the releaser
       half starts staging blocks of its own into the same nine globals. */
    release_loaded_tables();

    RUN_TEST(releases_all_nine_table_buffers);
    RUN_TEST(heap_is_intact_after_the_release);

    RUN_TEST(frees_the_character_base_table_pointer);
    RUN_TEST(frees_the_character_growth_table_pointer);
    RUN_TEST(frees_the_enemy_data_table_pointer);
    RUN_TEST(frees_the_class_table_pointer);
    RUN_TEST(frees_the_item_effect_table_pointer);
    RUN_TEST(frees_the_class_equip_table_pointer);
    RUN_TEST(frees_the_spell_effect_table_pointer);
    RUN_TEST(frees_the_spell_learning_table_pointer);
    RUN_TEST(frees_the_promotion_table_pointer);

    RUN_TEST(leaves_every_table_pointer_uncleared);
    RUN_TEST(passes_a_null_pointer_to_free_unguarded);

    /* Put the globals back before leaving.  Every staged block has been freed
       by the function under test, so what the slots hold now is nine dangling
       addresses; a later unit that finds one of them and frees it again gets a
       double free that has nothing to do with its own code. */
    clear_all();
}
