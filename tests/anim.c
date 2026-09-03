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
#include <stdio.h>
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
}
