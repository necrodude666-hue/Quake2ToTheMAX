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
// r_light.c

#include "gl_local.h"
#include "../qcommon/bdgn_prof.h"
#include "gl_dlightcull.h"	/* BDGN 3.51p */
#include <stddef.h>		/* offsetof: BDGN 3.50 layout checks for the 3DNow! kernels */

int	r_dlightframecount;

#define	DLIGHT_CUTOFF	64

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

void R_RenderDlight (dlight_t *light)
{
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;

	rad = light->intensity * 0.35;

	VectorSubtract (light->origin, r_origin, v);
#if 0
	// FIXME?
	if (VectorLengthF (v) < rad)
	{	// view is inside the dlight
		V_AddBlend (light->color[0], light->color[1], light->color[2], light->intensity * 0.0003, v_blend);
		return;
	}
#endif

	qglBegin (GL_TRIANGLE_FAN);
	qglColor3f (light->color[0]*0.2, light->color[1]*0.2, light->color[2]*0.2);
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	qglVertex3fv (v);
	qglColor3f (0,0,0);
	for (i=16 ; i>=0 ; i--)
	{
		a = i/16.0 * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*cos(a)*rad
				+ vup[j]*sin(a)*rad;
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;

	if (!gl_flashblend->value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	qglDepthMask (0);
	qglDisable (GL_TEXTURE_2D);
	qglShadeModel (GL_SMOOTH);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_ONE, GL_ONE);

	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_RenderDlight (l);

	qglColor3f (1,1,1);
	qglDisable (GL_BLEND);
	qglEnable (GL_TEXTURE_2D);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask (1);
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
static void R_MarkLights_C (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->intensity-DLIGHT_CUTOFF)
	{
		R_MarkLights_C (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->intensity+DLIGHT_CUTOFF)
	{
		R_MarkLights_C (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights_C (light, bit, node->children[0]);
	R_MarkLights_C (light, bit, node->children[1]);
}

/*
BDGN 3.50: the 3DNow! kernels below read mnode_t, msurface_t, cplane_t and dlight_t through the mirror structs in
gl_3dnow.h.  This fails to compile if a layout changes.
*/
typedef char q3n_layoutcheck[ sizeof(mnode_t) == sizeof(q3n_node_t) && sizeof(msurface_t) == sizeof(q3n_surf_t)
	&& sizeof(cplane_t) == sizeof(q3n_plane_t) && sizeof(dlight_t) == 7 * sizeof(float)
	&& offsetof(mnode_t, children) == offsetof(q3n_node_t, children)
	&& offsetof(mnode_t, firstsurface) == offsetof(q3n_node_t, firstsurface)
	&& offsetof(msurface_t, texinfo) == offsetof(q3n_surf_t, texinfo)
	&& offsetof(msurface_t, dlightframe) == offsetof(q3n_surf_t, dlightframe)
	&& offsetof(msurface_t, styles) == offsetof(q3n_surf_t, styles)
	&& offsetof(msurface_t, samples) == offsetof(q3n_surf_t, samples)
	&& offsetof(msurface_t, alphaframe) == offsetof(q3n_surf_t, alphaframe)
	&& offsetof(mtexinfo_t, vecs) == 0 && sizeof(lightstyle_t) == 4 * sizeof(float) ? 1 : -1 ];

void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	if ( q3d_use_3dnow && gl_amd_marklights->value )
	{	// BDGN 3.50: AMD's 3DNow! R_MarkLights, the whole walk in one call
		Q3N_MarkLights( light->origin, bit, (q3n_node_t *)node, (q3n_surf_t *)r_worldmodel->surfaces, r_dlightframecount );
		return;
	}
	R_MarkLights_C (light, bit, node);
}


/*
=============
R_PushDlights
=============
*/
/* BDGN 3.51p: dynamic-light reach per world surface (gl_dlightcull.h), computed once per map from the in-plane
   stretch of each surface's texture mapping; r_dlight_reachmax = largest on the map */
static float	*r_dlight_surfreach;
static int		r_dlight_nsurf;
static float	r_dlight_reachmax;
static float	r_dlight_need[MAX_DLIGHTS];		/* per light, this frame (0 = in view) */
static int		r_dlight_filter;				/* gl_dlight_cull active this frame */

static void R_DlightMapReach (void)
{
	static model_t	*model;
	static int		sequence = -1;
	msurface_t		*surf;
	float			k;
	int				i;

	if (model == r_worldmodel && sequence == registration_sequence && r_dlight_surfreach)
		return;
	model = r_worldmodel;
	sequence = registration_sequence;
	if (r_dlight_surfreach)
		free (r_dlight_surfreach);
	r_dlight_nsurf = r_worldmodel->numsurfaces;
	r_dlight_surfreach = (float *)malloc (sizeof(float) * (r_dlight_nsurf > 0 ? r_dlight_nsurf : 1));
	r_dlight_reachmax = DLIGHT_REACH_ANY;		/* no table: never skip a light */
	if (!r_dlight_surfreach)
	{
		r_dlight_nsurf = 0;
		return;
	}
	r_dlight_reachmax = 1.0f;
	for (i = 0, surf = r_worldmodel->surfaces ; i < r_dlight_nsurf ; i++, surf++)
	{
		if ((surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)) || !surf->texinfo || !surf->plane)
			k = 1.0f;		/* no lightmap: never rebuilt for dynamic lights */
		else
			k = R_DlightSurfReach (R_DlightTexSigma (surf->texinfo->vecs[0], surf->texinfo->vecs[1], surf->plane->normal));
		r_dlight_surfreach[i] = k;
		if (k > r_dlight_reachmax)
			r_dlight_reachmax = k;
	}
}

/* BDGN 3.51p: before a surface decides whether its lightmap is dynamic this frame: drop the lights that cannot change
   a visible texel through this surface; with none left it keeps its static lightmap. Safe to call more than once. */
void R_DlightFilterSurf (msurface_t *surf)
{
	int		i, bits, index;
	float	reach;

	if (!r_dlight_filter || surf->dlightframe != r_framecount || !r_dlight_surfreach)
		return;
	index = (int)(surf - r_worldmodel->surfaces);
	if (index < 0 || index >= r_dlight_nsurf)
		return;
	reach = r_dlight_surfreach[index];
	bits = surf->dlightbits;
	for (i = 0 ; i < r_newrefdef.num_dlights && i < MAX_DLIGHTS ; i++)
		if ((bits & (1 << i)) && !R_DlightSurfKeeps (r_dlight_need[i], reach))
			bits &= ~(1 << i);
	surf->dlightbits = bits;
	if (!bits)
		surf->dlightframe = r_framecount - 1;	/* not dynamic this frame */
}

void R_PushDlights (void)
{
	int		i, p, planes, cull;
	dlight_t	*l;
	float	normals[DLIGHT_MAX_PLANES][3], dists[DLIGHT_MAX_PLANES], maxdist, reach;

	r_dlight_filter = 0;
	if (gl_flashblend->value)
		return;
	if (gl_doomlight->value || gl_blakelight->value)
		return;	/* BDGN 3.57d: Doom light and Blake Stone shading have no dynamic lights (no lightmap uploads for them) */
	if (!r_newrefdef.num_dlights || !r_worldmodel)
		return;		/* BDGN 3.51p: nothing to mark (and no world before the first map, e.g. the player setup menu) */

	r_dlightframecount = r_framecount;	// BDGN 3.51p: now called after R_SetupFrame (was r_framecount + 1 before it)

	/* BDGN 3.51p: lights that cannot reach the view (gl_dlight_cull) or are too far (gl_dlight_maxdist) mark nothing */
	planes = r_numfrustumplanes > DLIGHT_MAX_PLANES ? DLIGHT_MAX_PLANES : r_numfrustumplanes;
	for (p = 0 ; p < planes ; p++)
	{
		VectorCopy (frustum[p].normal, normals[p]);
		dists[p] = frustum[p].dist;
	}
	cull = gl_dlight_cull->value != 0;
	maxdist = gl_dlight_maxdist->value;
	if (cull)
		R_DlightMapReach ();
	reach = cull ? r_dlight_reachmax : 0;
	r_dlight_filter = cull && r_dlight_surfreach;

	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
	{
		r_dlight_need[i & (MAX_DLIGHTS - 1)] = r_dlight_filter ? R_DlightNeed (l->origin, l->intensity, (const float (*)[3])normals, dists, planes) : 0;
		if (R_DlightMarksView (l->origin, l->intensity, reach, (const float (*)[3])normals, dists, planes, r_origin, cull, maxdist))
			R_MarkLights ( l, 1<<i, r_worldmodel->nodes );
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;

	if (node->contents != -1)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags&(SURF_DRAWTURB|SURF_DRAWSKY)) 
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy (vec3_origin, pointcolor);
		if (lightmap)
		{
			vec3_t scale;

			lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				for (i=0 ; i<3 ; i++)
					scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

				pointcolor[0] += lightmap[0] * scale[0] * (1.0/255);
				pointcolor[1] += lightmap[1] * scale[1] * (1.0/255);
				pointcolor[2] += lightmap[2] * scale[2] * (1.0/255);
				lightmap += 3*((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}
		
		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/*
===============
R_LightPoint
===============
*/
qboolean R_ShadowFloor(vec3_t origin,vec3_t point,vec3_t normal)
{
	vec3_t end;
	if (!r_worldmodel || !r_worldmodel->lightdata) return false;
	VectorCopy(origin,end);
	end[2]-=96;
	if (RecursiveLightPoint(r_worldmodel->nodes,origin,end)<0 || !lightplane)
		return false;
	VectorCopy(lightspot,point);
	VectorCopy(lightplane->normal,normal);
	if (normal[2]<0) VectorScale(normal,-1,normal);
	return normal[2]>=0.7f;
}

/* BDGN 3.55m: brightness of the static light map below p (no dynamic lights); lightspot/lightplane are kept */
float R_StaticLightLevel (vec3_t p)
{
	vec3_t end,spot,colour;
	cplane_t *plane=lightplane;
	if (!r_worldmodel || !r_worldmodel->lightdata) return 0;
	VectorCopy(lightspot,spot);
	VectorCopy(pointcolor,colour);
	VectorCopy(p,end);
	end[2]-=2048;
	if (RecursiveLightPoint(r_worldmodel->nodes,p,end)==-1) {
		VectorCopy(spot,lightspot); VectorCopy(colour,pointcolor); lightplane=plane;
		return 0;
	}
	end[0]=pointcolor[0]*0.30f+pointcolor[1]*0.59f+pointcolor[2]*0.11f;
	VectorCopy(spot,lightspot); VectorCopy(colour,pointcolor); lightplane=plane;
	return end[0];
}

/*
BDGN 3.56b: R_LightPoint in two parts.  R_LightPointWorld is the world light at p: the lightmap sample below the point
with its light styles and gl_modulate once (the scale of the wall's own lightmap), no dynamic lights.  Blood marks use
it (gl_decal.c); they keep their light for their whole life, so a passing muzzle flash must not be part of it.
*/
void R_LightPointWorld (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;

	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	if ( q3d_use_3dnow && gl_amd_lightpoint->value )
	{	// BDGN 3.50: AMD's 3DNow! RecursiveLightPoint (same pointcolor, lightspot, lightplane)
		q3n_lightpoint_t	lp;

		lp.surfaces = (const q3n_surf_t *)r_worldmodel->surfaces;
		lp.lightstyles = r_newrefdef.lightstyles[0].rgb;
		lp.modulate = &gl_modulate->value;
		lp.skipflags = SURF_DRAWTURB | SURF_DRAWSKY;
		lp.color = pointcolor;
		lp.spot = lightspot;
		lp.plane = (q3n_plane_t **)&lightplane;
		r = Q3N_LightPoint( &lp, (const q3n_node_t *)r_worldmodel->nodes, p, end );
	}
	else
		r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}
}

/* the world light, the dynamic lights at p, and gl_modulate a second time (id's model lighting) */
void R_LightPoint (vec3_t p, vec3_t color)
{
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;

	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	R_LightPointWorld (p, color);

	//
	// add dynamic lights
	//
	light = 0;
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		if (lnum<32 && (gl_pixel_model_skip&(1<<lnum)))
			continue;	/* BDGN 3.55h: this light reaches the model per pixel instead */
		/* BDGN 3.56b: from p, the point asked about (id measured from currententity->origin: the same for models,
		   but blood marks, the light level under the eye and other points got the lights of whatever entity was
		   drawn last, e.g. the view weapon at the muzzle flash) */
		VectorSubtract (p,
						dl->origin,
						dist);
		add = dl->intensity - VectorLengthF(dist);
		add *= (1.0/256);
		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}

	VectorScale (color, gl_modulate->value, color);
}


//===================================================================

static float s_blocklights[34*34*3];
static int   s_iblocklights[34*34*3];	// 3DNow!: rounded copy of s_blocklights
/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		fdist, frad, fminlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	float		*pfBL;
	float		fsacc, ftacc;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ((gl_pixel_lightmask&(1<<lnum)) && currentmodel==r_worldmodel &&
		    !(surf->texinfo->flags&SURF_FLOWING)) continue;
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		frad = dl->intensity;
		fdist = DotProduct (dl->origin, surf->plane->normal) -
				surf->plane->dist;
		frad -= fabs(fdist);
		// rad is now the highest intensity on the plane

		fminlight = DLIGHT_CUTOFF;	// FIXME: make configurable?
		if (frad < fminlight)
			continue;
		fminlight = frad - fminlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dl->origin[i] -
					surf->plane->normal[i]*fdist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		pfBL = s_blocklights;

		if ( GL_3DNOW() )
		{
			Q3D_AddDlight( pfBL, smax, tmax, local[0], local[1], frad, fminlight, dl->color );
			continue;
		}

		for (t = 0, ftacc = 0 ; t<tmax ; t++, ftacc += 16)
		{
			td = local[1] - ftacc;
			if ( td < 0 )
				td = -td;

			for ( s=0, fsacc = 0 ; s<smax ; s++, fsacc += 16, pfBL += 3)
			{
				sd = Q_ftol( local[0] - fsacc );

				if ( sd < 0 )
					sd = -sd;

				if (sd > td)
					fdist = sd + (td>>1);
				else
					fdist = td + (sd>>1);

				if ( fdist < fminlight )
				{
					pfBL[0] += ( frad - fdist ) * dl->color[0];
					pfBL[1] += ( frad - fdist ) * dl->color[1];
					pfBL[2] += ( frad - fdist ) * dl->color[2];
				}
			}
		}
	}
}


/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
/* BDGN 3.52t: building a lightmap is measured as PROF_LIGHT (the static ones at map load are before the timedemo) */
static void R_BuildLightMapReal (msurface_t *surf, byte *dest, int stride);
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	BDGN_PROF_CALL( PROF_LIGHT, R_BuildLightMapReal( surf, dest, stride ) );
}
static void R_BuildLightMapReal (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	int			r, g, b, a, max;
	int			i, j, size;
	byte		*lightmap;
	float		scale[4];
	int			nummaps;
	float		*bl;
	lightstyle_t	*style;
	int monolightmap;
	int use3d;
	int *ibl;

	if ( surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) )
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	if (size > (sizeof(s_blocklights)>>4) )
		ri.Sys_Error (ERR_DROP, "Bad s_blocklights size");

	/* BDGN 3.57d: Blake Stone shading: no light maps at all, one level per surface by its direction (floors 80 %,
	   ceilings 60 %, walls 75-100 % by facing), as sw_blakelight */
	if (gl_blakelight->value)
	{
		float f, nz, ny, lv;
		nz = surf->plane->normal[2]; ny = surf->plane->normal[1];
		if (surf->flags & SURF_PLANEBACK) { nz = -nz; ny = -ny; }
		if (nz > 0.7f) f = 0.8f;
		else if (nz < -0.7f) f = 0.6f;
		else f = 0.75f + 0.25f * (float)fabs(ny);
		lv = 150.0f * f * gl_modulate->value;
		for (i=0 ; i<size*3 ; i++)
			s_blocklights[i] = lv;
		goto store;
	}

// set to full bright if no light data
	if (!surf->samples)
	{
		int maps;

		for (i=0 ; i<size*3 ; i++)
			s_blocklights[i] = 255;
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			style = &r_newrefdef.lightstyles[surf->styles[maps]];
		}
		goto store;
	}

	// count the # of maps
	for ( nummaps = 0 ; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255 ;
		 nummaps++)
		;

	lightmap = surf->samples;

	// add all the lightmaps
	if ( nummaps == 1 )
	{
		int maps;

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( GL_3DNOW() || (size>=4 && (Q3D_SSE2LightmapEnabled() || Q3D_SSEPackedEnabled())) )
			{
				Q3D_LightmapScale( bl, lightmap, size, scale, 0 );
			}
			else if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0];
					bl[1] = lightmap[i*3+1];
					bl[2] = lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0] * scale[0];
					bl[1] = lightmap[i*3+1] * scale[1];
					bl[2] = lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}
	else
	{
		int maps;

		memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * 3 );

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( GL_3DNOW() || (size>=4 && (Q3D_SSE2LightmapEnabled() || Q3D_SSEPackedEnabled())) )
			{
				Q3D_LightmapScale( bl, lightmap, size, scale, 1 );
			}
			else if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3 )
				{
					bl[0] += lightmap[i*3+0];
					bl[1] += lightmap[i*3+1];
					bl[2] += lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] += lightmap[i*3+0] * scale[0];
					bl[1] += lightmap[i*3+1] * scale[1];
					bl[2] += lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}

	/* BDGN 3.57d: Doom light: the whole surface gets its average level in 8 steps (colour kept), no dynamic lights,
	   as sw_doomlight */
	if (gl_doomlight->value)
	{
		float sum[3] = {0,0,0}, lv;
		int c;
		for (i=0 ; i<size ; i++)
			for (c=0 ; c<3 ; c++)
				sum[c] += s_blocklights[i*3+c];
		for (c=0 ; c<3 ; c++)
		{
			lv = sum[c] / size;
			lv = (float)((((int)lv + 16) >> 5) << 5);
			for (i=0 ; i<size ; i++)
				s_blocklights[i*3+c] = lv;
		}
		goto store;
	}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

// put into texture format
store:
	stride -= (smax<<2);
	bl = s_blocklights;

	use3d = GL_3DNOW();
	ibl = s_iblocklights;
	if ( use3d )
		Q3D_RoundFloats( s_iblocklights, s_blocklights, smax*tmax*3 );

	monolightmap = gl_monolightmap->string[0];

	if ( 0 && monolightmap == '0' && use3d && q3d_use_mmx )	/* BDGN 3.51bbt: no MMX per frame (MMX was slower on the Pentium II and K6-III+); C store */
	{	// BDGN 3.41: MMX store, same result as the loop below
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			Q3M_LightStoreRow( dest, ibl, smax );
			ibl += smax*3;
			dest += smax*4;
		}
	}
	else if ( monolightmap == '0' )
	{
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				
				if ( use3d )
				{
					r = ibl[0];
					g = ibl[1];
					b = ibl[2];
					ibl += 3;
				}
				else
				{
					r = Q_ftol( bl[0] );
					g = Q_ftol( bl[1] );
					b = Q_ftol( bl[2] );
				}

				// catch negative lights
				if (r < 0)
					r = 0;
				if (g < 0)
					g = 0;
				if (b < 0)
					b = 0;

				/*
				** determine the brightest of the three color components
				*/
				if (r > g)
					max = r;
				else
					max = g;
				if (b > max)
					max = b;

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that 
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				dest[0] = r;
				dest[1] = g;
				dest[2] = b;
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
	else
	{
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				
				if ( use3d )
				{
					r = ibl[0];
					g = ibl[1];
					b = ibl[2];
					ibl += 3;
				}
				else
				{
					r = Q_ftol( bl[0] );
					g = Q_ftol( bl[1] );
					b = Q_ftol( bl[2] );
				}

				// catch negative lights
				if (r < 0)
					r = 0;
				if (g < 0)
					g = 0;
				if (b < 0)
					b = 0;

				/*
				** determine the brightest of the three color components
				*/
				if (r > g)
					max = r;
				else
					max = g;
				if (b > max)
					max = b;

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that 
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				/*
				** So if we are doing alpha lightmaps we need to set the R, G, and B
				** components to 0 and we need to set alpha to 1-alpha.
				*/
				switch ( monolightmap )
				{
				case 'L':
				case 'I':
					r = a;
					g = b = 0;
					break;
				case 'C':
					// try faking colored lighting
					a = 255 - ((r+g+b)/3);
					r *= a/255.0;
					g *= a/255.0;
					b *= a/255.0;
					break;
				case 'A':
				default:
					r = g = b = 0;
					a = 255 - a;
					break;
				}

				dest[0] = r;
				dest[1] = g;
				dest[2] = b;
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
}

