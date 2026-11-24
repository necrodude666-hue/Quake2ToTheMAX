/*
Copyright (C) 1997-2001 Id Software, Inc.  (GPL v2 or later, same as the rest of the source)

gl_3dnow.c -- AMD 3DNow! kernels for the OpenGL refresh.  See gl_3dnow.h.

Written with the standard 3DNow!/MMX intrinsics (<mm3dnow.h>), so it builds with
Visual C++ 2005.  Rounding: 3DNow! float ops differ from x87 only in the last
bit, which is irrelevant for lightmaps and vertex positions.
*/
#include "gl_3dnow.h"

#if defined(_MSC_VER) && !defined(Q3D_SHIM)
#include <excpt.h>
#endif

#ifdef Q3D_SHIM
#include "shim_mm.h"			/* test harness: software emulation of the intrinsics */
#else
#include <mmintrin.h>
#include <mm3dnow.h>
#endif

extern long Q_ftol( float f );	/* q_shared.c: fistp, round to nearest */

#define LD64(p)		( *(const __m64 *)(p) )
#define LD32(p)		_mm_cvtsi32_si64( *(const int *)(p) )
#define ST64(p,v)	( *(__m64 *)(p) = (v) )
#define ST32(p,v)	( *(int *)(p) = _mm_cvtsi64_si32( v ) )
#define HI32(v)		_mm_cvtsi64_si32( _mm_unpackhi_pi32( v, v ) )

/*
=============================================================================
CPU detection
=============================================================================
*/
int q3d_cpu_state = -1;
static int q3d_ext_ready, q3d_ext_active;

#if defined(_MSC_VER) && !defined(Q3D_SHIM)
static int Q3D_CPUID3DNow (void)
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
		jz		noid				; no CPUID instruction (486)
		mov		eax, 0x80000000
		cpuid
		cmp		eax, 0x80000001
		jb		noext				; no extended CPUID leaves
		mov		eax, 0x80000001
		cpuid
		test	edx, 0x80000000		; 3DNow! bit
		jz		noext
		mov		result, 1
		test	edx, 0x40000000
		jz		noext
		mov		result, 2
noext:
noid:
	}
	return result;
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
static int Q3D_CPUID3DNow (void)
{
	return ( BDGN_CpuidEdx( 0x80000001 ) & 0x80000000u ) ? 1 : 0;
}
#else
static int Q3D_CPUID3DNow (void)
{
#ifdef Q3D_SHIM
	return 1;
#else
	return 0;
#endif
}
#endif

#if defined(_MSC_VER) && !defined(Q3D_SHIM)
static int Q3D_TestEnhanced(void)
{
	static const float values[]={5.0f,2.0f};
	__m64 in,out;
	int result;
	__try {
		in=LD64(values);
		out=_m_pfnacc(in,in);
		result=HI32(out)==0x40400000 && _mm_cvtsi64_si32(out)==0x40400000;
		_m_femms();
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		_mm_empty();
		result=0;
	}
	return result;
}
#endif

int Q3D_DetectCPU (void)
{
	int features=Q3D_CPUID3DNow();
	q3d_cpu_state=features!=0;
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	q3d_ext_ready=features==2 && Q3D_TestEnhanced();
#endif
	return q3d_cpu_state;
}

int Q3D_EnhancedReady(void)
{
	return q3d_ext_ready;
}

int Q3D_EnableEnhanced(int enabled)
{
	q3d_ext_active=enabled && q3d_ext_ready;
	return q3d_ext_active;
}

/*
=============================================================================
Lightmaps
=============================================================================
*/

/*
===============
Q3N_LightmapScale

bl[] (floats, 3 per texel) = [bl[] +] lightmap bytes * scale
The last texel is loaded with scalar loads so that no byte past the end of
the lightmap is ever read.
===============
*/
void Q3N_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add)
{
	__m64	zero, sxy, sb, v, w, lo, hi;
	int		i;

	if ( count <= 0 )
		return;

	zero = _mm_setzero_si64();
	sxy = LD64( scale );			/* scale[0], scale[1] */
	sb = LD32( scale + 2 );			/* scale[2], 0 */

	if ( add )
	{
		for ( i = 0 ; i < count ; i++, bl += 3, lm += 3 )
		{
			if ( i < count - 1 )
				v = LD32( lm );
			else
				v = _mm_cvtsi32_si64( lm[0] | ( lm[1] << 8 ) | ( lm[2] << 16 ) );

			w = _mm_unpacklo_pi8( v, zero );					/* r g b x as words */
			lo = _m_pi2fd( _mm_unpacklo_pi16( w, zero ) );		/* r, g */
			hi = _m_pi2fd( _mm_unpackhi_pi16( w, zero ) );		/* b, x */
			lo = _m_pfadd( _m_pfmul( lo, sxy ), LD64( bl ) );
			hi = _m_pfadd( _m_pfmul( hi, sb ), LD32( bl + 2 ) );
			ST64( bl, lo );
			ST32( bl + 2, hi );
		}
	}
	else
	{
		for ( i = 0 ; i < count ; i++, bl += 3, lm += 3 )
		{
			if ( i < count - 1 )
				v = LD32( lm );
			else
				v = _mm_cvtsi32_si64( lm[0] | ( lm[1] << 8 ) | ( lm[2] << 16 ) );

			w = _mm_unpacklo_pi8( v, zero );
			lo = _m_pfmul( _m_pi2fd( _mm_unpacklo_pi16( w, zero ) ), sxy );
			hi = _m_pfmul( _m_pi2fd( _mm_unpackhi_pi16( w, zero ) ), sb );
			ST64( bl, lo );
			ST32( bl + 2, hi );
		}
	}
	_m_femms();
}

/*
===============
Q3N_AddDlight

Same arithmetic as the dlight loop in R_AddDynamicLights.  The per-column and
per-row distances are computed once with the original conversions (td truncates,
sd uses Q_ftol); "fdist < fminlight" on an integer distance is the same as
"dist < imin" with imin = ceil(fminlight).
===============
*/

void Q3N_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color)
{
	int		sdv[Q3D_MAXBLOCK], tdv[Q3D_MAXBLOCK];
	int		s, t, td, sd, dist, imin;
	float	ftacc, fsacc;
	__m64	cxy, cb, fradv, d, w;

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

	cxy = LD64( color );
	cb = LD32( color + 2 );
	fradv = LD32( &frad );
	fradv = _mm_unpacklo_pi32( fradv, fradv );

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
				d = _mm_cvtsi32_si64( dist );
				d = _m_pi2fd( _mm_unpacklo_pi32( d, d ) );
				w = _m_pfsub( fradv, d );					/* frad - dist in both halves */
				ST64( pfBL, _m_pfadd( LD64( pfBL ), _m_pfmul( w, cxy ) ) );
				ST32( pfBL + 2, _m_pfadd( LD32( pfBL + 2 ), _m_pfmul( w, cb ) ) );
			}
		}
	}
	_m_femms();
}

/*
===============
Q3N_RoundFloats
===============
*/
void Q3N_RoundFloats (int *out, const float *in, int n)
{
	__m64	half;
	float	h;

	h = 0.5f;
	half = LD32( &h );
	half = _mm_unpacklo_pi32( half, half );

	for ( ; n >= 2 ; n -= 2, in += 2, out += 2 )
		ST64( out, _m_pf2id( _m_pfadd( LD64( in ), half ) ) );
	if ( n > 0 )
		ST32( out, _m_pf2id( _m_pfadd( LD32( in ), half ) ) );
	_m_femms();
}

/*
=============================================================================
Alias models
=============================================================================
*/

/*
===============
Q3N_ColorArray
===============
*/
void Q3N_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n)
{
	__m64	slrg, slb, l;
	int		i;

	slrg = LD64( shadelight );
	slb = LD32( shadelight + 2 );

	for ( i = 0 ; i < n ; i++, out += 3 )
	{
		l = LD32( shadedots + verts[i*4+3] );
		l = _mm_unpacklo_pi32( l, l );
		ST64( out, _m_pfmul( l, slrg ) );
		ST32( out + 2, _m_pfmul( l, slb ) );
	}
	_m_femms();
}

/*
===============
Q3N_LerpVerts
===============
*/
void Q3N_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv)
{
	__m64	zero, mxy, mz, fxy, fz, bxy, bz, a, b, xy, z;
	int		i;

	if ( nverts <= 0 )
		return;

	zero = _mm_setzero_si64();
	mxy = LD64( move );		mz = LD32( move + 2 );
	fxy = LD64( frontv );	fz = LD32( frontv + 2 );
	bxy = LD64( backv );	bz = LD32( backv + 2 );

	for ( i = 0 ; i < nverts ; i++, v += 4, ov += 4, lerp += 4 )
	{
		a = _mm_unpacklo_pi8( LD32( v ), zero );
		b = _mm_unpacklo_pi8( LD32( ov ), zero );

		xy = _m_pfadd( _m_pfadd( _m_pfmul( _m_pi2fd( _mm_unpacklo_pi16( a, zero ) ), fxy ),
								 _m_pfmul( _m_pi2fd( _mm_unpacklo_pi16( b, zero ) ), bxy ) ), mxy );
		z = _m_pfadd( _m_pfadd( _m_pfmul( _m_pi2fd( _mm_unpackhi_pi16( a, zero ) ), fz ),
								_m_pfmul( _m_pi2fd( _mm_unpackhi_pi16( b, zero ) ), bz ) ), mz );
		ST64( lerp, xy );
		ST64( lerp + 2, z );					/* z, 0 (the padding float stays 0) */
	}
	_m_femms();
}


/*
===============
Q3N_LerpVertsIdx / Q3N_ColorArrayIdx

Same maths as Q3N_LerpVerts / Q3N_ColorArray, but for the expanded vertex list of the batched
alias model path (each entry names its source vertex).
===============
*/
void Q3N_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv)
{
	__m64	zero, mxy, mz, fxy, fz, bxy, bz, a, b, xy, z;
	int		i;

	if ( n <= 0 )
		return;

	zero = _mm_setzero_si64();
	mxy = LD64( move );		mz = LD32( move + 2 );
	fxy = LD64( frontv );	fz = LD32( frontv + 2 );
	bxy = LD64( backv );	bz = LD32( backv + 2 );

	for ( i = 0 ; i < n ; i++, lerp += 4 )
	{
		const unsigned char *pv = v + xyz[i] * 4;
		const unsigned char *po = ov + xyz[i] * 4;

		a = _mm_unpacklo_pi8( LD32( pv ), zero );
		b = _mm_unpacklo_pi8( LD32( po ), zero );

		xy = _m_pfadd( _m_pfadd( _m_pfmul( _m_pi2fd( _mm_unpacklo_pi16( a, zero ) ), fxy ),
								 _m_pfmul( _m_pi2fd( _mm_unpacklo_pi16( b, zero ) ), bxy ) ), mxy );
		z = _m_pfadd( _m_pfadd( _m_pfmul( _m_pi2fd( _mm_unpackhi_pi16( a, zero ) ), fz ),
								_m_pfmul( _m_pi2fd( _mm_unpackhi_pi16( b, zero ) ), bz ) ), mz );
		ST64( lerp, xy );
		ST64( lerp + 2, z );
	}
	_m_femms();
}

void Q3N_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n)
{
	__m64	slrg, slb, l;
	int		i;

	slrg = LD64( shadelight );
	slb = LD32( shadelight + 2 );

	for ( i = 0 ; i < n ; i++, out += 3 )
	{
		l = LD32( shadedots + verts[xyz[i]*4+3] );
		l = _mm_unpacklo_pi32( l, l );
		ST64( out, _m_pfmul( l, slrg ) );
		ST32( out + 2, _m_pfmul( l, slb ) );
	}
	_m_femms();
}

void Q3N_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n)
{
	__m64	slrg, slb, l, alpha, r01, r2, p;
	float	c255 = 255.0f;
	__m64	k;
	int		i;

	k = _mm_unpacklo_pi32( LD32( &c255 ), LD32( &c255 ) );
	slrg = _m_pfmul( LD64( shadelight ), k );
	slb = _m_pfmul( LD32( shadelight + 2 ), k );
	alpha = _mm_cvtsi32_si64( (int)0xFF000000 );

	for ( i = 0 ; i < n ; i++, out += 4 )
	{
		l = LD32( shadedots + verts[xyz[i]*4+3] );
		l = _mm_unpacklo_pi32( l, l );
		r01 = _m_pf2id( _m_pfmul( l, slrg ) );
		r2 = _m_pf2id( _m_pfmul( l, slb ) );
		p = _mm_packs_pi32( r01, r2 );			/* r,g,b,0 as shorts (saturating) */
		p = _mm_packs_pu16( p, p );				/* bytes, clamped to 0..255 */
		p = _mm_or_si64( p, alpha );
		ST32( out, p );
	}
	_m_femms();
}

/*
===============
Q3N_CullBox
===============
*/
int Q3N_CullBox (const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes)
{
	int		i, culled = 0;

	for ( i = 0 ; i < nplanes ; i++ )
	{
		const float	*n = planes + i*4;
		int			b = signbits[i];
		float		pv[3];		// corner of the box furthest along the plane normal
		__m64		a, z, s;

		pv[0] = ( b & 1 ) ? mins[0] : maxs[0];
		pv[1] = ( b & 2 ) ? mins[1] : maxs[1];
		pv[2] = ( b & 4 ) ? mins[2] : maxs[2];

		a = _m_pfmul( LD64( n ), LD64( pv ) );
		z = _m_pfmul( LD32( n + 2 ), LD32( pv + 2 ) );
		s = _m_pfacc( a, a );
		s = _m_pfadd( s, z );
		// culled if dot( normal, corner ) < dist
		if ( !_mm_cvtsi64_si32( _m_pfcmpge( s, LD32( n + 3 ) ) ) )
		{
			culled = 1;
			break;
		}
	}
	_m_femms();
	return culled;
}


/*
===============
Q3N_SkyClip

AMD 3DNow! patch, area world (W2).  Same sides and split points as id's ClipSkyPolygon.
The split fraction uses pfrcp refined with pfrcpit1/pfrcpit2 (AMD used the 14-bit pfrcp
alone).  Vertices are copied with MMX moves: no x87 code may run before the femms.
===============
*/
#define Q3N_MAX_CLIP_VERTS	64
int Q3N_SkyClip (const float *vecs, int nump, const float *norm, float eps,
				 float *front, int *nfront, float *back, int *nback)
{
	float	dists[Q3N_MAX_CLIP_VERTS];
	int		sides[Q3N_MAX_CLIP_VERTS];		// 0 = front, 1 = back, 2 = on
	int		i, k, nf, nb, anyfront = 0, anyback = 0;
	float	negeps = -eps;
	__m64	n01, n2, veps, vnegeps;

	n01 = LD64( norm );
	n2 = LD32( norm + 2 );
	veps = LD32( &eps );
	vnegeps = LD32( &negeps );

	for ( i = 0 ; i < nump ; i++ )
	{
		const float	*v = vecs + i*3;
		__m64		a, d;

		a = _m_pfmul( LD64( v ), n01 );
		d = _m_pfadd( _m_pfacc( a, a ), _m_pfmul( LD32( v + 2 ), n2 ) );
		ST32( dists + i, d );
		if ( _mm_cvtsi64_si32( _m_pfcmpgt( d, veps ) ) )
		{
			sides[i] = 0;
			anyfront = 1;
		}
		else if ( _mm_cvtsi64_si32( _m_pfcmpgt( vnegeps, d ) ) )
		{
			sides[i] = 1;
			anyback = 1;
		}
		else
			sides[i] = 2;
	}

	if ( !anyfront || !anyback )
	{
		_m_femms();
		return 0;
	}

	nf = nb = 0;
	for ( i = 0 ; i < nump ; i++ )
	{
		const float	*v = vecs + i*3, *w;
		__m64		di, dd, r, t, lo, hi;

		k = ( i + 1 == nump ) ? 0 : i + 1;
		w = vecs + k*3;
		if ( sides[i] != 1 )
		{
			ST64( front + nf*3, LD64( v ) );
			ST32( front + nf*3 + 2, LD32( v + 2 ) );
			nf++;
		}
		if ( sides[i] != 0 )
		{
			ST64( back + nb*3, LD64( v ) );
			ST32( back + nb*3 + 2, LD32( v + 2 ) );
			nb++;
		}
		if ( sides[i] == 2 || sides[k] == 2 || sides[k] == sides[i] )
			continue;

		// t = dists[i] / ( dists[i] - dists[k] ); split = v + t * ( w - v )
		di = LD32( dists + i );
		dd = _m_pfsub( di, LD32( dists + k ) );
		r = _m_pfrcp( dd );
		r = _m_pfrcpit2( _m_pfrcpit1( dd, r ), r );
		t = _m_pfmul( di, r );
		t = _mm_unpacklo_pi32( t, t );
		lo = LD64( v );
		lo = _m_pfadd( lo, _m_pfmul( t, _m_pfsub( LD64( w ), lo ) ) );
		hi = LD32( v + 2 );
		hi = _m_pfadd( hi, _m_pfmul( t, _m_pfsub( LD32( w + 2 ), hi ) ) );
		ST64( front + nf*3, lo );
		ST32( front + nf*3 + 2, hi );
		ST64( back + nb*3, lo );
		ST32( back + nb*3 + 2, hi );
		nf++;
		nb++;
	}
	_m_femms();
	*nfront = nf;
	*nback = nb;
	return 1;
}


/*
===============
Q3N_SinCos2

sin and cos of two angles in degrees.  k = nearest integer to deg/90 (exact quadrant,
fixed up from pf2id's truncation), r = (deg - 90k) in radians, |r| <= pi/4, then Taylor
polynomials (error < 3e-7 at pi/4) and a per-lane quadrant swap / sign flip.
MMX state stays active; the caller ends with femms.
===============
*/
static void Q3N_SinCos2 (__m64 deg, __m64 *s, __m64 *c)
{
	static const float	k_inv90[2] = { 1.0f / 90.0f, 1.0f / 90.0f };
	static const float	k_90[2] = { 90.0f, 90.0f };
	static const float	k_half[2] = { 0.5f, 0.5f };
	static const float	k_rad[2] = { 0.017453292519943296f, 0.017453292519943296f };
	static const float	k_s[3][2] = { { -1.0f/6, -1.0f/6 }, { 1.0f/120, 1.0f/120 }, { -1.0f/5040, -1.0f/5040 } };
	static const float	k_c[4][2] = { { -1.0f/2, -1.0f/2 }, { 1.0f/24, 1.0f/24 }, { -1.0f/720, -1.0f/720 }, { 1.0f/40320, 1.0f/40320 } };
	static const float	k_one[2] = { 1.0f, 1.0f };
	__m64	y, k, f, up, dn, r, r2, ps, pc, swap, sneg, cneg;
	int		k0, k1;

	y = _m_pfmul( deg, LD64( k_inv90 ) );
	k = _m_pf2id( y );								// truncated toward zero
	f = _m_pfsub( y, _m_pi2fd( k ) );
	up = _m_pfcmpgt( f, LD64( k_half ) );			// f > 0.5: k+1
	dn = _m_pfcmpgt( _m_pfsub( _mm_setzero_si64(), LD64( k_half ) ), f );	// f < -0.5: k-1
	k0 = _mm_cvtsi64_si32( k ) - _mm_cvtsi64_si32( up ) + _mm_cvtsi64_si32( dn );
	k1 = HI32( k ) - HI32( up ) + HI32( dn );
	k = _mm_unpacklo_pi32( _mm_cvtsi32_si64( k0 ), _mm_cvtsi32_si64( k1 ) );

	r = _m_pfmul( _m_pfsub( deg, _m_pfmul( _m_pi2fd( k ), LD64( k_90 ) ) ), LD64( k_rad ) );
	r2 = _m_pfmul( r, r );
	ps = _m_pfadd( LD64( k_s[1] ), _m_pfmul( r2, LD64( k_s[2] ) ) );
	ps = _m_pfadd( LD64( k_s[0] ), _m_pfmul( r2, ps ) );
	ps = _m_pfadd( r, _m_pfmul( _m_pfmul( r2, r ), ps ) );
	pc = _m_pfadd( LD64( k_c[2] ), _m_pfmul( r2, LD64( k_c[3] ) ) );
	pc = _m_pfadd( LD64( k_c[1] ), _m_pfmul( r2, pc ) );
	pc = _m_pfadd( LD64( k_c[0] ), _m_pfmul( r2, pc ) );
	pc = _m_pfadd( LD64( k_one ), _m_pfmul( r2, pc ) );

	// quadrant q = k & 3:  sin = s, c, -s, -c   cos = c, -s, -c, s
	swap = _mm_unpacklo_pi32( _mm_cvtsi32_si64( -( k0 & 1 ) ), _mm_cvtsi32_si64( -( k1 & 1 ) ) );
	sneg = _mm_unpacklo_pi32( _mm_cvtsi32_si64( ( k0 & 2 ) << 30 ), _mm_cvtsi32_si64( ( k1 & 2 ) << 30 ) );
	cneg = _mm_unpacklo_pi32( _mm_cvtsi32_si64( ( ( k0 + 1 ) & 2 ) << 30 ), _mm_cvtsi32_si64( ( ( k1 + 1 ) & 2 ) << 30 ) );
	*s = _mm_xor_si64( _mm_or_si64( _mm_and_si64( swap, pc ), _mm_andnot_si64( swap, ps ) ), sneg );
	*c = _mm_xor_si64( _mm_or_si64( _mm_and_si64( swap, ps ), _mm_andnot_si64( swap, pc ) ), cneg );
}

/*
===============
Q3N_AngleVectors

AMD 3DNow! patch, area mesh (M1).  Same formulas as id's AngleVectors.
===============
*/
void Q3N_AngleVectors (const float *angles, float *forward, float *right, float *up)
{
	static const int	k_sign[2] = { 0x80000000, 0x80000000 };
	static const int	k_signhi[2] = { 0, 0x80000000 };
	__m64	s1, c1, s2, c2, sp, cp, sr, cr, cysy, sycy, t;

	Q3N_SinCos2( LD64( angles ), &s1, &c1 );			// ( pitch, yaw )
	Q3N_SinCos2( LD32( angles + 2 ), &s2, &c2 );		// ( roll, 0 )
	sp = _mm_unpacklo_pi32( s1, s1 );
	cp = _mm_unpacklo_pi32( c1, c1 );
	sr = _mm_unpacklo_pi32( s2, s2 );
	cr = _mm_unpacklo_pi32( c2, c2 );
	cysy = _mm_unpackhi_pi32( c1, s1 );				// ( cy, sy )
	sycy = _mm_xor_si64( _mm_unpackhi_pi32( s1, c1 ), LD64( k_signhi ) );	// ( sy, -cy )

	if ( forward )
	{
		ST64( forward, _m_pfmul( cp, cysy ) );
		ST32( forward + 2, _mm_xor_si64( sp, LD64( k_sign ) ) );
	}
	if ( right )
	{	// ( -sr*sp*cy + cr*sy, -sr*sp*sy - cr*cy, -sr*cp )
		t = _mm_xor_si64( sr, LD64( k_sign ) );
		ST64( right, _m_pfadd( _m_pfmul( _m_pfmul( t, sp ), cysy ), _m_pfmul( cr, sycy ) ) );
		ST32( right + 2, _m_pfmul( t, cp ) );
	}
	if ( up )
	{	// ( cr*sp*cy + sr*sy, cr*sp*sy - sr*cy, cr*cp )
		ST64( up, _m_pfadd( _m_pfmul( _m_pfmul( cr, sp ), cysy ), _m_pfmul( sr, sycy ) ) );
		ST32( up + 2, _m_pfmul( cr, cp ) );
	}
	_m_femms();
}

/*
===============
Q3N_CullAliasBox

AMD 3DNow! patch, area mesh (M2).  The rotate + frustum part of R_CullAliasModel:
corner i takes x from mins when i&1, y when i&2, z when i&4 (else maxs), like id.
===============
*/
int Q3N_CullAliasBox (const float *mins, const float *maxs, const float *vectors, const float *origin,
					  const float *planes, int nplanes)
{
	static const int	k_signhi[2] = { 0, 0x80000000 };
	float	corners[8*3];
	int		i, f, culled = 0;
	__m64	v01z, xy, z, d, d2;

	v01z = _mm_unpacklo_pi32( LD32( vectors + 2 ), LD32( vectors + 5 ) );
	for ( i = 0 ; i < 8 ; i++ )
	{
		xy = _mm_unpacklo_pi32( LD32( ( i & 1 ) ? mins : maxs ), LD32( ( ( i & 2 ) ? mins : maxs ) + 1 ) );
		z = LD32( ( ( i & 4 ) ? mins : maxs ) + 2 );
		// ( forward . p, -( right . p ) ), up . p
		d = _m_pfacc( _m_pfmul( LD64( vectors ), xy ), _m_pfmul( LD64( vectors + 3 ), xy ) );
		d = _m_pfadd( d, _m_pfmul( v01z, _mm_unpacklo_pi32( z, z ) ) );
		d = _mm_xor_si64( d, LD64( k_signhi ) );
		d2 = _m_pfmul( LD64( vectors + 6 ), xy );
		d2 = _m_pfadd( _m_pfacc( d2, d2 ), _m_pfmul( LD32( vectors + 8 ), z ) );
		ST64( corners + i*3, _m_pfadd( LD64( origin ), d ) );
		ST32( corners + i*3 + 2, _m_pfadd( LD32( origin + 2 ), d2 ) );
	}

	// culled when every corner is behind one plane
	for ( f = 0 ; f < nplanes && !culled ; f++ )
	{
		const float	*n = planes + f*4;
		__m64		nxy = LD64( n ), nz = LD32( n + 2 ), dist = LD32( n + 3 ), a;

		culled = 1;
		for ( i = 0 ; i < 8 ; i++ )
		{
			a = _m_pfmul( nxy, LD64( corners + i*3 ) );
			a = _m_pfadd( _m_pfacc( a, a ), _m_pfmul( nz, LD32( corners + i*3 + 2 ) ) );
			if ( !_mm_cvtsi64_si32( _m_pfcmpgt( _mm_setzero_si64(), _m_pfsub( a, dist ) ) ) )
			{
				culled = 0;
				break;
			}
		}
	}
	_m_femms();
	return culled;
}


/*
===============
Q3N_WaterST

s = ( os + turbsin[ round((ot*0.125 + time) * TURBSCALE) & 255 ] + scroll ) / 64
t = ( ot + turbsin[ round((os*0.125 + time) * TURBSCALE) & 255 ] ) / 64
The table index is made positive with a bias that is a multiple of 256 so that
truncation (pf2id) rounds the same way as Q_ftol.
===============
*/
void Q3N_WaterST (float *out, const float *verts, int numverts, int vertexsize,
				  float rdt, float scroll, const float *turbsin)
{
	static const float	k_scale = (float)( 256.0 / ( 2 * 3.14159265358979323846 ) );
	float		kmul[2], kadd[2], kscroll[2], kinv[2];
	__m64		vmul, vadd, vscroll, vinv;
	int			i;

	kmul[0] = kmul[1] = 0.125f * k_scale;
	kadd[0] = kadd[1] = rdt * k_scale + 65536.0f + 0.5f;
	kscroll[0] = scroll; kscroll[1] = 0.0f;
	kinv[0] = kinv[1] = 1.0f / 64.0f;
	vmul = LD64( kmul );  vadd = LD64( kadd );  vscroll = LD64( kscroll );  vinv = LD64( kinv );

	for ( i = 0 ; i < numverts ; i++, verts += vertexsize, out += 2 )
	{
		__m64	p, u, idx, sn;
		int		i0, i1;

		p = LD64( verts + 3 );								// ( os, ot )
		u = _mm_unpacklo_pi32( LD32( verts + 4 ), LD32( verts + 3 ) );	// ( ot, os )
		idx = _m_pf2id( _m_pfadd( _m_pfmul( u, vmul ), vadd ) );
		i0 = _mm_cvtsi64_si32( idx ) & 255;					// from ot  -> s
		i1 = _mm_cvtsi64_si32( _mm_unpackhi_pi32( idx, idx ) ) & 255;	// from os  -> t
		sn = _mm_unpacklo_pi32( LD32( turbsin + i0 ), LD32( turbsin + i1 ) );
		p = _m_pfadd( p, sn );
		p = _m_pfadd( p, vscroll );
		ST64( out, _m_pfmul( p, vinv ) );
	}
	_m_femms();
}

/*
=============================================================================
BDGN 3.50: the rest of AMD's 3DNow! renderer code (ref_glam.dll)
=============================================================================
*/
#define BCAST(v)	_mm_unpacklo_pi32( v, v )

/*
===============
Q3N_MarkLights

R_MarkLights (AMD 0x100181e7).  The plane distance with 3DNow!, both cutoff compares as one pfcmpgt, and the
second child as a loop like AMD.  The whole walk is one call with one femms (AMD did femms around every recursive
call): the helper below runs only integer code between the 3DNow! steps.  id's compares are kept: dist > cut goes
to child 0, dist < -cut to child 1 (AMD used >=).  cuts = ( cut, -cut ), cut = intensity - DLIGHT_CUTOFF.
===============
*/
static void Q3N_MarkLightsR (const float *light, const float *cuts, int bit, q3n_node_t *node, q3n_surf_t *surfaces, int frame)
{
	const float	*plane;
	q3n_surf_t	*surf;
	__m64		d, c;
	int			i, n;

	while ( node->contents == -1 )
	{
		plane = node->plane->normal;
		d = _m_pfmul( LD64( light ), LD64( plane ) );
		d = _m_pfadd( _m_pfacc( d, d ), _m_pfmul( LD32( light + 2 ), LD32( plane + 2 ) ) );
		d = _m_pfsub( d, LD32( plane + 3 ) );
		// ( dist > cut, -cut > dist )
		c = _m_pfcmpgt( _mm_unpacklo_pi32( d, LD32( cuts + 1 ) ), _mm_unpacklo_pi32( LD32( cuts ), d ) );
		if ( _mm_cvtsi64_si32( c ) )
		{
			node = node->children[0];
			continue;
		}
		if ( HI32( c ) )
		{
			node = node->children[1];
			continue;
		}

		// mark the polygons
		surf = surfaces + node->firstsurface;
		for ( i = 0, n = node->numsurfaces ; i < n ; i++, surf++ )
		{
			if ( surf->dlightframe != frame )
			{
				surf->dlightbits = 0;
				surf->dlightframe = frame;
			}
			surf->dlightbits |= bit;
		}

		Q3N_MarkLightsR( light, cuts, bit, node->children[0], surfaces, frame );
		node = node->children[1];
	}
}

void Q3N_MarkLights (const float *light, int bit, q3n_node_t *node, q3n_surf_t *surfaces, int dlightframe)
{
	static const float	k_cutoff = 64.0f;		// DLIGHT_CUTOFF
	float	cuts[2];
	__m64	c;

	c = _m_pfsub( LD32( light + 6 ), LD32( &k_cutoff ) );
	ST64( cuts, _mm_unpacklo_pi32( c, _m_pfsub( _mm_setzero_si64(), c ) ) );
	Q3N_MarkLightsR( light, cuts, bit, node, surfaces, dlightframe );
	_m_femms();
}

/*
===============
Q3N_LightPoint

RecursiveLightPoint (AMD 0x10017680).  Front and back distances as one 3DNow! pair, 1/(front-back) with pfrcp and
both refinement steps (like AMD), s/t as a pair (pf2id truncates like id's float->int), the light colour sum with
3DNow!.  Same recursion as id; one femms per search.
===============
*/
static int Q3N_LightPointR (const q3n_lightpoint_t *lp, const q3n_node_t *node, const float *start, const float *end)
{
	static const float	k_inv255 = 1.0f / 255;
	const float			*plane, *vecs, *rgb;
	const q3n_surf_t	*surf;
	const unsigned char	*lightmap;
	__m64				a, b, fb, neg, d, r0, frac, st, mod, c01, c2;
	float				mid[3];
	int					side, r, i, n, s, t, ds, dt, smax, size, maps;

	if ( node->contents != -1 )
		return -1;		// didn't hit anything

	// ( front, back ) = ( start, end ) . normal - dist
	plane = node->plane->normal;
	a = _m_pfmul( LD64( start ), LD64( plane ) );
	b = _m_pfmul( LD64( end ), LD64( plane ) );
	fb = _m_pfacc( a, b );
	fb = _m_pfadd( fb, _m_pfmul( _mm_unpacklo_pi32( LD32( start + 2 ), LD32( end + 2 ) ), BCAST( LD32( plane + 2 ) ) ) );
	fb = _m_pfsub( fb, BCAST( LD32( plane + 3 ) ) );
	neg = _m_pfcmpgt( _mm_setzero_si64(), fb );			// ( front < 0, back < 0 )
	side = _mm_cvtsi64_si32( neg ) != 0;

	if ( ( HI32( neg ) != 0 ) == side )
		return Q3N_LightPointR( lp, node->children[side], start, end );

	// frac = front / ( front - back ), mid = start + ( end - start ) * frac
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	if (q3d_ext_active)
		d=_m_pfnacc(fb,fb); /* Both lanes become front - back. */
	else
#endif
		d=BCAST(_m_pfsub(fb,_mm_unpackhi_pi32(fb,fb)));
	r0 = _m_pfrcp( d );
	r0 = _m_pfrcpit2( _m_pfrcpit1( d, r0 ), r0 );
	frac = BCAST( _m_pfmul( fb, r0 ) );
	ST64( mid, _m_pfadd( LD64( start ), _m_pfmul( _m_pfsub( LD64( end ), LD64( start ) ), frac ) ) );
	ST32( mid + 2, _m_pfadd( LD32( start + 2 ), _m_pfmul( _m_pfsub( LD32( end + 2 ), LD32( start + 2 ) ), frac ) ) );
	if ( node->plane->type < 3 )
	{	// axial plane (every floor and wall): put mid exactly on it, as id's x87 code does (/fp:fast keeps
		// frac in a register).  The refined pfrcp is ~1 ulp off, and a mid 1 ulp off the plane takes the other
		// side at the next node on the same plane.  normal = +-1: mid = dist * normal (integer sign flip).
		int		bits = ((const int *)plane)[3];

		if ( bits & 0x7fffffff )
			bits ^= ((const int *)plane)[node->plane->type] & 0x80000000;
		else
			bits = 0;
		((int *)mid)[node->plane->type] = bits;
	}

	// go down front side
	r = Q3N_LightPointR( lp, node->children[side], start, mid );
	if ( r >= 0 )
		return r;		// hit something

	// check for impact on this node (integer copies: no x87 code while MMX is active)
	((int *)lp->spot)[0] = ((const int *)mid)[0];
	((int *)lp->spot)[1] = ((const int *)mid)[1];
	((int *)lp->spot)[2] = ((const int *)mid)[2];
	*lp->plane = node->plane;

	surf = lp->surfaces + node->firstsurface;
	for ( i = 0, n = node->numsurfaces ; i < n ; i++, surf++ )
	{
		if ( surf->flags & lp->skipflags )
			continue;	// no lightmaps

		// ( s, t ) = mid . vecs[0..1] + vecs[0..1][3]
		vecs = surf->texinfo;
		a = _m_pfmul( LD64( mid ), LD64( vecs ) );
		b = _m_pfmul( LD64( mid ), LD64( vecs + 4 ) );
		st = _m_pfacc( a, b );
		st = _m_pfadd( st, _m_pfmul( BCAST( LD32( mid + 2 ) ), _mm_unpacklo_pi32( LD32( vecs + 2 ), LD32( vecs + 6 ) ) ) );
		st = _m_pfadd( st, _mm_unpacklo_pi32( LD32( vecs + 3 ), LD32( vecs + 7 ) ) );
		st = _m_pf2id( st );
		s = _mm_cvtsi64_si32( st );
		t = HI32( st );

		if ( s < surf->texturemins[0] || t < surf->texturemins[1] )
			continue;

		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];

		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if ( !surf->samples )
			return 0;

		ds >>= 4;
		dt >>= 4;

		smax = ( surf->extents[0] >> 4 ) + 1;
		size = smax * ( ( surf->extents[1] >> 4 ) + 1 );
		lightmap = surf->samples + 3 * ( dt * smax + ds );

		// pointcolor = sum over the styles of lightmap * modulate * style rgb / 255
		mod = BCAST( LD32( lp->modulate ) );
		c01 = _mm_setzero_si64();
		c2 = _mm_setzero_si64();
		for ( maps = 0 ; maps < 4 && surf->styles[maps] != 255 ; maps++ )
		{
			rgb = lp->lightstyles + 4 * surf->styles[maps];
			a = _m_pi2fd( _mm_unpacklo_pi32( _mm_cvtsi32_si64( lightmap[0] ), _mm_cvtsi32_si64( lightmap[1] ) ) );
			b = _m_pi2fd( _mm_cvtsi32_si64( lightmap[2] ) );
			c01 = _m_pfadd( c01, _m_pfmul( _m_pfmul( a, _m_pfmul( LD64( rgb ), mod ) ), BCAST( LD32( &k_inv255 ) ) ) );
			c2 = _m_pfadd( c2, _m_pfmul( _m_pfmul( b, _m_pfmul( LD32( rgb + 2 ), mod ) ), LD32( &k_inv255 ) ) );
			lightmap += 3 * size;
		}
		ST64( lp->color, c01 );
		ST32( lp->color + 2, c2 );
		return 1;
	}

	// go down back side
	return Q3N_LightPointR( lp, node->children[!side], mid, end );
}

int Q3N_LightPoint (const q3n_lightpoint_t *lp, const q3n_node_t *node, const float *start, const float *end)
{
	int		r;

	r = Q3N_LightPointR( lp, node, start, end );
	_m_femms();
	return r;
}

/*
===============
Q3N_ParticleTris / Q3N_ParticleArrays

GL_DrawParticles (AMD 0x10015c80): the distance scale, the two corners and the colour with the alpha byte for all
particles first, so the GL loop that follows is only GL calls.  Q3N_ParticleArrays is the same colour step for
BDGN's point-sprite arrays (gl_fastparticles): pf2id instead of one x87 float->int conversion per particle, and the
positions copied as integers.  The alpha byte is (int)( alpha * 255 ) like id; the product is single precision,
so a value within a few millionths of an integer may land on the other side (one step of 255).
===============
*/
void Q3N_ParticleTris (int n, const q3n_particle_t *p, const unsigned *colortable, const float *vieworg,
					   const float *vpn, const float *up, const float *right, q3n_ptri_t *out)
{
	static const float	k_20 = 20.0f, k_one = 1.0f, k_scale = 0.004f, k_255 = 255.0f;
	__m64	org01, org2, vpn01, vpn2, up01, up2, rt01, rt2, one, d, near_particle, sc, o01, o2, al;
	int		i;

	org01 = LD64( vieworg );	org2 = LD32( vieworg + 2 );
	vpn01 = LD64( vpn );		vpn2 = LD32( vpn + 2 );
	up01 = LD64( up );			up2 = LD32( up + 2 );
	rt01 = LD64( right );		rt2 = LD32( right + 2 );
	one = LD32( &k_one );
	for ( i = 0 ; i < n ; i++, p++, out++ )
	{
		// hack a scale up to keep particles from disappearing: d = ( origin - vieworg ) . vpn
		o01 = LD64( p->origin );
		o2 = LD32( p->origin + 2 );
		d = _m_pfmul( _m_pfsub( o01, org01 ), vpn01 );
		d = _m_pfadd( _m_pfacc( d, d ), _m_pfmul( _m_pfsub( o2, org2 ), vpn2 ) );
		near_particle = _m_pfcmpgt( LD32( &k_20 ), d );				// d < 20: scale 1
		sc = _m_pfadd( one, _m_pfmul( d, LD32( &k_scale ) ) );
		sc = _mm_or_si64( _mm_and_si64( near_particle, one ), _mm_andnot_si64( near_particle, sc ) );
		sc = BCAST( sc );

		ST64( out->v1, _m_pfadd( o01, _m_pfmul( up01, sc ) ) );
		ST32( out->v1 + 2, _m_pfadd( o2, _m_pfmul( up2, sc ) ) );
		ST64( out->v2, _m_pfadd( o01, _m_pfmul( rt01, sc ) ) );
		ST32( out->v2 + 2, _m_pfadd( o2, _m_pfmul( rt2, sc ) ) );

		al = _m_pf2id( _m_pfmul( LD32( &p->alpha ), LD32( &k_255 ) ) );
		out->color = ( colortable[p->color] & 0x00ffffff ) | ( (unsigned)_mm_cvtsi64_si32( al ) << 24 );
	}
	_m_femms();
}

void Q3N_ParticleArrays (int n, const q3n_particle_t *p, const unsigned *colortable, float *verts, unsigned char *cols)
{
	static const float	k_255[2] = { 255.0f, 255.0f };
	__m64	al;
	int		i;

	for ( i = 0 ; i + 2 <= n ; i += 2, p += 2, verts += 6, cols += 8 )
	{	// two alphas per pfmul / pf2id
		ST64( verts, LD64( p[0].origin ) );
		ST32( verts + 2, LD32( p[0].origin + 2 ) );
		ST64( verts + 3, LD64( p[1].origin ) );
		ST32( verts + 5, LD32( p[1].origin + 2 ) );
		al = _m_pf2id( _m_pfmul( _mm_unpacklo_pi32( LD32( &p[0].alpha ), LD32( &p[1].alpha ) ), LD64( k_255 ) ) );
		*(unsigned *)cols = ( colortable[p[0].color] & 0x00ffffff ) | ( (unsigned)_mm_cvtsi64_si32( al ) << 24 );
		*(unsigned *)( cols + 4 ) = ( colortable[p[1].color] & 0x00ffffff ) | ( (unsigned)HI32( al ) << 24 );
	}
	if ( i < n )
	{
		ST64( verts, LD64( p->origin ) );
		ST32( verts + 2, LD32( p->origin + 2 ) );
		al = _m_pf2id( _m_pfmul( LD32( &p->alpha ), LD64( k_255 ) ) );
		*(unsigned *)cols = ( colortable[p->color] & 0x00ffffff ) | ( (unsigned)_mm_cvtsi64_si32( al ) << 24 );
	}
	_m_femms();
}
