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
 *
 * Covers fdps_dpmi_lock_region at 0003cb01.  Expected values come from its
 * assembly -- CMP EDX,EBX with the two JNC arms that order the endpoints, SUB
 * EDX,EAX / INC EDX for the inclusive byte count, the stores at [ESP+4],
 * [ESP+8], [ESP+0x10] and [ESP+0x14], and CMP dword ptr [ESP+0x34],0 / SETZ AL
 * for the inverted return sense -- and from the DPMI 0.9 specification of
 * function 0600h, which pins a linear range named by BX:CX and SI:DI.  Ranges
 * these tests lock are always unlocked again.
 *
 * Covers fdps_dpmi_alloc_dos_memory at 0003ca49.  Expected values come from its
 * assembly -- MOV dword ptr [ESP],0x100 with the paragraph count into [ESP+4],
 * the three stores SHL EAX,0x10 / AND EAX,0xffff+SHL EAX,4 / AND EAX,0xffff
 * through the pointers at [ESP+0x48], [ESP+0x44] and [ESP+0x4c], the CMP dword
 * ptr [ESP+0x34],0 that skips all of them on a set carry, and MOV ESI,0x1 /
 * MOV EAX,ESI at the tail -- and from the DPMI 0.9 specification of functions
 * 0100h and 0006h.  Blocks these tests allocate are always freed again.
 *
 * Covers fdps_dpmi_unlock_region at 0003cb6e.  Expected values come from its
 * assembly -- CMP EDX,EBX with the same two JNC arms, SUB EDX,EAX / INC EDX,
 * MOV dword ptr [ESP],0x601 and the JMP into 0003cb24, the tail it shares with
 * fdps_dpmi_lock_region, which stores base and count at [ESP+4], [ESP+8],
 * [ESP+0x10] and [ESP+0x14] and turns the carry word at [ESP+0x34] into the
 * return value with SETZ AL -- and from the DPMI 0.9 specification of function
 * 0601h.  What no test here can see is the one instruction that distinguishes
 * this function from the lock: DOS/4GW answers both 0600h and 0601h with the
 * carry flag clear and keeps no lock count, so a body that stored 0x600 by
 * mistake would pass every assertion below.  That single immediate is pinned
 * instead by reading the compiled object, which is recorded in the emit
 * verdict for 0003cb6e.
 *
 * Covers fdps_dpmi_lock_size at 0003cb93.  Expected values come from its
 * assembly -- MOV EAX,[ESP+4] / ADD EAX,[ESP+8] for the far endpoint, PUSH EAX
 * then PUSH of [ESP+8] re-read as `base` for the argument order, ADD ESP,8 for
 * the caller-cleaned pair, and a RET with no epilogue, which leaves the
 * primitive's EAX as the result -- together with the endpoint arithmetic of
 * fdps_dpmi_lock_region at 0003cb01, whose (max - min) + 1 makes the extent
 * size + 1 bytes.  What no case below can see is that byte count: DOS/4GW
 * answers 0600h with the carry flag clear for any plausible range and keeps no
 * lock count, so a body passing base + size - 1, or size where base + size
 * belongs, would pass every assertion here.  The addition itself is pinned
 * instead by reading the compiled object, recorded in the emit verdict for
 * 0003cb93.
 *
 * Covers fdps_dpmi_unlock_size at 0003cbaa.  Expected values come from its
 * assembly -- MOV EAX,[ESP+4] / ADD EAX,[ESP+8] for the far endpoint, PUSH EAX
 * then PUSH of [ESP+8] re-read as `base` for the argument order, CALL 0003cb6e,
 * ADD ESP,8 for the caller-cleaned pair, and a RET with no epilogue, which
 * leaves the primitive's EAX as the result -- together with the endpoint
 * arithmetic of fdps_dpmi_unlock_region at 0003cb6e, whose (max - min) + 1
 * makes the extent size + 1 bytes.  The same blindness applies as for the lock
 * wrapper: DOS/4GW answers 0601h cleanly for any plausible range and keeps no
 * lock count, so the byte count is invisible to a run-time assertion and is
 * pinned instead by reading the compiled object, recorded in the emit verdict
 * for 0003cbaa.
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

/* ---- fdps_dpmi_lock_region at 0003cb01 ------------------------------- */

/* Larger than one 4K page, so a locked range spans a page boundary the way the
   code extents the AIL callers pin do.  It is written to before it is locked
   because a page that has never been touched is a duller thing to ask a host
   to make resident. */
static char lock_probe_area[8192];

/* DPMI function 0601h, Unlock Linear Region, written out here so that whatever
   these tests lock is handed back and a later unit does not inherit a pinned
   range.  It takes its arguments exactly as the function under test does --
   two endpoints, the second being the last byte -- so the count it releases is
   the same count that was locked.  Deliberately not routed through
   fdps_dpmi_unlock_region: what a case exercises must be only the function it
   names, so the cases for the lock reach the host directly and the cases for
   the unlock use the mirror-image helper below. */
static int dpmi_unlock_linear_range(unsigned base, unsigned last)
{
    union REGS dpmi_in;
    union REGS dpmi_out;
    unsigned length;

    length = (last - base) + 1;
    dpmi_in.x.eax = 0x0601;
    dpmi_in.x.ebx = base >> 16;
    dpmi_in.x.ecx = base & 0xffffu;
    dpmi_in.x.esi = length >> 16;
    dpmi_in.x.edi = length & 0xffffu;
    int386(0x31, &dpmi_in, &dpmi_out);
    return dpmi_out.x.cflag == 0;
}

static void fill_lock_probe_area(void)
{
    int byte_index;

    for (byte_index = 0; byte_index < (int) sizeof(lock_probe_area);
         byte_index++) {
        lock_probe_area[byte_index] = (char) byte_index;
    }
}

/* The body stores into the first register set at offsets 4, 8, 0x10 and 0x14
   and reads the second set at ESP+0x34, which is offset 0x18 inside a set that
   begins at ESP+0x1c.  Those are the BX:CX and SI:DI slots fn 0600h reads and
   the carry word it answers in; a member landing anywhere else would issue the
   interrupt with the base or the count in the wrong register, and the
   behavioural tests below could only report that as a failed lock. */
static void dpmi_lock_register_set_has_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) ((char *) &probe.x.ebx - (char *) &probe), 0x4);
    CHECK_EQ((int) ((char *) &probe.x.ecx - (char *) &probe), 0x8);
    CHECK_EQ((int) ((char *) &probe.x.esi - (char *) &probe), 0x10);
    CHECK_EQ((int) ((char *) &probe.x.edi - (char *) &probe), 0x14);
    CHECK_EQ((int) ((char *) &probe.x.cflag - (char *) &probe), 0x18);
    CHECK_EQ((int) (0x1c + 0x18), 0x34);
}

/* A resident range of this program's own data, locked with the endpoints the
   right way round.  The DPMI host answers a request like this with the carry
   flag clear, and the assembly turns a clear carry into 1 (SETZ AL after CMP
   [ESP+0x34],0) -- so a spelling that handed the hardware flag back unchanged
   would return 0 here and every AIL caller would read a successful lock as a
   failure. */
static void dpmi_lock_returns_one_on_a_clear_carry(void)
{
    unsigned base;
    unsigned last;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    last = base + sizeof(lock_probe_area) - 1;

    CHECK_EQ(fdps_dpmi_lock_region(base, last), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, last), 1);
}

/* The same range with the arguments exchanged.  CMP EDX,EBX and the two JNC
   arms put the smaller endpoint in EAX and the larger in EDX whichever way
   they arrived, so the two calls describe the identical range and must answer
   alike.  Without the normalisation the reversed call computes a base above
   the range and a count of (small - large) + 1, which wraps to nearly 4GB. */
static void dpmi_lock_orders_its_endpoints(void)
{
    unsigned base;
    unsigned last;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    last = base + sizeof(lock_probe_area) - 1;

    CHECK_EQ(fdps_dpmi_lock_region(last, base), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, last), 1);
}

/* Both endpoints on the same byte.  INC EDX makes the count 1, so this is a
   one-byte request and the host locks the page holding it; dropping the INC
   would make it a zero-byte request, which the DPMI specification does not
   define and which no caller of this function ever intends.  The check is that
   the call still describes a real range and succeeds -- it cannot see the byte
   count directly, so it catches the missing INC only on a host that refuses a
   zero-length lock, and the layout test above is what pins where the count is
   written. */
static void dpmi_lock_counts_both_endpoints(void)
{
    unsigned base;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;

    CHECK_EQ(fdps_dpmi_lock_region(base, base), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, base), 1);
}

/* ---- fdps_dpmi_unlock_region at 0003cb6e ------------------------------ */

/* DPMI function 0600h, Lock Linear Region, written out here so that the cases
   below have something genuinely pinned to release without calling
   fdps_dpmi_lock_region and thereby testing two functions at once.  It takes
   its endpoints the way both image functions do -- the second is the last byte
   -- so the count it locks is the count the unlock is then asked for. */
static int dpmi_lock_linear_range(unsigned base, unsigned last)
{
    union REGS dpmi_in;
    union REGS dpmi_out;
    unsigned length;

    length = (last - base) + 1;
    dpmi_in.x.eax = 0x0600;
    dpmi_in.x.ebx = base >> 16;
    dpmi_in.x.ecx = base & 0xffffu;
    dpmi_in.x.esi = length >> 16;
    dpmi_in.x.edi = length & 0xffffu;
    int386(0x31, &dpmi_in, &dpmi_out);
    return dpmi_out.x.cflag == 0;
}

/* The function code goes to offset 0 of the input set and the base and count
   to offsets 4, 8, 0x10 and 0x14, and the answer is read from the second set's
   carry word at ESP+0x34, which is offset 0x18 of a set beginning at ESP+0x1c.
   A member landing anywhere else would issue INT 31h with the base or the
   count in the wrong register, or read the carry flag out of a word the host
   never wrote -- and because every AIL teardown wrapper discards the result,
   the only symptom in the game would be pages that quietly stay locked. */
static void dpmi_unlock_register_set_has_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) ((char *) &probe.x.eax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.x.ebx - (char *) &probe), 0x4);
    CHECK_EQ((int) ((char *) &probe.x.ecx - (char *) &probe), 0x8);
    CHECK_EQ((int) ((char *) &probe.x.esi - (char *) &probe), 0x10);
    CHECK_EQ((int) ((char *) &probe.x.edi - (char *) &probe), 0x14);
    CHECK_EQ((int) ((char *) &probe.x.cflag - (char *) &probe), 0x18);
    CHECK_EQ((int) (0x1c + 0x18), 0x34);
}

/* A range of this program's own data, locked directly through fn 0600h and
   then released through the function under test with the endpoints the right
   way round.  The host answers a request like this with the carry flag clear,
   and the shared tail turns a clear carry into 1 (CMP [ESP+0x34],0 / SETZ AL)
   -- so a spelling that handed the hardware flag back unchanged would report 0
   here, and fdps_dpmi_unlock_size would pass that failure on to its caller for
   an unlock that actually worked. */
static void dpmi_unlock_returns_one_on_a_clear_carry(void)
{
    unsigned base;
    unsigned last;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    last = base + sizeof(lock_probe_area) - 1;

    CHECK_EQ(dpmi_lock_linear_range(base, last), 1);
    CHECK_EQ(fdps_dpmi_unlock_region(base, last), 1);
}

/* The same range with the arguments exchanged.  CMP EDX,EBX and its two JNC
   arms put the smaller endpoint in EAX and the larger in EDX whichever way
   they arrived, so both spellings name the identical range and must answer
   alike.  Without the normalisation the reversed call would compute a base
   above the range and a count of (small - large) + 1, which wraps to nearly
   4GB -- a request no host can honour.  The six AIL wrappers always pass low
   then high, so this branch is reached only through argument order, never
   through the values themselves. */
static void dpmi_unlock_orders_its_endpoints(void)
{
    unsigned base;
    unsigned last;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    last = base + sizeof(lock_probe_area) - 1;

    CHECK_EQ(dpmi_lock_linear_range(base, last), 1);
    CHECK_EQ(fdps_dpmi_unlock_region(last, base), 1);
}

/* Both endpoints on the same byte.  INC EDX makes the count 1, so this is a
   one-byte request and the host releases the page holding it; dropping the INC
   would make it a zero-byte request, which the DPMI specification does not
   define.  Stated honestly: the case cannot read the byte count back, so it
   catches the missing INC only on a host that refuses a zero-length unlock,
   and it is the layout case above that pins where the count is written. */
static void dpmi_unlock_counts_both_endpoints(void)
{
    unsigned base;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;

    CHECK_EQ(dpmi_lock_linear_range(base, base), 1);
    CHECK_EQ(fdps_dpmi_unlock_region(base, base), 1);
}

/* There is no case here for the other arm of the return, the 0 a set carry
   produces.  It was tried: an unlock of 0xf0000000..0xf0000fff, a linear range
   no DOS/4GW program has mapped, comes back from this host with the carry flag
   CLEAR and the function returns 1.  The host validates neither the address
   nor the length of a 0601h request, so no argument reaches the set-carry
   path, and an assertion written to expect 0 there would be asserting
   something about the host rather than about this function.  That arm -- CMP
   [ESP+0x34],0 with the JNE that yields 0 -- is pinned instead by reading the
   compiled object, recorded in the emit verdict for 0003cb6e. */

/* ---- fdps_dpmi_alloc_dos_memory at 0003ca49 -------------------------- */

/* DPMI function 0006h again, this time for the answer rather than the carry
   flag: CX:DX comes back with the selector's 32-bit linear base.  A DOS memory
   block's selector describes the block, so its base is the block's linear
   address -- which is what makes this the independent witness for both
   `out_linear` and `out_selector`. */
static unsigned dpmi_selector_base(unsigned selector)
{
    union REGS dpmi_in;
    union REGS dpmi_out;

    dpmi_in.x.eax = 0x0006;
    dpmi_in.x.ebx = selector;
    int386(0x31, &dpmi_in, &dpmi_out);
    if (dpmi_out.x.cflag != 0) {
        return 0xffffffffu;
    }
    return ((dpmi_out.x.ecx & 0xffffu) << 16) | (dpmi_out.x.edx & 0xffffu);
}

/* The body writes the function code at [ESP+0] and the paragraph count at
   [ESP+4] of the input set, and reads the output set at ESP+0x1c (eax),
   ESP+0x28 (edx) and ESP+0x34 (cflag), the set itself beginning at ESP+0x1c.
   A member landing anywhere else would issue the interrupt with the paragraph
   count in the wrong register, or read the segment where the selector belongs
   -- and the block would then be reported at an address that is not its
   own. */
static void dpmi_alloc_register_set_has_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) ((char *) &probe.x.eax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.x.ebx - (char *) &probe), 0x4);
    CHECK_EQ((int) ((char *) &probe.x.edx - (char *) &probe), 0xc);
    CHECK_EQ((int) ((char *) &probe.x.cflag - (char *) &probe), 0x18);
    CHECK_EQ((int) (0x1c + 0xc), 0x28);
    CHECK_EQ((int) (0x1c + 0x18), 0x34);
}

/* One real block from the real DPMI host, checked against what the three
   stores say it must look like: the far pointer is the segment shifted left 16
   so its offset half is zero, the linear address is the same segment shifted
   left 4 -- which is the far pointer shifted right 12, exactly as the assembly
   recomputes it -- and the selector is the one DPMI made for this block, whose
   base fn 0006h reports as that same linear address.  Aiming the second and
   third arguments at the wrong words is the failure this catches: the pair
   would then disagree by a factor of 4096. */
static void dpmi_alloc_reports_the_block_three_ways(void)
{
    unsigned linear;
    unsigned real_mode_ptr;
    unsigned selector;

    linear = 0;
    real_mode_ptr = 0;
    selector = 0;
    CHECK_EQ(fdps_dpmi_alloc_dos_memory(TEST_BLOCK_PARAGRAPHS, &linear,
                                        &real_mode_ptr, &selector), 1);
    if (selector == 0) {
        CHECK_EQ(0, 1);   /* no DOS memory to test with; do not pass silently */
        return;
    }

    CHECK_EQ(real_mode_ptr & 0xffffu, 0);
    CHECK_EQ(linear, real_mode_ptr >> 12);
    CHECK_EQ(linear & 0xfu, 0);
    CHECK_EQ(linear < 0x100000u, 1);
    CHECK_EQ(dpmi_selector_base(selector), linear);

    fdps_dpmi_free_dos_memory(linear, real_mode_ptr, selector);
    CHECK_EQ(dpmi_selector_is_valid(selector), 0);
}

/* The whole block, from its first byte to its last, is handed to
   fdps_dpmi_lock_region before the function returns -- MOV EAX,[EBX] / SHR
   EAX,0xc for the base and SHL EDX,4 / ADD EDX,EAX / DEC EDX for the last
   byte.  Unlocking exactly that range afterwards must therefore succeed.
   Stated honestly: on a host that does not count locks, an unlock answers
   cleanly whether or not anything was locked, so this assertion is only as
   strong as the host underneath it.  What it does pin unconditionally is the
   range arithmetic -- an off-by-one or a base recomputed from the wrong
   pointer names a range the host has no reason to accept. */
static void dpmi_alloc_locks_the_whole_block(void)
{
    unsigned linear;
    unsigned real_mode_ptr;
    unsigned selector;

    linear = 0;
    real_mode_ptr = 0;
    selector = 0;
    CHECK_EQ(fdps_dpmi_alloc_dos_memory(TEST_BLOCK_PARAGRAPHS, &linear,
                                        &real_mode_ptr, &selector), 1);
    if (selector == 0) {
        CHECK_EQ(0, 1);
        return;
    }

    CHECK_EQ(dpmi_unlock_linear_range(linear,
                                      linear + TEST_BLOCK_PARAGRAPHS * 16 - 1),
             1);
    fdps_dpmi_free_dos_memory(linear, real_mode_ptr, selector);
}

/* A request for 0xffff paragraphs is 1,048,560 bytes, more than the 640K of
   conventional memory a DOS machine can ever hand back, so fn 0100h returns
   with the carry flag set.  The assembly then jumps straight to XOR ESI,ESI
   and out: not one of the three out-parameters is written.  The sentinels are
   what says so -- the intuitive defensive spelling clears them, and the two
   AIL loaders point all three at fields of the driver descriptor they are
   filling in, so clearing them would blank fields on a path where the original
   leaves them alone. */
static void dpmi_alloc_writes_nothing_when_dpmi_refuses(void)
{
    unsigned linear;
    unsigned real_mode_ptr;
    unsigned selector;

    linear = 0x11111111u;
    real_mode_ptr = 0x22222222u;
    selector = 0x33333333u;

    CHECK_EQ(fdps_dpmi_alloc_dos_memory(0xffffu, &linear, &real_mode_ptr,
                                        &selector), 0);
    CHECK_EQ(linear, 0x11111111u);
    CHECK_EQ(real_mode_ptr, 0x22222222u);
    CHECK_EQ(selector, 0x33333333u);
}

/* ---- fdps_dpmi_lock_size at 0003cb93 --------------------------------- */

/* A resident range of this program's own data, named the way this function
   takes it -- a base and a byte count -- with the count one short of the array
   so the extent actually pinned, size + 1 bytes, is exactly the array.  The
   assembly has no epilogue at all: CALL 0003cb01 / ADD ESP,8 / RET, so
   whatever the primitive left in EAX is the answer.  A wrapper that computed a
   result of its own, or returned a constant, would fail here.  The range is
   released again through the helper above, which takes the same inclusive
   endpoint pair the wrapper builds. */
static void dpmi_lock_size_returns_the_primitives_flag(void)
{
    unsigned base;
    unsigned size;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    size = sizeof(lock_probe_area) - 1;

    CHECK_EQ(fdps_dpmi_lock_size(base, size), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, base + size), 1);
}

/* The plain object size an AIL caller passes -- 0003d586 pushes 4, 0004521f
   pushes 0x40, 0004523d pushes 0x100 -- with the far endpoint therefore one
   byte past the block.  The range released afterwards is that same pair,
   [base, base + size], which is size + 1 bytes: releasing [base, base + size -
   1] instead would leave the last byte's page pinned.  Stated honestly, this
   host neither counts locks nor refuses an unlock of a range it never locked,
   so the assertion cannot see the byte count; what it does pin is that the call
   is made over a range the host accepts and that its flag comes back.  The
   arithmetic itself is pinned by the compiled object, in the verdict. */
static void dpmi_lock_size_ends_at_base_plus_size(void)
{
    unsigned base;
    unsigned size;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    size = sizeof(lock_probe_area);

    CHECK_EQ(fdps_dpmi_lock_size(base, size), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, base + size), 1);
}

/* A size of zero.  ADD EAX,0 leaves the endpoint on `base`, the primitive's
   INC EDX makes the count 1, and the request is a one-byte lock of the page
   holding `base` -- not the zero-byte request the DPMI specification does not
   define.  This is also the one shape where a body that passed `size` itself as
   the far endpoint is visibly different: it would ask the host to order 0
   against `base` and lock everything from linear 0 upwards. */
static void dpmi_lock_size_of_zero_locks_one_byte(void)
{
    unsigned base;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;

    CHECK_EQ(fdps_dpmi_lock_size(base, 0), 1);
    CHECK_EQ(dpmi_unlock_linear_range(base, base), 1);
}

/* ---- fdps_dpmi_unlock_size at 0003cbaa -------------------------------- */

/* A range of this program's own data, pinned directly through fn 0600h over the
   inclusive pair [base, base + size] and then released through the function
   under test, which is handed the same block the other way round -- a base and
   a byte count.  The count is one short of the array so that the extent the
   wrapper actually releases, size + 1 bytes, is exactly the array that was
   pinned.  The assembly has no epilogue at all: CALL 0003cb6e / ADD ESP,8 /
   RET, so whatever the primitive left in EAX is the answer, and a wrapper that
   computed a result of its own or returned a constant would fail here. */
static void dpmi_unlock_size_returns_the_primitives_flag(void)
{
    unsigned base;
    unsigned size;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    size = sizeof(lock_probe_area) - 1;

    CHECK_EQ(dpmi_lock_linear_range(base, base + size), 1);
    CHECK_EQ(fdps_dpmi_unlock_size(base, size), 1);
}

/* The plain object size an AIL teardown wrapper passes -- the run beginning at
   00045b42 pushes 0x40, 0x48, 0x100, 0x18, 4 and 4 -- with the far endpoint
   therefore one byte past the block.  The range pinned beforehand is that same pair,
   [base, base + size], which is size + 1 bytes, so a body that released
   [base, base + size - 1] would leave the last byte's page locked for the rest
   of the run and never say so, because all four callers discard the result.
   Stated honestly, this host neither counts locks nor refuses an unlock of a
   range it never locked, so the assertion cannot see the byte count; what it
   does pin is that the call is made over a range the host accepts and that its
   flag comes back.  The arithmetic itself is pinned by the compiled object, in
   the verdict. */
static void dpmi_unlock_size_ends_at_base_plus_size(void)
{
    unsigned base;
    unsigned size;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;
    size = sizeof(lock_probe_area);

    CHECK_EQ(dpmi_lock_linear_range(base, base + size), 1);
    CHECK_EQ(fdps_dpmi_unlock_size(base, size), 1);
}

/* A size of zero.  ADD EAX,0 leaves the endpoint on `base`, the primitive's
   INC EDX makes the count 1, and the request is a one-byte unlock of the page
   holding `base` -- not the zero-byte request the DPMI specification does not
   define.  This is also the one shape where a body that passed `size` itself as
   the far endpoint is visibly different: it would ask the host to order 0
   against `base` and release everything from linear 0 upwards. */
static void dpmi_unlock_size_of_zero_unlocks_one_byte(void)
{
    unsigned base;

    fill_lock_probe_area();
    base = (unsigned) (char *) lock_probe_area;

    CHECK_EQ(dpmi_lock_linear_range(base, base), 1);
    CHECK_EQ(fdps_dpmi_unlock_size(base, 0), 1);
}

/* There is no case here for the exchanged argument pair, the shape that would
   pin `size` as the addend rather than as an endpoint to be ordered.  It was
   written and then withdrawn: fdps_dpmi_unlock_size(0x40, base) names the range
   [0x40, base + 0x40], nearly everything the program has mapped, and this host
   validates neither the address nor the length of a 0601h request -- so the
   call answers 1 exactly as the correct spelling does, proving nothing, while
   actually unpinning whatever DOS/4GW and the AIL drivers had locked underneath
   the rest of this executable.  The argument order is pinned instead by reading
   the compiled object, recorded in the emit verdict for 0003cbaa. */

void run_dpmi_tests(void)
{
    RUN_TEST(dpmi_free_register_set_has_the_image_layout);
    RUN_TEST(dpmi_free_releases_the_block);
    RUN_TEST(dpmi_free_reads_only_the_third_argument);
    RUN_TEST(dpmi_lock_register_set_has_the_image_layout);
    RUN_TEST(dpmi_lock_returns_one_on_a_clear_carry);
    RUN_TEST(dpmi_lock_orders_its_endpoints);
    RUN_TEST(dpmi_lock_counts_both_endpoints);
    RUN_TEST(dpmi_unlock_register_set_has_the_image_layout);
    RUN_TEST(dpmi_unlock_returns_one_on_a_clear_carry);
    RUN_TEST(dpmi_unlock_orders_its_endpoints);
    RUN_TEST(dpmi_unlock_counts_both_endpoints);
    RUN_TEST(dpmi_alloc_register_set_has_the_image_layout);
    RUN_TEST(dpmi_alloc_reports_the_block_three_ways);
    RUN_TEST(dpmi_alloc_locks_the_whole_block);
    RUN_TEST(dpmi_alloc_writes_nothing_when_dpmi_refuses);
    RUN_TEST(dpmi_lock_size_returns_the_primitives_flag);
    RUN_TEST(dpmi_lock_size_ends_at_base_plus_size);
    RUN_TEST(dpmi_lock_size_of_zero_locks_one_byte);
    RUN_TEST(dpmi_unlock_size_returns_the_primitives_flag);
    RUN_TEST(dpmi_unlock_size_ends_at_base_plus_size);
    RUN_TEST(dpmi_unlock_size_of_zero_unlocks_one_byte);
}
