/* tests/cd.c -- cover for src/cd.c.
 *
 * Expected values come from the assembly at 0003bade -- MOV word ptr
 * [0x00069dc8],0x100 and MOV word ptr [0x00069dcc],0x20 into the input
 * register block that starts at 0x00069dc8, PUSH 0xc / PUSH 0 / PUSH 0x69df0 /
 * CALL memset, the two PUSH 0x31 / CALL int386x pairs, and the four stores
 * that publish the results (0003bb2a, 0003bb3d, 0003bb65, 0003bb77) -- plus
 * the two symbol sizes ghidra_snapshot records for the register blocks, 28 for
 * a union REGS and 12 for a struct SREGS.  None of them is read off the
 * emitted C.
 *
 * The DPMI allocation is made for real: the test executable runs under DOS/4GW
 * inside DOSBox-X, which is the same DPMI host the game has, so INT 31h
 * function 0100h is answered by the same code that answers it in the game.  A
 * fabricated stand-in would only prove the arithmetic agrees with itself.  The
 * two 512-byte blocks each call allocates are never freed, here as in the
 * game.
 *
 * Nothing below asserts what any of the globals holds on its own -- ticket 23
 * owns their contents.  Every expectation is either a relationship between two
 * values the function itself wrote, or a poison value the test put there.
 */
#include "testharn.h"
#include "gamedata.h"
#include "cd.h"

/* Fill the two shared register blocks with values the function is expected to
   overwrite, and values it is expected to leave alone, then run it.

   Only the 16-bit members are poisoned, so the filler halves of the input
   block stay zero and the interrupt is issued with the same full-width EAX and
   EBX the game issues it with.  CX and DX reach the interrupt carrying the
   poison; DPMI function 0100h reads neither.  ES and DS are left alone
   because int386x loads them into the segment registers -- an invalid selector
   there would fault before the interrupt ever happened -- and the function
   zeroes them itself anyway. */
static void alloc_with_poisoned_state(void)
{
    data_fdps_cd_int_regs_in.w.ax = 0x999;
    data_fdps_cd_int_regs_in.w.bx = 0x888;
    data_fdps_cd_int_regs_in.w.cx = 0x1234;
    data_fdps_cd_int_regs_in.w.dx = 0x5678;
    data_fdps_cd_int_sregs.cs = 0x1111;
    data_fdps_cd_int_sregs.ss = 0x2222;
    data_fdps_cd_int_sregs.fs = 0x3333;
    data_fdps_cd_int_sregs.gs = 0x4444;
    fdps_cd_alloc_dos_buffers();
}

/* The two register blocks are addressed as whole objects by the original --
   PUSH 0x69dc8, PUSH 0x69dac, PUSH 0x69df0 hand int386x their bases -- so
   their sizes are what decides whether the callee reads past them into the
   neighbouring globals (contract B).  The field the second store lands on is
   pinned the same way: 0x00069dcc is the input block's base plus four, which
   is where the 32-bit union REGS puts w.bx and nowhere else. */
static void cd_register_blocks_have_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) sizeof(union REGS), 28);
    CHECK_EQ((int) sizeof(struct SREGS), 12);
    CHECK_EQ((int) ((char *) &probe.x.eax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.w.ax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.w.bx - (char *) &probe), 4);
}

/* MOV word ptr [0x00069dc8],0x100 / MOV word ptr [0x00069dcc],0x20 are the
   only two stores the body makes into the input block, and int386x never
   writes to it, so CX and DX come back holding the poison. */
static void cd_alloc_fills_the_dpmi_request(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_regs_in.w.ax, 0x100);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.bx, 0x20);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.cx, 0x1234);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.dx, 0x5678);
}

/* PUSH 0xc / PUSH 0x0 / PUSH 0x69df0 / CALL memset clears all twelve bytes of
   the segment register block before the first interrupt.  int386x writes back
   only ES at +0 and DS at +6 (0004e27a, 0004e276), so the four members it
   never touches are the ones that show the memset happened -- and they show it
   only because the test poisoned them first: the block is zero-filled bss
   otherwise. */
static void cd_alloc_clears_the_segment_registers(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_sregs.cs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.ss, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.fs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.gs, 0);
}

/* MOV AX,[0x69dac] / MOV [0x69e54],AX publishes the segment, and MOV
   EAX,[0x69dac] / AND EAX,0xffff / SHL EAX,0x4 / MOV [0x69de8],EAX publishes
   the same 16 bits shifted left four.  The two are therefore locked together
   whatever DPMI returned, which is what makes this assertion independent of
   the host.  A zero segment would mean the allocation never happened: the
   caller reads exactly that as "already allocated" (CMP word ptr
   [0x00069e54],0x0 / JNZ at 0003c68e). */
static void cd_alloc_publishes_the_request_header_block(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_request_header_real_mode_seg != 0, 1);
    CHECK_EQ((long) (unsigned long) data_fdps_cd_request_header_buffer,
             (long) ((unsigned long) data_fdps_cd_request_header_real_mode_seg
                     << 4));
}

/* MOVZX EAX,word ptr [0x69dac] / SHL EAX,0x10 packs the second block's segment
   into the high half with a zero offset below it, and the same AND 0xffff /
   SHL 0x4 pair as above publishes its flat address, so the packed pointer's
   high half shifted left four has to be the flat address.

   x.cflag is int386x's sign-extended carry (SBB EAX,EAX at 0004e26d): zero
   means the second DPMI allocation reported success, which is what makes the
   segment above a segment rather than an error code.  The two blocks are
   separate allocations and a DOS memory block cannot be handed out twice, so
   their flat addresses differ. */
static void cd_alloc_publishes_the_ioctl_block(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cdrom_int_out_regs.x.cflag, 0);
    CHECK_EQ(data_fdps_cd_ioctl_buffer_real_mode_ptr & 0xffff, 0);
    CHECK_EQ((long) (unsigned long) data_fdps_cd_ioctl_buffer,
             (long) ((data_fdps_cd_ioctl_buffer_real_mode_ptr >> 16) << 4));
    CHECK_EQ(data_fdps_cd_ioctl_buffer != data_fdps_cd_request_header_buffer,
             1);
}

void run_cd_tests(void)
{
    RUN_TEST(cd_register_blocks_have_the_image_layout);
    RUN_TEST(cd_alloc_fills_the_dpmi_request);
    RUN_TEST(cd_alloc_clears_the_segment_registers);
    RUN_TEST(cd_alloc_publishes_the_request_header_block);
    RUN_TEST(cd_alloc_publishes_the_ioctl_block);
}
