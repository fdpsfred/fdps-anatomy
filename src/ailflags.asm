; ailflags.asm -- save EFLAGS and disable interrupts, in one 4-byte routine.
;
; AIL takes this at the top of every critical section and restores the value
; on the way out. It cannot be written in C: PUSHFD and CLI have no C
; spelling, and the routine must not touch anything else.
;
; The AIL library object references it as an external; the link script aliases
; the vendor's name onto this symbol. Register calling convention, result in
; EAX, no parameters.

        .386p

_TEXT   segment byte public use32 'CODE'
        assume  cs:_TEXT

        public  AIL_internal_isr_eflags_save_cli

AIL_internal_isr_eflags_save_cli proc near
        pushfd
        pop     eax
        cli
        ret
AIL_internal_isr_eflags_save_cli endp

_TEXT   ends

        end
