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

#include "r_local.h"
#include "sw_fastpath.h"	/* BDGN 3.56c */

int	r_dlightframecount;


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
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
//=====
//PGM
	i=light->intensity;
	if(i<0)
		i=-i;
//PGM
//=====

	if (dist > i)	// PGM (dist > light->intensity)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -i)	// PGM (dist < -light->intensity)
	{
		R_MarkLights (light, bit, node->children[1]);
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

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (model_t *model)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount;
	if (!sw_dynamic->value || SW_Fullbright() || !model) return;
	for (i=0, l = r_newrefdef.dlights ; i<r_newrefdef.num_dlights ; i++, l++)
	{
		R_MarkLights ( l, 1<<i, 
			model->nodes + model->firstnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t	pointcolor;
mplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	mplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	float		*scales;
	int			maps;
	float		samp;
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
	if (plane->type < 3)	// axial planes
		mid[plane->type] = plane->dist;

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
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];
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
		if (surf->samples_rgb && SW_TintTable ())
		{	/* BDGN 3.52q: coloured lighting: the model gets the colour of the light under it */
			int size = ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1);
			byte *rgb = surf->samples_rgb + (dt * ((surf->extents[0]>>4)+1) + ds) * 3;
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++, rgb += size*3)
			{
				scales = r_newrefdef.lightstyles[surf->styles[maps]].rgb;
				pointcolor[0] += rgb[0] * (1.0f/255) * scales[0];
				pointcolor[1] += rgb[1] * (1.0f/255) * scales[1];
				pointcolor[2] += rgb[2] * (1.0f/255) * scales[2];
			}
			return 1;
		}
		if (lightmap)
		{
			lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				samp = *lightmap * /* 0.5 * */ (1.0/255);	// adjust for gl scale
				scales = r_newrefdef.lightstyles[surf->styles[maps]].rgb;
				VectorMA (pointcolor, samp, scales, pointcolor);
				lightmap += ((surf->extents[0]>>4)+1) *
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
void R_LightPoint (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel || !r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//
	// add dynamic lights
	//
	light = 0;
	for (lnum=0 ; sw_dynamic->value && lnum<r_newrefdef.num_dlights ; lnum++)
	{
		dl = &r_newrefdef.dlights[lnum];
		VectorSubtract (p,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0/256);
		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}
}

//===================================================================


unsigned		blocklights[1024];	// allow some very large lightmaps

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (void)
{
	msurface_t *surf;
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	int			negativeLight;	//PGM

	surf = r_drawsurf.surf;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		rad = dl->intensity;

//=====
//PGM
		negativeLight = 0;
		if(rad < 0)
		{
			negativeLight = 1;
			rad = -rad;
		}
//PGM
//=====

		dist = DotProduct (dl->origin, surf->plane->normal) -
				surf->plane->dist;
		rad -= fabs(dist);
		minlight = 32;		// dl->minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dl->origin[i] -
					surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		
		for (t = 0 ; t<tmax ; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
//====
//PGM
				if(!negativeLight)
				{
					if (dist < minlight)
						blocklights[t*smax + s] += (rad - dist)*256;
				}
				else
				{
					if (dist < minlight)
						blocklights[t*smax + s] -= (rad - dist)*256;
					if(blocklights[t*smax + s] < minlight)
						blocklights[t*smax + s] = minlight;
				}
//PGM
//====
			}
		}
	}
}

/*
===============
BDGN 3.52p: coloured lighting for the software renderer (sw_coloredlight 1), so the colours come close to OpenGL.
id's ref_soft lights with the brightest of the three lightmap colours only (Mod_LoadLighting). After a surface has
been lit into the surface cache as usual, this pass gives every texel the colour of its light: the 24-bit lightmap
(+ coloured dynamic lights) is turned into a tint per lightmap sample (colour / brightest component, so the
brightness stays the one ref_soft already used), the tint is interpolated like the light, and the texel's RGB times
the tint is mapped back into the palette through a 32 KB 15-bit table (SW_TintTable, with a 2x2 dither against
banding). Texels under white light and fullbright colours are left alone. It runs only when a surface is (re)built
into the cache, not per frame; thread-safe (used by the sw_smp worker too).
===============
*/
#define TINT_MAX_SAMPLES	1024
void R_TintSurface (msurface_t *surf, int mip, byte *dest, int rowbytes, int width, int height, const int *lightadj)
{
	int			smax, tmax, size, maps, i, j, x, y, shift, bs, m, c, r, g, b, fy, fx, d;
	int			acc[TINT_MAX_SAMPLES*3];
	byte		tint[TINT_MAX_SAMPLES*3];
	int			row[64*3+3];
	const byte	*lm, *table, *fb;
	byte		*p;
	static const int dither[2][2] = { { 0, 4 }, { 6, 2 } };

	table = SW_TintTable ();
	fb = SW_FullbrightTable ();
	if (!table || !surf->samples_rgb || !surf->texinfo)
		return;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	if (size > TINT_MAX_SAMPLES || smax > 64)
		return;
	memset (acc, 0, size*3*sizeof(int));
	lm = surf->samples_rgb;
	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++, lm += size*3)
		for (i=0 ; i<size*3 ; i++)
			acc[i] += lm[i] * lightadj[maps];

	/* coloured dynamic lights (same distances as R_AddDynamicLights, positive lights only) */
	if (sw_dynamic->value && surf->dlightframe == r_framecount)
	{
		int lnum, sd, td, s, t;
		float dist, rad, minlight, add;
		vec3_t impact, local;
		mtexinfo_t *tex = surf->texinfo;
		dlight_t *dl;
		for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
		{
			if (!(surf->dlightbits & (1<<lnum)))
				continue;
			dl = &r_newrefdef.dlights[lnum];
			rad = dl->intensity;
			if (rad <= 0)
				continue;
			dist = DotProduct (dl->origin, surf->plane->normal) - surf->plane->dist;
			rad -= fabs(dist);
			minlight = 32;
			if (rad < minlight)
				continue;
			minlight = rad - minlight;
			for (i=0 ; i<3 ; i++)
				impact[i] = dl->origin[i] - surf->plane->normal[i]*dist;
			local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
			local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];
			for (t = 0 ; t<tmax ; t++)
			{
				td = (int)local[1] - t*16;
				if (td < 0) td = -td;
				for (s=0 ; s<smax ; s++)
				{
					sd = (int)local[0] - s*16;
					if (sd < 0) sd = -sd;
					dist = sd > td ? sd + (td>>1) : td + (sd>>1);
					if (dist < minlight)
					{
						add = (rad - dist)*256;
						acc[(t*smax+s)*3+0] += (int)(add * dl->color[0]);
						acc[(t*smax+s)*3+1] += (int)(add * dl->color[1]);
						acc[(t*smax+s)*3+2] += (int)(add * dl->color[2]);
					}
				}
			}
		}
	}

	/* tint = colour / brightest component (255 = full) */
	m = 0;
	for (i=0 ; i<size ; i++)
	{
		r = acc[i*3]; g = acc[i*3+1]; b = acc[i*3+2];
		c = r > g ? r : g; if (b > c) c = b;
		if (c <= 0)
			tint[i*3] = tint[i*3+1] = tint[i*3+2] = 255;
		else
		{
			tint[i*3] = (byte)((r > 0 ? r : 0) * 255 / c);
			tint[i*3+1] = (byte)((g > 0 ? g : 0) * 255 / c);
			tint[i*3+2] = (byte)((b > 0 ? b : 0) * 255 / c);
		}
		if (tint[i*3] < 250 || tint[i*3+1] < 250 || tint[i*3+2] < 250)
			m = 1;
	}
	if (!m)
		return;		/* white light everywhere: the surface stays as it is */

	shift = 4 - mip;
	bs = 1 << shift;
	for (y=0 ; y<height ; y++)
	{
		j = y >> shift;
		fy = (y & (bs-1)) << (8 - shift);
		for (i=0 ; i<smax ; i++)
		{	/* vertical interpolation of this row of samples */
			const byte *t0 = &tint[(j*smax+i)*3];
			const byte *t1 = j+1 < tmax ? t0 + smax*3 : t0;
			row[i*3]   = (t0[0]*(256-fy) + t1[0]*fy) >> 8;
			row[i*3+1] = (t0[1]*(256-fy) + t1[1]*fy) >> 8;
			row[i*3+2] = (t0[2]*(256-fy) + t1[2]*fy) >> 8;
		}
		row[smax*3] = row[smax*3-3]; row[smax*3+1] = row[smax*3-2]; row[smax*3+2] = row[smax*3-1];
		p = dest + y*rowbytes;
		for (x=0 ; x<width ; x++)
		{
			int tr, tg, tb, k = (x >> shift)*3;
			fx = (x & (bs-1)) << (8 - shift);
			tr = (row[k]*(256-fx) + row[k+3]*fx) >> 8;
			tg = (row[k+1]*(256-fx) + row[k+4]*fx) >> 8;
			tb = (row[k+2]*(256-fx) + row[k+5]*fx) >> 8;
			c = p[x];
			if (fb[c] || (tr >= 250 && tg >= 250 && tb >= 250))
				continue;
			{
				const byte *rgb = (const byte *)&d_8to24table[c];
				d = dither[y&1][x&1];
				r = (rgb[0]*tr >> 8) + d; if (r > 255) r = 255;
				g = (rgb[1]*tg >> 8) + d; if (g > 255) g = 255;
				b = (rgb[2]*tb >> 8) + d; if (b > 255) b = 255;
				p[x] = table[((r>>3)<<10) | ((g>>3)<<5) | (b>>3)];
			}
		}
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap (void)
{
	int			smax, tmax;
	int			i, size;
	byte		*lightmap;
	unsigned	scale;
	float modulate=SW_LightScale();
	int			maps;
	msurface_t	*surf;

	surf = r_drawsurf.surf;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;

	if (SW_Fullbright() || !r_worldmodel->lightdata)
	{
		for (i=0 ; i<size ; i++)
			blocklights[i] = 0;
		return;
	}

// clear to no light
	for (i=0 ; i<size ; i++)
		blocklights[i] = 0;


	if (SW_OldLightMode () == 2)
	{	/* BDGN 3.57 Blake Stone shading: the light maps are not read at all */
		SW_OldLight (surf, blocklights, size);
		SW_LightmapFinish (blocklights, size, modulate);
		return;
	}

// add all the lightmaps
	lightmap = surf->samples;
	if (lightmap)
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			scale = r_drawsurf.lightadj[maps];	// 8.8 fraction		
			for (i=0 ; i<size ; i++)
				blocklights[i] += lightmap[i] * scale;
			lightmap += size;	// skip to next lightmap
		}

// BDGN 3.57: Doom light / Blake Stone shading: one light level for the whole surface, no dynamic lights
	if (SW_OldLightMode ())
		SW_OldLight (surf, blocklights, size);
	else
// add all the dynamic lights
	if (sw_dynamic->value && surf->dlightframe == r_framecount)
		R_AddDynamicLights ();

// bound, invert, and shift (BDGN 3.56c: no float per sample at gl_modulate 1, sw_fastpath.h)
	SW_LightmapFinish (blocklights, size, modulate);
}

