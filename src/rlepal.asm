; rlepal.asm -- the palette kernels: remap sprite and backdrop (mode 1),
; remap the sprite only (mode 2) and recolor (mode 3).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rlecolor.c under #if 0; this file is what is linked.  Entered only from
; fdps_blit_dispatch (rledisp.asm); the mode operand is read out of the
; dispatcher's EBP frame.

        .386p

        extrn   data_fdps_graphics_rle_blit_remaining_rows:word
        extrn   data_fdps_graphics_rle_blit_src_width:word

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  fdps_rle_blit_remap_sprite_and_backdrop
        public  fdps_rle_blit_with_palette_remap
        public  fdps_rle_blit_recolor

; 00056a8d  fdps_rle_blit_remap_sprite_and_backdrop (blit mode 1)
;
; Decodes an RLE sprite stream and writes every pixel of the width x height
; rectangle as palette_remap[pixel]: fill (op 00), stretched (op 01) and
; literal (op 10) runs remap the stream's bytes, and op 11 -- the transparent
; run in the plain blitter -- reads the pixel already on the surface and
; writes its remapped value back, so the backdrop is remapped too.
;
; Entry (set up by fdps_blit_dispatch just before its CALL):
;   ESI = RLE command stream
;   EDI = destination pixel of the top row
;   EDX = row advance added at the end of each row (pitch - width)
;   EBP = fdps_blit_dispatch's frame pointer
;   ECX = 0 and EAX = 0 (the dispatcher's XORs; only CL and AL are written
;         here, but CX/ECX and EAX are read whole)
;   data_fdps_graphics_rle_blit_src_width      = row width in pixels
;   data_fdps_graphics_rle_blit_remaining_rows = row count (decremented to 0)
; Frame slots read: [EBP+0x1c], the dispatcher's sixth argument, which is the
;   256-byte palette_remap table; nothing in the frame is written.
; Exit: ESI past the consumed stream, EDI past the last row, remaining_rows = 0.
;   Destroys EAX, EBX, ECX, ESI, EDI and EBP (EBP becomes the table base; the
;   dispatcher recovers because its epilogue pops EBP off ESP). EDX preserved.
;
; See src/rlecolor.c for the long explanation (op encoding, the exact-zero row
; terminator, the one-pixel hole op 01 leaves, the do-while row count).
fdps_rle_blit_remap_sprite_and_backdrop proc near
        mov     ebp, dword ptr [ebp+1ch]        ; EBP = palette_remap table
rmsb_next_row:
        mov     bx, word ptr [data_fdps_graphics_rle_blit_src_width]
rmsb_next_command:
        lodsb
        mov     cl, al
        shl     cl, 1                           ; CF = bit 7 of the command
        jb      short rmsb_op_1x
        nop
        nop
        nop
        nop
        shl     cl, 1                           ; CF = bit 6 of the command
        jb      short rmsb_op_01_stretched
        nop
        nop
        nop
        nop
        ; op 00: fill -- one stream byte, remapped, repeated CL+1 times
        shr     cl, 2
        inc     cl
        sub     bx, cx
        lodsb
        mov     al, byte ptr [eax+ebp*1]
        rep     stosb
        or      bx, bx
        jne     rmsb_next_command
        jmp     short rmsb_end_of_row
        nop
        nop
        nop
rmsb_op_01_stretched:
        ; op 01: remapped byte into the second half of each of CL+1 pairs
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        lodsb
        mov     al, byte ptr [eax+ebp*1]
rmsb_stretch_pair:
        inc     edi                             ; first byte of the pair untouched
        stosb
        loop    rmsb_stretch_pair
        or      bx, bx
        jne     rmsb_next_command
        jmp     short rmsb_end_of_row
        nop
        nop
        nop
rmsb_op_1x:
        shl     cl, 1                           ; CF = bit 6 of the command
        jb      short rmsb_op_11_backdrop
        nop
        nop
        nop
        nop
        ; op 10: literal -- CL+1 stream bytes, each remapped
        shr     cl, 2
        inc     cl
        sub     bx, cx
rmsb_literal_pixel:
        lodsb
        mov     al, byte ptr [eax+ebp*1]
        stosb
        loop    rmsb_literal_pixel
        or      bx, bx
        jne     rmsb_next_command
        jmp     short rmsb_end_of_row
        nop
        nop
        nop
rmsb_op_11_backdrop:
        ; op 11: no stream byte -- remap CL+1 destination pixels in place
        shr     cl, 2
        inc     cl
        sub     bx, cx
rmsb_backdrop_pixel:
        mov     al, byte ptr [edi]
        mov     al, byte ptr [eax+ebp*1]
        stosb
        loop    rmsb_backdrop_pixel
        or      bx, bx
        jne     rmsb_next_command
rmsb_end_of_row:
        add     edi, edx
        dec     word ptr [data_fdps_graphics_rle_blit_remaining_rows]
        jne     rmsb_next_row
        ret
fdps_rle_blit_remap_sprite_and_backdrop endp

; ---------------------------------------------------------------------------
; 00056b25  fdps_rle_blit_with_palette_remap -- blit mode 2
;
; Decodes the RLE command stream onto the destination surface, passing every
; pixel byte the sprite writes through a 256-byte palette remap table.  Op 00
; is a fill run, op 01 a stretched run (the second byte of each destination
; pair only), op 10 a literal run, and op 11 a transparent skip that leaves
; the backdrop exactly as it was.  Each command byte's low six bits plus one
; give the run length (1..64).
;
; Called only from fdps_blit_dispatch, with no prologue and nothing on the
; stack.  On entry:
;   EBP  the dispatcher's frame pointer; [EBP+0x1c] (its sixth argument slot)
;        is read once as the remap table base, then EBP holds that base for
;        the rest of the call
;   ESI  the RLE command stream
;   EDI  the first destination pixel of the top row
;   EDX  the row advance (pitch - width)
;   EAX, ECX  zero (the dispatcher's XOR EAX,EAX / XOR ECX,ECX); only AL and
;        CL are ever written, so EAX indexes the table as 0..255 and ECX is
;        the run length for REP STOSB / LOOP
; Globals: data_fdps_graphics_rle_blit_src_width is re-read at the top of each
; row; data_fdps_graphics_rle_blit_remaining_rows is decremented in memory
; until zero (zero rows on entry means 0x10000 rows).
; Destroys EAX, EBX, ECX, ESI, EDI and EBP; EDX is kept.  The dispatcher
; restores EBX, ESI, EDI and EBP from its own stack.  Writes no frame slot.
;
; The long explanation, and why the exact-zero row test and the untouched
; first byte of each op 01 pair must stay, is in the C translation in
; src/rlecolor.c.
; ---------------------------------------------------------------------------
fdps_rle_blit_with_palette_remap proc near
        mov     ebp, dword ptr [ebp+1ch]        ; remap table from the dispatcher's 6th argument
rmpal_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
rmpal_next_cmd:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      short rmpal_op_1x
        nop
        nop
        nop
        nop
        shl     cl, 1
        jb      short rmpal_op_stretch
        nop
        nop
        nop
        nop
        ; op 00: fill with one remapped pixel
        shr     cl, 2
        inc     cl
        sub     bx, cx
        lodsb
        mov     al, byte ptr [eax+ebp]
        rep     stosb
        or      bx, bx
        jne     rmpal_next_cmd
        jmp     short rmpal_row_end
        nop
        nop
        nop
rmpal_op_stretch:
        ; op 01: remapped pixel into the second byte of each destination pair
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        lodsb
        mov     al, byte ptr [eax+ebp]
rmpal_stretch_loop:
        inc     edi
        stosb
        loop    rmpal_stretch_loop
        or      bx, bx
        jne     rmpal_next_cmd
        jmp     short rmpal_row_end
        nop
        nop
        nop
rmpal_op_1x:
        shl     cl, 1
        jb      short rmpal_op_skip
        nop
        nop
        nop
        nop
        ; op 10: literal run, each stream byte remapped
        shr     cl, 2
        inc     cl
        sub     bx, cx
rmpal_literal_loop:
        lodsb
        mov     al, byte ptr [eax+ebp]
        stosb
        loop    rmpal_literal_loop
        or      bx, bx
        jne     rmpal_next_cmd
        jmp     short rmpal_row_end
        nop
        nop
        nop
rmpal_op_skip:
        ; op 11: transparent skip, destination untouched
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx
        jne     rmpal_next_cmd
rmpal_row_end:
        add     edi, edx
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     rmpal_next_row
        ret
fdps_rle_blit_with_palette_remap endp

; 00056bb7.  Blit mode 3: decode the RLE command stream and write every sprite
; pixel as ((pixel + tint_offset) & band_mask) + color_base, each step wrapping
; in eight bits.  There is no lookup table; op 11 is a plain transparent skip
; that leaves the backdrop untouched.
;
; Entry (only from fdps_blit_dispatch, which CALLs it with its own frame live):
;   ESI  RLE command stream
;   EDI  first destination pixel of the top row
;   EDX  row advance (pitch - width), added to EDI at the end of every row
;   ECX  zero (the dispatcher's XOR ECX,ECX): only CL is loaded below, while
;        REP STOSB, LOOP, SUB BX,CX and ADD EDI,ECX use CX/ECX whole
;   EBP  fdps_blit_dispatch's frame.  Reads the three low bytes of its sixth
;        argument slot: [EBP+0x1c] tint_offset -> DH, [EBP+0x1d] color_base
;        -> DL, [EBP+0x1e] band_mask -> AH.  Nothing in the frame is written.
; Globals: reads data_fdps_graphics_rle_blit_src_width at the top of every
;   row; decrements data_fdps_graphics_rle_blit_remaining_rows to zero.
; Exit: EAX, EBX, ECX, EDX, ESI, EDI destroyed; EBP holds the row advance, not
;   the dispatcher's frame -- the dispatcher's epilogue pops its own EBP from
;   ESP, so it does not care.
; C translation and the long explanation: src/rlecolor.c (#if 0 reference).
fdps_rle_blit_recolor proc near
        push    edx
        mov     dh, byte ptr [ebp+1ch]          ; tint_offset
        mov     dl, byte ptr [ebp+1dh]          ; color_base
        mov     ah, byte ptr [ebp+1eh]          ; band_mask
        pop     ebp                             ; EBP = row advance from here on
rcol_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
rcol_next_command:
        lodsb
        mov     cl, al
        shl     cl, 1                           ; bit 7 -> CF
        jb      short rcol_op_1x
        nop
        nop
        nop
        nop
        shl     cl, 1                           ; bit 6 -> CF
        jb      short rcol_op_01_stretched
        nop
        nop
        nop
        nop
        ; op 00: fill run_length bytes with one recoloured pixel
        shr     cl, 2                           ; CL = run_length - 1
        inc     cl
        sub     bx, cx
        lodsb
        add     al, dh
        and     al, ah
        add     al, dl
        rep stosb
        or      bx, bx
        jne     rcol_next_command
        jmp     short rcol_row_done
        nop
        nop
        nop
rcol_op_01_stretched:
        ; op 01: one recoloured pixel into the second byte of each pair;
        ; the width counter drops by twice the run length
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        lodsb
        add     al, dh
        and     al, ah
        add     al, dl
rcol_op_01_pair:
        inc     edi
        stosb
        loop    rcol_op_01_pair
        or      bx, bx
        jne     rcol_next_command
        jmp     short rcol_row_done
        nop
        nop
        nop
rcol_op_1x:
        shl     cl, 1                           ; bit 6 -> CF
        jb      short rcol_op_11_skip
        nop
        nop
        nop
        nop
        ; op 10: run_length literal pixels, each recoloured
        shr     cl, 2
        inc     cl
        sub     bx, cx
rcol_op_10_pixel:
        lodsb
        add     al, dh
        and     al, ah
        add     al, dl
        stosb
        loop    rcol_op_10_pixel
        or      bx, bx
        jne     rcol_next_command
        jmp     short rcol_row_done
        nop
        nop
        nop
rcol_op_11_skip:
        ; op 11: transparent run, destination stepped over unwritten
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx
        jne     rcol_next_command
rcol_row_done:
        add     edi, ebp
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     rcol_next_row
        ret
fdps_rle_blit_recolor endp

_TEXT   ends

        end
