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
// cl_view.c -- player rendering positioning

#include "client.h"
#include "cl_strings.h"	/* BDGN 3.51ar: bounded copies of server strings */

//=============
//
// development tools for weapons
//
int			gun_frame;
struct model_s	*gun_model;

//=============

cvar_t		*crosshair;
cvar_t		*cl_testparticles;
cvar_t		*cl_testentities;
cvar_t		*cl_testlights;
cvar_t		*cl_testblend;
cvar_t		*cl_fastsqrt;
cvar_t		*cl_fastsqrt2;	// TEST: also pmove / game logic / culling	// BDGN 3.37 TEST: fastsqrt 0 = sqrt, 1 = Q_rsqrt, 2 = 3DNow! rsqrt (client effects, renderer)
cvar_t		*cl_fovaspect;	// BDGN 3.37: 1 = fov is for a 4:3 screen and is widened on wider screens (Hor+)

cvar_t		*cl_stats;


int			r_numdlights;
dlight_t	r_dlights[MAX_DLIGHTS];

int			r_numentities;
entity_t	r_entities[MAX_ENTITIES];

int			r_numparticles;
particle_t	r_particles[MAX_PARTICLES];

lightstyle_t	r_lightstyles[MAX_LIGHTSTYLES];

char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;

/*
====================
V_ClearScene

Specifies the model that will be used as the world
====================
*/
static int r_numclassic;	/* BDGN 3.55y: ordinary entities this frame */
/*
BDGN 3.56s: effects never cut into walls, floors or ceilings.
Every effect model (sprites, the 3D explosions and smoke, the classic explosion and smoke models) gets its size per
frame when it is registered (sp2 frame rectangle, md2 frame bounds). When such an entity is added to the scene its
origin is pushed out of the nearby walls (six short traces along the axes) until a sphere of that size is free, so the
picture stays in front of the surface: a camera-facing sprite whose centre is at least its radius from a plane never
crosses it. Only the copy that goes to the renderer moves; nothing changes on the server. Always on (3.58d, owner: no toggle).
*/
#define FXB_MAX 256
#define FXB_PER_FRAME 96	/* at most this many effects per frame are checked (6 point traces each) */
typedef struct
{
	struct model_s	*model;
	int				frames;
	float			*radius;	/* 4 per frame: centre x, y, z (model space) and the radius around it */
} fxbound_t;
static fxbound_t fxb[FXB_MAX];
static int fxb_count, fxb_checked;
static struct model_s *(*fxb_register) (char *name);

static qboolean FXB_IsEffect (const char *n)
{
	const char *b;
	if (!n || n[0] == '*')
		return false;
	if (!Q_strncasecmp ((char *)n, "sprites/", 8))
	{	/* BDGN 3.57s: only effect sprites (id's s_*, BDGN's bdgn_*, hdfx/, bdgn_fx/). The 2D items, 2D enemies and
		   other pictures of classic 2d sprites mode stand on the floor: the keep-out (3.56s) pushed them up off it,
		   so 2D barrels and weapons hung in the air (owner, 3.57r) */
		const char *s = n + 8;
		return !Q_strncasecmp ((char *)s, "s_", 2) || !Q_strncasecmp ((char *)s, "bdgn_", 5) ||
			!Q_strncasecmp ((char *)s, "hdfx/", 5);
	}
	if (!Q_strncasecmp ((char *)n, "models/objects/r_explode/", 25) || !Q_strncasecmp ((char *)n, "models/objects/explode/", 23) ||
		!Q_strncasecmp ((char *)n, "models/objects/b_explode/", 25) || !Q_strncasecmp ((char *)n, "models/objects/smoke/", 21))
		return true;
	b = strrchr (n, '/');
	b = b ? b + 1 : n;
	if (!Q_strncasecmp ((char *)n, "models/hdfx3d/", 14))	/* not the muzzle flashes (on the gun) or the flying bolts */
	{
		if (!Q_strncasecmp ((char *)b, "mz_", 3))
			return !Q_strncasecmp ((char *)b, "mz_smoke", 8);
		return Q_strncasecmp ((char *)b, "blaster_", 8) && Q_strncasecmp ((char *)b, "hyper_", 6);
	}
	if (!Q_strncasecmp ((char *)n, "models/hdgore/", 14))	/* explosions and smoke clouds of the gore pack, not gibs, rails or bolts */
		return !Q_strncasecmp ((char *)b, "grenade_", 8) || !Q_strncasecmp ((char *)b, "bfg_smoke", 9) ||
			((strstr (b, "_smoke") != NULL) && Q_strncasecmp ((char *)b, "rail", 4));
	return false;
}

/* per frame: centre (model space) and radius of the sphere around everything drawn; out holds 4 floats per frame */
static int FXB_Read (const char *name, float *out, int max)
{
	byte *buf;
	int len, n = 0, i, k;
	len = FS_LoadFile ((char *)name, (void **)&buf);
	if (!buf)
		return 0;
	if (len >= 12 && !memcmp (buf, "IDS2", 4))
	{
		int num = LittleLong (((int *)buf)[2]);
		for (i = 0; i < num && i < max && 12 + (i + 1) * 80 <= len; i++)
		{
			int *f = (int *)(buf + 12 + i * 80);
			float w = (float)LittleLong (f[0]), h = (float)LittleLong (f[1]), ox = (float)LittleLong (f[2]), oy = (float)LittleLong (f[3]);
			float x = ox > w - ox ? ox : w - ox, y = oy > h - oy ? oy : h - oy;
			out[n * 4] = out[n * 4 + 1] = out[n * 4 + 2] = 0;	/* a sprite turns to face the view around its origin */
			out[n * 4 + 3] = (float)sqrt (x * x + y * y);
			n++;
		}
	}
	else if (len >= 68 && !memcmp (buf, "IDP2", 4))
	{
		int *h = (int *)buf;
		int framesize = LittleLong (h[4]), numxyz = LittleLong (h[6]), num = LittleLong (h[10]), ofs = LittleLong (h[14]);
		for (i = 0; i < num && i < max && framesize > 40 && ofs + (i + 1) * framesize <= len && numxyz * 4 + 40 <= framesize; i++)
		{
			byte *f = buf + ofs + i * framesize;
			float sc[3], tr[3], mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f }, c[3], best = 0;
			for (k = 0; k < 3; k++)
			{
				sc[k] = LittleFloat (((float *)f)[k]);
				tr[k] = LittleFloat (((float *)f)[3 + k]);
			}
			for (k = 0; k < numxyz; k++)
			{
				byte *v = f + 40 + k * 4;
				int j;
				for (j = 0; j < 3; j++)
				{
					float x = v[j] * sc[j] + tr[j];
					if (x < mn[j]) mn[j] = x;
					if (x > mx[j]) mx[j] = x;
				}
			}
			for (k = 0; k < 3; k++)
				c[k] = numxyz ? (mn[k] + mx[k]) * 0.5f : 0;
			for (k = 0; k < numxyz; k++)
			{
				byte *v = f + 40 + k * 4;
				float x = v[0] * sc[0] + tr[0] - c[0], y = v[1] * sc[1] + tr[1] - c[1], z = v[2] * sc[2] + tr[2] - c[2], d = x * x + y * y + z * z;
				if (d > best)
					best = d;
			}
			out[n * 4] = c[0];
			out[n * 4 + 1] = c[1];
			out[n * 4 + 2] = c[2];
			out[n * 4 + 3] = (float)sqrt (best);
			n++;
		}
	}
	FS_FreeFile (buf);
	return n;
}

static void FXB_Forget (struct model_s *m)
{
	int i;
	for (i = 0; i < fxb_count; i++)
		if (fxb[i].model == m)
		{
			Z_Free (fxb[i].radius);
			fxb[i] = fxb[--fxb_count];
			return;
		}
}

static struct model_s *FXB_RegisterModel (char *name)
{
	struct model_s *m = fxb_register (name);
	if (!m)
		return m;
	FXB_Forget (m);	/* the renderer reuses model slots between maps */
	if (FXB_IsEffect (name) && fxb_count < FXB_MAX)
	{
		static float tmp[512 * 4];
		int n = FXB_Read (name, tmp, 512);
		if (n > 0)
		{
			fxb[fxb_count].model = m;
			fxb[fxb_count].frames = n;
			fxb[fxb_count].radius = Z_Malloc (n * 4 * sizeof(float));
			memcpy (fxb[fxb_count].radius, tmp, n * 4 * sizeof(float));
			fxb_count++;
		}
	}
	return m;
}

/* win32/vid_dll.c, after a renderer is loaded: every model registration goes through FXB_RegisterModel */
void CL_FXB_Hook (void)
{
	int i;
	for (i = 0; i < fxb_count; i++)
		Z_Free (fxb[i].radius);
	fxb_count = 0;
	if (re.RegisterModel && re.RegisterModel != FXB_RegisterModel)
	{
		fxb_register = re.RegisterModel;
		re.RegisterModel = FXB_RegisterModel;
	}
}

static void FXB_KeepOut (entity_t *e)
{
	int i, k, a, b;
	float r, back, sc = 1, *fa, *fb;
	vec3_t end, push, centre, fwd, right, up, c;
	trace_t tr;
	if (!e->model || fxb_checked >= FXB_PER_FRAME ||
		(e->flags & (RF_WEAPONMODEL | RF_DEPTHHACK | RF_VIEWERMODEL | RF_BEAM | RF_DISTORT | RF_DECAL | RF_FXBEAM)))
		return;
	for (i = 0; i < fxb_count && fxb[i].model != e->model; i++)
		;
	if (i == fxb_count)
		return;
	/* 3.56x: the size between the two frames the renderer blends (lerped), not the bigger of the two: the push
	   moved in steps at the model's frame rate, so the blaster puffs and the smoke clouds jerked near walls */
	a = e->frame < 0 ? 0 : (e->frame >= fxb[i].frames ? fxb[i].frames - 1 : e->frame);
	b = e->oldframe < 0 ? 0 : (e->oldframe >= fxb[i].frames ? fxb[i].frames - 1 : e->oldframe);
	back = (a != b && e->backlerp > 0 && e->backlerp < 1) ? e->backlerp : 0;
	fa = fxb[i].radius + a * 4;
	fb = fxb[i].radius + b * 4;
	for (k = 0; k < 3; k++)
		c[k] = fa[k] * (1 - back) + fb[k] * back;
	r = fa[3] * (1 - back) + fb[3] * back;
	if ((e->flags & RF_PUFFS) && !(e->flags & RF_RAILSMOKE_SHORT) && (e->lightstyle & FX_PUFF_SCALE_CENTRE))
		r *= ((e->lightstyle >> 16) & 255) / 128.0f;	/* 3.57h: scaled about its middle: the centre stays */
	else
	{
		if ((e->flags & RF_PUFFS) && !(e->flags & RF_RAILSMOKE_SHORT) && e->lightstyle >= FX_PUFF_SCALE_ONE)
			sc = ((e->lightstyle >> 16) & 255) / 255.0f;	/* 3.56x: a cloud that grows from nothing */
		r *= sc;
		VectorScale (c, sc, c);
	}
	if (r < 2)
		return;
	if (r > 160)
		r = 160;	/* the biggest clouds would leave their room */
	fxb_checked++;
	/* the sphere around the drawing: model x forward, y left, z up (as the renderer turns it) */
	AngleVectors (e->angles, fwd, right, up);
	for (k = 0; k < 3; k++)
		centre[k] = e->origin[k] + c[0] * fwd[k] - c[1] * right[k] + c[2] * up[k];
	if (CM_PointContents (centre, 0) & MASK_SOLID)
	{
		VectorCopy (e->origin, centre);	/* the centre is in the wall: check around the origin instead */
		if (CM_PointContents (centre, 0) & MASK_SOLID)
			return;	/* inside a wall: nothing sensible to do */
	}
	VectorClear (push);
	for (k = 0; k < 6; k++)
	{
		VectorCopy (centre, end);
		end[k >> 1] += (k & 1) ? -r : r;
		tr = CM_BoxTrace (centre, end, vec3_origin, vec3_origin, 0, MASK_SOLID);
		if (tr.fraction < 1 && !tr.startsolid)
			push[k >> 1] += ((k & 1) ? r : -r) * (1 - tr.fraction);
	}
	VectorAdd (e->origin, push, e->origin);
	VectorAdd (e->oldorigin, push, e->oldorigin);
}

/* BDGN 3.56s: room left for ordinary entities this frame (shell casings) */
int V_ClassicRoom (void)
{
	return MAX_ENTITIES_CLASSIC - r_numclassic;
}

void V_ClearScene (void)
{
	fxb_checked = 0;
	r_numdlights = 0;
	r_numentities = 0;
	r_numclassic = 0;
	r_numparticles = 0;
}


/*
=====================
V_AddEntity

=====================
*/
void V_AddEntity (entity_t *ent)
{
	if (r_numentities >= MAX_ENTITIES)
		return;
	if (!(ent->flags & RF_BDGN_FX))
	{	/* other renderers (AMD's glam, old DLLs) were built for 128 entities; effect entities only go to ref_gl */
		if (r_numclassic >= MAX_ENTITIES_CLASSIC)
			return;
		r_numclassic++;
	}
	r_entities[r_numentities] = *ent;
	FXB_KeepOut (&r_entities[r_numentities]);	/* BDGN 3.56s */
	r_numentities++;
}


/*
=====================
V_AddParticle

=====================
*/
void V_AddParticle (vec3_t org, int color, float alpha)
{
	particle_t	*p;

	if (r_numparticles >= MAX_PARTICLES)
		return;
	p = &r_particles[r_numparticles++];
	VectorCopy (org, p->origin);
	p->color = color;
	p->alpha = alpha;
}

/*
=====================
V_AddLight

=====================
*/
/*
BDGN 3.57a: light flash instead of dynamic lights (cl_lightflash, Doom style). Muzzle flashes and explosions do not
light the walls; the whole view gets a short warm flash instead, stronger for big and near lights, through the view
blend (in software only the palette changes). No dynamic light reaches the renderer, so the software surface cache
is never rebuilt for one. Lights that move with things (rocket glows, the BFG ball) are simply left out.
*/
static cvar_t *cl_lightflash;
static float v_lightflash;
int CL_LightFlashOn (void)
{
	if (!cl_lightflash)
		cl_lightflash = Cvar_Get ("cl_lightflash", "0", CVAR_ARCHIVE);
	return cl_lightflash->value != 0;
}
void V_LightFlash (vec3_t org, float radius)
{
	vec3_t d;
	float dist, w;
	if (radius <= 0)
		return;
	VectorSubtract (org, cl.refdef.vieworg, d);
	dist = VectorLength (d);
	w = (radius / 300.0f) * (1.0f - dist / (radius * 2.5f));
	if (w > 0)
		v_lightflash += w * 0.12f;
}

void V_AddLight (vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t	*dl;

	if (CL_LOWEND_SPRITES() || r_numdlights >= MAX_DLIGHTS || CL_LightFlashOn ())
		return;
	dl = &r_dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}


/*
=====================
V_AddLightStyle

=====================
*/
void V_AddLightStyle (int style, float r, float g, float b)
{
	lightstyle_t	*ls;

	if (style < 0 || style > MAX_LIGHTSTYLES)
		Com_Error (ERR_DROP, "Bad light style %i", style);
	ls = &r_lightstyles[style];

	ls->white = r+g+b;
	ls->rgb[0] = r;
	ls->rgb[1] = g;
	ls->rgb[2] = b;
}

/*
================
V_TestParticles

If cl_testparticles is set, create 4096 particles in the view
================
*/
void V_TestParticles (void)
{
	particle_t	*p;
	int			i, j;
	float		d, r, u;

	r_numparticles = MAX_PARTICLES;
	for (i=0 ; i<r_numparticles ; i++)
	{
		d = i*0.25;
		r = 4*((i&7)-3.5);
		u = 4*(((i>>3)&7)-3.5);
		p = &r_particles[i];

		for (j=0 ; j<3 ; j++)
			p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*d +
			cl.v_right[j]*r + cl.v_up[j]*u;

		p->color = 8;
		p->alpha = cl_testparticles->value;
	}
}

/*
================
V_TestEntities

If cl_testentities is set, create 32 player models
================
*/
void V_TestEntities (void)
{
	int			i, j;
	float		f, r;
	entity_t	*ent;

	r_numentities = 32;
	memset (r_entities, 0, sizeof(r_entities));

	for (i=0 ; i<r_numentities ; i++)
	{
		ent = &r_entities[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;

		ent->model = cl.baseclientinfo.model;
		ent->skin = cl.baseclientinfo.skin;
	}
}

/*
================
V_TestLights

If cl_testlights is set, create 32 lights models
================
*/
void V_TestLights (void)
{
	int			i, j;
	float		f, r;
	dlight_t	*dl;

	r_numdlights = 32;
	memset (r_dlights, 0, sizeof(r_dlights));

	for (i=0 ; i<r_numdlights ; i++)
	{
		dl = &r_dlights[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;
		dl->color[0] = ((i%6)+1) & 1;
		dl->color[1] = (((i%6)+1) & 2)>>1;
		dl->color[2] = (((i%6)+1) & 4)>>2;
		dl->intensity = 200;
	}
}

//===================================================================

/*
BDGN 3.51q: the loading screen was redrawn for every model and client in the map (SCR_UpdateScreen: whole frame and
buffer swap, with VSync on a wait for the next refresh each time; 40 models = 40 frames). Now at most every 100 ms;
the message pump (Sys_SendKeyEvents) still runs for every item. bdgn_load_ms holds the last load time.
*/
static int cl_loadscreen_last;
static void CL_LoadScreenUpdate (void)
{
	int now = Sys_Milliseconds ();
	if (now - cl_loadscreen_last < 100 && now >= cl_loadscreen_last)
		return;
	cl_loadscreen_last = now;
	SCR_UpdateScreen ();
}

/*
=================
CL_PrepRefresh

Call before entering a new level, or after changing dlls
=================
*/
/* BDGN 3.59f: map load phases. The load starts at the collision map or the sounds (cl_main.c), whichever comes first;
   CL_PrepRefresh ends it. bdgn_mapload.txt, cvar bdgn_load_phases and benchmark note LOAD_PHASES. */
int cl_load_cm_ms = -1, cl_load_snd_ms = -1;
static int cl_load_active;
static int cl_load_phys0, cl_load_page0;	/* BDGN 3.59i: free RAM / page file at the start of the load (KB) */
extern void Sys_MemStatusKB (int *avail_phys_kb, int *avail_page_kb);
void CL_LoadPhaseBegin (void)
{
	if (cl_load_active)
		return;
	cl_load_active = 1;
	cl_load_cm_ms = cl_load_snd_ms = -1;
	Sys_MemStatusKB (&cl_load_phys0, &cl_load_page0);
	FS_LoadBegin ();
}

void CL_LoadPhaseAbort (void)
{
	if (cl_load_active)
		FS_LoadEnd ();
	cl_load_active = 0;
}

void CL_PrepRefresh (void)
{
 int load_start,load_mark,load_world,load_pics,load_sprites,load_img0;
 int load_models=0,load_images=0,load_clients=0,load_sky=0,load_endreg=0;	/* BDGN 3.59f */
 FILE *load_log;char load_path[MAX_OSPATH];
	char		mapname[MAX_QPATH];	/* BDGN 3.51ar: was 32, the map path can have 63 characters */
	int			i;
	char		name[MAX_QPATH];
	float		rotate;
	vec3_t		axis;

	if (!cl.configstrings[CS_MODELS+1][0])
		return;		// no map loaded

	CL_LoadPhaseBegin ();	/* BDGN 3.59f: vid_restart comes here without the two steps before */
	load_start=Sys_Milliseconds();
	load_img0=(int)Cvar_VariableValue("bdgn_img_new");	/* BDGN 3.59q */
	SCR_AddDirtyPoint (0, 0);
	SCR_AddDirtyPoint (viddef.width-1, viddef.height-1);

	// let the render dll load the map
	CL_MapName (mapname, sizeof(mapname), cl.configstrings[CS_MODELS+1]);	// BDGN 3.51ar: skip "maps/", cut off ".bsp", bounded

	// register models, pics, and skins
	Com_Printf ("Map: %s\r", mapname); 
	SCR_UpdateScreen ();
	re.BeginRegistration (mapname);
	load_world=Sys_Milliseconds()-load_start;
	Com_Printf ("                                     \r");

	// precache status bar pics
	Com_Printf ("pics\r"); 
	SCR_UpdateScreen ();
	load_mark=Sys_Milliseconds();
	SCR_TouchPics ();
	load_pics=Sys_Milliseconds()-load_mark;
	Com_Printf ("                                     \r");

	Com_Printf("sprites/models\r");SCR_UpdateScreen();Sys_SendKeyEvents();
	load_mark=Sys_Milliseconds();
	CL_RegisterTEntModels ();
	load_sprites=Sys_Milliseconds()-load_mark;

	num_cl_weaponmodels = 1;
	strcpy(cl_weaponmodels[0], "weapon.md2");
	load_mark=Sys_Milliseconds();

	for (i=1 ; i<MAX_MODELS && cl.configstrings[CS_MODELS+i][0] ; i++)
	{
		CL_CopyString (name, sizeof(name), cl.configstrings[CS_MODELS+i]);	/* BDGN 3.51ar: a config string may run on past 64 bytes */
		name[37] = 0;	// never go beyond one line
		if (name[0] != '*')
			Com_Printf ("%s\r", name); 
		CL_LoadScreenUpdate ();	/* BDGN 3.51q: at most every 100 ms */
		Sys_SendKeyEvents ();	// pump message loop
		if (name[0] == '#')
		{
			// special player weapon model
			if (num_cl_weaponmodels < MAX_CLIENTWEAPONMODELS)
			{
				strncpy(cl_weaponmodels[num_cl_weaponmodels], cl.configstrings[CS_MODELS+i]+1,
					sizeof(cl_weaponmodels[num_cl_weaponmodels]) - 1);
				num_cl_weaponmodels++;
			}
		} 
		else
		{
			cl.model_draw[i] = re.RegisterModel (cl.configstrings[CS_MODELS+i]);
			CL_RegisterFarModel (i);	/* BDGN 3.57m */
			if (name[0] == '*')
				cl.model_clip[i] = CM_InlineModel (cl.configstrings[CS_MODELS+i]);
			else
				cl.model_clip[i] = NULL;
		}
		if (name[0] != '*')
			Com_Printf ("                                     \r");
	}

	load_models=Sys_Milliseconds()-load_mark;
	Com_Printf ("images\r", i); 
	SCR_UpdateScreen ();
	load_mark=Sys_Milliseconds();
	for (i=1 ; i<MAX_IMAGES && cl.configstrings[CS_IMAGES+i][0] ; i++)
	{
		cl.image_precache[i] = re.RegisterPic (cl.configstrings[CS_IMAGES+i]);
		Sys_SendKeyEvents ();	// pump message loop
	}
	
	Com_Printf ("                                     \r");
	load_images=Sys_Milliseconds()-load_mark;
	load_mark=Sys_Milliseconds();
	for (i=0 ; i<MAX_CLIENTS ; i++)
	{
		if (!cl.configstrings[CS_PLAYERSKINS+i][0])
			continue;
		Com_Printf ("client %i\r", i); 
		CL_LoadScreenUpdate ();	/* BDGN 3.51q: at most every 100 ms */
		Sys_SendKeyEvents ();	// pump message loop
		CL_ParseClientinfo (i);
		Com_Printf ("                                     \r");
	}

	CL_LoadClientinfo (&cl.baseclientinfo, "unnamed\\male/grunt");
	load_clients=Sys_Milliseconds()-load_mark;
	load_mark=Sys_Milliseconds();

	// set sky textures and speed
	Com_Printf ("sky\r", i); 
	SCR_UpdateScreen ();
	rotate = atof (cl.configstrings[CS_SKYROTATE]);
	sscanf (cl.configstrings[CS_SKYAXIS], "%f %f %f", 
		&axis[0], &axis[1], &axis[2]);
	re.SetSky (cl.configstrings[CS_SKY], rotate, axis);
	Com_Printf ("                                     \r");

	// the renderer can now free unneeded stuff
	load_sky=Sys_Milliseconds()-load_mark;
	load_mark=Sys_Milliseconds();
	re.EndRegistration ();
	load_endreg=Sys_Milliseconds()-load_mark;
	FS_LoadEnd ();	/* BDGN 3.59f */
	cl_load_active = 0;
	{	/* BDGN 3.59i: memory before and after: free RAM falling to almost nothing and the page file filling up means the
		   load swapped (the Pentium 133 with 31 MB: 30-60 s loads in Faster and PSYCHO, v3.59e) */
		extern char cl_load_fx_detail[];
		int phys1, page1;
		Sys_MemStatusKB (&phys1, &page1);
		Cvar_ForceSet("bdgn_load_phases",va("effects detail: %s ms; collision %i, sounds %i, world %i, pics %i, effects %i, models %i, images %i, clients %i, sky %i, free %i ms; files %i, misses %i, cached misses %i, file time %i ms; free RAM %i -> %i KB, free page file %i -> %i KB; images read from disk %i",
			cl_load_fx_detail,cl_load_cm_ms,cl_load_snd_ms,load_world,load_pics,load_sprites,load_models,load_images,load_clients,load_sky,load_endreg,
			fs_stat_opens,fs_stat_misses,fs_stat_cachehits,(int)(fs_stat_us/1000),cl_load_phys0,phys1,cl_load_page0,page1,(int)Cvar_VariableValue("bdgn_img_new")-load_img0));
	}
	Com_sprintf(load_path,sizeof(load_path),"%s/bdgn_mapload.txt",FS_Gamedir());
	load_log=fopen(load_path,"w");
	Cvar_ForceSet("bdgn_load_ms",va("%i",Sys_Milliseconds()-load_start));	/* BDGN 3.51q: for bdgn_bench */
	if(load_log) {
	 fprintf(load_log,"map: %s\nworld: %d ms\npics: %d ms\nsprites/temporary models: %d ms\nremaining registration: %d ms\ntotal: %d ms\ncl_simpleitems: %g\ncl_simpleenemies: %g\ncl_simpleenemy_anim: %g\n",mapname,load_world,load_pics,load_sprites,Sys_Milliseconds()-load_start-load_world-load_pics-load_sprites,Sys_Milliseconds()-load_start,Cvar_VariableValue("cl_simpleitems"),Cvar_VariableValue("cl_simpleenemies"),Cvar_VariableValue("cl_simpleenemy_anim"));
	 { extern char cl_load_fx_detail[]; fprintf(load_log,"effects detail: %s ms\n",cl_load_fx_detail); }
	 fprintf(load_log,"--- BDGN 3.59f phases ---\ncollision map (client): %d ms\nsounds: %d ms\nmodels: %d ms\nimages: %d ms\nclients: %d ms\nsky: %d ms\nfree unused (EndRegistration): %d ms\nfiles opened: %d\nmisses: %d\nmisses from cache: %d\ntime in file lookups: %d ms\nfs_misscache: %g\n",
		cl_load_cm_ms,cl_load_snd_ms,load_models,load_images,load_clients,load_sky,load_endreg,fs_stat_opens,fs_stat_misses,fs_stat_cachehits,(int)(fs_stat_us/1000),Cvar_VariableValue("fs_misscache"));
	 fclose(load_log);
	}

	// clear any lines of console text
	Con_ClearNotify ();

	SCR_UpdateScreen ();
	cl.refresh_prepped = true;
	cl.force_refdef = true;	// make sure we have a valid refdef

	// start the cd track
	CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true);

	{	// BDGN 3.46: look for stray windows now and a few times during the next frames
		extern int cl_sweep_frames;
		Sys_SweepStrayWindows ();
		cl_sweep_frames = 150;
	}
}

/*
====================
CalcFov
====================
*/
float CalcFov (float fov_x, float width, float height)
{
	float	a;
	float	x;

	if (fov_x < 1 || fov_x > 179)
		Com_Error (ERR_DROP, "Bad fov: %f", fov_x);

	x = width/tan(fov_x/360*M_PI);

	a = atan (height/x);

	a = a*360/M_PI;

	return a;
}

//============================================================================

// gun frame debugging functions
void V_Gun_Next_f (void)
{
	gun_frame++;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Prev_f (void)
{
	gun_frame--;
	if (gun_frame < 0)
		gun_frame = 0;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Model_f (void)
{
	char	name[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		gun_model = NULL;
		return;
	}
	Com_sprintf (name, sizeof(name), "models/%s/tris.md2", Cmd_Argv(1));
	gun_model = re.RegisterModel (name);
}

//============================================================================


/*
=================
SCR_DrawCrosshair
=================
*/
/* BDGN 3.57h: more crosshairs. 1-3 are id's pictures; 4-15 are drawn here with filled boxes, so they work with every
   renderer and need no new files. scale = pixels per crosshair pixel (the screen height / 240, at least 1). */
const char *crosshair_menu_names[CROSSHAIR_COUNT+1] = {
	"none", "cross", "dot", "angle", "small dot", "thin cross", "open cross", "circle", "circle and dot", "X",
	"T", "brackets", "chevron", "green cross", "red dot", "square", NULL
};
static void CH_Box (int x, int y, int w, int h, int c)
{
	if (w > 0 && h > 0)
		re.DrawFill (x, y, w, h, c);
}
void SCR_DrawCrosshairShape (int type, int cx, int cy, int s)
{
	int i, c = 15;	/* palette 15: the light grey of id's crosshairs; 208 green, 242 red */
	static const signed char ring[][2] = { {0,-4},{1,-4},{2,-3},{3,-2},{4,-1},{4,0},{4,1},{3,2},{2,3},{1,4},{0,4},
		{-1,4},{-2,3},{-3,2},{-4,1},{-4,0},{-4,-1},{-3,-2},{-2,-3},{-1,-4} };
	if (s < 1) s = 1;
	switch (type)
	{
	case 1: case 2: case 3:
	{	/* id's pictures */
		char name[8];
		int w, h;
		Com_sprintf (name, sizeof(name), "ch%i", type);
		re.DrawGetPicSize (&w, &h, name);
		if (w > 0) re.DrawPic (cx - w / 2, cy - h / 2, name);
		break;
	}
	case 4: CH_Box (cx - s, cy - s, 2 * s, 2 * s, c); break;
	case 13: c = 208;	/* green cross, then as the thin cross */
	case 5:
		CH_Box (cx - 5 * s, cy, 11 * s, s, c);
		CH_Box (cx, cy - 5 * s, s, 11 * s, c);
		break;
	case 6:
		CH_Box (cx - 7 * s, cy, 4 * s, s, c); CH_Box (cx + 4 * s, cy, 4 * s, s, c);
		CH_Box (cx, cy - 7 * s, s, 4 * s, c); CH_Box (cx, cy + 4 * s, s, 4 * s, c);
		break;
	case 8: CH_Box (cx, cy, s, s, c);	/* circle and dot */
	case 7:
		for (i = 0; i < (int)(sizeof(ring) / sizeof(ring[0])); i++)
			CH_Box (cx + ring[i][0] * s, cy + ring[i][1] * s, s, s, c);
		break;
	case 9:
		for (i = -4; i <= 4; i++) { CH_Box (cx + i * s, cy + i * s, s, s, c); CH_Box (cx + i * s, cy - i * s, s, s, c); }
		break;
	case 10:
		CH_Box (cx - 5 * s, cy, 11 * s, s, c);
		CH_Box (cx, cy, s, 6 * s, c);
		break;
	case 11:
		CH_Box (cx - 6 * s, cy - 4 * s, s, 9 * s, c); CH_Box (cx - 6 * s, cy - 4 * s, 3 * s, s, c); CH_Box (cx - 6 * s, cy + 4 * s, 3 * s, s, c);
		CH_Box (cx + 6 * s, cy - 4 * s, s, 9 * s, c); CH_Box (cx + 4 * s, cy - 4 * s, 3 * s, s, c); CH_Box (cx + 4 * s, cy + 4 * s, 3 * s, s, c);
		CH_Box (cx, cy, s, s, c);
		break;
	case 12:
		for (i = 0; i <= 4; i++) { CH_Box (cx - i * s, cy + i * s, s, s, c); CH_Box (cx + i * s, cy + i * s, s, s, c); }
		break;
	case 14: CH_Box (cx - s, cy - s, 3 * s, 3 * s, 242); break;
	case 15:
		CH_Box (cx - 4 * s, cy - 4 * s, 9 * s, s, c); CH_Box (cx - 4 * s, cy + 4 * s, 9 * s, s, c);
		CH_Box (cx - 4 * s, cy - 4 * s, s, 9 * s, c); CH_Box (cx + 4 * s, cy - 4 * s, s, 9 * s, c);
		break;
	}
}

/*
BDGN 3.57h: the zoom (+zoom, mouse 2 / mouse 3 / space in the presets) works only with the railgun, as a sniper scope:
the view narrows to zfov with zsensitivity, the weapon is hidden (the game hides it below 75 degrees) and the screen
around a round scope is black, with a thin cross. Letting go, or another weapon, gives back the field of view and the
mouse speed that were set before. A built-in command, so it is used even when a config defines an alias +zoom.
*/
extern cvar_t *zfov, *zsensitivity, *fov;
static qboolean scope_held, scope_applied;
static float scope_saved_fov, scope_saved_sens;
static void V_ZoomDown_f (void) { scope_held = true; }
static void V_ZoomUp_f (void) { scope_held = false; }
static qboolean V_RailgunInHand (void)
{
	int gun = cl.frame.playerstate.gunindex;
	return gun > 0 && gun < MAX_MODELS && !Q_stricmp (cl.configstrings[CS_MODELS + gun], "models/weapons/v_rail/tris.md2");
}
qboolean CL_ScopeActive (void)
{
	return scope_applied;
}
static void V_ScopeFrame (void)
{
	qboolean want = scope_held && cls.state == ca_active && cls.key_dest == key_game && V_RailgunInHand ();
	if (want && !scope_applied)
	{
		scope_saved_fov = fov->value;
		scope_saved_sens = sensitivity->value;
		Cvar_SetValue ("fov", zfov && zfov->value >= 5 ? zfov->value : 37.5f);
		if (zsensitivity && zsensitivity->value > 0)
			Cvar_SetValue ("sensitivity", zsensitivity->value);
		scope_applied = true;
	}
	else if (!want && scope_applied)
	{
		Cvar_SetValue ("fov", scope_saved_fov >= 5 ? scope_saved_fov : 90);
		Cvar_SetValue ("sensitivity", scope_saved_sens > 0 ? scope_saved_sens : 3);
		scope_applied = false;
	}
}
static void V_DrawScope (void)
{
	int cx = scr_vrect.x + scr_vrect.width / 2, cy = scr_vrect.y + scr_vrect.height / 2;
	int r = (scr_vrect.width < scr_vrect.height ? scr_vrect.width : scr_vrect.height) * 46 / 100;
	int y, x0 = scr_vrect.x, x1 = scr_vrect.x + scr_vrect.width, line = scr_vrect.height >= 480 ? 2 : 1;
	if (!scope_applied || r < 8)
		return;
	/* black outside the circle: above, below, and left / right of it on every row */
	if (cy - r > scr_vrect.y) re.DrawFill (x0, scr_vrect.y, scr_vrect.width, cy - r - scr_vrect.y, 0);
	if (cy + r < scr_vrect.y + scr_vrect.height) re.DrawFill (x0, cy + r, scr_vrect.width, scr_vrect.y + scr_vrect.height - cy - r, 0);
	for (y = cy - r; y < cy + r; y++)
	{
		int dy = y - cy, half = (int)sqrt ((float)(r * r - dy * dy));
		if (cx - half > x0) re.DrawFill (x0, y, cx - half - x0, 1, 0);
		if (cx + half < x1) re.DrawFill (cx + half, y, x1 - cx - half, 1, 0);
	}
	/* the cross: thin lines with a gap in the middle, thicker towards the edge */
	re.DrawFill (cx - r, cy, r - r / 12, line, 0);
	re.DrawFill (cx + r / 12, cy, r - r / 12, line, 0);
	re.DrawFill (cx, cy - r, line, r - r / 12, 0);
	re.DrawFill (cx, cy + r / 12, line, r - r / 12, 0);
	re.DrawFill (cx - r, cy - line, r / 3, line * 3, 0);
	re.DrawFill (cx + r - r / 3, cy - line, r / 3, line * 3, 0);
	re.DrawFill (cx - line, cy + r - r / 3, line * 3, r / 3, 0);
	re.DrawFill (cx, cy, line, line, 242);	/* red dot */
}

void SCR_DrawCrosshair (void)
{
	if (!crosshair->value)
		return;
	if (CL_ScopeActive ())
		return;	/* BDGN 3.57h: the scope has its own cross */
	if (crosshair->value > 3)
	{	/* BDGN 3.57h: a drawn crosshair */
		int s = viddef.height / 240;
		SCR_DrawCrosshairShape ((int)crosshair->value, scr_vrect.x + scr_vrect.width / 2, scr_vrect.y + scr_vrect.height / 2, s);
		return;
	}

	if (crosshair->modified)
	{
		crosshair->modified = false;
		SCR_TouchPics ();
	}

	if (!crosshair_pic[0])
		return;

	re.DrawPic (scr_vrect.x + ((scr_vrect.width - crosshair_width)>>1)
	, scr_vrect.y + ((scr_vrect.height - crosshair_height)>>1), crosshair_pic);
}

/*
==================
V_RenderView

==================
*/
void V_RenderView( float stereo_separation )
{
	extern int entitycmpfnc( const entity_t *, const entity_t * );
	byte fb_saved[MAX_ENTITIES];
	int fb_count = 0, fb_i;

	Q_SetFastSqrt ((int)cl_fastsqrt->value, (int)cl_fastsqrt2->value);
	V_ScopeFrame ();	/* BDGN 3.57h: railgun scope on / off */

	if (cls.state != ca_active)
		return;

	if (!cl.refresh_prepped)
		return;			// still loading

	if (cl_timedemo->value)
	{
		if (!cl.timedemo_start)
			{
			cl.timedemo_start = Sys_Milliseconds ();
			if ( Cvar_VariableValue( "r_profile" ) )
				CL_ProfileReset ();	/* BDGN 3.52t: profile exactly the timed part of the demo */
			else
				CL_FrameTimeMark (1);	/* BDGN 3.52v: frame times of the demo */
			}
		cl.timedemo_frames++;
	}
	CL_FrameTimeMark (0);	/* BDGN 3.52v: returns at once outside a timedemo / profiling */

	// an invalid frame will just use the exact previous refdef
	// we can't use the old frame if the video mode has changed, though...
	if ( cl.frame.valid && (cl.force_refdef || !cl_paused->value) )
	{
		cl.force_refdef = false;

		V_ClearScene ();

		// build a refresh entity list and calc cl.sim*
		// this also calls CL_CalcViewValues which loads
		// v_forward, etc.
		CL_AddEntities ();

		if (cl_testparticles->value)
			V_TestParticles ();
		if (cl_testentities->value)
			V_TestEntities ();
		if (cl_testlights->value)
			V_TestLights ();
		if (cl_testblend->value)
		{
			cl.refdef.blend[0] = 1;
			cl.refdef.blend[1] = 0.5;
			cl.refdef.blend[2] = 0.25;
			cl.refdef.blend[3] = 0.5;
		}

		// offset vieworg appropriately if we're doing stereo separation
		if ( stereo_separation != 0 )
		{
			vec3_t tmp;

			VectorScale( cl.v_right, stereo_separation, tmp );
			VectorAdd( cl.refdef.vieworg, tmp, cl.refdef.vieworg );
		}

		// never let it sit exactly on a node line, because a water plane can
		// dissapear when viewed with the eye exactly on it.
		// the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
		cl.refdef.vieworg[0] += 1.0/16;
		cl.refdef.vieworg[1] += 1.0/16;
		cl.refdef.vieworg[2] += 1.0/16;

		cl.refdef.x = scr_vrect.x;
		cl.refdef.y = scr_vrect.y;
		cl.refdef.width = scr_vrect.width;
		cl.refdef.height = scr_vrect.height;
		VID_RectToRealPixels (&cl.refdef.x, &cl.refdef.y, &cl.refdef.width, &cl.refdef.height);	// BDGN 3.50: 2D scale
		// BDGN 3.37: Hor+ - "fov" is the horizontal angle of a 4:3 screen; a wider view window gets a wider angle
		// and keeps the same vertical angle, so a widescreen picture is not squeezed
		if (cl_fovaspect->value && cl.refdef.width * 3 > cl.refdef.height * 4 && cl.refdef.height > 0)
		{
			float wide = atan (tan (cl.refdef.fov_x / 360 * M_PI) * ((float)cl.refdef.width * 3.0f) / ((float)cl.refdef.height * 4.0f)) * 360 / M_PI;

			if (wide > 179)
				wide = 179;
			cl.refdef.fov_x = wide;
		}
		cl.refdef.fov_y = CalcFov (cl.refdef.fov_x, cl.refdef.width, cl.refdef.height);
		cl.refdef.time = cl.time*0.001;

		cl.refdef.areabits = cl.frame.areabits;
		if (vid_simpleworld_drawn)
			cl.refdef.areabits = NULL;	/* BDGN 3.57o: the renderer's simpler map has one area (vid_dll.c) */

		if (!cl_add_entities->value)
			r_numentities = r_numclassic = 0;
		if (!cl_add_particles->value)
			r_numparticles = 0;
		if (!cl_add_lights->value)
			r_numdlights = 0;
		if (v_lightflash > 0)
		{	/* BDGN 3.57a: the light flash on top of the view blend (warm white) */
			float a = v_lightflash > 0.3f ? 0.3f : v_lightflash, old_a = cl.refdef.blend[3], na = old_a + a * (1 - old_a);
			static const float col[3] = { 1.0f, 0.85f, 0.55f };
			int k;
			for (k = 0; k < 3; k++)
				cl.refdef.blend[k] = (cl.refdef.blend[k] * old_a + col[k] * a) / (na > 0 ? na : 1);
			cl.refdef.blend[3] = na;
		}
		v_lightflash = 0;
		if (!cl_add_blend->value)
		{
			VectorClear (cl.refdef.blend);
		}

		cl.refdef.num_entities = r_numentities;
		cl.refdef.entities = r_entities;
		cl.refdef.num_particles = r_numparticles;
		cl.refdef.particles = r_particles;
		cl.refdef.num_dlights = r_numdlights;
		cl.refdef.dlights = r_dlights;
		cl.refdef.lightstyles = r_lightstyles;

		cl.refdef.rdflags = cl.frame.playerstate.rdflags;

		// sort entities for better cache locality
        qsort( cl.refdef.entities, cl.refdef.num_entities, sizeof( cl.refdef.entities[0] ), (int (*)(const void *, const void *))entitycmpfnc );
	}

	/* Pass fullbright to every model category and to older renderers too.
	** Restore only this flag afterwards so paused/stereo scene caches never stick. */
	if (cl_fullbright_active)
	{
		fb_count = cl.refdef.num_entities;
		if (fb_count > MAX_ENTITIES) fb_count = MAX_ENTITIES;
		for (fb_i=0; fb_i<fb_count; fb_i++)
		{
			fb_saved[fb_i] = (cl.refdef.entities[fb_i].flags & RF_FULLBRIGHT) != 0;
			cl.refdef.entities[fb_i].flags |= RF_FULLBRIGHT;
		}
	}
	if (CL_LOWEND_SPRITES()) {
		refdef_t view=cl.refdef;
		view.num_dlights=0;
		re.RenderFrame(&view);
	} else
		re.RenderFrame (&cl.refdef);
	for (fb_i=0; fb_i<fb_count; fb_i++)
		if (!fb_saved[fb_i]) cl.refdef.entities[fb_i].flags &= ~RF_FULLBRIGHT;
	if (cl_stats->value)
		Com_Printf ("ent:%i  lt:%i  part:%i\n", r_numentities, r_numdlights, r_numparticles);
	if ( log_stats->value && ( log_stats_file != 0 ) )
		fprintf( log_stats_file, "%i,%i,%i,",r_numentities, r_numdlights, r_numparticles);


	SCR_AddDirtyPoint (scr_vrect.x, scr_vrect.y);
	SCR_AddDirtyPoint (scr_vrect.x+scr_vrect.width-1,
		scr_vrect.y+scr_vrect.height-1);

	V_DrawScope ();	/* BDGN 3.57h */
	SCR_DrawCrosshair ();
}


/*
=============
V_Viewpos_f
=============
*/
void V_Viewpos_f (void)
{
	Com_Printf ("(%i %i %i) : %i\n", (int)cl.refdef.vieworg[0],
		(int)cl.refdef.vieworg[1], (int)cl.refdef.vieworg[2], 
		(int)cl.refdef.viewangles[YAW]);
}

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("gun_next", V_Gun_Next_f);
	Cmd_AddCommand ("gun_prev", V_Gun_Prev_f);
	Cmd_AddCommand ("gun_model", V_Gun_Model_f);

	Cmd_AddCommand ("viewpos", V_Viewpos_f);
	Cmd_AddCommand ("+zoom", V_ZoomDown_f);	/* BDGN 3.57h: railgun scope */
	Cmd_AddCommand ("-zoom", V_ZoomUp_f);

	crosshair = Cvar_Get ("crosshair", "0", CVAR_ARCHIVE);

	cl_testblend = Cvar_Get ("cl_testblend", "0", 0);
	cl_fovaspect = Cvar_Get ("cl_fovaspect", "1", CVAR_ARCHIVE);
	cl_fastsqrt = Cvar_Get ("fastsqrt", "0", CVAR_ARCHIVE);
	cl_fastsqrt2 = Cvar_Get ("fastsqrt2", "0", CVAR_ARCHIVE);
	Q_SetFastSqrt ((int)cl_fastsqrt->value, (int)cl_fastsqrt2->value);
	cl_testparticles = Cvar_Get ("cl_testparticles", "0", 0);
	cl_testentities = Cvar_Get ("cl_testentities", "0", 0);
	cl_testlights = Cvar_Get ("cl_testlights", "0", 0);

	cl_stats = Cvar_Get ("cl_stats", "0", 0);
}
