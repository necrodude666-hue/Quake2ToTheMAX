/*
BDGN 3.52t: built-in frame profiler for the renderers (ref_gl, ref_soft). VS2005 C89, works on Win98 and on every
CPU with RDTSC (Pentium, K6, ...).

The renderer time of a frame is split into areas. BDGN_ProfPush(area) / BDGN_ProfPop() bracket a piece of work; the
time is "exclusive": while a nested area runs, its parent does not count, so the areas add up to the renderer time.
PROF_OTHER is the rest of the 3D view (frustum, sorting, state changes). Client, game, sound and the 2D HUD are the
part of the frame outside the renderer; the client computes that from its own frame time.

Switched on by the cvar r_profile 1 (the extended benchmark sets it). Every frame the renderer publishes its totals
since the last reset in the cvar r_profile_data:  "frames c0 c1 ... c10"  (cycles per area, summed); r_profile_reset 1
zeroes them at the next frame. Off: one test of an int per measuring point.

One file of each renderer defines BDGN_PROF_IMPL before including this header (the data and functions live there).
*/
#ifndef BDGN_PROF_H
#define BDGN_PROF_H

#define PROF_OTHER		0	/* rest of the 3D view */
#define PROF_WORLD		1	/* world surfaces (and brush entities) */
#define PROF_LIGHT		2	/* dynamic lights: marking, lightmap building/upload (GL), flashblend */
#define PROF_SURFCACHE	3	/* software: building lit surfaces into the surface cache (incl. coloured light) */
#define PROF_MODELS		4	/* 3D models (alias) */
#define PROF_SPRITES	5	/* 2D sprites: simple / classic enemies, items, sp2 */
#define PROF_WEAPON		6	/* the view weapon (3D or 2D) */
#define PROF_PARTICLES	7
#define PROF_ALPHA		8	/* water, glass, translucent surfaces */
#define PROF_POST		9	/* fog (software), warp, palette / screen blend */
#define PROF_SWAP		10	/* showing the frame (buffer swap / blit) */
#define PROF_COUNT		11

/* BDGN 3.52v: what was drawn, summed per frame while profiling (published in r_profile_count: "frames n0 .. n6") */
#define PROFN_SURFS		0	/* world + brush surfaces drawn (GL polygons / software polygons) */
#define PROFN_MODELS	1	/* 3D models drawn (incl. a 3D view weapon) */
#define PROFN_SPRITES	2	/* sprites drawn (simple / classic enemies, items, sp2, 2D weapon) */
#define PROFN_PARTICLES	3
#define PROFN_DLIGHTS	4	/* dynamic lights in the scene */
#define PROFN_LIGHTMAPS	5	/* GL: lightmap uploads (blocks + dynamic sub-images); software: surface cache builds */
#define PROFN_ENTITIES	6	/* entities in the scene */
#define PROFN_COUNT		7

extern int bdgn_prof_on;
void BDGN_ProfPush (int area);
void BDGN_ProfPop (void);
void BDGN_ProfFrame (void);		/* once per frame, after the swap */

#define BDGN_PROF_PUSH(a)	do { if ( bdgn_prof_on ) BDGN_ProfPush( a ); } while (0)
#define BDGN_PROF_POP()		do { if ( bdgn_prof_on ) BDGN_ProfPop(); } while (0)
extern double bdgn_prof_n[PROFN_COUNT];
#define BDGN_PROF_N(i,n)	do { if ( bdgn_prof_on ) bdgn_prof_n[i] += (n); } while (0)
#define BDGN_PROF_CALL(a,call)	do { if ( bdgn_prof_on ) { BDGN_ProfPush( a ); call; BDGN_ProfPop(); } else { call; } } while (0)

#ifdef BDGN_PROF_IMPL
#if defined(_MSC_VER) && defined(_M_IX86)
#include <intrin.h>
#define BDGN_TICK() ( (double)(__int64)__rdtsc() )
#elif defined(__GNUC__) && defined(__i386__)
#define BDGN_TICK() ( (double)(long long)__builtin_ia32_rdtsc() )	/* gcc K6 test build */
#else
#define BDGN_TICK() ( 0.0 )
#endif

int bdgn_prof_on;
double bdgn_prof_n[PROFN_COUNT];
static double	prof_total[PROF_COUNT], prof_mark;
static int		prof_stack[16], prof_depth;
static unsigned	prof_frames;
static cvar_t	*prof_cv_on, *prof_cv_reset;

void BDGN_ProfPush (int area)
{
	double t = BDGN_TICK();
	if ( prof_depth > 0 && prof_depth <= 16 )
		prof_total[prof_stack[prof_depth-1]] += t - prof_mark;
	if ( prof_depth < 16 )
		prof_stack[prof_depth] = area;
	prof_depth++;
	prof_mark = t;
}

void BDGN_ProfPop (void)
{
	double t = BDGN_TICK();
	if ( prof_depth > 0 && prof_depth <= 16 )
		prof_total[prof_stack[prof_depth-1]] += t - prof_mark;
	if ( prof_depth > 0 )
		prof_depth--;
	prof_mark = t;
}

void BDGN_ProfFrame (void)
{
	char	buf[512];
	int		i, n;
	if ( !prof_cv_on )
	{
		prof_cv_on = ri.Cvar_Get( "r_profile", "0", 0 );
		prof_cv_reset = ri.Cvar_Get( "r_profile_reset", "0", 0 );
		ri.Cvar_Get( "r_profile_data", "", 0 );
		ri.Cvar_Get( "r_profile_count", "", 0 );
	}
	prof_depth = 0;
	if ( prof_cv_reset->value )
	{
		for ( i = 0; i < PROF_COUNT; i++ )
			prof_total[i] = 0;
		for ( i = 0; i < PROFN_COUNT; i++ )
			bdgn_prof_n[i] = 0;
		prof_frames = 0;
		ri.Cvar_Set( "r_profile_reset", "0" );
	}
	if ( !bdgn_prof_on )
	{
		bdgn_prof_on = prof_cv_on->value != 0;
		return;
	}
	prof_frames++;
	n = sprintf( buf, "%u", prof_frames );
	for ( i = 0; i < PROF_COUNT && n < (int)sizeof( buf ) - 24; i++ )
		n += sprintf( buf + n, " %.0f", prof_total[i] );
	ri.Cvar_Set( "r_profile_data", buf );
	n = sprintf( buf, "%u", prof_frames );
	for ( i = 0; i < PROFN_COUNT && n < (int)sizeof( buf ) - 24; i++ )
		n += sprintf( buf + n, " %.0f", bdgn_prof_n[i] );
	ri.Cvar_Set( "r_profile_count", buf );
	bdgn_prof_on = prof_cv_on->value != 0;
}
#endif

#endif
