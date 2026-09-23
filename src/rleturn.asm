; rleturn.asm -- the rotating kernels: rotated (mode 5) and rotated and
; scaled (mode 6).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rlerot.c under #if 0, and the rotation globals are still defined there;
; this file is what is linked.  Entered only from fdps_blit_dispatch
; (rledisp.asm); the geometry is read out of the dispatcher's EBP frame.

        .386p

        extrn   data_fdps_graphics_rle_blit_dest_height:word
        extrn   data_fdps_graphics_rle_blit_dest_rows_remaining:word
        extrn   data_fdps_graphics_rle_blit_dest_width:word
        extrn   data_fdps_graphics_rle_blit_dst_pitch:word
        extrn   data_fdps_graphics_rle_blit_remaining_rows:word
        extrn   data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator:word
        extrn   data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator:word
        extrn   data_fdps_graphics_rle_blit_rot_row_dest_step_x:dword
        extrn   data_fdps_graphics_rle_blit_rot_row_step_x_accumulator:word
        extrn   data_fdps_graphics_rle_blit_rot_row_step_y_accumulator:word
        extrn   data_fdps_graphics_rle_blit_rotated_src_pixel_step_y:dword
        extrn   data_fdps_graphics_rle_blit_src_width:word
        extrn   data_fdps_graphics_rle_blit_vscale_accumulator:word
        extrn   data_fdps_graphics_rle_rotate_cos_magnitude:word
        extrn   data_fdps_graphics_rle_rotate_dst_x_step_per_src_x:dword
        extrn   data_fdps_graphics_rle_rotate_dst_y_step_per_src_y:dword
        extrn   data_fdps_graphics_rle_rotate_sin_magnitude:word
        extrn   fdps_rle_skip_row:near

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  fdps_rle_blit_rotated
        public  fdps_rle_blit_rotated_scaled

; fdps_rle_blit_rotated -- original at 00056e2a, blit mode 5.
;
; Draws one RLE sprite rotated by the vector (dx, dy), both in 4.12 fixed
; point. On entry the signs of dx and dy pick one of four quadrant arms that
; publish the four signed step globals; from then on the loops work with
; |dx| (cos_magnitude) and |dy| (sin_magnitude). Each source pixel advances
; the destination cursor along the pixel vector through two 16-bit
; accumulators in DX and BP (one step of at most one pixel per axis per
; source pixel); each source row advances the row origin along the
; perpendicular vector through the two row-step accumulator globals, which
; carry from row to row.
;
; Entry (reached only from fdps_blit_dispatch, inside its frame):
;   ESI = RLE stream, EDI = destination pixel, ES = flat data selector,
;   ECX upper 24 bits zero (the dispatcher clears ECX; the body writes only
;   CL but SUB BX,CX, SHL CX,1 and LOOP read CX/ECX).
;   EBP = fdps_blit_dispatch's frame pointer. Reads [EBP+1Ch] (dx) and
;   [EBP+1Eh] (dy): the low and high words of the dispatcher's sixth
;   argument. Writes no frame slot.
; Exit:
;   ESI = stream after the last row, EDI = origin of the row after the last.
;   Destroys EAX (AL, AH), BX, ECX, DX, flags, and EBP: EBP is zeroed, holds
;   the destination pitch, then BP is the within-row y accumulator; it is NOT
;   restored and the dispatcher's epilogue recovers EBP from the stack.
; Globals written: rot_row_step_x/y_accumulator, rotate_dst_x_step_per_src_x,
;   rotated_src_pixel_step_y, rot_row_dest_step_x, rotate_dst_y_step_per_src_y,
;   rotate_cos_magnitude, rotate_sin_magnitude, remaining_rows (decremented).
; Globals read: dst_pitch, src_width, vscale_accumulator (dead load).
;
; C translation with the long explanation: src/rlerot.c (under #if 0).
fdps_rle_blit_rotated proc near
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator, 0
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator, 0
        mov     dx, word ptr [ebp+1Ch]  ; dx = low word of dispatcher arg 6
        mov     ax, word ptr [ebp+1Eh]  ; dy = high word of dispatcher arg 6
        xor     ebp, ebp                ; frame pointer destroyed from here on
        mov     bp, word ptr data_fdps_graphics_rle_blit_dst_pitch ; EBP = zero-extended pitch
        cmp     dx, 0
        jg      short rot_dx_positive
        nop
        nop
        nop
        nop
        cmp     ax, 0
        jg      short rot_dx_neg_dy_pos
        nop
        nop
        nop
        nop
        ; dx <= 0, dy <= 0
        neg     dx
        neg     ax
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 0FFFFFFFFh
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        jmp     near ptr rot_store_magnitudes ; long in the original (D=126): forced, because WASM's own pick at this distance depends on the code around it
rot_dx_neg_dy_pos:
        ; dx <= 0, dy > 0
        neg     dx
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1
        jmp     short rot_store_magnitudes
        nop
        nop
        nop
rot_dx_positive:
        cmp     ax, 0
        jg      short rot_dx_pos_dy_pos
        nop
        nop
        nop
        nop
        ; dx > 0, dy <= 0
        neg     ax
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        jmp     short rot_store_magnitudes
        nop
        nop
        nop
rot_dx_pos_dy_pos:
        ; dx > 0, dy > 0
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1
rot_store_magnitudes:
        mov     word ptr data_fdps_graphics_rle_rotate_cos_magnitude, dx
        mov     word ptr data_fdps_graphics_rle_rotate_sin_magnitude, ax
rot_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
        xor     dx, dx                  ; within-row x accumulator
        xor     bp, bp                  ; within-row y accumulator
        push    edi                     ; row origin, popped at rot_row_done
rot_next_command:
        lodsb
        mov     cl, al
        shl     cl, 1                   ; CF = bit 7
        jb      rot_op_1x
        shl     cl, 1                   ; CF = bit 6
        jb      short rot_op_halftone
        nop
        nop
        nop
        nop
        ; op 0: fill -- one pixel byte, stored at every step of the run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        lodsb
rot_fill_pixel:
        mov     byte ptr [edi], al
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rot_fill_no_x_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rot_fill_no_x_step:
        add     bp, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     bp, 1000h
        jb      short rot_fill_no_y_step
        nop
        nop
        nop
        nop
        sub     bp, 1000h
        mov     byte ptr [edi], al      ; gap filler before the y step
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rot_fill_no_y_step:
        loop    rot_fill_pixel
        or      bx, bx                  ; exact-zero end-of-row test
        jne     rot_next_command
        jmp     rot_row_done
rot_op_halftone:
        ; op 1: half-tone -- run doubled, pixel stored on every other step
        shr     cl, 2
        inc     cl
        shl     cx, 1
        sub     bx, cx
        lodsb
        xor     ah, ah                  ; AH = half-tone phase
rot_half_pixel:
        or      ah, ah
        je      short rot_half_no_store
        nop
        nop
        nop
        nop
        mov     byte ptr [edi], al
rot_half_no_store:
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rot_half_no_x_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rot_half_no_x_step:
        add     bp, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     bp, 1000h
        jb      short rot_half_no_y_step
        nop
        nop
        nop
        nop
        sub     bp, 1000h
        or      ah, ah
        je      short rot_half_y_advance
        nop
        nop
        nop
        nop
        mov     byte ptr [edi], al      ; gap filler before the y step
rot_half_y_advance:
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rot_half_no_y_step:
        xor     ah, 1
        loop    rot_half_pixel
        or      bx, bx
        jne     rot_next_command
        jmp     rot_row_done
rot_op_1x:
        shl     cl, 1                   ; CF = bit 6
        jb      short rot_op_skip
        nop
        nop
        nop
        nop
        ; op 2: literal -- one stream byte per step
        shr     cl, 2
        inc     cl
        sub     bx, cx
rot_copy_pixel:
        lodsb
        mov     byte ptr [edi], al
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rot_copy_no_x_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rot_copy_no_x_step:
        add     bp, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     bp, 1000h
        jb      short rot_copy_no_y_step
        nop
        nop
        nop
        nop
        sub     bp, 1000h
        mov     byte ptr [edi], al      ; gap filler before the y step
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rot_copy_no_y_step:
        loop    rot_copy_pixel
        or      bx, bx
        jne     rot_next_command
        jmp     short rot_row_done
        nop
        nop
        nop
rot_op_skip:
        ; op 3: skip -- transparent run, cursor steps without storing
        shr     cl, 2
        inc     cl
        sub     bx, cx
rot_skip_pixel:
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rot_skip_no_x_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rot_skip_no_x_step:
        add     bp, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     bp, 1000h
        jb      short rot_skip_no_y_step
        nop
        nop
        nop
        nop
        sub     bp, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rot_skip_no_y_step:
        loop    rot_skip_pixel
        or      bx, bx
        jne     rot_next_command
rot_row_done:
        pop     edi                     ; back to the row origin
        mov     dx, word ptr data_fdps_graphics_rle_blit_vscale_accumulator ; dead load, overwritten next
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rot_row_no_x_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x
rot_row_no_x_step:
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rot_row_no_y_step
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y
rot_row_no_y_step:
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator, dx
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     rot_next_row
        ret
fdps_rle_blit_rotated endp

; fdps_rle_blit_rotated_scaled -- original at 00057114, blit mode 6.
;
; Draws one RLE sprite rotated and resampled at the same time: the rotation
; walk of mode 5 (fractional 0x1000 accumulators stepping the cursor along the
; rotated pixel and row vectors) combined with the two Bresenham counters of
; mode 4, so the walked rectangle is the destination width x height. Each
; destination row re-decodes its source row (ESI and EDI pushed and popped
; around the row); fdps_rle_skip_row is called for every source row the
; vertical accumulator steps past.
;
; Entry (reached only from fdps_blit_dispatch, inside its frame):
;   ESI = RLE stream, EDI = destination pixel of the source's top-left corner,
;   ES = flat data selector, ECX bits 8..31 zero (fdps_rle_skip_row uses the
;   whole of CX / ECX as the run length; this routine keeps them zero).
;   EBP = fdps_blit_dispatch's frame pointer. Reads [EBP+1Ch], the
;   dispatcher's sixth argument, as a POINTER to a record of four dwords
;   (low words used): destination width, destination height, rotation dx,
;   rotation dy. Writes no frame slot.
; Exit:
;   ESI = stream after the last source row walked; EDI = origin of the row
;   after the last one. Destroys EAX (AX), BX, CX (CL), DX, flags, and EBP:
;   EBP is overwritten with the record pointer, then zeroed and used as the
;   pitch and horizontal accumulator, and is NOT restored; the dispatcher's
;   epilogue recovers EBP from the stack.
; Globals written: dest_width, dest_height, dest_rows_remaining,
;   vscale_accumulator, the rotate cos/sin magnitudes, the four step globals
;   and the four rot_*_accumulator words.
; Globals read: dst_pitch, src_width, remaining_rows (source height, not
;   decremented here).
;
; C translation with the long explanation: src/rlerot.c (under #if 0).
fdps_rle_blit_rotated_scaled proc near
        mov     ebp, dword ptr [ebp+1Ch] ; EBP = the geometry record
        mov     ax, word ptr [ebp]
        mov     word ptr data_fdps_graphics_rle_blit_dest_width, ax
        mov     ax, word ptr [ebp+4]
        mov     word ptr data_fdps_graphics_rle_blit_dest_height, ax
        mov     dx, word ptr [ebp+8]    ; rotation dx
        mov     ax, word ptr [ebp+0Ch]  ; rotation dy
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator, 0
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator, 0
        xor     ebp, ebp
        mov     bp, word ptr data_fdps_graphics_rle_blit_dst_pitch ; EBP = zero-extended pitch
        cmp     dx, 0
        jg      short rots_dx_positive
        nop
        nop
        nop
        nop
        cmp     ax, 0
        jg      short rots_dxneg_dypos
        nop
        nop
        nop
        nop
; dx <= 0, dy <= 0
        neg     dx
        neg     ax
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 0FFFFFFFFh
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        jmp     near ptr rots_store_magnitudes ; long in the original; WASM shortens it when assembled after rot
; dx <= 0, dy > 0
rots_dxneg_dypos:
        neg     dx
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1
        jmp     short rots_store_magnitudes
        nop
        nop
        nop
rots_dx_positive:
        cmp     ax, 0
        jg      short rots_dxpos_dypos
        nop
        nop
        nop
        nop
; dx > 0, dy <= 0
        neg     ax
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 0FFFFFFFFh
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        jmp     short rots_store_magnitudes
        nop
        nop
        nop
; dx > 0, dy > 0
rots_dxpos_dypos:
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x, 1
        mov     dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y, ebp
        neg     ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y, ebp
        mov     dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x, 1
rots_store_magnitudes:
        mov     word ptr data_fdps_graphics_rle_rotate_cos_magnitude, dx
        mov     word ptr data_fdps_graphics_rle_rotate_sin_magnitude, ax
        mov     bp, word ptr data_fdps_graphics_rle_blit_dest_height
        mov     word ptr data_fdps_graphics_rle_blit_dest_rows_remaining, bp
        mov     word ptr data_fdps_graphics_rle_blit_vscale_accumulator, bp
rots_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_dest_width ; BX = destination pixels left in row
        mov     bp, bx                  ; horizontal accumulator starts at dest width
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator, 0
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator, 0
        push    esi                     ; row stream start, popped at rots_row_done
        push    edi                     ; row origin, popped at rots_row_done
rots_next_command:
        lodsb
        mov     cl, al
        shl     cl, 1
        jae     short rots_op_0x
        nop
        nop
        nop
        nop
        jmp     rots_op_1x
rots_op_0x:
        shl     cl, 1
        jae     short rots_op_fill
        nop
        nop
        nop
        nop
        jmp     rots_op_halftone
; op 00: fill -- one pixel byte follows
rots_op_fill:
        shr     cl, 2
        inc     cl
        lodsb
rots_fill_loop:
        cmp     bp, word ptr data_fdps_graphics_rle_blit_src_width
        jae     short rots_fill_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        dec     cl
        jne     rots_fill_loop
        jmp     rots_next_command
rots_fill_emit:
        sub     bp, word ptr data_fdps_graphics_rle_blit_src_width
        mov     byte ptr [edi], al
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rots_fill_x_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rots_fill_x_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rots_fill_y_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        mov     byte ptr [edi], al      ; diagonal gap filler before the vertical step
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rots_fill_y_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator, dx
        dec     bx
        jne     rots_fill_loop
        jmp     rots_row_done
; op 01: half-tone -- run doubled, AH is the phase
rots_op_halftone:
        shr     cl, 2
        inc     cl
        shl     cx, 1                   ; CL <= 40h here, so CH stays zero
        lodsb
        xor     ah, ah
rots_halftone_loop:
        cmp     bp, word ptr data_fdps_graphics_rle_blit_src_width
        jae     short rots_halftone_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        xor     ah, 1
        dec     cl
        jne     rots_halftone_loop
        jmp     rots_next_command
rots_halftone_emit:
        sub     bp, word ptr data_fdps_graphics_rle_blit_src_width
        or      ah, ah
        je      short rots_halftone_step
        nop
        nop
        nop
        nop
        mov     byte ptr [edi], al
rots_halftone_step:
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rots_halftone_x_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rots_halftone_x_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rots_halftone_y_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        or      ah, ah
        je      short rots_halftone_gap_done
        nop
        nop
        nop
        nop
        mov     byte ptr [edi], al      ; diagonal gap filler, only on the stored phase
rots_halftone_gap_done:
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rots_halftone_y_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator, dx
        dec     bx
        jne     rots_halftone_loop
        jmp     rots_row_done
; ops 1x: literal (10) or skip (11)
rots_op_1x:
        shl     cl, 1
        jb      rots_op_skip
; op 10: literal -- ESI steps only when a source column is dropped
        shr     cl, 2
        inc     cl
rots_literal_loop:
        cmp     bp, word ptr data_fdps_graphics_rle_blit_src_width
        jae     short rots_literal_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        inc     esi
        dec     cl
        jne     rots_literal_loop
        jmp     rots_next_command
rots_literal_emit:
        sub     bp, word ptr data_fdps_graphics_rle_blit_src_width
        mov     al, byte ptr [esi]
        mov     byte ptr [edi], al
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rots_literal_x_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rots_literal_x_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rots_literal_y_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        mov     byte ptr [edi], al      ; diagonal gap filler before the vertical step
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rots_literal_y_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator, dx
        dec     bx
        jne     rots_literal_loop
        jmp     rots_row_done
; op 11: skip -- transparent run, the cursor still walks
rots_op_skip:
        shr     cl, 2
        inc     cl
rots_skip_loop:
        cmp     bp, word ptr data_fdps_graphics_rle_blit_src_width
        jae     short rots_skip_emit
        nop
        nop
        nop
        nop
        add     bp, word ptr data_fdps_graphics_rle_blit_dest_width
        dec     cl
        jne     rots_skip_loop
        jmp     rots_next_command
rots_skip_emit:
        sub     bp, word ptr data_fdps_graphics_rle_blit_src_width
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rots_skip_x_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_x_step_per_src_x
rots_skip_x_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rots_skip_y_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_blit_rotated_src_pixel_step_y
rots_skip_y_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_pixel_step_y_accumulator, dx
        dec     bx
        jne     rots_skip_loop
; end of destination row: vertical Bresenham step, then the row vector
rots_row_done:
        pop     edi
        pop     esi
        mov     dx, word ptr data_fdps_graphics_rle_blit_vscale_accumulator
rots_vstep_loop:
        cmp     dx, word ptr data_fdps_graphics_rle_blit_remaining_rows
        ja      short rots_vstep_done
        nop
        nop
        nop
        nop
        call    fdps_rle_skip_row       ; destroys BX, CX, AL; advances ESI; keeps DX
        add     dx, word ptr data_fdps_graphics_rle_blit_dest_height
        jmp     rots_vstep_loop
rots_vstep_done:
        sub     dx, word ptr data_fdps_graphics_rle_blit_remaining_rows
        mov     word ptr data_fdps_graphics_rle_blit_vscale_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_sin_magnitude
        cmp     dx, 1000h
        jb      short rots_row_x_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_blit_rot_row_dest_step_x
rots_row_x_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_x_accumulator, dx
        mov     dx, word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator
        add     dx, word ptr data_fdps_graphics_rle_rotate_cos_magnitude
        cmp     dx, 1000h
        jb      short rots_row_y_stored
        nop
        nop
        nop
        nop
        sub     dx, 1000h
        add     edi, dword ptr data_fdps_graphics_rle_rotate_dst_y_step_per_src_y
rots_row_y_stored:
        mov     word ptr data_fdps_graphics_rle_blit_rot_row_step_y_accumulator, dx
        dec     word ptr data_fdps_graphics_rle_blit_dest_rows_remaining
        jne     rots_next_row
        ret
fdps_rle_blit_rotated_scaled endp

_TEXT   ends

        end
