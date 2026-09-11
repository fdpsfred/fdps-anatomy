/* tests/btlturn.c -- cover for src/btlturn.c.
 *
 * Expected values come from the assembly at 000119b0 -- IMUL EAX,dword ptr
 * [EBP+0x14],0x50 for the stride and its signedness, MOV EDX,dword ptr
 * [0x00069cd8] for the base being re-read from the global, OR byte ptr
 * [EAX+0x5],0x80 for the field, the bit and the read-modify-write -- and from
 * the record layout ticket 17 settled (flags at +5, stride 0x50).  None of
 * them is read off the emitted C.
 *
 * The unit array is staged here rather than read from a game file: the
 * function takes its entire input from data_fdps_map_unit_array_ptr and one
 * argument, so pointing that global at a local block is the only way to reach
 * the write.  Nothing below asserts what the global itself holds -- ticket 23
 * owns that.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chapter.h"
#include "vfs.h"
#include "btlturn.h"

/* Six records' worth of room.  Record 1 is the one the global is normally
   pointed at, which leaves record 0 below it for the negative-index case and
   four above it for the stride and out-of-bound cases. */
#define STAGE_RECORDS 6
#define STAGE_BASE_RECORD 1

static unsigned char stage_units[STAGE_RECORDS * 0x50];

/* Fill every byte with 0x00 except the status byte of each record, which gets
   0x00 too but is read back individually below.  0x5a in every other byte is
   the sentinel that says a byte was not meant to be touched. */
static void stage(void)
{
    int i;

    for (i = 0; i < STAGE_RECORDS * 0x50; i++) {
        stage_units[i] = 0x5a;
    }
    for (i = 0; i < STAGE_RECORDS; i++) {
        stage_units[i * 0x50 + 5] = 0x00;
    }
    data_fdps_map_unit_array_ptr = stage_units + STAGE_BASE_RECORD * 0x50;
    data_fdps_map_unit_count = 3;
}

/* Status byte of the record `index` records away from the staged base, so a
   negative index reads the record staged below it. */
static int status_of(int index)
{
    return (int) stage_units[(STAGE_BASE_RECORD + index) * 0x50 + 5];
}

/* IMUL by 0x50 and the +0x5 displacement are the only two layout facts the
   function has, and both have to agree with the struct for the emitted
   pointer arithmetic to land on the same byte the original wrote. */
static void mark_record_layout_matches_the_assembly(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
}

/* The plain case: OR byte ptr [EAX+0x5],0x80 with the index the callers pass,
   raising bit 7 and nothing else. */
static void mark_raises_bit_seven(void)
{
    stage();
    fdps_battle_mark_unit_done(0);
    CHECK_EQ(status_of(0), 0x80);
}

/* IMUL EAX,[EBP+0x14],0x50: the argument is scaled by the record size, so a
   later index reaches a later record and no other record is written. */
static void mark_scales_the_index_by_the_record_size(void)
{
    stage();
    fdps_battle_mark_unit_done(2);
    CHECK_EQ(status_of(0), 0x00);
    CHECK_EQ(status_of(1), 0x00);
    CHECK_EQ(status_of(2), 0x80);
    CHECK_EQ(status_of(3), 0x00);
}

/* OR, not MOV.  Bit 0 of the same byte is the retired flag that
   fdps_unit_is_retired reads, and the phase engine tests flags & 0x81 -- a
   store of 0x80 would clear it and put a dead unit back in the phase. */
static void mark_preserves_the_other_status_bits(void)
{
    stage();
    stage_units[STAGE_BASE_RECORD * 0x50 + 5] = 0x01;
    fdps_battle_mark_unit_done(0);
    CHECK_EQ(status_of(0), 0x81);

    stage();
    stage_units[STAGE_BASE_RECORD * 0x50 + 5] = 0x3f;
    fdps_battle_mark_unit_done(0);
    CHECK_EQ(status_of(0), 0xbf);
}

/* Marking a unit that is already done is a no-op on the byte: OR of a bit that
   is already set leaves the value alone.  fdps_battle_unit_turn reaches here
   on each of its exits, so the same unit can be marked twice in a turn. */
static void mark_twice_is_the_same_as_marking_once(void)
{
    stage();
    fdps_battle_mark_unit_done(1);
    CHECK_EQ(status_of(1), 0x80);
    fdps_battle_mark_unit_done(1);
    CHECK_EQ(status_of(1), 0x80);
}

/* One byte is written and it is the one at +5.  Nothing else in the record --
   the coordinates below it, the side byte above it -- is read or written, so
   every other byte still carries the 0x5a sentinel. */
static void mark_writes_only_the_status_byte(void)
{
    int i;
    int untouched;

    stage();
    fdps_battle_mark_unit_done(1);

    untouched = 1;
    for (i = 0; i < 0x50; i++) {
        if (i != 5 && stage_units[(STAGE_BASE_RECORD + 1) * 0x50 + i] != 0x5a) {
            untouched = 0;
        }
    }
    CHECK_EQ(untouched, 1);
    CHECK_EQ((int) stage_units[(STAGE_BASE_RECORD + 1) * 0x50 + 4], 0x5a);
    CHECK_EQ((int) stage_units[(STAGE_BASE_RECORD + 1) * 0x50 + 6], 0x5a);
}

/* There is no CMP against data_fdps_map_unit_count anywhere in the function:
   the record address is formed and written whatever the index holds.  The
   count is set to 3 by stage() and index 4 is written all the same. */
static void mark_does_not_check_the_unit_count(void)
{
    stage();
    fdps_battle_mark_unit_done(4);
    CHECK_EQ((int) data_fdps_map_unit_count, 3);
    CHECK_EQ(status_of(4), 0x80);
}

/* IMUL is the signed multiply, so a negative index scales to a negative
   displacement and the byte 0x50 bytes below the base is the one written.  An
   unsigned multiply would put the write about four gigabytes away instead. */
static void mark_index_is_signed(void)
{
    stage();
    fdps_battle_mark_unit_done(-1);
    CHECK_EQ(status_of(-1), 0x80);
    CHECK_EQ(status_of(0), 0x00);
}

/* MOV EDX,dword ptr [0x00069cd8] is inside the function body, not hoisted:
   the base is whatever the global holds at the moment of the call, so moving
   the array moves where the write lands. */
static void mark_reads_the_base_from_the_global_each_call(void)
{
    stage();
    fdps_battle_mark_unit_done(0);
    CHECK_EQ(status_of(0), 0x80);

    data_fdps_map_unit_array_ptr = stage_units + (STAGE_BASE_RECORD + 2) * 0x50;
    fdps_battle_mark_unit_done(0);
    CHECK_EQ(status_of(2), 0x80);
    CHECK_EQ(status_of(1), 0x00);
}

/* fdps_battle_run_turn_events at 0002e0c0, from here down.
 *
 * The function's whole input is three things -- the block
 * data_fdps_tile_event_data_table_ptr points at, data_fdps_battle_turn_counter
 * and its own argument -- and its whole output is which slots of
 * data_fdps_chapter_event_handler_table it calls and with what.  So the cases
 * below point the pointer at a staged block and install counting handlers in
 * the table, which is how tests/btlact.c reaches the same table.
 *
 * Expected values come from the assembly -- CMP dword ptr [EBP-0x4],0x10 for
 * the sixteen entries, LEA EAX,[EAX+EAX*0x2] for the three-byte stride, the
 * +0x3/+0x4/+0x5 displacements for the fields, AND EAX,0xff for the widening,
 * PUSH 0x0 for the handler's argument and the single backward JMP at
 * 0002e149 for there being no early exit -- and, for the last case, from the
 * shipped MAP02.DAT's own turn-event table.
 */

#define EVENT_TABLE_AT 3
#define EVENT_ENTRY_BYTES 3
#define EVENT_ENTRY_COUNT 16

/* One entry's worth of room past the sixteen, so a walk that ran one entry too
   far would fire a handler and be seen. */
#define EVENT_BLOCK_BYTES \
    (EVENT_TABLE_AT + (EVENT_ENTRY_COUNT + 1) * EVENT_ENTRY_BYTES)

/* The filler the shipped maps put in the entries they do not use: turn 0xff,
   handler 0xff, side 0x00. */
#define EVENT_FILLER_TURN 0xff
#define EVENT_FILLER_HANDLER 0xff
#define EVENT_FILLER_SIDE 0x00

#define SIDE_ENEMY 0
#define SIDE_PLAYER 2

#define FIELD_NAME "FIELD.VFS"
#define FIELD_VFS "Field.vfs"
#define MAP_MEMBER "map02.dat"

/* MAP02.DAT's table: entries 0..11 are turns 3 to 14 on handler slot 0 and
   side 0, entry 12 is turn 22 on handler slot 5 and side 0, and the rest is
   filler. */
#define MAP02_FIRST_TURN 3
#define MAP02_FIRST_SLOT 0
#define MAP02_LAST_TURN 22
#define MAP02_LAST_SLOT 5

static unsigned char stage_block[EVENT_BLOCK_BYTES];
static unsigned char stage_block_b[EVENT_BLOCK_BYTES];

/* Which handler slots ran, in the order they ran, and what each was passed. */
#define EVENT_LOG_MAX 8
static int handler_slots[EVENT_LOG_MAX];
static int handler_args[EVENT_LOG_MAX];
static int handler_calls;

static void log_call(int slot, int unit_index)
{
    if (handler_calls < EVENT_LOG_MAX) {
        handler_slots[handler_calls] = slot;
        handler_args[handler_calls] = unit_index;
    }
    handler_calls++;
}

static void handler_in_slot_0(int unit_index)
{
    log_call(0, unit_index);
}

static void handler_in_slot_1(int unit_index)
{
    log_call(1, unit_index);
}

static void handler_in_slot_2(int unit_index)
{
    log_call(2, unit_index);
}

static void handler_in_slot_5(int unit_index)
{
    log_call(5, unit_index);
}

/* Stands in for a handler that reloads the chapter: it publishes a different
   block, which is what the second half of the walk has to read if the pointer
   is re-read per entry. */
static void handler_repoints_the_block(int unit_index)
{
    log_call(1, unit_index);
    data_fdps_tile_event_data_table_ptr = stage_block_b;
}

static void set_entry(unsigned char *block, int index, int turn, int handler,
                      int side)
{
    unsigned char *entry;

    entry = block + EVENT_TABLE_AT + index * EVENT_ENTRY_BYTES;
    entry[0] = (unsigned char) turn;
    entry[1] = (unsigned char) handler;
    entry[2] = (unsigned char) side;
}

/* Every entry of both blocks filled the way an unused entry is filled, the log
   emptied, and the four slots the cases fire installed.  The seventeenth
   entry, the one past the end, is filled the same way and each case that cares
   overwrites it. */
static void events_stage(void)
{
    int i;

    for (i = 0; i < EVENT_BLOCK_BYTES; i++) {
        stage_block[i] = 0;
        stage_block_b[i] = 0;
    }
    for (i = 0; i < EVENT_ENTRY_COUNT + 1; i++) {
        set_entry(stage_block, i, EVENT_FILLER_TURN, EVENT_FILLER_HANDLER,
                  EVENT_FILLER_SIDE);
        set_entry(stage_block_b, i, EVENT_FILLER_TURN, EVENT_FILLER_HANDLER,
                  EVENT_FILLER_SIDE);
    }
    data_fdps_tile_event_data_table_ptr = stage_block;
    data_fdps_battle_turn_counter = 4;

    handler_calls = 0;
    for (i = 0; i < EVENT_LOG_MAX; i++) {
        handler_slots[i] = -1;
        handler_args[i] = -1;
    }
    data_fdps_chapter_event_handler_table[0] = handler_in_slot_0;
    data_fdps_chapter_event_handler_table[1] = handler_in_slot_1;
    data_fdps_chapter_event_handler_table[2] = handler_in_slot_2;
    data_fdps_chapter_event_handler_table[5] = handler_in_slot_5;
}

static void events_unstage(void)
{
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_battle_turn_counter = 0;
    data_fdps_chapter_event_handler_table[0] = NULL;
    data_fdps_chapter_event_handler_table[1] = NULL;
    data_fdps_chapter_event_handler_table[2] = NULL;
    data_fdps_chapter_event_handler_table[5] = NULL;
}

/* An entry whose turn byte equals the counter and whose side byte equals the
   argument calls the slot its middle byte names, and the literal PUSH 0x0 is
   what that handler is passed. */
static void events_fires_the_entry_that_matches(void)
{
    events_stage();
    set_entry(stage_block, 0, 4, 2, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 2);
    CHECK_EQ(handler_args[0], 0);
    events_unstage();
}

/* Both bytes have to agree: the turn byte against
   data_fdps_battle_turn_counter, JNZ at 0002e102, and the side byte against
   the argument, JZ at 0002e11d.  One without the other fires nothing. */
static void events_needs_both_the_turn_and_the_side(void)
{
    events_stage();
    set_entry(stage_block, 0, 4, 2, SIDE_PLAYER);
    set_entry(stage_block, 1, 5, 1, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 0);

    fdps_battle_run_turn_events(SIDE_PLAYER);

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 2);
    events_unstage();
}

/* There is no early exit in the body -- the only backward branch is the loop's
   own JMP 0x0002e149 -- so every entry carrying the turn and the side fires,
   and they fire in table order. */
static void events_fires_every_match_in_table_order(void)
{
    events_stage();
    set_entry(stage_block, 2, 4, 2, SIDE_ENEMY);
    set_entry(stage_block, 5, 4, 0, SIDE_ENEMY);
    set_entry(stage_block, 9, 4, 1, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 3);
    CHECK_EQ(handler_slots[0], 2);
    CHECK_EQ(handler_slots[1], 0);
    CHECK_EQ(handler_slots[2], 1);
    events_unstage();
}

/* CMP dword ptr [EBP-0x4],0x10 bounds the walk at sixteen entries: the
   sixteenth, index 15, is read and the one after it is not. */
static void events_walks_exactly_sixteen_entries(void)
{
    events_stage();
    set_entry(stage_block, EVENT_ENTRY_COUNT - 1, 4, 0, SIDE_ENEMY);
    set_entry(stage_block, EVENT_ENTRY_COUNT, 4, 1, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 0);
    events_unstage();
}

/* The table starts at offset 3 of the block: the three header bytes in front
   of it are never read as an entry, however well they would match. */
static void events_skips_the_three_header_bytes(void)
{
    events_stage();
    stage_block[0] = 4;
    stage_block[1] = 2;
    stage_block[2] = SIDE_ENEMY;

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 0);
    events_unstage();
}

/* AND EAX,0xff widens the turn byte, so it is unsigned: 0xff is 255 and
   matches a counter of 255, not one of -1.  A MOVSX would invert both of
   these (contract C). */
static void events_widens_the_turn_byte_unsigned(void)
{
    int i;

    events_stage();
    for (i = 0; i < EVENT_ENTRY_COUNT + 1; i++) {
        set_entry(stage_block, i, 0, EVENT_FILLER_HANDLER, 0x7f);
    }
    set_entry(stage_block, 0, 0xff, 2, SIDE_ENEMY);

    data_fdps_battle_turn_counter = -1;
    fdps_battle_run_turn_events(SIDE_ENEMY);
    CHECK_EQ(handler_calls, 0);

    data_fdps_battle_turn_counter = 255;
    fdps_battle_run_turn_events(SIDE_ENEMY);
    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 2);
    events_unstage();
}

/* The side byte is widened the same way and compared against the whole dword
   argument, so a side of 0x100 -- byte 0 with a bit above it -- matches
   nothing.  The argument is not truncated to a byte anywhere. */
static void events_compares_the_whole_side_argument(void)
{
    events_stage();
    set_entry(stage_block, 0, 4, 2, SIDE_ENEMY);

    fdps_battle_run_turn_events(0x100);

    CHECK_EQ(handler_calls, 0);
    events_unstage();
}

/* MOV EDX,dword ptr [0x0006013c] is inside the loop: the block pointer is
   re-read on every entry, so a handler that publishes another block has the
   rest of the walk read the new one.  Entry 0 of the first block repoints;
   entry 5 of the second one is what fires next. */
static void events_rereads_the_block_pointer_each_entry(void)
{
    events_stage();
    data_fdps_chapter_event_handler_table[1] = handler_repoints_the_block;
    set_entry(stage_block, 0, 4, 1, SIDE_ENEMY);
    set_entry(stage_block, 5, 4, 0, SIDE_ENEMY);
    set_entry(stage_block_b, 5, 4, 2, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 2);
    CHECK_EQ(handler_slots[0], 1);
    CHECK_EQ(handler_slots[1], 2);
    events_unstage();
}

/* The function writes no global and no byte of the block: the counter it
   compares against comes out unchanged and so does every entry it read. */
static void events_writes_nothing(void)
{
    int i;
    int unchanged;

    events_stage();
    set_entry(stage_block, 3, 4, 2, SIDE_ENEMY);

    fdps_battle_run_turn_events(SIDE_ENEMY);

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(data_fdps_battle_turn_counter, 4);

    unchanged = 1;
    for (i = 0; i < EVENT_ENTRY_COUNT + 1; i++) {
        if (i != 3 &&
            stage_block[EVENT_TABLE_AT + i * EVENT_ENTRY_BYTES] !=
                EVENT_FILLER_TURN) {
            unchanged = 0;
        }
    }
    CHECK_EQ(unchanged, 1);
    CHECK_EQ((int) stage_block[EVENT_TABLE_AT + 3 * EVENT_ENTRY_BYTES], 4);
    CHECK_EQ((int) stage_block[EVENT_TABLE_AT + 3 * EVENT_ENTRY_BYTES + 1], 2);
    events_unstage();
}

/* The same walk over a block the game shipped rather than one staged here.
   MAP02.DAT's table is the largest of the sixty-three: turns 3 to 14 on
   handler slot 0 and turn 22 on slot 5, all of them on side 0.  It is loaded
   out of FIELD.VFS the way fdps_field_load_chapter_resources loads it, and the
   case skips itself if the container was not staged -- a member that is not
   there ends the process inside fdps_vfs_load_entry rather than failing a
   check. */
static void events_runs_the_shipped_map02_table(void)
{
    FILE *fp;
    unsigned char *block;

    fp = fopen(FIELD_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);

    events_stage();
    block = (unsigned char *) fdps_vfs_load_entry(FIELD_VFS, MAP_MEMBER);
    data_fdps_tile_event_data_table_ptr = block;

    /* The first entry of the shipped table, read at the offsets the emitted
       code uses.  If the table did not start at 3 with a stride of 3 these
       three would not be 3, 0 and 0. */
    CHECK_EQ((int) block[EVENT_TABLE_AT], MAP02_FIRST_TURN);
    CHECK_EQ((int) block[EVENT_TABLE_AT + 1], MAP02_FIRST_SLOT);
    CHECK_EQ((int) block[EVENT_TABLE_AT + 2], SIDE_ENEMY);

    data_fdps_battle_turn_counter = MAP02_FIRST_TURN;
    fdps_battle_run_turn_events(SIDE_ENEMY);
    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], MAP02_FIRST_SLOT);
    CHECK_EQ(handler_args[0], 0);

    /* The same turn on the player's phase fires nothing: every entry this map
       carries is on side 0. */
    handler_calls = 0;
    fdps_battle_run_turn_events(SIDE_PLAYER);
    CHECK_EQ(handler_calls, 0);

    /* Turn 22 is the one entry that names a different handler. */
    handler_calls = 0;
    data_fdps_battle_turn_counter = MAP02_LAST_TURN;
    fdps_battle_run_turn_events(SIDE_ENEMY);
    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], MAP02_LAST_SLOT);

    /* A turn no entry of this map is scheduled for. */
    handler_calls = 0;
    data_fdps_battle_turn_counter = 100;
    fdps_battle_run_turn_events(SIDE_ENEMY);
    CHECK_EQ(handler_calls, 0);

    free(block);
    events_unstage();
}

void run_btlturn_tests(void)
{
    RUN_TEST(mark_record_layout_matches_the_assembly);
    RUN_TEST(mark_raises_bit_seven);
    RUN_TEST(mark_scales_the_index_by_the_record_size);
    RUN_TEST(mark_preserves_the_other_status_bits);
    RUN_TEST(mark_twice_is_the_same_as_marking_once);
    RUN_TEST(mark_writes_only_the_status_byte);
    RUN_TEST(mark_does_not_check_the_unit_count);
    RUN_TEST(mark_index_is_signed);
    RUN_TEST(mark_reads_the_base_from_the_global_each_call);

    RUN_TEST(events_fires_the_entry_that_matches);
    RUN_TEST(events_needs_both_the_turn_and_the_side);
    RUN_TEST(events_fires_every_match_in_table_order);
    RUN_TEST(events_walks_exactly_sixteen_entries);
    RUN_TEST(events_skips_the_three_header_bytes);
    RUN_TEST(events_widens_the_turn_byte_unsigned);
    RUN_TEST(events_compares_the_whole_side_argument);
    RUN_TEST(events_rereads_the_block_pointer_each_entry);
    RUN_TEST(events_writes_nothing);
    RUN_TEST(events_runs_the_shipped_map02_table);

    /* Put the globals back before leaving.  stage() points them at this
       file's own fixture and the runners share one process: a later unit that
       expects an unallocated array would inherit a live pointer into another
       translation unit's block and pass or fail for the wrong reason.  The
       turn-event cases put back what they touched as each of them ends, and
       events_unstage does the same for the block pointer, the turn counter and
       the four handler slots. */
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
}
