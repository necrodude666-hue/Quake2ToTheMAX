/*
win95fix.c -- BDGN 3.55x: makes a VS2005 build loadable on Windows 95.

The static C runtime of Visual Studio 2005 imports KERNEL32 IsDebuggerPresent (only used when the program is
already failing: a stack cookie error or a bad CRT parameter). Windows 95's KERNEL32 does not have it, so the
loader refuses the whole exe / dll ("... is linked to a missing export KERNEL32.DLL:IsDebuggerPresent").
This tool renames that import in the file's import table to GetConsoleCP: also no arguments, present in Windows 95,
and it returns 0 (= "no debugger") for a program without a console. Nothing else in the file changes.
It also reports other KERNEL32 imports that Windows 95 does not have (it does not change those).

Run after linking (post-build event of quake2, ref_gl, ref_soft, game): win95fix.exe <file> [<file> ...]
Exit code 0 also when there is nothing to change. Plain C89, stdio only.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long rd32 (const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned rd16 (const unsigned char *p) { return p[0] | (p[1] << 8); }

static const char *not_in_win95[] = {
	"InterlockedCompareExchange", "InterlockedExchangeAdd", "IsDebuggerPresent", "GetFileAttributesExA",
	"SwitchToThread", "TryEnterCriticalSection", "InitializeCriticalSectionAndSpinCount", "IsProcessorFeaturePresent",
	"GlobalMemoryStatusEx", "SetThreadExecutionState", "CreateWaitableTimerA", "SignalObjectAndWait",
	"GetLongPathNameA", "CopyFileExA", "EncodePointer", "DecodePointer", "FlsAlloc", "GetFileSizeEx",
	"SetFilePointerEx", "GetModuleHandleExA", "HeapSetInformation", NULL
};

/* file offset of an RVA, or -1 */
static const unsigned char *pe_hdr;	/* the file header ("PE\0\0" + 4) */
static long rva_to_ofs (const unsigned char *buf, unsigned long size, unsigned long rva)
{
	unsigned nsec = rd16 (pe_hdr + 2), opt = rd16 (pe_hdr + 16), i;
	const unsigned char *s = pe_hdr + 20 + opt;
	for (i = 0; i < nsec; i++, s += 40)
	{
		unsigned long va = rd32 (s + 12), vsize = rd32 (s + 8), raw = rd32 (s + 20), rsize = rd32 (s + 16);
		if (vsize < rsize) vsize = rsize;
		if (rva >= va && rva < va + vsize)
		{
			unsigned long ofs = raw + (rva - va);
			(void)buf;
			return ofs < size ? (long)ofs : -1;
		}
	}
	return -1;
}

static int fix_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	unsigned char *buf, *pe;
	unsigned long size, peofs, imp_rva;
	long imp, changed = 0, warned = 0;
	if (!f) { printf ("win95fix: cannot open %s\n", path); return 1; }
	fseek (f, 0, SEEK_END); size = ftell (f); fseek (f, 0, SEEK_SET);
	buf = (unsigned char *)malloc (size);
	if (!buf || fread (buf, 1, size, f) != size) { fclose (f); printf ("win95fix: cannot read %s\n", path); return 1; }
	fclose (f);
	if (size < 0x100 || buf[0] != 'M' || buf[1] != 'Z') { printf ("win95fix: %s is not an exe/dll\n", path); return 1; }
	peofs = rd32 (buf + 0x3c);
	if (peofs + 248 > size || memcmp (buf + peofs, "PE\0\0", 4) || rd16 (buf + peofs + 24) != 0x10b)
	{ printf ("win95fix: %s is not a 32-bit PE file\n", path); return 1; }
	pe = buf + peofs + 4;	/* file header */
	pe_hdr = pe;
	imp_rva = rd32 (pe + 20 + 104);	/* optional header data directory 1 (import table) */
	imp = imp_rva ? rva_to_ofs (buf, size, imp_rva) : -1;
	for (; imp >= 0 && (unsigned long)imp + 20 <= size; imp += 20)
	{
		const unsigned char *d = buf + imp;
		unsigned long names = rd32 (d), first = rd32 (d + 16), name_rva = rd32 (d + 12);
		long nofs, t;
		if (!names && !first && !name_rva) break;
		nofs = rva_to_ofs (buf, size, name_rva);
		if (nofs < 0) break;
#ifdef _MSC_VER
		if (_stricmp ((const char *)buf + nofs, "KERNEL32.dll")) continue;
#else
		if (strcasecmp ((const char *)buf + nofs, "KERNEL32.dll")) continue;
#endif
		t = rva_to_ofs (buf, size, names ? names : first);
		for (; t >= 0 && (unsigned long)t + 4 <= size; t += 4)
		{
			unsigned long th = rd32 (buf + t);
			long hn;
			char *name;
			int k;
			if (!th) break;
			if (th & 0x80000000UL) continue;	/* by ordinal */
			hn = rva_to_ofs (buf, size, th);
			if (hn < 0) continue;
			name = (char *)buf + hn + 2;
			if (!strcmp (name, "IsDebuggerPresent"))
			{	/* same length or shorter: the name is written in place, zero padded */
				memset (name, 0, strlen (name));
				strcpy (name, "GetConsoleCP");
				buf[hn] = buf[hn + 1] = 0;	/* hint: let the loader search by name */
				changed++;
				continue;
			}
			for (k = 0; not_in_win95[k]; k++)
				if (!strcmp (name, not_in_win95[k]))
				{
					printf ("win95fix: WARNING %s imports KERNEL32 %s, which Windows 95 does not have\n", path, name);
					warned++;
				}
		}
	}
	if (changed)
	{
		f = fopen (path, "r+b");
		if (!f || fwrite (buf, 1, size, f) != size) { printf ("win95fix: cannot write %s\n", path); free (buf); return 1; }
		fclose (f);
		printf ("win95fix: %s: IsDebuggerPresent -> GetConsoleCP (Windows 95)\n", path);
	}
	else
		printf ("win95fix: %s: nothing to change\n", path);
	free (buf);
	return 0;
}

int main (int argc, char **argv)
{
	int i, err = 0;
	if (argc < 2) { printf ("usage: win95fix <exe or dll> ...\n"); return 0; }
	for (i = 1; i < argc; i++)
		err |= fix_file (argv[i]);
	return err;
}
