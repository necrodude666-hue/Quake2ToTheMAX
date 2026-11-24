BDGN 3.55y: id Software's Pentium span code for the software renderer, prebuilt.

r_draw16.obj  <- r_draw16.asm  (id's d_draw16 / r_draw16.asm, unchanged except the public names
                                _D_DrawSpans16 -> _D_DrawSpans16_id386 and _D_DrawZSpans -> _D_DrawZSpans_id386)
r_asmvars.obj <- r_asmvars.asm (constants and scratch words those routines use, from id's r_varsa.asm)

The .obj files are COFF (Win32), assembled with JWasm 2.x (MASM compatible):
    jwasm -coff -nologo -Fo r_draw16.obj r_draw16.asm
    jwasm -coff -nologo -Fo r_asmvars.obj r_asmvars.asm
They are linked by ref_soft.vcproj (Release and Release_SSE, linker AdditionalDependencies), so Visual C++ 2005
Express, which has no ml.exe, builds the same ref_soft.dll. ml.exe gives the same result:
    ml /c /coff /Fo r_draw16.obj r_draw16.asm
r_scan.c (SW_ID386_SPANS) calls them for the plain span path; sw_asmspans 0 switches back to the C loops.
id's r_aclipa.asm is NOT used: BDGN's finalvert_t is larger than id's (the asm offsets would be wrong).

BDGN 3.55ad: two more of id's files, from id's GPL source (github.com/id-Software/Quake-2, ref_soft/):
r_surf8.obj <- r_surf8.asm  (R_DrawSurfaceBlock8_mip0-3: lit surface blocks for the surface cache; names + _id386)
r_edgea.obj <- r_edgea.asm  (R_InsertNewEdges, R_RemoveEdges, R_StepActiveU, R_GenerateSpans; names + _id386, and
                             the edge fixed point changed from 20 to 19 fraction bits: shr 19, 07FFFFh, 2^-19,
                             edge_head_u_fixed / edge_tail_u_fixed, as SW_EDGE_SHIFT in r_local.h)
Both write addresses into their own instructions (R_Surf8Patch, R_SurfacePatch); r_surf.c and r_edge.c make the code
writable (Sys_MakeCodeWriteable) and patch when the colormap / the surface array changes.
qasm.inc: the two externdef names above changed.
Not used: r_drawa.asm (R_ClipEdge switches the FPU rounding mode and leaves it in chop mode, which BDGN's C code
around it does not expect), r_polysa.asm and r_aclipa.asm (BDGN's finalvert_t is larger than id's), r_spr8.asm.
Tests: _extra/test/sw_asm_spans.

BDGN 3.55ak: three more of id's files (same source, same JWasm command):
r_polysa.obj <- r_polysa.asm (R_PolysetDrawSpans8_Opaque, R_PolysetScanLeftEdge, D_PolysetAff8Start, D_Aff8Patch;
                              names + _id386). Used by r_polyse.c (SW_ID386_MODELS) for plain opaque models: no texture
                              filter, no coloured-light remap. The colormap address is patched into the code.
r_scana.obj  <- r_scana.asm  (D_DrawTurbulent8Span + _id386): the inner loop of water (r_scan.c, SW_ID386_WATER).
r_drawa.obj  <- r_drawa.asm  (R_ClipEdge + _id386, with the edge emitter): r_rast.c, SW_ID386_CLIP. Changes: 19-bit edge
                              fixed point (Lfp_edge_one / Lfp_edge_mask), the FPU control words come from r_asmvars
                              (_sw_fpu_ceil_cw / _sw_fpu_chop_cw, made by SW_FPUSetup from the current word), two labels
                              renamed (LP1/LP2 clash with Lp1/Lp2 without case sensitivity). Leaves the FPU in chop
                              mode; r_rast.c calls SW_FPURestore after each face.
r_asmvars.asm: float_1, the FPU words, SW_FPUSetup, SW_FPURestore added.
To build without any of them: remove SW_ID386_MODELS / SW_ID386_WATER / SW_ID386_CLIP from the preprocessor definitions
of ref_soft.vcproj (the .obj files can stay in the linker list). In game: sw_asmspans 0 = all C.
Still C: particles and the model vertex transform (inline assembly of id's r_part.c / r_alias.c: Visual C++ only, and it
needs id's FPU mode handling), r_aclipa.asm (clips model triangles at the screen edge; rounds differently outside
chop mode).
