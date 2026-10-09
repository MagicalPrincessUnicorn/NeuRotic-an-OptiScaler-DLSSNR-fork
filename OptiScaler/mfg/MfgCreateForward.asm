; Preserve the genuine NGX caller while observing provider feature creation.
; r10 = retained entry, r11 = noexcept preparation/admission callback.
; The callback returns the retained trampoline slot or zero to refuse creation.
.code
NrMfgCreateForward PROC FRAME
    sub rsp,88h
    .allocstack 88h
    .endprolog
    mov [rsp+20h],rcx
    mov [rsp+28h],rdx
    mov [rsp+30h],r8
    mov [rsp+38h],r9
    movaps [rsp+40h],xmm0
    movaps [rsp+50h],xmm1
    movaps [rsp+60h],xmm2
    movaps [rsp+70h],xmm3
    mov r8,rdx
    mov rdx,rcx
    mov rcx,r10
    call r11
    mov rcx,[rsp+20h]
    mov rdx,[rsp+28h]
    mov r8,[rsp+30h]
    mov r9,[rsp+38h]
    movaps xmm0,[rsp+40h]
    movaps xmm1,[rsp+50h]
    movaps xmm2,[rsp+60h]
    movaps xmm3,[rsp+70h]
    test rax,rax
    jz refused
    add rsp,88h
    ; Explicit REX.W makes this memory-indirect tail jump recognizable by
    ; RtlVirtualUnwind even when interrupted after the stack is restored.
    DB 048h
    jmp qword ptr [rax]
refused:
    mov eax,0BAD00001h
    add rsp,88h
    ret
NrMfgCreateForward ENDP
END
