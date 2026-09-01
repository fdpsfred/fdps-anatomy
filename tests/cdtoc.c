/* tests/cdtoc.c -- cover for src/cdtoc.c.
 *
 * So far this covers fdps_cd_unpack_msf at 0003bc3f alone.
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

void run_cdtoc_tests(void)
{
    RUN_TEST(cdtoc_unpack_splits_the_three_fields);
    RUN_TEST(cdtoc_unpack_ignores_the_top_byte);
    RUN_TEST(cdtoc_unpack_carries_full_width_fields);
    RUN_TEST(cdtoc_unpack_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_unpack_always_writes_all_three);
}
