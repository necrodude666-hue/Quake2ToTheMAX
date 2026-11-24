/*
cl_fx3d.c -- BDGN 3.55t: modern 3D weapon effects (pak8-hdfx3d.pak, models/hdfx3d/)

Six toggles, all on the effects page under HITS AND EXPLOSIONS, shown only when the BDGN ref_gl runs on a modern
driver (bdgn_gl_modernfx_capable: opengl32, OpenGL 2, SSE2, not a legacy machine) and the pack is installed:

  cl_hdsmoke      HD smoke: smoke clouds after grenade, rocket and BFG explosions. Two sets: grey smoke and a
                  faint green mist for the original / HD effects, sooty fire smoke and a bright green mist for the
                  3D explosions. The BFG mist spreads out in all directions as fast as the explosion animation,
                  stops 0.1 s after it, hangs for a second and fades out.
  cl_3dexplo      3D rocket explosions: fireball, flame tendrils, shock ring, fire puffs with fire in the smoke,
                  sparks, a flickering light. 1.5 s like the original. Turned onto the wall it hits.
  cl_3dbfg        3D BFG explosion: plasma shell, lightning arcs, shock ring, green core puffs, green light.
  cl_3dblaster    comet blaster / hyperblaster bolts: crackling energy ball and a 3D tail; replace the bolt and
                  its trail.
  cl_hdmuzzle     muzzle flashes for all weapons (3D, lit), smoke puffs for the firearms.
  cl_heatdistort  heat distortion around the energy balls, in front of the machine gun / shotgun muzzles and over
                  the explosions (also needs bdgn_gl_distort_capable: GLSL).

The renderer flags (client/ref.h RF_FXMESH, RF_PUFFS, RF_DISTORT, RF_FXADD) are understood by the BDGN ref_gl
only; nothing here is sent to another renderer. Visual only: nothing changes on the server.
*/
#include "client.h"
#include "cl_fx3d_mz2.h"

cvar_t *cl_hdsmoke, *cl_3dexplo, *cl_3dbfg, *cl_3dblaster, *cl_hdmuzzle, *cl_heatdistort;
cvar_t *cl_grenadesmoke, *cl_explosmoke;	/* BDGN 3.57h: modern grenade smoke trail; smoke puff after the fire smoke */
static cvar_t *cl_hdmuzzle_fwd, *cl_hdmuzzle_right, *cl_hdmuzzle_up, *hand_cvar;

extern cparticle_t *active_particles, *free_particles;

enum {
	M_FIRE1, M_FIRE2, M_FIRE3, M_FIREBALL, M_TENDRILS, M_RSHOCK, M_SMOKE_A1, M_SMOKE_A2, M_SMOKE_B1, M_SMOKE_B2,
	M_MIST_A, M_MIST_B, M_BFG_CORE, M_BFG_SHELL, M_BFG_ARCS, M_BFG_SHOCK, M_BFG_SMALL, M_BFG_SMALL_ARCS,
	M_BLASTER_HEAD, M_HYPER_HEAD, M_BLASTER_TAIL, M_HYPER_TAIL, M_MZ_MG, M_MZ_SHOTGUN, M_MZ_ENERGY, M_MZ_ROCKET,
	M_MZ_SMOKE, M_MZ_SMOKE_BIG,
	M_GREN1, M_GREN2, M_GREN_SHOCK,	/* BDGN 3.55ab: the grenade's own explosion; optional, from pak8-hdgore.pak (models/hdgore/) */
	M_BFG_SMOKE_BIG, M_BFG_SMOKE_HIT, M_BLASTER_SMOKE, M_HYPER_SMOKE, M_LAVA_SMOKE, M_SHOTGUN_SMOKE,
	M_LAVA_BURST1, M_LAVA_BURST2, M_LAVA_RING,	/* BDGN 3.56z: living lava (pak8-hdgore.pak) */
	M_COUNT
};
#define M_REQUIRED M_GREN1
static const char *fx3d_names[M_COUNT] = {
	"rocket_fire1", "rocket_fire2", "rocket_fire3", "rocket_fireball", "rocket_tendrils", "rocket_shock",
	"smoke_a1", "smoke_a2", "smoke_b1", "smoke_b2", "bfg_mist_a", "bfg_mist_b", "bfg_core", "bfg_shell", "bfg_arcs",
	"bfg_shock", "bfg_small", "bfg_small_arcs", "blaster_head", "hyper_head", "blaster_tail", "hyper_tail",
	"mz_mg", "mz_shotgun", "mz_energy", "mz_rocket", "mz_smoke", "mz_smoke_big",
	"grenade_fire1", "grenade_fire2", "grenade_shock", "bfg_smoke_big", "bfg_smoke_hit", "blaster_smoke", "hyper_smoke", "lava_smoke", "shotgun_smoke",
	"lava_burst1", "lava_burst2", "lava_ring"
};
static struct model_s *fx3d_model[M_COUNT];
/* BDGN 3.55v: two texture sets for the same models. standard = the textures inside pak8-hdfx3d.pak (made in code,
   the middle way); AI = Stable Diffusion versions in pak8-hdfx3d-sd.pak (models/hdfx3d_sd/, same names), chosen
   with cl_fx3d_tex 1 and used by the High Fidelity presets. The AI skins are given to the renderer as the entity's
   custom skin; a missing one falls back to the standard texture. */
cvar_t *cl_fx3d_tex;
static struct image_s *fx3d_sdskin[M_COUNT][4];
static int fx3d_sd_loaded;
static const char *FX_SkinName (int m, int skin)
{
	static const char *mz[4] = { "mz_yellow", "mz_orange", "mz_blue", "mz_green" };
	switch (m)
	{
	case M_FIREBALL: case M_TENDRILS: case M_RSHOCK: case M_GREN_SHOCK: return skin ? NULL : "fx_fire";
	case M_BFG_SMOKE_BIG: case M_BFG_SMOKE_HIT: return NULL; /* straight-alpha skin */
	case M_BFG_SHELL: case M_BFG_ARCS: case M_BFG_SHOCK: case M_BFG_SMALL: case M_BFG_SMALL_ARCS: return skin ? NULL : "fx_green";
	case M_BLASTER_HEAD: return skin ? NULL : "fx_energy";
	case M_HYPER_HEAD: return skin ? NULL : "fx_energy_w";
	case M_MZ_MG: case M_MZ_SHOTGUN: case M_MZ_ENERGY: case M_MZ_ROCKET: return skin >= 0 && skin < 4 ? mz[skin] : NULL;
	case M_LAVA_BURST1: case M_LAVA_BURST2: case M_LAVA_RING: return skin ? NULL : "lava_fx";	/* 3.56z */
	}
	return skin ? NULL : "puffs";	/* every billboard cloud */
}
static struct model_s *fx3d_laser;	/* the blaster / hyperblaster bolt model of the game */
static struct model_s *fx3d_bfg3;	/* the game's big BFG explosion sprite */
static int fx3d_loaded;

/* muzzle flash skins (models/hdfx3d/mz_*.md2) */
#define MZS_YELLOW 0
#define MZS_ORANGE 1
#define MZS_BLUE 2
#define MZS_GREEN 3

typedef enum { FX_FREE, FX_ANIM, FX_EXPAND, FX_DISTORT, FX_LIGHT, FX_VIEW } fx3d_kind_t;

typedef struct
{
	fx3d_kind_t		kind;
	struct model_s	*model;
	int				flags, skin;
	int				model_index;
	int				start, life;		/* ms; start can lie in the future (delayed part) */
	int				frames;
	float			fps;
	vec3_t			origin, angles;
	float			alpha;
	int				fadein, fadeout;	/* ms at the start / end of the life */
	int				expand, hold;		/* FX_EXPAND: frames 0..frames-1 over expand ms, then the last frame for hold ms */
	float			r0, r1, strength;	/* FX_DISTORT radius over the life, strength; FX_LIGHT intensity r0 -> r1 */
	int				mode;				/* FX_DISTORT 0 haze / 1 ring */
	vec3_t			color, color1;		/* FX_LIGHT */
	float			flicker;
	vec3_t			view_ofs;			/* FX_VIEW: forward / right / up from the eye */
	qboolean		depthhack;
	int				variant;
	int				grow;				/* BDGN 3.56x: RF_PUFFS cloud grows from nothing over grow ms (ease out) */
	float			scale0, scale1;		/* BDGN 3.57h: RF_PUFFS cloud scaled about its middle, scale0 -> scale1 over grow ms */
} fx3d_t;

#define MAX_FX3D 256	/* BDGN 3.56y: 256 (more lava smoke) */
static fx3d_t fx3d[MAX_FX3D];
static int lava_smoke_next;
static int lava_burst_next;	/* BDGN 3.56z */
static int hyper_cell_shots;	/* BDGN 3.56z: own hyperblaster shots since the last energy cell */
static int shotgun_smoke_weapon, shotgun_smoke_time;
static int fx3d_distorts;	/* distortions sent this frame */
#define MAX_FRAME_DISTORTS 12
static int re_frames_hint (int m);
static void Shells_Init (void);	/* BDGN 3.56s: shell casings, at the end of this file */
static void Shells_Register (void);
static void Shells_Clear (void);
static void Shells_AddEntities (void);
static void Shells_Shot (int entnum, int weapon_kind, int shots, vec3_t monster_origin);

/* ----------------------------------------------------------------------------------------- availability */

static qboolean CL_FX3D_Modern (void)
{	/* 3.55ab: asked for every effect, bolt and flash: the cvar lookups by name are done once per frame */
	static int frame = -1, ok;
	if (frame != cls.framecount)
	{
		frame = cls.framecount;
		ok = Cvar_VariableValue ("bdgn_gl_modernfx_capable") && !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
	}
	return fx3d_loaded && ok && !cl_classic2d->value && !CL_LOWEND_SPRITES ();
}
static qboolean CL_FX3D_On (cvar_t *cv)
{
	return cv && cv->value && CL_FX3D_Modern ();
}
static qboolean CL_FX3D_DistortOn (void)
{
	return CL_FX3D_On (cl_heatdistort) && Cvar_VariableValue ("bdgn_gl_distort_capable");
}

qboolean CL_FX3D_RocketOn (void) { return CL_FX3D_On (cl_3dexplo); }
qboolean CL_FX3D_BFGOn (void) { return CL_FX3D_On (cl_3dbfg); }

/* menu, benchmark and presets: -1 = not one of these cvars, else 1 when it can be chosen on this machine */
int CL_FX3D_CvarAvailable (const char *name)
{
	/* before the first map the models are not registered yet: then the pack's presence decides */
	qboolean installed = fx3d_loaded || FS_LoadFile ("models/hdfx3d/rocket_fire1.md2", NULL) > 0;
	qboolean modern = installed && Cvar_VariableValue ("bdgn_gl_modernfx_capable") && !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
	if (!strcmp (name, "cl_fx3d_tex"))	/* 3.56r: also with only the standard 4K pack */
		return modern && (fx3d_sd_loaded || FS_LoadFile ("models/hdfx3d_sd/puffs.tga", NULL) > 0 || CL_FX4K_Allowed (name, 2));
	if (!strcmp (name, "cl_heatdistort"))
		return modern && Cvar_VariableValue ("bdgn_gl_distort_capable") != 0;
	if (!strcmp (name, "cl_hdsmoke") || !strcmp (name, "cl_3dexplo") || !strcmp (name, "cl_3dbfg") ||
		!strcmp (name, "cl_3dblaster") || !strcmp (name, "cl_hdmuzzle") ||
		!strcmp (name, "cl_grenadesmoke") || !strcmp (name, "cl_explosmoke"))	/* 3.57h */
		return modern;
	return CL_Gore_CvarAvailable (name);	/* BDGN 3.55y: cl_decals, cl_3drail, cl_hdgibs (else -1) */
}

void CL_FX3D_Init (void)
{
	cl_hdsmoke = Cvar_Get ("cl_hdsmoke", "0", CVAR_ARCHIVE);
	cl_3dexplo = Cvar_Get ("cl_3dexplo", "0", CVAR_ARCHIVE);
	cl_3dbfg = Cvar_Get ("cl_3dbfg", "0", CVAR_ARCHIVE);
	cl_3dblaster = Cvar_Get ("cl_3dblaster", "0", CVAR_ARCHIVE);
	cl_hdmuzzle = Cvar_Get ("cl_hdmuzzle", "0", CVAR_ARCHIVE);
	cl_heatdistort = Cvar_Get ("cl_heatdistort", "0", CVAR_ARCHIVE);
	cl_grenadesmoke = Cvar_Get ("cl_grenadesmoke", "0", CVAR_ARCHIVE);	/* BDGN 3.57h */
	cl_explosmoke = Cvar_Get ("cl_explosmoke", "0", CVAR_ARCHIVE);
	/* the own weapon's muzzle point is a guess per weapon; these move all of them (units, view space) */
	cl_hdmuzzle_fwd = Cvar_Get ("cl_hdmuzzle_fwd", "0", CVAR_ARCHIVE);
	cl_hdmuzzle_right = Cvar_Get ("cl_hdmuzzle_right", "0", CVAR_ARCHIVE);
	cl_hdmuzzle_up = Cvar_Get ("cl_hdmuzzle_up", "0", CVAR_ARCHIVE);
	hand_cvar = Cvar_Get ("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	cl_fx3d_tex = Cvar_Get ("cl_fx3d_tex", "0", CVAR_ARCHIVE);	/* BDGN 3.55v: 0 standard, 1 AI (Stable Diffusion); 3.56r: 2 standard 4K, 3 AI 4K */
	Shells_Init ();	/* BDGN 3.56s */
}

/* BDGN 3.59o: the HD 3D effect models are loaded only where they can be shown (GL renderer, classic 2D off).
   2.7 s of a map load went here on a dual Pentium II 300 with 3dfx MiniGL although classic 2D left them unused
   (v3.59n, MEASURED). In that case only the availability is checked (file exists) and cl_tent.c loads them the
   moment classic 2D is turned off. */
void CL_FX3D_RegisterModels (void)
{
	char name[MAX_QPATH];
	int i;
	fx3d_loaded = 0;
	fx3d_sd_loaded = 0;
	memset (fx3d, 0, sizeof(fx3d));
	fx3d_laser = re.RegisterModel ("models/objects/laser/tris.md2");
	fx3d_bfg3 = re.RegisterModel ("sprites/s_bfg3.sp2");
	memset (fx3d_model, 0, sizeof(fx3d_model));	/* 3.55ab: no stale pointers of the last map after a break */
	memset (fx3d_sdskin, 0, sizeof(fx3d_sdskin));
	Shells_Register ();	/* BDGN 3.56s: every renderer */
	if (CL_HDAssetsUnused ())
	{
		for (i = 0; i < M_REQUIRED; i++)
		{
			Com_sprintf (name, sizeof(name), "models/hdfx3d/%s.md2", fx3d_names[i]);
			if (FS_LoadFile (name, NULL) <= 0)
				break;
		}
		Cvar_FullSet ("bdgn_hdfx3d_available", i == M_REQUIRED ? "1" : "0", 0);
		Cvar_FullSet ("bdgn_hdfx3d_sd_available", i == M_REQUIRED && FS_LoadFile ("models/hdfx3d_sd/puffs.tga", NULL) > 0 ? "1" : "0", 0);
		Cvar_FullSet ("bdgn_lavabursts_available", i == M_REQUIRED
			&& FS_LoadFile ("models/hdgore/lava_burst1.md2", NULL) > 0 && FS_LoadFile ("models/hdgore/lava_ring.md2", NULL) > 0 ? "1" : "0", 0);
		return;
	}
	CL_FX3D_RegisterHD ();
}

void CL_FX3D_RegisterHD (void)
{
	char name[MAX_QPATH];
	int i;
	fx3d_loaded = 0;
	memset (fx3d_model, 0, sizeof(fx3d_model));
	for (i = 0; i < M_REQUIRED; i++)
	{
		Com_sprintf (name, sizeof(name), "models/hdfx3d/%s.md2", fx3d_names[i]);
		fx3d_model[i] = FS_LoadFile (name, NULL) > 0 ? re.RegisterModel (name) : NULL;	/* not installed: no messages */
		if (!fx3d_model[i])
			break;
	}
	fx3d_loaded = i == M_REQUIRED;	/* all or nothing: a half pack would mix old and new effects */
	for (i = M_REQUIRED; i < M_COUNT && fx3d_loaded; i++)
	{	/* optional: without them the grenade uses the rocket's 3D explosion */
		Com_sprintf (name, sizeof(name), "models/hdgore/%s.md2", fx3d_names[i]);
		fx3d_model[i] = FS_LoadFile (name, NULL) > 0 ? re.RegisterModel (name) : NULL;
	}
	memset (fx3d_sdskin, 0, sizeof(fx3d_sdskin));
	fx3d_sd_loaded = fx3d_loaded && FS_LoadFile ("models/hdfx3d_sd/puffs.tga", NULL) > 0;
	if (fx3d_sd_loaded)
	{
		int k;
		for (i = 0; i < M_COUNT; i++)
			for (k = 0; k < 4; k++)
			{
				const char *n = FX_SkinName (i, k);
				if (!n)
					continue;
				Com_sprintf (name, sizeof(name), "models/hdfx3d_sd/%s.tga", n);
				if (FS_LoadFile (name, NULL) > 0)
					fx3d_sdskin[i][k] = re.RegisterSkin (name);
			}
	}
	Cvar_FullSet ("bdgn_hdfx3d_sd_available", fx3d_sd_loaded ? "1" : "0", 0);
	Cvar_FullSet ("bdgn_hdfx3d_available", fx3d_loaded ? "1" : "0", 0);
	Cvar_FullSet ("bdgn_lavabursts_available", fx3d_model[M_LAVA_BURST1] && fx3d_model[M_LAVA_RING] ? "1" : "0", 0);	/* 3.56z */
}

static float chaingun_heat;
static int chaingun_heat_time, chaingun_last_shot;

static void CL_CoolChaingun(void)
{
	int from=chaingun_heat_time;
	if (cl.time<from) { chaingun_heat=0; chaingun_last_shot=cl.time; }
	if (from<chaingun_last_shot+450) from=chaingun_last_shot+450;
	if (cl.time>from)
	{
		chaingun_heat-=(cl.time-from)/12000.0f;
		if (chaingun_heat<0) chaingun_heat=0;
	}
	chaingun_heat_time=cl.time;
}

void CL_ChaingunHeatShot(int shots)
{
	int encoded=cl.frame.playerstate.stats[STAT_BDGN_BULLET_CAPACITY];
	int capacity=(encoded&0xc000)==BDGN_BULLET_CAPACITY_TAG ? encoded&0x3fff : 200;
	if (capacity<=0) capacity=200;
	CL_CoolChaingun();
	chaingun_last_shot=cl.time;
	chaingun_heat+=(float)shots/capacity;
	if (chaingun_heat>1) chaingun_heat=1;
}

int CL_ChaingunHeatLevel(void)
{
	if (cl.frame.playerstate.stats[STAT_HEALTH]<=0) chaingun_heat=0;
	CL_CoolChaingun();
	return (int)(chaingun_heat*1000+0.5f);
}

/* BDGN 3.56z: the machine gun's muzzle glows red when three quarters of the full ammunition (the bullet capacity, as
   for the chaingun) has been fired without a pause; it cools as the chaingun does (12 s from full heat, after 0.45 s) */
static float mg_heat;
static int mg_heat_time, mg_last_shot;
static void CL_CoolMachinegun(void)
{
	int from=mg_heat_time;
	if (cl.time<from) { mg_heat=0; mg_last_shot=cl.time; }
	if (from<mg_last_shot+450) from=mg_last_shot+450;
	if (cl.time>from)
	{
		mg_heat-=(cl.time-from)/12000.0f;
		if (mg_heat<0) mg_heat=0;
	}
	mg_heat_time=cl.time;
}

void CL_MachinegunHeatShot(void)
{
	int encoded=cl.frame.playerstate.stats[STAT_BDGN_BULLET_CAPACITY];
	int capacity=(encoded&0xc000)==BDGN_BULLET_CAPACITY_TAG ? encoded&0x3fff : 200;
	if (capacity<=0) capacity=200;
	CL_CoolMachinegun();
	mg_last_shot=cl.time;
	mg_heat+=1.0f/capacity;
	if (mg_heat>1) mg_heat=1;
}

int CL_MachinegunHeatLevel(void)
{
	if (cl.frame.playerstate.stats[STAT_HEALTH]<=0) mg_heat=0;
	CL_CoolMachinegun();
	return (int)(mg_heat*1000+0.5f);
}

void CL_FX3D_Clear (void)
{
	chaingun_heat=0;
	mg_heat=0;
	mg_heat_time=mg_last_shot=0;
	chaingun_heat_time=chaingun_last_shot=0;
	lava_smoke_next = 0;
	lava_burst_next = 0;
	shotgun_smoke_weapon=shotgun_smoke_time=0;
	memset (fx3d, 0, sizeof(fx3d));
	Shells_Clear ();	/* BDGN 3.56s */
}

/* ----------------------------------------------------------------------------------------- pool */

static fx3d_t *FX_Alloc (void)
{
	int i, oldest = 0;
	for (i = 0; i < MAX_FX3D; i++)
	{
		if (fx3d[i].kind == FX_FREE)
			break;
		if (fx3d[i].start + fx3d[i].life < fx3d[oldest].start + fx3d[oldest].life)
			oldest = i;
	}
	if (i == MAX_FX3D)
		i = oldest;
	memset (&fx3d[i], 0, sizeof(fx3d[i]));
	fx3d[i].alpha = 1.0f;
	fx3d[i].start = cl.time;
	return &fx3d[i];
}

/* a timed model: frames at fps from start (delay ms from now) */
static fx3d_t *FX_Anim (int m, vec3_t org, vec3_t angles, int flags, int delay, float fps, float alpha)
{
	fx3d_t *f;
	if (!fx3d_model[m])
		return NULL;
	f = FX_Alloc ();
	f->kind = FX_ANIM;
	f->model = fx3d_model[m];
	f->model_index=m;
	f->flags = flags | RF_TRANSLUCENT;
	f->start = cl.time + delay;
	f->fps = fps;
	f->frames = re_frames_hint (m);
	f->life = (int)(f->frames * 1000 / fps);
	f->alpha = alpha;
	VectorCopy (org, f->origin);
	if (angles) VectorCopy (angles, f->angles);
	return f;
}

static void FX_Light (vec3_t org, int life, float i0, float i1, float r, float g, float b, float r1, float g1, float b1, float flicker)
{
	fx3d_t *f = FX_Alloc ();
	f->kind = FX_LIGHT;
	f->life = life;
	f->r0 = i0; f->r1 = i1;
	VectorSet (f->color, r, g, b);
	VectorSet (f->color1, r1, g1, b1);
	f->flicker = flicker;
	VectorCopy (org, f->origin);
}

static void FX_Distort (vec3_t org, int delay, int life, float r0, float r1, float strength, int mode)
{
	fx3d_t *f;
	if (!CL_FX3D_DistortOn ())
		return;
	f = FX_Alloc ();
	f->kind = FX_DISTORT;
	f->start = cl.time + delay;
	f->life = life;
	f->r0 = r0; f->r1 = r1;
	f->strength = strength;
	f->mode = mode;
	VectorCopy (org, f->origin);
}

/* ----------------------------------------------------------------------------------------- helpers */

/* the AI skin for a model of this pack when cl_fx3d_tex 1, else NULL (the model's own) */
static struct image_s *FX_Skin (int model, int skinnum)
{
	if (!fx3d_sd_loaded || (cl_fx3d_tex->value != 1 && cl_fx3d_tex->value != 3) || skinnum < 0 || skinnum > 3)	/* 3.56r: 1 AI, 3 AI 4K */
		return NULL;
	return fx3d_sdskin[model][skinnum];
}

/* the nearest wall within 24 units (6 directions): its normal, else straight up (explosion in the air) */
static qboolean FX_WallNormal (vec3_t pos, vec3_t normal)
{
	static float dirs[6][3] = { {0,0,-1}, {0,0,1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
	trace_t tr;
	vec3_t end;
	float best = 2.0f;
	int i;
	VectorSet (normal, 0, 0, 1);
	for (i = 0; i < 6; i++)
	{
		VectorMA (pos, 24, dirs[i], end);
		tr = CM_BoxTrace (pos, end, vec3_origin, vec3_origin, 0, MASK_SOLID);
		if (tr.fraction < 1.0f && !tr.allsolid && tr.fraction < best)
		{
			best = tr.fraction;
			VectorCopy (tr.plane.normal, normal);
		}
	}
	return best < 2.0f;
}

/* angles that turn the model's +x onto dir, with a random roll */
static void FX_AnglesFromDir (vec3_t dir, vec3_t angles)
{	/* as the game's vectoangles */
	float yaw, pitch;
	if (dir[1] == 0 && dir[0] == 0)
	{
		yaw = 0;
		pitch = dir[2] > 0 ? 90.0f : 270.0f;
	}
	else
	{
		yaw = (float)(atan2 (dir[1], dir[0]) * 180 / M_PI);
		if (yaw < 0) yaw += 360;
		pitch = (float)(atan2 (dir[2], sqrt (dir[0] * dir[0] + dir[1] * dir[1])) * 180 / M_PI);
		if (pitch < 0) pitch += 360;
	}
	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = (float)(rand () % 360);
}

static void FX_TowardsViewer (vec3_t pos, float dist, vec3_t out)
{
	vec3_t to;
	float d;
	VectorSubtract (cl.refdef.vieworg, pos, to);
	d = VectorNormalize (to);
	VectorMA (pos, dist < d * 0.5f ? dist : d * 0.5f, to, out);
}

/* frames per model, as made by _extra/tools/hdfx3d_gen.py */
static int re_frames_hint (int m)
{
	switch (m)
	{
	case M_FIRE1: case M_FIRE2: case M_FIRE3: case M_TENDRILS: case M_SMOKE_A1: case M_SMOKE_A2: return 30;
	case M_FIREBALL: case M_BFG_SMALL: case M_BFG_SMALL_ARCS: return 12;
	case M_RSHOCK: return 8;
	case M_SMOKE_B1: case M_SMOKE_B2: return 40;
	case M_MIST_A: case M_MIST_B: return 10;
	case M_BFG_CORE: case M_BFG_SHELL: case M_BFG_ARCS: case M_BLASTER_TAIL: case M_HYPER_TAIL: return 16;
	case M_BFG_SHOCK: case M_GREN_SHOCK: return 10;
	case M_GREN1: case M_GREN2: return 32;
	case M_BLASTER_HEAD: case M_HYPER_HEAD: return 8;
	case M_MZ_MG: case M_MZ_SHOTGUN: case M_MZ_ENERGY: case M_MZ_ROCKET: return 4;
	case M_MZ_SMOKE: case M_MZ_SMOKE_BIG: return 20;
	case M_BLASTER_SMOKE: case M_HYPER_SMOKE: case M_LAVA_SMOKE: case M_SHOTGUN_SMOKE: return 24;
	case M_LAVA_BURST1: case M_LAVA_BURST2: case M_LAVA_RING: return 32;	/* 3.56z: 1.6 s at 20 */
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- smoke */

/* grenade / rocket smoke: set A (grey) after the original and HD explosions, set B (fire smoke) after the 3D one */
void CL_FX3D_ExplosionSmoke (vec3_t pos, qboolean fire_set)
{
	vec3_t org, ang, n;
	fx3d_t *f;
	if (!CL_FX3D_On (cl_hdsmoke))
		return;
	FX_WallNormal (pos, n);
	VectorSet (ang, 0, (float)(rand () % 360), 0);	/* world-aligned: the smoke rises */
	if (fire_set)
	{	/* BDGN 3.56x: after the 3D rocket explosion the smoke comes out of the explosion: it starts at the point of
		   impact at size 0 and grows to its full size while it rises as before (the wall check pushes it out of the
		   wall as it grows). Before, it appeared at full size 14 units in front of the wall. */
		int first = (rand () & 1) ? M_SMOKE_B1 : M_SMOKE_B2;
		VectorMA (pos, 2, n, org);
		/* 3.57h: RF_PUFFS, as the clouds are made (one puff per triangle); without it the grow of 3.56x did nothing */
		f = FX_Anim (first, org, ang, RF_PUFFS, 0, 10, 1.0f);
		if (f)
		{
			int life = f->life;
			f->grow = 1200;	/* as long as the fireball at half speed */
			/* BDGN 3.57h: a smoke puff where the fire smoke ends (cl_explosmoke). It starts when 75 % of the fire
			   smoke's time has gone, at half the size of the fire smoke cloud at that moment, grows to twice that (the
			   size of the cloud) and fades out in the time the other clouds fade (1 s). The other cloud model is used,
			   so it does not lie exactly on top of the fire smoke. */
			if (CL_FX3D_On (cl_explosmoke))
			{
				int other = first == M_SMOKE_B1 ? M_SMOKE_B2 : M_SMOKE_B1;
				fx3d_t *p = FX_Anim (other, org, ang, RF_PUFFS, life * 3 / 4, 0.0001f, 1.0f);
				if (p)
				{
					p->fps = 0;
					p->variant = (p->frames * 3) / 4;	/* the cloud as it is at 75 % */
					p->life = 2000;
					p->grow = 1000;
					p->scale0 = 0.5f;
					p->scale1 = 1.0f;
					p->fadeout = 1000;
				}
			}
		}
	}
	else
	{
		VectorMA (pos, 14, n, org);
		FX_Anim ((rand () & 1) ? M_SMOKE_A1 : M_SMOKE_A2, org, ang, RF_PUFFS, 0, 10, 1.0f);	/* 3.57h: RF_PUFFS */
	}
}

/* blaster hit: a small puff blown out of the wall */
void CL_FX3D_HitSmoke(vec3_t pos, vec3_t dir, int delay, qboolean hyper_hit)
{
	vec3_t org, n, side;
	int i, count, model;
	if (!CL_FX3D_On(cl_hdsmoke) || cl_nosmoke->value)
		return;
	VectorCopy(dir,n);
	if (!VectorNormalize(n)) VectorSet(n,0,0,1);
	PerpendicularVector(side,n);
	count=hyper_hit?4:3;
	model=hyper_hit?M_HYPER_SMOKE:M_BLASTER_SMOKE;
	for(i=0;i<count;i++)
	{
		fx3d_t *f;
		VectorMA(pos,5+i*0.8f,n,org);
		VectorMA(org,((i&1)?-1.0f:1.0f)*i*1.2f,side,org);
		f=FX_Anim(model,org,NULL,RF_PUFFS,delay+i*85,24,0.68f-i*0.07f);
		if (f) f->fadeout=450;
	}
}

/* 3.60i hotfix: a dense impact cloud, using the blaster/hyperblaster smoke
   assets already selected by the effect texture menu. No extra asset loads.
   Starts just after the rail core, grows for one second, fades for two.
   Owner's 3.60i update: twelve puffs (3x), five times the linear size. */
void CL_FX3D_RailImpactSmoke(vec3_t pos, vec3_t dir)
{
 vec3_t n,side,org;
 int i,model;
 if (!CL_FX3D_On(cl_hdsmoke) || cl_nosmoke->value) return;
 VectorCopy(dir,n);
 if (!VectorNormalize(n)) VectorSet(n,0,0,1);
 PerpendicularVector(side,n);
 model=fx3d_model[M_HYPER_SMOKE]?M_HYPER_SMOKE:M_BLASTER_SMOKE;
 for (i=0;i<12;i++) {
  fx3d_t *f;
  VectorMA(pos,8.0f+(i%4)*1.5f,n,org);
  VectorMA(org,(i-5.5f)*3.0f,side,org);
  f=FX_Anim(model,org,NULL,RF_PUFFS,701+i*20,
    (re_frames_hint(model)-1)/3.0f,0.88f-(i%4)*0.06f);
  if (f) {
   f->life=3000; f->grow=1000; f->fadein=120; f->fadeout=2000;
   f->scale0=1.75f; f->scale1=9.0f;
  }
 }
}

qboolean CL_FX3D_RocketTrail(vec3_t start, vec3_t end, float *step)
{
 vec3_t travel, origin;
 float length, distance;
 if (!CL_FX3D_On(cl_hdsmoke)) { *step=0; return false; }
 if (cl_nosmoke->value) { *step=0; return true; }
 VectorSubtract(end,start,travel);
 length=VectorNormalize(travel);
 if (length>256) { *step=0; return true; }
 for (distance=*step;distance<length;distance+=8.0f)
 {
  fx3d_t *f;
  VectorMA(start,distance,travel,origin);
  f=FX_Anim(M_MZ_SMOKE,origin,NULL,RF_PUFFS,0,25,0.68f);
  if (f) f->fadeout=380;
 }
 *step=distance-length;
 return true;
}

/* BDGN 3.57h: modern smoke behind a flying grenade (cl_grenadesmoke): grey puffs every 10 units, smaller and lighter
   than the rocket's, that rise a little and fade in 0.6 s. Replaces the original particle trail. */
qboolean CL_FX3D_GrenadeTrail(vec3_t start, vec3_t end, float *step)
{
 vec3_t travel, origin;
 float length, distance;
 if (!CL_FX3D_On(cl_grenadesmoke) || !fx3d_model[M_MZ_SMOKE]) return false;
 if (cl_nosmoke->value) { *step=0; return true; }
 VectorSubtract(end,start,travel);
 length=VectorNormalize(travel);
 if (length>256) { *step=0; return true; }
 for (distance=*step;distance<length;distance+=10.0f)
 {
  fx3d_t *f;
  VectorMA(start,distance,travel,origin);
  f=FX_Anim(M_MZ_SMOKE,origin,NULL,RF_PUFFS,0,30,0.50f);
  if (f) f->fadeout=600;
 }
 *step=distance-length;
 return true;
}

/* BDGN 3.56y: cl_lavasmoke = puffs per quarter second (was 1; the menu's "lava smoke" row, 1x .. 10x, 3 by default).
   They come one at a time, evenly spread, at random lava spots up to 384 units around the view. */
static cvar_t *cl_lavasmoke;
static void FX_LavaSmoke(void)
{
 vec3_t from, to;
 trace_t trace;
 int i, amount, interval, spawned=0;
 if (!cl_lavasmoke) cl_lavasmoke=Cvar_Get("cl_lavasmoke","3",CVAR_ARCHIVE);
 if (!CL_FX3D_On(cl_hdsmoke) || cl_nosmoke->value || !fx3d_model[M_LAVA_SMOKE] ||
     (cl.refdef.rdflags&RDF_NOWORLDMODEL))
  return;
 amount=(int)cl_lavasmoke->value;
 if (amount<1) return;
 if (amount>10) amount=10;
 interval=250/amount;
 if (cl.time<lava_smoke_next && lava_smoke_next-cl.time<=250)
  return;
 lava_smoke_next=cl.time+interval;
 for (i=0;i<6 && !spawned;i++)
 {
  fx3d_t *f;
  VectorCopy(cl.refdef.vieworg,from);
  from[0]+=crand()*384;
  from[1]+=crand()*384;
  from[2]+=48;
  VectorCopy(from,to);
  to[2]-=400;
  trace=CM_BoxTrace(from,to,vec3_origin,vec3_origin,0,MASK_SOLID|CONTENTS_LAVA);
  if (trace.startsolid || trace.fraction>=1 || !(trace.contents&CONTENTS_LAVA))
   continue;
  trace.endpos[2]+=3;
  f=FX_Anim(M_LAVA_SMOKE,trace.endpos,NULL,RF_PUFFS,0,10,0.45f);
  if (f) { f->fadein=240; f->fadeout=1000; }
  spawned=1;
 }
}

/* BDGN 3.56z: living lava. With 3D smoke on and cl_lavabursts > 0 (Weapon Effects -> "lava bursts": off, 1x .. 4x),
   lava now and then spouts up out of a pool near the view: a column of molten lava that throws drops and falls back,
   and a glowing ring that runs outwards on the lava where it fell (models/hdgore/lava_burst1/2, lava_ring). At 1x
   about one burst every 2.5 seconds within 512 units. */
static cvar_t *cl_lavabursts;
static void FX_LavaBursts(void)
{
 vec3_t from, to, ang, up = {0, 0, 1};
 trace_t trace;
 int i, amount;
 fx3d_t *f;
 if (!cl_lavabursts) cl_lavabursts=Cvar_Get("cl_lavabursts","0",CVAR_ARCHIVE);
 amount=(int)cl_lavabursts->value;
 if (amount<1 || !CL_FX3D_On(cl_hdsmoke) || !fx3d_model[M_LAVA_BURST1] || !fx3d_model[M_LAVA_RING] ||
     (cl.refdef.rdflags&RDF_NOWORLDMODEL))
  return;
 if (amount>4) amount=4;
 if (cl.time<lava_burst_next && lava_burst_next-cl.time<=5000)
  return;
 lava_burst_next=cl.time+(2500+(rand()%1500))/amount;
 for (i=0;i<6;i++)
 {
  VectorCopy(cl.refdef.vieworg,from);
  from[0]+=crand()*512;
  from[1]+=crand()*512;
  from[2]+=64;
  VectorCopy(from,to);
  to[2]-=600;
  trace=CM_BoxTrace(from,to,vec3_origin,vec3_origin,0,MASK_SOLID|CONTENTS_LAVA);
  if (trace.startsolid || trace.fraction>=1 || !(trace.contents&CONTENTS_LAVA))
   continue;
  {	/* room above it for the spout */
   trace_t up_tr;
   vec3_t top;
   VectorCopy(trace.endpos,top);
   top[2]+=64;
   up_tr=CM_BoxTrace(trace.endpos,top,vec3_origin,vec3_origin,0,MASK_SOLID);
   if (up_tr.fraction<0.6f)
    continue;
  }
  VectorSet(ang,0,(float)(rand()%360),0);
  f=FX_Anim((rand()&1)?M_LAVA_BURST1:M_LAVA_BURST2,trace.endpos,ang,RF_FXMESH,0,20,1.0f);
  if (f) f->fadeout=300;
  FX_AnglesFromDir(up,ang);	/* the ring lies on the lava */
  trace.endpos[2]+=0.5f;
  FX_Anim(M_LAVA_RING,trace.endpos,ang,RF_FXMESH|RF_FXADD,250,20,0.9f);
  FX_Light(trace.endpos,700,140,0,1.0f,0.45f,0.1f,0.8f,0.2f,0.0f,0.15f);
  break;
 }
}

/* Expand to 125% of the corresponding scorch mark and fade on the game clock. */
void CL_FX3D_BFGMist (vec3_t pos, int anim_ms, qboolean small_hit)
{
	fx3d_t *f;
	int m = small_hit ? M_BFG_SMOKE_HIT : M_BFG_SMOKE_BIG;
	if (!CL_FX3D_On (cl_hdsmoke) || !fx3d_model[m])
		return;
	f = FX_Alloc ();
	f->kind = FX_EXPAND;
	f->model = fx3d_model[m];
	f->model_index=m;
	f->flags = RF_TRANSLUCENT | RF_PUFFS;
	f->skin = FX_LAYER_BFG_SMOKE;
	f->frames = 24;
	f->expand = 1500;
	f->hold = 3000;
	if (anim_ms>f->expand) f->expand=anim_ms;
	f->life = f->expand + f->hold;
	/* Pull the billboard off the impact surface so the wall does not cut through it. */
	FX_TowardsViewer (pos, small_hit ? 2 : 4, f->origin);
	VectorClear (f->angles);
}

/* ----------------------------------------------------------------------------------------- explosions */

/* 3D rocket explosion (1.5 s like the original). Returns false when the effect is off (caller draws its own). */
qboolean CL_FX3D_RocketExplosion (vec3_t pos)
{
	vec3_t n, org, ang, puff, glow;
	if (!CL_FX3D_On (cl_3dexplo))
		return false;
	FX_WallNormal (pos, n);
	VectorMA (pos, 3, n, org);
	FX_AnglesFromDir (n, ang);
	/* BDGN 3.56x: half speed (10 instead of 20 frames a second; the frames are blended, so it stays smooth): the
	   fireball lasts 1.2 s, the shock ring 0.8 s, the flames 3 s; light and heat haze follow */
	FX_Anim (M_RSHOCK, org, ang, RF_FXMESH | RF_FXADD, 0, 10, 0.9f);
	FX_Anim (M_FIREBALL, org, ang, RF_FXMESH | RF_FXADD, 0, 10, 1.0f);
	FX_Anim (M_TENDRILS, org, ang, RF_FXMESH | RF_FXADD, 0, 10, 1.0f);
	FX_TowardsViewer (org, 10, puff);	/* billboards: not cut by the wall */
	FX_Anim (M_FIRE1 + rand () % 3, puff, ang, RF_PUFFS, 0, 10, 1.0f);
	VectorMA (pos, 20, n, glow);
	FX_Light (glow, 2200, 520, 0, 1.0f, 0.62f, 0.28f, 0.9f, 0.25f, 0.05f, 0.25f);
	FX_Distort (glow, 0, 960, 38, 76, 0.40f, 0);
	CL_FX3D_ExplosionSmoke (pos, true);
	return true;
}

/* BDGN 3.55ab: the grenade's own 3D explosion (also grenade-like explosions in water): a low, wide blast on the
   ground - ring of flame and dust along the floor, a short column of fire that becomes a mushroom of dark smoke,
   clods and shrapnel sparks, a fast flat shock ring; 1.6 s. Without the models (old pak8-hdgore.pak) the rocket's. */
qboolean CL_FX3D_GrenadeExplosion (vec3_t pos)
{
	vec3_t n, org, ang, puff, glow;
	if (!CL_FX3D_On (cl_3dexplo))
		return false;
	if (!fx3d_model[M_GREN1] || !fx3d_model[M_GREN2] || !fx3d_model[M_GREN_SHOCK])
		return CL_FX3D_RocketExplosion (pos);
	FX_WallNormal (pos, n);
	VectorMA (pos, 3, n, org);
	FX_AnglesFromDir (n, ang);
	FX_Anim (M_GREN_SHOCK, org, ang, RF_FXMESH | RF_FXADD, 0, 20, 0.9f);
	FX_Anim (M_FIREBALL, org, ang, RF_FXMESH | RF_FXADD, 0, 26, 0.8f);	/* the rocket's fireball mesh, quicker */
	FX_TowardsViewer (org, 10, puff);
	FX_Anim ((rand () & 1) ? M_GREN1 : M_GREN2, puff, ang, RF_PUFFS, 0, 20, 1.0f);
	VectorMA (pos, 24, n, glow);
	FX_Light (glow, 900, 600, 0, 1.0f, 0.7f, 0.35f, 0.8f, 0.2f, 0.05f, 0.3f);
	FX_Distort (glow, 0, 550, 60, 140, 0.8f, 0);
	FX_Distort (org, 0, 320, 230, 230, 1.0f, 1);
	return true;
}

/* 3D BFG explosion: the big one (bfg ball hits) or the small one on every target the BFG hurts */
qboolean CL_FX3D_BFGExplosion (vec3_t pos, qboolean big)
{
	vec3_t n, ang, glow;
	if (!CL_FX3D_On (cl_3dbfg))
		return false;
	if (!big)
	{
		VectorSet (ang, 0, (float)(rand () % 360), 0);
		FX_Anim (M_BFG_SMALL, pos, ang, RF_FXMESH | RF_FXADD, 0, 20, 1.0f);
		FX_Anim (M_BFG_SMALL_ARCS, pos, ang, RF_FXMESH | RF_FXADD, 0, 20, 1.0f);
		FX_Light (pos, 600, 300, 0, 0.2f, 1.0f, 0.3f, 0.0f, 0.6f, 0.1f, 0.15f);
		FX_Distort (pos, 0, 400, 30, 60, 0.6f, 0);
		CL_FX3D_BFGMist(pos,600,true);
		return true;
	}
	FX_WallNormal (pos, n);
	VectorMA (pos, 4, n, glow);
	FX_AnglesFromDir (n, ang);
	FX_Anim (M_BFG_SHOCK, glow, ang, RF_FXMESH | RF_FXADD, 0, 20, 0.9f);
	FX_Anim (M_BFG_SHELL, glow, ang, RF_FXMESH | RF_FXADD, 0, 20, 1.0f);
	FX_Anim (M_BFG_ARCS, glow, ang, RF_FXMESH | RF_FXADD, 0, 20, 1.0f);
	FX_Anim (M_BFG_CORE, glow, ang, RF_PUFFS | RF_FULLBRIGHT, 0, 20, 1.0f);
	VectorMA (pos, 24, n, glow);
	FX_Light (glow, 1600, 650, 0, 0.25f, 1.0f, 0.35f, 0.0f, 0.55f, 0.1f, 0.2f);
	FX_Distort (glow, 0, 900, 80, 200, 0.9f, 0);
	FX_Distort (glow, 0, 500, 320, 320, 1.0f, 1);
	CL_FX3D_BFGMist (pos, re_frames_hint(M_BFG_ARCS)*1000/20, false);
	return true;
}

/* the 3D BFG explosion replaces the game's explosion sprite entity */
qboolean CL_FX3D_HideEntity (entity_t *ent)
{
	return fx3d_bfg3 && ent->model == fx3d_bfg3 && !cl_static_bfg->value && CL_FX3D_On (cl_3dbfg);
}

/* ----------------------------------------------------------------------------------------- comet bolts */

static int bolt_birth[MAX_EDICTS], bolt_seen[MAX_EDICTS];

/* replaces a blaster / hyperblaster bolt (the laser model) and its trail; true when handled (do not draw the
   original entity, its trail or light) */
qboolean CL_FX3D_Bolt (entity_t *ent, int effects, int number, vec3_t oldorg)
{
	entity_t e;
	qboolean hyper;
	int age, k;
	vec3_t dir;
	if (!CL_FX3D_On (cl_3dblaster) || !fx3d_laser || ent->model != fx3d_laser || number <= 0 || number >= MAX_EDICTS)
		return false;
	if (effects & (EF_TRACKER | EF_BLUEHYPERBLASTER))
		return false;	/* green blaster (Rogue) and blue hyperblaster keep their look */
	hyper = !(effects & EF_BLASTER);	/* hyperblaster bolts carry EF_HYPERBLASTER or nothing */
	if (cl.time - bolt_seen[number] > 250 || bolt_birth[number] > cl.time)
		bolt_birth[number] = cl.time;
	bolt_seen[number] = cl.time;
	age = cl.time - bolt_birth[number];

	memset (&e, 0, sizeof(e));
	VectorCopy (ent->origin, e.origin);
	VectorCopy (ent->origin, e.oldorigin);
	VectorCopy (ent->angles, e.angles);
	e.angles[2] = (float)((cl.time / 3 + number * 37) % 360);	/* the ball spins */
	e.model = fx3d_model[hyper ? M_HYPER_HEAD : M_BLASTER_HEAD];
	e.flags = RF_FXMESH | RF_FXADD | RF_TRANSLUCENT;
	e.alpha = 1.0f;
	e.skin = FX_Skin(hyper ? M_HYPER_HEAD : M_BLASTER_HEAD,0);
	e.frame = ((cl.time / 40) + number) & 7;	/* new arcs 25 times a second: crackling */
	e.oldframe = e.frame;

	V_AddEntity (&e);

	e.model = fx3d_model[hyper ? M_HYPER_TAIL : M_BLASTER_TAIL];
	e.flags = RF_PUFFS | RF_FULLBRIGHT | RF_TRANSLUCENT;
	e.angles[2] = 0;
	if (age < 100)
	{	/* the tail grows over the first 0.1 s */
		float f = age / 100.0f * 7.0f;
		e.oldframe = (int)f;
		e.frame = e.oldframe + 1 > 7 ? 7 : e.oldframe + 1;
		e.backlerp = 1.0f - (f - (int)f);
	}
	else
	{
		float f = (cl.time % 320) / 40.0f;	/* 8 loop frames, 40 ms each */
		e.oldframe = 8 + (int)f;
		e.frame = e.oldframe == 15 ? 8 : e.oldframe + 1;
		e.backlerp = 1.0f - (f - (int)f);
	}
	e.skin = FX_Skin(hyper ? M_HYPER_TAIL : M_BLASTER_TAIL,0);
	V_AddEntity (&e);

	if (CL_FX3D_DistortOn () && fx3d_distorts < MAX_FRAME_DISTORTS)
	{	/* the air around the ball shimmers */
		memset (&e, 0, sizeof(e));
		VectorCopy (ent->origin, e.origin);
		VectorCopy (ent->origin, e.oldorigin);
		e.flags = RF_DISTORT | RF_TRANSLUCENT;
		e.skinnum = hyper ? 14 : 18;
		e.alpha = 0.55f;
		V_AddEntity (&e);
		fx3d_distorts++;
	}

	/* glowing embers left behind on the path (the "3D trail" behind the tail) */
	VectorSubtract (ent->origin, oldorg, dir);
	for (k = 0; k < (hyper ? 2 : 3) && free_particles && VectorLength (dir) < 300; k++)
	{
		cparticle_t *p = free_particles;
		float f = frand ();
		int j;
		free_particles = p->next;
		p->next = active_particles;
		active_particles = p;
		p->time = cl.time;
		p->color = 0xe0 + (rand () & 7);
		for (j = 0; j < 3; j++)
		{
			p->org[j] = oldorg[j] + dir[j] * f + crand () * 2;
			p->vel[j] = crand () * 18;
			p->accel[j] = 0;
		}
		p->accel[2] = -PARTICLE_GRAVITY * 0.5f;
		p->alpha = 0.9f;
		p->alphavel = -1.0f / (0.25f + frand () * 0.25f);
	}

	if (effects & (EF_BLASTER | EF_HYPERBLASTER))
		CL_AddEffectLight (ent->origin, hyper ? 170 : 210, 1.0f, 0.75f, 0.3f, effects);
	return true;
}

/* ----------------------------------------------------------------------------------------- muzzle flashes */

/* the own weapon's muzzle in view space (forward, right, up), right-handed; a guess per weapon */
static void FX_ViewMuzzle (int weapon, vec3_t ofs)
{
	switch (weapon)
	{
	/* BDGN 3.55al: measured in id's view models (the front end of the barrel in the first firing frame, + 1.5 forward)
	   instead of guessed: the guesses were 5 to 14 units too far forward, so the flash hung in the air beyond the gun */
	case MZ_BLASTER:		VectorSet (ofs, 22.5f, 6.9f, -4.8f); break;
	case MZ_HYPERBLASTER:	VectorSet (ofs, 22, 4.6f, -5.4f); break;
	case MZ_MACHINEGUN:		VectorSet (ofs, 27, 9.2f, -7.4f); break;
	case MZ_SHOTGUN:		VectorSet (ofs, 25, 8.0f, -9.8f); break;
	case MZ_SSHOTGUN:		VectorSet (ofs, 21, 7.4f, -5.1f); break;
	case MZ_CHAINGUN1: case MZ_CHAINGUN2: case MZ_CHAINGUN3: VectorSet (ofs, 16, 5.0f, -7.0f); break;
	case MZ_RAILGUN:		VectorSet (ofs, 20.5f, 6.6f, -4.9f); break;
	case MZ_ROCKET:			VectorSet (ofs, 19, 3.9f, -7.0f); break;
	case MZ_GRENADE:		VectorSet (ofs, 18.5f, 5.6f, -7.0f); break;
	case MZ_BFG:			VectorSet (ofs, 26.5f, 9.0f, -8.5f); break;
	default:				VectorSet (ofs, 22, 6.5f, -6.5f); break;
	}
	ofs[0] += cl_hdmuzzle_fwd->value;
	ofs[1] += cl_hdmuzzle_right->value;
	ofs[2] += cl_hdmuzzle_up->value;
	if (hand_cvar->value == 1) ofs[1] = -ofs[1];
	else if (hand_cvar->value == 2) ofs[1] = 0;
}

/* type: 1 bullets, 2 shotgun, 3 blaster, 4 railgun, 5 rocket, 6 grenade, 7 BFG, 8 green blaster */
static void FX_MuzzleParts (int type, int *model, int *skin, int *smoke, float *haze)
{
	*model = -1; *skin = MZS_YELLOW; *smoke = -1; *haze = 0;
	switch (type)
	{
	case 1: *model = M_MZ_MG; *smoke = M_MZ_SMOKE; *haze = 11; break;
	case 2: *model = M_MZ_SHOTGUN; *smoke = M_MZ_SMOKE_BIG; *haze = 15; break;
	case 3: *model = M_MZ_ENERGY; *skin = MZS_ORANGE; break;
	case 4: *model = M_MZ_ENERGY; *skin = MZS_BLUE; break;
	case 5: *model = M_MZ_ROCKET; *skin = MZS_ORANGE; *smoke = M_MZ_SMOKE_BIG; *haze = 14; break;
	case 6: *model = M_MZ_MG; *skin = MZS_ORANGE; *smoke = M_MZ_SMOKE; break;
	case 7: *model = M_MZ_ENERGY; *skin = MZS_GREEN; break;
	case 8: *model = M_MZ_ENERGY; *skin = MZS_GREEN; break;
	}
}

static int FX_PlayerMuzzleType (int weapon)
{
	switch (weapon)
	{
	case MZ_MACHINEGUN: case MZ_CHAINGUN1: case MZ_CHAINGUN2: case MZ_CHAINGUN3: case MZ_ETF_RIFLE: return 1;
	case MZ_SHOTGUN: case MZ_SSHOTGUN: case MZ_SHOTGUN2: return 2;
	case MZ_BLASTER: case MZ_HYPERBLASTER: case MZ_PHALANX: case MZ_IONRIPPER: return 3;
	case MZ_RAILGUN: return 4;
	case MZ_ROCKET: return 5;
	case MZ_GRENADE: return 6;
	case MZ_BFG: return 7;
	case MZ_BLASTER2: return 8;
	}
	return 0;
}

/* BDGN 3.56x: width of the bullet haze of the next shot (CL_FX3D_PlayerMuzzle): machine gun half, chaingun two thirds */
static float haze_width=1.0f;

/* Visual corridor to the wall along the muzzle axis. Shotguns expand to the pellet cone. */
static void FX_BulletHaze(int type,vec3_t org,vec3_t angles)
{
 vec3_t dir,end,start;
 trace_t hit;
 fx3d_t *f;
 float distance, spread;
 AngleVectors(angles,dir,NULL,NULL);
 VectorCopy(org,start);
 if (CM_PointContents(start,0)&MASK_SHOT)
 {
  VectorMA(start,-8,dir,start);
  if (CM_PointContents(start,0)&MASK_SHOT) return;
 }
 VectorMA(start,2048,dir,end);
 hit=CM_BoxTrace(start,end,vec3_origin,vec3_origin,0,MASK_SHOT);
 if (hit.startsolid) return;
 distance=2048*hit.fraction;
 if (distance<12) return;
 f=FX_Alloc();
 f->kind=FX_DISTORT;
 f->mode=3;
 f->life=type==2?260:230;
 f->strength=type==2?0.70f:0.64f;
 f->r0=type==2?12:8;
 spread=distance*(1000.0f/8192.0f);
 f->r1=type==2?(spread>12?spread:12):1.25f;
 f->r0*=haze_width;
 f->r1*=haze_width;
 VectorCopy(start,f->origin);
 VectorCopy(hit.endpos,f->angles);
}

static void FX_Muzzle (int type, vec3_t org, vec3_t angles, qboolean view, vec3_t view_ofs, qboolean silenced)
{
	int model, skin, smoke, life;
	float haze;
	fx3d_t *f;
	if ((type==1 || type==2) && CL_FX3D_DistortOn())
		FX_BulletHaze(type,org,angles);
	if (!CL_FX3D_On(cl_hdmuzzle)) return;
	FX_MuzzleParts (type, &model, &skin, &smoke, &haze);
	if (model < 0 || !fx3d_model[model])
		return;
	life = type == 5 ? 90 : (type == 2 ? 75 : 60);
	f = FX_Alloc ();
	f->kind = view ? FX_VIEW : FX_ANIM;
	f->model = fx3d_model[model];
	f->model_index=model;
	f->flags = RF_FXMESH | RF_FXADD | RF_TRANSLUCENT;
	f->skin = skin;
	f->life = life;
	f->frames = 4;
	f->fps = 0;
	f->variant = rand () & 3;	/* 4 random shapes, no animation */
	f->alpha = silenced ? 0.45f : 1.0f;
	f->fadeout = life;
	f->depthhack = view;
	VectorCopy (org, f->origin);
	VectorCopy (angles, f->angles);
	f->angles[2] = (float)(rand () % 360);
	if (view) VectorCopy (view_ofs, f->view_ofs);
	if (smoke >= 0 && (type!=2 || !CL_FX3D_On(cl_hdsmoke)) && !silenced && !cl_nosmoke->value)
	{
		vec3_t sa;
		VectorSet (sa, angles[0] * 0.3f, angles[1], 0);	/* drifts forward, rises in the world */
		FX_Anim (smoke, org, sa, 0, 0, 20, 1.0f);
	}
	if (!view && type!=1 && type!=2 && type!=5 && haze > 0 && CL_FX3D_DistortOn ())
	{
		f = FX_Alloc ();
		f->kind = FX_DISTORT;
		f->model = NULL;
		f->life = 160;
		f->r0 = f->r1 = haze;
		f->strength = 0.7f;
		f->mode = 0;
		VectorCopy (org, f->origin);
		VectorCopy (angles, f->angles);
	}
}

/* World-space puffs remain at the shot position when the weapon moves. */
static void FX_ShotgunMuzzleSmoke(vec3_t muzzle,vec3_t right,qboolean twin,int delay)
{
 vec3_t origin;
 int barrel,puff;
 if (!CL_FX3D_On(cl_hdsmoke) || cl_nosmoke->value) return;
 for (barrel=0;barrel<(twin?2:1);barrel++)
 {
  VectorMA(muzzle,twin?(barrel?1.8f:-1.8f):0,right,origin);
  for (puff=0;puff<3;puff++)
  {
   fx3d_t *f=FX_Anim(M_SHOTGUN_SMOKE,origin,NULL,RF_PUFFS,
                    delay+barrel*68+puff*(65+barrel*14),36,0.60f-puff*0.11f);
   if (f) f->fadeout=290;
  }
 }
}

/* Both stock models return to rest at the end of the fire cycle. Use the
   interpolated view weapon's transform once that pose is on screen. */
void CL_FX3D_ViewShotgunSmoke(const entity_t *gun,int gun_index)
{
	const char *expected;
	qboolean twin;
	vec3_t forward,right,up,muzzle;
	int idle,elapsed;
	if (!shotgun_smoke_weapon) return;
	elapsed=cl.time-shotgun_smoke_time;
	twin=shotgun_smoke_weapon==MZ_SSHOTGUN;
	expected=twin?"models/weapons/v_shotg2/tris.md2":"models/weapons/v_shotg/tris.md2";
	if (elapsed<0 || elapsed>2000 || gun_index<=0 || gun_index>=MAX_MODELS ||
	    Q_stricmp(cl.configstrings[CS_MODELS+gun_index],expected))
	{
		shotgun_smoke_weapon=0;
		return;
	}
	idle=twin?18:19;
	if (elapsed<800 || gun->frame<idle || (gun->oldframe<idle && gun->backlerp>0.15f))
		return;
	shotgun_smoke_weapon=0;
	AngleVectors(gun->angles,forward,right,up);
	VectorMA(gun->origin,twin?25.9f:27.5f,forward,muzzle);
	VectorMA(muzzle,(hand_cvar->value==1?-1:1)*(twin?6.7f:7.4f),right,muzzle);
	VectorMA(muzzle,twin?-4.9f:-8.5f,up,muzzle);
	FX_ShotgunMuzzleSmoke(muzzle,right,twin,0);
}

/* svc_muzzleflash (players). The own weapon gets a flash that moves with the view. */
void CL_FX3D_PlayerMuzzle (int entnum, int weapon, qboolean silenced)
{
	int type;
	vec3_t org, fv, rv, ofs;
	centity_t *pl;
	switch (weapon)	/* BDGN 3.56s: shell casings, with every renderer */
	{
	case MZ_MACHINEGUN: Shells_Shot (entnum, 0, 1, NULL); break;
	case MZ_CHAINGUN1: case MZ_CHAINGUN2: case MZ_CHAINGUN3: Shells_Shot (entnum, 1, weapon - MZ_CHAINGUN1 + 1, NULL); break;
	case MZ_SHOTGUN: Shells_Shot (entnum, 2, 1, NULL); break;
	case MZ_SSHOTGUN: Shells_Shot (entnum, 3, 1, NULL); break;
	case MZ_HYPERBLASTER:	/* 3.56z: the own gun throws a cell casing out; 3.57h: one per 10 cells used (a shot uses
		   one cell), was one per 50 */
		if (entnum == cl.playernum + 1 && ++hyper_cell_shots >= 10)
		{
			hyper_cell_shots = 0;
			Shells_Shot (entnum, 4, 1, NULL);
		}
		break;
	case MZ_BFG: Shells_Shot (entnum, 5, 1, NULL); break;	/* 50 cells a shot: one cell each time */
	}
	if (!CL_FX3D_On (cl_hdmuzzle) && !CL_FX3D_DistortOn() && !CL_FX3D_On(cl_hdsmoke))
		return;
	type = FX_PlayerMuzzleType (weapon);
	if (!type)
		return;
	haze_width = weapon == MZ_MACHINEGUN ? 0.5f : ((weapon >= MZ_CHAINGUN1 && weapon <= MZ_CHAINGUN3) ? 0.67f : 1.0f);	/* BDGN 3.56x */
	if (entnum == cl.playernum + 1)
	{
		if (!cl_gun->value || hand_cvar->value == 2)
			return;	/* no weapon drawn */
		FX_ViewMuzzle (weapon, ofs);
		VectorMA (cl.refdef.vieworg, ofs[0], cl.v_forward, org);
		VectorMA (org, ofs[1], cl.v_right, org);
		VectorMA (org, ofs[2], cl.v_up, org);
		FX_Muzzle (type, org, cl.refdef.viewangles, true, ofs, silenced);
		if (type==2 && !silenced && CL_FX3D_On(cl_hdsmoke))
		{
			shotgun_smoke_weapon=weapon;
			shotgun_smoke_time=cl.time;
		}
		return;
	}
	pl = &cl_entities[entnum];
	AngleVectors (pl->current.angles, fv, rv, NULL);
	VectorMA (pl->current.origin, 22, fv, org);
	VectorMA (org, 9, rv, org);
	org[2] += 8;
	FX_Muzzle (type, org, pl->current.angles, false, NULL, silenced);
	if (type==2 && !silenced) FX_ShotgunMuzzleSmoke(org,rv,weapon==MZ_SSHOTGUN,1100);
}

/* svc_muzzleflash2 (monsters): origin from the game's flash offsets */
void CL_FX3D_MonsterMuzzle (int entnum, int flash_number, vec3_t origin)
{
	if (flash_number >= 0 && flash_number < 256 && entnum > 0 && entnum < MAX_EDICTS)
	{	/* BDGN 3.56s: the monsters' machine guns, chainguns and shotguns throw casings too */
		if (cl_fx3d_mz2_type[flash_number] == 1) Shells_Shot (entnum, 0, 1, origin);
		else if (cl_fx3d_mz2_type[flash_number] == 2) Shells_Shot (entnum, 2, 1, origin);
	}
	if ((!CL_FX3D_On (cl_hdmuzzle) && !CL_FX3D_DistortOn()) || flash_number < 0 || flash_number > 255 || entnum < 1 || entnum >= MAX_EDICTS)
		return;
	haze_width = 1.0f;
	FX_Muzzle (cl_fx3d_mz2_type[flash_number], origin, cl_entities[entnum].current.angles, false, NULL, false);
}

/* ----------------------------------------------------------------------------------------- per frame */

void CL_FX3D_AddEntities (void)
{
	int i;
	fx3d_t *f;
	entity_t e;
	qboolean modern = CL_FX3D_Modern ();
	qboolean distort = CL_FX3D_DistortOn ();

	Shells_AddEntities ();	/* BDGN 3.56s */
	FX_LavaSmoke();
	FX_LavaBursts();	/* BDGN 3.56z */
	for (i = 0, f = fx3d; i < MAX_FX3D; i++, f++)
	{
		int t;
		float a;
		if (f->kind == FX_FREE)
			continue;
		t = cl.time - f->start;
		if (t >= f->life || t < -2000 || !modern)
		{
			f->kind = FX_FREE;
			continue;
		}
		if (t < 0)
			continue;	/* delayed part */
		a = f->alpha;
		if (f->fadein > 0 && t < f->fadein) a *= t / (float)f->fadein;
		if (f->fadeout > 0 && t > f->life - f->fadeout) a *= (f->life - t) / (float)f->fadeout;

		if (f->kind != FX_LIGHT && f->kind != FX_VIEW && f->model_index >= M_SMOKE_A1 &&
		    (f->model_index <= M_MIST_B || f->model_index == M_MZ_SMOKE || f->model_index == M_MZ_SMOKE_BIG ||
		     (f->model_index >= M_BFG_SMOKE_BIG && f->model_index <= M_SHOTGUN_SMOKE)) && CL_ObjectTooFar (f->origin))
			continue;	/* BDGN 3.57c: object draw distance: only the smoke and mist (fire, explosions, BFG stay) */
		memset (&e, 0, sizeof(e));
		VectorCopy (f->origin, e.origin);
		VectorCopy (f->angles, e.angles);
		switch (f->kind)
		{
		case FX_LIGHT:
		{
			float x = t / (float)f->life, in = f->r0 + (f->r1 - f->r0) * x;
			if (!CL_WeaponLightEnabled (EF_ROCKET))
				break;
			in *= 1.0f - f->flicker * frand ();
			V_AddLight (f->origin, in, f->color[0] + (f->color1[0] - f->color[0]) * x,
				f->color[1] + (f->color1[1] - f->color[1]) * x, f->color[2] + (f->color1[2] - f->color[2]) * x);
			break;
		}
		case FX_DISTORT:
			if (!distort || fx3d_distorts >= MAX_FRAME_DISTORTS)
				break;
			e.flags = RF_DISTORT | RF_TRANSLUCENT;
			e.skinnum = (int)(f->r0 + (f->r1 - f->r0) * (t / (float)f->life));
			e.alpha = f->strength * (1.0f - t / (float)f->life);
			e.frame = f->mode;
			e.oldframe = f->mode ? (int)(10 + 85 * (t / (float)f->life)) : 0;
			if (f->mode==3)
			{
				e.skinnum=(int)f->r0;
				e.oldframe=(int)(f->r1*16);
				VectorCopy(f->angles,e.oldorigin);
			}
			else VectorCopy (e.origin, e.oldorigin);
			V_AddEntity (&e);
			fx3d_distorts++;
			break;
		case FX_ANIM:
		case FX_VIEW:
			if (f->kind == FX_VIEW)
			{	/* the own muzzle: follows the view */
				VectorMA (cl.refdef.vieworg, f->view_ofs[0], cl.v_forward, e.origin);
				VectorMA (e.origin, f->view_ofs[1], cl.v_right, e.origin);
				VectorMA (e.origin, f->view_ofs[2], cl.v_up, e.origin);
				VectorAdd (e.origin, cl.frame.playerstate.gunoffset, e.origin);	/* 3.55al: moves with the weapon (bob, kick) */
				e.angles[0] = cl.refdef.viewangles[0];
				e.angles[1] = cl.refdef.viewangles[1];
				if (f->depthhack) e.flags |= RF_DEPTHHACK;
			}
			e.model = f->model;
			e.flags |= f->flags;
			e.skinnum = f->skin;
			e.alpha = a;
			if (f->fps > 0)
			{
				float fr = t * f->fps / 1000.0f;
				int i0 = (int)fr;
				if (i0 >= f->frames - 1)
				{
					e.frame = e.oldframe = f->frames - 1;
					e.backlerp = 0;
				}
				else
				{
					e.oldframe = i0;
					e.frame = i0 + 1;
					e.backlerp = 1.0f - (fr - i0);
				}
			}
			else
				e.frame = e.oldframe = f->variant % f->frames;
			if (f->depthhack) e.flags |= RF_DEPTHHACK;
			if (f->scale1 > 0 && (e.flags & RF_PUFFS))
			{	/* 3.57h: about the middle of the cloud, scale0 -> scale1 over grow ms (ease out), then scale1 */
				float x = f->grow > 0 && t < f->grow ? t / (float)f->grow : 1.0f, g = 1.0f - (1.0f - x) * (1.0f - x);
				int q = (int)((f->scale0 + (f->scale1 - f->scale0) * g) * 128);
				if (q < 1) q = 1;
				if (q > 255) q = 255;
				e.lightstyle = (e.lightstyle & 0xffff) | (q << 16) | FX_PUFF_SCALE_CENTRE;
			}
			else if (f->grow > 0 && t < f->grow && (e.flags & RF_PUFFS))
			{	/* 3.56x: the scale rides in the lightstyle (client/ref.h FX_PUFF_SCALE_ONE) */
				float x = t / (float)f->grow, g = 1.0f - (1.0f - x) * (1.0f - x);
				int q = (int)(g * 255);
				if (q < 1) q = 1;
				e.lightstyle = (e.lightstyle & 0xffff) | (q << 16);
			}
			e.skin = FX_Skin (f->model_index, e.skinnum);
			VectorCopy (e.origin, e.oldorigin);
			V_AddEntity (&e);
			break;
		case FX_EXPAND:
		{	/* frames over the expansion time, then the last frame while fading */
			float fr;
			int i0;
			e.model = f->model;
			e.flags = f->flags;
			e.lightstyle=f->skin;
			if (t < f->expand)
			{
				float x = t / (float)f->expand;
				fr = x * (f->frames - 1);
				e.alpha = 1.0f;
			}
			else
			{
				fr = (float)(f->frames - 1);
				e.alpha = 1.0f - (t - f->expand) / (float)f->hold;
			}
			i0 = (int)fr;
			if (i0 >= f->frames - 1)
			{
				e.frame = e.oldframe = f->frames - 1;
				e.backlerp = 0;
			}
			else
			{
				e.oldframe = i0;
				e.frame = i0 + 1;
				e.backlerp = 1.0f - (fr - i0);
			}
			e.skin = FX_Skin (f->model_index, 0);
			VectorCopy (e.origin, e.oldorigin);
			V_AddEntity (&e);
			break;
		}
		default:
			break;
		}
	}
	fx3d_distorts = 0;
}

/* ----------------------------------------------------------------------------------------- shell casings */
/*
BDGN 3.56s: shell casings (cl_shells, Weapon Effects -> "shell casings"). The machine gun and the chaingun throw brass
casings out of the right side of the gun (the chaingun out of the side of its body, not the turning barrels), the
shotgun one red shell after the pump and the super shotgun two red shells when it breaks open. They fly in an upward
arc, tumble, bounce off walls and floors and lie on the floor for a while before they fade. Models and skins:
pak5.pak models/bdgn_shells/ (_extra/tools/shells_gen.py): a .pcx skin for the software renderer and older OpenGL
drivers, a .tga skin of twice the size for the OpenGL renderers. Works with every renderer; at most SHELL_DRAWN are
drawn and they never take the room of the ordinary entities (128 for the old renderers). Visual only.
Kinds: 0 machine gun, 1 chaingun, 2 shotgun, 3 super shotgun.
*/
cvar_t *cl_shells;
static cvar_t *cl_shells_time, *cl_shells_fwd, *cl_shells_right, *cl_shells_up;
static struct model_s *shell_model[3];	/* brass, shotgun shell, 3.56z: energy cell */
static struct image_s *shell_skin[3];
static int shells_loaded, cells_loaded;
static cvar_t *cl_cells;

#define MAX_SHELLS 64
#define SHELL_DRAWN 48
#define MAX_SHELL_QUEUE 16

typedef struct
{
	int		used, kind, start, rest;
	vec3_t	origin, velocity, angles, avel;
	int		bounces;
} shell_t;
static shell_t shells[MAX_SHELLS];

typedef struct
{
	int		time, kind, entnum, count;
	vec3_t	origin, angles;	/* monsters: the flash; else taken when it is due */
} shell_queue_t;
static shell_queue_t shell_queue[MAX_SHELL_QUEUE];

int V_ClassicRoom (void);	/* cl_view.c */

static void Shells_Clear (void)
{
	memset (shells, 0, sizeof(shells));
	memset (shell_queue, 0, sizeof(shell_queue));
}

static void Shells_Init (void)
{
	cl_shells = Cvar_Get ("cl_shells", "0", CVAR_ARCHIVE);
	cl_shells_time = Cvar_Get ("cl_shells_time", "8", CVAR_ARCHIVE);	/* seconds on the floor */
	cl_cells = Cvar_Get ("cl_cells", "0", CVAR_ARCHIVE);	/* BDGN 3.56z */
	/* BDGN 3.56u: move the own weapon's ejection port (units, view space), as cl_hdmuzzle_fwd/right/up do for the flash */
	cl_shells_fwd = Cvar_Get ("cl_shells_fwd", "0", CVAR_ARCHIVE);
	cl_shells_right = Cvar_Get ("cl_shells_right", "0", CVAR_ARCHIVE);
	cl_shells_up = Cvar_Get ("cl_shells_up", "0", CVAR_ARCHIVE);
}

static void Shells_Register (void)
{
	static const char *names[3] = { "brass", "shell", "cell" };
	char name[MAX_QPATH];
	qboolean gl = !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl") || !Q_stricmp (Cvar_VariableString ("vid_ref"), "glam");
	int i;
	memset (shells, 0, sizeof(shells));
	memset (shell_queue, 0, sizeof(shell_queue));
	shells_loaded = 1;
	hyper_cell_shots = 0;
	for (i = 0; i < 3; i++)
	{
		Com_sprintf (name, sizeof(name), "models/bdgn_shells/%s.md2", names[i]);
		shell_model[i] = FS_LoadFile (name, NULL) > 0 ? re.RegisterModel (name) : NULL;
		shell_skin[i] = NULL;
		if (!shell_model[i])
		{
			if (i < 2) shells_loaded = 0;
		}
		else if (gl)
		{	/* the OpenGL renderers read TGA; the software renderer keeps the model's own PCX */
			Com_sprintf (name, sizeof(name), "models/bdgn_shells/%s.tga", names[i]);
			if (FS_LoadFile (name, NULL) > 0)
				shell_skin[i] = re.RegisterSkin (name);
		}
	}
	cells_loaded = shell_model[2] != NULL;
	Cvar_FullSet ("bdgn_shells_available", shells_loaded ? "1" : "0", 0);
}

/* menu, presets: the models are installed (before the first map: the file decides) */
int CL_Shells_Available (void)
{
	return shells_loaded || FS_LoadFile ("models/bdgn_shells/brass.md2", NULL) > 0;
}

int CL_Cells_Available (void)	/* BDGN 3.56z */
{
	return cells_loaded || FS_LoadFile ("models/bdgn_shells/cell.md2", NULL) > 0;
}

static void Shells_Queue (int kind, int entnum, int delay, int count, vec3_t origin, vec3_t angles)
{
	int i, oldest = 0;
	for (i = 0; i < MAX_SHELL_QUEUE; i++)
	{
		if (!shell_queue[i].count)
			break;
		if (shell_queue[i].time < shell_queue[oldest].time)
			oldest = i;
	}
	if (i == MAX_SHELL_QUEUE)
		i = oldest;
	shell_queue[i].time = cl.time + delay;
	shell_queue[i].kind = kind;
	shell_queue[i].entnum = entnum;
	shell_queue[i].count = count;
	if (origin) VectorCopy (origin, shell_queue[i].origin);
	else VectorClear (shell_queue[i].origin);
	if (angles) VectorCopy (angles, shell_queue[i].angles);
	else VectorClear (shell_queue[i].angles);
}

static shell_t *Shells_Alloc (void)
{
	int i, oldest = 0;
	for (i = 0; i < MAX_SHELLS; i++)
	{
		if (!shells[i].used)
			break;
		if (shells[i].start < shells[oldest].start)
			oldest = i;
	}
	if (i == MAX_SHELLS)
		i = oldest;	/* the oldest one on the floor goes */
	memset (&shells[i], 0, sizeof(shells[i]));
	shells[i].used = 1;
	shells[i].start = cl.time;
	return &shells[i];
}

/* one casing: org in the world, v = (forward, right, up) speed in the shooter's frame */
static void Shells_Throw (int kind, vec3_t org, vec3_t fv, vec3_t rv, vec3_t uv, float f, float r, float u, vec3_t base_vel, float yaw)
{
	shell_t *s = Shells_Alloc ();
	int k;
	s->kind = kind;
	VectorCopy (org, s->origin);
	for (k = 0; k < 3; k++)
		s->velocity[k] = fv[k] * f + rv[k] * r + uv[k] * u + base_vel[k];
	s->angles[0] = crand () * 30;
	s->angles[1] = yaw + 90 + crand () * 20;	/* the case lies across the gun */
	s->angles[2] = frand () * 360;
	s->avel[0] = crand () * 900;
	s->avel[1] = crand () * 500;
	s->avel[2] = crand () * 300;
}

/* the ejection port in view space (forward, right, up), right-handed, and the speeds of the kind */
static void Shells_ViewPort (int kind, vec3_t ofs)
{
	switch (kind)
	{
	case 0: VectorSet (ofs, 13, 8.5f, -5.5f); break;	/* machine gun: port behind the muzzle on the right */
	case 1: VectorSet (ofs, 9, 9.5f, -8); break;		/* chaingun: the side of the body, behind the barrels */
	/* BDGN 3.56u: the shotgun ports from the owner's screenshots (fov 90): the shotgun's on top of the right side,
	   about a quarter of the screen right of the centre and a fifth below it; the super shotgun's at the back of the
	   right side, low on the right of the screen. Before, both started below the screen. */
	case 2: VectorSet (ofs, 20, 4.7f, -4.1f); break;	/* shotgun: top right of the body */
	case 3: VectorSet (ofs, 12, 8.8f, -4.6f); break;	/* super shotgun: the breech, right side */
	case 4: VectorSet (ofs, 12, 8, -6); break;		/* 3.56z hyperblaster: the cell comes out of the right side */
	default: VectorSet (ofs, 14, 2, -13); break;		/* 3.56z BFG: the cell falls out underneath */
	}
	ofs[0] += cl_shells_fwd->value;
	ofs[1] += cl_shells_right->value;
	ofs[2] += cl_shells_up->value;
	if (hand_cvar->value == 1) ofs[1] = -ofs[1];
}

static void Shells_Spawn (shell_queue_t *q)
{
	vec3_t org, fv, rv, uv, vel, ofs;
	float yaw, side;
	int n;
	VectorClear (vel);
	side = 1;
	if (q->entnum == cl.playernum + 1)
	{
		if (!cl_gun->value || hand_cvar->value == 2)
			return;	/* no weapon drawn */
		Shells_ViewPort (q->kind, ofs);
		VectorCopy (cl.v_forward, fv);
		VectorCopy (cl.v_right, rv);
		VectorCopy (cl.v_up, uv);
		VectorMA (cl.refdef.vieworg, ofs[0], fv, org);
		VectorMA (org, ofs[1], rv, org);
		VectorMA (org, ofs[2], uv, org);
		VectorAdd (org, cl.frame.playerstate.gunoffset, org);
		for (n = 0; n < 3; n++)
			vel[n] = cl.frame.playerstate.pmove.velocity[n] * 0.125f;
		yaw = cl.refdef.viewangles[1];
		if (hand_cvar->value == 1) side = -1;
	}
	else if (q->entnum > 0 && q->entnum < MAX_EDICTS)
	{
		centity_t *pl = &cl_entities[q->entnum];
		vec3_t a;
		if (q->origin[0] || q->origin[1] || q->origin[2])
		{	/* a monster: just behind its flash */
			VectorCopy (pl->current.angles, a);
			AngleVectors (a, fv, rv, uv);
			VectorMA (q->origin, -10, fv, org);
		}
		else
		{
			VectorCopy (pl->current.angles, a);
			AngleVectors (a, fv, rv, uv);
			VectorMA (pl->current.origin, 10, fv, org);
			VectorMA (org, 7, rv, org);
			org[2] += 14;
		}
		if (VectorLength (org) == 0)
			return;
		yaw = a[1];
	}
	else
		return;
	{	/* far away from the view: not worth it */
		vec3_t d;
		VectorSubtract (org, cl.refdef.vieworg, d);
		if (VectorLength (d) > 1500)
			return;
	}
	if (CM_PointContents (org, 0) & MASK_SOLID)
		return;	/* the gun is in a wall */
	switch (q->kind)
	{
	case 0:
		Shells_Throw (0, org, fv, rv, uv, -5 + crand () * 10, side * (75 + crand () * 15), 100 + crand () * 20, vel, yaw);
		break;
	case 1:
		Shells_Throw (0, org, fv, rv, uv, crand () * 10, side * (85 + crand () * 15), 80 + crand () * 20, vel, yaw);
		break;
	case 2:
		Shells_Throw (1, org, fv, rv, uv, 15 + crand () * 8, side * (90 + crand () * 12), 95 + crand () * 15, vel, yaw);	/* 3.56u: up and far out to the right */
		break;
	case 4:	/* 3.56z: the hyperblaster's cell, thrown to the right as the machine gun's casings, heavier */
		Shells_Throw (2, org, fv, rv, uv, crand () * 8, side * (60 + crand () * 10), 70 + crand () * 15, vel, yaw);
		break;
	case 5:	/* 3.56z: the BFG's cell drops straight down */
		Shells_Throw (2, org, fv, rv, uv, crand () * 4, crand () * 4, -30, vel, yaw);
		break;
	case 3:	/* both barrels: 3.56u: steeply up and a little to the right, a little apart */
		for (n = 0; n < 2; n++)
		{
			vec3_t o;
			VectorMA (org, n ? 1.2f : -1.2f, uv, o);
			Shells_Throw (1, o, fv, rv, uv, -10 + crand () * 8, side * (45 + crand () * 10) + (n ? 8 : -8), 150 + crand () * 15, vel, yaw);
		}
		break;
	}
}

/* svc_muzzleflash (players) and svc_muzzleflash2 (monsters): queue the casings of the shot */
static void Shells_Shot (int entnum, int weapon_kind, int shots, vec3_t monster_origin)
{
	if (cl_classic2d->value)
		return;
	if (weapon_kind >= 4)
	{	/* 3.56z: energy cells */
		if (!cl_cells || !cl_cells->value || !cells_loaded)
			return;
		if (weapon_kind == 4)
			Shells_Queue (4, entnum, 120, 1, monster_origin, NULL);
		else
			Shells_Queue (5, entnum, 450, 1, monster_origin, NULL);	/* after the shot has left */
		return;
	}
	if (!cl_shells || !cl_shells->value || !shells_loaded)
		return;
	switch (weapon_kind)
	{
	case 0: Shells_Queue (0, entnum, 25, 1, monster_origin, NULL); break;
	case 1: Shells_Queue (1, entnum, 20, shots, monster_origin, NULL); break;
	case 2: Shells_Queue (2, entnum, monster_origin ? 150 : 450, 1, monster_origin, NULL); break;	/* after the pump */
	case 3: Shells_Queue (3, entnum, 650, 1, monster_origin, NULL); break;	/* when the gun breaks open */
	}
}

static void Shells_Physics (shell_t *s, float dt)
{
	static vec3_t mins = { -0.4f, -0.4f, -0.4f }, maxs = { 0.4f, 0.4f, 0.4f };
	vec3_t end;
	trace_t tr;
	int k;
	if (s->rest)
		return;
	s->velocity[2] -= 800 * dt;
	VectorMA (s->origin, dt, s->velocity, end);
	tr = CM_BoxTrace (s->origin, end, mins, maxs, 0, MASK_SOLID);
	if (tr.allsolid)
	{
		s->used = 0;
		return;
	}
	VectorCopy (tr.endpos, s->origin);
	for (k = 0; k < 3; k++)
		s->angles[k] = anglemod (s->angles[k] + s->avel[k] * dt);
	if (tr.fraction < 1)
	{
		float vn = DotProduct (s->velocity, tr.plane.normal);
		VectorMA (s->velocity, -1.45f * vn, tr.plane.normal, s->velocity);	/* bounce, 45 % */
		VectorScale (s->velocity, 0.7f, s->velocity);	/* friction */
		VectorScale (s->avel, 0.6f, s->avel);
		s->bounces++;
		if (tr.plane.normal[2] > 0.7f && (VectorLength (s->velocity) < 40 || s->bounces > 5))
		{	/* lies on the floor: the case on its side */
			s->rest = cl.time;
			s->angles[0] = 0;
			s->angles[2] = 0;
			s->origin[2] += (s->kind == 2 ? 1.25f : (s->kind ? 0.68f : 0.45f)) - 0.4f;
			VectorClear (s->velocity);
		}
	}
}

static void Shells_AddEntities (void)
{
	int i, drawn = 0, room, stay, life;
	float dt = cls.frametime;
	entity_t e;
	if (!shells_loaded && !cells_loaded)
		return;
	if ((!cl_shells->value && !cl_cells->value) || cl_classic2d->value)
	{
		memset (shells, 0, sizeof(shells));
		memset (shell_queue, 0, sizeof(shell_queue));
		return;
	}
	if (dt > 0.05f) dt = 0.05f;
	if (dt < 0) dt = 0;
	for (i = 0; i < MAX_SHELL_QUEUE; i++)
	{
		shell_queue_t *q = &shell_queue[i];
		if (!q->count || q->time > cl.time)
			continue;
		if (q->time < cl.time - 1000)
		{	/* stale (a level change, a pause) */
			q->count = 0;
			continue;
		}
		Shells_Spawn (q);
		q->count--;
		q->time += 35;	/* the chaingun's next one */
	}
	stay = (int)(cl_shells_time->value * 1000);
	if (stay < 500) stay = 500;
	if (stay > 60000) stay = 60000;
	room = V_ClassicRoom () - 16;	/* the casings never take the room of monsters and items */
	if (room > SHELL_DRAWN) room = SHELL_DRAWN;
	for (i = 0; i < MAX_SHELLS; i++)
	{
		shell_t *s = &shells[i];
		float a = 1;
		if (!s->used)
			continue;
		if (cl.time < s->start || cl.time - s->start > 15000 + stay)
		{
			s->used = 0;
			continue;
		}
		Shells_Physics (s, dt);
		if (!s->used)
			continue;
		if (s->rest)
		{
			life = cl.time - s->rest;
			if (life > stay)
			{
				s->used = 0;
				continue;
			}
			if (life > stay - 1500)
				a = (stay - life) / 1500.0f;
		}
		if (drawn >= room || CL_ObjectTooFar (s->origin))
			continue;	/* 3.57c: object draw distance */
		memset (&e, 0, sizeof(e));
		e.model = shell_model[s->kind];
		e.skin = shell_skin[s->kind];
		VectorCopy (s->origin, e.origin);
		VectorCopy (s->origin, e.oldorigin);
		VectorCopy (s->angles, e.angles);
		if (a < 1)
		{
			e.flags = RF_TRANSLUCENT;
			e.alpha = a;
		}
		V_AddEntity (&e);
		drawn++;
	}
}
