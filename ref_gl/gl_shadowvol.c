/*
gl_shadowvol.c -- BDGN 3.55ai: shadow volumes (gl_shadows 4, "light volumes")

Every dynamic light - rocket, grenade, blaster and hyperblaster bolts, the BFG ball, explosions, muzzle flashes -
throws the shadows of the models around it outwards in all directions, and they fall on whatever is there: floor,
walls, stairs, ceilings, other models. The static light of the room gives each model one more shadow, away from the
brighter side (the direction of gl_shadows 3). The method is the stencil shadow volume of Doom 3:

  for a light, every triangle of a model that faces the light is pushed away from it into a closed prism; the prisms
  are drawn into the stencil buffer only, back faces adding and front faces subtracting where they lie BEHIND what is
  already drawn (depth fail, so it also works with the eye inside a shadow); what is left non-zero is in shadow and
  one blended quad over the screen darkens it. Triangles are extruded one by one (no silhouette search), so open and
  badly built models work too; inner faces cancel out. The counting wraps (OpenGL 1.4), so the face winding cannot
  break it.

Needs a stencil buffer: asked for in the window's pixel format on modern computers (win32/glw_imp.c) and packed into
the scene's frame buffer (gl_render_scale.c). Checked every frame for the frame buffer in use; without one this
mode draws the shadows of gl_shadows 3. bdgn_gl_volume_shadow_capable tells the menu and the presets.

Limits: the 3 strongest lights near the view, models within 1200 units, shadows as long as the light reaches (at
most 320 units); the world itself casts no shadow (lights still shine through walls as in Quake II).
*/
#include "gl_local.h"

#define SV_MAX_CASTERS	96
#define SV_MAX_VERTS	65536
#define SV_MAX_LIGHTS	3
#define GL_INCR_WRAP_14	0x8507
#define GL_DECR_WRAP_14	0x8508

typedef struct
{
	dmdl_t	*hdr;
	int		first, num;
	vec3_t	center, dir;	/* dir: static light direction (away from the light), length 1 */
	float	radius, ext;	/* ext: length of the static shadow */
} sv_caster_t;

static sv_caster_t	sv_casters[SV_MAX_CASTERS];
static int			sv_ncasters, sv_nverts;
static float		sv_verts[SV_MAX_VERTS][3];
static float		sv_far[MAX_VERTS][3], sv_near[MAX_VERTS][3];
int					r_shadow_stencil, r_shadow_stencil_ref;	/* 3.56a: gl_shadows 3 with a stencil buffer */
int					r_volume_shadows;	/* this frame: 1 = models add themselves with R_ShadowVolumeAdd */
extern int			gl_window_stencil;

int GL_WindowStencil (void)
{
	if (gl_window_stencil < 0)
	{
		GLint bits = 0;
		qglGetIntegerv (GL_STENCIL_BITS, &bits);
		gl_window_stencil = bits;
	}
	return gl_window_stencil;
}

/* start of a view: is there a stencil buffer in the frame buffer the scene is drawn into? */
void R_ShadowVolumeFrame (void)
{
	GLint bits = 0, func = GL_LEQUAL;
	sv_ncasters = sv_nverts = 0;
	r_volume_shadows = 0;
	r_shadow_stencil = 0;
	if (!gl_shadows || gl_shadows->value < 3 || !gl_soft_shadow_capable || gl_reflect_pass ||
	    (r_newrefdef.rdflags & RDF_NOWORLDMODEL) || !qglStencilOp || !qglStencilFunc || !qglColorMask)
		return;
	qglGetIntegerv (GL_STENCIL_BITS, &bits);
	if (gl_shadows->value != 4)
	{	/* BDGN 3.56a: shadows "from lights" count in the stencil buffer too when there is one (gl_mesh.c) */
		if (bits >= 8)
		{
			r_shadow_stencil = 1;
			r_shadow_stencil_ref = 0;
			qglClearStencil (0);
			qglClear (GL_STENCIL_BUFFER_BIT);
		}
		return;
	}
	qglGetIntegerv (GL_DEPTH_FUNC, &func);
	if (bits >= 8 && func == GL_LEQUAL)	/* gl_ztrick turns the depth test round every other frame: not with it */
		r_volume_shadows = 1;
}

/* a model that was just drawn: its vertices (model space, this frame's blend) are kept in world space.
   dx, dy: where the static light throws the shadow (sideways per unit of height), height: above the floor under it */
void R_ShadowVolumeAdd (entity_t *e, dmdl_t *hdr, float (*lerped)[4], float dx, float dy, float height)
{
	sv_caster_t *c;
	vec3_t forward, right, up, d;
	float *out, r2 = 0;
	int i;
	if (!r_volume_shadows || gl_reflect_pass || sv_ncasters >= SV_MAX_CASTERS || hdr->num_xyz <= 0 || hdr->num_xyz > MAX_VERTS ||
	    sv_nverts + hdr->num_xyz > SV_MAX_VERTS)
		return;
	VectorSubtract (e->origin, r_newrefdef.vieworg, d);
	if (DotProduct (d, d) > 1200.0f * 1200.0f)
		return;
	c = &sv_casters[sv_ncasters++];
	c->hdr = hdr;
	c->first = sv_nverts;
	c->num = hdr->num_xyz;
	AngleVectors (e->angles, forward, right, up);
	out = sv_verts[sv_nverts];
	for (i = 0; i < hdr->num_xyz; i++, out += 3)
	{
		float *v = lerped[i], l2;
		out[0] = e->origin[0] + forward[0] * v[0] - right[0] * v[1] + up[0] * v[2];
		out[1] = e->origin[1] + forward[1] * v[0] - right[1] * v[1] + up[1] * v[2];
		out[2] = e->origin[2] + forward[2] * v[0] - right[2] * v[1] + up[2] * v[2];
		l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		if (l2 > r2) r2 = l2;
	}
	sv_nverts += hdr->num_xyz;
	VectorCopy (e->origin, c->center);
	c->radius = (float)sqrt (r2);
	VectorSet (c->dir, dx, dy, -1);
	VectorNormalize (c->dir);
	c->ext = (height + 48) * (float)sqrt (1.0f + dx * dx + dy * dy);	/* reaches the floor under it and a little more (steps); 3.55ak: also along a slanted direction (sun) */
	if (height + 48 > 200) c->ext = 0;	/* high in the air: no shadow from the room's light */
}

/* the prisms of one model for one light. point: light position, or NULL for the model's static direction */
static void SV_Emit (sv_caster_t *c, float *point, float ext)
{
	dtriangle_t *tri = (dtriangle_t *)((byte *)c->hdr + c->hdr->ofs_tris);
	float (*v)[3] = &sv_verts[c->first];
	int i, n = c->hdr->num_tris;
	for (i = 0; i < c->num; i++)
	{	/* near = a little behind the surface (so the lit side of the model is not inside its own shadow), far = end */
		vec3_t d;
		if (point)
		{
			VectorSubtract (v[i], point, d);
			VectorNormalize (d);
		}
		else
			VectorCopy (c->dir, d);
		VectorMA (v[i], 2.0f, d, sv_near[i]);
		VectorMA (v[i], ext, d, sv_far[i]);
	}
	qglBegin (GL_TRIANGLES);
	for (i = 0; i < n; i++, tri++)
	{
		int ia = tri->index_xyz[0], ib = tri->index_xyz[1], ic = tri->index_xyz[2];
		vec3_t e1, e2, nrm, tl;
		float *a, *b, *cc, *a2, *b2, *c2, facing;
		if (ia >= c->num || ib >= c->num || ic >= c->num || ia < 0 || ib < 0 || ic < 0)
			continue;
		VectorSubtract (v[ib], v[ia], e1);
		VectorSubtract (v[ic], v[ia], e2);
		CrossProduct (e1, e2, nrm);
		if (point)
		{
			VectorSubtract (point, v[ia], tl);
			facing = DotProduct (nrm, tl);
		}
		else
			facing = -DotProduct (nrm, c->dir);
		if (facing <= 0)
			continue;	/* only the triangles turned towards the light cast */
		a = sv_near[ia]; b = sv_near[ib]; cc = sv_near[ic];
		a2 = sv_far[ia]; b2 = sv_far[ib]; c2 = sv_far[ic];
		qglVertex3fv (a); qglVertex3fv (b); qglVertex3fv (cc);		/* cap at the model */
		qglVertex3fv (c2); qglVertex3fv (b2); qglVertex3fv (a2);	/* cap at the end */
		qglVertex3fv (a); qglVertex3fv (a2); qglVertex3fv (b2);		/* three sides */
		qglVertex3fv (a); qglVertex3fv (b2); qglVertex3fv (b);
		qglVertex3fv (b); qglVertex3fv (b2); qglVertex3fv (c2);
		qglVertex3fv (b); qglVertex3fv (c2); qglVertex3fv (cc);
		qglVertex3fv (cc); qglVertex3fv (c2); qglVertex3fv (a2);
		qglVertex3fv (cc); qglVertex3fv (a2); qglVertex3fv (a);
	}
	qglEnd ();
}

/* one light: point = its position (NULL = the static shadows of all models), reach, darkness. False = nothing in it. */
static qboolean SV_Round (float *point, float reach, float alpha)
{
	int pass, i, any = 0;
	for (pass = 0; pass < 2; pass++)
	{
		qglCullFace (pass ? GL_BACK : GL_FRONT);
		qglStencilOp (GL_KEEP, pass ? GL_DECR_WRAP_14 : GL_INCR_WRAP_14, GL_KEEP);
		for (i = 0; i < sv_ncasters; i++)
		{
			sv_caster_t *c = &sv_casters[i];
			float ext;
			if (point)
			{
				vec3_t d;
				float dist;
				VectorSubtract (c->center, point, d);
				dist = VectorLength (d);
				if (dist < 24 || dist > reach + c->radius)
					continue;	/* the light sits in the model (its own flash), or does not reach it */
				ext = reach - dist + 32;
				if (ext < 32) ext = 32;
				if (ext > 320) ext = 320;
			}
			else
			{
				ext = c->ext;
				if (ext <= 0)
					continue;
			}
			SV_Emit (c, point, ext);
			any = 1;
		}
		if (!any)
			return false;
	}
	/* darken what is left in the stencil, and clear it on the way */
	qglColorMask (1, 1, 1, 1);
	qglStencilFunc (GL_NOTEQUAL, 0, 0xff);
	qglStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglEnable (GL_BLEND);
	qglColor4f (0, 0, 0, alpha);
	qglMatrixMode (GL_PROJECTION);
	qglPushMatrix ();
	qglLoadIdentity ();
	qglMatrixMode (GL_MODELVIEW);
	qglPushMatrix ();
	qglLoadIdentity ();
	qglBegin (GL_QUADS);
	qglVertex3f (-1, -1, 0); qglVertex3f (1, -1, 0); qglVertex3f (1, 1, 0); qglVertex3f (-1, 1, 0);
	qglEnd ();
	qglPopMatrix ();
	qglMatrixMode (GL_PROJECTION);
	qglPopMatrix ();
	qglMatrixMode (GL_MODELVIEW);
	qglDisable (GL_BLEND);
	qglEnable (GL_CULL_FACE);
	qglEnable (GL_DEPTH_TEST);
	qglColorMask (0, 0, 0, 0);
	qglStencilFunc (GL_ALWAYS, 0, 0xff);
	return true;
}

/* after the solid models, before the translucent ones */
void R_ShadowVolumeDraw (void)
{
	if (!r_volume_shadows || !sv_ncasters)
		return;
	qglMatrixMode (GL_MODELVIEW);
	qglLoadMatrixf (r_world_matrix);
	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_BLEND);
	qglDepthMask (GL_FALSE);
	qglColorMask (0, 0, 0, 0);
	if (qglClearStencil) qglClearStencil (0);
	qglClear (GL_STENCIL_BUFFER_BIT);
	qglEnable (GL_STENCIL_TEST);
	qglStencilFunc (GL_ALWAYS, 0, 0xff);
	qglEnable (GL_CULL_FACE);

	SV_Round (NULL, 0, 0.30f);	/* the room's light */
	qglDisable (GL_STENCIL_TEST);
	qglStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
	qglColorMask (1, 1, 1, 1);
	qglCullFace (GL_FRONT);	/* Quake II's normal state */
	qglDepthMask (GL_TRUE);
	qglColor4f (1, 1, 1, 1);
	qglEnable (GL_ALPHA_TEST);
	qglEnable (GL_TEXTURE_2D);
	sv_ncasters = sv_nverts = 0;
}
