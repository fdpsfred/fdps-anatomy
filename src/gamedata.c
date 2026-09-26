/* gamedata.c -- the game-state globals more than one file reads.
 *
 * A global lands here because it is shared, not because it is miscellaneous:
 * rebuild_info/code_layout.md routes a global read by two or more files here
 * and one read by a single file to that file.  The declarations are in
 * gamedata.h, which this file includes so that every definition is checked
 * against its declaration by the compiler.
 */
#include "gamedata.h"
#include "indicat.h"

/* Global data owned by this file, in the original image's address order.
 * Initialised definitions come first and their order is the layout
 * (rebuild_info/data_emit.md); zero-filled ones follow. */

/* 00060008. The image holds 1, but that value is never observed:
   fdps_title_screen stores 1 here (0002a56b) after the opening movie and
   before its menu, and every reader of the flag -- the options menu, the
   battle system submenu, the save screen and the CD music routines -- runs
   only after that store. */
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
static unsigned char village_mode_flag_dword_tail[3] = { 0, 0, 0 };

/* 00060158. Starts at 1, so the terrain/cursor info panel is shown by default
   until the player toggles it off in the options menu or a save restores a
   different value. */
unsigned char data_fdps_ui_terrain_hud_user_enabled = 0x01;

/* 00060159. Starts set (0x01) in the image, so the cursor info panel's gate is
   open until the first menu, status window or script clears it;
   fdps_title_screen also stores 1 at 0002a2e5 before any battle runs. */
unsigned char data_fdps_ui_play_active_flag = 0x01;

/* 00064120. Starts all zero (bss in the original); written as an explicit zero
   initialiser because it heads the contiguous run 0x64120..0x653ef that the
   unbounded indicator queue cursor overruns, so every later member must sit at
   the original's offset from it. The static zero dword before it is nothing
   the game touches: it only forces the char array, and with it the whole run,
   to start 4-aligned as at 0x64120. */
static int indicator_queue_run_align_below = 0;
unsigned char data_fdps_indicator_queue_cell_x_offset[INDICATOR_QUEUE_CELLS] = { 0 };

/* 000641e8. Starts all zero. It is written with an explicit initialiser only
   so that it lands in _DATA directly after
   data_fdps_indicator_queue_cell_x_offset, inside the one contiguous
   initialised run 0x64120..0x653ef: the queue cursor is never bounded, so a
   batch of more than 50 popups writes cell offsets into this array and unit
   indices into glyph_ids and on up to the cursor itself, exactly as in the
   original. */
unsigned char data_fdps_battle_indicator_queue_unit_idx[INDICATOR_QUEUE_CELLS] = { 0 };

/* 000642b0. Starts all zero; it is initialised only so it sits in _DATA
   directly after data_fdps_battle_indicator_queue_unit_idx and directly before
   data_fdps_indicator_queue_count, because the unbounded queue cursor runs
   unit indices into this array and this array's cell 200 onto the cursor's low
   byte, exactly as in the original. */
unsigned char data_fdps_indicator_queue_glyph_ids[INDICATOR_QUEUE_CELLS] = { 0 };

/* 00064378. Zero in the image (empty queue). It is an initialised zero placed
   directly after data_fdps_indicator_queue_glyph_ids because
   glyph_ids[200..203] is this cursor: an unbounded batch of more than 50
   popups first overwrites its low byte with a glyph id and later resets it
   through unit_idx[400], exactly as in the original. */
int data_fdps_indicator_queue_count = 0;

/* 0006437c. Zero in the image and always written by the item or spell teleport
   target picker before fdps_cast_spell_on_targets reads it. It is explicitly
   initialised only so it keeps its original place in the contiguous run
   0x64120..0x653ef that the floating-indicator queue overrun writes through.
   */
int data_fdps_battle_teleport_dest_tile_x = 0;

/* 00064380. Starts at zero and is always written by the item or spell teleport
   picker (cursor pixel y / 24) before the teleport spell reads it. It is
   initialised, followed by the original's 12 unreferenced zero bytes, so that
   it and everything after it keep the original offsets from the indicator
   queue arrays whose unbounded overrun writes into this region. */
int data_fdps_teleport_destination_tile_y = 0;
static unsigned char teleport_tile_y_unreferenced_gap[12] = { 0 };

/* 00064390. Starts at zero; the spell list wait loop stores the current tick
   before it matters, so the value itself only makes the first frame redraw. It
   is initialised and followed by the original's 12 unreferenced zero bytes so
   that it and every member after it keep the original offsets from the
   indicator queue arrays, whose unbounded overrun writes into this region. */
int data_fdps_spell_list_window_last_tick = 0;
static unsigned char spell_list_last_tick_unreferenced_gap[12] = { 0 };

/* 000643a0. Starts as NULL; fdps_load_global_resources stores the loaded
   BASEWAV sound-effect pack buffer here at startup. It is explicitly
   initialised only so it keeps its original offset inside the contiguous block
   that the unbounded floating-indicator queue overruns. */
unsigned char *data_fdps_audio_basewav_sfx_bank_buf_ptr = 0;

/* 000643a4. Zero in the image; the balance is loaded from the save record
   before anything reads it. Written as an explicit "= 0" so it stays at its
   original offset inside the contiguous initialised block 0x64120..0x653ef
   that the indicator-queue overrun writes through. */
int data_fdps_shared_party_total_gold = 0;

/* 000643a8. Null in the image; fdps_load_global_resources stores the loaded
   BASEANI archive buffer here and aborts if the load returned null. It is
   explicitly initialised only so it keeps its original place in the contiguous
   run 0x64120..0x653ef that the floating-indicator queue overrun writes
   through. */
unsigned char *data_fdps_animation_baseani_archive_ptr = 0;

/* 000643ac. Null in the image; fdps_load_global_resources fills it with the
   loaded command sprite sheet before any reader runs. It is explicitly
   initialised only so it keeps its original place in the contiguous run
   0x64120..0x653ef that the floating-indicator queue overrun writes through.
   */
unsigned char *data_fdps_command_sprite_sheet_ptr = 0;

/* 000643b0. Starts NULL; fdps_load_global_resources fills it before any reader
   runs. It must be explicitly initialised and placed right after
   data_fdps_command_sprite_sheet_ptr because the unchecked indicator-queue
   cursor overrun writes glyph bytes into it (glyph_ids[256..259]), exactly as
   in the original. */
unsigned char *data_fdps_level_up_window_sheet_ptr = 0;

/* 000643b4. Starts as a null pointer; fdps_load_global_resources stores the
   0x306-byte gauge sheet here at run time. Written as an explicit zero so it
   stays at its original offset inside the contiguous run that the
   floating-indicator queue overrun writes over (glyph_ids[260..263] land on
   it). */
unsigned char *data_fdps_unit_gauge_sheet_ptr = 0;

/* 000643b8. Starts NULL; fdps_load_global_resources stores the loaded sheet
   here and fdps_shutdown_free_resources frees it. Explicitly initialised so it
   sits at its original offset inside the contiguous 0x64120..0x653ef run that
   an unbounded floating-indicator queue cursor overruns in the original. */
unsigned char *data_fdps_cursor_highlight_sprite_sheet_ptr = 0;

/* 000643bc. Null in the image; fdps_load_global_resources fills it through its
   address and aborts on a null load, and fdps_load_savegame frees and reloads
   it. Written as an explicit '= 0' so it stays at its original offset inside
   the contiguous run 0x64120..0x653ef that the floating-indicator queue
   overrun writes through. */
unsigned char *data_fdps_vga_main_palette_ptr = 0;

/* 000643c0. Null in the image; fdps_load_global_resources loads the
   selection-bar sheet into it (fatal on null) and shutdown frees it. Written
   as an explicit "= 0" so it keeps its original offset inside the contiguous
   initialised block 0x64120..0x653ef that the unbounded indicator-queue
   overrun writes glyph bytes into. */
unsigned char *data_fdps_selection_bar_sheet_ptr = 0;

/* 000643c4. Starts NULL in the image; fdps_load_global_resources stores the
   loaded text block into it at startup. Written as an explicit zero so it
   stays in the contiguous initialised run 0x64120..0x653ef at its original
   offset, where an overrun of the floating-indicator queue (its glyph
   stores; the unit-index stores stop at the cursor) lands on it exactly as
   in the original. */
unsigned char *data_fdps_all_game_text_ptr = 0;

/* 000643c8. Starts NULL in the image; fdps_load_global_resources allocates the
   0xaf8-byte sheet before any gauge is drawn. It is written '= 0' so it stays
   at its original offset inside the initialised run that the unbounded
   indicator-queue overrun reaches. */
unsigned char *data_fdps_status_gauge_bar_sheet_ptr = 0;

/* 000643cc. Null in the image; fdps_load_global_resources loads the font sheet
   into it (fatal on null) and shutdown frees it. Written as an explicit "= 0"
   so it keeps its original offset inside the contiguous initialised block
   0x64120..0x653ef that the unbounded indicator-queue overrun writes glyph
   bytes into. */
unsigned char *data_fdps_font_sheet_ptr = 0;

/* 000643d0. Starts NULL; fdps_load_global_resources fills it at startup and
   aborts if the load fails. It is explicitly initialised and placed right
   after data_fdps_font_sheet_ptr because the unchecked indicator-queue cursor
   overrun writes glyph bytes into it (glyph_ids[288..291]), exactly as in the
   original. */
unsigned char *data_fdps_message_window_sheet_ptr = 0;

/* 000643d4. Null in the image; fdps_load_global_resources loads the unit
   status icon sheet into it and fdps_shutdown_free_resources frees it. Written
   as an explicit "= 0" so it keeps its original offset inside the contiguous
   initialised block 0x64120..0x653ef that the unbounded indicator-queue
   overrun writes glyph bytes into. */
unsigned char *data_fdps_unit_status_icon_sheet_ptr = 0;

/* 000643d8. Null in the image; fdps_load_global_resources loads the number
   glyph sheet into it and aborts if the load fails. Written as an explicit "=
   0" so it keeps its original offset inside the contiguous initialised block
   0x64120..0x653ef that the unbounded indicator-queue overrun writes glyph
   bytes into. */
unsigned char *data_fdps_number_glyph_sheet_ptr = 0;

/* 000643dc. Starts NULL in the image; fdps_load_global_resources fills it at
   startup. It is written explicitly as = 0 because it sits inside the
   contiguous run that an overflowing floating-indicator queue overwrites
   (glyph_ids[300..303]), so it must keep the original's offset right after
   data_fdps_number_glyph_sheet_ptr. */
unsigned char *data_fdps_shadow_sprite_sheet_ptr = 0;

/* 000643e0. Starts NULL; fdps_load_global_resources allocates the sheet at
   startup. Defined with an explicit zero so it sits at its original offset
   inside the contiguous 0x64120..0x653ef run that the unbounded
   indicator-queue overrun writes through. */
unsigned char *data_fdps_ui_terrain_hud_panel_sheet_ptr = 0;

/* 000643e4. Null in the image; fdps_load_global_resources loads the fight
   palette into it and fdps_shutdown_free_resources frees it. Written as an
   explicit "= 0" so it keeps its original offset inside the contiguous
   initialised block 0x64120..0x653ef that the unbounded indicator-queue
   overrun writes glyph bytes into. */
unsigned char *data_fdps_vga_fight_palette_ptr = 0;

/* 000643e8. Starts empty; main fills it at startup from the config file's
   third token (a drive spec such as "E:") before any CD path is built. Defined
   with an explicit zero so it keeps its original offset inside the contiguous
   0x64120..0x653ef run that the unbounded indicator-queue overrun writes
   through. */
char data_fdps_cdrom_path[3] = { 0 };

/* 000643eb. Starts at zero and main clears it again before the title/game
   loop, so the load-time value is never observed. It is written explicitly as
   zero because it must sit exactly after data_fdps_cdrom_path, completing that
   dword, inside the contiguous block the floating-indicator queue overrun
   writes through. */
unsigned char data_fdps_shared_quit_game_requested = 0;

/* 000643ec. Starts NULL in the image and is always overwritten by
   fdps_baseani_get_entry_or_exit before being read. It is written as an
   explicit zero initialiser only so it lands at its original offset inside the
   contiguous run that the indicator-queue overrun writes into. */
unsigned char *data_fdps_animation_baseani_entry_ptr = 0;

/* 000643f0. All zero in the image; filled at run time by
   fdps_build_palette_tables or read back from a saved 4096-byte copy. Defined
   initialised so it stays the last member of the contiguous 0x64120..0x653ef
   run: an overrun of the floating-indicator queue stores glyph bytes into its
   first 80 bytes (0x643f0..0x6443f), exactly as in the original. */
unsigned char data_fdps_inverse_palette_cube[4096] = { 0 };

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

/* 00060131. Starts at 0, so the village phase runs its save prompt path by
   default; only the title screen sets it to 1 before entering the village, and
   the village phase clears it again. */
unsigned char data_fdps_village_skip_save_prompt_flag;

/* 00060138. Starts as NULL: no sprite cache exists until
   fdps_cache_cel_sprite_group allocates one, and fdps_shutdown_free_resources
   tests it against zero before freeing. */
unsigned char *data_fdps_cel_sprite_cache_ptr;

/* 0006013c. Starts as a null pointer; the chapter loader and the savegame
   loader free it only when non-null before allocating a new table, and
   shutdown frees it only when non-null, so the initial null is load-bearing.
   */
unsigned char *data_fdps_tile_event_data_table_ptr;

/* 00060144. Starts NULL; fdps_field_load_chapter_resources allocates the grid
   and stores the pointer, and every reader and fdps_shutdown_free_resources
   test it against NULL first, so the NULL start is what makes the first load
   skip the free. */
unsigned char *data_fdps_battle_move_grid_ptr;

/* 00060148. Starts as NULL; the chapter loader and shutdown free it only when
   non-NULL, so the NULL initial value is what makes the first chapter load
   skip the free. */
unsigned char *data_fdps_map_cell_event_code_layer_ptr;

/* 00060150. Starts at zero: no units exist until a chapter load or savegame
   load assigns the count, and every loop over the unit array uses it as its
   bound. */
int data_fdps_map_unit_count;

/* 0006015c. Starts at zero in the image; every write site either resets it to
   20 or advances it modulo 56 before any read, so the initial value only
   matters for the first scene-layer frame drawn before a reset. */
int data_fdps_marked_tile_blend_phase;

/* 00063f74. Starts at zero in the loaded image. It is written only when the
   attack search finds a target, so the first AI turn that reads it without a
   fresh write resolves unit 0, a valid index, which is why zero-initialised
   storage is enough. */
int data_fdps_battle_ai_best_physical_target_idx;

/* 00063f78. Starts at zero in the image (BSS);
   fdps_map_actor_score_best_attack resets it to 0 before every scoring pass,
   so the initial value is never observed. */
int data_fdps_battle_ai_best_physical_score;

/* 00063f7c. Starts at zero in BSS; fdps_map_actor_score_best_item overwrites
   it whenever it finds a better item, and fdps_map_actor_use_item only reads
   it after such a decision, so the initial value is never meaningful. */
int data_fdps_map_ai_best_item_bag_slot;

/* 00063f80. Zero in the image; it is only meaningful after
   fdps_map_actor_score_best_attack stores a chosen tile x, and
   fdps_map_actor_move_and_attack reads it only after that scorer ran. */
int data_fdps_battle_ai_best_physical_target_x;

/* 00063f84. Starts at zero in BSS; fdps_map_actor_score_best_attack always
   stores it before fdps_map_actor_move_and_attack reads it, so the initial
   value is never observed. */
int data_fdps_battle_ai_best_attack_tile_y;

/* 00063f88. Starts at zero in the image; fdps_map_actor_score_best_spell
   resets it to 0 at entry (0001342c) before every search, so no initial value
   is observable. */
int data_fdps_battle_ai_best_spell_score;

/* 00063f8c. Starts at zero in the image's BSS; fdps_map_actor_score_best_item
   zeroes it on entry before every search, so the load-time value is never
   observed by a reader. */
int data_fdps_battle_ai_best_item_score;

/* 00063f90. Starts at zero in BSS; fdps_map_actor_score_best_spell writes it
   before any reader uses it, so the initial value carries no meaning. */
int data_fdps_map_ai_best_spell_id;

/* 00063f94. Starts at 0 in the image (bss); it is only meaningful after
   fdps_map_actor_score_best_item has found a winning item use, so the initial
   value is never relied on. */
int data_fdps_battle_ai_best_item_target_y;

/* 00063f98. Starts at zero; fdps_map_actor_score_best_item writes it before
   fdps_map_actor_use_item reads it, so the initial value is never observed. */
int data_fdps_map_ai_best_item_target_x;

/* 00063f9c. Starts at zero in the image (bss); it is only meaningful after
   fdps_map_actor_score_best_spell has stored a winning tile, and the cast path
   reads it only when the spell score gate (>= 6) passed, so no initial value
   is observable. */
unsigned int data_fdps_battle_ai_best_spell_target_x;

/* 00063fa0. Starts as zero in BSS; it is always written by the spell scorer
   before the spell caster reads it, so the initial value is never observed. */
unsigned int data_fdps_battle_ai_best_spell_target_y;

/* 00063fb4. Starts NULL in the image; every village screen (item menu, church,
   bar, weapon shop, secret menu) stores its freshly allocated backdrop page
   here before the status and spell-list windows read it, so the initial value
   is never dereferenced. */
unsigned char *data_fdps_village_backdrop_page_ptr;

/* 00063fd0. Starts as a null pointer in BSS; fdps_load_data_tables allocates
   and fills it from ProMap.dat at startup, so no initial value is meaningful.
   */
unsigned char *data_fdps_class_table_ptr;

/* 00063fd4. Starts NULL in the image; fdps_load_data_tables fills it with the
   EnemyDat.dat buffer at startup before any reader runs, so no initial value
   is needed. */
unsigned char *data_fdps_battle_enemy_data_table_ptr;

/* 00063fd8. Starts NULL in the image; fdps_load_data_tables fills it with the
   heap buffer loaded from Friaprda.dat before any reader indexes it. */
unsigned char *data_fdps_battle_character_base_table_ptr;

/* 00063fdc. Starts NULL; fdps_load_data_tables fills it with the heap buffer
   loaded from RankUp.dat before any promotion lookup, and shutdown frees it.
   */
unsigned char *data_fdps_promotion_table_ptr;

/* 00063fe0. Starts NULL in the image; fdps_load_data_tables stores the loaded
   Item.dat buffer here at startup, so no reader sees the initial value. */
unsigned char *data_fdps_item_effect_table_ptr;

/* 00063fe4. Starts NULL in the image (bss); fdps_load_data_tables fills it
   with the heap buffer for ProEqu.dat before any reader indexes it. */
unsigned char *data_fdps_class_equip_table_ptr;

/* 00063fe8. Starts NULL; fdps_load_data_tables fills it with the heap buffer
   holding GetMgTab.dat, and fdps_free_global_resource_buffers frees it on
   shutdown. */
unsigned char *data_fdps_spell_learning_table_ptr;

/* 00063fec. Starts NULL in the image (BSS); fdps_load_data_tables fills it
   with the FriLevUp.dat buffer at startup and
   fdps_free_global_resource_buffers frees it. */
unsigned char *data_fdps_battle_character_growth_table_ptr;

/* 00063ff0. Starts NULL; fdps_load_data_tables fills it with the loaded
   MagicDat.dat buffer through fdps_vfs_load_file_or_exit, which exits on
   failure, so no reader ever sees the NULL. */
unsigned char *data_fdps_battle_spell_effect_table_ptr;

/* 00064000. Starts null; the combat and spell animation drivers allocate the
   gauge fill sheet into it and free it again at the end of each exchange, so
   no initial value is observable. */
unsigned char *data_fdps_gauge_fill_sheet_ptr;

/* 00064004. Starts null in the image; the combat and spell sequences load the
   gauge sprite sheet into it on entry and free it on exit, so no initial value
   is needed. */
unsigned char *data_fdps_combat_gauge_sprite_sheet_ptr;

/* 00064030. Starts at zero in the image (BSS); every path that shows an action
   message writes it before fdps_draw_text reads it, so the initial value is
   never observed. */
int data_fdps_dialog_last_action_text_id_param;

/* 00064034. Starts at zero in the image (bss); every writer stores a fresh
   text id (item/class id plus 0xc9 or 0xa1) before the dialog that contains
   the -5 substitution code is drawn, so the initial value is never observed.
   */
int data_fdps_dialog_subst_text_id_2;

/* 00064038. Starts at zero in the image (BSS); every reader stores a value
   before it pushes it to fdps_draw_text or adds it to a stat or gold total, so
   the initial value is never observed. */
int data_fdps_dialog_last_action_value_param;

/* 0006403c. Zero in the image; fdps_load_global_resources sets it to 16 at
   startup before any glyph is drawn, so no initial value is needed. */
unsigned char data_fdps_font_glyph_width;

/* 0006403d. Zero in the image; fdps_load_global_resources sets it to 16 at
   startup before any glyph is drawn, so no initial value matters. */
unsigned char data_fdps_glyph_cell_height;

/* 0006403e. Zero in the image (BSS); fdps_load_global_resources sets it to 1
   at startup before any glyph is drawn, so the load-time value is never
   observed. */
int data_fdps_font_shadow_offset_x;

/* 00064042. Zero in the image (bss); fdps_load_global_resources sets it to 1
   at startup before any glyph is drawn, so the load-time value is never
   observed. */
int data_fdps_glyph_shadow_row_offset;

/* 00064046. Starts as zero in the image; fdps_load_global_resources stores
   0x20 (32 bytes per 16x16 1bpp glyph) before any text is drawn, so the
   load-time value is never observed. */
int data_fdps_font_glyph_stride_bytes;

/* 0006404a. Starts zero (outline off) in the image; fdps_load_global_resources
   also stores 0 explicitly at startup, so the image value is never observed
   before that write. */
unsigned char data_fdps_font_outline_enabled_flag;

/* 0006404b. Zero in the image (bss); fdps_load_global_resources sets it to 16
   at startup before any text is drawn, so the rebuild needs no initial value.
   */
int data_fdps_glyph_advance_x;

/* 0006404f. Zero in the image; fdps_load_global_resources stores 18 at startup
   before any text is drawn, so the initial value is never observed. */
int data_fdps_font_line_height;

/* 000640d8. Starts all zero (no cell event has fired);
   fdps_chapter_state_reset clears it with memset each chapter and savegames
   overwrite it with memcpy, so no static initial value is observable. */
unsigned char data_fdps_map_cell_event_triggered_flags[32];

/* 000640f8. Starts all zero in the image (BSS); every entry is rewritten by
   the save/load panel build before the slot-select loop reads it, so the
   initial value is never observed. */
int data_fdps_ui_save_slot_occupied_flags[3];

/* 00064108. Starts as a null pointer in BSS; fdps_load_global_resources
   allocates the 0xA00-byte roster block and stores it here before any reader
   runs, and shutdown frees it. */
unsigned char *data_fdps_roster_array_ptr;

/* 0006410c. Starts at zero; it is only meaningful after a chapter load or
   savegame load stores byte 2 of the chapter header into it, so the image
   holds no initial value. */
int data_fdps_map_char_spawn_count;

/* 00064110. Starts at zero (lottery not yet drawn); the title screen re-zeroes
   it for a new game and a loaded save restores it, so the initial value only
   matters before either runs. */
int data_fdps_bonus_lottery_drawn_flag;

/* 00064114. Starts at zero in the image (bss); the title screen, title demo
   and savegame loaders assign it before any roster loop reads it, so no
   initial value is needed. */
int data_fdps_roster_member_count;

/* 00064118. Starts at zero in the image; it is always written from byte 1 of
   the chapter resource header (field load or savegame load) before
   fdps_build_map_unit_array reads it. */
int data_fdps_map_player_slot_count;

/* 000653f0. All 18432 bytes are zero in the image; fdps_build_palette_tables
   fills the 18 rows of 256 entries at startup and several scenes save/restore
   the whole table through a 0x4800-byte temp file, so no initial value is
   observable. */
unsigned int data_fdps_palette_shade_ramp_table[4608];

/* 00069bf0. Starts all zero in BSS; fdps_field_load_chapter_resources seeds
   each active layer's accumulator from the chapter resource before any draw
   reads it, so no initial value is observable. */
int data_fdps_scene_layer_scroll_x_accumulator[6];

/* 00069c08. Starts all zero in the image; fdps_field_load_chapter_resources
   seeds each loaded slot from the layer descriptor before any frame reads it,
   so no initial value is observable. */
int data_fdps_scene_layer_scroll_offset_y[6];

/* 00069c20. All six slots start as null pointers in the image; the chapter
   loader fills the first data_fdps_scene_layer_count slots with malloc'd cel
   sheets and the free loops release only that many, so the initial null value
   is never dereferenced. */
unsigned char *data_fdps_scene_layer_tile_sheet_ptrs[6];

/* 00069c38. Starts all zero in the image; fdps_field_load_chapter_resources
   fills one entry per layer from field 6 of each dsc%02d.dat record before any
   frame reads it. */
int data_fdps_scene_layer_scroll_step_y[6];

/* 00069c50. All 24 bytes are zero in the image (bss);
   fdps_field_load_chapter_resources fills each layer's step from the chapter's
   dsc%02d.dat record before fdps_draw_scene_layers ever adds it to the x
   accumulator. */
int data_fdps_scene_layer_scroll_x_step[6];

/* 00069c68. Starts all zero; every active slot is overwritten from the
   chapter's dsc%02d.dat record before any draw reads it, so no initial value
   is observable. */
int data_fdps_scene_layer_parallax_factor_y[6];

/* 00069c80. Starts all zero in BSS; fdps_field_load_chapter_resources fills
   one slot per scene layer from the chapter's layer table before any draw
   reads it, so no initial value matters. */
int data_fdps_scene_layer_parallax_factor_x[6];

/* 00069c98. All six slots start NULL; fdps_field_load_chapter_resources fills
   each with the loaded attr%02d%d.dat buffer and shutdown/reload paths free
   them, so no static initial value is needed. */
unsigned char *data_fdps_scene_layer_tile_attr_ptr[6];

/* 00069cb0. Starts all NULL in BSS; fdps_field_load_chapter_resources fills
   one slot per scene layer (count at 0x69cdc) when a chapter loads, and the
   shutdown/reload paths free the same slots. */
unsigned char *data_fdps_scene_layer_tile_map_ptrs[6];

/* 00069cc8. Starts at 0 in the image (bss); fdps_draw_map_unit advances it
   modulo 16 each frame, so the unit walk cycle always begins on phase 0 after
   load. */
int data_fdps_map_unit_walk_anim_counter;

/* 00069ccc. Starts at zero in BSS; fdps_chapter_state_reset and
   fdps_load_savegame set it before any cursor code reads it, so no initial
   value is needed. */
int data_fdps_map_cursor_world_y;

/* 00069cd0. Starts at 0 (no map cursor overlay drawn) in the original's BSS;
   every mode (1..6) is stored by code before fdps_draw_map_cursor reads it, so
   no initialiser is needed. */
int data_fdps_map_cursor_draw_mode;

/* 00069cd4. Starts at zero in the image; fdps_chapter_state_reset,
   fdps_load_savegame and the icon scripts set it before any map screen reads
   it. */
int data_fdps_map_cursor_world_x;

/* 00069cd8. Starts NULL in the image (bss); fdps_build_map_unit_array,
   fdps_deploy_unit, fdps_load_savegame, fdps_relocate_unit_array and
   fdps_load_field_chapter_resources store the allocated unit array into it,
   and fdps_shutdown_free_resources frees it only when non-NULL. */
unsigned char *data_fdps_map_unit_array_ptr;

/* 00069cdc. Starts at zero in the image; main() also stores 0 at startup and
   the chapter loader sets it from the layer descriptor count, so no nonzero
   initial value is needed. */
int data_fdps_scene_layer_count;

/* 00069ce0. Starts at zero in the image; fdps_chapter_state_reset and
   fdps_load_savegame set it before any reader runs, so no initial value is
   load-bearing. */
int data_fdps_battle_view_window_origin_y;

/* 00069ce4. Starts at zero in the image; fdps_chapter_state_reset and
   fdps_load_savegame set it before any battle view is drawn, so the load-time
   value is never observed. */
int data_fdps_battle_view_window_origin_x;

/* 00069ce8. Starts at zero in the image; fdps_chapter_state_reset sets it to 1
   at chapter start and fdps_load_savegame restores it, so the load-time value
   is never observed. */
int data_fdps_battle_turn_counter;

/* 00069cec. Starts at zero in the image; every action path (attack, spell,
   item, action menu, player phase) stores 0 before accumulating, so no initial
   value is observable. */
int data_fdps_battle_pending_xp_credit;

/* 00069cf0. Starts at zero in BSS: no cel sprite groups are cached until
   fdps_cache_cel_sprite_group increments it, and every loader resets it to 0
   after freeing the cache. */
int data_fdps_cel_sprite_cache_count;

/* 00069cf4. Starts at zero; every path into play (title new game, demo, load
   game, icon script, chapter end handlers) stores the real chapter index
   before it is read. */
int data_fdps_chapter_current_chapter_id;

/* 00069cf8. Starts all zero in BSS; every entry up to the layer count is
   overwritten from byte field +0x20 of each dsc%02d.dat record when a
   chapter's field resources load, before any draw reads it. */
unsigned char data_fdps_scene_layer_tile_attr_mode[6];

/* 00069cfe. Starts all zero in the image (bss); every slot is filled from byte
   +0x18 of the chapter's layer descriptor by fdps_field_load_chapter_resources
   before any draw reads it, so no initial value is observable. */
unsigned char data_fdps_scene_layer_draw_depth[6];

/* 00069d04. Starts at zero in BSS; fdps_map_load_tile_info writes it before
   any reader uses it, so the initial value is never observed. */
short data_fdps_map_tile_info_tile_id;

/* 00069d06. Starts as zero in the image (BSS); fdps_map_load_tile_info writes
   it before any reader consumes it, so no initial value matters. */
short data_fdps_map_current_cell_event_code;

/* 00069d08. Starts at zero in BSS; fdps_map_load_tile_info overwrites it from
   the tile attribute record before any reader masks it with 0x60, so the
   initial value is never observed. */
unsigned char data_fdps_map_current_tile_attr_flags;

/* 00069d09. Starts at 0 in the image (bss) and is always stored by
   fdps_map_load_tile_info before any reader looks at it, so the initial value
   carries no meaning. */
unsigned char data_fdps_map_tile_terrain_type;

/* 00069d0b. Starts at zero in the image (bss); fdps_map_load_tile_info fills
   it from the tile attribute record before any combat reads it, so no initial
   value is observable. */
unsigned char data_fdps_map_tile_combat_backdrop_id;

/* 00069d0c. Starts at zero in the image; fdps_map_load_tile_info overwrites it
   before any reader compares it against 0xFF, so the initial value carries no
   meaning. */
unsigned char data_fdps_map_current_move_grid_marker;

/* 00069d54. Starts as zero in BSS; main stores -1 (no music) at 000292cd
   before any CD music code runs, so the image's zero is never observed as a
   track index. */
int data_fdps_audio_cd_current_music_index;

/* 00069d64. Starts at zero in bss; only the timer interrupt handler increments
   it. The volatile qualifier matters because some thirty wait loops reload it
   on each pass, so a hoisted load would hang the animation that is waiting. */
volatile unsigned int data_fdps_timer_tick_counter;

/* 00069d70. Zero in the image (BSS); fdps_audio_init sets it to 1 at startup
   and the options menu toggles it with XOR 1, while save/load copy it as one
   byte of the save header. */
unsigned char data_fdps_audio_sfx_enabled_flag;

/* 00069d80. Starts NULL; fdps_load_field_chapter_resources stores the loaded
   shop stock buffer here before any shop reads it, and the village phase frees
   it on exit. */
unsigned char *data_fdps_shop_stock_table_ptr;

/* 00069d84. Starts as a null pointer in BSS; fdps_run_village_phase stores the
   loaded window sheet here before any reader runs and frees it when the phase
   ends. */
unsigned char *data_fdps_village_window_sheet_ptr;

/* 00069d90. Starts zero in BSS; every battle-phase reader seeds it to 0xff (no
   pending event) before calling the tile-event hook, so the initial value is
   never observed. */
unsigned int data_fdps_chapter_pending_event_idx;

/* 00069da0. Starts at zero (no battle end pending); main and
   fdps_chapter_state_reset clear it again before each chapter, so the image
   value is only the load-time state. */
unsigned int data_fdps_chapter_event_or_battle_end_code;

/* 00069da4. Starts as a null pointer in BSS; fdps_cd_alloc_dos_buffers stores
   the flat address of the DOS transfer block (segment << 4) before any CD
   request uses it. */
unsigned char *data_fdps_cd_ioctl_buffer;

/* 00069da8. Starts as zero in BSS; fdps_cd_alloc_dos_buffers packs the DOS
   buffer's real-mode segment into the high word before any IOCTL request
   copies it into a request header's transfer address, so nothing depends on
   the initial value. */
unsigned int data_fdps_cd_ioctl_buffer_real_mode_ptr;

/* 00069de8. Starts as a null pointer; fdps_cd_alloc_dos_buffers sets it to the
   flat address of the DOS real-mode request-header block before any CD driver
   call copies through it. */
unsigned char *data_fdps_cd_request_header_buffer;

/* 00069dff. Starts as zero in BSS; fdps_cdrom_read_track_info stores the
   queried track number before any reader runs, so the initial value is never
   observed. */
short data_fdps_cd_track_info_track_number;

/* 00069e01. Starts at zero; every read follows a track-info query
   (fdps_cdrom_read_track_info) that stores the field first, so the initial
   value is never observed. */
unsigned int data_fdps_cd_track_start_sector;

/* 00069e07. Starts at zero in BSS; fdps_cdrom_read_disk_info fills it from the
   IOCTL audio disk info reply before any reader uses it. */
unsigned char data_fdps_cd_highest_track_number;

/* 00069e0b. Starts as zero in the image (bss); fdps_cdrom_read_disk_info fills
   it from the IOCTL audio disk info reply before any reader uses it. */
unsigned int data_fdps_cd_leadout_sector;

/* 00069e20. Starts at zero; every CD request wrapper stores the device request
   header status word here before anything reads it, so no initial value is
   observable. */
unsigned short data_fdps_cd_last_request_status;

/* 00070022. Starts at zero in the image; fdps_blit_dispatch stores the
   sprite's row count into it before every RLE blit, so the initial value is
   never read. */
unsigned short data_fdps_graphics_rle_blit_remaining_rows;

/* 00070024. Starts at zero in the image; fdps_blit_dispatch stores the source
   width before any RLE blitter reads it, so the initial value is never
   observed. */
unsigned short data_fdps_graphics_rle_blit_src_width;

/* 00070026. Starts at zero; every scaled blit stores a fresh seed (the
   vertical scale step) before the row loop reads it, so the initial value is
   never observed. */
unsigned short data_fdps_graphics_rle_blit_vscale_accumulator;

/* 00070028. Starts as zero; both scaled RLE blitters store it from their
   parameter block before any read, so the initial value never reaches a blit.
   */
unsigned short data_fdps_graphics_rle_blit_dest_width;

/* 0007002a. Starts at zero; both scaled RLE blitters store the destination
   height here before every use, so the initial value is never observed. */
unsigned short data_fdps_graphics_rle_blit_dest_height;

/* 0007002c. Starts at zero; the scaled and rotated-scaled RLE blitters always
   load it from the destination height before the row loop decrements it, so
   the initial value is never observed. */
unsigned short data_fdps_graphics_rle_blit_dest_rows_remaining;

/* 0007002e. Starts at zero in the image; fdps_blit_dispatch stores the
   destination pitch before any RLE kernel reads it, so no non-zero initial
   value is needed. */
unsigned short data_fdps_graphics_rle_blit_dst_pitch;

/* 00070030. Starts as zero; every RLE blitter that reads it stores it on entry
   before its row loop adds it to the destination cursor, so the initial value
   is never observed. */
int data_fdps_graphics_rle_blit_dst_row_advance;

/* End of global data. */
