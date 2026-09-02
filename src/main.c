/* main.c -- entry point and global resource lifecycle.
 *
 * See main.h.  Nothing here owns state of its own: the data-table pointers it
 * fills at startup, the resource pointers it releases at shutdown and the
 * scene-layer arrays it walks are all gamedata.c's, and the container they are
 * read out of belongs to whoever opened it.
 */
#include <stddef.h>
#include <stdlib.h>
#include "gamedata.h"
#include "keybd.h"
#include "vfs.h"
#include "main.h"

/* 00018930.  Nine identical groups, each MOV EAX,<destination global> / PUSH /
   MOV EAX,<filename> / PUSH / MOV EAX,[EBP + 0x14] / PUSH / CALL 00029400 /
   ADD ESP,0xc, and nothing else: one basic block, no compare, no jump and no
   local.  The frame is the canonical Watcom one with SUB ESP,0x0, and the
   argument is read fresh from [EBP + 0x14] before each of the nine calls
   rather than being kept in a register, which is what -od does with a
   parameter and not a sign that anything reassigns it.

   The PUSH triples and the ADD ESP,0xc belong to the callee's stack
   convention; this function's own is the same one, RET with no immediate.

   Nothing is done with what the loader leaves behind.  fdps_vfs_load_file_or_exit
   returns void and writes its answer through the third argument, so the
   destination global is written by the callee and read by nobody here; and its
   failure arm calls fdps_wait_any_key and exit(1) instead of coming back, which
   is why nine unguarded loads in a row need no test between them.

   The order of the nine calls is the order of the assembly, which is neither
   the order of the globals in memory nor the order fdps_free_global_resource_buffers
   frees them in.  Nothing observable depends on it -- the nine loads are
   independent -- so it is reproduced because it is what the original does.

   The names are passed as literals, exactly as the original passes pointers
   into its own literal pool at 0x6160c..0x61680.  That matters here rather
   than being a style choice: the loader upper-cases the name it is given IN
   PLACE, so each of these nine literals is folded to upper case by the first
   call and stays that way for the life of the process (see vfs.h). */
void fdps_load_data_tables(void *vfs)
{
    fdps_vfs_load_file_or_exit(vfs, "Friaprda.dat",
        (void **)&data_fdps_battle_character_base_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "FriLevUp.dat",
        (void **)&data_fdps_battle_character_growth_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "Item.dat",
        (void **)&data_fdps_item_effect_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "EnemyDat.dat",
        (void **)&data_fdps_battle_enemy_data_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "ProMap.dat",
        (void **)&data_fdps_class_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "ProEqu.dat",
        (void **)&data_fdps_class_equip_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "MagicDat.dat",
        (void **)&data_fdps_battle_spell_effect_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "GetMgTab.dat",
        (void **)&data_fdps_spell_learning_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "RankUp.dat",
        (void **)&data_fdps_promotion_table_ptr);
}

/* 00018a20.  Nine identical groups, PUSH dword ptr [global] / CALL free /
   ADD ESP,4, and nothing else: no branch, no compare, no local -- the frame is
   the canonical Watcom one with SUB ESP,0x0.  The PUSH and the ADD ESP,4
   belong to free()'s __cdecl convention, not to this function's, which takes
   nothing and returns nothing; the sole caller at 0002964f calls it with an
   empty stack and never looks at EAX.

   The order below is the order of the CALLs, which is not the order of the
   globals in memory (63fd8, 63fec, 63fd4, 63fd0, 63fe0, 63fe4, 63ff0, 63fe8,
   63fdc) and is not the order the loader at 00018930 fills them in either.
   Nothing observable depends on it -- free() has no ordering contract -- so it
   is reproduced because it is what the original does, not because anything is
   known to need it.

   No pointer is tested against null first, which is worth reading against the
   caller: fdps_shutdown_free_resources guards seven of its own pointers with
   CMP/JZ before freeing them and leaves nineteen unguarded, so the guard is a
   statement about which resources are optional, not a house style.  These nine
   are loaded unconditionally at startup, so an unguarded free matches; and in
   any case this CRT's free() returns at once on a null pointer (OR EAX,EAX /
   JZ inside the near-heap worker at 00043d9a), so a failed load costs nothing
   here.

   Nothing stores back into the nine globals -- there is no MOV [global],0
   anywhere in the body -- so they are left holding freed addresses.  That is
   what makes "runs exactly once" a contract rather than a preference: a second
   call is a double free, and the pointers give the caller no way to tell. */
void fdps_free_global_resource_buffers(void)
{
    free(data_fdps_battle_character_base_table_ptr);
    free(data_fdps_battle_character_growth_table_ptr);
    free(data_fdps_battle_enemy_data_table_ptr);
    free(data_fdps_class_table_ptr);
    free(data_fdps_item_effect_table_ptr);
    free(data_fdps_class_equip_table_ptr);
    free(data_fdps_battle_spell_effect_table_ptr);
    free(data_fdps_spell_learning_table_ptr);
    free(data_fdps_promotion_table_ptr);
}

/* 00029440.  Three straight-line stretches and one counted loop, in this
   order: eighteen unguarded free() groups, seven CMP/JZ-guarded ones, then a
   for-loop over three parallel arrays, then two calls with an empty stack.
   The frame is the canonical Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP,
   SUB ESP,0x4 for the single local -- and the sole caller at 000293b8 pushes
   nothing before the CALL, pops nothing after it and never reads EAX, so the
   signature is void(void) with a stack purge of 0.

   The frees are the assembly's order, which is neither the order the globals
   sit in memory nor the order fdps_load_global_resources fills them.  Nothing
   observable depends on it -- free() has no ordering contract and none of the
   blocks points at another -- so it is reproduced because it is what the
   original does.

   The eighteen unguarded ones are the startup resources: a load that fails
   never returns (fdps_wait_any_key then exit(1)) and the roster block is a
   plain malloc that is never released before now, so past startup every one of
   them is live.  Seventeen of them lie inside 0x643a0-0x643e4 and the
   eighteenth, the roster at 0x64108, does not; the seventeen are still
   seventeen individually named globals and not a slice of an array, and the
   assembly proves it by stepping over 0x643a4 -- data_fdps_shared_party_total_gold,
   an int -- in the middle of the run (rebuild_info/pitfalls.md, contract B).

   The seven guarded ones are the optional resources, and the guard is a
   statement about them rather than a house style: each is null until something
   loads it, and each has call sites of its own that free it and store 0 back.
   The guard cannot be observed from outside -- this CRT's free() begins OR
   EAX,EAX / JZ inside the near-heap worker at 00043d9a and so returns at once
   on null -- so it is here because the original tests, not because the test
   changes what happens.

   The loop bound is data_fdps_scene_layer_count and the compare is JL at
   000295f9, the SIGNED branch, which is what makes both sides int
   (rebuild_info/pitfalls.md, contract C).  Its three arrays are declared six
   wide and the count comes out of a chapter's layer descriptor file with
   nothing clamping it (see gamedata.h); the shipped descriptors are what keeps
   it in range, here as in every other walk over those arrays.  The assembly
   scales the index itself -- MOV EAX,[EBP-0x4] / LEA EAX,[EAX*0x4+0x0] / PUSH
   dword ptr [EAX + 0x69cb0] -- and each of the three bases is a full literal
   displacement, so none of the three is a folded offset into its neighbour
   (contract H).

   Nothing is stored back into any of the twenty-five pointers or the three
   arrays, so all of them are left holding freed addresses.  With the single
   call site immediately before the process ends, that is a contract rather
   than a leak: a second call would be a double free, and nothing here would
   be able to tell. */
void fdps_shutdown_free_resources(void)
{
    int layer_slot;

    free(data_fdps_roster_array_ptr);
    free(data_fdps_animation_baseani_archive_ptr);
    free(data_fdps_audio_basewav_sfx_bank_buf_ptr);
    free(data_fdps_vga_main_palette_ptr);
    free(data_fdps_vga_fight_palette_ptr);
    free(data_fdps_cursor_highlight_sprite_sheet_ptr);
    free(data_fdps_shadow_sprite_sheet_ptr);
    free(data_fdps_command_sprite_sheet_ptr);
    free(data_fdps_unit_status_icon_sheet_ptr);
    free(data_fdps_number_glyph_sheet_ptr);
    free(data_fdps_status_gauge_bar_sheet_ptr);
    free(data_fdps_font_sheet_ptr);
    free(data_fdps_all_game_text_ptr);
    free(data_fdps_unit_gauge_sheet_ptr);
    free(data_fdps_message_window_sheet_ptr);
    free(data_fdps_selection_bar_sheet_ptr);
    free(data_fdps_level_up_window_sheet_ptr);
    free(data_fdps_ui_terrain_hud_panel_sheet_ptr);

    if (data_fdps_current_chapter_text_ptr != NULL) {
        free(data_fdps_current_chapter_text_ptr);
    }
    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
    }
    if (data_fdps_battle_move_grid_ptr != NULL) {
        free(data_fdps_battle_move_grid_ptr);
    }
    if (data_fdps_cel_sprite_cache_ptr != NULL) {
        free(data_fdps_cel_sprite_cache_ptr);
    }
    if (data_fdps_tile_event_data_table_ptr != NULL) {
        free(data_fdps_tile_event_data_table_ptr);
    }
    if (data_fdps_map_cell_event_code_layer_ptr != NULL) {
        free(data_fdps_map_cell_event_code_layer_ptr);
    }
    if (data_fdps_map_unit_array_ptr != NULL) {
        free(data_fdps_map_unit_array_ptr);
    }

    for (layer_slot = 0; layer_slot < data_fdps_scene_layer_count;
         layer_slot++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer_slot]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer_slot]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer_slot]);
    }

    fdps_free_global_resource_buffers();
    fdps_uninstall_keyboard_isr();
}
