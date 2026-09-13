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

/* fdps_battle_enemy_turn_phase at 00012960, from here down.
 *
 * WHAT CAN BE REACHED AND WHAT CANNOT.  The phase drives five real functions:
 * fdps_relocate_unit_array and fdps_get_unit_record (unit.h),
 * fdps_map_actor_score_best_spell and fdps_map_actor_score_best_item
 * (aiscore.h) and fdps_map_actor_behavior_step (mapai.h).  The first four run
 * here for real.  The fifth cannot be OBSERVED from a test at all, for the
 * reason tests/mapai.c sets out at length: every one of its eleven behaviour
 * arms ends in the tail that calls fdps_render_view_frame, which spins on the
 * VGA retrace bit and then on a tick counter nothing in a test process moves,
 * and its one arm that returns before the tail -- behaviour 8 -- writes
 * nothing whatever.  So the phase's score gate is pinned on its NEGATIVE side
 * only: the cases below show the two scorers running and the gate refusing the
 * unit when both scores come back below six.  A score of six or more needs the
 * loaded MAGICDAT.DAT and ITEM.DAT tables and a live movement grid, and taking
 * the gate's positive side would then hang the run inside the frame clock.
 *
 * The SECOND sweep hands every eligible unit to the behaviour step with no
 * gate in front of it, so an eligible fixture cannot avoid that call -- which
 * is why every one of them carries behaviour 8.
 *
 * For the same reason the phase's event dispatch is pinned on its negative
 * side only.  data_fdps_chapter_pending_event_idx is seeded with 0xff at the
 * top of each iteration and the only code that runs between that seed and the
 * test of it is the two scorers and the behaviour step; the scorers never
 * write the slot and the behaviour step is out of reach, so no test can make
 * the slot non-0xff at the moment the phase reads it.  What IS pinned, by
 * phase_seeds_the_pending_slot_every_iteration, is that the seed happens once
 * per unit and not once per phase -- a build that hoisted it would let a value
 * written by one iteration's post-action handler fire an event handler on the
 * next, and that case would see it.
 *
 * HOW THE SWEEPS ARE TOLD APART.  The post-action handler is called once per
 * unit in each sweep, after the unit has been dealt with and before the
 * battle-end check, and it is the phase's only observation point.  So it logs
 * what it can see -- the array pointer, the cursor draw mode, the unit count --
 * and the cases read the log back.  A unit the eligibility gate turns away
 * still reaches it, which is what lets every gate case below run with no
 * behaviour step in it.
 *
 * THE ARRAY MUST BE MALLOCED.  fdps_relocate_unit_array frees the block
 * data_fdps_map_unit_array_ptr names and publishes a fresh one, so the fixture
 * is a heap block and not a static array, and every read of a record goes
 * through the global after the call rather than through a pointer taken before
 * it.  The block is (count + 1) records long because the relocation's memmove
 * length is the length of the NEW block (unit.h).
 *
 * A ZEROED RECORD IS NOT AN EMPTY UNIT.  fdps_unit_item_count counts the
 * inventory entries whose flag byte does NOT carry 0x80 (unititem.h), so a
 * record full of zeroes reads as a unit carrying eight items and sends
 * fdps_map_actor_score_best_item into fdps_get_item_record against a table
 * pointer no test has filled.  Every staged record therefore has 0x80 in all
 * eight inventory flag bytes, which is what an empty bag looks like.
 *
 * Expected values come from the assembly at 00012960 -- CMP EAX,dword ptr
 * [0x00060150] at 00012976 and 00012a6a inside both loops for the re-read
 * bound, MOV dword ptr [0x00069cd0],0x0 at 0001298b for the cursor clear being
 * in the first sweep only, CALL 0x0002df90 at 00012995 for the relocation
 * being in the first sweep only, MOV dword ptr [0x00069d90],0xff at 000129a9
 * and 00012a8e for the per-iteration seed, CMP byte ptr [EAX+0x6],0x0 /
 * AND AL,0x81 / CMP byte ptr [EAX+0x26],0x0 for the three-part gate,
 * CMP dword ptr [0x00063f88],0x6 / JGE and CMP dword ptr [0x00063f8c],0x6 / JL
 * at 000129f6 for the score threshold, CALL dword ptr [EAX+0x6028c] at
 * 00012a48 and 00012aff for the post-action dispatch, and CMP dword ptr
 * [0x00069da0],0x0 at 00012a4e and 00012b05 for the abort -- and from the
 * record layout ticket 17 settled.  None of them is read off the emitted C.
 */

/* The chapter slot the staged post-action handler is installed in, and a
   second slot with a second handler, so "which slot ran" is an assertion and
   not an assumption. */
#define PHASE_CHAPTER_ID 3
#define PHASE_OTHER_CHAPTER_ID 7

#define SIDE_NPC 1
#define UNIT_RECORD_BYTES 0x50

/* Bit 7 of an inventory entry's flag byte: the entry is empty (unititem.h). */
#define INVENTORY_FLAG_EMPTY 0x80

/* Values planted before a run that no code path under test writes, so finding
   one still in place is evidence that the path did not run.  The two scorers
   zero their score global before anything else, which is what makes the score
   sentinel evidence that they were called. */
#define PHASE_SCORE_SENTINEL 0x5a
#define PHASE_CURSOR_SENTINEL 5
#define PHASE_PENDING_SENTINEL 0x2a

/* What the phase seeds the pending-event slot with, CMP dword ptr
   [0x00069d90],0xff. */
#define PHASE_NO_EVENT 0xff

/* The value the phase writes into the cursor draw mode in the first sweep. */
#define PHASE_CURSOR_HIDDEN 0

/* The score the gate wants before it spends a unit's turn, CMP 0x6 / JGE. */
#define PHASE_SCORE_THRESHOLD 6

/* The one behaviour fdps_map_actor_behavior_step returns from before its tail,
   and so the only one an eligible fixture may carry.  See phase_make_eligible
   below. */
#define PHASE_IDLE_BEHAVIOR 0x08

#define PHASE_LOG_MAX 10

static int post_calls;
static int other_post_calls;
static unsigned char *post_array_ptr[PHASE_LOG_MAX];
static int post_cursor_mode[PHASE_LOG_MAX];
static int post_unit_count[PHASE_LOG_MAX];

/* Which post-action call, counting from 1, each of these fires on; 0 means
   never.  They are how a case reaches into the middle of a sweep. */
static int phase_end_on_call;
static int phase_grow_on_call;
static int phase_set_pending_on_call;
static int phase_switch_chapter_on_call;
/* 1 puts the cursor sentinel back after each call, so the next sweep's value
   is its own and not the first sweep's leftover. */
static int phase_restore_cursor;

static struct fdps_unit_record *phase_unit(int index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + index * UNIT_RECORD_BYTES);
}

/* An empty bag and no spells, which is what keeps both scorers on their early
   return.  Side 2 is the player's, so a freshly staged record is ineligible
   until a case says otherwise. */
static void phase_blank_record(int index)
{
    struct fdps_unit_record *unit;
    unsigned char *bytes;
    int slot;

    unit = phase_unit(index);
    bytes = (unsigned char *) unit;
    for (slot = 0; slot < UNIT_RECORD_BYTES; slot++) {
        bytes[slot] = 0;
    }
    for (slot = 0; slot < 8; slot++) {
        unit->inventory_slots[slot * 2] = INVENTORY_FLAG_EMPTY;
    }
    unit->side = SIDE_PLAYER;
}

/* Side 0, nothing set in the status byte and no paralysis: the three tests the
   gate applies, all passed.  Behaviour 8 with it, and that is not decoration:
   the SECOND sweep hands every eligible unit to fdps_map_actor_behavior_step
   with no score gate in front of it, and 8 is the one arm that returns before
   the tail (mapai.h).  Any other behaviour would reach fdps_render_view_frame
   and hold the run there for ever. */
static void phase_make_eligible(int index)
{
    struct fdps_unit_record *unit;

    unit = phase_unit(index);
    unit->side = SIDE_ENEMY;
    unit->flags = 0;
    unit->status_timers[4] = 0;
    unit->ai_behavior = PHASE_IDLE_BEHAVIOR;
}

/* A block one record longer than the count, because that is the length the
   relocation's memmove reads and writes. */
static void phase_alloc(int unit_count)
{
    int index;

    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) ((unit_count + 1) * UNIT_RECORD_BYTES));
    data_fdps_map_unit_count = unit_count;
    for (index = 0; index < unit_count + 1; index++) {
        phase_blank_record(index);
    }
}

/* Stands in for an event handler that deploys another enemy: the array grows
   by one record and the count follows it, the way fdps_deploy_unit leaves
   them.  The block in hand is (count + 1) records long, which is exactly what
   the copy below reads. */
static void phase_grow_array(void)
{
    unsigned char *old_array;
    unsigned char *new_array;
    int old_count;
    int copied;

    old_count = data_fdps_map_unit_count;
    old_array = data_fdps_map_unit_array_ptr;
    new_array = (unsigned char *)
        malloc((size_t) ((old_count + 2) * UNIT_RECORD_BYTES));
    for (copied = 0; copied < (old_count + 1) * UNIT_RECORD_BYTES; copied++) {
        new_array[copied] = old_array[copied];
    }
    free(old_array);
    data_fdps_map_unit_array_ptr = new_array;
    data_fdps_map_unit_count = old_count + 1;
    phase_blank_record(old_count);
    phase_blank_record(old_count + 1);
}

static void phase_post_action(void)
{
    if (post_calls < PHASE_LOG_MAX) {
        post_array_ptr[post_calls] = data_fdps_map_unit_array_ptr;
        post_cursor_mode[post_calls] = data_fdps_map_cursor_draw_mode;
        post_unit_count[post_calls] = data_fdps_map_unit_count;
    }
    post_calls++;

    if (phase_restore_cursor != 0) {
        data_fdps_map_cursor_draw_mode = PHASE_CURSOR_SENTINEL;
    }
    if (phase_set_pending_on_call == post_calls) {
        data_fdps_chapter_pending_event_idx = 1;
    }
    if (phase_grow_on_call == post_calls) {
        phase_grow_array();
    }
    if (phase_end_on_call == post_calls) {
        data_fdps_chapter_event_or_battle_end_code = 3;
    }
}

static void phase_post_action_other(void)
{
    other_post_calls++;
    if (phase_switch_chapter_on_call == other_post_calls) {
        data_fdps_chapter_current_chapter_id = PHASE_CHAPTER_ID;
    }
}

static void phase_stage(int unit_count)
{
    int index;

    post_calls = 0;
    other_post_calls = 0;
    for (index = 0; index < PHASE_LOG_MAX; index++) {
        post_array_ptr[index] = NULL;
        post_cursor_mode[index] = -1;
        post_unit_count[index] = -1;
    }
    phase_end_on_call = 0;
    phase_grow_on_call = 0;
    phase_set_pending_on_call = 0;
    phase_switch_chapter_on_call = 0;
    phase_restore_cursor = 0;

    handler_calls = 0;
    for (index = 0; index < EVENT_LOG_MAX; index++) {
        handler_slots[index] = -1;
        handler_args[index] = -1;
    }
    data_fdps_chapter_event_handler_table[0] = handler_in_slot_0;
    data_fdps_chapter_event_handler_table[1] = handler_in_slot_1;

    phase_alloc(unit_count);
    data_fdps_chapter_current_chapter_id = PHASE_CHAPTER_ID;
    data_fdps_chapter_post_action_handler_table[PHASE_CHAPTER_ID] =
        phase_post_action;
    data_fdps_chapter_post_action_handler_table[PHASE_OTHER_CHAPTER_ID] =
        phase_post_action_other;
    data_fdps_chapter_event_or_battle_end_code = 0;
    data_fdps_chapter_pending_event_idx = PHASE_PENDING_SENTINEL;
    data_fdps_map_cursor_draw_mode = PHASE_CURSOR_SENTINEL;
    data_fdps_battle_ai_best_spell_score = PHASE_SCORE_SENTINEL;
    data_fdps_battle_ai_best_item_score = PHASE_SCORE_SENTINEL;
}

static void phase_unstage(void)
{
    free(data_fdps_map_unit_array_ptr);
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_chapter_post_action_handler_table[PHASE_CHAPTER_ID] = NULL;
    data_fdps_chapter_post_action_handler_table[PHASE_OTHER_CHAPTER_ID] = NULL;
    data_fdps_chapter_event_handler_table[0] = NULL;
    data_fdps_chapter_event_handler_table[1] = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_chapter_pending_event_idx = 0;
    data_fdps_chapter_event_or_battle_end_code = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_battle_ai_best_spell_score = 0;
    data_fdps_battle_ai_best_item_score = 0;
}

/* The three record offsets the phase reads by hand -- the side byte at +6, the
   status byte at +5 and the paralysis counter at +0x26 -- against the struct
   the emitted C goes through.  If any of them disagreed the gate would be
   reading a different field from the one the assembly reads. */
static void phase_reads_the_fields_the_assembly_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers) + 4, 0x26);
}

/* An empty battle: both loop tests fail on their first evaluation, so nothing
   at all happens.  Every write the body would make is in the body -- the
   cursor clear included -- and the sentinels are all still standing. */
static void phase_with_no_units_does_nothing(void)
{
    unsigned char *base;

    phase_stage(0);
    base = data_fdps_map_unit_array_ptr;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 0);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, PHASE_CURSOR_SENTINEL);
    CHECK_EQ((int) data_fdps_chapter_pending_event_idx,
             PHASE_PENDING_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(data_fdps_map_unit_array_ptr == base, 1);
    phase_unstage();
}

/* Two sweeps, not one: three units give six post-action calls.  None of the
   three is eligible, which is what shows the dispatch is reached whether or
   not the unit acted. */
static void phase_sweeps_every_unit_twice(void)
{
    phase_stage(3);

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 6);
    CHECK_EQ(handler_calls, 0);
    phase_unstage();
}

/* MOV dword ptr [0x00069cd0],0x0 is in the FIRST loop body only.  The handler
   puts the sentinel back after each call, so the second sweep's value is its
   own and not the first sweep's leftover: it comes back as the sentinel, which
   it could only do if the second sweep never wrote the global. */
static void phase_hides_the_cursor_in_the_first_sweep_only(void)
{
    phase_stage(1);
    phase_restore_cursor = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(post_cursor_mode[0], PHASE_CURSOR_HIDDEN);
    CHECK_EQ(post_cursor_mode[1], PHASE_CURSOR_SENTINEL);
    phase_unstage();
}

/* CALL 0x0002df90 is in the FIRST loop body only, and it publishes a new block
   every time it runs.  So the pointer the first sweep's handler sees is not the
   one the fixture staged, and the pointer the second sweep's handler sees is
   the same one again.  The record's contents have to survive the move: the
   relocation copies them into the new block. */
static void phase_relocates_the_array_in_the_first_sweep_only(void)
{
    unsigned char *base;

    phase_stage(1);
    phase_unit(0)->char_id = 0x33;
    base = data_fdps_map_unit_array_ptr;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(post_array_ptr[0] != base, 1);
    CHECK_EQ(post_array_ptr[1] == post_array_ptr[0], 1);
    CHECK_EQ(data_fdps_map_unit_array_ptr == post_array_ptr[1], 1);
    CHECK_EQ((int) phase_unit(0)->char_id, 0x33);
    phase_unstage();
}

/* MOV EAX,[0x00069cf4] sits inside the loop body, so the chapter id is read
   fresh for every dispatch.  The run starts on the other chapter's slot; that
   handler switches the id after its first call, and the second sweep's
   dispatch lands on the first slot instead. */
static void phase_dispatches_on_the_current_chapter_id(void)
{
    phase_stage(1);
    data_fdps_chapter_current_chapter_id = PHASE_OTHER_CHAPTER_ID;
    phase_switch_chapter_on_call = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(other_post_calls, 1);
    CHECK_EQ(post_calls, 1);
    phase_unstage();
}

/* MOV dword ptr [0x00069d90],0xff is inside both loop bodies, once per unit.
   The handler writes slot 1 into the pending slot after the first call; if the
   seed were hoisted out of the loop the next iteration would read that 1 back
   and call handler_in_slot_1, so handler_calls staying at 0 is the assertion.
   The slot is left holding the seed when the phase returns. */
static void phase_seeds_the_pending_slot_every_iteration(void)
{
    phase_stage(2);
    phase_set_pending_on_call = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 4);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ((int) data_fdps_chapter_pending_event_idx, PHASE_NO_EVENT);
    phase_unstage();
}

/* An eligible enemy gets both scorers run over it, and each of them zeroes its
   own score global before it does anything else.  So both sentinels are gone
   afterwards -- that is what says the two calls happened -- and both come back
   0, which is below the threshold, so the gate refuses the unit. */
static void phase_scores_an_eligible_enemy(void)
{
    phase_stage(1);
    phase_make_eligible(0);

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_spell_score < PHASE_SCORE_THRESHOLD, 1);
    CHECK_EQ(post_calls, 2);
    phase_unstage();
}

/* CMP byte ptr [EAX+0x6],0x0 / JNZ: the side has to be exactly 0.  Neither the
   NPC side nor the player's is swept by this phase, and the untouched score
   sentinels are what say the scorers were never reached. */
static void phase_skips_a_unit_on_another_side(void)
{
    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->side = SIDE_NPC;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(post_calls, 2);
    phase_unstage();

    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->side = SIDE_PLAYER;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    phase_unstage();
}

/* AND AL,0x81 tests two bits at once and no others.  Bit 0 is retired and bit
   7 is already-acted; either one alone turns the unit away, and the six bits
   between them do not. */
static void phase_skips_a_retired_or_already_acted_unit(void)
{
    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->flags = 0x01;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    phase_unstage();

    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->flags = 0x80;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    phase_unstage();

    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->flags = 0x7e;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    phase_unstage();
}

/* CMP byte ptr [EAX+0x26],0x0 reads status_timers[4], the paralysis counter,
   and neither of the bytes beside it.  A paralysed enemy is passed over; one
   whose other timers are running is not. */
static void phase_skips_a_paralysed_unit(void)
{
    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->status_timers[4] = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(post_calls, 2);
    phase_unstage();

    phase_stage(1);
    phase_make_eligible(0);
    phase_unit(0)->status_timers[3] = 9;
    phase_unit(0)->status_timers[5] = 9;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, 0);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, 0);
    phase_unstage();
}

/* CMP dword ptr [0x00069da0],0x0 / JNZ leaves the function at once, from
   whichever sweep is running.  Three units make the first sweep three calls
   long, so an abort on call 1 stops everything and an abort on call 4 -- the
   second sweep's first unit -- stops the rest of that sweep. */
static void phase_stops_on_a_battle_end_code(void)
{
    phase_stage(3);
    phase_end_on_call = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 1);
    phase_unstage();

    phase_stage(3);
    phase_end_on_call = 4;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 4);
    phase_unstage();
}

/* CMP EAX,dword ptr [0x00060150] is evaluated inside both loops, so an enemy
   deployed part-way through is reached by the sweep that is running.  The
   handler grows the array and the count after the first unit of the first
   sweep; the bound has to pick that up, giving two units in each sweep and
   four calls.  A count hoisted into a local would give three. */
static void phase_rereads_the_unit_count_every_iteration(void)
{
    phase_stage(1);
    phase_grow_on_call = 1;

    fdps_battle_enemy_turn_phase();

    CHECK_EQ(post_calls, 4);
    CHECK_EQ(post_unit_count[0], 1);
    CHECK_EQ(post_unit_count[1], 2);
    CHECK_EQ(post_unit_count[3], 2);
    phase_unstage();
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

    RUN_TEST(phase_reads_the_fields_the_assembly_reads);
    RUN_TEST(phase_with_no_units_does_nothing);
    RUN_TEST(phase_sweeps_every_unit_twice);
    RUN_TEST(phase_hides_the_cursor_in_the_first_sweep_only);
    RUN_TEST(phase_relocates_the_array_in_the_first_sweep_only);
    RUN_TEST(phase_dispatches_on_the_current_chapter_id);
    RUN_TEST(phase_seeds_the_pending_slot_every_iteration);
    RUN_TEST(phase_scores_an_eligible_enemy);
    RUN_TEST(phase_skips_a_unit_on_another_side);
    RUN_TEST(phase_skips_a_retired_or_already_acted_unit);
    RUN_TEST(phase_skips_a_paralysed_unit);
    RUN_TEST(phase_stops_on_a_battle_end_code);
    RUN_TEST(phase_rereads_the_unit_count_every_iteration);

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
