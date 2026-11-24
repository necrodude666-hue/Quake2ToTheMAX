/*
Copyright (C) 1997-2001 Id Software, Inc.  (GPL v2 or later, same as the rest of the source)

gl_3dnow.h -- AMD 3DNow! kernels for the OpenGL refresh (BDGN 3.28 3DNow! patch)

All kernels are self-contained (no engine types) and are only called when
GL_3DNOW() is true, i.e. the CPU reports 3DNow! (CPUID 0x80000001 EDX bit 31)
and the cvar gl_3dnow is nonzero.  Every call site keeps the original C code
as the fallback.  Each kernel ends with femms before returning to x87 code.
*/
#ifndef GL_3DNOW_H
#define GL_3DNOW_H

extern int q3d_cpu_state;			/* -1 = not checked yet, 0 = no 3DNow!, 1 = 3DNow! */
int Q3D_DetectCPU (void);
#define Q3D_CPU()	( q3d_cpu_state > 0 || ( q3d_cpu_state < 0 && Q3D_DetectCPU() ) )

/* bl[i*3+k] = (add ? bl[i*3+k] : 0) + lm[i*3+k] * scale[k]   for i < count */
void Q3D_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add);

/* the dlight loop of R_AddDynamicLights for one light on one surface */
void Q3D_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color);

/* out[i] = (int)( in[i] + 0.5f ), i < n    (rounds like Q_ftol for the non-negative values that matter) */
void Q3D_RoundFloats (int *out, const float *in, int n);

/* out[i*3+k] = shadedots[ verts[i*4+3] ] * shadelight[k]   (dtrivertx_t is 4 bytes, lightnormalindex last) */
void Q3D_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n);

/* plain (non powerup) GL_LerpVerts: lerp[i*4+k] = move[k] + ov[i*4+k]*backv[k] + v[i*4+k]*frontv[k]; lerp[i*4+3] = 0 */
void Q3D_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv);

/* BDGN 3.32 batched alias drawing: the same two kernels for an "expanded" vertex list.
   xyz[i] = index of the source vertex of expanded vertex i (dtrivertx_t = 4 bytes: x,y,z,lightnormalindex) */
void Q3D_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv);
/* same colours as bytes: out[i*4+0..3] = r,g,b,255 with r = min(255, shadedots[..] * shadelight[0] * 255) (truncated) */
void Q3D_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n);
void Q3D_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n);

#endif

/* Frustum test: planes = nplanes x { nx, ny, nz, dist }, signbits = id's per-plane sign bits
   (bit0/1/2 set when the normal component is negative). Returns 1 when the box is completely
   behind any plane (same result as BoxOnPlaneSide(...) == 2 for any of the four planes). */
int Q3D_CullBox(const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes);

/* Water warp texture coordinates for one polygon (EmitWaterPolys). verts points at the first
   vertex (x,y,z,s,t,...), vertexsize is the stride in floats. Writes numverts pairs (s,t) to out. */
void Q3D_WaterST(float *out, const float *verts, int numverts, int vertexsize,
				 float rdt, float scroll, const float *turbsin);

/* One clip stage of ClipSkyPolygon (AMD patch, area world W2; 3DNow! only, gate with q3d_use_3dnow).
   vecs = nump vertices (x,y,z), not modified.  Sides as id: front d > eps, back d < -eps, else on,
   with d = dot( v, norm ).  Returns 0 when the polygon is not split (the caller passes vecs on);
   otherwise writes the front part to front (*nfront vertices) and the back part to back (*nback). */
int Q3N_SkyClip(const float *vecs, int nump, const float *norm, float eps,
				float *front, int *nfront, float *back, int *nback);

/* AngleVectors (q_shared.c) with 3DNow! sin/cos (AMD patch, area mesh M1; gate with q3d_use_3dnow).
   angles in degrees (PITCH, YAW, ROLL); any output may be NULL.  Not bit-identical (~1e-6). */
void Q3N_AngleVectors(const float *angles, float *forward, float *right, float *up);

/* The rotate + frustum part of R_CullAliasModel (AMD patch, area mesh M2).  vectors = forward, right,
   up of the entity; the 8 corners of mins/maxs are rotated like id (y negated), moved to origin and
   tested against planes = nplanes x { nx, ny, nz, dist }.  Returns 1 when all corners are behind
   one plane (id's aggregatemask != 0). */
int Q3N_CullAliasBox(const float *mins, const float *maxs, const float *vectors, const float *origin,
					 const float *planes, int nplanes);

#ifndef GL_SIMD_PROTOS
#define GL_SIMD_PROTOS

/* ---- BDGN 3.32: modes, SSE / MMX kernels ---- */
#define Q3D_MAXBLOCK	64

extern int q3d_use_sse, q3d_use_3dnow, q3d_use_mmx;	/* set by Q3D_UpdateModes() every frame */
void Q3D_UpdateModes (int sse, int n3d, int mmx);
int Q3D_EnhancedReady(void);
int Q3D_EnableEnhanced(int enabled);
int Q3S_CPUHasSSE (void);
int Q3S_SelfTest (void);
int Q3S_SafeSelfTest (void);
int Q3M_SafeSelfTest (void);	/* BDGN 3.51bat: MMX routines against id's C code */
void Q3D_AutoSIMD (void (*print)( const char *line ));	/* BDGN 3.51bat: automatic SSE/MMX, once per renderer start */
extern int q3d_auto_sse, q3d_auto_mmx, q3d_auto_sse2;	/* 1 on, 0 not supported, 2 self-test failed, 3 3DNow! used */	/* BDGN 3.51axt: the same inside __try/__except (crash -> report, SSE off) */		/* BDGN 3.51awt: in-game check of every SSE kernel, bit per failure */
extern int q3s_selftest_fail;
extern char q3s_selftest_report[256];
int Q3S_CPUHasMMX (void);

/* Q3D_x = dispatcher (SSE if on, else 3DNow!).  Q3N_x = 3DNow! kernel, Q3S_x = SSE kernel (same arguments). */
void Q3N_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add);
void Q3S_LightmapScale (float *bl, const unsigned char *lm, int count, const float *scale, int add);
void Q3N_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color);
void Q3S_AddDlight (float *pfBL, int smax, int tmax, float local0, float local1,
					float frad, float fminlight, const float *color);
void Q3N_RoundFloats (int *out, const float *in, int n);
void Q3S_RoundFloats (int *out, const float *in, int n);
void Q3N_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n);
void Q3S_ColorArray (float *out, const unsigned char *verts, const float *shadedots,
					 const float *shadelight, int n);
void Q3N_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv);
void Q3S_LerpVerts (int nverts, const unsigned char *v, const unsigned char *ov, float *lerp,
					const float *move, const float *frontv, const float *backv);
void Q3N_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv);
void Q3S_LerpVertsIdx (int n, const int *xyz, const unsigned char *v, const unsigned char *ov, float *lerp,
					   const float *move, const float *frontv, const float *backv);
void Q3N_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n);
void Q3S_ColorArrayIdxB (unsigned char *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						 const float *shadelight, int n);
void Q3N_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n);
void Q3S_ColorArrayIdx (float *out, const int *xyz, const unsigned char *verts, const float *shadedots,
						const float *shadelight, int n);
int Q3N_CullBox(const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes);
int Q3S_CullBox(const float *mins, const float *maxs, const float *planes, const int *signbits, int nplanes);
void Q3N_WaterST(float *out, const float *verts, int numverts, int vertexsize,
				 float rdt, float scroll, const float *turbsin);
void Q3S_WaterST(float *out, const float *verts, int numverts, int vertexsize,
				 float rdt, float scroll, const float *turbsin);

/* MMX integer routines of the texture loader (identical results to the C code) */
void Q3D_SetSSE2MipMap(int enabled);
int Q3D_SSE2LightmapEnabled(void);
void Q3D_SetSSEPacked (int enabled);	/* BDGN 3.58w: gl_sse_packed */
int Q3D_SSEPackedEnabled (void);
void Q3S_LightmapScale4 (float *bl, const unsigned char *lm, int count, const float *scale, int add);
int Q3S2_TryMipMap(unsigned char *pixels,int width,int height);
void Q3M_MipMap (unsigned char *in, int width, int height);
void Q3M_ResampleRow (unsigned *out, const unsigned char *inrow, const unsigned char *inrow2,
					  const unsigned *p1, const unsigned *p2, int outwidth);
/* BDGN 3.41: R_BuildLightMap store step for one row (gl_monolightmap 0), bit-identical to the C loop;
   ibl = rounded r,g,b ints (3 per texel), dest = 4 bytes per texel (r,g,b,brightest) */
void Q3M_LightStoreRow (unsigned char *dest, const int *ibl, int n);
/* BDGN 3.42: copy n vertices of 7 floats (MMX when on, else memcpy) */
void Q3M_CopyVerts (float *dst, const float *src, int n);
#endif

/*
BDGN 3.50: the rest of AMD's 3DNow! renderer code (ref_glam.dll).  3DNow! only: gate with q3d_use_3dnow.
The structs mirror the engine types (gl_model.h, ref.h); the callers check the sizes at compile time.
*/
#ifndef Q3N_AMD350_H
#define Q3N_AMD350_H

typedef struct
{
	float			normal[3];
	float			dist;
	unsigned char	type, signbits, pad[2];
} q3n_plane_t;						/* cplane_t */

typedef struct q3n_node_s
{
	int					contents;		/* -1 = node, else a leaf */
	int					visframe;
	float				minmaxs[6];
	struct q3n_node_s	*parent;
	q3n_plane_t			*plane;
	struct q3n_node_s	*children[2];
	unsigned short		firstsurface;
	unsigned short		numsurfaces;
} q3n_node_t;						/* mnode_t */

typedef struct q3n_surf_s
{
	int					visframe;
	q3n_plane_t			*plane;
	int					flags;
	int					firstedge;
	int					numedges;
	short				texturemins[2];
	short				extents[2];
	int					light_s, light_t;
	int					dlight_s, dlight_t;
	void				*polys;
	struct q3n_surf_s	*texturechain;
	struct q3n_surf_s	*lightmapchain;
	const float			*texinfo;			/* mtexinfo_t: starts with vecs[2][4] */
	int					dlightframe;
	int					dlightbits;
	int					lightmaptexturenum;
	unsigned char		styles[4];			/* MAXLIGHTMAPS */
	float				cached_light[4];
	unsigned char		*samples;
	int					alphaframe;			/* BDGN 3.56b: not read here, keeps the size of msurface_t */
} q3n_surf_t;						/* msurface_t */

/* R_MarkLights (AMD 0x100181e7): light = dlight_t (origin[3], color[3], intensity).  Same surfaces get the same
   bits as id; the whole walk is one call with one femms. */
void Q3N_MarkLights (const float *light, int bit, q3n_node_t *node, q3n_surf_t *surfaces, int dlightframe);

typedef struct
{
	const q3n_surf_t	*surfaces;		/* r_worldmodel->surfaces */
	const float			*lightstyles;	/* r_newrefdef.lightstyles: rgb[3], white */
	const float			*modulate;		/* &gl_modulate->value */
	int					skipflags;		/* SURF_DRAWTURB | SURF_DRAWSKY: no lightmaps */
	float				*color;			/* pointcolor: written when a lit surface is hit */
	float				*spot;			/* lightspot */
	q3n_plane_t			**plane;		/* lightplane */
} q3n_lightpoint_t;

/* RecursiveLightPoint (AMD 0x10017680): same return value, lightspot, lightplane and pointcolor as id. */
int Q3N_LightPoint (const q3n_lightpoint_t *lp, const q3n_node_t *node, const float *start, const float *end);

/* GL_DrawParticles input: particle_t (origin[3], color, alpha), 20 bytes */
typedef struct
{
	float		origin[3];
	int			color;
	float		alpha;
} q3n_particle_t;

/* one particle of the triangle path: colour bytes r,g,b,a and the two corners */
typedef struct
{
	unsigned	color;
	float		v1[3];
	float		v2[3];
} q3n_ptri_t;

/* GL_DrawParticles triangle path (AMD 0x10015c80): out[i] = colour (alpha byte = (int)(alpha*255)) and the
   corners origin + up*scale, origin + right*scale, scale as id (1 under 20 units in front, else 1 + d*0.004) */
void Q3N_ParticleTris (int n, const q3n_particle_t *p, const unsigned *colortable, const float *vieworg,
					   const float *vpn, const float *up, const float *right, q3n_ptri_t *out);
/* point path arrays (gl_fastparticles): verts = 3 floats, cols = 4 bytes per particle */
void Q3N_ParticleArrays (int n, const q3n_particle_t *p, const unsigned *colortable, float *verts, unsigned char *cols);

#endif
