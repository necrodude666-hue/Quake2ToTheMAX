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
// r_main.c
#include "gl_local.h"
#define BDGN_PROF_IMPL
#include "../qcommon/bdgn_prof.h"	/* BDGN 3.52t: built-in profiler */

void R_Clear (void);

viddef_t	vid;

refimport_t	ri;

int GL_TEXTURE0, GL_TEXTURE1;

model_t		*r_worldmodel;

float		gldepthmin, gldepthmax;

glconfig_t gl_config;
glstate_t  gl_state;

image_t		*r_notexture;		// use for bad textures
image_t		*r_particletexture;	// little dot for particles

entity_t	*currententity;
model_t		*currentmodel;

cplane_t	frustum[5];
int			r_numfrustumplanes = 4;	// 4, or 5 when the draw distance (far) plane is active

// Draw distance: gl_drawdist 0 = unlimited (no fog), 1..10 = nearer and nearer (about 3100 down
// to 160 world units, roughly 5 m). Linear fog hides the cut-off.
cvar_t	*gl_drawdist;
float	r_drawdistance;
static float r_fogcolor[4] = { 0.30f, 0.30f, 0.32f, 1.0f };	/* 3.57d: black with gl_q1fog */

static float R_DrawDistanceFromCvar (void)
{
	float step = gl_drawdist->value;

	/* BDGN 3.57d: Quake 1 fog fades to black; without a draw distance it uses step 4 (about 1100 units), as the
	   software renderer */
	if ( gl_q1fog && gl_q1fog->value )
	{
		r_fogcolor[0] = r_fogcolor[1] = r_fogcolor[2] = 0;
		if ( step < 0.5f )
			step = 4;
	}
	else
	{
		r_fogcolor[0] = r_fogcolor[1] = 0.30f; r_fogcolor[2] = 0.32f;
	}
	if ( step < 0.5f )
		return 0;
	if ( step > 10 )
		step = 10;
	return 4096.0f * (float)pow( 160.0 / 4096.0, step / 10.0 );
}

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color

void GL_Strings_f( void );

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*gl_fastsky;
cvar_t	*gl_fastwater;
cvar_t *r_staticmap;
cvar_t *r_brightskins;
float r_map_animation_time;
cvar_t	*r_drawflat;
cvar_t	*gl_flatfloors;	/* BDGN 3.58w */
cvar_t	*gl_flatstate;	/* BDGN 3.59p */
/* BDGN 3.59s: video card with 4 MB at most: vid_lowvram 1 = on, 0 = off, 2 = auto (default; bdgn_vram_kb from the
   client's DirectDraw query, 0 = unknown = off). Limits the 2D sprite atlas and the 2D weapon cache to 1 MB each. */
int R_LowVram (void)
{
	static cvar_t *mode, *kb;
	if (!mode)
	{
		mode = ri.Cvar_Get ("vid_lowvram", "2", CVAR_ARCHIVE);
		kb = ri.Cvar_Get ("bdgn_vram_kb", "0", 0);
	}
	if (mode->value == 1)
		return 1;
	return mode->value == 2 && kb->value > 0 && kb->value <= 4608;
}
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*scr_scale_active;	// BDGN 3.50: 2D scale chosen by the client (menus, HUD, console)
cvar_t	*r_nocull;
cvar_t	*r_lerpmodels;
cvar_t	*r_lefthand;

cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

cvar_t	*gl_nosubimage;
cvar_t	*gl_allow_software;

cvar_t	*gl_vertex_arrays;

cvar_t	*gl_particle_min_size;
cvar_t	*gl_particle_max_size;
cvar_t	*gl_particle_size;
cvar_t	*gl_particle_att_a;
cvar_t	*gl_particle_att_b;
cvar_t	*gl_particle_att_c;

cvar_t	*gl_ext_swapinterval;
cvar_t	*gl_ext_palettedtexture;
cvar_t	*gl_ext_multitexture;
cvar_t	*gl_ext_pointparameters;
cvar_t	*gl_ext_compiled_vertex_array;

cvar_t	*gl_log;
cvar_t	*gl_bitdepth;
cvar_t	*gl_drawbuffer;
cvar_t  *gl_driver;
cvar_t	*gl_lightmap;
cvar_t	*gl_shadows;
qboolean gl_soft_shadow_capable;
cvar_t	*gl_mode;
cvar_t	*gl_dynamic;
cvar_t	*gl_dlight_cull;
cvar_t	*gl_dlight_maxdist;
cvar_t  *gl_monolightmap;
cvar_t  *gl_doomlight, *gl_blakelight, *gl_q1fog, *gl_q1particles, *gl_solidalpha;	/* BDGN 3.57d */
cvar_t	*gl_modulate;
cvar_t	*gl_3dnow;
cvar_t	*gl_amd_sky;
cvar_t	*gl_amd_angles;
cvar_t	*gl_amd_cull;
cvar_t	*gl_amd_marklights;	// BDGN 3.50
cvar_t	*gl_amd_lightpoint;
cvar_t	*gl_amd_particles;
cvar_t	*gl_sse;
cvar_t	*gl_fastsqrt;
cvar_t	*gl_fastsqrt2;
cvar_t	*gl_mmx;
cvar_t	*gl_nobind;
cvar_t	*gl_round_down;
cvar_t	*gl_picmip;
cvar_t	*gl_skymip;
cvar_t	*gl_showtris;
cvar_t	*gl_ztrick;
cvar_t	*gl_finish;
cvar_t	*gl_sortentities;	/* BDGN 3.51s */
cvar_t	*gl_clear;
cvar_t	*gl_cull;
cvar_t	*gl_polyblend;
cvar_t	*gl_flashblend;
cvar_t	*gl_playermip;
cvar_t  *gl_saturatelighting;
cvar_t	*gl_swapinterval;
cvar_t	*gl_texturemode;
cvar_t	*gl_anisotropy;
cvar_t	*gl_texturealphamode;
cvar_t	*gl_texturesolidmode;
cvar_t	*gl_lockpvs;
cvar_t	*gl_fastworld;
cvar_t	*gl_fastdraw;
cvar_t *gl_modelbatch_auto;
cvar_t *gl_particlebatch_auto;
cvar_t	*gl_fastparticles;
cvar_t	*gl_fastforce;
cvar_t	*gl_flatmodels;
cvar_t	*gl_lowdetail;
cvar_t	*gl_maxtexsize;
int		gl_vaunsafe;

cvar_t	*gl_3dlabs_broken;

// doogies code
// Q2TTM: add cvars to disable image loading (gl_hack_noimages) and texture application to world surfaces (gl_hack_notextures).
cvar_t  *gl_hack_noimages;
cvar_t  *gl_hack_notextures;
//

cvar_t	*vid_fullscreen;
cvar_t	*vid_gamma;
cvar_t	*vid_ref;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
// frustum planes flattened for the 3DNow! culling kernels (filled in R_SetFrustum)
float			q3d_frustum[20];
static int		q3d_frustum_signbits[5];

/*
=================
R_AngleVectors

AngleVectors for the refresh: the 3DNow! version (AMD patch, area mesh M1) when gl_amd_angles is set
=================
*/
void R_AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	if ( q3d_use_3dnow && gl_amd_angles->value )
		Q3N_AngleVectors( angles, forward, right, up );
	else
		AngleVectors( angles, forward, right, up );
}

qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	if (r_nocull->value)
		return false;

	if ( GL_3DNOW() )
		return Q3D_CullBox( mins, maxs, q3d_frustum, q3d_frustum_signbits, r_numfrustumplanes );

	for (i=0 ; i<r_numfrustumplanes ; i++)
		if ( BOX_ON_PLANE_SIDE(mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void R_RotateForEntity (entity_t *e)
{
    qglTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

    qglRotatef (e->angles[1],  0, 0, 1);
    qglRotatef (-e->angles[0],  0, 1, 0);
    qglRotatef (-e->angles[2],  1, 0, 0);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/


/* BDGN 3.52r: the light at a 2D sprite's origin (world lightmap + dynamic lights, with gl_modulate), clamped to
   full brightness and to a floor so a sprite never turns black */
void R_SpriteLightColor (vec3_t origin, byte rgb[3])
{
	vec3_t	c;
	int		i, v;
	R_LightPoint( origin, c );
	for ( i = 0; i < 3; i++ )
	{
		v = (int)( c[i] * 255 );
		rgb[i] = (byte)( v < 40 ? 40 : v > 255 ? 255 : v );
	}
}

/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	float alpha = 1.0F;
	vec3_t	point;
	dsprframe_t	*frame;
	float		*up, *right;
	dsprite_t		*psprite;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache

	psprite = (dsprite_t *)currentmodel->extradata;

#if 0
	if (e->frame < 0 || e->frame >= psprite->numframes)
	{
		ri.Con_Printf (PRINT_ALL, "no such sprite frame %i\n", e->frame);
		e->frame = 0;
	}
#endif
	e->frame %= psprite->numframes;

	frame = &psprite->frames[e->frame];

#if 0
	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
	vec3_t		v_forward, v_right, v_up;

	AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
#endif
	{	// normal sprite
		up = vup;
		right = vright;
	}

	if ( e->flags & RF_TRANSLUCENT )
		alpha = e->alpha;

	if ( alpha != 1.0F )
		qglEnable( GL_BLEND );

	if ((e->flags & RF_SIMPLEBRIGHT) && e->skin)
	{
		byte color[3];
		GL_BrightSkinColor (e->skin, color);
		qglColor4f (color[0]*(1.0f/255), color[1]*(1.0f/255), color[2]*(1.0f/255), alpha);
	}
	else if ( e->flags & RF_LITSPRITE )
	{	/* BDGN 3.52r: sprite lighting */
		byte color[3];
		R_SpriteLightColor( e->origin, color );
		qglColor4f (color[0]*(1.0f/255), color[1]*(1.0f/255), color[2]*(1.0f/255), alpha);
	}
	else
		qglColor4f( 1, 1, 1, alpha );

    GL_Bind(currentmodel->skins[e->frame]->texnum);

	GL_TexEnv( GL_MODULATE );

	if ( alpha == 1.0 )
		qglEnable (GL_ALPHA_TEST);
	else
		qglDisable( GL_ALPHA_TEST );

	qglBegin (GL_QUADS);

	qglTexCoord2f (currentmodel->skins[e->frame]->sl, currentmodel->skins[e->frame]->th);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (currentmodel->skins[e->frame]->sl, currentmodel->skins[e->frame]->tl);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, -frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (currentmodel->skins[e->frame]->sh, currentmodel->skins[e->frame]->tl);
	VectorMA (e->origin, frame->height - frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);

	qglTexCoord2f (currentmodel->skins[e->frame]->sh, currentmodel->skins[e->frame]->th);
	VectorMA (e->origin, -frame->origin_y, up, point);
	VectorMA (point, frame->width - frame->origin_x, right, point);
	qglVertex3fv (point);
	
	qglEnd ();

	qglDisable (GL_ALPHA_TEST);
	GL_TexEnv( GL_REPLACE );

	if ( alpha != 1.0F )
		qglDisable( GL_BLEND );

	qglColor4f( 1, 1, 1, 1 );
}

//==================================================================================

/*
=============
R_DrawNullModel
=============
*/
void R_DrawNullModel (void)
{
	vec3_t	shadelight;
	int		i;

	if ( (currententity->flags & RF_FULLBRIGHT) || GL_FULLBRIGHT() )
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	else
		R_LightPoint (currententity->origin, shadelight);

    qglPushMatrix ();
	R_RotateForEntity (currententity);

	qglDisable (GL_TEXTURE_2D);
	qglColor3fv (shadelight);

	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, -16);
	for (i=0 ; i<=4 ; i++)
		qglVertex3f (16*cos(i*M_PI/2), 16*sin(i*M_PI/2), 0);
	qglEnd ();

	qglBegin (GL_TRIANGLE_FAN);
	qglVertex3f (0, 0, 16);
	for (i=4 ; i>=0 ; i--)
		qglVertex3f (16*cos(i*M_PI/2), 16*sin(i*M_PI/2), 0);
	qglEnd ();

	qglColor3f (1,1,1);
	qglPopMatrix ();
	qglEnable (GL_TEXTURE_2D);
}

/*
=============
R_DrawEntitiesOnList
=============
*/
/* BDGN 3.51s: opaque alias models (and pre-rendered enemies) are drawn grouped by model and skin, so the same
   texture / atlas page is bound once per group instead of once per entity. Opaque order does not change the picture
   (depth buffer); the view weapon, beams, brush and sprite models keep their order and are drawn first.
   Automatic (gl_sortentities 1); 0 = the original order. */
static int R_EntitySortKeyLess (const entity_t *a, const entity_t *b)
{
	if (a->model != b->model)
		return (size_t)a->model < (size_t)b->model;
	if (a->skin != b->skin)
		return (size_t)a->skin < (size_t)b->skin;
	return a->skinnum < b->skinnum;
}

/* BDGN 3.52t: profiler area of an entity */
static int R_EntityProfArea (entity_t *e, int type)
{	/* only called while profiling (inside BDGN_PROF_CALL); 3.52v: also counts what is drawn */
	int sprite = type == mod_sprite || ( e->flags & ( RF_DUKEANIM | RF_WEAPONSPRITE ) );
	if ( type != mod_brush )
		bdgn_prof_n[sprite ? PROFN_SPRITES : PROFN_MODELS]++;
	if ( e->flags & ( RF_WEAPONMODEL | RF_WEAPONSPRITE ) )
		return PROF_WEAPON;
	if ( sprite )
		return PROF_SPRITES;
	if ( type == mod_brush )
		return PROF_WORLD;
	return PROF_MODELS;
}

static void R_DrawOpaqueEntity (void)
{
	if ( currententity->flags & RF_BEAM )
	{
		R_DrawBeam( currententity );
		return;
	}
	currentmodel = currententity->model;
	if (!currentmodel)
	{
		R_DrawNullModel ();
		return;
	}
	switch (currentmodel->type)
	{
	case mod_alias:
		BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_alias ), R_DrawAliasModel (currententity) );
		break;
	case mod_brush:
		BDGN_PROF_CALL( PROF_WORLD, R_DrawBrushModel (currententity) );
		break;
	case mod_sprite:
		BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_sprite ), R_DrawSpriteModel (currententity) );
		break;
	default:
		ri.Sys_Error (ERR_DROP, "Bad modeltype");
		break;
	}
}

void R_DrawEntitiesOnList (void)
{
	int		i, j, nsorted = 0;
	entity_t	*sorted[MAX_ENTITIES], *e;
	int deferred=(GL_DistortWorldEnabled() || GL_ModernNeedsCleanWorld())?(RF_WEAPONMODEL|RF_DEPTHHACK):0;

	if (!r_drawentities->value)
		return;

	if (gl_sortentities->value)
	{
		for (i=0 ; i<r_newrefdef.num_entities ; i++)
		{
			e = &r_newrefdef.entities[i];
			if (e->flags & (RF_TRANSLUCENT|deferred))
				continue;
			if (!(e->flags & (RF_BEAM | RF_WEAPONMODEL)) && e->model && e->model->type == mod_alias && nsorted < MAX_ENTITIES)
			{	// insertion sort: at most 128 entities, mostly already grouped
				for (j = nsorted; j > 0 && R_EntitySortKeyLess (e, sorted[j-1]); j--)
					sorted[j] = sorted[j-1];
				sorted[j] = e;
				nsorted++;
				continue;
			}
			currententity = e;
			R_DrawOpaqueEntity ();
		}
		for (i=0 ; i<nsorted ; i++)
		{
			currententity = sorted[i];
			R_DrawOpaqueEntity ();
		}
	}
	else
	// draw non-transparent first
	for (i=0 ; i<r_newrefdef.num_entities ; i++)
	{
		currententity = &r_newrefdef.entities[i];
		if (currententity->flags & (RF_TRANSLUCENT|deferred))
			continue;	// opaque pass

		if ( currententity->flags & RF_BEAM )
		{
			R_DrawBeam( currententity );
		}
		else
		{
			currentmodel = currententity->model;
			if (!currentmodel)
			{
				R_DrawNullModel ();
				continue;
			}
			switch (currentmodel->type)
			{
			case mod_alias:
				BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_alias ), R_DrawAliasModel (currententity) );
				break;
			case mod_brush:
				BDGN_PROF_CALL( PROF_WORLD, R_DrawBrushModel (currententity) );
				break;
			case mod_sprite:
				BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_sprite ), R_DrawSpriteModel (currententity) );
				break;
			default:
				ri.Sys_Error (ERR_DROP, "Bad modeltype");
				break;
			}
		}
	}

	BDGN_PROF_CALL( PROF_SPRITES, R_DukeFlush () );	// BDGN 3.52h: the queued solid classic sprites, by atlas page

	GL_DrawReliefModels ();	/* BDGN 3.55aj: relief on doors and lifts, after the solid entities, before marks and translucent ones */
	R_DrawDecals ();	/* BDGN 3.55y: marks on the world, over the opaque scene */

	R_ShadowVolumeDraw ();	/* BDGN 3.55ai: shadow volumes of the solid models, before the translucent ones */

 GL_ModernOpaqueDepth();

	/* Smoke sits behind the translucent BFG explosion even when their models sort differently. */
	qglDepthMask(0);
	for (i=0;i<r_newrefdef.num_entities;i++)
	{
		currententity=&r_newrefdef.entities[i];
		if ((currententity->flags&RF_PUFFS) && currententity->lightstyle==FX_LAYER_BFG_SMOKE)
			R_DrawOpaqueEntity();
	}
	// draw transparent entities
	// we could sort these if it ever becomes a problem...
	qglDepthMask (0);		// no z writes
	for (i=0 ; i<r_newrefdef.num_entities ; i++)
	{
		currententity = &r_newrefdef.entities[i];
		if (!(currententity->flags & RF_TRANSLUCENT) || (currententity->flags & deferred))
			continue;
		if ((currententity->flags&RF_PUFFS) && currententity->lightstyle==FX_LAYER_BFG_SMOKE)
			continue;
		if ((currententity->flags&RF_ACTOR_MARK)==RF_ACTOR_MARK)
		{
			R_DrawActorMark(currententity);
			continue;
		}
		if (currententity->flags & RF_WATER_RIPPLE)
			continue;
		if (GL_DistortQueue (currententity))
			continue;	/* BDGN 3.55t: heat distortion, drawn after the translucent surfaces */
		if (currententity->flags & RF_DECAL)
			continue;	/* BDGN 3.55y: drawn by R_DrawDecals */
		if (R_DrawFXBeam (currententity))
			continue;	/* BDGN 3.55y: textured effect beam */

		if ( currententity->flags & RF_BEAM )
		{
			R_DrawBeam( currententity );
		}
		else
		{
			currentmodel = currententity->model;

			if (!currentmodel)
			{
				R_DrawNullModel ();
				continue;
			}
			switch (currentmodel->type)
			{
			case mod_alias:
				BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_alias ), R_DrawAliasModel (currententity) );
				break;
			case mod_brush:
				BDGN_PROF_CALL( PROF_WORLD, R_DrawBrushModel (currententity) );
				break;
			case mod_sprite:
				BDGN_PROF_CALL( R_EntityProfArea( currententity, mod_sprite ), R_DrawSpriteModel (currententity) );
				break;
			default:
				ri.Sys_Error (ERR_DROP, "Bad modeltype");
				break;
			}
		}
	}
	qglDepthMask (1);		// back to writing

}

static void R_DrawViewWeaponAfterDistortion(void)
{
 int i, pass;
 if ((!GL_DistortWorldEnabled() && !GL_ModernNeedsCleanWorld()) || !r_drawentities->value) return;
 for (pass=0;pass<2;pass++) {
  qglDepthMask(!pass);
  for (i=0;i<r_newrefdef.num_entities;i++) {
   currententity=&r_newrefdef.entities[i];
   if (!(currententity->flags&(RF_WEAPONMODEL|RF_DEPTHHACK)) ||
       (currententity->flags&RF_DISTORT) || !!(currententity->flags&RF_TRANSLUCENT)!=pass)
    continue;
   R_DrawOpaqueEntity();
  }
 }
 qglDepthMask(1);
 R_DukeFlush();
}

/*
** GL_DrawParticles
**
*/
void GL_DrawParticles( int num_particles, const particle_t particles[], const unsigned colortable[768] )
{
	const particle_t *p;
	int				i;
	vec3_t			up, right;
	float			scale;
	byte			color[4];

    GL_Bind(r_particletexture->texnum);
	qglDepthMask( GL_FALSE );		// no z buffering
	qglEnable( GL_BLEND );
	GL_TexEnv( GL_MODULATE );
	qglBegin( GL_TRIANGLES );

	VectorScale (vup, 1.5, up);
	VectorScale (vright, 1.5, right);

	if ( q3d_use_3dnow && gl_amd_particles->value && num_particles <= MAX_PARTICLES )
	{	// BDGN 3.50: as AMD, the scale, corners and colours of all particles with 3DNow! first, then only GL calls
		static q3n_ptri_t	tris[MAX_PARTICLES];
		const q3n_ptri_t	*t;
		typedef char		q3n_particlecheck[ sizeof(particle_t) == sizeof(q3n_particle_t) ? 1 : -1 ];

		Q3N_ParticleTris( num_particles, (const q3n_particle_t *)particles, colortable, r_origin, vpn, up, right, tris );
		for ( p = particles, t = tris, i = 0 ; i < num_particles ; i++, p++, t++ )
		{
			qglColor4ubv( (const GLubyte *)&t->color );

			qglTexCoord2f( 0.0625, 0.0625 );
			qglVertex3fv( p->origin );

			qglTexCoord2f( 1.0625, 0.0625 );
			qglVertex3fv( t->v1 );

			qglTexCoord2f( 0.0625, 1.0625 );
			qglVertex3fv( t->v2 );
		}
	}
	else
	for ( p = particles, i=0 ; i < num_particles ; i++,p++)
	{
		// hack a scale up to keep particles from disapearing
		scale = ( p->origin[0] - r_origin[0] ) * vpn[0] + 
			    ( p->origin[1] - r_origin[1] ) * vpn[1] +
			    ( p->origin[2] - r_origin[2] ) * vpn[2];

		if (scale < 20)
			scale = 1;
		else
			scale = 1 + scale * 0.004;

		*(int *)color = colortable[p->color];
		color[3] = p->alpha*255;

		qglColor4ubv( color );

		qglTexCoord2f( 0.0625, 0.0625 );
		qglVertex3fv( p->origin );

		qglTexCoord2f( 1.0625, 0.0625 );
		qglVertex3f( p->origin[0] + up[0]*scale, 
			         p->origin[1] + up[1]*scale, 
					 p->origin[2] + up[2]*scale);

		qglTexCoord2f( 0.0625, 1.0625 );
		qglVertex3f( p->origin[0] + right[0]*scale, 
			         p->origin[1] + right[1]*scale, 
					 p->origin[2] + right[2]*scale);
	}

	qglEnd ();
	qglDisable( GL_BLEND );
	qglColor4f( 1,1,1,1 );
	qglDepthMask( 1 );		// back to normal Z buffering
	GL_TexEnv( GL_REPLACE );
}

/*
===============
R_DrawParticles
===============
*/
/* BDGN 3.57d: Quake 1 particles for OpenGL (gl_q1particles): small solid triangles without texture and without
   blending, at most 256 per frame (more are thinned out evenly). Saves the texture read and the blend per pixel. */
static void R_DrawQ1Particles (void)
{
	const particle_t *p;
	int i, step = 1, n = r_newrefdef.num_particles;
	vec3_t up, right;
	float scale;
	byte color[4];

	if ( n <= 0 )
		return;
	while ( n / step > 256 )
		step++;
	VectorScale (vup, 1.5, up);
	VectorScale (vright, 1.5, right);
	qglDisable( GL_TEXTURE_2D );
	qglBegin( GL_TRIANGLES );
	for ( i = 0, p = r_newrefdef.particles ; i < n ; i += step, p += step )
	{
		scale = ( p->origin[0] - r_origin[0] ) * vpn[0] + ( p->origin[1] - r_origin[1] ) * vpn[1] +
		        ( p->origin[2] - r_origin[2] ) * vpn[2];
		scale = scale < 20 ? 1 : 1 + scale * 0.004f;
		*(int *)color = d_8to24table[p->color & 255];
		color[3] = 255;
		qglColor4ubv( color );
		qglVertex3fv( p->origin );
		qglVertex3f( p->origin[0] + up[0]*scale, p->origin[1] + up[1]*scale, p->origin[2] + up[2]*scale );
		qglVertex3f( p->origin[0] + right[0]*scale, p->origin[1] + right[1]*scale, p->origin[2] + right[2]*scale );
	}
	qglEnd();
	qglColor4f( 1,1,1,1 );
	qglEnable( GL_TEXTURE_2D );
}

void R_DrawParticles (void)
{
	if ( gl_q1particles->value )
	{
		R_DrawQ1Particles ();
		return;
	}
	if ( gl_ext_pointparameters->value && qglPointParameterfEXT )
	{
		int i;
		unsigned char color[4];
		const particle_t *p;

		qglDepthMask( GL_FALSE );
		qglEnable( GL_BLEND );
		qglDisable( GL_TEXTURE_2D );

		qglPointSize( gl_particle_size->value );

		if ( GL_FASTPARTICLES() && ( !gl_vaunsafe || gl_fastforce->value ) && qglDrawArrays && qglVertexPointer && qglColorPointer
			&& qglEnableClientState && qglDisableClientState
			&& r_newrefdef.num_particles > 0 && r_newrefdef.num_particles <= MAX_PARTICLES )
		{
			// BDGN 3.32: one DrawArrays call instead of two GL calls per particle
			static float			pverts[MAX_PARTICLES*3];
			static unsigned char	pcols[MAX_PARTICLES*4];

			if ( q3d_use_3dnow && gl_amd_particles->value )	// BDGN 3.50: colour + alpha with 3DNow!, positions as integers
				Q3N_ParticleArrays( r_newrefdef.num_particles, (const q3n_particle_t *)r_newrefdef.particles, d_8to24table, pverts, pcols );
			else
			for ( i = 0, p = r_newrefdef.particles; i < r_newrefdef.num_particles; i++, p++ )
			{
				*(int *)(pcols + i*4) = d_8to24table[p->color];
				pcols[i*4+3] = p->alpha*255;
				pverts[i*3+0] = p->origin[0];
				pverts[i*3+1] = p->origin[1];
				pverts[i*3+2] = p->origin[2];
			}

			qglEnableClientState( GL_VERTEX_ARRAY );
			qglEnableClientState( GL_COLOR_ARRAY );
			qglVertexPointer( 3, GL_FLOAT, 0, pverts );
			qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, pcols );
			qglDrawArrays( GL_POINTS, 0, r_newrefdef.num_particles );
			qglDisableClientState( GL_COLOR_ARRAY );
			qglDisableClientState( GL_VERTEX_ARRAY );
		}
		else
		{
		qglBegin( GL_POINTS );
		for ( i = 0, p = r_newrefdef.particles; i < r_newrefdef.num_particles; i++, p++ )
		{
			*(int *)color = d_8to24table[p->color];
			color[3] = p->alpha*255;

			qglColor4ubv( color );

			qglVertex3fv( p->origin );
		}
		qglEnd();
		}

		qglDisable( GL_BLEND );
		qglColor4f( 1.0F, 1.0F, 1.0F, 1.0F );
		qglDepthMask( GL_TRUE );
		qglEnable( GL_TEXTURE_2D );

	}
	else
	{
		GL_DrawParticles( r_newrefdef.num_particles, r_newrefdef.particles, d_8to24table );
	}
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (!gl_polyblend->value)
		return;
	if (!v_blend[3])
		return;

	qglDisable (GL_ALPHA_TEST);
	qglEnable (GL_BLEND);
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

    qglLoadIdentity ();

	// FIXME: get rid of these
    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

	qglColor4fv (v_blend);

	qglBegin (GL_QUADS);

	qglVertex3f (10, 100, 100);
	qglVertex3f (10, -100, 100);
	qglVertex3f (10, -100, -100);
	qglVertex3f (10, 100, -100);
	qglEnd ();

	qglDisable (GL_BLEND);
	qglEnable (GL_TEXTURE_2D);
	qglEnable (GL_ALPHA_TEST);

	qglColor4f(1,1,1,1);
}

//=======================================================================

int SignbitsForPlane (cplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

#if 0
	/*
	** this code is wrong, since it presume a 90 degree FOV both in the
	** horizontal and vertical plane
	*/
	// front side is visible
	VectorAdd (vpn, vright, frustum[0].normal);
	VectorSubtract (vpn, vright, frustum[1].normal);
	VectorAdd (vpn, vup, frustum[2].normal);
	VectorSubtract (vpn, vup, frustum[3].normal);

	// we theoretically don't need to normalize these vectors, but I do it
	// anyway so that debugging is a little easier
	VectorNormalize( frustum[0].normal );
	VectorNormalize( frustum[1].normal );
	VectorNormalize( frustum[2].normal );
	VectorNormalize( frustum[3].normal );
#else
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_newrefdef.fov_x / 2 ) );
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_newrefdef.fov_x / 2 );
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_newrefdef.fov_y / 2 );
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_newrefdef.fov_y / 2 ) );
	if (gl_yshear_t)
	{	/* BDGN 3.60i: Duke Nukem 3D view: the top and bottom edges of the moved window (elevations et, eb) */
		float th = (float)tan (r_newrefdef.fov_y * M_PI / 360.0), et, eb;
		et = (float)atan (th - gl_yshear_t);
		eb = (float)atan (-th - gl_yshear_t);
		for (i=0 ; i<3 ; i++)
		{
			frustum[2].normal[i] = (float)sin (et) * vpn[i] - (float)cos (et) * vup[i];
			frustum[3].normal[i] = -(float)sin (eb) * vpn[i] + (float)cos (eb) * vup[i];
		}
	}
#endif

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);

		q3d_frustum[i*4+0] = frustum[i].normal[0];
		q3d_frustum[i*4+1] = frustum[i].normal[1];
		q3d_frustum[i*4+2] = frustum[i].normal[2];
		q3d_frustum[i*4+3] = frustum[i].dist;
		q3d_frustum_signbits[i] = frustum[i].signbits;
	}

	// far plane for the draw distance setting
	r_drawdistance = ( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) ? 0 : R_DrawDistanceFromCvar();
	r_numfrustumplanes = 4;
	if ( r_drawdistance > 0 )
	{
		// normal points back at the viewer, so "in front" of the plane means nearer than the distance
		VectorSubtract( vec3_origin, vpn, frustum[4].normal );
		frustum[4].type = PLANE_ANYZ;
		frustum[4].dist = DotProduct( r_origin, frustum[4].normal ) - r_drawdistance;
		frustum[4].signbits = SignbitsForPlane( &frustum[4] );

		q3d_frustum[16] = frustum[4].normal[0];
		q3d_frustum[17] = frustum[4].normal[1];
		q3d_frustum[18] = frustum[4].normal[2];
		q3d_frustum[19] = frustum[4].dist;
		q3d_frustum_signbits[4] = frustum[4].signbits;
		r_numfrustumplanes = 5;
	}
}

//=======================================================================

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int i;
	mleaf_t	*leaf;

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_newrefdef.vieworg, r_origin);

	R_AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

// current viewcluster
	if ( !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{	// look down a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
		else
		{	// look up a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
	}

	for (i=0 ; i<4 ; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

	// clear out the portion of the screen that the NOWORLDMODEL defines
	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
	{
		qglEnable( GL_SCISSOR_TEST );
		qglClearColor( 0.3, 0.3, 0.3, 1 );
		qglScissor( r_newrefdef.x, vid.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height );
		qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		qglClearColor (0, 0, 0, 1);
		qglDisable( GL_SCISSOR_TEST );
	}
}


#include "../qcommon/yshear.h"	/* BDGN 3.60i: the Duke Nukem 3D view */
float gl_yshear_t;	/* tan of the view pitch drawn as a moved horizon (0: off); set in R_RenderView */

void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

   /* BDGN 3.60i: Duke Nukem 3D view: the window moves down when looking down (the view itself stays level) */
   ymin -= zNear * gl_yshear_t;
   ymax -= zNear * gl_yshear_t;

   xmin += -( 2 * gl_state.camera_separation ) / zNear;
   xmax += -( 2 * gl_state.camera_separation ) / zNear;

   qglFrustum( xmin, xmax, ymin, ymax, zNear, zFar );
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	float	screenaspect;
//	float	yfov;
	int		x, x2, y2, y, w, h;

	//
	// set up viewport
	//
	x = floor(r_newrefdef.x * vid.width / vid.width);
	x2 = ceil((r_newrefdef.x + r_newrefdef.width) * vid.width / vid.width);
	y = floor(vid.height - r_newrefdef.y * vid.height / vid.height);
	y2 = ceil(vid.height - (r_newrefdef.y + r_newrefdef.height) * vid.height / vid.height);

	w = x2 - x;
	h = y - y2;

	if (GL_ScaleOffscreen())
		qglViewport(0,0,w,h);
	else
		qglViewport (x, y2, w, h);

	//
	// set up projection matrix
	//
    screenaspect = (float)r_newrefdef.width/r_newrefdef.height;
//	yfov = 2*atan((float)r_newrefdef.height/r_newrefdef.width)*180/M_PI;
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
    MYgluPerspective (r_newrefdef.fov_y,  screenaspect,  4,  ( r_drawdistance > 0 ) ? r_drawdistance + 32 : 4096);

	qglCullFace(GL_FRONT);

	// fog hides the draw distance cut-off
	if ( r_drawdistance > 0 )
	{
		qglFogi( GL_FOG_MODE, GL_LINEAR );
		qglFogf( GL_FOG_START, r_drawdistance * 0.35f );
		qglFogf( GL_FOG_END, r_drawdistance );
		qglFogfv( GL_FOG_COLOR, r_fogcolor );
		qglEnable( GL_FOG );
	}
	else
		qglDisable( GL_FOG );

	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();

    qglRotatef (-90,  1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up
    qglRotatef (-r_newrefdef.viewangles[2],  1, 0, 0);
    qglRotatef (-r_newrefdef.viewangles[0],  0, 1, 0);
    qglRotatef (-r_newrefdef.viewangles[1],  0, 0, 1);
    qglTranslatef (-r_newrefdef.vieworg[0],  -r_newrefdef.vieworg[1],  -r_newrefdef.vieworg[2]);

//	if ( gl_state.camera_separation != 0 && gl_state.stereo_enabled )
//		qglTranslatef ( gl_state.camera_separation, 0, 0 );

	qglGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix);

	//
	// set drawing parms
	//
	if (gl_cull->value)
		qglEnable(GL_CULL_FACE);
	else
		qglDisable(GL_CULL_FACE);

	qglDisable(GL_BLEND);
	qglDisable(GL_ALPHA_TEST);
	qglEnable(GL_DEPTH_TEST);
}

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	// with a draw distance the sky is not drawn, so the background has to be the fog colour
	int fogclear = ( R_DrawDistanceFromCvar() > 0 );

	if ( fogclear )
		qglClearColor( r_fogcolor[0], r_fogcolor[1], r_fogcolor[2], 1 );

	if (gl_ztrick->value)
	{
		static int trickframe;

		if (gl_clear->value || fogclear)
			qglClear (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			qglDepthFunc (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			qglDepthFunc (GL_GEQUAL);
		}
	}
	else
	{
		if (gl_clear->value || fogclear)
			qglClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			qglClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		qglDepthFunc (GL_LEQUAL);
	}

	if ( fogclear )
		qglClearColor (0, 0, 0, 1);

	qglDepthRange (gldepthmin, gldepthmax);

}

void R_Flash( void )
{
	R_PolyBlend ();
}

/*
================
R_RenderReflection

BDGN 3.55o: the mirrored view for the water reflection (see gl_render_scale.c GL_ReflectBegin). Visibility (PVS)
from the real eye of the last frame, so the area seen above the water is drawn.
================
*/
static void R_RenderReflection (void)
{
	static entity_t ents[MAX_ENTITIES];
	refdef_t saved=r_newrefdef;
	int i,n=0,cluster=r_viewcluster,cluster2=r_viewcluster2;
	float h, shear=gl_yshear_t;
	GLdouble plane[4];
	if (!GL_ReflectBegin(&h))
		return;
	gl_yshear_t=-shear;	/* BDGN 3.60i: the mirrored view looks the other way */
	for (i=0;i<r_newrefdef.num_entities && n<MAX_ENTITIES;i++)
		if (!(r_newrefdef.entities[i].flags & (RF_WEAPONMODEL|RF_WEAPONSPRITE|RF_DEPTHHACK)))
			ents[n++]=r_newrefdef.entities[i];
	r_newrefdef.entities=ents;
	r_newrefdef.num_entities=n;
	r_newrefdef.vieworg[2]=2*h-r_newrefdef.vieworg[2];
	r_newrefdef.viewangles[0]=-r_newrefdef.viewangles[0];
	r_newrefdef.viewangles[2]=-r_newrefdef.viewangles[2];
	gl_reflect_pass=true;
	gl_pixel_lightmask=0;
	R_SetupFrame ();
	r_viewcluster=cluster; r_viewcluster2=cluster2;	/* the real eye's PVS */
	R_SetFrustum ();
	R_SetupGL ();
	GL_ReflectViewport ();
	plane[0]=0; plane[1]=0; plane[2]=1; plane[3]=-(h+0.5);
	qglClipPlane (GL_CLIP_PLANE0,plane);
	qglEnable (GL_CLIP_PLANE0);
	R_MarkLeaves ();
	R_DrawWorld ();
	R_DrawEntitiesOnList ();
	/* BDGN 3.56b: the mirrored world walk also chains its water and glass surfaces into r_alpha_surfaces, and only
	   R_DrawAlphaSurfaces empties that list (it is not called here).  The main view then added its own surfaces to
	   the same list: a surface seen in both views made the list loop back on itself (R_DrawAlphaSurfaces never ended:
	   the game hung, "not responding"), and surfaces of the mirrored view (the underside of the water) were drawn in
	   the main view over the real water.  They are not part of the reflection either (below the clip plane or never
	   drawn), so the list is dropped. */
	R_AlphaChainClear (&r_alpha);
	qglDisable (GL_CLIP_PLANE0);
	gl_reflect_pass=false;
	GL_ReflectEnd ();
	r_newrefdef=saved;
	gl_yshear_t=shear;
}

/*
================
R_RenderView

r_newrefdef must be set before the first call
================
*/
void R_RenderView (refdef_t *fd)
{
	if (r_norefresh->value)
		return;

	BDGN_PROF_PUSH( PROF_OTHER );	/* BDGN 3.52t: everything below that has no area of its own */
	R_ValidateEntities (fd);	/* BDGN 3.52s: no entity with a foreign model pointer */
	r_newrefdef = *fd;
	gl_yshear_t = R_YShearTake (r_newrefdef.viewangles, r_newrefdef.fov_y, r_newrefdef.rdflags);	/* BDGN 3.60i */
	r_map_animation_time = r_staticmap->value ? 0 : r_newrefdef.time;

	// BDGN 3.32: "dynamic lights" off = no flashes from weapons / explosions at all
	if ( !gl_dynamic->value )
		r_newrefdef.num_dlights = 0;

	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		BDGN_PROF_POP();
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");
	}

	if (r_speeds->value)
	{
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	if (gl_finish->value)
		qglFinish ();

	GL_DistortBeginFrame ();	/* BDGN 3.55t */
	BDGN_PROF_CALL( PROF_WORLD, R_RenderReflection () );	/* BDGN 3.55o: water reflection, before the main view */

	R_SetupFrame ();
	R_ShadowVolumeFrame ();	/* BDGN 3.55ai: after the reflection pass, with the scene's frame buffer bound */

	R_SetFrustum ();
	GL_SelectPixelLight();

	BDGN_PROF_CALL( PROF_LIGHT, R_PushDlights () );	// BDGN 3.51p: after the frustum, so lights that cannot reach the view are skipped (gl_light.c)

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	BDGN_PROF_CALL( PROF_WORLD, R_DrawWorld () );
	if (r_drawworld->value) BDGN_PROF_CALL(PROF_WORLD, GL_DrawRelief());	/* BDGN 3.55ah: 3D surface structure, before the light passes */
	if (r_drawworld->value) BDGN_PROF_CALL(PROF_LIGHT, GL_DrawPixelLight());

	R_DrawEntitiesOnList ();	/* BDGN 3.52t: areas per entity inside */

	BDGN_PROF_CALL( PROF_LIGHT, R_RenderDlights () );

	BDGN_PROF_CALL( PROF_PARTICLES, R_DrawParticles () );

	BDGN_PROF_CALL( PROF_ALPHA, R_DrawAlphaSurfaces () );

	BDGN_PROF_CALL( PROF_POST, GL_DrawDistortion () );	/* BDGN 3.55t: heat distortion */
 GL_ModernEnvironment();
	R_DrawViewWeaponAfterDistortion();
	BDGN_PROF_CALL( PROF_POST, GL_DrawGodRays () );	/* BDGN 3.55ak: light shafts from the sun */

	BDGN_PROF_CALL( PROF_POST, R_Flash() );

	if (r_speeds->value)
	{
		ri.Con_Printf (PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n",
			c_brush_polys, 
			c_alias_polys, 
			c_visible_textures, 
			c_visible_lightmaps); 
	}
	BDGN_PROF_N( PROFN_SURFS, c_brush_polys );	/* BDGN 3.52v: frame counters */
	BDGN_PROF_N( PROFN_PARTICLES, r_newrefdef.num_particles );
	BDGN_PROF_N( PROFN_DLIGHTS, r_newrefdef.num_dlights );
	BDGN_PROF_N( PROFN_ENTITIES, r_newrefdef.num_entities );
	BDGN_PROF_POP();
}

/* BDGN 3.52t: EndFrame with the swap measured, then the profiler's frame end */
static void R_ProfEndFrame (void)
{
	BDGN_PROF_CALL( PROF_SWAP, GLimp_EndFrame () );
	BDGN_ProfFrame ();
}


/*
** R_Set2DProjection (BDGN 3.50): the client lays 2D out on the real screen divided by scr_scale_active
** (a whole number), so 2D coordinates are scaled up by exactly that factor. Viewport is the whole window.
*/
static float R_Scale2D (void)
{
	float	scale = scr_scale_active ? scr_scale_active->value : 1;
	return scale < 1 ? 1 : scale;
}

static void R_Set2DProjection (void)
{
	float	scale = R_Scale2D ();
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	qglOrtho  (0, vid.width / scale, vid.height / scale, 0, -99999, 99999);
}

/*
** R_Clear2DMargin (BDGN 3.50): when the window is not a multiple of the 2D scale, the client's 2D screen
** (window / scale, rounded down) leaves a strip of up to scale-1 pixels at the right and bottom that no
** 2D drawing covers. Clear it to black each frame; an in-game 3D view is stretched over it anyway.
*/
static void R_Clear2DMargin (void)
{
	int		scale = (int)R_Scale2D ();
	int		width2d = (vid.width / scale) * scale, height2d = (vid.height / scale) * scale;
	if (width2d >= (int)vid.width && height2d >= (int)vid.height)
		return;
	qglClearColor (0, 0, 0, 1);
	qglEnable (GL_SCISSOR_TEST);
	if (width2d < (int)vid.width)
	{
		qglScissor (width2d, 0, vid.width - width2d, vid.height);
		qglClear (GL_COLOR_BUFFER_BIT);
	}
	if (height2d < (int)vid.height)
	{
		qglScissor (0, 0, vid.width, vid.height - height2d);	// GL rows count from the bottom
		qglClear (GL_COLOR_BUFFER_BIT);
	}
	qglDisable (GL_SCISSOR_TEST);
	qglClearColor (0, 0, 0, 1);		// the default clear colour (GL_SetDefaultState)
}

void	R_SetGL2D (void)
{
	qglDisable (GL_FOG);
	// set 2D virtual screen size
	R_Set2DProjection ();
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	qglEnable (GL_ALPHA_TEST);
	qglColor4f (1,1,1,1);
}

/*
====================
R_SetLightLevel

====================
*/
void R_SetLightLevel (void)
{
	vec3_t		shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	// save off light value for server to look at (BIG HACK!)

	R_LightPoint (r_newrefdef.vieworg, shadelight);

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

/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame

@@@@@@@@@@@@@@@@@@@@@
*/
void R_RenderFrame (refdef_t *fd)
{
	refdef_t scaled_view;
	GL_ResetWaterDrawCount();
	if (!r_norefresh->value && GL_ScaleBegin(fd, &scaled_view, R_Scale2D(),
		R_DrawDistanceFromCvar()>0?r_fogcolor:NULL)) {
		R_RenderView(&scaled_view);
		R_SetGL2D();
		if (GL_ScalePresent(&scaled_view, fd, R_Scale2D())) {
			R_SetLightLevel();
			return;
		}
		/* A driver can accept the scaled_view probe and reject the full copy. */
		qglClear(GL_DEPTH_BUFFER_BIT);
	}
	R_RenderView(fd);
	R_SetLightLevel();
	R_SetGL2D();
}



/* BDGN 3.51bat: automatic SIMD.  Self-test lines go to the console; r_simd_3dnow / r_simd_sse / r_simd_mmx (not saved)
   tell the video menu: 1 on, 0 not supported, 2 self-test failed, 3 not used because 3DNow! is used. */
static cvar_t *r_nosse, *r_nommx, *r_cpu_path, *r_no3dnow_ext;
static void R_SimdPrint( const char *line )
{
	ri.Con_Printf( PRINT_ALL, "%s (%s)\n", line, REF_VERSION );
}
static void R_SimdPublish( int force )
{
	static int last3d = -1, last_sse2 = -1, last_sse = -1, last_ext = -1, last_lightmap=-1;
	int sse2=q3d_auto_sse2==1 && q3d_use_sse && r_cpu_path->value!=1;
	int ext=Q3D_EnableEnhanced(q3d_use_3dnow && !r_no3dnow_ext->value);
	int lightmap=Q3D_SSE2LightmapEnabled();
	if (force || last3d!=q3d_use_3dnow || last_sse2!=sse2 || last_sse!=q3d_use_sse ||
	    last_ext!=ext || last_lightmap!=lightmap)
	{
		last3d = q3d_use_3dnow;
		last_sse2=sse2;
		last_sse=q3d_use_sse;
		last_ext=ext;
		last_lightmap=lightmap;
		ri.Cvar_SetValue("r_simd_sse2_lightmap",lightmap);
		ri.Cvar_SetValue("r_simd_sse2_mipmap",sse2);
		ri.Cvar_Get( "r_simd_3dnow", "0", 0 );
		ri.Cvar_Get( "r_simd_sse", "0", 0 );
		ri.Cvar_Get( "r_simd_mmx", "0", 0 );
		ri.Cvar_Set( "r_simd_3dnow", q3d_use_3dnow ? "1" : ( Q3D_CPU() ? "2" : "0" ) );
		ri.Cvar_SetValue("r_simd_sse_ready",q3d_auto_sse==1);
		ri.Cvar_SetValue("r_simd_sse2_ready",q3d_auto_sse2==1);
		ri.Cvar_SetValue("r_simd_3dnow_ext_ready",Q3D_EnhancedReady());
		ri.Cvar_SetValue("r_simd_3dnow_ext",ext);
		ri.Cvar_SetValue("r_simd_sse",q3d_use_sse?1:q3d_auto_sse==2?2:q3d_use_3dnow?3:0);
		ri.Cvar_Set( "r_simd_mmx", va( "%i", q3d_auto_mmx ) );
	}
}

static void R_UpdateCPUPaths(void)
{
	int amd=(int)gl_3dnow->value && Q3D_CPU();
	Q3D_UpdateModes(!amd && q3d_auto_sse==1 && !r_nosse->value,
	                amd, q3d_auto_mmx==1 && !r_nommx->value);
	Q3D_SetSSE2MipMap(r_cpu_path->value!=1);
	{	/* BDGN 3.58w: the packed SSE light map kernel (gl_simd.c), on by default; 0 for the benchmark comparison */
		static cvar_t *packed;
		if (!packed) packed = ri.Cvar_Get("gl_sse_packed", "1", CVAR_ARCHIVE);
		Q3D_SetSSEPacked(packed->value != 0);
	}
}

void R_Register( void )
{
	r_lefthand = ri.Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	gl_batchworld = ri.Cvar_Get ("gl_batchworld", "0", CVAR_ARCHIVE);	// BDGN 3.42
	gl_fastsky = ri.Cvar_Get ("gl_fastsky", "0", CVAR_ARCHIVE);
	gl_fastwater = ri.Cvar_Get ("gl_fastwater", "0", CVAR_ARCHIVE);
	r_staticmap = ri.Cvar_Get ("cl_staticmap", "0", CVAR_ARCHIVE);
	r_brightskins = ri.Cvar_Get("r_brightskins","0",CVAR_ARCHIVE);
	gl_flatfloors = ri.Cvar_Get ("gl_flatfloors", "0", CVAR_ARCHIVE);	/* BDGN 3.58w */
	gl_flatstate = ri.Cvar_Get ("gl_flatstate", "1", CVAR_ARCHIVE);	/* BDGN 3.59p: menu DRAW PATHS */
	r_drawflat = ri.Cvar_Get ("r_drawflat", "0", CVAR_ARCHIVE);	// BDGN 3.38: flat coloured polygons (shows how the map is split)
	r_novis = ri.Cvar_Get ("r_novis", "0", 0);
	scr_scale_active = ri.Cvar_Get ("scr_scale_active", "1", 0);	// set by the client (vid_dll.c)
	r_nocull = ri.Cvar_Get ("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get ("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);

	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", 0);

	gl_nosubimage = ri.Cvar_Get( "gl_nosubimage", "0", 0 );
	gl_allow_software = ri.Cvar_Get( "gl_allow_software", "0", 0 );

	gl_particle_min_size = ri.Cvar_Get( "gl_particle_min_size", "2", CVAR_ARCHIVE );
	gl_particle_max_size = ri.Cvar_Get( "gl_particle_max_size", "40", CVAR_ARCHIVE );
	gl_particle_size = ri.Cvar_Get( "gl_particle_size", "40", CVAR_ARCHIVE );
	gl_particle_att_a = ri.Cvar_Get( "gl_particle_att_a", "0.01", CVAR_ARCHIVE );
	gl_particle_att_b = ri.Cvar_Get( "gl_particle_att_b", "0.0", CVAR_ARCHIVE );
	gl_particle_att_c = ri.Cvar_Get( "gl_particle_att_c", "0.01", CVAR_ARCHIVE );

	gl_modulate = ri.Cvar_Get ("gl_modulate", "1", CVAR_ARCHIVE );
	gl_drawdist = ri.Cvar_Get ("gl_drawdist", "0", CVAR_ARCHIVE );	// 0 = unlimited, 1..10 = shorter draw distance + fog
	gl_3dnow = ri.Cvar_Get ("gl_3dnow", "1", CVAR_ARCHIVE );	// 0 = never use the 3DNow! kernels
	R_DukeReset();
 ri.Cmd_AddCommand("duke_stats",R_DukeStats);
 ri.Cvar_Get("r_dlight_stats","",0);ri.Cmd_AddCommand("dlight_stats",R_DlightStats_f);	/* BDGN 3.52 (WESTBECK): measurement build */
 ri.Cvar_Get("r_duke_anim_support","0",0);
 ri.Cvar_SetValue("r_duke_anim_support",3);
 ri.Cvar_Get("r_yshear_support","0",0);
 ri.Cvar_SetValue("r_yshear_support",1);	/* BDGN 3.60i: the Duke Nukem 3D view (qcommon/yshear.h) */	/* BDGN 3.51q: 3 = reads r_duke_res_avail (3.52h: 32x32 library only); 2 = 3.51p */
	ri.Cvar_Get ("r_simpleenemy_bright", "0", 0);
	ri.Cvar_SetValue ("r_simpleenemy_bright", 1);
	gl_sse = ri.Cvar_Get ("gl_sse", "0", CVAR_ARCHIVE | CVAR_NOSET );	// BDGN 3.50f: locked off to prevent dark/blue texture output.
	if (gl_sse->value != 0)
	{
		gl_sse->value = 0;
		strcpy (gl_sse->string, "0");
		gl_sse->modified = true;
	}
	gl_mmx = ri.Cvar_Get ("gl_mmx", "0", CVAR_ARCHIVE );		// 1 = MMX versions of the mip map / resample loops
	gl_amd_sky = ri.Cvar_Get ("gl_amd_sky", "1", CVAR_ARCHIVE );	// 3DNow! sky clipping (follows the 3DNow! menu row, needs gl_3dnow)
	gl_amd_angles = ri.Cvar_Get ("gl_amd_angles", "1", CVAR_ARCHIVE );	// 3DNow! AngleVectors (follows the 3DNow! menu row, needs gl_3dnow)
	gl_amd_cull = ri.Cvar_Get ("gl_amd_cull", "1", CVAR_ARCHIVE );	// 3DNow! alias model culling (follows the 3DNow! menu row, needs gl_3dnow)
	gl_amd_marklights = ri.Cvar_Get ("gl_amd_marklights", "1", CVAR_ARCHIVE );	// BDGN 3.50: 3DNow! R_MarkLights (follows the 3DNow! menu row)
	gl_amd_lightpoint = ri.Cvar_Get ("gl_amd_lightpoint", "1", CVAR_ARCHIVE );	// BDGN 3.50: 3DNow! RecursiveLightPoint (follows the 3DNow! menu row)
	gl_amd_particles = ri.Cvar_Get ("gl_amd_particles", "1", CVAR_ARCHIVE );	// BDGN 3.50: 3DNow! particle setup (follows the 3DNow! menu row)
	/* BDGN 3.51bat: SSE and MMX are automatic (Q3D_AutoSIMD: CPU support + self-test, none of them with 3DNow!);
	   gl_sse, gl_mmx and gl_sse_test are no longer read.  r_simd_* tell the video menu what is on. */
	Q3D_AutoSIMD( R_SimdPrint );
	r_nosse = ri.Cvar_Get( "bdgn_nosse", "0", 0 );		/* BDGN 3.51bbt: video menu "SSE: off (test)", not saved */
	r_cpu_path=ri.Cvar_Get("bdgn_cpu_path","0",CVAR_ARCHIVE);
	r_no3dnow_ext=ri.Cvar_Get("bdgn_no3dnow_ext","0",0);
	r_nommx = ri.Cvar_Get( "bdgn_nommx", "0", 0 );		/* BDGN 3.51bbt: video menu "MMX: off (test)", not saved */
	R_UpdateCPUPaths();
	R_SimdPublish( 1 );
	gl_fastsqrt = ri.Cvar_Get ("fastsqrt", "0", CVAR_ARCHIVE );	// BDGN 3.37 TEST: 0 = sqrt, 1 = Q_rsqrt, 2 = 3DNow! rsqrt
	gl_fastsqrt2 = ri.Cvar_Get ("fastsqrt2", "0", CVAR_ARCHIVE );	// TEST: also frustum planes and model radii
	Q_SetFastSqrt( (int)gl_fastsqrt->value, (int)gl_fastsqrt2->value );
	gl_log = ri.Cvar_Get( "gl_log", "0", 0 );
	gl_bitdepth = ri.Cvar_Get( "gl_bitdepth", "0", CVAR_ARCHIVE );	/* BDGN 3.58k: saved (video menu) */
	gl_mode = ri.Cvar_Get( "gl_mode", "3", CVAR_ARCHIVE );
	gl_lightmap = ri.Cvar_Get ("gl_lightmap", "0", 0);
	gl_shadows = ri.Cvar_Get ("gl_shadows", "0", CVAR_ARCHIVE );
	R_SunInit ();	/* BDGN 3.55ak: gl_sun */
	gl_dynamic = ri.Cvar_Get ("gl_dynamic", "1", CVAR_ARCHIVE);
	gl_dlight_cull = ri.Cvar_Get ("gl_dlight_cull", "1", CVAR_ARCHIVE);		// BDGN 3.51p: same picture, less work
	gl_dlight_maxdist = ri.Cvar_Get ("gl_dlight_maxdist", "0", CVAR_ARCHIVE);	// BDGN 3.51p: optional, changes the picture
	gl_nobind = ri.Cvar_Get ("gl_nobind", "0", 0);
	gl_round_down = ri.Cvar_Get ("gl_round_down", "1", 0);
	gl_picmip = ri.Cvar_Get ("gl_picmip", "0", 0);
	gl_skymip = ri.Cvar_Get ("gl_skymip", "0", 0);
	gl_showtris = ri.Cvar_Get ("gl_showtris", "0", 0);
	gl_ztrick = ri.Cvar_Get ("gl_ztrick", "0", 0);
	gl_finish = ri.Cvar_Get ("gl_finish", "0", CVAR_ARCHIVE);
	gl_sortentities = ri.Cvar_Get ("gl_sortentities", "1", 0);	// BDGN 3.51s: automatic texture grouping of models
	gl_clear = ri.Cvar_Get ("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get ("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get ("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get ("gl_flashblend", "0", 0);
	gl_playermip = ri.Cvar_Get ("gl_playermip", "0", CVAR_ARCHIVE);	/* BDGN 3.58k: saved (video menu) */
	gl_monolightmap = ri.Cvar_Get( "gl_monolightmap", "0", 0 );
	/* BDGN 3.57d: the old-school modes of the software renderer, where OpenGL can do them (gl_light.c, gl_rsurf.c, below) */
	gl_doomlight = ri.Cvar_Get( "gl_doomlight", "0", CVAR_ARCHIVE );
	gl_blakelight = ri.Cvar_Get( "gl_blakelight", "0", CVAR_ARCHIVE );
	gl_q1fog = ri.Cvar_Get( "gl_q1fog", "0", CVAR_ARCHIVE );
	gl_q1particles = ri.Cvar_Get( "gl_q1particles", "0", CVAR_ARCHIVE );
	gl_solidalpha = ri.Cvar_Get( "gl_solidalpha", "0", CVAR_ARCHIVE );
	ri.Cvar_SetValue( "gl_oldschool_capable", 1 );	/* the menu shows the OpenGL rows only with this renderer (not ref_glam) */
	gl_driver = ri.Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
	gl_texturemode = ri.Cvar_Get( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	gl_anisotropy = ri.Cvar_Get("gl_anisotropy","1",CVAR_ARCHIVE);
	gl_texturealphamode = ri.Cvar_Get( "gl_texturealphamode", "default", CVAR_ARCHIVE );
	gl_texturesolidmode = ri.Cvar_Get( "gl_texturesolidmode", "default", CVAR_ARCHIVE );
	gl_lockpvs = ri.Cvar_Get( "gl_lockpvs", "0", 0 );

	gl_vertex_arrays = ri.Cvar_Get( "gl_vertex_arrays", "0", CVAR_ARCHIVE );

	gl_ext_swapinterval = ri.Cvar_Get( "gl_ext_swapinterval", "1", CVAR_ARCHIVE );
	gl_ext_palettedtexture = ri.Cvar_Get( "gl_ext_palettedtexture", "1", CVAR_ARCHIVE );
	gl_ext_multitexture = ri.Cvar_Get( "gl_ext_multitexture", "0", CVAR_ARCHIVE );	/* BDGN 3.52i: off by default (K6-III+/GF2: 116.8 off, 114.5 on); the 3dfx presets turn it on */
	gl_ext_pointparameters = ri.Cvar_Get( "gl_ext_pointparameters", "1", CVAR_ARCHIVE );
	gl_ext_compiled_vertex_array = ri.Cvar_Get( "gl_ext_compiled_vertex_array", "1", CVAR_ARCHIVE );

	gl_drawbuffer = ri.Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = ri.Cvar_Get( "gl_swapinterval", "0", CVAR_ARCHIVE );
	if ( gl_swapinterval->value )
		ri.Cvar_SetValue( "gl_swapinterval", 0 );	/* BDGN 3.58o: vsync is always off (owner, 2026-11-20) */

	gl_saturatelighting = ri.Cvar_Get( "gl_saturatelighting", "0", 0 );

	gl_3dlabs_broken = ri.Cvar_Get( "gl_3dlabs_broken", "1", CVAR_ARCHIVE );
	gl_fastworld = ri.Cvar_Get( "gl_fastworld", "0", CVAR_ARCHIVE );
	gl_fastdraw = ri.Cvar_Get( "gl_fastdraw", "0", CVAR_ARCHIVE );
	gl_modelbatch_auto = ri.Cvar_Get ("gl_modelbatch_auto", "1", CVAR_ARCHIVE);
	gl_particlebatch_auto = ri.Cvar_Get ("gl_particlebatch_auto", "1", CVAR_ARCHIVE);
	gl_fastparticles = ri.Cvar_Get( "gl_fastparticles", "0", CVAR_ARCHIVE );
	gl_fastforce = ri.Cvar_Get( "gl_fastforce", "0", 0 );
	gl_flatmodels = ri.Cvar_Get( "gl_flatmodels", "0", CVAR_ARCHIVE );
	gl_lowdetail = ri.Cvar_Get( "gl_lowdetail", "0", CVAR_ARCHIVE );
	gl_maxtexsize = ri.Cvar_Get( "gl_maxtexsize", "0", CVAR_ARCHIVE );

	// doogies code
	// Q2TTM: add cvars to disable image loading (gl_hack_noimages) and texture application to world surfaces (gl_hack_notextures).
	gl_hack_noimages = ri.Cvar_Get( "gl_hack_noimages", "0", CVAR_ARCHIVE );
	gl_hack_notextures = ri.Cvar_Get( "gl_hack_notextures", "0", CVAR_ARCHIVE );
	// end

	vid_fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
	vid_gamma = ri.Cvar_Get( "vid_gamma", "1.0", CVAR_ARCHIVE );
	vid_ref = ri.Cvar_Get( "vid_ref", "soft", CVAR_ARCHIVE );

	ri.Cmd_AddCommand( "imagelist", GL_ImageList_f );
	ri.Cmd_AddCommand( "screenshot", GL_ScreenShot_f );
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
	ri.Cmd_AddCommand( "gl_strings", GL_Strings_f );
}

/*
==================
R_SetMode
==================
*/
qboolean R_SetMode (void)
{
	rserr_t err;
	qboolean fullscreen;

	if ( vid_fullscreen->modified && !gl_config.allow_cds )
	{
		ri.Con_Printf( PRINT_ALL, "R_SetMode() - CDS not allowed with this driver\n" );
		ri.Cvar_SetValue( "vid_fullscreen", !vid_fullscreen->value );
		vid_fullscreen->modified = false;
	}

	fullscreen = vid_fullscreen->value;

	vid_fullscreen->modified = false;
	gl_mode->modified = false;

	if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_mode->value, fullscreen ) ) == rserr_ok )
	{
		gl_state.prev_mode = gl_mode->value;
	}
	else
	{
		if ( err == rserr_invalid_fullscreen )
		{
			ri.Cvar_SetValue( "vid_fullscreen", 0);
			vid_fullscreen->modified = false;
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - fullscreen unavailable in this mode\n" );
			if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_mode->value, false ) ) == rserr_ok )
			{
				gl_state.prev_mode=gl_mode->value;
				return true;
			}
		}
		else if ( err == rserr_invalid_mode )
		{
			ri.Cvar_SetValue( "gl_mode", gl_state.prev_mode );
			gl_mode->modified = false;
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - invalid mode\n" );
		}

		// try setting it back to something safe
		if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_state.prev_mode, false ) ) != rserr_ok )
		{
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - could not revert to safe mode\n" );
			return false;
		}
	}
	return true;
}

/*
===============
R_Init
===============
*/
qboolean R_Init( void *hinstance, void *hWnd )
{	
	char renderer_buffer[1000];
	char vendor_buffer[1000];
	int		err;
	int		j;
	extern float r_turbsin[256];
	int		t_start = Sys_Milliseconds (), t_window = 0, t_images = 0, t_models = 0, t_shaders = 0;	/* BDGN 3.57h */

	for ( j = 0; j < 256; j++ )
	{
		r_turbsin[j] *= 0.5;
	}

	ri.Con_Printf (PRINT_ALL, "ref_gl version: "REF_VERSION"\n");

	Draw_GetPalette ();

	R_Register();

	// initialize our QGL dynamic bindings
	if ( !QGL_Init( gl_driver->string ) )
	{
		QGL_Shutdown();
        ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - could not load \"%s\"\n", gl_driver->string );
		return (qboolean)-1;
	}

	// initialize OS-specific parts of OpenGL
	if ( !GLimp_Init( hinstance, hWnd ) )
	{
		QGL_Shutdown();
		return (qboolean)-1;
	}

	t_window = Sys_Milliseconds ();
	// set our "safe" modes
	gl_state.prev_mode = 3;

	// create the window and set up the context
	if ( !R_SetMode () )
	{
		QGL_Shutdown();
        ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - could not R_SetMode()\n" );
		return (qboolean)-1;
	}

	/*
	** get our various GL strings
	*/
	gl_config.vendor_string = qglGetString (GL_VENDOR);
	ri.Con_Printf (PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string );
	gl_config.renderer_string = qglGetString (GL_RENDERER);
	ri.Cvar_Set("bdgn_gl_renderer",gl_config.renderer_string?(char *)gl_config.renderer_string:"");	/* BDGN 3.55q: auto preset fallback (Windows 95) */
	ri.Con_Printf (PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string );
	gl_config.version_string = qglGetString (GL_VERSION);
	ri.Con_Printf (PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string );
	/* BDGN 3.51p: for bdgn_status and the benchmark log (vid_dll.c clears them before any renderer loads) */
	/* glGetString returns NULL when the driver or context failed: publish "" then (Cvar_Set needs a string) */
	ri.Cvar_Get ("bdgn_gl_vendor", "", 0);
	ri.Cvar_Set ("bdgn_gl_vendor", gl_config.vendor_string ? (char *)gl_config.vendor_string : "");
	ri.Cvar_Get ("bdgn_gl_renderer", "", 0);
	ri.Cvar_Set ("bdgn_gl_renderer", gl_config.renderer_string ? (char *)gl_config.renderer_string : "");
	ri.Cvar_Get ("bdgn_gl_version", "", 0);
	ri.Cvar_Set ("bdgn_gl_version", gl_config.version_string ? (char *)gl_config.version_string : "");
	gl_config.extensions_string = qglGetString (GL_EXTENSIONS);
	//dont print gl extension, solves bug with modern drivers if th exe is not named quake2.exe
//	ri.Con_Printf (PRINT_ALL, "GL_EXTENSIONS: %s\n", gl_config.extensions_string );
	
	strcpy( renderer_buffer, gl_config.renderer_string );
	strlwr( renderer_buffer );

	strcpy( vendor_buffer, gl_config.vendor_string );
	strlwr( vendor_buffer );

	// BDGN 3.36: 3dfx miniGL / Voodoo drivers accept the vertex array calls but draw nothing,
	// which made monsters and items disappear with batched drawing.  Keep the old drawing path there.
	gl_vaunsafe = ( strstr( renderer_buffer, "voodoo" ) || strstr( renderer_buffer, "3dfx" )
		|| strstr( renderer_buffer, "minigl" ) || strstr( vendor_buffer, "3dfx" )
		|| strstr( renderer_buffer, "pcx" ) ) ? 1 : 0;	/* BDGN 3.52k: + PowerVR PCX1/PCX2 miniGL */
	if ( strstr( vendor_buffer, "metabyte" ) || strstr( renderer_buffer, "wicked" ) || strstr( vendor_buffer, "wicked" ) )
		gl_vaunsafe = 0;	/* BDGN 3.52k: WickedGL has working vertex arrays (GL_EXT_vertex_array, compiled vertex arrays) */
	ri.Cvar_Set( "bdgn_gl_vertex_arrays_allowed", gl_vaunsafe ? "0" : "1" );
	if ( gl_vaunsafe )
		ri.Con_Printf( PRINT_ALL, "3dfx driver detected: batched drawing (gl_fastdraw/gl_fastparticles) disabled (gl_fastforce 1 overrides)\n" );

	if ( strstr( renderer_buffer, "voodoo" ) )
	{
		if ( !strstr( renderer_buffer, "rush" ) )
			gl_config.renderer = GL_RENDERER_VOODOO;
		else
			gl_config.renderer = GL_RENDERER_VOODOO_RUSH;
	}
	else if ( strstr( vendor_buffer, "sgi" ) )
		gl_config.renderer = GL_RENDERER_SGI;
	else if ( strstr( renderer_buffer, "permedia" ) )
		gl_config.renderer = GL_RENDERER_PERMEDIA2;
	else if ( strstr( renderer_buffer, "glint" ) )
		gl_config.renderer = GL_RENDERER_GLINT_MX;
	else if ( strstr( renderer_buffer, "glzicd" ) )
		gl_config.renderer = GL_RENDERER_REALIZM;
	else if ( strstr( renderer_buffer, "gdi" ) )
		gl_config.renderer = GL_RENDERER_MCD;
	else if ( strstr( renderer_buffer, "pcx2" ) )
		gl_config.renderer = GL_RENDERER_PCX2;
	else if ( strstr( renderer_buffer, "verite" ) )
		gl_config.renderer = GL_RENDERER_RENDITION;
	else
		gl_config.renderer = GL_RENDERER_OTHER;

	if ( toupper( gl_monolightmap->string[1] ) != 'F' )
	{
		if ( gl_config.renderer == GL_RENDERER_PERMEDIA2 )
		{
			ri.Cvar_Set( "gl_monolightmap", "A" );
			ri.Con_Printf( PRINT_ALL, "...using gl_monolightmap 'a'\n" );
		}
		else if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
		else
		{
			ri.Cvar_Set( "gl_monolightmap", "0" );
		}
	}

	// power vr can't have anything stay in the framebuffer, so
	// the screen needs to redraw the tiled background every frame
	if ( gl_config.renderer & GL_RENDERER_POWERVR ) 
	{
		ri.Cvar_Set( "scr_drawall", "1" );
	}
	else
	{
		ri.Cvar_Set( "scr_drawall", "0" );
	}

#ifdef __linux__
	ri.Cvar_SetValue( "gl_finish", 1 );
#endif

	// MCD has buffering issues
	if ( gl_config.renderer == GL_RENDERER_MCD )
	{
		ri.Cvar_SetValue( "gl_finish", 1 );
	}

	if ( gl_config.renderer & GL_RENDERER_3DLABS )
	{
		if ( gl_3dlabs_broken->value )
			gl_config.allow_cds = false;
		else
			gl_config.allow_cds = true;
	}
	else
	{
		gl_config.allow_cds = true;
	}

	if ( gl_config.allow_cds )
		ri.Con_Printf( PRINT_ALL, "...allowing CDS\n" );
	else
		ri.Con_Printf( PRINT_ALL, "...disabling CDS\n" );

	/*
	** grab extensions
	*/
	if ( strstr( gl_config.extensions_string, "GL_EXT_compiled_vertex_array" ) || 
		 strstr( gl_config.extensions_string, "GL_SGI_compiled_vertex_array" ) )
	{
		ri.Con_Printf( PRINT_ALL, "...enabling GL_EXT_compiled_vertex_array\n" );
		qglLockArraysEXT = ( void * ) qwglGetProcAddress( "glLockArraysEXT" );
		qglUnlockArraysEXT = ( void * ) qwglGetProcAddress( "glUnlockArraysEXT" );
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

#ifdef _WIN32
	/* Some system ICDs expose the WGL entry points outside the GL extension list. */
	if (!Q_stricmp(gl_driver->string,"opengl32") || !Q_stricmp(gl_driver->string,"opengl32.dll") ||
		strstr(gl_config.extensions_string,"WGL_EXT_swap_control"))
		GL_InitSwapInterval();
	else
		gl_swapinterval->modified = true;
#endif

	if ( strstr( gl_config.extensions_string, "GL_EXT_point_parameters" ) )
	{
		if ( gl_ext_pointparameters->value )
		{
			qglPointParameterfEXT = ( void (APIENTRY *)( GLenum, GLfloat ) ) qwglGetProcAddress( "glPointParameterfEXT" );
			qglPointParameterfvEXT = ( void (APIENTRY *)( GLenum, const GLfloat * ) ) qwglGetProcAddress( "glPointParameterfvEXT" );
			ri.Con_Printf( PRINT_ALL, "...using GL_EXT_point_parameters\n" );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_EXT_point_parameters\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_point_parameters not found\n" );
	}

#ifdef __linux__
	if ( strstr( gl_config.extensions_string, "3DFX_set_global_palette" ))
	{
		if ( gl_ext_palettedtexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using 3DFX_set_global_palette\n" );
			qgl3DfxSetPaletteEXT = ( void ( APIENTRY * ) (GLuint *) )qwglGetProcAddress( "gl3DfxSetPaletteEXT" );
			qglColorTableEXT = Fake_glColorTableEXT;
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring 3DFX_set_global_palette\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...3DFX_set_global_palette not found\n" );
	}
#endif

	if ( !qglColorTableEXT &&
		strstr( gl_config.extensions_string, "GL_EXT_paletted_texture" ) && 
		strstr( gl_config.extensions_string, "GL_EXT_shared_texture_palette" ) )
	{
		if ( gl_ext_palettedtexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_EXT_shared_texture_palette\n" );
			qglColorTableEXT = ( void ( APIENTRY * ) ( int, int, int, int, int, const void * ) ) qwglGetProcAddress( "glColorTableEXT" );
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_EXT_shared_texture_palette\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_EXT_shared_texture_palette not found\n" );
	}

	ri.Cvar_Set( "bdgn_gl_palette_active", qglColorTableEXT ? "1" : "0" );

	if ( strstr( gl_config.extensions_string, "GL_ARB_multitexture" ) )
	{
		if ( gl_ext_multitexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
			qglMTexCoord2fSGIS = ( void * ) qwglGetProcAddress( "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( void * ) qwglGetProcAddress( "glActiveTextureARB" );
			qglClientActiveTextureARB = ( void * ) qwglGetProcAddress( "glClientActiveTextureARB" );
			GL_TEXTURE0 = GL_TEXTURE0_ARB;
			GL_TEXTURE1 = GL_TEXTURE1_ARB;
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	if ( strstr( gl_config.extensions_string, "GL_SGIS_multitexture" ) )
	{
		if ( qglActiveTextureARB )
		{
			ri.Con_Printf( PRINT_ALL, "...GL_SGIS_multitexture deprecated in favor of ARB_multitexture\n" );
		}
		else if ( gl_ext_multitexture->value )
		{
			ri.Con_Printf( PRINT_ALL, "...using GL_SGIS_multitexture\n" );
			qglMTexCoord2fSGIS = ( void * ) qwglGetProcAddress( "glMTexCoord2fSGIS" );
			qglSelectTextureSGIS = ( void * ) qwglGetProcAddress( "glSelectTextureSGIS" );
			GL_TEXTURE0 = GL_TEXTURE0_SGIS;
			GL_TEXTURE1 = GL_TEXTURE1_SGIS;
		}
		else
		{
			ri.Con_Printf( PRINT_ALL, "...ignoring GL_SGIS_multitexture\n" );
		}
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "...GL_SGIS_multitexture not found\n" );
	}

	GL_InitTextureFiltering();
	GL_SetDefaultState();

	t_images = Sys_Milliseconds ();
	GL_InitImages ();
	t_models = Sys_Milliseconds ();
	/* Weapon compatibility checks need the selected driver and a live GL context. */
	R_WeaponSpritesInit ();
	ri.Cmd_AddCommand ("weapon_stats", R_WeaponStats);
	if (!qglTexSubImage2D)
		ri.Cvar_SetValue ("r_duke_anim_support",0); /* Static sprites on drivers without atlas updates. */
	Mod_Init ();
	R_InitParticleTexture ();
	Draw_InitLocal ();
	gl_soft_shadow_capable=ri.Cvar_Get("bdgn_modern_lighting_cpu","0",0)->value && !ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value &&
		(!Q_stricmp(gl_driver->string,"opengl32") || !Q_stricmp(gl_driver->string,"opengl32.dll")) &&
		!ri.Cvar_Get("vid_rendition","0",0)->value &&
		gl_config.version_string && atoi((const char *)gl_config.version_string)>=2;
	ri.Cvar_SetValue("bdgn_gl_soft_shadow_capable",gl_soft_shadow_capable);
	/* BDGN 3.55ai: shadow volumes need a stencil buffer in the window (asked for on modern computers) */
	ri.Cvar_SetValue("bdgn_gl_volume_shadow_capable",gl_soft_shadow_capable && GL_WindowStencil()>=8);
	ri.Cvar_SetValue("bdgn_gl_modernfx_capable",gl_soft_shadow_capable);	/* BDGN 3.55t: 3D weapon effects (RF_FXMESH, RF_PUFFS) */
	/* BDGN 3.55y: world marks (RF_DECAL, gl_decal.c) only need polygon offset (OpenGL 1.1): every driver of a computer
	   that is not a legacy one (Pentium / K6 class or 32 MB) */
	ri.Cvar_SetValue("bdgn_gl_decal_capable",qglPolygonOffset!=NULL && !ri.Cvar_Get("bdgn_legacy_graphics","0",0)->value);
	t_shaders = Sys_Milliseconds ();
	GL_ScaleInit();
	GL_InitPixelLighting();
 GL_InitModernEffects();
	GL_InitDistortion();	/* BDGN 3.55t */
	ri.Vid_MenuInit();
	{	/* BDGN 3.57h: where the start of the renderer goes (bdgn_startup.txt has it as one line) */
		int t_end = Sys_Milliseconds ();
		ri.Con_Printf (PRINT_ALL, "ref_gl start: driver %i ms, mode + context + extensions %i ms, images %i ms, "
			"models + pictures %i ms, shaders + tests %i ms (total %i ms)\n", t_window - t_start,
			t_images - t_window, t_models - t_images, t_shaders - t_models, t_end - t_shaders, t_end - t_start);
		{	/* BDGN 3.59f: also into bdgn_startup.txt (qcommon/common.c) */
			char steps[256];
			Com_sprintf (steps, sizeof(steps), "ref_gl: driver %i, mode + context + extensions %i, images %i, models + pictures %i, "
				"shaders + tests %i, total %i ms", t_window - t_start, t_images - t_window, t_models - t_images,
				t_shaders - t_models, t_end - t_shaders, t_end - t_start);
			ri.Cvar_Get ("bdgn_ref_init", "", 0);
			ri.Cvar_Set ("bdgn_ref_init", steps);
		}
	}

	err = qglGetError();
	if ( err != GL_NO_ERROR )
		ri.Con_Printf (PRINT_ALL, "glGetError() = 0x%x\n", err);
	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown (void)
{
	GL_ScaleShutdown();
	R_DukeReset();
	ri.Cmd_RemoveCommand("duke_stats");
	ri.Cvar_SetValue("r_duke_anim_support",0);	/* BDGN 3.51q: a renderer loaded next (glam, soft without support) must not inherit it */
	ri.Cvar_SetValue("r_yshear_support",0);	/* BDGN 3.60i */
	R_WeaponSpritesReset ();
	ri.Cmd_RemoveCommand ("weapon_stats");
	ri.Cmd_RemoveCommand ("dlight_stats");	/* BDGN 3.52 (WESTBECK) */
	ri.Cvar_SetValue ("r_weaponsprite_support", 0);
	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("imagelist");
	ri.Cmd_RemoveCommand ("gl_strings");

	Mod_FreeAll ();

	GL_ShutdownImages ();

	/*
	** shut down OS specific OpenGL stuff like contexts, etc.
	*/
	GLimp_Shutdown();

	/*
	** shutdown our QGL subsystem
	*/
	QGL_Shutdown();
}



/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginFrame( float camera_separation )
{
	GL_FX4KCheck ();	/* BDGN 3.56r: effect rows switched between HD and HD 4K */
	GL_ScaleAbortFrame();
	R_DlightStatsFrame ();	/* BDGN 3.52 (WESTBECK): dlight_stats frame time and clock calibration */
	R_UpdateCPUPaths();
	R_SimdPublish( 0 );
	Q_SetFastSqrt( (int)gl_fastsqrt->value, (int)gl_fastsqrt2->value );

	gl_state.camera_separation = camera_separation;

	/*
	** change modes if necessary
	*/
	if ( gl_mode->modified || vid_fullscreen->modified )
	{	// FIXME: only restart if CDS is required
		cvar_t	*ref;

		ref = ri.Cvar_Get ("vid_ref", "gl", 0);
		ref->modified = true;
	}

	if ( gl_log->modified )
	{
		GLimp_EnableLogging( gl_log->value );
		gl_log->modified = false;
	}

	if ( gl_log->value )
	{
		GLimp_LogNewFrame();
	}

	/*
	** update 3Dfx gamma -- it is expected that a user will do a vid_restart
	** after tweaking this value
	*/
	if ( vid_gamma->modified )
	{
		vid_gamma->modified = false;

		if ( gl_config.renderer & ( GL_RENDERER_VOODOO ) )
		{
			char envbuffer[1024];
			float g;

			g = 2.00 * ( 0.8 - ( vid_gamma->value - 0.5 ) ) + 1.0F;
			Com_sprintf( envbuffer, sizeof(envbuffer), "SSTV2_GAMMA=%f", g );
			putenv( envbuffer );
			Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
			putenv( envbuffer );
		}
	}

	GLimp_BeginFrame( camera_separation );

	/*
	** go into 2D mode
	*/
	R_Clear2DMargin ();
	R_Set2DProjection ();
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	qglEnable (GL_ALPHA_TEST);
	qglColor4f (1,1,1,1);

	/*
	** draw buffer stuff
	*/
	if ( gl_drawbuffer->modified )
	{
		gl_drawbuffer->modified = false;

		if ( gl_state.camera_separation == 0 || !gl_state.stereo_enabled )
		{
			if ( Q_stricmp( gl_drawbuffer->string, "GL_FRONT" ) == 0 )
				qglDrawBuffer( GL_FRONT );
			else
				qglDrawBuffer( GL_BACK );
		}
	}

	/*
	** texturemode stuff
	*/
	if (gl_texturemode->modified || gl_anisotropy->modified)
	{
		GL_TextureMode(gl_texturemode->string);
		gl_texturemode->modified = gl_anisotropy->modified = false;
	}

	if ( gl_texturealphamode->modified )
	{
		GL_TextureAlphaMode( gl_texturealphamode->string );
		gl_texturealphamode->modified = false;
	}

	if ( gl_texturesolidmode->modified )
	{
		GL_TextureSolidMode( gl_texturesolidmode->string );
		gl_texturesolidmode->modified = false;
	}

	/*
	** swapinterval stuff
	*/
	GL_UpdateSwapInterval();

	//
	// clear screen if desired
	//
	R_Clear ();
}

/*
=============
R_SetPalette
=============
*/
unsigned r_rawpalette[256];

void R_SetPalette ( const unsigned char *palette)
{
	int		i;

	byte *rp = ( byte * ) r_rawpalette;

	if ( palette )
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = palette[i*3+0];
			rp[i*4+1] = palette[i*3+1];
			rp[i*4+2] = palette[i*3+2];
			rp[i*4+3] = 0xff;
		}
	}
	else
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = d_8to24table[i] & 0xff;
			rp[i*4+1] = ( d_8to24table[i] >> 8 ) & 0xff;
			rp[i*4+2] = ( d_8to24table[i] >> 16 ) & 0xff;
			rp[i*4+3] = 0xff;
		}
	}
	GL_SetTexturePalette( r_rawpalette );

	qglClearColor (0,0,0,0);
	qglClear (GL_COLOR_BUFFER_BIT);
	qglClearColor (0, 0, 0, 1);
}

/*
** R_DrawBeam
*/
void R_DrawBeam( entity_t *e )
{
#define NUM_BEAM_SEGS 6

	int	i;
	float r, g, b;

	vec3_t perpvec;
	vec3_t direction, normalized_direction;
	vec3_t	start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t oldorigin, origin;

	oldorigin[0] = e->oldorigin[0];
	oldorigin[1] = e->oldorigin[1];
	oldorigin[2] = e->oldorigin[2];

	origin[0] = e->origin[0];
	origin[1] = e->origin[1];
	origin[2] = e->origin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if ( VectorNormalizeF( normalized_direction ) == 0 )
		return;

	PerpendicularVector( perpvec, normalized_direction );
	VectorScale( perpvec, e->frame / 2, perpvec );

	for ( i = 0; i < 6; i++ )
	{
		RotatePointAroundVector( start_points[i], normalized_direction, perpvec, (360.0/NUM_BEAM_SEGS)*i );
		VectorAdd( start_points[i], origin, start_points[i] );
		VectorAdd( start_points[i], direction, end_points[i] );
	}

	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_BLEND );
	qglDepthMask( GL_FALSE );

	r = ( d_8to24table[e->skinnum & 0xFF] ) & 0xFF;
	g = ( d_8to24table[e->skinnum & 0xFF] >> 8 ) & 0xFF;
	b = ( d_8to24table[e->skinnum & 0xFF] >> 16 ) & 0xFF;

	r *= 1/255.0F;
	g *= 1/255.0F;
	b *= 1/255.0F;

	qglColor4f( r, g, b, e->alpha );

	qglBegin( GL_TRIANGLE_STRIP );
	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		qglVertex3fv( start_points[i] );
		qglVertex3fv( end_points[i] );
		qglVertex3fv( start_points[(i+1)%NUM_BEAM_SEGS] );
		qglVertex3fv( end_points[(i+1)%NUM_BEAM_SEGS] );
	}
	qglEnd();

	qglEnable( GL_TEXTURE_2D );
	qglDisable( GL_BLEND );
	qglDepthMask( GL_TRUE );
}

//===================================================================


void	R_BeginRegistration (char *map);
struct model_s	*R_RegisterModel (char *name);
struct image_s	*R_RegisterSkin (char *name);
void R_SetSky (char *name, float rotate, vec3_t axis);
void	R_EndRegistration (void);

void	R_RenderFrame (refdef_t *fd);

struct image_s	*Draw_FindPic (char *name);

void	Draw_Pic (int x, int y, char *name);
void	Draw_Char (int x, int y, int c);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeScreen (void);

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t GetRefAPI (refimport_t rimp )
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;

	re.RenderFrame = R_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen= Draw_FadeScreen;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = R_ProfEndFrame;	/* BDGN 3.52t: GLimp_EndFrame + profiler */

	re.AppActivate = GLimp_AppActivate;

	Swap_Init ();

	return re;
}


#ifndef REF_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	ri.Sys_Error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, fmt);
	vsprintf (text, fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
}

#endif
