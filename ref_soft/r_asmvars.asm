;
; r_asmvars.asm -- BDGN 3.51p: only the constants id's r_draw16.asm (D_DrawSpans16, D_DrawZSpans) and r_aclipa.asm
; (R_Alias_clip_*) need, copied from id's r_varsa.asm. r_varsa.asm itself cannot be linked into this C_ONLY build:
; it also defines _d_sdivzstepu ... _d_zrowbytes, which r_main.c defines in C here (duplicate symbols).
; Built only by the "Release ASM" configuration (ref_soft.vcproj).
;
 .386P
 .model FLAT

include qasm.inc
include d_if.inc

if	id386

_DATA SEGMENT

 public float_point5, fp_16, fp_64k
 public Float2ToThe31nd, FloatMinus2ToThe31nd

float_point5 dd 0.5
fp_64k dd 65536.0
fp_16 dd 16.0
Float2ToThe31nd dd 04f000000h
FloatMinus2ToThe31nd dd 0cf000000h

 public reciprocal_table_16, entryvec_table_16
; 1/2, 1/3, 1/4, 1/5, 1/6, 1/7, 1/8, 1/9, 1/10, 1/11, 1/12, 1/13,
; 1/14, and 1/15 in 0.32 form
reciprocal_table_16 dd 040000000h, 02aaaaaaah, 020000000h
 dd 019999999h, 015555555h, 012492492h
 dd 010000000h, 0e38e38eh, 0ccccccch, 0ba2e8bah
 dd 0aaaaaaah, 09d89d89h, 09249249h, 08888888h

 externdef Entry2_16:dword
 externdef Entry3_16:dword
 externdef Entry4_16:dword
 externdef Entry5_16:dword
 externdef Entry6_16:dword
 externdef Entry7_16:dword
 externdef Entry8_16:dword
 externdef Entry9_16:dword
 externdef Entry10_16:dword
 externdef Entry11_16:dword
 externdef Entry12_16:dword
 externdef Entry13_16:dword
 externdef Entry14_16:dword
 externdef Entry15_16:dword
 externdef Entry16_16:dword

entryvec_table_16 dd 0, Entry2_16, Entry3_16, Entry4_16
 dd Entry5_16, Entry6_16, Entry7_16, Entry8_16
 dd Entry9_16, Entry10_16, Entry11_16, Entry12_16
 dd Entry13_16, Entry14_16, Entry15_16, Entry16_16

;-------------------------------------------------------
; ASM-only variables (id's r_varsa.asm): scratch words of the span and clip routines
;-------------------------------------------------------
 public izi
izi dd 0

 public pbase, s, t, sfracf, tfracf, snext, tnext
 public spancountminus1, zi16stepu, sdivz16stepu, tdivz16stepu
 public zi8stepu, sdivz8stepu, tdivz8stepu, pz
s dd 0
t dd 0
snext dd 0
tnext dd 0
sfracf dd 0
tfracf dd 0
pbase dd 0
zi8stepu dd 0
sdivz8stepu dd 0
tdivz8stepu dd 0
zi16stepu dd 0
sdivz16stepu dd 0
tdivz16stepu dd 0
spancountminus1 dd 0
pz dd 0

 public izistep
izistep dd 0

;
; advancetable is 8 bytes, but points to the middle of that range so negative
; offsets will work
;
 public advancetable, sstep, tstep, pspantemp, counttemp, jumptemp
advancetable dd 0, 0
sstep dd 0
tstep dd 0

pspantemp dd 0
counttemp dd 0
jumptemp dd 0

 public _r_bmodelactive	; BDGN 3.55ad: for r_edgea.asm (from id's r_varsa.asm)
_r_bmodelactive dd 0	
_DATA ENDS

endif	;id386

 END
