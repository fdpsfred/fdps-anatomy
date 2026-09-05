/* tests/btlend.c -- cover for src/btlend.c.
 *
 * Three subjects: fdps_battle_count_remaining_units_on_side at 00018350,
 * fdps_battle_check_default_end_conditions at 0003a2e0 and
 * fdps_battle_show_win_fail_window at 00017ca0.  The notes below belong to the
 * first two; the third has its own banner further down.
 *
 * Expected values come from the assembly at 00018350: the CMP EAX,[0x00060150]
 * / JL at 0001836d that bounds the walk with a signed compare before the body
 * runs, the MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff at 00018391 that widens
 * the side byte without sign, the CMP EAX,[EBP+0x14] / JNZ at 00018399 that
 * matches it as an equality against the argument, the TEST EAX,EAX / JZ at
 * 000183aa that counts the unit only when fdps_unit_is_retired answered zero,
 * and the INC dword ptr [EBP-0xc] at 000183b3 that bumps the tally by one per
 * surviving unit.  The retirement flag is bit 0 of the record's flags byte at
 * offset 5, from src/unit.c's fdps_unit_is_retired at 000109b0.  None of them
 * is read off the emitted C.
 *
 * The three side codes exercised are the ones the only caller pushes --
 * PUSH 0x0 at 00017e14, PUSH 0x2 at 00017e3a and PUSH 0x1 at 00017e60, all
 * inside fdps_battle_show_win_fail_window.
 *
 * The unit array is staged here rather than read from a game file: the
 * function takes its whole input from its argument, from the unit count global
 * and from the records the accessor resolves, so pointing the array global at
 * a local block is the only way to reach the walk.  Nothing below asserts what
 * either global holds on its own -- ticket 23 owns that.
 *
 * The standard end test's expected values come from the assembly at 0003a2e0:
 * the CMP dword ptr [0x00069da0],0x0 / JNZ at 0003a2ec that abandons the whole
 * body for a code that is already non-zero, the MOV dword ptr
 * [0x00069da0],0x2 at 0003a2f9 that writes the cleared verdict before anything
 * is examined, the CMP EAX,[0x00060150] / JL at 0003a30d that bounds the walk
 * signed, the CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 and the MOV AL,byte
 * ptr [EAX+0x5] / AND AL,0x1 at 0003a33a that are the walk's two tests, the
 * CMP [0x00069cf4],0x10 and CMP [0x00069cf4],0x15 at 0003a356 and 0003a35f
 * that pick between PUSH 0x3 and PUSH 0x0, and the two unguarded MOV dword ptr
 * [0x00069da0],0x1 stores at 0003a376 and 0003a390 that are reached without
 * any test of what the walk concluded.  That the chapter ids are 0-based is
 * from the dispatch table at 0x00060304, whose entry 0x10 is
 * fdps_chapter_17_end at 0003ad40.  The 0/1/2 meanings of the code are
 * program_info/architecture.md.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "btlend.h"

/* Eight slots, so a unit can be parked one past the count the walk is given
   and a stray step past the bound would be visible. */
#define STAGE_UNITS 8

/* The side codes the result window asks about, from the caller's pushes. */
#define SIDE_ENEMY  0
#define SIDE_THIRD  1
#define SIDE_PLAYER 2

/* Bit 0 of the flags byte is the retirement flag; bit 7 is the separate
   acted-this-turn flag that fdps_unit_is_retired does not answer for. */
#define FLAG_RETIRED 0x01
#define FLAG_ACTED   0x80

static struct fdps_unit_record stage_units[STAGE_UNITS];

/* Zero every slot and publish the block.  Every unit is then a non-retired
   member of side 0, which is the state each case edits only the fields it is
   about. */
static void stage(int live_unit_count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) stage_units;
    for (i = 0; i < (int) sizeof(stage_units); i++) {
        bytes[i] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = live_unit_count;
}

static void stage_unit(int unit_index, int side, int flags)
{
    stage_units[unit_index].side = (unsigned char) side;
    stage_units[unit_index].flags = (unsigned char) flags;
}

/* The record offsets the walk addresses as literals: +0x6 for the side byte it
   reads itself and +0x5 for the flags byte the retirement test reads, plus the
   0x50 stride the accessor multiplies by.  If the record measured anything
   else every lookup past index 0 would read a different unit. */
static void record_layout_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
}

/* CMP EAX,[0x00060150] / JL guards the body, so the count is a bound tested
   first and not a do-while count.  A count of 0 counts nothing, and because
   the compare is the signed JL a negative count does too rather than running
   away as an unsigned one would. */
static void an_empty_battle_counts_nothing(void)
{
    stage(0);
    stage_unit(0, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 0);
    stage(-1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
}

/* The bound is exclusive: the unit sitting at index == count is outside the
   walk even though the array holds it. */
static void the_bound_is_exclusive(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 3);
}

/* CMP / JNZ over the widened side byte is an equality, so each of the three
   codes the window asks about picks out its own units and nothing else.  The
   totals also pin the INC: one per matching unit, not a flag or a bitwise
   accumulation. */
static void each_side_code_counts_its_own_units(void)
{
    stage(6);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_THIRD, 0);
    stage_unit(4, SIDE_ENEMY, 0);
    stage_unit(5, SIDE_ENEMY, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 3);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_THIRD), 1);
}

/* A code no unit carries is not an error and is not a wildcard: it counts
   nothing.  Checked at both ends of the byte range so that a spelling which
   treated the argument as a truth value or as a range would show. */
static void an_unused_side_code_counts_nothing(void)
{
    stage(3);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_unit(2, SIDE_THIRD, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(3), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0xff), 0);
}

/* TEST EAX,EAX / JZ after the retirement call: a unit whose flags bit 0 is set
   is passed over even though its side matches. */
static void retired_units_are_not_counted(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(2, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 0);
}

/* Only bit 0 of the flags byte retires a unit.  Bit 7 is the acted-this-turn
   flag and a unit that has already moved is still standing, so a test written
   against the whole byte rather than against bit 0 would lose it. */
static void the_acted_flag_does_not_retire_a_unit(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, FLAG_ACTED);
    stage_unit(1, SIDE_PLAYER, 0xfe);
    stage_unit(2, SIDE_PLAYER, FLAG_ACTED | FLAG_RETIRED);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 2);
}

/* AND EAX,0xff widens the side byte without sign, so a code with the top bit
   set matches the positive number a caller would push and never a negative
   one.  Reading the byte through a signed char inverts both of these. */
static void the_side_byte_is_zero_extended(void)
{
    stage(2);
    stage_unit(0, 0x80, 0);
    stage_unit(1, 0xff, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0x80), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(-128), 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(0xff), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(-1), 0);
}

/* The walk resolves each record through the accessor with the loop index, so
   the units it reads are the ones at 0..count-1 of the published block and the
   0x50 stride lands on each in turn.  Moving the block moves the answer,
   which is what re-resolving on every iteration buys. */
static void the_walk_follows_the_published_array(void)
{
    stage(4);
    stage_unit(0, SIDE_ENEMY, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(2, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_ENEMY, 0);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 3);
    data_fdps_map_unit_array_ptr = (unsigned char *) &stage_units[2];
    data_fdps_map_unit_count = 2;
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_ENEMY), 1);
    CHECK_EQ(fdps_battle_count_remaining_units_on_side(SIDE_PLAYER), 1);
}

/* The other two globals the standard end test works from: the chapter id it
   branches on and the battle-end code it both reads as a gate and writes as
   its answer.  Staged for the same reason the array is -- the function takes
   no arguments at all. */
static void stage_chapter(int chapter_id, int battle_end_code)
{
    data_fdps_chapter_current_chapter_id = chapter_id;
    data_fdps_chapter_event_or_battle_end_code = (unsigned int) battle_end_code;
}

static int end_code(void)
{
    return (int) data_fdps_chapter_event_or_battle_end_code;
}

/* CMP dword ptr [0x00069da0],0x0 / JNZ jumps to the epilogue, so a code some
   chapter event already recorded is not recomputed.  Each value below would be
   overwritten by a body that ran: the array holds a live enemy, which would
   settle the code at 0, and the slot 0 unit has not retired. */
static void a_recorded_verdict_is_never_recomputed(void)
{
    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_chapter(0, 1);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0, 2);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0, 7);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 7);
}

/* The 2 written up front survives when the walk finds no live enemy, whether
   the enemies are all retired or the battle holds none at all. */
static void every_enemy_gone_clears_the_chapter(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_PLAYER, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* One unit on side 0 whose flags bit 0 is clear puts the code back to 0, and
   nothing restores the 2 afterwards -- so its position in the array does not
   matter and retired enemies following it do not undo it. */
static void one_live_enemy_keeps_the_battle_going(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(3, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, 0);
    stage_unit(3, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);
}

/* AND AL,0x1 masks bit 0 alone, so an enemy that has merely acted this turn --
   bit 7 -- is still standing, and so is one carrying every bit but bit 0. */
static void only_bit_zero_retires_an_enemy(void)
{
    stage(2);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, FLAG_ACTED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, 0xfe);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage_unit(1, SIDE_ENEMY, FLAG_ACTED | FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* CMP byte ptr [EAX+0x6],0x0 is an equality against zero, so only side 0 is an
   enemy: neither the player side nor the third side nor an unused code holds
   the battle open. */
static void only_side_zero_holds_the_battle_open(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_THIRD, 0);
    stage_unit(2, 3, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage_unit(2, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);
}

/* The bound is the live unit count, tested before the body: a live enemy at
   index == count is invisible, and a count of zero or below examines no record
   at all rather than running away as an unsigned bound would. */
static void the_walk_stops_at_the_live_unit_count(void)
{
    stage(1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(1, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    data_fdps_map_unit_count = 2;
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 0);

    stage(0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);

    stage(-1);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* The defeat store carries no guard, and it runs after the walk: a defeat
   overwrites the 2 the walk left standing as readily as it overwrites the 0.
   This is the case an if/else spelling gets backwards. */
static void a_retired_watched_unit_outranks_the_walk(void)
{
    stage(3);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_unit(2, SIDE_ENEMY, 0);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
}

/* Chapter ids 0x10 and 0x15 -- chapters 17 and 22 -- watch slot 3 and every
   other id watches slot 0.  The test is two equalities and not a range, so the
   ids on either side of each take the other arm. */
static void chapters_17_and_22_watch_slot_three(void)
{
    stage(4);
    stage_unit(0, SIDE_PLAYER, FLAG_RETIRED);
    stage_unit(1, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(2, SIDE_ENEMY, FLAG_RETIRED);
    stage_unit(3, SIDE_PLAYER, 0);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0x15, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
    stage_chapter(0x0f, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x11, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x14, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x16, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0x15, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);
    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* The defeat test pushes a literal slot index and nothing compares it with the
   live unit count, so slot 3 is read in chapter 17 even in a battle that
   deployed one unit. */
static void the_watched_slot_ignores_the_unit_count(void)
{
    stage(1);
    stage_unit(0, SIDE_PLAYER, 0);
    stage_unit(3, SIDE_PLAYER, FLAG_RETIRED);
    stage_chapter(0x10, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 1);

    stage_chapter(0, 0);
    fdps_battle_check_default_end_conditions();
    CHECK_EQ(end_code(), 2);
}

/* ------------------------------------------------------------------ *
 * fdps_battle_show_win_fail_window @ 00017ca0
 *
 * Expected values come from the assembly, never from the emitted C: PUSH
 * 0xa0504 / PUSH 0x140 / PUSH 0x138 / PUSH 0xc0 against the scene page's
 * 0x21d8 and 0x168 at 0001801c-0001803e for the presented window; CMP dword
 * ptr [EBP-0xc],0x13 / JL at 00017ed6 and CMP dword ptr [EBP-0xc],0xd / JL at
 * 000181b9 for the two slide lengths; the CALL to 0x000567b3 at 0001806a that
 * empties the ring before the hold, the CALL to 0x000567be at 0001806f at the
 * TOP of the hold and the AND EAX,0xff / CMP EAX,0x7f / JLE at 00018074 that
 * decides it; the CALL to 0x00018930 at 00017d33 that reloads the nine data
 * tables with nothing freeing the old ones; and the three frees at 00017d55,
 * 00017da3 and 0001805d against the mallocs at 00017d5d, 00017ee9 and the two
 * that follow.  That MISC.VFS is the container and WinFail.Cel the member is
 * MOV EAX,0x60128 at 00017d00 and MOV EAX,0x615f4 at 00017d3f.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  Four channels, and each case says which
 * it uses: the VGA aperture, which only answers in a graphics mode, so every
 * run puts the adapter into mode 13h the way the game does, paints a known
 * pattern over it, calls, captures the frame and returns to text mode; the
 * heap, walked with _heapwalk the way tests/main.c walks it; the nine data
 * table globals; and a scancode script the installed timer interrupt feeds
 * into the keyboard ring.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every frame of all three phases ends
 * waiting for data_fdps_timer_tick_counter to change, and in the game that
 * counter is advanced by the timer handler off AIL's interrupt.  Nothing
 * advances it in a test image, so the first frame would never end.  Each run
 * hooks IRQ0 for the duration of the call and chains to the handler that was
 * there.
 *
 * HOW THE HOLD IS ENDED, AND WHY THE SCRIPT IS FED FROM THE ISR.  The function
 * empties the ring itself before the hold begins, so nothing staged before the
 * call survives to be read; the ring has to be fed while the call is running,
 * which is exactly what the game's INT 09h handler does.  The ISR here pushes
 * one code ONLY WHEN THE RING IS EMPTY, which makes the count exact however
 * long a frame takes: a code is pushed only after the previous one has been
 * read, so the run consumes the script in order at one entry per read.  The
 * script is 0x1c, 0x80, 0x7f: the make code first, which must be thrown away
 * by the flush and so is never read; then the exact threshold value, which
 * must not end the hold; then the value one below it, which must.  Four pushes
 * at the return is the whole assertion -- one during the slide-in, two read by
 * the hold, and one during the slide-out, which is also what says a phase ran
 * after the hold ended.
 *
 * WHAT IS ON THE SCREEN AND WHAT IS NOT.  The strips are blitted into a scene
 * page malloc hands over and nothing clears, and with no layers, no units and
 * no cursor staged fdps_draw_scene_layers writes nothing into it -- so what
 * surrounds the panel in the presented frame is the allocator's leftovers and
 * cannot be asserted.  What is still determined is the frame's edge: the
 * present is 312 x 192 at screen (4, 4), so the four bands outside it keep the
 * pattern exactly.  Where each strip lands on each frame, the artwork
 * WinFail.Cel decodes to and the six numbers stamped onto the panel are not
 * reachable from here -- the panel page is allocated and freed inside the call
 * -- and are playtest contracts.
 *
 * THE SYNTHETIC NUMBER GLYPH SHEET is a byte buffer published through
 * data_fdps_number_glyph_sheet_ptr the way tests/msgwin.c stages a Command.cel:
 * thirteen entries of eight rows, each row one 6-pixel fill run
 * (resource_info/cel.md), which is the rectangle fdps_draw_number blits.  It is
 * staged because that global is null until ticket 23 and the six draws would
 * read through it regardless.  The chapter text block is the same kind of
 * fixture: a four-entry offset table whose entries 2 and 3 both point at a
 * lone -1 terminator, so both condition draws walk an empty stream.
 *
 * The not-found path is not exercised: it ends in exit(1), so a test of it
 * would take the whole run with it.  A container without WINFAIL.CEL in it is
 * worse -- the loader waits for a keypress that never comes -- which is why
 * the first case below reads the shipped container's own entry table and every
 * other case skips itself unless that member is there.
 * ------------------------------------------------------------------ */

/* The container and the member, in the container's own upper case -- which is
   the spelling the lookup produces from the function's "WinFail.Cel" literal
   before it compares (resource_info/vfs.md). */
#define WIN_ARCHIVE "MISC.VFS"
#define WIN_MEMBER "WINFAIL.CEL"

/* The container's header and entry table, from resource_info/vfs.md: a 35-byte
   header carrying the table offset at 0x05 and the entry count at 0x07, then
   26-byte entries each opening with a NUL-terminated 8.3 name. */
#define VFS_HEADER_BYTES 35
#define VFS_TABLE_OFFSET_AT 0x05
#define VFS_ENTRY_COUNT_AT 0x07
#define VFS_ENTRY_BYTES 26

/* The adapter, and the two modes the run moves between. */
#define WIN_VGA_BASE 0x000a0000
#define WIN_MODE_TEXT 0x03
#define WIN_MODE_320X200X256 0x13
#define WIN_SCREEN_PITCH 320
#define WIN_SCREEN_ROWS 200
#define WIN_SCREEN_BYTES (WIN_SCREEN_PITCH * WIN_SCREEN_ROWS)

/* The part of the screen a frame is presented into: 312 x 192 at (4, 4). */
#define WIN_VIEW_ROW 4
#define WIN_VIEW_COL 4
#define WIN_VIEW_W 312
#define WIN_VIEW_H 192

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define WIN_TIMER_VECTOR 8

/* The scancode script, and what the run must do with it.  0x1c is Enter's make
   code, 0x80 the exact value the threshold lets through and 0x7f the first one
   below it.  Anything past the script is a make code, so a run that got away
   from the script still ends rather than hanging. */
#define WIN_SCRIPT_LEN 3
#define WIN_MAKE_CODE 0x1c
#define WIN_AT_THRESHOLD 0x80
#define WIN_BELOW_THRESHOLD 0x7f
#define WIN_EXPECTED_PUSHES 4

/* The two slides, whose frames are each paced to one tick, so no run can take
   fewer ticks than their sum. */
#define WIN_SLIDE_IN_FRAMES 19
#define WIN_SLIDE_OUT_FRAMES 13

/* The nine data tables the open reloads.  Nothing frees the set that was
   there, so a run adds nine used heap entries and takes none away. */
#define WIN_TABLE_COUNT 9

/* The synthetic number glyph sheet: a 15-byte header, the offset table at the
   hardwired 0x0f with one entry per glyph plus the closing sentinel, then one
   stream per glyph.  Thirteen glyphs is one colour row of the sheet, which is
   what fdps_draw_number indexes with colour row 0. */
#define WIN_CEL_TABLE_AT 0x0f
#define WIN_GLYPH_ENTRIES 13
#define WIN_GLYPH_W 6
#define WIN_GLYPH_H 8
#define WIN_CEL_FILL_6 0x05
#define WIN_GLYPH_STREAM_BYTES (WIN_GLYPH_H * 2)
#define WIN_GLYPH_STREAM_BASE (WIN_CEL_TABLE_AT + (WIN_GLYPH_ENTRIES + 1) * 4)
#define WIN_GLYPH_SHEET_BYTES \
    (WIN_GLYPH_STREAM_BASE + WIN_GLYPH_ENTRIES * WIN_GLYPH_STREAM_BYTES)

/* The synthetic chapter text block: four 16-bit entry offsets, then the -1 the
   walk stops at.  Entries 2 and 3 are the two conditions the panel draws. */
#define WIN_TEXT_ENTRIES 4
#define WIN_TEXT_STREAM_AT (WIN_TEXT_ENTRIES * 2)
#define WIN_TEXT_BYTES (WIN_TEXT_STREAM_AT + 2)

/* Values put in the three globals the panel prints, small enough that each
   fits its field.  Nothing below asserts them -- they are not reachable from
   outside the call -- but a field they overflowed would draw '?' glyphs. */
#define WIN_CHAPTER_ID 5
#define WIN_TURN_COUNT 12
#define WIN_PARTY_GOLD 1234

/* A value data_fdps_scene_layer_scroll_last_tick cannot reach on its own here,
   so that the compositor having run is visible as that global no longer
   holding it. */
#define WIN_SCROLL_MARKER 0x7fffff00

static unsigned char win_glyph_sheet[WIN_GLYPH_SHEET_BYTES];
static unsigned char win_text_block[WIN_TEXT_BYTES];
static unsigned char win_capture[WIN_SCREEN_BYTES];
static unsigned char win_script[WIN_SCRIPT_LEN] = {
    WIN_MAKE_CODE, WIN_AT_THRESHOLD, WIN_BELOW_THRESHOLD
};
static void (__interrupt __far *win_saved_timer)();
static int win_pushed;
static int win_checked = 0;
static int win_ready = 0;
static int win_heap_before;
static int win_heap_after;
static unsigned int win_scroll_after;
static unsigned int win_ticks;
static unsigned char *win_tables_before[WIN_TABLE_COUNT];
static unsigned char *win_tables_after[WIN_TABLE_COUNT];

/* The nine globals fdps_load_data_tables fills, in the order it fills them. */
static unsigned char **const win_table_slots[WIN_TABLE_COUNT] = {
    &data_fdps_battle_character_base_table_ptr,
    &data_fdps_battle_character_growth_table_ptr,
    &data_fdps_item_effect_table_ptr,
    &data_fdps_battle_enemy_data_table_ptr,
    &data_fdps_class_table_ptr,
    &data_fdps_class_equip_table_ptr,
    &data_fdps_battle_spell_effect_table_ptr,
    &data_fdps_spell_learning_table_ptr,
    &data_fdps_promotion_table_ptr
};

/* Advances the counter the way the game's timer does, and feeds the script
   into the ring the way the game's INT 09h handler feeds keystrokes -- but
   only while the ring is empty, so exactly one code is pushed per code read
   and the push count is a reading of the run rather than of the clock. */
static void __interrupt __far win_timer_isr(void)
{
    unsigned char code;

    ++data_fdps_timer_tick_counter;
    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        code = WIN_MAKE_CODE;
        if (win_pushed < WIN_SCRIPT_LEN) {
            code = win_script[win_pushed];
        }
        data_fdps_input_scancode_queue[
            data_fdps_input_scancode_queue_write_index] = code;
        data_fdps_input_scancode_queue_write_index =
            (data_fdps_input_scancode_queue_write_index + 1)
            % SCANCODE_QUEUE_LEN;
        win_pushed++;
    }
    _chain_intr(win_saved_timer);
}

static void win_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void win_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* Whether the shipped container names the member the function asks for.  Read
   straight out of the container's entry table, so it is the file that answers
   and not anything under src/. */
static int archive_has_member(char *path, char *member)
{
    FILE *fp;
    unsigned char header[VFS_HEADER_BYTES];
    unsigned char entry[VFS_ENTRY_BYTES];
    long table_at;
    long count;
    long index;
    int found;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fread(header, 1, VFS_HEADER_BYTES, fp) != VFS_HEADER_BYTES) {
        fclose(fp);
        return 0;
    }
    table_at = (long) header[VFS_TABLE_OFFSET_AT]
               | ((long) header[VFS_TABLE_OFFSET_AT + 1] << 8);
    count = (long) header[VFS_ENTRY_COUNT_AT]
            | ((long) header[VFS_ENTRY_COUNT_AT + 1] << 8)
            | ((long) header[VFS_ENTRY_COUNT_AT + 2] << 16);
    fseek(fp, table_at, SEEK_SET);
    found = 0;
    for (index = 0; index < count; index++) {
        if (fread(entry, 1, VFS_ENTRY_BYTES, fp) != VFS_ENTRY_BYTES) {
            break;
        }
        entry[12] = 0;
        if (strcmp((char *) entry, member) == 0) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

/* Used entries currently in the heap, counted the way tests/main.c counts
   them: a used entry becomes a free entry the moment it is released. */
static int win_used_heap_blocks(void)
{
    struct _heapinfo entry;
    int used;

    used = 0;
    entry._pentry = NULL;
    while (_heapwalk(&entry) == _HEAPOK) {
        if (entry._useflag == _USEDENTRY) {
            used++;
        }
    }
    return used;
}

static void win_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Every byte has bit 7 set, so none of it is zero and a byte still holding it
   is a byte the present did not write. */
static int win_pattern(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void win_paint_pattern(unsigned char *page)
{
    int row;
    int col;

    for (row = 0; row < WIN_SCREEN_ROWS; row++) {
        for (col = 0; col < WIN_SCREEN_PITCH; col++) {
            page[(long) row * WIN_SCREEN_PITCH + col] =
                (unsigned char) win_pattern(row, col);
        }
    }
}

/* Bytes of the captured page that are no longer the pattern, over a window of
   rows and columns. */
static long win_changed(int first_row, int rows, int first_col, int cols)
{
    long changed;
    int row;
    int col;

    changed = 0;
    for (row = first_row; row < first_row + rows; row++) {
        for (col = first_col; col < first_col + cols; col++) {
            if (win_capture[(long) row * WIN_SCREEN_PITCH + col]
                    != (unsigned char) win_pattern(row, col)) {
                changed++;
            }
        }
    }
    return changed;
}

/* Everything the call reads that a test image does not otherwise have. */
static void win_stage(void)
{
    int index;
    int row;
    int at;

    memset(win_glyph_sheet, 0, (size_t) WIN_GLYPH_SHEET_BYTES);
    win_glyph_sheet[0] = 'C';
    win_glyph_sheet[1] = 'E';
    win_glyph_sheet[2] = 'L';
    win_u16(win_glyph_sheet, 0x07, WIN_GLYPH_W);
    win_u16(win_glyph_sheet, 0x09, WIN_GLYPH_H);
    win_u16(win_glyph_sheet, 0x0b, WIN_GLYPH_ENTRIES);
    for (index = 0; index < WIN_GLYPH_ENTRIES; index++) {
        at = WIN_GLYPH_STREAM_BASE + index * WIN_GLYPH_STREAM_BYTES;
        win_u32(win_glyph_sheet, WIN_CEL_TABLE_AT + index * 4,
                (unsigned long) at);
        for (row = 0; row < WIN_GLYPH_H; row++) {
            win_glyph_sheet[at + row * 2] = WIN_CEL_FILL_6;
            win_glyph_sheet[at + row * 2 + 1] = (unsigned char) (0x30 + index);
        }
    }
    win_u32(win_glyph_sheet, WIN_CEL_TABLE_AT + WIN_GLYPH_ENTRIES * 4,
            (unsigned long) WIN_GLYPH_SHEET_BYTES);

    memset(win_text_block, 0, (size_t) WIN_TEXT_BYTES);
    for (index = 0; index < WIN_TEXT_ENTRIES; index++) {
        win_u16(win_text_block, index * 2, WIN_TEXT_STREAM_AT);
    }
    win_text_block[WIN_TEXT_STREAM_AT] = 0xff;
    win_text_block[WIN_TEXT_STREAM_AT + 1] = 0xff;

    data_fdps_number_glyph_sheet_ptr = win_glyph_sheet;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_current_chapter_text_ptr = win_text_block;
    data_fdps_font_line_height = WIN_GLYPH_H;
    data_fdps_chapter_current_chapter_id = WIN_CHAPTER_ID;
    data_fdps_battle_turn_counter = WIN_TURN_COUNT;
    data_fdps_shared_party_total_gold = WIN_PARTY_GOLD;
    data_fdps_map_unit_array_ptr = (unsigned char *) stage_units;
    data_fdps_map_unit_count = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_scene_layer_scroll_last_tick = WIN_SCROLL_MARKER;
    data_fdps_timer_tick_counter = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    win_pushed = 0;
}

/* One whole panel, with the adapter in the mode the game shows it in, the
   pattern on the screen and a timer interrupt running.  Leaves the frame in
   win_capture[] and the four other readings in their statics. */
static void win_execute(void)
{
    int index;

    win_stage();
    for (index = 0; index < WIN_TABLE_COUNT; index++) {
        win_tables_before[index] = *win_table_slots[index];
    }
    win_heap_before = win_used_heap_blocks();

    win_set_mode(WIN_MODE_320X200X256);
    win_paint_pattern((unsigned char *) WIN_VGA_BASE);

    win_saved_timer = _dos_getvect(WIN_TIMER_VECTOR);
    _dos_setvect(WIN_TIMER_VECTOR, win_timer_isr);
    fdps_battle_show_win_fail_window();
    _dos_setvect(WIN_TIMER_VECTOR, win_saved_timer);

    memmove(win_capture, (void *) WIN_VGA_BASE, (size_t) WIN_SCREEN_BYTES);
    win_set_mode(WIN_MODE_TEXT);

    win_heap_after = win_used_heap_blocks();
    win_scroll_after = data_fdps_scene_layer_scroll_last_tick;
    win_ticks = data_fdps_timer_tick_counter;
    for (index = 0; index < WIN_TABLE_COUNT; index++) {
        win_tables_after[index] = *win_table_slots[index];
    }
}

/* The panel is shown twice and only the second is measured: the first call is
   what warms the CRT paths the heap reading would otherwise count -- stdio's
   buffers among them -- so that the difference the second leaves behind is the
   function's own and nothing else's. */
static int win_prepare(void)
{
    if (win_checked) {
        return win_ready;
    }
    win_checked = 1;
    if (!archive_has_member(WIN_ARCHIVE, WIN_MEMBER)) {
        return 0;
    }
    win_execute();
    win_execute();
    win_ready = 1;
    return 1;
}

/* ---------------------------------------------------------------------- */

/* The member the function names is in the container it names.  Read out of the
   shipped MISC.VFS's own entry table: nothing in the call can report a miss --
   the loader waits on a key and then ends the process -- so this is the only
   place the pairing can be checked at all, and it is also what makes every
   case below safe to run. */
static void the_container_holds_the_sheet_the_panel_is_built_from(void)
{
    CHECK_EQ(archive_has_member(WIN_ARCHIVE, WIN_MEMBER), 1);
}

/* PUSH 0xa0504 / PUSH 0x140 against PUSH 0x138 / PUSH 0xc0: the present writes
   312 x 192 pixels at screen (4, 4) and nothing else, so the four bands around
   it still hold the pattern to the byte.  A width, a height, an origin or a
   destination stride wrong on any frame spills into one of them. */
static void nothing_outside_the_presented_window_is_written(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ(win_changed(0, WIN_VIEW_ROW, 0, WIN_SCREEN_PITCH), 0);
    CHECK_EQ(win_changed(WIN_VIEW_ROW + WIN_VIEW_H,
                         WIN_SCREEN_ROWS - WIN_VIEW_ROW - WIN_VIEW_H,
                         0, WIN_SCREEN_PITCH), 0);
    CHECK_EQ(win_changed(WIN_VIEW_ROW, WIN_VIEW_H, 0, WIN_VIEW_COL), 0);
    CHECK_EQ(win_changed(WIN_VIEW_ROW, WIN_VIEW_H, WIN_VIEW_COL + WIN_VIEW_W,
                         WIN_SCREEN_PITCH - WIN_VIEW_COL - WIN_VIEW_W), 0);
}

/* And the window itself is written: the run really did present frames rather
   than compose them and stop.  What it was written WITH is the scene page as
   malloc handed it over and is not asserted. */
static void the_presented_window_is_written(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ(win_changed(WIN_VIEW_ROW, WIN_VIEW_H, WIN_VIEW_COL, WIN_VIEW_W)
             > 0, 1);
}

/* The scancode script, read back as the number of codes the ring accepted.
   Four, and each of the four says something different: the make code pushed
   during the slide-in was thrown away by the flush at 0001806a and never read;
   0x80 was read by the hold and did not end it, which is the CMP EAX,0x7f /
   JLE and not a test against the queue-empty 0xff; 0x7f was read and did end
   it; and a fourth was pushed afterwards, which only a phase that waits on the
   tick can have done -- the slide-out.  A hold that ended on the first code it
   saw reads three here, and one that ignored the flush reads three as well. */
static void the_hold_ends_on_a_make_code_and_on_nothing_else(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ(win_pushed, WIN_EXPECTED_PUSHES);
}

/* Nineteen slide-in frames and thirteen slide-out frames, each ending on a
   change of the tick counter, so no run can have cost fewer ticks than their
   sum however fast the drawing was.  The hold's own frames are on top of
   that. */
static void every_frame_is_paced_to_one_timer_tick(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ((long) win_ticks >= WIN_SLIDE_IN_FRAMES + WIN_SLIDE_OUT_FRAMES,
             1);
}

/* fdps_draw_scene_layers is called once per frame, and its scroll gate is the
   one part of it that leaves a mark with no layers staged: the marker is gone
   afterwards, replaced by a tick the run actually ran on.  The gate is only
   ever set to the counter's own value, so it cannot have got past it. */
static void every_frame_recomposes_the_battle_map(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ(win_scroll_after == WIN_SCROLL_MARKER, 0);
    CHECK_EQ(win_scroll_after <= win_ticks, 1);
}

/* Opening the container reloads all nine data tables: every slot holds a
   different block afterwards, and none of them is null.  A build that skipped
   the load, or loaded into the wrong global, leaves one of these holding what
   the previous call put there. */
static void opening_the_container_reloads_every_data_table(void)
{
    int index;

    if (!win_prepare()) {
        return;
    }
    for (index = 0; index < WIN_TABLE_COUNT; index++) {
        CHECK_EQ(win_tables_after[index] != NULL, 1);
        CHECK_EQ(win_tables_after[index] != win_tables_before[index], 1);
    }
}

/* And the set that was there is leaked, which is the whole of the accounting:
   nine used heap entries more at the return than at the call, so the nine new
   tables stayed and everything else the call took -- the container handle, the
   sheet, the panel page and one scene page per frame -- went back.  Eight
   would mean a table was not reloaded, ten that something was not freed, and
   zero that the old set was released after all. */
static void the_previous_data_tables_are_leaked_and_nothing_else_is(void)
{
    if (!win_prepare()) {
        return;
    }
    CHECK_EQ(win_heap_after - win_heap_before, WIN_TABLE_COUNT);
    CHECK_EQ(_heapchk(), _HEAPOK);
}

void run_btlend_tests(void)
{
    RUN_TEST(record_layout_matches_the_offsets);
    RUN_TEST(an_empty_battle_counts_nothing);
    RUN_TEST(the_bound_is_exclusive);
    RUN_TEST(each_side_code_counts_its_own_units);
    RUN_TEST(an_unused_side_code_counts_nothing);
    RUN_TEST(retired_units_are_not_counted);
    RUN_TEST(the_acted_flag_does_not_retire_a_unit);
    RUN_TEST(the_side_byte_is_zero_extended);
    RUN_TEST(the_walk_follows_the_published_array);
    RUN_TEST(a_recorded_verdict_is_never_recomputed);
    RUN_TEST(every_enemy_gone_clears_the_chapter);
    RUN_TEST(one_live_enemy_keeps_the_battle_going);
    RUN_TEST(only_bit_zero_retires_an_enemy);
    RUN_TEST(only_side_zero_holds_the_battle_open);
    RUN_TEST(the_walk_stops_at_the_live_unit_count);
    RUN_TEST(a_retired_watched_unit_outranks_the_walk);
    RUN_TEST(chapters_17_and_22_watch_slot_three);
    RUN_TEST(the_watched_slot_ignores_the_unit_count);
    RUN_TEST(the_container_holds_the_sheet_the_panel_is_built_from);
    RUN_TEST(nothing_outside_the_presented_window_is_written);
    RUN_TEST(the_presented_window_is_written);
    RUN_TEST(the_hold_ends_on_a_make_code_and_on_nothing_else);
    RUN_TEST(every_frame_is_paced_to_one_timer_tick);
    RUN_TEST(every_frame_recomposes_the_battle_map);
    RUN_TEST(opening_the_container_reloads_every_data_table);
    RUN_TEST(the_previous_data_tables_are_leaked_and_nothing_else_is);
}
