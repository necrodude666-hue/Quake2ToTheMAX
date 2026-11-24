BDGN 3.60d - the last patch round for the binary renderers (ref_glam, ref_swam, ref_v1k)

pcxfix.asm         new LoadPCX decode loop (182 bytes, in place of id's): reads bytes_per_line per row, keeps the
                   first "width" pixels, never writes outside the picture, stops at the end of the file. id's loop
                   wrote past the picture for PCX files with an odd width (107 skins of bdgn_simplemodels.pak).
swam_pagein.asm    r_pagein in ref_swam: 1 = always (3.21), 0 = never, 2 = auto (not when bdgn_lowram is set).
                   Position independent, integer only, in the free tail of the .bdgns section.
build_amd_360d.py  builds ref_glam.dll, ref_swam.dll and ref_v1k.dll from the earlier versions (needs nasm and
                   Python 3 with pefile).

Tested in an emulator (Unicorn): the PCX loop against a reference decoder (even and odd widths, runs across rows,
cut-off files, 40 real skins from bdgn_simplemodels.pak) and the r_pagein gate at two load addresses.
