/*
Copyright (C) 1997-2001 Id Software, Inc.  (GPL v2 or later, same as the rest of the source)

gl_simd.c -- BDGN 3.32: SSE and MMX kernels for the OpenGL refresh, CPU detection and the
dispatch between SSE / 3DNow! / plain C.

* Q3S_*  SSE (SSE1 only, Pentium III / Athlon XP and newer) versions of the float kernels that
         gl_3dnow.c has for 3DNow!.  SSE1 has no packed integer ops, so float<->int
         conversions use the MMX registers (cvttps2pi); every kernel ends with _mm_empty().
* Q3M_*  MMX versions of two integer routines of the texture loader (mip map generation and
         the 4 tap resample).  They give bit-identical results to the C code.
* Q3D_*  The names the renderer calls.  Q3D_x() runs the SSE kernel when SSE is on, else the
         3DNow! kernel (Q3N_x); the callers only get here when GL_3DNOW() (= any vector kernel)
         is true and keep the original C code as the fallback.
Visual C++ 2005 intrinsics (<xmmintrin.h>), C89.
*/
#include <string.h>
#include <stdio.h>
#include "gl_3dnow.h"
#include "gl_perfstats.h"	/* BDGN 3.52 (WESTBECK): perf_cycles_t (Q3S_Cycles) */

#ifdef Q3D_SHIM
#include <xmmintrin.h>			/* test harness: native SSE/MMX on the build machine */
#else
#include <mmintrin.h>
#include <xmmintrin.h>
#endif

#if defined(_MSC_VER) || defined(Q3D_SHIM)
#include <emmintrin.h>
#endif

#if defined(_MSC_VER) && !defined(Q3D_SHIM)
#include <windows.h>
#endif

extern long Q_ftol( float f );	/* q_shared.c: fistp, round to nearest */

#define LD64(p)		( *(const __m64 *)(p) )
#define LD32(p)		_mm_cvtsi32_si64( *(const int *)(p) )
#define ST64(p,v)	( *(__m64 *)(p) = (v) )
#define ST32(p,v)	( *(int *)(p) = _mm_cvtsi64_si32( v ) )

/*
=============================================================================
CPU detection / mode switches
=============================================================================
*/
int q3d_use_sse = 0;
int q3d_use_3dnow = 0;
int q3d_use_mmx = 0;
extern int q3s_selftest_fail;	/* BDGN 3.51awt */

static int q3s_cpu_sse = -1;
static int q3s_cpu_mmx = -1;

#if defined(_MSC_VER) && !defined(Q3D_SHIM)

/* CPUID leaf 1 EDX, 0 when the CPU has no CPUID (486) */
static int Q3S_Features (void)
{
	int result;

	result = 0;
	__asm {
		pushfd
		pop		eax
		mov		ecx, eax
		xor		eax, 0x200000		; try to flip EFLAGS.ID
		push	eax
		popfd
		pushfd
		pop		eax
		push	ecx
		popfd						; restore the flags
		xor		eax, ecx
		jz		noid				; no CPUID instruction
		xor		eax, eax
		cpuid
		test	eax, eax
		jz		noid				; no leaf 1
		mov		eax, 1
		cpuid
		mov		result, edx
noid:
	}
	return result;
}

/* the CPU may have SSE but the operating system must save its registers (Windows 98 SE and later) */
static int Q3S_OSSupportsSSE (void)
{
	int ok;

	ok = 1;
	__try
	{
		__asm { xorps xmm0, xmm0 }
	}
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		ok = 0;
	}
	return ok;
}

#elif defined(__GNUC__) && defined(__i386__)
#include <cpuid.h>	/* gcc K6 test build: __get_cpuid checks EFLAGS.ID (486) itself */
static unsigned BDGN_CpuidEdx (unsigned leaf)
{
	unsigned a, b, c, d;
	if ( !__get_cpuid( leaf, &a, &b, &c, &d ) )	/* also 0 when the leaf is above the CPU's maximum */
		return 0;
	return d;
}
/* gcc K6 test build: CPUID leaf 1 EDX without the SSE bits (no __try for the OS test; the build is for the K6-III) */
static int Q3S_Features (void)
{
	return (int)( BDGN_CpuidEdx( 1 ) & ~( ( 1u << 25 ) | ( 1u << 26 ) ) );
}
static int Q3S_OSSupportsSSE (void)
{
	return 0;
}

#else

static int Q3S_Features (void)
{
	/* test harness on the build machine */
	return ( ( __builtin_cpu_supports( "mmx" ) ? ( 1 << 23 ) : 0 ) |
			 ( __builtin_cpu_supports( "sse" ) ? ( 1 << 25 ) | ( 1 << 24 ) : 0 ) );
}
static int Q3S_OSSupportsSSE (void)
{
	return 1;
}

#endif

/* BDGN 3.52 (WESTBECK) measurement build: the CPU's cycle counter (RDTSC, CPUID leaf 1 EDX bit 4: every Pentium, K6
   and later), for dlight_stats and weapon_stats.  0 without one. */
int Q3S_CPUHasTSC (void)
{
	static int tsc = -1;
	if ( tsc < 0 )
		tsc = ( Q3S_Features() & ( 1 << 4 ) ) ? 1 : 0;
	return tsc;
}
perf_cycles_t Q3S_Cycles (void)
{
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	unsigned lo, hi;
	if ( !Q3S_CPUHasTSC() )
		return 0;
	__asm {
		rdtsc
		mov		lo, eax
		mov		hi, edx
	}
	return ( (perf_cycles_t)hi << 32 ) | lo;
#elif defined(__GNUC__) && defined(__i386__)
	if ( !Q3S_CPUHasTSC() )
		return 0;
	return (perf_cycles_t)__builtin_ia32_rdtsc();
#else
	return 0;	/* test harness: not timed */
#endif
}

int Q3S_CPUHasMMX (void)
{
	if ( q3s_cpu_mmx < 0 )
		q3s_cpu_mmx = ( Q3S_Features() & ( 1 << 23 ) ) ? 1 : 0;
	return q3s_cpu_mmx;
}

int Q3S_CPUHasSSE (void)
{
	if ( q3s_cpu_sse < 0 )
	{
		int f = Q3S_Features();
		/* SSE (bit 25) and FXSAVE/FXRSTOR (bit 24) and MMX (23, used for the conversions) */
		q3s_cpu_sse = ( ( f & ( 1 << 25 ) ) && ( f & ( 1 << 24 ) ) && ( f & ( 1 << 23 ) ) && Q3S_OSSupportsSSE() ) ? 1 : 0;
	}
	return q3s_cpu_sse;
}

/* called once per frame (and at init) with the three cvar values */
void Q3D_UpdateModes (int sse, int n3d, int mmx)
{
	/* BDGN 3.50f: SSE kernels off (incorrect colours were reported). BDGN 3.51p: sse is only the diagnostic
	   gl_sse_test (not saved; gl_sse stays locked), so the fault can be compared with the MMX/x87 fixes of 3.51p. */
	q3d_use_sse = ( sse && Q3S_CPUHasSSE() && !q3s_selftest_fail ) ? 1 : 0;	/* 3.51bat: sse = q3d_auto_sse == 1 */
	q3d_use_3dnow = ( n3d && Q3D_CPU() ) ? 1 : 0;
	q3d_use_mmx = ( mmx && Q3S_CPUHasMMX() ) ? 1 : 0;
}

/*
=============================================================================
SSE kernels
=============================================================================
*/

/* load / store three floats (never touches the fourth).
   BDGN 3.51avt: no __m64 anywhere in the SSE kernels.  The old LDF3/STF3 used _mm_loadl_pi/_mm_storel_pi through an
   (__m64 *) and the byte loads used _mm_cvtpu8_ps (MMX).  Visual C++ 2005 can carry such values in MMX registers
   across x87 code, which shares those registers: lanes 0/1 (red, green) came out wrong and lane 2 (blue, loaded
   with _mm_load_ss) was right -- the blue picture of gl_sse_test 1 (Windows 11, RTX 5060).  Now only SSE scalar
   loads/stores, shuffles and cvttss2si. */
#define LDF3(p)		_mm_movelh_ps( _mm_unpacklo_ps( _mm_load_ss( (p) ), _mm_load_ss( (p) + 1 ) ), _mm_load_ss( (p) + 2 ) )
/* BDGN 3.51azt: STF3 through one unaligned 16-byte store into a local float[4] and three plain copies.  The VS2005
   build of the three-_mm_store_ss version lost lane 0 (self-test 3.51ayt on Windows 11: FAIL mask 0x41b, red, in
   exactly the kernels that use STF3: LightmapScale, ColorArray, ColorArrayIdx, AddDlight); _mm_storeu_ps (used by
   LerpVerts) passed. */
#define STF3(p,v)	do { float stf3_[4]; _mm_storeu_ps( stf3_, (v) ); \
					 (p)[0] = stf3_[0]; (p)[1] = stf3_[1]; (p)[2] = stf3_[2]; } while ( 0 )
/* bytes -> floats without MMX (_mm_cvtpu8_ps used the MMX registers) */
/* BDGN 3.51ayt: no _mm_set_ps / _mm_set1_ps.  Visual C++ 2005 builds them in a 16-byte stack temporary read with
   movaps, and the stack is only 4-byte aligned in the game: access violation 0xc0000005 in LightmapScale (the
   3.51axt self-test on Windows 11).  Unaligned loads (movss/movups) and shuffles only. */
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target("sse")	/* gcc build: SSE only in the SSE kernels */
#endif
static __inline __m128 Q3S_Set1 (float x)
{
	__m128 v = _mm_load_ss( &x );
	return _mm_shuffle_ps( v, v, _MM_SHUFFLE( 0, 0, 0, 0 ) );
}
/* BDGN 3.52 (WESTBECK): each byte straight into an SSE register (cvtsi2ss), then shuffles.  3.51avt-3.52u wrote the
   four floats to a stack array and read it back with one 16-byte load; a wide load right after narrow stores cannot be
   forwarded (store-forwarding stall on P3, P4, Athlon and later), which made LerpVerts about 5 times and
   LightmapScale about 3 times slower than the plain C code.  Same values (a byte converts exactly), still no MMX. */
static __inline __m128 Q3S_LoadB (const unsigned char *p, int four)
{
	__m128 z = _mm_setzero_ps();
	__m128 x = _mm_unpacklo_ps( _mm_cvtsi32_ss( z, p[0] ), _mm_cvtsi32_ss( z, p[1] ) );		/* p0 p1 0 0 */
	__m128 y = _mm_unpacklo_ps( _mm_cvtsi32_ss( z, p[2] ), four ? _mm_cvtsi32_ss( z, p[3] ) : z );	/* p2 p3 0 0 */
	return _mm_movelh_ps( x, y );
}
#define LDB3(p)		Q3S_LoadB( (p), 0 )
#define LDB4(p)		Q3S_LoadB( (p), 1 )

void Q3S_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add)
{
	__m128	sc, v, o;
	int		i;

	if ( count <= 0 )
		return;

	sc = LDF3( scale );

	for ( i = 0 ; i < count ; i++, bl += 3, lm += 3 )
	{
		v = LDB3( lm );								/* r g b 0 as floats */
		o = _mm_mul_ps( v, sc );
		if ( add )
			o = _mm_add_ps( o, LDF3( bl ) );
		STF3( bl, o );
	}
}

/* BDGN 3.58w (owner 2026-11-21: "mer FPS med SSE"): the light map scaling with four texels (12 floats) per step, all in
   whole 16-byte SSE operations. Q3S_LightmapScale above does one texel per step and writes it through a small stack
   array (STF3), three floats at a time. Here the 12 bytes become floats through a table (no int-to-float conversion
   per byte), the three scale vectors are rotated once (r g b r | g b r g | b r g b), and bl is read and written with
   three unaligned 16-byte loads and stores. SSE1 only (Pentium III, VIA C3 Nehemiah, Athlon XP when SSE is used),
   no MMX, no __m64. Checked against the plain code at start (Q3S_CheckLightmap4); gl_sse_packed 0 turns it off.
   Gain TEORETISK ANALYS until the benchmark case "gl_sse_packed = 0" has run. */
static float q3s_bytef[256];
static int q3s_bytef_ready;
static void Q3S_ByteTable (void)
{
	int i;
	for ( i = 0 ; i < 256 ; i++ )
		q3s_bytef[i] = (float)i;
	q3s_bytef_ready = 1;
}
void Q3S_LightmapScale4 (float *bl, const unsigned char *lm, int count, const float *scale, int add)
{
	__m128	s0, s1, s2, a, b, c;
	const float *t = q3s_bytef;
	int		i, n4;

	if ( count <= 0 )
		return;
	if ( !q3s_bytef_ready )
		Q3S_ByteTable();
	s0 = _mm_set_ps( scale[0], scale[2], scale[1], scale[0] );	/* lanes 0..3: r g b r */
	s1 = _mm_set_ps( scale[1], scale[0], scale[2], scale[1] );	/* g b r g */
	s2 = _mm_set_ps( scale[2], scale[1], scale[0], scale[2] );	/* b r g b */
	n4 = count >> 2;
	for ( i = 0 ; i < n4 ; i++, bl += 12, lm += 12 )
	{
		a = _mm_mul_ps( _mm_set_ps( t[lm[3]], t[lm[2]], t[lm[1]], t[lm[0]] ), s0 );
		b = _mm_mul_ps( _mm_set_ps( t[lm[7]], t[lm[6]], t[lm[5]], t[lm[4]] ), s1 );
		c = _mm_mul_ps( _mm_set_ps( t[lm[11]], t[lm[10]], t[lm[9]], t[lm[8]] ), s2 );
		if ( add )
		{
			a = _mm_add_ps( a, _mm_loadu_ps( bl ) );
			b = _mm_add_ps( b, _mm_loadu_ps( bl + 4 ) );
			c = _mm_add_ps( c, _mm_loadu_ps( bl + 8 ) );
		}
		_mm_storeu_ps( bl, a );
		_mm_storeu_ps( bl + 4, b );
		_mm_storeu_ps( bl + 8, c );
	}
	for ( i = n4 << 2 ; i < count ; i++, bl += 3, lm += 3 )	/* the last 1-3 texels */
	{
		if ( add )
		{
			bl[0] += t[lm[0]] * scale[0];
			bl[1] += t[lm[1]] * scale[1];
			bl[2] += t[lm[2]] * scale[2];
		}
		else
		{
			bl[0] = t[lm[0]] * scale[0];
			bl[1] = t[lm[1]] * scale[1];
			bl[2] = t[lm[2]] * scale[2];
		}
	}
}

void Q3S_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color)
{
	int		sdv[Q3D_MAXBLOCK], tdv[Q3D_MAXBLOCK];
	int		s, t, td, sd, dist, imin;
	float	ftacc, fsacc;
	__m128	col, w;

	if ( smax > Q3D_MAXBLOCK ) smax = Q3D_MAXBLOCK;
	if ( tmax > Q3D_MAXBLOCK ) tmax = Q3D_MAXBLOCK;

	for ( t = 0, ftacc = 0 ; t < tmax ; t++, ftacc += 16 )
	{
		td = (int)( local1 - ftacc );
		if ( td < 0 )
			td = -td;
		tdv[t] = td;
	}
	for ( s = 0, fsacc = 0 ; s < smax ; s++, fsacc += 16 )
	{
		sd = Q_ftol( local0 - fsacc );
		if ( sd < 0 )
			sd = -sd;
		sdv[s] = sd;
	}

	imin = (int)fminlight;
	if ( (float)imin < fminlight )
		imin++;

	col = LDF3( color );

	for ( t = 0 ; t < tmax ; t++ )
	{
		td = tdv[t];
		for ( s = 0 ; s < smax ; s++, pfBL += 3 )
		{
			sd = sdv[s];
			if ( sd > td )
				dist = sd + ( td >> 1 );
			else
				dist = td + ( sd >> 1 );

			if ( dist < imin )
			{
				w = Q3S_Set1( frad - (float)dist );
				STF3( pfBL, _mm_add_ps( LDF3( pfBL ), _mm_mul_ps( w, col ) ) );
			}
		}
	}
}

void Q3S_RoundFloats (int *out, const float *in, int n)
{
	__m128	half;

	half = Q3S_Set1( 0.5f );

	for ( ; n > 0 ; n--, in++, out++ )		/* BDGN 3.51avt: scalar cvttss2si only (no MMX cvttps2pi) */
		*out = _mm_cvtt_ss2si( _mm_add_ss( _mm_load_ss( in ), half ) );
}

void Q3S_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n)
{
	__m128	sl;
	int		i;

	sl = LDF3( shadelight );
	for ( i = 0 ; i < n ; i++, out += 3 )
		STF3( out, _mm_mul_ps( Q3S_Set1( shadedots[ verts[i*4+3] ] ), sl ) );
}

void Q3S_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n)
{
	__m128	sl;
	int		i;

	sl = LDF3( shadelight );
	for ( i = 0 ; i < n ; i++, out += 3 )
		STF3( out, _mm_mul_ps( Q3S_Set1( shadedots[ verts[xyz[i]*4+3] ] ), sl ) );
}

void Q3S_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n)
{
	__m128	sl, v;
	int		i, r, g, b;

	sl = _mm_mul_ps( LDF3( shadelight ), Q3S_Set1( 255.0f ) );

	for ( i = 0 ; i < n ; i++, out += 4 )
	{	/* BDGN 3.51avt: scalar truncation and clamping, same bytes as the old MMX packs (no MMX state) */
		v = _mm_mul_ps( Q3S_Set1( shadedots[ verts[xyz[i]*4+3] ] ), sl );
		r = _mm_cvtt_ss2si( v );
		g = _mm_cvtt_ss2si( _mm_shuffle_ps( v, v, _MM_SHUFFLE( 1, 1, 1, 1 ) ) );
		b = _mm_cvtt_ss2si( _mm_movehl_ps( v, v ) );
		out[0] = (unsigned char)( r < 0 ? 0 : r > 255 ? 255 : r );
		out[1] = (unsigned char)( g < 0 ? 0 : g > 255 ? 255 : g );
		out[2] = (unsigned char)( b < 0 ? 0 : b > 255 ? 255 : b );
		out[3] = 255;
	}
}

void Q3S_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv)
{
	__m128	m, f, b, a, c;
	int		i;

	if ( nverts <= 0 )
		return;

	/* lane 3 of every constant is 0, so lerp[i*4+3] stays 0 */
	m = LDF3( move );
	f = LDF3( frontv );
	b = LDF3( backv );

	for ( i = 0 ; i < nverts ; i++, v += 4, ov += 4, lerp += 4 )
	{
		a = LDB4( v );
		c = LDB4( ov );
		_mm_storeu_ps( lerp, _mm_add_ps( _mm_add_ps( _mm_mul_ps( a, f ), _mm_mul_ps( c, b ) ), m ) );
	}
}

void Q3S_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv)
{
	__m128	m, f, b, a, c;
	int		i;

	if ( n <= 0 )
		return;

	m = LDF3( move );
	f = LDF3( frontv );
	b = LDF3( backv );

	for ( i = 0 ; i < n ; i++, lerp += 4 )
	{
		a = LDB4( v + xyz[i] * 4 );
		c = LDB4( ov + xyz[i] * 4 );
		_mm_storeu_ps( lerp, _mm_add_ps( _mm_add_ps( _mm_mul_ps( a, f ), _mm_mul_ps( c, b ) ), m ) );
	}
}

int Q3S_CullBox (const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes)
{
	int		i;

	for ( i = 0 ; i < nplanes ; i++ )
	{
		const float	*n = planes + i*4;
		int			b = signbits[i];
		float		pv[4];
		__m128		nv, m, d;

		pv[0] = ( b & 1 ) ? mins[0] : maxs[0];
		pv[1] = ( b & 2 ) ? mins[1] : maxs[1];
		pv[2] = ( b & 4 ) ? mins[2] : maxs[2];
		pv[3] = 0.0f;

		nv = _mm_loadu_ps( n );							/* nx ny nz dist */
		m = _mm_mul_ps( nv, _mm_loadu_ps( pv ) );
		d = _mm_add_ss( m, _mm_shuffle_ps( m, m, _MM_SHUFFLE( 1, 1, 1, 1 ) ) );
		d = _mm_add_ss( d, _mm_shuffle_ps( m, m, _MM_SHUFFLE( 2, 2, 2, 2 ) ) );
		/* culled if dot( normal, corner ) < dist */
		if ( !_mm_comige_ss( d, _mm_shuffle_ps( nv, nv, _MM_SHUFFLE( 3, 3, 3, 3 ) ) ) )
			return 1;
	}
	return 0;
}

void Q3S_WaterST (float *out, const float *verts, int numverts, int vertexsize,
				  float rdt, float scroll, const float *turbsin)
{
	static const float	k_scale = (float)( 256.0 / ( 2 * 3.14159265358979323846 ) );
	__m128		vmul, vadd, vscroll, vinv, p, u, sn;
	int			i, i0, i1;

	vmul = Q3S_Set1( 0.125f * k_scale );
	vadd = Q3S_Set1( rdt * k_scale + 65536.0f + 0.5f );
	vscroll = _mm_load_ss( &scroll );		/* scroll, 0, 0, 0 */
	vinv = Q3S_Set1( 1.0f / 64.0f );

	for ( i = 0 ; i < numverts ; i++, verts += vertexsize, out += 2 )
	{
		p = _mm_unpacklo_ps( _mm_load_ss( verts + 3 ), _mm_load_ss( verts + 4 ) );	/* os, ot (3.51avt: no __m64) */
		u = _mm_shuffle_ps( p, p, _MM_SHUFFLE( 0, 0, 0, 1 ) );					/* ot, os */
		/* BDGN 3.51p: SSE scalar truncation (cvttss2si) instead of MMX cvttps2pi: same result, and no MMX state in
		   this loop, where the table loads below must not meet MMX-tagged x87 registers */
		u = _mm_add_ps( _mm_mul_ps( u, vmul ), vadd );
		i0 = _mm_cvtt_ss2si( u ) & 255;													/* from ot -> s */
		i1 = _mm_cvtt_ss2si( _mm_shuffle_ps( u, u, _MM_SHUFFLE( 1, 1, 1, 1 ) ) ) & 255;	/* from os -> t */
		sn = _mm_unpacklo_ps( _mm_load_ss( turbsin + i0 ), _mm_load_ss( turbsin + i1 ) );	/* t[i0], t[i1], 0, 0 */
		p = _mm_mul_ps( _mm_add_ps( _mm_add_ps( p, sn ), vscroll ), vinv );
		_mm_store_ss( out, p );
		_mm_store_ss( out + 1, _mm_shuffle_ps( p, p, _MM_SHUFFLE( 1, 1, 1, 1 ) ) );
	}
}

#ifdef __GNUC__
#pragma GCC pop_options
#endif
/*
=============================================================================
MMX integer routines (texture loader) -- exact same results as the C code
=============================================================================
*/

/* GL_MipMap: 2x2 box filter, in place.  width/height in pixels as in GL_MipMap */
void Q3M_MipMap (unsigned char *in, int width, int height)
{
	__m64			zero, r0, r1, s;
	unsigned char	*out;
	int				i, j;

	zero = _mm_setzero_si64();
	width <<= 2;
	height >>= 1;
	out = in;
	for ( i = 0 ; i < height ; i++, in += width )
	{
		for ( j = 0 ; j < width ; j += 8, out += 4, in += 8 )
		{
			r0 = LD64( in );
			r1 = LD64( in + width );
			s = _mm_add_pi16( _mm_add_pi16( _mm_unpacklo_pi8( r0, zero ), _mm_unpackhi_pi8( r0, zero ) ),
							  _mm_add_pi16( _mm_unpacklo_pi8( r1, zero ), _mm_unpackhi_pi8( r1, zero ) ) );
			s = _mm_srli_pi16( s, 2 );
			ST32( out, _mm_packs_pu16( s, s ) );
		}
	}
	_mm_empty();
}

/* inner loop of GL_ResampleTexture for one output row */
void Q3M_ResampleRow (unsigned *out, const unsigned char *inrow, const unsigned char *inrow2,
					  const unsigned *p1, const unsigned *p2, int outwidth)
{
	__m64	zero, s;
	int		j;

	zero = _mm_setzero_si64();
	for ( j = 0 ; j < outwidth ; j++ )
	{
		s = _mm_add_pi16( _mm_add_pi16( _mm_unpacklo_pi8( LD32( inrow + p1[j] ), zero ),
										_mm_unpacklo_pi8( LD32( inrow + p2[j] ), zero ) ),
						  _mm_add_pi16( _mm_unpacklo_pi8( LD32( inrow2 + p1[j] ), zero ),
										_mm_unpacklo_pi8( LD32( inrow2 + p2[j] ), zero ) ) );
		s = _mm_srli_pi16( s, 2 );
		ST32( out + j, _mm_packs_pu16( s, s ) );
	}
	_mm_empty();
}

/*
===============
Q3M_CopyVerts

BDGN 3.42 (idea: R_CopyFaceVerts_MMX of the Quake III Brain Drain build): n vertices of VERTEXSIZE (7) floats,
three 8 byte moves and one 4 byte move per vertex.  Plain copy, gated on MMX by the caller's wrapper below.
===============
*/
void Q3M_CopyVerts (float *dst, const float *src, int n)
{
	int		i;

	/* BDGN 3.51bbt: always memcpy.  MMX runs only in the texture loader now: the per-frame MMX code (this copy, the
	   lightmap store, the sound mixing) was slower on the dual Pentium II (3.43) and the K6-III+. */
	memcpy( dst, src, n * 7 * sizeof( float ) );
	return;
	for ( i = 0 ; i < n ; i++, dst += 7, src += 7 )
	{
		ST64( dst, LD64( src ) );
		ST64( dst + 2, LD64( src + 2 ) );
		ST64( dst + 4, LD64( src + 4 ) );
		*(int *)( dst + 6 ) = *(const int *)( src + 6 );
	}
	_mm_empty();
}

/*
===============
Q3M_LightStoreRow

BDGN 3.41: the "put into texture format" step of R_BuildLightMap for one row of n texels (gl_monolightmap 0).
ibl = the rounded light values r,g,b as ints.  Same as id's loop: negative -> 0, alpha = brightest of r,g,b,
and when that exceeds 255 all four are scaled by 255/max.  The common case (everything <= 255) is two
compares, a select and two packs per texel; a texel above 255 takes id's float code, so the result is
bit-identical.
===============
*/
/* BDGN 3.51p: texels brighter than 255 are only noted in the MMX loop and stored by id's float code after the single
   _mm_empty(). The old code left MMX state and ran x87 code inside the loop, then used MMX again: MMX and x87 share
   registers and VS2005 does not treat x87 code as overwriting MMX values, so the zero constant kept in an MMX register
   could come back as garbage and zero or shift colour channels of the rest of the row (dark / tinted lightmaps). */
static void Q3M_LightStoreOver (unsigned char *dest, const int *ibl)
{
	int		r = ibl[0], g = ibl[1], bb = ibl[2], max, a;
	float	t;

	if ( r < 0 ) r = 0;
	if ( g < 0 ) g = 0;
	if ( bb < 0 ) bb = 0;
	max = ( r > g ) ? r : g;
	if ( bb > max ) max = bb;
	a = max;
	t = 255.0F / max;
	r = r*t;
	g = g*t;
	bb = bb*t;
	a = a*t;
	dest[0] = r;
	dest[1] = g;
	dest[2] = bb;
	dest[3] = a;
}

void Q3M_LightStoreRow (unsigned char *dest, const int *ibl, int n)
{
	__m64	zero, rg, b, gg, mk, mx, w;
	int		j, k, nover = 0;
	int		over[64];			/* texels above 255 in this chunk */
	unsigned char	*dest0 = dest;
	const int		*ibl0 = ibl;

	if ( n > 64 )
	{	/* rows are at most 18 texels in Quake II; longer input is done in chunks */
		Q3M_LightStoreRow( dest, ibl, 64 );
		Q3M_LightStoreRow( dest + 64*4, ibl + 64*3, n - 64 );
		return;
	}
	zero = _mm_setzero_si64();
	for ( j = 0 ; j < n ; j++, ibl += 3, dest += 4 )
	{
		rg = LD64( ibl );
		b = LD32( ibl + 2 );
		rg = _mm_and_si64( rg, _mm_cmpgt_pi32( rg, zero ) );		/* negative lights -> 0 */
		b = _mm_and_si64( b, _mm_cmpgt_pi32( b, zero ) );
		gg = _mm_unpackhi_pi32( rg, rg );						/* (g,g) */
		mk = _mm_cmpgt_pi32( rg, gg );							/* lane 0: r > g */
		mx = _mm_or_si64( _mm_and_si64( mk, rg ), _mm_andnot_si64( mk, gg ) );
		mk = _mm_cmpgt_pi32( mx, b );							/* lane 0: max(r,g) > b */
		mx = _mm_or_si64( _mm_and_si64( mk, mx ), _mm_andnot_si64( mk, b ) );

		if ( _mm_cvtsi64_si32( mx ) > 255 )
		{	/* brighter than 1.0: stored after the loop by id's float code */
			over[nover++] = j;
			continue;
		}
		w = _mm_packs_pi32( rg, _mm_unpacklo_pi32( b, mx ) );	/* words r,g,b,max */
		ST32( dest, _mm_packs_pu16( w, w ) );
	}
	_mm_empty();
	for ( k = 0 ; k < nover ; k++ )
		Q3M_LightStoreOver( dest0 + over[k]*4, ibl0 + over[k]*3 );
}

/*
=============================================================================
BDGN 3.51awt: SSE self-test, run in the game the first time gl_sse_test is switched on.  Every SSE kernel is
compared with the plain C code on fixed data, inside the Visual C++ 2005 build itself (test_simd only proves the
kernels on the build machine's compiler).  Q3S_SelfTest returns a bit per failed kernel and the first failing
channel; any failure keeps the SSE kernels off (q3s_selftest_fail).
=============================================================================
*/
int q3s_selftest_fail = 0;
char q3s_selftest_report[256];
static volatile int q3s_step = -1;	/* BDGN 3.51axt: the kernel being tested (names[] below), for a crash report */
static const char *q3s_names[] = { "LightmapScale", "LightmapScale(add)", "RoundFloats", "ColorArray", "ColorArrayIdx",
								   "ColorArrayIdxB", "LerpVerts", "LerpVertsIdx", "CullBox", "WaterST", "AddDlight" };

#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target("sse")	/* gcc build: SSE only in the SSE kernels */
#endif
static int Q3S_Near (float a, float b)
{
	float d = a - b;
	if ( d < 0 ) d = -d;
	return d <= 0.002f + 0.0005f * ( b < 0 ? -b : b );
}

int Q3S_SelfTest (void)
{
	static float	scale[3] = { 1.5f, 0.75f, 2.25f }, sl[3] = { 0.9f, 0.6f, 0.3f };
	static float	move[3] = { 10.f, -20.f, 5.f }, fv[3] = { 0.25f, 0.5f, 0.75f }, bv[3] = { 0.125f, 0.375f, 0.625f };
	float			bl[3*8], ref[3*8], dots[256], fo[4*8], fr[4*8], fin[8];
	float			pl[4*4], mins[3] = { -8, -8, -8 }, maxs[3] = { 8, 8, 8 };
	float			wv[7*4], wo[2*4], wr[2*4], tab[256], col[3] = { 100.f, 50.f, 25.f };
	float			dl[3*4*4], dr[3*4*4];
	unsigned char	lm[3*8], verts[4*8], overts[4*8], bo[4*8];
	int				xyz[8], sb[4], io[8], i, c, fail = 0, first = -1, firstc = -1;

	for ( i = 0 ; i < 24 ; i++ ) { lm[i] = (unsigned char)( 17 + i * 9 ); bl[i] = ref[i] = (float)i * 3.0f; }
	for ( i = 0 ; i < 32 ; i++ ) { verts[i] = (unsigned char)( 5 + i * 7 ); overts[i] = (unsigned char)( 250 - i * 6 ); }
	for ( i = 0 ; i < 256 ; i++ ) { dots[i] = 0.5f + (float)i / 256.0f; tab[i] = (float)( i - 128 ) * 0.05f; }
	for ( i = 0 ; i < 8 ; i++ ) { xyz[i] = 7 - i; fin[i] = (float)i * 3.3f + 0.2f; }

#define Q3S_CHECK(bit, cond, ch) if ( !(cond) ) { fail |= ( 1 << (bit) ); if ( first < 0 ) { first = (bit); firstc = (ch); } }
	/* 0: LightmapScale add 0, 1: add 1 */
	q3s_step = 0;
	Q3S_LightmapScale( bl, lm, 8, scale, 0 );
	for ( i = 0 ; i < 24 ; i++ ) Q3S_CHECK( 0, Q3S_Near( bl[i], lm[i] * scale[i%3] ), i % 3 );
	for ( i = 0 ; i < 24 ; i++ ) ref[i] = bl[i] + lm[i] * scale[i%3];
	q3s_step = 1;
	Q3S_LightmapScale( bl, lm, 8, scale, 1 );
	for ( i = 0 ; i < 24 ; i++ ) Q3S_CHECK( 1, Q3S_Near( bl[i], ref[i] ), i % 3 );
	/* 2: RoundFloats */
	q3s_step = 2;
	Q3S_RoundFloats( io, fin, 8 );
	for ( i = 0 ; i < 8 ; i++ ) Q3S_CHECK( 2, io[i] == (int)( fin[i] + 0.5f ), 0 );
	/* 3: ColorArray, 4: ColorArrayIdx, 5: ColorArrayIdxB */
	q3s_step = 3;
	Q3S_ColorArray( fo, verts, dots, sl, 8 );
	for ( i = 0 ; i < 24 ; i++ ) Q3S_CHECK( 3, Q3S_Near( fo[i], dots[verts[(i/3)*4+3]] * sl[i%3] ), i % 3 );
	q3s_step = 4;
	Q3S_ColorArrayIdx( fo, xyz, verts, dots, sl, 8 );
	for ( i = 0 ; i < 24 ; i++ ) Q3S_CHECK( 4, Q3S_Near( fo[i], dots[verts[xyz[i/3]*4+3]] * sl[i%3] ), i % 3 );
	q3s_step = 5;
	Q3S_ColorArrayIdxB( bo, xyz, verts, dots, sl, 8 );
	for ( i = 0 ; i < 8 ; i++ )
		for ( c = 0 ; c < 4 ; c++ )
		{
			int e = c == 3 ? 255 : (int)( dots[verts[xyz[i]*4+3]] * ( sl[c] * 255.0f ) );
			if ( e > 255 ) e = 255;
			Q3S_CHECK( 5, bo[i*4+c] >= e - 1 && bo[i*4+c] <= e + 1, c );
		}
	/* 6: LerpVerts, 7: LerpVertsIdx */
	q3s_step = 6;
	Q3S_LerpVerts( 8, verts, overts, fo, move, fv, bv );
	for ( i = 0 ; i < 8 ; i++ )
		for ( c = 0 ; c < 3 ; c++ )
			Q3S_CHECK( 6, Q3S_Near( fo[i*4+c], move[c] + overts[i*4+c] * bv[c] + verts[i*4+c] * fv[c] ), c );
	q3s_step = 7;
	Q3S_LerpVertsIdx( 8, xyz, verts, overts, fo, move, fv, bv );
	for ( i = 0 ; i < 8 ; i++ )
		for ( c = 0 ; c < 3 ; c++ )
			Q3S_CHECK( 7, Q3S_Near( fo[i*4+c], move[c] + overts[xyz[i]*4+c] * bv[c] + verts[xyz[i]*4+c] * fv[c] ), c );
	/* 8: CullBox (inside, then outside of the first plane) */
	for ( i = 0 ; i < 4 ; i++ ) { pl[i*4+0] = i == 0 ? 1.f : 0.f; pl[i*4+1] = i == 1 ? 1.f : 0.f; pl[i*4+2] = i >= 2 ? 1.f : 0.f; pl[i*4+3] = -100.f; sb[i] = 0; }
	q3s_step = 8;
	Q3S_CHECK( 8, Q3S_CullBox( mins, maxs, pl, sb, 4 ) == 0, 0 );
	pl[3] = 100.f;
	Q3S_CHECK( 8, Q3S_CullBox( mins, maxs, pl, sb, 4 ) == 1, 0 );
	/* 9: WaterST */
	for ( i = 0 ; i < 28 ; i++ ) wv[i] = (float)( i * 13 % 97 ) - 40.f;
	q3s_step = 9;
	Q3S_WaterST( wo, wv, 4, 7, 1.25f, 0.5f, tab );
	for ( i = 0 ; i < 4 ; i++ )
	{
		float k = (float)( 256.0 / ( 2 * 3.14159265358979323846 ) ), os = wv[i*7+3], ot = wv[i*7+4];
		int i0 = (int)( ot * ( 0.125f * k ) + ( 1.25f * k + 65536.0f + 0.5f ) ) & 255;
		int i1 = (int)( os * ( 0.125f * k ) + ( 1.25f * k + 65536.0f + 0.5f ) ) & 255;
		wr[i*2+0] = ( os + tab[i0] + 0.5f ) * ( 1.0f / 64.0f );
		wr[i*2+1] = ( ot + tab[i1] ) * ( 1.0f / 64.0f );
		Q3S_CHECK( 9, Q3S_Near( wo[i*2+0], wr[i*2+0] ), 0 );
		Q3S_CHECK( 9, Q3S_Near( wo[i*2+1], wr[i*2+1] ), 1 );
	}
	/* 10: AddDlight (4x4 block, the light in its middle) */
	for ( i = 0 ; i < 48 ; i++ ) dl[i] = dr[i] = (float)i;
	q3s_step = 10;
	Q3S_AddDlight( dl, 4, 4, 24.0f, 24.0f, 60.0f, 40.0f, col );
	{
		int s, t, sd, td, dist;
		for ( t = 0 ; t < 4 ; t++ )
			for ( s = 0 ; s < 4 ; s++ )
			{
				sd = (int)( 24.0f - s * 16 ); if ( sd < 0 ) sd = -sd;
				td = (int)( 24.0f - t * 16 ); if ( td < 0 ) td = -td;
				dist = sd > td ? sd + ( td >> 1 ) : td + ( sd >> 1 );
				if ( dist < 40 )
					for ( c = 0 ; c < 3 ; c++ )
						dr[(t*4+s)*3+c] += ( 60.0f - dist ) * col[c];
			}
		for ( i = 0 ; i < 48 ; i++ ) Q3S_CHECK( 10, Q3S_Near( dl[i], dr[i] ), i % 3 );
	}
#undef Q3S_CHECK
	(void)fr;
	{
		const char **names = q3s_names;
		static const char *chan[] = { "red/x/s", "green/y/t", "blue/z", "alpha" };
		if ( !fail )
			sprintf( q3s_selftest_report, "SSE self-test: all 11 kernels PASS" );
		else
			sprintf( q3s_selftest_report, "SSE self-test: FAIL mask 0x%03x, first %s (%s) -> SSE kernels stay off",
					 fail, names[first], chan[firstc < 0 ? 0 : firstc] );
	}
	q3s_step = -1;
	return fail;
}

#ifdef __GNUC__
#pragma GCC pop_options
#endif
/* BDGN 3.51axt: the self-test inside structured exception handling (Visual C++).  The 3.51awt test crashed the game
   (Windows 11); a crash in a kernel is now caught, reported with the kernel and the exception code, and SSE stays off. */
int Q3S_SafeSelfTest (void)
{
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	unsigned long code = 0;
	int fail = 0;
	__try
	{
		fail = Q3S_SelfTest();
	}
	__except ( code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER )
	{
		_mm_empty();
		sprintf( q3s_selftest_report, "SSE self-test: CRASH in %s, exception 0x%08lx -> SSE kernels stay off",
				 q3s_step >= 0 && q3s_step <= 10 ? q3s_names[q3s_step] : "setup", code );
		q3s_step = -1;
		return 0x7ff;
	}
	return fail;
#else
	return Q3S_SelfTest();
#endif
}

/*
=============================================================================
BDGN 3.51bat: MMX self-test (same idea as the SSE one).  The MMX routines are compared byte for byte with id's C
code: GL_MipMap, the GL_ResampleTexture row, the lightmap store (with texels above 255 and negative ones) and the
vertex copy.  Q3M_SafeSelfTest runs it inside __try/__except.  Any failure or crash keeps MMX off.
=============================================================================
*/
char q3m_selftest_report[256];

static int Q3M_SelfTest (void)
{
	unsigned char	img[8*8*4], ref[8*8*4], row0[16*4], row1[16*4], dst[20*4], dref[20*4];
	unsigned		out[8], oref[8], p1[8], p2[8];
	int				ibl[20*3], i, j, c, fail = 0;
	float			vs[3*7], vd[3*7];

	/* 0: MipMap 8x8 -> 4x4 (in place) */
	for ( i = 0 ; i < 8*8*4 ; i++ ) img[i] = ref[i] = (unsigned char)( ( i * 37 + 11 ) & 255 );
	{
		unsigned char *in = ref, *o = ref;
		int w = 8 << 2, h = 8 >> 1;
		for ( i = 0 ; i < h ; i++, in += w )
			for ( j = 0 ; j < w ; j += 8, o += 4, in += 8 )
				for ( c = 0 ; c < 4 ; c++ )
					o[c] = ( in[c] + in[c+4] + in[w+c] + in[w+c+4] ) >> 2;
	}
	Q3M_MipMap( img, 8, 8 );
	if ( memcmp( img, ref, 4*4*4 ) ) fail |= 1;
	/* 1: ResampleRow */
	for ( i = 0 ; i < 16*4 ; i++ ) { row0[i] = (unsigned char)( i * 13 + 7 ); row1[i] = (unsigned char)( 255 - i * 11 ); }
	for ( j = 0 ; j < 8 ; j++ ) { p1[j] = 4 * ( j * 2 ); p2[j] = 4 * ( j * 2 + 1 ); }
	for ( j = 0 ; j < 8 ; j++ )
		for ( c = 0 ; c < 4 ; c++ )
			( (unsigned char *)( oref + j ) )[c] = ( row0[p1[j]+c] + row0[p2[j]+c] + row1[p1[j]+c] + row1[p2[j]+c] ) >> 2;
	Q3M_ResampleRow( out, row0, row1, p1, p2, 8 );
	if ( memcmp( out, oref, sizeof( out ) ) ) fail |= 2;
	/* 2: LightStoreRow (negative, normal and > 255 texels) */
	for ( i = 0 ; i < 20*3 ; i++ ) ibl[i] = ( i * 53 ) % 400 - 40;
	for ( j = 0 ; j < 20 ; j++ )
	{
		int r = ibl[j*3], g = ibl[j*3+1], b = ibl[j*3+2], max, a;
		if ( r < 0 ) r = 0;
		if ( g < 0 ) g = 0;
		if ( b < 0 ) b = 0;
		max = r > g ? r : g;
		if ( b > max ) max = b;
		a = max;
		if ( max > 255 )
		{
			float t = 255.0F / max;
			r = r*t; g = g*t; b = b*t; a = a*t;
		}
		dref[j*4] = r; dref[j*4+1] = g; dref[j*4+2] = b; dref[j*4+3] = a;
	}
	(void)dst; (void)dref;	/* 3.51bbt: the MMX lightmap store is no longer used */
	/* 3: CopyVerts */
	for ( i = 0 ; i < 3*7 ; i++ ) { vs[i] = (float)i * 1.5f - 7.0f; vd[i] = 0; }
	(void)vd; (void)vs;		/* 3.51bbt: CopyVerts is a plain memcpy now */
	{
		static const char *names[] = { "MipMap", "ResampleRow", "LightStoreRow", "CopyVerts" };
		if ( !fail )
			sprintf( q3m_selftest_report, "MMX self-test: texture loader routines PASS" );
		else
		{
			for ( i = 0 ; i < 4 && !( fail & ( 1 << i ) ) ; i++ )
				;
			sprintf( q3m_selftest_report, "MMX self-test: FAIL mask 0x%x, first %s -> MMX stays off", fail, names[i] );
		}
	}
	return fail;
}

int Q3M_SafeSelfTest (void)
{
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	unsigned long code = 0;
	int fail = 0;
	__try
	{
		fail = Q3M_SelfTest();
	}
	__except ( code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER )
	{
		_mm_empty();
		sprintf( q3m_selftest_report, "MMX self-test: CRASH, exception 0x%08lx -> MMX stays off", code );
		return 0xf;
	}
	return fail;
#else
	return Q3M_SelfTest();
#endif
}

/*
=============================================================================
BDGN 3.51bat: automatic SSE / MMX.  Q3D_AutoSIMD runs once per renderer start: with 3DNow! the 3DNow! code is used
(SSE and MMX stay off; MMX was slower than 3DNow! on the K6-III+).  Without 3DNow!, SSE and MMX are each switched
on when the CPU has them and their self-test passes.  q3d_auto_sse / q3d_auto_mmx: 1 on, 0 not supported,
2 self-test failed, 3 not used because 3DNow! is used.
=============================================================================
*/
int q3d_auto_sse = 0, q3d_auto_mmx = 0, q3d_auto_sse2 = 0;
/* BDGN 3.58w: Q3S_LightmapScale4 against the plain arithmetic, 1-13 texels, both modes (a relative tolerance: the
   plain code may keep more precision on the x87) */
static int q3s_packed_ready, q3s_packed_allowed = 1;
static int Q3S_CheckLightmap4 (void)
{
	unsigned char px[3*13+1];
	float out[3*13+2], ref[3*13+2], d;
	static const float scale[3] = { 1.5f, 0.75f, 2.25f };
	int count, i, add;
	for ( i = 0 ; i < 3*13+1 ; i++ ) px[i] = (unsigned char)( i * 53 + 7 );
	for ( count = 1 ; count <= 13 ; count++ )
		for ( add = 0 ; add <= 1 ; add++ )
		{
			for ( i = 0 ; i < 3*13+2 ; i++ ) out[i] = ref[i] = (float)i * 0.5f;
			for ( i = 0 ; i < count * 3 ; i++ )
				ref[i+1] = ( add ? ref[i+1] : 0.0f ) + (float)px[i+1] * scale[i%3];
			Q3S_LightmapScale4( out + 1, px + 1, count, scale, add );
			for ( i = 0 ; i < 3*13+2 ; i++ )
			{
				d = out[i] - ref[i];
				if ( d < 0 ) d = -d;
				if ( d > 0.001f * ( ref[i] < 0 ? -ref[i] : ref[i] ) + 0.0001f )
					return 0;	/* also catches a write outside the 3*count floats */
			}
		}
	return 1;
}
void Q3D_SetSSEPacked (int enabled)
{
	q3s_packed_allowed = enabled;
}
int Q3D_SSEPackedEnabled (void)
{
	return q3s_packed_ready && q3s_packed_allowed && q3d_use_sse;
}

static int q3d_sse2_lightmap_ready;
static int q3d_sse2_allowed=1;

#if defined(_MSC_VER) || defined(Q3D_SHIM)
static __inline __m128 Q3S2_LightBytes(const unsigned char *in,__m128i zero)
{
	int packed;
	__m128i bytes;
	memcpy(&packed,in,sizeof(packed));
	bytes=_mm_cvtsi32_si128(packed);
	return _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(bytes,zero),zero));
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
static void Q3S2_LightmapScale(float *out,const unsigned char *in,int count,const float *scale,int add)
{
	float scales[12];
	__m128 sc0,sc1,sc2;
	__m128i zero=_mm_setzero_si128();
	int i;
	for(i=0;i<12;i++) scales[i]=scale[i%3];
	sc0=_mm_loadu_ps(scales);
	sc1=_mm_loadu_ps(scales+4);
	sc2=_mm_loadu_ps(scales+8);
	for(;count>=4;count-=4,in+=12,out+=12) {
		__m128 a=_mm_mul_ps(Q3S2_LightBytes(in,zero),sc0);
		__m128 b=_mm_mul_ps(Q3S2_LightBytes(in+4,zero),sc1);
		__m128 c=_mm_mul_ps(Q3S2_LightBytes(in+8,zero),sc2);
		if(add) {
			a=_mm_add_ps(a,_mm_loadu_ps(out));
			b=_mm_add_ps(b,_mm_loadu_ps(out+4));
			c=_mm_add_ps(c,_mm_loadu_ps(out+8));
		}
		_mm_storeu_ps(out,a);
		_mm_storeu_ps(out+4,b);
		_mm_storeu_ps(out+8,c);
	}
	for(i=0;i<count*3;i++) {
		float value=in[i]*scale[i%3];
		out[i]=add?out[i]+value:value;
	}
}

static int Q3S2_CheckLightmap(void)
{
	unsigned char pixels[28];
	float actual[29],expected[29];
	static const float scale[3]={.5f,1.25f,2.125f};
	int count,i,add;
	for(i=0;i<28;i++) pixels[i]=(unsigned char)(i*47+19);
	for(count=1;count<=9;count++)
		for(add=0;add<=1;add++) {
			for(i=0;i<29;i++) actual[i]=expected[i]=(float)i*.25f;
			for(i=0;i<count*3;i++)
				expected[i+1]=(add?expected[i+1]:0)+pixels[i+1]*scale[i%3];
			Q3S2_LightmapScale(actual+1,pixels+1,count,scale,add);
			if(memcmp(actual,expected,sizeof(actual))) return 0;
		}
	return 1;
}

/* Box-filter four RGBA texels at a time. Keep the scalar filter's floor rounding. */
#if defined(_MSC_VER)
__declspec(noinline)
#endif
static void Q3S2_MipMap(unsigned char *pixels,int width,int height)
{
	const int stride=width*4;
	unsigned char *out=pixels;
	__m128i zero=_mm_setzero_si128();
	int y,x;
	for (y=0;y<height/2;y++) {
		const unsigned char *row=pixels+y*stride*2;
		for (x=0;x<stride;x+=16,out+=8) {
			__m128i a=_mm_loadu_si128((const __m128i *)(row+x));
			__m128i b=_mm_loadu_si128((const __m128i *)(row+x+stride));
			__m128i lo=_mm_add_epi16(_mm_unpacklo_epi8(a,zero),_mm_unpacklo_epi8(b,zero));
			__m128i hi=_mm_add_epi16(_mm_unpackhi_epi8(a,zero),_mm_unpackhi_epi8(b,zero));
			lo=_mm_add_epi16(lo,_mm_srli_si128(lo,8));
			hi=_mm_add_epi16(hi,_mm_srli_si128(hi,8));
			lo=_mm_srli_epi16(_mm_unpacklo_epi64(lo,hi),2);
			_mm_storel_epi64((__m128i *)out,_mm_packus_epi16(lo,zero));
		}
	}
}

static int Q3S2_CheckMipMap(void)
{
	unsigned char src[16*8*4+1],out[16*8*4+1];
	unsigned int state=13;
	int i,x,y,c,width;
	for(i=0;i<(int)sizeof(src);i++) {
		state=1664525u*state+1013904223u;
		src[i]=(unsigned char)(state>>24);
	}
	for(width=4;width<=16;width+=4) {
		memcpy(out,src,sizeof(out));
		Q3S2_MipMap(out+1,width,8);
		for(y=0;y<4;y++)
			for(x=0;x<width/2;x++)
				for(c=0;c<4;c++) {
					int offset=1+((y*2)*width+x*2)*4+c;
					int expected=(src[offset]+src[offset+4]+src[offset+width*4]+src[offset+width*4+4])/4;
					if(out[1+(y*width/2+x)*4+c]!=expected) return 0;
				}
		if(out[0]!=src[0] || memcmp(out+1+width*8,src+1+width*8,sizeof(out)-1-width*8)) return 0;
	}
	return 1;
}
#endif

int Q3D_SSE2LightmapEnabled(void)
{
	return q3d_sse2_allowed && q3d_sse2_lightmap_ready && q3d_use_sse;
}

void Q3D_SetSSE2MipMap(int enabled)
{
	q3d_sse2_allowed=enabled;
}

int Q3S2_TryMipMap(unsigned char *pixels,int width,int height)
{
#if defined(_MSC_VER) || defined(Q3D_SHIM)
	if (q3d_sse2_allowed && q3d_auto_sse2==1 && q3d_use_sse && width>=4 && !(width&3) && height>=2 && !(height&1)) {
		Q3S2_MipMap(pixels,width,height);
		return 1;
	}
#else
	(void)pixels; (void)width; (void)height;
#endif
	return 0;
}

void Q3D_AutoSIMD (void (*print)( const char *line ))
{
	int has3d = Q3D_CPU() ? 1 : 0;

	q3d_auto_sse2=0;
	q3d_sse2_lightmap_ready=0;
	q3s_selftest_fail = 0;
	if ( !Q3S_CPUHasSSE() )
		q3d_auto_sse = 0;
	else
	{
		q3s_selftest_fail = Q3S_SafeSelfTest();
		q3d_auto_sse = q3s_selftest_fail ? 2 : 1;
		print( q3s_selftest_report );
	}
	q3s_packed_ready = 0;	/* BDGN 3.58w */
	if ( q3d_auto_sse == 1 )
	{
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
		__try { q3s_packed_ready = Q3S_CheckLightmap4(); }
		__except ( EXCEPTION_EXECUTE_HANDLER ) { q3s_packed_ready = 0; }
#else
		q3s_packed_ready = Q3S_CheckLightmap4();
#endif
		print( q3s_packed_ready ? "SSE self-test: packed lightmap scaling (4 texels) PASS" :
		                          "SSE self-test: packed lightmap scaling failed; fallback enabled" );
	}
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	if (q3d_auto_sse==1 && (Q3S_Features()&(1<<26))) {
		__try { q3d_auto_sse2=Q3S2_CheckMipMap()?1:2; }
		__except(EXCEPTION_EXECUTE_HANDLER) { q3d_auto_sse2=2; }
		print(q3d_auto_sse2==1?"SSE2 self-test: texture mipmaps PASS":
		                        "SSE2 self-test: texture mipmaps failed; fallback enabled");
		__try { q3d_sse2_lightmap_ready=Q3S2_CheckLightmap(); }
		__except(EXCEPTION_EXECUTE_HANDLER) { q3d_sse2_lightmap_ready=0; }
		print(q3d_sse2_lightmap_ready?"SSE2 self-test: lightmap scaling PASS":
		                              "SSE2 self-test: lightmap scaling failed; fallback enabled");
	}
#endif
	if ( !Q3S_CPUHasMMX() )
		q3d_auto_mmx = 0;
	else if ( has3d )
		q3d_auto_mmx = 3;
	else
	{
		q3d_auto_mmx = Q3M_SafeSelfTest() ? 2 : 1;
		print( q3m_selftest_report );
	}
}

/*
=============================================================================
Dispatch: SSE when switched on, else 3DNow!
=============================================================================
*/
void Q3D_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add)
{
#if defined(_MSC_VER) || defined(Q3D_SHIM)
	if (Q3D_SSE2LightmapEnabled() && count>=4) {
		Q3S2_LightmapScale(bl,lm,count,scale,add);
		return;
	}
#endif
	if ( Q3D_SSEPackedEnabled() && count >= 4 ) {	/* BDGN 3.58w */
		Q3S_LightmapScale4( bl, lm, count, scale, add );
		return;
	}
	if ( q3d_use_sse ) Q3S_LightmapScale( bl, lm, count, scale, add ); else Q3N_LightmapScale( bl, lm, count, scale, add );
}
void Q3D_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color)
{
	if ( q3d_use_sse ) Q3S_AddDlight( pfBL, smax, tmax, local0, local1, frad, fminlight, color );
	else Q3N_AddDlight( pfBL, smax, tmax, local0, local1, frad, fminlight, color );
}
void Q3D_RoundFloats (int *out, const float *in, int n)
{
	if ( q3d_use_sse ) Q3S_RoundFloats( out, in, n ); else Q3N_RoundFloats( out, in, n );
}
void Q3D_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n)
{
	if ( q3d_use_sse ) Q3S_ColorArray( out, verts, shadedots, shadelight, n ); else Q3N_ColorArray( out, verts, shadedots, shadelight, n );
}
void Q3D_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv)
{
	if ( q3d_use_sse ) Q3S_LerpVerts( nverts, v, ov, lerp, move, frontv, backv ); else Q3N_LerpVerts( nverts, v, ov, lerp, move, frontv, backv );
}
void Q3D_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv)
{
	if ( q3d_use_sse ) Q3S_LerpVertsIdx( n, xyz, v, ov, lerp, move, frontv, backv ); else Q3N_LerpVertsIdx( n, xyz, v, ov, lerp, move, frontv, backv );
}
void Q3D_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n)
{
	if ( q3d_use_sse ) Q3S_ColorArrayIdx( out, xyz, verts, shadedots, shadelight, n ); else Q3N_ColorArrayIdx( out, xyz, verts, shadedots, shadelight, n );
}
void Q3D_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n)
{
	if ( q3d_use_sse ) Q3S_ColorArrayIdxB( out, xyz, verts, shadedots, shadelight, n ); else Q3N_ColorArrayIdxB( out, xyz, verts, shadedots, shadelight, n );
}
int Q3D_CullBox (const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes)
{
	if ( q3d_use_sse ) return Q3S_CullBox( mins, maxs, planes, signbits, nplanes );
	return Q3N_CullBox( mins, maxs, planes, signbits, nplanes );
}
void Q3D_WaterST (float *out, const float *verts, int numverts, int vertexsize,
				  float rdt, float scroll, const float *turbsin)
{
	if ( q3d_use_sse ) Q3S_WaterST( out, verts, numverts, vertexsize, rdt, scroll, turbsin );
	else Q3N_WaterST( out, verts, numverts, vertexsize, rdt, scroll, turbsin );
}
