# BDGN 3.60d: builds the patched AMD / Rendition renderers from the earlier files. Needs nasm and Python 3 + pefile.
#   python3 -I build_amd_360d.py
# In:  ../ref_glam_patched/ref_glam_3.51l.dll, ../ref_swam_patched/ref_swam_3.53b.dll, ref_v1k_original.dll (BDGN 3.60c and earlier)
# Out: ref_glam.dll, ref_swam.dll, ref_v1k.dll here (copy them to Quake2\)
import hashlib, os, struct, subprocess, sys
import pefile
HERE = os.path.dirname(os.path.abspath(__file__))
def nasm(src, out, **d):
	subprocess.check_call(['nasm', '-f', 'bin'] + ['-D%s=%s' % kv for kv in d.items()] + [os.path.join(HERE, src), '-o', out])
	return open(out, 'rb').read()
def build(src, dst, patches, strings=()):
	pe = pefile.PE(src)
	for va, data in patches:
		pe.set_bytes_at_offset(pe.get_offset_from_rva(va - pe.OPTIONAL_HEADER.ImageBase), data)
	b = bytearray(pe.write())
	for old, new in strings:
		assert len(old) == len(new) and b.count(old) == 1, old
		b = b.replace(old, new)
	pe = pefile.PE(data=bytes(b))
	pe.OPTIONAL_HEADER.CheckSum = 0
	pe.OPTIONAL_HEADER.CheckSum = pe.generate_checksum()
	pe.write(dst)
	print(os.path.basename(dst), hashlib.md5(open(dst, 'rb').read()).hexdigest())
tmp = os.path.join(HERE, '_tmp.bin')
# ref_glam (AMD 3DNow! OpenGL): LoadPCX decode loop
glam = nasm('pcxfix.asm', tmp, START='0x100022af', END='0x10002365', DPTR='0x28', YS='0x2c', LEN='0x18', PIX='0x14')
build(os.path.join(HERE, '..', 'ref_glam_patched', 'ref_glam_3.51l.dll'), os.path.join(HERE, 'ref_glam.dll'),
	[(0x100022af, glam)], [(b'v3.51l\0', b'v3.60d\0'), (b'renderer v3.51l', b'renderer v3.60d')])
# ref_swam (AMD 3DNow! software): LoadPCX decode loop + r_pagein gate on Com_PageInMemory (code in .bdgns free tail)
swam = nasm('pcxfix.asm', tmp, START='0x10005369', END='0x1000541f', DPTR='0x10', YS='0x2c', LEN='0x1c', PIX='0x18')
cave = nasm('swam_pagein.asm', tmp, CAVE='0x1016b600', BACK='0x10001d65', RI_CVARGET='0x100eaf68')
jmp = b'\xe9' + struct.pack('<i', 0x1016b600 - (0x10001d60 + 5))
build(os.path.join(HERE, '..', 'ref_swam_patched', 'ref_swam_3.53b.dll'), os.path.join(HERE, 'ref_swam.dll'),
	[(0x10005369, swam), (0x10001d60, jmp), (0x1016b600, cave)],
	[(b'version\0v3.53b', b'version\0v3.60d'), (b'software v3.53b', b'software v3.60d')])
# ref_v1k (Rendition Verite): LoadPCX decode loop
v1k = nasm('pcxfix.asm', tmp, START='0x100062ff', END='0x100063b5', DPTR='0x10', YS='0x2c', LEN='0x1c', PIX='0x18')
build(os.path.join(HERE, 'ref_v1k_original.dll'), os.path.join(HERE, 'ref_v1k.dll'), [(0x100062ff, v1k)])
os.remove(tmp)
