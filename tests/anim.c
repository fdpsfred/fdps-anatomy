/* tests/anim.c -- cover for src/anim.c.
 *
 * Expected values come from the assembly at 0002a240 -- PUSH dword ptr
 * [0x000643a8] at 0002a254 for the archive base, the store of EAX into
 * [0x000643ec] at 0002a262, the CMP/JZ at 0002a267 and the load of that same
 * global at 0002a270 for the value returned -- and from the directory of the
 * shipped FIELD2.VFS, whose member starts are the numbers tests/vfs.c asserts
 * against the same container.  None of them is read off the emitted C.
 *
 * WHAT STANDS IN FOR BaseAni.vfs.  The function looks its member up in
 * whatever resident container image the archive global points at; nothing in
 * it is particular to BaseAni.vfs, which is not a loose file on disk but a
 * member of MISC.VFS.  FIELD2.VFS is a real container the game ships, it is
 * the smallest at 112,350 bytes, and it is already staged for tests/vfs.c
 * (tests/gamefile.lst), so it is read whole here exactly as the loader holds
 * the animation pack and the lookup is exercised against a directory the game
 * itself wrote.  A fabricated image would only be a copy of this one with
 * different numbers in it.
 *
 * WHAT IS NOT COVERED.  The miss path prints "File not found: %s" and calls
 * exit(1), so it ends the process rather than returning: no assertion in a
 * test executable can reach it and no cover here tries.  What the message says
 * about the query -- that it is the upper-cased spelling, not the caller's --
 * is asserted through the found path instead, which folds the buffer the same
 * way.
 */
#include <stddef.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "sprite.h"
#include "anim.h"
#include "testharn.h"

/* The staged container, and its own length asserted rather than assumed so a
   staged file that is not this container fails here instead of failing as
   arithmetic (resource_info/vfs.md). */
#define VFS_NAME "FIELD2.VFS"
#define VFS_IMAGE_BYTES 112350L

/* Query buffers are written into, because the lookup upper-cases the caller's
   own storage. */
#define QUERY_MAX 16

/* Two members of FIELD2.VFS, taken from its directory: entry 0 and entry 130,
   the last.  Entry 0 begins where the member data begins so it is found under
   any stride, and entry 130's 36 bytes end exactly at the container's 112,350.
   They are far enough apart that a stale answer cannot pass for a fresh one. */
#define ATTR000_START 3441L
#define DSC64_START 112314L

/* Reads a container into one block with plain library calls, exactly as the
   startup loader holds the animation pack.  Returns NULL rather than
   asserting, and every caller checks; the block is the caller's to free. */
static char *read_whole_container(char *archive_name, long *out_bytes)
{
    FILE *fp;
    char *image;
    long bytes;

    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    image = (char *)malloc((size_t)bytes);
    if (image == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(image, (size_t)bytes, 1, fp) != 1) {
        free(image);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_bytes = bytes;
    return image;
}

/* The answer is checked as a distance from the image base, because that is
   what the lookup computes and what makes this an assertion about a member
   found where it lies rather than about a copy.  The global is checked against
   the same pointer: the store at 0002a262 and the load at 0002a270 mean the
   returned value IS the global's contents, so a caller and a later reader of
   the global cannot disagree. */
static void baseani_lookup_returns_a_pointer_into_the_image(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }
    CHECK_EQ(bytes, VFS_IMAGE_BYTES);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr == member, 1);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* Nothing clears the global and nothing else writes it, so the only thing that
   can change it is the next lookup: two calls in a row have to leave it
   holding the second member, not the first.  The two starts are 108,873 bytes
   apart, so a global that was written once and not again is not a near miss. */
static void baseani_lookup_republishes_on_every_call(void)
{
    char *image;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "ATTR000.DAT");
    fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr - image,
             ATTR000_START);

    strcpy(query, "DSC64.DAT");
    fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr - image,
             DSC64_START);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* The archive base is loaded from the global at the moment of the call, PUSH
   dword ptr [0x000643a8] at 0002a254, and is not cached anywhere: the same
   query against two images has to answer inside whichever one the global names
   now.  Two separate blocks are used rather than one, so the two answers are
   different addresses and not the same address asserted twice. */
static void baseani_lookup_reads_the_archive_base_each_call(void)
{
    char *first_image;
    char *second_image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    first_image = read_whole_container(VFS_NAME, &bytes);
    second_image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(first_image != NULL, 1);
    CHECK_EQ(second_image != NULL, 1);
    if (first_image == NULL || second_image == NULL) {
        free(first_image);
        free(second_image);
        return;
    }
    CHECK_EQ(first_image != second_image, 1);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)first_image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member - first_image, ATTR000_START);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)second_image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member - second_image, ATTR000_START);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(first_image);
    free(second_image);
}

/* strupr runs on the caller's buffer inside the lookup (vfs.h), so a
   mixed-case query finds the upper-case member and the caller's string is
   upper-case afterwards.  Both halves are asserted: the second is not a side
   effect nobody depends on -- it is the spelling this function would print in
   its "File not found: %s" message, which is the one thing about the miss path
   a test can establish without ending the process. */
static void baseani_lookup_folds_the_query_in_place(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "attr000.dat");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ(strcmp(query, "ATTR000.DAT"), 0);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* ---- fdps_draw_turn_number, 0001ea80 ------------------------------------
 *
 * Expected values come from the assembly: SUB EDX,0x2f at 0001ead2 for the
 * bank entry a formatted character becomes, MOV dword ptr [EAX+0x18],EDX at
 * 0001ead5 and ADD dword ptr [EAX+0xc],0x1c at 0001eaea for the two slots that
 * move and by how much, and the CALL at 0001eaaf sitting inside the loop's own
 * condition for how many times round it goes.  None of them is read off the
 * emitted C.
 *
 * HOW THE PAINTING IS KEPT OUT OF THE WAY.  fdps_draw_composite_sprite is real
 * code, not a stub, and it would walk a .SAF frame and blit it.  The request
 * here is aimed at a sprite bank that is 64 zero bytes, which makes the frame
 * count the drawer reads at +0x0c zero, so its one range test rejects every
 * entry index this function can produce -- the smallest is 1 -- and it returns
 * having drawn nothing, played nothing and written nothing (sprite.h).  That
 * is not a stand-in for a .SAF: it is the drawer's own documented do-nothing
 * path, chosen so that everything the request block holds afterwards was put
 * there by the function under test and by nothing else.
 *
 * WHAT IS NOT COVERED.  x is advanced AFTER each call, so the k-th digit is
 * painted at the caller's x plus 28(k-1); with the drawer inert there is
 * nothing that records where each digit landed, and no assertion here can tell
 * that order from the reverse.  It is settled by reading 0001ead5 through
 * 0001eaea, where the store of the entry index and the call both precede the
 * ADD, and not by a case below.
 */

/* Nine dwords, the block sprite.h describes, with the slots this function does
   not touch set to values that are recognisable if something writes them. */
#define REQ_DEST_BASE_MARK 0x11110000
#define REQ_DEST_PITCH_MARK 0x168
#define REQ_DEST_ROWS_MARK 0xf0
#define REQ_START_X 100
#define REQ_START_Y 0x22220000
#define REQ_BLIT_OPERAND_MARK 0x33330000
#define REQ_BLIT_MODE_MARK 0x44440000

/* The banner bank stood down to its do-nothing path: 64 bytes of zero, of
   which only the frame count at +0x0c is read. */
static unsigned char inert_sprite_bank[64];

static void build_turn_request(int *request)
{
    int slot;

    for (slot = 0; slot < (int) sizeof(inert_sprite_bank); slot++) {
        inert_sprite_bank[slot] = 0;
    }
    request[DRAW_REQUEST_DEST_BASE] = REQ_DEST_BASE_MARK;
    request[DRAW_REQUEST_DEST_PITCH] = REQ_DEST_PITCH_MARK;
    request[DRAW_REQUEST_DEST_ROWS] = REQ_DEST_ROWS_MARK;
    request[DRAW_REQUEST_X] = REQ_START_X;
    request[DRAW_REQUEST_Y] = REQ_START_Y;
    request[DRAW_REQUEST_IMAGE] = (int) inert_sprite_bank;
    request[DRAW_REQUEST_ITEM_INDEX] = 0;
    request[DRAW_REQUEST_BLIT_OPERAND] = REQ_BLIT_OPERAND_MARK;
    request[DRAW_REQUEST_BLIT_MODE] = REQ_BLIT_MODE_MARK;
}

/* One digit is one pass: x moves exactly one 28-pixel pitch and the entry
   index comes back as the digit plus one.  Turn 7 draws bank entry 8. */
static void turn_number_draws_one_pass_per_digit(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 7;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 8);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 123;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 3);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 4);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 9999;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 4);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 10);
}

/* The bias is 0x2f and not '0', so a digit selects the entry one past its own
   number: '0' is entry 1 and never entry 0, which is the banner word graphic.
   Both numbers here end in a zero, so the naive bias would leave the entry
   index at 0 while x still came out right -- the entry index is the only slot
   that can tell the two spellings apart. */
static void turn_number_biases_digits_one_past_the_word_sprite(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 0;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 1);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 10;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 2);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 1);
}

/* The counter is read at the moment of the call and nothing is passed in, so
   the same block drawn twice with the counter moved on in between produces two
   different lengths, and the second run starts from wherever the first left x
   rather than from the block's original x.  This is what the caller sees when
   it forgets to reset the two slots. */
static void turn_number_reads_the_counter_at_the_call(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 9;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 10);

    data_fdps_battle_turn_counter = 25;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 3);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 6);
}

/* Seven of the nine slots are neither read nor written: the assembly's only
   stores through the request pointer are [EAX+0x18] at 0001ead5 and
   [EAX+0xc] at 0001eaea.  The drawer is on its do-nothing path, so anything
   else that moved was moved here. */
static void turn_number_leaves_the_other_seven_slots_alone(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 42;
    fdps_draw_turn_number(request);

    CHECK_EQ(request[DRAW_REQUEST_DEST_BASE], REQ_DEST_BASE_MARK);
    CHECK_EQ(request[DRAW_REQUEST_DEST_PITCH], REQ_DEST_PITCH_MARK);
    CHECK_EQ(request[DRAW_REQUEST_DEST_ROWS], REQ_DEST_ROWS_MARK);
    CHECK_EQ(request[DRAW_REQUEST_Y], REQ_START_Y);
    CHECK_EQ(request[DRAW_REQUEST_IMAGE] == (int) inert_sprite_bank, 1);
    CHECK_EQ(request[DRAW_REQUEST_BLIT_OPERAND], REQ_BLIT_OPERAND_MARK);
    CHECK_EQ(request[DRAW_REQUEST_BLIT_MODE], REQ_BLIT_MODE_MARK);
}

/* ---- fdps_animate_turn_banner, 0001e840 ---------------------------------
 *
 * Expected values come from the assembly: PUSH 0x15180 / CALL malloc at
 * 0001e85b for the scratch surface; the stores of 0x168, 0xf0 and 0x5c into
 * [EBP-0x2c], [EBP-0x28] and [EBP-0x20] at 0001e86b, 0001e872 and 0001e879 for
 * the request's pitch, rows and row; the thirteen dwords at 0x0001c280 copied
 * with REP MOVSD at 0001e859 for the offset table; ADD EAX,0x14 at 0001e8fb
 * and MOV EAX,0x12c / SUB EAX,[table] at 0001e924 for the two columns a step
 * derives; MOV dword ptr [EBP-0x18],0x0 at 0001e901 for the entry index the
 * sign is drawn with; CMP [EBP-0x4],0xd / JL at 0001e8ac for thirteen steps in
 * and MOV dword ptr [EBP-0x4],0xa at 0001e98b for eleven out; and the two
 * six-push blits at 0001e8bf and 0001e93c -- 0xc0, 0x138, 0x168,
 * surface + 0x21d8, 0x140, saved_screen + 0x504 and then the same rectangle
 * out to 0xa0504 -- for the window that moves.  None of them is read off the
 * emitted C.
 *
 * WHAT THE RUN IS OBSERVED THROUGH.  The routine's whole output is the VGA
 * aperture at 0xa0000, which only answers in a graphics mode, so every case
 * that calls it puts the adapter into mode 13h the way the game does, captures
 * the frame and returns to text mode afterwards.  The scratch surface it
 * composes on is allocated and freed inside the call and cannot be looked at.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED.  Every step ends waiting for
 * data_fdps_timer_tick_counter to change, and in the game that counter is
 * advanced by fdps_timer_tick_handler off AIL's timer.  Nothing advances it in
 * a test image, so the wait after the first step would never end.  Each run
 * hooks IRQ0 for the duration of the call with a handler that increments the
 * counter and chains to the one that was there.
 *
 * ONLY THE LAST OF THE 24 STEPS IS VISIBLE, because every step repaints the
 * background over the one before it.  That last step is the slide-out's step 0
 * and it places both pieces from table entry -60, which is exactly what makes
 * it worth looking at: the two columns it derives, -60 + 0x14 and 0x12c - -60,
 * are the widest apart the table can put them.  Where the banner sat on the
 * other 23 steps has no unit observable and is a playtest contract
 * (rebuild_info/pitfalls.md), and so is the tick pacing, which timing would
 * only measure the emulator's cycle setting for.  The slide-out starting at
 * entry 10 rather than 12 is in the same position: both spellings end on entry
 * 0, so the frame cannot tell them apart, and it is recorded on the definition.
 *
 * WHAT THE SPRITE SHEET IS.  One case runs against the game's own Turn.saf,
 * reached the way the routine reaches it -- BASEANI.VFS lifted out of the
 * shipped MISC.VFS and published as the resident archive -- and a misspelling
 * of the literal the routine looks up would end the process there rather than
 * fail an assertion.  What that case can say is bounded by the sheet's own
 * shape: its eleven frames carry one layer each, at offset (0,0) in every one
 * of them but frame 6 -- the digit '5' -- whose layer sits at (0,1), the sign's
 * tilemap is 5 x 2 cells of 24 x 24 and each digit's is 1 x 2, and the
 * placement test the drawer applies is per cell.  So on the last step the
 * number's column of 360 puts its one cell past the surface's 336 limit and
 * nothing of it is drawn, while the sign's column of -40 puts three of its five
 * cells at 8, 32 and 56, all of them inside -- the banner does NOT leave the
 * screen clean, and 56 of the sign's 120 columns are still inside the window
 * when the routine returns.  The case asserts where that leftover is and not
 * how many bytes of it there are, because how much of those three cells is
 * opaque is the artwork's business.
 *
 * The placement case therefore uses a synthetic sheet instead, whose two layers
 * carry offsets of +100 and -100 that pull both pieces well inside the surface
 * where their columns can be read; it is wrapped in a synthetic 26-byte-entry
 * container because the lookup is what the routine takes its sheet through.
 * The .SAF layout is resource_info/saf.md and the container's is
 * resource_info/vfs.md; both are restated here rather than taken from src/.
 *
 * WHAT IS NOT COVERED.  The sound the sign is drawn with -- the 1 at 0001e908
 * and 0001e9ee -- reaches fdps_sfx_play as the frame's own sound number, and
 * every frame of the shipped Turn.saf carries -1, which that function rejects.
 * There is nothing for a run against the real sheet to observe, and what a
 * non-zero play_sound does is fdps_draw_composite_sprite's own behaviour,
 * covered in tests/sprite.c.
 * ------------------------------------------------------------------ */

#define BANNER_VGA_BASE 0x000a0000
#define BANNER_SCREEN_W 0x140
#define BANNER_SCREEN_H 0xc8
#define BANNER_SCREEN_BYTES (BANNER_SCREEN_W * BANNER_SCREEN_H)

#define BANNER_MODE_TEXT 0x03
#define BANNER_MODE_320X200X256 0x13

/* IRQ0.  DOS/4GW reflects a hardware interrupt taken in protected mode to the
   protected-mode vector, so the handler installed here is the one that runs
   while the routine spins on the counter. */
#define BANNER_TIMER_VECTOR 8

/* The window that moves: 312 x 192 at screen pixel (4,4), which is byte 0x504
   of a 320-pitch frame, and at surface pixel (24,24), which is byte 0x21d8 of
   a 360-pitch one. */
#define BANNER_WINDOW_W 0x138
#define BANNER_WINDOW_H 0xc0
#define BANNER_WINDOW_ROW 4
#define BANNER_WINDOW_COL 4
#define BANNER_SURFACE_WINDOW_ROW 24
#define BANNER_SURFACE_WINDOW_COL 24
#define BANNER_WINDOW_BYTES (BANNER_WINDOW_W * BANNER_WINDOW_H)
#define BANNER_BORDER_BYTES (BANNER_SCREEN_BYTES - BANNER_WINDOW_BYTES)

/* A surface column or row, converted to the screen one it is copied out to. */
#define BANNER_TO_SCREEN_COL (BANNER_WINDOW_COL - BANNER_SURFACE_WINDOW_COL)
#define BANNER_TO_SCREEN_ROW (BANNER_WINDOW_ROW - BANNER_SURFACE_WINDOW_ROW)

/* The row both pieces are drawn on, the two ways a table entry becomes a
   column, and the entry the last step of the whole animation places from. */
#define BANNER_ROW 0x5c
#define BANNER_SIGN_X_BIAS 0x14
#define BANNER_NUMBER_X_BASE 0x12c
#define BANNER_TABLE_ENTRY_0 (-60)

/* What the aperture is filled with before each run.  Nothing the routine
   writes can produce it, so a byte that still holds it was not written. */
#define BANNER_SENTINEL 0x5a

/* The turn the number is drawn for.  One digit, and '1' - 0x2f is bank entry
   2, which is the synthetic sheet's third frame. */
#define BANNER_TURN 1
#define BANNER_DIGIT_FRAME 2

/* One pixel value per piece, neither of them zero -- zero is the drawer's
   transparency key -- and both below 0x80, which the background never is. */
#define BANNER_SIGN_PIXEL 0x11
#define BANNER_DIGIT_PIXEL 0x22

/* The synthetic sheet's cell: four across and two down, small enough that the
   two marks cannot meet and tall enough that a draw one row out shows. */
#define BANNER_CELL_W 4
#define BANNER_CELL_H 2

/* The offsets the synthetic sheet's two layers carry, which is what pulls both
   pieces back inside the surface on the last step. */
#define BANNER_SIGN_LAYER_X 100
#define BANNER_SIGN_LAYER_Y 0
#define BANNER_DIGIT_LAYER_X (-100)
#define BANNER_DIGIT_LAYER_Y 10

/* Where the two marks therefore land on the captured screen. */
#define BANNER_SIGN_COL (BANNER_TABLE_ENTRY_0 + BANNER_SIGN_X_BIAS \
                         + BANNER_SIGN_LAYER_X + BANNER_TO_SCREEN_COL)
#define BANNER_SIGN_ROW (BANNER_ROW + BANNER_SIGN_LAYER_Y \
                         + BANNER_TO_SCREEN_ROW)
#define BANNER_DIGIT_COL (BANNER_NUMBER_X_BASE - BANNER_TABLE_ENTRY_0 \
                          + BANNER_DIGIT_LAYER_X + BANNER_TO_SCREEN_COL)
#define BANNER_DIGIT_ROW (BANNER_ROW + BANNER_DIGIT_LAYER_Y \
                          + BANNER_TO_SCREEN_ROW)

/* The shipped Turn.saf's own shape, read out of BASEANI.VFS: 24x24 cells, a
   sign whose tilemap is 5 cells across and 2 down, and a digit whose tilemap is
   1 across and 2 down.  These are the sheet's numbers and not the routine's. */
#define TURN_SAF_CELL 24
#define TURN_SIGN_CELLS_W 5
#define TURN_SIGN_CELLS_H 2

/* Where the sign's leftover therefore is on the last step.  Its left edge is at
   surface column -40, so its 120 columns end at 79 and the window's own left
   edge at 24 clips the rest away; its 48 rows start at the drawing row.  Every
   byte the run changes has to be inside this box. */
#define TURN_LEFTOVER_FIRST_ROW (BANNER_ROW + BANNER_TO_SCREEN_ROW)
#define TURN_LEFTOVER_LAST_ROW (BANNER_ROW + TURN_SAF_CELL * TURN_SIGN_CELLS_H \
                                - 1 + BANNER_TO_SCREEN_ROW)
#define TURN_LEFTOVER_FIRST_COL BANNER_WINDOW_COL
#define TURN_LEFTOVER_LAST_COL (BANNER_TABLE_ENTRY_0 + BANNER_SIGN_X_BIAS \
                                + TURN_SAF_CELL * TURN_SIGN_CELLS_W - 1 \
                                + BANNER_TO_SCREEN_COL)

/* The synthetic .SAF.  Header offsets first: the three magic bytes, the cell
   size, and the three section descriptors. */
#define BSAF_CELL_W_AT 0x07
#define BSAF_CELL_H_AT 0x09
#define BSAF_FRAME_COUNT_AT 0x0c
#define BSAF_FRAME_TABLE_PTR_AT 0x0e
#define BSAF_TILEMAP_COUNT_AT 0x16
#define BSAF_TILEMAP_TABLE_PTR_AT 0x18
#define BSAF_TILE_COUNT_AT 0x20
#define BSAF_TILE_TABLE_PTR_AT 0x22

/* Where the fixture puts each section: the header ends at 0x34, then two tile
   offsets and their four-byte streams, two tilemap offsets and their six-byte
   records, and a three-entry frame table with a record for each. */
#define BSAF_TILE_TABLE_AT 0x34
#define BSAF_TILE0_STREAM_AT 0x3c
#define BSAF_TILE1_STREAM_AT 0x40
#define BSAF_TILEMAP_TABLE_AT 0x44
#define BSAF_TILEMAP0_AT 0x4c
#define BSAF_TILEMAP1_AT 0x54
#define BSAF_FRAME_TABLE_AT 0x5c
#define BSAF_FRAME0_AT 0x68
#define BSAF_FRAME1_AT 0x80
#define BSAF_FRAME2_AT 0x98
#define BSAF_FRAME_COUNT 3
#define BSAF_IMAGE_BYTES 0x100

/* The frame record: sound at +0, duration at +2, layer count at +8, the first
   layer at +0x0a; a layer is 13 bytes of tilemap number, x, y, blend flag and
   blend level.  Sound 0xffff is the -1 every frame of the real sheet carries,
   so nothing is asked of the mixer. */
#define BSAF_FRAME_SOUND_AT 0x00
#define BSAF_FRAME_DURATION_AT 0x02
#define BSAF_FRAME_LAYERS_AT 0x08
#define BSAF_LAYER_AT 0x0a
#define BSAF_LAYER_TILEMAP_AT 0x00
#define BSAF_LAYER_X_AT 0x02
#define BSAF_LAYER_Y_AT 0x04
#define BSAF_LAYER_BLEND_AT 0x06
#define BSAF_NO_SOUND 0xffff

/* The synthetic container: a 35-byte header, one 26-byte entry, then the
   member.  Only the table offset at 5, the count at 7, and each entry's name,
   size and start are read. */
#define BVFS_TABLE_AT 35
#define BVFS_ENTRY_BYTES 26
#define BVFS_ENTRY_SIZE_AT 0x0d
#define BVFS_ENTRY_SIZE2_AT 0x11
#define BVFS_ENTRY_START_AT 0x16
#define BVFS_MEMBER_AT (BVFS_TABLE_AT + BVFS_ENTRY_BYTES)
#define BVFS_IMAGE_BYTES (BVFS_MEMBER_AT + BSAF_IMAGE_BYTES)

/* The nested container the real sheet lives in, and the outer one that holds
   it.  MISC.VFS is staged by tests/gamefile.lst. */
#define MISC_NAME "MISC.VFS"
#define BASEANI_MEMBER "BASEANI.VFS"

static unsigned char banner_background[BANNER_SCREEN_BYTES];
static unsigned char banner_screen[BANNER_SCREEN_BYTES];
static unsigned char banner_saf[BSAF_IMAGE_BYTES];
static unsigned char banner_vfs[BVFS_IMAGE_BYTES];

static void (__interrupt __far *banner_saved_timer)();

static void __interrupt __far banner_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(banner_saved_timer);
}

static void banner_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void banner_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* The background the banner slides over.  Every byte has bit 7 set, so no byte
   of it can be mistaken for the sentinel or for either mark. */
static int banner_pattern(int row, int col)
{
    return ((row * 31 + col * 17) & 0x7f) | 0x80;
}

static void banner_stage_background(void)
{
    int row;
    int col;

    for (row = 0; row < BANNER_SCREEN_H; row++) {
        for (col = 0; col < BANNER_SCREEN_W; col++) {
            banner_background[row * BANNER_SCREEN_W + col] =
                (unsigned char) banner_pattern(row, col);
        }
    }
}

/* One frame record naming one layer: which tilemap, and the offset the layer
   carries from the request's own origin. */
static void banner_stage_frame(int at, int tilemap, int layer_x, int layer_y)
{
    banner_u16(banner_saf, at + BSAF_FRAME_SOUND_AT, BSAF_NO_SOUND);
    banner_u16(banner_saf, at + BSAF_FRAME_DURATION_AT, 1);
    banner_u16(banner_saf, at + BSAF_FRAME_LAYERS_AT, 1);
    banner_u16(banner_saf, at + BSAF_LAYER_AT + BSAF_LAYER_TILEMAP_AT,
               (unsigned int) tilemap);
    banner_u16(banner_saf, at + BSAF_LAYER_AT + BSAF_LAYER_X_AT,
               (unsigned int) layer_x);
    banner_u16(banner_saf, at + BSAF_LAYER_AT + BSAF_LAYER_Y_AT,
               (unsigned int) layer_y);
    banner_saf[at + BSAF_LAYER_AT + BSAF_LAYER_BLEND_AT] = 0;
}

/* The synthetic sheet: two 4x2 tiles of their own pixel value, one single-cell
   tilemap each, and three frames.  Frame 0 is what the routine draws the sign
   with and frame 2 is the bank entry the digit '1' selects; frame 1 is only
   there so the table has an entry between them.  Command 0x03 is a fill run of
   four pixels (resource_info/cel.md), so two of them make the two rows of a
   cell. */
static void banner_stage_sheet(void)
{
    memset(banner_saf, 0, (size_t) BSAF_IMAGE_BYTES);
    banner_saf[0] = 'S';
    banner_saf[1] = 'A';
    banner_saf[2] = 'F';
    banner_u16(banner_saf, BSAF_CELL_W_AT, BANNER_CELL_W);
    banner_u16(banner_saf, BSAF_CELL_H_AT, BANNER_CELL_H);

    banner_u16(banner_saf, BSAF_TILE_COUNT_AT, 2);
    banner_u32(banner_saf, BSAF_TILE_TABLE_PTR_AT,
               (unsigned long) BSAF_TILE_TABLE_AT);
    banner_u32(banner_saf, BSAF_TILE_TABLE_AT,
               (unsigned long) BSAF_TILE0_STREAM_AT);
    banner_u32(banner_saf, BSAF_TILE_TABLE_AT + 4,
               (unsigned long) BSAF_TILE1_STREAM_AT);
    banner_saf[BSAF_TILE0_STREAM_AT] = 0x03;
    banner_saf[BSAF_TILE0_STREAM_AT + 1] = BANNER_SIGN_PIXEL;
    banner_saf[BSAF_TILE0_STREAM_AT + 2] = 0x03;
    banner_saf[BSAF_TILE0_STREAM_AT + 3] = BANNER_SIGN_PIXEL;
    banner_saf[BSAF_TILE1_STREAM_AT] = 0x03;
    banner_saf[BSAF_TILE1_STREAM_AT + 1] = BANNER_DIGIT_PIXEL;
    banner_saf[BSAF_TILE1_STREAM_AT + 2] = 0x03;
    banner_saf[BSAF_TILE1_STREAM_AT + 3] = BANNER_DIGIT_PIXEL;

    banner_u16(banner_saf, BSAF_TILEMAP_COUNT_AT, 2);
    banner_u32(banner_saf, BSAF_TILEMAP_TABLE_PTR_AT,
               (unsigned long) BSAF_TILEMAP_TABLE_AT);
    banner_u32(banner_saf, BSAF_TILEMAP_TABLE_AT,
               (unsigned long) BSAF_TILEMAP0_AT);
    banner_u32(banner_saf, BSAF_TILEMAP_TABLE_AT + 4,
               (unsigned long) BSAF_TILEMAP1_AT);
    banner_u16(banner_saf, BSAF_TILEMAP0_AT, 1);
    banner_u16(banner_saf, BSAF_TILEMAP0_AT + 2, 1);
    banner_u16(banner_saf, BSAF_TILEMAP0_AT + 4, 0);
    banner_u16(banner_saf, BSAF_TILEMAP1_AT, 1);
    banner_u16(banner_saf, BSAF_TILEMAP1_AT + 2, 1);
    banner_u16(banner_saf, BSAF_TILEMAP1_AT + 4, 1);

    banner_u16(banner_saf, BSAF_FRAME_COUNT_AT, BSAF_FRAME_COUNT);
    banner_u32(banner_saf, BSAF_FRAME_TABLE_PTR_AT,
               (unsigned long) BSAF_FRAME_TABLE_AT);
    banner_u32(banner_saf, BSAF_FRAME_TABLE_AT,
               (unsigned long) BSAF_FRAME0_AT);
    banner_u32(banner_saf, BSAF_FRAME_TABLE_AT + 4,
               (unsigned long) BSAF_FRAME1_AT);
    banner_u32(banner_saf, BSAF_FRAME_TABLE_AT + 8,
               (unsigned long) BSAF_FRAME2_AT);
    banner_stage_frame(BSAF_FRAME0_AT, 0, BANNER_SIGN_LAYER_X,
                       BANNER_SIGN_LAYER_Y);
    banner_stage_frame(BSAF_FRAME1_AT, 0, 0, 0);
    banner_stage_frame(BSAF_FRAME2_AT, 1, BANNER_DIGIT_LAYER_X,
                       BANNER_DIGIT_LAYER_Y);
}

/* The synthetic container holding that sheet under the name the routine looks
   up.  The entry's name is stored upper-case because the lookup folds only the
   query and compares the entry as the packer wrote it. */
static void banner_stage_container(void)
{
    banner_stage_sheet();
    memset(banner_vfs, 0, (size_t) BVFS_IMAGE_BYTES);
    banner_vfs[0] = 'V';
    banner_vfs[1] = 'F';
    banner_vfs[2] = 'S';
    banner_u16(banner_vfs, 3, 1);
    banner_u16(banner_vfs, 5, BVFS_TABLE_AT);
    banner_u32(banner_vfs, 7, 1);
    strcpy((char *) banner_vfs + BVFS_TABLE_AT, "TURN.SAF");
    banner_u32(banner_vfs, BVFS_TABLE_AT + BVFS_ENTRY_SIZE_AT,
               (unsigned long) BSAF_IMAGE_BYTES);
    banner_u32(banner_vfs, BVFS_TABLE_AT + BVFS_ENTRY_SIZE2_AT,
               (unsigned long) BSAF_IMAGE_BYTES);
    banner_u32(banner_vfs, BVFS_TABLE_AT + BVFS_ENTRY_START_AT,
               (unsigned long) BVFS_MEMBER_AT);
    memmove(banner_vfs + BVFS_MEMBER_AT, banner_saf,
            (size_t) BSAF_IMAGE_BYTES);
}

/* BASEANI.VFS lifted out of MISC.VFS by walking the outer container's own
   directory -- 26-byte entries at the offset the header's field at 5 names,
   each with its name at +0 and its start at +0x16 (resource_info/vfs.md).  The
   walk is written out here rather than taken through src/vfs.c so that staging
   does not lean on the code the rest of this file is about.  Returns NULL when
   the file is not staged, and the whole outer image is the caller's to free. */
static unsigned char *banner_read_baseani(unsigned char **out_outer)
{
    FILE *fp;
    unsigned char *outer;
    long bytes;
    unsigned long table_at;
    unsigned long count;
    unsigned long index;
    unsigned long entry;

    *out_outer = NULL;
    fp = fopen(MISC_NAME, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    outer = (unsigned char *) malloc((size_t) bytes);
    if (outer == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(outer, (size_t) bytes, 1, fp) != 1) {
        free(outer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_outer = outer;

    table_at = (unsigned long) outer[5] | ((unsigned long) outer[6] << 8);
    count = (unsigned long) outer[7] | ((unsigned long) outer[8] << 8)
            | ((unsigned long) outer[9] << 16)
            | ((unsigned long) outer[10] << 24);
    for (index = 0; index < count; index++) {
        entry = table_at + index * BVFS_ENTRY_BYTES;
        if (strcmp((char *) outer + entry, BASEANI_MEMBER) == 0) {
            return outer
                   + ((unsigned long) outer[entry + BVFS_ENTRY_START_AT]
                      | ((unsigned long) outer[entry + BVFS_ENTRY_START_AT + 1]
                         << 8)
                      | ((unsigned long) outer[entry + BVFS_ENTRY_START_AT + 2]
                         << 16)
                      | ((unsigned long) outer[entry + BVFS_ENTRY_START_AT + 3]
                         << 24));
        }
    }
    return NULL;
}

static void banner_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* One whole banner, with the adapter in the mode the game plays it in and a
   timer interrupt running, leaving the frame in banner_screen[]. */
static void banner_run(unsigned char *archive)
{
    banner_stage_background();
    data_fdps_animation_baseani_archive_ptr = archive;
    data_fdps_battle_turn_counter = BANNER_TURN;

    banner_set_mode(BANNER_MODE_320X200X256);
    memset((void *) BANNER_VGA_BASE, BANNER_SENTINEL,
           (size_t) BANNER_SCREEN_BYTES);

    banner_saved_timer = _dos_getvect(BANNER_TIMER_VECTOR);
    _dos_setvect(BANNER_TIMER_VECTOR, banner_timer_isr);
    fdps_animate_turn_banner(banner_background);
    _dos_setvect(BANNER_TIMER_VECTOR, banner_saved_timer);

    memmove(banner_screen, (void *) BANNER_VGA_BASE,
            (size_t) BANNER_SCREEN_BYTES);
    banner_set_mode(BANNER_MODE_TEXT);
    data_fdps_animation_baseani_archive_ptr = NULL;
}

static int banner_pixel(int row, int col)
{
    return (int) banner_screen[row * BANNER_SCREEN_W + col];
}

/* How many bytes of the captured frame hold a given value. */
static int banner_count(int value)
{
    long index;
    int found;

    found = 0;
    for (index = 0; index < (long) BANNER_SCREEN_BYTES; index++) {
        if ((int) banner_screen[index] == value) {
            found++;
        }
    }
    return found;
}

/* How many bytes inside the 312x192 window are not the background byte that
   was under them. */
static int banner_window_mismatches(void)
{
    int row;
    int col;
    long at;
    int bad;

    bad = 0;
    for (row = 0; row < BANNER_WINDOW_H; row++) {
        for (col = 0; col < BANNER_WINDOW_W; col++) {
            at = (long) (row + BANNER_WINDOW_ROW) * BANNER_SCREEN_W
                 + (col + BANNER_WINDOW_COL);
            if (banner_screen[at] != banner_background[at]) {
                bad++;
            }
        }
    }
    return bad;
}

/* How many bytes inside the window differ from the background they were
   painted from while lying outside the given box. */
static int banner_mismatches_outside(int first_row, int last_row,
                                     int first_col, int last_col)
{
    int row;
    int col;
    long at;
    int bad;

    bad = 0;
    for (row = 0; row < BANNER_WINDOW_H; row++) {
        for (col = 0; col < BANNER_WINDOW_W; col++) {
            at = (long) (row + BANNER_WINDOW_ROW) * BANNER_SCREEN_W
                 + (col + BANNER_WINDOW_COL);
            if (row + BANNER_WINDOW_ROW >= first_row
                && row + BANNER_WINDOW_ROW <= last_row
                && col + BANNER_WINDOW_COL >= first_col
                && col + BANNER_WINDOW_COL <= last_col) {
                continue;
            }
            if (banner_screen[at] != banner_background[at]) {
                bad++;
            }
        }
    }
    return bad;
}

/* How many bytes outside that window still hold the sentinel. */
static int banner_border_untouched(void)
{
    int row;
    int col;
    int kept;

    kept = 0;
    for (row = 0; row < BANNER_SCREEN_H; row++) {
        for (col = 0; col < BANNER_SCREEN_W; col++) {
            if (row >= BANNER_WINDOW_ROW
                && row < BANNER_WINDOW_ROW + BANNER_WINDOW_H
                && col >= BANNER_WINDOW_COL
                && col < BANNER_WINDOW_COL + BANNER_WINDOW_W) {
                continue;
            }
            if (banner_pixel(row, col) == BANNER_SENTINEL) {
                kept++;
            }
        }
    }
    return kept;
}

/* The premise every case below rests on: in mode 13h the aperture is a plain
   linear window that reads back what was written to it, at the first byte of
   the frame and at its last. */
static void banner_the_aperture_reads_back_in_mode_13h(void)
{
    unsigned char *aperture;
    int first;
    int last;

    banner_set_mode(BANNER_MODE_320X200X256);
    aperture = (unsigned char *) BANNER_VGA_BASE;
    aperture[0] = 0x5a;
    aperture[BANNER_SCREEN_BYTES - 1] = 0xa5;
    first = (int) aperture[0];
    last = (int) aperture[BANNER_SCREEN_BYTES - 1];
    banner_set_mode(BANNER_MODE_TEXT);

    CHECK_EQ(first, 0x5a);
    CHECK_EQ(last, 0xa5);
}

/* The whole animation against the game's own sheet, ending where the routine
   leaves it.  Three things at once.

   Everything the run changed is inside the sign's own last-step footprint --
   rows 72 to 119, columns 4 to 59 -- and something is: the number is gone,
   because its single cell at column 360 is past the surface's 336 limit, and
   the background is back everywhere the sign is not.  A run that stopped in the
   held position leaves the sign at column 110 and the number at column 210, and
   a run whose background repaint was missing leaves the trail of every step,
   and both put changed bytes far outside the box.

   Nothing outside the 312x192 window is written on the way in or on the way
   out: the two blits take byte 0x504 of a 320-pitch frame for 192 rows of 312,
   so the outermost four rows and columns, and the four columns past the window,
   keep the sentinel.  4,096 bytes of the 64,000 are outside it. */
static void banner_ends_with_the_signs_last_step_and_nothing_else(void)
{
    unsigned char *outer;
    unsigned char *baseani;

    baseani = banner_read_baseani(&outer);
    if (baseani == NULL) {
        free(outer);
        return;
    }

    banner_run(baseani);
    CHECK_EQ(banner_mismatches_outside(TURN_LEFTOVER_FIRST_ROW,
                                       TURN_LEFTOVER_LAST_ROW,
                                       TURN_LEFTOVER_FIRST_COL,
                                       TURN_LEFTOVER_LAST_COL), 0);
    CHECK_EQ(banner_window_mismatches() > 0, 1);
    CHECK_EQ(banner_border_untouched(), BANNER_BORDER_BYTES);
    free(outer);
}

/* The last step's two columns, read off the frame.  Both marks are 4x2 cells
   and both are placed from the same table entry, -60: the sign lands at
   -60 + 0x14 and the number at 0x12c - -60, and the surface's window starts 20
   pixels left of and above the screen's, so they come out at screen columns 40
   and 240 on rows 72 and 82.  Dropping the 0x14, or deriving the number from
   0x140 instead of 0x12c, or reading the table backwards moves one of the two
   and leaves the other where it was. */
static void banner_places_both_pieces_from_one_table_entry(void)
{
    banner_stage_container();
    banner_run(banner_vfs);

    CHECK_EQ(banner_pixel(BANNER_SIGN_ROW, BANNER_SIGN_COL),
             BANNER_SIGN_PIXEL);
    CHECK_EQ(banner_pixel(BANNER_SIGN_ROW,
                          BANNER_SIGN_COL + BANNER_CELL_W - 1),
             BANNER_SIGN_PIXEL);
    CHECK_EQ(banner_pixel(BANNER_SIGN_ROW + BANNER_CELL_H - 1,
                          BANNER_SIGN_COL),
             BANNER_SIGN_PIXEL);
    CHECK_EQ(banner_pixel(BANNER_SIGN_ROW, BANNER_SIGN_COL - 1),
             banner_pattern(BANNER_SIGN_ROW, BANNER_SIGN_COL - 1));

    CHECK_EQ(banner_pixel(BANNER_DIGIT_ROW, BANNER_DIGIT_COL),
             BANNER_DIGIT_PIXEL);
    CHECK_EQ(banner_pixel(BANNER_DIGIT_ROW + BANNER_CELL_H - 1,
                          BANNER_DIGIT_COL + BANNER_CELL_W - 1),
             BANNER_DIGIT_PIXEL);
    CHECK_EQ(banner_pixel(BANNER_DIGIT_ROW,
                          BANNER_DIGIT_COL + BANNER_CELL_W),
             banner_pattern(BANNER_DIGIT_ROW,
                            BANNER_DIGIT_COL + BANNER_CELL_W));
}

/* Exactly one cell of each piece survives the run, and nothing else in the
   window differs from the background.  That is three things at once: the
   background really is repainted at the start of every step, so 23 earlier
   placements left nothing behind; the sign really is drawn with bank entry 0
   put back before each draw, because the digit's own entry index would paint
   the digit's pixel at the sign's column; and the number really is one digit
   for turn 1. */
static void banner_repaints_the_background_under_every_step(void)
{
    banner_stage_container();
    banner_run(banner_vfs);

    CHECK_EQ(banner_count(BANNER_SIGN_PIXEL), BANNER_CELL_W * BANNER_CELL_H);
    CHECK_EQ(banner_count(BANNER_DIGIT_PIXEL), BANNER_CELL_W * BANNER_CELL_H);
    CHECK_EQ(banner_window_mismatches(), 2 * BANNER_CELL_W * BANNER_CELL_H);
    CHECK_EQ(banner_border_untouched(), BANNER_BORDER_BYTES);
}

/* ---- fdps_play_vfs_animation, 0001eb00 -----------------------------------
 *
 * Expected values come from the assembly: MOV EAX,0x60128 / PUSH at 0001eb10
 * for the container the member is loaded out of; PUSH 0xfa00 / CALL malloc at
 * 0001eb21 and 0001eb31 for the two saved copies and PUSH 0xfa00 / PUSH
 * 0xa0000 / CALL memmove at 0001eb4f and 0001eb65 for both being filled from
 * the adapter; the ten-push blit at 0001ebb6 -- level, 0, 0x643f0, 0x653f0,
 * 0xc8, 0x140, 0x140, 0xa0000, 0x140, the saved copy -- for the rectangle and
 * the two tables a step folds through; MOV dword ptr [EBP-0x8],0x1 / CMP
 * [EBP-0x8],0x6 / JL at 0001eb6d and 0001eb74 for the dimming pass's levels 1
 * to 5, and MOV dword ptr [EBP-0x8],0x5 / CMP [EBP-0x8],0x0 / JG at 0001ec49
 * and 0001ec50 for the restoring pass's 5 down to 1; the third memmove at
 * 0001ec03, which reads 0xa0000 into the FIRST copy only; and the strcmp at
 * 0001ec15 against 0x617c0 guarding the call to fdps_animate_turn_banner.  None
 * of them is read off the emitted C.
 *
 * HOW A FADE STEP IS MADE READABLE.  A step's output is
 * inverse_palette_cube[(shade_ramp[level * 0x100 + 0x900 + pixel] >> 4)
 * folded], with the tint entry at shade_ramp[level * 0x100] contributing zero
 * (blit.h); both tables are globals ticket 23 has not written, so the cases
 * below fill them themselves.  Each ramp entry is set so that the fold lands on
 * cube index level * 0x100 + pixel, and that cube entry is set to
 * 0x40 + level * 16 + pixel.  So a byte on the adapter names the level it was
 * tinted at AND the source pixel it came from, source pixels are kept to 0..15
 * so the two never collide, and every index the tables do not define answers 0.
 * That is a fixture for observing the function under test, not an assertion
 * about what the tables hold in the game.
 *
 * WHAT THE CLIP IS.  ME03.SAF, a real 66-byte member of the shipped MISC.VFS:
 * one frame, no layers and a duration of 12 ticks, so the player repaints the
 * backdrop twelve times and draws nothing over it.  A fabricated container
 * cannot stand in -- the loader takes the container's name as a literal, so
 * there is nothing to point at a smaller file, and a member it cannot find ends
 * the process rather than failing an assertion.
 *
 * WHY A TIMER INTERRUPT IS INSTALLED, and why the handler samples the adapter:
 * every fade step and every frame of the clip ends waiting for
 * data_fdps_timer_tick_counter to change, so nothing runs at all unless
 * something advances it, and the handler that does is also the only place from
 * which the screen can be looked at WHILE the call is still running.  The
 * restoring pass overwrites all 64000 bytes, so the dimmed backdrop the clip
 * plays over cannot be seen after the call returns.
 *
 * WHAT IS NOT COVERED.  Which of the two saved copies is freed where -- the
 * backdrop at 0001ec41 before the restoring pass, the member and the untouched
 * copy at 0001ecd5 and 0001ece1 after it -- has no unit observable; a leak or a
 * double free would show as an allocator failure and not as a wrong value.  Nor
 * does anything here pin the retrace waits: they cost a fraction of the tick
 * every step already waits for, and what they buy is a playtest contract
 * (rebuild_info/pitfalls.md).  The turn banner's own frames are covered by the
 * cases above; the case below only establishes that it runs, and for which
 * name.
 * ------------------------------------------------------------------ */

#define ANIM_ARCHIVE "MISC.VFS"

/* One frame, no layers, duration 12: the shortest real clip in the container,
   so the backdrop is repainted twelve times and nothing is drawn over it. */
#define ANIM_CLIP "ME03.SAF"

/* The two phase announcements, 35 and 34 frames, of which only the first is
   the name the turn banner is keyed on -- spelled the way the game's own call
   sites spell them, which is MIXED CASE: "EnyPhase.saf" at 0x61790 and
   "PlyPhase.saf" at 0x617a0 and 0x61acc.  The comparison inside is against an
   upper-case literal, so passing them as written is what puts the in-place fold
   in the path of the test. */
#define ANIM_PLAYER_CLIP "PlyPhase.saf"
#define ANIM_ENEMY_CLIP "EnyPhase.saf"

/* Queries are written into, because the loader upper-cases the caller's own
   storage. */
#define ANIM_NAME_MAX 16

/* The shade ramp's shape, restated here from the fold at 00030010 rather than
   taken from src/: 0x100 entries to a row and 0x900 entries from a tint row to
   its complementary source row. */
#define ANIM_RAMP_ROW_ENTRIES 0x100
#define ANIM_RAMP_COMPLEMENT_ROWS 0x900

/* The levels both passes walk, and the source values the fixture defines a
   ramp entry for.  Sixteen keeps a level and a pixel in separate nibbles of the
   cube index. */
#define ANIM_LEVELS 5
#define ANIM_SOURCE_VALUES 16

/* What a defined cube entry answers: 0x50 through 0x9f, none of which a source
   pixel can be and none of which is the 0 an undefined index gives. */
#define ANIM_TINT_MARK 0x40

/* Where the interrupt handler watches the screen, and how many ticks of it are
   kept.  Row 100 column 160 is inside every rectangle in play; 128 samples is
   five times the 22 ticks a run of the short clip costs. */
#define ANIM_PROBE_ROW 100
#define ANIM_PROBE_COL 160
#define ANIM_SAMPLE_MAX 128

/* How many ticks the probe has to spend on the darkest level before the clip
   can be said to have played over the dimmed screen.  The two passes contribute
   one step at level 5 each; everything beyond that is the clip. */
#define ANIM_DIMMED_MIN_TICKS 3

/* How many distinct levels have to show up for the fade to be a ramp rather
   than a single step.  Ten steps are walked and one tick is sampled per step,
   so three is well inside what the sampling can lose. */
#define ANIM_LEVELS_MIN_SEEN 3

/* What the player-phase name has to cost over the enemy-phase one before the
   banner can be said to have run: 24 banner steps each waiting a tick, of which
   two are documented as unpaced, plus the 500 ms hold, which is nine ticks at
   the 18.2 Hz the interrupt arrives on.  The two clips themselves are 35 and 34
   frames, so one tick of the difference is theirs. */
#define ANIM_BANNER_MIN_TICKS 20

static unsigned char anim_background[BANNER_SCREEN_BYTES];
static unsigned char anim_screen[BANNER_SCREEN_BYTES];

static volatile int anim_sample_count;
static volatile unsigned char anim_samples[ANIM_SAMPLE_MAX];
static int anim_probe_at;

static void (__interrupt __far *anim_saved_timer)();

static void __interrupt __far anim_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    if (anim_sample_count < ANIM_SAMPLE_MAX) {
        anim_samples[anim_sample_count] =
            ((unsigned char *) BANNER_VGA_BASE)[anim_probe_at];
        anim_sample_count = anim_sample_count + 1;
    }
    _chain_intr(anim_saved_timer);
}

/* The pre-call picture.  Every byte is 0..15, which is the range the fixture
   defines a ramp entry for and is disjoint from every value a tint can
   produce. */
static int anim_pattern(int row, int col)
{
    return (row * 7 + col * 3) & 0x0f;
}

/* What a step at this level over this source pixel has to put on the
   adapter. */
static int anim_tint(int level, int source_pixel)
{
    return ANIM_TINT_MARK + level * 16 + source_pixel;
}

/* Which level produced a byte, or 0 for a byte no level of this source pixel
   can produce. */
static int anim_level_of(int value, int source_pixel)
{
    int level;

    for (level = 1; level <= ANIM_LEVELS; level++) {
        if (value == anim_tint(level, source_pixel)) {
            return level;
        }
    }
    return 0;
}

static void anim_stage_tables(void)
{
    int level;
    int pixel;
    unsigned int blended;

    memset(data_fdps_palette_shade_ramp_table, 0,
           sizeof(data_fdps_palette_shade_ramp_table));
    memset(data_fdps_inverse_palette_cube, 0,
           sizeof(data_fdps_inverse_palette_cube));

    for (level = 1; level <= ANIM_LEVELS; level++) {
        for (pixel = 0; pixel < ANIM_SOURCE_VALUES; pixel++) {
            blended = ((unsigned int) level << 8) | (unsigned int) pixel;
            data_fdps_palette_shade_ramp_table[level * ANIM_RAMP_ROW_ENTRIES
                                               + ANIM_RAMP_COMPLEMENT_ROWS
                                               + pixel] = blended << 4;
            data_fdps_inverse_palette_cube[blended] =
                (unsigned char) anim_tint(level, pixel);
        }
    }
}

static void anim_stage_background(void)
{
    int row;
    int col;

    for (row = 0; row < BANNER_SCREEN_H; row++) {
        for (col = 0; col < BANNER_SCREEN_W; col++) {
            anim_background[row * BANNER_SCREEN_W + col] =
                (unsigned char) anim_pattern(row, col);
        }
    }
}

/* One whole animation, with the adapter in the mode the game plays it in, the
   pre-call picture on the screen and a timer interrupt running.  Leaves the
   frame in anim_screen[] and the per-tick probe in anim_samples[], and answers
   how many ticks the call took. */
static unsigned int anim_play(char *clip_name)
{
    char query[ANIM_NAME_MAX];
    unsigned int before;
    unsigned int after;

    anim_stage_tables();
    anim_stage_background();
    anim_sample_count = 0;
    anim_probe_at = ANIM_PROBE_ROW * BANNER_SCREEN_W + ANIM_PROBE_COL;
    strcpy(query, clip_name);

    banner_set_mode(BANNER_MODE_320X200X256);
    memmove((void *) BANNER_VGA_BASE, anim_background,
            (size_t) BANNER_SCREEN_BYTES);

    anim_saved_timer = _dos_getvect(BANNER_TIMER_VECTOR);
    _dos_setvect(BANNER_TIMER_VECTOR, anim_timer_isr);
    before = data_fdps_timer_tick_counter;
    fdps_play_vfs_animation(query);
    after = data_fdps_timer_tick_counter;
    _dos_setvect(BANNER_TIMER_VECTOR, anim_saved_timer);

    memmove(anim_screen, (void *) BANNER_VGA_BASE,
            (size_t) BANNER_SCREEN_BYTES);
    banner_set_mode(BANNER_MODE_TEXT);
    return after - before;
}

static int anim_archive_is_staged(void)
{
    FILE *fp;

    fp = fopen(ANIM_ARCHIVE, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Every one of the 64000 bytes comes back as level 1 of the byte that was under
   it before the call, which is three things at once.

   The restoring pass reads the copy that was never written again: had it read
   the backdrop, whose bytes are level 5's 0x90..0x9f by then, the fold would
   index a cube entry the fixture leaves at 0 and the frame would be black.

   It ends at level 1 and not at 0: level 1 and level 2 differ by sixteen in
   every byte.

   And it covers the whole 320x200 frame at a pitch of 320: a byte the two
   passes missed would still hold the pre-call value, which is 0..15 and cannot
   be mistaken for a tint.  That last point is asserted again on its own, since
   it is also what says the call does NOT put the picture back as it was. */
static void vfs_animation_ends_on_level_one_of_the_untouched_screen(void)
{
    int row;
    int col;
    long at;
    int wrong_level;
    int still_pre_call;

    if (!anim_archive_is_staged()) {
        return;
    }
    anim_play(ANIM_CLIP);

    wrong_level = 0;
    still_pre_call = 0;
    for (row = 0; row < BANNER_SCREEN_H; row++) {
        for (col = 0; col < BANNER_SCREEN_W; col++) {
            at = (long) row * BANNER_SCREEN_W + col;
            if ((int) anim_screen[at] != anim_tint(1, anim_pattern(row, col))) {
                wrong_level++;
            }
            if ((int) anim_screen[at] == anim_pattern(row, col)) {
                still_pre_call++;
            }
        }
    }
    CHECK_EQ(wrong_level, 0);
    CHECK_EQ(still_pre_call, 0);
}

/* What the screen held on each of the ticks the call spent, read at one pixel.

   Before the first step lands the screen still holds the pre-call picture, and
   every sample up to that point has to be exactly it.  From the first tinted
   sample onwards no sample may be the pre-call value again and none may be a
   byte no level can produce: the clip plays over the DIMMED screen, so a
   backdrop taken from the untouched copy -- or one taken before the dimming
   pass overwrote it -- would put the pre-call value back on the adapter for the
   twelve ticks the clip lasts.

   The darkest level has to hold for more ticks than the two passes can account
   for on their own, which is what says the clip ran over it rather than the
   passes simply meeting in the middle; and three different levels have to show
   up, which is what says the screen is walked down a ramp rather than dropped
   to one level and lifted back. */
static void vfs_animation_plays_the_clip_over_the_dimmed_screen(void)
{
    int source_pixel;
    int index;
    int level;
    int first_tinted;
    int early_strangers;
    int late_strangers;
    int dimmed_ticks;
    int levels_seen[ANIM_LEVELS + 1];
    int distinct;

    if (!anim_archive_is_staged()) {
        return;
    }
    anim_play(ANIM_CLIP);

    source_pixel = anim_pattern(ANIM_PROBE_ROW, ANIM_PROBE_COL);
    for (level = 0; level <= ANIM_LEVELS; level++) {
        levels_seen[level] = 0;
    }

    first_tinted = -1;
    early_strangers = 0;
    late_strangers = 0;
    dimmed_ticks = 0;
    for (index = 0; index < anim_sample_count; index++) {
        level = anim_level_of((int) anim_samples[index], source_pixel);
        if (level != 0 && first_tinted < 0) {
            first_tinted = index;
        }
        if (first_tinted < 0) {
            if ((int) anim_samples[index] != source_pixel) {
                early_strangers++;
            }
        } else {
            if (level == 0) {
                late_strangers++;
            } else {
                levels_seen[level] = 1;
                if (level == ANIM_LEVELS) {
                    dimmed_ticks++;
                }
            }
        }
    }

    distinct = 0;
    for (level = 1; level <= ANIM_LEVELS; level++) {
        distinct += levels_seen[level];
    }

    CHECK_EQ(first_tinted >= 0, 1);
    CHECK_EQ(early_strangers, 0);
    CHECK_EQ(late_strangers, 0);
    CHECK_EQ(dimmed_ticks >= ANIM_DIMMED_MIN_TICKS, 1);
    CHECK_EQ(distinct >= ANIM_LEVELS_MIN_SEEN, 1);
}

/* The banner is keyed on the name and on nothing else, so the two phase
   announcements -- 34 frames and 35, the same two fades, the same container --
   differ only in whether it runs.  It costs 24 tick-paced steps and a 500 ms
   hold, so the player-phase name has to be at least twenty ticks dearer; a
   comparison written against the wrong string, or dropped, brings the two
   within one tick of each other.  Nothing the banner draws survives the call --
   the clip repaints the whole frame over it -- so the tick it costs is what
   there is to see.

   BOTH NAMES GO IN MIXED CASE, because that is what the game's own three call
   sites pass and because it is the whole point: the comparison is against an
   upper-case literal, so the banner runs only if the loader upper-cased the
   caller's buffer in place first.  Passing "PLYPHASE.SAF" here would pass
   whether that fold happened or not.

   The banner's sheet is reached the way it reaches it, through the resident
   archive, and the synthetic container the cases above build serves: what the
   banner draws is not what is being asserted here. */
static void vfs_animation_banners_only_the_player_phase_clip(void)
{
    unsigned int enemy_ticks;
    unsigned int player_ticks;

    if (!anim_archive_is_staged()) {
        return;
    }
    banner_stage_container();
    data_fdps_animation_baseani_archive_ptr = banner_vfs;
    data_fdps_battle_turn_counter = BANNER_TURN;

    enemy_ticks = anim_play(ANIM_ENEMY_CLIP);
    player_ticks = anim_play(ANIM_PLAYER_CLIP);

    data_fdps_animation_baseani_archive_ptr = NULL;
    CHECK_EQ(player_ticks > enemy_ticks, 1);
    CHECK_EQ((int) (player_ticks - enemy_ticks) >= ANIM_BANNER_MIN_TICKS, 1);
}


/* ---- fdps_play_attack_animation, 0001ef40 --------------------------------
 *
 * Expected values come from the assembly at 0001ef40 -- CMP EAX,0x1 / JNZ at
 * 0001ef6c and MOV dword ptr [EBP-0x38],0xffffffff at 0001ef83 for the counter
 * test and the poison that suppresses the attacker's bar; the four MOVSX at
 * 0001ef9c, 0001efa6, 0001efd8 and 0001efe2 for the two HP pairs and CMP byte
 * ptr [EAX+0x6],0x0 / JNZ at 0001efb0 and 0001efec for the two graphics; MOV
 * AL,byte ptr [EAX] / AND EAX,0xff / IMUL EAX,EAX,0x18 / SUB EAX,[0x00069ce4]
 * at 0001f002 and the same with SUB EAX,[0x00069ce0] / SUB EAX,0x6 at 0001f018
 * for the request origin; PUSH 0x15180 / CALL malloc at 0001f032 with the
 * pitch and rows at 0001f042 and 0001f049; MOV EAX,0x617d0 / PUSH
 * [0x000643a8] / CALL at 0001f054 for the sheet and CALL 0x000144e0 at 0001f07d
 * for the frame count the loop runs to; ADD EAX,0x18 / IMUL EAX,EAX,0x168 /
 * ADD the page / ADD the x / ADD EAX,0x18 at 0001f088 and 0001f0a0 for the two
 * bar pixels; MOV [EBP-0x4c],EAX at 0001f0d4 for the frame slot; the IMUL
 * ...,0x29 / ADD / DEC / SAR / IDIV pairs at 0001f171 and 0001f267 with their
 * CMP ...,0x0 / JG guards for the two fills, and PUSH 0x0 / PUSH 0xf at
 * 0001f190 and 0001f286 for how both bars are painted; PUSH 0x1 / CALL
 * 0x00014140 at 0001f2b2 for the sprite and its sound flag; and PUSH 0xc0 /
 * PUSH 0x138 / PUSH 0x140 / PUSH 0xa0504 / PUSH 0x168 / page + 0x21d8 at
 * 0001f2e6 for the window.  None of them is read off the emitted C.
 *
 * HOW THE RUN IS WATCHED.  The function composes every frame on a page it
 * allocates and frees itself and blits that page's 312x192 window straight
 * over the live mode 13h screen, so the adapter is the only place its output
 * can be read back from.  Every case sets mode 13h, fills the frame with a
 * border sentinel, seeds the page through the heap, runs a real timer
 * interrupt so the frame waits end, calls, snapshots the 64,000 bytes and
 * returns to text mode -- the same way tests/gauge.c watches
 * fdps_battle_show_combat_gauges and the banner cases above watch the banner.
 *
 * WHY THE PAGE IS SEEDED THROUGH THE HEAP.  The page is not cleared, and with
 * no scene layers, no map cursor and no units staged the compositor writes
 * nothing into it, so whatever malloc hands over is what shows everywhere the
 * bars and the clip do not reach.  Each run frees a zeroed block of exactly
 * the page's 0x15180 bytes immediately before the call, so every undrawn
 * window pixel reads 0.  attack_animation_frees_the_page is that assumption
 * stated as an assertion.
 *
 * WHAT THE SNAPSHOT SHOWS IS EVERY FRAME AT ONCE, not the last one.  Nothing
 * clears the page between passes, so the clip's three frames pile up on it and
 * the frame left on the adapter carries all three marks side by side.  That is
 * what makes the marks worth reading: the fixture puts frame i's mark eight
 * pixels right of frame i-1's, so three marks in the right three places say
 * the loop made one pass per frame of the sheet AND that pass i drew frame i,
 * where a run that drew frame 0 three times would leave one mark and a run
 * that ran the frames backwards would leave them in the wrong order.
 *
 * THE SHEET IS SYNTHETIC AND SO IS THE CONTAINER IT IS LOOKED UP IN.  The
 * shipped EasyAni.Saf is 24-pixel artwork whose own shape would decide every
 * position below; the fixture's frames carry one 4x2 cell each so a mark can
 * be read as a position.  It is wrapped in a synthetic 26-byte-entry container
 * under the name EASYANI.SAF because the resident-image lookup is how the
 * function reaches its sheet, and the entry's name is stored upper-case
 * because that lookup folds only the query.  The .SAF layout is
 * resource_info/saf.md and the container's is resource_info/vfs.md; the offset
 * macros the banner fixture above already spells out are reused rather than
 * restated, and neither set was read off src/.
 *
 * WHAT IS NOT COVERED.  Which frame's sound effect fires -- the 1 at 0001f2b2
 * -- is fdps_draw_composite_sprite's own behaviour and is covered in
 * tests/sprite.c; the fixture's frames all carry -1, which the mixer rejects.
 * The tick pacing between frames and the retrace each frame straddles are
 * playtest contracts (rebuild_info/pitfalls.md): only the floor on how many
 * ticks a whole run costs is asserted here, because the first frame's latch is
 * uninitialised and may end its wait at once.
 * ------------------------------------------------------------------ */

/* The adapter, the frame it presents and the two modes the cases switch
   between. */
#define AA_VGA_BASE 0x000a0000
#define AA_SCREEN_W 0x140
#define AA_SCREEN_H 0xc8
#define AA_SCREEN_BYTES (AA_SCREEN_W * AA_SCREEN_H)
#define AA_MODE_TEXT 0x03
#define AA_MODE_320X200X256 0x13

/* IRQ0, the same vector the banner cases above hook and for the same reason:
   every frame ends waiting for data_fdps_timer_tick_counter to change. */
#define AA_TIMER_VECTOR 8

/* The window the function copies out of its page: 312x192 taken from page byte
   0x21d8, which is page pixel (24,24), and landing at screen byte 0x504, which
   is screen pixel (4,4).  A page column is therefore 20 lower on screen. */
#define AA_WINDOW_ROW 4
#define AA_WINDOW_COL 4
#define AA_WINDOW_W 0x138
#define AA_WINDOW_H 0xc0
#define AA_PAGE_BORDER 24
#define AA_PAGE_BYTES 0x15180
#define AA_TO_SCREEN (AA_WINDOW_COL - AA_PAGE_BORDER)

/* What a screen byte outside the presented window holds. */
#define AA_BORDER_FILL 0xa5

/* The unit gauge sheet's geometry, restated from the assembly at 0001cb00 the
   way tests/gauge.c states it: three 43x6 graphics 0x102 bytes apart with a
   0x2b row pitch, and a 41-column interior between the two-pixel caps. */
#define AA_ART_GRAPHIC_STRIDE 0x102
#define AA_ART_ROW_PITCH 0x2b
#define AA_BAR_WIDTH 0x2b
#define AA_BAR_ROWS 6
#define AA_INTERIOR 0x29
#define AA_SHEET_BYTES 0x306
#define AA_BAR_PIXELS (AA_BAR_WIDTH * AA_BAR_ROWS)

/* Item record geometry and the equipped flag, from IMUL EAX,dword ptr
   [EBP+0x14],0x17 in fdps_get_item_record and the AND AL,0x40 in
   fdps_unit_find_equipped_slot. */
#define AA_ITEM_STRIDE 0x17
#define AA_ITEM_COUNT 256
#define AA_EQUIPPED 0x40
#define AA_ITEM_TYPE_WEAPON 0x01
#define AA_COUNTER_ITEM_ID 7

/* Four records, so an index other than 0 has somewhere to land. */
#define AA_UNITS 4
#define AA_ATTACKER 0
#define AA_DEFENDER 1

/* A map tile and the six-pixel lift the request origin carries. */
#define AA_TILE 0x18
#define AA_LIFT 6

/* The defender's tile, and the attacker's in each of the two arrangements: one
   step away, which is what fdps_check_can_counter_attack needs, and far away,
   which makes it refuse. */
#define AA_DEFENDER_TILE_X 3
#define AA_DEFENDER_TILE_Y 3
#define AA_NEAR_ATTACKER_TILE_X 3
#define AA_NEAR_ATTACKER_TILE_Y 4
#define AA_FAR_ATTACKER_TILE_X 10
#define AA_FAR_ATTACKER_TILE_Y 10

/* fdps_battle_compute_unit_gauge_position's answers for those two at the view
   origin, worked out from its own measured constants (gauge.c): the defender
   faces 0, so its anchor (76,72) takes the affordable up step to y 56 and the
   affordable right step to x 100; the attacker faces 2, so its anchor (76,96)
   takes the affordable down step to y 118 and the affordable left step to
   x 32.  Both bars therefore land clear of each other and of the clip. */
#define AA_DEFENDER_POS_X 100
#define AA_DEFENDER_POS_Y 56
#define AA_ATTACKER_POS_X 32
#define AA_ATTACKER_POS_Y 118

/* Where the clip's first mark lands with the view at the origin: page column
   3 * 24 and page row 3 * 24 - 6, both carried out to the screen. */
#define AA_SPRITE_COL (AA_DEFENDER_TILE_X * AA_TILE + AA_TO_SCREEN)
#define AA_SPRITE_ROW (AA_DEFENDER_TILE_Y * AA_TILE - AA_LIFT + AA_TO_SCREEN)

/* A view scrolled off the map origin, and where the clip lands then.  Neither
   number is a multiple of the tile, so a run that scaled the scroll or applied
   it to the wrong axis misses. */
#define AA_SCROLL_X 10
#define AA_SCROLL_Y 7
#define AA_SCROLLED_SPRITE_COL (AA_SPRITE_COL - AA_SCROLL_X)
#define AA_SCROLLED_SPRITE_ROW (AA_SPRITE_ROW - AA_SCROLL_Y)

/* The synthetic sheet: three frames of one 4x2 cell each, frame i's cell eight
   pixels right of frame i-1's and painted in its own colour.  Frames are
   0x18 bytes apart, which clears the 0x0a header and the 13-byte layer of the
   record before it. */
#define AA_CELL_W 4
#define AA_CELL_H 2
#define AA_FRAMES 3
#define AA_MARK_STEP 8
#define AA_SAF_TILE_TABLE_AT 0x34
#define AA_SAF_TILE0_STREAM_AT 0x40
#define AA_SAF_TILEMAP_TABLE_AT 0x4c
#define AA_SAF_TILEMAP0_AT 0x58
#define AA_SAF_FRAME_TABLE_AT 0x70
#define AA_SAF_FRAME0_AT 0x80
#define AA_SAF_TILE_STREAM_BYTES 4
#define AA_SAF_TILEMAP_BYTES 8
#define AA_SAF_FRAME_BYTES 0x18
#define AA_SAF_IMAGE_BYTES 0x100

/* The synthetic container, laid out with the same macros the banner fixture
   above uses, and the member name the function looks up.  It is stored
   upper-case because fdps_vfs_image_get_entry folds the query and compares the
   entry as the packer wrote it. */
#define AA_VFS_MEMBER "EASYANI.SAF"
#define AA_VFS_MEMBER_AT (BVFS_TABLE_AT + BVFS_ENTRY_BYTES)
#define AA_VFS_IMAGE_BYTES (AA_VFS_MEMBER_AT + AA_SAF_IMAGE_BYTES)

static struct fdps_unit_record aa_units[AA_UNITS];
static unsigned char aa_gauge_sheet[AA_SHEET_BYTES];
static unsigned char aa_items[(AA_ITEM_COUNT + 1) * AA_ITEM_STRIDE];
static unsigned char aa_saf[AA_SAF_IMAGE_BYTES];
static unsigned char aa_vfs[AA_VFS_IMAGE_BYTES];

/* The snapshot is on the heap and not a static, for the reason tests/gauge.c
   gives: this file already holds two 64,000-byte frames and reads the whole of
   MISC.VFS into one malloc, and the guest has 32 MB. */
static unsigned char *aa_screen;
static void (__interrupt __far *aa_saved_timer)();
static int aa_blocks_before;
static int aa_blocks_after;
static unsigned int aa_ticks_used;

static void __interrupt __far aa_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(aa_saved_timer);
}

/* Which colour frame index carries. */
static int aa_mark_pixel(int frame_index)
{
    return 0x11 * (frame_index + 1);
}

/* Distinct neighbours a row pitch and a graphic stride apart, 1..251 so
   nothing is the transparency key by accident and every drawn bar pixel is
   non-zero. */
static void aa_stage_sheet(void)
{
    int offset;

    for (offset = 0; offset < AA_SHEET_BYTES; offset++) {
        aa_gauge_sheet[offset] = (unsigned char) (offset % 251 + 1);
    }
    data_fdps_unit_gauge_sheet_ptr = aa_gauge_sheet;
}

/* One sheet byte, in the bar's own coordinates. */
static int aa_art(int graphic, int row, int column)
{
    return (int) aa_gauge_sheet[graphic * AA_ART_GRAPHIC_STRIDE
                                + row * AA_ART_ROW_PITCH + column];
}

/* The clip: three tiles of their own colour, one single-cell tilemap each, and
   three frames whose layers step eight pixels apart.  Command 0x03 is a fill
   run of four pixels (resource_info/cel.md), so two of them make the two rows
   of a 4x2 cell.  Sound 0xffff is the -1 the mixer rejects. */
static void aa_stage_clip(void)
{
    int frame_index;
    int tile_at;
    int tilemap_at;
    int frame_at;

    memset(aa_saf, 0, (size_t) AA_SAF_IMAGE_BYTES);
    aa_saf[0] = 'S';
    aa_saf[1] = 'A';
    aa_saf[2] = 'F';
    banner_u16(aa_saf, BSAF_CELL_W_AT, AA_CELL_W);
    banner_u16(aa_saf, BSAF_CELL_H_AT, AA_CELL_H);

    banner_u16(aa_saf, BSAF_TILE_COUNT_AT, AA_FRAMES);
    banner_u32(aa_saf, BSAF_TILE_TABLE_PTR_AT,
               (unsigned long) AA_SAF_TILE_TABLE_AT);
    banner_u16(aa_saf, BSAF_TILEMAP_COUNT_AT, AA_FRAMES);
    banner_u32(aa_saf, BSAF_TILEMAP_TABLE_PTR_AT,
               (unsigned long) AA_SAF_TILEMAP_TABLE_AT);
    banner_u16(aa_saf, BSAF_FRAME_COUNT_AT, AA_FRAMES);
    banner_u32(aa_saf, BSAF_FRAME_TABLE_PTR_AT,
               (unsigned long) AA_SAF_FRAME_TABLE_AT);

    for (frame_index = 0; frame_index < AA_FRAMES; frame_index++) {
        tile_at = AA_SAF_TILE0_STREAM_AT
                  + frame_index * AA_SAF_TILE_STREAM_BYTES;
        tilemap_at = AA_SAF_TILEMAP0_AT + frame_index * AA_SAF_TILEMAP_BYTES;
        frame_at = AA_SAF_FRAME0_AT + frame_index * AA_SAF_FRAME_BYTES;

        banner_u32(aa_saf, AA_SAF_TILE_TABLE_AT + frame_index * 4,
                   (unsigned long) tile_at);
        aa_saf[tile_at] = 0x03;
        aa_saf[tile_at + 1] = (unsigned char) aa_mark_pixel(frame_index);
        aa_saf[tile_at + 2] = 0x03;
        aa_saf[tile_at + 3] = (unsigned char) aa_mark_pixel(frame_index);

        banner_u32(aa_saf, AA_SAF_TILEMAP_TABLE_AT + frame_index * 4,
                   (unsigned long) tilemap_at);
        banner_u16(aa_saf, tilemap_at, 1);
        banner_u16(aa_saf, tilemap_at + 2, 1);
        banner_u16(aa_saf, tilemap_at + 4, (unsigned int) frame_index);

        banner_u32(aa_saf, AA_SAF_FRAME_TABLE_AT + frame_index * 4,
                   (unsigned long) frame_at);
        banner_u16(aa_saf, frame_at + BSAF_FRAME_SOUND_AT, BSAF_NO_SOUND);
        banner_u16(aa_saf, frame_at + BSAF_FRAME_DURATION_AT, 1);
        banner_u16(aa_saf, frame_at + BSAF_FRAME_LAYERS_AT, 1);
        banner_u16(aa_saf, frame_at + BSAF_LAYER_AT + BSAF_LAYER_TILEMAP_AT,
                   (unsigned int) frame_index);
        banner_u16(aa_saf, frame_at + BSAF_LAYER_AT + BSAF_LAYER_X_AT,
                   (unsigned int) (frame_index * AA_MARK_STEP));
        banner_u16(aa_saf, frame_at + BSAF_LAYER_AT + BSAF_LAYER_Y_AT, 0);
        aa_saf[frame_at + BSAF_LAYER_AT + BSAF_LAYER_BLEND_AT] = 0;
    }
}

/* The container that clip is looked up in, published as the resident archive
   the function reads on every call. */
static void aa_stage_container(void)
{
    aa_stage_clip();
    memset(aa_vfs, 0, (size_t) AA_VFS_IMAGE_BYTES);
    aa_vfs[0] = 'V';
    aa_vfs[1] = 'F';
    aa_vfs[2] = 'S';
    banner_u16(aa_vfs, 3, 1);
    banner_u16(aa_vfs, 5, BVFS_TABLE_AT);
    banner_u32(aa_vfs, 7, 1);
    strcpy((char *) aa_vfs + BVFS_TABLE_AT, AA_VFS_MEMBER);
    banner_u32(aa_vfs, BVFS_TABLE_AT + BVFS_ENTRY_SIZE_AT,
               (unsigned long) AA_SAF_IMAGE_BYTES);
    banner_u32(aa_vfs, BVFS_TABLE_AT + BVFS_ENTRY_SIZE2_AT,
               (unsigned long) AA_SAF_IMAGE_BYTES);
    banner_u32(aa_vfs, BVFS_TABLE_AT + BVFS_ENTRY_START_AT,
               (unsigned long) AA_VFS_MEMBER_AT);
    memmove(aa_vfs + AA_VFS_MEMBER_AT, aa_saf, (size_t) AA_SAF_IMAGE_BYTES);
}

/* One unit's tile, facing, side and HP pair.  Every record is zeroed first, so
   nothing is equipped and every status timer is clear. */
static void aa_set_unit(int unit_index, int tile_column, int tile_row,
                        int facing, int side, int hp_current, int hp_max)
{
    aa_units[unit_index].pos_x = (unsigned char) tile_column;
    aa_units[unit_index].pos_y = (unsigned char) tile_row;
    aa_units[unit_index].facing = (unsigned char) facing;
    aa_units[unit_index].side = (unsigned char) side;
    aa_units[unit_index].hp_current = (short) hp_current;
    aa_units[unit_index].hp_max = (short) hp_max;
}

/* Nothing on the map and nothing in the way -- no scene layers, no cursor
   overlay and no units -- so the compositor writes nothing into the page and
   every window pixel the bars and the clip do not reach is the seed. */
static void aa_stage(void)
{
    int offset;

    for (offset = 0; offset < (int) sizeof(aa_units); offset++) {
        ((unsigned char *) aa_units)[offset] = 0;
    }
    for (offset = 0; offset < (int) sizeof(aa_items); offset++) {
        aa_items[offset] = 0;
    }
    aa_stage_sheet();
    aa_stage_container();
    data_fdps_map_unit_array_ptr = (unsigned char *) aa_units;
    data_fdps_item_effect_table_ptr = aa_items + AA_ITEM_STRIDE;
    data_fdps_animation_baseani_archive_ptr = aa_vfs;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_count = 0;
}

/* Put the staged globals back the way a freshly started program has them, for
   the reason tests/gauge.c gives: three of them hold blocks the game's own
   loaders free, and leaving one pointing at a static here hands a later test a
   free() of storage that never came from the heap. */
static void aa_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_item_effect_table_ptr = NULL;
    data_fdps_unit_gauge_sheet_ptr = NULL;
    data_fdps_animation_baseani_archive_ptr = NULL;
    free(aa_screen);
    aa_screen = NULL;
}

/* Give the defender an equipped weapon of minimum range 1, which is the last
   of fdps_check_can_counter_attack's four tests. */
static void aa_arm_defender(void)
{
    struct fdps_item_effect *weapon;

    aa_units[AA_DEFENDER].inventory_slots[0] = AA_EQUIPPED;
    aa_units[AA_DEFENDER].inventory_slots[1] = AA_COUNTER_ITEM_ID;
    weapon = (struct fdps_item_effect *)
             (aa_items + (AA_COUNTER_ITEM_ID + 1) * AA_ITEM_STRIDE);
    weapon->type = AA_ITEM_TYPE_WEAPON;
    weapon->range_min = 1;
    weapon->range_max = 1;
}

static void aa_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* Used entries currently in the heap, so a case can say the page came back. */
static int aa_used_heap_blocks(void)
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

/* Leave a zeroed block of exactly the page's size at the head of the free
   list. */
static void aa_seed_page(void)
{
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) AA_PAGE_BYTES);
    if (page != NULL) {
        memset(page, 0, (size_t) AA_PAGE_BYTES);
        free(page);
    }
}

/* One whole run, leaving the frame in aa_screen[]. */
static void aa_run(void)
{
    unsigned int before_ticks;

    aa_screen = (unsigned char *) malloc((size_t) AA_SCREEN_BYTES);
    CHECK_EQ(aa_screen != NULL, 1);
    if (aa_screen == NULL) {
        return;
    }
    memset(aa_screen, AA_BORDER_FILL, (size_t) AA_SCREEN_BYTES);

    aa_blocks_before = aa_used_heap_blocks();
    aa_set_mode(AA_MODE_320X200X256);
    memset((void *) AA_VGA_BASE, AA_BORDER_FILL, (size_t) AA_SCREEN_BYTES);
    aa_seed_page();

    aa_saved_timer = _dos_getvect(AA_TIMER_VECTOR);
    _dos_setvect(AA_TIMER_VECTOR, aa_timer_isr);
    before_ticks = data_fdps_timer_tick_counter;
    fdps_play_attack_animation(AA_ATTACKER, AA_DEFENDER);
    aa_ticks_used = data_fdps_timer_tick_counter - before_ticks;
    _dos_setvect(AA_TIMER_VECTOR, aa_saved_timer);

    memmove(aa_screen, (void *) AA_VGA_BASE, (size_t) AA_SCREEN_BYTES);
    aa_set_mode(AA_MODE_TEXT);
    aa_blocks_after = aa_used_heap_blocks();
}

static int aa_pixel(int row, int col)
{
    return (int) aa_screen[row * AA_SCREEN_W + col];
}

/* One pixel of a bar whose gauge position is (pos_x, pos_y), in the bar's own
   coordinates: the position plus the page's 24-pixel apron, carried out to the
   screen's own 4-pixel inset. */
static int aa_bar(int pos_x, int pos_y, int row, int column)
{
    return aa_pixel(pos_y + AA_WINDOW_ROW + row, pos_x + AA_WINDOW_COL + column);
}

/* How many bytes inside the presented window are not the page seed. */
static int aa_painted(void)
{
    int row;
    int col;
    int painted;

    painted = 0;
    for (row = 0; row < AA_WINDOW_H; row++) {
        for (col = 0; col < AA_WINDOW_W; col++) {
            if (aa_pixel(AA_WINDOW_ROW + row, AA_WINDOW_COL + col) != 0) {
                painted++;
            }
        }
    }
    return painted;
}

/* How many bytes outside the presented window are no longer the sentinel. */
static int aa_border_touched(void)
{
    int row;
    int col;
    int touched;

    touched = 0;
    for (row = 0; row < AA_SCREEN_H; row++) {
        for (col = 0; col < AA_SCREEN_W; col++) {
            if (row >= AA_WINDOW_ROW && row < AA_WINDOW_ROW + AA_WINDOW_H
                && col >= AA_WINDOW_COL && col < AA_WINDOW_COL + AA_WINDOW_W) {
                continue;
            }
            if (aa_pixel(row, col) != AA_BORDER_FILL) {
                touched++;
            }
        }
    }
    return touched;
}

/* Every segment of one bar as the plain painter leaves it: the two-pixel left
   cap and the fill run out of graphic gfx_index, the rest of the 41-column
   interior out of GRAPHIC 0 at its own columns, and the right cap out of
   gfx_index again.  Row 5 is read alongside row 0 because every blit is handed
   6 as its row count and steps by the page's 0x168 pitch.  The values being
   raw sheet bytes is what pins the painting mode at 0: mode 1 would write
   inverse-cube entries and any other value a tint. */
static void aa_bar_is(int pos_x, int pos_y, int gfx_index, int fill_width)
{
    CHECK_EQ(aa_bar(pos_x, pos_y, 0, 0), aa_art(gfx_index, 0, 0));
    CHECK_EQ(aa_bar(pos_x, pos_y, 5, 1), aa_art(gfx_index, 5, 1));
    if (fill_width > 0) {
        CHECK_EQ(aa_bar(pos_x, pos_y, 0, fill_width + 1),
                 aa_art(gfx_index, 0, fill_width + 1));
    }
    CHECK_EQ(aa_art(0, 0, fill_width + 2)
             != aa_art(gfx_index, 0, fill_width + 2), 1);
    CHECK_EQ(aa_bar(pos_x, pos_y, 0, fill_width + 2),
             aa_art(0, 0, fill_width + 2));
    CHECK_EQ(aa_bar(pos_x, pos_y, 0, AA_INTERIOR), aa_art(gfx_index, 0,
                                                          AA_INTERIOR));
    CHECK_EQ(aa_bar(pos_x, pos_y, 5, AA_BAR_WIDTH - 1),
             aa_art(gfx_index, 5, AA_BAR_WIDTH - 1));
}

/* Every mark the clip left, read at both ends of its 4x2 cell, with the pixel
   just left of the first one still the seed so a mark one column wide of where
   it belongs fails here. */
static void aa_marks_are(int first_col, int row)
{
    int frame_index;
    int col;

    for (frame_index = 0; frame_index < AA_FRAMES; frame_index++) {
        col = first_col + frame_index * AA_MARK_STEP;
        CHECK_EQ(aa_pixel(row, col), aa_mark_pixel(frame_index));
        CHECK_EQ(aa_pixel(row + AA_CELL_H - 1, col + AA_CELL_W - 1),
                 aa_mark_pixel(frame_index));
    }
    CHECK_EQ(aa_pixel(row, first_col - 1), 0);
    CHECK_EQ(aa_pixel(row - 1, first_col), 0);
}

/* The five record bytes the function addresses by literal displacement: +0 and
   +1 for the tile the clip is placed over, +6 for the side that picks each
   graphic, and +0x40 / +0x42 for the HP pair each fill is taken over.  If the
   layout moved, every case below would still pass while reading the wrong
   bytes. */
static void attack_animation_reads_the_measured_offsets(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_max), 0x42);
}

/* The clip is placed over the DEFENDER's tile and one frame is drawn per pass,
   frame i on pass i.  Tile (3,3) with the view at the origin puts frame 0's
   cell at page (72,66) -- the tile times 24 on x and the same less the
   six-pixel lift on y -- which the window blit carries out to screen (52,46),
   and the fixture steps each later frame eight pixels right of that.

   The attacker is two tiles away, so no counter is coming and only the
   defender's bar is drawn: 258 bar pixels and three 4x2 marks are the whole of
   what the run painted, which is also what says the clip was drawn over the
   defender's tile and not the attacker's -- the attacker's would put the marks
   168 pixels to the right. */
static void attack_animation_draws_one_frame_per_pass_over_the_defender(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    aa_marks_are(AA_SPRITE_COL, AA_SPRITE_ROW);
    CHECK_EQ(aa_painted(),
             AA_BAR_PIXELS + AA_FRAMES * AA_CELL_W * AA_CELL_H);
    aa_unstage();
}

/* SUB EAX,[0x00069ce4] and SUB EAX,[0x00069ce0]: the view scroll origin is
   subtracted from each axis of the request origin, and the six-pixel lift is
   applied on top of the y one rather than instead of it.  A scroll of (10,7)
   moves the whole clip by exactly that and by nothing else. */
static void attack_animation_subtracts_the_view_scroll_origin(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    data_fdps_battle_view_window_origin_x = AA_SCROLL_X;
    data_fdps_battle_view_window_origin_y = AA_SCROLL_Y;
    aa_run();

    aa_marks_are(AA_SCROLLED_SPRITE_COL, AA_SCROLLED_SPRITE_ROW);
    aa_unstage();
}

/* The one bar that run drew, read back segment by segment, and where it
   landed.  The defender's side byte is 0, so its graphic is 2 and not 1, and
   1 HP of 1000 is (41 + 999) / 1000 = 1 filled column -- the sliver the
   ceiling exists for.  The bar's top-left pixel at screen (60,104) for a gauge
   position of (100,56) is what fixes the page's 24-pixel apron and its 0x168
   pitch against the window's 4-pixel inset: a run that left the apron out
   would put it at (80,124). */
static void attack_animation_draws_the_defender_bar_from_side_zero(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_art(2, 0, 0) != aa_art(1, 0, 0), 1);
    CHECK_EQ(aa_pixel(AA_DEFENDER_POS_Y + AA_WINDOW_ROW,
                      AA_DEFENDER_POS_X + AA_WINDOW_COL), aa_art(2, 0, 0));
    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 2, 1);
    aa_unstage();
}

/* CMP byte ptr [EAX+0x6],0x0 / JNZ at 0001efec: any side other than 0 takes
   graphic 1, so the same bar in the same place comes out of a different
   graphic.  Side 2 is used rather than 1 to show the test is against 0 and not
   a two-way flag. */
static void attack_animation_side_other_than_zero_takes_graphic_one(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 2,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 1, 1);
    aa_unstage();
}

/* Both bars, with the counter confirmed.  The attacker is one tile away and
   the defender holds an equipped weapon of minimum range 1, which is what
   makes fdps_check_can_counter_attack answer exactly 1; the attacker's pair
   then holds its own placement instead of the -1 that suppresses it.

   The attacker's side is 1, so its graphic is 1 against the defender's 2, and
   3 HP of 4 is (123 + 3) / 4 = 31 filled columns against the defender's 1.
   The painted count is two whole bars and the clip's three marks, so neither
   bar has overwritten the other or the clip. */
static void attack_animation_draws_both_bars_on_a_counter(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_NEAR_ATTACKER_TILE_X, AA_NEAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_arm_defender();
    aa_run();

    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 2, 1);
    aa_bar_is(AA_ATTACKER_POS_X, AA_ATTACKER_POS_Y, 1, 31);
    CHECK_EQ(aa_painted(),
             2 * AA_BAR_PIXELS + AA_FRAMES * AA_CELL_W * AA_CELL_H);
    aa_unstage();
}

/* The same two units and the same one step apart, but with nothing equipped:
   the counter is refused, the attacker's pair x is poisoned with -1 and its
   bar is skipped for every pass.  The count is one bar and the clip, and the
   place the attacker's bar would have taken is still the page seed. */
static void attack_animation_suppresses_the_attacker_bar_without_a_counter(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_NEAR_ATTACKER_TILE_X, AA_NEAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_painted(),
             AA_BAR_PIXELS + AA_FRAMES * AA_CELL_W * AA_CELL_H);
    CHECK_EQ(aa_bar(AA_ATTACKER_POS_X, AA_ATTACKER_POS_Y, 0, 0), 0);
    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 2, 1);
    aa_unstage();
}

/* ADD EDX,max / DEC EDX before the IDIV at 0001f267 is what makes the fill a
   ceiling and not a truncation: 1 HP of 2 is (41 + 1) / 2 = 21 columns, where
   41 / 2 would be 20 and the seam would sit one column left.  The seam is read
   at both ends, so a fill of 20 or 22 fails here. */
static void attack_animation_fill_is_the_ceiling_over_41_columns(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 2);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_bar(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 0, 22),
             aa_art(2, 0, 22));
    CHECK_EQ(aa_art(0, 0, 23) != aa_art(2, 0, 23), 1);
    CHECK_EQ(aa_bar(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 0, 23),
             aa_art(0, 0, 23));
    aa_unstage();
}

/* CMP dword ptr [EBP+0xffffff34],0x0 / JG at 0001f252: a maximum of 0 never
   reaches the IDIV and the bar is drawn empty, so the interior is graphic 0
   from its first column.  A current of 50 against it would be a division by
   zero if the guard were not there, and the guard is JG and not JNE, which is
   what the negative case says. */
static void attack_animation_zero_max_hp_draws_an_empty_bar(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                50, 0);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 2, 0);
    CHECK_EQ(aa_painted(),
             AA_BAR_PIXELS + AA_FRAMES * AA_CELL_W * AA_CELL_H);
    aa_unstage();
}

/* The HP words are read with MOVSX at 0001efd8 and 0001efe2, so a current
   above 0x7fff is negative and not a huge positive: -1 of 1000 gives
   (-41 + 999) / 1000 = 0 columns after truncation toward zero, which is the
   empty bar, where an unsigned read would give a fill far past the interior
   and smear the art's next row across it. */
static void attack_animation_hp_words_are_read_signed(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                -1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    aa_bar_is(AA_DEFENDER_POS_X, AA_DEFENDER_POS_Y, 2, 0);
    CHECK_EQ(aa_painted(),
             AA_BAR_PIXELS + AA_FRAMES * AA_CELL_W * AA_CELL_H);
    aa_unstage();
}

/* PUSH 0xc0 / PUSH 0x138 / PUSH 0x140 / PUSH 0xa0504 / PUSH 0x168 with
   page + 0x21d8 as the source: 312x192 out of page pixel (24,24) and down at
   screen pixel (4,4).  Nothing outside that rectangle is touched, which is
   what the four-pixel margin of the sentinel proves -- a destination of
   0xa0000, or a source of page byte 0, would carry the clip and the bar four
   rows and four columns out of place and leave the margin alone anyway, which
   is why the marks are read as well. */
static void attack_animation_presents_312x192_at_screen_4_4(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_border_touched(), 0);
    aa_marks_are(AA_SPRITE_COL, AA_SPRITE_ROW);
    CHECK_EQ(aa_pixel(AA_DEFENDER_POS_Y + AA_WINDOW_ROW,
                      AA_DEFENDER_POS_X + AA_WINDOW_COL - 1), 0);
    CHECK_EQ(aa_pixel(AA_DEFENDER_POS_Y + AA_WINDOW_ROW + AA_BAR_ROWS,
                      AA_DEFENDER_POS_X + AA_WINDOW_COL), 0);
    aa_unstage();
}

/* CALL free at 0001f32c: the page is released before the return, so the heap
   holds no more used blocks after the run than before it.  This is also what
   the seeding depends on -- if the block did not come back, every expected
   value above would be comparing against rubbish. */
static void attack_animation_frees_the_page(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_blocks_after, aa_blocks_before);
    aa_unstage();
}

/* Every frame ends waiting for the timer tick to move -- MOV EAX,[EBP-0x20] /
   CMP EAX,[0x00069d64] / JZ back at 0001f310, inside the loop and not after
   it.  The first frame's latch is uninitialised and may end its wait at once,
   so two ticks are the guaranteed floor for a three-frame clip; a run that
   waited nowhere does not reach it, and one that latched the counter before
   the loop would cost three. */
static void attack_animation_paces_the_frames_with_the_tick(void)
{
    aa_stage();
    aa_set_unit(AA_DEFENDER, AA_DEFENDER_TILE_X, AA_DEFENDER_TILE_Y, 0, 0,
                1, 1000);
    aa_set_unit(AA_ATTACKER, AA_FAR_ATTACKER_TILE_X, AA_FAR_ATTACKER_TILE_Y,
                2, 1, 3, 4);
    aa_run();

    CHECK_EQ(aa_ticks_used >= (unsigned int) (AA_FRAMES - 1), 1);
    aa_unstage();
}

/* ---- fdps_play_vfs_animation_over_units, 00026e00 ------------------------
 *
 * Expected values come from the assembly at 00026e00 -- PUSH 0x15180 / CALL
 * malloc at 00026e0c with the pitch and rows stored at 00026e1c and 00026e23;
 * MOV EAX,0x60128 / PUSH at 00026e2e for the container the member is loaded out
 * of and CALL 0x000144e0 at 00026e51 for the frame total the outer loop runs
 * to; CMP dword ptr [EBP-0x8],0x2 / JL at 00026e7f for the two ticks every
 * frame is held; MOV AL,byte ptr [EAX] / AND EAX,0xff / IMUL EAX,EAX,0x18 /
 * SUB EAX,[0x00069ce4] / SUB EAX,0x18 at 00026ec6 and the same down the y axis
 * with SUB EAX,[0x00069ce0] / SUB EAX,0x1e at 00026ef2 for where each copy is
 * placed; the pair of tests at 00026f0c for which single draw carries the sound
 * flag; and PUSH 0xc0 / PUSH 0x138 / PUSH 0x140 / PUSH 0xa0504 / PUSH 0x168
 * with page + 0x21d8 at 00026f63 for the window.  None of them is read off the
 * emitted C.
 *
 * HOW THE RUN IS WATCHED, and why the page is seeded through the heap: exactly
 * as the attack-animation cases above do it, and for the same reasons.  The
 * page is allocated and freed inside the call and its 312x192 window is blitted
 * straight over the live mode 13h screen, so the adapter is the only place the
 * output can be read back from; the page is never cleared, so a zeroed block of
 * its exact size is freed immediately before each call and every undrawn window
 * pixel then reads 0.  The staging is the attack cases' staging -- no scene
 * layers, no cursor overlay, no map units -- so fdps_draw_scene_layers writes
 * nothing into the page, which vfx_animation_no_units_draws_nothing states as
 * an assertion rather than leaving as an assumption.
 *
 * THE CLIP IS REAL AND CANNOT BE STOOD IN FOR.  The container's name is a
 * literal inside the function, so there is nothing to point at a smaller file,
 * and a member it cannot find ends the process rather than failing an
 * assertion.  PosEff.saf is one of the members the game's own call sites name:
 * 990 bytes, ten frames of 24x24 cells, seven single-cell tilemaps and layer
 * offsets that move between frames, every frame carrying sound -1.  It is
 * passed in the MIXED CASE the call sites use, so the loader's in-place fold is
 * in the path of the test.  Its frame count is asserted out of the member's own
 * header rather than assumed.
 *
 * WHAT THE SNAPSHOT IS COMPARED WITH.  Every window byte is compared against a
 * page this file composes itself, by driving the same production compositor
 * over the same clip at the origins the ARITHMETIC ABOVE gives -- one draw per
 * listed unit per tick, two ticks per frame, frames in ascending order.  That
 * makes the comparison pin WHERE each copy lands and how many are drawn without
 * pinning anything about the artwork, which is the sheet's business: a run that
 * dropped the -0x18 or the -0x1e, or scaled a tile by anything but 24, or drew
 * one copy for the list instead of one per entry, moves or loses bytes the
 * comparison counts.  The reference does not call fdps_draw_scene_layers,
 * because under this staging it writes nothing.
 *
 * WHAT IS NOT COVERED.  Which draw carries the sound flag -- the 1 at 00026f2a
 * for the first unit of a frame's first tick -- reaches fdps_sfx_play as the
 * frame's own sound number, and every frame of PosEff.saf carries -1, which
 * that function rejects; what a non-zero play_sound does is
 * fdps_draw_composite_sprite's own behaviour and is covered in tests/sprite.c.
 * The retrace each tick straddles is a playtest contract
 * (rebuild_info/pitfalls.md).  Only a FLOOR is put under the tick cost: the
 * first tick's latch is uninitialised so it may end its wait at once, and the
 * counter can advance more than once while a tick is composed, so an upper
 * bound would be measuring the emulator's cycle setting.
 * ------------------------------------------------------------------ */

/* The container, and the member out of it.  Mixed case on purpose -- see the
   note above. */
#define VU_ARCHIVE "MISC.VFS"
#define VU_CLIP "PosEff.saf"
#define VU_CLIP_MEMBER "POSEFF.SAF"
#define VU_CLIP_FRAMES 10
#define VU_CLIP_BYTES 990L
#define VU_NAME_MAX 16

/* The .SAF header field the frame total is asserted out of, +0x0c
   (resource_info/saf.md). */
#define VU_SAF_FRAME_COUNT_AT 0x0c

/* The page and the window it is presented through, the same numbers the attack
   cases above spell out for the same page. */
#define VU_PAGE_PITCH 0x168
#define VU_PAGE_ROWS 0xf0
#define VU_PAGE_BYTES 0x15180
#define VU_PAGE_BORDER 24

/* Two ticks a frame, a 24-pixel tile, and the corner the copy is drawn from. */
#define VU_TICKS_PER_FRAME 2
#define VU_TILE 0x18
#define VU_ORIGIN_LEFT 0x18
#define VU_ORIGIN_UP 0x1e

/* Four records, so a unit id that is neither 0 nor its own position in the list
   has somewhere to land, and the two ids the list carries. */
#define VU_UNITS 4
#define VU_FIRST_ID 2
#define VU_SECOND_ID 0

/* Two tiles far enough apart that the two copies cannot overlap, both placing
   every one of the clip's ten frames inside the page and inside the presented
   window. */
#define VU_FIRST_TILE_X 5
#define VU_FIRST_TILE_Y 5
#define VU_SECOND_TILE_X 9
#define VU_SECOND_TILE_Y 7

/* A view scrolled off the map origin.  Neither number is a multiple of the
   tile, so a run that scaled the scroll or applied it to the wrong axis
   misses. */
#define VU_SCROLL_X 10
#define VU_SCROLL_Y 7

static struct fdps_unit_record vu_units[VU_UNITS];
static unsigned char vu_ids[VU_UNITS];

/* The snapshot, the reference page and the clip, all on the heap for the reason
   tests/gauge.c gives: this file already holds two 64,000-byte frames. */
static unsigned char *vu_screen;
static unsigned char *vu_reference;
static unsigned char *vu_clip;
static long vu_clip_bytes;

static void (__interrupt __far *vu_saved_timer)();
static int vu_blocks_before;
static int vu_blocks_after;
static unsigned int vu_ticks_used;

static void __interrupt __far vu_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(vu_saved_timer);
}

/* One member out of a container, by walking the container's own directory --
   26-byte entries at the offset the header's field at 5 names, each with its
   name at +0, its size at +0x0d and its start at +0x16
   (resource_info/vfs.md).  The walk is written out here rather than taken
   through src/vfs.c so that staging does not lean on a callee of the function
   under test.  Returns NULL when the file is not staged or holds no such
   member; the block is the caller's to free. */
static unsigned char *vu_read_member(char *archive_name, char *member_name,
                                     long *out_bytes)
{
    FILE *fp;
    unsigned char header[16];
    unsigned char *table;
    unsigned char *member;
    unsigned long table_at;
    unsigned long count;
    unsigned long index;
    unsigned long entry;
    unsigned long start;
    unsigned long bytes;

    *out_bytes = 0;
    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fread(header, (size_t) sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }
    table_at = (unsigned long) header[5] | ((unsigned long) header[6] << 8);
    count = (unsigned long) header[7] | ((unsigned long) header[8] << 8)
            | ((unsigned long) header[9] << 16)
            | ((unsigned long) header[10] << 24);
    table = (unsigned char *) malloc((size_t) (count * BVFS_ENTRY_BYTES));
    if (table == NULL) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, (long) table_at, SEEK_SET);
    if (fread(table, (size_t) (count * BVFS_ENTRY_BYTES), 1, fp) != 1) {
        free(table);
        fclose(fp);
        return NULL;
    }

    member = NULL;
    for (index = 0; index < count; index++) {
        entry = index * BVFS_ENTRY_BYTES;
        if (strcmp((char *) table + entry, member_name) != 0) {
            continue;
        }
        bytes = (unsigned long) table[entry + BVFS_ENTRY_SIZE_AT]
                | ((unsigned long) table[entry + BVFS_ENTRY_SIZE_AT + 1] << 8)
                | ((unsigned long) table[entry + BVFS_ENTRY_SIZE_AT + 2] << 16)
                | ((unsigned long) table[entry + BVFS_ENTRY_SIZE_AT + 3] << 24);
        start = (unsigned long) table[entry + BVFS_ENTRY_START_AT]
                | ((unsigned long) table[entry + BVFS_ENTRY_START_AT + 1] << 8)
                | ((unsigned long) table[entry + BVFS_ENTRY_START_AT + 2] << 16)
                | ((unsigned long) table[entry + BVFS_ENTRY_START_AT + 3] << 24);
        member = (unsigned char *) malloc((size_t) bytes);
        if (member != NULL) {
            fseek(fp, (long) start, SEEK_SET);
            if (fread(member, (size_t) bytes, 1, fp) != 1) {
                free(member);
                member = NULL;
            } else {
                *out_bytes = (long) bytes;
            }
        }
        break;
    }

    free(table);
    fclose(fp);
    return member;
}

/* Everything the run needs and nothing else: four zeroed records published as
   the battle's unit array, no scene layers, no cursor overlay and no map units,
   so the scene repaint writes nothing into the page. */
static int vu_stage(void)
{
    int offset;

    for (offset = 0; offset < (int) sizeof(vu_units); offset++) {
        ((unsigned char *) vu_units)[offset] = 0;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) vu_units;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_map_unit_count = 0;

    vu_screen = (unsigned char *) malloc((size_t) BANNER_SCREEN_BYTES);
    vu_reference = (unsigned char *) malloc((size_t) VU_PAGE_BYTES);
    vu_clip = vu_read_member(VU_ARCHIVE, VU_CLIP_MEMBER, &vu_clip_bytes);
    if (vu_screen == NULL || vu_reference == NULL || vu_clip == NULL) {
        return 0;
    }
    return 1;
}

/* Put the staged globals back the way a freshly started program has them, for
   the reason the attack cases give: the array pointer holds storage the game's
   own loaders free. */
static void vu_unstage(void)
{
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    free(vu_screen);
    free(vu_reference);
    free(vu_clip);
    vu_screen = NULL;
    vu_reference = NULL;
    vu_clip = NULL;
}

static void vu_set_unit(int unit_index, int tile_column, int tile_row)
{
    vu_units[unit_index].pos_x = (unsigned char) tile_column;
    vu_units[unit_index].pos_y = (unsigned char) tile_row;
}

/* Used entries currently in the heap, so a case can say the page and the clip
   came back. */
static int vu_used_heap_blocks(void)
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

/* Leave a zeroed block of exactly the page's size at the head of the free
   list. */
static void vu_seed_page(void)
{
    unsigned char *page;

    page = (unsigned char *) malloc((size_t) VU_PAGE_BYTES);
    if (page != NULL) {
        memset(page, 0, (size_t) VU_PAGE_BYTES);
        free(page);
    }
}

/* One whole run, leaving the frame in vu_screen[]. */
static void vu_run(int unit_count)
{
    char query[VU_NAME_MAX];
    unsigned int before_ticks;

    strcpy(query, VU_CLIP);
    memset(vu_screen, AA_BORDER_FILL, (size_t) BANNER_SCREEN_BYTES);

    vu_blocks_before = vu_used_heap_blocks();
    banner_set_mode(BANNER_MODE_320X200X256);
    memset((void *) BANNER_VGA_BASE, AA_BORDER_FILL,
           (size_t) BANNER_SCREEN_BYTES);
    vu_seed_page();

    vu_saved_timer = _dos_getvect(BANNER_TIMER_VECTOR);
    _dos_setvect(BANNER_TIMER_VECTOR, vu_timer_isr);
    before_ticks = data_fdps_timer_tick_counter;
    fdps_play_vfs_animation_over_units(unit_count, vu_ids, query);
    vu_ticks_used = data_fdps_timer_tick_counter - before_ticks;
    _dos_setvect(BANNER_TIMER_VECTOR, vu_saved_timer);

    memmove(vu_screen, (void *) BANNER_VGA_BASE,
            (size_t) BANNER_SCREEN_BYTES);
    banner_set_mode(BANNER_MODE_TEXT);
    vu_blocks_after = vu_used_heap_blocks();
}

/* The page the run had to produce: the clip's ten frames in ascending order,
   each drawn twice, and each of those drawn once per listed unit at the origin
   the arithmetic gives.  play_sound is 0 throughout because it decides nothing
   about pixels (sprite.h). */
static void vu_render_reference(int unit_count)
{
    int request[DRAW_REQUEST_DWORDS];
    struct fdps_unit_record *record;
    int frame;
    int tick;
    int unit;

    memset(vu_reference, 0, (size_t) VU_PAGE_BYTES);
    request[DRAW_REQUEST_DEST_BASE] = (int) vu_reference;
    request[DRAW_REQUEST_DEST_PITCH] = VU_PAGE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = VU_PAGE_ROWS;
    request[DRAW_REQUEST_IMAGE] = (int) vu_clip;
    request[DRAW_REQUEST_BLIT_OPERAND] = 0;
    request[DRAW_REQUEST_BLIT_MODE] = 0;

    for (frame = 0; frame < VU_CLIP_FRAMES; frame++) {
        for (tick = 0; tick < VU_TICKS_PER_FRAME; tick++) {
            request[DRAW_REQUEST_ITEM_INDEX] = frame;
            for (unit = 0; unit < unit_count; unit++) {
                record = &vu_units[vu_ids[unit]];
                request[DRAW_REQUEST_X] =
                    (int) record->pos_x * VU_TILE
                    - data_fdps_battle_view_window_origin_x - VU_ORIGIN_LEFT;
                request[DRAW_REQUEST_Y] =
                    (int) record->pos_y * VU_TILE
                    - data_fdps_battle_view_window_origin_y - VU_ORIGIN_UP;
                fdps_draw_composite_sprite(request, 0);
            }
        }
    }
}

/* How many bytes of the presented window differ from the reference page's own
   window: screen pixel (4,4) against page pixel (24,24), 312 x 192 of them. */
static int vu_window_differences(void)
{
    int row;
    int col;
    int bad;

    bad = 0;
    for (row = 0; row < AA_WINDOW_H; row++) {
        for (col = 0; col < AA_WINDOW_W; col++) {
            if (vu_screen[(row + AA_WINDOW_ROW) * BANNER_SCREEN_W
                          + col + AA_WINDOW_COL]
                != vu_reference[(row + VU_PAGE_BORDER) * VU_PAGE_PITCH
                                + col + VU_PAGE_BORDER]) {
                bad++;
            }
        }
    }
    return bad;
}

/* How many bytes of the reference page's window are not the seed, so a
   comparison that matched two blank pictures cannot pass for a match. */
static int vu_reference_painted(void)
{
    int row;
    int col;
    int painted;

    painted = 0;
    for (row = 0; row < AA_WINDOW_H; row++) {
        for (col = 0; col < AA_WINDOW_W; col++) {
            if (vu_reference[(row + VU_PAGE_BORDER) * VU_PAGE_PITCH
                             + col + VU_PAGE_BORDER] != 0) {
                painted++;
            }
        }
    }
    return painted;
}

/* How many bytes of the presented window are not the page seed. */
static int vu_screen_painted(void)
{
    int row;
    int col;
    int painted;

    painted = 0;
    for (row = 0; row < AA_WINDOW_H; row++) {
        for (col = 0; col < AA_WINDOW_W; col++) {
            if (vu_screen[(row + AA_WINDOW_ROW) * BANNER_SCREEN_W
                          + col + AA_WINDOW_COL] != 0) {
                painted++;
            }
        }
    }
    return painted;
}

/* How many bytes outside the presented window are no longer the sentinel. */
static int vu_border_touched(void)
{
    int row;
    int col;
    int touched;

    touched = 0;
    for (row = 0; row < BANNER_SCREEN_H; row++) {
        for (col = 0; col < BANNER_SCREEN_W; col++) {
            if (row >= AA_WINDOW_ROW && row < AA_WINDOW_ROW + AA_WINDOW_H
                && col >= AA_WINDOW_COL && col < AA_WINDOW_COL + AA_WINDOW_W) {
                continue;
            }
            if (vu_screen[row * BANNER_SCREEN_W + col] != AA_BORDER_FILL) {
                touched++;
            }
        }
    }
    return touched;
}

/* The two record bytes the function addresses by literal displacement, +0 and
   +1, and the ten frames the fixture's own header names.  If the layout moved
   or a different member were staged, every case below would still pass while
   measuring something else. */
static void vfx_animation_reads_the_measured_offsets(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);

    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    CHECK_EQ(vu_clip_bytes, VU_CLIP_BYTES);
    CHECK_EQ((int) vu_clip[VU_SAF_FRAME_COUNT_AT]
             | ((int) vu_clip[VU_SAF_FRAME_COUNT_AT + 1] << 8),
             VU_CLIP_FRAMES);
    vu_unstage();
}

/* One unit, the view at the map origin.  Every byte of the presented window is
   the byte the same clip drawn at tile * 24 - 24 across and tile * 24 - 30 down
   puts on a page of its own, and the reference is not blank.  Dropping either
   corner offset, or scaling a tile by anything but 24, moves every drawn byte.
   Nothing outside the 312x192 window at screen (4,4) is touched. */
static void vfx_animation_draws_the_clip_over_the_unit_tile(void)
{
    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    vu_set_unit(VU_FIRST_ID, VU_FIRST_TILE_X, VU_FIRST_TILE_Y);
    vu_ids[0] = VU_FIRST_ID;
    vu_run(1);
    vu_render_reference(1);

    CHECK_EQ(vu_reference_painted() > 0, 1);
    CHECK_EQ(vu_window_differences(), 0);
    CHECK_EQ(vu_border_touched(), 0);
    vu_unstage();
}

/* Two entries in the list, two records, and a view scrolled off the map origin
   by (10,7).  One copy per ENTRY and not one per run: the second entry's id is
   0 while its position in the list is 1, so a run that used the loop counter as
   the unit id draws the wrong record's tile.  The scroll is subtracted from
   both axes, which the same comparison covers because the reference subtracts
   it too and a run that did not moves everything by (10,7).  The window holds
   strictly more painted bytes than the one-unit run's reference did, so the
   second copy really was drawn. */
static void vfx_animation_draws_one_copy_per_listed_unit(void)
{
    int one_unit_painted;

    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    vu_set_unit(VU_FIRST_ID, VU_FIRST_TILE_X, VU_FIRST_TILE_Y);
    vu_set_unit(VU_SECOND_ID, VU_SECOND_TILE_X, VU_SECOND_TILE_Y);
    vu_ids[0] = VU_FIRST_ID;
    vu_ids[1] = VU_SECOND_ID;
    data_fdps_battle_view_window_origin_x = VU_SCROLL_X;
    data_fdps_battle_view_window_origin_y = VU_SCROLL_Y;

    vu_render_reference(1);
    one_unit_painted = vu_reference_painted();

    vu_run(2);
    vu_render_reference(2);

    CHECK_EQ(vu_window_differences(), 0);
    CHECK_EQ(vu_screen_painted() > one_unit_painted, 1);
    CHECK_EQ(vu_border_touched(), 0);
    vu_unstage();
}

/* MOV EAX,[EBP-0x10] / CMP EAX,[EBP+0x14] / JL at 00026eab: a count of 0 never
   enters the per-unit loop, so nothing is drawn at all -- and since the page is
   the seed and the scene repaint writes nothing under this staging, the whole
   window comes back as zero.  That is the assumption every case above rests on,
   stated as an assertion: a scene repaint that painted anything would show
   here.  The blit still runs, so the window is written and the border is
   not. */
static void vfx_animation_no_units_draws_nothing(void)
{
    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    vu_set_unit(VU_FIRST_ID, VU_FIRST_TILE_X, VU_FIRST_TILE_Y);
    vu_ids[0] = VU_FIRST_ID;
    vu_run(0);

    CHECK_EQ(vu_screen_painted(), 0);
    CHECK_EQ(vu_border_touched(), 0);
    vu_unstage();
}

/* Every one of the ten frames is held for two ticks -- the inner counter runs
   to 2 at 00026e7f and the wait at 00026f8d sits inside it -- so a run costs at
   least nineteen tick changes, the twentieth being the first tick's, whose
   latch is uninitialised and may end at once.  A run that held each frame for
   one tick floors at nine and does not reach it, and a run that waited nowhere
   costs none.  No ceiling is asserted: the counter can advance more than once
   while a tick is composed. */
static void vfx_animation_holds_every_frame_for_two_ticks(void)
{
    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    vu_set_unit(VU_FIRST_ID, VU_FIRST_TILE_X, VU_FIRST_TILE_Y);
    vu_ids[0] = VU_FIRST_ID;
    vu_run(1);

    CHECK_EQ(vu_ticks_used >= (unsigned int)
             (VU_TICKS_PER_FRAME * VU_CLIP_FRAMES - 1), 1);
    vu_unstage();
}

/* CALL free at 00026fae and again at 00026fba: the page goes back and so does
   the loaded member, so the heap holds no more used blocks after the run than
   before it.  This is also what the seeding depends on -- if the page did not
   come back, the next case's expected values would be comparing against
   rubbish. */
static void vfx_animation_frees_the_page_and_the_clip(void)
{
    if (!vu_stage()) {
        vu_unstage();
        return;
    }
    vu_set_unit(VU_FIRST_ID, VU_FIRST_TILE_X, VU_FIRST_TILE_Y);
    vu_ids[0] = VU_FIRST_ID;
    vu_run(1);

    CHECK_EQ(vu_blocks_after, vu_blocks_before);
    vu_unstage();
}

void run_anim_tests(void)
{
    RUN_TEST(baseani_lookup_returns_a_pointer_into_the_image);
    RUN_TEST(baseani_lookup_republishes_on_every_call);
    RUN_TEST(baseani_lookup_reads_the_archive_base_each_call);
    RUN_TEST(baseani_lookup_folds_the_query_in_place);
    RUN_TEST(turn_number_draws_one_pass_per_digit);
    RUN_TEST(turn_number_biases_digits_one_past_the_word_sprite);
    RUN_TEST(turn_number_reads_the_counter_at_the_call);
    RUN_TEST(turn_number_leaves_the_other_seven_slots_alone);
    RUN_TEST(banner_the_aperture_reads_back_in_mode_13h);
    RUN_TEST(banner_ends_with_the_signs_last_step_and_nothing_else);
    RUN_TEST(banner_places_both_pieces_from_one_table_entry);
    RUN_TEST(banner_repaints_the_background_under_every_step);
    RUN_TEST(vfs_animation_ends_on_level_one_of_the_untouched_screen);
    RUN_TEST(vfs_animation_plays_the_clip_over_the_dimmed_screen);
    RUN_TEST(vfs_animation_banners_only_the_player_phase_clip);
    RUN_TEST(attack_animation_reads_the_measured_offsets);
    RUN_TEST(attack_animation_draws_one_frame_per_pass_over_the_defender);
    RUN_TEST(attack_animation_subtracts_the_view_scroll_origin);
    RUN_TEST(attack_animation_draws_the_defender_bar_from_side_zero);
    RUN_TEST(attack_animation_side_other_than_zero_takes_graphic_one);
    RUN_TEST(attack_animation_draws_both_bars_on_a_counter);
    RUN_TEST(attack_animation_suppresses_the_attacker_bar_without_a_counter);
    RUN_TEST(attack_animation_fill_is_the_ceiling_over_41_columns);
    RUN_TEST(attack_animation_zero_max_hp_draws_an_empty_bar);
    RUN_TEST(attack_animation_hp_words_are_read_signed);
    RUN_TEST(attack_animation_presents_312x192_at_screen_4_4);
    RUN_TEST(attack_animation_frees_the_page);
    RUN_TEST(attack_animation_paces_the_frames_with_the_tick);
    RUN_TEST(vfx_animation_reads_the_measured_offsets);
    RUN_TEST(vfx_animation_draws_the_clip_over_the_unit_tile);
    RUN_TEST(vfx_animation_draws_one_copy_per_listed_unit);
    RUN_TEST(vfx_animation_no_units_draws_nothing);
    RUN_TEST(vfx_animation_holds_every_frame_for_two_ticks);
    RUN_TEST(vfx_animation_frees_the_page_and_the_clip);
}
