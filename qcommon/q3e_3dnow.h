/*
Copyright (C) 1997-2001 Id Software, Inc.  (GPL v2 or later, same as the rest of the source)

q3e_3dnow.h -- AMD 3DNow! kernels for the engine (quake2.exe), ported from AMD's 1998
3DNow! patch (see source/AMD3DNow-RE/notes/engine.md).

The kernels are self-contained: the structs below mirror the engine's cnode_t, cplane_t,
cparticle_t and particle_t, and the callers check the sizes at compile time.  Every call
site keeps the original C code as the fallback.  The kernels end with femms.
*/
#ifndef Q3E_3DNOW_H
#define Q3E_3DNOW_H

/* 1 when the CPU reports 3DNow! (CPUID 0x80000001 EDX bit 31); cached */
int Q3E_CPUHas3DNow (void);
int Q3E_CPUHasMMX (void);
int Q3E_CPUHasSSE (void);	/* CPU and operating system */

/* BDGN 3.51p: raw CPUID registers (zero / has_cpuid 0 without CPUID) and SSE2 with operating system support.
   Decoding is in q3e_cpuinfo.h (bdgn_status). */
#include "q3e_cpuinfo.h"
void Q3E_ReadCPUID (q3e_cpuid_raw_t *r);
int Q3E_CPUHasSSE2 (void);
int Q3E_UsableAVXLevel(void); /* 0 unavailable, 1 AVX, 2 AVX2 */

/* mirrors of cplane_t (q_shared.h) and cnode_t (cmodel.c) */
typedef struct
{
	float			normal[3];
	float			dist;
	unsigned char	type, signbits, pad[2];
} q3e_plane_t;

typedef struct
{
	const q3e_plane_t	*plane;
	int					children[2];		/* negative numbers are leafs */
} q3e_node_t;

typedef struct
{
	const q3e_node_t	*nodes;			/* map_nodes */
	const float			*fraction;		/* &trace_trace.fraction (changed by leaf) */
	const float			*extents;		/* trace_extents */
	int					ispoint;		/* trace_ispoint */
	void				(*leaf)( int leafnum );	/* CM_TraceToLeaf, called after femms */
} q3e_hull_t;

/* CM_RecursiveHullCheck (cmodel.c, engine E1).  f = { p1f, p2f }.  Same traversal as id;
   the split fractions use a refined pfrcp (AMD used the 14-bit pfrcp alone). */
void Q3E_RecursiveHullCheck (const q3e_hull_t *h, int num, const float *f, const float *p1, const float *p2);

/* mirrors of cparticle_t (client.h) and particle_t (ref.h) */
typedef struct q3e_cparticle_s
{
	struct q3e_cparticle_s	*next;
	float		time;
	float		org[3];
	float		vel[3];
	float		accel[3];
	float		color;
	float		colorvel;
	float		alpha;
	float		alphavel;
} q3e_cparticle_t;

typedef struct
{
	float		origin[3];
	int			color;
	float		alpha;
} q3e_particle_t;

/* CL_AddParticles (cl_fx.c, engine E2): walks the active list, moves faded particles to
   *freelist, appends live ones to out[*nout] (like V_AddParticle, dropped when
   *nout == maxout) and returns the new active list.  INSTANT_PARTICLE (-10000) particles
   keep their alpha, use the previous particle's time like id, and get alpha = alphavel = 0. */
q3e_cparticle_t *Q3E_AddParticles (q3e_cparticle_t *list, q3e_cparticle_t **freelist, int cltime,
								   q3e_particle_t *out, int *nout, int maxout);

/* BDGN 3.50: AMD's sound mixing (quake2.exe 0x438a80 / 0x438bc0).  samp = count (left, right) int pairs.
   8 bit: id's volume tables (snd_scaletable rows), bit-identical to id.
   16 bit: (data * vol) >> 8 with the product in float (pi2fd/pfmul/pf2id), at most 1 off id's integer result.
   Returns 0 without mixing when a volume is outside 0..0xff00 (s_volume above 1): use id's code then. */
void Q3E_PaintFrom8 (int *samp, const unsigned char *sfx, int count, const int *lscale, const int *rscale);
int Q3E_PaintFrom16 (int *samp, const short *sfx, int count, int leftvol, int rightvol);

#endif
