/* tests/savefile.c -- cover for src/savefile.c.
 *
 * Expected values come from the assembly at 00056898 and 000568b7 -- SUB
 * ECX,0x4, XOR EAX,EAX before a LODSB that writes only AL, ADD EBX,EAX, and
 * for the cipher MOV DX,0xa5 / ADD DX,0x9014 / ROL DX,3 / XOR AL,DL / STOSB,
 * both loops closed by LOOP -- and from the shipped FDE.SAV itself.  None of
 * them is read off the emitted C.
 *
 * fdps_load_savegame at 00023e20 is covered at the end of the file and NO CASE
 * CALLS IT.  Three of the four things it does on the way out are unbounded in
 * an unattended run: fdps_render_view_frame holds on
 * data_fdps_view_frame_last_tick until the timer moves, the "PlyPhase.saf"
 * banner holds the same way once per fade step and once per frame, and
 * fdps_cd_verify_disc_and_play_track has no exit but the right disc -- a
 * machine without the image reaches a modal getch nothing comes back from
 * (cdaudio.h).  Reaching them at all first needs the whole game up:
 * fdps_load_global_resources for the sheets and the font the view frame draws
 * through, and a chapter's field resources for the scene it composes from.
 * What that costs is not the reason no case calls it -- the reason is that the
 * result would be a cover that stands down on any machine without the disc
 * mounted, and that installing a whole battle over the live globals would
 * leave every test unit sorting after this one reading state this one wrote.
 * Confirming the installed battle is a playtest (ADR-0003) and is recorded as
 * such in the emit issues.
 *
 * WHAT THE CASES BELOW DO INSTEAD is hold the loader's map of the file against
 * the file itself.  Every offset it reads is one fdps_battle_system_submenu
 * writes with the same literal -- ADD EAX,0x8a3 at 00015177 and 00023f09, ADD
 * EAX,0x12a3 at 00015196 and 00024043, ADD EAX,0x30a3 at 000151af and
 * 0002405c, ADD EAX,0x30c3 at 000151c0 and 00023ea8 -- so the two sides of the
 * contract are checkable against a save the game actually wrote, and a wrong
 * offset or a wrong stride reads bytes that cannot pass.
 *
 * The checksum's buffers are staged here rather than read from a game file
 * because that function takes its entire input from its two arguments: it
 * reads no global and opens nothing.  The cipher is the routine that makes the
 * real file usable as a witness, and the last case does exactly that -- it
 * decrypts the shipped FDE.SAV and hands the plaintext to the checksum, which
 * has to agree with the dword the file itself stores at +0x59c7.  That single
 * assertion exercises the whole keystream over 22,987 bytes against a file the
 * game wrote, and neither routine can be wrong for it to hold.
 */
#include <stdio.h>
#include <stddef.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "testharn.h"
#include "savefile.h"

/* 300 summed bytes plus the four skipped ones: 300 * 0xff overflows 16 bits,
   which is what the width case needs. */
#define WIDE_BYTES 304

static unsigned char stage_small[12];
static unsigned char stage_wide[WIDE_BYTES];

/* SUB ECX,0x4 at 000568a5.  Eight bytes summing to 36 followed by four 0xff
   bytes: the trailing four are the stored checksum field and must not enter
   the sum that gets compared against them.  Summing the buffer whole would
   give 36 + 4 * 0xff = 1056. */
static void trailing_four_bytes_are_not_summed(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        stage_small[i] = (unsigned char) (i + 1);
    }
    for (i = 8; i < 12; i++) {
        stage_small[i] = 0xff;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 36);
}

/* The boundary itself: the byte at size - 4 is the first one left out, and the
   byte at size - 5 is the last one taken in.  One 0x7f planted on either side
   of the line separates a count of size - 4 from a count of size - 3 or
   size - 5, which the case above cannot. */
static void the_skip_starts_exactly_at_size_minus_four(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[8] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0);

    stage_small[8] = 0x00;
    stage_small[7] = 0x7f;
    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 0x7f);
}

/* XOR EAX,EAX at 000568aa followed by a LODSB that writes AL alone: bytes
   enter the sum zero-extended.  0x80 + 0xff + 0x80 + 0xff is 766 unsigned and
   -258 if the image were read as signed char. */
static void bytes_are_summed_zero_extended(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = 0x00;
    }
    stage_small[0] = 0x80;
    stage_small[1] = 0xff;
    stage_small[2] = 0x80;
    stage_small[3] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 12), 766);
}

/* ADD EBX,EAX is a 32-bit add into a 32-bit accumulator.  300 bytes of 0xff
   sum to 76500, which does not fit in 16 bits: a narrower accumulator would
   report 76500 - 65536 = 10964. */
static void the_accumulator_is_thirty_two_bits_wide(void)
{
    int i;

    for (i = 0; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0xff;
    }
    for (i = WIDE_BYTES - 4; i < WIDE_BYTES; i++) {
        stage_wide[i] = 0x00;
    }

    CHECK_EQ(fdps_compute_save_checksum(stage_wide, WIDE_BYTES), 76500L);
}

/* The shortest walk the loop can take without wrapping its count: size 5 sums
   exactly one byte.  LOOP tests after the body, so a size of 4 would walk 2^32
   bytes instead of none -- that one is described in save.c and deliberately
   not exercised here. */
static void size_five_sums_exactly_one_byte(void)
{
    stage_small[0] = 0x2a;
    stage_small[1] = 0xff;
    stage_small[2] = 0xff;
    stage_small[3] = 0xff;
    stage_small[4] = 0xff;

    CHECK_EQ(fdps_compute_save_checksum(stage_small, 5), 0x2a);
}

/* LODSB alone: there is no STOSB in this routine, unlike fdps_xor_crypt_buffer
   next to it, so the image comes back untouched.  A caller that computed the
   checksum over a buffer it was about to write out would otherwise be shipping
   whatever the routine had scribbled. */
static void the_image_is_not_written_to(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        stage_small[i] = (unsigned char) (0x11 * (i + 1));
    }

    fdps_compute_save_checksum(stage_small, 12);

    CHECK_EQ(stage_small[0], 0x11);
    CHECK_EQ(stage_small[7], 0x88);
    CHECK_EQ(stage_small[11], (unsigned char) (0x11 * 12));
}

/* ------------------------------------------------------ fdps_xor_crypt_buffer

   The first eight keystream bytes, worked out by hand from the four
   instructions that make one and confirmed byte for byte against the shipped
   file: FDE.SAV opens with cc 01 b7 53 on disc and 00 01 16 ff once decrypted,
   so its first four keystream bytes are cc 00 a1 ac.

   The very first byte is the load-bearing one.  0xcc is rol16(0xa5 + 0x9014,
   3) & 0xff, and it pins two things at once that no later byte can separate:
   that the key is advanced before the XOR rather than after -- the 0xa5 seed
   is never applied to anything -- and that the rotate is sixteen bits wide.  A
   32-bit key rotating a preserved carry would give 0xc8 here. */
#define KEYSTREAM_LENGTH 8

static const unsigned char keystream[KEYSTREAM_LENGTH] = {
    0xcc, 0x00, 0xa1, 0xac, 0x06, 0xd1, 0x2c, 0x04
};

static unsigned char crypt_stage[16];

/* Crypting a run of zeroes copies the keystream out where it can be read. */
static void the_keystream_is_the_low_half_of_the_rotating_key(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0x00;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], keystream[i]);
    }
}

/* The keystream is a function of the byte index alone -- nothing in the loop
   feeds a data byte back into DX.  A buffer of 0xff must therefore come out as
   the same keystream inverted, byte for byte, as the buffer of zeroes above.
   If the cipher were chained on its own output the two would diverge from the
   second byte on. */
static void the_keystream_does_not_depend_on_the_data(void)
{
    int i;

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        crypt_stage[i] = 0xff;
    }

    fdps_xor_crypt_buffer(crypt_stage, KEYSTREAM_LENGTH);

    for (i = 0; i < KEYSTREAM_LENGTH; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0xff ^ keystream[i]));
    }
}

/* The one property the game depends on: the same routine encrypts and
   decrypts, because the key is reseeded to 0xa5 on entry and the keystream
   restarts with it.  Both save call sites and both load call sites call this
   function and there is no second one. */
static void crypting_twice_restores_the_buffer(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        crypt_stage[i] = (unsigned char) (0x37 * i + 0x5a);
    }

    fdps_xor_crypt_buffer(crypt_stage, 16);
    fdps_xor_crypt_buffer(crypt_stage, 16);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(crypt_stage[i], (unsigned char) (0x37 * i + 0x5a));
    }
}

/* ECX is the byte count, and STOSB writes one byte per pass: a length of 1
   touches the first byte and stops.  The neighbour is checked because the
   LOOP-after-body shape makes an off-by-one here write past the end rather
   than fall short. */
static void the_length_is_a_byte_count(void)
{
    crypt_stage[0] = 0x00;
    crypt_stage[1] = 0x00;
    crypt_stage[2] = 0x00;

    fdps_xor_crypt_buffer(crypt_stage, 1);

    CHECK_EQ(crypt_stage[0], keystream[0]);
    CHECK_EQ(crypt_stage[1], 0x00);
    CHECK_EQ(crypt_stage[2], 0x00);
}

/* The shipped save, and the only witness that covers the whole keystream.
   FDE.SAV is 0x59cb bytes, which is the length all eight call sites push. */
#define SAVE_NAME "FDE.SAV"
#define SAVE_IMAGE_SIZE 0x59cbL
#define SAVE_CHECKSUM_OFFSET 0x59c7L
#define SAVE_STORED_CHECKSUM 0x002dedc4L

static unsigned char save_image[0x59cb];

static int load_shipped_save(void)
{
    FILE *fp;
    size_t got;

    fp = fopen(SAVE_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(save_image, 1, (size_t) SAVE_IMAGE_SIZE, fp);
    fclose(fp);
    return got == (size_t) SAVE_IMAGE_SIZE;
}

/* Decrypt the file the game wrote and let it check itself.  The dword at
   +0x59c7 is the checksum the game stored when it saved, and
   fdps_compute_save_checksum over the plaintext has to reproduce it: 22,983
   keystream bytes all have to be right for that sum to land, and the stored
   dword is four more.  The opening bytes are asserted separately so that a
   failure says whether the keystream went wrong at the start or somewhere in
   the middle.

   The case skips itself when the file is not staged, the way the container
   cases in tests/rsrc.c do: there is nothing to assert against and a
   fabricated stand-in would only be asserting against bytes this test
   wrote. */
static void the_shipped_save_decrypts_to_its_own_checksum(void)
{
    unsigned long stored_checksum;

    if (!load_shipped_save()) {
        return;
    }

    fdps_xor_crypt_buffer(save_image, (unsigned int) SAVE_IMAGE_SIZE);

    stored_checksum =
        (unsigned long) save_image[SAVE_CHECKSUM_OFFSET]
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 1] << 8)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 2] << 16)
        | ((unsigned long) save_image[SAVE_CHECKSUM_OFFSET + 3] << 24);

    CHECK_EQ(save_image[0], 0x00);
    CHECK_EQ(save_image[1], 0x01);
    CHECK_EQ(save_image[2], 0x16);
    CHECK_EQ(save_image[3], 0xff);
    CHECK_EQ(stored_checksum, SAVE_STORED_CHECKSUM);
    CHECK_EQ(fdps_compute_save_checksum(save_image,
                                        (unsigned int) SAVE_IMAGE_SIZE),
             stored_checksum);
}

/* ------------------------------------------------------- fdps_load_savegame

   The first 0x312b bytes of the image are the battle resume region, and every
   number below is one of the loader's own displacements read off the assembly:
   the four ADD EAX,<offset> at 00023f09, 00024043, 0002405c and 00023ea8 for
   where the blocks start, PUSH 0xa00 at 00023f01, PUSH 0x1e00 at 00024005,
   PUSH 0x20 at 00024057 and PUSH 0x8a3 at 00023f70 for how long they are, and
   IMUL ...,0x50 at 00024038 for the unit stride.  fdps_battle_system_submenu
   writes the same file with the same displacements, which is what makes them a
   contract rather than one function's opinion. */
#define RESUME_FIELD_BLOCK_AT 0x0000L
#define RESUME_FIELD_BLOCK_BYTES 0x08a3L
#define RESUME_ROSTER_AT 0x08a3L
#define RESUME_ROSTER_BYTES 0x0a00L
#define RESUME_UNITS_AT 0x12a3L
#define RESUME_UNITS_BYTES 0x1e00L
#define RESUME_UNIT_STRIDE 0x50L
#define RESUME_FLAGS_AT 0x30a3L
#define RESUME_FLAGS_BYTES 0x20L
#define RESUME_HEADER_AT 0x30c3L

/* Where the four slot records begin, ADD EDX,0x312b at 00024929 in save.c's
   half of the file: the resume region has to end before it. */
#define SAVE_SLOTS_AT 0x312bL

/* The offsets inside the scalar header, from the MOV AL,byte ptr [EAX + n]
   chain at 00024105, 00023ff8, 00023f50, 00024114, 00024127, 0002413a,
   0002414d, 00024160, 00024170, 0002417b, 00024186, 00024191 and 0002419c. */
#define AT_TURN 0x00
#define AT_UNIT_COUNT 0x01
#define AT_CHAPTER 0x02
#define AT_VIEW_TILE_X 0x03
#define AT_VIEW_TILE_Y 0x04
#define AT_CURSOR_TILE_X 0x05
#define AT_CURSOR_TILE_Y 0x06
#define AT_ROSTER_MEMBERS 0x09
#define AT_PARTY_GOLD 0x0a
#define AT_BATTLE_ANIM 0x0e
#define AT_TERRAIN_HUD 0x0f
#define AT_BGM 0x10
#define AT_SFX 0x11

/* Inside the field block, from MOV AL,byte ptr [EAX + 0x1] at 00023fbf and
   [EAX + 0x2] at 00023fd1. */
#define AT_PLAYER_SLOT_COUNT 0x01
#define AT_CHAR_SPAWN_COUNT 0x02

/* Inside a unit record: byte 2 is the sprite cache slot the loader overwrites
   with what fdps_cache_cel_sprite_group hands back, byte 7 is the group id it
   asks for.  MOV AL,byte ptr [EAX + 0x7] at 000240d4 and MOV byte ptr
   [EDX + 0x2],AL at 000240f1; the same two bytes are sprite_cache_slot and
   portrait_id of struct fdps_unit_record. */
#define AT_UNIT_SPRITE_SLOT 0x02
#define AT_UNIT_GROUP_ID 0x07

/* IMUL EAX,EAX,0x18 behind each of the four coordinate reads. */
#define RESUME_TILE_SIZE 0x18

/* ICON.CEL holds 1920 sprites in 160 groups of twelve (tests/gamefile.lst), so
   a group id past 159 is one the sheet has no offset table entry for. */
#define ICON_CEL_GROUP_COUNT 160

/* What the shipped FDE.SAV's resume region holds, read out of the file: turn
   10 of chapter 0, twenty-three units on the field, the view origin at tile
   (10, 6) and the cursor at tile (17, 11), one enrolled party member, 600 gold
   and all four options on.  These are the file's bytes and not the emitted C's
   idea of them. */
#define SHIPPED_TURN 10
#define SHIPPED_UNIT_COUNT 23
#define SHIPPED_CHAPTER 0
#define SHIPPED_VIEW_TILE_X 10
#define SHIPPED_VIEW_TILE_Y 6
#define SHIPPED_CURSOR_TILE_X 17
#define SHIPPED_CURSOR_TILE_Y 11
#define SHIPPED_ROSTER_MEMBERS 1
#define SHIPPED_PARTY_GOLD 600L
#define SHIPPED_BATTLE_ANIM 1
#define SHIPPED_TERRAIN_HUD 1
#define SHIPPED_BGM 1
#define SHIPPED_SFX 1

/* The two counts in the field block, and the group ids of the first and last
   unit record.  The two counts are the file's own and they add up to the unit
   count, which is what makes the pair worth asserting: the loader reads them
   from a different block of the image than the count it compares against. */
#define SHIPPED_PLAYER_SLOTS 1
#define SHIPPED_CHAR_SPAWNS 22
#define SHIPPED_FIRST_GROUP_ID 0
#define SHIPPED_LAST_GROUP_ID 74

/* The image, decrypted once and kept, so no case depends on what another one
   did to it.  0 means not tried yet, 1 staged, -1 not staged. */
static unsigned char resume_image[0x59cb];
static int resume_staged;

static int resume_image_is_staged(void)
{
    FILE *fp;
    size_t got;

    if (resume_staged != 0) {
        return resume_staged > 0;
    }
    resume_staged = -1;
    fp = fopen(SAVE_NAME, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(resume_image, 1, (size_t) SAVE_IMAGE_SIZE, fp);
    fclose(fp);
    if (got != (size_t) SAVE_IMAGE_SIZE) {
        return 0;
    }
    fdps_xor_crypt_buffer(resume_image, (unsigned int) SAVE_IMAGE_SIZE);
    resume_staged = 1;
    return 1;
}

static int resume_header_byte(long offset)
{
    return (int) resume_image[RESUME_HEADER_AT + offset];
}

/* The thirteen bytes the loader fans out into thirteen globals, each at the
   displacement its own MOV AL carries.  A header read one byte off would move
   every field after it, and no two neighbouring values here are equal. */
static void the_header_holds_the_thirteen_scalars_the_loader_publishes(void)
{
    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    CHECK_EQ(resume_header_byte(AT_TURN), SHIPPED_TURN);
    CHECK_EQ(resume_header_byte(AT_UNIT_COUNT), SHIPPED_UNIT_COUNT);
    CHECK_EQ(resume_header_byte(AT_CHAPTER), SHIPPED_CHAPTER);
    CHECK_EQ(resume_header_byte(AT_VIEW_TILE_X), SHIPPED_VIEW_TILE_X);
    CHECK_EQ(resume_header_byte(AT_VIEW_TILE_Y), SHIPPED_VIEW_TILE_Y);
    CHECK_EQ(resume_header_byte(AT_CURSOR_TILE_X), SHIPPED_CURSOR_TILE_X);
    CHECK_EQ(resume_header_byte(AT_CURSOR_TILE_Y), SHIPPED_CURSOR_TILE_Y);
    CHECK_EQ(resume_header_byte(AT_ROSTER_MEMBERS), SHIPPED_ROSTER_MEMBERS);
    CHECK_EQ(resume_header_byte(AT_BATTLE_ANIM), SHIPPED_BATTLE_ANIM);
    CHECK_EQ(resume_header_byte(AT_TERRAIN_HUD), SHIPPED_TERRAIN_HUD);
    CHECK_EQ(resume_header_byte(AT_BGM), SHIPPED_BGM);
    CHECK_EQ(resume_header_byte(AT_SFX), SHIPPED_SFX);
}

/* +0x0a is a dword and not a byte, and it sits at an offset that is not
   four-byte aligned.  600 needs two bytes, so a byte read would answer 88 and
   a read from the wrong end of the dword would answer 2. */
static void the_party_gold_is_an_unaligned_dword(void)
{
    unsigned long gold;

    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    gold = (unsigned long) resume_image[RESUME_HEADER_AT + AT_PARTY_GOLD]
        | ((unsigned long) resume_image[RESUME_HEADER_AT + AT_PARTY_GOLD + 1]
           << 8)
        | ((unsigned long) resume_image[RESUME_HEADER_AT + AT_PARTY_GOLD + 2]
           << 16)
        | ((unsigned long) resume_image[RESUME_HEADER_AT + AT_PARTY_GOLD + 3]
           << 24);

    CHECK_EQ(gold, SHIPPED_PARTY_GOLD);
    CHECK_EQ((RESUME_HEADER_AT + AT_PARTY_GOLD) & 3L, 1L);
}

/* The four coordinates are stored as tile indices and every global they land
   in is in world pixels, IMUL EAX,EAX,0x18.  These are the four products the
   loader publishes, computed here from the file's own bytes. */
static void the_view_origin_and_cursor_are_tiles_scaled_by_twenty_four(void)
{
    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    CHECK_EQ(resume_header_byte(AT_VIEW_TILE_X) * RESUME_TILE_SIZE, 240);
    CHECK_EQ(resume_header_byte(AT_VIEW_TILE_Y) * RESUME_TILE_SIZE, 144);
    CHECK_EQ(resume_header_byte(AT_CURSOR_TILE_X) * RESUME_TILE_SIZE, 408);
    CHECK_EQ(resume_header_byte(AT_CURSOR_TILE_Y) * RESUME_TILE_SIZE, 264);
}

/* The loader takes the unit count out of the header at +0x30c4 and the two map
   counts out of the field block at +0x0001 and +0x0002, three reads in two
   different blocks of the image.  In the shipped save the two add up to the
   one, which no wrong offset among the three can reproduce. */
static void the_field_block_counts_add_up_to_the_unit_count(void)
{
    int player_slots;
    int char_spawns;

    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    player_slots = (int) resume_image[RESUME_FIELD_BLOCK_AT
                                      + AT_PLAYER_SLOT_COUNT];
    char_spawns = (int) resume_image[RESUME_FIELD_BLOCK_AT
                                     + AT_CHAR_SPAWN_COUNT];

    CHECK_EQ(player_slots, SHIPPED_PLAYER_SLOTS);
    CHECK_EQ(char_spawns, SHIPPED_CHAR_SPAWNS);
    CHECK_EQ(player_slots + char_spawns, resume_header_byte(AT_UNIT_COUNT));
}

/* The five blocks are written end to end with nothing between them, and the
   whole region ends before save.c's first slot record.  The malloc the loader
   makes for the unit array is the whole 0x1e00 and the copy that follows is
   count * 0x50, so the count the header carries has to fit -- 23 records is
   0x730 bytes of the 0x1e00 taken. */
static void the_resume_blocks_are_written_end_to_end(void)
{
    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    CHECK_EQ(RESUME_FIELD_BLOCK_AT + RESUME_FIELD_BLOCK_BYTES,
             RESUME_ROSTER_AT);
    CHECK_EQ(RESUME_ROSTER_AT + RESUME_ROSTER_BYTES, RESUME_UNITS_AT);
    CHECK_EQ(RESUME_UNITS_AT + RESUME_UNITS_BYTES, RESUME_FLAGS_AT);
    CHECK_EQ(RESUME_FLAGS_AT + RESUME_FLAGS_BYTES, RESUME_HEADER_AT);
    CHECK_EQ(RESUME_HEADER_AT + AT_SFX < SAVE_SLOTS_AT, 1);
    CHECK_EQ(resume_header_byte(AT_UNIT_COUNT) * RESUME_UNIT_STRIDE,
             0x730L);
    CHECK_EQ(resume_header_byte(AT_UNIT_COUNT) * RESUME_UNIT_STRIDE
             <= RESUME_UNITS_BYTES, 1);
}

/* The stride the loader walks the unit array at, checked against what the
   records themselves say.  Every one of the twenty-three group ids the sheet
   is asked for is inside the 160 groups ICON.CEL holds, which a stride other
   than 0x50 or a field other than +7 does not survive: at a stride of 0x4f the
   fourth record's byte 7 is already a level and not a group.  The first and
   last are pinned exactly as well, so a walk that started or stopped in the
   wrong place fails even if every byte it read happened to be small. */
static void every_unit_record_names_a_group_the_sheet_holds(void)
{
    int unit_index;
    int group_id;
    int outside_the_sheet;

    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    outside_the_sheet = 0;
    for (unit_index = 0; unit_index < SHIPPED_UNIT_COUNT; unit_index++) {
        group_id = (int) resume_image[RESUME_UNITS_AT
                                      + unit_index * RESUME_UNIT_STRIDE
                                      + AT_UNIT_GROUP_ID];
        if (group_id >= ICON_CEL_GROUP_COUNT) {
            outside_the_sheet++;
        }
    }

    CHECK_EQ(outside_the_sheet, 0);
    CHECK_EQ((int) resume_image[RESUME_UNITS_AT + AT_UNIT_GROUP_ID],
             SHIPPED_FIRST_GROUP_ID);
    CHECK_EQ((int) resume_image[RESUME_UNITS_AT
                                + (SHIPPED_UNIT_COUNT - 1)
                                  * RESUME_UNIT_STRIDE
                                + AT_UNIT_GROUP_ID],
             SHIPPED_LAST_GROUP_ID);
}

/* The sprite cache slot the file carries is byte 2 of the record, and the
   loader overwrites all twenty-three of them with the slots the fresh cache
   hands out.  In the shipped save the slots run 0,1,2,1,1,1... -- a value that
   repeats, which is what says the field is a cache slot shared by units of the
   same group and not a per-unit index.  The case pins the first three so that
   a record layout with byte 2 somewhere else fails here as well as above. */
static void the_sprite_cache_slot_is_byte_two_of_the_record(void)
{
    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    CHECK_EQ((int) resume_image[RESUME_UNITS_AT + AT_UNIT_SPRITE_SLOT], 0);
    CHECK_EQ((int) resume_image[RESUME_UNITS_AT + RESUME_UNIT_STRIDE
                                + AT_UNIT_SPRITE_SLOT], 1);
    CHECK_EQ((int) resume_image[RESUME_UNITS_AT + 2 * RESUME_UNIT_STRIDE
                                + AT_UNIT_SPRITE_SLOT], 2);
    CHECK_EQ(offsetof(struct fdps_unit_record, sprite_cache_slot),
             AT_UNIT_SPRITE_SLOT);
    CHECK_EQ(offsetof(struct fdps_unit_record, portrait_id),
             AT_UNIT_GROUP_ID);
    CHECK_EQ(sizeof(struct fdps_unit_record), RESUME_UNIT_STRIDE);
}

/* The 0x20 bytes the loader copies straight into
   data_fdps_map_cell_event_triggered_flags, which is declared as exactly 32
   bytes: the copy fills the array and does not reach past it (contract H in
   rebuild_info/emit_pipeline.md).  All thirty-two are clear in the shipped
   save, and the byte on either side of the block is checked so that a copy
   taken one byte early or late shows up -- 0x30a2 is the last unit record's
   tail and 0x30c3 is the turn counter. */
static void the_triggered_flags_block_is_thirty_two_clear_bytes(void)
{
    int index;
    int set_flags;

    CHECK_EQ(resume_image_is_staged(), 1);
    if (!resume_image_is_staged()) {
        return;
    }

    set_flags = 0;
    for (index = 0; index < (int) RESUME_FLAGS_BYTES; index++) {
        if (resume_image[RESUME_FLAGS_AT + index] != 0) {
            set_flags++;
        }
    }

    CHECK_EQ(set_flags, 0);
    CHECK_EQ(sizeof(data_fdps_map_cell_event_triggered_flags),
             RESUME_FLAGS_BYTES);
    CHECK_EQ((int) resume_image[RESUME_FLAGS_AT - 1] != 0, 1);
    CHECK_EQ((int) resume_image[RESUME_FLAGS_AT + RESUME_FLAGS_BYTES],
             SHIPPED_TURN);
}

void run_savefile_tests(void)
{
    RUN_TEST(trailing_four_bytes_are_not_summed);
    RUN_TEST(the_skip_starts_exactly_at_size_minus_four);
    RUN_TEST(bytes_are_summed_zero_extended);
    RUN_TEST(the_accumulator_is_thirty_two_bits_wide);
    RUN_TEST(size_five_sums_exactly_one_byte);
    RUN_TEST(the_image_is_not_written_to);
    RUN_TEST(the_keystream_is_the_low_half_of_the_rotating_key);
    RUN_TEST(the_keystream_does_not_depend_on_the_data);
    RUN_TEST(crypting_twice_restores_the_buffer);
    RUN_TEST(the_length_is_a_byte_count);
    RUN_TEST(the_shipped_save_decrypts_to_its_own_checksum);
    RUN_TEST(the_header_holds_the_thirteen_scalars_the_loader_publishes);
    RUN_TEST(the_party_gold_is_an_unaligned_dword);
    RUN_TEST(the_view_origin_and_cursor_are_tiles_scaled_by_twenty_four);
    RUN_TEST(the_field_block_counts_add_up_to_the_unit_count);
    RUN_TEST(the_resume_blocks_are_written_end_to_end);
    RUN_TEST(every_unit_record_names_a_group_the_sheet_holds);
    RUN_TEST(the_sprite_cache_slot_is_byte_two_of_the_record);
    RUN_TEST(the_triggered_flags_block_is_thirty_two_clear_bytes);
}
