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

/* 00060144.  Base of the battle map's working movement grid: a four-byte
   header of two signed 16-bit dimensions followed by width*height two-byte
   cells (see src/movegrid.h).  Null until a chapter has been loaded, and the
   readers all test it before use.  The original types it as a byte pointer and
   does the header and cell arithmetic itself, so a reader casts. */
extern unsigned char *data_fdps_battle_move_grid_ptr;

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

#endif
