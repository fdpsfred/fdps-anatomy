/* tests/table.c -- cover for src/table.c.
 *
 * Expected values come from the assembly at 00018ab0 -- PUSH EBX/ESI/EDI/EBP,
 * MOV EBP,ESP, SUB ESP,0x4, then IMUL EAX,dword ptr [EBP+0x14],0x18 / MOV
 * EDX,dword ptr [0x00063fd8] / ADD EDX,EAX and nothing else -- and from ticket
 * 17's layout of struct fdps_character_base_record in src/fdpstype.h.  None of
 * them is read off the emitted C.
 *
 * The three facts that assembly states, and that everything below is aimed at:
 * the stride is 0x18, the base is the table pointer global read fresh on every
 * call, and there is no test of any kind in the body -- no bound on char_id, no
 * null check on the base.
 *
 * FRIAPRDA.DAT is not a loose file: it reaches this table only as a block
 * fdps_load_data_tables has already unpacked out of the VFS container into the
 * heap, so a staged byte buffer is exactly the shape the global holds at run
 * time.  Nothing here asserts what the real table contains -- every byte read
 * back is one this file wrote -- and the global is put back to null on the way
 * out, since ticket 23 has yet to define it and a later unit must not find a
 * stale address in it.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"

/* The stride the IMUL states, and the 60 records the table holds. */
#define RECORD_STRIDE 0x18
#define RECORD_COUNT 60

/* One record's worth of buffer in front of the base the tests install, so that
   the negative-id case -- which the signed IMUL makes reachable -- lands on
   real storage instead of off the front of the array. */
#define LEAD_PAD RECORD_STRIDE

/* One record past the end as well, for the case that walks off the back. */
static unsigned char table_image[LEAD_PAD + (RECORD_COUNT + 1) * RECORD_STRIDE];

/* Where the tests point the global: the notional record 0. */
static unsigned char *table_base = table_image + LEAD_PAD;

/* How far the returned pointer sits from the installed base, in bytes.  Byte
   distance rather than pointer equality, so a failure says how far out the
   arithmetic was and not merely that it was. */
static long record_offset(int char_id)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_character_base_record(char_id);
    return (long) (record - table_base);
}

/* Writes the one record the field cases read, a distinct byte per field so a
   field picked up from a neighbouring offset reads a value that belongs to
   some other field.  Byte at a time: hp_base and mp_base sit at the odd
   offsets +0x03 and +0x05 in the file, and this fixture must not assume they
   are addressable any other way. */
static void stage_record(int char_id)
{
    unsigned char *record;

    record = table_base + char_id * RECORD_STRIDE;
    record[0x00] = 0x11;  /* race_id */
    record[0x01] = 0x22;  /* class_id */
    record[0x02] = 0x33;  /* level */
    record[0x03] = 0x34;  /* hp_base low  -- 0x1234 */
    record[0x04] = 0x12;  /* hp_base high */
    record[0x05] = 0xff;  /* mp_base low  -- 0xffff, i.e. -1 signed */
    record[0x06] = 0xff;  /* mp_base high */
    record[0x07] = 0x07;  /* move */
    record[0x08] = 0xa1;  /* spell_mask[0] */
    record[0x09] = 0xa2;  /* spell_mask[1] */
    record[0x0a] = 0xa3;  /* spell_mask[2] */
    record[0x0b] = 0xa4;  /* spell_mask[3] */
    record[0x0c] = 0x51;  /* equipped_item_0 */
    record[0x0d] = 0x52;  /* equipped_item_1 */
    record[0x0e] = 0x61;  /* carried_items[0] */
    record[0x0f] = 0x62;  /* carried_items[1] */
    record[0x10] = 0x63;  /* carried_items[2] */
    record[0x11] = 0x64;  /* carried_items[3] */
    record[0x12] = 0x64;  /* ap_base low  -- 100 */
    record[0x13] = 0x00;  /* ap_base high */
    record[0x14] = 0xce;  /* dp_base low  -- 0xffce, i.e. -50 signed */
    record[0x15] = 0xff;  /* dp_base high */
    record[0x16] = 0x2c;  /* dx_base low  -- 0x012c, i.e. 300 */
    record[0x17] = 0x01;  /* dx_base high */
}

static void install_base(unsigned char *base)
{
    data_fdps_battle_character_base_table_ptr = base;
}

/* Record 0 is the table base itself: the ADD is base + id*0x18 with no
   constant term, so there is no header to step over and no bias. */
static void record_zero_is_the_table_base(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(0), 0);
}

/* The stride the IMUL multiplies by.  A record accessor that used
   sizeof(struct) with the pack pragma lost would land at 26 or 28 here. */
static void consecutive_records_are_one_stride_apart(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(record_offset(2) - record_offset(1), RECORD_STRIDE);
    CHECK_EQ(record_offset(17), 17 * RECORD_STRIDE);
}

/* The last record the 60-entry table really holds. */
static void the_last_real_record_is_at_the_end_of_the_table(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(RECORD_COUNT - 1), (RECORD_COUNT - 1) * RECORD_STRIDE);
}

/* There is no CMP against 0x3c anywhere in the body, so an id past the end is
   multiplied and added like any other and the caller is handed an address off
   the end of the table.  The bound lives in fdps_deploy_unit, which routes
   0x3c and above to fdps_get_enemy_record; adding one here would change which
   record the game reads for every caller that relies on the caller-side
   routing. */
static void an_id_past_the_end_is_not_clamped(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(RECORD_COUNT), RECORD_COUNT * RECORD_STRIDE);
}

/* IMUL is the signed multiply, so a negative id steps backwards off the front
   of the table rather than becoming a vast positive offset.  A test that saw
   an unsigned stride would read 0xffffffe8 here. */
static void a_negative_id_steps_backwards(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(-1), -RECORD_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fd8] is inside the body, not hoisted anywhere: the
   base is re-read on every call, so moving the global moves every answer.  A
   transcription that cached the base in a file-scope copy passes every case
   above and fails this one. */
static void the_table_base_is_read_on_every_call(void)
{
    install_base(table_base);
    CHECK_EQ(record_offset(3), 3 * RECORD_STRIDE);
    install_base(table_base + RECORD_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_character_base_record(3)
                     - table_base),
             4 * RECORD_STRIDE);
    install_base(table_base);
}

/* Nothing in the body tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  Asserted because it is the reason the accessor must not be
   called during startup, and because a "helpful" null guard returning NULL
   would be a silent behaviour change. */
static void a_null_table_base_is_not_guarded(void)
{
    install_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_character_base_record(2),
             2 * RECORD_STRIDE);
    install_base(table_base);
}

/* The returned pointer addresses the file's own field offsets: every field of
   the staged record reads back the byte this file wrote at that offset.  This
   is where the base and the stride are checked together against the packed
   layout -- a record pointer that is right to within a byte or two still reads
   the neighbouring field's value here. */
static void the_returned_pointer_addresses_the_packed_record(void)
{
    struct fdps_character_base_record *record;

    install_base(table_base);
    stage_record(5);
    record = fdps_get_character_base_record(5);
    CHECK_EQ(record->race_id, 0x11);
    CHECK_EQ(record->class_id, 0x22);
    CHECK_EQ(record->level, 0x33);
    CHECK_EQ(record->move, 0x07);
    CHECK_EQ(record->spell_mask[0], 0xa1);
    CHECK_EQ(record->spell_mask[3], 0xa4);
    CHECK_EQ(record->equipped_item_0, 0x51);
    CHECK_EQ(record->equipped_item_1, 0x52);
    CHECK_EQ(record->carried_items[0], 0x61);
    CHECK_EQ(record->carried_items[3], 0x64);
}

/* The five 16-bit stats, two of them at odd offsets, all of them signed in the
   layout: 0xffff at mp_base is -1 and not 65535.  Signedness is the branch a
   caller takes, so it is asserted here rather than left to the first caller to
   discover (contract C). */
static void the_sixteen_bit_stats_are_signed_and_unaligned(void)
{
    struct fdps_character_base_record *record;

    install_base(table_base);
    stage_record(5);
    record = fdps_get_character_base_record(5);
    CHECK_EQ(record->hp_base, 0x1234);
    CHECK_EQ(record->mp_base, -1);
    CHECK_EQ(record->ap_base, 100);
    CHECK_EQ(record->dp_base, -50);
    CHECK_EQ(record->dx_base, 300);
}

void run_table_tests(void)
{
    RUN_TEST(record_zero_is_the_table_base);
    RUN_TEST(consecutive_records_are_one_stride_apart);
    RUN_TEST(the_last_real_record_is_at_the_end_of_the_table);
    RUN_TEST(an_id_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_id_steps_backwards);
    RUN_TEST(the_table_base_is_read_on_every_call);
    RUN_TEST(a_null_table_base_is_not_guarded);
    RUN_TEST(the_returned_pointer_addresses_the_packed_record);
    RUN_TEST(the_sixteen_bit_stats_are_signed_and_unaligned);

    /* Put the global back the way it was found.  It is null until ticket 23
       defines it, and leaving a pointer to this file's static buffer in it
       would hand the next unit an address it has no business holding. */
    install_base((unsigned char *) 0);
}
