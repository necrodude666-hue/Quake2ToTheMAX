/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// d_scan.c
//
// Portable C scan-level rasterization code, all pixel depths.

#include "r_local.h"
#include "sw_texfilter.h"

unsigned char	*r_turb_pbase, *r_turb_pdest;
fixed16_t		r_turb_s, r_turb_t, r_turb_sstep, r_turb_tstep;
int				*r_turb_turb;
int				r_turb_spancount;

void D_DrawTurbulent8Span (void);


/*
=============
D_WarpScreen

this performs a slight compression of the screen at the same time as
the sine warp, to keep the edges from wrapping
=============
*/
void D_WarpScreen (void)
{
	int		w, h;
	int		u,v, u2, v2;
	byte	*dest;
	int		*turb;
	int		*col;
	byte	**row;

	static int	cached_width, cached_height, cached_vw, cached_vh;
	/* BDGN 3.53a: sized for MAXHEIGHT/MAXWIDTH (id: 1200/1600 entries, which overflowed under water at
	   1920x1080 and above and crashed on a NULL row pointer); +8 covers the 4-pixel loop past an odd width */
	static byte	*rowptr[MAXHEIGHT+AMP2*2+8];
	static int	column[MAXWIDTH+AMP2*2+8];

	//
	// these are constant over resolutions, and can be saved
	//
	w = r_newrefdef.width;
	h = r_newrefdef.height;
	if (w > MAXWIDTH) w = MAXWIDTH;
	if (h > MAXHEIGHT) h = MAXHEIGHT;
	if (w != cached_width || h != cached_height ||
		r_refdef.vrect.width != cached_vw || r_refdef.vrect.height != cached_vh)
	{
		cached_width = w;
		cached_height = h;
		cached_vw = r_refdef.vrect.width;
		cached_vh = r_refdef.vrect.height;
		for (v=0 ; v<h+AMP2*2+8 ; v++)
		{
			v2 = (int)((float)v/(h + AMP2 * 2) * r_refdef.vrect.height);
			if (v2 > r_refdef.vrect.height - 1) v2 = r_refdef.vrect.height - 1;
			if (v2 < 0) v2 = 0;
			rowptr[v] = r_warpbuffer + (WARP_WIDTH * v2);
		}

		for (u=0 ; u<w+AMP2*2+8 ; u++)
		{
			u2 = (int)((float)u/(w + AMP2 * 2) * r_refdef.vrect.width);
			if (u2 > r_refdef.vrect.width - 1) u2 = r_refdef.vrect.width - 1;
			if (u2 < 0) u2 = 0;
			column[u] = u2;
		}
	}

	turb = intsintable + ((int)(r_newrefdef.time*SPEED)&(CYCLE-1));
	dest = vid.buffer + r_newrefdef.y * vid.rowbytes + r_newrefdef.x;

	for (v=0 ; v<h ; v++, dest += vid.rowbytes)
	{
		col = &column[turb[v]];
		row = &rowptr[v];
		for (u=0 ; u<w ; u+=4)
		{
			dest[u+0] = row[turb[u+0]][col[u+0]];
			dest[u+1] = row[turb[u+1]][col[u+1]];
			dest[u+2] = row[turb[u+2]][col[u+2]];
			dest[u+3] = row[turb[u+3]][col[u+3]];
		}
	}
}


#if	!id386

/*
=============
D_DrawTurbulent8Span
=============
*/
void D_DrawTurbulent8Span (void)
{
	int		sturb, tturb;

	do
	{
		sturb = ((r_turb_s + r_turb_turb[(r_turb_t>>16)&(CYCLE-1)])>>16)&63;
		tturb = ((r_turb_t + r_turb_turb[(r_turb_s>>16)&(CYCLE-1)])>>16)&63;
		*r_turb_pdest++ = *(r_turb_pbase + (tturb<<6) + sturb);
		r_turb_s += r_turb_sstep;
		r_turb_t += r_turb_tstep;
	} while (--r_turb_spancount > 0);
}

#endif	// !id386

#ifdef SW_ID386_WATER
/* BDGN 3.55ak: id's r_scana.asm (ref_soft/asm/r_scana.obj) for the inner loop of water, slime and lava;
   sw_asmspans 0 keeps the C loop */
void D_DrawTurbulent8Span_id386 (void);
static int turb_asm;
static void SW_TurbAsmCheck (void)
{
	static cvar_t *cv;
	if (!cv)
		cv = ri.Cvar_Get ("sw_asmspans", "1", 0);
	turb_asm = cv->value != 0;
}
#define D_TURBSPAN()	{ if (turb_asm) D_DrawTurbulent8Span_id386 (); else D_DrawTurbulent8Span (); }
#else
#define SW_TurbAsmCheck()
#define D_TURBSPAN()	D_DrawTurbulent8Span ()
#endif


/*
=============
Turbulent8
=============
*/
void Turbulent8 (espan_t *pspan)
{
	int				count;
	fixed16_t		snext, tnext;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz16stepu, tdivz16stepu, zi16stepu;
	
	r_turb_turb = sintable + ((int)(r_newrefdef.time*SPEED)&(CYCLE-1));

	SW_TurbAsmCheck ();
	r_turb_sstep = 0;	// keep compiler happy
	r_turb_tstep = 0;	// ditto

	r_turb_pbase = (unsigned char *)cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;

	do
	{
		r_turb_pdest = (unsigned char *)((byte *)d_viewbuffer +
				(r_screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		r_turb_s = (int)(sdivz * z) + sadjust;
		if (r_turb_s > bbextents)
			r_turb_s = bbextents;
		else if (r_turb_s < 0)
			r_turb_s = 0;

		r_turb_t = (int)(tdivz * z) + tadjust;
		if (r_turb_t > bbextentt)
			r_turb_t = bbextentt;
		else if (r_turb_t < 0)
			r_turb_t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 16)
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if (count)
			{
			// calculate s/z, t/z, zi->fixed s and t at far end of span,
			// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				r_turb_sstep = (snext - r_turb_s) >> 4;
				r_turb_tstep = (tnext - r_turb_t) >> 4;
			}
			else
			{
			// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
			// can't step off polygon), clamp, calculate s and t steps across
			// span by division, biasing steps low so we don't run off the
			// texture
				spancountminus1 = (float)(r_turb_spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				if (r_turb_spancount > 1)
				{
					r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
					r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
				}
			}

			r_turb_s = r_turb_s & ((CYCLE<<16)-1);
			r_turb_t = r_turb_t & ((CYCLE<<16)-1);

			D_TURBSPAN();

			r_turb_s = snext;
			r_turb_t = tnext;

		} while (count > 0);

	} while ((pspan = pspan->pnext) != NULL);
}

//====================
//PGM
/*
=============
NonTurbulent8 - this is for drawing scrolling textures. they're warping water textures
	but the turbulence is automatically 0.
=============
*/
void NonTurbulent8 (espan_t *pspan)
{
	int				count;
	fixed16_t		snext, tnext;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz16stepu, tdivz16stepu, zi16stepu;
	
//	r_turb_turb = sintable + ((int)(r_newrefdef.time*SPEED)&(CYCLE-1));
	r_turb_turb = blanktable;

	SW_TurbAsmCheck ();
	r_turb_sstep = 0;	// keep compiler happy
	r_turb_tstep = 0;	// ditto

	r_turb_pbase = (unsigned char *)cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;

	do
	{
		r_turb_pdest = (unsigned char *)((byte *)d_viewbuffer +
				(r_screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		r_turb_s = (int)(sdivz * z) + sadjust;
		if (r_turb_s > bbextents)
			r_turb_s = bbextents;
		else if (r_turb_s < 0)
			r_turb_s = 0;

		r_turb_t = (int)(tdivz * z) + tadjust;
		if (r_turb_t > bbextentt)
			r_turb_t = bbextentt;
		else if (r_turb_t < 0)
			r_turb_t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 16)
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if (count)
			{
			// calculate s/z, t/z, zi->fixed s and t at far end of span,
			// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				r_turb_sstep = (snext - r_turb_s) >> 4;
				r_turb_tstep = (tnext - r_turb_t) >> 4;
			}
			else
			{
			// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
			// can't step off polygon), clamp, calculate s and t steps across
			// span by division, biasing steps low so we don't run off the
			// texture
				spancountminus1 = (float)(r_turb_spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				if (r_turb_spancount > 1)
				{
					r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
					r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
				}
			}

			r_turb_s = r_turb_s & ((CYCLE<<16)-1);
			r_turb_t = r_turb_t & ((CYCLE<<16)-1);

			D_TURBSPAN();

			r_turb_s = snext;
			r_turb_t = tnext;

		} while (count > 0);

	} while ((pspan = pspan->pnext) != NULL);
}
//PGM
//====================


#ifdef SW_ID386_SPANS
/* BDGN 3.55y: id's own x86 span code is linked again (prebuilt from ref_soft/asm/r_draw16.asm, unchanged apart from
   the two public names, so Visual C++ Express without ml.exe can link it). 3.21's ref_soft drew every world pixel with
   it; the C loops divide every 8 pixels and stall the Pentium's FPU. Used for the plain path only: texture filtering
   and the prefetch test keep the C loops. sw_asmspans 0 = C loops (test switch). */
void D_DrawSpans16_id386 (espan_t *pspan);
void D_DrawZSpans_id386 (espan_t *pspan);
static int SW_PrefetchMode (void);
static int SW_UseId386Spans (void)
{
	static cvar_t *cv;
	if ( !cv )
		cv = ri.Cvar_Get( "sw_asmspans", "1", 0 );
	return cv->value != 0 && !SW_PrefetchMode();
}
#endif

#if	!id386 && !defined(SW_ASM_SPANS)	// BDGN 3.51p: SW_ASM_SPANS = id's asm (ref_soft.vcproj "Release ASM")

/*
=============
D_DrawSpans16

  FIXME: actually make this subdivide by 16 instead of 8!!!
=============
*/
/*
=============
BDGN 3.52r: prefetch test toggle (sw_prefetch 0/1, saved). With 1 the span loops ask the CPU to fetch the texels of
the next 8-pixel segment into the cache while the current one is drawn: 3DNow! PREFETCH on K6-2/K6-III/Athlon,
SSE PREFETCHNTA on Pentium III and later, nothing on other CPUs (chosen with CPUID once). Intrinsics, so the
compiler still optimises the loops; the instructions only run on CPUs that have them.
=============
*/
#if defined(_MSC_VER) && defined(_M_IX86)
#include <intrin.h>
#include <mm3dnow.h>
#include <xmmintrin.h>
static int SW_PrefetchCpu (void)
{	/* 1 = 3DNow! PREFETCH, 2 = SSE PREFETCHNTA, 0 = none */
	int r[4];
	__cpuid( r, 0x80000000 );
	if ( (unsigned)r[0] >= 0x80000001 )
	{
		__cpuid( r, 0x80000001 );
		if ( r[3] & 0x80000000 )
			return 1;
	}
	__cpuid( r, 1 );
	return ( r[3] & 0x02000000 ) ? 2 : 0;
}
#define SW_PREFETCH(mode,p) do { if ( (mode) == 1 ) _m_prefetch( (void *)(p) ); else if ( (mode) == 2 ) _mm_prefetch( (const char *)(p), _MM_HINT_NTA ); } while (0)
#elif defined(__GNUC__) && defined(__i386__)
#include <cpuid.h>	/* gcc K6 test build: same choice as above */
static int SW_PrefetchCpu (void)
{
	unsigned a, b, c, d;
	if ( __get_cpuid( 0x80000001, &a, &b, &c, &d ) && ( d & 0x80000000u ) )
		return 1;
	if ( __get_cpuid( 1, &a, &b, &c, &d ) && ( d & 0x02000000u ) )
		return 2;
	return 0;
}
#define SW_PREFETCH(mode,p) do { if ( (mode) == 1 ) __asm__ __volatile__ ( "prefetch (%0)" : : "r" (p) ); \
	else if ( (mode) == 2 ) __asm__ __volatile__ ( "prefetchnta (%0)" : : "r" (p) ); } while (0)
#else
static int SW_PrefetchCpu (void) { return 0; }
#define SW_PREFETCH(mode,p) do { (void)(mode); (void)(p); } while (0)
#endif
static int SW_PrefetchMode (void)
{
	static int cpu = -1;
	static cvar_t *cv;
	if ( !cv )
		cv = ri.Cvar_Get( "sw_prefetch", "0", 0 );	/* BDGN 3.56c: test switch, no longer saved (a saved 1 kept walls and floors in C) */
	if ( !cv->value )
		return 0;
	if ( cpu < 0 )
		cpu = SW_PrefetchCpu();
	return cpu;
}
int SW_PrefetchCpuKind (void)	/* BDGN 3.56c: for sw_paths (1 = 3DNow! PREFETCH, 2 = SSE, 0 = none) */
{
	return SW_PrefetchCpu();
}

/*
=============
BDGN 3.52n: dithered texture filtering (sw_texfilter 1), the trick of Unreal's software renderer: no blending, but
every pixel reads its texel a fraction of a texel away, by a fixed 2x2 screen pattern. Neighbouring pixels read
different texels, so near walls show a fine pattern instead of big square texels and at a distance the surface
looks smooth. Works with the 8-bit palette (it only picks another texel) and costs one add and two clamps per pixel.
Offsets in 1/4 texels (16.16): (x even, y even) 0.25/0.00, (odd, even) 0.50/0.75, (even, odd) 0.75/0.50,
(odd, odd) 0.00/0.25.
=============
*/
static const fixed16_t sw_dither_s[2][2] = { { 0x4000, 0x8000 }, { 0xC000, 0x0000 } };	/* [y&1][x&1] */
static const fixed16_t sw_dither_t[2][2] = { { 0x0000, 0xC000 }, { 0x8000, 0x4000 } };

static void D_DrawSpans16FilteredLane (espan_t *pspan, int bilinear, const surfcache_t *coarse, int weight, int lane, int lanes)
{
	int				count, spancount, x;
	unsigned char	*pbase, *pdest;
	fixed16_t		s, t, snext, tnext, sstep, tstep, ss, tt;
	const fixed16_t	*ds, *dt;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz8stepu, tdivz8stepu, zi8stepu;

	int				pf = SW_PrefetchMode();	/* BDGN 3.52r */

	sstep = 0;
	tstep = 0;
	pbase = (unsigned char *)cacheblock;
	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;

	do
	{
		if (lanes>1 && pspan->v%lanes!=lane) continue;
		pdest = (unsigned char *)((byte *)d_viewbuffer + (r_screenwidth * pspan->v) + pspan->u);
		count = pspan->count;
		ds = sw_dither_s[pspan->v & 1];
		dt = sw_dither_t[pspan->v & 1];
		x = pspan->u & 1;

		du = (float)pspan->u;
		dv = (float)pspan->v;
		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;

		s = (int)(sdivz * z) + sadjust;
		if (s > bbextents) s = bbextents; else if (s < 0) s = 0;
		t = (int)(tdivz * z) + tadjust;
		if (t > bbextentt) t = bbextentt; else if (t < 0) t = 0;

		do
		{
			spancount = count >= 8 ? 8 : count;
			count -= spancount;
			if (count)
			{
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi;
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents) snext = bbextents; else if (snext < 8) snext = 8;
				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt) tnext = bbextentt; else if (tnext < 8) tnext = 8;
				sstep = (snext - s) >> 3;
				tstep = (tnext - t) >> 3;
				if (pf)
					SW_PREFETCH( pf, pbase + (snext >> 16) + (tnext >> 16) * cachewidth );
			}
			else
			{
				spancountminus1 = (float)(spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents) snext = bbextents; else if (snext < 8) snext = 8;
				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt) tnext = bbextentt; else if (tnext < 8) tnext = 8;
				if (spancount > 1)
				{
					sstep = (snext - s) / (spancount - 1);
					tstep = (tnext - t) / (spancount - 1);
				}
			}

			if (coarse)
			{
				do
				{
					sw_filter_color_t a=SW_FilterSample(pbase,cachewidth,cacheheight,s,t,NULL,NULL);
					sw_filter_color_t b=SW_FilterSample(coarse->data,coarse->width,coarse->height,s>>1,t>>1,NULL,NULL);
					*pdest++ = a.exact && b.exact && a.index==b.index ? a.index :
						SW_FilterPalette(SW_FilterMix(a.rgb,b.rgb,weight));
					s += sstep;
					t += tstep;
				} while (--spancount > 0);
			}
			else if (bilinear)
			{
				do
				{
					*pdest++ = SW_BilinearTexel(pbase, cachewidth, cacheheight, s, t, NULL,NULL);
					s += sstep;
					t += tstep;
				} while (--spancount > 0);
			}
			else do
			{	/* the offset never leaves the surface: clamped to the last texel */
				ss = s + ds[x];
				if (ss > bbextents) ss = bbextents;
				tt = t + dt[x];
				if (tt > bbextentt) tt = bbextentt;
				*pdest++ = *(pbase + (ss >> 16) + (tt >> 16) * cachewidth);
				x ^= 1;
				s += sstep;
				t += tstep;
			} while (--spancount > 0);

			s = snext;
			t = tnext;
		} while (count > 0);

	} while ((pspan = pspan->pnext) != NULL);
}

typedef struct {
 espan_t *spans;
 int bilinear, weight;
 const surfcache_t *coarse;
} sw_filter_task_t;

static void D_DrawFilterLane (void *data, int lane, int lanes)
{
 const sw_filter_task_t *task=(const sw_filter_task_t *)data;
 D_DrawSpans16FilteredLane(task->spans,task->bilinear,task->coarse,task->weight,lane,lanes);
}

static void D_DrawSpans16Filtered (espan_t *spans, int bilinear, const surfcache_t *coarse, int weight)
{
 static cvar_t *parallel;
 sw_filter_task_t task;
 int threads;
 if(!parallel) parallel=ri.Cvar_Get("sw_filter_threads","0",0);
 threads=(int)parallel->value;
 if(!threads && bilinear) threads=R_SMP_FilterThreads(0);
 if(threads>=2 && threads<=4) {
  int pixels=0;
  espan_t *span;
  for(span=spans;span && pixels<32768;span=span->pnext) pixels+=span->count;
  if(pixels>=32768) {
   task.spans=spans;
   task.bilinear=bilinear;
   task.coarse=coarse;
   task.weight=weight;
   if(R_SMP_DrawFiltered(D_DrawFilterLane,&task,threads)) return;
  }
 }
 D_DrawSpans16FilteredLane(spans,bilinear,coarse,weight,0,1);
}

void D_DrawSpansTrilinear (espan_t *spans,const surfcache_t *coarse,int weight)
{
	D_DrawSpans16Filtered(spans,1,coarse,weight);
}

/*
BDGN 3.57: Build engine floors (sw_buildfloors). Floors and ceilings are drawn with one texture step for the whole
span: s and t are worked out (with a divide) at both ends of each span and stepped evenly between them, as the Build
engine (Duke Nukem 3D) and the PlayStation drew them. When the view looks level a floor span lies at one depth, so it
looks right; looking up or down the floors bend a little, as in those games. Two divides a span instead of one every
16 pixels.
*/
void D_DrawSpansAffine (espan_t *pspan)
{
	int				count;
	unsigned char	*pbase, *pdest;
	fixed16_t		s, t, s1, t1, sstep, tstep;
	float			sdivz, tdivz, zi, z, du, dv, last;

	pbase = (unsigned char *)cacheblock;
	do
	{
		pdest = (unsigned char *)((byte *)d_viewbuffer + (r_screenwidth * pspan->v) + pspan->u);
		count = pspan->count;
		du = (float)pspan->u;
		dv = (float)pspan->v;
		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;
		s = (int)(sdivz * z) + sadjust;
		if (s > bbextents) s = bbextents; else if (s < 0) s = 0;
		t = (int)(tdivz * z) + tadjust;
		if (t > bbextentt) t = bbextentt; else if (t < 0) t = 0;
		if (count > 1)
		{
			last = (float)(count - 1);
			sdivz += d_sdivzstepu * last;
			tdivz += d_tdivzstepu * last;
			zi += d_zistepu * last;
			z = (float)0x10000 / zi;
			s1 = (int)(sdivz * z) + sadjust;
			if (s1 > bbextents) s1 = bbextents; else if (s1 < 8) s1 = 8;
			t1 = (int)(tdivz * z) + tadjust;
			if (t1 > bbextentt) t1 = bbextentt; else if (t1 < 8) t1 = 8;
			sstep = (s1 - s) / (count - 1);
			tstep = (t1 - t) / (count - 1);
		}
		else
			sstep = tstep = 0;
		do
		{
			*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
			s += sstep;
			t += tstep;
		} while (--count > 0);
	} while ((pspan = pspan->pnext) != NULL);
}

void D_DrawSpans16 (espan_t *pspan)
{
	int				count, spancount, pf;
	unsigned char	*pbase, *pdest;
	fixed16_t		s, t, snext, tnext, sstep, tstep;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz8stepu, tdivz8stepu, zi8stepu;

	if ( sw_texture_filter_mode )
	{
		D_DrawSpans16Filtered( pspan, sw_texture_filter_mode >= 2, NULL, 0 );
		return;
	}
#ifdef SW_ID386_SPANS
	if ( SW_UseId386Spans() )
	{	/* BDGN 3.55y: id's Pentium span loop (r_draw16.asm): 16-pixel steps, the divide overlaps the pixel loop */
		D_DrawSpans16_id386( pspan );
		return;
	}
#endif

	sstep = 0;	// keep compiler happy
	tstep = 0;	// ditto

	pf = SW_PrefetchMode();	/* BDGN 3.52r */
	pbase = (unsigned char *)cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;

	do
	{
		pdest = (unsigned char *)((byte *)d_viewbuffer +
				(r_screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		s = (int)(sdivz * z) + sadjust;
		if (s > bbextents)
			s = bbextents;
		else if (s < 0)
			s = 0;

		t = (int)(tdivz * z) + tadjust;
		if (t > bbextentt)
			t = bbextentt;
		else if (t < 0)
			t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 8)
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if (count)
			{
			// calculate s/z, t/z, zi->fixed s and t at far end of span,
			// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;	// guard against round-off error on <0 steps

				sstep = (snext - s) >> 3;
				tstep = (tnext - t) >> 3;
				if (pf)
					SW_PREFETCH( pf, pbase + (snext >> 16) + (tnext >> 16) * cachewidth );	/* BDGN 3.52r */
			}
			else
			{
			// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
			// can't step off polygon), clamp, calculate s and t steps across
			// span by division, biasing steps low so we don't run off the
			// texture
				spancountminus1 = (float)(spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;	// guard against round-off error on <0 steps

				if (spancount > 1)
				{
					sstep = (snext - s) / (spancount - 1);
					tstep = (tnext - t) / (spancount - 1);
				}
			}

			do
			{
				*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
				s += sstep;
				t += tstep;
			} while (--spancount > 0);

			s = snext;
			t = tnext;

		} while (count > 0);

	} while ((pspan = pspan->pnext) != NULL);
}

#endif


#if	!id386 && !defined(SW_ASM_SPANS)	// BDGN 3.51p: SW_ASM_SPANS = id's asm (ref_soft.vcproj "Release ASM")

/*
=============
D_DrawZSpans
=============
*/
static void D_DrawZSpans_C (espan_t *pspan);
void D_DrawZSpans (espan_t *pspan)
{
#ifdef SW_ID386_SPANS
	if ( SW_UseId386Spans() )
	{
		D_DrawZSpans_id386( pspan );
		return;
	}
#endif
	D_DrawZSpans_C( pspan );
}
static void D_DrawZSpans_C (espan_t *pspan)
{
	int				count, doublecount, izistep;
	int				izi;
	short			*pdest;
	unsigned		ltemp;
	float			zi;
	float			du, dv;

// FIXME: check for clamping/range problems
// we count on FP exceptions being turned off to avoid range problems
	izistep = (int)(d_zistepu * 0x8000 * 0x10000);

	do
	{
		pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;

		count = pspan->count;

	// calculate the initial 1/z
		du = (float)pspan->u;
		dv = (float)pspan->v;

		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
	// we count on FP exceptions being turned off to avoid range problems
		izi = (int)(zi * 0x8000 * 0x10000);

		if ((long)pdest & 0x02)
		{
			*pdest++ = (short)(izi >> 16);
			izi += izistep;
			count--;
		}

		if ((doublecount = count >> 1) > 0)
		{
			do
			{
				ltemp = izi >> 16;
				izi += izistep;
				ltemp |= izi & 0xFFFF0000;
				izi += izistep;
				*(int *)pdest = ltemp;
				pdest += 2;
			} while (--doublecount > 0);
		}

		if (count & 1)
			*pdest = (short)(izi >> 16);

	} while ((pspan = pspan->pnext) != NULL);
}

#endif

