/* tests/table.c -- cover for src/table.c.
 *
 * Expected values come from the assembly of the nine accessors -- 00018ab0
 * over FRIAPRDA.DAT, 00018ae0 over FRILEVUP.DAT, 00018b10 over ENEMYDAT.DAT,
 * 00018b40 over ITEM.DAT, 00018b70 over PROMAP.DAT, 00018ba0 over PROEQU.DAT,
 * 00018bd0 over MAGICDAT.DAT, 00018c00 over GETMGTAB.DAT and 00018c30 over
 * RANKUP.DAT -- and from ticket 17's layouts of struct
 * fdps_character_base_record, struct fdps_character_growth, struct
 * fdps_enemy_data, struct fdps_item_effect, struct fdps_class_record, struct
 * fdps_class_equip_record, struct fdps_spell_effect, struct
 * fdps_spell_learning_record and struct fdps_promotion_record in
 * src/fdpstype.h.  None of them is read off the emitted C.  The tenth
 * accessor, 00023950 over the party roster, is covered in its own section at
 * the end of this file: it has the same shape but its array is a heap block
 * rather than a file table, so what its expected values come from is stated
 * there.
 *
 * Every body is PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x4, then IMUL
 * EAX,dword ptr [EBP+0x14],<stride> / MOV EDX,dword ptr [<table base global>]
 * / ADD EDX,EAX and nothing else, so the three facts everything below is aimed
 * at are the same for each: the stride is the file's own (0x18, 0x0b, 0x0a,
 * 0x17, 0x0a, 0x06, 0x07, 0x0c and 0x0c), the base is that accessor's own pointer
 * global read fresh on every call, and there is no test of any kind in the
 * body -- no bound on the index, no null check on the base.
 *
 * No .DAT is a loose file: each reaches its table only as a block
 * fdps_load_data_tables has already unpacked out of the VFS container into the
 * heap, so a staged byte buffer is exactly the shape the global holds at run
 * time.  Nothing here asserts what the real tables contain -- every byte read
 * back is one this file wrote -- and all nine globals are put back to null on
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

/* ------------------------------------------------------------------ *
 * fdps_get_class_equip_record @ 00018ba0
 *
 * The sixth accessor of the same one-block shape, and its expected values come
 * from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0x6 / MOV EDX,dword ptr
 * [0x00063fe4] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x06, the base is a sixth global, and there is no test of any kind.  The
 * record layout is ticket 17's struct fdps_class_equip_record in
 * src/fdpstype.h and the 36 records are PROEQU.DAT's 216 bytes divided by that
 * stride (resource_info/data_tables.md).
 *
 * The one fact here that is not arithmetic is the absence of the +1 its
 * neighbour fdps_get_class_record needs: the sole caller loads the unit
 * record's class byte and pushes it unbiased -- 00025ffe MOV AL,byte ptr
 * [EAX+0x20] / 00026001 AND EAX,0xff / 00026006 PUSH EAX, with no INC between
 * -- so class code 0x00 is record 0 here where it is row 1 there.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, the records the 216-byte file holds, and the
   file's own length. */
#define EQUIP_STRIDE 0x06
#define EQUIP_COUNT 36
#define EQUIP_FILE_BYTES 216

/* The class codes the game uses run to 0x27, four past the last record the
   file has storage for, so the image covers every one of them plus a record of
   lead-in for the negative index: each case lands on real storage. */
static unsigned char equip_image[EQUIP_STRIDE + 0x28 * EQUIP_STRIDE];

static unsigned char *equip_base = equip_image + EQUIP_STRIDE;

static void install_equip_base(unsigned char *base)
{
    data_fdps_class_equip_table_ptr = base;
}

static long equip_offset(int class_index)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_class_equip_record(class_index);
    return (long) (record - equip_base);
}

/* A distinct byte per position, in the ascending order the file stores them
   in, with the tail padded 0xFF the way a class that uses fewer than six types
   is padded.  0x00 goes in position 0 deliberately: it is a live item type
   carried by real records, not an empty marker, and a fixture that used it as
   filler would hide that (rebuild_info/pitfalls.md). */
static void stage_equip_record(int class_index)
{
    unsigned char *record;

    record = equip_base + class_index * EQUIP_STRIDE;
    record[0x00] = 0x00;  /* allowed_item_type[0], a live type code */
    record[0x01] = 0x03;  /* allowed_item_type[1] */
    record[0x02] = 0x2d;  /* allowed_item_type[2] */
    record[0x03] = 0xff;  /* allowed_item_type[3], padding */
    record[0x04] = 0xff;  /* allowed_item_type[4], padding */
    record[0x05] = 0xff;  /* allowed_item_type[5], padding */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over. */
static void equip_record_zero_is_the_table_base(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(0), 0);
}

/* The 0x6 the IMUL states.  An accessor that reached for a neighbouring
   table's stride would land at 0xa, 0xb, 0x17 or 0x18 here. */
static void consecutive_equip_records_are_six_bytes_apart(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(1), EQUIP_STRIDE);
    CHECK_EQ(equip_offset(2) - equip_offset(1), EQUIP_STRIDE);
    CHECK_EQ(equip_offset(19), 19 * EQUIP_STRIDE);
}

/* 0x23 is the last class code the 216-byte file has storage for, and the byte
   after that record is the file's last. */
static void the_last_real_equip_record_is_at_the_end_of_the_table(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(0x23), 0x23 * EQUIP_STRIDE);
    CHECK_EQ(equip_offset(EQUIP_COUNT - 1) + EQUIP_STRIDE, EQUIP_FILE_BYTES);
}

/* The whole index expression as the caller writes it: the class byte straight
   out of the unit record, with no INC.  Its neighbour fdps_get_class_record is
   fed class code PLUS ONE for the same unit, so the two accessors disagree by
   one record on purpose and a +1 moved in here would pass every stride case
   above while shifting every class onto the next class's equipment list
   (rebuild_info/pitfalls.md).  Asserted against the class accessor on the same
   class code, which is where the difference shows. */
static void a_class_code_indexes_its_equip_record_unbiased(void)
{
    install_equip_base(equip_base);
    install_class_base(class_base);
    CHECK_EQ(equip_offset(0x00), 0);
    CHECK_EQ(equip_offset(0x01), EQUIP_STRIDE);
    CHECK_EQ(equip_offset(0x23), 0x23 * EQUIP_STRIDE);
    CHECK_EQ(class_offset(0x00 + 1), CLASS_STRIDE);
}

/* There is no CMP in the body, so a class code past the last record is
   multiplied and added like any other.  This is the case the game reaches:
   class codes run to 0x27 and the file stops at 0x23, so the four classes
   above it are handed an address off the end and the caller scans six bytes
   there.  A bound added here would change what those classes may equip. */
static void a_class_code_past_the_end_is_not_clamped(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(EQUIP_COUNT), EQUIP_COUNT * EQUIP_STRIDE);
    CHECK_EQ(equip_offset(0x27), 0x27 * EQUIP_STRIDE);
    CHECK_EQ(equip_offset(0x24) - EQUIP_FILE_BYTES, 0);
}

/* IMUL is the signed multiply, so a negative index steps backwards off the
   front of the table rather than becoming a vast positive offset.  An unsigned
   stride would read 0xfffffffa here. */
static void a_negative_class_index_steps_back_in_the_equip_table(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(-1), -EQUIP_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fe4] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_equip_table_base_is_read_on_every_call(void)
{
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(3), 3 * EQUIP_STRIDE);
    install_equip_base(equip_base + EQUIP_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_class_equip_record(3)
                     - equip_base),
             4 * EQUIP_STRIDE);
    install_equip_base(equip_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_equip_table_base_is_not_guarded(void)
{
    install_equip_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_class_equip_record(2),
             2 * EQUIP_STRIDE);
    install_equip_base(equip_base);
}

/* Six accessors, six globals, at 0x00063fd0, 0x00063fd4, 0x00063fd8,
   0x00063fe0, 0x00063fe4 and 0x00063fec.  They are neighbours in bss and one
   loader call fills each, so an accessor naming the wrong one is invisible to
   any case that installs a single base: here each global gets its own and each
   accessor must follow its own (contract B -- the nine table pointers are nine
   globals, not an array). */
static void the_equip_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    CHECK_EQ(equip_offset(1), EQUIP_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    CHECK_EQ(equip_offset(1), EQUIP_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base + EQUIP_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);
    CHECK_EQ(class_offset(1), CLASS_STRIDE);

    install_equip_base(equip_base);
}

/* The returned pointer addresses the file's own six positions: all six bytes
   of the staged record read back what was written there, so base and stride
   are checked together against the layout.  0xFF is the padding the file
   really uses and it must read as 255 and not -1, because the caller's compare
   is against a zero-extended item type byte -- 0002604a AND EAX,0xff before
   the CMP -- so a signed position byte would never match a type above 0x7f
   either way round (contract C). */
static void the_returned_equip_pointer_addresses_the_six_positions(void)
{
    struct fdps_class_equip_record *record;

    install_equip_base(equip_base);
    stage_equip_record(7);
    record = fdps_get_class_equip_record(7);
    CHECK_EQ(record->allowed_item_type[0], 0x00);
    CHECK_EQ(record->allowed_item_type[1], 0x03);
    CHECK_EQ(record->allowed_item_type[2], 0x2d);
    CHECK_EQ(record->allowed_item_type[3], 0xff);
    CHECK_EQ(record->allowed_item_type[5], 0xff);
}

/* The six positions the caller scans, as raw bytes through the returned
   pointer: 00026042 MOV EAX,[EBP-0x10] / 00026045 ADD EAX,[EBP-0xc] / 00026048
   MOV AL,byte ptr [EAX], with the counter at [EBP-0xc] running 0..5 against
   CMP ...,0x6.  The offsets are pinned to the file's layout and not merely to
   the field order this test happens to declare, and the last one is +0x05:
   byte +0x06 already belongs to the next class, which the second assertion
   states as the distance between the two records. */
static void the_caller_scans_offsets_zero_through_five(void)
{
    unsigned char *record;

    install_equip_base(equip_base);
    stage_equip_record(9);
    record = (unsigned char *) fdps_get_class_equip_record(9);
    CHECK_EQ(record[0x00], 0x00);
    CHECK_EQ(record[0x05], 0xff);
    CHECK_EQ((long) ((unsigned char *) fdps_get_class_equip_record(10)
                     - record),
             EQUIP_STRIDE);
    CHECK_EQ((long) (record - equip_base), 9 * EQUIP_STRIDE);
}

/* ------------------------------------------------------------------ *
 * fdps_get_spell_record @ 00018bd0
 *
 * The seventh accessor of the same one-block shape, and its expected values
 * come from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0x7 / MOV EDX,dword
 * ptr [0x00063ff0] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x07, the base is a seventh global, and there is no test of any kind.  The
 * record layout is ticket 17's struct fdps_spell_effect in src/fdpstype.h and
 * the 40 records are MAGICDAT.DAT's 280 bytes divided by that stride
 * (resource_info/data_tables.md, assets/tables/spells.md).
 *
 * The stride is the fact most at risk here: the record opens with a signed
 * 16-bit power and carries five single bytes behind it, so a struct that lost
 * the pack pragma measures 8 and every id from 0x01 up reads a record that
 * starts one byte late and drifts further with each id.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, the records the 280-byte file holds, and the
   file's own length.  ids 0x00-0x27 are continuous with no gap
   (assets/spells.md). */
#define SPELL_STRIDE 0x07
#define SPELL_COUNT 40
#define SPELL_FILE_BYTES 280

/* A record's worth of lead-in for the negative id and a spare record past the
   end, so both of those cases land on real storage. */
static unsigned char spell_image[SPELL_STRIDE + (SPELL_COUNT + 1) * SPELL_STRIDE];

static unsigned char *spell_base = spell_image + SPELL_STRIDE;

static void install_spell_base(unsigned char *base)
{
    data_fdps_battle_spell_effect_table_ptr = base;
}

static long spell_offset(int spell_id)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_spell_record(spell_id);
    return (long) (record - spell_base);
}

/* A distinct byte per field so a field picked up one offset out reads a value
   belonging to some other field.  The values are ones the file really carries:
   power 0xff06 is -250, the form the eight attack-multiplier spells store (a
   2.50x multiplier held as the negative percentage, assets/tables/spells.md);
   cast_range_flags 0x17 is the straight-line bit 0x10 over a range of 7;
   mp_cost 130 is above 0x7f, which separates the unsigned byte the layout
   declares from a signed one; and target_side 0x03 is the third value the
   field takes -- spell 0x16 carries it, against the 0x00 and 0x01 the field's
   documentation lists (assets/spells.md). */
static void stage_spell_record(int spell_id)
{
    unsigned char *record;

    record = spell_base + spell_id * SPELL_STRIDE;
    record[0x00] = 0x06;  /* power low  -- 0xff06, i.e. -250 signed */
    record[0x01] = 0xff;  /* power high */
    record[0x02] = 0x5f;  /* hit_rate, 95 per cent */
    record[0x03] = 0x17;  /* cast_range_flags, straight line at range 7 */
    record[0x04] = 0x02;  /* area */
    record[0x05] = 0x82;  /* mp_cost, 130 */
    record[0x06] = 0x03;  /* target_side */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over, and spell 0x00 is the first record. */
static void spell_record_zero_is_the_table_base(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(0), 0);
}

/* The 0x7 the IMUL states.  This is the case a struct that lost the pack
   pragma fails: sizeof(struct fdps_spell_effect) would be 8 and record 1 would
   land at 8 rather than 7, with the error growing by a byte per id. */
static void consecutive_spell_records_are_seven_bytes_apart(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(1), SPELL_STRIDE);
    CHECK_EQ(spell_offset(2) - spell_offset(1), SPELL_STRIDE);
    CHECK_EQ(spell_offset(0x14), 0x14 * SPELL_STRIDE);
    CHECK_EQ(spell_offset(0x27) - spell_offset(0x26), SPELL_STRIDE);
}

/* 0x27 is the last spell id and the last record the 280-byte file holds; the
   byte after that record is the file's 280th. */
static void the_last_real_spell_record_is_at_the_end_of_the_table(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(0x27), 0x27 * SPELL_STRIDE);
    CHECK_EQ(spell_offset(SPELL_COUNT - 1) + SPELL_STRIDE, SPELL_FILE_BYTES);
}

/* There is no CMP in the body, so an id past the last spell is multiplied and
   added like any other.  A bound added here would change what every caller
   reads for an id above 0x27. */
static void a_spell_id_past_the_end_is_not_clamped(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(SPELL_COUNT), SPELL_COUNT * SPELL_STRIDE);
    CHECK_EQ(spell_offset(SPELL_COUNT) - SPELL_FILE_BYTES, 0);
}

/* IMUL is the signed multiply, so a negative id steps backwards off the front
   of the table rather than becoming a vast positive offset.  An unsigned
   stride would read 0xfffffff9 here. */
static void a_negative_spell_id_steps_backwards(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(-1), -SPELL_STRIDE);
}

/* MOV EDX,dword ptr [0x00063ff0] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_spell_table_base_is_read_on_every_call(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(3), 3 * SPELL_STRIDE);
    install_spell_base(spell_base + SPELL_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_spell_record(3) - spell_base),
             4 * SPELL_STRIDE);
    install_spell_base(spell_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_spell_table_base_is_not_guarded(void)
{
    install_spell_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_spell_record(2),
             2 * SPELL_STRIDE);
    install_spell_base(spell_base);
}

/* Seven accessors, seven globals, at 0x00063fd0, 0x00063fd4, 0x00063fd8,
   0x00063fe0, 0x00063fe4, 0x00063fec and 0x00063ff0.  They are neighbours in
   bss and one loader call fills each, so an accessor naming the wrong one is
   invisible to any case that installs a single base: here each global gets its
   own and each accessor must follow its own (contract B -- the nine table
   pointers are nine globals, not an array). */
static void the_spell_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(1), SPELL_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    install_equip_base(equip_base + EQUIP_STRIDE);
    CHECK_EQ(spell_offset(1), SPELL_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base + SPELL_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);
    CHECK_EQ(enemy_offset(1), ENEMY_STRIDE);
    CHECK_EQ(item_offset(1), ITEM_STRIDE);
    CHECK_EQ(class_offset(1), CLASS_STRIDE);
    CHECK_EQ(equip_offset(1), EQUIP_STRIDE);

    install_spell_base(spell_base);
}

/* The returned pointer addresses the file's own field offsets: all seven bytes
   of the staged record read back what was written there, so base and stride are
   checked together against the packed layout.  The power word at +0x00 pushes
   the five bytes behind it onto offsets a padded struct would move, and it is
   staged at an odd id so the record itself starts at an odd address -- which is
   where a struct the compiler thought it could align would come apart. */
static void the_returned_spell_pointer_addresses_the_packed_record(void)
{
    struct fdps_spell_effect *record;

    install_spell_base(spell_base);
    stage_spell_record(7);
    record = fdps_get_spell_record(7);
    CHECK_EQ(record->hit_rate, 0x5f);
    CHECK_EQ(record->cast_range_flags, 0x17);
    CHECK_EQ(record->area, 0x02);
    CHECK_EQ(record->mp_cost, 130);
    CHECK_EQ(record->target_side, 0x03);
    CHECK_EQ((long) ((unsigned char *) record - spell_base), 7 * SPELL_STRIDE);
}

/* power is the one 16-bit field and the one signed field in the record, which
   is what both callers that read it say outright: 00028591 MOVSX EAX,word ptr
   [EAX] in fdps_spell_heal_unit and 00013945 in fdps_score_targets_for_spell,
   each straight after the ADD ESP,0x4.  The eight attack-multiplier spells
   store the multiplier as a negative percentage, so -250 is 2.50x and reading
   it unsigned would give 65286 (assets/tables/spells.md).  Signedness is the
   branch a caller takes -- fdps_score_targets_for_spell compares it against a
   unit's HP with JGE -- so it is pinned here (contract C). */
static void the_spell_power_is_a_signed_word(void)
{
    struct fdps_spell_effect *record;

    install_spell_base(spell_base);
    stage_spell_record(7);
    record = fdps_get_spell_record(7);
    CHECK_EQ(record->power, -250);
}

/* The five byte fields are unsigned, which is what the callers' zero-extension
   says: XOR EDX,EDX / MOV DL,byte ptr [EAX+0x5] at 000285f7 in
   fdps_spell_deduct_mp_cost and XOR EAX,EAX / MOV AL,byte ptr [EDX+0x2] at
   00028fde in fdps_unit_apply_status_effect.  An all-ones byte must therefore
   read 255 and not -1: in the hit-rate case that is the difference between a
   spell that always lands and one whose CMP against a 0..99 roll can never
   succeed (contract C). */
static void the_spell_byte_fields_are_unsigned(void)
{
    struct fdps_spell_effect *record;
    unsigned char *bytes;
    int field;

    install_spell_base(spell_base);
    bytes = spell_base + 11 * SPELL_STRIDE;
    for (field = 0x02; field < SPELL_STRIDE; field++) {
        bytes[field] = 0xff;
    }
    record = fdps_get_spell_record(11);
    CHECK_EQ(record->hit_rate, 255);
    CHECK_EQ(record->cast_range_flags, 255);
    CHECK_EQ(record->area, 255);
    CHECK_EQ(record->mp_cost, 255);
    CHECK_EQ(record->target_side, 255);
}

/* The three offsets the callers read straight after the call, as raw bytes
   through the returned pointer: the power word at +0x00 and +0x01, the hit rate
   at +0x02 -- 00028fe3 MOV AL,byte ptr [EDX+0x2] -- and the MP cost at +0x05 --
   000285f9 MOV DL,byte ptr [EAX+0x5].  Pinned to the file's layout and not
   merely to the field order this test happens to declare, and the last one is
   +0x06: byte +0x07 already belongs to the next spell, which the last
   assertion states as the distance between two records. */
static void the_offsets_the_spell_callers_read_are_zero_two_and_five(void)
{
    unsigned char *record;

    install_spell_base(spell_base);
    stage_spell_record(9);
    record = (unsigned char *) fdps_get_spell_record(9);
    CHECK_EQ(record[0x00], 0x06);
    CHECK_EQ(record[0x01], 0xff);
    CHECK_EQ(record[0x02], 0x5f);
    CHECK_EQ(record[0x05], 0x82);
    CHECK_EQ(record[0x06], 0x03);
    CHECK_EQ((long) ((unsigned char *) fdps_get_spell_record(10) - record),
             SPELL_STRIDE);
    CHECK_EQ((long) (record - spell_base), 9 * SPELL_STRIDE);
}

/* The id substitution fdps_unit_apply_status_effect performs before it calls:
   00028f92 onward tests its effect code against 0x11, 0x12 and 0x13 and passes
   those through, and for anything else 00028fc8 MOV dword ptr [EBP+0x14],0x14
   replaces it before the PUSH at 00028fd2.  The substitution is the caller's
   and stays the caller's -- nothing in the accessor maps one id to another --
   so this pins that asking for 0x14 and asking for 0x11 reach two different
   records, which is what makes moving the substitution in here visible. */
static void the_status_effect_substitution_is_not_this_accessors(void)
{
    install_spell_base(spell_base);
    CHECK_EQ(spell_offset(0x14), 0x14 * SPELL_STRIDE);
    CHECK_EQ(spell_offset(0x11), 0x11 * SPELL_STRIDE);
    CHECK_EQ(spell_offset(0x14) - spell_offset(0x11), 3 * SPELL_STRIDE);
}

/* ------------------------------------------------------------------
 * fdps_get_spell_learn_record @ 00018c00, over GETMGTAB.DAT.
 *
 * The eighth accessor of the same one-block shape, and its expected values
 * come from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0xc / MOV EDX,dword
 * ptr [0x00063fe8] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x0c, the base is an eighth global, and there is no test of any kind.  The
 * record layout is ticket 17's struct fdps_spell_learning_record in
 * src/fdpstype.h and the 60 records are GETMGTAB.DAT's 720 bytes divided by
 * that stride (resource_info/data_tables.md, assets/tables/characters.md).
 *
 * The fact most at risk here is not the stride but the 0xff sentinel: the
 * index this accessor is handed comes from a field whose "no spells" value is
 * 0xff, and the test for it lives in the caller.  An accessor that grew a null
 * return or a clamp for 0xff would look defensive and would be a behaviour
 * change, so the cases below pin that 0xff is arithmetic like every other
 * index, and that an all-0xff record reads back as twelve 255s rather than
 * twelve -1s.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, the records the 720-byte file holds, and the
   file's own length.  One record per FRIAPRDA.DAT character slot, indexes
   0x00-0x3b (resource_info/data_tables.md). */
#define LEARN_STRIDE 0x0c
#define LEARN_COUNT 60
#define LEARN_FILE_BYTES 720

/* A record's worth of lead-in for the negative index and a spare record past
   the end, so both of those cases land on real storage. */
static unsigned char learn_image[LEARN_STRIDE + (LEARN_COUNT + 1) * LEARN_STRIDE];

static unsigned char *learn_base = learn_image + LEARN_STRIDE;

static void install_learn_base(unsigned char *base)
{
    data_fdps_spell_learning_table_ptr = base;
}

static long learn_offset(int learn_index)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_spell_learn_record(learn_index);
    return (long) (record - learn_base);
}

/* Schedule 0x01 as the file really carries it -- the second playable
   spellcaster's unpromoted form: Lv11 -> spell 0x13, Lv15 -> 0x06, Lv20 ->
   0x0e, Lv25 -> 0x07, the last two pairs unused and therefore (0xff, 0xff)
   (assets/characters.md).  A real row rather than a made-up one, and staged at
   an odd index so the record does not start where the array does. */
static void stage_learn_record(int learn_index)
{
    unsigned char *record;

    record = learn_base + learn_index * LEARN_STRIDE;
    record[0x00] = 11;    /* lv_0 */
    record[0x01] = 0x13;  /* spell_id_0 */
    record[0x02] = 15;    /* lv_1 */
    record[0x03] = 0x06;  /* spell_id_1 */
    record[0x04] = 20;    /* lv_2 */
    record[0x05] = 0x0e;  /* spell_id_2 */
    record[0x06] = 25;    /* lv_3 */
    record[0x07] = 0x07;  /* spell_id_3 */
    record[0x08] = 0xff;  /* lv_4, unused pair */
    record[0x09] = 0xff;  /* spell_id_4 */
    record[0x0a] = 0xff;  /* lv_5, unused pair */
    record[0x0b] = 0xff;  /* spell_id_5 */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over, and schedule 0x00 is the first record. */
static void learn_record_zero_is_the_table_base(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(0), 0);
}

/* The stride the IMUL states, checked as a distance rather than as an absolute
   offset, at the two schedules the guide names first and at the last pair of
   records the file holds. */
static void consecutive_learn_records_are_twelve_bytes_apart(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(1), LEARN_STRIDE);
    CHECK_EQ(learn_offset(2) - learn_offset(1), LEARN_STRIDE);
    CHECK_EQ(learn_offset(0x21), 0x21 * LEARN_STRIDE);
    CHECK_EQ(learn_offset(LEARN_COUNT - 1) - learn_offset(LEARN_COUNT - 2),
             LEARN_STRIDE);
}

/* Stride and record count against the file's own length: the last record
   starts 12 bytes before the end of the 720 bytes GETMGTAB.DAT occupies, so a
   stride that were wrong by one would not reach it or would overshoot. */
static void the_last_real_learn_record_is_at_the_end_of_the_table(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(LEARN_COUNT - 1), (LEARN_COUNT - 1) * LEARN_STRIDE);
    CHECK_EQ(learn_offset(LEARN_COUNT - 1) + LEARN_STRIDE, LEARN_FILE_BYTES);
}

/* There is no CMP in the body, so an index past the last record is multiplied
   and added like any other and lands past the end of the table.  A bound added
   here would be a behaviour change. */
static void a_learn_index_past_the_end_is_not_clamped(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(LEARN_COUNT), LEARN_COUNT * LEARN_STRIDE);
    CHECK_EQ(learn_offset(LEARN_COUNT) - LEARN_FILE_BYTES, 0);
}

/* IMUL is the signed multiply, so a negative index steps backwards off the
   front of the table rather than becoming a four-gigabyte offset.  The
   distinction is invisible in the low 32 bits of the product itself and shows
   only once the product is added to the base, which is what this measures. */
static void a_negative_learn_index_steps_backwards(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(-1), -LEARN_STRIDE);
}

/* The MOV reloads the global on every call rather than caching it, so a base
   moved between two calls moves the answer with it. */
static void the_learn_table_base_is_read_on_every_call(void)
{
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(3), 3 * LEARN_STRIDE);
    install_learn_base(learn_base + LEARN_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_spell_learn_record(3) - learn_base),
             4 * LEARN_STRIDE);
    install_learn_base(learn_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_learn_table_base_is_not_guarded(void)
{
    install_learn_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_spell_learn_record(2),
             2 * LEARN_STRIDE);
    install_learn_base(learn_base);
}

/* Eight accessors, eight globals.  This one's is 0x00063fe8, and its two
   immediate neighbours in bss are the class-equip table's at 0x00063fe4 and
   the growth table's at 0x00063fec -- one pointer either side, which is
   exactly the confusion a single-base fixture cannot see.  Each global gets
   its own base here and each accessor must follow its own (contract B -- the
   nine table pointers are nine globals, not an array). */
static void the_learn_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    install_learn_base(learn_base);
    CHECK_EQ(learn_offset(1), LEARN_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    install_equip_base(equip_base + EQUIP_STRIDE);
    install_spell_base(spell_base + SPELL_STRIDE);
    CHECK_EQ(learn_offset(1), LEARN_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    install_learn_base(learn_base + LEARN_STRIDE);
    CHECK_EQ(equip_offset(1), EQUIP_STRIDE);
    CHECK_EQ(growth_offset(1), GROWTH_STRIDE);

    install_learn_base(learn_base);
}

/* The returned pointer addresses the file's own field offsets: all twelve
   bytes of the staged record read back through the struct's twelve fields, so
   base and stride are checked together against the layout.  The row is
   schedule 0x01's real content, and it is staged at an odd index so the record
   starts at an odd address. */
static void the_returned_learn_pointer_addresses_the_packed_record(void)
{
    struct fdps_spell_learning_record *record;

    install_learn_base(learn_base);
    stage_learn_record(1);
    record = fdps_get_spell_learn_record(1);
    CHECK_EQ(record->lv_0, 11);
    CHECK_EQ(record->spell_id_0, 0x13);
    CHECK_EQ(record->lv_1, 15);
    CHECK_EQ(record->spell_id_1, 0x06);
    CHECK_EQ(record->lv_2, 20);
    CHECK_EQ(record->spell_id_2, 0x0e);
    CHECK_EQ(record->lv_3, 25);
    CHECK_EQ(record->spell_id_3, 0x07);
    CHECK_EQ(record->lv_4, 0xff);
    CHECK_EQ(record->spell_id_4, 0xff);
    CHECK_EQ(record->lv_5, 0xff);
    CHECK_EQ(record->spell_id_5, 0xff);
    CHECK_EQ((long) ((unsigned char *) record - learn_base), LEARN_STRIDE);
}

/* The record is six two-byte pairs and the caller walks them as such: at
   0001e069 it doubles its 0..5 counter, adds it to the pointer this accessor
   returned, compares byte [pair] against the unit's level at +0x21 of the unit
   record and takes byte [pair+1] as the spell id.  So the six levels are at
   +0x00, +0x02, +0x04, +0x06, +0x08 and +0x0a and the six ids one byte behind
   each, pinned here as raw bytes through the returned pointer rather than
   through the field order this file happens to declare.  Byte +0x0c already
   belongs to the next schedule, which the last assertion states as the
   distance between two records. */
static void the_offsets_the_level_up_walk_reads_are_the_six_even_bytes(void)
{
    unsigned char *record;

    install_learn_base(learn_base);
    stage_learn_record(5);
    record = (unsigned char *) fdps_get_spell_learn_record(5);
    CHECK_EQ(record[0x00], 11);
    CHECK_EQ(record[0x01], 0x13);
    CHECK_EQ(record[0x02], 15);
    CHECK_EQ(record[0x03], 0x06);
    CHECK_EQ(record[0x04], 20);
    CHECK_EQ(record[0x05], 0x0e);
    CHECK_EQ(record[0x06], 25);
    CHECK_EQ(record[0x07], 0x07);
    CHECK_EQ(record[0x08], 0xff);
    CHECK_EQ(record[0x09], 0xff);
    CHECK_EQ(record[0x0a], 0xff);
    CHECK_EQ(record[0x0b], 0xff);
    CHECK_EQ((long) ((unsigned char *) fdps_get_spell_learn_record(6) - record),
             LEARN_STRIDE);
    CHECK_EQ((long) (record - learn_base), 5 * LEARN_STRIDE);
}

/* Every field of the record is an unsigned byte, and the value that decides it
   is 0xff: 38 of the 60 records are twelve 0xff bytes and every real record
   pads its unused pairs the same way (assets/characters.md).  Read signed, a
   level byte of 0xff would be -1 and a spell id of 0xff would index the spell
   bitmap backwards.  The caller zero-extends the id it takes -- 0001e087 XOR
   EAX,EAX / MOV AL,byte ptr [EDX+0x1] -- so 255 is the answer and -1 is not
   (contract C). */
static void the_learn_pair_bytes_are_unsigned(void)
{
    struct fdps_spell_learning_record *record;
    unsigned char *bytes;
    int field;

    install_learn_base(learn_base);
    bytes = learn_base + 40 * LEARN_STRIDE;
    for (field = 0; field < LEARN_STRIDE; field++) {
        bytes[field] = 0xff;
    }
    record = fdps_get_spell_learn_record(40);
    CHECK_EQ(record->lv_0, 255);
    CHECK_EQ(record->spell_id_0, 255);
    CHECK_EQ(record->lv_3, 255);
    CHECK_EQ(record->spell_id_5, 255);
}

/* The 0xff that FRILEVUP.DAT byte +0x0a carries for a form that learns no
   spells is the caller's business and stays the caller's: 0001e036 CMP dword
   ptr [EBP+-0x2c],0xff / JZ skips the call, and there is no CMP anywhere in
   this body.  So asking for 0xff is arithmetic like any other index -- the
   record it names lies 2,340 bytes past the end of the 720-byte table -- and a
   null return or a clamp added here would hide that rather than preserve it.
   The buffer is not indexed at 0xff; only the arithmetic is, through the null
   base, so nothing is dereferenced out there. */
static void the_ff_no_spells_sentinel_is_not_this_accessors(void)
{
    install_learn_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_spell_learn_record(0xff),
             0xff * LEARN_STRIDE);
    CHECK_EQ((long) (unsigned long) fdps_get_spell_learn_record(0xff)
             - LEARN_FILE_BYTES, 2340);
    install_learn_base(learn_base);
}

/* ------------------------------------------------------------------ *
 * fdps_get_promotion_record @ 00018c30
 *
 * The ninth accessor of the same one-block shape, and its expected values come
 * from its own assembly: IMUL EAX,dword ptr [EBP+0x14],0xc / MOV EDX,dword ptr
 * [0x00063fdc] / ADD EDX,EAX, with nothing else in the body.  The stride is
 * 0x0c -- the same as the spell-learning table's, over a different global --
 * and there is no test of any kind.  The record layout is ticket 17's struct
 * fdps_promotion_record in src/fdpstype.h and the nine records are
 * RANKUP.DAT's 108 bytes divided by that stride (resource_info/data_tables.md).
 *
 * The two facts here that are not arithmetic are both the callers': the index
 * is the unit record's char_id byte at +0x08 and not its portrait id at +0x07
 * -- 00034797, 00034cdd and 00035468 each MOV AL,byte ptr [<unit>+0x8] / AND
 * EAX,0xff / PUSH EAX -- and the choice among the four 3-byte routes inside the
 * record is made by the caller scaling a route number by three itself
 * (00034cee LEA EDX,[EDX+EDX*2] / ADD EAX,EDX).  The cases below pin that this
 * accessor does neither.
 * ------------------------------------------------------------------ */

/* What the IMUL multiplies by, the records the 108-byte file holds, and the
   file's own length. */
#define PROMO_STRIDE 0x0c
#define PROMO_COUNT 9
#define PROMO_FILE_BYTES 108

/* The byte a caller pushes is zero-extended from a unit record field, so the
   ids the game can reach run to 0xff -- past the end of a nine-record table,
   which is why the past-the-end case below is arithmetic through a null base
   rather than an index into this buffer.  A record of lead-in covers the
   negative id and a spare record the step off the back. */
static unsigned char promo_image[PROMO_STRIDE + (PROMO_COUNT + 1) * PROMO_STRIDE];

static unsigned char *promo_base = promo_image + PROMO_STRIDE;

static void install_promo_base(unsigned char *base)
{
    data_fdps_promotion_table_ptr = base;
}

static long promo_offset(int char_id)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_promotion_record(char_id);
    return (long) (record - promo_base);
}

/* A distinct byte per field, so a field picked up one offset out reads a value
   belonging to some other field, and the three bytes of a route differ from
   the three of every other route so a route index scaled by something other
   than three is visible.  The hero-badge route's portrait id is 0xff, the
   value that separates the unsigned byte the layout declares from a signed
   -1. */
static void stage_promo_record(int char_id)
{
    unsigned char *record;

    record = promo_base + char_id * PROMO_STRIDE;
    record[0x00] = 0x11;  /* default_portrait_id */
    record[0x01] = 0x12;  /* default_class_id */
    record[0x02] = 0x13;  /* default_move_bonus */
    record[0x03] = 0x21;  /* light_badge_portrait_id */
    record[0x04] = 0x22;  /* light_badge_class_id */
    record[0x05] = 0x23;  /* light_badge_move_bonus */
    record[0x06] = 0x31;  /* dark_badge_portrait_id */
    record[0x07] = 0x32;  /* dark_badge_class_id */
    record[0x08] = 0x33;  /* dark_badge_move_bonus */
    record[0x09] = 0xff;  /* hero_badge_portrait_id */
    record[0x0a] = 0x42;  /* hero_badge_class_id */
    record[0x0b] = 0x43;  /* hero_badge_move_bonus */
}

/* The ADD has no constant term, so record 0 is the table base itself -- the
   file has no header to step over. */
static void promo_record_zero_is_the_table_base(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(0), 0);
}

/* The 0xc the IMUL states.  An accessor that reached for a neighbouring
   table's stride would land at 0xb, 0xa or 0x17 here. */
static void consecutive_promo_records_are_twelve_bytes_apart(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(1), PROMO_STRIDE);
    CHECK_EQ(promo_offset(2) - promo_offset(1), PROMO_STRIDE);
    CHECK_EQ(promo_offset(8), 8 * PROMO_STRIDE);
}

/* 8 is the last id the 108-byte file has storage for, and the byte after that
   record is the file's last (resource_info/data_tables.md). */
static void the_last_real_promo_record_is_at_the_end_of_the_table(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(PROMO_COUNT - 1), (PROMO_COUNT - 1) * PROMO_STRIDE);
    CHECK_EQ(promo_offset(PROMO_COUNT - 1) + PROMO_STRIDE, PROMO_FILE_BYTES);
}

/* There is no CMP in the body, so an id past the ninth record is multiplied
   and added like any other.  The guard is the church screen's own test of the
   unit's portrait id against 9; a clamp added here would change which record
   it reads instead of protecting it.  The far case is measured through a null
   base so nothing out there is dereferenced. */
static void a_promo_char_id_past_the_end_is_not_clamped(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(PROMO_COUNT), PROMO_COUNT * PROMO_STRIDE);
    install_promo_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_promotion_record(0xff),
             0xff * PROMO_STRIDE);
    install_promo_base(promo_base);
}

/* IMUL is the signed multiply, so a negative id steps backwards off the front
   of the table rather than becoming a vast positive offset.  An unsigned
   stride would read 0xfffffff4 here. */
static void a_negative_promo_char_id_steps_backwards(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(-1), -PROMO_STRIDE);
}

/* MOV EDX,dword ptr [0x00063fdc] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_promo_table_base_is_read_on_every_call(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(3), 3 * PROMO_STRIDE);
    install_promo_base(promo_base + PROMO_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_promotion_record(3) - promo_base),
             4 * PROMO_STRIDE);
    install_promo_base(promo_base);
}

/* Nothing tests the base, so a null table -- the state before
   fdps_load_data_tables has run -- yields the offset alone as though it were
   an address.  A null guard returning NULL would be a silent behaviour
   change. */
static void a_null_promo_table_base_is_not_guarded(void)
{
    install_promo_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_promotion_record(2),
             2 * PROMO_STRIDE);
    install_promo_base(promo_base);
}

/* This accessor and fdps_get_spell_learn_record share the stride 0xc and
   differ only in the global they read -- 0x00063fdc against 0x00063fe8.  Every
   stride case above would pass with the two globals swapped, so this is the
   case that separates them: each of the nine globals gets its own base and each
   accessor must follow its own (contract B -- the nine table pointers are nine
   globals, not an array). */
static void the_promo_accessor_reads_its_own_global(void)
{
    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    install_learn_base(learn_base);
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(1), PROMO_STRIDE);

    install_learn_base(learn_base + LEARN_STRIDE);
    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    install_equip_base(equip_base + EQUIP_STRIDE);
    install_spell_base(spell_base + SPELL_STRIDE);
    CHECK_EQ(promo_offset(1), PROMO_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    install_learn_base(learn_base);
    install_promo_base(promo_base + PROMO_STRIDE);
    CHECK_EQ(learn_offset(1), LEARN_STRIDE);
    CHECK_EQ(record_offset(1), RECORD_STRIDE);

    install_promo_base(promo_base);
}

/* The returned pointer addresses the file's own field offsets: all twelve
   bytes of the staged record read back through the struct's twelve fields, so
   base and stride are checked together against the layout.  A record pointer
   right to within a byte still reads the neighbouring route's field here. */
static void the_returned_promo_pointer_addresses_the_packed_record(void)
{
    struct fdps_promotion_record *record;

    install_promo_base(promo_base);
    stage_promo_record(5);
    record = fdps_get_promotion_record(5);
    CHECK_EQ(record->default_portrait_id, 0x11);
    CHECK_EQ(record->default_class_id, 0x12);
    CHECK_EQ(record->default_move_bonus, 0x13);
    CHECK_EQ(record->light_badge_portrait_id, 0x21);
    CHECK_EQ(record->light_badge_class_id, 0x22);
    CHECK_EQ(record->light_badge_move_bonus, 0x23);
    CHECK_EQ(record->dark_badge_portrait_id, 0x31);
    CHECK_EQ(record->dark_badge_class_id, 0x32);
    CHECK_EQ(record->dark_badge_move_bonus, 0x33);
    CHECK_EQ(record->hero_badge_class_id, 0x42);
    CHECK_EQ(record->hero_badge_move_bonus, 0x43);
    CHECK_EQ((long) ((unsigned char *) record - promo_base), 5 * PROMO_STRIDE);
}

/* The record is four three-byte routes and the caller addresses them as such:
   at 00034cee it takes its own route number, triples it with LEA
   EDX,[EDX+EDX*2], adds it to the pointer this accessor returned, then reads
   byte [route] as the new portrait id and byte [route+1] as the new class
   code.  So the four routes start at +0x00, +0x03, +0x06 and +0x09, pinned
   here as raw bytes through the returned pointer rather than through the field
   order this file happens to declare.  The accessor itself performs none of
   that scaling: a triple folded in here would move every route but route 0. */
static void the_four_routes_are_three_bytes_apart_from_the_record_base(void)
{
    unsigned char *record;

    install_promo_base(promo_base);
    stage_promo_record(6);
    record = (unsigned char *) fdps_get_promotion_record(6);
    CHECK_EQ((long) (record - promo_base), 6 * PROMO_STRIDE);
    CHECK_EQ(record[0 * 3 + 0], 0x11);
    CHECK_EQ(record[0 * 3 + 1], 0x12);
    CHECK_EQ(record[1 * 3 + 0], 0x21);
    CHECK_EQ(record[1 * 3 + 1], 0x22);
    CHECK_EQ(record[2 * 3 + 0], 0x31);
    CHECK_EQ(record[2 * 3 + 1], 0x32);
    CHECK_EQ(record[3 * 3 + 0], 0xff);
    CHECK_EQ(record[3 * 3 + 1], 0x42);
    CHECK_EQ((long) ((unsigned char *) fdps_get_promotion_record(7) - record),
             PROMO_STRIDE);
}

/* Every field of the record is an unsigned byte.  The caller zero-extends both
   bytes it takes -- 00034cf9 XOR EAX,EAX / MOV AL,byte ptr [EDX] for the
   portrait id -- so 0xff reads 255 and not -1, which is the difference between
   a sprite name of Stand255.saf and an index four gigabytes down
   (contract C). */
static void the_promo_record_bytes_are_unsigned(void)
{
    struct fdps_promotion_record *record;

    install_promo_base(promo_base);
    stage_promo_record(5);
    record = fdps_get_promotion_record(5);
    CHECK_EQ(record->hero_badge_portrait_id, 255);
    CHECK_EQ(((unsigned char *) record)[0x09], 255);
}

/* The index is the unit record's char_id byte at +0x08, which a promotion
   leaves alone, and not the portrait id at +0x07, which a promotion
   overwrites: 00034c10 writes route byte 0 into +0x07 after this call.  The
   nine records are the nine promotable characters, char_ids 0..8, so this is
   the case that states the mapping the assembly's MOV AL,byte ptr [<unit>+0x8]
   fixes -- an accessor fed the portrait id of an already-promoted unit would
   read a record outside the table. */
static void a_char_id_indexes_its_promotion_record_unbiased(void)
{
    install_promo_base(promo_base);
    CHECK_EQ(promo_offset(0x00), 0);
    CHECK_EQ(promo_offset(0x01), PROMO_STRIDE);
    CHECK_EQ(promo_offset(0x08), (PROMO_COUNT - 1) * PROMO_STRIDE);
}

/* ------------------------------------------------------------------ *
 * fdps_get_roster_record @ 00023950
 *
 * The same one-block shape as the nine table accessors, over the party roster
 * instead of a file table: IMUL EAX,dword ptr [EBP+0x14],0x50 / MOV EDX,dword
 * ptr [0x00064108] / ADD EDX,EAX, with nothing else in the body.  Expected
 * values come from that assembly, from the allocation at 000296b8 in
 * fdps_load_global_resources -- PUSH 0xa00 / CALL malloc / MOV
 * [0x00064108],EAX -- and from the field widths the callers state at 000335a7,
 * 000335ba, 000335cd (MOVSX word) and at 00039ec6, 00039ee0, 00039eee,
 * 00039ef7 (word and byte).  The record layout is ticket 17's struct
 * fdps_unit_record in src/fdpstype.h.
 *
 * The roster is not one of the nine tables and is not read out of the VFS
 * container: it is a single heap block that lives for the whole run.  A staged
 * byte buffer is still exactly the shape the global holds, and nothing here
 * asserts what a real roster contains -- every byte read back is one this file
 * wrote.
 * ------------------------------------------------------------------ */

/* The stride the IMUL states, and the block the allocation sizes: 0xa00 bytes
   is exactly 32 records, which is the roster's capacity. */
#define ROSTER_STRIDE 0x50
#define ROSTER_CAPACITY 32
#define ROSTER_BLOCK_BYTES 0xa00

/* A record of lead-in in front of the base, so the negative-index case -- which
   the signed IMUL makes reachable -- lands on real storage, and one spare
   record behind the block for the step off the back. */
static unsigned char roster_image[ROSTER_STRIDE
                                  + (ROSTER_CAPACITY + 1) * ROSTER_STRIDE];

static unsigned char *roster_base = roster_image + ROSTER_STRIDE;

static void install_roster_base(unsigned char *base)
{
    data_fdps_roster_array_ptr = base;
}

static long roster_offset(int roster_index)
{
    unsigned char *record;

    record = (unsigned char *) fdps_get_roster_record(roster_index);
    return (long) (record - roster_base);
}

/* Writes the one record the field cases read, a distinct value per field the
   callers touch so a field picked up from a neighbouring offset reads a value
   belonging to something else.  Byte at a time and little-endian, because the
   three base stats sit at the ODD offsets 0x37, 0x39 and 0x3e and this fixture
   must not assume the layout it is checking. */
static void stage_roster_record(int roster_index)
{
    unsigned char *record;

    record = roster_base + roster_index * ROSTER_STRIDE;
    record[0x05] = 0x03;  /* flags */
    record[0x07] = 0x0b;  /* portrait_id */
    record[0x08] = 0x07;  /* char_id */
    record[0x20] = 0x12;  /* clazz */
    record[0x21] = 0x2f;  /* level */
    record[0x37] = 0x34;  /* ap_base low   -- 0x1234 */
    record[0x38] = 0x12;  /* ap_base high */
    record[0x39] = 0xce;  /* dp_base low   -- 0xffce, i.e. -50 signed */
    record[0x3a] = 0xff;  /* dp_base high */
    record[0x3e] = 0x2c;  /* dx_base low   -- 0x012c, i.e. 300 */
    record[0x3f] = 0x01;  /* dx_base high */
    record[0x40] = 0x00;  /* hp_current low  -- 0, the fallen state */
    record[0x41] = 0x00;  /* hp_current high */
    record[0x42] = 0xc8;  /* hp_max low    -- 200 */
    record[0x43] = 0x00;  /* hp_max high */
    record[0x44] = 0x0a;  /* mp_current low  -- 10 */
    record[0x45] = 0x00;  /* mp_current high */
    record[0x46] = 0x1e;  /* mp_max low    -- 30 */
    record[0x47] = 0x00;  /* mp_max high */
    record[0x48] = 0x41;  /* ap low        -- 0x41 */
    record[0x49] = 0x00;  /* ap high */
    record[0x4a] = 0x42;  /* dp low        -- 0x42 */
    record[0x4b] = 0x00;  /* dp high */
    record[0x4c] = 0x43;  /* hit low       -- 0x43 */
    record[0x4d] = 0x00;  /* hit high */
    record[0x4e] = 0x44;  /* ev low        -- 0x44 */
    record[0x4f] = 0x00;  /* ev high */
}

/* The ADD has no constant term, so member 0 is the array base itself: the
   roster has no header record in front of it, unlike PROMAP.DAT's default
   row. */
static void roster_record_zero_is_the_array_base(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(0), 0);
}

/* The 0x50 the IMUL states, which is sizeof(struct fdps_unit_record) only for
   as long as the record stays byte-packed: with the pack pragma lost the three
   odd-offset stats at 0x37, 0x39 and 0x3e would pad it to 0x52 or 0x54 and
   every member from 1 on would be read at the wrong address. */
static void consecutive_roster_records_are_eighty_bytes_apart(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(1), ROSTER_STRIDE);
    CHECK_EQ(roster_offset(2) - roster_offset(1), ROSTER_STRIDE);
    CHECK_EQ(roster_offset(13), 13 * ROSTER_STRIDE);
}

/* The allocation at 000296b8 asks for 0xa00 bytes, so member 31 is the last one
   the block has storage for and the byte after its record is the end of the
   block.  This is the case that ties the stride to the allocation: a stride of
   0x52 would put member 31 past the end of what malloc was asked for. */
static void the_last_roster_member_is_at_the_end_of_the_block(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(ROSTER_CAPACITY - 1),
             (ROSTER_CAPACITY - 1) * ROSTER_STRIDE);
    CHECK_EQ(roster_offset(ROSTER_CAPACITY - 1) + ROSTER_STRIDE,
             ROSTER_BLOCK_BYTES);
}

/* There is no CMP in the body at all -- in particular none against
   data_fdps_roster_member_count at 0x00064114, which is what the callers
   themselves compare their loop counters with (00039e9f, 00039f77).  An index
   past the occupied members, or past the 32 the block holds, is multiplied and
   added like any other.  The far case is measured through a null base so
   nothing out there is dereferenced. */
static void a_roster_index_past_the_end_is_not_clamped(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(ROSTER_CAPACITY), ROSTER_CAPACITY * ROSTER_STRIDE);
    install_roster_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_roster_record(100),
             100 * ROSTER_STRIDE);
    install_roster_base(roster_base);
}

/* IMUL is the signed multiply, so a negative index steps backwards off the
   front of the array rather than becoming a vast positive offset.  An unsigned
   stride would read 0xffffffb0 here. */
static void a_negative_roster_index_steps_backwards(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(-1), -ROSTER_STRIDE);
}

/* MOV EDX,dword ptr [0x00064108] is inside the body: the base is re-read on
   every call, so moving the global moves every answer.  A transcription that
   cached it in a file-scope copy passes everything above and fails here. */
static void the_roster_base_is_read_on_every_call(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(3), 3 * ROSTER_STRIDE);
    install_roster_base(roster_base + ROSTER_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_roster_record(3) - roster_base),
             4 * ROSTER_STRIDE);
    install_roster_base(roster_base);
}

/* Nothing tests the base, so a null roster -- the state before
   fdps_load_global_resources has allocated it -- yields the offset alone as
   though it were an address.  A null guard returning NULL would be a silent
   behaviour change. */
static void a_null_roster_base_is_not_guarded(void)
{
    install_roster_base((unsigned char *) 0);
    CHECK_EQ((long) (unsigned long) fdps_get_roster_record(2),
             2 * ROSTER_STRIDE);
    install_roster_base(roster_base);
}

/* The roster and the map unit array hold the same record type at the same
   0x50 stride and differ only in the global they read -- 0x00064108 against
   0x00069cd8.  Every stride case above would pass with the two swapped, so
   this is the case that separates them: the accessor must follow the roster
   base while the map array's is moved out from under it, and follow it back
   when only the roster base moves.  They are two globals with two lifetimes,
   not one array (contract B).  The map global is put back to null on the way
   out, because the other test units in this build read it. */
static void the_roster_accessor_reads_the_roster_base(void)
{
    install_roster_base(roster_base);
    data_fdps_map_unit_array_ptr = roster_base;
    CHECK_EQ(roster_offset(1), ROSTER_STRIDE);

    data_fdps_map_unit_array_ptr = roster_base + 7 * ROSTER_STRIDE;
    CHECK_EQ(roster_offset(1), ROSTER_STRIDE);

    install_roster_base(roster_base + ROSTER_STRIDE);
    CHECK_EQ((long) ((unsigned char *) fdps_get_roster_record(1) - roster_base),
             2 * ROSTER_STRIDE);

    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
    install_roster_base(roster_base);
}

/* The nine table accessors each read their own one of nine adjacent bss
   pointers; this one reads a tenth global that is nowhere near them, so a base
   confusion would show as an answer that moves when one of those nine moves.
   Every table base is displaced by a record and the roster answer must not
   budge. */
static void the_roster_accessor_reads_none_of_the_table_bases(void)
{
    install_roster_base(roster_base);
    CHECK_EQ(roster_offset(2), 2 * ROSTER_STRIDE);

    install_base(table_base + RECORD_STRIDE);
    install_growth_base(growth_base + GROWTH_STRIDE);
    install_enemy_base(enemy_base + ENEMY_STRIDE);
    install_item_base(item_base + ITEM_STRIDE);
    install_class_base(class_base + CLASS_STRIDE);
    install_equip_base(equip_base + EQUIP_STRIDE);
    install_spell_base(spell_base + SPELL_STRIDE);
    install_learn_base(learn_base + LEARN_STRIDE);
    install_promo_base(promo_base + PROMO_STRIDE);
    CHECK_EQ(roster_offset(2), 2 * ROSTER_STRIDE);

    install_base(table_base);
    install_growth_base(growth_base);
    install_enemy_base(enemy_base);
    install_item_base(item_base);
    install_class_base(class_base);
    install_equip_base(equip_base);
    install_spell_base(spell_base);
    install_learn_base(learn_base);
    install_promo_base(promo_base);
}

/* The returned pointer addresses the record's own field offsets: the staged
   bytes read back through the struct's fields, so base and stride are checked
   together against the layout.  A record pointer right to within a byte still
   reads a neighbouring field here, and a record that padded past 0x50 would
   put every field from ap_base on somewhere else. */
static void the_returned_roster_pointer_addresses_the_packed_record(void)
{
    struct fdps_unit_record *record;

    install_roster_base(roster_base);
    stage_roster_record(9);
    record = fdps_get_roster_record(9);
    CHECK_EQ(record->flags, 0x03);
    CHECK_EQ(record->portrait_id, 0x0b);
    CHECK_EQ(record->char_id, 0x07);
    CHECK_EQ(record->clazz, 0x12);
    CHECK_EQ(record->level, 0x2f);
    CHECK_EQ(record->hp_current, 0);
    CHECK_EQ(record->hp_max, 200);
    CHECK_EQ(record->mp_current, 10);
    CHECK_EQ(record->mp_max, 30);
    CHECK_EQ(record->ap, 0x41);
    CHECK_EQ(record->dp, 0x42);
    CHECK_EQ(record->hit, 0x43);
    CHECK_EQ(record->ev, 0x44);
    CHECK_EQ((long) ((unsigned char *) record - roster_base),
             9 * ROSTER_STRIDE);
}

/* The three base stats live at the odd offsets 0x37, 0x39 and 0x3e and are
   signed: fdps_roster_preview_combat_stats_with_item takes all three with
   MOVSX -- 000335a7, 000335ba, 000335cd -- and adds an item's own signed
   modifier to each.  So 0xffce here is -50, not 65486, and the preview it
   feeds shows a weaker weapon as a drop rather than as a vast gain
   (contract C). */
static void the_roster_base_stats_are_signed_and_unaligned(void)
{
    struct fdps_unit_record *record;
    unsigned char *bytes;

    install_roster_base(roster_base);
    stage_roster_record(9);
    record = fdps_get_roster_record(9);
    CHECK_EQ(record->ap_base, 0x1234);
    CHECK_EQ(record->dp_base, -50);
    CHECK_EQ(record->dx_base, 300);

    bytes = (unsigned char *) record;
    CHECK_EQ((long) ((unsigned char *) &record->ap_base - bytes), 0x37);
    CHECK_EQ((long) ((unsigned char *) &record->dp_base - bytes), 0x39);
    CHECK_EQ((long) ((unsigned char *) &record->dx_base - bytes), 0x3e);
}

/* The offsets the two callers read through the returned pointer, pinned as raw
   bytes rather than through the field order this file happens to declare.
   fdps_roster_revive_fallen_members tests the current-HP word at +0x40 for
   zero (00039ec6), refills it from the maximum at +0x42 (00039ee0), clears the
   flags byte at +0x05 (00039eee) and zero-extends the class at +0x20 and the
   level at +0x21 (00039ef7, 00039f02); fdps_shop_draw_member_entry reads the
   char_id at +0x08 (00033362) and the four derived stats at +0x48, +0x4a,
   +0x4c and +0x4e (0003347f, 000334db, 0003340a, 000333ae).  The accessor
   itself performs none of that: an offset folded in here would move every one
   of them. */
static void the_offsets_the_roster_callers_read(void)
{
    unsigned char *record;

    install_roster_base(roster_base);
    stage_roster_record(4);
    record = (unsigned char *) fdps_get_roster_record(4);
    CHECK_EQ((long) (record - roster_base), 4 * ROSTER_STRIDE);
    CHECK_EQ(record[0x05], 0x03);
    CHECK_EQ(record[0x08], 0x07);
    CHECK_EQ(record[0x20], 0x12);
    CHECK_EQ(record[0x21], 0x2f);
    CHECK_EQ(record[0x40], 0x00);
    CHECK_EQ(record[0x42], 0xc8);
    CHECK_EQ(record[0x48], 0x41);
    CHECK_EQ(record[0x4a], 0x42);
    CHECK_EQ(record[0x4c], 0x43);
    CHECK_EQ(record[0x4e], 0x44);
    CHECK_EQ((long) ((unsigned char *) fdps_get_roster_record(5) - record),
             ROSTER_STRIDE);
}

/* The write-back loop at 00023980 forms the very same address inline -- IMUL
   EAX,dword ptr [EBP+-0x18],0x50 / MOV EDX,dword ptr [0x00064108] / ADD EDX,EAX
   at 000239ed -- rather than calling this accessor, and so does the credit-roll
   loop at 00039f8d.  The two must agree for every index, because they address
   the same array: this states that they do, so a stride or base changed in one
   place shows up here. */
static void the_inline_roster_arithmetic_agrees_with_the_accessor(void)
{
    int index;

    install_roster_base(roster_base);
    for (index = 0; index < ROSTER_CAPACITY; index++)
    {
        CHECK_EQ((long) ((unsigned char *) fdps_get_roster_record(index)
                         - data_fdps_roster_array_ptr),
                 (long) index * ROSTER_STRIDE);
    }
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

    RUN_TEST(equip_record_zero_is_the_table_base);
    RUN_TEST(consecutive_equip_records_are_six_bytes_apart);
    RUN_TEST(the_last_real_equip_record_is_at_the_end_of_the_table);
    RUN_TEST(a_class_code_indexes_its_equip_record_unbiased);
    RUN_TEST(a_class_code_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_class_index_steps_back_in_the_equip_table);
    RUN_TEST(the_equip_table_base_is_read_on_every_call);
    RUN_TEST(a_null_equip_table_base_is_not_guarded);
    RUN_TEST(the_equip_accessor_reads_its_own_global);
    RUN_TEST(the_returned_equip_pointer_addresses_the_six_positions);
    RUN_TEST(the_caller_scans_offsets_zero_through_five);

    RUN_TEST(spell_record_zero_is_the_table_base);
    RUN_TEST(consecutive_spell_records_are_seven_bytes_apart);
    RUN_TEST(the_last_real_spell_record_is_at_the_end_of_the_table);
    RUN_TEST(a_spell_id_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_spell_id_steps_backwards);
    RUN_TEST(the_spell_table_base_is_read_on_every_call);
    RUN_TEST(a_null_spell_table_base_is_not_guarded);
    RUN_TEST(the_spell_accessor_reads_its_own_global);
    RUN_TEST(the_returned_spell_pointer_addresses_the_packed_record);
    RUN_TEST(the_spell_power_is_a_signed_word);
    RUN_TEST(the_spell_byte_fields_are_unsigned);
    RUN_TEST(the_offsets_the_spell_callers_read_are_zero_two_and_five);
    RUN_TEST(the_status_effect_substitution_is_not_this_accessors);

    RUN_TEST(learn_record_zero_is_the_table_base);
    RUN_TEST(consecutive_learn_records_are_twelve_bytes_apart);
    RUN_TEST(the_last_real_learn_record_is_at_the_end_of_the_table);
    RUN_TEST(a_learn_index_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_learn_index_steps_backwards);
    RUN_TEST(the_learn_table_base_is_read_on_every_call);
    RUN_TEST(a_null_learn_table_base_is_not_guarded);
    RUN_TEST(the_learn_accessor_reads_its_own_global);
    RUN_TEST(the_returned_learn_pointer_addresses_the_packed_record);
    RUN_TEST(the_offsets_the_level_up_walk_reads_are_the_six_even_bytes);
    RUN_TEST(the_learn_pair_bytes_are_unsigned);
    RUN_TEST(the_ff_no_spells_sentinel_is_not_this_accessors);

    RUN_TEST(promo_record_zero_is_the_table_base);
    RUN_TEST(consecutive_promo_records_are_twelve_bytes_apart);
    RUN_TEST(the_last_real_promo_record_is_at_the_end_of_the_table);
    RUN_TEST(a_promo_char_id_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_promo_char_id_steps_backwards);
    RUN_TEST(the_promo_table_base_is_read_on_every_call);
    RUN_TEST(a_null_promo_table_base_is_not_guarded);
    RUN_TEST(the_promo_accessor_reads_its_own_global);
    RUN_TEST(the_returned_promo_pointer_addresses_the_packed_record);
    RUN_TEST(the_four_routes_are_three_bytes_apart_from_the_record_base);
    RUN_TEST(the_promo_record_bytes_are_unsigned);
    RUN_TEST(a_char_id_indexes_its_promotion_record_unbiased);

    RUN_TEST(roster_record_zero_is_the_array_base);
    RUN_TEST(consecutive_roster_records_are_eighty_bytes_apart);
    RUN_TEST(the_last_roster_member_is_at_the_end_of_the_block);
    RUN_TEST(a_roster_index_past_the_end_is_not_clamped);
    RUN_TEST(a_negative_roster_index_steps_backwards);
    RUN_TEST(the_roster_base_is_read_on_every_call);
    RUN_TEST(a_null_roster_base_is_not_guarded);
    RUN_TEST(the_roster_accessor_reads_the_roster_base);
    RUN_TEST(the_roster_accessor_reads_none_of_the_table_bases);
    RUN_TEST(the_returned_roster_pointer_addresses_the_packed_record);
    RUN_TEST(the_roster_base_stats_are_signed_and_unaligned);
    RUN_TEST(the_offsets_the_roster_callers_read);
    RUN_TEST(the_inline_roster_arithmetic_agrees_with_the_accessor);

    /* Put the globals back the way they were found.  They are null until
       ticket 23 defines them, and leaving a pointer to this file's static
       buffers in one would hand the next unit an address it has no business
       holding. */
    install_base((unsigned char *) 0);
    install_growth_base((unsigned char *) 0);
    install_enemy_base((unsigned char *) 0);
    install_item_base((unsigned char *) 0);
    install_class_base((unsigned char *) 0);
    install_equip_base((unsigned char *) 0);
    install_spell_base((unsigned char *) 0);
    install_learn_base((unsigned char *) 0);
    install_promo_base((unsigned char *) 0);

    /* Same for the roster base and for the map unit array the contract-B case
       borrowed: both are null until ticket 23 defines them, and the units that
       walk the map array must not find this file's buffer in it. */
    install_roster_base((unsigned char *) 0);
    data_fdps_map_unit_array_ptr = (unsigned char *) 0;
}
