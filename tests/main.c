/* tests/main.c -- cover for src/main.c.
 *
 * Expected values come from the assembly at 00018a20 -- nine PUSH dword ptr
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
#include <malloc.h>
#include "testharn.h"
#include "gamedata.h"
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

void run_main_tests(void)
{
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
