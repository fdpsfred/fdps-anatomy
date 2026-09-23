; rlebase.asm -- the plain RLE kernels: pass-through (mode 0), scaled (mode
; 4), the row skipper the scaling kernels share, and the two mirrors (modes 7
; and 8).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in src/rle.c
; under #if 0; this file is what is linked.  The kernels are entered only from
; fdps_blit_dispatch (rledisp.asm), with ESI the stream, EDI the destination
; and EDX the row advance, and they read the rest out of the dispatcher's EBP
; frame and the rectangle globals gamedata.c defines.

        .386p

        extrn   data_fdps_graphics_rle_blit_dest_height:word
        extrn   data_fdps_graphics_rle_blit_dest_rows_remaining:word
        extrn   data_fdps_graphics_rle_blit_dest_width:word
        extrn   data_fdps_graphics_rle_blit_dst_pitch:word
        extrn   data_fdps_graphics_rle_blit_dst_row_advance:dword
        extrn   data_fdps_graphics_rle_blit_remaining_rows:word
        extrn   data_fdps_graphics_rle_blit_src_width:word
        extrn   data_fdps_graphics_rle_blit_vscale_accumulator:word

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  fdps_rle_blit_passthrough
        public  fdps_rle_blit_scaled
        public  fdps_rle_skip_row
        public  fdps_rle_blit_mirrored_horizontal
        public  fdps_rle_blit_mirrored_vertical

; 00056a0d  fdps_rle_blit_passthrough -- blit mode 0, the plain sprite blit.
;
; Decodes the RLE command stream into an 8bpp destination row by row, writing
; every pixel unchanged. Each command byte selects one of four ops by its top
; two bits (00 fill, 01 stretched fill into every second byte, 10 literal copy,
; 11 transparent skip); the low six bits plus one are the run length. A row
; ends when the 16-bit width counter reaches exactly zero; the routine ends
; when the row counter in memory decrements to zero.
;
; Entry:  ESI = RLE stream, EDI = first destination pixel of the top row,
;         EDX = destination advance added at the end of each row (signed;
;         pitch - width, negated by mode 8 to draw bottom-up),
;         ECX bits 8..31 = 0 (the caller clears ECX; only CL is ever written
;         here, but SUB BX,CX, REP and LOOP read CX/ECX), ES = flat data.
; Exit:   ESI past the consumed stream, EDI past the last row.
;         Destroys EAX (AL), EBX (BX), ECX, ESI, EDI, flags. EDX, EBP kept.
; Frame:  reads and writes no slot of fdps_blit_dispatch's EBP frame; uses the
;         globals data_fdps_graphics_rle_blit_src_width (read every row) and
;         data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rle.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_passthrough proc near
pass_next_row:
        mov     bx, data_fdps_graphics_rle_blit_src_width
pass_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      short pass_op_literal_or_skip   ; top bit set: op 10 or 11
        nop
        nop
        nop
        nop
        shl     cl, 1
        jb      short pass_op_stretched         ; op 01
        nop
        nop
        nop
        nop
        ; op 00: fill the run with one pixel byte
        shr     cl, 2
        inc     cl
        sub     bx, cx
        lodsb
        rep     stosb
        or      bx, bx
        jne     pass_next_op
        jmp     short pass_end_row
        nop
        nop
        nop
pass_op_stretched:
        ; op 01: the pixel goes into the second byte of each destination pair,
        ; the first byte is left untouched; the width drops by 2 * length
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        lodsb
pass_stretch_loop:
        inc     edi
        stosb
        loop    pass_stretch_loop
        or      bx, bx
        jne     pass_next_op
        jmp     short pass_end_row
        nop
        nop
        nop
pass_op_literal_or_skip:
        shl     cl, 1
        jb      short pass_op_skip              ; op 11
        nop
        nop
        nop
        nop
        ; op 10: copy the run literally from the stream
        shr     cl, 2
        inc     cl
        sub     bx, cx
        rep     movsb
        or      bx, bx
        jne     pass_next_op
        jmp     short pass_end_row
        nop
        nop
        nop
pass_op_skip:
        ; op 11: transparent run, step the destination over it
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     pass_next_op
pass_end_row:
        add     edi, edx
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     pass_next_row
        ret
fdps_rle_blit_passthrough endp

; fdps_rle_blit_scaled -- original at 00056c5e, blit mode 4.
;
; Draws one RLE sprite scaled to a destination width x height with two
; Bresenham accumulators: horizontally in BP inside each row, vertically in
; the vscale_accumulator global between rows. Each destination row re-decodes
; its source row from the start (ESI pushed and popped around the row), and
; fdps_rle_skip_row is called once for every source row the vertical
; accumulator steps past.
;
; Entry (reached only from fdps_blit_dispatch, inside its frame):
;   ESI = RLE stream, EDI = destination pixel, ES = flat data selector,
;   ECX upper bits zero (the dispatcher clears ECX; CH must be zero for the
;   SHL CX,1 of the half-tone op).
;   EBP = fdps_blit_dispatch's frame pointer. Reads [EBP+1Ch] (destination
;   width) and [EBP+1Eh] (destination height): the low and high words of the
;   dispatcher's sixth argument. Writes no frame slot.
; Exit:
;   ESI = stream after the last source row consumed, EDI = past the last row.
;   Destroys EAX (AL, AH), BX, CX, DX, flags, and EBP: EBP is zeroed and used
;   as the horizontal and vertical accumulator and is NOT restored; the
;   dispatcher's epilogue recovers EBP from the stack.
; Globals written: dest_width, dest_height, dest_rows_remaining,
;   vscale_accumulator, dst_row_advance (16-bit pitch - width, zero-extended).
; Globals read: dst_pitch, src_width, remaining_rows (source rows).
;
; C translation with the long explanation: src/rle.c (under #if 0).
fdps_rle_blit_scaled proc near
        mov     dx, word ptr [ebp+1Ch]
        mov     word ptr data_fdps_graphics_rle_blit_dest_width, dx
        mov     dx, word ptr [ebp+1Eh]
        mov     word ptr data_fdps_graphics_rle_blit_dest_height, dx
        xor     ebp, ebp                ; EBP top half stays zero from here on
        mov     bp, word ptr data_fdps_graphics_rle_blit_dest_height
        mov     word ptr data_fdps_graphics_rle_blit_dest_rows_remaining, bp
        mov     word ptr data_fdps_graphics_rle_blit_vscale_accumulator, bp
        mov     bp, word ptr data_fdps_graphics_rle_blit_dst_pitch
        sub     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        mov     dword ptr data_fdps_graphics_rle_blit_dst_row_advance, ebp ; zero-extended 16-bit advance
        mov     dx, word ptr data_fdps_graphics_rle_blit_src_width
scal_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_dest_width
        mov     bp, bx                  ; horizontal accumulator starts at dest width
        push    esi                     ; row start, popped at scal_row_done
scal_next_command:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      short scal_op_1x
        nop
        nop
        nop
        nop
        shl     cl, 1
        jb      short scal_op_halftone
        nop
        nop
        nop
        nop
; op 00: fill -- one pixel byte follows
        shr     cl, 2
        inc     cl
        lodsb
scal_fill_loop:
        cmp     bp, dx
        jae     short scal_fill_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        dec     cl
        jne     scal_fill_loop
        jmp     scal_next_command
scal_fill_emit:
        sub     bp, dx
        stosb
        dec     bx
        jne     scal_fill_loop
        jmp     scal_row_done
; op 01: half-tone -- run doubled, AH is the phase
scal_op_halftone:
        shr     cl, 2
        inc     cl
        shl     cx, 1
        lodsb
        xor     ah, ah
scal_halftone_loop:
        cmp     bp, dx
        jae     short scal_halftone_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        xor     ah, 1
        dec     cl
        jne     scal_halftone_loop
        jmp     scal_next_command
scal_halftone_emit:
        sub     bp, dx
        or      ah, ah
        je      short scal_halftone_step
        nop
        nop
        nop
        nop
        mov     byte ptr [edi], al
scal_halftone_step:
        inc     edi
        dec     bx
        jne     scal_halftone_loop
        jmp     short scal_row_done
        nop
        nop
        nop
; ops 1x: literal (10) or skip (11)
scal_op_1x:
        shl     cl, 1
        jb      short scal_op_skip
        nop
        nop
        nop
        nop
; op 10: literal -- ESI steps only when a source pixel is consumed
        shr     cl, 2
        inc     cl
scal_literal_loop:
        cmp     bp, dx
        jae     short scal_literal_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        inc     esi
        dec     cl
        jne     scal_literal_loop
        jmp     scal_next_command
scal_literal_emit:
        sub     bp, dx
        mov     al, byte ptr [esi]
        stosb
        dec     bx
        jne     scal_literal_loop
        jmp     short scal_row_done
        nop
        nop
        nop
; op 11: skip -- transparent run
scal_op_skip:
        shr     cl, 2
        inc     cl
scal_skip_loop:
        cmp     bp, dx
        jae     short scal_skip_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        dec     cl
        jne     scal_skip_loop
        jmp     scal_next_command
scal_skip_emit:
        sub     bp, dx
        inc     edi
        dec     bx
        jne     scal_skip_loop
; vertical step: walk every source row the accumulator passes over
scal_row_done:
        pop     esi
        mov     bp, word ptr data_fdps_graphics_rle_blit_vscale_accumulator
scal_vstep_loop:
        cmp     bp, word ptr data_fdps_graphics_rle_blit_remaining_rows
        ja      short scal_vstep_done
        nop
        nop
        nop
        nop
        call    fdps_rle_skip_row       ; destroys BX, CX, AL; advances ESI
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_height
        jmp     scal_vstep_loop
scal_vstep_done:
        sub     bp, word ptr data_fdps_graphics_rle_blit_remaining_rows
        mov     word ptr data_fdps_graphics_rle_blit_vscale_accumulator, bp
        add     edi, dword ptr data_fdps_graphics_rle_blit_dst_row_advance
        dec     word ptr data_fdps_graphics_rle_blit_dest_rows_remaining
        jne     scal_next_row
        ret
fdps_rle_blit_scaled endp

; 00056dc9  fdps_rle_skip_row -- walk the RLE stream past one encoded row.
;
; Decodes the command bytes of one source row for their byte cost alone and
; never reads a pixel byte, so the scaled blitters can drop a source row the
; vertical Bresenham step says not to draw. Per op (top two bits of the
; command byte, length = low six bits + 1): 00 fill steps 2 bytes, width -=
; len; 01 stretched steps 2 bytes, width -= 2 * len; 10 literal steps 1 + len
; bytes, width -= len; 11 skip steps 1 byte, width -= len. The width is read
; once on entry and tested for exactly zero after each op (bottom test).
;
; Entry:  ESI = first command byte of the row to drop,
;         ECX bits 8..31 = 0 (only CL is written here, but SUB BX,CX and
;         ADD ESI,ECX read CX/ECX; fdps_blit_dispatch clears ECX and both
;         callers write only CL, keeping ECX below 0x100).
; Exit:   ESI = first command byte of the next row.
;         Destroys AL, BX, CL, flags. EDX, EDI, EBP and the rest of EAX,
;         EBX, ECX are kept.
; Frame:  reads and writes no slot of fdps_blit_dispatch's EBP frame; reads
;         the global data_fdps_graphics_rle_blit_src_width once.
; Called by fdps_rle_blit_scaled and fdps_rle_blit_rotated_scaled.
; See src/rle.c (the #if 0 C translation) for the long explanation.
fdps_rle_skip_row proc near
        mov     bx, data_fdps_graphics_rle_blit_src_width
skip_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      short skip_op_literal_or_skip   ; top bit set: op 10 or 11
        nop
        nop
        nop
        nop
        shl     cl, 1
        jb      short skip_op_stretched         ; op 01
        nop
        nop
        nop
        nop
        ; op 00 fill: step over the one pixel byte
        shr     cl, 2
        inc     cl
        sub     bx, cx
        inc     esi
        or      bx, bx
        jne     skip_next_op
        ret
skip_op_stretched:
        ; op 01 stretched: one pixel byte, two columns per unit of length
        shr     cl, 2
        inc     cl
        inc     esi
        sub     bx, cx
        sub     bx, cx
        or      bx, bx
        jne     skip_next_op
        ret
skip_op_literal_or_skip:
        shl     cl, 1
        jb      short skip_op_skip              ; op 11
        nop
        nop
        nop
        nop
        ; op 10 literal: step over the run's pixel bytes
        shr     cl, 2
        inc     cl
        sub     bx, cx
        add     esi, ecx
        or      bx, bx
        jne     skip_next_op
        ret
skip_op_skip:
        ; op 11 skip: nothing follows the command byte
        shr     cl, 2
        inc     cl
        sub     bx, cx
        or      bx, bx
        jne     skip_next_op
        ret
fdps_rle_skip_row endp

; 00057551  fdps_rle_blit_mirrored_horizontal -- blit mode 7, left-right mirror.
;
; Decodes the same four RLE ops as fdps_rle_blit_passthrough (00 fill, 01
; stretched fill into every second byte, 10 literal copy, 11 transparent skip;
; low six bits plus one = run length), but writes each row starting at its
; right-hand column and walking left, so the drawn rectangle is mode 0's output
; reflected inside the same box. Each row restarts from the saved row origin and
; steps it by the full destination pitch; the row counter in memory is
; decremented once per row until it reaches zero.
;
; Entry:  ESI = RLE stream, EDI = top-left pixel of the destination rectangle
;         (the same pointer mode 0 gets; NOT the right-hand end),
;         ECX bits 8..31 = 0 (the caller clears ECX; only CL is written here,
;         but SUB BX,CX, REP, LOOP and SUB EDI,ECX read CX/ECX), ES = flat data,
;         direction flag clear. The EDX advance the dispatcher leaves is ignored.
; Exit:   ESI past the consumed stream, EDI = origin of the row after the last.
;         Destroys EAX (AL), EBX, ECX, EDX (zero-extended pitch), ESI, EDI,
;         flags; DF is left clear. EBP kept; the stack is balanced (one PUSH /
;         POP EDI per row).
; Frame:  reads and writes no slot of fdps_blit_dispatch's EBP frame; uses the
;         globals data_fdps_graphics_rle_blit_dst_pitch (read once),
;         data_fdps_graphics_rle_blit_src_width (read every row) and
;         data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rle.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_mirrored_horizontal proc near
        xor     ebx, ebx
        xor     edx, edx
        mov     dx, word ptr data_fdps_graphics_rle_blit_dst_pitch  ; full pitch, zero-extended
mirh_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
        push    edi                             ; save the row origin
        add     edi, ebx                        ; right-hand column of the row
        dec     edi
mirh_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      short mirh_op_literal_or_skip   ; top bit set: op 10 or 11
        nop
        nop
        nop
        nop
        shl     cl, 1
        jb      short mirh_op_stretched         ; op 01
        nop
        nop
        nop
        nop
        ; op 00: fill the run with one pixel byte, walking left
        shr     cl, 2
        inc     cl
        sub     bx, cx
        lodsb
        std
        rep     stosb
        cld
        or      bx, bx
        jne     mirh_next_op
        jmp     short mirh_end_row
        nop
        nop
        nop
mirh_op_stretched:
        ; op 01: step left before and after each store, so the painted columns
        ; are the mirror images of mode 0's second-of-pair columns
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        lodsb
mirh_stretch_loop:
        dec     edi
        mov     byte ptr [edi], al
        dec     edi
        loop    mirh_stretch_loop
        or      bx, bx
        jne     mirh_next_op
        jmp     short mirh_end_row
        nop
        nop
        nop
mirh_op_literal_or_skip:
        shl     cl, 1
        jb      short mirh_op_skip              ; op 11
        nop
        nop
        nop
        nop
        ; op 10: copy the run from the stream, walking left
        shr     cl, 2
        inc     cl
        sub     bx, cx
mirh_literal_loop:
        lodsb
        mov     byte ptr [edi], al
        dec     edi
        loop    mirh_literal_loop
        or      bx, bx
        jne     mirh_next_op
        jmp     short mirh_end_row
        nop
        nop
        nop
mirh_op_skip:
        ; op 11: transparent run, step the destination left over it
        shr     cl, 2
        inc     cl
        sub     edi, ecx
        sub     bx, cx
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     mirh_next_op
mirh_end_row:
        pop     edi                             ; back to the row origin
        add     edi, edx                        ; full pitch to the next row
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     mirh_next_row
        ret
fdps_rle_blit_mirrored_horizontal endp

; 000575ed  fdps_rle_blit_mirrored_vertical -- blit mode 8, top-bottom mirror.
;
; Draws nothing itself. It moves the destination cursor down to the bottom row
; of the rectangle (pitch * (rows - 1)) and replaces the row advance with
; -(pitch + width), then tail-jumps into fdps_rle_blit_passthrough, which
; decodes the stream forwards while climbing the rectangle, so the sprite comes
; out reflected top to bottom inside the same box. No RET of its own: the
; pass-through kernel's RET returns to fdps_blit_dispatch.
;
; Entry:  ESI = RLE stream, EDI = top-left pixel of the destination rectangle
;         (the same pointer mode 0 gets; NOT the bottom row),
;         ECX bits 8..31 = 0 and ES = flat data, both only passed through to
;         the pass-through kernel. The EDX advance the dispatcher leaves is
;         discarded and rebuilt here.
; Exit:   (via fdps_rle_blit_passthrough) ESI past the consumed stream, EDI
;         past the last (topmost) row drawn. Destroys EAX, EDX (the 32-bit
;         upward advance -(pitch + width), kept by the kernel), EBX, ECX, ESI,
;         EDI, flags. EBP kept; no stack use.
; Frame:  reads and writes no slot of fdps_blit_dispatch's EBP frame; reads the
;         globals data_fdps_graphics_rle_blit_dst_pitch,
;         data_fdps_graphics_rle_blit_remaining_rows (only read here; DEC DX
;         steps the register copy) and data_fdps_graphics_rle_blit_src_width.
; See src/rle.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_mirrored_vertical proc near
        xor     eax, eax
        xor     edx, edx
        mov     ax, word ptr data_fdps_graphics_rle_blit_dst_pitch
        mov     dx, word ptr data_fdps_graphics_rle_blit_remaining_rows
        dec     dx                              ; rows - 1, 16-bit (0 wraps to 65535)
        mul     edx                             ; EAX = pitch * (rows - 1); EDX = 0
        add     edi, eax                        ; bottom row of the rectangle
        xor     edx, edx
        mov     dx, word ptr data_fdps_graphics_rle_blit_dst_pitch
        add     dx, word ptr data_fdps_graphics_rle_blit_src_width  ; 16-bit sum, wraps
        neg     edx                             ; advance = -(pitch + width)
        jmp     fdps_rle_blit_passthrough       ; tail jump into mode 0
fdps_rle_blit_mirrored_vertical endp

_TEXT   ends

        end
