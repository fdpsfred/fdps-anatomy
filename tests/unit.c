/* tests/unit.c -- cover for src/unit.c.
 *
 * Each function's cases carry the addresses their expected values were read
 * from; the two blocks below stage the unit array independently of each other
 * and each case republishes the base it wants before it calls anything.
 *
 * Expected values come from the assembly at 0002ccd0 -- the five-byte table at
 * 0002b28e (23 22 24 25 27), IMUL EAX,[EBP+0x14],0x50 for the stride, CMP byte
 * ptr [EAX],0x0 / JZ for "this timer is running", CDQ+IDIV / INC EDX for the
 * rotation and ADD [EBP+0x18],-1 / CMP / JNZ for the pick -- and from the unit
 * record layout ticket 17 settled (status_timers at +0x22, record size 0x50).
 * None of them is read off the emitted C.
 *
 * The unit array is staged here rather than read from a game file: the
 * function takes its whole input from data_fdps_map_unit_array_ptr and its two
 * arguments, so pointing that global at a local block is the only way to reach
 * the loops.  Nothing below asserts what that global holds on its own --
 * ticket 23 owns that.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"

/* Four records, so an index other than 0 has somewhere to land and a walk that
   strayed into the neighbouring record would be visible. */
#define STAGE_UNITS 4

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every record, including all six status timers, and point the global at
   the block.  A record staged this way has no active effect at all, which is
   the case the function short-circuits on. */
static void stage(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
}

/* Set one status timer of one record by its slot in status_timers[], which is
   the record layout's own numbering and NOT the icon slot the function returns
   -- keeping the two apart here is what lets the swap below be asserted. */
static void set_timer(int unit_index, int timer_index, int value)
{
    stage_units[unit_index].status_timers[timer_index] =
        (unsigned char) value;
}

/* IMUL EAX,dword ptr [EBP+0x14],0x50 at 0002cced is the stride, and the five
   table bytes are record offsets 0x22..0x27, so the record has to be 0x50
   bytes with its timers at +0x22 for the offsets in the C to address the same
   bytes the original does. */
static void unit_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) sizeof(stage_units[0].status_timers), 6);
}

/* CMP dword ptr [EBP-0x8],0x0 / JNZ at 0002cd2f: with the first pass having
   counted nothing, the function stores -1 and jumps to the epilogue without
   dividing.  It has to hold for every cycle, including one that would select a
   slot if any timer were running. */
static void no_active_effect_returns_minus_one(void)
{
    stage();
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), -1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 1), -1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 7), -1);
}

/* The first two table entries are 0x23 then 0x22, so the timer at +0x23 --
   status_timers[1] -- is icon slot 0 and the timer at +0x22 is icon slot 1.
   Emitting the offsets in record order would return 1 and 0 here instead, and
   would swap those two icons on the map. */
static void first_two_offsets_are_swapped(void)
{
    stage();
    set_timer(0, 1, 3);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 0);

    stage();
    set_timer(0, 0, 3);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 1);
}

/* The remaining three entries are 0x24, 0x25, 0x27: status_timers[2], [3] and
   [5] are icon slots 2, 3 and 4.  status_timers[4] at +0x26 is in NO table
   entry, so a unit carrying only that effect shows no icon -- the sixth frame
   index a straight loop over 0x22..0x27 would produce does not exist in
   IconSts.cel. */
static void offset_26_has_no_icon_slot(void)
{
    stage();
    set_timer(0, 2, 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 2);

    stage();
    set_timer(0, 3, 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 3);

    stage();
    set_timer(0, 5, 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 4);

    stage();
    set_timer(0, 4, 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 4), -1);
}

/* All five table entries active: the count is 5, cycle % 5 + 1 walks 1..5 and
   the second pass hands back slots 0..4 in table order, wrapping at 5.  This
   is the whole selection rule in one case: an advancing cycle rotates through
   the effects the unit is carrying. */
static void full_set_rotates_through_every_slot(void)
{
    stage();
    set_timer(0, 0, 9);
    set_timer(0, 1, 9);
    set_timer(0, 2, 9);
    set_timer(0, 3, 9);
    set_timer(0, 5, 9);

    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 0);
    CHECK_EQ(fdps_unit_select_status_icon(0, 1), 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 2), 2);
    CHECK_EQ(fdps_unit_select_status_icon(0, 3), 3);
    CHECK_EQ(fdps_unit_select_status_icon(0, 4), 4);
    CHECK_EQ(fdps_unit_select_status_icon(0, 5), 0);
    CHECK_EQ(fdps_unit_select_status_icon(0, 11), 1);
}

/* Two effects, at table slots 2 and 4.  The modulus is the COUNT of active
   effects and not the table length, so cycle alternates between the two live
   slots and never selects an empty one; the counter is decremented at every
   active timer the pass walks over, which is what makes slot 4 the one that
   drives it to zero on an even count. */
static void modulus_is_the_active_count(void)
{
    stage();
    set_timer(0, 2, 1);
    set_timer(0, 5, 1);

    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 2);
    CHECK_EQ(fdps_unit_select_status_icon(0, 1), 4);
    CHECK_EQ(fdps_unit_select_status_icon(0, 2), 2);
    CHECK_EQ(fdps_unit_select_status_icon(0, 3), 4);
}

/* SAR EDX,0x1f before IDIV at 0002cd44 makes the division signed and it
   truncates towards zero, so -1 % 2 is -1 and cycle becomes 0: the pass then
   decrements to -1 at the first active timer, never equals zero, and falls out
   of the loop to the -1 at 0002cd8d.  -2 % 2 is 0, cycle becomes 1, and the
   first active timer is selected.  An unsigned modulo would return a slot for
   both, and a <= 0 test in the pass would return one for -1 as well. */
static void negative_cycle_follows_the_signed_modulo(void)
{
    stage();
    set_timer(0, 2, 1);
    set_timer(0, 5, 1);

    CHECK_EQ(fdps_unit_select_status_icon(0, -1), -1);
    CHECK_EQ(fdps_unit_select_status_icon(0, -3), -1);
    CHECK_EQ(fdps_unit_select_status_icon(0, -2), 2);
    CHECK_EQ(fdps_unit_select_status_icon(0, -4), 2);
}

/* A timer counts as active on any non-zero value, not on a particular one:
   CMP byte ptr [EAX],0x0 / JZ.  0x01, 0x80 and 0xff all make the record byte
   count once, and only the value 0 takes it out of the rotation. */
static void any_non_zero_timer_counts(void)
{
    stage();
    set_timer(0, 1, 0xff);
    set_timer(0, 0, 0x80);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 0);
    CHECK_EQ(fdps_unit_select_status_icon(0, 1), 1);

    set_timer(0, 1, 0);
    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 1);
    CHECK_EQ(fdps_unit_select_status_icon(0, 1), 1);
}

/* The record is base + unit_index * 0x50 and nothing else: a unit index picks
   its own record's timers, and the neighbouring records' timers -- staged here
   with a different effect each -- do not reach it.  A stride other than 0x50
   would read one of those neighbours and return the wrong slot. */
static void index_selects_its_own_record(void)
{
    stage();
    set_timer(0, 0, 1);
    set_timer(1, 2, 1);
    set_timer(2, 5, 1);
    set_timer(3, 4, 1);

    CHECK_EQ(fdps_unit_select_status_icon(0, 0), 1);
    CHECK_EQ(fdps_unit_select_status_icon(1, 0), 2);
    CHECK_EQ(fdps_unit_select_status_icon(2, 0), 4);
    CHECK_EQ(fdps_unit_select_status_icon(3, 0), -1);
}

/* fdps_get_unit_record @ 0002d210.  Expected values come from the three
   instructions the body is -- IMUL EAX,dword ptr [EBP+0x14],0x50 at 0002d21c,
   MOV EDX,dword ptr [0x00069cd8] at 0002d220, ADD EDX,EAX -- and from what the
   callers do with the result: fdps_battle_count_remaining_units_on_side reads
   the side byte at +6 through the returned pointer (MOV AL,byte ptr [EAX+0x6]
   / AND EAX,0xff at 00018391) after PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4.
   The stride the original writes as a literal. */
#define UNIT_RECORD_STRIDE 0x50

/* Eight records with the published base parked at the third of them, so a
   negative index has real memory in front of it to land on and an overrunning
   stride is visible on either side of the base. */
#define LOOKUP_UNITS 8
#define LOOKUP_BASE_UNIT 2

static unsigned char lookup_block[LOOKUP_UNITS * UNIT_RECORD_STRIDE];

static unsigned char *lookup_base(void)
{
    return lookup_block + LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE;
}

/* Zero the block, then give every record a distinct value in its side byte at
   +6 so a pointer landing one record out reads a different number rather than
   the same zero. */
static void stage_lookup(void)
{
    int i;

    for (i = 0; i < (int) sizeof(lookup_block); i++) {
        lookup_block[i] = 0;
    }
    for (i = 0; i < LOOKUP_UNITS; i++) {
        lookup_block[i * UNIT_RECORD_STRIDE + 6] = (unsigned char) (0x10 + i);
    }
    data_fdps_map_unit_array_ptr = lookup_base();
}

/* base + unit_index * 0x50 and nothing else.  Index 0 is the base itself --
   the ADD contributes no constant of its own -- and each further index is one
   whole record on, which is what makes the record the caller reads the record
   it asked for. */
static void the_record_is_base_plus_index_times_stride(void)
{
    unsigned char *base;

    stage_lookup();
    base = lookup_base();

    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(0) - base), 0);
    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(1) - base), 0x50);
    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(3) - base), 0xf0);
    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(5) - base), 0x190);
}

/* IMUL is the signed multiply, so a negative index scales to a negative offset
   and addresses memory in front of the array.  An unsigned multiply would turn
   -1 into an offset near four gigabytes instead, which is a different wrong
   address and not the one the original computes. */
static void a_negative_index_steps_backwards(void)
{
    unsigned char *base;

    stage_lookup();
    base = lookup_base();

    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(-1) - base),
             -0x50);
    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(-2) - base),
             -0xa0);
    CHECK_EQ(fdps_get_unit_record(-1)->side, 0x11);
    CHECK_EQ(fdps_get_unit_record(-2)->side, 0x10);
}

/* The returned pointer addresses the packed record's own field offsets: the
   side byte the caller at 00018391 reads is at +6 of the record this index
   names, and the records on either side hold different values.  Base and
   stride are checked together here against the layout ticket 17 settled. */
static void the_returned_pointer_addresses_the_packed_record(void)
{
    struct fdps_unit_record *record;

    stage_lookup();

    record = fdps_get_unit_record(0);
    CHECK_EQ(record->side, 0x12);
    CHECK_EQ((long) ((unsigned char *) &record->side -
                     (unsigned char *) record), 6);

    CHECK_EQ(fdps_get_unit_record(1)->side, 0x13);
    CHECK_EQ(fdps_get_unit_record(2)->side, 0x14);
    CHECK_EQ((long) (int) sizeof(struct fdps_unit_record),
             UNIT_RECORD_STRIDE);
}

/* MOV EDX,dword ptr [0x00069cd8] is inside the body, so the base is fetched
   afresh on every call.  That is what makes the accessor safe to use after
   fdps_relocate_unit_array at 0002df90 has moved the array, wiped the old
   storage and freed it -- the battle turn loops call that once per unit and
   re-resolve through here immediately.  A base cached anywhere would keep
   handing back addresses inside the freed block. */
static void the_base_is_reread_on_every_call(void)
{
    stage_lookup();
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x13);

    data_fdps_map_unit_array_ptr = lookup_block;
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x11);

    data_fdps_map_unit_array_ptr = lookup_block + 5 * UNIT_RECORD_STRIDE;
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x16);
}

/* Nothing compares the index against data_fdps_map_unit_count: the count's
   address never appears in the body, and an index past the live units returns
   the arithmetic result like any other.  The bound is the caller's -- for
   example fdps_battle_count_remaining_units_on_side does CMP EAX,dword ptr
   [0x00060150] / JL before it pushes -- so a clamp or a null return added here
   would move that responsibility rather than add safety. */
static void the_unit_count_does_not_bound_the_index(void)
{
    unsigned char *base;

    stage_lookup();
    base = lookup_base();
    data_fdps_map_unit_count = 1;

    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(2) - base), 0xa0);
    CHECK_EQ(fdps_get_unit_record(2)->side, 0x14);
    CHECK_EQ((long) ((unsigned char *) fdps_get_unit_record(4) - base), 0x140);

    data_fdps_map_unit_count = 0;
}

/* Contract B.  The party roster at 0x00064108 holds the SAME record type at
   the SAME 0x50 stride as the map unit array at 0x00069cd8, so an accessor
   that had named the wrong one of the two globals would still step by the
   right amount and every case above would still pass.  This separates them:
   moving the roster base out from under the accessor changes nothing, and
   moving the unit base moves every answer.  The instruction that decides it is
   MOV EDX,dword ptr [0x00069cd8] at 0002d220. */
static void the_accessor_reads_the_map_unit_base_not_the_roster(void)
{
    unsigned char *saved_roster;

    saved_roster = data_fdps_roster_array_ptr;

    stage_lookup();
    data_fdps_roster_array_ptr = lookup_block;
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x13);

    data_fdps_roster_array_ptr = lookup_block + 4 * UNIT_RECORD_STRIDE;
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x13);

    data_fdps_map_unit_array_ptr = lookup_block + 4 * UNIT_RECORD_STRIDE;
    CHECK_EQ(fdps_get_unit_record(1)->side, 0x15);

    data_fdps_roster_array_ptr = saved_roster;
}

/* Nothing tests the base either, so a null array -- the state before
   fdps_build_map_unit_array has run -- yields the scaled offset alone as
   though it were an address.  Asserted because a "helpful" null guard
   returning NULL would be a silent behaviour change, and because it is the
   reason the accessor must not be reached outside a battle. */
static void a_null_unit_array_base_is_not_guarded(void)
{
    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
    CHECK_EQ((long) (unsigned long) fdps_get_unit_record(0), 0);
    CHECK_EQ((long) (unsigned long) fdps_get_unit_record(3),
             3 * UNIT_RECORD_STRIDE);
    stage_lookup();
}

void run_unit_tests(void)
{
    RUN_TEST(the_record_is_base_plus_index_times_stride);
    RUN_TEST(a_negative_index_steps_backwards);
    RUN_TEST(the_returned_pointer_addresses_the_packed_record);
    RUN_TEST(the_base_is_reread_on_every_call);
    RUN_TEST(the_unit_count_does_not_bound_the_index);
    RUN_TEST(the_accessor_reads_the_map_unit_base_not_the_roster);
    RUN_TEST(a_null_unit_array_base_is_not_guarded);
    RUN_TEST(unit_record_shape_matches_the_offsets);
    RUN_TEST(no_active_effect_returns_minus_one);
    RUN_TEST(first_two_offsets_are_swapped);
    RUN_TEST(offset_26_has_no_icon_slot);
    RUN_TEST(full_set_rotates_through_every_slot);
    RUN_TEST(modulus_is_the_active_count);
    RUN_TEST(negative_cycle_follows_the_signed_modulo);
    RUN_TEST(any_non_zero_timer_counts);
    RUN_TEST(index_selects_its_own_record);
}
