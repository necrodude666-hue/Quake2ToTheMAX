; BDGN 3.60c: LoadPCX decode loop for AMD's ref_glam.dll / ref_swam.dll (replaces id's loop in place).
; id's loop ignored bytes_per_line and wrote runs without a bound: PCX files with an odd width (bytes_per_line =
; width + 1, 107 skins of bdgn_simplemodels.pak) wrote past the picture on the heap and came out skewed. This one reads
; bytes_per_line bytes per row, keeps only the first width, never writes outside the picture and stops at the end
; of the file. Same registers / stack slots in and out as the original loop.
bits 32
org START
	mov edx, [esp+DPTR]         ; data pointer (raw + 128)
	movzx eax, word [ebp+8]
	inc eax
	mov [esp+DPTR], eax          ; width (the slot is free from here)
	movzx ecx, word [ebp+0x42]   ; bytes_per_line
	cmp ecx, eax
	jae .b
	mov ecx, eax
.b:	mov [esp+YS], ecx            ; bytes per row to read
	mov ebx, [esp+LEN]
	add ebx, ebp                 ; end of the file
	movzx esi, word [ebp+0xa]
	inc esi                      ; rows
	mov edi, [esp+PIX]           ; row pointer
.row:
	xor ecx, ecx
.next:
	cmp ecx, [esp+YS]
	jae .rowend
	cmp edx, ebx
	jae .done
	movzx eax, byte [edx]
	inc edx
	mov ebp, 1
	cmp al, 0xc0
	jb .run
	mov ebp, eax
	and ebp, 0x3f
	cmp edx, ebx
	jae .done
	movzx eax, byte [edx]
	inc edx
.run:
	test ebp, ebp
	jz .next
.put:
	cmp ecx, [esp+DPTR]
	jae .skip
	mov [edi+ecx], al
.skip:
	inc ecx
	dec ebp
	jnz .put
	jmp .next
.rowend:
	add edi, [esp+DPTR]
	dec esi
	jnz .row
.done:
	jmp END
	times (END-START)-($-$$) db 0xcc
