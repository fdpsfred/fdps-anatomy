; rledisp.asm -- fdps_blit_dispatch, the one entry point into the RLE sprite
; blitters (000568db in the original).
;
; Hand-written assembly in the original, transcribed instruction for
; instruction (ticket 22.3): the same instructions, the same lengths, the same
; NOP padding behind every forward short branch.  The C spelling of the same
; routine is kept for reading in src/blit.c under #if 0; this file is what is
; linked.  rebuild_info/code_layout.md says how to switch back to the C.
;
; The dispatcher is called from C with the stack convention blit.h declares
; (seven arguments, caller pops), keeps EBX, ESI, EDI and EBP, and hands the
; stream to its kernel in ESI, the destination in EDI and the row advance in
; EDX.  The kernels in rlebase.asm, rlepal.asm, rleturn.asm and rlemix.asm
; read the rest of their input out of this routine's own EBP frame.

        .386p

        extrn   data_fdps_graphics_rle_blit_dst_pitch:word
        extrn   data_fdps_graphics_rle_blit_remaining_rows:word
        extrn   data_fdps_graphics_rle_blit_src_width:word
        extrn   fdps_rle_blit_mirrored_horizontal:near
        extrn   fdps_rle_blit_mirrored_vertical:near
        extrn   fdps_rle_blit_passthrough:near
        extrn   fdps_rle_blit_recolor:near
        extrn   fdps_rle_blit_remap_sprite_and_backdrop:near
        extrn   fdps_rle_blit_rotated:near
        extrn   fdps_rle_blit_rotated_scaled:near
        extrn   fdps_rle_blit_scaled:near
        extrn   fdps_rle_blit_tint:near
        extrn   fdps_rle_blit_tint_sprite_and_backdrop:near
        extrn   fdps_rle_blit_translucent:near
        extrn   fdps_rle_blit_translucent_color_range:near
        extrn   fdps_rle_blit_with_palette_remap:near

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  fdps_blit_dispatch

; fdps_blit_dispatch -- original at 000568db.
;
; Draws one RLE sprite. Publishes the destination pitch, the source width and
; the source row count to the shared RLE globals, computes the destination row
; advance (pitch - width), then selects one of thirteen drawing kernels by
; blit_mode (0..12) through a CMP BH,n / JNE chain. A mode above 12 calls
; nothing and falls through to the epilogue.
;
; Entry: stack convention, caller removes the seven dword arguments. EBP frame
;   [ebp+08h] rle_stream      -> ESI
;   [ebp+0Ch] dest_pixel      -> EDI
;   [ebp+10h] src_width       (low word -> data_fdps_graphics_rle_blit_src_width)
;   [ebp+14h] src_rows        (low word -> data_fdps_graphics_rle_blit_remaining_rows)
;   [ebp+18h] dest_pitch      (low word -> data_fdps_graphics_rle_blit_dst_pitch)
;   [ebp+1Ch] mode_operand    not read here; the kernels read it from this frame
;   [ebp+20h] blit_mode       (low byte -> BH)
; Handoff to the kernel: ESI = rle_stream, EDI = dest_pixel,
;   EDX = dest_pitch - src_width, BH = blit_mode, ECX = 0, EAX = 0 (the kernels
;   write only CL / AL but read all of ECX / EAX), EBP = this frame.
;   Some kernels also use this frame's argument slots as scratch.
; Exit: EBX, ESI, EDI, EBP restored by POP from ESP (fdps_rle_blit_scaled
;   zeroes EBP and relies on this); EAX, ECX, EDX destroyed. No return value.
;
; The C translation with the long explanation is in src/blit.c (#if 0).

fdps_blit_dispatch proc near
        push    ebp
        mov     ebp, esp
        push    edi
        push    esi
        push    ebx
        mov     esi, dword ptr [ebp+8]
        mov     edi, dword ptr [ebp+0Ch]
        mov     edx, dword ptr [ebp+18h]
        mov     word ptr data_fdps_graphics_rle_blit_dst_pitch, dx
        sub     edx, dword ptr [ebp+10h]        ; EDX = row advance
        mov     bx, word ptr [ebp+10h]
        mov     word ptr data_fdps_graphics_rle_blit_src_width, bx
        mov     bx, word ptr [ebp+14h]
        mov     word ptr data_fdps_graphics_rle_blit_remaining_rows, bx
        mov     bh, byte ptr [ebp+20h]          ; BH = blit_mode
        xor     ecx, ecx
        xor     eax, eax
        cmp     bh, 0
        jne     short disp_mode1
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_passthrough
        jmp     disp_done
disp_mode1:
        cmp     bh, 1
        jne     short disp_mode2
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_remap_sprite_and_backdrop
        jmp     disp_done
disp_mode2:
        cmp     bh, 2
        jne     short disp_mode3
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_with_palette_remap
        jmp     disp_done
disp_mode3:
        cmp     bh, 3
        jne     short disp_mode4
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_recolor
        jmp     disp_done
disp_mode4:
        cmp     bh, 4
        jne     short disp_mode5
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_scaled
        jmp     disp_done
disp_mode5:
        cmp     bh, 5
        jne     short disp_mode6
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_rotated
        jmp     disp_done
disp_mode6:
        cmp     bh, 6
        jne     short disp_mode7
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_rotated_scaled
        jmp     short disp_done
        nop
        nop
        nop
disp_mode7:
        cmp     bh, 7
        jne     short disp_mode8
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_mirrored_horizontal
        jmp     short disp_done
        nop
        nop
        nop
disp_mode8:
        cmp     bh, 8
        jne     short disp_mode9
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_mirrored_vertical
        jmp     short disp_done
        nop
        nop
        nop
disp_mode9:
        cmp     bh, 9
        jne     short disp_mode10
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_translucent
        jmp     short disp_done
        nop
        nop
        nop
disp_mode10:
        cmp     bh, 0Ah
        jne     short disp_mode11
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_tint_sprite_and_backdrop
        jmp     short disp_done
        nop
        nop
        nop
disp_mode11:
        cmp     bh, 0Bh
        jne     short disp_mode12
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_tint
        jmp     short disp_done
        nop
        nop
        nop
disp_mode12:
        cmp     bh, 0Ch
        jne     short disp_done                 ; mode above 12: draw nothing
        nop
        nop
        nop
        nop
        call    fdps_rle_blit_translucent_color_range
        jmp     short disp_done
        nop
        nop
        nop
disp_done:
        pop     ebx
        pop     esi
        pop     edi
        pop     ebp
        ret
fdps_blit_dispatch endp

_TEXT   ends

        end
