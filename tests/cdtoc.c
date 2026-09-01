/* tests/cdtoc.c -- cover for src/cdtoc.c.
 *
 * So far this covers fdps_cd_unpack_msf at 0003bc3f and
 * fdps_cd_msf_to_sector at 0003bc78.
 *
 * Every expected value below comes from the sixteen instructions of that
 * function and from its two call sites, never from the emitted C:
 *
 *   MOV AL,byte ptr [ESP+0x4] / MOV EDX,dword ptr [ESP+0x10] / MOV [EDX],AL
 *   MOV EAX,[ESP+0x4] / AND EAX,0xff00   / SHR EAX,0x8  / MOV [ESP+0xc]  -> AL
 *   MOV EAX,[ESP+0x4] / AND EAX,0xff0000 / SHR EAX,0x10 / MOV [ESP+0x8]  -> AL
 *
 * __CHK pops its own argument (RET 0x4 at 00043627), so at every one of those
 * loads [ESP+0x4] is the first argument and [ESP+0x8], [ESP+0xc], [ESP+0x10]
 * are the second, third and fourth.  That is what fixes the parameter order:
 * the fourth pointer receives the low byte, the second the bits at 16-23.
 * Both callers clean four dwords off the stack afterwards (ADD ESP,0x10 at
 * 0003bc9f and 0003c0a6), which is the same reading from the outside.
 *
 * The function reads and writes no global, so nothing here depends on data
 * ticket 23 has not emitted yet.
 */
#include "testharn.h"
#include "cdtoc.h"

/* Poison bytes on both sides of the three destinations.  0x5a is a value no
   assertion below expects, so a store that lands wide shows up as a changed
   guard rather than as a coincidence. */
static unsigned char msf_bytes[5];

static void unpack_into_poisoned_bytes(unsigned int msf_packed)
{
    int i;

    for (i = 0; i < 5; i++) {
        msf_bytes[i] = 0x5a;
    }
    fdps_cd_unpack_msf(msf_packed, &msf_bytes[1], &msf_bytes[2],
                       &msf_bytes[3]);
}

/* The three masks and shifts, on a value whose three fields are all different
   and none of which is a plausible copy of another.  0x00332c1f is 51 minutes
   32 seconds 31 frames read as a packed address; what matters is that 0x33
   arrives at the minute pointer and 0x1f at the frame pointer, which is the
   parameter order the two call sites clean up after. */
static void cdtoc_unpack_splits_the_three_fields(void)
{
    unpack_into_poisoned_bytes(0x00332c1fu);
    CHECK_EQ(msf_bytes[1], 0x33);
    CHECK_EQ(msf_bytes[2], 0x2c);
    CHECK_EQ(msf_bytes[3], 0x1f);
}

/* AND 0xff0000 is the widest mask the body applies, so bits 24-31 of the
   argument reach nothing.  MSCDEX reports its addresses in the low three bytes
   of a dword whose top byte it does not define, so this is the case the game
   actually feeds the function. */
static void cdtoc_unpack_ignores_the_top_byte(void)
{
    unpack_into_poisoned_bytes(0xff123456u);
    CHECK_EQ(msf_bytes[1], 0x12);
    CHECK_EQ(msf_bytes[2], 0x34);
    CHECK_EQ(msf_bytes[3], 0x56);
}

/* Each field is masked before it is shifted, and the shift is SHR rather than
   SAR, so a field whose top bit is set comes down as itself and nothing above
   it survives.  0x00ffffff sets every bit of all three fields at once; a
   widened or sign-propagating store would have to disturb one of the guards
   to show all three as 0xff. */
static void cdtoc_unpack_carries_full_width_fields(void)
{
    unpack_into_poisoned_bytes(0x00ffffffu);
    CHECK_EQ(msf_bytes[1], 0xff);
    CHECK_EQ(msf_bytes[2], 0xff);
    CHECK_EQ(msf_bytes[3], 0xff);
    CHECK_EQ(msf_bytes[0], 0x5a);
    CHECK_EQ(msf_bytes[4], 0x5a);
}

/* All three stores are MOV byte ptr [EDX],AL, one byte each, and
   fdps_cdrom_read_disk_info relies on it: at 0003c0a1 it passes 0x69e08,
   0x69e09 and 0x69e0a -- three adjacent bss bytes -- as minute, second and
   frame.  A store any wider than a byte would carry one field over the next
   one and over the neighbouring global past it.  A zero argument is the case
   that shows it, because every field it writes is 0x00 and only an untouched
   guard is still 0x5a. */
static void cdtoc_unpack_writes_one_byte_per_pointer(void)
{
    unpack_into_poisoned_bytes(0u);
    CHECK_EQ(msf_bytes[0], 0x5a);
    CHECK_EQ(msf_bytes[1], 0);
    CHECK_EQ(msf_bytes[2], 0);
    CHECK_EQ(msf_bytes[3], 0);
    CHECK_EQ(msf_bytes[4], 0x5a);
}

/* A field of the packed value that is zero has to clear its destination rather
   than leave it: there is no branch anywhere in the body, so all three stores
   happen for every argument.  Here the minute and frame fields are zero and
   only the second field is set. */
static void cdtoc_unpack_always_writes_all_three(void)
{
    unpack_into_poisoned_bytes(0x00004b00u);
    CHECK_EQ(msf_bytes[1], 0);
    CHECK_EQ(msf_bytes[2], 0x4b);
    CHECK_EQ(msf_bytes[3], 0);
}

/* fdps_cd_msf_to_sector at 0003bc78.  Every number below is read off the
   thirty-one instructions of that body, never off the C:

     MOVZX EDX,byte ptr [ESP]      -- the minute byte
     SHL/SUB/SHL/ADD/SHL/MOV/SHL/ADD folds it to EDX = minute * 4500:
       (((m<<5)-m)<<2)+m  = 125m, <<2 = 500m, plus (500m<<3) = 4000m
     MOVZX EBX,byte ptr [ESP+0x8]  -- the second byte
     SHL/ADD/MOV/SHL/SUB folds it to EAX = second * 75:
       ((s<<2)+s) = 5s, (5s<<4) - 5s = 75s
     MOVZX EAX,byte ptr [ESP+0x4]  -- the frame byte, added unscaled
     SUB EAX,0x96                  -- less 150, then RET with EAX

   The three MOVZX loads are what make every field unsigned, and the ADD ESP,4
   after each of the two call sites (0003c0b2 and 0003c180) is what makes the
   argument a single stack dword.  The address 00:02:00 mapping to sector 0 is
   the Red Book lead-in that the 0x96 encodes.

   fdps_cd_unpack_msf is emitted in this same file, so these assertions run
   against real code and not against a stub. */

/* The lead-in itself: 00:02:00 is logical sector 0.  This is the single value
   that fixes the 0x96 -- a body without the SUB would answer 150 here. */
static void cdtoc_sector_of_lead_in_is_zero(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000200u), 0);
}

/* The minute scale, on its own: 01:00:00 is 4500 frames from 00:00:00, so 4350
   sectors from 00:02:00.  This pins the whole shift chain, since any other
   multiplier lands somewhere else entirely. */
static void cdtoc_sector_scales_minutes_by_4500(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00010000u), 4350);
}

/* The second scale, on its own: 00:03:00 is one second past the lead-in, and
   one second is the 75 frames the second chain multiplies by. */
static void cdtoc_sector_scales_seconds_by_75(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000300u), 75);
}

/* The frame field is added unscaled, and 74 is the largest a real address
   carries: 00:02:74 is 74 sectors past the lead-in. */
static void cdtoc_sector_adds_frames_unscaled(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x0000024au), 74);
}

/* All three fields at once, none of them a plausible copy of another:
   0x00332c1f is 51 minutes 44 seconds 31 frames, so
   51*4500 + 44*75 + 31 - 150 = 229500 + 3300 + 31 - 150. */
static void cdtoc_sector_sums_all_three_fields(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00332c1fu), 232681);
}

/* Below the lead-in the result is negative and is not clamped: there is no
   branch anywhere in the body and no CDQ or AND after the SUB.  00:00:00 is
   the case the game can actually reach, because it is what an unanswered
   driver query leaves in the reply block, and 00:01:00 shows the sign is not
   a special case of zero. */
static void cdtoc_sector_goes_negative_below_the_lead_in(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000000u), -150);
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000100u), -75);
}

/* Bits 24-31 of the argument reach nothing, because fdps_cd_unpack_msf masks
   the minute field with AND 0xff0000 before it stores it.  MSCDEX leaves that
   byte undefined in the replies both callers pass through. */
static void cdtoc_sector_ignores_the_top_byte(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0xff000200u), 0);
}

/* Full-width fields, well past anything a real disc carries: the three MOVZX
   loads zero-extend, so 0xff arrives as 255 and not as -1, and the sum stays
   in the 32-bit EAX the function returns.  255*4500 + 255*75 + 255 - 150. */
static void cdtoc_sector_carries_full_width_fields(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00ffffffu), 1166730L);
}

void run_cdtoc_tests(void)
{
    RUN_TEST(cdtoc_unpack_splits_the_three_fields);
    RUN_TEST(cdtoc_unpack_ignores_the_top_byte);
    RUN_TEST(cdtoc_unpack_carries_full_width_fields);
    RUN_TEST(cdtoc_unpack_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_unpack_always_writes_all_three);
    RUN_TEST(cdtoc_sector_of_lead_in_is_zero);
    RUN_TEST(cdtoc_sector_scales_minutes_by_4500);
    RUN_TEST(cdtoc_sector_scales_seconds_by_75);
    RUN_TEST(cdtoc_sector_adds_frames_unscaled);
    RUN_TEST(cdtoc_sector_sums_all_three_fields);
    RUN_TEST(cdtoc_sector_goes_negative_below_the_lead_in);
    RUN_TEST(cdtoc_sector_ignores_the_top_byte);
    RUN_TEST(cdtoc_sector_carries_full_width_fields);
}
