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

/* fdps_unit_is_retired @ 000109b0.  Expected values come from the six
   instructions that are the whole body -- PUSH EAX / CALL 0x0002d210 / ADD
   ESP,0x4 at 000109bf, then MOV AL,byte ptr [EAX + 0x5] at 000109ce, AND
   AL,0x1 at 000109d1 and AND EAX,0xff at 000109d3 -- and from the record
   layout ticket 17 settled, which puts flags at +5.  The lookup_block staging
   above is reused because the predicate reaches its record through the same
   accessor; each case republishes the base it wants first. */
#define RETIRED_BIT 0x01

/* Set one record's flags byte outright, by its index into the staged lookup
   block rather than relative to the published base, so a case can put a value
   in the record on either side of the one it asks about. */
static void set_flags(int block_slot, int value)
{
    lookup_block[block_slot * UNIT_RECORD_STRIDE + 5] =
        (unsigned char) value;
}

/* MOV AL,byte ptr [EAX + 0x5]: the byte read is the record's flags byte, at
   +5 of the record fdps_get_unit_record hands back and not at +5 of the array
   base.  Asserted against the layout so a field moving under this function
   would fail here rather than silently read the side byte next door. */
static void the_flag_byte_is_at_record_offset_five(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);

    stage_lookup();
    set_flags(LOOKUP_BASE_UNIT, RETIRED_BIT);
    CHECK_EQ(fdps_unit_is_retired(0), 1);

    set_flags(LOOKUP_BASE_UNIT, 0);
    CHECK_EQ(fdps_unit_is_retired(0), 0);
}

/* AND AL,0x1 isolates bit 0 alone.  Bit 7 is the separate per-turn redraw
   flag and every other bit is somebody else's: a unit carrying 0xfe is NOT
   retired, and one carrying 0x01 is, whatever else is set alongside it.  A
   plain non-zero test on the byte would call all four of these retired. */
static void only_bit_zero_decides(void)
{
    stage_lookup();

    set_flags(LOOKUP_BASE_UNIT, 0x01);
    CHECK_EQ(fdps_unit_is_retired(0), 1);

    set_flags(LOOKUP_BASE_UNIT, 0x80);
    CHECK_EQ(fdps_unit_is_retired(0), 0);

    set_flags(LOOKUP_BASE_UNIT, 0xfe);
    CHECK_EQ(fdps_unit_is_retired(0), 0);

    set_flags(LOOKUP_BASE_UNIT, 0x81);
    CHECK_EQ(fdps_unit_is_retired(0), 1);
}

/* AND EAX,0xff after AND AL,0x1 narrows the answer to 0 or 1: the upper three
   bytes of EAX at that point still hold the top of the record pointer the CALL
   returned, so without the second mask the function would return an address
   fragment.  The result is compared for equality with 1 here, not merely for
   truth, which is what pins the narrowing -- callers such as
   fdps_battle_count_remaining_units_on_side test it with TEST EAX,EAX / JZ at
   000183aa, and a caller that took the flag byte whole would count wrongly. */
static void the_result_is_narrowed_to_zero_or_one(void)
{
    stage_lookup();

    set_flags(LOOKUP_BASE_UNIT, 0xff);
    CHECK_EQ(fdps_unit_is_retired(0), 1);

    set_flags(LOOKUP_BASE_UNIT, 0x03);
    CHECK_EQ(fdps_unit_is_retired(0), 1);

    set_flags(LOOKUP_BASE_UNIT, 0x0f);
    CHECK_EQ(fdps_unit_is_retired(0), 1);
}

/* The record is the one fdps_get_unit_record names, so the index scales by the
   0x50 stride and each unit answers for its own flags byte.  Staged with the
   retired bit set on alternate records, so a stride or base that was one
   record out would invert every answer. */
static void the_index_picks_its_own_record(void)
{
    stage_lookup();
    set_flags(LOOKUP_BASE_UNIT + 0, RETIRED_BIT);
    set_flags(LOOKUP_BASE_UNIT + 1, 0);
    set_flags(LOOKUP_BASE_UNIT + 2, RETIRED_BIT);
    set_flags(LOOKUP_BASE_UNIT + 3, 0);

    CHECK_EQ(fdps_unit_is_retired(0), 1);
    CHECK_EQ(fdps_unit_is_retired(1), 0);
    CHECK_EQ(fdps_unit_is_retired(2), 1);
    CHECK_EQ(fdps_unit_is_retired(3), 0);
}

/* Nothing here bounds unit_index -- there is no compare in the body at all --
   and the signed multiply inside the accessor carries a negative index
   backwards off the front of the array, where it reads a flags byte like any
   other.  Asserted so a guard added on the way past would fail rather than
   quietly change what the predicate answers. */
static void a_negative_index_reads_the_record_in_front(void)
{
    stage_lookup();
    set_flags(LOOKUP_BASE_UNIT - 1, RETIRED_BIT);
    set_flags(LOOKUP_BASE_UNIT - 2, 0);
    set_flags(LOOKUP_BASE_UNIT, 0);

    CHECK_EQ(fdps_unit_is_retired(-1), 1);
    CHECK_EQ(fdps_unit_is_retired(-2), 0);
    CHECK_EQ(fdps_unit_is_retired(0), 0);
}

/* The record is resolved through the accessor on every call, so the predicate
   follows the array when fdps_relocate_unit_array moves it: republishing the
   base under an unchanged index changes the answer.  A record pointer cached
   anywhere between the two would keep reading the old block. */
static void the_record_is_resolved_on_every_call(void)
{
    stage_lookup();
    set_flags(LOOKUP_BASE_UNIT, RETIRED_BIT);
    set_flags(0, 0);
    CHECK_EQ(fdps_unit_is_retired(0), 1);

    data_fdps_map_unit_array_ptr = lookup_block;
    CHECK_EQ(fdps_unit_is_retired(0), 0);
    CHECK_EQ(fdps_unit_is_retired(LOOKUP_BASE_UNIT), 1);
}

/* fdps_unit_mark_retired @ 000138f0.  Expected values come from the four
   instructions that are the whole body -- MOV EAX,dword ptr [EBP + 0x14] /
   PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 at 000138fc, then MOV byte ptr
   [EAX + 0x5],0x1 at 0001390e, whose encoding c6 40 05 01 is the immediate
   move and not the 80 48 05 01 an OR would be -- and from the record layout
   ticket 17 settled, which puts flags at +5.  The lookup_block staging above is
   reused because this writer reaches its record through the same accessor;
   each case republishes the base it wants first. */

/* Read one record's flags byte back out of the staged block by its index there,
   so a case can inspect a record the call was not aimed at. */
static int flags_of(int block_slot)
{
    return (int) lookup_block[block_slot * UNIT_RECORD_STRIDE + 5];
}

/* The store is an assignment of the literal 1 to the whole byte, so whatever
   the byte held beforehand is gone: 0x80, the acted-this-turn flag, comes out
   as 1 and not as 0x81.  This is the case that separates the emitted
   record->flags = 1 from the record->flags |= 1 that the neighbouring
   fdps_battle_mark_unit_done invites -- every other assertion in this block
   passes under either spelling. */
static void retiring_assigns_the_whole_flags_byte(void)
{
    stage_lookup();

    set_flags(LOOKUP_BASE_UNIT, 0x80);
    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);

    set_flags(LOOKUP_BASE_UNIT, 0xff);
    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);

    set_flags(LOOKUP_BASE_UNIT, 0x00);
    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);

    set_flags(LOOKUP_BASE_UNIT, 0x01);
    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);
}

/* One byte is written and it is the one at +5.  The bytes on either side of it
   -- walk_step at +4 and side at +6, which stage_lookup gives a distinct value
   per record -- are untouched, and so is the class byte further in.  A store
   through a mistyped pointer, or an offset one out, would show up here. */
static void nothing_but_the_flags_byte_is_written(void)
{
    stage_lookup();
    lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 4] = 0x33;
    lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 0x20] = 0x1f;

    fdps_unit_mark_retired(0);

    CHECK_EQ((int) lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 4],
             0x33);
    CHECK_EQ((int) lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 6],
             0x12);
    CHECK_EQ((int) lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 0x20],
             0x1f);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);
}

/* The record written is base + unit_index * 0x50: each index retires its own
   unit and leaves its neighbours alone.  A stride or base one record out would
   retire the wrong unit while still passing the byte-value cases above. */
static void the_index_picks_the_record_to_retire(void)
{
    stage_lookup();

    fdps_unit_mark_retired(1);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 0), 0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 1), 1);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 2), 0);

    fdps_unit_mark_retired(3);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 2), 0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 3), 1);
}

/* No compare appears in the body at all, so nothing bounds unit_index, and the
   accessor's signed IMUL carries a negative index backwards off the front of
   the array -- where the store lands like any other.  Asserted so a guard added
   on the way past would fail here rather than quietly drop the write. */
static void a_negative_index_retires_the_record_in_front(void)
{
    stage_lookup();
    data_fdps_map_unit_count = 1;

    fdps_unit_mark_retired(-1);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT - 1), 1);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 0);

    fdps_unit_mark_retired(4);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT + 4), 1);

    data_fdps_map_unit_count = 0;
}

/* The record is resolved through the accessor on every call, so the write
   follows the array when fdps_relocate_unit_array moves it: republishing the
   base under an unchanged index puts the 1 in a different record.  A base or a
   record pointer cached anywhere between the two would keep writing into the
   old block. */
static void the_written_record_is_resolved_on_every_call(void)
{
    stage_lookup();

    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(LOOKUP_BASE_UNIT), 1);

    data_fdps_map_unit_array_ptr = lookup_block;
    fdps_unit_mark_retired(0);
    CHECK_EQ(flags_of(0), 1);
    CHECK_EQ(flags_of(1), 0);
}

/* The two halves of the pair agree: what this function writes is exactly what
   fdps_unit_is_retired reads back, on the same index and through the same
   accessor.  Bit 0 is the retired flag both ways round. */
static void the_predicate_reads_back_what_this_wrote(void)
{
    stage_lookup();

    CHECK_EQ(fdps_unit_is_retired(2), 0);
    fdps_unit_mark_retired(2);
    CHECK_EQ(fdps_unit_is_retired(2), 1);
    CHECK_EQ(fdps_unit_is_retired(1), 0);

    set_flags(LOOKUP_BASE_UNIT + 1, 0x80);
    CHECK_EQ(fdps_unit_is_retired(1), 0);
    fdps_unit_mark_retired(1);
    CHECK_EQ(fdps_unit_is_retired(1), 1);
}

/* fdps_unit_is_flying @ 00012550.  Expected values come from the body itself --
   MOV AL,byte ptr [EDX + 0x20] at 00012570 for the record byte, and the five
   immediates of the comparison chain, CMP dword ptr [EBP + -0x8],0x16 at
   00012576, 0x17 at 0001257c, 0x18 at 00012584, 0x1f at 0001258c and 0x25 at
   00012594, against MOV dword ptr [EBP + -0x4],0x1 at 0001259a and the same
   store of 0 at 000125a3 -- plus the class code table in assets/classes.md,
   which names those five 技師, 機械伯爵, 機械大師, 飛兵 and 惡靈 and is where
   活屍 being 0x26 comes from.  The lookup_block staging above is reused: the
   predicate reaches its record through the same accessor, and each case
   republishes the base it wants first. */

/* The five immediates of the comparison chain, in the order the chain tests
   them.  Written out here from the CMP instructions above so the sweep below
   has a statement of the set that does not come from the emitted C. */
static unsigned char flying_class_codes[5] = { 0x16, 0x17, 0x18, 0x1f, 0x25 };

/* Set one record's class byte outright, by its index into the staged lookup
   block rather than relative to the published base, so a case can put a value
   in the record on either side of the one it asks about. */
static void set_clazz(int block_slot, int value)
{
    lookup_block[block_slot * UNIT_RECORD_STRIDE + 0x20] =
        (unsigned char) value;
}

/* MOV AL,byte ptr [EDX + 0x20]: the byte read is the record's class byte at
   +0x20 of the record fdps_get_unit_record hands back.  Its neighbours are
   race at +0x1f and level at +0x21, both of which take class codes as values
   in their own right, so an offset one either way would answer from a field
   that looks exactly like the right one.  Staged with a flying code in both
   neighbours and a non-flying code in clazz to separate them. */
static void the_class_byte_is_at_record_offset_twenty(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), 0x20);

    stage_lookup();
    set_clazz(LOOKUP_BASE_UNIT, 0x1f);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x1e);
    CHECK_EQ(fdps_unit_is_flying(0), 0);

    stage_lookup();
    lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 0x1f] = 0x16;
    lookup_block[LOOKUP_BASE_UNIT * UNIT_RECORD_STRIDE + 0x21] = 0x16;
    set_clazz(LOOKUP_BASE_UNIT, 0x00);
    CHECK_EQ(fdps_unit_is_flying(0), 0);
}

/* Each of the five codes the chain compares against, one arm at a time, and
   each checked for the value 1 rather than for truth -- the function stores the
   literal 1, so a caller may compare against it as well as TEST it. */
static void every_flying_class_code_returns_one(void)
{
    stage_lookup();

    set_clazz(LOOKUP_BASE_UNIT, 0x16);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x17);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x18);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x1f);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x25);
    CHECK_EQ(fdps_unit_is_flying(0), 1);
}

/* The chain is five equality tests and nothing else, so the whole 0..0xff
   domain of the class byte is settled: exactly those five codes answer 1 and
   every other one answers 0.  A range test, a bitmask, or a sixth code slipped
   into the set would show up here rather than in whichever of the 40 class
   codes a spot check happened to name.  The byte load is zero-extended (XOR
   EAX,EAX at 0001256b), so 0x80..0xff are as much a part of the domain as the
   low half. */
static void the_flying_set_is_exactly_five_codes(void)
{
    int code;
    int entry;
    int expected;
    int flying_count;
    int disagreements;

    stage_lookup();
    flying_count = 0;
    disagreements = 0;

    for (code = 0; code <= 0xff; code++) {
        expected = 0;
        for (entry = 0; entry < 5; entry++) {
            if ((int) flying_class_codes[entry] == code) {
                expected = 1;
            }
        }

        set_clazz(LOOKUP_BASE_UNIT, code);
        if (fdps_unit_is_flying(0) != expected) {
            disagreements++;
        }
        if (fdps_unit_is_flying(0) == 1) {
            flying_count++;
        }
    }

    CHECK_EQ(disagreements, 0);
    CHECK_EQ(flying_count, 5);
}

/* 0x26 活屍 is next to 0x25 惡靈 in the class table and is the class the set
   most obviously invites: its PROMAP.DAT record leaves five terrain columns
   passable where the five flying classes leave six (assets/classes.md), and no
   CMP in the body names it.  0x19 機兵 is the other near miss -- a mechanical
   class sitting one past 0x18 機械大師.  Both answer 0, and the flying
   neighbours on either side answer 1, which is what makes the boundary the
   value and not the neighbourhood. */
static void the_classes_next_to_the_set_do_not_fly(void)
{
    stage_lookup();

    set_clazz(LOOKUP_BASE_UNIT, 0x25);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x26);
    CHECK_EQ(fdps_unit_is_flying(0), 0);

    set_clazz(LOOKUP_BASE_UNIT, 0x27);
    CHECK_EQ(fdps_unit_is_flying(0), 0);

    set_clazz(LOOKUP_BASE_UNIT, 0x18);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    set_clazz(LOOKUP_BASE_UNIT, 0x19);
    CHECK_EQ(fdps_unit_is_flying(0), 0);

    set_clazz(LOOKUP_BASE_UNIT, 0x15);
    CHECK_EQ(fdps_unit_is_flying(0), 0);
}

/* The record is the one fdps_get_unit_record names, so the index scales by the
   0x50 stride and each unit answers for its own class byte.  The signed
   multiply inside the accessor carries a negative index backwards off the front
   of the array, where it reads a class byte like any other -- nothing in this
   body bounds unit_index, there is no compare against the unit count in it at
   all. */
static void the_index_picks_its_own_record_class(void)
{
    stage_lookup();
    set_clazz(LOOKUP_BASE_UNIT - 1, 0x1f);
    set_clazz(LOOKUP_BASE_UNIT + 0, 0x00);
    set_clazz(LOOKUP_BASE_UNIT + 1, 0x16);
    set_clazz(LOOKUP_BASE_UNIT + 2, 0x26);
    set_clazz(LOOKUP_BASE_UNIT + 3, 0x25);

    CHECK_EQ(fdps_unit_is_flying(-1), 1);
    CHECK_EQ(fdps_unit_is_flying(0), 0);
    CHECK_EQ(fdps_unit_is_flying(1), 1);
    CHECK_EQ(fdps_unit_is_flying(2), 0);
    CHECK_EQ(fdps_unit_is_flying(3), 1);
}

/* The record is resolved through the accessor on every call, so the predicate
   follows the array when fdps_relocate_unit_array moves it: republishing the
   base under an unchanged index changes the answer.  A record pointer cached
   anywhere between the two would keep reading the old block. */
static void the_flying_record_is_resolved_on_every_call(void)
{
    stage_lookup();
    set_clazz(LOOKUP_BASE_UNIT, 0x17);
    set_clazz(0, 0x00);
    CHECK_EQ(fdps_unit_is_flying(0), 1);

    data_fdps_map_unit_array_ptr = lookup_block;
    CHECK_EQ(fdps_unit_is_flying(0), 0);
    CHECK_EQ(fdps_unit_is_flying(LOOKUP_BASE_UNIT), 1);
}

/* fdps_unit_face_target @ 0001c2e0.  Expected values come from the body's own
   assembly: the two PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 pairs at 0001c2f0
   and 0001c2ff, the AND EDX,0xff / AND EAX,0xff / SUB EDX,EAX pairs feeding the
   CRT abs at 0x0003d364 (0001c322, 0001c343), the axis test CMP EAX,dword ptr
   [EBP + -0x4] / JLE 0x0001c374 at 0001c351, the two coordinate compares
   CMP DL,byte ptr [EAX] / JBE at 0001c35e and 0001c37d, and the four literal
   stores MOV byte ptr [EAX + 0x3],<code> at 0001c365 (1), 0001c36e (3),
   0001c385 (2) and 0001c38e (0).  The record layout is ticket 17's, which puts
   pos_x at +0, pos_y at +1 and facing at +3.

   The direction codes' meaning is fixed by the movement playback that writes
   the same byte: 0002d590 stores 1 and then DEC byte ptr [EAX] on pos_x, so 1
   is left, and 0002d360 stores 0 and then INC byte ptr [EAX + 0x1] on pos_y,
   so 0 is down.

   The lookup_block staging above is reused because this function reaches both
   of its records through the same accessor; each case republishes the base it
   wants first. */
#define FACING_DOWN 0
#define FACING_LEFT 1
#define FACING_UP 2
#define FACING_RIGHT 3

/* Put a record's tile coordinates, by its index into the staged lookup block
   rather than relative to the published base, so a case can place a unit on
   either side of the one it asks about. */
static void set_tile(int block_slot, int tile_x, int tile_y)
{
    lookup_block[block_slot * UNIT_RECORD_STRIDE + 0] =
        (unsigned char) tile_x;
    lookup_block[block_slot * UNIT_RECORD_STRIDE + 1] =
        (unsigned char) tile_y;
}

static void set_facing(int block_slot, int value)
{
    lookup_block[block_slot * UNIT_RECORD_STRIDE + 3] =
        (unsigned char) value;
}

static int facing_of(int block_slot)
{
    return (int) lookup_block[block_slot * UNIT_RECORD_STRIDE + 3];
}

/* MOV byte ptr [EAX + 0x3],<code> writes at +3 of the record the accessor
   handed back, and the two coordinates come from +0 and +1 of the same
   records.  Asserted against the layout so a field moving under this function
   fails here rather than silently turning the sprite by writing the sprite
   cache slot. */
static void the_facing_byte_is_at_record_offset_three(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, facing), 3);

    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 4, 10);
    set_facing(LOOKUP_BASE_UNIT, 0xff);

    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);
}

/* The horizontal arm, both ways round.  JBE at 0001c360 sends "unit x is not
   above target x" to the store of 3 and falls through to the store of 1
   otherwise, so a target further left gives 1 and a target further right gives
   3.  Both cases have dy 0, which is strictly below dx and so cannot reach the
   vertical arm. */
static void a_greater_x_gap_faces_along_x(void)
{
    stage_lookup();

    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 4, 10);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);

    set_tile(LOOKUP_BASE_UNIT, 4, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 10, 10);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_RIGHT);
}

/* The vertical arm, both ways round.  JBE at 0001c380 sends "unit y is not
   above target y" to the store of 0 and falls through to the store of 2
   otherwise, so a target further up gives 2 and a target further down gives 0.
   Both cases have dx 0. */
static void a_greater_y_gap_faces_along_y(void)
{
    stage_lookup();

    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 10, 4);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_UP);

    set_tile(LOOKUP_BASE_UNIT, 10, 4);
    set_tile(LOOKUP_BASE_UNIT + 1, 10, 10);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);
}

/* The axis test is JLE, not JL: an equal pair of absolute differences takes
   the vertical arm.  Every one of the four diagonals with dx == dy answers
   with a vertical code, and the symmetric form -- horizontal when dx >= dy --
   would answer all four horizontally instead.  This is the case the rebuild
   note is about. */
static void an_equal_gap_goes_to_the_vertical_arm(void)
{
    stage_lookup();

    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 4, 4);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_UP);

    set_tile(LOOKUP_BASE_UNIT + 1, 16, 4);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_UP);

    set_tile(LOOKUP_BASE_UNIT + 1, 4, 16);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);

    set_tile(LOOKUP_BASE_UNIT + 1, 16, 16);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);
}

/* One tile either side of the tie pins the comparison down to strictly
   greater-than.  dx one larger than dy is horizontal; dy one larger than dx is
   vertical; the pair in between is the tie above. */
static void one_tile_either_side_of_the_tie_switches_axis(void)
{
    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT, 10, 10);

    set_tile(LOOKUP_BASE_UNIT + 1, 3, 4);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);

    set_tile(LOOKUP_BASE_UNIT + 1, 4, 3);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_UP);

    set_tile(LOOKUP_BASE_UNIT + 1, 17, 16);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_RIGHT);

    set_tile(LOOKUP_BASE_UNIT + 1, 16, 17);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);
}

/* Both units on one tile makes both differences 0, which is a tie, so the
   vertical arm runs and JBE takes the store of 0.  The facing standing before
   the call is overwritten: there is no compare against the current facing
   anywhere in the body and no early return, so a unit facing right that is
   asked to face something on its own tile ends up facing down. */
static void the_same_tile_forces_facing_down(void)
{
    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 10, 10);

    set_facing(LOOKUP_BASE_UNIT, FACING_RIGHT);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);

    set_facing(LOOKUP_BASE_UNIT, FACING_LEFT);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);
}

/* The differences are built after AND EDX,0xff / AND EAX,0xff, so the SUB runs
   at int width on two zero-extended bytes and the CRT abs at 0x0003d364 turns
   the negative one positive.  A difference taken in unsigned char instead
   would wrap: with the unit at x 0 against a target at x 200 the wrapped gap
   is 56 and the y gap 156, which would pick the vertical arm, where the real
   gaps 200 and 100 pick the horizontal one. */
static void the_differences_are_taken_at_int_width(void)
{
    stage_lookup();

    set_tile(LOOKUP_BASE_UNIT, 0, 0);
    set_tile(LOOKUP_BASE_UNIT + 1, 200, 100);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_RIGHT);

    set_tile(LOOKUP_BASE_UNIT, 0, 0);
    set_tile(LOOKUP_BASE_UNIT + 1, 100, 200);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_DOWN);
}

/* CMP DL,byte ptr [EAX] / JBE is the unsigned compare, and the coordinates are
   zero-extended into it, so a tile coordinate above 127 is a large number and
   not a negative one.  A unit at x 200 facing a target at x 0 is facing left;
   read as signed char the same pair would answer right. */
static void the_coordinate_compare_is_unsigned(void)
{
    stage_lookup();

    set_tile(LOOKUP_BASE_UNIT, 200, 0);
    set_tile(LOOKUP_BASE_UNIT + 1, 0, 0);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);

    set_tile(LOOKUP_BASE_UNIT, 0, 200);
    set_tile(LOOKUP_BASE_UNIT + 1, 0, 0);
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_UP);
}

/* Only the first argument's record is written.  The target's own facing byte,
   its coordinates and its side byte at +6 all survive the call, and so do the
   records on either side of the acting one -- the single store is
   MOV byte ptr [EAX + 0x3],<code> against the pointer the FIRST call to the
   accessor returned and there is no other write in the body. */
static void only_the_acting_unit_is_written(void)
{
    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 4, 10);
    set_facing(LOOKUP_BASE_UNIT + 1, FACING_UP);
    set_facing(LOOKUP_BASE_UNIT - 1, FACING_UP);
    set_facing(LOOKUP_BASE_UNIT + 2, FACING_UP);

    fdps_unit_face_target(0, 1);

    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT + 1), FACING_UP);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT - 1), FACING_UP);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT + 2), FACING_UP);
    CHECK_EQ((int) lookup_block[(LOOKUP_BASE_UNIT + 1) *
                                UNIT_RECORD_STRIDE + 0], 4);
    CHECK_EQ((int) lookup_block[(LOOKUP_BASE_UNIT + 1) *
                                UNIT_RECORD_STRIDE + 1], 10);
    CHECK_EQ((int) lookup_block[(LOOKUP_BASE_UNIT + 1) *
                                UNIT_RECORD_STRIDE + 6], 0x13);
}

/* Both records come from the accessor, so each index scales by the 0x50 stride
   and the signed multiply carries a negative index backwards off the front of
   the array.  Nothing in the body bounds either argument -- there is no
   compare against the unit count in it at all -- so index -1 turns the record
   in front of the base to face the record at the base. */
static void each_index_picks_its_own_record(void)
{
    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT - 1, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 0, 10, 4);
    set_tile(LOOKUP_BASE_UNIT + 2, 20, 10);
    set_facing(LOOKUP_BASE_UNIT - 1, 0xff);
    set_facing(LOOKUP_BASE_UNIT + 0, 0xff);

    fdps_unit_face_target(-1, 0);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT - 1), FACING_UP);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), 0xff);

    fdps_unit_face_target(0, 2);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_RIGHT);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT - 1), FACING_UP);
}

/* Both records are resolved through fdps_get_unit_record on every call, so the
   pair follows the array when fdps_relocate_unit_array moves it: republishing
   the base under unchanged indices turns a different unit.  A record pointer
   cached across that call would write into the old block. */
static void the_faced_records_are_resolved_on_every_call(void)
{
    stage_lookup();
    set_tile(LOOKUP_BASE_UNIT, 10, 10);
    set_tile(LOOKUP_BASE_UNIT + 1, 4, 10);
    set_tile(0, 10, 10);
    set_tile(1, 10, 4);
    set_facing(0, 0xff);

    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);
    CHECK_EQ(facing_of(0), 0xff);

    data_fdps_map_unit_array_ptr = lookup_block;
    fdps_unit_face_target(0, 1);
    CHECK_EQ(facing_of(0), FACING_UP);
    CHECK_EQ(facing_of(LOOKUP_BASE_UNIT), FACING_LEFT);
}

/* fdps_unit_recompute_combat_stats @ 00024d70.  Expected values come from that
   body's assembly and from the two layouts ticket 17 settled; none of them is
   read off the emitted C.  The facts the cases below are aimed at:

     00024d8e  MOVSX from +0x37, +0x39 and +0x3e seeds attack, defense and BOTH
               hit and evade, and 00024db6 copies the one dexterity value into
               the second of them -- there is no evade base field.
     00024dac  CMP byte ptr [EAX+0x24],0x0 / JZ then ADD dword ptr
               [EBP-0x18],0xf -- the +15 goes on BEFORE that copy.
     00024dc3  the scan runs entry 0..7 over the 2-byte entries at +0x0a, tests
               AND AL,0x40 on the flag byte and zero-extends the id byte.
     00024e0e  the item modifiers go +0x01 to attack, +0x05 to defense, +0x03
               to hit and +0x07 to evade -- an order that does not match the
               order of the four destination fields at +0x48..+0x4e.
     00024e3e  FILD / FMUL double ptr [0x00061b1c] / CALL __CHP / FISTP, under
               CMP byte ptr [EAX+0x22],0x0, and the same again at 00024e58 for
               defense against [0x00061b24].  Both constants hold 1.15.
     00024e6f  four word stores at +0x48, +0x4a, +0x4c and +0x4e.

   The lookup_block staging above is reused because this function reaches its
   record through the same accessor.  The item table is a second staged block:
   the lookup runs through the real fdps_get_item_record in src/table.c, so the
   fixture is laid out at that accessor's own 0x17 stride.  Nothing below
   asserts what a real unit array or a real ITEM.DAT holds -- every byte read
   back is one these cases wrote -- because ticket 23 owns those globals.

   Stat words are written and read a byte at a time, little-endian, because
   every one of them sits at an ODD offset in the record and this fixture must
   not assume the layout it is checking. */
#define ITEM_RECORD_STRIDE 0x17

/* Sized for every id a byte can hold rather than for ITEM.DAT's 251 records,
   because the id arrives zero-extended out of an inventory entry: ids 251..255
   are reachable and land past the end of the real table. */
#define ITEM_IMAGE_RECORDS 257

/* Field offsets inside a unit record, from struct fdps_unit_record. */
#define OFF_INVENTORY 0x0a
#define OFF_STATUS_TIMERS 0x22
#define OFF_AP_BASE 0x37
#define OFF_DP_BASE 0x39
#define OFF_DX_BASE 0x3e
#define OFF_HP_CURRENT 0x40
#define OFF_MP_MAX 0x46
#define OFF_AP 0x48
#define OFF_DP 0x4a
#define OFF_HIT 0x4c
#define OFF_EV 0x4e

/* Field offsets inside an item record, from struct fdps_item_effect. */
#define ITEM_OFF_AP 0x01
#define ITEM_OFF_HIT 0x03
#define ITEM_OFF_DP 0x05
#define ITEM_OFF_EV 0x07

static unsigned char item_image[ITEM_IMAGE_RECORDS * ITEM_RECORD_STRIDE];

static void put_stat_word(unsigned char *field, int value)
{
    field[0] = (unsigned char) (value & 0xff);
    field[1] = (unsigned char) ((value >> 8) & 0xff);
}

/* Read back signed, because every one of these fields is a short and a
   negative total is a case below. */
static short get_stat_word(unsigned char *field)
{
    return (short) (unsigned short) (field[0] | (field[1] << 8));
}

static unsigned char *unit_slot(int block_slot)
{
    return lookup_block + block_slot * UNIT_RECORD_STRIDE;
}

static short stat_of(int block_slot, int offset)
{
    return get_stat_word(unit_slot(block_slot) + offset);
}

/* Zero both blocks and publish both bases.  stage_lookup leaves every timer
   byte and every inventory flag zero, which is the unbuffed, unequipped
   unit. */
static void stage_recompute(void)
{
    int i;

    stage_lookup();
    for (i = 0; i < (int) sizeof(item_image); i++) {
        item_image[i] = 0;
    }
    data_fdps_item_effect_table_ptr = item_image;
}

static void set_bases(int block_slot, int ap_base, int dp_base, int dx_base)
{
    unsigned char *record;

    record = unit_slot(block_slot);
    put_stat_word(record + OFF_AP_BASE, ap_base);
    put_stat_word(record + OFF_DP_BASE, dp_base);
    put_stat_word(record + OFF_DX_BASE, dx_base);
}

/* Fills one of the eight 2-byte inventory entries: flag byte first, id byte
   second, which is the pair the scan reads at +0x0a + 2 * entry. */
static void set_entry(int block_slot, int entry_index, int flags, int item_id)
{
    unsigned char *entry;

    entry = unit_slot(block_slot) + OFF_INVENTORY + entry_index * 2;
    entry[0] = (unsigned char) flags;
    entry[1] = (unsigned char) item_id;
}

static void set_item(int item_id, int ap, int hit, int dp, int ev)
{
    unsigned char *record;

    record = item_image + item_id * ITEM_RECORD_STRIDE;
    put_stat_word(record + ITEM_OFF_AP, ap);
    put_stat_word(record + ITEM_OFF_HIT, hit);
    put_stat_word(record + ITEM_OFF_DP, dp);
    put_stat_word(record + ITEM_OFF_EV, ev);
}

/* Set one status timer by its slot in status_timers[], the record layout's own
   numbering: [0] is +0x22, the attack buff, [1] is +0x23, defense, and [2] is
   +0x24, dexterity. */
static void set_status(int block_slot, int timer_index, int value)
{
    unit_slot(block_slot)[OFF_STATUS_TIMERS + timer_index] =
        (unsigned char) value;
}

/* The offsets the body writes as literals have to be the layout's own fields,
   or the C addresses different bytes from the original.  The item record's
   0x17 stride is fdps_get_item_record's IMUL and is what makes the modifier
   fixture land where that accessor looks. */
static void the_stat_offsets_match_the_two_layouts(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap_base), 0x37);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp_base), 0x39);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dx_base), 0x3e);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dp), 0x4a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hit), 0x4c);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ev), 0x4e);
    CHECK_EQ((int) sizeof(struct fdps_item_effect), ITEM_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, ap), ITEM_OFF_AP);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, hit), ITEM_OFF_HIT);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, dp), ITEM_OFF_DP);
    CHECK_EQ((int) offsetof(struct fdps_item_effect, ev), ITEM_OFF_EV);
}

/* With nothing equipped and no timer running the four totals are the three
   base stats and nothing else: MOVSX word ptr [EAX+0x37] into the attack
   accumulator, [EAX+0x39] into defense, [EAX+0x3e] into the other two. */
static void the_base_stats_seed_the_four_totals(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 40);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 25);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 17);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 17);
}

/* The rebuild note's first trap.  Both hit and evade seed from the one
   dexterity word at +0x3e; +0x40 is hp_current and is a base stat for nothing.
   hp_current is staged to a value that would show if evade reached for it. */
static void hit_and_evade_seed_from_one_dexterity_word(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    put_stat_word(unit_slot(LOOKUP_BASE_UNIT) + OFF_HP_CURRENT, 99);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 17);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 17);
}

/* Each item modifier reaches its own destination, and the mapping is the one
   the assembly makes rather than field order: item +0x01 to record +0x48,
   +0x05 to +0x4a, +0x03 to +0x4c and +0x07 to +0x4e.  The four modifiers are
   distinct so a swapped pair shows as a wrong number rather than a
   coincidence, and hit and evade differ so the two accumulators that started
   equal are seen to part company. */
static void an_equipped_item_adds_its_four_modifiers(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 47);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 28);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 22);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 26);
}

/* AND AL,0x40 tests that one bit.  A carried entry with flags 0, an entry
   flagged empty with 0x80 and one holding every bit but 0x40 all contribute
   nothing, and an entry with 0x40 set alongside other bits contributes. */
static void only_bit_0x40_equips_an_entry(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x00, 3);
    set_entry(LOOKUP_BASE_UNIT, 1, 0x80, 3);
    set_entry(LOOKUP_BASE_UNIT, 2, 0xbf, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 40);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 17);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0xc1, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 47);
}

/* The scan covers all eight entries at +0x0a: an equipped item in the last one
   counts, and a ninth entry's worth of bytes -- +0x1a, which is
   spells_known_bitmap -- is past the end of the scan and counts for nothing
   however it is flagged. */
static void all_eight_inventory_entries_are_scanned(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 7, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 47);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 8, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 40);
}

/* The +0x24 timer is worth 15 dexterity, it reaches hit and evade alike, and
   it reaches neither attack nor defense.  Because the +15 lands on the seed
   before the copy, hit gets it too -- adding it only to the accumulator the
   assembly names second would leave hit short by 15. */
static void the_dexterity_buff_lifts_hit_and_evade_by_fifteen(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_status(LOOKUP_BASE_UNIT, 2, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 32);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 32);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 40);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 25);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_status(LOOKUP_BASE_UNIT, 2, 200);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 37);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 41);
}

/* The rebuild note's second trap.  While +0x22 runs the attack total is a
   DOUBLE multiply by 1.15 truncated toward zero, and the nearest double to
   1.15 is a shade below it: 100 comes out 114 and 200 comes out 229, where the
   obvious total * 115 / 100 gives 115 and 230.  Both values are named in the
   rebuild note at 00024d70, and both hold whether the x87 multiplies at
   extended or at double precision.  Defense is untouched by this timer. */
static void the_attack_buff_multiplies_by_a_truncated_double(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 100, 25, 17);
    set_status(LOOKUP_BASE_UNIT, 0, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 114);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 25);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 200, 25, 17);
    set_status(LOOKUP_BASE_UNIT, 0, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 229);
}

/* +0x23 is the defense timer and scales defense the same way, by the second
   1.15 at 00061b24, leaving attack alone; the two timers are independent and
   both together scale both. */
static void the_defense_buff_scales_defense_alone(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 100, 17);
    set_status(LOOKUP_BASE_UNIT, 1, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 114);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 40);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 200, 100, 17);
    set_status(LOOKUP_BASE_UNIT, 0, 1);
    set_status(LOOKUP_BASE_UNIT, 1, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 229);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 114);
}

/* Both multiplies happen after the equipment loop, so a buff scales the
   equipped total and not the base: a base of 90 with a +10 item buffs from 100
   and gives 114, where scaling the base first would give 103 + 10. */
static void the_buffs_scale_the_total_after_equipment(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 90, 90, 17);
    set_item(3, 10, 0, 10, 0);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_status(LOOKUP_BASE_UNIT, 0, 1);
    set_status(LOOKUP_BASE_UNIT, 1, 1);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 114);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 114);
}

/* Any non-zero count runs a buff -- CMP byte ptr [EAX+0x22],0x0 / JZ, not a
   comparison against a particular value -- and the three timers the body never
   reads, status_timers[3], [4] and [5] at +0x25..+0x27, change nothing. */
static void only_the_first_three_timers_reach_the_stats(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 100, 100, 17);
    set_status(LOOKUP_BASE_UNIT, 0, 255);
    set_status(LOOKUP_BASE_UNIT, 1, 255);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 114);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 114);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 100, 100, 17);
    set_status(LOOKUP_BASE_UNIT, 3, 9);
    set_status(LOOKUP_BASE_UNIT, 4, 9);
    set_status(LOOKUP_BASE_UNIT, 5, 9);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 100);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 100);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 17);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 17);
}

/* Contract C.  Every value entering the accumulators arrives through MOVSX: a
   base stat word of 0xffce is -50 and an item modifier of 0xfff6 is -10, so a
   cursed item lowers the stat instead of adding 65526 to it. */
static void base_stats_and_modifiers_are_signed(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 0xffce, 0xffff, 0xfffb);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), -50);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), -1);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), -5);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), -5);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 0xfff6, 0xffff, 0xfffe, 0xfffd);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 30);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_DP), 23);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HIT), 16);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 14);
}

/* XOR EAX,EAX / MOV AL,byte ptr [EDX+0x1]: the id is zero-extended, so 0xff is
   255 and indexes forwards.  Item 0xff is reachable in play and its record
   lies past the end of ITEM.DAT's 251 records; the fixture stages storage
   there so the case measures the index and not whatever memory follows the
   real table. */
static void the_item_id_byte_is_unsigned(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(0xff, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 0xff);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 47);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_EV), 26);
}

/* The store narrows and does not saturate: MOV word ptr [EDX+0x48],AX takes
   the low 16 bits of a total accumulated as a dword.  Two items of +20000 on a
   base of 30000 make 70000, which lands as 4464 and not as 32767.  This pins
   the store rather than the accumulator's width -- addition agrees on the low
   16 bits whatever width it is done in -- and the width itself is taken from
   00024e12's ADD dword ptr [EBP-0x20],EAX. */
static void the_totals_are_truncated_by_the_word_store(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 30000, 0, 0);
    set_item(3, 20000, 0, 0, 0);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_entry(LOOKUP_BASE_UNIT, 1, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 4464);

    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 0x8ad0, 0, 0);
    set_item(3, 0xb1e0, 0, 0, 0);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_entry(LOOKUP_BASE_UNIT, 1, 0x40, 3);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), -4464);
}

/* The record comes from fdps_get_unit_record, so unit_index picks it by the
   0x50 stride from the published base and the neighbours on either side are
   neither read nor written.  Both neighbours are staged with equipment that
   would move their totals if the wrong record were addressed. */
static void the_index_selects_its_own_unit_record(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 11, 11, 11);
    set_bases(LOOKUP_BASE_UNIT + 1, 40, 25, 17);
    set_bases(LOOKUP_BASE_UNIT + 2, 33, 33, 33);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_entry(LOOKUP_BASE_UNIT + 1, 0, 0x40, 3);
    set_entry(LOOKUP_BASE_UNIT + 2, 0, 0x40, 3);
    fdps_unit_recompute_combat_stats(1);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT + 1, OFF_AP), 47);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT + 1, OFF_EV), 26);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_AP), 0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT + 2, OFF_AP), 0);
}

/* Only the four stat words are written.  The record's other fields -- here
   hp_current at +0x40 and mp_max at +0x46 -- and the timer bytes the body read
   keep the values staged into them, and the record behind is not reached at
   all. */
static void only_the_four_stat_words_are_written(void)
{
    stage_recompute();
    set_bases(LOOKUP_BASE_UNIT, 40, 25, 17);
    set_item(3, 7, 5, 3, 9);
    set_entry(LOOKUP_BASE_UNIT, 0, 0x40, 3);
    set_status(LOOKUP_BASE_UNIT, 0, 6);
    put_stat_word(unit_slot(LOOKUP_BASE_UNIT) + OFF_HP_CURRENT, 99);
    put_stat_word(unit_slot(LOOKUP_BASE_UNIT) + OFF_MP_MAX, 77);
    fdps_unit_recompute_combat_stats(0);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_HP_CURRENT), 99);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT, OFF_MP_MAX), 77);
    CHECK_EQ((int) unit_slot(LOOKUP_BASE_UNIT)[OFF_STATUS_TIMERS], 6);
    CHECK_EQ(stat_of(LOOKUP_BASE_UNIT + 1, OFF_AP), 0);
}

/* fdps_set_flag_bit @ 000282b0.  Expected values come from the assembly -- the
   OR byte ptr [EAX+0x1a],DL at 00028309 for the field, the signed /8 at
   000282db and the IDIV by 8 at 000282f7 for the split, and the eight mask
   bytes at 00027660 (01 02 04 08 10 20 40 80) -- from the ids the five call
   sites pass (0x27 and 0x0b at 0002acdf and 0002aceb, 0x1d at 00026b85, 0x00
   at 0003a41c), and from the record layout ticket 17 settled.  None of them is
   read off the emitted C. */
#define SPELL_BITMAP_OFFSET 0x1a
#define SPELL_BITMAP_LEN 5

/* Four records with the published base parked at the second of them, so an
   index of -1 has a real record in front of it to land on. */
#define FLAG_UNITS 4
#define FLAG_BASE_UNIT 1

static struct fdps_unit_record flag_units[FLAG_UNITS];

/* Zero every record, including all five bitmap bytes, and publish the base at
   the second record.  A record staged this way knows no spell at all. */
static void stage_flags(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) flag_units;
    for (i = 0; i < (int) sizeof(flag_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr =
        (unsigned char *) &flag_units[FLAG_BASE_UNIT];
}

/* One bitmap byte of the record the given index names, reached through the
   staged block rather than through the function under test. */
static int bitmap_byte(int unit_index, int byte_index)
{
    return (int) flag_units[FLAG_BASE_UNIT + unit_index]
        .spells_known_bitmap[byte_index];
}

/* The OR's displacement is the literal 0x1a and the ids reach 0x27, so the
   field has to be the five bytes at record offset 0x1a for the C to write the
   bytes the original writes.  A bitmap one byte shorter would leave spell 0x20
   and up addressing the class and level bytes in C as well as in the original,
   but for a different reason. */
static void the_spell_bitmap_is_five_bytes_at_record_offset_0x1a(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, spells_known_bitmap),
             SPELL_BITMAP_OFFSET);
    CHECK_EQ((int) sizeof(flag_units[0].spells_known_bitmap),
             SPELL_BITMAP_LEN);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
}

/* fdps_chapter_01_end passes 0 and 0 at 0003a41c: spell id 0 is bit 0 of the
   first bitmap byte, and no other byte of the bitmap is touched. */
static void spell_zero_sets_bit_zero_of_the_first_byte(void)
{
    stage_flags();
    fdps_set_flag_bit(0, 0);

    CHECK_EQ(bitmap_byte(0, 0), 0x01);
    CHECK_EQ(bitmap_byte(0, 1), 0);
    CHECK_EQ(bitmap_byte(0, 2), 0);
    CHECK_EQ(bitmap_byte(0, 3), 0);
    CHECK_EQ(bitmap_byte(0, 4), 0);
}

/* The eight bytes at 00027660 are 01 02 04 08 10 20 40 80, so the table is one
   bit per entry in ascending order and entry n is the mask for bit n.  A table
   in descending order, or one starting at 0x80, would set a different spell for
   every id whose remainder is not 0 or 7. */
static void the_mask_table_is_one_ascending_bit_per_entry(void)
{
    static unsigned char expected[8] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };
    int spell_id;

    for (spell_id = 0; spell_id < 8; spell_id++) {
        stage_flags();
        fdps_set_flag_bit(0, spell_id);
        CHECK_EQ(bitmap_byte(0, 0), (int) expected[spell_id]);
    }
}

/* The id splits at eight: the byte is id / 8 and the bit is id % 8, with no gap
   between one byte's block and the next.  Spell 8 is bit 0 of the second byte
   rather than anything in the first; 0x0b 封神裂震 and 0x27 萬神降臨 are the
   two the title demo grants and 0x1d 轟神砲 is the one the item effect
   teaches, and 0x27 is the last id the five bytes can hold. */
static void the_byte_index_is_the_id_over_eight(void)
{
    stage_flags();
    fdps_set_flag_bit(0, 8);
    CHECK_EQ(bitmap_byte(0, 0), 0);
    CHECK_EQ(bitmap_byte(0, 1), 0x01);

    stage_flags();
    fdps_set_flag_bit(0, 0x0b);
    CHECK_EQ(bitmap_byte(0, 1), 0x08);

    stage_flags();
    fdps_set_flag_bit(0, 0x1d);
    CHECK_EQ(bitmap_byte(0, 3), 0x20);

    stage_flags();
    fdps_set_flag_bit(0, 0x27);
    CHECK_EQ(bitmap_byte(0, 0), 0);
    CHECK_EQ(bitmap_byte(0, 1), 0);
    CHECK_EQ(bitmap_byte(0, 2), 0);
    CHECK_EQ(bitmap_byte(0, 3), 0);
    CHECK_EQ(bitmap_byte(0, 4), 0x80);
}

/* The instruction is an OR, not a store: the spells the unit already knows in
   that byte survive, and setting one it already knows leaves the byte alone.
   A MOV of the mask would clear every other bit of the byte, which would cost
   the unit up to seven spells each time it learnt one. */
static void the_bit_is_ored_into_the_byte(void)
{
    stage_flags();
    flag_units[FLAG_BASE_UNIT].spells_known_bitmap[0] = 0x0a;

    fdps_set_flag_bit(0, 0);
    CHECK_EQ(bitmap_byte(0, 0), 0x0b);

    fdps_set_flag_bit(0, 1);
    CHECK_EQ(bitmap_byte(0, 0), 0x0b);

    fdps_set_flag_bit(0, 4);
    CHECK_EQ(bitmap_byte(0, 0), 0x1b);
}

/* One byte of one record changes and nothing else in the block does: the whole
   staged block is filled with a pattern, one spell is granted, and the scan
   below finds exactly one byte that moved and it is at
   record + 0x1a + id / 8.  This is the case that would catch a stride other
   than 0x50, a field offset other than 0x1a and any write to a neighbouring
   field. */
static void only_the_one_bitmap_byte_is_written(void)
{
    unsigned char *bytes;
    int changed_count;
    int changed_offset;
    int i;

    stage_flags();
    bytes = (unsigned char *) flag_units;
    for (i = 0; i < (int) sizeof(flag_units); i++) {
        bytes[i] = 0x11;
    }

    fdps_set_flag_bit(0, 0x0b);

    changed_count = 0;
    changed_offset = -1;
    for (i = 0; i < (int) sizeof(flag_units); i++) {
        if (bytes[i] != 0x11) {
            changed_count++;
            changed_offset = i;
        }
    }

    CHECK_EQ(changed_count, 1);
    CHECK_EQ(changed_offset,
             FLAG_BASE_UNIT * 0x50 + SPELL_BITMAP_OFFSET + 1);
    CHECK_EQ((int) bytes[changed_offset], 0x11 | 0x08);
}

/* The record is base + unit_index * 0x50, so each index grants its spell to its
   own record and the signed multiply carries -1 to the record in front of the
   published base.  Three units, three different ids, each landing in one place
   only. */
static void the_index_picks_the_record_that_learns(void)
{
    stage_flags();
    fdps_set_flag_bit(0, 0);
    fdps_set_flag_bit(1, 8);
    fdps_set_flag_bit(-1, 3);

    CHECK_EQ(bitmap_byte(0, 0), 0x01);
    CHECK_EQ(bitmap_byte(0, 1), 0);
    CHECK_EQ(bitmap_byte(1, 0), 0);
    CHECK_EQ(bitmap_byte(1, 1), 0x01);
    CHECK_EQ(bitmap_byte(-1, 0), 0x08);
    CHECK_EQ(bitmap_byte(2, 0), 0);
}

/* The base is re-read through fdps_get_unit_record on every call rather than
   cached, so a call made after the unit array has moved writes into the new
   block and leaves the old one as it was. */
static void the_learning_record_is_resolved_on_every_call(void)
{
    stage_flags();
    fdps_set_flag_bit(0, 0);
    CHECK_EQ(bitmap_byte(0, 0), 0x01);

    data_fdps_map_unit_array_ptr =
        (unsigned char *) &flag_units[FLAG_BASE_UNIT + 1];
    fdps_set_flag_bit(0, 2);

    CHECK_EQ((int) flag_units[FLAG_BASE_UNIT + 1].spells_known_bitmap[0],
             0x04);
    CHECK_EQ(bitmap_byte(0, 0), 0x01);
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
    RUN_TEST(the_flag_byte_is_at_record_offset_five);
    RUN_TEST(only_bit_zero_decides);
    RUN_TEST(the_result_is_narrowed_to_zero_or_one);
    RUN_TEST(the_index_picks_its_own_record);
    RUN_TEST(a_negative_index_reads_the_record_in_front);
    RUN_TEST(the_record_is_resolved_on_every_call);
    RUN_TEST(retiring_assigns_the_whole_flags_byte);
    RUN_TEST(nothing_but_the_flags_byte_is_written);
    RUN_TEST(the_index_picks_the_record_to_retire);
    RUN_TEST(a_negative_index_retires_the_record_in_front);
    RUN_TEST(the_written_record_is_resolved_on_every_call);
    RUN_TEST(the_predicate_reads_back_what_this_wrote);
    RUN_TEST(the_class_byte_is_at_record_offset_twenty);
    RUN_TEST(every_flying_class_code_returns_one);
    RUN_TEST(the_flying_set_is_exactly_five_codes);
    RUN_TEST(the_classes_next_to_the_set_do_not_fly);
    RUN_TEST(the_index_picks_its_own_record_class);
    RUN_TEST(the_flying_record_is_resolved_on_every_call);
    RUN_TEST(the_facing_byte_is_at_record_offset_three);
    RUN_TEST(a_greater_x_gap_faces_along_x);
    RUN_TEST(a_greater_y_gap_faces_along_y);
    RUN_TEST(an_equal_gap_goes_to_the_vertical_arm);
    RUN_TEST(one_tile_either_side_of_the_tie_switches_axis);
    RUN_TEST(the_same_tile_forces_facing_down);
    RUN_TEST(the_differences_are_taken_at_int_width);
    RUN_TEST(the_coordinate_compare_is_unsigned);
    RUN_TEST(only_the_acting_unit_is_written);
    RUN_TEST(each_index_picks_its_own_record);
    RUN_TEST(the_faced_records_are_resolved_on_every_call);

    RUN_TEST(the_stat_offsets_match_the_two_layouts);
    RUN_TEST(the_base_stats_seed_the_four_totals);
    RUN_TEST(hit_and_evade_seed_from_one_dexterity_word);
    RUN_TEST(an_equipped_item_adds_its_four_modifiers);
    RUN_TEST(only_bit_0x40_equips_an_entry);
    RUN_TEST(all_eight_inventory_entries_are_scanned);
    RUN_TEST(the_dexterity_buff_lifts_hit_and_evade_by_fifteen);
    RUN_TEST(the_attack_buff_multiplies_by_a_truncated_double);
    RUN_TEST(the_defense_buff_scales_defense_alone);
    RUN_TEST(the_buffs_scale_the_total_after_equipment);
    RUN_TEST(only_the_first_three_timers_reach_the_stats);
    RUN_TEST(base_stats_and_modifiers_are_signed);
    RUN_TEST(the_item_id_byte_is_unsigned);
    RUN_TEST(the_totals_are_truncated_by_the_word_store);
    RUN_TEST(the_index_selects_its_own_unit_record);
    RUN_TEST(only_the_four_stat_words_are_written);

    RUN_TEST(the_spell_bitmap_is_five_bytes_at_record_offset_0x1a);
    RUN_TEST(spell_zero_sets_bit_zero_of_the_first_byte);
    RUN_TEST(the_mask_table_is_one_ascending_bit_per_entry);
    RUN_TEST(the_byte_index_is_the_id_over_eight);
    RUN_TEST(the_bit_is_ored_into_the_byte);
    RUN_TEST(only_the_one_bitmap_byte_is_written);
    RUN_TEST(the_index_picks_the_record_that_learns);
    RUN_TEST(the_learning_record_is_resolved_on_every_call);
}
