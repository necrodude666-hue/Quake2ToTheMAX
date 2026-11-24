/*
q3e_cpuinfo.h -- BDGN 3.51p: decoding of the CPUID registers for bdgn_status (no instructions are executed here).

Q3E_ReadCPUID (q3e_3dnow.c) collects the registers behind the EFLAGS.ID check (a 486 has no CPUID) and only reads
leaves the CPU reports. This header turns them into vendor, family/model/stepping and feature flags. These are
hardware capabilities: whether the operating system saves the SSE registers is tested separately (Q3E_CPUHasSSE),
and how many processors Windows gives the game comes from Windows, not from CPUID. "htt" / "logical_per_package"
(CPUID 1 EDX bit 28, EBX bits 16-23) only say the package has room for more than one logical processor: that is
Hyper-Threading or several cores (also AMD dual-core), and not what is enabled.
No engine code, so it is tested on its own (test/cpuinfo_test.c).
*/
#ifndef Q3E_CPUINFO_H
#define Q3E_CPUINFO_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef struct
{
	int				has_cpuid;
	unsigned		max_leaf, vendor_ebx, vendor_edx, vendor_ecx;	/* leaf 0 */
	unsigned		leaf1_eax, leaf1_ebx, leaf1_ecx, leaf1_edx;		/* leaf 1 (valid when max_leaf >= 1) */
	unsigned		leaf7_ebx;
	unsigned		max_ext, ext1_edx;								/* 0x80000000, 0x80000001 EDX (valid when max_ext >= 0x80000001) */
	unsigned		ext6_ecx;										/* BDGN 3.58w: 0x80000006 ECX, L2 size in KB in bits 31-16 (AMD, VIA) */
} q3e_cpuid_raw_t;

/* BDGN 3.58w: CPU profiles (qcommon/common.c Com_CpuProfileClass, client/cl_main.c CL_CPUProfileApply) */
enum { CPUP_MODERN, CPUP_P5, CPUP_PMMX, CPUP_K5, CPUP_K6, CPUP_K62, CPUP_K63, CPUP_K62P, CPUP_K63P, CPUP_ATHLON,
	CPUP_ATHLONXP, CPUP_PPRO, CPUP_P2, CPUP_P3, CPUP_C3, CPUP_C3SSE, CPUP_WINCHIP, CPUP_CYRIX, CPUP_NONE };
int Com_CpuProfileClass (const q3e_cpuid_raw_t *r);
const char *Com_CpuProfileName (int cls);

typedef struct
{
	char	vendor[13];
	int		family, model, stepping;
	int		mmx, mmxext, amd3dnow, amd3dnowext, sse, sse2;
	int		sse3, ssse3, sse41, sse42, avx_hw, avx2_hw;
	int		htt, logical_per_package;
} q3e_cpuinfo_t;

static void Q3E_DecodeCPU (const q3e_cpuid_raw_t *r, q3e_cpuinfo_t *c)
{
	unsigned	eax, base;

	memset (c, 0, sizeof(*c));
	c->logical_per_package = 1;
	if (!r->has_cpuid)
	{
		strcpy (c->vendor, "unknown");
		c->family = 4;				/* no CPUID: 486 class */
		return;
	}
	memcpy (c->vendor, &r->vendor_ebx, 4);
	memcpy (c->vendor + 4, &r->vendor_edx, 4);
	memcpy (c->vendor + 8, &r->vendor_ecx, 4);
	c->vendor[12] = 0;
	if (r->max_leaf < 1)
	{
		c->family = 5;				/* CPUID without leaf 1: early Pentium class, no features reported */
		return;
	}
	eax = r->leaf1_eax;
	c->stepping = eax & 15;
	c->model = (eax >> 4) & 15;
	base = (eax >> 8) & 15;
	c->family = base;
	if (base == 15)
		c->family += (eax >> 20) & 255;
	if (base == 15 || base == 6)	/* display model: extended model bits for base family 6 and 15 (AMD family 6 has them 0) */
		c->model += ((eax >> 16) & 15) << 4;
	c->mmx = (r->leaf1_edx >> 23) & 1;
	c->sse = ((r->leaf1_edx >> 25) & 1) && ((r->leaf1_edx >> 24) & 1);	/* SSE needs FXSAVE */
	c->sse2 = c->sse && ((r->leaf1_edx >> 26) & 1);
	c->sse3 = (r->leaf1_ecx >> 0) & 1;
	c->ssse3 = (r->leaf1_ecx >> 9) & 1;
	c->sse41 = (r->leaf1_ecx >> 19) & 1;
	c->sse42 = (r->leaf1_ecx >> 20) & 1;
	c->avx_hw = (r->leaf1_ecx >> 28) & 1;
	c->avx2_hw = c->avx_hw && ((r->leaf7_ebx >> 5) & 1);
	c->htt = (r->leaf1_edx >> 28) & 1;
	if (c->htt)
	{
		c->logical_per_package = (r->leaf1_ebx >> 16) & 255;
		if (c->logical_per_package < 1)
			c->logical_per_package = 1;
	}
	c->mmxext = c->sse;				/* SSE CPUs have the integer MMX extensions (pavgb, pmulhuw, ...) */
	if (r->max_ext >= 0x80000001u)
	{
		c->amd3dnow = (r->ext1_edx >> 31) & 1;
		c->amd3dnowext = (r->ext1_edx >> 30) & 1;
		if (!strcmp (c->vendor, "AuthenticAMD") && ((r->ext1_edx >> 22) & 1))
			c->mmxext = 1;			/* AMD MMX extensions (Athlon) */
	}
}

static void Q3E_Append (char *out, int size, const char *fmt, ...)
{
	int		used;
	va_list	args;

	if (size <= 0)
		return;
	used = (int)strlen (out);
	if (used >= size - 1)
		return;
	va_start (args, fmt);
#ifdef _MSC_VER
	_vsnprintf (out + used, size - used - 1, fmt, args);
#else
	vsnprintf (out + used, size - used - 1, fmt, args);
#endif
	va_end (args);
	out[size - 1] = 0;
}

/* e.g. "AuthenticAMD family 5 model 9 stepping 1: MMX 3DNow!" */
static void Q3E_DescribeCPU (const q3e_cpuinfo_t *c, char *out, int size)
{
	if (size <= 0)
		return;
	out[0] = 0;
	Q3E_Append (out, size, "%s family %d model %d stepping %d:", c->vendor, c->family, c->model, c->stepping);
	if (c->mmx) Q3E_Append (out, size, " MMX");
	if (c->mmxext) Q3E_Append (out, size, " MMX+");
	if (c->amd3dnow) Q3E_Append (out, size, " 3DNow!");
	if (c->amd3dnowext) Q3E_Append (out, size, " 3DNow!+");
	if (c->sse) Q3E_Append (out, size, " SSE");
	if (c->sse2) Q3E_Append (out, size, " SSE2");
	if (c->sse3) Q3E_Append (out, size, " SSE3");
	if (c->ssse3) Q3E_Append (out, size, " SSSE3");
	if (c->sse41) Q3E_Append (out, size, " SSE4.1");
	if (c->sse42) Q3E_Append (out, size, " SSE4.2");
	if (c->avx_hw) Q3E_Append (out, size, " AVX(hw)");
	if (c->avx2_hw) Q3E_Append (out, size, " AVX2(hw)");
	if (c->htt) Q3E_Append (out, size, ", %d logical per package", c->logical_per_package);
	if (!c->mmx && !c->sse && !c->amd3dnow) Q3E_Append (out, size, " none of MMX/3DNow!/SSE");
}

#endif
