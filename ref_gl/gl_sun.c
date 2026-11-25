/*
BDGN 3.55ak: fixed light sources for the shadows - the sun and the map's lamps.   gl_sun 1

Quake II's maps are lit once, when the map is compiled; the game knows the light on every wall but not where it
comes from. The model shadows (gl_shadows 3 "from lights" and 4 "light volumes") therefore had to guess a direction
from the brightness around the model. Here the real sources are found again:

The sun. When the sky is set, the six sky pictures are searched for their brightest spot; the direction to that
spot is the sun (or moon, or the brightest star cluster). A model that can see the sky in that direction - a line
through the map from the model towards the sun ends on a sky surface - stands in sunlight and throws its shadow away
from the sun, all models in the same direction, as outdoors in reality. Indoors and behind walls it does not.

The lamps. Every surface whose texture gives off light (the map compiler's light value: ceiling lights, lamps, light
strips, monitors) is collected when the map loads; neighbours are merged into one lamp. A model that is not in the
sun takes the strongest lamp it can see (brightness / distance squared) and throws its shadow away from it.

Without either, the old estimate from the brightness around the model is used. Nothing here changes the map's own
light: walls and floors keep the light they were compiled with.

The sun's direction and colour are also what GL_DrawGodRays (gl_render_scale.c) uses.
*/
#include "gl_local.h"
#include "../qcommon/sky_profiles.h"
static const q2ttm_sky_profile_t *sky_profile;
static cvar_t *sky_lighting;

int R_SkyLighting(vec3_t tint, float *gain)
{
 if (!sky_profile || !sky_lighting || !sky_lighting->value || !gl_soft_shadow_capable) return 0;
 VectorCopy(sky_profile->tint,tint);
 *gain=sky_profile->gain;
 return 1;
}

#define MAX_LAMPS		384
#define LAMP_MERGE		96.0f
#define LAMP_RANGE		700.0f

typedef struct { vec3_t origin; float light; int count; } lamp_t;

static lamp_t	r_lamps[MAX_LAMPS];
static int		r_numlamps;
static model_t	*r_lampmodel;

vec3_t	r_sun_dir;		/* towards the sun */
vec3_t	r_sun_color;	/* 0..1 */
int		r_sun_valid;
cvar_t	*gl_sun;

extern int st_to_vec[6][3];
extern float skyrotate;
void LoadTGA (char *name, byte **pic, int *width, int *height);

void R_SunInit (void)
{
	gl_sun = ri.Cvar_Get ("gl_sun", "0", CVAR_ARCHIVE);
 sky_lighting = ri.Cvar_Get("gl_sky_lighting","1",CVAR_ARCHIVE);
}

/*
The sky pictures are 6 x 256x256 (or larger); each is read in a 32x32 grid of blocks. The sun is the block with the
highest mean brightness, if it stands out from the sky (clearly brighter than the average) and is above the horizon.
*/
static char	sun_skyname[MAX_QPATH];
static int	sun_pending;

/* the sky changed: the pictures are read when the sun is first asked for (never with gl_sun and gl_godrays off) */
void R_SunSetSky (const char *skyname)
{
 int i;
 sky_profile=NULL;
 if (gl_soft_shadow_capable && skyname)
  for (i=0;i<10;i++) if (!strcmp(skyname,q2ttm_sky_profiles[i].name)) { sky_profile=&q2ttm_sky_profiles[i]; break; }
	r_sun_valid = 0;
	sun_pending = 1;
	strncpy (sun_skyname, skyname ? skyname : "", sizeof(sun_skyname) - 1);
	sun_skyname[sizeof(sun_skyname) - 1] = 0;
}

static void R_SunFind (const char *skyname);
void R_SunCheck (void)
{
	if (!sun_pending)
		return;
	sun_pending = 0;
 if (sky_profile) {
  VectorCopy(sky_profile->direction,r_sun_dir);
  VectorCopy(sky_profile->sun,r_sun_color);
  r_sun_valid=1;
  return;
 }
	R_SunFind (sun_skyname);
}

static void R_SunFind (const char *skyname)
{
	static const int axis_of_image[6] = { 0, 2, 1, 3, 4, 5 };	/* skytexorder of gl_warp.c */
	static const char *suf[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
	char	path[MAX_QPATH];
	int		i, x, y, bx, by, w, h, j, k;
	float	best = 0, total = 0, s, t, len, b[3];
	int		ntotal = 0, bestimage = -1, bestx = 0, besty = 0;
	float	bestcolor[3];
	byte	*pic;

	r_sun_valid = 0;
	bestcolor[0] = bestcolor[1] = bestcolor[2] = 1;
	if (!skyname || !skyname[0] || skyrotate)
		return;		/* a turning sky has no fixed sun */
	for (i = 0; i < 6; i++)
	{
		int grid, step;
		pic = NULL; w = h = 0;
		Com_sprintf (path, sizeof(path), "env/%s%s.tga", skyname, suf[i]);
		LoadTGA (path, &pic, &w, &h);
		if (!pic)
			continue;
		if (w < 32 || h < 32) { free (pic); continue; }
		grid = 32;
		step = (w / grid) > 4 ? (w / grid) / 4 : 1;	/* read at most 4x4 points of a block */
		for (by = 0; by < grid; by++)
		{
			for (bx = 0; bx < grid; bx++)
			{
				int x0 = bx * w / grid, x1 = (bx + 1) * w / grid, y0 = by * h / grid, y1 = (by + 1) * h / grid, n = 0;
				float r = 0, g = 0, bl = 0, lum;
				for (y = y0; y < y1; y += step)
					for (x = x0; x < x1; x += step)
					{
						byte *p = pic + (y * w + x) * 4;
						r += p[0]; g += p[1]; bl += p[2]; n++;
					}
				if (!n)
					continue;
				r /= n; g /= n; bl /= n;
				lum = r * 0.299f + g * 0.587f + bl * 0.114f;
				total += lum; ntotal++;
				if (lum > best)
				{
					best = lum; bestimage = i; bestx = bx; besty = by;
					bestcolor[0] = r / 255.0f; bestcolor[1] = g / 255.0f; bestcolor[2] = bl / 255.0f;
				}
			}
		}
		free (pic);
	}
	if (bestimage < 0 || !ntotal)
		return;
	total /= ntotal;
	if (best < 110 || best < total * 1.35f + 16)
		return;		/* an even sky: no sun to be found */
	/* the block's place on its sky face -> direction (MakeSkyVec of gl_warp.c backwards) */
	s = (bestx + 0.5f) / 32.0f * 2 - 1;
	t = 1 - (besty + 0.5f) / 32.0f * 2;
	b[0] = s; b[1] = t; b[2] = 1;
	for (j = 0; j < 3; j++)
	{
		k = st_to_vec[axis_of_image[bestimage]][j];
		r_sun_dir[j] = k < 0 ? -b[-k - 1] : b[k - 1];
	}
	len = (float)sqrt (DotProduct (r_sun_dir, r_sun_dir));
	if (len < 0.001f)
		return;
	VectorScale (r_sun_dir, 1.0f / len, r_sun_dir);
	if (r_sun_dir[2] < 0.12f)
		return;		/* at or below the horizon: no shadows on the ground from it */
	len = bestcolor[0] > bestcolor[1] ? bestcolor[0] : bestcolor[1];
	if (bestcolor[2] > len) len = bestcolor[2];
	if (len < 0.01f) len = 1;
	VectorScale (bestcolor, 1.0f / len, r_sun_color);
	r_sun_valid = 1;
	ri.Con_Printf (PRINT_DEVELOPER, "sun: sky %s, direction %.2f %.2f %.2f (brightness %.0f, sky average %.0f)\n",
		skyname, r_sun_dir[0], r_sun_dir[1], r_sun_dir[2], best, total);
}

/*
The first surface a line through the map meets. Returns 1 for a sky surface, 2 for any other, 0 for none.
Glass and water do not stop the line.
*/
static int R_TraceNode (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side, i, r, s, t;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	mtexinfo_t	*tex;

	if (node->contents != -1)
		return 0;
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	if ((back < 0) == side)
		return R_TraceNode (node->children[side], start, end);
	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;
	r = R_TraceNode (node->children[side], start, mid);
	if (r)
		return r;
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		tex = surf->texinfo;
		if ((tex->flags & (SURF_TRANS33|SURF_TRANS66|SURF_NODRAW)) || ((surf->flags & SURF_DRAWTURB) && !(tex->flags & SURF_SKY)))
			continue;
		s = (int)(DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3]);
		t = (int)(DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3]);
		if (s < surf->texturemins[0] || t < surf->texturemins[1] ||
		    s - surf->texturemins[0] > surf->extents[0] || t - surf->texturemins[1] > surf->extents[1])
			continue;
		return (tex->flags & SURF_SKY) ? 1 : 2;
	}
	return R_TraceNode (node->children[!side], mid, end);
}

static int R_TraceWorld (vec3_t start, vec3_t end)
{
	if (!r_worldmodel || !r_worldmodel->nodes)
		return 0;
	return R_TraceNode (r_worldmodel->nodes, start, end);
}

/* collect the light-giving surfaces of the map */
void R_SunLoadMap (void)
{
	msurface_t	*surf;
	int			i, j, n;

	r_numlamps = 0;
	r_lampmodel = r_worldmodel;
	if (!r_worldmodel)
		return;
	for (i = 0, surf = r_worldmodel->surfaces; i < r_worldmodel->numsurfaces; i++, surf++)
	{
		mtexinfo_t	*tex = surf->texinfo;
		glpoly_t	*poly = surf->polys;
		vec3_t		c;
		float		light, sign, d;
		lamp_t		*lamp;

		if (!(tex->flags & SURF_LIGHT) || (tex->flags & (SURF_SKY|SURF_WARP|SURF_NODRAW)) || tex->value < 80 || !poly || poly->numverts < 3)
			continue;
		if (surf->extents[0] > 384 || surf->extents[1] > 384)
			continue;		/* a whole glowing wall is not a lamp */
		VectorClear (c);
		for (j = 0; j < poly->numverts; j++)
			VectorAdd (c, poly->verts[j], c);
		VectorScale (c, 1.0f / poly->numverts, c);
		sign = (surf->flags & SURF_PLANEBACK) ? -1.0f : 1.0f;
		VectorMA (c, 10 * sign, surf->plane->normal, c);
		/* brightness: the texture's value times the area in units of 64x64 */
		light = tex->value * ((surf->extents[0] + 16) * (surf->extents[1] + 16) / 4096.0f);
		for (n = 0, lamp = r_lamps; n < r_numlamps; n++, lamp++)
		{
			vec3_t v;
			VectorSubtract (lamp->origin, c, v);
			d = DotProduct (v, v);
			if (d < LAMP_MERGE * LAMP_MERGE)
				break;
		}
		if (n == r_numlamps)
		{
			if (r_numlamps == MAX_LAMPS)
				continue;
			r_numlamps++;
			VectorCopy (c, lamp->origin);
			lamp->light = light;
			lamp->count = 1;
		}
		else
		{	/* merge: weighted place, summed brightness */
			float wsum = lamp->light + light;
			if (wsum > 0)
				for (j = 0; j < 3; j++)
					lamp->origin[j] = (lamp->origin[j] * lamp->light + c[j] * light) / wsum;
			lamp->light = wsum;
			lamp->count++;
		}
	}
	ri.Con_Printf (PRINT_DEVELOPER, "lamps: %d light sources from light-giving textures\n", r_numlamps);
}

/*
The fixed light a model at 'origin' stands in. dx, dy: how far the shadow moves sideways per unit of height above
the floor (away from the light). Returns the strength 0..1, 0 = no fixed light found.
*/
float R_SunShadowDirection (vec3_t origin, float *dx, float *dy)
{
	vec3_t	start, end;
	int		i, tries;
	float	best[3];
	int		bestlamp[3];

	if (!gl_sun || !gl_sun->value || !r_worldmodel || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return 0;
	VectorCopy (origin, start);
	start[2] += 20;
	R_SunCheck ();
	if (r_sun_valid)
	{
		VectorMA (start, 8192, r_sun_dir, end);
		if (R_TraceWorld (start, end) == 1)
		{
			*dx = -r_sun_dir[0] / r_sun_dir[2];
			*dy = -r_sun_dir[1] / r_sun_dir[2];
			return 1;
		}
	}
	if (r_lampmodel != r_worldmodel)
		R_SunLoadMap ();
	/* the three strongest lamps in reach, then the first of them the model can see */
	best[0] = best[1] = best[2] = 0;
	bestlamp[0] = bestlamp[1] = bestlamp[2] = -1;
	for (i = 0; i < r_numlamps; i++)
	{
		vec3_t	v;
		float	d2, score;
		int		k, m;
		VectorSubtract (r_lamps[i].origin, start, v);
		if (v[2] < 24)
			continue;		/* not above the model */
		d2 = DotProduct (v, v);
		if (d2 > LAMP_RANGE * LAMP_RANGE)
			continue;
		score = r_lamps[i].light / (d2 + 1024.0f);
		for (k = 0; k < 3; k++)
			if (score > best[k])
			{
				for (m = 2; m > k; m--) { best[m] = best[m - 1]; bestlamp[m] = bestlamp[m - 1]; }
				best[k] = score; bestlamp[k] = i;
				break;
			}
	}
	for (tries = 0; tries < 3 && bestlamp[tries] >= 0; tries++)
	{
		vec3_t	v;
		float	hz, k;
		VectorCopy (r_lamps[bestlamp[tries]].origin, end);
		if (R_TraceWorld (start, end))
			continue;		/* something in between */
		VectorSubtract (origin, end, v);
		hz = -v[2];
		if (hz < 16) hz = 16;
		*dx = v[0] / hz;
		*dy = v[1] / hz;
		k = best[tries] * 6.0f;	/* a 64x64 lamp of value 1000 at 250 units: about 1 */
		if (k > 1) k = 1;
		if (k < 0.35f) k = 0.35f;
		return k;
	}
	return 0;
}
