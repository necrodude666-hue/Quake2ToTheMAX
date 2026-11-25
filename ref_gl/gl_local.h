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

#ifdef _WIN32
#  include <windows.h>
#endif

#include <stdio.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>

#ifndef __linux__
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT GL_COLOR_INDEX
#endif
#endif

#include "../client/ref.h"

#include "qgl.h"

#include "gl_perfstats.h"	/* BDGN 3.52 (WESTBECK) measurement build: dlight_stats */
perf_cycles_t Q3S_Cycles (void);
void R_DlightStatsFrame (void);
void R_DlightStats_f (void);
double R_PerfCyclesPerMs (void);

#include "../qcommon/bdgn_version.h"
#define REF_VERSION "BDGN v" BDGN_VERSION_STRING " GL"

// up / down
#define	PITCH	0

// left / right
#define	YAW		1

// fall over
#define	ROLL	2


#ifndef __VIDDEF_T
#define __VIDDEF_T
typedef struct
{
	unsigned		width, height;			// coordinates from main game
} viddef_t;
#endif

extern	viddef_t	vid;


/*

  skins will be outline flood filled and mip mapped
  pics and sprites with alpha will be outline flood filled
  pic won't be mip mapped

  model skin
  sprite frame
  wall texture
  pic

*/

typedef enum 
{
	it_skin,
	it_sprite,
	it_wall,
	it_pic,
	it_sky
} imagetype_t;

typedef struct image_s
{
	char	name[MAX_QPATH];			// game path, including extension
	imagetype_t	type;
	int		width, height;				// source image
	int		upload_width, upload_height;	// after power of two and picmip
	int		registration_sequence;		// 0 = free
	struct msurface_s	*texturechain;	// for sort-by-texture world drawing
	int		texnum;						// gl texture binding
	float	sl, tl, sh, th;				// 0,0 - 1,1 unless part of the scrap
	qboolean	scrap;
	qboolean	has_alpha;

	qboolean paletted;

	byte	flat_rgb[3];				// BDGN 3.45: dominant material colour (r_drawflat 2/3/4), already lifted for unlit display
	byte bright_rgb[3];
	qboolean bright_valid;			// cached unique high-contrast skin colour
	byte	flat_gray;					// the same as a grey level
	unsigned	hd_bytes;
	qboolean	nearest;				// BDGN 3.51bct: classic-look sprite picture: no mipmaps, no picmip, GL_NEAREST
} image_t;

#define	TEXNUM_LIGHTMAPS	1024
#define	TEXNUM_SCRAPS		1152
#define	TEXNUM_IMAGES		1153
/* BDGN 3.59i: the 2D enemy atlas pages (32) and the 2D weapon slots (128) use texture names below the lightmaps
   (768-927), not above all images (5313 and up with MAX_GLTEXTURES 4096). 3dfx MiniGL (3dfxgl.dll) crashed on the
   Voodoo2 and the Voodoo3 in v3.59e at the first run that drew 2D enemies (Ultra Fast); a fixed table of texture names
   in the MiniGL is the likely cause (TEORETISK ANALYS). id's 3.21 never used names below 1024. */
#define	TEXNUM_WEAPSPRITES	768
#define	TEXNUM_DUKE		896

#define		MAX_GLTEXTURES	4096

//===================================================================

typedef enum
{
	rserr_ok,

	rserr_invalid_fullscreen,
	rserr_invalid_mode,

	rserr_unknown
} rserr_t;

#include "gl_model.h"

void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

void GL_SetDefaultState( void );
void GL_InitSwapInterval( void );
void GL_UpdateSwapInterval( void );

extern	float	gldepthmin, gldepthmax;

typedef struct
{
	float	x, y, z;
	float	s, t;
	float	r, g, b;
} glvert_t;


#define	MAX_LBM_HEIGHT		480

#define BACKFACE_EPSILON	0.01


//====================================================

extern	image_t		gltextures[MAX_GLTEXTURES];
extern	int			numgltextures;


extern	image_t		*r_notexture;
extern	image_t		*r_particletexture;
extern	entity_t	*currententity;
extern	model_t		*currentmodel;
extern	int			r_visframecount;
extern	int			r_framecount;
extern	cplane_t	frustum[5];
extern	int			r_numfrustumplanes;	// 4, or 5 when a far (draw distance) plane is active
extern	int			c_brush_polys, c_alias_polys;


extern	int			gl_filter_min, gl_filter_max;

//
// view origin
//
extern	vec3_t	vup;
extern	vec3_t	vpn;
extern	vec3_t	vright;
extern	vec3_t	r_origin;

//
// screen size info
//
extern	refdef_t	r_newrefdef;
extern	int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

extern	cvar_t	*r_norefresh;
extern	cvar_t	*r_lefthand;
extern	cvar_t	*r_drawentities;
extern	cvar_t	*r_drawworld;
extern	cvar_t	*gl_batchworld;		// BDGN 3.42: batched drawing of the static lightmapped world
extern	cvar_t	*gl_fastsky;		// BDGN 3.38 lab: sky in one flat colour
extern cvar_t *r_staticmap;
extern cvar_t *r_brightskins;
extern float r_map_animation_time;
extern	cvar_t	*gl_fastwater;		// BDGN 3.38 lab: water without waves
void GL_FlatImageColor (image_t *im, int mode, byte *rgb);		// BDGN 3.45: mode 2 material, 3 grey, 4 = material (retro: see GL_FlatRetro)
void GL_FlatLoadDB (void);										// reads flatcolors.txt
void GL_FlatFreeDB (void);
void GL_FlatRetro (byte *rgb);									// nearest colour of the 16 colour retro palette
void GL_FlatLift (float *rgb);									// unlit display brightness: V -> 60 + 1.5 V (80..235), 0-255 in and out
void R_SkyFlatColor (byte *rgb);									// gl_warp.c: average colour of the sky, lifted
extern	float	gl_yshear_t;	/* BDGN 3.60i: Duke Nukem 3D view (gl_rmain.c, qcommon/yshear.h) */
extern	cvar_t	*gl_flatfloors;		// BDGN 3.58w: floors and ceilings in one colour (Wolfenstein 3D mode)
int R_LowVram (void);	/* BDGN 3.59s: gl_rmain.c */
extern	cvar_t	*gl_flatstate;		// BDGN 3.59p: r_drawflat switches texturing off once per run
extern	cvar_t	*r_drawflat;		// BDGN 3.38: 1 = every world polygon in its own solid colour (no textures, no lightmaps, no sky)
extern	cvar_t	*r_speeds;
extern	cvar_t	*r_fullbright;
extern	cvar_t	*r_novis;
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_lerpmodels;

extern	cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

extern cvar_t	*gl_vertex_arrays;

extern cvar_t	*gl_ext_swapinterval;
extern cvar_t	*gl_ext_palettedtexture;
extern cvar_t	*gl_ext_multitexture;
extern cvar_t	*gl_ext_pointparameters;
extern cvar_t	*gl_ext_compiled_vertex_array;

extern cvar_t	*gl_particle_min_size;
extern cvar_t	*gl_particle_max_size;
extern cvar_t	*gl_particle_size;
extern cvar_t	*gl_particle_att_a;
extern cvar_t	*gl_particle_att_b;
extern cvar_t	*gl_particle_att_c;

extern	cvar_t	*gl_nosubimage;
extern	cvar_t	*gl_bitdepth;
extern	cvar_t	*gl_mode;
extern	cvar_t	*gl_log;
extern	cvar_t	*gl_lightmap;
extern	cvar_t	*gl_shadows;
extern	cvar_t	*gl_dynamic;
extern	cvar_t	*gl_dlight_cull;		// BDGN 3.51p: skip lights that cannot reach the view (gl_dlightcull.h)
extern	cvar_t	*gl_dlight_maxdist;	// BDGN 3.51p: lights farther than this do not light walls (0 = off)
void	R_DlightFilterSurf (msurface_t *surf);	// BDGN 3.51p: per-surface light reach (gl_light.c)
extern  cvar_t  *gl_monolightmap;
extern  cvar_t  *gl_doomlight, *gl_blakelight, *gl_q1fog, *gl_q1particles, *gl_solidalpha;	/* BDGN 3.57d: old-school modes for OpenGL */
extern	cvar_t	*gl_nobind;
extern	cvar_t	*gl_round_down;
extern	cvar_t	*gl_picmip;
extern	cvar_t	*gl_skymip;
extern	cvar_t	*gl_showtris;
extern	cvar_t	*gl_finish;
extern	cvar_t	*gl_ztrick;
extern	cvar_t	*gl_clear;
extern	cvar_t	*gl_cull;
extern	cvar_t	*gl_poly;
extern	cvar_t	*gl_texsort;
extern	cvar_t	*gl_polyblend;
extern	cvar_t	*gl_flashblend;
extern	cvar_t	*gl_lightmaptype;
extern	cvar_t	*gl_modulate;
extern	cvar_t	*gl_3dnow;
extern	cvar_t	*gl_amd_sky;	// AMD patch port, area world W2: 3DNow! sky clipping
extern	cvar_t	*gl_amd_angles;	// AMD patch port, area mesh M1: 3DNow! AngleVectors
extern	cvar_t	*gl_amd_cull;	// AMD patch port, area mesh M2: 3DNow! R_CullAliasModel
extern	cvar_t	*gl_amd_marklights;	// BDGN 3.50: 3DNow! R_MarkLights (AMD 0x100181e7)
extern	cvar_t	*gl_amd_lightpoint;	// BDGN 3.50: 3DNow! RecursiveLightPoint (AMD 0x10017680)
extern	cvar_t	*gl_amd_particles;	// BDGN 3.50: 3DNow! GL_DrawParticles setup (AMD 0x10015c80)
extern	float	q3d_frustum[20];	// r_numfrustumplanes x { normal, dist }, see R_SetFrustum
void R_AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
extern	cvar_t	*gl_sse;		// BDGN 3.32: SSE kernels
extern	cvar_t	*gl_mmx;		// BDGN 3.32: MMX texture loader routines
#define GL_MMX()	( q3d_use_mmx )
extern	cvar_t	*gl_drawdist;
extern	float	r_drawdistance;		// world units, 0 = unlimited
extern	cvar_t	*gl_playermip;
extern	cvar_t	*gl_drawbuffer;
extern	cvar_t	*gl_3dlabs_broken;
extern  cvar_t  *gl_driver;
extern	cvar_t	*gl_swapinterval;
extern	cvar_t	*gl_texturemode;
extern	cvar_t	*gl_anisotropy;
extern	cvar_t	*gl_texturealphamode;
extern	cvar_t	*gl_texturesolidmode;
extern  cvar_t  *gl_saturatelighting;
extern  cvar_t  *gl_lockpvs;
extern  cvar_t  *gl_fastdraw;	// BDGN 3.32: batched alias model drawing 0 = off (default), 1 = triangle list, 2 = strips/fans, 3 = strips/fans + byte colours (best); uses the 3DNow! kernels when 3DNow! is on, C code otherwise
extern  int      gl_vaunsafe;	// BDGN 3.36: 1 = driver (3dfx miniGL/Voodoo) does not handle vertex arrays; batched drawing is forced off
extern  cvar_t  *gl_flatmodels;	// BDGN 3.37: 1 = flat (single colour) lighting on models, faster
extern  cvar_t  *gl_lowdetail;	// BDGN 3.37: distance in units; models farther away are drawn without frame interpolation (0 = off)
extern  cvar_t  *gl_maxtexsize;	// BDGN 3.37: biggest size of a world/skin texture (0 = off, else 8..256); needs a renderer restart
extern  cvar_t  *gl_fastforce;	// BDGN 3.36: 1 = use batched drawing even on drivers detected as unsafe
extern cvar_t *gl_modelbatch_auto;
extern cvar_t *gl_particlebatch_auto;
extern  cvar_t  *gl_fastparticles;	// BDGN 3.32: 1 = batched particle drawing
extern  cvar_t  *gl_fastworld;	// BDGN 3.4: 1 = world and brush models without lightmaps (Breakfast Pie)

// doogies code
extern  cvar_t  *gl_hack_noimages;
extern  cvar_t  *gl_hack_notextures;
// end

extern	cvar_t	*vid_fullscreen;
extern	cvar_t	*vid_gamma;

extern	cvar_t		*intensity;

extern	int		gl_lightmap_format;
extern	int		gl_solid_format;
extern	int		gl_alpha_format;
extern	int		gl_tex_solid_format;
extern	int		gl_tex_alpha_format;

extern	int		c_visible_lightmaps;
extern	int		c_visible_textures;

extern	float	r_world_matrix[16];

void R_TranslatePlayerSkin (int playernum);
void GL_Bind (int texnum);
void GL_ScaleInit(void);
void GL_ScaleShutdown(void);
qboolean GL_ScaleOffscreen(void);
void GL_ScaleAbortFrame(void);
void GL_InitPixelLighting(void);
void GL_SelectPixelLight(void);
void GL_DrawPixelLight(void);
void GL_DrawRelief(void);
/* BDGN 3.55ak: gl_sun.c */
extern vec3_t r_sun_dir, r_sun_color;
extern int r_sun_valid;
extern cvar_t *gl_sun;
void R_SunInit (void);
int R_WaterLight(vec3_t point,vec3_t direction,vec3_t colour,vec3_t source,int *sun);
int R_WaterLightVisible(vec3_t point,vec3_t source,int sun);
void R_SMPInit(void);
void R_SMPPrepare(void);
void R_SMPFinish(void);
void R_SMPShutdown(void);
void R_SunSetSky (const char *skyname);
int R_SkyLighting (vec3_t tint, float *gain);
void GL_InitModernEffects(void);
int GL_ModernNeedsCleanWorld(void);
void GL_ModernOpaqueDepth(void);
void GL_ModernEnvironment(void);
void GL_ModernPuffBegin(int premultiplied);
void GL_ModernPuffEnd(void);
void R_SunCheck (void);
void R_SunLoadMap (void);
float R_SunShadowDirection (vec3_t origin, float *dx, float *dy);
void GL_DrawGodRays (void);
void GL_DrawReliefModels(void);
/* BDGN 3.55ai: gl_shadowvol.c */
extern int r_volume_shadows;
extern int r_shadow_stencil, r_shadow_stencil_ref;
int GL_WindowStencil (void);
void R_ShadowVolumeFrame (void);
void R_ShadowVolumeAdd (entity_t *e, dmdl_t *hdr, float (*lerped)[4], float dx, float dy, float height);
void R_ShadowVolumeDraw (void);
extern int gl_pixel_lightmask;	/* BDGN 3.55h: dynamic lights drawn per pixel this frame */
extern int gl_pixel_model_skip;	/* lights left out of R_LightPoint for the model being lit */
int GL_PixelLightModelMask(const entity_t *e,float radius);
void GL_DrawPixelLightModel(const float (*xyz)[4],const float (*normal)[3],const int *order);
image_t *R_TextureAnimation(mtexinfo_t *tex);
image_t *R_TextureAnimationFrame(mtexinfo_t *tex, int frame);
qboolean GL_ScaleBegin(const refdef_t *view, refdef_t *scaled_view, float ui_scale, const float *fog_color);
qboolean GL_ScalePresent(const refdef_t *scaled_view, const refdef_t *view, float ui_scale);
void GL_MBind( GLenum target, int texnum );
void GL_TexEnv( GLenum value );
void GL_EnableMultitexture( qboolean enable );
void GL_SelectTexture( GLenum );

void R_LightPoint (vec3_t p, vec3_t color);
void R_LightPointWorld (vec3_t p, vec3_t color);	/* BDGN 3.56b: world light only, the wall's scale */
void R_PushDlights (void);
qboolean R_ShadowFloor(vec3_t origin,vec3_t point,vec3_t normal);
float R_StaticLightLevel (vec3_t p);	/* BDGN 3.55m */
/* BDGN 3.55o: water reflection (gl_render_scale.c) */
extern qboolean gl_reflect_pass;
void GL_InitWaterReflection(void);
qboolean GL_DrawModernWater(msurface_t *fa);
void GL_ResetWaterDrawCount(void);
void GL_InitDistortion(void);		/* BDGN 3.55t: heat distortion (RF_DISTORT) */
void R_DrawDecals (void);			/* BDGN 3.55y: gl_decal.c */
void R_DecalClearCache (void);
qboolean R_DrawFXBeam (entity_t *e);
void GL_DistortBeginFrame(void);
qboolean GL_DistortWorldEnabled(void);
void GL_DistortNoteLava(msurface_t *fa);
qboolean GL_DistortQueue(entity_t *e);
void GL_DrawDistortion(void);
void R_DrawActorMark(entity_t *e);
void GL_ReflectNoteWater(msurface_t *fa);
qboolean GL_ReflectBegin(float *height);
void GL_ReflectViewport(void);
void GL_ReflectEnd(void);
void GL_ReflectDrawWater(msurface_t *fa);
extern qboolean gl_soft_shadow_capable;

//====================================================================

extern	model_t	*r_worldmodel;

extern	unsigned	d_8to24table[256];

extern	int		registration_sequence;


void V_AddBlend (float r, float g, float b, float a, float *v_blend);

qboolean R_Init( void *hinstance, void *hWnd );
void	R_Shutdown( void );

void R_RenderView (refdef_t *fd);
void GL_ScreenShot_f (void);
void GL_FreeFastMesh (struct model_s *mod);
void R_DrawAliasModel (entity_t *e);
void R_DrawBrushModel (entity_t *e);
void R_DrawSpriteModel (entity_t *e);
void R_DrawBeam( entity_t *e );
void R_DrawWorld (void);
void R_RenderDlights (void);
void R_DrawAlphaSurfaces (void);
#include "gl_alphachain.h"
extern alphachain_t r_alpha;	/* BDGN 3.56b: the translucent surfaces (was msurface_t *r_alpha_surfaces) */
void R_RenderBrushPoly (msurface_t *fa);
void R_InitParticleTexture (void);
void Draw_InitLocal (void);
void GL_SubdivideSurface (msurface_t *fa);
qboolean R_CullBox (vec3_t mins, vec3_t maxs);
void R_RotateForEntity (entity_t *e);
void R_MarkLeaves (void);

glpoly_t *WaterWarpPolyVerts (glpoly_t *p);
void EmitWaterPolys (msurface_t *fa);
void R_AddSkySurface (msurface_t *fa);
void R_ClearSkyBox (void);
void R_DrawSkyBox (void);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);

#if 0
short LittleShort (short l);
short BigShort (short l);
int	LittleLong (int l);
float LittleFloat (float f);

char	*va(char *format, ...);
// does a varargs printf into a temp buffer
#endif

void COM_StripExtension (char *in, char *out);

void	Draw_GetPicSize (int *w, int *h, char *name);
void	Draw_Pic (int x, int y, char *name);
void	Draw_StretchPic (int x, int y, int w, int h, char *name);
void	Draw_Char (int x, int y, int c);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeScreen (void);
void	Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data);

void	R_BeginFrame( float camera_separation );
void	R_SwapBuffers( int );
void	R_SetPalette ( const unsigned char *palette);

int		Draw_GetPalette (void);

void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight);

struct image_s *R_RegisterSkin (char *name);

void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height);
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type, int bits);
image_t	*GL_FindImage (char *name, imagetype_t type);
void	GL_FX4KCheck (void);	/* BDGN 3.56r */
void	GL_TextureMode( char *string );
void	GL_InitTextureFiltering(void);
void	GL_ImageList_f (void);

void	GL_SetTexturePalette( unsigned palette[256] );

void	GL_InitImages (void);
void	GL_ShutdownImages (void);
void GL_ReleaseOptionalSkyImage(image_t *image);

void	GL_FreeUnusedImages (void);

void GL_TextureAlphaMode( char *string );
void GL_TextureSolidMode( char *string );

/*
** GL extension emulation functions
*/
void GL_DrawParticles( int n, const particle_t particles[], const unsigned colortable[768] );

/*
** GL config stuff
*/
#define GL_RENDERER_VOODOO		0x00000001
#define GL_RENDERER_VOODOO2   	0x00000002
#define GL_RENDERER_VOODOO_RUSH	0x00000004
#define GL_RENDERER_BANSHEE		0x00000008
#define		GL_RENDERER_3DFX		0x0000000F

#define GL_RENDERER_PCX1		0x00000010
#define GL_RENDERER_PCX2		0x00000020
#define GL_RENDERER_PMX			0x00000040
#define		GL_RENDERER_POWERVR		0x00000070

#define GL_RENDERER_PERMEDIA2	0x00000100
#define GL_RENDERER_GLINT_MX	0x00000200
#define GL_RENDERER_GLINT_TX	0x00000400
#define GL_RENDERER_3DLABS_MISC	0x00000800
#define		GL_RENDERER_3DLABS	0x00000F00

#define GL_RENDERER_REALIZM		0x00001000
#define GL_RENDERER_REALIZM2	0x00002000
#define		GL_RENDERER_INTERGRAPH	0x00003000

#define GL_RENDERER_3DPRO		0x00004000
#define GL_RENDERER_REAL3D		0x00008000
#define GL_RENDERER_RIVA128		0x00010000
#define GL_RENDERER_DYPIC		0x00020000

#define GL_RENDERER_V1000		0x00040000
#define GL_RENDERER_V2100		0x00080000
#define GL_RENDERER_V2200		0x00100000
#define		GL_RENDERER_RENDITION	0x001C0000

#define GL_RENDERER_O2          0x00100000
#define GL_RENDERER_IMPACT      0x00200000
#define GL_RENDERER_RE			0x00400000
#define GL_RENDERER_IR			0x00800000
#define		GL_RENDERER_SGI			0x00F00000

#define GL_RENDERER_MCD			0x01000000
#define GL_RENDERER_OTHER		0x80000000

typedef struct
{
	int         renderer;
	const char *renderer_string;
	const char *vendor_string;
	const char *version_string;
	const char *extensions_string;

	qboolean	allow_cds;
} glconfig_t;

typedef struct
{
	float inverse_intensity;
	qboolean fullscreen;

	int     prev_mode;

	unsigned char *d_16to8table;

	int lightmap_textures;

	int	currenttextures[2];
	int currenttmu;

	float camera_separation;
	qboolean stereo_enabled;

	unsigned char originalRedGammaTable[256];
	unsigned char originalGreenGammaTable[256];
	unsigned char originalBlueGammaTable[256];
} glstate_t;

extern glconfig_t  gl_config;
extern glstate_t   gl_state;

/*
====================================================================

IMPORTED FUNCTIONS

====================================================================
*/

extern	refimport_t	ri;

// AMD 3DNow! kernels (BDGN 3DNow! patch); the C code stays as the fallback at every call site
#include "gl_3dnow.h"
// BDGN 3.51p: fullbright world/models does not depend on the CPU, so it is defined for the standard C build too
#define GL_FULLBRIGHT() ((gl_fastworld->value || r_fullbright->value) && !r_drawflat->value && !gl_hack_notextures->value)
#if id386
// Auto preserves the older 3DNow! batching; explicit off overrides only batching.
#define GL_FASTDRAW()		( (int)gl_fastdraw->value ? (int)gl_fastdraw->value : ( gl_modelbatch_auto->value && q3d_use_3dnow ? 3 : 0 ) )
#define GL_FASTPARTICLES()	( gl_fastparticles->value || (gl_particlebatch_auto->value && q3d_use_3dnow) )
// any vector kernel on: SSE (gl_sse) or 3DNow! (gl_3dnow); Q3D_UpdateModes() sets the flags every frame
#define GL_3DNOW()	( q3d_use_sse || q3d_use_3dnow )
#else
// BDGN 3.51p: standard C build (C_ONLY or not x86): batching only when asked for, no vector kernels
#define GL_FASTDRAW()		( (int)gl_fastdraw->value )
#define GL_FASTPARTICLES()	( gl_fastparticles->value )
#define GL_3DNOW()	0
#endif


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

void		GLimp_BeginFrame( float camera_separation );
void		GLimp_EndFrame( void );
int 		GLimp_Init( void *hinstance, void *hWnd );
void		GLimp_Shutdown( void );
int     	GLimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen );
void		GLimp_AppActivate( qboolean active );
void		GLimp_EnableLogging( qboolean enable );
void		GLimp_LogNewFrame( void );

void GL_BrightSkinColor (image_t *image, byte rgb[3]);

void R_DukeReset(void);
void R_ValidateEntities (refdef_t *fd);	/* BDGN 3.52s */
void R_SpriteLightColor (vec3_t origin, byte rgb[3]);	/* BDGN 3.52r: cl_spritelight */
void R_DukeFlush(void);	/* BDGN 3.52h: draws the queued classic sprites */
void R_DukeStats(void);
void R_DukeBeginRegistration(void);
void R_DukeRegisterModel(model_t *model);
/* BDGN 3.51q: 2D first-person weapon (gl_weapsprite.h) */
void R_WeaponSpritesInit (void);
void R_WeaponSpritesReset (void);
void R_WeaponSpritesPreload (void);	/* BDGN 3.51s */
void R_WeaponStats (void);
