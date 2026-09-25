/* main.c -- entry point and global resource lifecycle.
 *
 * main is the C entry the Watcom startup's __CMain calls; it runs the whole
 * program from the installation check to the farewell line.
 *
 * See main.h.  Nothing here owns state of its own: the data-table pointers it
 * fills at startup, the resource pointers it releases at shutdown and the
 * scene-layer arrays it walks are all gamedata.c's, and the container they are
 * read out of belongs to whoever opened it.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <i86.h>
#include "fdpstype.h"
#include "audio.h"
#include "blit.h"
#include "btlturn.h"
#include "cd.h"
#include "cdaudio.h"
#include "chapter.h"
#include "gamedata.h"
#include "keybd.h"
#include "palette.h"
#include "title.h"
#include "vfs.h"
#include "village.h"
#include "main.h"

/* The party roster block: PUSH 0xa00 / CALL malloc / MOV [0x00064108],EAX at
   000296b8.  Thirty-two members of 0x50 bytes, as gamedata.h records. */
#define ROSTER_BLOCK_BYTES 0xa00

/* The font metrics the startup loader seeds, one constant per MOV between
   0002966c and 000296b3.  The two cell dimensions and the decoration selector
   are byte stores; the other five are dword stores. */
#define FONT_GLYPH_WIDTH 0x10
#define FONT_GLYPH_CELL_HEIGHT 0x10
#define FONT_SHADOW_OFFSET_X 1
#define FONT_SHADOW_ROW_OFFSET 1
#define FONT_GLYPH_STRIDE_BYTES 0x20
#define FONT_OUTLINE_DISABLED 0
#define FONT_GLYPH_ADVANCE_X 0x10
#define FONT_LINE_HEIGHT 0x12

/* A .CEL's sub-image offset table starts at byte 0x0f of the file and holds
   one dword per sub-image, each the distance from the file's own base to that
   sub-image's RLE stream: MOV EDX,[EBP-0x10] / ADD EDX,EAX / MOV EAX,[EBP-0x10]
   / ADD EAX,dword ptr [EDX + 0xf] at 00029d1d and again at 00029e1d. */
#define CEL_SUB_IMAGE_TABLE_OFFSET 0x0f

/* Both composites take the first three sub-images of their source sheet and
   nothing else: MOV [EBP-0x8],0x0 / CMP [EBP-0x8],0x3 / JL at 00029cfc and
   00029dfc. */
#define GAUGE_SUB_IMAGE_COUNT 3

/* EasyBar.cel's three 43x6 gauges, packed one after another with the
   destination pitch equal to the sub-image width, so each occupies exactly
   43 * 6 = 0x102 bytes and the buffer is 3 * 0x102 = 0x306. */
#define UNIT_GAUGE_WIDTH 0x2b
#define UNIT_GAUGE_ROWS 6
#define UNIT_GAUGE_STRIDE 0x102
#define UNIT_GAUGE_SHEET_BYTES 0x306

/* Bar.cel's three 117x8 bars, packed the same way: 117 * 8 = 0x3a8 each and
   3 * 0x3a8 = 0xaf8 for the buffer. */
#define STATUS_GAUGE_WIDTH 0x75
#define STATUS_GAUGE_ROWS 8
#define STATUS_GAUGE_STRIDE 0x3a8
#define STATUS_GAUGE_SHEET_BYTES 0xaf8

/* Mode 0 of fdps_blit_dispatch, the plain copy: PUSH 0x0 for the mode and
   PUSH 0x0 for the operand mode 0 never reads (src/blit.h). */
#define GAUGE_BLIT_MODE 0
#define GAUGE_BLIT_OPERAND 0

/* The two palette lookup tables' byte counts, as the fread and fwrite calls
   count them: 0x4800 for the 18-row shade ramp and 0x1000 for the 16x16x16
   inverse cube. */
#define SHADE_RAMP_BYTES 0x4800
#define INVERSE_CUBE_BYTES 0x1000

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

/* 00029660.  The startup loader, 0xad9 bytes of straight-line code with two
   counted loops and one two-armed branch in it.  The frame is the canonical
   Watcom one -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x1ac -- and the
   sole caller, main at 00029316, pushes nothing, pops nothing and never reads
   EAX, so the signature is void(void) with a stack purge of 0.

   Only five of the 0x1ac bytes of frame are variables: the container handle at
   [EBP-0x14], the source sheet at [EBP-0x10], the sub-image pointer at
   [EBP-0xc], the loop counter at [EBP-0x8] and the cache FILE * at [EBP-0x4].
   Everything above [EBP-0x18] is argument scratch, three dwords per resource
   load, written and read once each and never afterwards.

   Those scratch triples are why each of the fifteen loads is fourteen
   instructions rather than five: what stands at each of them is the body of
   fdps_vfs_load_file_or_exit (00029400) expanded in place -- the same CALL
   00039bd0, the same MOV [EDX],EAX through a pointer to the destination, the
   same CMP dword ptr [EAX],0x0 / CALL fdps_wait_any_key / exit(1) -- with the
   wrapper's three parameters standing in this frame instead of on the stack.
   There is no CALL 00029400 anywhere in this function, so the C below does not
   make one either: the load and its guard are written out fifteen times, which
   is what the original executes.

   The container handle is one variable used twice.  Misc.vfs is opened at
   000296d0, thirteen members and the two gauge sheets come out of it, it is
   freed at 00029e63, and the same slot then takes Field.vfs from 00029e71 for
   the last two members.  The two failure arms differ from the fifteen loads'
   and from each other: a container that will not open prints its own complete
   message -- one PUSH and ADD ESP,0x4, so a single argument with the filename
   already in the literal, and the two literals disagree about case, Misc.vfs
   at 00061cb0 against field.vfs at 00061d84 -- and then exits.  A member that
   will not load prints nothing here; fdps_vfs_load_file has already said which
   member it was (src/vfs.h), and the arm only waits for a key and exits.

   The nine data tables are loaded through fdps_load_data_tables at 000296fd,
   before any of the fifteen, and out of the same handle.

   Both composite loops are the same shape: CMP [EBP-0x8],0x3 / JL, the signed
   compare, with the increment block sitting above the body and jumped back to,
   which is what -od does with a for-statement.  Each iteration reads a dword
   out of the source sheet's offset table at +0xf, adds it to the sheet's own
   base, and blits 43x6 (or 117x8) pixels to base + index * stride with the
   destination pitch equal to the source width -- so the three sub-images tile
   their buffer end to end with no gap.  The buffer is malloc'd and memset to
   zero first, which is what leaves a transparent pixel transparent: mode 0
   skips the pixels the RLE stream does not cover, and the zero underneath is
   what shows.  The source sheet is freed straight after each loop; only the
   composites survive.

   The palette-blend cache is the one branch: TEST EAX,EAX / JZ 0002a0ba after
   access("FMer1.tmp", 0), so the fall-through arm is the one taken when the
   file is NOT there.  That arm builds the tables twice -- Fight.pal first,
   written out as FMer1.tmp and FMer2.tmp, then Fde.pal, written out as
   Mer1.tmp and Mer2.tmp -- and the second build is what the program runs on,
   because fdps_build_palette_tables replaces both tables outright
   (src/palette.h).  The taken arm reads Mer1.tmp and Mer2.tmp back, which is
   the same two files and therefore the same Fde.pal-derived contents.  The
   Fight.pal pair is not read here: every reference to the two literals at
   00061dc0 and 00061dd0 is inside this function, and in it FMer1.tmp is only
   looked at by the access() that decides the branch.  The pair is read back
   elsewhere, each reader through its own copy of the two names: the combat
   exchange (src/combat.c), the spell animation (src/cmbspell.c), the church
   promotion (src/church.c) and the ending credit roll (src/ending.c) each
   load FMer1.tmp and FMer2.tmp over the same two globals for the length of
   their fight-palette screen and put Mer1.tmp and Mer2.tmp back afterwards.
   Only FMer1.tmp is tested for; nothing checks that the other three are, and
   no fopen result is tested at all, so a partial cache directory hands a null
   FILE * to fread.  That is the original's behaviour and no check is added.

   The stores into 0006403c..0006404f are eight separate absolute
   displacements, three byte-wide and five dword-wide, and every reader of
   those globals addresses them the same way; nothing indexes across them, so
   they stay eight globals here (rebuild_info/pitfalls.md, contract B). */
void fdps_load_global_resources(void)
{
    void *archive;
    unsigned char *source_cel;
    unsigned char *sub_image;
    int sub_image_index;
    FILE *cache_file;

    data_fdps_font_glyph_width = FONT_GLYPH_WIDTH;
    data_fdps_glyph_cell_height = FONT_GLYPH_CELL_HEIGHT;
    data_fdps_font_shadow_offset_x = FONT_SHADOW_OFFSET_X;
    data_fdps_glyph_shadow_row_offset = FONT_SHADOW_ROW_OFFSET;
    data_fdps_font_glyph_stride_bytes = FONT_GLYPH_STRIDE_BYTES;
    data_fdps_font_outline_enabled_flag = FONT_OUTLINE_DISABLED;
    data_fdps_glyph_advance_x = FONT_GLYPH_ADVANCE_X;
    data_fdps_font_line_height = FONT_LINE_HEIGHT;

    fdps_install_keyboard_isr();
    data_fdps_roster_array_ptr = (unsigned char *) malloc(ROSTER_BLOCK_BYTES);

    archive = fdps_vfs_open("MISC.VFS");
    if (archive == NULL) {
        printf("file not found: 'Misc.vfs'\n");
        exit(1);
    }

    fdps_load_data_tables(archive);

    data_fdps_vga_main_palette_ptr =
        (unsigned char *) fdps_vfs_load_file("Fde.pal", archive);
    if (data_fdps_vga_main_palette_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_vga_fight_palette_ptr =
        (unsigned char *) fdps_vfs_load_file("Fight.pal", archive);
    if (data_fdps_vga_fight_palette_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_cursor_highlight_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Cusor.cel", archive);
    if (data_fdps_cursor_highlight_sprite_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_command_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Command.cel", archive);
    if (data_fdps_command_sprite_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_shadow_sprite_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Shadow.cel", archive);
    if (data_fdps_shadow_sprite_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_unit_status_icon_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("IconSts.cel", archive);
    if (data_fdps_unit_status_icon_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_message_window_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Message.cel", archive);
    if (data_fdps_message_window_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_number_glyph_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Number.cel", archive);
    if (data_fdps_number_glyph_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_selection_bar_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("SelBar.cel", archive);
    if (data_fdps_selection_bar_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_level_up_window_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("LevUp.cel", archive);
    if (data_fdps_level_up_window_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_audio_basewav_sfx_bank_buf_ptr =
        (unsigned char *) fdps_vfs_load_file("BaseWav.vfs", archive);
    if (data_fdps_audio_basewav_sfx_bank_buf_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_animation_baseani_archive_ptr =
        (unsigned char *) fdps_vfs_load_file("BaseAni.vfs", archive);
    if (data_fdps_animation_baseani_archive_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_ui_terrain_hud_panel_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("ADWin.cel", archive);
    if (data_fdps_ui_terrain_hud_panel_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    source_cel = (unsigned char *) fdps_vfs_load_file("EasyBar.cel", archive);
    if (source_cel == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_unit_gauge_sheet_ptr =
        (unsigned char *) malloc(UNIT_GAUGE_SHEET_BYTES);
    memset(data_fdps_unit_gauge_sheet_ptr, 0, UNIT_GAUGE_SHEET_BYTES);
    for (sub_image_index = 0; sub_image_index < GAUGE_SUB_IMAGE_COUNT;
         sub_image_index++) {
        sub_image = source_cel
            + *(int *) (source_cel + sub_image_index * 4
                        + CEL_SUB_IMAGE_TABLE_OFFSET);
        fdps_blit_dispatch(sub_image,
                           data_fdps_unit_gauge_sheet_ptr
                               + sub_image_index * UNIT_GAUGE_STRIDE,
                           UNIT_GAUGE_WIDTH, UNIT_GAUGE_ROWS,
                           UNIT_GAUGE_WIDTH,
                           GAUGE_BLIT_OPERAND, GAUGE_BLIT_MODE);
    }
    free(source_cel);

    source_cel = (unsigned char *) fdps_vfs_load_file("Bar.cel", archive);
    if (source_cel == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_status_gauge_bar_sheet_ptr =
        (unsigned char *) malloc(STATUS_GAUGE_SHEET_BYTES);
    memset(data_fdps_status_gauge_bar_sheet_ptr, 0, STATUS_GAUGE_SHEET_BYTES);
    for (sub_image_index = 0; sub_image_index < GAUGE_SUB_IMAGE_COUNT;
         sub_image_index++) {
        sub_image = source_cel
            + *(int *) (source_cel + sub_image_index * 4
                        + CEL_SUB_IMAGE_TABLE_OFFSET);
        fdps_blit_dispatch(sub_image,
                           data_fdps_status_gauge_bar_sheet_ptr
                               + sub_image_index * STATUS_GAUGE_STRIDE,
                           STATUS_GAUGE_WIDTH, STATUS_GAUGE_ROWS,
                           STATUS_GAUGE_WIDTH,
                           GAUGE_BLIT_OPERAND, GAUGE_BLIT_MODE);
    }
    free(source_cel);

    free(archive);

    archive = fdps_vfs_open("Field.vfs");
    if (archive == NULL) {
        printf("file not found: 'field.vfs'\n");
        exit(1);
    }

    data_fdps_font_sheet_ptr =
        (unsigned char *) fdps_vfs_load_file("Fdetxt.fon", archive);
    if (data_fdps_font_sheet_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    data_fdps_all_game_text_ptr =
        (unsigned char *) fdps_vfs_load_file("Fdetxt00.txt", archive);
    if (data_fdps_all_game_text_ptr == NULL) {
        fdps_wait_any_key();
        exit(1);
    }

    free(archive);

    if (access("FMer1.tmp", F_OK) != 0) {
        fdps_build_palette_tables(
            (struct fdps_palette_entry *) data_fdps_vga_fight_palette_ptr);

        cache_file = fopen("FMer1.tmp", "wb");
        fwrite(data_fdps_palette_shade_ramp_table, 1, SHADE_RAMP_BYTES,
               cache_file);
        fclose(cache_file);

        cache_file = fopen("FMer2.tmp", "wb");
        fwrite(data_fdps_inverse_palette_cube, 1, INVERSE_CUBE_BYTES,
               cache_file);
        fclose(cache_file);

        fdps_build_palette_tables(
            (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr);

        cache_file = fopen("Mer1.tmp", "wb");
        fwrite(data_fdps_palette_shade_ramp_table, 1, SHADE_RAMP_BYTES,
               cache_file);
        fclose(cache_file);

        cache_file = fopen("Mer2.tmp", "wb");
        fwrite(data_fdps_inverse_palette_cube, 1, INVERSE_CUBE_BYTES,
               cache_file);
        fclose(cache_file);
    } else {
        cache_file = fopen("Mer1.tmp", "rb");
        fread(data_fdps_palette_shade_ramp_table, 1, SHADE_RAMP_BYTES,
              cache_file);
        fclose(cache_file);

        cache_file = fopen("Mer2.tmp", "rb");
        fread(data_fdps_inverse_palette_cube, 1, INVERSE_CUBE_BYTES,
              cache_file);
        fclose(cache_file);
    }
}

/* The request codes the battle loop leaves in
   data_fdps_chapter_event_or_battle_end_code for main to act on (CMP 1 / CMP 2
   at 00029373-00029383); 0 is "nothing to do", and any code past 2 is treated
   the same way. */
#define END_CODE_GAME_OVER 1
#define END_CODE_CHAPTER_CLEARED 2

/* What fdps_cdrom_detect answers when the drive and the disc are both usable
   (CMP dword ptr [EBP-0x8],0x1 / JZ at 000292e0). */
#define CDROM_DETECT_OK 1

/* Audio timer rate handed to fdps_audio_init: PUSH 0x19 at 0002930c. */
#define AUDIO_TICK_RATE_HZ 25

/* INT 10h AH=00h set-video-mode requests, loaded as the whole of AX
   (MOV word ptr [EBP-0x38],0x13 at 0002931b and ,0x3 at 000293c7). */
#define VIDEO_BIOS_INT 0x10
#define VIDEO_MODE_VGA_320X200X256 0x13
#define VIDEO_MODE_TEXT_80X25 0x03

/* The program.  Installation check, CD check, start-up, the outer game loop,
   shut-down -- in that order, with no return to an earlier step.

   Called by the Watcom startup's __CMain at 0004df8f with argc and argv
   pushed, and neither is read.  What it returns goes straight to exit() at
   0004df98.  The original loads nothing into EAX after the farewell printf, so
   the process's exit code is whatever that printf returned; `return printf`
   below states that explicitly instead of leaving it to the register.  The two
   failure paths end in exit(1) and never come back.

   Disk.No is the file the installer writes ("CDROM at e:").  Its first two
   tokens are scanned into one 20-byte stack buffer and dropped; the third is
   scanned straight into data_fdps_cdrom_path, which is three bytes long -- a
   drive letter, a colon and the terminator.  No width limit is given to either
   scan and the fopen result is not tested, exactly as in the original.

   The music-index global is set to -1 ("no track playing") between the file
   read and the CD probe, before any audio code has run.

   The mode switches reuse one register block for input and output, so the
   text-mode request at the end goes in carrying whatever the mode-13h call
   left in every register but AX.  INT 10h AH=00h reads only AL.

   The loop body always runs one fdps_battle_player_phase_loop pass first and
   clears the request code last, whichever arm ran.  A cleared chapter goes
   through data_fdps_chapter_end_handler_table (chapter.h) at the current
   chapter id, unchecked, and then into the village phase; a game over shows
   the game-over screen and then goes back to the title screen. */
int main(int argc, char **argv)
{
    FILE *disk_no_file;
    char discarded_token[20];
    int cdrom_status;
    union REGS video_regs;

    if (access("DISK.NO", 0) != 0) {
        printf("\nCan't found file 'Disk.No' !!!\a\n");
        printf("Please use install function.\n");
        exit(1);
    }

    disk_no_file = fopen("Disk.no", "rt");
    fscanf(disk_no_file, "%s", discarded_token);
    fscanf(disk_no_file, "%s", discarded_token);
    fscanf(disk_no_file, "%s", data_fdps_cdrom_path);
    fclose(disk_no_file);

    data_fdps_audio_cd_current_music_index = -1;

    cdrom_status = fdps_cdrom_detect();
    if (cdrom_status != CDROM_DETECT_OK) {
        printf("Fatal error: CDROM is not install!!!\a\n");
        printf("Check your CDROM please!!!\n");
        exit(1);
    }

    fdps_audio_init(AUDIO_TICK_RATE_HZ);
    fdps_load_global_resources();

    video_regs.w.ax = VIDEO_MODE_VGA_320X200X256;
    int386(VIDEO_BIOS_INT, &video_regs, &video_regs);

    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_shared_quit_game_requested = 0;
    data_fdps_chapter_event_or_battle_end_code = 0;

    fdps_title_screen();
    while (data_fdps_shared_quit_game_requested == 0) {
        fdps_battle_player_phase_loop();
        switch (data_fdps_chapter_event_or_battle_end_code) {
        case END_CODE_GAME_OVER:
            fdps_show_game_over();
            fdps_title_screen();
            break;
        case END_CODE_CHAPTER_CLEARED:
            (*data_fdps_chapter_end_handler_table
                 [data_fdps_chapter_current_chapter_id])();
            fdps_run_village_phase();
            break;
        }
        data_fdps_chapter_event_or_battle_end_code = 0;
    }

    fdps_shutdown_free_resources();
    fdps_audio_shutdown();
    fdps_cd_stop_audio();

    video_regs.w.ax = VIDEO_MODE_TEXT_80X25;
    int386(VIDEO_BIOS_INT, &video_regs, &video_regs);

    return printf("\nThank you for playing Flame Dragon Plus!! \n\n");
}
