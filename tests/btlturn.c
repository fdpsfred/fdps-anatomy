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
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "chapter.h"
#include "keybd.h"
#include "mapdraw.h"
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

/* fdps_battle_npc_turn_phase at 00012b20, from here down.
 *
 * The same fixture the enemy-phase cases use, and the same wall in front of
 * it: fdps_map_actor_behavior_step cannot be observed from a test process
 * (see the note above).  Here that wall is larger, because this phase has no
 * score gate and no scorers -- the behaviour step is the ONLY thing an
 * eligible NPC gets, and behaviour 8, the one arm that returns before the
 * frame-clock tail, writes nothing at all.  So an eligible NPC and an
 * ineligible one leave identical state behind, and no case below can pin the
 * eligibility gate on its positive side; the verdict records that as an open
 * issue for a real machine.
 *
 * What IS reachable is everything around the gate, and the differences from
 * the enemy phase are exactly there: ONE sweep instead of two, a cursor clear
 * BEFORE the sweep as well as inside it, and no call to either scorer -- the
 * score sentinels survive even a run whose unit is eligible, which is the
 * assertion that a copy of the enemy phase would fail.
 *
 * Expected values come from the assembly at 00012b20 -- MOV dword ptr
 * [0x00069cd0],0x0 at 00012b2c ahead of the index being zeroed at 00012b36
 * for the pre-loop clear and again at 00012b55 for the per-unit one, CMP
 * EAX,dword ptr [0x00060150] / JL at 00012b40 inside the loop for the re-read
 * bound, CALL 0x0002df90 at 00012b5f for the relocation, MOV dword ptr
 * [0x00069d90],0xff at 00012b73 for the per-iteration seed, CMP EAX,0x1 at
 * 00012b88 / AND AL,0x81 at 00012b93 / CMP byte ptr [EAX+0x26],0x0 at 00012ba3
 * for the three-part gate, PUSH 0x1 at 00012bab for the side literal, CALL
 * dword ptr [EAX+0x6028c] at 00012beb for the post-action dispatch, CMP dword
 * ptr [0x00069da0],0x0 / JZ 0x00012b4d at 00012bf1 for the abort and for the
 * loop being the only backward branch in the body -- and from the record
 * layout ticket 17 settled.  None of them is read off the emitted C.
 */

/* Side 1, nothing set in the status byte and no paralysis: the three tests
   this phase applies, all passed.  Behaviour 8 with them, for the same reason
   phase_make_eligible carries it -- the NPC phase hands every eligible unit to
   fdps_map_actor_behavior_step with no gate in front of it, and 8 is the one
   arm that returns before fdps_render_view_frame. */
static void npc_make_eligible(int index)
{
    struct fdps_unit_record *unit;

    unit = phase_unit(index);
    unit->side = SIDE_NPC;
    unit->flags = 0;
    unit->status_timers[4] = 0;
    unit->ai_behavior = PHASE_IDLE_BEHAVIOR;
}

/* MOV dword ptr [0x00069cd0],0x0 at 00012b2c is ahead of the loop, so it runs
   even when the loop body never does.  With no units that clear is the whole
   function: the cursor sentinel is gone, and every sentinel the body would
   have touched is still standing.  The enemy phase, whose clear is inside its
   first loop, leaves the cursor sentinel in place in this same case. */
static void npc_phase_hides_the_cursor_before_the_sweep(void)
{
    unsigned char *base;

    phase_stage(0);
    base = data_fdps_map_unit_array_ptr;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(data_fdps_map_cursor_draw_mode, PHASE_CURSOR_HIDDEN);
    CHECK_EQ(post_calls, 0);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ((int) data_fdps_chapter_pending_event_idx,
             PHASE_PENDING_SENTINEL);
    CHECK_EQ(data_fdps_map_unit_array_ptr == base, 1);
    phase_unstage();
}

/* One sweep, not two: three units give three post-action calls, where the
   enemy phase's two sweeps over the same fixture give six.  None of the three
   is eligible, which is what shows the dispatch is reached whether or not the
   unit acted. */
static void npc_phase_sweeps_every_unit_once(void)
{
    phase_stage(3);

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 3);
    CHECK_EQ(handler_calls, 0);
    phase_unstage();
}

/* MOV dword ptr [0x00069cd0],0x0 at 00012b55 is inside the loop body too, so
   every unit gets the overlay put away before it acts.  The handler puts the
   sentinel back after each call; both iterations still report the hidden
   value, which they could only do if the clear runs once per unit. */
static void npc_phase_hides_the_cursor_every_iteration(void)
{
    phase_stage(2);
    phase_restore_cursor = 1;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(post_cursor_mode[0], PHASE_CURSOR_HIDDEN);
    CHECK_EQ(post_cursor_mode[1], PHASE_CURSOR_HIDDEN);
    phase_unstage();
}

/* CALL 0x0002df90 at 00012b5f is inside the loop body, and it publishes a new
   block every time it runs.  So each iteration's handler sees a different
   pointer from the one before it, and the records' contents have to survive
   every move. */
static void npc_phase_relocates_the_array_every_iteration(void)
{
    unsigned char *base;

    phase_stage(2);
    phase_unit(0)->char_id = 0x33;
    phase_unit(1)->char_id = 0x44;
    base = data_fdps_map_unit_array_ptr;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(post_array_ptr[0] != base, 1);
    CHECK_EQ(post_array_ptr[1] != post_array_ptr[0], 1);
    CHECK_EQ(data_fdps_map_unit_array_ptr == post_array_ptr[1], 1);
    CHECK_EQ((int) phase_unit(0)->char_id, 0x33);
    CHECK_EQ((int) phase_unit(1)->char_id, 0x44);
    phase_unstage();
}

/* MOV dword ptr [0x00069d90],0xff at 00012b73 is inside the loop body, once
   per unit.  The handler writes slot 1 into the pending slot after the first
   call; if the seed were hoisted out of the loop the next iteration would read
   that 1 back and call handler_in_slot_1, so handler_calls staying at 0 is the
   assertion.  The slot is left holding the seed when the phase returns. */
static void npc_phase_seeds_the_pending_slot_every_iteration(void)
{
    phase_stage(2);
    phase_set_pending_on_call = 1;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ((int) data_fdps_chapter_pending_event_idx, PHASE_NO_EVENT);
    phase_unstage();
}

/* MOV EAX,[0x00069cf4] at 00012bdf sits inside the loop body, so the chapter
   id is read fresh for every dispatch.  The run starts on the other chapter's
   slot; that handler switches the id after its first call, and the second
   unit's dispatch lands on the first slot instead. */
static void npc_phase_dispatches_on_the_current_chapter_id(void)
{
    phase_stage(2);
    data_fdps_chapter_current_chapter_id = PHASE_OTHER_CHAPTER_ID;
    phase_switch_chapter_on_call = 1;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(other_post_calls, 1);
    CHECK_EQ(post_calls, 1);
    phase_unstage();
}

/* There is no CALL to either scorer anywhere in this body, so neither score
   global is written -- not even for a unit that passes the eligibility gate
   and is handed to the behaviour step.  Both scorers zero their own score
   first thing, so a surviving sentinel is what says they never ran.  This is
   the case that separates the NPC phase from a copy of the enemy phase. */
static void npc_phase_calls_no_scorer(void)
{
    phase_stage(1);
    npc_make_eligible(0);

    fdps_battle_npc_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(post_calls, 1);
    phase_unstage();

    /* And the same for a unit the gate turns away, so the sentinel above is
       not standing merely because the unit was skipped. */
    phase_stage(1);
    npc_make_eligible(0);
    phase_unit(0)->flags = 0x80;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(data_fdps_battle_ai_best_spell_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(data_fdps_battle_ai_best_item_score, PHASE_SCORE_SENTINEL);
    CHECK_EQ(post_calls, 1);
    phase_unstage();
}

/* CMP dword ptr [0x00069da0],0x0 / JZ 0x00012b4d at 00012bf1: a non-zero code
   falls out of the loop and off the end of the function.  Three units make the
   sweep three calls long, so an abort on call 1 stops everything and an abort
   on call 2 stops the third unit only. */
static void npc_phase_stops_on_a_battle_end_code(void)
{
    phase_stage(3);
    phase_end_on_call = 1;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 1);
    phase_unstage();

    phase_stage(3);
    phase_end_on_call = 2;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 2);
    phase_unstage();
}

/* CMP EAX,dword ptr [0x00060150] at 00012b40 is evaluated inside the loop, so
   an NPC deployed part-way through is reached by the sweep that is running.
   The handler grows the array and the count after the first unit; the bound
   has to pick that up, giving two calls where a count hoisted into a local
   would give one. */
static void npc_phase_rereads_the_unit_count_every_iteration(void)
{
    phase_stage(1);
    phase_grow_on_call = 1;

    fdps_battle_npc_turn_phase();

    CHECK_EQ(post_calls, 2);
    CHECK_EQ(post_unit_count[0], 1);
    CHECK_EQ(post_unit_count[1], 2);
    phase_unstage();
}

/* ---------------------------------------------------------------------- *
 * 00015470 fdps_battle_unit_turn
 *
 * WHICH OF THE TURN'S ARMS A TEST CAN REACH.  Every pass of the turn loop
 * goes through fdps_map_cursor_select_loop before anything else is decided,
 * and that loop only returns on a key: it throws the scancode latch away on
 * entry (0002b5c8) and then reads it once a pass, drawing a whole frame with
 * fdps_render_view_frame between reads (mapcur.h).  So the cases below put
 * the adapter in mode 13h, install a timer interrupt that advances the game's
 * clock -- which is what lets the frame's pacing spin finish -- and writes ESC
 * into the latch on every tick, the way fdps_keyboard_isr would on a
 * keypress.  The select loop reads it on its next pass and answers -1.
 *
 * That is the one arm reachable from here: backing out of the range cursor.
 * Everything behind a confirm -- the path trace, the walk, the action menu
 * and the take-back -- needs the select loop to answer 1, which in mode 4
 * takes a SPACE with the cursor on a marked tile, and then fdps_battle_action_menu
 * running to an answer as well, with its sheets, its text block and a second
 * key script (tests/btlact.c).  Those arms are settled in the assembly, row by
 * row, in the comment above the function, and are a playtest observation.
 *
 * WHAT THE FIXTURE STAGES, AND WHY SO LITTLE OF THE SCENE.  The range flood
 * and the tile lookups it makes read the slot-0 tile map, the slot-0 attribute
 * table, the movement grid, the cell event layer and the PROMAP.DAT row, so a
 * 4x4 map of each is staged.  Every terrain costs 20 against a move of 2, so
 * the flood marks the start cell and nothing else.  The scene layer count is
 * 0, the cursor overlay mode 0 and the terrain panel off, so a frame draws
 * nothing but still blits and paces -- and, because the marked-tile pulse only
 * steps inside a drawn layer, the phase this function writes is still readable
 * afterwards.  The unit count is 1 and record 0 is RETIRED, which every unit
 * sweep on the way passes over (the zone marks, the tile blocking and the unit
 * draw); the acting unit is record 1.  The function resolves unit_index by
 * address and never reads the count, and fdps_relocate_unit_array carries
 * count + 1 records, so record 1 survives the move.  Index 1 rather than 0 is
 * so that the argument the exit hands an event handler is told apart from a
 * literal 0.
 *
 * Expected values come from the assembly at 00015470 -- MOV dword ptr
 * [0x0006015c],0x14 at 000154cc, CALL 0x0002df90 at 000154e0 inside the loop,
 * MOV dword ptr [0x00069d90],0xff at 000154f4, the two IDIV EBX by 0x18 of
 * 00069cd4 and 00069ccc at 0001551d / 00015535 feeding the start tile, CALL
 * 0x00010b20 at 0001557f right after the select loop, the cancel arm at
 * 0001558a (IMUL by 0x18 of the start tile, CALL 0x0002d7c0, MOV byte ptr
 * [EBP-0x4],0x1 and nothing else), and the exit at 00015806 (MOV dword ptr
 * [0x00069cd0],0x1, then the pending-event call with PUSH EAX of the unit
 * index, then the post-action call) -- and from the record layout ticket 17
 * settled.  None of them is read off the emitted C.
 */

#define TURN_MAP_W 4
#define TURN_MAP_H 4
#define TURN_MAP_CELLS (TURN_MAP_W * TURN_MAP_H)
#define TURN_TILE_PIXELS 24
#define TURN_ATTR_HEADER 0x11
#define TURN_CLASS_ROW_BYTES 10
#define TURN_IMPASSABLE_COST 20

/* The acting unit: record 1, standing on tile (1, 2) with a move of 2.
   Record 0 is a retired filler; see the block comment above. */
#define TURN_UNIT_INDEX 1
#define TURN_UNIT_COUNT 1
#define TURN_UNIT_FLAG_RETIRED 0x01
#define TURN_UNIT_TILE_X 1
#define TURN_UNIT_TILE_Y 2
#define TURN_UNIT_MOVE 2
#define TURN_UNIT_CHAR_ID 0x33

/* What the function writes: the pulse phase at 000154cc, the "no event" seed
   at 000154f4 and the overlay mode at 00015806. */
#define TURN_BLEND_PHASE_START 0x14
#define TURN_NO_EVENT 0xff
#define TURN_CURSOR_NORMAL 1

/* Values nothing on the cancel path writes, planted so that finding one still
   in place is evidence. */
#define TURN_PLAY_FLAG_SENTINEL 0x5a
#define TURN_PENDING_SENTINEL 0x2a
#define TURN_BLEND_SENTINEL 3
#define TURN_GRID_MARKER_SENTINEL 0x33

#define TURN_CHAPTER_ID 3
#define TURN_MODE_13H 0x13
#define TURN_MODE_TEXT 0x03
#define TURN_TIMER_VECTOR 8
#define TURN_SCANCODE_ESC 0x01
#define TURN_SCANCODE_NONE 0xff

static unsigned char turn_tile_map[0x0b + TURN_MAP_CELLS * 2];
static unsigned char turn_move_grid[4 + TURN_MAP_CELLS * 2];
static unsigned char turn_tile_attr[TURN_ATTR_HEADER + TURN_MAP_CELLS * 4];
static unsigned char turn_event_layer[sizeof(struct fdps_map_cell_code_layer)
                                      + TURN_MAP_CELLS];
static unsigned char turn_class_table[2 * TURN_CLASS_ROW_BYTES];

static unsigned char *turn_latch;
/* -1 leaves the pending-event slot alone; anything else is written into it on
   every tick, which is how a case reaches the exit's event dispatch. */
static int turn_isr_pending_slot;
static void (__interrupt __far *turn_saved_timer)();

static int turn_post_calls;
static int turn_post_cursor_mode;
static int turn_post_after_handler;
static int turn_saved_music_index;

static void __interrupt __far turn_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    *turn_latch = (unsigned char) TURN_SCANCODE_ESC;
    if (turn_isr_pending_slot >= 0) {
        data_fdps_chapter_pending_event_idx =
            (unsigned int) turn_isr_pending_slot;
    }
    _chain_intr(turn_saved_timer);
}

static void turn_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void turn_post_action(void)
{
    turn_post_calls++;
    turn_post_cursor_mode = data_fdps_map_cursor_draw_mode;
    turn_post_after_handler = handler_calls;
}

static struct fdps_unit_record *turn_unit(void)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + TURN_UNIT_INDEX * 0x50);
}

static void turn_stage(void)
{
    struct fdps_map_cell_code_layer *event_layer;
    struct fdps_unit_record *unit;
    unsigned char *bytes;
    int i;

    *(short *) (turn_tile_map + 7) = TURN_MAP_W;
    *(short *) (turn_tile_map + 9) = TURN_MAP_H;
    *(short *) turn_move_grid = TURN_MAP_W;
    *(short *) (turn_move_grid + 2) = TURN_MAP_H;
    for (i = 0; i < TURN_MAP_CELLS; i++) {
        *(short *) (turn_tile_map + 0x0b + i * 2) = (short) i;
        turn_move_grid[4 + i * 2] = 0;
        turn_move_grid[4 + i * 2 + 1] = TURN_GRID_MARKER_SENTINEL;
    }
    memset(turn_tile_attr, 0, sizeof(turn_tile_attr));
    memset(turn_event_layer, 0, sizeof(turn_event_layer));
    event_layer = (struct fdps_map_cell_code_layer *) turn_event_layer;
    event_layer->width = TURN_MAP_W;
    event_layer->height = TURN_MAP_H;
    memset(turn_class_table, TURN_IMPASSABLE_COST, sizeof(turn_class_table));

    data_fdps_scene_layer_tile_map_ptrs[0] = turn_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = turn_tile_attr;
    data_fdps_scene_layer_count = 0;
    data_fdps_battle_move_grid_ptr = turn_move_grid;
    data_fdps_map_cell_event_code_layer_ptr = turn_event_layer;
    data_fdps_class_table_ptr = turn_class_table;

    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) ((TURN_UNIT_COUNT + 1) * 0x50));
    data_fdps_map_unit_count = TURN_UNIT_COUNT;
    bytes = data_fdps_map_unit_array_ptr;
    for (i = 0; i < (TURN_UNIT_COUNT + 1) * 0x50; i++) {
        bytes[i] = 0;
    }
    ((struct fdps_unit_record *) bytes)->flags = TURN_UNIT_FLAG_RETIRED;
    ((struct fdps_unit_record *) bytes)->side = SIDE_ENEMY;
    unit = turn_unit();
    for (i = 0; i < 8; i++) {
        unit->inventory_slots[i * 2] = 0x80;
    }
    unit->pos_x = TURN_UNIT_TILE_X;
    unit->pos_y = TURN_UNIT_TILE_Y;
    unit->side = SIDE_PLAYER;
    unit->char_id = TURN_UNIT_CHAR_ID;
    unit->move = TURN_UNIT_MOVE;
    unit->clazz = 0;

    data_fdps_map_cursor_world_x = TURN_UNIT_TILE_X * TURN_TILE_PIXELS;
    data_fdps_map_cursor_world_y = TURN_UNIT_TILE_Y * TURN_TILE_PIXELS;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = TURN_PLAY_FLAG_SENTINEL;
    data_fdps_marked_tile_blend_phase = TURN_BLEND_SENTINEL;
    data_fdps_chapter_pending_event_idx = TURN_PENDING_SENTINEL;
    turn_saved_music_index = data_fdps_audio_cd_current_music_index;
    data_fdps_audio_cd_current_music_index = -1;
    data_fdps_timer_tick_counter = 100;
    data_fdps_view_frame_last_tick = 100;

    turn_post_calls = 0;
    turn_post_cursor_mode = -1;
    turn_post_after_handler = -1;
    turn_isr_pending_slot = -1;
    handler_calls = 0;
    for (i = 0; i < EVENT_LOG_MAX; i++) {
        handler_slots[i] = -1;
        handler_args[i] = -1;
    }
    data_fdps_chapter_event_handler_table[1] = handler_in_slot_1;
    data_fdps_chapter_current_chapter_id = TURN_CHAPTER_ID;
    data_fdps_chapter_post_action_handler_table[TURN_CHAPTER_ID] =
        turn_post_action;
}

static void turn_run(void)
{
    turn_latch = fdps_keyboard_scancode_ptr();
    turn_set_mode(TURN_MODE_13H);
    turn_saved_timer = _dos_getvect(TURN_TIMER_VECTOR);
    _dos_setvect(TURN_TIMER_VECTOR, turn_timer_isr);
    fdps_battle_unit_turn(TURN_UNIT_INDEX);
    _dos_setvect(TURN_TIMER_VECTOR, turn_saved_timer);
    turn_set_mode(TURN_MODE_TEXT);
    *turn_latch = (unsigned char) TURN_SCANCODE_NONE;
}

static void turn_unstage(void)
{
    free(data_fdps_map_unit_array_ptr);
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_tile_map_ptrs[0] = NULL;
    data_fdps_scene_layer_tile_attr_ptr[0] = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_class_table_ptr = NULL;
    data_fdps_chapter_event_handler_table[1] = NULL;
    data_fdps_chapter_post_action_handler_table[TURN_CHAPTER_ID] = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_chapter_pending_event_idx = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_marked_tile_blend_phase = 0;
    data_fdps_audio_cd_current_music_index = turn_saved_music_index;
}

/* The five record bytes the function reads by displacement: [EDX+0x3b] for
   the move at 000154ac, [EDX+0x20] for the class code at 000154c6, [EAX+0x8]
   for the character at 00015649, [EDX] and [EDX+0x1] for the tile written back
   at 000156cf / 000156d7 -- and the 0x50 stride of IMUL EDX,...,0x50 at
   000156bb. */
static void turn_reads_the_fields_the_assembly_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, move), 0x3b);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 0x08);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
}

/* Backing out of the range cursor ends the call after one pass, with the turn
   NOT spent: the arm at 0001558a sets only the loop flag, so bit 7 of the
   status byte stays clear and the unit is left where it stood.  The exit
   still runs whole: the overlay goes back to mode 1 -- the post-action handler
   already sees it -- the pending slot is left at the 0xff the pass seeded it
   with, so no event fires, and the chapter's post-action handler runs exactly
   once.  The play-active flag is written only after a confirm, so its
   sentinel survives. */
static void turn_cancel_ends_the_call_with_nothing_spent(void)
{
    turn_stage();

    turn_run();

    CHECK_EQ(turn_post_calls, 1);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ(turn_post_cursor_mode, TURN_CURSOR_NORMAL);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, TURN_CURSOR_NORMAL);
    CHECK_EQ((int) data_fdps_chapter_pending_event_idx, TURN_NO_EVENT);
    CHECK_EQ((int) turn_unit()->flags, 0);
    CHECK_EQ((int) turn_unit()->pos_x, TURN_UNIT_TILE_X);
    CHECK_EQ((int) turn_unit()->pos_y, TURN_UNIT_TILE_Y);
    CHECK_EQ((int) data_fdps_ui_play_active_flag, TURN_PLAY_FLAG_SENTINEL);
    turn_unstage();
}

/* MOV dword ptr [0x0006015c],0x14 at 000154cc: the pulse is restarted once
   before the loop.  With no scene layer drawn nothing steps it afterwards, so
   it comes back as exactly 0x14. */
static void turn_restarts_the_marked_tile_pulse(void)
{
    turn_stage();

    turn_run();

    CHECK_EQ(data_fdps_marked_tile_blend_phase, TURN_BLEND_PHASE_START);
    turn_unstage();
}

/* CALL 0x0002df90 at 000154e0 is inside the loop, so even a turn that is
   backed out of at once publishes a new block -- and the record has to come
   through the move, because the function re-resolves it afterwards. */
static void turn_relocates_the_array_each_pass(void)
{
    unsigned char *staged;

    turn_stage();
    staged = data_fdps_map_unit_array_ptr;

    turn_run();

    CHECK_EQ(data_fdps_map_unit_array_ptr != staged, 1);
    CHECK_EQ((int) turn_unit()->char_id, TURN_UNIT_CHAR_ID);
    CHECK_EQ((int) turn_unit()->move, TURN_UNIT_MOVE);
    turn_unstage();
}

/* The start tile is the CURSOR's pixel divided by 24 (the two IDIVs at
   0001552b and 00015543), not the record's pos_x/pos_y, and the cancel arm
   walks the cursor back to that tile times 24.  With the record placed
   somewhere else entirely, a body that took the record's tile would walk the
   cursor off to (72, 72); a body that swapped the two quotients would send it
   to (48, 24).  Taken from the cursor, the target is where the cursor already
   is and fdps_map_cursor_move_to returns without moving it. */
static void turn_takes_the_start_tile_from_the_cursor(void)
{
    turn_stage();
    turn_unit()->pos_x = 3;
    turn_unit()->pos_y = 3;

    turn_run();

    CHECK_EQ(data_fdps_map_cursor_world_x, TURN_UNIT_TILE_X * TURN_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_world_y, TURN_UNIT_TILE_Y * TURN_TILE_PIXELS);
    CHECK_EQ((int) turn_unit()->pos_x, 3);
    CHECK_EQ((int) turn_unit()->pos_y, 3);
    turn_unstage();
}

/* CALL 0x00010b20 at 0001557f follows the select loop on every pass: the
   flood marked the start cell 0 and nothing else (every terrain costs 20
   against a move of 2), and the reset then puts every marker in the grid to
   0xff -- including the cells planted with a sentinel the flood never
   reached. */
static void turn_resets_the_grid_after_the_cursor(void)
{
    int i;
    int all_reset;

    turn_stage();

    turn_run();

    all_reset = 1;
    for (i = 0; i < TURN_MAP_CELLS; i++) {
        if (turn_move_grid[4 + i * 2 + 1] != 0xff) {
            all_reset = 0;
        }
    }
    CHECK_EQ(all_reset, 1);
    turn_unstage();
}

/* The exit's event dispatch, CMP dword ptr [0x00069d90],0xff / PUSH EAX /
   CALL dword ptr [EDX+0x601c4] at 0001581c: a slot that is no longer 0xff
   when the loop ends fires its handler with the acting unit's index, and it
   fires BEFORE the post-action handler.  Nothing on the cancel arm itself
   writes the slot, so the timer interrupt writes slot 1 into it on every tick
   -- after the pass's own 0xff seed, since the frame the select loop draws
   waits for a tick. */
static void turn_exit_fires_a_pending_event_with_the_unit(void)
{
    turn_stage();
    turn_isr_pending_slot = 1;

    turn_run();

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 1);
    CHECK_EQ(handler_args[0], TURN_UNIT_INDEX);
    CHECK_EQ(turn_post_calls, 1);
    CHECK_EQ(turn_post_after_handler, 1);
    turn_unstage();
}

/* fdps_battle_advance_turn at 0001e3f0, from here down.
 *
 * Only the stretch up to the first battle-end test can be run here: past it
 * the function calls fdps_play_vfs_animation, which loads a clip out of the
 * animation pack, and fdps_cd_verify_disc_and_play_track, which holds the
 * game until the right disc answers.  So every case below ends the call at
 * one of the first two tests of data_fdps_chapter_event_or_battle_end_code --
 * either by planting a non-zero code before the call (the test at 0001e588
 * returns) or by having the chapter's post-action handler raise it on its
 * second call, which is the first unit of the NPC phase (the test at 0001e59a
 * returns).  The chapter's post-action handler is reached once before that,
 * from fdps_battle_tick_status_effects(1).
 *
 * That stretch holds the whole rest sweep, the present, the NPC side's turn
 * events and status ticks and, in the second shape, the NPC phase.  The page
 * composition runs for real: no scene layer, a cursor mode that draws
 * nothing, the terrain panel off, every unit's portrait 0x80 so the map pass
 * skips it and its tile row 9 so the flash -- sprite origin y 9 * 24 + 18 =
 * 234, past the 0xd8 limit (sprite.h) -- is dropped too.  No unit's HP is 0,
 * so the death pass in the status tick finds nobody.  The rest sound is looked
 * up in an empty pack and plays nothing.
 *
 * Expected values come from the assembly at 0001e3f0 -- CMP EAX,0x2 on +6 at
 * 0001e477, AND AL,0x81 on +5 at 0001e482, CMP byte ptr [EAX+0x25] / [EAX+0x26]
 * at 0001e492 / 0001e49d, CMP / JNZ of the two MOVSX'd HP words at 0001e4a8,
 * IDIV by 5 at 0001e4bd, JLE at 0001e4c8, the word store at 0001e4d6, MOV byte
 * ptr [0x00060159],0x0 at 0001e407, PUSH 0x1 into 0002e0c0 at 0001e574, the two
 * CMP dword ptr [0x00069da0],0x0 at 0001e588 / 0001e59a, and INC dword ptr
 * [0x00069ce8] at 0001e5fd only after the enemy phase -- and from the record
 * layout ticket 17 settled.  None of them is read off the emitted C.
 */

#define ADV_UNIT_COUNT 14
#define ADV_CHAPTER_ID 3
#define ADV_TURN 4
#define ADV_SIDE_ENEMY 0
#define ADV_SIDE_NPC 1
#define ADV_SIDE_PLAYER 2
#define ADV_NO_MAP_SPRITE 0x80
#define ADV_OFFSCREEN_ROW 9
#define ADV_FLAG_RETIRED 0x01
#define ADV_FLAG_ACTED 0x80
#define ADV_POISON_TIMER 3
#define ADV_PARALYSIS_TIMER 4

/* Values nothing on the reached stretch is meant to leave in place, planted so
   that finding one afterwards is evidence.  The cursor mode is outside the
   0..6 fdps_draw_map_cursor dispatches on, so it draws nothing either. */
#define ADV_PLAY_FLAG_SENTINEL 0x5a
#define ADV_CURSOR_MODE_SENTINEL 0x2a

#define ADV_MODE_13H 0x13
#define ADV_MODE_TEXT 0x03
#define ADV_TIMER_VECTOR 8

static void (__interrupt __far *adv_saved_timer)();
static unsigned char adv_empty_wav_bank[0x40];
static int adv_post_calls;
/* Which call of the post-action handler raises the battle-end code; 0 never
   does. */
static int adv_end_on_post_call;
static int adv_saved_music_index;

/* The timer has to run: fdps_render_view_frame spins until the tick counter
   moves. */
static void __interrupt __far adv_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(adv_saved_timer);
}

static void adv_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static void adv_post_action(void)
{
    adv_post_calls++;
    if (adv_post_calls == adv_end_on_post_call) {
        data_fdps_chapter_event_or_battle_end_code = 1;
    }
}

/* Read through the global every time: the NPC phase relocates the array. */
static struct fdps_unit_record *adv_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * 0x50);
}

static void adv_set_unit(int unit_index, int side, int flags, int hp_current,
                         int hp_max)
{
    struct fdps_unit_record *unit;

    unit = adv_unit(unit_index);
    unit->side = (unsigned char) side;
    unit->flags = (unsigned char) flags;
    unit->hp_current = (short) hp_current;
    unit->hp_max = (short) hp_max;
}

/* The fourteen units the rest sweep is run over.  Each row of the table in
   adv_rest_* below names what the unit tests. */
static void adv_stage(void)
{
    unsigned char *bytes;
    int i;

    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) ((ADV_UNIT_COUNT + 1) * 0x50));
    data_fdps_map_unit_count = ADV_UNIT_COUNT;
    bytes = data_fdps_map_unit_array_ptr;
    for (i = 0; i < (ADV_UNIT_COUNT + 1) * 0x50; i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < ADV_UNIT_COUNT; i++) {
        adv_unit(i)->portrait_id = ADV_NO_MAP_SPRITE;
        adv_unit(i)->pos_y = ADV_OFFSCREEN_ROW;
        adv_set_unit(i, ADV_SIDE_PLAYER, 0, 50, 100);
    }
    adv_set_unit(1, ADV_SIDE_PLAYER, 0, 95, 100);
    adv_set_unit(2, ADV_SIDE_PLAYER, 0, 120, 100);
    adv_set_unit(3, ADV_SIDE_PLAYER, 0, -10, 100);
    adv_set_unit(4, ADV_SIDE_PLAYER, 0, 3, 9);
    adv_set_unit(5, ADV_SIDE_ENEMY, 0, 50, 100);
    adv_set_unit(6, ADV_SIDE_PLAYER, ADV_FLAG_ACTED, 50, 100);
    adv_set_unit(7, ADV_SIDE_PLAYER, ADV_FLAG_RETIRED, 50, 100);
    adv_unit(8)->status_timers[ADV_POISON_TIMER] = 2;
    adv_unit(9)->status_timers[ADV_PARALYSIS_TIMER] = 2;
    adv_set_unit(10, ADV_SIDE_PLAYER, 0, 100, 100);
    adv_set_unit(11, ADV_SIDE_PLAYER, 0x02, 50, 100);
    adv_unit(12)->status_timers[0] = 3;
    adv_set_unit(13, ADV_SIDE_NPC, 0, 50, 100);

    data_fdps_scene_layer_count = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_map_cursor_draw_mode = ADV_CURSOR_MODE_SENTINEL;
    data_fdps_ui_play_active_flag = ADV_PLAY_FLAG_SENTINEL;
    memset(adv_empty_wav_bank, 0, sizeof(adv_empty_wav_bank));
    data_fdps_audio_basewav_sfx_bank_buf_ptr = adv_empty_wav_bank;
    adv_saved_music_index = data_fdps_audio_cd_current_music_index;
    data_fdps_timer_tick_counter = 100;
    data_fdps_view_frame_last_tick = 100;

    /* The turn-event table: filler everywhere, from events_stage, which also
       sets the counter to 4 and installs the logging handlers. */
    events_stage();
    data_fdps_battle_turn_counter = ADV_TURN;

    data_fdps_chapter_event_or_battle_end_code = 0;
    adv_post_calls = 0;
    adv_end_on_post_call = 0;
    data_fdps_chapter_current_chapter_id = ADV_CHAPTER_ID;
    data_fdps_chapter_post_action_handler_table[ADV_CHAPTER_ID] =
        adv_post_action;
}

static void adv_run(void)
{
    adv_set_mode(ADV_MODE_13H);
    adv_saved_timer = _dos_getvect(ADV_TIMER_VECTOR);
    _dos_setvect(ADV_TIMER_VECTOR, adv_timer_isr);
    fdps_battle_advance_turn();
    _dos_setvect(ADV_TIMER_VECTOR, adv_saved_timer);
    adv_set_mode(ADV_MODE_TEXT);
}

static void adv_unstage(void)
{
    events_unstage();
    free(data_fdps_map_unit_array_ptr);
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
    data_fdps_chapter_post_action_handler_table[ADV_CHAPTER_ID] = NULL;
    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_chapter_event_or_battle_end_code = 0;
    data_fdps_chapter_pending_event_idx = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    data_fdps_audio_cd_current_music_index = adv_saved_music_index;
}

/* The displacements the function reads: +5 and +6 for the status and side
   bytes, +0x0a for the inventory entries' flag bytes (ADD EAX,EAX / MOV
   AL,byte ptr [EAX+0xa]), +0x25 and +0x26 for the poison and paralysis
   counters, +0x40/+0x42 for HP and +0x44/+0x46 for MP. */
static void adv_reads_the_fields_the_assembly_reads(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers)
             + ADV_POISON_TIMER, 0x25);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers)
             + ADV_PARALYSIS_TIMER, 0x26);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_current), 0x44);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, mp_max), 0x46);
}

/* The rest.  A fifth of the maximum, IDIV by 5, added and clamped with JLE:
   50/100 gains 20; 95/100 is clamped at 100; 3/9 gains 9/5 = 1.  The HP test
   is CMP / JNZ, inequality -- a unit ABOVE its maximum rests too and comes
   back down to it (120 -> 100), and one at exactly its maximum is left alone.
   MOVSX makes the current HP signed: -10 gains 20 and lands on 10, where an
   unsigned read would have clamped it to 100. */
static void adv_rest_heals_a_fifth_of_the_maximum(void)
{
    adv_stage();
    data_fdps_chapter_event_or_battle_end_code = 1;

    adv_run();

    CHECK_EQ((int) adv_unit(0)->hp_current, 70);
    CHECK_EQ((int) adv_unit(1)->hp_current, 100);
    CHECK_EQ((int) adv_unit(2)->hp_current, 100);
    CHECK_EQ((int) adv_unit(3)->hp_current, 10);
    CHECK_EQ((int) adv_unit(4)->hp_current, 4);
    CHECK_EQ((int) adv_unit(10)->hp_current, 100);
    adv_unstage();
}

/* Who does not rest: another side (an enemy, 5, and an NPC, 13 -- the test is
   equality with 2), a unit with bit 7 or bit 0 of the status byte (6 and 7),
   a poisoned one (8) and a paralysed one (9).  The mask is 0x81 and not the
   whole byte, so bit 1 (11) rests; and the timers tested are +0x25 and +0x26
   only, so a unit with status_timers[0] running (12) rests. */
static void adv_rest_skips_the_ineligible(void)
{
    adv_stage();
    data_fdps_chapter_event_or_battle_end_code = 1;

    adv_run();

    CHECK_EQ((int) adv_unit(5)->hp_current, 50);
    CHECK_EQ((int) adv_unit(13)->hp_current, 50);
    CHECK_EQ((int) adv_unit(6)->hp_current, 50);
    CHECK_EQ((int) adv_unit(7)->hp_current, 50);
    CHECK_EQ((int) adv_unit(8)->hp_current, 50);
    CHECK_EQ((int) adv_unit(9)->hp_current, 50);
    CHECK_EQ((int) adv_unit(11)->hp_current, 70);
    CHECK_EQ((int) adv_unit(12)->hp_current, 70);
    adv_unstage();
}

/* A battle-end code already standing: the NPC side's turn events fire
   (0002e0c0 with PUSH 0x1, at the unbumped turn) and its status tick runs --
   that is the post-action handler's one call -- and the test at 0001e588
   returns.  The NPC phase, which would call the handler again, does not run;
   the side-0 event at the same turn does not fire; the play-active flag is
   left at the 0 stored on entry, not raised; the turn counter is not bumped;
   and the cursor mode, which only the later stages write, is untouched. */
static void adv_stops_after_the_npc_ticks_on_a_battle_end_code(void)
{
    adv_stage();
    set_entry(stage_block, 0, ADV_TURN, 2, ADV_SIDE_NPC);
    set_entry(stage_block, 1, ADV_TURN, 5, ADV_SIDE_ENEMY);
    set_entry(stage_block, 2, ADV_TURN, 1, ADV_SIDE_PLAYER);
    data_fdps_chapter_event_or_battle_end_code = 1;

    adv_run();

    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(handler_slots[0], 2);
    CHECK_EQ(adv_post_calls, 1);
    CHECK_EQ((int) data_fdps_ui_play_active_flag, 0);
    CHECK_EQ(data_fdps_battle_turn_counter, ADV_TURN);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, ADV_CURSOR_MODE_SENTINEL);
    CHECK_EQ((int) adv_unit(6)->flags, ADV_FLAG_ACTED);
    adv_unstage();
}

/* With no code standing the NPC phase runs: its first iteration relocates the
   array and calls the post-action handler a second time, which raises the
   code, and the test at 0001e59a returns before the enemy banner.  So the
   array has moved, the handler ran twice, the acted bit is still on unit 6
   (fdps_units_clear_status_bit7 comes after the banner), the enemy-side event
   has not fired, the counter is not bumped and the flag is left lowered. */
static void adv_stops_after_the_npc_phase_on_a_battle_end_code(void)
{
    unsigned char *staged;

    adv_stage();
    set_entry(stage_block, 0, ADV_TURN, 5, ADV_SIDE_ENEMY);
    adv_end_on_post_call = 2;
    staged = data_fdps_map_unit_array_ptr;

    adv_run();

    CHECK_EQ(data_fdps_map_unit_array_ptr != staged, 1);
    CHECK_EQ(adv_post_calls, 2);
    CHECK_EQ(handler_calls, 0);
    CHECK_EQ((int) adv_unit(6)->flags, ADV_FLAG_ACTED);
    CHECK_EQ(data_fdps_battle_turn_counter, ADV_TURN);
    CHECK_EQ((int) data_fdps_ui_play_active_flag, 0);
    CHECK_EQ((int) adv_unit(0)->hp_current, 70);
    adv_unstage();
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

    RUN_TEST(npc_phase_hides_the_cursor_before_the_sweep);
    RUN_TEST(npc_phase_sweeps_every_unit_once);
    RUN_TEST(npc_phase_hides_the_cursor_every_iteration);
    RUN_TEST(npc_phase_relocates_the_array_every_iteration);
    RUN_TEST(npc_phase_seeds_the_pending_slot_every_iteration);
    RUN_TEST(npc_phase_dispatches_on_the_current_chapter_id);
    RUN_TEST(npc_phase_calls_no_scorer);
    RUN_TEST(npc_phase_stops_on_a_battle_end_code);
    RUN_TEST(npc_phase_rereads_the_unit_count_every_iteration);

    RUN_TEST(turn_reads_the_fields_the_assembly_reads);
    RUN_TEST(turn_cancel_ends_the_call_with_nothing_spent);
    RUN_TEST(turn_restarts_the_marked_tile_pulse);
    RUN_TEST(turn_relocates_the_array_each_pass);
    RUN_TEST(turn_takes_the_start_tile_from_the_cursor);
    RUN_TEST(turn_resets_the_grid_after_the_cursor);
    RUN_TEST(turn_exit_fires_a_pending_event_with_the_unit);

    RUN_TEST(adv_reads_the_fields_the_assembly_reads);
    RUN_TEST(adv_rest_heals_a_fifth_of_the_maximum);
    RUN_TEST(adv_rest_skips_the_ineligible);
    RUN_TEST(adv_stops_after_the_npc_ticks_on_a_battle_end_code);
    RUN_TEST(adv_stops_after_the_npc_phase_on_a_battle_end_code);


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
