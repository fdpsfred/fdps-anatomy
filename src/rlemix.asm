; rlemix.asm -- the blending kernels: translucent (mode 9), tint sprite and
; backdrop (mode 10), tint (mode 11) and translucent colour range (mode 12).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3).  The C spelling is kept for reading in
; src/rleblend.c under #if 0, and the colour-range globals are still defined
; there; this file is what is linked.  Entered only from fdps_blit_dispatch
; (rledisp.asm); the blend descriptor is read out of the dispatcher's EBP
; frame, and four of these kernels use the dispatcher's argument slots as
; their own scratch.

        .386p

        extrn   data_fdps_graphics_rle_blit_dst_row_advance:dword
        extrn   data_fdps_graphics_rle_blit_remaining_rows:word
        extrn   data_fdps_graphics_rle_blit_src_width:word
        extrn   data_fdps_graphics_rle_blit_translucent_color_max:word
        extrn   data_fdps_graphics_rle_blit_translucent_color_min:word

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  fdps_rle_blit_translucent
        public  fdps_rle_blit_tint_sprite_and_backdrop
        public  fdps_rle_blit_tint
        public  fdps_rle_blit_translucent_color_range

; 0005761b  fdps_rle_blit_translucent -- blit mode 9, translucent sprite.
;
; Decodes the four RLE ops (00 fill, 01 stretched fill into the second byte of
; every pair, 10 literal, 11 transparent skip; low six bits plus one = run
; length) and blends every painted pixel with the destination byte under it:
; source and destination are weighted through two rows of the shade ramp whose
; coefficients sum to 16, added, folded by SHR 4 / AND 0f0f0fh, turned into a
; green-major cube index and mapped back to a palette byte through the inverse
; colour cube. The level picks the ramp rows; above 8 the rows are swapped.
;
; Entry:  ESI = RLE stream, EDI = top-left destination pixel, EDX = row advance
;         (pitch - width), EBP = fdps_blit_dispatch's frame, ES = flat data,
;         direction flag clear. ECX is cleared here before the row loop.
; Exit:   ESI past the consumed stream, EDI past the last row. Destroys EAX,
;         EBX, ECX, EDX, ESI, EDI, flags. EBP kept; no PUSH or POP.
; Frame:  [ebp+1Ch] read on entry as the blend record pointer
;         ([+0] shade ramp base, [+4] level, [+8] inverse colour cube base),
;         then reused as scratch for the weighted source pixel of a fill run.
;         Written as scratch: [ebp+08h] cube base, [ebp+0Ch] source weight row,
;         [ebp+10h] destination weight row, [ebp+14h] 0f0f0fh, [ebp+18h] 0ffffh.
;         The dispatcher never reads those slots back.
; Globals: data_fdps_graphics_rle_blit_dst_row_advance (written on entry, read
;         at every row end), data_fdps_graphics_rle_blit_src_width (read every
;         row), data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rleblend.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_translucent proc near
        mov     data_fdps_graphics_rle_blit_dst_row_advance, edx
        mov     eax, dword ptr [ebp+1Ch]        ; blend record
        mov     ebx, dword ptr [eax+8]
        mov     dword ptr [ebp+8], ebx          ; inverse colour cube base
        mov     ebx, dword ptr [eax]            ; shade ramp base
        mov     ecx, dword ptr [eax+4]          ; blend level
        mov     eax, 2400h                      ; nine ramp rows (9 * 400h bytes)
        mov     edx, 0
        cmp     ecx, 8                          ; unsigned
        jbe     short tran_level_ready
        nop
        nop
        nop
        nop
        ; level above 8: use row 16 - level and swap the two row offsets
        sub     ecx, 10h
        neg     ecx
        xchg    edx, eax
tran_level_ready:
        shl     ecx, 0Ah                        ; ramp row * 400h bytes
        add     ecx, ebx
        add     eax, ecx
        mov     dword ptr [ebp+0Ch], eax        ; source weight row
        add     edx, ecx
        mov     dword ptr [ebp+10h], edx        ; destination weight row
        mov     eax, 0F0F0Fh
        mov     dword ptr [ebp+14h], eax
        mov     eax, 0FFFFh
        mov     dword ptr [ebp+18h], eax
        xor     ecx, ecx
tran_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
tran_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      tran_op_literal_or_skip         ; top bit set: op 10 or 11
        shl     cl, 1
        jb      short tran_op_stretched         ; op 01
        nop
        nop
        nop
        nop
        ; op 00: blended fill, source pixel weighted once for the whole run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx        ; keep the weighted source
tran_fill_loop:
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx                        ; green-major cube index
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        mov     edx, dword ptr [ebp+1Ch]        ; reload the weighted source
        loop    tran_fill_loop
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     tran_next_op
        jmp     tran_end_row
tran_op_stretched:
        ; op 01: blend into the second byte of each pair, first byte untouched
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx        ; keep the weighted source
tran_stretch_loop:
        inc     edi
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        mov     edx, dword ptr [ebp+1Ch]
        loop    tran_stretch_loop
        or      bx, bx
        jne     tran_next_op
        jmp     short tran_end_row
        nop
        nop
        nop
tran_op_literal_or_skip:
        shl     cl, 1
        jb      short tran_op_skip              ; op 11
        nop
        nop
        nop
        nop
        ; op 10: blended literal, each source pixel weighted on its own
        shr     cl, 2
        inc     cl
        sub     bx, cx
tran_literal_loop:
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        loop    tran_literal_loop
        or      bx, bx
        jne     tran_next_op
        jmp     short tran_end_row
        nop
        nop
        nop
tran_op_skip:
        ; op 11: transparent run, step the destination over it
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx
        jne     tran_next_op
tran_end_row:
        add     edi, data_fdps_graphics_rle_blit_dst_row_advance
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     tran_next_row
        ret
fdps_rle_blit_translucent endp

; 00057793  fdps_rle_blit_tint_sprite_and_backdrop -- blit mode 10, tinted
; sprite plus tinted backdrop (the battle map's movement-range highlight).
;
; Decodes the four RLE ops (00 fill, 01 stretched fill into the second byte of
; every pair, 10 literal, 11 backdrop; low six bits plus one = run length) and
; blends every pixel it touches toward one constant tint colour. The tint's
; weighted value is fetched once before drawing; each pixel is its own weighted
; colour plus that constant, folded by SHR 4 / AND 0f0f0fh, turned into a
; green-major cube index and mapped back through the inverse colour cube.
; Op 11 does NOT skip: it re-reads the destination pixel, tints it and writes
; it back, so the whole rectangle comes out tinted.
;
; Entry:  ESI = RLE stream, EDI = top-left destination pixel, EDX = row advance
;         (pitch - width), EBP = fdps_blit_dispatch's frame, ES = flat data,
;         direction flag clear. ECX is cleared here before the row loop.
; Exit:   ESI past the consumed stream, EDI past the last row. Destroys EAX,
;         EBX, ECX, EDX, ESI, EDI, flags. EBP kept; no PUSH or POP.
; Frame:  [ebp+1Ch] read on entry as the blend record pointer
;         ([+0] shade ramp base, [+4] level, [+8] inverse colour cube base,
;         [+0Ch] tint colour), then overwritten with the tint colour and then
;         with the tint's weighted value for the rest of the routine.
;         Written as scratch: [ebp+08h] cube base, [ebp+0Ch] tint weight row,
;         [ebp+10h] pixel weight row, [ebp+14h] 0f0f0fh, [ebp+18h] 0ffffh.
;         The dispatcher never reads those slots back.
; Globals: data_fdps_graphics_rle_blit_dst_row_advance (written on entry, read
;         at every row end), data_fdps_graphics_rle_blit_src_width (read every
;         row), data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rleblend.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_tint_sprite_and_backdrop proc near
        mov     data_fdps_graphics_rle_blit_dst_row_advance, edx
        mov     eax, dword ptr [ebp+1Ch]        ; blend record
        mov     ebx, dword ptr [eax+0Ch]
        mov     dword ptr [ebp+1Ch], ebx        ; tint colour (palette index)
        mov     ebx, dword ptr [eax+8]
        mov     dword ptr [ebp+8], ebx          ; inverse colour cube base
        mov     ebx, dword ptr [eax]            ; shade ramp base
        mov     ecx, dword ptr [eax+4]          ; blend level
        mov     edx, 2400h                      ; nine ramp rows (9 * 400h bytes)
        mov     eax, 0
        cmp     ecx, 8                          ; unsigned
        jbe     short tsb_level_ready
        nop
        nop
        nop
        nop
        ; level above 8: use row 16 - level and swap the two row offsets
        sub     ecx, 10h
        neg     ecx
        xchg    edx, eax
tsb_level_ready:
        shl     ecx, 0Ah                        ; ramp row * 400h bytes
        add     ecx, ebx
        add     eax, ecx
        mov     dword ptr [ebp+0Ch], eax        ; tint weight row
        add     edx, ecx
        mov     dword ptr [ebp+10h], edx        ; pixel weight row
        mov     eax, 0F0F0Fh
        mov     dword ptr [ebp+14h], eax
        mov     eax, 0FFFFh
        mov     dword ptr [ebp+18h], eax
        mov     eax, dword ptr [ebp+1Ch]        ; tint colour, a whole dword index
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx        ; weighted tint, constant from here
        xor     ecx, ecx
tsb_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
tsb_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      near ptr tsb_op_literal_or_backdrop ; top bit set: op 10 or 11
        shl     cl, 1
        jb      short tsb_op_stretched          ; op 01
        nop
        nop
        nop
        nop
        ; op 00: tinted fill, one stream pixel blended once for the whole run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx                        ; green-major cube index
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        rep     stosb
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     tsb_next_op
        jmp     tsb_end_row
tsb_op_stretched:
        ; op 01: tint into the second byte of each pair, first byte untouched;
        ; the run length comes off the row width twice
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
tsb_stretch_loop:
        inc     edi
        stosb
        loop    tsb_stretch_loop
        or      bx, bx
        jne     tsb_next_op
        jmp     short tsb_end_row
        nop
        nop
        nop
tsb_op_literal_or_backdrop:
        shl     cl, 1
        jb      short tsb_op_backdrop           ; op 11
        nop
        nop
        nop
        nop
        ; op 10: tinted literal, each stream pixel blended on its own
        shr     cl, 2
        inc     cl
        sub     bx, cx
tsb_literal_loop:
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        loop    tsb_literal_loop
        or      bx, bx
        jne     tsb_next_op
        jmp     short tsb_end_row
        nop
        nop
        nop
tsb_op_backdrop:
        ; op 11: no stream bytes; tint the destination pixel already there
        shr     cl, 2
        inc     cl
        sub     bx, cx
tsb_backdrop_loop:
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        loop    tsb_backdrop_loop
        or      bx, bx
        jne     tsb_next_op
tsb_end_row:
        add     edi, data_fdps_graphics_rle_blit_dst_row_advance
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     tsb_next_row
        ret
fdps_rle_blit_tint_sprite_and_backdrop endp

; 00057916  fdps_rle_blit_tint -- blit mode 11, tinted sprite.
;
; Decodes the four RLE ops (00 fill, 01 stretched fill into the second byte of
; every pair, 10 literal, 11 transparent skip; low six bits plus one = run
; length) and blends every painted sprite pixel toward one constant tint
; colour: the pixel is weighted through one shade-ramp row, the tint through
; the complementary row (fetched once before drawing), the two are added,
; folded by SHR 4 / AND 0f0f0fh into a green-major cube index and mapped back
; to a palette byte through the inverse colour cube. Transparent runs are
; skipped untouched (unlike mode 10, which tints the backdrop there).
;
; Entry:  ESI = RLE stream, EDI = top-left destination pixel, EDX = row advance
;         (pitch - width), EBP = fdps_blit_dispatch's frame, ES = flat data,
;         direction flag clear. ECX is cleared here before the row loop.
; Exit:   ESI past the consumed stream, EDI past the last row. Destroys EAX,
;         EBX, ECX, EDX, ESI, EDI, flags. EBP kept; no PUSH or POP.
; Frame:  [ebp+1Ch] read on entry as the blend record pointer
;         ([+0] shade ramp base, [+4] level, [+8] inverse colour cube base,
;         [+0Ch] tint colour), then overwritten with the tint colour and then
;         with the weighted tint.
;         Written as scratch: [ebp+08h] cube base, [ebp+0Ch] tint weight row,
;         [ebp+10h] pixel weight row, [ebp+14h] 0f0f0fh, [ebp+18h] 0ffffh.
;         The dispatcher never reads those slots back.
; Globals: data_fdps_graphics_rle_blit_dst_row_advance (written on entry, read
;         at every row end), data_fdps_graphics_rle_blit_src_width (read every
;         row), data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rleblend.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_tint proc near
        mov     data_fdps_graphics_rle_blit_dst_row_advance, edx
        mov     eax, dword ptr [ebp+1Ch]        ; blend record
        mov     ebx, dword ptr [eax+0Ch]
        mov     dword ptr [ebp+1Ch], ebx        ; tint colour
        mov     ebx, dword ptr [eax+8]
        mov     dword ptr [ebp+8], ebx          ; inverse colour cube base
        mov     ebx, dword ptr [eax]            ; shade ramp base
        mov     ecx, dword ptr [eax+4]          ; blend level
        mov     edx, 2400h                      ; nine ramp rows (9 * 400h bytes)
        mov     eax, 0
        cmp     ecx, 8                          ; unsigned, no upper clamp
        jbe     short tint_level_ready
        nop
        nop
        nop
        nop
        ; level above 8: use row 16 - level and swap the two row offsets
        sub     ecx, 10h
        neg     ecx
        xchg    edx, eax
tint_level_ready:
        shl     ecx, 0Ah                        ; ramp row * 400h bytes
        add     ecx, ebx
        add     eax, ecx
        mov     dword ptr [ebp+0Ch], eax        ; tint weight row
        add     edx, ecx
        mov     dword ptr [ebp+10h], edx        ; pixel weight row
        mov     eax, 0F0F0Fh
        mov     dword ptr [ebp+14h], eax
        mov     eax, 0FFFFh
        mov     dword ptr [ebp+18h], eax
        ; weighted tint = tint row[tint colour], fetched once for the whole blit
        mov     eax, dword ptr [ebp+1Ch]
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx
        xor     ecx, ecx
tint_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
tint_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      near ptr tint_op_literal_or_skip ; top bit set: op 10 or 11
        shl     cl, 1
        jb      short tint_op_stretched         ; op 01
        nop
        nop
        nop
        nop
        ; op 00: tinted fill, blended once, then REP STOSB for the whole run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]        ; + weighted tint
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx                        ; green-major cube index
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        rep     stosb
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     tint_next_op
        jmp     tint_end_row
tint_op_stretched:
        ; op 01: tinted fill into the second byte of each pair, width taken twice
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
tint_stretch_loop:
        inc     edi
        stosb
        loop    tint_stretch_loop
        or      bx, bx
        jne     tint_next_op
        jmp     short tint_end_row
        nop
        nop
        nop
tint_op_literal_or_skip:
        shl     cl, 1
        jb      short tint_op_skip              ; op 11
        nop
        nop
        nop
        nop
        ; op 10: tinted literal, each pixel weighted and blended on its own
        shr     cl, 2
        inc     cl
        sub     bx, cx
tint_literal_loop:
        xor     eax, eax
        lodsb
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, dword ptr [ebp+1Ch]
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        loop    tint_literal_loop
        or      bx, bx
        jne     tint_next_op
        jmp     short tint_end_row
        nop
        nop
        nop
tint_op_skip:
        ; op 11: transparent run, step the destination over it untouched
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx
        jne     tint_next_op
tint_end_row:
        add     edi, data_fdps_graphics_rle_blit_dst_row_advance
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     tint_next_row
        ret
fdps_rle_blit_tint endp

; 00057a74  fdps_rle_blit_translucent_color_range -- blit mode 12, translucent
; sprite limited to a palette index range.
;
; Decodes the four RLE ops (00 fill, 01 stretched fill into the second byte of
; every pair, 10 literal, 11 transparent skip; low six bits plus one = run
; length) like mode 9, but tests every painted source pixel against a signed
; 16-bit index range [color_min, color_max]: inside it the pixel is blended with
; the destination exactly as fdps_rle_blit_translucent does (two shade ramp
; rows, SHR 4 / AND 0f0f0fh, green-major cube index, inverse colour cube);
; outside it the raw pixel is stored opaque.
;
; Entry:  ESI = RLE stream, EDI = top-left destination pixel, EDX = row advance
;         (pitch - width), EBP = fdps_blit_dispatch's frame, ES = flat data,
;         direction flag clear. ECX is cleared here before the row loop.
; Exit:   ESI past the consumed stream, EDI past the last row. Destroys EAX,
;         EBX, ECX, EDX, ESI, EDI, flags. EBP kept; no PUSH or POP.
; Frame:  [ebp+1Ch] read on entry as the blend record pointer
;         ([+0] shade ramp base, [+4] level, [+8] inverse colour cube base,
;         [+0Ch] colour range min, [+10h] colour range max; low 16 bits used),
;         then reused as scratch for the weighted source pixel of a fill run.
;         Written as scratch: [ebp+08h] cube base, [ebp+0Ch] source weight row,
;         [ebp+10h] destination weight row, [ebp+14h] 0f0f0fh, [ebp+18h] 0ffffh.
;         The dispatcher never reads those slots back.
; Globals: data_fdps_graphics_rle_blit_dst_row_advance (written on entry, read
;         at every row end), data_fdps_graphics_rle_blit_translucent_color_min /
;         _max (written on entry, read by every range test and left holding the
;         last call's range), data_fdps_graphics_rle_blit_src_width (read every
;         row), data_fdps_graphics_rle_blit_remaining_rows (decremented in place).
; See src/rleblend.c (the #if 0 C translation) for the long explanation.
fdps_rle_blit_translucent_color_range proc near
        mov     data_fdps_graphics_rle_blit_dst_row_advance, edx
        mov     eax, dword ptr [ebp+1Ch]        ; blend record
        mov     bx, word ptr [eax+0Ch]
        mov     data_fdps_graphics_rle_blit_translucent_color_min, bx
        mov     bx, word ptr [eax+10h]
        mov     data_fdps_graphics_rle_blit_translucent_color_max, bx
        mov     ebx, dword ptr [eax+8]
        mov     dword ptr [ebp+8], ebx          ; inverse colour cube base
        mov     ebx, dword ptr [eax]            ; shade ramp base
        mov     ecx, dword ptr [eax+4]          ; blend level
        mov     eax, 2400h                      ; nine ramp rows (9 * 400h bytes)
        mov     edx, 0
        cmp     ecx, 8                          ; unsigned
        jbe     short tcr_level_ready
        nop
        nop
        nop
        nop
        ; level above 8: use row 16 - level and swap the two row offsets
        sub     ecx, 10h
        neg     ecx
        xchg    edx, eax
tcr_level_ready:
        shl     ecx, 0Ah                        ; ramp row * 400h bytes
        add     ecx, ebx
        add     eax, ecx
        mov     dword ptr [ebp+0Ch], eax        ; source weight row
        add     edx, ecx
        mov     dword ptr [ebp+10h], edx        ; destination weight row
        mov     eax, 0F0F0Fh
        mov     dword ptr [ebp+14h], eax
        mov     eax, 0FFFFh
        mov     dword ptr [ebp+18h], eax
        xor     ecx, ecx
tcr_next_row:
        mov     bx, word ptr data_fdps_graphics_rle_blit_src_width
tcr_next_op:
        lodsb
        mov     cl, al
        shl     cl, 1
        jb      tcr_op_literal_or_skip          ; top bit set: op 10 or 11
        shl     cl, 1
        jb      short tcr_op_stretched          ; op 01
        nop
        nop
        nop
        nop
        ; op 00: fill, one pixel byte tested once for the whole run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        xor     eax, eax
        lodsb
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_min   ; signed
        jl      short tcr_fill_opaque
        nop
        nop
        nop
        nop
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_max
        jle     short tcr_fill_blend
        nop
        nop
        nop
        nop
tcr_fill_opaque:
        rep     stosb                           ; out of range: plain fill
        jmp     short tcr_fill_done
        nop
        nop
        nop
tcr_fill_blend:
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx        ; keep the weighted source
tcr_fill_loop:
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx                        ; green-major cube index
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        mov     edx, dword ptr [ebp+1Ch]        ; reload the weighted source
        loop    tcr_fill_loop
tcr_fill_done:
        or      bx, bx                          ; exact-zero test, see rle.c
        jne     tcr_next_op
        jmp     tcr_end_row
tcr_op_stretched:
        ; op 01: second byte of each pair, first byte untouched, one test per run
        shr     cl, 2
        inc     cl
        sub     bx, cx
        sub     bx, cx
        xor     eax, eax
        lodsb
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_min   ; signed
        jl      short tcr_stretch_opaque
        nop
        nop
        nop
        nop
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_max
        jle     short tcr_stretch_blend
        nop
        nop
        nop
        nop
tcr_stretch_opaque:
        inc     edi                             ; out of range: plain store
        stosb
        loop    tcr_stretch_opaque
        jmp     short tcr_stretch_done
        nop
        nop
        nop
tcr_stretch_blend:
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        mov     dword ptr [ebp+1Ch], edx        ; keep the weighted source
tcr_stretch_loop:
        inc     edi
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
        stosb
        mov     edx, dword ptr [ebp+1Ch]
        loop    tcr_stretch_loop
tcr_stretch_done:
        or      bx, bx
        jne     tcr_next_op
        ; Goes to tcr_end_row, exactly 127 bytes past this 2-byte JMP. With the
        ; long forward JB at the top of the op dispatch jumping to a label in
        ; between, WASM 10.0a silently assembles "jmp short tcr_end_row" as a
        ; 5-byte JMP at this distance; the $-relative form stays 2 bytes
        ; (rebuild_info/build_flags.md).
        jmp     short $+129
        nop
        nop
        nop
tcr_op_literal_or_skip:
        shl     cl, 1
        jb      short tcr_op_skip               ; op 11
        nop
        nop
        nop
        nop
        ; op 10: literal, each source pixel tested and weighted on its own
        shr     cl, 2
        inc     cl
        sub     bx, cx
tcr_literal_loop:
        xor     eax, eax
        lodsb
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_min   ; signed
        jl      short tcr_literal_store
        nop
        nop
        nop
        nop
        cmp     ax, data_fdps_graphics_rle_blit_translucent_color_max
        jg      short tcr_literal_store
        nop
        nop
        nop
        nop
        shl     eax, 2
        add     eax, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [eax]
        xor     eax, eax
        mov     al, byte ptr [edi]
        shl     eax, 2
        add     eax, dword ptr [ebp+10h]
        mov     eax, dword ptr [eax]
        add     eax, edx
        shr     eax, 4
        and     eax, dword ptr [ebp+14h]
        mov     edx, eax
        and     edx, dword ptr [ebp+18h]
        shr     eax, 0Ch
        or      eax, edx
        add     eax, dword ptr [ebp+8]
        mov     al, byte ptr [eax]
tcr_literal_store:
        stosb                                   ; AL = cube byte or raw pixel
        loop    tcr_literal_loop
        or      bx, bx
        jne     tcr_next_op
        jmp     short tcr_end_row
        nop
        nop
        nop
tcr_op_skip:
        ; op 11: transparent run, step the destination over it
        shr     cl, 2
        inc     cl
        add     edi, ecx
        sub     bx, cx
        or      bx, bx
        jne     tcr_next_op
tcr_end_row:
        add     edi, data_fdps_graphics_rle_blit_dst_row_advance
        dec     word ptr data_fdps_graphics_rle_blit_remaining_rows
        jne     tcr_next_row
        ret
fdps_rle_blit_translucent_color_range endp

_TEXT   ends

        end
