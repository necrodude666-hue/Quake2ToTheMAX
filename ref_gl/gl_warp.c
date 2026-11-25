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
// gl_warp.c -- sky and water polygons

#include "gl_local.h"

extern	model_t	*loadmodel;

char	skyname[MAX_QPATH];
float	skyrotate;
vec3_t	skyaxis;
image_t	*sky_images[6];

msurface_t	*warpface;

#define	SUBDIVIDE_SIZE	64
//#define	SUBDIVIDE_SIZE	1024

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;
	vec3_t	total;
	float	total_s, total_t;

	if (numverts > 60)
		ri.Sys_Error (ERR_DROP, "numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = SUBDIVIDE_SIZE * floor (m/SUBDIVIDE_SIZE + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	// add a point in the center to help keep warp valid
	poly = Hunk_Alloc (sizeof(glpoly_t) + ((numverts-4)+2) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts+2;
	VectorClear (total);
	total_s = 0;
	total_t = 0;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i+1]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		VectorAdd (total, verts, total);

		poly->verts[i+1][3] = s;
		poly->verts[i+1][4] = t;
	}

	VectorScale (total, (1.0/numverts), poly->verts[0]);
	poly->verts[0][3] = total_s/numverts;
	poly->verts[0][4] = total_t/numverts;

	// copy first vertex to last
	memcpy (poly->verts[i+1], poly->verts[1], sizeof(poly->verts[0]));
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
float	r_turbsin[] =
{
	#include "warpsin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
static void EmitWaterPolysBase (msurface_t *fa);

/* BDGN 3.55o: water, then its reflection */
void EmitWaterPolys (msurface_t *fa)
{
	if (!GL_DrawModernWater(fa))
		EmitWaterPolysBase (fa);
	GL_DistortNoteLava(fa);
	GL_ReflectNoteWater (fa);
	GL_ReflectDrawWater (fa);
}

static void EmitWaterPolysBase (msurface_t *fa)
{
	glpoly_t	*p, *bp;
	float		*v;
	int			i;
	float		s, t, os, ot;
	float		scroll;
	float		rdt = r_map_animation_time;

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64 * ( (r_map_animation_time*0.5) - (int)(r_map_animation_time*0.5) );
	else
		scroll = 0;
	if ( gl_fastwater->value || r_staticmap->value )
	{	// BDGN 3.38 lab: no waves, the texture only scrolls (flowing water) - no sine lookups per vertex
		for (bp=fa->polys ; bp ; bp=bp->next)
		{
			qglBegin (GL_TRIANGLE_FAN);
			for (i=0,v=bp->verts[0] ; i<bp->numverts ; i++, v+=VERTEXSIZE)
			{
				qglTexCoord2f ((v[3] + scroll) * (1.0f/64), v[4] * (1.0f/64));
				qglVertex3fv (v);
			}
			qglEnd ();
		}
		return;
	}

	for (bp=fa->polys ; bp ; bp=bp->next)
	{
		p = bp;

		if ( p->numverts <= 256 && GL_3DNOW() )
		{
			static float	st[256*2];

			Q3D_WaterST( st, p->verts[0], p->numverts, VERTEXSIZE, rdt, scroll, r_turbsin );
			qglBegin (GL_TRIANGLE_FAN);
			for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
			{
				qglTexCoord2f (st[i*2], st[i*2+1]);
				qglVertex3fv (v);
			}
			qglEnd ();
			continue;
		}

		qglBegin (GL_TRIANGLE_FAN);
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			os = v[3];
			ot = v[4];

#if !id386
			s = os + r_turbsin[(int)((ot*0.125+r_map_animation_time) * TURBSCALE) & 255];
#else
			s = os + r_turbsin[Q_ftol( ((ot*0.125+rdt) * TURBSCALE) ) & 255];
#endif
			s += scroll;
			s *= (1.0/64);

#if !id386
			t = ot + r_turbsin[(int)((os*0.125+rdt) * TURBSCALE) & 255];
#else
			t = ot + r_turbsin[Q_ftol( ((os*0.125+rdt) * TURBSCALE) ) & 255];
#endif
			t *= (1.0/64);

			qglTexCoord2f (s, t);
			qglVertex3fv (v);
		}
		qglEnd ();
	}
}


//===================================================================


vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};
int	c_sky;

// 1 = s, 2 = t, 3 = 2048
int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down

//	{-1,2,3},
//	{1,2,-3}
};

// s = [0]/[2], t = [1]/[2]
int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}

//	{-1,2,3},
//	{1,2,-3}
};

float	skymins[2][6], skymaxs[2][6];
float	sky_min, sky_max;

void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	c_sky++;
#if 0
glBegin (GL_POLYGON);
for (i=0 ; i<nump ; i++, vecs+=3)
{
	VectorAdd(vecs, r_origin, v);
	qglVertex3fv (v);
}
glEnd();
return;
#endif
	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64
void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		ri.Sys_Error (ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	if ( q3d_use_3dnow && gl_amd_sky->value )
	{	// AMD 3DNow! patch (area world W2): the whole clip step in one kernel
		if ( !Q3N_SkyClip( vecs, nump, skyclip[stage], ON_EPSILON, newv[0][0], &newc[0], newv[1][0], &newc[1] ) )
		{
			ClipSkyPolygon (nump, vecs, stage+1);
			return;
		}
		ClipSkyPolygon (newc[0], newv[0][0], stage+1);
		ClipSkyPolygon (newc[1], newv[1][0], stage+1);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}

/*
=================
R_AddSkySurface
=================
*/
void R_AddSkySurface (msurface_t *fa)
{
	int			i;
	vec3_t		verts[MAX_CLIP_VERTS];
	glpoly_t	*p;

	// calculate vertex values for sky box
	for (p=fa->polys ; p ; p=p->next)
	{
		for (i=0 ; i<p->numverts ; i++)
		{
			VectorSubtract (p->verts[i], r_origin, verts[i]);
		}
		ClipSkyPolygon (p->numverts, verts[0], 0);
	}
}


/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox (void)
{
	int		i;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}
}


void MakeSkyVec (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s*2300;
	b[1] = t*2300;
	b[2] = 2300;

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}

	// avoid bilerp seam
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	if (s < sky_min)
		s = sky_min;
	else if (s > sky_max)
		s = sky_max;
	if (t < sky_min)
		t = sky_min;
	else if (t > sky_max)
		t = sky_max;

	t = 1.0 - t;
	qglTexCoord2f (s, t);
	qglVertex3fv (v);
}

/*
==============
R_DrawSkyBox
==============
*/
int	skytexorder[6] = {0,2,1,3,4,5};
static float	sky_avg[3] = { 0.45f, 0.55f, 0.75f };	// BDGN 3.38 lab: average colour of the sky textures
void R_DrawSkyBox (void)
{
	int		i;

#if 0
qglEnable (GL_BLEND);
GL_TexEnv( GL_MODULATE );
qglColor4f (1,1,1,0.5);
qglDisable (GL_DEPTH_TEST);
#endif
	if (skyrotate)
	{	// check for no sky at all
		for (i=0 ; i<6 ; i++)
			if (skymins[0][i] < skymaxs[0][i]
			&& skymins[1][i] < skymaxs[1][i])
				break;
		if (i == 6)
			return;		// nothing visible
	}

qglPushMatrix ();
qglTranslatef (r_origin[0], r_origin[1], r_origin[2]);
qglRotatef (r_map_animation_time * skyrotate, skyaxis[0], skyaxis[1], skyaxis[2]);

	if ( gl_fastsky->value )
	{	// BDGN 3.38 lab: the sky sides in the average colour of the sky textures, no texture lookups
		qglDisable (GL_TEXTURE_2D);
		qglColor3fv (sky_avg);
		for (i=0 ; i<6 ; i++)
		{
			if (skyrotate)
			{
				skymins[0][i] = -1;
				skymins[1][i] = -1;
				skymaxs[0][i] = 1;
				skymaxs[1][i] = 1;
			}
			if (skymins[0][i] >= skymaxs[0][i]
			|| skymins[1][i] >= skymaxs[1][i])
				continue;
			qglBegin (GL_QUADS);
			MakeSkyVec (skymins[0][i], skymins[1][i], i);
			MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
			MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
			MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
			qglEnd ();
		}
		qglEnable (GL_TEXTURE_2D);
		qglColor4f (1,1,1,1);
		qglPopMatrix ();
		return;
	}

	for (i=0 ; i<6 ; i++)
	{
		if (skyrotate)
		{	// hack, forces full sky to draw when rotating
			skymins[0][i] = -1;
			skymins[1][i] = -1;
			skymaxs[0][i] = 1;
			skymaxs[1][i] = 1;
		}

		if (skymins[0][i] >= skymaxs[0][i]
		|| skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_Bind (sky_images[skytexorder[i]]->texnum);

		qglBegin (GL_QUADS);
		MakeSkyVec (skymins[0][i], skymins[1][i], i);
		MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
		qglEnd ();
	}
qglPopMatrix ();
#if 0
glDisable (GL_BLEND);
glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
glColor4f (1,1,1,0.5);
glEnable (GL_DEPTH_TEST);
#endif
}


/*
============
R_SetSky
============
*/
/*
============
R_SkyAverage

BDGN 3.38 lab: average colour of the six sky images (read back from the card, every 4th pixel)
for gl_fastsky.  Keeps the default blue-grey when the card can't read textures back.
============
*/
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#endif

void R_SkyFlatColor (byte *rgb)
{
	float c[3];
	int i;

	for ( i = 0 ; i < 3 ; i++ )
		c[i] = sky_avg[i] * 255.0f;
	GL_FlatLift (c);
	for ( i = 0 ; i < 3 ; i++ )
		rgb[i] = (byte)(c[i] + 0.5f);
}

static void R_SkyAverage (void)
{
	int		i, x, y, w, h;
	float	sum[3];
	int		n = 0;
	byte	*buf;

	sum[0] = sum[1] = sum[2] = 0;
	if ( !qglGetTexImage || !qglGetTexLevelParameteriv )
		return;

	for (i=0 ; i<6 ; i++)
	{
		w = h = 0;
		if ( sky_images[i] == r_notexture )
			continue;
		GL_Bind (sky_images[i]->texnum);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		qglGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
		if ( w < 4 || h < 4 || w > 1024 || h > 1024 )
			continue;
		buf = malloc (w*h*3);
		if ( !buf )
			continue;
		qglGetTexImage (GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
		for (y=0 ; y<h ; y+=4)
			for (x=0 ; x<w ; x+=4)
			{
				sum[0] += buf[(y*w+x)*3+0];
				sum[1] += buf[(y*w+x)*3+1];
				sum[2] += buf[(y*w+x)*3+2];
				n++;
			}
		free (buf);
	}
	if ( n )
	{
		sky_avg[0] = sum[0] / (n*255.0f);
		sky_avg[1] = sum[1] / (n*255.0f);
		sky_avg[2] = sum[2] / (n*255.0f);
	}
}

// 3dstudio environment map names
char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void R_SetSky (char *name, float rotate, vec3_t axis)
{
	int		i,j;
 image_t *previous[6];
	char	pathname[MAX_QPATH];
 memcpy(previous,sky_images,sizeof(previous));

	strncpy (skyname, name, sizeof(skyname)-1);
	skyrotate = rotate;
	VectorCopy (axis, skyaxis);

	for (i=0 ; i<6 ; i++)
	{
		// chop down rotating skies for less memory
		if (gl_skymip->value || skyrotate)
			gl_picmip->value++;

		if ( qglColorTableEXT && gl_ext_palettedtexture->value && strncmp(skyname,"q2ttm_skies/",12) )
			Com_sprintf (pathname, sizeof(pathname), "env/%s%s.pcx", skyname, suf[i]);
		else
			Com_sprintf (pathname, sizeof(pathname), "env/%s%s.tga", skyname, suf[i]);
		
		// doogies code
//		sky_images[i] = GL_FindImage (pathname, it_sky);
		if (gl_hack_noimages->value)    // Q2TTM: include the skybox when applying (lack of) images
			sky_images[i] = r_notexture;
		else
			sky_images[i] = GL_FindImage (pathname, it_sky);
		// end
		if (!sky_images[i])
			sky_images[i] = r_notexture;

		if (gl_skymip->value || skyrotate)
		{	// take less memory
			gl_picmip->value--;
			sky_min = 1.0/256;
			sky_max = 255.0/256;
		}
		else	
		{
			sky_min = 1.0/512;
			sky_max = 511.0/512;
		}
	}
	{	/* BDGN 3.55al: the lines where the sides of the sky meet. The 1/512 above is half a texel of a 256x256 picture;
		   of a 2048x2048 HD sky it cuts 4 texels off every edge, so the sides no longer continue into each other.
		   With edge clamping (OpenGL 1.2 or the extension) the whole picture is used and the filter cannot reach
		   across the edge; without it the cut is half a texel of the picture as loaded. */
		const char *ext = gl_config.extensions_string ? gl_config.extensions_string : "";
		const char *ver = gl_config.version_string ? gl_config.version_string : "";
		int edge = strstr (ext, "GL_EXT_texture_edge_clamp") || strstr (ext, "GL_SGIS_texture_edge_clamp") ||
			(ver[0] >= '1' && ver[0] <= '9' && ver[1] == '.' && (ver[0] > '1' || (ver[2] >= '2' && ver[2] <= '9')));
		int size = 256;
		for (i=0 ; i<6 ; i++)
		{
			image_t *im = sky_images[i];
			if (!im || im == r_notexture || gl_hack_noimages->value)
				continue;
			if (im->upload_width > 0 && (i == 0 || im->upload_width < size))
				size = im->upload_width;
			if (edge)
			{
				GL_Bind (im->texnum);
				qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);	/* GL_CLAMP_TO_EDGE */
				qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
			}
		}
		if (edge)
		{
			sky_min = 0;
			sky_max = 1;
		}
		else if (size > 0)
		{
			sky_min = 0.5f / size;
			sky_max = 1 - sky_min;
		}
	}

 for(i=0;i<6;i++) {
  for(j=0;j<6;j++) if(previous[i]==sky_images[j]) break;
  if(j==6) GL_ReleaseOptionalSkyImage(previous[i]);
 }
	R_SkyAverage ();
	R_SunSetSky (skyname);	/* BDGN 3.55ak: find the sun in the sky pictures */
}
