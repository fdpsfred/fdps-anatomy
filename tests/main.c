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
 *
 * The third function, fdps_shutdown_free_resources, is measured the same way
 * and needs one thing more.  Its last act is fdps_uninstall_keyboard_isr, a
 * real INT 21h AH=25h that files whatever data_fdps_input_prev_int9_handler_selector
 * and data_fdps_prev_int9_handler_offset hold onto interrupt vector 09h -- and
 * under test nothing has installed anything, so those two hold zero and the
 * call would point the keyboard at 0000:00000000 for the rest of the run.  So
 * every call to it below is fenced the way tests/keybd.c fences its own: IRQ1
 * masked at the 8259 for the duration, the machine's own vector 09h read
 * first, staged into those two globals so the uninstall restores it, and
 * written back afterwards regardless.  The mask stops delivery outright rather
 * than deferring it the way CLI would, which CLI could not do here because DOS
 * re-enables interrupts inside the very INT 21h being made.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "testharn.h"
#include "gamedata.h"
#include "keybd.h"
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

/* --- fdps_shutdown_free_resources at 00029440 ------------------------------
 *
 * Twenty-five individually named global pointers, three parallel scene-layer
 * pointer arrays walked to data_fdps_scene_layer_count, then
 * fdps_free_global_resource_buffers and fdps_uninstall_keyboard_isr.  It takes
 * nothing and returns nothing, so it is measured through the heap exactly as
 * the releaser above is: a used entry becomes a free entry the moment it is
 * released, so the drop in used entries is the number of live blocks that
 * reached free().
 *
 * The counts below come from the assembly and from nowhere else: eighteen
 * unguarded groups at 0002944c..00029545, seven CMP/JZ-guarded ones at
 * 00029548..000295e9, and the loop body at 00029605..0002964d freeing three
 * dwords per iteration under CMP EAX,[0x00069cdc] / JL at 000295f3.
 *
 * The seven guards are deliberately not asserted about: free() returns at once
 * on a null pointer, so a body with the guards and a body without them do the
 * same observable thing, and a test claiming otherwise would be asserting
 * about the shape of the C rather than about behaviour.
 * -------------------------------------------------------------------------- */

/* INT 21h AH=35h for vector 09h, offset as the result and selector through the
   pointer.  Separate in-line assembly from src/keybd.c's on purpose: the
   fence has to learn this machine's own vector 09h independently of anything
   the code under test does with it. */
extern unsigned int shutdown_read_int9_vector(unsigned short *selector_out);
#pragma aux shutdown_read_int9_vector = \
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

/* INT 21h AH=25h for vector 09h with an arbitrary selector:offset -- what
   putting the machine's own handler back needs. */
extern void shutdown_write_int9_vector(unsigned short selector,
                                       unsigned int offset);
#pragma aux shutdown_write_int9_vector = \
    "push ds"                            \
    "mov  eax,2509h"                     \
    "mov  ds,cx"                         \
    "int  21h"                           \
    "pop  ds"                            \
    parm [cx] [edx]                      \
    modify [eax ebx ecx edx];

/* Set bit 1 of the master 8259's mask register so IRQ1 cannot be delivered,
   and hand the mask back as it was found so it can be put back byte for
   byte. */
extern unsigned char shutdown_mask_irq1(void);
#pragma aux shutdown_mask_irq1 =        \
    "in   al,21h"                       \
    "mov  ah,al"                        \
    "or   al,2"                         \
    "out  21h,al"                       \
    "mov  al,ah"                        \
    value [al]                          \
    modify [eax];

extern void shutdown_restore_irq_mask(unsigned char mask);
#pragma aux shutdown_restore_irq_mask = "out 21h,al" parm [al] modify [eax];

/* The eighteen pointers freed with no null test, in the order of the eighteen
   PUSH/CALL/ADD groups: 0002944c 0x64108, 0002945a 0x643a8, 00029468 0x643a0,
   00029476 0x643bc, 00029484 0x643e4, 00029492 0x643b8, 000294a0 0x643dc,
   000294ae 0x643ac, 000294bc 0x643d4, 000294ca 0x643d8, 000294d8 0x643c8,
   000294e6 0x643cc, 000294f4 0x643c4, 00029502 0x643b4, 00029510 0x643d0,
   0002951e 0x643c0, 0002952c 0x643b0, 0002953a 0x643e0. */
#define UNGUARDED_COUNT 18

static unsigned char **const unguarded_slots[UNGUARDED_COUNT] = {
    &data_fdps_roster_array_ptr,                  /* 00064108 */
    &data_fdps_animation_baseani_archive_ptr,     /* 000643a8 */
    &data_fdps_audio_basewav_sfx_bank_buf_ptr,    /* 000643a0 */
    &data_fdps_vga_main_palette_ptr,              /* 000643bc */
    &data_fdps_vga_fight_palette_ptr,             /* 000643e4 */
    &data_fdps_cursor_highlight_sprite_sheet_ptr, /* 000643b8 */
    &data_fdps_shadow_sprite_sheet_ptr,           /* 000643dc */
    &data_fdps_command_sprite_sheet_ptr,          /* 000643ac */
    &data_fdps_unit_status_icon_sheet_ptr,        /* 000643d4 */
    &data_fdps_number_glyph_sheet_ptr,            /* 000643d8 */
    &data_fdps_status_gauge_bar_sheet_ptr,        /* 000643c8 */
    &data_fdps_font_sheet_ptr,                    /* 000643cc */
    &data_fdps_all_game_text_ptr,                 /* 000643c4 */
    &data_fdps_unit_gauge_sheet_ptr,              /* 000643b4 */
    &data_fdps_message_window_sheet_ptr,          /* 000643d0 */
    &data_fdps_selection_bar_sheet_ptr,           /* 000643c0 */
    &data_fdps_level_up_window_sheet_ptr,         /* 000643b0 */
    &data_fdps_ui_terrain_hud_panel_sheet_ptr     /* 000643e0 */
};

/* The seven freed under a CMP/JZ, in the order of the seven guarded groups:
   00029548 0x60124, 0002955f 0x60120, 00029576 0x60144, 0002958d 0x60138,
   000295a4 0x6013c, 000295bb 0x60148, 000295d2 0x69cd8. */
#define GUARDED_COUNT 7

static unsigned char **const guarded_slots[GUARDED_COUNT] = {
    &data_fdps_current_chapter_text_ptr,      /* 00060124 */
    &data_fdps_portrait_sprite_buf_ptr,       /* 00060120 */
    &data_fdps_battle_move_grid_ptr,          /* 00060144 */
    &data_fdps_cel_sprite_cache_ptr,          /* 00060138 */
    &data_fdps_tile_event_data_table_ptr,     /* 0006013c */
    &data_fdps_map_cell_event_code_layer_ptr, /* 00060148 */
    &data_fdps_map_unit_array_ptr             /* 00069cd8 */
};

/* The three arrays the loop walks, in the order of the three frees inside one
   iteration: 0002960f 0x69cb0, 00029627 0x69c20, 0002963f 0x69c98.  Each base
   is a full literal displacement in its own instruction, so these are three
   arrays and not one walked with an offset. */
#define LAYER_ARRAY_COUNT 3

/* Six, the width all three are declared at in gamedata.h. */
#define LAYER_SLOTS 6

static unsigned char **const layer_arrays[LAYER_ARRAY_COUNT] = {
    data_fdps_scene_layer_tile_map_ptrs,   /* 00069cb0 */
    data_fdps_scene_layer_tile_sheet_ptrs, /* 00069c20 */
    data_fdps_scene_layer_tile_attr_ptr    /* 00069c98 */
};

/* Everything the function touches, back to the state a fresh process would
   have: no pointer live, no layer slot filled and no layer to walk.  The nine
   data tables go too, because fdps_free_global_resource_buffers is called from
   inside the function under test and would otherwise free whatever the loader
   half of this file left in them. */
static void shutdown_clear_everything(void)
{
    int slot;
    int array;

    clear_all();
    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        *unguarded_slots[slot] = NULL;
    }
    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        *guarded_slots[slot] = NULL;
    }
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            layer_arrays[array][slot] = NULL;
        }
    }
    data_fdps_scene_layer_count = 0;
}

/* What one fenced shutdown saw.  File-scope for the same reason tests/keybd.c
   keeps its probe results there: several cases assert about different parts of
   one observation. */
static unsigned char shutdown_latch_after;
static unsigned short shutdown_vector_selector_after;
static unsigned int shutdown_vector_offset_after;
static unsigned short shutdown_vector_selector_staged;
static unsigned int shutdown_vector_offset_staged;

/* One real call, with the keyboard fenced.  This machine's vector 09h is read
   first and staged into the two globals the uninstall restores from, so the
   uninstall inside the function puts back the handler that was already there
   instead of the 0000:00000000 those globals would otherwise hold; the vector
   and the 8259 mask are then written back anyway, because a call that did not
   reach the uninstall must leave the machine intact for the cases after it. */
static void shutdown_call_fenced(void)
{
    unsigned char saved_mask;
    unsigned char saved_scancode;
    unsigned short saved_selector;
    unsigned int saved_offset;

    saved_scancode = data_fdps_input_last_scancode;
    saved_mask = shutdown_mask_irq1();
    saved_offset = shutdown_read_int9_vector(&saved_selector);

    data_fdps_input_prev_int9_handler_selector = saved_selector;
    data_fdps_prev_int9_handler_offset = saved_offset;
    shutdown_vector_selector_staged = saved_selector;
    shutdown_vector_offset_staged = saved_offset;

    /* A key the handler might have latched a moment earlier, so that the 0xff
       the uninstall stores cannot pass on a byte that already held it. */
    data_fdps_input_last_scancode = 0x39;   /* space */

    fdps_shutdown_free_resources();

    shutdown_latch_after = data_fdps_input_last_scancode;
    shutdown_vector_offset_after =
        shutdown_read_int9_vector(&shutdown_vector_selector_after);

    shutdown_write_int9_vector(saved_selector, saved_offset);
    shutdown_restore_irq_mask(saved_mask);
    data_fdps_input_last_scancode = saved_scancode;
}

/* How many live blocks one fenced shutdown released.  The fence itself
   allocates nothing, so the whole difference belongs to the function. */
static int shutdown_released_blocks(void)
{
    int before;
    int after;

    before = used_heap_blocks();
    shutdown_call_fenced();
    after = used_heap_blocks();
    return before - after;
}

/* Hand the function one live block in `slot` and nothing else anywhere, and
   report how many blocks it released.  One means that global reached free();
   zero means the emitted C is reading some other symbol in its place -- which
   is the failure a name that is real but simply wrong produces, since the
   build stubs an unknown global rather than rejecting it. */
static int shutdown_released_from(unsigned char **slot)
{
    shutdown_clear_everything();
    *slot = (unsigned char *) malloc(BLOCK_BYTES);
    return shutdown_released_blocks();
}

/* Eighteen unguarded groups, so eighteen blocks go back when all eighteen
   globals are live.  Seventeen would mean a group was dropped in the
   transcription and nineteen that one was written twice. */
static void releases_all_eighteen_unguarded_pointers(void)
{
    int slot;

    shutdown_clear_everything();
    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        *unguarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
    }
    CHECK_EQ(shutdown_released_blocks(), UNGUARDED_COUNT);
}

/* One assertion per PUSH: this particular global is one of the eighteen. */
static void frees_each_unguarded_pointer(void)
{
    int slot;

    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        CHECK_EQ(shutdown_released_from(unguarded_slots[slot]), 1);
    }
}

/* Seven guarded groups, seven blocks back when all seven are live. */
static void releases_all_seven_guarded_pointers(void)
{
    int slot;

    shutdown_clear_everything();
    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        *guarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
    }
    CHECK_EQ(shutdown_released_blocks(), GUARDED_COUNT);
}

/* One assertion per guarded PUSH, same reading as the unguarded ones. */
static void frees_each_guarded_pointer(void)
{
    int slot;

    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        CHECK_EQ(shutdown_released_from(guarded_slots[slot]), 1);
    }
}

/* Every pointer null and no layer loaded: nothing is released and the heap is
   untouched.  Twenty-five free(NULL) calls, nine more inside
   fdps_free_global_resource_buffers, and the CRT returns from each at once. */
static void releases_nothing_when_every_pointer_is_null(void)
{
    shutdown_clear_everything();
    CHECK_EQ(shutdown_released_blocks(), 0);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

/* The loop walks to data_fdps_scene_layer_count and no further.  Eighteen
   blocks are staged -- all six slots of all three arrays -- with the count set
   to two, and exactly six come back: two iterations times the three frees in
   the body.  A body that walked to six instead would release eighteen, and one
   that walked the wrong array would release fewer. */
static void walks_the_layer_arrays_only_as_far_as_the_count(void)
{
    int array;
    int slot;

    shutdown_clear_everything();
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            layer_arrays[array][slot] =
                (unsigned char *) malloc(BLOCK_BYTES);
        }
    }
    data_fdps_scene_layer_count = 2;
    CHECK_EQ(shutdown_released_blocks(), 2 * LAYER_ARRAY_COUNT);
    CHECK_EQ(_heapchk(), _HEAPOK);

    /* The twelve the loop was not allowed to reach are still live. */
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 2; slot < LAYER_SLOTS; slot++) {
            free(layer_arrays[array][slot]);
        }
    }
    shutdown_clear_everything();
}

/* A count of zero is a loop that does not run: the compare at 000295f3 is made
   before the body, so eighteen staged blocks all survive. */
static void frees_no_layer_slot_when_the_count_is_zero(void)
{
    int array;
    int slot;

    shutdown_clear_everything();
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            layer_arrays[array][slot] =
                (unsigned char *) malloc(BLOCK_BYTES);
        }
    }
    data_fdps_scene_layer_count = 0;
    CHECK_EQ(shutdown_released_blocks(), 0);

    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            free(layer_arrays[array][slot]);
        }
    }
    shutdown_clear_everything();
}

/* Each of the three frees in the loop body reaches a different array: one
   block in one array's slot 0 with a count of one releases exactly that
   one. */
static void frees_slot_zero_of_each_of_the_three_layer_arrays(void)
{
    int array;

    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        shutdown_clear_everything();
        layer_arrays[array][0] = (unsigned char *) malloc(BLOCK_BYTES);
        data_fdps_scene_layer_count = 1;
        CHECK_EQ(shutdown_released_blocks(), 1);
    }
    shutdown_clear_everything();
}

/* CALL 0x00018a20 at 0002964f: the nine data-table pointers are released by
   the shutdown as well, through fdps_free_global_resource_buffers.  Nine
   blocks staged into those nine globals and nothing anywhere else, so nine is
   the whole answer. */
static void releases_the_nine_data_tables_through_the_table_releaser(void)
{
    shutdown_clear_everything();
    stage_all();
    CHECK_EQ(shutdown_released_blocks(), TABLE_COUNT);
}

/* CALL 0x00056818 at 00029654, the last instruction before the epilogue: the
   keyboard hook comes down.  Two things say so and neither can be produced by
   the frees.  The latch at 0x00070006 is 0xff, which only
   fdps_uninstall_keyboard_isr's MOV byte ptr [0x00070006],0xff at 0005682f
   writes; and vector 09h holds the selector:offset pair the fence staged into
   the two saved-vector globals, which only that function's INT 21h AH=25h can
   have filed. */
static void uninstalls_the_keyboard_isr(void)
{
    shutdown_clear_everything();
    shutdown_call_fenced();
    CHECK_EQ(shutdown_latch_after, 0xff);
    CHECK_EQ(shutdown_vector_offset_after == shutdown_vector_offset_staged, 1);
    CHECK_EQ(shutdown_vector_selector_after == shutdown_vector_selector_staged,
             1);
}

/* Nothing in the body stores into any of the twenty-five globals or into any
   layer slot -- there is no MOV [global],0 anywhere between 00029440 and
   0002965f -- so every one of them still holds the address it held on the way
   in, now dangling.  That is what makes a second call a double free, and it is
   the only part of the contract a test can see. */
static void leaves_every_pointer_uncleared(void)
{
    unsigned char *staged_unguarded[UNGUARDED_COUNT];
    unsigned char *staged_guarded[GUARDED_COUNT];
    unsigned char *staged_layers[LAYER_ARRAY_COUNT][LAYER_SLOTS];
    int slot;
    int array;

    shutdown_clear_everything();
    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        *unguarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
        staged_unguarded[slot] = *unguarded_slots[slot];
    }
    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        *guarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
        staged_guarded[slot] = *guarded_slots[slot];
    }
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            layer_arrays[array][slot] =
                (unsigned char *) malloc(BLOCK_BYTES);
            staged_layers[array][slot] = layer_arrays[array][slot];
        }
    }
    data_fdps_scene_layer_count = LAYER_SLOTS;

    shutdown_call_fenced();

    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        CHECK_EQ(*unguarded_slots[slot] == staged_unguarded[slot], 1);
    }
    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        CHECK_EQ(*guarded_slots[slot] == staged_guarded[slot], 1);
    }
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            CHECK_EQ(layer_arrays[array][slot] == staged_layers[array][slot],
                     1);
        }
    }
    shutdown_clear_everything();
}

/* One whole shutdown with everything live at once -- eighteen, seven, six
   layers times three, and the nine tables -- releases all fifty-two and leaves
   a walkable heap.  A free() aimed at something that was never malloc'd, an
   interior pointer or a global that is not a heap block at all, shows up here
   rather than as a wrong count. */
static void releases_every_block_a_full_shutdown_holds(void)
{
    int slot;
    int array;

    shutdown_clear_everything();
    for (slot = 0; slot < UNGUARDED_COUNT; slot++) {
        *unguarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
    }
    for (slot = 0; slot < GUARDED_COUNT; slot++) {
        *guarded_slots[slot] = (unsigned char *) malloc(BLOCK_BYTES);
    }
    for (array = 0; array < LAYER_ARRAY_COUNT; array++) {
        for (slot = 0; slot < LAYER_SLOTS; slot++) {
            layer_arrays[array][slot] =
                (unsigned char *) malloc(BLOCK_BYTES);
        }
    }
    data_fdps_scene_layer_count = LAYER_SLOTS;
    stage_all();

    CHECK_EQ(shutdown_released_blocks(),
             UNGUARDED_COUNT + GUARDED_COUNT
                 + LAYER_ARRAY_COUNT * LAYER_SLOTS + TABLE_COUNT);
    CHECK_EQ(_heapchk(), _HEAPOK);
    shutdown_clear_everything();
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

    /* Put the globals back before the shutdown half starts staging blocks of
       its own into the same nine.  Every staged block has been freed by the
       function under test, so what the slots hold now is nine dangling
       addresses; a later unit that finds one of them and frees it again gets a
       double free that has nothing to do with its own code. */
    clear_all();

    RUN_TEST(releases_all_eighteen_unguarded_pointers);
    RUN_TEST(frees_each_unguarded_pointer);
    RUN_TEST(releases_all_seven_guarded_pointers);
    RUN_TEST(frees_each_guarded_pointer);
    RUN_TEST(releases_nothing_when_every_pointer_is_null);

    RUN_TEST(walks_the_layer_arrays_only_as_far_as_the_count);
    RUN_TEST(frees_no_layer_slot_when_the_count_is_zero);
    RUN_TEST(frees_slot_zero_of_each_of_the_three_layer_arrays);

    RUN_TEST(releases_the_nine_data_tables_through_the_table_releaser);
    RUN_TEST(uninstalls_the_keyboard_isr);

    RUN_TEST(leaves_every_pointer_uncleared);
    RUN_TEST(releases_every_block_a_full_shutdown_holds);

    /* And out with every global this file touched back at null, so nothing
       downstream finds one of the dangling addresses the shutdown left. */
    shutdown_clear_everything();
}
