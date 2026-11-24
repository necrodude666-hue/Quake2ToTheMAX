/*
gl_decal.c -- BDGN 3.55y: marks on the world (bullet holes, scorch, blaster and rail burns, blood) and textured
effect beams (the HD railgun).

Decals (RF_DECAL, client cl_decals.c): origin = the hit point, angles turn +x onto the surface normal (roll = the
picture's rotation), frame = half size in units, skinnum = picture in the 4x4 atlas of the model's skin (or the
entity's custom skin), oldframe = the client's decal id, lightstyle = 0 darken / 1 glow, alpha = strength.
Like Quake III's marks, the square is clipped against the world surfaces it touches (found through the BSP
nodes whose plane is within reach), so a mark at a corner wraps around it and never hangs in the air. The pieces
are kept in a cache by id. Darkening marks multiply the picture over the surface (the atlas holds how much each
colour channel is darkened), so they look right in any light; glow marks add.

Beams (RF_FXBEAM): from origin to oldorigin, frame = width, oldframe = 0 straight ribbon facing the viewer /
1 helix ribbon around the line (lightstyle = radius, angles[2] = phase in degrees), skinnum = the model's skin, alpha; drawn additive, the
texture runs across (u) and along (v, repeating every 2 widths).
*/
#include "gl_local.h"
#include "gl_decal_light.h"	/* BDGN 3.56b: blood mark brightness */

#define DECAL_CACHE		1024	/* 3.55z: power of two, slot = id & (DECAL_CACHE-1) (the client keeps at most 512 alive) */
#define DECAL_MAXVERTS	96
#define DECAL_MAXFRAGS	8

typedef struct
{
	int		id;
	vec3_t	origin, normal;
	int		size, numverts, numfrags;
	int		fragstart[DECAL_MAXFRAGS], fraglen[DECAL_MAXFRAGS];
	float	verts[DECAL_MAXVERTS][5];	/* xyz st */
	msurface_t	*lsurf;					/* 3.56b: the surface whose light the mark takes (its centre lies on it if possible) */
	int		ls, lt, linside;			/* texture coordinates of the centre on lsurf; 1 = inside that surface */
} decal_cache_t;

static decal_cache_t	decal_cache[DECAL_CACHE];
/* BDGN 3.59r: which slots have been written. The cache is 2 MB; clearing every id at each map load touched all of its
   memory pages, so Windows had to give the whole 2 MB real RAM even with decals off (low RAM computers). Only the
   written slots are cleared now; an untouched slot is still zero from the start. */
static byte				decal_used[DECAL_CACHE];
static int				decal_cache_next;
static model_t			*decal_world;

static int				decal_mark;	/* per search */
static int				*decal_surfmark;
static int				decal_surfcount;

void R_DecalClearCache (void)
{
	int i;
	for (i = 0; i < DECAL_CACHE; i++)
		if (decal_used[i])
		{
			decal_cache[i].id = 0;
			decal_used[i] = 0;
		}
	decal_cache_next = 0;
	decal_world = NULL;
	if (decal_surfmark) free (decal_surfmark);
	decal_surfmark = NULL;
	decal_surfcount = 0;
}

/* Sutherland-Hodgman: keep the part with dot(p, n) - d <= 0 */
static int R_DecalClip (float in[][5], int n, float out[][5], const vec3_t pn, float pd)
{
	int i, k = 0;
	for (i = 0; i < n && k < DECAL_MAXVERTS - 1; i++)
	{
		float *a = in[i], *b = in[(i + 1) % n];
		float da = DotProduct (a, pn) - pd, db = DotProduct (b, pn) - pd;
		if (da <= 0)
		{
			memcpy (out[k++], a, sizeof(float) * 5);
		}
		if ((da <= 0) != (db <= 0) && k < DECAL_MAXVERTS - 1)
		{
			float t = da / (da - db);
			int j;
			for (j = 0; j < 5; j++)
				out[k][j] = a[j] + (b[j] - a[j]) * t;
			k++;
		}
	}
	return k;
}

typedef struct
{
	decal_cache_t	*c;
	vec3_t			origin, normal, right, up;
	float			size;
} decal_build_t;

static void R_DecalSurface (decal_build_t *b, msurface_t *fa)
{
	static float	pa[DECAL_MAXVERTS][5], pb[DECAL_MAXVERTS][5];
	glpoly_t		*p = fa->polys;
	cplane_t		*pl = fa->plane;
	vec3_t			sn, cn;
	float			dist;
	int				i, n;

	if (!p || !pl || !fa->texinfo || (fa->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_NODRAW)))
		return;
	if (fa->flags & SURF_PLANEBACK) { VectorNegate (pl->normal, sn); } else { VectorCopy (pl->normal, sn); }
	if (DotProduct (sn, b->normal) < 0.5f)
		return;	/* facing away or too steep against the mark */
	dist = DotProduct (b->origin, sn) - ((fa->flags & SURF_PLANEBACK) ? -pl->dist : pl->dist);
	if (dist > b->size || dist < -b->size)
		return;
	n = p->numverts;
	if (n > DECAL_MAXVERTS - 8) return;
	for (i = 0; i < n; i++)
	{
		VectorCopy (p->verts[i], pa[i]);
		pa[i][3] = pa[i][4] = 0;
	}
	/* the mark's square (4 sides) and a depth of +-size */
	VectorCopy (b->right, cn); n = R_DecalClip (pa, n, pb, cn, DotProduct (b->origin, cn) + b->size); if (n < 3) return;
	VectorNegate (b->right, cn); n = R_DecalClip (pb, n, pa, cn, DotProduct (b->origin, cn) + b->size); if (n < 3) return;
	VectorCopy (b->up, cn); n = R_DecalClip (pa, n, pb, cn, DotProduct (b->origin, cn) + b->size); if (n < 3) return;
	VectorNegate (b->up, cn); n = R_DecalClip (pb, n, pa, cn, DotProduct (b->origin, cn) + b->size); if (n < 3) return;
	if (b->c->numfrags >= DECAL_MAXFRAGS || b->c->numverts + n > DECAL_MAXVERTS)
		return;
	b->c->fragstart[b->c->numfrags] = b->c->numverts;
	b->c->fraglen[b->c->numfrags] = n;
	b->c->numfrags++;
	if (!(fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY)))
	{	/* 3.56b: the light comes from the surface the mark's centre lies on (else the first one it touches) */
		mtexinfo_t *tex = fa->texinfo;
		int s = (int)(DotProduct (b->origin, tex->vecs[0]) + tex->vecs[0][3]);
		int t = (int)(DotProduct (b->origin, tex->vecs[1]) + tex->vecs[1][3]);
		int ds = s - fa->texturemins[0], dt = t - fa->texturemins[1];
		int inside = ds >= 0 && dt >= 0 && ds <= fa->extents[0] && dt <= fa->extents[1];
		if (!b->c->lsurf || (inside && !b->c->linside))
		{
			b->c->lsurf = fa;
			b->c->ls = s;
			b->c->lt = t;
			b->c->linside = inside;
		}
	}
	for (i = 0; i < n; i++)
	{
		float *v = b->c->verts[b->c->numverts++];
		vec3_t d;
		VectorCopy (pa[i], v);
		VectorSubtract (pa[i], b->origin, d);
		v[3] = 0.5f + DotProduct (d, b->right) / (2 * b->size);
		v[4] = 0.5f - DotProduct (d, b->up) / (2 * b->size);
	}
}

static void R_DecalNode (decal_build_t *b, mnode_t *node)
{
	float d;
	int i;
	msurface_t *surf;
	while (node->contents == -1)
	{
		d = DotProduct (b->origin, node->plane->normal) - node->plane->dist;
		if (d > b->size * 1.5f) { node = node->children[0]; continue; }
		if (d < -b->size * 1.5f) { node = node->children[1]; continue; }
		surf = r_worldmodel->surfaces + node->firstsurface;
		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			int idx = (int)(surf - r_worldmodel->surfaces);
			if (idx < 0 || idx >= decal_surfcount || decal_surfmark[idx] == decal_mark) continue;
			decal_surfmark[idx] = decal_mark;
			R_DecalSurface (b, surf);
		}
		R_DecalNode (b, node->children[0]);
		node = node->children[1];
	}
}

static decal_cache_t *R_DecalFind (entity_t *e)
{
	int i;
	decal_cache_t *c;
	decal_build_t b;
	vec3_t fwd;
	if (decal_world != r_worldmodel)
	{
		R_DecalClearCache ();
		decal_world = r_worldmodel;
	}
	c = &decal_cache[e->oldframe & (DECAL_CACHE - 1)];	/* 3.55z: direct slot instead of a search over all */
	if (c->id == e->oldframe && c->size == e->frame && VectorCompare (c->origin, e->origin))
		return c;
	(void)i;
	if (!r_worldmodel || !r_worldmodel->nodes || e->frame < 1)
		return NULL;
	if (decal_surfcount != r_worldmodel->numsurfaces)
	{
		if (decal_surfmark) free (decal_surfmark);
		decal_surfcount = r_worldmodel->numsurfaces;
		decal_surfmark = (int *)calloc (decal_surfcount > 0 ? decal_surfcount : 1, sizeof(int));
		if (!decal_surfmark) { decal_surfcount = 0; return NULL; }
	}
	decal_mark++;
	memset (c, 0, sizeof(*c));
	decal_used[c - decal_cache] = 1;
	c->id = e->oldframe;
	c->size = e->frame;
	VectorCopy (e->origin, c->origin);
	AngleVectors (e->angles, fwd, b.right, b.up);
	VectorCopy (fwd, c->normal);
	b.c = c;
	VectorCopy (e->origin, b.origin);
	VectorCopy (fwd, b.normal);
	b.size = (float)e->frame;
	R_DecalNode (&b, r_worldmodel->nodes);
	return c;
}

/* after the opaque entities, before the translucent ones */
void R_DrawDecals (void)
{
	int i, k, first = 1, mode = -1;
	image_t *bound = NULL;
	if (gl_reflect_pass || !r_worldmodel)
		return;
	for (i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t *e = &r_newrefdef.entities[i];
		decal_cache_t *c;
		image_t *skin;
		float u0, v0, a;
		model_t *m = (model_t *)e->model;
		if (!(e->flags & RF_DECAL) || (e->flags&RF_ACTOR_MARK)==RF_ACTOR_MARK || !m || m->type != mod_alias)
			continue;
		{	/* 3.55ab: marks outside the view are skipped before anything else (up to 512 of them) */
			vec3_t mins, maxs;
			float r = (float)e->frame * 1.5f;
			VectorSet (mins, e->origin[0] - r, e->origin[1] - r, e->origin[2] - r);
			VectorSet (maxs, e->origin[0] + r, e->origin[1] + r, e->origin[2] + r);
			if (R_CullBox (mins, maxs))
				continue;
		}
		c = R_DecalFind (e);
		if (!c || !c->numfrags)
			continue;
		skin = e->skin ? (image_t *)e->skin : ((e->skinnum >> 4) < MAX_MD2SKINS && m->skins[e->skinnum >> 4] ? m->skins[e->skinnum >> 4] : m->skins[0]);
		if (!skin) continue;	/* 3.55z: skinnum = skin * 16 + tile (second atlas: blood) */
		if (first)
		{
			first = 0;
			qglDepthMask (0);
			qglEnable (GL_BLEND);
			qglDisable (GL_ALPHA_TEST);
			qglEnable (GL_POLYGON_OFFSET_FILL);
			qglPolygonOffset (-1, -4);
			GL_TexEnv (GL_MODULATE);
			qglShadeModel (GL_SMOOTH);
		}
		if (skin != bound) { GL_Bind (skin->texnum); bound = skin; }
		if (e->lightstyle != mode)
		{
			mode = e->lightstyle;
			if (mode == 2) qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	/* paint (3.55ae: blood) */
			else if (mode) qglBlendFunc (GL_SRC_ALPHA, GL_ONE);	/* glow */
			else qglBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_COLOR);	/* surface * (1 - darkness * strength) */
		}
		a = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
		if (a > 1) a = 1;
		if (mode == 2)
		{	/* BDGN 3.55ae: blood is painted in its own colour (red), not darkened into the wall (that gave black blood
			   on dark walls). 3.56b: lit like the surface it lies on - that surface's lightmap at the mark's centre,
			   read every frame (follows flickering lights and gl_modulate), on the wall's scale, minimum 12 %
			   (3.55af-3.56a: 0.44 + 0.7 x R_LightPoint once per mark: the light of the floor below, gl_modulate twice,
			   the muzzle flash of the last drawn entity - blood glowed in the dark) */
			vec3_t l;
			int q;
			if (GL_FULLBRIGHT () || !r_worldmodel->lightdata)
				l[0] = l[1] = l[2] = 1.0f;
			else if (c->lsurf)
			{
				const float *rgb[MAXLIGHTMAPS];
				for (q = 0; q < MAXLIGHTMAPS; q++)
					rgb[q] = c->lsurf->styles[q] != 255 ? r_newrefdef.lightstyles[c->lsurf->styles[q]].rgb : NULL;
				R_LightmapTexel (c->lsurf->samples, c->lsurf->texturemins, c->lsurf->extents, c->lsurf->styles, rgb,
					gl_modulate->value, c->ls, c->lt, l);
			}
			else
				R_LightPointWorld (e->origin, l);
			for (q = 0; q < 3; q++)
				l[q] = R_DecalBloodLight (l[q]);
			{	/* 3.55ah: the client gives the shade in backlerp (amount of blood and how dry it is) */
				float shade = e->backlerp > 0 ? e->backlerp : 1.0f;
				qglColor4f (l[0] * shade > 1 ? 1 : l[0] * shade, l[1] * shade > 1 ? 1 : l[1] * shade,
					l[2] * shade > 1 ? 1 : l[2] * shade, a);
			}

		}
		else if (mode) qglColor4f (1, 1, 1, a);
		else qglColor4f (a, a, a, 1);
		u0 = (e->skinnum & 3) * 0.25f;
		v0 = ((e->skinnum >> 2) & 3) * 0.25f;
		for (k = 0; k < c->numfrags; k++)
		{
			int j;
			qglBegin (GL_POLYGON);
			for (j = 0; j < c->fraglen[k]; j++)
			{
				float *v = c->verts[c->fragstart[k] + j];
				qglTexCoord2f (u0 + 0.004f + v[3] * 0.242f, v0 + 0.004f + v[4] * 0.242f);
				qglVertex3fv (v);
			}
			qglEnd ();
		}
	}
	if (!first)
	{
		qglPolygonOffset (0, 0);
		qglDisable (GL_POLYGON_OFFSET_FILL);
		qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		qglDisable (GL_BLEND);
		qglDepthMask (1);
		qglColor4f (1, 1, 1, 1);
		GL_TexEnv (GL_REPLACE);
		qglShadeModel (GL_FLAT);
	}
}

/* RF_FXBEAM, from R_DrawEntitiesOnList (translucent pass) */
qboolean R_DrawFXBeam (entity_t *e)
{
	model_t *m = (model_t *)e->model;
	image_t *skin;
	vec3_t d, u, v, mid, toeye, side;
	float len, w = (float)e->frame, a = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
	int i, n;
	if (!(e->flags & RF_FXBEAM))
		return false;
	if (!m || m->type != mod_alias || w <= 0)
		return true;
	skin = e->skin ? (image_t *)e->skin : ((e->skinnum >= 0 && e->skinnum < MAX_MD2SKINS && m->skins[e->skinnum]) ? m->skins[e->skinnum] : m->skins[0]);
	if (!skin) return true;
	VectorSubtract (e->oldorigin, e->origin, d);
	len = VectorNormalize (d);
	if (len < 1) return true;
	GL_Bind (skin->texnum);
	GL_TexEnv (GL_MODULATE);
	qglEnable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDisable (GL_ALPHA_TEST);
	qglDisable (GL_CULL_FACE);
	qglShadeModel (GL_SMOOTH);
	qglColor4f (1, 1, 1, a > 1 ? 1 : a);
	if (e->oldframe == 0)
	{	/* straight ribbon facing the viewer */
		VectorMA (e->origin, len * 0.5f, d, mid);
		VectorSubtract (r_origin, mid, toeye);
		CrossProduct (d, toeye, side);
		VectorNormalize (side);
		VectorScale (side, w * 0.5f, side);
		qglBegin (GL_QUADS);
		qglTexCoord2f (0, 0); qglVertex3f (e->origin[0] - side[0], e->origin[1] - side[1], e->origin[2] - side[2]);
		qglTexCoord2f (1, 0); qglVertex3f (e->origin[0] + side[0], e->origin[1] + side[1], e->origin[2] + side[2]);
		qglTexCoord2f (1, len / (w * 2)); qglVertex3f (e->oldorigin[0] + side[0], e->oldorigin[1] + side[1], e->oldorigin[2] + side[2]);
		qglTexCoord2f (0, len / (w * 2)); qglVertex3f (e->oldorigin[0] - side[0], e->oldorigin[1] - side[1], e->oldorigin[2] - side[2]);
		qglEnd ();
	}
	else
	{	/* helix: a ribbon spiralling around the line, 40 units per turn */
		float r = (float)e->lightstyle;
		VectorSet (u, 0, 0, 1);
		if (fabs (DotProduct (u, d)) > 0.9f) VectorSet (u, 1, 0, 0);
		CrossProduct (d, u, v); VectorNormalize (v);
		CrossProduct (v, d, u); VectorNormalize (u);
		n = (int)(len / 4.0f);
		if (n > 1024) n = 1024;
		if (n < 2) n = 2;
		qglBegin (GL_QUAD_STRIP);
		for (i = 0; i <= n; i++)
		{
			float t = len * i / n, ang = t * (2 * (float)M_PI / 40.0f) + e->angles[2] * ((float)M_PI / 180.0f), c = (float)cos (ang), s = (float)sin (ang);
			vec3_t p, rad, tang, sd;
			int k;
			for (k = 0; k < 3; k++)
			{
				rad[k] = c * u[k] + s * v[k];
				p[k] = e->origin[k] + d[k] * t + rad[k] * r;
			}
			for (k = 0; k < 3; k++)	/* direction of the helix at this point */
				tang[k] = d[k] * 40.0f / (2 * (float)M_PI) + (-s * u[k] + c * v[k]) * r;
			VectorSubtract (r_origin, p, toeye);
			CrossProduct (tang, toeye, sd);
			VectorNormalize (sd);
			VectorScale (sd, w * 0.5f, sd);
			qglTexCoord2f (0, t / (w * 2)); qglVertex3f (p[0] - sd[0], p[1] - sd[1], p[2] - sd[2]);
			qglTexCoord2f (1, t / (w * 2)); qglVertex3f (p[0] + sd[0], p[1] + sd[1], p[2] + sd[2]);
		}
		qglEnd ();
	}
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDisable (GL_BLEND);
	qglEnable (GL_CULL_FACE);
	qglColor4f (1, 1, 1, 1);
	GL_TexEnv (GL_REPLACE);
	qglShadeModel (GL_FLAT);
	return true;
}
