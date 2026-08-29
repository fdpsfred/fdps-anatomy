/* tests/table.c -- cover for src/table.c.
 *
 * Expected values come from the assembly of the five accessors -- 00018ab0
 * over FRIAPRDA.DAT, 00018ae0 over FRILEVUP.DAT, 00018b10 over ENEMYDAT.DAT,
 * 00018b40 over ITEM.DAT and 00018b70 over PROMAP.DAT -- and from ticket 17's
 * layouts of struct fdps_character_base_record, struct fdps_character_growth,
 * struct fdps_enemy_data, struct fdps_item_effect and struct fdps_class_record
 * in src/fdpstype.h.  None of them is read off the emitted C.
 *
 * Every body is PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x4, then IMUL
 * EAX,dword ptr [EBP+0x14],<stride> / MOV EDX,dword ptr [<table base global>]
 * / ADD EDX,EAX and nothing else, so the three facts everything below is aimed
 * at are the same for each: the stride is the file's own (0x18, 0x0b, 0x0a,
 * 0x17 and 0x0a), the base is that accessor's own pointer global read fresh on
 * every call, and there is no test of any kind in the body -- no bound on the
 * index, no null check on the base.
 *
 * No .DAT is a loose file: each reaches its table only as a block
 * fdps_load_data_tables has already unpacked out of the VFS container into the
 * heap, so a staged byte buffer is exactly the shape the global holds at run
 * time.  Nothing here asserts what the real tables contain -- every byte read
 * back is one this file wrote -- and all four globals are put back to null on
 * the way out, since ticket 23 has yet to define them and a later unit must not
 * find a stale address in any of them.
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

/* ------------------------------------------------------------------ *
 * fdps_get_growth_record @ 00018ae0
 *
 * The same one-block shape over the other table, and the expected values come
 * from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0xb / MOV EDX,dword
 * ptr [0x00063fec] / ADD EDX,EAX, with nothing else in the body.  The stride
 * is 0x0b, the base is a different global from the one above, and there is no
 * test of any kind.  The record layout is ticket 17's struct
 * fdps_character_growth in src/fdpstype.h.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, and the 60 records FRILEVUP.DAT holds -- the
   same 60 portrait ids the base table above is indexed by. */
#define GROWTH_STRIDE 0x0b
#define GROWTH_COUNT 60

/* A record's worth of lead-in and one spare record at the end, for the
   negative id and the past-the-end id respectively. */
static unsigned char growth_image[GROWTH_STRIDE + (GROWTH_COUNT + 1) * GROWTH_STRIDE];

static unsigned char *growth_base = growth_image + GROWTH_STRIDE;

static void install_growth_base(unsigned char *base)
{
    data_fdps_battle_character_growth_table_ptr = base;
}

static long growth_offset(int char_id)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_growth_record(char_id);
    return (long) (record - growth_base);
}

/* A distinct byte per field, so a field picked up one offset out reads a
   value belonging to some other field.  spell_learning_idx gets 0xff, the
   sentinel the file uses for a form that learns no spells, which also pins
   that the field is read unsigned rather than as a signed -1. */
static void stage_growth_record(int char_id)
{
    unsigned char *record;

    record = growth_base + char_id * GROWTH_STRIDE;
    record[0x00] = 0x01;  /* ap_min */
    record[0x01] = 0x02;  /* ap_max */
    record[0x02] = 0x03;  /* dp_min */
    record[0x03] = 0x04;  /* dp_max */
    record[0x04] = 0x05;  /* dx_min */
    record[0x05] = 0x06;  /* dx_max */
    record[0x06] = 0x07;  /* hp_min */
    record[0x07] = 0x08;  /* hp_max */
    record[0x08] = 0x09;  /* mp_min */
    record[0x09] = 0x0a;  /* mp_max */
    record[0x0a] = 0xff;  /* spell_learning_idx, the "learns nothing" value */
}

/* The ADD has no constant term, so record 0 is the table base itself. */
static void growth_record_zero_is_the_table_base(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(0), 0);
}

/* The 0xb the IMUL states.  An accessor that took the stride from the base
   table next door would land at 0x18 here. */
static void consecutive_growth_records_are_eleven_bytes_apart(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(growth_offset(2) - growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(growth_offset(35), 35 * GROWTH_STRIDE);
}

/* The promoted forms run to id 0x23, and 59 is the last id the 60-record file
   has storage for. */
static void the_last_real_growth_record_is_at_the_end_of_the_table(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(0x23), 0x23 * GROWTH_STRIDE);
    CHECK_EQ(growth_offset(GROWTH_COUNT - 1), (GROWTH_COUNT - 1) * GROWTH_STRIDE);
}

/* There is no CMP in the body, so an id past the end is multiplied and added
   like any other.  Adding a bound here would change what every caller reads
   for an out-of-range id. */
static void a_growth_id_past_the_end_is_not_clamped(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(GROWTH_COUNT), GROWTH_COUNT * GROWTH_STRIDE);
}

/* IMUL is the signed multiply: a negative id steps backwards off the front of
   the table.  An unsigned stride would read 0xfffffff5 instead. */
static void a_negative_growth_id_steps_backwards(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(-1), -GROWTH_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fec] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_growth_table_base_is_read_on_every_call(void)
{
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(3), 3 * GROWTH_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_growth_record(3) - growth_base),
             4 * GROWTH_STRIDE);
    install_growth_base(growth_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A "helpful" null guard returning NULL would be a silent
   behaviour change. */
static void a_null_growth_table_base_is_not_guarded(void)
{
    install_growth_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_growth_record(2),
             2 * GROWTH_STRIDE);
    install_growth_base(growth_base);
}

/* The two accessors read two different globals, at 0x00063fd8 and 0x00063fec.
   They are neighbours in bss and one loader call fills each, so an accessor
   naming the wrong one is not caught by any case that installs a single base:
   here each global gets its own base and each accessor must follow its own
   (contract B -- the nine table pointers are nine globals, not an array). */
static void the_two_accessors_read_two_different_globals(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);

    install_growth_base(growth_base + GROWTH_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);

    install_growth_base(growth_base);
    install_base(table_base + RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);

    install_base(table_base);
}

/* The returned pointer addresses the file's own field offsets: all eleven
   fields of the staged record read back the byte written at that offset, so
   the base and the stride are checked together against the packed layout.  A
   record pointer right to within a byte still reads the neighbouring field
   here, and a struct that lost the pack pragma would find hp_min somewhere
   other than +0x06. */
static void the_returned_growth_pointer_addresses_the_packed_record(void)
{
    struct fdps_character_growth *record;

    install_growth_base(growth_base);
    stage_growth_record(7);
    record = fdps_get_growth_record(7);
    CHECK_EQ(record->ap_min, 0x01);
    CHECK_EQ(record->ap_max, 0x02);
    CHECK_EQ(record->dp_min, 0x03);
    CHECK_EQ(record->dp_max, 0x04);
    CHECK_EQ(record->dx_min, 0x05);
    CHECK_EQ(record->dx_max, 0x06);
    CHECK_EQ(record->hp_min, 0x07);
    CHECK_EQ(record->hp_max, 0x08);
    CHECK_EQ(record->mp_min, 0x09);
    CHECK_EQ(record->mp_max, 0x0a);
    CHECK_EQ(record->spell_learning_idx, 0xff);
}

/* The two fields fdps_roster_add_character reads out of this record are at
   +0x06 and +0x08, which its assembly states outright.  Asserted as raw bytes
   through the returned pointer as well as through the struct above, so the
   offsets are pinned to the file's layout and not merely to the field order
   this test happens to declare. */
static void the_offsets_the_roster_reads_are_six_and_eight(void)
{
    unsigned char *record;

    install_growth_base(growth_base);
    stage_growth_record(9);
    record = (unsigned char *) fdps_get_growth_record(9);
    CHECK_EQ(record[0x06], 0x07);
    CHECK_EQ(record[0x08], 0x09);
    CHECK_EQ((long) (record - growth_base), 9 * GROWTH_STRIDE);
}

/* ------------------------------------------------------------------ *
 * fdps_get_enemy_record @ 00018b10
 *
 * The third accessor of the same one-block shape, and its expected values come
 * from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0xa / MOV EDX,dword ptr
 * [0x00063fd4] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x0a, the base is a third global, and there is no test of any kind.  The
 * record layout is ticket 17's struct fdps_enemy_data in src/fdpstype.h and
 * the 91 records are ENEMYDAT.DAT's 910 bytes divided by that stride
 * (resource_info/data_tables.md).
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, and the records the 910-byte file holds. */
#define ENEMY_STRIDE 0x0a
#define ENEMY_COUNT 91

/* A record's worth of lead-in and one spare record at the end.  The lead-in
   matters more here than for the two tables above: every caller forms the
   index as portrait_id - 0x3c, so a negative index is a value the game can
   actually produce and the case that exercises it must land on real storage. */
static unsigned char enemy_image[ENEMY_STRIDE + (ENEMY_COUNT + 1) * ENEMY_STRIDE];

static unsigned char *enemy_base = enemy_image + ENEMY_STRIDE;

static void install_enemy_base(unsigned char *base)
{
    data_fdps_battle_enemy_data_table_ptr = base;
}

static long enemy_offset(int enemy_index)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_enemy_record(enemy_index);
    return (long) (record - enemy_base);
}

/* A distinct byte per field so a field picked up one offset out reads a value
   belonging to some other field.  hp gets 0xffff, which is the value that
   separates the unsigned short the layout declares from a signed one. */
static void stage_enemy_record(int enemy_index)
{
    unsigned char *record;

    record = enemy_base + enemy_index * ENEMY_STRIDE;
    record[0x00] = 0x11;  /* race_id */
    record[0x01] = 0x22;  /* class_id */
    record[0x02] = 0xff;  /* hp low  -- 0xffff */
    record[0x03] = 0xff;  /* hp high */
    record[0x04] = 0x44;  /* mp */
    record[0x05] = 0x55;  /* ap */
    record[0x06] = 0x66;  /* dp */
    record[0x07] = 0x77;  /* dx */
    record[0x08] = 0x88;  /* mv */
    record[0x09] = 0x99;  /* exp_reward */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over. */
static void enemy_record_zero_is_the_table_base(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(0), 0);
}

/* The 0xa the IMUL states.  An accessor that reached for the neighbouring
   table's stride would land at 0x18 or 0xb here. */
static void consecutive_enemy_records_are_ten_bytes_apart(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);
    CHECK_EQ(enemy_offset(2) - enemy_offset(1), ENEMY_STRIDE);
    CHECK_EQ(enemy_offset(41), 41 * ENEMY_STRIDE);
}

/* 90 is the last index the 910-byte file has storage for, and the last byte of
   that record is the file's last byte. */
static void the_last_real_enemy_record_is_at_the_end_of_the_table(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(ENEMY_COUNT - 1), (ENEMY_COUNT - 1) * ENEMY_STRIDE);
    CHECK_EQ(enemy_offset(ENEMY_COUNT - 1) + ENEMY_STRIDE,
             ENEMY_COUNT * ENEMY_STRIDE);
}

/* There is no CMP in the body, so an index past the end is multiplied and
   added like any other.  A bound added here would change what every caller
   reads for a portrait id above 0x96. */
static void an_enemy_index_past_the_end_is_not_clamped(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(ENEMY_COUNT), ENEMY_COUNT * ENEMY_STRIDE);
}

/* IMUL is the signed multiply, and this is the case the callers can reach:
   portrait_id - 0x3c is negative for every playable character, so an index of
   -1 must step one record backwards rather than becoming 0xfffffff6 and an
   address four gigabytes away. */
static void a_negative_enemy_index_steps_backwards(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(-1), -ENEMY_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fd4] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_enemy_table_base_is_read_on_every_call(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(3), 3 * ENEMY_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_enemy_record(3) - enemy_base),
             4 * ENEMY_STRIDE);
    install_enemy_base(enemy_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_enemy_table_base_is_not_guarded(void)
{
    install_enemy_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_enemy_record(2),
             2 * ENEMY_STRIDE);
    install_enemy_base(enemy_base);
}

/* Three accessors, three globals, at 0x00063fd4, 0x00063fd8 and 0x00063fec.
   They are neighbours in bss and one loader call fills each, so an accessor
   naming the wrong one is invisible to any case that installs a single base:
   here each global gets its own and each accessor must follow its own
   (contract B -- the nine table pointers are nine globals, not an array). */
static void the_enemy_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);

    install_enemy_base(enemy_base);
}

/* The returned pointer addresses the file's own field offsets: all ten fields
   of the staged record read back the byte written at that offset, so base and
   stride are checked together against the packed layout.  hp straddles +0x02
   and +0x03 and is the only field wider than a byte, which is what makes the
   remaining eight land at odd offsets a padded struct would move. */
static void the_returned_enemy_pointer_addresses_the_packed_record(void)
{
    struct fdps_enemy_data *record;

    install_enemy_base(enemy_base);
    stage_enemy_record(7);
    record = fdps_get_enemy_record(7);
    CHECK_EQ(record->race_id, 0x11);
    CHECK_EQ(record->class_id, 0x22);
    CHECK_EQ(record->mp, 0x44);
    CHECK_EQ(record->ap, 0x55);
    CHECK_EQ(record->dp, 0x66);
    CHECK_EQ(record->dx, 0x77);
    CHECK_EQ(record->mv, 0x88);
    CHECK_EQ(record->exp_reward, 0x99);
}

/* hp is the one 16-bit field and the layout declares it unsigned, so the
   all-ones pattern is 65535 and not -1.  Signedness is the branch a caller
   takes the moment it compares the value, so it is pinned here rather than
   left to the first caller to discover (contract C). */
static void the_enemy_hp_coefficient_is_an_unsigned_word(void)
{
    struct fdps_enemy_data *record;

    install_enemy_base(enemy_base);
    stage_enemy_record(7);
    record = fdps_get_enemy_record(7);
    CHECK_EQ(record->hp, 0xffff);
}

/* The field both damage paths read straight after the call is the byte at
   +0x9: 0001a319 MOV BL,byte ptr [EDX+0x9] in fdps_combat_compute_hit_outcome
   and 0002851c MOV DL,byte ptr [EAX+0x9] in fdps_unit_apply_damage, each
   multiplying it into the reward at 0x00069cec.  Asserted as a raw byte
   through the returned pointer as well as through the struct above, so the
   offset is pinned to the file's layout and not merely to the field order this
   test happens to declare. */
static void the_offset_the_reward_reads_is_nine(void)
{
    unsigned char *record;

    install_enemy_base(enemy_base);
    stage_enemy_record(9);
    record = (unsigned char *) fdps_get_enemy_record(9);
    CHECK_EQ(record[0x09], 0x99);
    CHECK_EQ((long) (record - enemy_base), 9 * ENEMY_STRIDE);
}

/* The whole index expression as a caller writes it: portrait_id - 0x3c, with
   0x3c the first enemy form and 0x96 the last the file has storage for.  This
   is where the accessor's arithmetic is checked against the id the unit record
   actually carries, which is the number the strategy-guide tables and
   assets/characters.md are written in. */
static void a_portrait_id_maps_to_its_record_by_subtracting_sixty(void)
{
    install_enemy_base(enemy_base);
    CHECK_EQ(enemy_offset(0x3c - 0x3c), 0);
    CHECK_EQ(enemy_offset(0x3d - 0x3c), ENEMY_STRIDE);
    CHECK_EQ(enemy_offset(0x96 - 0x3c), (ENEMY_COUNT - 1) * ENEMY_STRIDE);
}

/* ------------------------------------------------------------------ *
 * fdps_get_item_record @ 00018b40
 *
 * The fourth accessor of the same one-block shape, and its expected values
 * come from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0x17 / MOV
 * EDX,dword ptr [0x00063fe0] / ADD EDX,EAX, with nothing else in the body.
 * The stride is 0x17, the base is a fourth global, and there is no test of any
 * kind.  The record layout is ticket 17's struct fdps_item_effect in
 * src/fdpstype.h, and the 251 records are ITEM.DAT's 5,773 bytes divided by
 * that stride (resource_info/data_tables.md).
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, the records the 5,773-byte file holds, and the
   file's own length.  ids 0x00-0xFA exist, of which 0x00-0xE1 carry content
   and 0xE2-0xFA are blank records (assets/items.md). */
#define ITEM_STRIDE 0x17
#define ITEM_COUNT 251
#define ITEM_FILE_BYTES 5773

/* The id an equipment or bag slot can hold is one byte, so the ids the game
   can reach run to 0xFF -- past the end of the table, which is the point of
   the FF case below.  The image covers every one of them plus a record of
   lead-in for the negative id, so each case lands on real storage. */
static unsigned char item_image[ITEM_STRIDE + 0x100 * ITEM_STRIDE];

static unsigned char *item_base = item_image + ITEM_STRIDE;

static void install_item_base(unsigned char *base)
{
    data_fdps_item_effect_table_ptr = base;
}

static long item_offset(int item_id)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_item_record(item_id);
    return (long) (record - item_base);
}

/* A distinct byte per field so a field picked up one offset out reads a value
   belonging to some other field.  ap and dp get all-ones patterns, which is
   what separates the signed words the layout declares from unsigned ones, and
   price gets one too, which separates the unsigned word it declares from a
   signed one. */
static void stage_item_record(int item_id)
{
    unsigned char *record;

    record = item_base + item_id * ITEM_STRIDE;
    record[0x00] = 0x03;  /* type */
    record[0x01] = 0x64;  /* ap low  -- 100 */
    record[0x02] = 0x00;  /* ap high */
    record[0x03] = 0x0a;  /* hit low  -- 10 */
    record[0x04] = 0x00;  /* hit high */
    record[0x05] = 0xce;  /* dp low  -- 0xffce, i.e. -50 signed */
    record[0x06] = 0xff;  /* dp high */
    record[0x07] = 0x2c;  /* ev low  -- 0x012c, i.e. 300 */
    record[0x08] = 0x01;  /* ev high */
    record[0x09] = 0x11;  /* hit_effect */
    record[0x0a] = 0x22;  /* hit_effect_rate */
    record[0x0b] = 0x01;  /* range_min */
    record[0x0c] = 0x02;  /* range_max */
    record[0x0d] = 0x33;  /* use_effect */
    record[0x0e] = 0xd4;  /* use_amount low  -- 0xffd4, i.e. -44 signed */
    record[0x0f] = 0xff;  /* use_amount high */
    record[0x10] = 0x44;  /* use_distance */
    record[0x11] = 0x55;  /* use_target */
    record[0x12] = 0x66;  /* use_radius */
    record[0x13] = 0xff;  /* price low  -- 0xffff, i.e. 65535 unsigned */
    record[0x14] = 0xff;  /* price high */
    record[0x15] = 0x77;  /* select_mode */
    record[0x16] = 0x00;  /* the byte that is zero in every real record */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over. */
static void item_record_zero_is_the_table_base(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(0), 0);
}

/* The 0x17 the IMUL states.  An accessor that took sizeof(struct
   fdps_item_effect) with the pack pragma lost would land at 24 here, and one
   that reached for a neighbouring table's stride at 0x18, 0xb or 0xa. */
static void consecutive_item_records_are_twenty_three_bytes_apart(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);
    CHECK_EQ(item_offset(2) - item_offset(1), ITEM_STRIDE);
    CHECK_EQ(item_offset(100), 100 * ITEM_STRIDE);
}

/* 0xE1 is the last id that carries content and 0xFA the last the file has
   storage for; the byte after that record is the file's 5,773rd
   (resource_info/data_tables.md). */
static void the_last_real_item_record_is_at_the_end_of_the_table(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(0xe1), 0xe1 * ITEM_STRIDE);
    CHECK_EQ(item_offset(ITEM_COUNT - 1), (ITEM_COUNT - 1) * ITEM_STRIDE);
    CHECK_EQ(item_offset(ITEM_COUNT - 1) + ITEM_STRIDE, ITEM_FILE_BYTES);
}

/* There is no CMP in the body, so an id past the end is multiplied and added
   like any other.  Item id 0xFF is the case the game actually reaches: its
   record starts 92 bytes beyond the end of the table, which is why the guide's
   "FF BUG item" reads a different price and effect every time.  A bound added
   here would replace that with a fixed record and change observable play
   (rebuild_info/pitfalls.md). */
static void an_item_id_past_the_end_is_not_clamped(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(ITEM_COUNT), ITEM_COUNT * ITEM_STRIDE);
    CHECK_EQ(item_offset(0xff), 0xff * ITEM_STRIDE);
    CHECK_EQ(item_offset(0xff) - ITEM_FILE_BYTES, 92);
}

/* IMUL is the signed multiply, so a negative id steps backwards off the front
   of the table rather than becoming a vast positive offset.  A test that saw
   an unsigned stride would read 0xffffffe9 here. */
static void a_negative_item_id_steps_backwards(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(-1), -ITEM_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fe0] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_item_table_base_is_read_on_every_call(void)
{
    install_item_base(item_base);
    CHECK_EQ(item_offset(3), 3 * ITEM_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_item_record(3) - item_base),
             4 * ITEM_STRIDE);
    install_item_base(item_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_item_table_base_is_not_guarded(void)
{
    install_item_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_item_record(2), 2 * ITEM_STRIDE);
    install_item_base(item_base);
}

/* Four accessors, four globals, at 0x00063fd4, 0x00063fd8, 0x00063fe0 and
   0x00063fec.  They are neighbours in bss and one loader call fills each, so
   an accessor naming the wrong one is invisible to any case that installs a
   single base: here each global gets its own and each accessor must follow its
   own (contract B -- the nine table pointers are nine globals, not an
   array). */
static void the_item_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base + ITEM_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);

    install_item_base(item_base);
}

/* The returned pointer addresses the file's own field offsets: every field of
   the staged record reads back the byte written at that offset, so base and
   stride are checked together against the packed layout.  The four stat words
   push the eleven fields behind them onto odd offsets that a padded struct
   would move, which is exactly the divergence the literal stride guards
   against. */
static void the_returned_item_pointer_addresses_the_packed_record(void)
{
    struct fdps_item_effect *record;

    install_item_base(item_base);
    stage_item_record(11);
    record = fdps_get_item_record(11);
    CHECK_EQ(record->type, 0x03);
    CHECK_EQ(record->hit_effect, 0x11);
    CHECK_EQ(record->hit_effect_rate, 0x22);
    CHECK_EQ(record->range_min, 0x01);
    CHECK_EQ(record->range_max, 0x02);
    CHECK_EQ(record->use_effect, 0x33);
    CHECK_EQ(record->use_distance, 0x44);
    CHECK_EQ(record->use_target, 0x55);
    CHECK_EQ(record->use_radius, 0x66);
    CHECK_EQ(record->select_mode, 0x77);
}

/* The five 16-bit fields, four of them at odd offsets.  The layout declares
   ap, hit, dp, ev and use_amount signed and price unsigned, so the all-ones
   pattern is -1 in use_amount and 65535 in price.  Signedness is the branch a
   caller takes the moment it compares the value -- the stat words are added to
   a unit's totals and the price to the party's gold -- so it is pinned here
   rather than left to the first caller to discover (contract C). */
static void the_item_stat_words_are_signed_and_the_price_is_not(void)
{
    struct fdps_item_effect *record;

    install_item_base(item_base);
    stage_item_record(11);
    record = fdps_get_item_record(11);
    CHECK_EQ(record->ap, 100);
    CHECK_EQ(record->hit, 10);
    CHECK_EQ(record->dp, -50);
    CHECK_EQ(record->ev, 300);
    CHECK_EQ(record->use_amount, -44);
    CHECK_EQ(record->price, 65535);
}

/* The three offsets the callers read straight after the call: byte +0x00 to
   classify the item -- 00026026 MOV AL,byte ptr [EDX] in
   fdps_unit_can_equip_item, right after ADD ESP,0x4 -- byte +0x0d to dispatch
   a use effect, and the word at +0x13 for the shop price.  Asserted as raw
   bytes through the returned pointer as well as through the struct above, so
   the offsets are pinned to the file's layout and not merely to the field
   order this test happens to declare. */
static void the_offsets_the_callers_read_are_zero_thirteen_and_nineteen(void)
{
    unsigned char *record;

    install_item_base(item_base);
    stage_item_record(9);
    record = (unsigned char *) fdps_get_item_record(9);
    CHECK_EQ(record[0x00], 0x03);
    CHECK_EQ(record[0x0d], 0x33);
    CHECK_EQ(record[0x13], 0xff);
    CHECK_EQ(record[0x14], 0xff);
    CHECK_EQ((long) (record - item_base), 9 * ITEM_STRIDE);
}

/* ------------------------------------------------------------------ *
 * fdps_get_class_record @ 00018b70
 *
 * The fifth accessor of the same one-block shape, and its expected values come
 * from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0xa / MOV EDX,dword ptr
 * [0x00063fd0] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x0a -- the same as the enemy table's, over a different global -- and there
 * is no test of any kind.  The record layout is ticket 17's struct
 * fdps_class_record in src/fdpstype.h and the 41 rows are PROMAP.DAT's 410
 * bytes divided by that stride (resource_info/data_tables.md).
 *
 * The one fact about this table that is not arithmetic is the +1: row 0 is a
 * default row and class 0x00 lives in row 1, so callers with a unit in hand
 * push class_code + 1 and callers that want the default row push a literal 0
 * (assets/tables/classes.md).  The bias lives in the caller, and the case
 * below pins that this accessor adds none of its own.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, and the rows the 410-byte file holds. */
#define CLASS_STRIDE 0x0a
#define CLASS_COUNT 41
#define CLASS_FILE_BYTES 410

/* A row of lead-in for the negative index and a spare row at the end. */
static unsigned char class_image[CLASS_STRIDE + (CLASS_COUNT + 1) * CLASS_STRIDE];

static unsigned char *class_base = class_image + CLASS_STRIDE;

static void install_class_base(unsigned char *base)
{
    data_fdps_class_table_ptr = base;
}

static long class_offset(int record_index)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_class_record(record_index);
    return (long) (record - class_base);
}

/* A distinct byte per field so a field picked up one offset out reads a value
   belonging to some other field.  The eight movement costs get 0xff in the
   middle, the value the file uses for impassable terrain, so the field is
   pinned as an unsigned byte rather than a signed -1. */
static void stage_class_record(int record_index)
{
    unsigned char *record;
    int terrain;

    record = class_base + record_index * CLASS_STRIDE;
    for (terrain = 0; terrain < 8; terrain++) {
        record[terrain] = (unsigned char) (0x10 + terrain);
    }
    record[0x03] = 0xff;  /* move_cost[3], impassable */
    record[0x08] = 0x1e;  /* critical, 30 per cent */
    record[0x09] = 0x64;  /* magic_resist_complement, 100 */
}

/* The ADD has no constant term, so row 0 is the table base itself -- the file
   has no header to step over, which is what makes the default row reachable
   with a pushed literal 0. */
static void class_record_zero_is_the_table_base(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(0), 0);
}

/* The 0xa the IMUL states, over the whole width of the table. */
static void consecutive_class_records_are_ten_bytes_apart(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(1), CLASS_STRIDE);
    CHECK_EQ(class_offset(2) - class_offset(1), CLASS_STRIDE);
    CHECK_EQ(class_offset(23), 23 * CLASS_STRIDE);
}

/* 40 is the last row the 410-byte file has storage for, and the byte after
   that row is the file's last. */
static void the_last_real_class_record_is_at_the_end_of_the_table(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(CLASS_COUNT - 1), (CLASS_COUNT - 1) * CLASS_STRIDE);
    CHECK_EQ(class_offset(CLASS_COUNT - 1) + CLASS_STRIDE, CLASS_FILE_BYTES);
}

/* The whole index expression as a caller writes it: MOV AL,[unit+0x20] / INC
   EAX / PUSH EAX, so class code 0x00 is row 1 and the 40 class codes fill rows
   1..40.  Nothing in the accessor performs the addition -- a +1 moved in here
   would pass every stride case above and silently shift the two literal-zero
   callers onto class 0x00's row.  The row-0 default and the +1 are
   assets/tables/classes.md's; the INC is the callers' assembly. */
static void a_class_code_maps_to_its_row_by_adding_one(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(0x00 + 1), CLASS_STRIDE);
    CHECK_EQ(class_offset(0x01 + 1), 2 * CLASS_STRIDE);
    CHECK_EQ(class_offset(0x27 + 1), (CLASS_COUNT - 1) * CLASS_STRIDE);
    CHECK_EQ(class_offset(0), 0);
}

/* There is no CMP in the body, so an index past the last row is multiplied and
   added like any other. */
static void a_class_index_past_the_end_is_not_clamped(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(CLASS_COUNT), CLASS_COUNT * CLASS_STRIDE);
}

/* IMUL is the signed multiply, so a negative index steps backwards off the
   front of the table rather than becoming a vast positive offset.  An unsigned
   stride would read 0xfffffff6 here. */
static void a_negative_class_index_steps_backwards(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(-1), -CLASS_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fd0] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_class_table_base_is_read_on_every_call(void)
{
    install_class_base(class_base);
    CHECK_EQ(class_offset(3), 3 * CLASS_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_class_record(3) - class_base),
             4 * CLASS_STRIDE);
    install_class_base(class_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_class_table_base_is_not_guarded(void)
{
    install_class_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_class_record(2),
             2 * CLASS_STRIDE);
    install_class_base(class_base);
}

/* This accessor and fdps_get_enemy_record share the stride 0xa and differ only
   in the global they read -- 0x00063fd0 against 0x00063fd4, four bytes apart in
   bss.  Every stride case above would pass with the two globals swapped, so
   this is the case that separates them: each of the five globals gets its own
   base and each accessor must follow its own (contract B -- the nine table
   pointers are nine globals, not an array). */
static void the_class_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    CHECK_EQ(class_offset(1), CLASS_STRIDE);

    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    CHECK_EQ(class_offset(1), CLASS_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base + CLASS_STRIDE);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);

    install_class_base(class_base);
}

/* The returned pointer addresses the file's own field offsets: all ten bytes
   of the staged row read back what was written there, so base and stride are
   checked together against the layout.  move_cost is eight bytes and the two
   scalars sit behind it at +0x08 and +0x09, which is the arrangement a caller
   indexing move_cost by terrain type depends on. */
static void the_returned_class_pointer_addresses_the_packed_record(void)
{
    struct fdps_class_record *record;

    install_class_base(class_base);
    stage_class_record(7);
    record = fdps_get_class_record(7);
    CHECK_EQ(record->move_cost[0], 0x10);
    CHECK_EQ(record->move_cost[1], 0x11);
    CHECK_EQ(record->move_cost[2], 0x12);
    CHECK_EQ(record->move_cost[3], 0xff);
    CHECK_EQ(record->move_cost[7], 0x17);
    CHECK_EQ(record->critical, 0x1e);
    CHECK_EQ(record->magic_resist_complement, 0x64);
}

/* The two bytes the combat callers read straight after the call, each
   zero-extended: 0001a00c MOV AL,byte ptr [EDX+0x8] in
   fdps_combat_compute_hit_outcome and 0002835e MOV AL,byte ptr [EDX+0x9] in
   fdps_spell_damage_unit.  Asserted as raw bytes through the returned pointer
   as well as through the struct above, so the offsets are pinned to the file's
   layout and not merely to the field order this test happens to declare. */
static void the_offsets_the_combat_callers_read_are_eight_and_nine(void)
{
    unsigned char *record;

    install_class_base(class_base);
    stage_class_record(9);
    record = (unsigned char *) fdps_get_class_record(9);
    CHECK_EQ(record[0x08], 0x1e);
    CHECK_EQ(record[0x09], 0x64);
    CHECK_EQ((long) (record - class_base), 9 * CLASS_STRIDE);
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

    RUN_TEST(growth_record_zero_is_the_table_base);
    RUN_TEST(consecutive_growth_records_are_eleven_bytes_apart);
    RUN_TEST(the_last_real_growth_record_is_at_the_end_of_the_table);
    RUN_TEST(a_growth_id_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_growth_id_steps_backwards);
    RUN_TEST(the_growth_table_base_is_read_on_every_call);
    RUN_TEST(a_null_growth_table_base_is_not_guarded);
    RUN_TEST(the_two_accessors_read_two_different_globals);
    RUN_TEST(the_returned_growth_pointer_addresses_the_packed_record);
    RUN_TEST(the_offsets_the_roster_reads_are_six_and_eight);

    RUN_TEST(enemy_record_zero_is_the_table_base);
    RUN_TEST(consecutive_enemy_records_are_ten_bytes_apart);
    RUN_TEST(the_last_real_enemy_record_is_at_the_end_of_the_table);
    RUN_TEST(an_enemy_index_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_enemy_index_steps_backwards);
    RUN_TEST(the_enemy_table_base_is_read_on_every_call);
    RUN_TEST(a_null_enemy_table_base_is_not_guarded);
    RUN_TEST(the_enemy_accessor_reads_its_own_global);
    RUN_TEST(the_returned_enemy_pointer_addresses_the_packed_record);
    RUN_TEST(the_enemy_hp_coefficient_is_an_unsigned_word);
    RUN_TEST(the_offset_the_reward_reads_is_nine);
    RUN_TEST(a_portrait_id_maps_to_its_record_by_subtracting_sixty);

    RUN_TEST(item_record_zero_is_the_table_base);
    RUN_TEST(consecutive_item_records_are_twenty_three_bytes_apart);
    RUN_TEST(the_last_real_item_record_is_at_the_end_of_the_table);
    RUN_TEST(an_item_id_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_item_id_steps_backwards);
    RUN_TEST(the_item_table_base_is_read_on_every_call);
    RUN_TEST(a_null_item_table_base_is_not_guarded);
    RUN_TEST(the_item_accessor_reads_its_own_global);
    RUN_TEST(the_returned_item_pointer_addresses_the_packed_record);
    RUN_TEST(the_item_stat_words_are_signed_and_the_price_is_not);
    RUN_TEST(the_offsets_the_callers_read_are_zero_thirteen_and_nineteen);

    RUN_TEST(class_record_zero_is_the_table_base);
    RUN_TEST(consecutive_class_records_are_ten_bytes_apart);
    RUN_TEST(the_last_real_class_record_is_at_the_end_of_the_table);
    RUN_TEST(a_class_code_maps_to_its_row_by_adding_one);
    RUN_TEST(a_class_index_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_class_index_steps_backwards);
    RUN_TEST(the_class_table_base_is_read_on_every_call);
    RUN_TEST(a_null_class_table_base_is_not_guarded);
    RUN_TEST(the_class_accessor_reads_its_own_global);
    RUN_TEST(the_returned_class_pointer_addresses_the_packed_record);
    RUN_TEST(the_offsets_the_combat_callers_read_are_eight_and_nine);

    /* Put the globals back the way they were found.  They are null until
       ticket 23 defines them, and leaving a pointer to this file's static
       buffers in one would hand the next unit an address it has no business
       holding. */
    install_base((unsigned char *) 0);
    install_growth_base((unsigned char *) 0);
    install_enemy_base((unsigned char *) 0);
    install_item_base((unsigned char *) 0);
    install_class_base((unsigned char *) 0);
}
