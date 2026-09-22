/* gamedata.c -- the game-state globals more than one file reads.
 *
 * A global lands here because it is shared, not because it is miscellaneous:
 * rebuild_info/code_layout.md routes a global read by two or more files here
 * and one read by a single file to that file.  The declarations are in
 * gamedata.h, which this file includes so that every definition is checked
 * against its declaration by the compiler.
 */
#include "gamedata.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00060008. Starts at 1, so CD background music is on by default before any
   save or the options menu touches it; the title screen also forces it back to
   1. */
unsigned char data_fdps_audio_bgm_enabled_flag = 0x01;

/* 00060010. Starts at 1, so battle animations are on in a new game until the
   options menu toggles the flag or a save slot overwrites it. */
unsigned char data_fdps_ui_battle_animation_enabled = 0x01;

/* 00060040. Signed percent AP adjustments per terrain class 0..5 (+5, 0, -5,
   -5, -5, 0). Terrain class 6, placed in shipped maps, reads index 6, which in
   the original is the first entry of
   data_fdps_battle_tile_attr_def_modifier_table (0), so that table must be
   defined immediately after this one in the same file. */
int data_fdps_battle_tile_attr_ap_modifier_table[6] = { 5, 0, -5, -5, -5, 0 };

/* 00060058. Initial percentages {0, 0, +10, +10, -5, 0} per terrain class; the
   table is never written. It must start exactly where the AP table ends:
   shipped maps place terrain class 6, and ap_table[6] reads this table's
   element 0 (0), while this table's index 6 reads the dword holding
   data_fdps_village_mode_flag. */
int data_fdps_battle_tile_attr_def_modifier_table[6] = { 0, 0, 10, 10, -5, 0 };

/* 00060070. Starts at 0 and is 1 only inside fdps_run_village_phase's shop
   loop. It must be an initialised object placed exactly 0x18 bytes after the
   terrain defense modifier table, as the low byte of a zero-filled dword:
   class-6 terrain reads def_table[6], which in the original is this byte
   zero-extended. */
unsigned char data_fdps_village_mode_flag = 0;

/* 0006000c. Starts at 0, the first colour row of Number.cel; every panel that
   changes it parks it back at 0, so digits drawn by callers that never set it
   use row 0. */
int data_fdps_number_glyph_color_row;

/* 00060120. Starts NULL: no portrait is loaded at startup. Every user tests it
   against zero before freeing or drawing, so the NULL start is what keeps the
   first message window from freeing or blitting a stale buffer. */
unsigned char *data_fdps_portrait_sprite_buf_ptr;

/* 00060124. Starts as NULL; the chapter loaders test it for non-NULL before
   freeing the previous chapter's text block and then store the newly loaded
   buffer. */
unsigned char *data_fdps_current_chapter_text_ptr;

/* End of global data. */
