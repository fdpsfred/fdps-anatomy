/* gamedata.h -- the externs of the game-state globals src/gamedata.c owns.
 *
 * A global that more than one file reads is defined in gamedata.c and declared
 * here, exactly once (rebuild_info/code_layout.md).  A file that needs one
 * includes this header; nobody writes a private extern of their own, because a
 * second copy of a declaration does not follow the definition when its type
 * changes and the linker says nothing.
 *
 * The definitions themselves arrive with ticket 23.  Until then the build
 * links a zero-filled stub for every name here that nothing defines yet.
 */
#ifndef GAMEDATA_H
#define GAMEDATA_H

/* 00060150.  How many unit records the current battle holds, and so the bound
   of every walk over the array below.  Signed: the loop compares are JL. */
extern int data_fdps_map_unit_count;

/* 00069cd8.  Base of the map's unit record array.  The stride is 0x50, the
   size of struct fdps_unit_record in src/fdpstype.h; the original types the
   base as a byte pointer and scales the index itself, so a reader casts. */
extern unsigned char *data_fdps_map_unit_array_ptr;

/* 00064108.  Base of the party roster: the members the player has enrolled,
   held as struct fdps_unit_record just like the map unit array above, so the
   stride is the same 0x50 and a reader that named the wrong one of the two
   globals would still step by the right amount (rebuild_info/pitfalls.md,
   contract B).  They are two separate arrays with two separate lifetimes: the
   roster is the party between battles and the map array is who is on the field
   now, and fdps_roster_write_back_battle_units at 00023980 copies one into the
   other a record at a time.

   fdps_load_global_resources allocates it once at startup -- PUSH 0xa00 / CALL
   malloc / MOV [0x00064108],EAX at 000296b8 -- so the block is exactly 32
   records and is never reallocated or freed until shutdown.  How many of those
   32 are occupied is data_fdps_roster_member_count at 00064114, which is a
   separate global and is what every walk over the roster compares against.

   The original types the base as a byte pointer and scales the index itself,
   so a reader casts. */
extern unsigned char *data_fdps_roster_array_ptr;

/* 00064114.  How many of the roster block's 32 record slots are occupied, and
   equally the index of the next free one: fdps_roster_add_character at
   00023bc0 writes the new member at that index -- IMUL EAX,[EBP-0x30],0x50 /
   MOV EDX,[0x00064108] / ADD -- and only then increments the count, INC dword
   ptr [0x00064114] at 00023e12.  Nothing compares it against 32 on the way in.

   It is a separate global from the block itself and not a field of it, so the
   two are written independently: a chapter's init runs a series of
   fdps_roster_add_character calls that each move this by one, while the block
   the records land in never moves. */
extern int data_fdps_roster_member_count;

/* 0006013c.  Base of the loaded chapter's resident MAP%02d.DAT block: a header
   whose byte +2 is how many scripted unit deployments the map carries,
   followed at +0x83 by that many struct fdps_char_spawn_record, 0x1a bytes
   each (src/fdpstype.h).  Eight files read it -- deployment, per-cell events,
   the battle menus, the AI and the save code -- and each does its own
   arithmetic from the base, which is why the count is not cached anywhere.

   Null until a chapter has been loaded, and nothing tests it before use.  The
   original types it as a byte pointer, so a reader casts to the record. */
extern unsigned char *data_fdps_tile_event_data_table_ptr;

/* 00060144.  Base of the battle map's working movement grid: a four-byte
   header of two signed 16-bit dimensions followed by width*height two-byte
   cells (see src/movegrid.h).  Null until a chapter has been loaded, and the
   readers all test it before use.  The original types it as a byte pointer and
   does the header and cell arithmetic itself, so a reader casts. */
extern unsigned char *data_fdps_battle_move_grid_ptr;

/* 00069ce4 and 00069ce0.  Where the visible view window sits in the map's
   world pixels: the world-pixel coordinate of the left edge and of the top
   edge.  Everything that draws a map object converts its tile position with
   tile * 0x18 - origin, so these two are the scroll position and the only
   thing that moves the map under the camera.

   Both are signed.  fdps_map_cursor_move_to clamps each one up to zero after
   scrolling -- CMP dword ptr [0x00069ce0],0x0 / JGE at 0002d99a and the same
   pair for x at 0002da15 -- and fdps_map_cursor_select_loop compares them
   against 0x18 with JL at 0002b967; both are the signed jumps, and a reader
   that widened them as unsigned would take the wrong branch the moment a
   scroll subtraction went below zero.

   They are written together but they are two globals, not a pair: every
   writer names each one by its own absolute address and nothing indexes
   across them (rebuild_info/pitfalls.md, contract B).  fdps_chapter_state_reset
   zeroes both at 00022790 and 0002279a, and the cursor, cut-scene, item and
   spell code moves them from there. */
extern int data_fdps_battle_view_window_origin_x;
extern int data_fdps_battle_view_window_origin_y;

/* 00069c98 and 00069cb0.  The loaded scene's six layers, held as two parallel
   arrays of six byte pointers each: the tile map of layer n and the tileset
   attribute table of layer n.  The battle map is layer 0 and is the only one
   src/maptile.c reads.

   A tile map opens with a header whose signed 16-bit tile width sits at +7
   and whose signed 16-bit tile height sits at +9, and its 16-bit tile ids
   follow at +0xb in row-major order.  Both dimensions are read MOVSX and are
   compared with JL wherever they bound a walk, so a header word of 0xffff has
   to come out as -1.  An attribute table's 4-byte rows -- struct
   fdps_tile_attr_entry -- start at +0x11, one row per tile id.  The original
   types both as byte pointers and does that arithmetic itself, so a reader
   casts.

   The two arrays are adjacent -- 00069c98 + 24 is 00069cb0 -- and are declared
   as two because that is how ticket 17 settled them; nothing emitted so far
   indexes past element 5 of either (rebuild_info/pitfalls.md, contract B). */
extern unsigned char *data_fdps_scene_layer_tile_attr_ptr[6];
extern unsigned char *data_fdps_scene_layer_tile_map_ptrs[6];

/* 00069cdc.  How many of the scene's six layer slots the loaded chapter
   actually filled, and the bound of every walk over the parallel layer arrays
   above.  Signed: each of those walks compares it with JL.

   It is not a constant and it is not derived from the arrays: the chapter
   resource loader takes it straight out of the first dword of the layer
   descriptor file -- MOV EAX,[EAX] / MOV [0x00069cdc],EAX at 00022979 -- and
   nothing between there and the readers clamps it.  Six is the width the
   arrays are declared at and the size of the stack array
   fdps_draw_scene_layers hands to fdps_build_scene_layer_draw_order (SUB
   ESP,0x28 then LEA EAX,[EBP-0x28] at 0002bfe1, six ints), so the shipped
   descriptor files are what keeps it in range. */
extern int data_fdps_scene_layer_count;

/* 00069cfe.  The depth key of each of the six scene layer slots, one byte per
   slot, written by the chapter resource loader out of byte +0x18 of the
   layer's descriptor record (00022a79).  It decides two things and both are
   read as UNSIGNED bytes.

   fdps_build_scene_layer_draw_order sorts the slot indices by it ascending,
   comparing with CMP AL,byte ptr [EDX+0x69cfe] / JBE at 0002c2c4 -- unsigned,
   so a depth of 0x80 sorts after 0x01 and not before it.
   fdps_draw_scene_layers then splits the sorted list at 10: it draws the
   slots whose depth is below 10 before the map units and those above 10 after
   them, and both tests widen the byte with AND EAX,0xff first (0002c026,
   0002c142).  A depth of exactly 10 is drawn in neither pass -- JGE skips it
   in the first loop and JLE skips it in the second -- so 10 is the sentinel
   that parks a layer, not a boundary value one of the two passes takes. */
extern unsigned char data_fdps_scene_layer_draw_depth[6];

/* 00060148.  The battle map's per-cell event-code layer, a struct
   fdps_map_cell_code_layer: its own signed 16-bit width at +7 and one byte per
   cell at +0x10.  It carries its own width and readers use that rather than
   the terrain layer's.  Null until a chapter has been loaded.  The original
   types it as a byte pointer, so a reader casts to the record. */
extern unsigned char *data_fdps_map_cell_event_code_layer_ptr;

/* 00069d04..00069d0c.  The tile-info scratch block: what
   fdps_map_load_tile_info (src/maptile.h) leaves behind about the one map cell
   it was last asked about, read by the battle, cursor and combat code straight
   after the call.  There is no "which cell is this" field -- the caller knows,
   because it just named it.

   They are separate globals and not one record: every reader in the image
   names each one by its own absolute address and nothing indexes across them
   (rebuild_info/pitfalls.md, contract B).  00069d0a is the sixth and is read
   only by maptile.c, so it is declared in src/maptile.h instead of here.

   The tile id is signed -- it is MOVSX'd out of the layer and scaled into the
   attribute table -- while the event code, equally signed here, only ever
   receives a zero-extended byte, so it is 0..255 in practice. */
extern short data_fdps_map_tile_info_tile_id;
extern short data_fdps_map_current_cell_event_code;
extern unsigned char data_fdps_map_current_tile_attr_flags;
extern unsigned char data_fdps_map_tile_terrain_type;
extern unsigned char data_fdps_map_tile_combat_backdrop_id;
extern unsigned char data_fdps_map_current_move_grid_marker;

/* 000640d8.  One byte per map-cell event code, non-zero once that code's event
   has fired on the current map: the per-cell event state of the chapter, and
   part of the savegame.

   Thirty-two entries.  fdps_chapter_state_reset memsets 0x20 bytes of it when
   a chapter starts (00022782), the save and load paths memmove the same 0x20
   bytes to and from offset 0x30a3 of the slot (000151a6, 00024062), and the
   next global begins at 000640f8.

   It is indexed by a cell's raw event code and nothing in the image bounds that
   index, so the declared size is the only thing keeping a read inside it.  The
   code is a layer byte widened to 0..255 while the table is 32 entries; every
   event plane in the shipped M%02d.DTL layers uses codes 0 to 15, so no read
   reaches the neighbouring global (rebuild_info/pitfalls.md, contract B).

   The writers each set one entry to 1 and then call
   fdps_map_apply_triggered_cell_changes (src/maptile.h) to make the map show
   it: the chest and search paths at 0001038b, 0001868b and 000188aa, the icon
   script at 00021c8f and the chapter 30 wave event at 00039876. */
extern unsigned char data_fdps_map_cell_event_triggered_flags[32];

/* 00069d90.  Which chapter-event handler the battle loop still owes a call to:
   an index into the chapter-event handler pointer table at 000601c4, with 0xff
   meaning nothing is pending.

   It is a hand-off slot between two pieces of code and not a state flag.  The
   four turn drivers -- fdps_battle_system_menu, fdps_battle_unit_turn,
   fdps_battle_enemy_turn_phase and fdps_battle_npc_turn_phase -- each seed it
   with 0xff before letting a unit act, and once the unit is done read it back
   and, if it is no longer 0xff, call table[slot](unit_index).
   fdps_map_set_pending_tile_event (src/maptile.h) is the only other writer in
   the image: it fills the slot when the tile a unit is standing on carries an
   event for the occasion being reported.

   A full dword even though every value it ever receives is a zero-extended
   byte, and never compared for order: every reader tests it against 0xff for
   equality -- CMP dword ptr [0x00069d90],0xff at 0001581c, 00012a16, 00012acd,
   00012bb9 and 00014d0b -- and otherwise scales it by 4 into the pointer table
   with LEA EDX,[EDX*0x4 + 0x0]. */
extern unsigned int data_fdps_chapter_pending_event_idx;

/* 00069d64.  The free-running tick counter fdps_timer_tick_handler increments
   from the timer interrupt, and the game's only clock: 31 files read it, for
   animation pacing, input repeat and every "once per tick" guard.  Unsigned.
   The one reader emitted so far, fdps_cycle_ui_palette, latches a copy and
   tests it for equality rather than for order, so wrapping costs it nothing;
   whether that holds of the other readers is theirs to state.

   volatile is not decoration here.  The animations spin on this counter
   waiting for it to move, and nothing inside those loops writes it -- an
   optimiser is entitled to hoist the load out and the game stops dead at the
   first wait (rebuild_info/pitfalls.md).  It is qualified at the one
   declaration rather than at each spinning reader so that no reader has to
   remember; ticket 23's definition has to carry the same qualifier. */
extern volatile unsigned int data_fdps_timer_tick_counter;

/* 00069d70.  The player's sound-effect toggle, one of the four option flags
   the options menu writes and a save file carries (the sfx_enabled_flag field
   of struct fdps_save_slot).  Read as a boolean; it says what the player
   asked for, not whether a driver was found -- that is
   data_fdps_audio_sfx_driver_available_flag, in audio.h. */
extern unsigned char data_fdps_audio_sfx_enabled_flag;

/* 000643a0.  BaseWav.vfs, the sound-effect pack, held whole in one heap block:
   header, directory and every member's .WAV bytes in the one image.
   fdps_load_global_resources loads it at startup -- the address of this
   pointer and the name "BaseWav.vfs" at 0x61d40 go to the loader together at
   00029b0d -- and fdps_shutdown_free_resources frees it at 00029468.  Null
   before the loader has run, and not cleared by the free.

   fdps_play_sfx is the only reader: it takes the base from here on every call
   and hands it to fdps_vfs_image_get_entry, so the block is addressed as a
   resident container image and never as a handle.  Typed as a byte pointer
   like the other resource blocks, and cast to the container header at the one
   use. */
extern unsigned char *data_fdps_audio_basewav_sfx_bank_buf_ptr;

/* 000643a8.  BaseAni.vfs, the animation pack, held whole in one heap block the
   same way the sound pack above is: header, directory and every member's .SAF
   bytes in the one image.  fdps_load_global_resources fills it through
   fdps_vfs_load_file at 00029bbe and fdps_shutdown_free_resources frees it at
   0002945a, one slot before the sound pack's.  Null before the loader has run,
   and not cleared by the free.

   Three readers take the base from here and all three address it as a resident
   container image rather than as a handle: fdps_baseani_get_entry_or_exit,
   which is the only one that reports a miss, and the death and attack
   animations.  Typed as a byte pointer like the other resource blocks, and
   cast to the container header at each use. */
extern unsigned char *data_fdps_animation_baseani_archive_ptr;

/* 00063fd0..00063ff0.  The nine static game data tables, each a heap block
   holding one file read whole out of the VFS container.  fdps_load_data_tables
   at 00018930 fills all nine at startup -- one loader call per table, each
   handed the ADDRESS of its own pointer -- and fdps_free_global_resource_buffers
   releases all nine at shutdown.  Null before the loader has run, and never
   cleared after the release.

   They are nine separate globals and not an array, however tidily they sit
   next to each other: both the loader and the releaser name each one by its
   own absolute address, and nothing in the image indexes across them
   (rebuild_info/pitfalls.md, contract B).

   The original types them as byte pointers and does the record arithmetic at
   each use, so a reader casts to the record type it wants.  Which file each
   one holds, from the loader's argument list:

     data_fdps_class_table_ptr                    ProMap.dat
     data_fdps_battle_enemy_data_table_ptr        EnemyDat.dat
     data_fdps_battle_character_base_table_ptr    Friaprda.dat
     data_fdps_promotion_table_ptr                RankUp.dat
     data_fdps_item_effect_table_ptr              Item.dat
     data_fdps_class_equip_table_ptr              ProEqu.dat
     data_fdps_spell_learning_table_ptr           GetMgTab.dat
     data_fdps_battle_character_growth_table_ptr  FriLevUp.dat
     data_fdps_battle_spell_effect_table_ptr      MagicDat.dat */
extern unsigned char *data_fdps_class_table_ptr;
extern unsigned char *data_fdps_battle_enemy_data_table_ptr;
extern unsigned char *data_fdps_battle_character_base_table_ptr;
extern unsigned char *data_fdps_promotion_table_ptr;
extern unsigned char *data_fdps_item_effect_table_ptr;
extern unsigned char *data_fdps_class_equip_table_ptr;
extern unsigned char *data_fdps_spell_learning_table_ptr;
extern unsigned char *data_fdps_battle_character_growth_table_ptr;
extern unsigned char *data_fdps_battle_spell_effect_table_ptr;

/* 0006403c and 0006403d.  The glyph cell the font is drawn on: width in
   pixels, height in rows.  The font loader in main.c writes them once and
   every text routine reads them, so all glyphs share one cell size.

   They are bytes, and every reader widens them with XOR EAX,EAX / MOV AL --
   zero extension, not sign extension.  The distinction is behaviour: the loop
   bounds they feed are tested with the signed JG, so a cell height of 200 read
   through a signed char is -56 and the drawing loop never runs.

   Adjacent in bss and read one at a time, never indexed across
   (rebuild_info/pitfalls.md, contract B): they are two globals, not a pair. */
extern unsigned char data_fdps_font_glyph_width;
extern unsigned char data_fdps_glyph_cell_height;

/* 00064046.  Bytes from one glyph's bitmap to the next in the font sheet, so
   glyph n lives at sheet + n * this.  A full dword, not a byte like the two
   cell dimensions above: fdps_draw_glyph reads it with IMUL EAX,dword ptr
   [0x00064046] at 0001fd8f.  Signed, because that IMUL is the signed multiply
   and the index it scales is a signed int.

   It is the stride of the sheet and not a size derived from the cell: nothing
   recomputes it from the width and height, so a rebuild that rounds
   ceil(width/8)*height for itself will disagree with whatever the font loader
   wrote here. */
extern int data_fdps_font_glyph_stride_bytes;

/* 0006403e and 00064042.  The drop shadow's displacement from the glyph it
   belongs to: a column count in bytes and a row count in whole pitches, so
   fdps_draw_glyph places the shadow at dst + rows * pitch + columns
   (0001fe6d-0001fe81).  Both are full dwords and both are signed -- a shadow
   above or to the left of the glyph is a negative offset here, and reading
   either through an unsigned type turns it into an enormous forward step.

   They are read only when data_fdps_font_outline_enabled_flag is clear; the
   outline style ignores them entirely and steps by exactly one pixel. */
extern int data_fdps_font_shadow_offset_x;
extern int data_fdps_glyph_shadow_row_offset;

/* 000643bc.  Pointer to the game's master VGA palette: the 768 bytes of
   Fde.pal, 256 records of three 6-bit components in R, G, B order, so a
   struct fdps_palette_entry[256] read through a cast.  The startup resource
   loader fills it and a failure there is fatal, so past startup every reader
   may assume 768 valid bytes; nothing in the image ever writes through the
   pointer, so the block stays as loaded for the whole run.

   Fourteen files read it, and every one of them hands it to
   fdps_set_palette_range as the source of a whole-DAC upload.  Bias 0 means
   "put the normal screen back"; a fade is the same upload repeated with the
   bias ramped, which is why the block must not be modified in place -- each
   step re-derives the DAC from these untouched bytes, and that is what makes
   a ramp land back on the exact original palette at bias 0.

   The original types it as a byte pointer and casts at each use. */
extern unsigned char *data_fdps_vga_main_palette_ptr;

/* 0006404a.  Which decoration the text routines draw under a glyph: non-zero
   selects the four-way one-pixel outline, zero selects the single drop shadow
   at the offsets above.  A byte, tested with CMP byte ptr [0x0006404a],0x0 at
   0001fdf4.

   It is a style selector and not an enable: clearing it does not turn the
   decoration off, it switches to the other one. */
extern unsigned char data_fdps_font_outline_enabled_flag;

/* 00060138 and 00069cf0.  The .CEL sprite cache: one heap block holding a slot
   table of thirty struct fdps_cel_cache_slot at its base followed by every
   cached group's pixel bytes, and how many of those slots are filled.

   Null and zero until fdps_cache_cel_sprite_group (src/rsrc.h) is asked for a
   group; that function is the only writer of both, and every other reader --
   fifteen files for the pointer, five for the count -- takes a slot index it
   was given, reads the slot's offsets and adds the pointer to reach a stream.
   The block moves on every miss, because a miss reallocs it, so nothing may
   hold a pointer into it across a call that could cache a group.

   The count is signed: the linear key search compares it with JL.  The
   original types the block as a byte pointer and casts at each use. */
extern unsigned char *data_fdps_cel_sprite_cache_ptr;
extern int data_fdps_cel_sprite_cache_count;

/* 000653f0.  The blend weight table: 18 rows of 256 words, one row per weight
   and one word per palette entry, built by fdps_build_palette_tables
   (src/palette.h) and read by the alpha blitters in eighteen files.

   Row 0 is all zero, row 1 holds the palette itself in the nibble-per-byte
   form 0x000R0G0B, and rows 2..17 are row 1 scaled by that row's multiplier.
   The nibble layout is what makes one 32-bit multiply weight all three
   channels at once without any of them carrying into the next.

   It is ONE array, not two (rebuild_info/pitfalls.md, contract B).  The
   builder writes row 0 through the base 0x653f0 and row 1 through 0x657f0,
   which is the same array with 256 words folded into the displacement, and
   every reader indexes across the row boundary.  Splitting it into two globals
   would let the linker put the rows apart and the table would read as
   garbage. */
extern unsigned int data_fdps_palette_shade_ramp_table[4608];

/* 00064378.  How many cells of the battle indicator queue are filled, and
   equally the index the next cell is appended at: the cursor into the three
   parallel arrays src/indicat.h declares.  Signed, and it is a count and not a
   ring -- nothing wraps it and nothing checks it against the arrays' 200 cells.

   Every producer appends its popup's cells at the cursor and adds that many to
   it.  Two places put it back to zero and a sweep of every reference to 00064378
   finds no others: fdps_play_indicator_queue at 0001f4ff, after animating
   everything queued, and fdps_apply_item_effect_to_targets at 000262ac, on entry
   and before queueing a batch of its own, so popups left over from an
   interrupted playback cannot leak into an item's. */
extern int data_fdps_indicator_queue_count;

/* 00063f78, 00063f74, 00063f80 and 00063f84.  What the map AI's physical
   attack search decided: how good the best attack it found is, which unit it
   would hit, and the tile it would attack from.  fdps_map_actor_score_best_attack
   (src/aiscore.h) is the only writer of all four; fdps_map_actor_behavior_step
   and fdps_map_actor_take_best_action read the score, and
   fdps_map_actor_move_and_attack reads the other three back to carry the attack
   out.

   The score is a tier and not a damage figure -- 0, 8 or 0x12 -- on the same
   scale as data_fdps_battle_ai_best_spell_score and
   data_fdps_battle_ai_best_item_score below, which is what lets
   fdps_map_actor_take_best_action compare the three with one threshold.  It is
   signed: the search's own ranking compares it with JG at 000124d4.

   The two tile coordinates receive zero-extended tile bytes and the target
   index a zero-extended unit index, and every reader hands all three straight
   on as call arguments, so none of the three is ever compared or scaled.

   They are four separate globals and not a record: every writer and every
   reader names each one by its own absolute address, and
   data_fdps_map_ai_best_item_bag_slot sits between the score and the x
   coordinate at 00063f7c (rebuild_info/pitfalls.md, contract B).

   Only the score is written on every call.  The other three keep the previous
   actor's decision when the search finds nothing, so a reader that has not
   checked the score first is looking at a stale tile and a stale target. */
extern int data_fdps_battle_ai_best_physical_score;
extern int data_fdps_battle_ai_best_physical_target_idx;
extern int data_fdps_battle_ai_best_physical_target_x;
extern int data_fdps_battle_ai_best_attack_tile_y;

/* 00063f88, 00063f90, 00063f9c and 00063fa0.  What the map AI's spell search
   decided: how good the best spell it found is, which spell that is and the
   tile to centre the cast on.  fdps_map_actor_score_best_spell (src/aiscore.h)
   is the only writer of all four, and a sweep of every reference to 00063f88
   finds no other writer of the score anywhere in the image.

   The score is a tier on the same scale as
   data_fdps_battle_ai_best_physical_score above, which is what lets
   fdps_map_actor_take_best_action compare the three searches with one
   threshold.  It is signed: the search's own ranking compares it with JG at
   000135f7.

   The score is written on every call, zeroed before anything else so even the
   two early returns leave a fresh 0.  The other three are written only inside
   the winning branch and hold the previous actor's decision otherwise, so a
   reader that has not looked at the score first is looking at a stale spell and
   a stale tile.  Every reader in the image does look: fdps_map_actor_behavior_step
   gates at 00010693, fdps_map_actor_cast_chosen_spell re-checks at 00013cd9,
   and every arm of fdps_map_actor_take_best_action that reads the spell id has
   already established that the score is the largest of the three and reaches 6.

   The spell id receives a zero-extended MAGICDAT.DAT id and the two
   coordinates zero-extended tile bytes, and both readers hand all three
   straight on as call arguments, so none of them is ever compared or scaled.

   They are four separate globals and not a record: every writer and every
   reader names each one by its own absolute address, and
   data_fdps_battle_ai_best_item_score at 00063f8c,
   data_fdps_battle_ai_best_item_target_y at 00063f94 and
   data_fdps_map_ai_best_item_target_x at 00063f98 are interleaved between them
   (rebuild_info/pitfalls.md, contract B). */
extern int data_fdps_battle_ai_best_spell_score;
extern int data_fdps_map_ai_best_spell_id;
extern unsigned int data_fdps_battle_ai_best_spell_target_x;
extern unsigned int data_fdps_battle_ai_best_spell_target_y;

/* 00063f7c, 00063f8c, 00063f94 and 00063f98.  What the map AI's item search
   decided: how good the best use it found is, which of the acting unit's eight
   bag entries that use spends, and the tile to aim it at.
   fdps_map_actor_score_best_item (src/aiscore.h) writes all four, and a sweep
   of every reference to 00063f8c and 00063f7c finds no other writer of the
   score or of the bag slot anywhere in the image.  The two coordinates have a
   second writer: fdps_map_actor_use_item reads all four to carry the choice
   out and then rewrites the pair at 000273b5-00027443 as it walks the aim
   cursor onto the tile.

   The score is a tier on the same scale as
   data_fdps_battle_ai_best_physical_score and
   data_fdps_battle_ai_best_spell_score above, which is what lets
   fdps_map_actor_take_best_action compare the three searches with one
   threshold of 6.  It is signed: the search's own ranking compares it with JLE
   at 0001325d, and fdps_battle_enemy_turn_phase, fdps_map_actor_behavior_step
   and fdps_map_actor_take_best_action all read it back with JL / JGE.

   The score is written on every call, zeroed before anything else so even the
   empty-bag return leaves a fresh 0.  The other three are written only inside
   the winning branch and hold the previous actor's decision otherwise, so a
   reader that has not looked at the score first is looking at a stale slot and
   a stale tile.

   The bag slot receives the search's own loop counter, the two coordinates
   zero-extended tile bytes, and every reader hands all three straight on as
   call arguments, so none of them is ever compared or scaled.

   They are four separate globals and not a record, and they are not even
   contiguous: 00063f80 through 00063f88 belong to the physical and spell
   searches and sit between the bag slot and the score
   (rebuild_info/pitfalls.md, contract B). */
extern int data_fdps_map_ai_best_item_bag_slot;
extern int data_fdps_battle_ai_best_item_score;
extern int data_fdps_battle_ai_best_item_target_y;
extern int data_fdps_map_ai_best_item_target_x;

/* 000643f0.  The inverse palette: which DAC entry is nearest to each of the
   4096 quantised colours, one byte per cell, built by
   fdps_build_palette_tables (src/palette.h) and read by the same eighteen
   files as the table above -- the pair is what turns a blended 24-bit result
   back into a palette index.

   Green-major: the cell for a quantised (r, g, b), each 0..15, is at
   green * 256 + red * 16 + blue.  That is the order the builder's three nested
   loops write it in, and it is not the order the axis names suggest.

   It sits immediately below the shade ramp -- 000643f0 + 0x1000 is 000653f0 --
   but nothing indexes from one into the other, so the two are separate
   globals. */
extern unsigned char data_fdps_inverse_palette_cube[4096];

/* 00060040 and 00060058.  What the ground under a combatant does to its attack
   and to its defense, as a percentage indexed by the terrain class of the tile
   the unit is standing on -- data_fdps_map_tile_terrain_type above, left there
   by fdps_map_load_tile_info.  Both are applied the same way, as
   stat += table[terrain] * stat / 100, so an entry is a signed percentage
   delta and zero means the ground is neutral.

   Signed, and both hold negative entries in the shipped image: the dword loads
   feed IMUL and IDIV, and the terrain-modified totals are compared with the
   signed jumps by everything downstream.  Three functions read them --
   fdps_unit_resolve_attack_hit, fdps_combat_compute_hit_outcome and
   fdps_draw_cursor_info_panel -- and all three index both tables with the same
   terrain byte.

   They are two tables and not one 2 x 6 array: each reader names each base by
   its own absolute address, with the terrain index scaled by 4 against that
   base (rebuild_info/pitfalls.md, contract B).

   The declared width is six entries, which covers terrain classes 0..5, and
   the shipped maps place cells above that.  Resolving every placed cell of all
   68 M<map><layer>.MPL terrain layers through its own ATTR table -- width at
   +7, tile ids from +0x0b, terrain class at byte +2 of the row at 0x11 + 4 *
   tile id -- gives classes 0 (27398 cells), 1 (161), 2 (1018), 5 (7028) and 6
   (3222), with class 6 on sixteen maps' layer 0 and the majority class on
   several of them.  So a class of 6 indexes one entry past the end of either
   table on shipped data and not merely in theory.  In the original the entry
   it lands on is whatever follows the table: 00060058, which is the defense
   table's own first entry and is 0 in the image, and 00060070, whose first
   byte is the live global data_fdps_village_mode_flag.  Two tables linked as
   separate objects reproduce neither read.

   The two combat readers reach that index only for a unit standing on such a
   cell, and PROMAP.DAT gives move_cost[6] as 0xFF -- impassable -- in all 40
   class rows, so the movement flood fill cannot walk one onto one; the only
   row that costs a class-6 tile less is the all-ones default row 0, which the
   two range queries that are not asking about a particular unit pass a literal
   0 for.  fdps_draw_cursor_info_panel has no such precondition: at 0002de20
   and 0002de59 it indexes both tables with the terrain class of whatever cell
   the cursor is over.  Ticket 23 owns what these definitions ultimately
   are. */
extern int data_fdps_battle_tile_attr_ap_modifier_table[6];
extern int data_fdps_battle_tile_attr_def_modifier_table[6];

/* 00069cec.  The experience one blow just earned, worked out by the physical
   and magical damage resolvers and read back by the code that awards it.

   Whether a writer assigns or accumulates is that writer's own contract, and
   the image does both: fdps_unit_apply_heal, fdps_unit_apply_damage and
   fdps_unit_apply_status_effect add to it, while fdps_unit_resolve_attack_hit
   stores a fresh figure over whatever was there.  So a caller that wants the
   total of several physical blows has to take a copy after each one, and a
   blow that earns nothing -- a friendly-fire hit, or a target that is not an
   enemy record -- leaves the previous blow's figure standing rather than
   clearing it.

   Signed: every writer forms it with IDIV on sign-extended operands and the
   readers scale it with the signed multiply. */
extern int data_fdps_battle_pending_xp_credit;

/* 00069cd4 and 00069ccc.  Where the map cursor sits, held in map PIXELS and
   not in tiles: every writer stores the tile multiplied by the 24-pixel tile
   size -- IMUL EAX,dword ptr [EBP+0x20],0x18 / MOV [0x00069cd4],EAX at
   000136ff in fdps_collect_targets_in_line, and MOV EAX,dword ptr [EBP-0x24] /
   ADD EAX,0x18 / MOV [0x00069cd4],EAX at 00021f1e in
   fdps_icon_script_scroll_view_to_tile -- so a reader that wants the tile
   divides by 24.  fdps_chapter_state_reset zeroes both at 000227a4.

   Note the pair is stored with y at the LOWER address; they are two separate
   globals and nothing in the image indexes across them as a two-element array.

   Signed: every division of them is SAR EDX,0x1f / IDIV, so a cursor pixel
   left of or above the map origin truncates towards zero -- -1 gives tile 0 --
   rather than becoming a very large positive tile number. */
extern int data_fdps_map_cursor_world_x;
extern int data_fdps_map_cursor_world_y;

/* 000643b4.  Base of the battle unit gauge sheet: three 43x6 8bpp graphics
   laid end to end, 0x102 (43 * 6) bytes apart, at a row pitch of 0x2b.
   Graphic 0 is the empty track and graphics 1 and 2 are the two filled
   colours, one per side of the battle.

   fdps_load_global_resources builds it once at startup and it never moves:
   PUSH 0x306 / CALL malloc / MOV [0x000643b4],EAX at 00029cd5, the whole 0x306
   bytes memset to 0 at 00029cf4, then a three-iteration loop -- CMP dword ptr
   [EBP-0x8],0x3 / JL at 00029d03 -- decoding the three frames of "EasyBar.cel"
   (the string at 00061d64) into base + i * 0x102 at 0x2b by 6.
   fdps_shutdown_free_resources frees it at 00029502.

   The original types it as a byte pointer and does the frame arithmetic at
   every use, so a reader casts nothing and just adds.  Because the memset
   covers the buffer before the decode, a frame the .cel does not supply reads
   as 0 -- transparent -- rather than as rubbish. */
extern unsigned char *data_fdps_unit_gauge_sheet_ptr;

/* 000643c8.  Base of the status panel's gauge bar sheet: three 117x8 8bpp
   graphics laid end to end, 0x3a8 (117 * 8) bytes apart, at a row pitch of
   0x75.  Graphic 0 is the empty track and graphics 1 and 2 are the two filled
   colours, the HP one and the MP one.

   fdps_load_global_resources builds it once at startup and it never moves:
   PUSH 0xaf8 / CALL malloc / MOV [0x000643c8],EAX at 00029dd5, the whole 0xaf8
   bytes memset to 0 at 00029df4, then a three-iteration loop -- CMP dword ptr
   [EBP-0x8],0x3 / JL at 00029e03 -- decoding the three frames of "Bar.cel"
   (the string at 00061d70) into base + i * 0x3a8 at 0x75 by 8.
   fdps_shutdown_free_resources frees it at 000294d8.

   The original types it as a byte pointer and does the frame arithmetic at
   every use, so a reader casts nothing and just adds.  Because the memset
   covers the buffer before the decode, a frame the .cel does not supply reads
   as 0 -- transparent -- rather than as rubbish. */
extern unsigned char *data_fdps_status_gauge_bar_sheet_ptr;

/* 00064000.  Base of the combat gauge fill sheet: ONE 0x9c4-byte surface
   holding four 125x5 fill strips stacked five rows apart at a row pitch of
   0x7d, so strip N begins N * 0x271 bytes in and 4 * 0x271 is exactly the
   whole 0x9c4.  The four strips are one contiguous surface and not four
   separate images -- a reader walks a strip's rows with a stride of 0x7d,
   which is only the strip's own width because they share the buffer.

   Unlike the sheets at 0x643xx this one is not loaded once at startup.  Both
   combat presenters build it for themselves and free it again:
   fdps_combat_play_attack_exchange has PUSH 0x9c4 / CALL malloc /
   MOV [0x00064000],EAX at 00018e15, then a four-iteration loop at
   00018e2e-00018e68 decoding sprites 4..7 of the FigBar.cel that
   data_fdps_combat_gauge_sprite_sheet_ptr holds into the buffer at pitch 0x7d
   and y = i * 5, and frees it at 00019192.
   fdps_combat_play_spell_on_targets does the identical thing at
   0001a603-0001a653 and frees it at 0001b8f4.  Neither stores to the global
   again after the free, so outside a combat animation it holds a stale
   pointer and nothing reads it. */
extern unsigned char *data_fdps_gauge_fill_sheet_ptr;

/* 00069d80.  Base of the loaded chapter's SHOP%02d.DAT image: three rows of
   twelve item-id bytes, 36 bytes in all, with row 0 the item shop, row 1 the
   weapon shop and row 2 the secret shop.  The row is the whole record -- there
   is no count byte and no header -- so a reader walks all twelve.

   0xff marks an EMPTY SLOT and not the end of the row, and the shipped members
   put stock after one: SHOP01.DAT's weapon row is 02 71 FF FF 72 73 FF FF FF
   FF FF FF and SHOP03.DAT's begins with the empty slot, FF 03 FF 1E 1F 30 64
   65 74 75 82 83 (rebuild_info/pitfalls.md).

   Unsigned, and that is behaviour rather than spelling: the read is XOR EAX,
   EAX / MOV AL,byte ptr [EDX] at 0003173b, and ids above 0x7f are ordinary
   stock -- SHOP01.DAT's item row is B4 DE.  Through a signed char pointer the
   0xff compares as -1, the empty-slot test never fires, and every high id
   arrives negative.

   fdps_load_field_chapter_resources loads the member out of "Field.vfs" and
   stores the block here, MOV [0x00069d80],EAX at 000315bd;
   fdps_run_village_phase frees it, PUSH dword ptr [0x00069d80] / CALL free at
   000314ac, and does not clear the global, so outside a village visit this
   holds a stale pointer. */
extern unsigned char *data_fdps_shop_stock_table_ptr;

/* 00069cf4.  Which chapter is loaded, counted from 0: the chapter the player
   is told is chapter 1 is id 0.  Twenty-seven files read it, most of them to
   build a per-chapter resource file name or to pick a chapter's row out of a
   table, and the two conventions do not agree -- fdetxt%02d.txt is formatted
   from the value plus one while map%02d.dat and its neighbours take it raw --
   so a reader has to say which numbering it wants.

   fdps_check_secret_code_key indexes its 24-row table with the value minus
   one, which is the same 0-based reading: the table's first row belongs to id
   1, the chapter the player sees as chapter 2. */
extern int data_fdps_chapter_current_chapter_id;

/* 00069da0.  How the current battle is to end, and until then that it is not:
   0 is still running, 1 is defeat and 2 is the chapter cleared.  Ten files
   write it and the battle loops read it; nothing else in the image touches it.

   It is a flag and not a stop: a handler that decides the battle is over just
   stores its code here and returns, and the phase loops -- the player phase,
   the enemy phase and the NPC phase -- each test it against 0 at the top of an
   iteration and return when it is anything else.  The caller of the battle
   loop then reads it once, dispatches on 0 / 1 / 2, and stores 0 back before
   the next chapter starts.  The chapter state reset installs 0 as well.

   Unsigned, and that is behaviour rather than spelling: the dispatch on the
   value loads it and compares with JC at 00029377 and JBE at 0002937d, which
   are unsigned branches, so a code the compare treats as huge falls out of the
   handled range instead of landing on the low side of it.  Every other test in
   the image is an equality against 0 or 2, so nothing else can see the
   difference.

   Every store in the image writes a literal 0, 1 or 2; there is no
   accumulation and no bit in it. */
extern unsigned int data_fdps_chapter_event_or_battle_end_code;

/* 00069ce8.  Which turn of the current battle is being played, counted from 1.
   fdps_chapter_state_reset installs 1 at 000227c2, fdps_battle_advance_turn
   raises it once per turn cycle -- INC dword ptr [0x00069ce8] at 0001e5fd,
   reached only after the battle-end code at 00069da0 has tested 0 -- and
   fdps_load_savegame restores it out of the save image at 0002410c.  Nothing
   else writes it, so it only ever counts up within one battle.

   Signed, and that is behaviour rather than spelling.  Nine chapter handlers
   order it with a signed branch: JG at 000377d7, 00037b88, 00038a4a, 0003af5b,
   0003b163 and 0003b4ab, JLE at 00037cfb, 0003831d and 0003a73b.  On top of
   those, fdps_chapter_28_event_deploy_wave_for_turn halves it at
   00039564..0003956f with SAR EDX,0x1f / SUB EAX,EDX / SAR EAX,0x1, the
   round-toward-zero correction wcc386 emits only for a signed operand.  The
   remaining reads are equalities or small additions, which cannot see the
   difference.

   fdps_battle_run_turn_events fires a turn-event record while the counter
   still holds the turn whose phase has just ended, so a handler dispatched
   from that table reads the turn number the record was scheduled for, not the
   next one.  The chapter-event handlers that branch on it therefore compare
   against the very turn numbers the map files' turn-event tables carry.

   fdps_draw_turn_number pushes the value itself into the formatted string at
   0001ea8c, so the number the player is shown is this one unadjusted;
   fdps_battle_system_submenu copies only its low byte into a byte buffer at
   000151c8..000151d0. */
extern int data_fdps_battle_turn_counter;

/* 00069de8.  Flat linear address of the 512-byte DOS real-mode block the
   MSCDEX request header is built in.  It is the real-mode segment
   fdps_cd_alloc_dos_buffers got back from DPMI, shifted left four: DOS/4GW
   identity maps the first megabyte, so the same bytes the real-mode driver
   sees through data_fdps_cd_request_header_real_mode_seg are reachable through
   this pointer directly.  Zero until fdps_cdrom_detect runs. */
extern unsigned char *data_fdps_cd_request_header_buffer;

/* 00069da4.  Flat linear address of the second 512-byte DOS real-mode block,
   the one the driver transfers IOCTL replies and audio status into.  Same
   segment-shifted-left-four relationship as the header block above, and the
   same lifetime: allocated once, never freed. */
extern unsigned char *data_fdps_cd_ioctl_buffer;

/* 00069da8.  The same IOCTL block as a real-mode far pointer already packed
   into one dword, segment in the high half and offset zero in the low half,
   which is the form the MSCDEX request header's transfer address field wants.
   fdps_cd_alloc_dos_buffers stores it that way at 0003bb5b-0003bb65 so that no
   later call has to build it; the low half is therefore always zero. */
extern unsigned int data_fdps_cd_ioctl_buffer_real_mode_ptr;

/* 00069e20.  The request status word the CD-ROM device driver left in the last
   request header the module sent, published here by every function that sends
   one so that the answer outlives the stack frame the header was built in.  It
   is the word at header+3 -- the DOS device-driver status word, whose bit 15 is
   error, bit 9 busy and bit 8 done -- copied out whole and unmasked; the
   senders store it and never look at it themselves.
   fdps_cd_status_is_not_busy at 0003c6d0 is what reads it. */
extern unsigned short data_fdps_cd_last_request_status;

/* 00069e07.  The disc's last track number, from the Read Disk Info reply
   fdps_cdrom_read_disk_info publishes.  It is the module's bound on every
   track walk: fdps_cd_get_track_length_sectors compares the track it was asked
   about against it at 0003c1d8 to decide whether the next track's start or the
   lead-out is the end of that track, and fdps_cd_resolve_track_range does the
   same at 0003c821.

   Unsigned: the compare at 0003c1d8 is followed by JNC and the load at
   0003c821 is a MOVZX.  Zero when no driver answered the query, which the
   readers do not distinguish from a disc whose last track is 0. */
extern unsigned char data_fdps_cd_highest_track_number;

/* 00069e0b.  Where the disc's lead-out starts, as a logical sector number:
   the packed lead-out address from the Read Disk Info reply run through
   fdps_cd_msf_to_sector.  It is the end of the last track for every caller
   that needs one -- the play range fdps_cd_play_whole_disc hands the drive
   ends here, and the two track-length routines fall back to it for the last
   track on the disc.

   Unsigned, on its readers: fdps_cd_get_disk_info_msf subtracts the 150-frame
   lead-in at 0003c2a8 and then divides by 75 with DIV, the unsigned divide.
   The value it is given is signed arithmetic that is not clamped, so a disc
   the driver never answered for leaves 00:00:00 here and the -150 that comes
   out of the conversion arrives as 0xffffff6a. */
extern unsigned int data_fdps_cd_leadout_sector;

#endif
