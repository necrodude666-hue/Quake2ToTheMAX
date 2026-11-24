/*
BDGN 3.55aj: float -> int without the C runtime.

The software renderer is built as plain C (C_ONLY), so "(int)x" calls the compiler's _ftol2 (which loads the FPU
control word twice) and "(int)ceil(x)" calls the runtime's ceil() and then _ftol2. id's own 3.21 build avoided both
with assembly (r_drawa.asm, the inline asm of r_polyse.c). These do the same job from C and give exactly the same
number as the C expression they replace: one fistp (round to whatever mode the FPU is in), then a correction by
comparing the integer with the value. Correct in every rounding mode; the value must fit in 32 bits.
*/
#ifndef R_FASTINT_H
#define R_FASTINT_H

#if defined SW_NO_FASTINT
/* plain C */
#elif defined _MSC_VER && defined _M_IX86
static __inline int SW_RoundInt (double f)
{
	int i;
	__asm fld f
	__asm fistp i
	return i;
}
#define SW_FASTINT 1
#elif defined __GNUC__ && defined __i386__
static __inline__ int SW_RoundInt (double f)
{
	int i;
	__asm__ ("fistpl %0" : "=m" (i) : "t" (f) : "st");
	return i;
}
#define SW_FASTINT 1
#endif

#ifdef SW_FASTINT
static __inline int SW_CeilInt (double f)	/* == (int)ceil(f) */
{
	int i = SW_RoundInt (f);
	return i + ((double)i < f);
}
static __inline int SW_TruncInt (double f)	/* == (int)f */
{
	int i = SW_RoundInt (f);
	if (f >= 0)
		return i - ((double)i > f);
	return i + ((double)i < f);
}
#else
#define SW_CeilInt(f)	((int)ceil(f))
#define SW_TruncInt(f)	((int)(f))
#endif

#endif
