/*
Copyright (C) 1997-2001 Id Software, Inc.  (GPL v2 or later, same as the rest of the source)

q3e_3dnow.c -- AMD 3DNow! kernels for the engine (quake2.exe).  See q3e_3dnow.h.

Written with the standard 3DNow!/MMX intrinsics (<mm3dnow.h>), so it builds with
Visual C++ 2005.  No x87 code may run while MMX state is active: floats are moved with
MMX loads/stores, passed by pointer, and every call out of a kernel comes after femms.
*/
#include "q3e_3dnow.h"

#if defined(_MSC_VER) && !defined(Q3D_SHIM)
#include <windows.h>		/* EXCEPTION_EXECUTE_HANDLER (SSE test) */
#endif

#ifdef Q3D_SHIM
#include "shim_mm.h"			/* test harness: software emulation of the intrinsics */
#else
#include <mmintrin.h>
#include <mm3dnow.h>
#endif

#define LD64(p)		( *(const __m64 *)(p) )
#define LD32(p)		_mm_cvtsi32_si64( *(const int *)(p) )
#define ST64(p,v)	( *(__m64 *)(p) = (v) )
#define ST32(p,v)	( *(int *)(p) = _mm_cvtsi64_si32( v ) )
#define LO(v)		_mm_cvtsi64_si32( v )
#define HI(v)		_mm_cvtsi64_si32( _mm_unpackhi_pi32( v, v ) )
#define BCAST(v)	_mm_unpacklo_pi32( v, v )

/*
=============================================================================
CPU detection
=============================================================================
*/
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
int Q3E_CPUHas3DNow (void)
{
	static int	has = -1;
	int			result;

	if ( has >= 0 )
		return has;
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
		push	ebx
		mov		eax, 0x80000000
		cpuid
		cmp		eax, 0x80000001
		jb		noext				; no extended CPUID leaves
		mov		eax, 0x80000001
		cpuid
		test	edx, 0x80000000		; 3DNow! bit
		jz		noext
		mov		result, 1
noext:
		pop		ebx
noid:
	}
	has = result;
	return has;
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
int Q3E_CPUHas3DNow (void)
{
	static int has = -1;
	if ( has < 0 )
		has = ( BDGN_CpuidEdx( 0x80000001 ) & 0x80000000u ) ? 1 : 0;
	return has;
}
#else
int Q3E_CPUHas3DNow (void)
{
#ifdef Q3D_SHIM
	return 1;
#else
	return 0;
#endif
}
#endif

/*
===============
Q3E_CPUHasMMX / Q3E_CPUHasSSE

BDGN 3.40: used by the video menu and by cpu_auto.  SSE also needs the operating system to save the SSE
registers (Windows 98 SE and later), which is tested by running one SSE instruction.
===============
*/
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
static int Q3E_Features (void)
{
	static int	f = -1;
	int			result;

	if ( f >= 0 )
		return f;
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
		popfd
		xor		eax, ecx
		jz		noid				; no CPUID instruction (486)
		push	ebx
		xor		eax, eax
		cpuid
		test	eax, eax
		jz		nol1
		mov		eax, 1
		cpuid
		mov		result, edx
nol1:
		pop		ebx
noid:
	}
	f = result & 0x7fffffff;
	return f;
}

int Q3E_CPUHasMMX (void)
{
	return ( Q3E_Features() & ( 1 << 23 ) ) ? 1 : 0;
}

int Q3E_CPUHasSSE (void)
{
	static int	has = -1;
	int			f, ok;

	if ( has >= 0 )
		return has;
	f = Q3E_Features();
	has = 0;
	if ( ( f & ( 1 << 25 ) ) && ( f & ( 1 << 24 ) ) && ( f & ( 1 << 23 ) ) )
	{
		ok = 1;
		__try
		{
			__asm { xorps xmm0, xmm0 }
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			ok = 0;
		}
		has = ok;
	}
	return has;
}

/* BDGN 3.51p: all CPUID registers bdgn_status reports. Same EFLAGS.ID guard as above; extended leaves only when the
   CPU reports them (older Intel CPUs answer unknown leaves with leaf data, which is below 0x80000000). */
void Q3E_ReadCPUID (q3e_cpuid_raw_t *r)
{
	unsigned	a, b, c, d;
	int			has;

	memset (r, 0, sizeof(*r));
	__asm {
		pushfd
		pop		eax
		mov		ecx, eax
		xor		eax, 0x200000
		push	eax
		popfd
		pushfd
		pop		eax
		push	ecx
		popfd
		xor		eax, ecx
		mov		has, eax
	}
	if (!has)
		return;
	r->has_cpuid = 1;
	__asm {
		push	ebx
		xor		eax, eax
		cpuid
		mov		a, eax
		mov		b, ebx
		mov		c, ecx
		mov		d, edx
		pop		ebx
	}
	r->max_leaf = a;
	r->vendor_ebx = b;
	r->vendor_edx = d;
	r->vendor_ecx = c;
	if (r->max_leaf >= 1)
	{
		__asm {
			push	ebx
			mov		eax, 1
			cpuid
			mov		a, eax
			mov		b, ebx
			mov		c, ecx
			mov		d, edx
			pop		ebx
		}
		r->leaf1_eax = a;
		r->leaf1_ebx = b;
		r->leaf1_ecx = c;
		r->leaf1_edx = d;
	}
	if (r->max_leaf>=7) {
		__asm {
			push ebx
			mov eax, 7
			xor ecx, ecx
			cpuid
			mov b, ebx
			pop ebx
		}
		r->leaf7_ebx=b;
	}
	__asm {
		push	ebx
		mov		eax, 0x80000000
		cpuid
		mov		a, eax
		pop		ebx
	}
	if (a >= 0x80000001u && a <= 0x8000ffffu)
	{
		r->max_ext = a;
		__asm {
			push	ebx
			mov		eax, 0x80000001
			cpuid
			mov		d, edx
			pop		ebx
		}
		r->ext1_edx = d;
		if (a >= 0x80000006u)
		{
			__asm {
				push	ebx
				mov		eax, 0x80000006
				cpuid
				mov		c, ecx
				pop		ebx
			}
			r->ext6_ecx = c;	/* BDGN 3.58w: L2 size (K6-2+ 128 KB, K6-III+ 256 KB) */
		}
	}
}

/* SSE2 reported by the CPU, SSE state saved by the operating system, and one SSE2 instruction runs */
int Q3E_CPUHasSSE2 (void)
{
	static int	has = -1;
	int			ok;

	if ( has >= 0 )
		return has;
	has = 0;
	if ( ( Q3E_Features() & ( 1 << 26 ) ) && Q3E_CPUHasSSE() )
	{
		ok = 1;
		__try
		{
			__asm { pxor xmm0, xmm0 }
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			ok = 0;
		}
		has = ok;
	}
	return has;
}
#elif defined(__GNUC__) && defined(__i386__)
/* gcc K6 test build: SSE/SSE2 stay off (Visual C++ tests the operating system's SSE support with __try, which gcc
   has not; Windows 98 first edition does not save the SSE registers).  This build is for the K6-III. */
int Q3E_CPUHasMMX (void)
{
	return ( BDGN_CpuidEdx( 1 ) & ( 1 << 23 ) ) ? 1 : 0;
}
int Q3E_CPUHasSSE (void)
{
	return 0;
}
int Q3E_CPUHasSSE2 (void)
{
	return 0;
}
void Q3E_ReadCPUID (q3e_cpuid_raw_t *r)
{
	unsigned a, b, c, d;
	memset (r, 0, sizeof(*r));
	if ( !__get_cpuid( 0, &a, &b, &c, &d ) )
		return;
	r->has_cpuid = 1;
	r->max_leaf = a; r->vendor_ebx = b; r->vendor_edx = d; r->vendor_ecx = c;
	if ( r->max_leaf >= 1 && __get_cpuid( 1, &a, &b, &c, &d ) )
	{
		r->leaf1_eax = a; r->leaf1_ebx = b; r->leaf1_ecx = c; r->leaf1_edx = d;
	}
	if (r->max_leaf>=7 && __get_cpuid_count(7,0,&a,&b,&c,&d))
		r->leaf7_ebx=b;
	a = __get_cpuid_max( 0x80000000, 0 );
	if ( a >= 0x80000001u && a <= 0x8000ffffu )
	{
		r->max_ext = a;
		r->ext1_edx = BDGN_CpuidEdx( 0x80000001 );
		if ( a >= 0x80000006u && __get_cpuid( 0x80000006, &a, &b, &c, &d ) )
			r->ext6_ecx = c;
	}
}
#else
void Q3E_ReadCPUID (q3e_cpuid_raw_t *r)
{
	memset (r, 0, sizeof(*r));
}
int Q3E_CPUHasSSE2 (void)
{
	return 0;
}
int Q3E_CPUHasMMX (void)
{
#ifdef Q3D_SHIM
	return 1;
#else
	return 0;
#endif
}
int Q3E_CPUHasSSE (void)
{
#ifdef Q3D_SHIM
	return 1;
#else
	return 0;
#endif
}
#endif

/* AVX requires the OS to preserve both XMM and YMM state. */
int Q3E_UsableAVXLevel(void)
{
#if defined(_MSC_VER) && !defined(Q3D_SHIM)
	static int level=-1;
	q3e_cpuid_raw_t raw;
	unsigned xcr0=0;
	if (level>=0) return level;
	level=0;
	Q3E_ReadCPUID(&raw);
	if ((raw.leaf1_ecx&0x18000000u)!=0x18000000u) return 0;
	__try {
		__asm {
			xor ecx, ecx
			_emit 0x0f
			_emit 0x01
			_emit 0xd0 /* XGETBV; VS2005 predates its mnemonic. */
			mov xcr0, eax
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
	if ((xcr0&6)==6) level=(raw.leaf7_ebx&(1u<<5))?2:1;
	return level;
#else
	return 0;
#endif
}

/*
===============
Q3E_RecursiveHullCheck

AMD 3DNow! patch, engine E1 (CM_RecursiveHullCheck).  Same sides, fractions and
recursion order as id.  t = ( t1, t2 ) and the two split fractions are computed as
pairs; 1/(t1-t2) is pfrcp refined with pfrcpit1/pfrcpit2.
===============
*/
#define Q3E_DIST_EPSILON	0.03125f

void Q3E_RecursiveHullCheck (const q3e_hull_t *h, int num, const float *f, const float *p1, const float *p2)
{
	static const int	k_absmask[2] = { 0x7fffffff, 0x7fffffff };
	static const float	k_one[2] = { 1.0f, 1.0f };
	static const float	k_epsfront[2] = { Q3E_DIST_EPSILON, -Q3E_DIST_EPSILON };	// t1 > t2: frac, frac2
	static const float	k_epsback[2] = { Q3E_DIST_EPSILON, Q3E_DIST_EPSILON };	// t1 < t2
	const q3e_node_t	*node;
	const q3e_plane_t	*plane;
	__m64	vf1, t, off, offb, ge, lt, a, b, r, d, fr, fr1, fr2, df, midf;
	float	f1[2], f2[2], mid1[3], mid2[3];
	int		side;

	vf1 = LD32( f );
	if ( LO( _m_pfcmpge( vf1, LD32( h->fraction ) ) ) )
	{	// already hit something nearer
		_m_femms();
		return;
	}

	if ( num < 0 )
	{
		_m_femms();
		h->leaf( -1 - num );
		return;
	}

	node = h->nodes + num;
	plane = node->plane;

	if ( plane->type < 3 )
	{
		t = _mm_unpacklo_pi32( LD32( p1 + plane->type ), LD32( p2 + plane->type ) );
		off = LD32( h->extents + plane->type );
	}
	else
	{
		a = _m_pfmul( LD64( plane->normal ), LD64( p1 ) );
		b = _m_pfmul( LD64( plane->normal ), LD64( p2 ) );
		t = _m_pfacc( a, b );
		t = _m_pfadd( t, _m_pfmul( BCAST( LD32( plane->normal + 2 ) ),
								   _mm_unpacklo_pi32( LD32( p1 + 2 ), LD32( p2 + 2 ) ) ) );
		if ( h->ispoint )
			off = _mm_setzero_si64();
		else
		{
			a = _mm_and_si64( _m_pfmul( LD64( h->extents ), LD64( plane->normal ) ), LD64( k_absmask ) );
			b = _mm_and_si64( _m_pfmul( LD32( h->extents + 2 ), LD32( plane->normal + 2 ) ), LD64( k_absmask ) );
			off = _m_pfadd( _m_pfacc( a, a ), b );
		}
	}
	t = _m_pfsub( t, BCAST( LD32( &plane->dist ) ) );
	offb = BCAST( off );

	// see which sides we need to consider
	ge = _m_pfcmpge( t, offb );
	if ( LO( ge ) && HI( ge ) )
	{
		_m_femms();
		Q3E_RecursiveHullCheck( h, node->children[0], f, p1, p2 );
		return;
	}
	lt = _m_pfcmpgt( _m_pfsub( _mm_setzero_si64(), offb ), t );
	if ( LO( lt ) && HI( lt ) )
	{
		_m_femms();
		Q3E_RecursiveHullCheck( h, node->children[1], f, p1, p2 );
		return;
	}

	// put the crosspoint DIST_EPSILON pixels on the near side; fr = ( frac, frac2 )
	a = _mm_unpackhi_pi32( t, t );		// t2
	b = BCAST( t );						// t1
	if ( LO( _m_pfcmpgt( a, b ) ) )
	{	// t1 < t2
		side = 1;
		fr = _m_pfadd( _m_pfadd( b, _mm_unpacklo_pi32( _m_pfsub( _mm_setzero_si64(), off ), off ) ), LD64( k_epsback ) );
	}
	else if ( LO( _m_pfcmpgt( b, a ) ) )
	{	// t1 > t2
		side = 0;
		fr = _m_pfadd( _m_pfadd( b, _mm_unpacklo_pi32( off, _m_pfsub( _mm_setzero_si64(), off ) ) ), LD64( k_epsfront ) );
	}
	else
	{
		side = 0;
		fr = _mm_unpacklo_pi32( LD32( k_one ), _mm_setzero_si64() );
	}
	if ( side || LO( _m_pfcmpgt( b, a ) ) )
	{
		d = BCAST( _m_pfsub( b, a ) );
		r = _m_pfrcp( d );
		r = _m_pfrcpit2( _m_pfrcpit1( d, r ), r );
		fr = _m_pfmul( fr, r );
	}
	fr = _m_pfmin( _m_pfmax( fr, _mm_setzero_si64() ), LD64( k_one ) );

	// midf = p1f + ( p2f - p1f ) * frac, for both fractions
	df = BCAST( _m_pfsub( LD32( f + 1 ), vf1 ) );
	midf = _m_pfadd( BCAST( vf1 ), _m_pfmul( df, fr ) );
	ST32( f1, vf1 );
	ST32( f1 + 1, midf );
	ST32( f2, _mm_unpackhi_pi32( midf, midf ) );
	ST32( f2 + 1, LD32( f + 1 ) );

	// mid = p1 + frac * ( p2 - p1 )
	fr1 = BCAST( fr );
	fr2 = _mm_unpackhi_pi32( fr, fr );
	a = _m_pfsub( LD64( p2 ), LD64( p1 ) );
	b = _m_pfsub( LD32( p2 + 2 ), LD32( p1 + 2 ) );
	ST64( mid1, _m_pfadd( LD64( p1 ), _m_pfmul( fr1, a ) ) );
	ST32( mid1 + 2, _m_pfadd( LD32( p1 + 2 ), _m_pfmul( fr1, b ) ) );
	ST64( mid2, _m_pfadd( LD64( p1 ), _m_pfmul( fr2, a ) ) );
	ST32( mid2 + 2, _m_pfadd( LD32( p1 + 2 ), _m_pfmul( fr2, b ) ) );

	_m_femms();
	Q3E_RecursiveHullCheck( h, node->children[side], f1, p1, mid1 );
	Q3E_RecursiveHullCheck( h, node->children[side^1], f2, mid2, p2 );
}

/*
===============
Q3E_AddParticles

AMD 3DNow! patch, engine E2 (CL_AddParticles + V_AddParticle).  Same list handling as id.
===============
*/
q3e_cparticle_t *Q3E_AddParticles (q3e_cparticle_t *p, q3e_cparticle_t **freelist, int cltime,
								   q3e_particle_t *out, int *nout, int maxout)
{
	static const float	k_ms = 0.001f;
	static const float	k_one = 1.0f;
	static const float	k_instant = -10000.0f;	// INSTANT_PARTICLE
	q3e_cparticle_t		*next, *active = 0, *tail = 0;
	int					n = *nout, instant;
	__m64				now, tm, al, t2, xy, z;

	now = _m_pi2fd( _mm_cvtsi32_si64( cltime ) );
	tm = _mm_setzero_si64();
	for ( ; p ; p = next )
	{
		next = p->next;

		instant = LO( _m_pfcmpeq( LD32( &p->alphavel ), LD32( &k_instant ) ) );
		if ( !instant )
		{
			tm = BCAST( _m_pfmul( _m_pfsub( now, LD32( &p->time ) ), LD32( &k_ms ) ) );
			al = _m_pfadd( LD32( &p->alpha ), _m_pfmul( tm, LD32( &p->alphavel ) ) );
			if ( !LO( _m_pfcmpgt( al, _mm_setzero_si64() ) ) )
			{	// faded out
				p->next = *freelist;
				*freelist = p;
				continue;
			}
		}
		else
			al = LD32( &p->alpha );

		p->next = 0;
		if ( !tail )
			active = tail = p;
		else
		{
			tail->next = p;
			tail = p;
		}

		if ( n < maxout )
		{
			al = _m_pfmin( al, LD32( &k_one ) );
			t2 = _m_pfmul( tm, tm );
			xy = _m_pfadd( _m_pfadd( LD64( p->org ), _m_pfmul( LD64( p->vel ), tm ) ), _m_pfmul( LD64( p->accel ), t2 ) );
			z = _m_pfadd( _m_pfadd( LD32( p->org + 2 ), _m_pfmul( LD32( p->vel + 2 ), tm ) ), _m_pfmul( LD32( p->accel + 2 ), t2 ) );
			ST64( out[n].origin, xy );
			ST32( out[n].origin + 2, z );
			out[n].color = LO( _m_pf2id( LD32( &p->color ) ) );
			ST32( &out[n].alpha, al );
			n++;
		}

		if ( instant )
		{	// integer stores: no x87 code while MMX is active
			*(int *)&p->alphavel = 0;
			*(int *)&p->alpha = 0;
		}
	}
	_m_femms();
	*nout = n;
	return active;
}

/*
===============
Q3E_PaintFrom8 / Q3E_PaintFrom16

BDGN 3.50: AMD's sound mixing (quake2.exe 0x438a80 S_PaintChannelFrom8, 0x438bc0 S_PaintChannelFrom16).
8 bit: id's volume tables; the left and right value of a sample go into one MMX register and are added to the
paint buffer with one paddd, two samples per step like AMD.  Bit-identical to id.
16 bit: data * vol in float (pi2fd / pfmul / pf2id) and >> 8 like AMD, at most one step off id's integer product.
===============
*/
void Q3E_PaintFrom8 (int *samp, const unsigned char *sfx, int count, const int *lscale, const int *rscale)
{
	__m64	*out = (__m64 *)samp;
	int		i;

	for ( i = 0 ; i + 2 <= count ; i += 2 )
	{
		out[i] = _mm_add_pi32( out[i], _mm_unpacklo_pi32( _mm_cvtsi32_si64( lscale[sfx[i]] ), _mm_cvtsi32_si64( rscale[sfx[i]] ) ) );
		out[i+1] = _mm_add_pi32( out[i+1], _mm_unpacklo_pi32( _mm_cvtsi32_si64( lscale[sfx[i+1]] ), _mm_cvtsi32_si64( rscale[sfx[i+1]] ) ) );
	}
	if ( i < count )
		out[i] = _mm_add_pi32( out[i], _mm_unpacklo_pi32( _mm_cvtsi32_si64( lscale[sfx[i]] ), _mm_cvtsi32_si64( rscale[sfx[i]] ) ) );
	_m_femms();
}

int Q3E_PaintFrom16 (int *samp, const short *sfx, int count, int leftvol, int rightvol)
{
	__m64	*out = (__m64 *)samp;
	__m64	vol, a, b;
	int		i;

	if ( leftvol < 0 || rightvol < 0 || leftvol > 0xff00 || rightvol > 0xff00 )
		return 0;	// s_volume above 1: id's int product wraps, pf2id would saturate; the caller uses id's code

	vol = _m_pi2fd( _mm_unpacklo_pi32( _mm_cvtsi32_si64( leftvol ), _mm_cvtsi32_si64( rightvol ) ) );
	for ( i = 0 ; i + 2 <= count ; i += 2 )
	{
		a = _m_pi2fd( BCAST( _mm_cvtsi32_si64( sfx[i] ) ) );
		b = _m_pi2fd( BCAST( _mm_cvtsi32_si64( sfx[i+1] ) ) );
		out[i] = _mm_add_pi32( out[i], _mm_srai_pi32( _m_pf2id( _m_pfmul( a, vol ) ), 8 ) );
		out[i+1] = _mm_add_pi32( out[i+1], _mm_srai_pi32( _m_pf2id( _m_pfmul( b, vol ) ), 8 ) );
	}
	if ( i < count )
	{
		a = _m_pi2fd( BCAST( _mm_cvtsi32_si64( sfx[i] ) ) );
		out[i] = _mm_add_pi32( out[i], _mm_srai_pi32( _m_pf2id( _m_pfmul( a, vol ) ), 8 ) );
	}
	_m_femms();
	return 1;
}
