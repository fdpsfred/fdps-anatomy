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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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

    /* Put the globals back before leaving.  stage() points them at this
       file's own fixture and the runners share one process: a later unit that
       expects an unallocated array would inherit a live pointer into another
       translation unit's block and pass or fail for the wrong reason. */
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_map_unit_count = 0;
}
