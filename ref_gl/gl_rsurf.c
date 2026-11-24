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
// GL_RSURF.C: surface-related refresh code
#include <assert.h>

#include "gl_local.h"
#include "../qcommon/bdgn_prof.h"
#include "gl_dlightcull.h"	/* BDGN 3.51p: gl_dlight_maxdist for brush models too */

static vec3_t	modelorg;		// relative to viewpoint

alphachain_t	r_alpha;	/* BDGN 3.56b: a surface once per list (gl_alphachain.h); was msurface_t *r_alpha_surfaces */

#define DYNAMIC_LIGHT_WIDTH  128
#define DYNAMIC_LIGHT_HEIGHT 128

#define LIGHTMAP_BYTES 4

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	128

int		c_visible_lightmaps;
int		c_visible_textures;

#define GL_LIGHTMAP_FORMAT GL_RGBA

typedef struct
{
	int internal_format;
	int	current_lightmap_texture;

	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];

	int			allocated[BLOCK_WIDTH];

	// the lightmap texture data needs to be kept in
	// main memory so texsubimage can update properly
	byte		lightmap_buffer[4*BLOCK_WIDTH*BLOCK_HEIGHT];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;


static void		LM_InitBlock( void );
static void		LM_UploadBlock( qboolean dynamic );
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

/* BDGN 3.52 (WESTBECK) measurement build: dlight_stats (gl_perfstats.h). Counters and CPU cycles around the lightmap builds and
   uploads that happen while drawing; nothing is drawn or uploaded differently. Q3S_Cycles returns 0 without a cycle
   counter (then only the counts are shown). */
dls_t				r_dls;
static perf_cycles_t	dls_mark, dls_last, dls_cal_cyc;
static unsigned		dls_cal_ms;

static void DLS_Begin (void)
{
	dls_mark = Q3S_Cycles ();
}
static void DLS_Build (int dynamic)
{
	perf_cycles_t c = Q3S_Cycles ();
	if (dls_mark && c > dls_mark)
		r_dls.c_build += c - dls_mark;
	if (dynamic)
		r_dls.dyn_builds++;
	else
		r_dls.style_builds++;
	dls_mark = c;
}
static void DLS_Upload (int texels, int block)
{
	perf_cycles_t c = Q3S_Cycles ();
	if (dls_mark && c > dls_mark)
		r_dls.c_upload += c - dls_mark;
	if (block)
		r_dls.block_uploads++;
	else
		r_dls.uploads++;
	r_dls.texels += texels;
}
/* once per drawn frame (R_BeginFrame): whole-frame cycles, and the clock calibration since the renderer started */
void R_DlightStatsFrame (void)
{
	perf_cycles_t c = Q3S_Cycles ();
	if (!dls_cal_cyc)
	{
		dls_cal_cyc = c;
		dls_cal_ms = Sys_Milliseconds ();
	}
	if (!r_dls.ms0)
	{
		r_dls.cyc0 = c;
		r_dls.ms0 = Sys_Milliseconds ();
	}
	r_dls.frames++;		/* every frame begun, also without a cycle counter (Codex review) */
	if (r_dls.frames > 1 && dls_last && c > dls_last)
	{	/* the previous frame's length */
		r_dls.c_frames += c - dls_last;
		r_dls.intervals++;
	}
	dls_last = c;
}
/* cycles per millisecond since the renderer started (0 = unknown); also used by weapon_stats */
double R_PerfCyclesPerMs (void)
{
	return Perf_CyclesPerMs (dls_cal_cyc, Q3S_Cycles (), dls_cal_ms, Sys_Milliseconds ());
}
/* dlight_stats [zero|quiet]: per-frame numbers since the last zero (bdgn_bench zeroes it for every run) */
void R_DlightStats_f (void)
{
	char line[512];
	if (ri.Cmd_Argc () > 1 && !strcmp (ri.Cmd_Argv (1), "zero"))
	{
		memset (&r_dls, 0, sizeof(r_dls));
		dls_last = 0;
		return;
	}
	DLS_Format (&r_dls, Perf_CyclesPerMs (r_dls.cyc0, Q3S_Cycles (), r_dls.ms0, Sys_Milliseconds ()), line, sizeof(line));
	ri.Cvar_Set ("r_dlight_stats", line);
	if (ri.Cmd_Argc () > 1 && !strcmp (ri.Cmd_Argv (1), "quiet"))
		return;
	ri.Con_Printf (PRINT_ALL, "BDGN dynamic lights: %s\n", line);
	ri.Con_Printf (PRINT_ALL, "  (per frame: lightmaps rebuilt for light styles / dynamic lights, surface and block uploads, texels;\n"
		"   ms building and uploading and of the whole frame, their share; mhz = CPU clock measured; -1 = no cycle counter)\n");
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

#if 0
/*
=================
WaterWarpPolyVerts

Mangles the x and y coordinates in a copy of the poly
so that any drawing routine can be water warped
=================
*/
glpoly_t *WaterWarpPolyVerts (glpoly_t *p)
{
	int		i;
	float	*v, *nv;
	static byte	buffer[1024];
	glpoly_t *out;

	out = (glpoly_t *)buffer;

	out->numverts = p->numverts;
	v = p->verts[0];
	nv = out->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE, nv+=VERTEXSIZE)
	{
		nv[0] = v[0] + 4*sin(v[1]*0.05+r_map_animation_time)*sin(v[2]*0.05+r_map_animation_time);
		nv[1] = v[1] + 4*sin(v[0]*0.05+r_map_animation_time)*sin(v[2]*0.05+r_map_animation_time);

		nv[2] = v[2];
		nv[3] = v[3];
		nv[4] = v[4];
		nv[5] = v[5];
		nv[6] = v[6];
	}

	return out;
}

/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
void DrawGLWaterPoly (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	int		i;
	float	*v;

	p = WaterWarpPolyVerts (p);
	qglBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[5], v[6]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
#endif

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p)
{
	int		i;
	float	*v;

	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3], v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
================
R_DrawFlatSurface

BDGN 3.38: r_drawflat.  The whole surface in one solid colour that depends on the surface only (no
texture, no lightmap), so every polygon the map compiler made can be told apart.
================
*/
/*
BDGN 3.45: r_drawflat 2 / 3 / 4 = the dominant colour of the surface's texture (material, grey, 16 colour retro
palette); sky = the average sky colour.  A fixed light direction gives each surface a brightness (floor light,
ceiling dark) so that the shape of the map can still be seen without lightmaps.  Costs one plane lookup per surface.
r_drawflat 1 is the old colour per polygon.
*/
static void R_FlatMaterialColor (msurface_t *surf, int mode, byte *out)
{
	cplane_t	*pl = surf->plane;
	float		d, f;
	int			i;

	if ( surf->texinfo->flags & SURF_SKY )
	{
		R_SkyFlatColor( out );
		if ( mode == 3 )
			out[0] = out[1] = out[2] = (byte)( ( out[0] * 77 + out[1] * 150 + out[2] * 29 ) >> 8 );
	}
	else
	{
		GL_FlatImageColor( surf->texinfo->image, mode, out );
		d = pl->normal[0] * 0.40f + pl->normal[1] * 0.28f + pl->normal[2] * 0.87f;
		if ( surf->flags & SURF_PLANEBACK )
			d = -d;
		f = 0.55f + 0.225f * ( d + 1.0f );
		for ( i = 0 ; i < 3 ; i++ )
			out[i] = (byte)( out[i] * f );
	}
	if ( mode == 4 )
		GL_FlatRetro( out );
}

static void R_DrawFlatSurfaceMode (msurface_t *surf, int mode);
/* BDGN 3.59p: with r_drawflat every world surface is flat, so texturing is switched off once for the whole run
   instead of off and on again for every surface (gl_flatstate 1). PSYCHO Fast (r_drawflat 2) was 27-29 % slower
   than Ultra Fast on 3dfx MiniGL, and none of the first ten PSYCHO settings gave the fps back (Chromatic, Banshee,
   v3.59o cases; dual Pentium II, Voodoo2 SLI, v3.59n; MEASURED); MiniGL turns each GL state change into Glide state work
   (TEORETISK ANALYS). The flat floors (gl_flatfloors) mix with textured walls and keep the per-surface switch. */
static int r_flat_run;
static void R_FlatRunEnd (void)
{
	if (!r_flat_run)
		return;
	r_flat_run = 0;
	qglEnable( GL_TEXTURE_2D );
}
static void R_DrawFlatSurface (msurface_t *surf)
{
	if (gl_flatstate && gl_flatstate->value)
	{
		if (!r_flat_run)
		{
			GL_EnableMultitexture( false );
			qglDisable( GL_TEXTURE_2D );
			r_flat_run = 1;
		}
		R_DrawFlatSurfaceMode (surf, -(int)r_drawflat->value);	/* negative: state already set */
		return;
	}
	R_DrawFlatSurfaceMode (surf, (int)r_drawflat->value);
}
/* BDGN 3.58w: flat floors and ceilings in OpenGL (gl_flatfloors 1, Wolfenstein 3D mode, cl_retromode 2/3): a world
   surface that faces up or down (|normal z| > 0.7) gets the material colour of its texture, as r_drawflat 2, no
   texture and no light map; the walls keep theirs. Same idea as Software's sw_flatfloors 1 (MEASURED +11.3 % there on
   the K6-3+); in OpenGL TEORETISK ANALYS (a floor costs the card one untextured fill instead of texture + light map). */
static qboolean R_FlatFloor (msurface_t *surf)
{
	float z;
	if (!gl_flatfloors || !gl_flatfloors->value || r_drawflat->value)
		return false;
	if (surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP))
		return false;
	z = surf->plane->normal[2];
	return z > 0.7f || z < -0.7f;
}
static void R_DrawFlatSurfaceMode (msurface_t *surf, int mode)
{
	glpoly_t	*p;
	float		*v;
	int			i;
	int			run = mode < 0;

	if ( run )
		mode = -mode;
	else
	{
		GL_EnableMultitexture( false );
		qglDisable( GL_TEXTURE_2D );
	}
	if ( mode >= 2 )
	{
		byte rgb[3];

		R_FlatMaterialColor( surf, mode > 4 ? 4 : mode, rgb );
		/* BDGN 3.60: glColor3f, not glColor3ub: the 3dfx MiniGL (3dfxgl) has no working glColor3ub, so every flat wall
		   stayed white in PSYCHO on Voodoo2 (owner video, v3.59z); id's 3.21 uses glColor3f (Draw_Fill) */
		qglColor3f( rgb[0] * (1.0f/255.0f), rgb[1] * (1.0f/255.0f), rgb[2] * (1.0f/255.0f) );
	}
	else
	{
		unsigned	h = (unsigned)( surf - r_worldmodel->surfaces ) * 2654435761u;

		qglColor3f( 0.2f + 0.8f * (float)( ( h >> 8 ) & 255 ) / 255.0f,
					0.2f + 0.8f * (float)( ( h >> 16 ) & 255 ) / 255.0f,
					0.2f + 0.8f * (float)( ( h >> 24 ) & 255 ) / 255.0f );
	}
	c_brush_polys++;
	for ( p = surf->polys ; p ; p = p->next )
	{
		qglBegin( GL_POLYGON );
		v = p->verts[0];
		for ( i = 0 ; i < p->numverts ; i++, v += VERTEXSIZE )
			qglVertex3fv( v );
		qglEnd();
	}
	if ( !run )
		qglEnable( GL_TEXTURE_2D );
}

//============
//PGM
/*
================
DrawGLFlowingPoly -- version of DrawGLPoly that handles scrolling texture
================
*/
void DrawGLFlowingPoly (msurface_t *fa)
{
	int		i;
	float	*v;
	glpoly_t *p;
	float	scroll;

	p = fa->polys;

	scroll = -64 * ( (r_map_animation_time / 40.0) - (int)(r_map_animation_time / 40.0) );
	if(scroll == 0.0)
		scroll = -64.0;

	qglBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f ((v[3] + scroll), v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
}
//PGM
//============

/*
** R_DrawTriangleOutlines
*/
void R_DrawTriangleOutlines (void)
{
	int			i, j;
	glpoly_t	*p;

	if (!gl_showtris->value)
		return;

	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_DEPTH_TEST);
	qglColor4f (1,1,1,1);

	for (i=0 ; i<MAX_LIGHTMAPS ; i++)
	{
		msurface_t *surf;

		for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
		{
			p = surf->polys;
			for ( ; p ; p=p->chain)
			{
				for (j=2 ; j<p->numverts ; j++ )
				{
					qglBegin (GL_LINE_STRIP);
					qglVertex3fv (p->verts[0]);
					qglVertex3fv (p->verts[j-1]);
					qglVertex3fv (p->verts[j]);
					qglVertex3fv (p->verts[0]);
					qglEnd ();
				}
			}
		}
	}

	qglEnable (GL_DEPTH_TEST);
	qglEnable (GL_TEXTURE_2D);
}

/*
** DrawGLPolyChain
*/
void DrawGLPolyChain( glpoly_t *p, float soffset, float toffset )
{
	if ( soffset == 0 && toffset == 0 )
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5], v[6] );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
	else
	{
		for ( ; p != 0; p = p->chain )
		{
			float *v;
			int j;

			qglBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				qglTexCoord2f (v[5] - soffset, v[6] - toffset );
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
}

/*
** R_BlendLightMaps
**
** This routine takes all the given light mapped surfaces in the world and
** blends them into the framebuffer.
*/
void R_BlendLightmaps (void)
{
	int			i;
	msurface_t	*surf, *newdrawsurf = 0;

	// don't bother if we're set to fullbright
	if (r_fullbright->value)
		return;
	if (!r_worldmodel->lightdata)
		return;

	// don't bother writing Z
	qglDepthMask( 0 );

	/*
	** set the appropriate blending mode unless we're only looking at the
	** lightmaps.
	*/
	if (!gl_lightmap->value)
	{
		qglEnable (GL_BLEND);

		if ( gl_saturatelighting->value )
		{
			qglBlendFunc( GL_ONE, GL_ONE );
		}
		else
		{
			if ( gl_monolightmap->string[0] != '0' )
			{
				switch ( toupper( gl_monolightmap->string[0] ) )
				{
				case 'I':
					qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'L':
					qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'A':
				default:
					qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					break;
				}
			}
			else
			{
				qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
			}
		}
	}

	if ( currentmodel == r_worldmodel )
		c_visible_lightmaps = 0;

	/*
	** render static lightmaps first
	*/
	for ( i = 1; i < MAX_LIGHTMAPS; i++ )
	{
		if ( gl_lms.lightmap_surfaces[i] )
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;
			GL_Bind( gl_state.lightmap_textures + i);

			for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
			{
				if ( surf->polys )
					DrawGLPolyChain( surf->polys, 0, 0 );
			}
		}
	}

	/*
	** render dynamic lightmaps
	*/
	if ( gl_dynamic->value )
	{
		LM_InitBlock();

		GL_Bind( gl_state.lightmap_textures+0 );

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for ( surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain )
		{
			int		smax, tmax;
			byte	*base;

			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			if ( LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats (path without multitexture) */
				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
				DLS_Build (surf->dlightframe == r_framecount);	/* this chain also holds animated light styles */
			}
			else
			{
				msurface_t *drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for ( drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain )
				{
					if ( drawsurf->polys )
						DrawGLPolyChain( drawsurf->polys, 
							              ( drawsurf->light_s - drawsurf->dlight_s ) * ( 1.0 / 128.0 ), 
										( drawsurf->light_t - drawsurf->dlight_t ) * ( 1.0 / 128.0 ) );
				}

				newdrawsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if ( !LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
				{
					ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax );
				}

				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats (path without multitexture) */
				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
				DLS_Build (surf->dlightframe == r_framecount);	/* this chain also holds animated light styles */
			}
		}

		/*
		** draw remainder of dynamic lightmaps that haven't been uploaded yet
		*/
		if ( newdrawsurf )
			LM_UploadBlock( true );

		for ( surf = newdrawsurf; surf != 0; surf = surf->lightmapchain )
		{
			if ( surf->polys )
				DrawGLPolyChain( surf->polys, ( surf->light_s - surf->dlight_s ) * ( 1.0 / 128.0 ), ( surf->light_t - surf->dlight_t ) * ( 1.0 / 128.0 ) );
		}
	}

	/*
	** restore state
	*/
	qglDisable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask( 1 );
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	int			maps;
	image_t		*image;
	qboolean is_dynamic = false;

	c_brush_polys++;
	R_DlightFilterSurf (fa);	/* BDGN 3.51p: before the dynamic-lightmap decision */

	image = R_TextureAnimation (fa->texinfo);

	if (fa->flags & SURF_DRAWTURB)
	{	
		GL_Bind( image->texnum );

		// warp texture, no lightmaps
		GL_TexEnv( GL_MODULATE );
		qglColor4f( gl_state.inverse_intensity, 
			        gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					1.0F );
		EmitWaterPolys (fa);
		GL_TexEnv( GL_REPLACE );

		return;
	}
	else
	{
		GL_Bind( image->texnum );

		GL_TexEnv( GL_REPLACE );
	}

//======
//PGM
	if(fa->texinfo->flags & SURF_FLOWING)
		DrawGLFlowingPoly (fa);
	else
		DrawGLPoly (fa->polys);
//PGM
//======

	/*
	** check for lightmap modification
	*/
	for ( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if ( r_newrefdef.lightstyles[fa->styles[maps]].white != fa->cached_light[maps] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( fa->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if (!( fa->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		if ( ( fa->styles[maps] >= 32 || fa->styles[maps] == 0 ) && ( fa->dlightframe != r_framecount ) )
		{
			static unsigned	temp[34*34];	// static: a local this big makes MSVC probe every 4 KB stack page (_chkstk) on each call (AMD patch, area world W1)
			int			smax, tmax;

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats */
			R_BuildLightMap( fa, (void *)temp, smax*4 );
			DLS_Build (0);
			R_SetCacheState( fa );

			GL_Bind( gl_state.lightmap_textures + fa->lightmaptexturenum );

			BDGN_PROF_PUSH( PROF_LIGHT );	/* BDGN 3.52t */
			BDGN_PROF_N( PROFN_LIGHTMAPS, 1 );
			DLS_Begin ();	/* upload time: glTexSubImage2D only */
			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  fa->light_s, fa->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );
			DLS_Upload (smax * tmax, 0);	/* BDGN 3.52 (WESTBECK) */
			BDGN_PROF_POP();

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_DrawAlphaSurfaces

Draw water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain
of alpha_surfaces will draw back to front, giving proper ordering.
================
*/
void R_DrawAlphaSurfaces (void)
{
	msurface_t	*s;
	float		intens;
	qboolean alpha_test;

	//
	// go back to the world matrix
	//
    qglLoadMatrixf (r_world_matrix);

	alpha_test=qglIsEnabled(GL_ALPHA_TEST);
	qglDisable(GL_ALPHA_TEST);
	if (gl_solidalpha->value)
		qglDisable (GL_BLEND);	/* BDGN 3.57d: glass and water drawn solid (no blend per pixel, as sw_stipplealpha 2) */
	else
		qglEnable (GL_BLEND);
	qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	GL_SelectTexture(GL_TEXTURE0);
	GL_TexEnv( GL_MODULATE );

	// the textures are prescaled up for a better lighting range,
	// so scale it back down
	intens = gl_state.inverse_intensity;

	for (s=r_alpha.head ; s ; s=s->texturechain)
	{
		GL_Bind(s->texinfo->image->texnum);
		c_brush_polys++;
		if (gl_solidalpha->value)
			qglColor4f (intens,intens,intens,1);
		else if (s->texinfo->flags & SURF_TRANS33)
			qglColor4f (intens,intens,intens,0.33);
		else if (s->texinfo->flags & SURF_TRANS66)
			qglColor4f (intens,intens,intens,0.66);
		else
			qglColor4f (intens,intens,intens,1);
		if (s->flags & SURF_DRAWTURB)
			EmitWaterPolys (s);
		else if(s->texinfo->flags & SURF_FLOWING)			// PGM	9/16/98
			DrawGLFlowingPoly (s);							// PGM
		else
			DrawGLPoly (s->polys);
	}

	GL_TexEnv( GL_REPLACE );
	qglColor4f (1,1,1,1);
	qglDisable (GL_BLEND);
	if (alpha_test) qglEnable(GL_ALPHA_TEST);

	R_AlphaChainClear (&r_alpha);
}

/*
================
DrawTextureChains
================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

//	GL_TexEnv( GL_REPLACE );

	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;
			c_visible_textures++;

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);

			image->texturechain = NULL;
		}
	}
	else
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			if (!image->texturechain)
				continue;
			c_visible_textures++;

			for ( s = image->texturechain; s ; s=s->texturechain)
			{
				if ( !( s->flags & SURF_DRAWTURB ) )
					R_RenderBrushPoly (s);
			}
		}

		GL_EnableMultitexture( false );
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;

			for ( ; s ; s=s->texturechain)
			{
				if ( s->flags & SURF_DRAWTURB )
					R_RenderBrushPoly (s);
			}

			image->texturechain = NULL;
		}
//		GL_EnableMultitexture( true );
	}

	GL_TexEnv( GL_REPLACE );
}


static void GL_RenderLightmappedPoly( msurface_t *surf )
{
	int		i, nv = surf->polys->numverts;
	int		map;
	float	*v;
	image_t *image = R_TextureAnimation( surf->texinfo );
	qboolean is_dynamic = false;
	unsigned lmtex = surf->lightmaptexturenum;
	glpoly_t *p;

	R_DlightFilterSurf (surf);	/* BDGN 3.51p: before the dynamic-lightmap decision */
	for ( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
	{
		if ( r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( surf->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if ( !(surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		static unsigned	temp[128*128];	// static: a local this big makes MSVC probe every 4 KB stack page (_chkstk) on each call (AMD patch, area world W1)
		int			smax, tmax;

		if ( ( surf->styles[map] >= 32 || surf->styles[map] == 0 ) && ( surf->dlightframe != r_framecount ) )
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats */
			R_BuildLightMap( surf, (void *)temp, smax*4 );
			DLS_Build (0);
			R_SetCacheState( surf );

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum );

			lmtex = surf->lightmaptexturenum;

			BDGN_PROF_PUSH( PROF_LIGHT );	/* BDGN 3.52t */
			BDGN_PROF_N( PROFN_LIGHTMAPS, 1 );
			DLS_Begin ();	/* upload time: glTexSubImage2D only */
			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  surf->light_s, surf->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );
			DLS_Upload (smax * tmax, 0);	/* BDGN 3.52 (WESTBECK) */
			BDGN_PROF_POP();

		}
		else
		{
			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats */
			R_BuildLightMap( surf, (void *)temp, smax*4 );
			DLS_Build (surf->dlightframe == r_framecount);

			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + 0 );

			lmtex = 0;

			BDGN_PROF_PUSH( PROF_LIGHT );	/* BDGN 3.52t */
			BDGN_PROF_N( PROFN_LIGHTMAPS, 1 );
			DLS_Begin ();	/* upload time: glTexSubImage2D only */
			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  surf->light_s, surf->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );
			DLS_Upload (smax * tmax, 0);	/* BDGN 3.52 (WESTBECK) */
			BDGN_PROF_POP();

		}

		c_brush_polys++;

		GL_MBind( GL_TEXTURE0, image->texnum );
		GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );

//==========
//PGM
		if (surf->texinfo->flags & SURF_FLOWING)
		{
			float scroll;
		
			scroll = -64 * ( (r_map_animation_time / 40.0) - (int)(r_map_animation_time / 40.0) );
			if(scroll == 0.0)
				scroll = -64.0;

			for ( p = surf->polys; p; p = p->chain )
			{
				v = p->verts[0];
				qglBegin (GL_POLYGON);
				for (i=0 ; i< nv; i++, v+= VERTEXSIZE)
				{
					qglMTexCoord2fSGIS( GL_TEXTURE0, (v[3]+scroll), v[4]);
					qglMTexCoord2fSGIS( GL_TEXTURE1, v[5], v[6]);
					qglVertex3fv (v);
				}
				qglEnd ();
			}
		}
		else
		{
			for ( p = surf->polys; p; p = p->chain )
			{
				v = p->verts[0];
				qglBegin (GL_POLYGON);
				for (i=0 ; i< nv; i++, v+= VERTEXSIZE)
				{
					qglMTexCoord2fSGIS( GL_TEXTURE0, v[3], v[4]);
					qglMTexCoord2fSGIS( GL_TEXTURE1, v[5], v[6]);
					qglVertex3fv (v);
				}
				qglEnd ();
			}
		}
//PGM
//==========
	}
	else
	{
		c_brush_polys++;

		GL_MBind( GL_TEXTURE0, image->texnum );
		GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );

//==========
//PGM
		if (surf->texinfo->flags & SURF_FLOWING)
		{
			float scroll;
		
			scroll = -64 * ( (r_map_animation_time / 40.0) - (int)(r_map_animation_time / 40.0) );
			if(scroll == 0.0)
				scroll = -64.0;

			for ( p = surf->polys; p; p = p->chain )
			{
				v = p->verts[0];
				qglBegin (GL_POLYGON);
				for (i=0 ; i< nv; i++, v+= VERTEXSIZE)
				{
					qglMTexCoord2fSGIS( GL_TEXTURE0, (v[3]+scroll), v[4]);
					qglMTexCoord2fSGIS( GL_TEXTURE1, v[5], v[6]);
					qglVertex3fv (v);
				}
				qglEnd ();
			}
		}
		else
		{
//PGM
//==========
			for ( p = surf->polys; p; p = p->chain )
			{
				v = p->verts[0];
				qglBegin (GL_POLYGON);
				for (i=0 ; i< nv; i++, v+= VERTEXSIZE)
				{
					qglMTexCoord2fSGIS( GL_TEXTURE0, v[3], v[4]);
					qglMTexCoord2fSGIS( GL_TEXTURE1, v[5], v[6]);
					qglVertex3fv (v);
				}
				qglEnd ();
			}
//==========
//PGM
		}
//PGM
//==========
	}
}

/*
=============================================================================
BDGN 3.42: gl_batchworld - batched drawing of the world

The static lightmapped world polygons are collected during the BSP walk, sorted by (texture, lightmap block)
with a radix sort, and drawn as triangles from one client side vertex array: one glDrawElements per run of
polygons instead of three GL calls per vertex.  Polygons that change their lightmap this frame (dynamic
lights, light styles) and flowing surfaces are drawn at once as before; the opaque world is depth tested, so
the order does not matter.  Needs ARB multitexture and vertex arrays; not used on drivers marked unsafe
(3dfx, see gl_vaunsafe / gl_fastforce).
=============================================================================
*/
#define WQ_MAXSURF	4096
#define WQ_MAXV		2048

cvar_t	*gl_batchworld;

static int			wq_on;
static int			wq_n;
static msurface_t	*wq_surf[WQ_MAXSURF];
static int			wq_key[WQ_MAXSURF];
static float		wq_va[WQ_MAXV*VERTEXSIZE];
static unsigned short	wq_idx[WQ_MAXV*3];

static void R_WorldQueueBegin (void)
{
	wq_n = 0;
	wq_on = ( gl_batchworld && gl_batchworld->value && !r_drawflat->value && !gl_fastworld->value
			&& !( gl_flatfloors && gl_flatfloors->value )	/* BDGN 3.58w */
			&& !gl_hack_notextures->value && !gl_lightmap->value
			&& qglActiveTextureARB && qglClientActiveTextureARB && qglDrawElements && qglVertexPointer
			&& qglTexCoordPointer && qglEnableClientState && qglDisableClientState
			&& ( !gl_vaunsafe || gl_fastforce->value ) ) ? 1 : 0;
}

static void R_WorldDrawBatch (int ni)
{
	if ( ni > 0 )
		qglDrawElements( GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, wq_idx );
}

static void R_FlushWorldQueue (void)
{
	static int	order[WQ_MAXSURF], tmp[WQ_MAXSURF];
	static int	cnt[512];
	int			n = wq_n, i, k, pass, nv = 0, ni = 0, curkey = -1;
	int			stride = VERTEXSIZE * sizeof(float);
	glpoly_t	*p;

	wq_n = 0;
	if ( !n )
		return;

	// radix sort of the keys, two passes of 9 bits (stable)
	for ( i = 0 ; i < n ; i++ )
		order[i] = i;
	for ( pass = 0 ; pass < 2 ; pass++ )
	{
		int shift = pass * 9;

		memset( cnt, 0, sizeof( cnt ) );
		for ( i = 0 ; i < n ; i++ )
			cnt[( wq_key[order[i]] >> shift ) & 511]++;
		for ( i = 1 ; i < 512 ; i++ )
			cnt[i] += cnt[i-1];
		for ( i = n - 1 ; i >= 0 ; i-- )
			tmp[--cnt[( wq_key[order[i]] >> shift ) & 511]] = order[i];
		memcpy( order, tmp, n * sizeof( int ) );
	}

	qglEnableClientState( GL_VERTEX_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, stride, wq_va );
	qglClientActiveTextureARB( GL_TEXTURE0 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, stride, wq_va + 3 );
	qglClientActiveTextureARB( GL_TEXTURE1 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, stride, wq_va + 5 );
	qglClientActiveTextureARB( gl_state.currenttmu ? GL_TEXTURE1 : GL_TEXTURE0 );

	for ( k = 0 ; k < n ; k++ )
	{
		msurface_t	*surf = wq_surf[order[k]];
		int			key = wq_key[order[k]];

		if ( key != curkey )
		{
			R_WorldDrawBatch( ni );
			nv = ni = 0;
			GL_MBind( GL_TEXTURE0, gltextures[key >> 8].texnum );
			GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + ( key & 255 ) );
			curkey = key;
		}

		c_brush_polys++;
		for ( p = surf->polys ; p ; p = p->chain )
		{
			int nvp = p->numverts;

			if ( nvp < 3 )
				continue;
			if ( nv + nvp > WQ_MAXV )
			{
				R_WorldDrawBatch( ni );
				nv = ni = 0;
			}
			Q3M_CopyVerts( wq_va + nv * VERTEXSIZE, p->verts[0], nvp );
			for ( i = 1 ; i < nvp - 1 ; i++ )
			{
				wq_idx[ni++] = (unsigned short)nv;
				wq_idx[ni++] = (unsigned short)( nv + i );
				wq_idx[ni++] = (unsigned short)( nv + i + 1 );
			}
			nv += nvp;
		}
	}
	R_WorldDrawBatch( ni );

	qglClientActiveTextureARB( GL_TEXTURE1 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglClientActiveTextureARB( GL_TEXTURE0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisableClientState( GL_VERTEX_ARRAY );
	qglClientActiveTextureARB( gl_state.currenttmu ? GL_TEXTURE1 : GL_TEXTURE0 );
	wq_on = 0;
}

/* a static lightmapped polygon is queued, anything else is drawn as before */
static void R_DrawWorldPoly (msurface_t *surf)
{
	int			map, dynamic = 0;
	image_t		*image;

	if ( !wq_on || ( surf->texinfo->flags & SURF_FLOWING ) || surf->polys->numverts > WQ_MAXV )
	{
		GL_RenderLightmappedPoly( surf );
		return;
	}

	R_DlightFilterSurf (surf);	/* BDGN 3.51p: before the dynamic-lightmap decision */
	for ( map = 0 ; map < MAXLIGHTMAPS && surf->styles[map] != 255 ; map++ )
	{
		if ( r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map] )
			dynamic = 1;
	}
	if ( surf->dlightframe == r_framecount )
		dynamic = 1;
	if ( dynamic && gl_dynamic->value )
	{	// the lightmap changes: upload and draw at once (same as GL_RenderLightmappedPoly)
		GL_RenderLightmappedPoly( surf );
		return;
	}

	if ( wq_n >= WQ_MAXSURF )
		R_FlushWorldQueue ();
	image = R_TextureAnimation( surf->texinfo );
	wq_surf[wq_n] = surf;
	wq_key[wq_n] = ( (int)( image - gltextures ) << 8 ) | ( surf->lightmaptexturenum & 255 );
	wq_n++;
}

/*
=================
R_DrawInlineBModel
=================
*/
void R_DrawInlineBModel (void)
{
	int			i, k;
	cplane_t	*pplane;
	float		dot;
	msurface_t	*psurf;
	dlight_t	*lt;

	// calculate dynamic lighting for bmodel
	if ( !gl_flashblend->value )
	{
		lt = r_newrefdef.dlights;
		for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
		{
			/* BDGN 3.51p: same distance limit as the world (no view test: brush models mark in their own space) */
			if (R_DlightMarksView (lt->origin, lt->intensity, 0, NULL, NULL, 0, r_origin, 0, gl_dlight_maxdist->value))
				R_MarkLights (lt, 1<<k, currentmodel->nodes + currentmodel->firstnode);
		}
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglEnable (GL_BLEND);
		qglColor4f (1,1,1,0.25);
		GL_TexEnv( GL_MODULATE );
	}

	//
	// draw texture
	//
	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if ( r_drawflat->value )
				R_DrawFlatSurface( psurf );		// BDGN 3.38
			else if (psurf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) )
			{	// add to the translucent chain
				R_AlphaChainAdd (&r_alpha, psurf);	/* BDGN 3.56b: once per list */
			}
			else if ( qglMTexCoord2fSGIS && !( psurf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( psurf );
			}
			else
			{
				GL_EnableMultitexture( false );
				R_RenderBrushPoly( psurf );
				GL_EnableMultitexture( ( gl_fastworld->value && !gl_hack_notextures->value ) ? false : true );	// BDGN 3.4: gl_fastworld = no lightmaps
			}
		}
	}

	R_FlatRunEnd ();
	if ( r_drawflat->value )
		qglColor4f (1,1,1,1);

	if ( !(currententity->flags & RF_TRANSLUCENT) )
	{
		if ( !qglMTexCoord2fSGIS )
			R_BlendLightmaps ();
	}
	else
	{
		qglDisable (GL_BLEND);
		qglColor4f (1,1,1,1);
		GL_TexEnv( GL_REPLACE );
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, currentmodel->mins, mins);
		VectorAdd (e->origin, currentmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		R_AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

    qglPushMatrix ();
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug
	R_RotateForEntity (e);
e->angles[0] = -e->angles[0];	// stupid quake bug
e->angles[2] = -e->angles[2];	// stupid quake bug

	GL_EnableMultitexture( ( gl_fastworld->value && !gl_hack_notextures->value ) ? false : true );
	GL_SelectTexture( GL_TEXTURE0);
	GL_TexEnv( GL_REPLACE );
	GL_SelectTexture( GL_TEXTURE1);
	GL_TexEnv( GL_MODULATE );

	R_DrawInlineBModel ();
	GL_EnableMultitexture( false );

	qglPopMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return;
	
// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
				return;		// not visible
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side

		if ( r_drawflat->value )
		{	// BDGN 3.38: sky, water, glass and normal surfaces alike
			R_DrawFlatSurface (surf);
		}
		else if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{	// add to the translucent chain
			R_AlphaChainAdd (&r_alpha, surf);	/* BDGN 3.56b: once per list */
		}
		else if ( !( surf->flags & SURF_DRAWTURB ) && R_FlatFloor( surf ) )
		{
			R_DrawFlatSurfaceMode( surf, 2 );	/* BDGN 3.58w: gl_flatfloors */
			qglColor4f( 1, 1, 1, 1 );
			if ( qglMTexCoord2fSGIS )
				GL_EnableMultitexture( true );	/* back for the next lightmapped polygon */
		}
		else
		{
			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				R_DrawWorldPoly( surf );		// BDGN 3.42: gl_batchworld
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				image = R_TextureAnimation (surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side]);
/*
	for ( ; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side

		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{	// add to the translucent chain
//			surf->texturechain = alpha_surfaces;
//			alpha_surfaces = surf;
		}
		else
		{
			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( surf );
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				image = R_TextureAnimation (surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}
*/
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (!r_drawworld->value)
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	VectorCopy (r_newrefdef.vieworg, modelorg);

	// auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_map_animation_time*2);
	currententity = &ent;

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	R_ClearSkyBox ();

	if ( qglMTexCoord2fSGIS )
	{
		GL_EnableMultitexture( ( gl_fastworld->value && !gl_hack_notextures->value ) ? false : true );

		// doogies code
//		GL_SelectTexture( GL_TEXTURE0);
//		GL_TexEnv( GL_REPLACE );
//		GL_SelectTexture( GL_TEXTURE1);
		if ( gl_hack_notextures->value )
		{
			// Q2TTM: don't apply textures to world surfaces, if set to "1"
		}
		else
		{
			GL_SelectTexture( GL_TEXTURE0);
			GL_TexEnv( GL_REPLACE );
			GL_SelectTexture( GL_TEXTURE1);
		}
		// end
		if ( gl_lightmap->value )
			GL_TexEnv( GL_REPLACE );
		else 
			GL_TexEnv( GL_MODULATE );

		R_WorldQueueBegin ();
		R_RecursiveWorldNode (r_worldmodel->nodes);
		R_FlushWorldQueue ();

		GL_EnableMultitexture( false );
	}
	else
	{
		R_RecursiveWorldNode (r_worldmodel->nodes);
	}

	/*
	** theoretically nothing should happen in the next two functions
	** if multitexture is enabled
	*/
	R_FlatRunEnd ();
	DrawTextureChains ();
	R_BlendLightmaps ();
	
	if ( r_drawflat->value )
		qglColor4f (1,1,1,1);
	else if ( r_drawdistance <= 0 )		// with a draw distance the sky box would be cut off by the far plane
		R_DrawSkyBox ();

	R_DrawTriangleOutlines ();
}


/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where
	// the pvs ends
	if (gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}
	
	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}

#if 0
	for (i=0 ; i<r_worldmodel->vis->numclusters ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&r_worldmodel->leafs[i];	// FIXME: cluster
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
#endif
}



/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ) );
}

/* BDGN 3.52t: uploading lightmaps is measured as PROF_LIGHT */
static void LM_UploadBlockReal( qboolean dynamic );
static void LM_UploadBlock( qboolean dynamic )
{
	BDGN_PROF_N( PROFN_LIGHTMAPS, 1 );	/* BDGN 3.52v */
	BDGN_PROF_CALL( PROF_LIGHT, LM_UploadBlockReal( dynamic ) );
}
static void LM_UploadBlockReal( qboolean dynamic )
{
	int texture;
	int height = 0;

	if ( dynamic )
	{
		texture = 0;
	}
	else
	{
		texture = gl_lms.current_lightmap_texture;
	}

	GL_Bind( gl_state.lightmap_textures + texture );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if ( dynamic )
	{
		int i;

		for ( i = 0; i < BLOCK_WIDTH; i++ )
		{
			if ( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		DLS_Begin ();	/* BDGN 3.52 (WESTBECK): dlight_stats (whole dynamic block) */
		qglTexSubImage2D( GL_TEXTURE_2D, 
						  0,
						  0, 0,
						  BLOCK_WIDTH, height,
						  GL_LIGHTMAP_FORMAT,
						  GL_UNSIGNED_BYTE,
						  gl_lms.lightmap_buffer );
		DLS_Upload (BLOCK_WIDTH * height, 1);	/* BDGN 3.52 (WESTBECK): dlight_stats */
	}
	else
	{
		qglTexImage2D( GL_TEXTURE_2D, 
					   0, 
					   gl_lms.internal_format,
					   BLOCK_WIDTH, BLOCK_HEIGHT, 
					   0, 
					   GL_LIGHTMAP_FORMAT, 
					   GL_UNSIGNED_BYTE, 
					   gl_lms.lightmap_buffer );
		if ( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			ri.Sys_Error( ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n" );
	}
}

// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;

	best = BLOCK_HEIGHT;

	for (i=0 ; i<BLOCK_WIDTH-w ; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (gl_lms.allocated[i+j] >= best)
				break;
			if (gl_lms.allocated[i+j] > best2)
				best2 = gl_lms.allocated[i+j];
		}
		if (j == w)
		{	// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return false;

	for (i=0 ; i<w ; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	int			vertpage;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;
	vec3_t		total;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	VectorClear (total);
	//
	// draw texture
	//
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd (total, vec, total);
		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	poly->numverts = lnumverts;

}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int		smax, tmax;
	byte	*base;

	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		LM_InitBlock();
		if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
		{
			ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax );
		}
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState( surf );
	R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
}


/*
==================
GL_BeginBuildingLightmaps

==================
*/
void GL_BeginBuildingLightmaps (model_t *m)
{
	static lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int				i;
	unsigned		dummy[128*128];

	memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated) );

	r_framecount = 1;		// no dlightcache

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE1);

	/*
	** setup the base lightstyles so the lightmaps won't have to be regenerated
	** the first time they're seen
	*/
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	if (!gl_state.lightmap_textures)
	{
		gl_state.lightmap_textures	= TEXNUM_LIGHTMAPS;
//		gl_state.lightmap_textures	= gl_state.texture_extension_number;
//		gl_state.texture_extension_number = gl_state.lightmap_textures + MAX_LIGHTMAPS;
	}

	gl_lms.current_lightmap_texture = 1;

	/*
	** if mono lightmaps are enabled and we want to use alpha
	** blending (a,1-a) then we're likely running on a 3DLabs
	** Permedia2.  In a perfect world we'd use a GL_ALPHA lightmap
	** in order to conserve space and maximize bandwidth, however 
	** this isn't a perfect world.
	**
	** So we have to use alpha lightmaps, but stored in GL_RGBA format,
	** which means we only get 1/16th the color resolution we should when
	** using alpha lightmaps.  If we find another board that supports
	** only alpha lightmaps but that can at least support the GL_ALPHA
	** format then we should change this code to use real alpha maps.
	*/
	if ( toupper( gl_monolightmap->string[0] ) == 'A' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	/*
	** try to do hacked colored lighting with a blended texture
	*/
	else if ( toupper( gl_monolightmap->string[0] ) == 'C' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'I' )
	{
		gl_lms.internal_format = GL_INTENSITY8;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'L' ) 
	{
		gl_lms.internal_format = GL_LUMINANCE8;
	}
	else
	{
		gl_lms.internal_format = gl_tex_solid_format;
	}

	/*
	** initialize the dynamic lightmap texture
	*/
	GL_Bind( gl_state.lightmap_textures + 0 );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexImage2D( GL_TEXTURE_2D, 
				   0, 
				   gl_lms.internal_format,
				   BLOCK_WIDTH, BLOCK_HEIGHT, 
				   0, 
				   GL_LIGHTMAP_FORMAT, 
				   GL_UNSIGNED_BYTE, 
				   dummy );
}

/*
=======================
GL_EndBuildingLightmaps
=======================
*/
void GL_EndBuildingLightmaps (void)
{
	LM_UploadBlock( false );
	GL_EnableMultitexture( false );
}

