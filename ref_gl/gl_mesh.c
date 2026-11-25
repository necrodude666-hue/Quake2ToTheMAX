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
// gl_mesh.c: triangle model functions

#include "gl_local.h"
#include "weapon_colors.h"
#define BDGN_DUKE_GL
#include "../qcommon/duke_playback.h"
#include "gl_weapsprite.h"	/* BDGN 3.51q: 2D first-person weapon */

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

#include "gl_smp.h"

typedef float vec4_t[4];

static	vec4_t	s_lerped[MAX_VERTS];
static	float	s_colors[MAX_VERTS*3];	// 3DNow!: per-vertex light colour
//static	vec3_t	lerped[MAX_VERTS];

vec3_t	shadevector;
float	shadelight[3];

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anormtab.h"
;

float	*shadedots = r_avertexnormal_dots[0];

// BDGN 3.37: gl_flatmodels = every vertex gets the same light factor (no per-vertex normal shading)
static float	s_flatdots[256];
static int		s_flatdots_init;

static float *GL_FlatDots (void)
{
	if ( !s_flatdots_init )
	{
		int i;

		for ( i = 0 ; i < 256 ; i++ )
			s_flatdots[i] = 1.0f;
		s_flatdots_init = 1;
	}
	return s_flatdots;
}

void GL_LerpVerts( int nverts, dtrivertx_t *v, dtrivertx_t *ov, dtrivertx_t *verts, float *lerp, float move[3], float frontv[3], float backv[3] )
{
	int i;

	//PMM -- added RF_SHELL_DOUBLE, RF_SHELL_HALF_DAM
	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) )
	{
		for (i=0 ; i < nverts; i++, v++, ov++, lerp+=4 )
		{
			float *normal = r_avertexnormals[verts[i].lightnormalindex];

			lerp[0] = move[0] + ov->v[0]*backv[0] + v->v[0]*frontv[0] + normal[0] * POWERSUIT_SCALE;
			lerp[1] = move[1] + ov->v[1]*backv[1] + v->v[1]*frontv[1] + normal[1] * POWERSUIT_SCALE;
			lerp[2] = move[2] + ov->v[2]*backv[2] + v->v[2]*frontv[2] + normal[2] * POWERSUIT_SCALE; 
		}
	}
	else
	{
		if ( GL_3DNOW() )
		{
			Q3D_LerpVerts( nverts, (const unsigned char *)v, (const unsigned char *)ov, lerp, move, frontv, backv );
		}
		else
		{
			for (i=0 ; i < nverts; i++, v++, ov++, lerp+=4)
			{
				lerp[0] = move[0] + ov->v[0]*backv[0] + v->v[0]*frontv[0];
				lerp[1] = move[1] + ov->v[1]*backv[1] + v->v[1]*frontv[1];
				lerp[2] = move[2] + ov->v[2]*backv[2] + v->v[2]*frontv[2];
			}
		}
	}

}

/*
=============================================================================
BDGN 3.32: batched alias model drawing (gl_fastdraw)

The glcmds strips/fans are converted once per model into one triangle list over an
"expanded" vertex list (one entry per unique xyz index + s,t pair).  A frame is then
drawn with a single glDrawElements call instead of three GL calls per vertex.
=============================================================================
*/
#define FM_MAXEXP	4096

typedef struct
{
	int				nexp;		// expanded vertices; -1 = model can not use the batched path
	int				nidx;
	int				*xyz;		// source vertex of each expanded vertex
	float			*st;		// 2 floats per expanded vertex
	unsigned short	*idx;		// triangle list (gl_fastdraw 1)
	int				nprim;		// strips / fans (gl_fastdraw 2 and 3)
	int				*pcount;	// vertex count, negative = fan
	int				*pfirst;	// first entry in sidx
	unsigned short	*sidx;		// expanded vertex indices of all strips/fans, one after the other
} fastmesh_t;

static	float	s_fx_lerp[FM_MAXEXP*4];
static	float	s_fx_col[FM_MAXEXP*3];
static	unsigned char	s_fx_colb[FM_MAXEXP*4];

void GL_FreeFastMesh (model_t *mod)
{
	fastmesh_t *fm = (fastmesh_t *)mod->fastmesh;

	if ( !fm )
		return;
	if ( fm->xyz )	free( fm->xyz );
	if ( fm->st )	free( fm->st );
	if ( fm->idx )	free( fm->idx );
	if ( fm->pcount )	free( fm->pcount );
	if ( fm->pfirst )	free( fm->pfirst );
	if ( fm->sidx )	free( fm->sidx );
	free( fm );
	mod->fastmesh = NULL;
}

static fastmesh_t *GL_BuildFastMesh (dmdl_t *hdr)
{
	fastmesh_t	*fm;
	int			*order, *o;
	int			count, i, k, e;
	int			total, ntri, nexp, nidx;
	int			*head, *next, *prim;
	int			nprim, nsidx;
	int			ok = 0;

	fm = (fastmesh_t *)malloc( sizeof(*fm) );
	if ( !fm )
		return NULL;
	memset( fm, 0, sizeof(*fm) );
	fm->nexp = -1;

	order = (int *)((byte *)hdr + hdr->ofs_glcmds);

	// pass 1: count vertices and triangles
	total = 0;
	ntri = 0;
	for ( o = order ; ; )
	{
		count = *o++;
		if ( !count )
			break;
		if ( count < 0 )
			count = -count;
		total += count;
		if ( count >= 3 )
			ntri += count - 2;
		o += count * 3;
	}

	if ( total <= 0 || ntri <= 0 || hdr->num_xyz <= 0 || hdr->num_xyz > MAX_VERTS )
		return fm;

	fm->xyz = (int *)malloc( total * sizeof(int) );
	fm->st = (float *)malloc( total * 2 * sizeof(float) );
	fm->idx = (unsigned short *)malloc( ntri * 3 * sizeof(unsigned short) );
	head = (int *)malloc( hdr->num_xyz * sizeof(int) );
	next = (int *)malloc( total * sizeof(int) );
	prim = (int *)malloc( total * sizeof(int) );
	fm->pcount = (int *)malloc( total * sizeof(int) );
	fm->pfirst = (int *)malloc( total * sizeof(int) );
	fm->sidx = (unsigned short *)malloc( total * sizeof(unsigned short) );

	if ( fm->xyz && fm->st && fm->idx && head && next && prim && fm->pcount && fm->pfirst && fm->sidx )
	{
		for ( i = 0 ; i < hdr->num_xyz ; i++ )
			head[i] = -1;

		nexp = 0;
		nidx = 0;
		nprim = 0;
		nsidx = 0;
		ok = 1;

		// pass 2: expanded vertices and triangle list
		for ( o = order ; ok ; )
		{
			int fan;

			count = *o++;
			if ( !count )
				break;
			fan = ( count < 0 );
			if ( fan )
				count = -count;

			for ( i = 0 ; i < count ; i++, o += 3 )
			{
				float s = ((float *)o)[0], t = ((float *)o)[1];
				int x = o[2];

				if ( x < 0 || x >= hdr->num_xyz )
				{
					ok = 0;
					break;
				}
				for ( e = head[x] ; e >= 0 ; e = next[e] )
					if ( fm->st[e*2] == s && fm->st[e*2+1] == t )
						break;
				if ( e < 0 )
				{
					if ( nexp >= FM_MAXEXP )
					{
						ok = 0;
						break;
					}
					e = nexp++;
					fm->xyz[e] = x;
					fm->st[e*2] = s;
					fm->st[e*2+1] = t;
					next[e] = head[x];
					head[x] = e;
				}
				prim[i] = e;
			}
			if ( !ok )
				break;

			fm->pcount[nprim] = fan ? -count : count;
			fm->pfirst[nprim] = nsidx;
			nprim++;
			for ( i = 0 ; i < count ; i++ )
				fm->sidx[nsidx++] = (unsigned short)prim[i];

			for ( k = 0 ; k + 2 < count ; k++ )
			{
				if ( fan )
				{
					fm->idx[nidx++] = (unsigned short)prim[0];
					fm->idx[nidx++] = (unsigned short)prim[k+1];
					fm->idx[nidx++] = (unsigned short)prim[k+2];
				}
				else if ( k & 1 )
				{
					fm->idx[nidx++] = (unsigned short)prim[k+1];
					fm->idx[nidx++] = (unsigned short)prim[k];
					fm->idx[nidx++] = (unsigned short)prim[k+2];
				}
				else
				{
					fm->idx[nidx++] = (unsigned short)prim[k];
					fm->idx[nidx++] = (unsigned short)prim[k+1];
					fm->idx[nidx++] = (unsigned short)prim[k+2];
				}
			}
		}

		if ( ok )
		{
			fm->nexp = nexp;
			fm->nidx = nidx;
			fm->nprim = nprim;
		}
	}

	if ( head )	free( head );
	if ( next )	free( next );
	if ( prim )	free( prim );

	if ( !ok )
	{
		// unusable: keep only the marker (nexp = -1)
		if ( fm->xyz )	free( fm->xyz );
		if ( fm->st )	free( fm->st );
		if ( fm->idx )	free( fm->idx );
		if ( fm->pcount )	free( fm->pcount );
		if ( fm->pfirst )	free( fm->pfirst );
		if ( fm->sidx )	free( fm->sidx );
		fm->xyz = NULL;
		fm->st = NULL;
		fm->idx = NULL;
		fm->pcount = NULL;
		fm->pfirst = NULL;
		fm->sidx = NULL;
		fm->nexp = -1;
	}
	return fm;
}

/*
Draws the current frame with the batched path.  Returns 0 when the model can not use it
(the caller then uses the normal path).
*/
static int GL_DrawAliasFast (dmdl_t *paliashdr, dtrivertx_t *v, dtrivertx_t *ov, float move[3], float frontv[3], float backv[3])
{
	fastmesh_t	*fm = (fastmesh_t *)currentmodel->fastmesh;
	int			i;
	int			mode = GL_FASTDRAW();	// 1 = triangle list, 2 = strips/fans, 3 = strips/fans + byte colours
	const smp_pose_t *pose=SMP_Pose(currententity,paliashdr,currententity->backlerp);
	int			flat = ( shadedots == s_flatdots );	// gl_flatmodels: one colour for the whole model, no colour array

	if ( !fm )
	{
		fm = GL_BuildFastMesh( paliashdr );
		currentmodel->fastmesh = fm;
	}
	if ( !fm || fm->nexp <= 0 )
		return 0;

	if ( GL_3DNOW() )
	{
		if(!pose) Q3D_LerpVertsIdx( fm->nexp, fm->xyz, (const unsigned char *)v, (const unsigned char *)ov, s_fx_lerp, move, frontv, backv );
  else for(i=0;i<fm->nexp;i++) memcpy(s_fx_lerp+i*4,pose->xyz[fm->xyz[i]],4*sizeof(float));
		if ( flat )
			;
		else if ( mode >= 3 )
			Q3D_ColorArrayIdxB( s_fx_colb, fm->xyz, (const unsigned char *)v, shadedots, shadelight, fm->nexp );
		else
			Q3D_ColorArrayIdx( s_fx_col, fm->xyz, (const unsigned char *)v, shadedots, shadelight, fm->nexp );
	}
	else
	{
		for ( i = 0 ; i < fm->nexp ; i++ )
		{
			int x = fm->xyz[i];
			float l = shadedots[v[x].lightnormalindex];

   if(pose) memcpy(s_fx_lerp+i*4,pose->xyz[x],4*sizeof(float));
   else {
			s_fx_lerp[i*4+0] = move[0] + ov[x].v[0]*backv[0] + v[x].v[0]*frontv[0];
			s_fx_lerp[i*4+1] = move[1] + ov[x].v[1]*backv[1] + v[x].v[1]*frontv[1];
			s_fx_lerp[i*4+2] = move[2] + ov[x].v[2]*backv[2] + v[x].v[2]*frontv[2];
			s_fx_lerp[i*4+3] = 0;
   }
			if ( flat )
			{
			}
			else if ( mode >= 3 )
			{
				int k;

				for ( k = 0 ; k < 3 ; k++ )
				{
					int cb = (int)( l * shadelight[k] * 255.0f );

					s_fx_colb[i*4+k] = (unsigned char)( cb < 0 ? 0 : ( cb > 255 ? 255 : cb ) );
				}
				s_fx_colb[i*4+3] = 255;
			}
			else
			{
				s_fx_col[i*3+0] = l * shadelight[0];
				s_fx_col[i*3+1] = l * shadelight[1];
				s_fx_col[i*3+2] = l * shadelight[2];
			}
		}
	}

	GL_SelectTexture( GL_TEXTURE0 );

	qglEnableClientState( GL_VERTEX_ARRAY );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, 16, s_fx_lerp );
	if ( flat )
	{
		qglColor4f( shadelight[0], shadelight[1], shadelight[2], 1.0f );
	}
	else
	{
		qglEnableClientState( GL_COLOR_ARRAY );
		if ( mode >= 3 )
			qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, s_fx_colb );
		else
			qglColorPointer( 3, GL_FLOAT, 0, s_fx_col );
	}
	qglTexCoordPointer( 2, GL_FLOAT, 0, fm->st );

	if ( qglLockArraysEXT != 0 )
		qglLockArraysEXT( 0, fm->nexp );

	if ( mode >= 2 )
	{
		for ( i = 0 ; i < fm->nprim ; i++ )
		{
			int cnt = fm->pcount[i];

			if ( cnt < 0 )
				qglDrawElements( GL_TRIANGLE_FAN, -cnt, GL_UNSIGNED_SHORT, fm->sidx + fm->pfirst[i] );
			else
				qglDrawElements( GL_TRIANGLE_STRIP, cnt, GL_UNSIGNED_SHORT, fm->sidx + fm->pfirst[i] );
		}
	}
	else
		qglDrawElements( GL_TRIANGLES, fm->nidx, GL_UNSIGNED_SHORT, fm->idx );

	if ( qglUnlockArraysEXT != 0 )
		qglUnlockArraysEXT();

	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	if ( !flat )
		qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_VERTEX_ARRAY );
	return 1;
}

/*
=============
GL_DrawAliasFrameLerp

interpolates between two frames and origins
FIXME: batch lerp all vertexes
=============
*/
void GL_DrawAliasFrameLerp (dmdl_t *paliashdr, float backlerp)
{
	float 	l;
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov, *verts;
	int		*order;
	int		count;
	float	frontlerp;
	float	alpha;
	vec3_t	move, delta, vectors[3];
	vec3_t	frontv, backv;
	int		i;
	int		index_xyz;
	float	*lerp;

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = v = frame->verts;

	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->oldframe * paliashdr->framesize);
	ov = oldframe->verts;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

//	glTranslatef (frame->translate[0], frame->translate[1], frame->translate[2]);
//	glScalef (frame->scale[0], frame->scale[1], frame->scale[2]);

	if (currententity->flags & RF_TRANSLUCENT)
		alpha = currententity->alpha;
	else
		alpha = 1.0;

	// PMM - added double shell
	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) )
		qglDisable( GL_TEXTURE_2D );

	frontlerp = 1.0 - backlerp;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	R_AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct (delta, vectors[0]);	// forward
	move[1] = -DotProduct (delta, vectors[1]);	// left
	move[2] = DotProduct (delta, vectors[2]);	// up

	VectorAdd (move, oldframe->translate, move);

	for (i=0 ; i<3 ; i++)
	{
		move[i] = backlerp*move[i] + frontlerp*frame->translate[i];
	}

	for (i=0 ; i<3 ; i++)
	{
		frontv[i] = frontlerp*frame->scale[i];
		backv[i] = backlerp*oldframe->scale[i];
	}

	// BDGN 3.32: batched path (opaque, no powerup shell)
	if ( GL_FASTDRAW() && ( !gl_vaunsafe || gl_fastforce->value ) && alpha >= 1.0f
		&& !( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM ) )
		&& qglDrawElements && qglVertexPointer && qglColorPointer && qglTexCoordPointer
		&& qglEnableClientState && qglDisableClientState )
	{
		if ( GL_DrawAliasFast( paliashdr, v, ov, move, frontv, backv ) )
			return;
	}

	lerp = s_lerped[0];

 { const smp_pose_t *pose=SMP_Pose(currententity,paliashdr,backlerp);
   if(pose) memcpy(s_lerped,pose->xyz,paliashdr->num_xyz*sizeof(s_lerped[0]));
   else GL_LerpVerts(paliashdr->num_xyz,v,ov,verts,lerp,move,frontv,backv);
 }

	if ( gl_vertex_arrays->value && ( !gl_vaunsafe || gl_fastforce->value ) )	/* BDGN 3.52i: not on 3dfx drivers (gl_vaunsafe), like the batched paths */
	{
		static float colorArray[MAX_VERTS*4];	// static: a local this big makes MSVC probe every 4 KB stack page (_chkstk) on each call (AMD patch, area world W1)

		qglEnableClientState( GL_VERTEX_ARRAY );
		qglVertexPointer( 3, GL_FLOAT, 16, s_lerped );	// padded for SIMD

//		if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
		// PMM - added double damage shell
		if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) )
		{
			qglColor4f( shadelight[0], shadelight[1], shadelight[2], alpha );
		}
		else
		{
			qglEnableClientState( GL_COLOR_ARRAY );
			qglColorPointer( 3, GL_FLOAT, 0, colorArray );

			//
			// pre light everything
			//
			if ( GL_3DNOW() )
			{
				Q3D_ColorArray( colorArray, (const unsigned char *)verts, shadedots, shadelight, paliashdr->num_xyz );
			}
			else
			{
				for ( i = 0; i < paliashdr->num_xyz; i++ )
				{
					float l = shadedots[verts[i].lightnormalindex];

					colorArray[i*3+0] = l * shadelight[0];
					colorArray[i*3+1] = l * shadelight[1];
					colorArray[i*3+2] = l * shadelight[2];
				}
			}
		}

		if ( qglLockArraysEXT != 0 )
			qglLockArraysEXT( 0, paliashdr->num_xyz );

		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			if (count < 0)
			{
				count = -count;
				qglBegin (GL_TRIANGLE_FAN);
			}
			else
			{
				qglBegin (GL_TRIANGLE_STRIP);
			}

			// PMM - added double damage shell
			if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) )
			{
				do
				{
					index_xyz = order[2];
					order += 3;

					qglVertex3fv( s_lerped[index_xyz] );

				} while (--count);
			}
			else
			{
				do
				{
					// texture coordinates come from the draw list
					qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
					index_xyz = order[2];

					order += 3;

					// normals and vertexes come from the frame list
//					l = shadedots[verts[index_xyz].lightnormalindex];
					
//					qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
					qglArrayElement( index_xyz );

				} while (--count);
			}
			qglEnd ();
		}

		if ( qglUnlockArraysEXT != 0 )
			qglUnlockArraysEXT();
	}
	else
	{
		int use3d = GL_3DNOW() && !( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) );

		if ( use3d )
			Q3D_ColorArray( s_colors, (const unsigned char *)verts, shadedots, shadelight, paliashdr->num_xyz );

		while (1)
		{
			// get the vertex count and primitive type
			count = *order++;
			if (!count)
				break;		// done
			if (count < 0)
			{
				count = -count;
				qglBegin (GL_TRIANGLE_FAN);
			}
			else
			{
				qglBegin (GL_TRIANGLE_STRIP);
			}

			if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
			{
				do
				{
					index_xyz = order[2];
					order += 3;

					qglColor4f( shadelight[0], shadelight[1], shadelight[2], alpha);
					qglVertex3fv (s_lerped[index_xyz]);

				} while (--count);
			}
			else
			{
				if ( use3d )
				{
					// colours were precomputed once per vertex (shared by many strip vertices)
					do
					{
						const float *c;

						qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
						index_xyz = order[2];
						order += 3;

						c = &s_colors[index_xyz*3];
						qglColor4f (c[0], c[1], c[2], alpha);
						qglVertex3fv (s_lerped[index_xyz]);
					} while (--count);
				}
				else
				do
				{
					// texture coordinates come from the draw list
					qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
					index_xyz = order[2];
					order += 3;

					// normals and vertexes come from the frame list
					l = shadedots[verts[index_xyz].lightnormalindex];
					
					qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
					qglVertex3fv (s_lerped[index_xyz]);
				} while (--count);
			}

			qglEnd ();
		}
	}

//	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
	// PMM - added double damage shell
	if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM) )
		qglEnable( GL_TEXTURE_2D );
}


#if 1
/*
=============
GL_DrawAliasShadow
=============
*/
extern	vec3_t			lightspot;

void GL_DrawAliasShadow (dmdl_t *paliashdr, int posenum)
{
	dtrivertx_t	*verts;
	int		*order;
	vec3_t	point;
	float	height, lheight;
	int		count;
	daliasframe_t	*frame;

	lheight = currententity->origin[2] - lightspot[2];

	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
		+ currententity->frame * paliashdr->framesize);
	verts = frame->verts;

	height = 0;

	order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);

	height = -lheight + 1.0;

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			qglBegin (GL_TRIANGLE_FAN);
		}
		else
			qglBegin (GL_TRIANGLE_STRIP);

		do
		{
			// normals and vertexes come from the frame list
/*
			point[0] = verts[order[2]].v[0] * frame->scale[0] + frame->translate[0];
			point[1] = verts[order[2]].v[1] * frame->scale[1] + frame->translate[1];
			point[2] = verts[order[2]].v[2] * frame->scale[2] + frame->translate[2];
*/

			memcpy( point, s_lerped[order[2]], sizeof( point )  );

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;
//			height -= 0.001;
			qglVertex3fv (point);

			order += 3;

//			verts++;

		} while (--count);

		qglEnd ();
	}	
}

#endif

/*
** R_CullAliasModel
*/
static qboolean R_CullAliasModel( vec3_t bbox[8], entity_t *e )
{
	int i;
	vec3_t		mins, maxs;
	dmdl_t		*paliashdr;
	vec3_t		vectors[3];
	vec3_t		thismins, oldmins, thismaxs, oldmaxs;
	daliasframe_t *pframe, *poldframe;
	vec3_t angles;

	paliashdr = (dmdl_t *)currentmodel->extradata;

	if ( ( e->frame >= paliashdr->num_frames ) || ( e->frame < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such frame %d\n", 
			currentmodel->name, e->frame);
		e->frame = 0;
	}
	if ( ( e->oldframe >= paliashdr->num_frames ) || ( e->oldframe < 0 ) )
	{
		ri.Con_Printf (PRINT_ALL, "R_CullAliasModel %s: no such oldframe %d\n", 
			currentmodel->name, e->oldframe);
		e->oldframe = 0;
	}

	pframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->frame * paliashdr->framesize);

	poldframe = ( daliasframe_t * ) ( ( byte * ) paliashdr + 
		                              paliashdr->ofs_frames +
									  e->oldframe * paliashdr->framesize);

	/*
	** compute axially aligned mins and maxs
	*/
	if ( pframe == poldframe )
	{
		for ( i = 0; i < 3; i++ )
		{
			mins[i] = pframe->translate[i];
			maxs[i] = mins[i] + pframe->scale[i]*255;
		}
	}
	else
	{
		for ( i = 0; i < 3; i++ )
		{
			thismins[i] = pframe->translate[i];
			thismaxs[i] = thismins[i] + pframe->scale[i]*255;

			oldmins[i]  = poldframe->translate[i];
			oldmaxs[i]  = oldmins[i] + poldframe->scale[i]*255;

			if ( thismins[i] < oldmins[i] )
				mins[i] = thismins[i];
			else
				mins[i] = oldmins[i];

			if ( thismaxs[i] > oldmaxs[i] )
				maxs[i] = thismaxs[i];
			else
				maxs[i] = oldmaxs[i];
		}
	}

	/*
	** compute a full bounding box
	*/
	for ( i = 0; i < 8; i++ )
	{
		vec3_t   tmp;

		if ( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if ( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if ( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];

		VectorCopy( tmp, bbox[i] );
	}

	/*
	** rotate the bounding box
	*/
	VectorCopy( e->angles, angles );
	angles[YAW] = -angles[YAW];
	R_AngleVectors( angles, vectors[0], vectors[1], vectors[2] );

	if ( q3d_use_3dnow && gl_amd_cull->value )
	{	// AMD 3DNow! patch (area mesh M2): rotate + frustum test in one kernel (bbox is not used by the caller)
		return Q3N_CullAliasBox( mins, maxs, vectors[0], e->origin, q3d_frustum, r_numfrustumplanes );
	}

	for ( i = 0; i < 8; i++ )
	{
		vec3_t tmp;

		VectorCopy( bbox[i], tmp );

		bbox[i][0] = DotProduct( vectors[0], tmp );
		bbox[i][1] = -DotProduct( vectors[1], tmp );
		bbox[i][2] = DotProduct( vectors[2], tmp );

		VectorAdd( e->origin, bbox[i], bbox[i] );
	}

	{
		int p, f, aggregatemask = ~0;

		for ( p = 0; p < 8; p++ )
		{
			int mask = 0;

			for ( f = 0; f < r_numfrustumplanes; f++ )
			{
				float dp = DotProduct( frustum[f].normal, bbox[p] );

				if ( ( dp - frustum[f].dist ) < 0 )
				{
					mask |= ( 1 << f );
				}
			}

			aggregatemask &= mask;
		}

		if ( aggregatemask )
		{
			return true;
		}

		return false;
	}
}

/*
=================
R_DrawAliasModel

=================
*/
/* BDGN 3.57m: the part of a model path after "models/" or "models/simple1..3/" (simpler models drawn far away keep the
   same rules as their normal model); NULL if it is not under models/ */
static const char *R_ModelRest (const char *n)
{
 if (strncmp (n, "models/", 7)) return NULL;
 n += 7;
 if (!strncmp (n, "simple", 6) && n[6] >= '1' && n[6] <= '3' && n[7] == '/') n += 8;
 return n;
}
static int R_BrightSkinModel (const char *name,int flags)
{
 int len;
 const char *rest=R_ModelRest(name);
 if(flags & (RF_WEAPONMODEL|RF_SHELL_HALF_DAM|RF_SHELL_GREEN|RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_DOUBLE))return 0;
 if(rest && !strncmp(rest,"monsters/",9))return 1;
 len=strlen(name);
 return !strncmp(name,"players/",8) && len>=9 && !strcmp(name+len-9,"/tris.md2");
}

/* BDGN 3.55i: shadows only under bodies standing on the floor (monsters, players, items, weapons, barrels).
   Projectiles, debris, gibs, explosions, flashes and other models/objects get none; nor do fullbright ones. */
static int R_ShadowAllowed (const entity_t *e)
{
	const char *name = currentmodel ? currentmodel->name : "";
	const char *rest = R_ModelRest (name);
	if (gl_reflect_pass) return 0;	/* BDGN 3.55o: no shadows in the water reflection */
	if (e->flags & (RF_FULLBRIGHT | RF_BEAM | RF_TRANSLUCENT | RF_WEAPONMODEL | RF_DEPTHHACK))
		return 0;
	if (rest && !strncmp(rest, "objects/", 8) && strncmp(rest, "objects/barrels/", 16))
		return 0;
	if (!strncmp(name, "models/hdgore/", 14))
		return 0;	/* BDGN 3.55z: HD gibs lie in their blood: no shadow (like the game's own gibs) */
	return 1;
}

static void R_DrawSoftAliasShadow(entity_t *e)
{
	vec3_t center,normal,p;
	float radius, height, alpha;
	int i;
	if (!R_ShadowFloor(e->origin,center,normal)) return;
	height=e->origin[2]-center[2];
	alpha=0.42f*(1.0f-height/96.0f);
	if (alpha<=0) return;
	radius=currentmodel->radius*0.45f;
	if (radius<8) radius=8;
	if (radius>36) radius=36;
	center[2]+=0.5f;
	qglDisable(GL_TEXTURE_2D);
	qglDisable(GL_ALPHA_TEST);
	qglDisable(GL_CULL_FACE);
	qglDepthMask(GL_FALSE);
	qglEnable(GL_BLEND);
	qglShadeModel(GL_SMOOTH);

	/* One uniform interior and one nonoverlapping feathered ring. */
	qglColor4f(0,0,0,alpha);
	qglBegin(GL_TRIANGLE_FAN);
	qglVertex3fv(center);
	for (i=0;i<=20;i++) {
		float angle=(float)((i%20)*(2*M_PI/20));
		float x=radius*(float)cos(angle),y=radius*(float)sin(angle);
		VectorSet(p,center[0]+x,center[1]+y,center[2]-(normal[0]*x+normal[1]*y)/normal[2]);
		qglVertex3fv(p);
	}
	qglEnd();
	qglBegin(GL_TRIANGLE_STRIP);
	for (i=0;i<=20;i++) {
		float angle=(float)((i%20)*(2*M_PI/20));
		float x=radius*(float)cos(angle),y=radius*(float)sin(angle);
		float z=-(normal[0]*x+normal[1]*y)/normal[2];
		qglColor4f(0,0,0,alpha);
		qglVertex3f(center[0]+x,center[1]+y,center[2]+z);
		qglColor4f(0,0,0,0);
		qglVertex3f(center[0]+1.45f*x,center[1]+1.45f*y,center[2]+1.45f*z);
	}
	qglEnd();

	qglShadeModel(GL_FLAT);
	qglDisable(GL_BLEND);
	qglDepthMask(GL_TRUE);
	qglEnable(GL_CULL_FACE);
	qglEnable(GL_ALPHA_TEST);
	qglEnable(GL_TEXTURE_2D);
}

/*
=================
R_LightShadowDirection

BDGN 3.55m: where the shadow falls, as the horizontal distance per unit of height (x, y) in world space.
The static light map is sampled 32 units around the model: the shadow points away from the brighter side.
The strongest dynamic light nearby (explosion, muzzle flash, rocket, lamp effect) pulls the shadow away from it.
=================
*/
static float R_LightShadowDirection (entity_t *e, float *dx, float *dy)
{
	vec3_t p;
	float px,mx,py,my,avg,gx,gy,len,k,limit=0.6f;
	VectorCopy(e->origin,p); p[2]+=8;
	p[0]=e->origin[0]+32; px=R_StaticLightLevel(p);
	p[0]=e->origin[0]-32; mx=R_StaticLightLevel(p);
	p[0]=e->origin[0];
	p[1]=e->origin[1]+32; py=R_StaticLightLevel(p);
	p[1]=e->origin[1]-32; my=R_StaticLightLevel(p);
	avg=(px+mx+py+my)*0.25f;
	gx=px-mx; gy=py-my;
	len=(float)sqrt(gx*gx+gy*gy);
	k=len/(avg+8.0f)*1.5f;
	if (k>1) k=1;
	*dx=*dy=0;
	if (len>0.001f) { *dx=-gx/len*1.1f*k; *dy=-gy/len*1.1f*k; }
	{	/* BDGN 3.55ak: a real light source - the sun when the model can see it, else the strongest lamp it can see
		   (gl_sun, gl_sun.c) - replaces the estimate from the brightness around the model */
		float sx,sy,sk=R_SunShadowDirection(e->origin,&sx,&sy);
		if (sk>0) { *dx=sx; *dy=sy; k=sk; limit=1.6f; }
	}
	len=(float)sqrt(*dx*(*dx)+*dy*(*dy));
	/* BDGN 3.55y: at most 0.6 units sideways per unit of height (about 31 degrees from straight down): the
	   shadow stays under the model, also under floating pickups (1.6 before: long, detached shadows) */
	if (len>limit) { *dx*=limit/len; *dy*=limit/len; }	/* 3.55ak: up to 1.6 (58 degrees) from the sun or a lamp */
	return k;
}

/*
=================
R_DrawLightAliasShadow

BDGN 3.55m: gl_shadows 3. The model is projected along the light direction onto the floor plane below it (world
space, the floor's own slope). Equal-depth overlap is rejected with GL_LESS, so the shadow has one even darkness
without a stencil buffer.
=================
*/
static void R_DrawLightAliasShadow (entity_t *e, dmdl_t *paliashdr)
{
	vec3_t center,normal,forward,right,up,dir,w,pt,jx,jy;
	float dx,dy,height,alpha,nd,t,k,soft;
	int *order,*start,count,pass,step;
	GLint depthfunc;
	/* BDGN 3.55y: soft edge = 4 passes, each moved a little sideways on the floor (more with height) and drawn a
	   bit closer (polygon offset), so they add up only at the edges; darkness follows how clear the light
	   direction is (a diffuse room gives a lighter shadow) and fades with the height above the floor */
	static const float jit[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
	if (!R_ShadowFloor(e->origin,center,normal)) return;
	height=e->origin[2]-center[2];
	alpha=0.42f*(1.0f-height/140.0f);
	if (alpha<=0.02f) return;
	k=R_LightShadowDirection(e,&dx,&dy);
	alpha*=0.45f+0.55f*k;
	VectorSet(dir,dx,dy,-1);
	nd=DotProduct(normal,dir);
	if (nd>-0.1f) return;
	AngleVectors(e->angles,forward,right,up);
	qglGetIntegerv(GL_DEPTH_FUNC,&depthfunc);	/* gl_ztrick alternates LEQUAL / GEQUAL */
	qglDisable(GL_TEXTURE_2D);
	qglDisable(GL_ALPHA_TEST);
	qglDisable(GL_CULL_FACE);
	qglEnable(GL_BLEND);
	qglDepthFunc(depthfunc==GL_GEQUAL?GL_GREATER:GL_LESS);
	qglEnable(GL_POLYGON_OFFSET_FILL);
	/* two directions on the floor plane for the soft edge */
	VectorSet(jx,1,0,0);
	VectorMA(jx,-DotProduct(jx,normal),normal,jx);
	VectorNormalize(jx);
	CrossProduct(normal,jx,jy);
	soft=0.5f+height*0.012f;	/* BDGN 3.55ai: was 1 + height*0.06 up to 5 units: four separate copies of the shadow were visible */
	if (soft>1.2f) soft=1.2f;
	start=(int *)((byte *)paliashdr+paliashdr->ofs_glcmds);
	for (pass=0;pass<4;pass++) {
		vec3_t off;
		VectorScale(jx,jit[pass][0]*soft,off);
		VectorMA(off,jit[pass][1]*soft,jy,off);
		qglPolygonOffset(-1,-2.0f-2.0f*pass);
		qglColor4f(0,0,0,alpha*0.42f);
		/* BDGN 3.56a: the flickering, ragged shadow. All triangles of the shadow lie in one plane, and "draw only
		   where nothing of this pass is yet" was done with the depth test alone (GL_LESS); the depth of two triangles
		   in the same plane differs in the last bits here and there, so a second triangle got through and darkened
		   those pixels twice - other pixels every frame. With a stencil buffer the pass now marks every pixel it has
		   darkened with its own number and leaves marked pixels alone: once per pixel, always. Without a stencil
		   buffer the depth test is all there is, as before. */
		for (step=1;step<2;step++) {
		if (r_shadow_stencil) {
			if (++r_shadow_stencil_ref>255) r_shadow_stencil_ref=1;
			qglEnable(GL_STENCIL_TEST);
			qglStencilFunc(GL_NOTEQUAL,r_shadow_stencil_ref,255);
			qglStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
			qglDepthFunc(depthfunc==GL_GEQUAL?GL_GEQUAL:GL_LEQUAL);
			qglDepthMask(0);
		}
		order=start;
		while ((count=*order++)!=0) {
			if (count<0) { count=-count; qglBegin(GL_TRIANGLE_FAN); }
			else qglBegin(GL_TRIANGLE_STRIP);
			do {
				float *v=s_lerped[order[2]];
				w[0]=e->origin[0]+forward[0]*v[0]-right[0]*v[1]+up[0]*v[2];
				w[1]=e->origin[1]+forward[1]*v[0]-right[1]*v[1]+up[1]*v[2];
				w[2]=e->origin[2]+forward[2]*v[0]-right[2]*v[1]+up[2]*v[2];
				VectorSubtract(center,w,pt);
				t=DotProduct(normal,pt)/nd;
				VectorMA(w,t,dir,pt);
				VectorMA(pt,0.3f,normal,pt);
				VectorAdd(pt,off,pt);
				qglVertex3fv(pt);
				order+=3;
			} while (--count);
			qglEnd();
		}
		}
	}
	if (r_shadow_stencil) {
		qglDisable(GL_STENCIL_TEST);
		qglDepthMask(1);
	}
	qglPolygonOffset(0,0);
	qglDisable(GL_POLYGON_OFFSET_FILL);
	qglDepthFunc(depthfunc);
	qglDisable(GL_BLEND);
	qglEnable(GL_CULL_FACE);
	qglEnable(GL_ALPHA_TEST);
	qglEnable(GL_TEXTURE_2D);
}

/*
=================
R_PixelLightAlias

BDGN 3.55h: per-pixel dynamic light pass for an alias model; same frame interpolation as GL_DrawAliasFrameLerp,
normals from the frame's light normal indices, model space (the entity's modelview is still current).
=================
*/
static void R_PixelLightAlias (dmdl_t *paliashdr, float backlerp)
{
	static float	xyz[MAX_VERTS][4], nrm[MAX_VERTS][3];
	daliasframe_t	*frame, *oldframe;
	dtrivertx_t	*v, *ov;
	vec3_t		move, delta, vectors[3], frontv, backv;
	float		frontlerp = 1.0f - backlerp;
	int		i;

 { const smp_pose_t *pose=SMP_Pose(currententity,paliashdr,backlerp);
   if(pose) {
    GL_DrawPixelLightModel((const float (*)[4])pose->xyz,(const float (*)[3])pose->normal,
      (const int *)((byte *)paliashdr+paliashdr->ofs_glcmds)); return;
   }
 }
	if ( paliashdr->num_xyz > MAX_VERTS )
		return;
	frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames + currententity->frame * paliashdr->framesize);
	oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames + currententity->oldframe * paliashdr->framesize);
	v = frame->verts;
	ov = oldframe->verts;
	VectorSubtract (currententity->oldorigin, currententity->origin, delta);
	R_AngleVectors (currententity->angles, vectors[0], vectors[1], vectors[2]);
	move[0] = DotProduct (delta, vectors[0]);
	move[1] = -DotProduct (delta, vectors[1]);
	move[2] = DotProduct (delta, vectors[2]);
	VectorAdd (move, oldframe->translate, move);
	for (i=0 ; i<3 ; i++)
	{
		move[i] = backlerp*move[i] + frontlerp*frame->translate[i];
		frontv[i] = frontlerp*frame->scale[i];
		backv[i] = backlerp*oldframe->scale[i];
	}
	for (i=0 ; i<paliashdr->num_xyz ; i++)
	{
		const float *n1 = r_avertexnormals[v[i].lightnormalindex], *n0 = r_avertexnormals[ov[i].lightnormalindex];

		xyz[i][0] = move[0] + ov[i].v[0]*backv[0] + v[i].v[0]*frontv[0];
		xyz[i][1] = move[1] + ov[i].v[1]*backv[1] + v[i].v[1]*frontv[1];
		xyz[i][2] = move[2] + ov[i].v[2]*backv[2] + v[i].v[2]*frontv[2];
		nrm[i][0] = n1[0]*frontlerp + n0[0]*backlerp;
		nrm[i][1] = n1[1]*frontlerp + n0[1]*backlerp;
		nrm[i][2] = n1[2]*frontlerp + n0[2]*backlerp;
	}
	GL_DrawPixelLightModel ((const float (*)[4])xyz, (const float (*)[3])nrm,
		(const int *)((byte *)paliashdr + paliashdr->ofs_glcmds));
}

/*
=================
R_DrawFXModel

BDGN 3.55t: modern weapon effects (pak8-hdfx3d.pak). The client sends these flags only to this renderer, and only
when bdgn_gl_modernfx_capable is set. Two ways to draw an alias model:

RF_FXMESH  the triangles with the skin, unlit and two-sided. Vertex alpha = light normal index / 161 (lerped
           between the frames) times the entity alpha. RF_FXADD adds the colour (glow), else normal blending.
RF_PUFFS   every triangle (a, b, c) is one camera-facing puff: centre a, half size |b - a|, rotation from the
           direction a -> c in the model's x/y. The light normal indices carry: a = alpha, b = heat (0 smoke lit by
           the world and the dynamic lights .. 161 glowing and nearly additive), c = picture in the 4x4 skin atlas.
           Skins are premultiplied (rgb already times alpha); the puffs are drawn back to front.
=================
*/
#define FX_MAX_PUFFS 512
typedef struct { float d; int i; } fx_order_t;
static int R_FXOrderCompare (const void *a, const void *b)
{
	float d = ((const fx_order_t *)b)->d - ((const fx_order_t *)a)->d;	/* far first */
	return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

static void R_FXLerpVert (const daliasframe_t *f, const daliasframe_t *of, int index, float front, float back, vec3_t out)
{
	const dtrivertx_t *v = &f->verts[index], *ov = &of->verts[index];
	int j;
	for (j = 0; j < 3; j++)
		out[j] = (v->v[j] * f->scale[j] + f->translate[j]) * front + (ov->v[j] * of->scale[j] + of->translate[j]) * back;
}

static void R_FXToWorld (const entity_t *e, vec3_t axis[3], const vec3_t p, vec3_t out)
{	/* model: x forward, y left, z up (as R_RotateForEntity) */
	int j;
	for (j = 0; j < 3; j++)
		out[j] = e->origin[j] + p[0] * axis[0][j] - p[1] * axis[1][j] + p[2] * axis[2][j];
}

/* Project the stored model-space hit back onto the current interpolated MD2 surface. */
void R_DrawActorMark(entity_t *e)
{
	model_t *model=(model_t *)e->model;
	image_t *skin=(image_t *)e->skin;
	const dmdl_t *header;
	const daliasframe_t *frame,*previous;
	const dtriangle_t *triangle;
	vec3_t axis[3],ray,source,hit,normal,world,view,p0,p1,p2,u,v,n,delta;
	float closest=36,back,offset,scale,sbase;
	int i, j, cur, old;
	if (!model || model->type!=mod_alias || !skin) return;
	header=(const dmdl_t *)model->extradata;
	if (!header || header->num_frames<1) return;
	cur=e->frame>=0 && e->frame<header->num_frames?e->frame:0;
	old=e->oldframe>=0 && e->oldframe<header->num_frames?e->oldframe:cur;
	frame=(const daliasframe_t *)((const byte *)header+header->ofs_frames+cur*header->framesize);
	previous=(const daliasframe_t *)((const byte *)header+header->ofs_frames+old*header->framesize);
	back=r_lerpmodels->value?e->backlerp:0;
	for(i=0;i<3;i++) ray[i]=((e->lightstyle>>(8*i))&255)-128;
	if (!VectorNormalize(ray)) return;
	VectorMA(e->oldorigin,12,ray,source);
	triangle=(const dtriangle_t *)((const byte *)header+header->ofs_tris);
	for(i=0;i<header->num_tris;i++,triangle++)
	{
		float denom, dist, side;
		R_FXLerpVert(frame,previous,triangle->index_xyz[0],1-back,back,p0);
		R_FXLerpVert(frame,previous,triangle->index_xyz[1],1-back,back,p1);
		R_FXLerpVert(frame,previous,triangle->index_xyz[2],1-back,back,p2);
		VectorSubtract(p1,p0,u);
		VectorSubtract(p2,p0,v);
		CrossProduct(u,v,n);
		denom=DotProduct(n,ray);
		if (fabs(denom)<0.001f) continue;
		VectorSubtract(source,p0,delta);
		dist=DotProduct(n,delta)/denom;
		if (dist<0 || dist>=closest) continue;
		VectorMA(source,-dist,ray,world);
		VectorSubtract(world,p0,delta); CrossProduct(u,delta,view);
		side=DotProduct(view,n);
		if (side<0) continue;
		VectorSubtract(p2,p1,u); VectorSubtract(world,p1,delta); CrossProduct(u,delta,view);
		if (DotProduct(view,n)<0) continue;
		VectorSubtract(p0,p2,u); VectorSubtract(world,p2,delta); CrossProduct(u,delta,view);
		if (DotProduct(view,n)<0) continue;
		closest=dist;
		VectorCopy(world,hit);
		VectorScale(n,denom<0?-1.0f:1.0f,normal);
	}
	if (closest>=36 || !VectorNormalize(normal)) return;
	AngleVectors(e->angles,axis[0],axis[1],axis[2]);
	R_FXToWorld(e,axis,hit,world);
	VectorSubtract(r_origin,world,view);
	VectorSet(n,DotProduct(view,axis[0]),-DotProduct(view,axis[1]),DotProduct(view,axis[2]));
	if (DotProduct(n,normal)<0) return;
	PerpendicularVector(u,normal);
	CrossProduct(normal,u,v);
	offset=0.30f;
	scale=e->skinnum==1?2.3f:1.85f;
	sbase=e->skinnum/4.0f;
	VectorMA(hit,offset,normal,hit);
	GL_Bind(skin->texnum);
	GL_TexEnv(GL_MODULATE);
	qglEnable(GL_BLEND);
	qglDisable(GL_ALPHA_TEST);
	qglDisable(GL_CULL_FACE);
	/* Filter the already lit skin. A fully dark surface stays dark. */
	qglBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
	qglColor4f(1,1,1,1);
	qglBegin(GL_QUADS);
	for(j=0;j<4;j++)
	{
		VectorMA(hit,(j==0||j==3)?-scale:scale,u,delta);
		VectorMA(delta,j<2?-scale:scale,v,delta);
		R_FXToWorld(e,axis,delta,world);
		qglTexCoord2f(sbase+((j==0||j==3)?.004f:.246f),j<2?.004f:.996f);
		qglVertex3fv(world);
	}
	qglEnd();
	qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	qglColor4f(1,1,1,1);
	qglEnable(GL_CULL_FACE);
	qglEnable(GL_ALPHA_TEST);
	qglDisable(GL_BLEND);
	GL_TexEnv(GL_REPLACE);
}

static qboolean R_DrawFXModel (entity_t *e)
{
	dmdl_t			*hdr = (dmdl_t *)currentmodel->extradata;
	daliasframe_t	*fr, *ofr;
	dtriangle_t		*tri;
	image_t			*skin;
	float			front, back, alpha;
	vec3_t			axis[3], p0, p1, p2, w;
	int				i, k, n;

	if (e->frame < 0 || e->frame >= hdr->num_frames) e->frame = 0;
	if (e->oldframe < 0 || e->oldframe >= hdr->num_frames) e->oldframe = 0;
	fr = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + e->frame * hdr->framesize);
	ofr = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + e->oldframe * hdr->framesize);
	back = r_lerpmodels->value ? e->backlerp : 0;
	front = 1.0f - back;
	alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
	skin = e->skin ? (image_t *)e->skin : NULL;	/* BDGN 3.55v: the client's texture set (AI / standard) */
	if (!skin) skin = (e->skinnum >= 0 && e->skinnum < MAX_MD2SKINS) ? currentmodel->skins[e->skinnum] : NULL;
	if (!skin) skin = currentmodel->skins[0];
	if (!skin) skin = r_notexture;
	AngleVectors (e->angles, axis[0], axis[1], axis[2]);
	tri = (dtriangle_t *)((byte *)hdr + hdr->ofs_tris);

	GL_Bind (skin->texnum);
	GL_TexEnv (GL_MODULATE);
	qglShadeModel (GL_SMOOTH);
	qglEnable (GL_BLEND);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_CULL_FACE);
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));

	if (e->flags & RF_PUFFS)
	{
		static fx_order_t	order[FX_MAX_PUFFS];
		static vec3_t		pc[FX_MAX_PUFFS];
		static float		pa[FX_MAX_PUFFS], ph[FX_MAX_PUFFS], ps[FX_MAX_PUFFS], pr[FX_MAX_PUFFS];
		static int			pt[FX_MAX_PUFFS];
		vec3_t				light;
		int					m = 0;
		qboolean rail_smoke=(e->lightstyle&0xffff)==FX_LAYER_RAIL_SMOKE;
		qboolean straight_alpha=(e->lightstyle&0xffff)==FX_LAYER_BFG_SMOKE || rail_smoke;
		float grow=1.0f;	/* BDGN 3.56x: a cloud that grows from nothing (client/ref.h FX_PUFF_SCALE_ONE) */
		qboolean centred=false;
		vec3_t centre={0,0,0};
		if (!(e->flags & RF_RAILSMOKE_SHORT) && e->lightstyle>=FX_PUFF_SCALE_ONE)
		{
			if (e->lightstyle & FX_PUFF_SCALE_CENTRE)
			{	/* BDGN 3.57h: scale about the middle of the cloud (1/128 .. 2) */
				int cn=0;
				grow=((e->lightstyle>>16)&255)*(1.0f/128.0f);
				centred=true;
				for (i=0;i<(hdr->num_tris>FX_MAX_PUFFS?FX_MAX_PUFFS:hdr->num_tris);i++)
				{
					R_FXLerpVert (fr, ofr, tri[i].index_xyz[0], front, back, p0);
					VectorAdd (centre, p0, centre);
					cn++;
				}
				if (cn) VectorScale (centre, 1.0f/cn, centre);
			}
			else
				grow=((e->lightstyle>>16)&255)*(1.0f/255.0f);
		}

		n = hdr->num_tris > FX_MAX_PUFFS ? FX_MAX_PUFFS : hdr->num_tris;
		if (e->flags & RF_FULLBRIGHT)
			light[0] = light[1] = light[2] = 1.0f;
		else
		{
			float lum;
			R_LightPoint (e->origin, light);	/* world light and the dynamic lights (explosion, muzzle flash) */
			/* BDGN 3.57r: smoke keeps most of its own grey. R_LightPoint takes the colour of the surface under the
			   point (and the dynamic lights), and in a red-lit room the smoke after a hit came out red (owner's
			   video). Only 35 % of the colour difference is kept; the brightness stays. Blood mist is red in its
			   picture, so it stays red. */
			/* 3.57y: no colour at all from the light any more (the brightness only). Above lava the light under the
			   point is the lava's glowing floor, and 35 % of it still made the lava smoke red (owner, 2026-11-20).
			   Puffs that have a colour have it in their own picture (blood mist red, fire orange, BFG green). */
			lum = light[0] * 0.30f + light[1] * 0.59f + light[2] * 0.11f;
			for (k = 0; k < 3; k++)
				light[k] = lum;
			for (k = 0; k < 3; k++)	/* 3.55z: smoke and blood stay visible in dark corners (was 0.12 .. 1) */
				light[k] = light[k] > 0.85f ? 1.0f : 0.28f + 0.85f * light[k];
		}
		for (i = 0; i < n; i++)
		{
			int a = tri[i].index_xyz[0], b = tri[i].index_xyz[1], c = tri[i].index_xyz[2];
			float pal = (fr->verts[a].lightnormalindex * front + ofr->verts[a].lightnormalindex * back) * (1.0f / 161.0f) * alpha;
			if (pal <= 0.004f)
				continue;
			R_FXLerpVert (fr, ofr, a, front, back, p0);
			if ((e->flags & RF_RAILSMOKE_SHORT) && p0[0] > e->lightstyle)
				continue;
			R_FXLerpVert (fr, ofr, b, front, back, p1);
			R_FXLerpVert (fr, ofr, c, front, back, p2);
			if (centred)
			{	/* 3.57h: every puff moves to / from the middle of the cloud and grows or shrinks with it */
				for (k=0;k<3;k++)
				{
					p0[k]=centre[k]+(p0[k]-centre[k])*grow;
					p1[k]=centre[k]+(p1[k]-centre[k])*grow;
					p2[k]=centre[k]+(p2[k]-centre[k])*grow;
					if (rail_smoke)
					{	/* Its fixed frame supplies size/texture, not a baked rising path. */
						p0[k]-=centre[k];
						p1[k]-=centre[k];
						p2[k]-=centre[k];
					}
				}
			}
			else if (grow<1.0f)
			{	/* every puff moves towards the origin and shrinks with it */
				VectorScale (p0, grow, p0);
				VectorScale (p1, grow, p1);
				VectorScale (p2, grow, p2);
			}
			if (!(e->flags & RF_FULLBRIGHT))
				pal *= 1.35f;	/* 3.55z: lit puffs (smoke, blood mist) were too faint in game */
			pa[m] = pal > 1.0f ? 1.0f : pal;
			ph[m] = (fr->verts[b].lightnormalindex * front + ofr->verts[b].lightnormalindex * back) * (1.0f / 161.0f);
			if (ph[m] > 1.0f) ph[m] = 1.0f;
			VectorSubtract (p1, p0, w);
			ps[m] = VectorLength (w);
			pr[m] = (float)atan2 (p2[1] - p0[1], p2[0] - p0[0]);
			pt[m] = fr->verts[c].lightnormalindex & 15;
			R_FXToWorld (e, axis, p0, pc[m]);
			VectorSubtract (pc[m], r_origin, w);
			order[m].d = DotProduct (w, vpn);
			order[m].i = m;
			m++;
		}
		qsort (order, m, sizeof(order[0]), R_FXOrderCompare);
		qglBlendFunc (straight_alpha?GL_SRC_ALPHA:GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  GL_ModernPuffBegin(!straight_alpha);
		qglBegin (GL_QUADS);
		for (k = 0; k < m; k++)
		{
			int		j = order[k].i;
			float	cr = (float)cos (pr[j]) * ps[j], sr = (float)sin (pr[j]) * ps[j];
			float	u0 = (pt[j] & 3) * 0.25f + 0.002f, v0 = (pt[j] >> 2) * 0.25f + 0.002f, u1 = u0 + 0.246f, v1 = v0 + 0.246f;
			float	h = ph[j], a = pa[j], colour_alpha=straight_alpha?1.0f:a;
			vec3_t	R, U, q;
			for (i = 0; i < 3; i++)
			{
				R[i] = cr * vright[i] + sr * vup[i];
				U[i] = -sr * vright[i] + cr * vup[i];
			}
			qglColor4f ((light[0] * (1.0f - h) + h) * colour_alpha, (light[1] * (1.0f - h) + h) * colour_alpha, (light[2] * (1.0f - h) + h) * colour_alpha,
				a * (1.0f - 0.85f * h));
			for (i = 0; i < 3; i++) q[i] = pc[j][i] - R[i] - U[i];
			qglTexCoord2f (u0, v1); qglVertex3fv (q);
			for (i = 0; i < 3; i++) q[i] = pc[j][i] - R[i] + U[i];
			qglTexCoord2f (u0, v0); qglVertex3fv (q);
			for (i = 0; i < 3; i++) q[i] = pc[j][i] + R[i] + U[i];
			qglTexCoord2f (u1, v0); qglVertex3fv (q);
			for (i = 0; i < 3; i++) q[i] = pc[j][i] + R[i] - U[i];
			qglTexCoord2f (u1, v1); qglVertex3fv (q);
		}
		qglEnd ();
  GL_ModernPuffEnd();
		c_alias_polys += m * 2;
	}
	else
	{
		dstvert_t	*st = (dstvert_t *)((byte *)hdr + hdr->ofs_st);
		float		isw = 1.0f / (hdr->skinwidth > 0 ? hdr->skinwidth : 256), ish = 1.0f / (hdr->skinheight > 0 ? hdr->skinheight : 256);

		qglBlendFunc (GL_SRC_ALPHA, (e->flags & RF_FXADD) ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
		qglBegin (GL_TRIANGLES);
		for (i = 0; i < hdr->num_tris; i++)
		{
			for (k = 0; k < 3; k++)
			{
				int	idx = tri[i].index_xyz[k];
				float va = (fr->verts[idx].lightnormalindex * front + ofr->verts[idx].lightnormalindex * back) * (1.0f / 161.0f) * alpha;
				R_FXLerpVert (fr, ofr, idx, front, back, p0);
				R_FXToWorld (e, axis, p0, w);
				qglColor4f (1, 1, 1, va > 1.0f ? 1.0f : va);
				qglTexCoord2f ((st[tri[i].index_st[k]].s + 0.5f) * isw, (st[tri[i].index_st[k]].t + 0.5f) * ish);
				qglVertex3fv (w);
			}
		}
		qglEnd ();
		c_alias_polys += hdr->num_tris;
	}

	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDisable (GL_BLEND);
	qglEnable (GL_CULL_FACE);
	qglColor4f (1, 1, 1, 1);
	GL_TexEnv (GL_REPLACE);
	qglShadeModel (GL_FLAT);
	if (e->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);
	return true;
}

static void R_DrawChaingunHeat(dmdl_t *header,int heat)
{
	const int *order=(const int *)((const byte *)header+header->ofs_glcmds);
	float strength=0.025f+(heat-500)*0.00285f;
	int count;
	if (strength>0.74f) strength=0.74f;
	qglDisable(GL_TEXTURE_2D);
	qglDisable(GL_ALPHA_TEST);
	qglEnable(GL_BLEND);
	qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask(GL_FALSE);
	qglShadeModel(GL_SMOOTH);
	while ((count=*order++)!=0)
	{
		qglBegin(count<0?GL_TRIANGLE_FAN:GL_TRIANGLE_STRIP);
		if (count<0) count=-count;
		while (count--)
		{
			float *v=s_lerped[order[2]];
			float mask=(v[0]-8.5f)/4.5f;
			if (mask<0) mask=0;
			if (mask>1) mask=1;
			qglColor4f(0.98f,0.025f+0.09f*(1-strength),0.009f,strength*mask*mask);
			qglVertex3fv(v);
			order+=3;
		}
		qglEnd();
	}
	qglDepthMask(GL_TRUE);
	qglDisable(GL_BLEND);
	qglEnable(GL_ALPHA_TEST);
	qglEnable(GL_TEXTURE_2D);
	qglColor4f(1,1,1,1);
}

/* BDGN 3.56z: the machine gun's muzzle glows red from 75 % heat. The muzzle is found on the model's first frame (in
   its own space the front end of the barrel lies beyond x 18.5, above z -26: not the magazine), so the glow stays
   on the same vertices however the weapon moves in its animation. */
static void R_DrawMachinegunHeat(dmdl_t *header,int heat)
{
	const int *order=(const int *)((const byte *)header+header->ofs_glcmds);
	const daliasframe_t *f0=(const daliasframe_t *)((const byte *)header+header->ofs_frames);
	float strength=0.025f+(heat-750)*0.00286f;
	int count;
	if (strength>0.74f) strength=0.74f;
	qglDisable(GL_TEXTURE_2D);
	qglDisable(GL_ALPHA_TEST);
	qglEnable(GL_BLEND);
	qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask(GL_FALSE);
	qglShadeModel(GL_SMOOTH);
	while ((count=*order++)!=0)
	{
		qglBegin(count<0?GL_TRIANGLE_FAN:GL_TRIANGLE_STRIP);
		if (count<0) count=-count;
		while (count--)
		{
			const dtrivertx_t *rv=&f0->verts[order[2]];
			float x=rv->v[0]*f0->scale[0]+f0->translate[0], z=rv->v[2]*f0->scale[2]+f0->translate[2];
			float mask=(x-18.5f)/3.0f;
			if (mask<0 || z<-26) mask=0;
			if (mask>1) mask=1;
			qglColor4f(0.98f,0.025f+0.09f*(1-strength),0.009f,strength*mask*mask);
			qglVertex3fv(s_lerped[order[2]]);
			order+=3;
		}
		qglEnd();
	}
	qglDepthMask(GL_TRUE);
	qglDisable(GL_BLEND);
	qglEnable(GL_ALPHA_TEST);
	qglEnable(GL_TEXTURE_2D);
	qglColor4f(1,1,1,1);
}

void R_DrawAliasModel (entity_t *e)
{
	int			i;
	dmdl_t		*paliashdr;
	float		an;
	vec3_t		bbox[8];
	image_t		*skin;
	int			flatmodel,brightmodel;
	int			pixelmask=0;	/* BDGN 3.55h: dynamic lights added per pixel after the model */

	brightmodel=r_brightskins->value && R_BrightSkinModel(currentmodel->name,e->flags);

	/* BDGN 3.51q: first-person weapon as a 2D sprite (falls back to the 3D model without a bank) */
	if ((e->flags & (RF_WEAPONSPRITE | RF_WEAPONFLASH)) && R_DrawWeaponSprite (e)) return;

	/* BDGN 3.51q: pre-rendered enemy: sphere visibility test before any bank work, then the sprite; when no sprite
	   can be drawn (missing bank, ...) the 3D model below is culled and drawn as usual */
	if (e->flags & RF_DUKEANIM)
	{
		if (R_DukeCull(e)) return;
		if (R_DrawDuke(e)) return;
	}

	if ( !( e->flags & RF_WEAPONMODEL ) )
	{
		if ( R_CullAliasModel( bbox, e ) )
			return;
	}

	if ( ( e->flags & ( RF_FXMESH | RF_PUFFS ) ) && R_DrawFXModel( e ) )
		return;	/* BDGN 3.55t: modern weapon effects */

	if ( e->flags & RF_WEAPONMODEL )
	{
		if ( r_lefthand->value == 2 )
			return;
	}

	paliashdr = (dmdl_t *)currentmodel->extradata;

	//
	// get lighting information
	//
	// PMM - rewrote, reordered to handle new shells & mixing
	// PMM - 3.20 code .. replaced with original way of doing it to keep mod authors happy
	//
	if ( currententity->flags & ( RF_SHELL_HALF_DAM | RF_SHELL_GREEN | RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE ) )
	{
		VectorClear (shadelight);
		if (currententity->flags & RF_SHELL_HALF_DAM)
		{
				shadelight[0] = 0.56;
				shadelight[1] = 0.59;
				shadelight[2] = 0.45;
		}
		if ( currententity->flags & RF_SHELL_DOUBLE )
		{
			shadelight[0] = 0.9;
			shadelight[1] = 0.7;
		}
		if ( currententity->flags & RF_SHELL_RED )
			shadelight[0] = 1.0;
		if ( currententity->flags & RF_SHELL_GREEN )
			shadelight[1] = 1.0;
		if ( currententity->flags & RF_SHELL_BLUE )
			shadelight[2] = 1.0;
	}
/*
		// PMM -special case for godmode
		if ( (currententity->flags & RF_SHELL_RED) &&
			(currententity->flags & RF_SHELL_BLUE) &&
			(currententity->flags & RF_SHELL_GREEN) )
		{
			for (i=0 ; i<3 ; i++)
				shadelight[i] = 1.0;
		}
		else if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE ) )
		{
			VectorClear (shadelight);

			if ( currententity->flags & RF_SHELL_RED )
			{
				shadelight[0] = 1.0;
				if (currententity->flags & (RF_SHELL_BLUE|RF_SHELL_DOUBLE) )
					shadelight[2] = 1.0;
			}
			else if ( currententity->flags & RF_SHELL_BLUE )
			{
				if ( currententity->flags & RF_SHELL_DOUBLE )
				{
					shadelight[1] = 1.0;
					shadelight[2] = 1.0;
				}
				else
				{
					shadelight[2] = 1.0;
				}
			}
			else if ( currententity->flags & RF_SHELL_DOUBLE )
			{
				shadelight[0] = 0.9;
				shadelight[1] = 0.7;
			}
		}
		else if ( currententity->flags & ( RF_SHELL_HALF_DAM | RF_SHELL_GREEN ) )
		{
			VectorClear (shadelight);
			// PMM - new colors
			if ( currententity->flags & RF_SHELL_HALF_DAM )
			{
				shadelight[0] = 0.56;
				shadelight[1] = 0.59;
				shadelight[2] = 0.45;
			}
			if ( currententity->flags & RF_SHELL_GREEN )
			{
				shadelight[1] = 1.0;
			}
		}
	}
			//PMM - ok, now flatten these down to range from 0 to 1.0.
	//		max_shell_val = max(shadelight[0], max(shadelight[1], shadelight[2]));
	//		if (max_shell_val > 0)
	//		{
	//			for (i=0; i<3; i++)
	//			{
	//				shadelight[i] = shadelight[i] / max_shell_val;
	//			}
	//		}
	// pmm
*/
	else if ( (currententity->flags & RF_FULLBRIGHT) || GL_FULLBRIGHT() || brightmodel )
	{
		for (i=0 ; i<3 ; i++)
			shadelight[i] = 1.0;
	}
	else
	{
		R_LightPoint (currententity->origin, shadelight);	/* all lights: also feeds r_lightlevel below (game AI) */

		// player lighting hack for communication back to server
		// big hack!
		if ( currententity->flags & RF_WEAPONMODEL )
		{
			// pick the greatest component, which should be the same
			// as the mono value returned by software
			if (shadelight[0] > shadelight[1])
			{
				if (shadelight[0] > shadelight[2])
					r_lightlevel->value = 150*shadelight[0];
				else
					r_lightlevel->value = 150*shadelight[2];
			}
			else
			{
				if (shadelight[1] > shadelight[2])
					r_lightlevel->value = 150*shadelight[1];
				else
					r_lightlevel->value = 150*shadelight[2];
			}

		}

		/* BDGN 3.55h: lights drawn per pixel on this model are left out of its per-vertex light */
		if ( !( currententity->flags & RF_TRANSLUCENT ) && (int)r_drawflat->value < 2 )
			pixelmask = GL_PixelLightModelMask( currententity, currentmodel->radius > 0 ? currentmodel->radius : 48.0f );	/* alias models have no radius */
		if ( pixelmask )
		{
			gl_pixel_model_skip = pixelmask;
			R_LightPoint (currententity->origin, shadelight);
			gl_pixel_model_skip = 0;
		}

		if ( gl_monolightmap->string[0] != '0' )
		{
			float s = shadelight[0];

			if ( s < shadelight[1] )
				s = shadelight[1];
			if ( s < shadelight[2] )
				s = shadelight[2];

			shadelight[0] = s;
			shadelight[1] = s;
			shadelight[2] = s;
		}
	}

	if ( currententity->flags & RF_MINLIGHT )
	{
		for (i=0 ; i<3 ; i++)
			if (shadelight[i] > 0.1)
				break;
		if (i == 3)
		{
			shadelight[0] = 0.1;
			shadelight[1] = 0.1;
			shadelight[2] = 0.1;
		}
	}

	if ( (currententity->flags & RF_GLOW) && !GL_FULLBRIGHT() && !brightmodel )
	{	// bonus items will pulse with time
		float	scale;
		float	min;

		scale = 0.1 * sin(r_newrefdef.time*7);
		for (i=0 ; i<3 ; i++)
		{
			min = shadelight[i] * 0.8;
			shadelight[i] += scale;
			if (shadelight[i] < min)
				shadelight[i] = min;
		}
	}

// =================
// PGM	ir goggles color override
	if ( r_newrefdef.rdflags & RDF_IRGOGGLES && currententity->flags & RF_IR_VISIBLE)
	{
		shadelight[0] = 1.0;
		shadelight[1] = 0.0;
		shadelight[2] = 0.0;
	}
// PGM	
// =================

	shadedots = r_avertexnormal_dots[((int)(currententity->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	if ( gl_flatmodels->value || GL_FULLBRIGHT() || brightmodel )
		shadedots = GL_FlatDots();	// BDGN 3.37
	
	an = currententity->angles[1]/180*M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalizeF (shadevector);

	//
	// locate the proper data
	//

	c_alias_polys += paliashdr->num_tris;

	//
	// draw all the triangles
	//
	if (currententity->flags & RF_DEPTHHACK) // hack the depth range to prevent view model from poking into walls
		qglDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));

	if ( ( currententity->flags & RF_WEAPONMODEL ) && ( r_lefthand->value == 1.0F || gl_yshear_t ) )
	{
		extern void MYgluPerspective( GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar );
		float shear = gl_yshear_t;

		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadIdentity();
		if ( r_lefthand->value == 1.0F )
			qglScalef( -1, 1, 1 );
		gl_yshear_t = 0;	/* BDGN 3.60i: the view weapon stays put in the Duke Nukem 3D view */
	    MYgluPerspective( r_newrefdef.fov_y, ( float ) r_newrefdef.width / r_newrefdef.height,  4,  4096);
		gl_yshear_t = shear;
		qglMatrixMode( GL_MODELVIEW );

		if ( r_lefthand->value == 1.0F )
			qglCullFace( GL_BACK );
	}

    qglPushMatrix ();
	e->angles[PITCH] = -e->angles[PITCH];	// sigh.
	R_RotateForEntity (e);
	e->angles[PITCH] = -e->angles[PITCH];	// sigh.

	// select skin
	if (currententity->skin)
		skin = currententity->skin;	// custom player skin
	else
	{
		if (currententity->skinnum >= MAX_MD2SKINS)
			skin = currentmodel->skins[0];
		else
		{
			skin = currentmodel->skins[currententity->skinnum];
			if (!skin)
				skin = currentmodel->skins[0];
		}
	}
	if (!skin)
		skin = r_notexture;	// fallback...
	GL_Bind(skin->texnum);

	// BDGN 3.45: r_drawflat 2/3/4: monsters, players, the weapon and items in the dominant colour of their skin
	flatmodel = brightmodel || ( (int)r_drawflat->value >= 2 && !( currententity->flags &
				( RF_SHELL_HALF_DAM | RF_SHELL_GREEN | RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE ) ) );
	if ( flatmodel )
	{
		byte	fc[3];
		int		fm = (int)r_drawflat->value > 4 ? 4 : (int)r_drawflat->value;

		float	lo = 0.55f, hi = 0.45f;

		if(brightmodel) {
			GL_BrightSkinColor(skin,fc);
			for(i=0;i<3;i++)shadelight[i]=fc[i]*(1.0f/255.0f);
		} else {
		if (fm == 2 && (currententity->flags & RF_WEAPONMODEL) &&
			R_FlatWeaponColor(currentmodel->name, fc))
		{
			lo = 1.0f; hi = 0.0f; /* strong held-weapon colours, independent of map light */
		}
		else
			GL_FlatImageColor( skin, fm, fc );
		if ( fm == 4 )
			GL_FlatRetro( fc );
		if ( fm == 2 && !( currententity->flags & RF_WEAPONMODEL ) )
		{	// BDGN 3.49: things you must see get a signal colour (not the colour of the floor)
			const char *mn = currentmodel->name;
			int cat = 0;

			if ( strstr( mn, "/g_" ) )
				cat = 8;		// weapon lying on the map: dominant colour of its skin, made bright and saturated (3.49d)
			else if ( strstr( mn, "players/" ) )
			{	// other players (and the weapon they hold): blue, but team games keep their team colour
				const char *sn = skin->name;

				if ( strstr( sn, "ctf_r" ) || strstr( sn, "red" ) )
					cat = 7;		// red team skin (CTF: ctf_r)
				else if ( strstr( sn, "ctf_b" ) || strstr( sn, "blue" ) )
					cat = 2;		// blue team skin (CTF: ctf_b)
				else if ( (int)ri.Cvar_Get( "dmflags", "0", 0 )->value & 192 )
					cat = -1;		// DF_MODELTEAMS / DF_SKINTEAMS: other team games keep the dominant colour of the skin
				else
					cat = 2;
			}
			else if ( strstr( mn, "armor/jacket" ) )
				cat = 5;		// jacket armor: green
			else if ( strstr( mn, "armor/combat" ) )
				cat = 6;		// combat armor: yellow
			else if ( strstr( mn, "armor/body" ) )
				cat = 7;		// body armor: red
			else if ( strstr( mn, "/monsters/" ) )	/* 3.57m: also models/simple3/monsters/ */
				cat = 3;		// monsters: orange
			// powerups keep the dominant colour of their skin (3.49b)
			if ( cat == 8 )
			{	// brightest channel to 255 and the colour pushed away from grey, nearly unaffected by light:
				// the floor is lit and mostly dull, so the weapon stands out without a new colour
				int mx = fc[0] > fc[1] ? fc[0] : fc[1], mean;

				if ( fc[2] > mx )
					mx = fc[2];
				if ( mx < 1 )
					mx = 1;
				for ( i = 0 ; i < 3 ; i++ )
					fc[i] = (byte)( fc[i] * 255 / mx );
				mean = ( fc[0] + fc[1] + fc[2] ) / 3;
				for ( i = 0 ; i < 3 ; i++ )
				{
					int v = mean + ( fc[i] - mean ) * 3 / 2;

					fc[i] = (byte)( v < 0 ? 0 : v > 255 ? 255 : v );
				}
				lo = 0.92f; hi = 0.08f;
			}
			else if ( cat > 0 )
			{
				static const byte sig[8][3] = { {0,0,0}, {255,20,20}, {30,90,255}, {255,140,0}, {255,40,255}, {0,230,40}, {255,235,0}, {255,20,20} };

				fc[0] = sig[cat][0]; fc[1] = sig[cat][1]; fc[2] = sig[cat][2];
				lo = 0.85f; hi = 0.15f;		// almost unaffected by the light, so they do not vanish in a dark corner
			}
		}
		for ( i = 0 ; i < 3 ; i++ )
		{
			float v = shadelight[i] > 1.0f ? 1.0f : shadelight[i];

			shadelight[i] = ( lo + hi * v ) * fc[i] * ( 1.0f / 255.0f );		// no model vanishes in a dark corner
		}
		}

		qglDisable( GL_TEXTURE_2D );
	}

	// draw it

	qglShadeModel (GL_SMOOTH);

	GL_TexEnv( GL_MODULATE );
	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglEnable (GL_BLEND);
	}


	if ( (currententity->frame >= paliashdr->num_frames) 
		|| (currententity->frame < 0) )
	{
		ri.Con_Printf (PRINT_ALL, "R_DrawAliasModel %s: no such frame %d\n",
			currentmodel->name, currententity->frame);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}

	if ( (currententity->oldframe >= paliashdr->num_frames)
		|| (currententity->oldframe < 0))
	{
		ri.Con_Printf (PRINT_ALL, "R_DrawAliasModel %s: no such oldframe %d\n",
			currentmodel->name, currententity->oldframe);
		currententity->frame = 0;
		currententity->oldframe = 0;
	}

	if ( !r_lerpmodels->value )
		currententity->backlerp = 0;

	// BDGN 3.37: gl_lowdetail = models farther away than this many units are drawn without frame interpolation
	if ( gl_lowdetail->value > 0 && !( currententity->flags & RF_WEAPONMODEL ) )
	{
		vec3_t	ld_delta;

		VectorSubtract( currententity->origin, r_origin, ld_delta );
		if ( DotProduct( ld_delta, ld_delta ) > gl_lowdetail->value * gl_lowdetail->value )
			currententity->backlerp = 0;
	}
	GL_DrawAliasFrameLerp (paliashdr, currententity->backlerp);
	if ( pixelmask && !flatmodel )
		R_PixelLightAlias (paliashdr, currententity->backlerp);	/* BDGN 3.55h */

	if (gl_soft_shadow_capable && !gl_reflect_pass && !flatmodel &&
	    (e->flags & RF_WEAPONMODEL) && e->lightstyle>=500 &&
	    !Q_stricmp(currentmodel->name,"models/weapons/v_chain/tris.md2"))
		R_DrawChaingunHeat(paliashdr,e->lightstyle);
	if (gl_soft_shadow_capable && !gl_reflect_pass && !flatmodel &&
	    (e->flags & RF_WEAPONMODEL) && e->lightstyle>=750 &&
	    !Q_stricmp(currentmodel->name,"models/weapons/v_machn/tris.md2"))
		R_DrawMachinegunHeat(paliashdr,e->lightstyle);	/* BDGN 3.56z */

	if ( flatmodel )
		qglEnable( GL_TEXTURE_2D );		// BDGN 3.45
	GL_TexEnv( GL_REPLACE );
	qglShadeModel (GL_FLAT);

	qglPopMatrix ();

#if 0
	qglDisable( GL_CULL_FACE );
	qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
	qglDisable( GL_TEXTURE_2D );
	qglBegin( GL_TRIANGLE_STRIP );
	for ( i = 0; i < 8; i++ )
	{
		qglVertex3fv( bbox[i] );
	}
	qglEnd();
	qglEnable( GL_TEXTURE_2D );
	qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	qglEnable( GL_CULL_FACE );
#endif

	if ( ( currententity->flags & RF_WEAPONMODEL ) && ( r_lefthand->value == 1.0F || gl_yshear_t ) )
	{
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglCullFace( GL_FRONT );
	}

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglDisable (GL_BLEND);
	}

	if (currententity->flags & RF_DEPTHHACK)
		qglDepthRange (gldepthmin, gldepthmax);

#if 1
	if (gl_shadows->value==4 && r_volume_shadows && !GL_FULLBRIGHT() && R_ShadowAllowed(e))
	{	/* BDGN 3.55ai: shadow volumes (gl_shadowvol.c); drawn for all models together after the solid ones */
		vec3_t fc, fn;
		float sdx = 0, sdy = 0, h = 1000;
		if (R_ShadowFloor (e->origin, fc, fn))
		{
			h = e->origin[2] - fc[2];
			R_LightShadowDirection (e, &sdx, &sdy);
		}
		R_ShadowVolumeAdd (e, paliashdr, s_lerped, sdx, sdy, h);
	}
	else if (gl_shadows->value>=3 && gl_soft_shadow_capable && !GL_FULLBRIGHT() && R_ShadowAllowed(e))
		R_DrawLightAliasShadow(e,paliashdr);	/* BDGN 3.55m: shadow from the strongest light (also 4 without a stencil buffer) */
	else if (gl_shadows->value==2 && gl_soft_shadow_capable && !GL_FULLBRIGHT() && R_ShadowAllowed(e))
		R_DrawSoftAliasShadow(e);
	else if (gl_shadows->value==1 && !GL_FULLBRIGHT() && R_ShadowAllowed(e))
	{
		qglPushMatrix ();
		R_RotateForEntity (e);
		qglDisable (GL_TEXTURE_2D);
		qglEnable (GL_BLEND);
		qglColor4f (0,0,0,0.5);
		GL_DrawAliasShadow (paliashdr, currententity->frame );
		qglEnable (GL_TEXTURE_2D);
		qglDisable (GL_BLEND);
		qglPopMatrix ();
	}
#endif
	qglColor4f (1,1,1,1);
}


