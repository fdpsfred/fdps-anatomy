/* tests/dpmi.c -- cover for src/dpmi.c.
 *
 * Covers fdps_dpmi_free_dos_memory at 0003cad2.  Expected values come from its
 * assembly -- SUB ESP,0x38 for two adjacent 0x1c-byte register sets, MOV dword
 * ptr [ESP],0x101, MOV EAX,dword ptr [ESP+0x44] / AND EAX,0xffff / MOV dword
 * ptr [ESP+0xc],EAX, and PUSH of the two set addresses plus PUSH 0x31 into
 * int386 -- and from the DPMI 0.9 specification of function 0101h, which frees
 * the block named by the selector in DX and releases that selector's
 * descriptor with it.  None of them is read off the emitted C.
 *
 * The DPMI calls are made for real: the test executable runs under DOS/4GW
 * inside DOSBox-X, the same DPMI host the game has, so INT 31h is answered by
 * the same code that answers it in the game.  A fabricated stand-in would only
 * prove the arithmetic agrees with itself.  Blocks these tests allocate are
 * always freed again, so a later unit does not inherit a leak.
 *
 * The three-argument shape is what the two behavioural tests exist for: the
 * four AIL callers push the block's linear address, its real-mode far pointer
 * and its selector and drop all three with ADD ESP,0xc, and a one-argument
 * spelling still links and still balances the stack while freeing whatever the
 * linear address happens to name.  Nothing but the third argument may decide
 * which block goes back.
 */
#include <i86.h>
#include "testharn.h"
#include "dpmi.h"

/* Two paragraphs, 32 bytes: the smallest allocation that still gets its own
   DOS memory block and its own selector. */
#define TEST_BLOCK_PARAGRAPHS 2

/* DPMI function 0100h, the allocator the game's own 0003ca49 wraps: BX holds
   the paragraph count, and on success AX comes back with the real-mode segment
   and DX with the protected-mode selector.  Written out here rather than
   called through fdps_dpmi_alloc_dos_memory so that what this file exercises
   is only the function under test. */
static int dpmi_alloc_dos_block(unsigned paragraphs,
                                unsigned *out_real_mode_ptr,
                                unsigned *out_selector)
{
    union REGS dpmi_in;
    union REGS dpmi_out;

    dpmi_in.x.eax = 0x0100;
    dpmi_in.x.ebx = paragraphs;
    int386(0x31, &dpmi_in, &dpmi_out);
    if (dpmi_out.x.cflag != 0) {
        return 0;
    }
    *out_real_mode_ptr = dpmi_out.x.eax << 16;
    *out_selector = dpmi_out.x.edx & 0xffffu;
    return 1;
}

/* DPMI function 0006h, Get Segment Base Address: it answers for a selector
   that exists and returns with carry set for one that does not.  Freeing a DOS
   memory block releases the block's descriptor too, so this is what tells a
   freed selector from a live one.  The selector is only ever passed in BX and
   never loaded into a segment register, so asking about a dead one cannot
   fault. */
static int dpmi_selector_is_valid(unsigned selector)
{
    union REGS dpmi_in;
    union REGS dpmi_out;

    dpmi_in.x.eax = 0x0006;
    dpmi_in.x.ebx = selector;
    int386(0x31, &dpmi_in, &dpmi_out);
    return dpmi_out.x.cflag == 0;
}

/* The 0x38-byte frame is two register sets, and the two stores the body makes
   land on the first set's EAX and EDX -- offset 0 and offset 0xc.  If either
   member sat anywhere else the interrupt would be issued with the function
   code or the selector in the wrong register, which is a fault the behavioural
   tests below could only report as a mysteriously failed free. */
static void dpmi_free_register_set_has_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) sizeof(union REGS), 0x1c);
    CHECK_EQ((int) (2 * sizeof(union REGS)), 0x38);
    CHECK_EQ((int) ((char *) &probe.x.eax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.x.edx - (char *) &probe), 0xc);
}

/* One block, allocated and then handed back through the function under test
   with the arguments the AIL callers pass: the linear address, the real-mode
   far pointer and the selector, all three describing the same block.  The
   selector answers function 0006h before the call and must not answer it
   after. */
static void dpmi_free_releases_the_block(void)
{
    unsigned real_mode_ptr;
    unsigned selector;
    unsigned linear;

    real_mode_ptr = 0;
    selector = 0;
    if (!dpmi_alloc_dos_block(TEST_BLOCK_PARAGRAPHS, &real_mode_ptr,
                              &selector)) {
        CHECK_EQ(0, 1);   /* no DOS memory to test with; do not pass silently */
        return;
    }

    /* The allocator at 0003ca49 forms the linear address as
       (real-mode segment) << 4, which is (real_mode_ptr >> 16) << 4. */
    linear = (real_mode_ptr >> 16) << 4;

    CHECK_EQ(dpmi_selector_is_valid(selector), 1);
    fdps_dpmi_free_dos_memory(linear, real_mode_ptr, selector);
    CHECK_EQ(dpmi_selector_is_valid(selector), 0);
}

/* Two blocks, with the block that must survive named in the two argument slots
   the body does not read.  A spelling that read the first argument instead
   would free the wrong block, and both checks would swap: this is the one
   failure the plate comment at 0003cad2 calls out, and it is silent at run
   time because the discarded carry flag means nothing ever reports it. */
static void dpmi_free_reads_only_the_third_argument(void)
{
    unsigned kept_ptr;
    unsigned kept_selector;
    unsigned doomed_ptr;
    unsigned doomed_selector;

    kept_ptr = 0;
    kept_selector = 0;
    doomed_ptr = 0;
    doomed_selector = 0;
    if (!dpmi_alloc_dos_block(TEST_BLOCK_PARAGRAPHS, &kept_ptr,
                              &kept_selector)) {
        CHECK_EQ(0, 1);
        return;
    }
    if (!dpmi_alloc_dos_block(TEST_BLOCK_PARAGRAPHS, &doomed_ptr,
                              &doomed_selector)) {
        CHECK_EQ(0, 1);
        fdps_dpmi_free_dos_memory(0, 0, kept_selector);
        return;
    }
    CHECK_EQ(kept_selector == doomed_selector, 0);

    fdps_dpmi_free_dos_memory(kept_selector, kept_selector, doomed_selector);
    CHECK_EQ(dpmi_selector_is_valid(doomed_selector), 0);
    CHECK_EQ(dpmi_selector_is_valid(kept_selector), 1);

    fdps_dpmi_free_dos_memory(0, 0, kept_selector);
    CHECK_EQ(dpmi_selector_is_valid(kept_selector), 0);
}

void run_dpmi_tests(void)
{
    RUN_TEST(dpmi_free_register_set_has_the_image_layout);
    RUN_TEST(dpmi_free_releases_the_block);
    RUN_TEST(dpmi_free_reads_only_the_third_argument);
}
