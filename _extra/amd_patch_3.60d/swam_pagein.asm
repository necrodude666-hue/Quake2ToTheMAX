; BDGN 3.60c: r_pagein in AMD's ref_swam.dll, as in BDGN's ref_soft (r_model.c R_PageInWanted).
; Com_PageInMemory (0x10001d60) reads one byte of every 4 KB page of a model or picture at the end of a load; on low
; RAM computers that only makes Windows swap (Pentium 133 / 31 MB: 4.7-29.5 s per load, MEASURED with ref_soft).
; r_pagein: 1 = always (3.21), 0 = never, 2 = auto (default) = not when bdgn_lowram is set.
; Position independent (call/pop): no relocations needed. Integer only (no x87 / MMX / 3DNow!).
bits 32
org CAVE
	call .g
.g:	pop eax
	mov ecx, [eax + (p_pagein - .g)]
	test ecx, ecx
	jnz .have
	push eax
	push 1                              ; CVAR_ARCHIVE
	lea edx, [eax + (s_two - .g)]
	push edx
	lea edx, [eax + (s_pagein - .g)]
	push edx
	mov edx, [eax + (RI_CVARGET - .g)]
	call edx
	add esp, 12
	mov ecx, [esp]
	mov [ecx + (p_pagein - .g)], eax
	push 0
	lea edx, [ecx + (s_zero - .g)]
	push edx
	lea edx, [ecx + (s_lowram - .g)]
	push edx
	mov edx, [ecx + (RI_CVARGET - .g)]
	call edx
	add esp, 12
	pop ecx
	mov [ecx + (p_lowram - .g)], eax
	mov eax, ecx
	mov ecx, [eax + (p_pagein - .g)]
.have:
	mov ecx, [ecx + 0x14]               ; value (float bits)
	cmp ecx, 0x3f800000                 ; 1: always
	je .do
	cmp ecx, 0x40000000                 ; 2: auto
	jne .skip
	mov edx, [eax + (p_lowram - .g)]
	test edx, edx
	jz .do
	mov edx, [edx + 0x14]
	and edx, 0x7fffffff                 ; bdgn_lowram != 0 (also -0.0)
	jnz .skip
.do:
	mov eax, [esp + 8]                  ; the two instructions the jump replaced
	dec eax
	jmp BACK
.skip:
	ret
	align 4
p_pagein: dd 0
p_lowram: dd 0
s_pagein: db "r_pagein", 0
s_two: db "2", 0
s_lowram: db "bdgn_lowram", 0
s_zero: db "0", 0
