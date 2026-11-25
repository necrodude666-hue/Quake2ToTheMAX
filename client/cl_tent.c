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
// cl_tent.c -- client side temporary entities

#include "client.h"

typedef enum
{
	ex_free, ex_explosion, ex_misc, ex_flash, ex_mflash, ex_poly, ex_poly2, ex_simple
} exptype_t;

typedef struct
{
	exptype_t	type;
	entity_t	ent;

	int			frames;
	float		light;
	vec3_t		lightcolor;
	float		start;
	int			baseframe;
	unsigned int light_effects;
	int			duration;	// simple marker lifetime in milliseconds
						// BDGN 3.51v: ex_simple with frames > 1 is an animated sprite (sprites/bdgn_fx)
	vec3_t		base;		// BDGN 3.51ac: explosion point of a sprite explosion
	float		pull;		// BDGN 3.51ac: drawn this far towards the viewer, so the flat picture is not cut by the wall
	int			based;		// BDGN 3.55s: base set (3D explosion models are pulled too)
} explosion_t;



#define	MAX_EXPLOSIONS	32
explosion_t	cl_explosions[MAX_EXPLOSIONS];


#define	MAX_BEAMS	32
typedef struct
{
	int		entity;
	int		dest_entity;
	struct model_s	*model;
	int		endtime;
	vec3_t	offset;
	vec3_t	start, end;
} beam_t;
beam_t		cl_beams[MAX_BEAMS];
//PMM - added this for player-linked beams.  Currently only used by the plasma beam
beam_t		cl_playerbeams[MAX_BEAMS];


#define	MAX_LASERS	32
typedef struct
{
	entity_t	ent;
	int			endtime;
} laser_t;
laser_t		cl_lasers[MAX_LASERS];
/* Separate pool: long-lived rail beams must not evict short BFG lasers. */
typedef struct
{
	entity_t ent;
	int endtime, duration;
	float alpha;
} railbeam_t;
static railbeam_t cl_railbeams[MAX_LASERS];

static void CL_RailBeam (vec3_t start, vec3_t end, int color, int diameter, int duration, float alpha)
{
	railbeam_t *l;
	int i, oldest = 0;

	for (i = 0; i < MAX_LASERS; i++)
	{
		if (cl_railbeams[i].endtime <= cl.time)
			break;
		if (cl_railbeams[i].endtime < cl_railbeams[oldest].endtime)
			oldest = i;
	}
	l = &cl_railbeams[i < MAX_LASERS ? i : oldest];
	memset (l, 0, sizeof(*l));
	l->ent.flags = RF_TRANSLUCENT | RF_BEAM;
	VectorCopy (start, l->ent.origin);
	VectorCopy (end, l->ent.oldorigin);
	l->ent.skinnum = color;
	l->ent.frame = diameter;
	l->endtime = cl.time + duration;
	l->duration = duration;
	l->alpha = alpha;
}

void CL_SimpleRailTrail (vec3_t start, vec3_t end)
{
	CL_RailBeam (start, end, 0xf3, 4, 1200, 0.45f);
}

void CL_SpriteRailCore (vec3_t start, vec3_t end)
{
	/* The original's grey core fades faster than its blue spiral. */
	CL_RailBeam (start, end, 10, 2, 700, 0.32f);
}


//ROGUE
cl_sustain_t	cl_sustains[MAX_SUSTAINS];
//ROGUE

//PGM
extern void CL_TeleportParticles (vec3_t org);
//PGM

void CL_BlasterParticles (vec3_t org, vec3_t dir);
void CL_ExplosionParticles (vec3_t org);
void CL_BFGExplosionParticles (vec3_t org);
// RAFAEL
void CL_BlueBlasterParticles (vec3_t org, vec3_t dir);

struct sfx_s	*cl_sfx_ric1;
struct sfx_s	*cl_sfx_ric2;
struct sfx_s	*cl_sfx_ric3;
struct sfx_s	*cl_sfx_lashit;
struct sfx_s	*cl_sfx_spark5;
struct sfx_s	*cl_sfx_spark6;
struct sfx_s	*cl_sfx_spark7;
struct sfx_s	*cl_sfx_railg;
struct sfx_s	*cl_sfx_rockexp;
struct sfx_s	*cl_sfx_grenexp;
struct sfx_s	*cl_sfx_watrexp;
// RAFAEL
struct sfx_s	*cl_sfx_plasexp;
struct sfx_s	*cl_sfx_footsteps[4];

struct model_s	*cl_mod_explode;
struct model_s	*cl_mod_smoke;
struct model_s	*cl_mod_flash;
struct model_s	*cl_mod_parasite_segment;
struct model_s	*cl_mod_grapple_cable;
struct model_s	*cl_mod_parasite_tip;
struct model_s	*cl_mod_explo4;
struct model_s	*cl_mod_bfg_explo;
static struct model_s *cl_mod_rocket_circle, *cl_mod_bfg_oval, *cl_mod_bfg_core_oval;
static struct model_s *cl_mod_rocket_ball, *cl_mod_bfg_ball, *cl_mod_blaster_ball;
static struct model_s *cl_mod_blaster_projectile;
static struct model_s *cl_mod_hit_grey, *cl_mod_hit_blood, *cl_mod_hit_energy;
/* BDGN 3.51v: "sprites" mode (cl_static_rockets / cl_static_bfg / cl_simpleblaster 2): animated 2D sprites made from
   the original explosion model and sprites (_extra/fx/build_fx.py, sprites/bdgn_fx in bdgn_sprites.pak). Value 1
   keeps the simple flat shapes; a missing sprite falls back to the simple shape. */
static struct model_s *cl_fx_rocket_explo, *cl_fx_grenade_explo, *cl_fx_bfg_explo, *cl_fx_bfg_ball, *cl_fx_bfg_hit;
/* BDGN 3.55s: HD weapon effects (pak8-hdfx.pak, sprites/hdfx/), cl_hdfx 1 */
static struct model_s *cl_hdfx_explo, *cl_hdfx_grenade, *cl_hdfx_bfg_explo, *cl_hdfx_bfg_ball, *cl_hdfx_blaster,
	*cl_hdfx_hyper, *cl_hdfx_rocket, *cl_hdfx_blaster_hit, *cl_hdfx_rail_hit;
cvar_t *cl_hdfx;
static qboolean CL_HDFX(void)
{
	return cl_hdfx && cl_hdfx->value && cl_hdfx_explo && !cl_classic2d->value &&
		!Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
}
/* BDGN 3.56: 2D classic mode shows explosions and energy hits as animated 2D sprites in its standard form too (was
   only with "low end mode" on top of it). 80 ms per picture = 12.5 pictures per second; low end keeps its shorter run. */
#define CL_2D_SPRITES() (cl_classic2d->value != 0)
/* BDGN 3.58u: the simple one-colour explosions and hits (value 1) are shown in place of the classic 2D sprites when
   both are on (Ultra Fast and PSYCHO Fast, owner 2026-11-20); value 2 asks for the sprites, 0 follows the 2D mode */
#define CL_SPR_ROCKET() (cl_static_rockets->value >= 2 || (CL_2D_SPRITES() && cl_static_rockets->value != 1))
#define CL_SPR_BFG() (cl_static_bfg->value >= 2 || (CL_2D_SPRITES() && cl_static_bfg->value != 1))
#define CL_SPR_BLASTER() (cl_simpleblaster->value >= 2 || (CL_2D_SPRITES() && cl_simpleblaster->value != 1))
static struct model_s *CL_HDFXModel (const char *name)
{
	if (FS_LoadFile ((char *)name, NULL) <= 0)
		return NULL;	/* pack not installed: no "can't find" messages */
	return re.RegisterModel ((char *)name);
}
/* BDGN 3.59o: the HD sprites, 3D effects and HD gore cannot be shown in classic 2D mode or by the software renderer.
   A map load then only checks that the packs exist (the menus keep their "available" state) and skips loading
   them: 6.8 s of an 8.9 s effects phase on a dual Pentium II 300 with 3dfx MiniGL (v3.59n, MEASURED). Turning
   classic 2D off during a map loads them at that moment (CL_HDAssetsFrame, once). */
static int cl_hd_deferred;
qboolean CL_HDAssetsUnused (void)
{
	static cvar_t *lazy;
	if (!lazy)
		lazy = Cvar_Get ("cl_hdload_lazy", "1", CVAR_ARCHIVE);	/* menu: LOADING; 0 loads them as 3.59n did */
	return lazy->value && (cl_classic2d->value || Q_stricmp (Cvar_VariableString ("vid_ref"), "gl"));
}
static void CL_RegisterHDFX (void)
{
	cl_hdfx_explo = CL_HDFXModel ("sprites/hdfx/explosion.sp2");	/* BDGN 3.55s */
	cl_hdfx_grenade = CL_HDFXModel ("sprites/hdfx/grenade.sp2");
	cl_hdfx_bfg_explo = CL_HDFXModel ("sprites/hdfx/bfg_explo.sp2");
	cl_hdfx_bfg_ball = CL_HDFXModel ("sprites/hdfx/bfg_ball.sp2");
	cl_hdfx_blaster = CL_HDFXModel ("sprites/hdfx/blaster.sp2");
	cl_hdfx_hyper = CL_HDFXModel ("sprites/hdfx/hyper.sp2");
	cl_hdfx_rocket = CL_HDFXModel ("sprites/hdfx/rocket.sp2");
	cl_hdfx_blaster_hit = CL_HDFXModel ("sprites/hdfx/blaster_hit.sp2");
	cl_hdfx_rail_hit = CL_HDFXModel ("sprites/hdfx/rail_hit.sp2");
}
void CL_HDAssetsFrame (void)
{
	if (!cl_hd_deferred || CL_HDAssetsUnused ())
		return;
	cl_hd_deferred = 0;
	CL_RegisterHDFX ();
	CL_FX3D_RegisterHD ();
	CL_Gore_RegisterHD ();
}
static struct model_s *cl_fx_blaster_bolt, *cl_fx_blaster_hit, *cl_fx_rocket_side, *cl_mod_bfg3;
struct model_s	*cl_mod_powerscreen;
// RAFAEL
struct model_s	*cl_mod_plasmaexplo;

//ROGUE
struct sfx_s	*cl_sfx_lightning;
struct sfx_s	*cl_sfx_disrexp;
struct model_s	*cl_mod_lightning;
struct model_s	*cl_mod_heatbeam;
struct model_s	*cl_mod_monster_heatbeam;
struct model_s	*cl_mod_explo4_big;

//ROGUE
/*
=================
CL_RegisterTEntSounds
=================
*/
void CL_RegisterTEntSounds (void)
{
	int		i;
	char	name[MAX_QPATH];

	// PMM - version stuff
//	Com_Printf ("%s\n", ROGUE_VERSION_STRING);
	// PMM
	cl_sfx_ric1 = S_RegisterSound ("world/ric1.wav");
	cl_sfx_ric2 = S_RegisterSound ("world/ric2.wav");
	cl_sfx_ric3 = S_RegisterSound ("world/ric3.wav");
	cl_sfx_lashit = S_RegisterSound("weapons/lashit.wav");
	cl_sfx_spark5 = S_RegisterSound ("world/spark5.wav");
	cl_sfx_spark6 = S_RegisterSound ("world/spark6.wav");
	cl_sfx_spark7 = S_RegisterSound ("world/spark7.wav");
	cl_sfx_railg = S_RegisterSound ("weapons/railgf1a.wav");
	cl_sfx_rockexp = S_RegisterSound ("weapons/rocklx1a.wav");
	cl_sfx_grenexp = S_RegisterSound ("weapons/grenlx1a.wav");
	cl_sfx_watrexp = S_RegisterSound ("weapons/xpld_wat.wav");
	// RAFAEL
	// cl_sfx_plasexp = S_RegisterSound ("weapons/plasexpl.wav");
	S_RegisterSound ("player/land1.wav");

	S_RegisterSound ("player/fall2.wav");
	S_RegisterSound ("player/fall1.wav");

	for (i=0 ; i<4 ; i++)
	{
		Com_sprintf (name, sizeof(name), "player/step%i.wav", i+1);
		cl_sfx_footsteps[i] = S_RegisterSound (name);
	}

//PGM
	cl_sfx_lightning = S_RegisterSound ("weapons/tesla.wav");
	cl_sfx_disrexp = S_RegisterSound ("weapons/disrupthit.wav");
	// version stuff
//	sprintf (name, "weapons/sound%d.wav", ROGUE_VERSION_ID);
//	if (name[0] == 'w')
//		name[0] = 'W';
//PGM
}	

/*
=================
CL_RegisterTEntModels
=================
*/
/* BDGN 3.59k: where the "effects" phase of a map load goes (5.9 s on a dual Pentium II 300 with 3dfx MiniGL,
   v3.59h, MEASURED); cl_view.c writes it into bdgn_mapload.txt and LOAD_PHASES */
char cl_load_fx_detail[320];

void CL_RegisterTEntModels (void)
{
	int t0 = Sys_Milliseconds (), t1, t2, t3, t4, t5, t6, t7;
	int i0 = (int)Cvar_VariableValue ("bdgn_img_new"), i1, i2;	/* 3.59q: images read from disk (ref_gl gl_image.c) */
	CL_RegisterSimpleItems ();
	t1 = Sys_Milliseconds ();
	i1 = (int)Cvar_VariableValue ("bdgn_img_new");
	CL_RegisterSimpleEnemies ();
	t2 = Sys_Milliseconds ();
	i2 = (int)Cvar_VariableValue ("bdgn_img_new");
	cl_mod_explode = re.RegisterModel ("models/objects/explode/tris.md2");
	cl_mod_smoke = re.RegisterModel ("models/objects/smoke/tris.md2");
	cl_mod_flash = re.RegisterModel ("models/objects/flash/tris.md2");
	cl_mod_parasite_segment = re.RegisterModel ("models/monsters/parasite/segment/tris.md2");
	cl_mod_grapple_cable = re.RegisterModel ("models/ctf/segment/tris.md2");
	cl_mod_parasite_tip = re.RegisterModel ("models/monsters/parasite/tip/tris.md2");
	cl_mod_explo4 = re.RegisterModel ("models/objects/r_explode/tris.md2");
	cl_mod_bfg_explo = re.RegisterModel ("sprites/s_bfg2.sp2");
	cl_mod_rocket_circle = re.RegisterModel ("sprites/bdgn_rocket_circle.sp2");
	cl_mod_bfg_oval = re.RegisterModel ("sprites/bdgn_bfg_oval.sp2");
	cl_mod_bfg_core_oval = re.RegisterModel ("sprites/bdgn_bfg_core_oval.sp2");
	cl_mod_rocket_ball = re.RegisterModel ("sprites/bdgn_rocket_ball.sp2");
	cl_mod_bfg_ball = re.RegisterModel ("sprites/bdgn_bfg_ball.sp2");
	cl_mod_blaster_ball = re.RegisterModel ("sprites/bdgn_blaster_ball.sp2");
	cl_mod_hit_grey = re.RegisterModel ("sprites/bdgn_hit_grey.sp2");
	cl_mod_hit_blood = re.RegisterModel ("sprites/bdgn_hit_blood.sp2");
	cl_mod_hit_energy = re.RegisterModel ("sprites/bdgn_hit_energy.sp2");
	cl_fx_rocket_explo = re.RegisterModel ("sprites/bdgn_fx/rocket_explo.sp2");
	cl_fx_grenade_explo = re.RegisterModel ("sprites/bdgn_fx/grenade_explo.sp2");
	cl_fx_bfg_explo = re.RegisterModel ("sprites/bdgn_fx/bfg_explo.sp2");
	cl_fx_bfg_ball = re.RegisterModel ("sprites/bdgn_fx/bfg_ball.sp2");
	cl_fx_bfg_hit = re.RegisterModel ("sprites/bdgn_fx/bfg_hit.sp2");
	cl_fx_blaster_bolt = re.RegisterModel ("sprites/bdgn_fx/blaster_bolt.sp2");
	cl_fx_blaster_hit = re.RegisterModel ("sprites/bdgn_fx/blaster_hit.sp2");
	cl_fx_rocket_side = re.RegisterModel ("sprites/bdgn_fx/rocket_side.sp2");
	cl_mod_bfg3 = re.RegisterModel ("sprites/s_bfg3.sp2");
	t3 = Sys_Milliseconds ();
	cl_hd_deferred = CL_HDAssetsUnused ();
	if (cl_hd_deferred)
	{
		cl_hdfx_explo = cl_hdfx_grenade = cl_hdfx_bfg_explo = cl_hdfx_bfg_ball = cl_hdfx_blaster = NULL;
		cl_hdfx_hyper = cl_hdfx_rocket = cl_hdfx_blaster_hit = cl_hdfx_rail_hit = NULL;
		Cvar_FullSet ("bdgn_hdfx_available", FS_LoadFile ("sprites/hdfx/explosion.sp2", NULL) > 0 ? "1" : "0", 0);
	}
	else
	{
		CL_RegisterHDFX ();
		Cvar_FullSet ("bdgn_hdfx_available", cl_hdfx_explo ? "1" : "0", 0);
	}
	t4 = Sys_Milliseconds ();
	CL_FX3D_RegisterModels ();	/* BDGN 3.55t */
	t5 = Sys_Milliseconds ();
	CL_Gore_RegisterModels ();	/* BDGN 3.55y */
	t6 = Sys_Milliseconds ();
	CL_RegisterSpriteTrails ();	/* BDGN 3.51ah (test) */
	t7 = Sys_Milliseconds ();
	Com_sprintf (cl_load_fx_detail, sizeof(cl_load_fx_detail),
		"simple items %i (%i new images), simple enemies + 2D sprite banks %i (%i new images), explosions + sprites %i, HD effects %i, 3D effects %i, gore %i, sprite trails %i, all %i new images",
		t1 - t0, i1 - i0, t2 - t1, i2 - i1, t3 - t2, t4 - t3, t5 - t4, t6 - t5, t7 - t6, (int)Cvar_VariableValue ("bdgn_img_new") - i0);
	cl_mod_powerscreen = re.RegisterModel ("models/items/armor/effect/tris.md2");

cl_mod_blaster_projectile = re.RegisterModel ("models/objects/laser/tris.md2");
re.RegisterModel ("models/objects/grenade2/tris.md2");
re.RegisterModel ("models/weapons/v_machn/tris.md2");
re.RegisterModel ("models/weapons/v_handgr/tris.md2");
re.RegisterModel ("models/weapons/v_shotg2/tris.md2");
re.RegisterModel ("models/objects/gibs/bone/tris.md2");
re.RegisterModel ("models/objects/gibs/sm_meat/tris.md2");
re.RegisterModel ("models/objects/gibs/bone2/tris.md2");
// RAFAEL
// re.RegisterModel ("models/objects/blaser/tris.md2");

re.RegisterPic ("w_machinegun");
re.RegisterPic ("a_bullets");
re.RegisterPic ("i_health");
re.RegisterPic ("a_grenades");

//ROGUE
	cl_mod_explo4_big = re.RegisterModel ("models/objects/r_explode2/tris.md2");
	cl_mod_lightning = re.RegisterModel ("models/proj/lightning/tris.md2");
	cl_mod_heatbeam = re.RegisterModel ("models/proj/beam/tris.md2");
	cl_mod_monster_heatbeam = re.RegisterModel ("models/proj/widowbeam/tris.md2");
//ROGUE
}	

/*
=================
CL_ClearTEnts
=================
*/
static struct { vec3_t origin; int start; } water_ripples[MAX_WATER_RIPPLES];
static int water_ripple_next, water_entry_frame[MAX_EDICTS];

static qboolean CL_WaterInteraction(void)
{
 static cvar_t *capable, *surface, *interaction;
 if (!capable) capable=Cvar_Get("bdgn_gl_water_surface_capable","0",0);
 if (!capable->value) return false;
 if (!surface) surface=Cvar_Get("gl_water_surface","0",CVAR_ARCHIVE);
 if (!interaction) interaction=Cvar_Get("gl_water_interaction","0",CVAR_ARCHIVE);
 return surface->value && interaction->value;
}

static void CL_AddWaterRipple(vec3_t pos)
{
 int slot=water_ripple_next++ % MAX_WATER_RIPPLES;
 VectorCopy(pos,water_ripples[slot].origin);
 water_ripples[slot].start=cl.time+1;
}

void CL_WaterEntry(int entity, int serverframe, vec3_t previous, vec3_t current)
{
 vec3_t above, below, point, normal={0,0,1};
 int i;
 if (previous[2]<=current[2] || !CL_WaterInteraction() || water_entry_frame[entity]==serverframe)
  return;
 water_entry_frame[entity]=serverframe;
 VectorCopy(previous,above);
 VectorCopy(current,below);
 above[2]-=20;
 below[2]-=20;
 if ((CM_PointContents(above,0)&CONTENTS_WATER) || !(CM_PointContents(below,0)&CONTENTS_WATER))
  return;
 for (i=0;i<7;i++)
 {
  VectorAdd(above,below,point);
  VectorScale(point,0.5f,point);
  if (CM_PointContents(point,0)&CONTENTS_WATER) { VectorCopy(point,below); }
  else { VectorCopy(point,above); }
 }
 CL_AddWaterRipple(above);
 CL_ParticleEffect(above,normal,0xb0,18);
}

static void CL_AddWaterRipples(void)
{
 entity_t ent;
 int i, slot, age;
 if (!CL_WaterInteraction()) return;
 memset(&ent,0,sizeof(ent));
 ent.flags=RF_TRANSLUCENT|RF_WATER_RIPPLE;
 for (i=0;i<MAX_WATER_RIPPLES && i<water_ripple_next;i++)
 {
  slot=(water_ripple_next-1-i)%MAX_WATER_RIPPLES;
  if (!water_ripples[slot].start) continue;
  age=cl.time-water_ripples[slot].start+1;
  if (age<0 || age>=1500) continue;
  VectorCopy(water_ripples[slot].origin,ent.origin);
  ent.alpha=age*0.001f;
  V_AddEntity(&ent);
 }
}

void CL_ClearTEnts (void)
{
	water_ripple_next=0;
	memset(water_ripples,0,sizeof(water_ripples));
	memset(water_entry_frame,0,sizeof(water_entry_frame));
	memset (cl_beams, 0, sizeof(cl_beams));
	memset (cl_explosions, 0, sizeof(cl_explosions));
	memset (cl_lasers, 0, sizeof(cl_lasers));
	memset (cl_railbeams, 0, sizeof(cl_railbeams));
	CL_FX3D_Clear ();	/* BDGN 3.55t */
	CL_Gore_Clear ();	/* BDGN 3.55y */

//ROGUE
	memset (cl_playerbeams, 0, sizeof(cl_playerbeams));
	memset (cl_sustains, 0, sizeof(cl_sustains));
//ROGUE
}

/*
=================
CL_AllocExplosion
=================
*/
explosion_t *CL_AllocExplosion (void)
{
	int		i;
	int		time;
	int		index;
	
	for (i=0 ; i<MAX_EXPLOSIONS ; i++)
	{
		if (cl_explosions[i].type == ex_free)
		{
			memset (&cl_explosions[i], 0, sizeof (cl_explosions[i]));
			return &cl_explosions[i];
		}
	}
// find the oldest explosion
	time = cl.time;
	index = 0;

	for (i=0 ; i<MAX_EXPLOSIONS ; i++)
		if (cl_explosions[i].start < time)
		{
			time = cl_explosions[i].start;
			index = i;
		}
	memset (&cl_explosions[index], 0, sizeof (cl_explosions[index]));
	return &cl_explosions[index];
}

/*
=================
CL_SmokeAndFlash
=================
*/
/* BDGN 3.51v: one animated sprite frame in place of a projectile (visual only) */
static void CL_SetSpriteProjectile (entity_t *ent, struct model_s *model, int frame, float alpha)
{
	ent->model = model;
	ent->skin = NULL;
	ent->skinnum = 0;
	ent->frame = ent->oldframe = frame;
	ent->backlerp = 0;
	ent->flags = RF_FULLBRIGHT;
	if (alpha < 1.0f) ent->flags |= RF_TRANSLUCENT;
	ent->alpha = alpha;
	VectorClear (ent->angles);
}

/* Replace only the rendered projectile, never its server state or movement. */
qboolean CL_SimpleProjectile (entity_t *ent, unsigned int effects)
{
	struct model_s *model;
	float alpha;

	model = NULL;
	alpha = 1.0;
	/* BDGN 3.51v: sprites mode; the frame is set below, after the shared fields */
	if (CL_SPR_BFG() && (effects & EF_BFG) && cl_mod_bfg3 && ent->model == cl_mod_bfg3 && cl_fx_bfg_explo)
	{	// the BFG's big explosion entity (6 frames on the server) -> 3 sprite frames
		int f = ent->frame / 2;
		CL_SetSpriteProjectile (ent, cl_fx_bfg_explo, f > 2 ? 2 : (f < 0 ? 0 : f), 0.70f);
		return true;
	}
	if (cl_static_bfg->value >= 2 && (effects & EF_BFG) && (effects & EF_ANIM_ALLFAST) && cl_fx_bfg_ball)
	{
		CL_SetSpriteProjectile (ent, cl_fx_bfg_ball, (cl.time / 80) & 1, 0.80f);
		return true;
	}
	if (cl_static_rockets->value >= 2 && (effects & EF_ROCKET) && cl_fx_rocket_side)
	{	// BDGN 3.51w: 4 directions from the rocket's flight direction (its angles) and the view:
		// 0 flying away (tail and flame), 1 coming closer (nose), 2 nose to the left, 3 nose to the right
		vec3_t fwd, tocam;
		float d;
		int dir;
		AngleVectors (ent->angles, fwd, NULL, NULL);
		VectorSubtract (ent->origin, cl.refdef.vieworg, tocam);
		VectorNormalize (tocam);
		d = DotProduct (fwd, tocam);
		if (d > 0.75f) dir = 0;
		else if (d < -0.75f) dir = 1;
		else dir = DotProduct (fwd, cl.v_right) > 0 ? 3 : 2;
		CL_SetSpriteProjectile (ent, cl_fx_rocket_side, dir * 2 + ((cl.time / 80) & 1), 1.0f);
		return true;
	}
	if ((cl_simpleblaster->value >= 2 || (CL_LOWEND_SPRITES() && cl_simpleblaster->value != 1)) && cl_fx_blaster_bolt &&
		((effects & (EF_BLASTER|EF_HYPERBLASTER|EF_BLUEHYPERBLASTER)) ||
		(cl_mod_blaster_projectile && ent->model == cl_mod_blaster_projectile)))
	{
		CL_SetSpriteProjectile (ent, cl_fx_blaster_bolt, (cl.time / 80) & 1, 0.85f);
		return true;
	}
	if (cl_static_bfg->value && (effects & EF_BFG) && (effects & EF_ANIM_ALLFAST))
	{
		model = cl_mod_bfg_ball;
		alpha = 0.50;
	}
	else if (cl_static_rockets->value && (effects & EF_ROCKET))
		model = cl_mod_rocket_ball;
	/* Most hyperblaster bolts have effects == 0. Reuse the registered model. */
	else if (cl_simpleblaster->value &&
		((effects & (EF_BLASTER|EF_HYPERBLASTER|EF_BLUEHYPERBLASTER)) ||
		(cl_mod_blaster_projectile && ent->model == cl_mod_blaster_projectile)))
	{
		model = cl_mod_blaster_ball;
		alpha = 0.50;
	}
	if (!model) return false;	// missing optional asset: keep the original projectile
	ent->model = model;
	ent->skin = NULL;
	ent->skinnum = 0;
	ent->frame = ent->oldframe = 0;
	ent->backlerp = 0;
	ent->flags = RF_FULLBRIGHT;
	if (alpha < 1.0) ent->flags |= RF_TRANSLUCENT;
	ent->alpha = alpha;
	VectorClear (ent->angles);
	return true;
}

static void CL_SimpleExplosion (vec3_t origin, struct model_s *model, int duration)
{
	explosion_t *ex;

	if (!model) return;	// optional sprite files absent: no original explosion fallback
	ex = CL_AllocExplosion ();
	ex->type = ex_simple;
	ex->duration = duration;
	ex->start = cl.time;
	ex->ent.model = model;
	ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
	ex->ent.alpha = 0.50;
	VectorCopy (origin, ex->ent.origin);
	VectorCopy (origin, ex->ent.oldorigin);
}

/* BDGN 3.51v: an animated sprite explosion: frames shown evenly over duration ms, fading out over the second half */
static explosion_t *CL_SpriteExplosion (vec3_t origin, struct model_s *model, int duration, int frames)
{
	explosion_t *ex;
	float r;

	if (!model) return NULL;
	ex = CL_AllocExplosion ();
	/* BDGN 3.51ac: a flat sprite at the impact point goes half into the wall; draw it about half its size towards the
	   viewer (the original 3D explosion has the same problem only a little, being round) */
	r = model == cl_fx_rocket_explo || model == cl_fx_grenade_explo ? 48.0f : (model == cl_fx_bfg_explo ? 40.0f : (model == cl_fx_bfg_hit ? 24.0f : 6.0f));
	if (model && (model == cl_hdfx_explo || model == cl_hdfx_grenade)) r = 56.0f;	/* BDGN 3.55s */
	else if (model && model == cl_hdfx_bfg_explo) r = 80.0f;
	else if (model && (model == cl_hdfx_blaster_hit || model == cl_hdfx_rail_hit)) r = 14.0f;
	ex->pull = r;
	VectorCopy (origin, ex->base);
	ex->type = ex_simple;
	ex->duration = CL_LOWEND_SPRITES()?duration*2/3:duration;
	ex->frames = frames;
	ex->start = cl.time;
	ex->ent.model = model;
	ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
	ex->ent.alpha = 0.90f;
	VectorCopy (origin, ex->ent.origin);
	VectorCopy (origin, ex->ent.oldorigin);
	return ex;
}

/* BDGN 3.51w: blaster / hyperblaster / green blaster / flechette hits follow "simple explosions"
   (cl_simpleblaster, set together with cl_static_rockets by the menu): 2 = sparks sprite, 1 = the simple energy ball,
   0 = the original effect (no longer changed by "blood / bullet hits"). Returns true when handled. */
static void CL_SimpleImpact (vec3_t pos, vec3_t normal, struct model_s *model);
static qboolean CL_SimpleBlasterHit (vec3_t pos, vec3_t dir, qboolean sound)
{
	vec3_t n, at;
	if (!cl_simpleblaster->value && !(CL_2D_SPRITES() && cl_fx_blaster_hit))
		return false;
	VectorCopy (dir, n);
	VectorNormalize (n);
	if (CL_SPR_BLASTER() && cl_fx_blaster_hit)
	{
		VectorMA (pos, 2.0f, n, at);
		CL_SpriteExplosion (at, cl_fx_blaster_hit, 320, 4);
	}
	else
		CL_SimpleImpact (pos, n, cl_mod_hit_energy);
	if (sound)
		S_StartSound (pos, 0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
	return true;
}

/* rocket / grenade / plasma / plain explosion: sprites mode, else the simple shape */
static void CL_RocketStyleExplosion (vec3_t pos, qboolean grenade)
{
	struct model_s *m = grenade && cl_fx_grenade_explo ? cl_fx_grenade_explo : cl_fx_rocket_explo;
	if (CL_SPR_ROCKET() && m)
		CL_SpriteExplosion (pos, m, 640, 8);
	else
		CL_SimpleExplosion (pos, cl_mod_rocket_circle, 250);
}

/* BDGN 3.55s: HD weapon effects. Animated sprite with its own light (fading), drawn towards the viewer. */
static void CL_HDFXExplosion (vec3_t pos, struct model_s *model, int duration, int frames, float light, float r, float g, float b)
{
	explosion_t *ex = CL_SpriteExplosion (pos, model, duration, frames);
	if (!ex)
		return;
	ex->ent.alpha = 0.95f;
	ex->light = light;
	ex->lightcolor[0] = r; ex->lightcolor[1] = g; ex->lightcolor[2] = b;
}

/* glow sprite on a flying projectile (cl_ents.c) */
void CL_HDFXProjectile (vec3_t origin, int effects)
{
	entity_t ent;
	struct model_s *m = NULL;
	if (!CL_HDFX ())
		return;
	if (effects & EF_ROCKET) m = cl_hdfx_rocket;
	else if ((effects & EF_BLASTER) && !(effects & EF_TRACKER)) m = cl_hdfx_blaster;
	else if ((effects & EF_HYPERBLASTER) && !(effects & EF_TRACKER)) m = cl_hdfx_hyper;
	else if (effects & EF_BFG) m = cl_hdfx_bfg_ball;
	if (!m)
		return;
	memset (&ent, 0, sizeof(ent));
	ent.model = m;
	ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
	ent.alpha = 0.9f;
	ent.frame = ent.oldframe = (cl.time / 100) & 3;
	VectorCopy (origin, ent.origin);
	VectorCopy (origin, ent.oldorigin);
	V_AddEntity (&ent);
}

static void CL_SimpleImpact (vec3_t pos, vec3_t normal, struct model_s *model)
{
 vec3_t origin, direction;
 VectorCopy(normal,direction);
 VectorNormalize(direction);
 VectorMA(pos,2.0f,direction,origin);
 CL_SimpleExplosion(origin,model,200);
}

static unsigned int CL_ExplosionLightEffects (int type)
{
	switch (type)
	{
	case TE_BLASTER: case TE_BLUEHYPERBLASTER: case TE_BLASTER2: case TE_FLECHETTE: return EF_BLASTER;
	case TE_ROCKET_EXPLOSION: case TE_ROCKET_EXPLOSION_WATER:
	case TE_GRENADE_EXPLOSION: case TE_GRENADE_EXPLOSION_WATER: return EF_ROCKET;
	case TE_BFG_EXPLOSION: return EF_BFG;
	case TE_PLASMA_EXPLOSION: return EF_PLASMA;
	case TE_EXPLOSION1: case TE_EXPLOSION2: case TE_PLAIN_EXPLOSION:
	case TE_EXPLOSION1_BIG: case TE_EXPLOSION1_NP: return CL_GENERIC_WEAPON_LIGHT;
	default: return 0; /* e.g. map welding sparks are not a weapon flash */
	}
}

void CL_SmokeAndFlash(vec3_t origin)
{
	explosion_t	*ex;

	if (cl_nosmoke->value)	// BDGN 3.4: skip the smoke puff + flash entity (Breakfast Pie)
		return;
	ex = CL_AllocExplosion ();
	VectorCopy (origin, ex->ent.origin);
	ex->type = ex_misc;
	ex->frames = 4;
	ex->ent.flags = RF_TRANSLUCENT;
	ex->start = cl.frame.servertime - 100;
	ex->ent.model = cl_mod_smoke;

	ex = CL_AllocExplosion ();
	VectorCopy (origin, ex->ent.origin);
	ex->type = ex_flash;
	ex->ent.flags = RF_FULLBRIGHT;
	ex->frames = 2;
	ex->start = cl.frame.servertime - 100;
	ex->ent.model = cl_mod_flash;
}

/*
=================
CL_ParseParticles
=================
*/
void CL_ParseParticles (void)
{
	int		color, count;
	vec3_t	pos, dir;

	MSG_ReadPos (&net_message, pos);
	MSG_ReadDir (&net_message, dir);

	color = MSG_ReadByte (&net_message);

	count = MSG_ReadByte (&net_message);

	CL_ParticleEffect (pos, dir, color, count);
}

/*
=================
CL_ParseBeam
=================
*/
int CL_ParseBeam (struct model_s *model)
{
	int		ent;
	vec3_t	start, end;
	beam_t	*b;
	int		i;
	
	ent = MSG_ReadShort (&net_message);
	
	MSG_ReadPos (&net_message, start);
	MSG_ReadPos (&net_message, end);

// override any beam with the same entity
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorClear (b->offset);
			return ent;
		}

// find a free beam
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorClear (b->offset);
			return ent;
		}
	}
	Com_Printf ("beam list overflow!\n");	
	return ent;
}

/*
=================
CL_ParseBeam2
=================
*/
int CL_ParseBeam2 (struct model_s *model)
{
	int		ent;
	vec3_t	start, end, offset;
	beam_t	*b;
	int		i;
	
	ent = MSG_ReadShort (&net_message);
	
	MSG_ReadPos (&net_message, start);
	MSG_ReadPos (&net_message, end);
	MSG_ReadPos (&net_message, offset);

//	Com_Printf ("end- %f %f %f\n", end[0], end[1], end[2]);

// override any beam with the same entity

	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorCopy (offset, b->offset);
			return ent;
		}

// find a free beam
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 200;	
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorCopy (offset, b->offset);
			return ent;
		}
	}
	Com_Printf ("beam list overflow!\n");	
	return ent;
}

// ROGUE
/*
=================
CL_ParsePlayerBeam
  - adds to the cl_playerbeam array instead of the cl_beams array
=================
*/
int CL_ParsePlayerBeam (struct model_s *model)
{
	int		ent;
	vec3_t	start, end, offset;
	beam_t	*b;
	int		i;
	
	ent = MSG_ReadShort (&net_message);
	
	MSG_ReadPos (&net_message, start);
	MSG_ReadPos (&net_message, end);
	// PMM - network optimization
	if (model == cl_mod_heatbeam)
		VectorSet(offset, 2, 7, -3);
	else if (model == cl_mod_monster_heatbeam)
	{
		model = cl_mod_heatbeam;
		VectorSet(offset, 0, 0, 0);
	}
	else
		MSG_ReadPos (&net_message, offset);

//	Com_Printf ("end- %f %f %f\n", end[0], end[1], end[2]);

// override any beam with the same entity
// PMM - For player beams, we only want one per player (entity) so..
	for (i=0, b=cl_playerbeams ; i< MAX_BEAMS ; i++, b++)
	{
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorCopy (offset, b->offset);
			return ent;
		}
	}

// find a free beam
	for (i=0, b=cl_playerbeams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = model;
			b->endtime = cl.time + 100;		// PMM - this needs to be 100 to prevent multiple heatbeams
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorCopy (offset, b->offset);
			return ent;
		}
	}
	Com_Printf ("beam list overflow!\n");	
	return ent;
}
//rogue

/*
=================
CL_ParseLightning
=================
*/
int CL_ParseLightning (struct model_s *model)
{
	int		srcEnt, destEnt;
	vec3_t	start, end;
	beam_t	*b;
	int		i;
	
	srcEnt = MSG_ReadShort (&net_message);
	destEnt = MSG_ReadShort (&net_message);

	MSG_ReadPos (&net_message, start);
	MSG_ReadPos (&net_message, end);

// override any beam with the same source AND destination entities
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
		if (b->entity == srcEnt && b->dest_entity == destEnt)
		{
//			Com_Printf("%d: OVERRIDE  %d -> %d\n", cl.time, srcEnt, destEnt);
			b->entity = srcEnt;
			b->dest_entity = destEnt;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorClear (b->offset);
			return srcEnt;
		}

// find a free beam
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
//			Com_Printf("%d: NORMAL  %d -> %d\n", cl.time, srcEnt, destEnt);
			b->entity = srcEnt;
			b->dest_entity = destEnt;
			b->model = model;
			b->endtime = cl.time + 200;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			VectorClear (b->offset);
			return srcEnt;
		}
	}
	Com_Printf ("beam list overflow!\n");	
	return srcEnt;
}

/*
=================
CL_ParseLaser
=================
*/
/* BDGN 3.55s: HD railgun: a bright blue core beam over the spiral (0.6 s) and a flash where it hits */
static void CL_HDFXRail (vec3_t start, vec3_t end)
{
	laser_t	*l;
	int		i, n = 0;
	for (i=0, l=cl_lasers ; i< MAX_LASERS && n < 2 ; i++, l++)
	{
		if (l->endtime >= cl.time)
			continue;
		l->ent.flags = RF_TRANSLUCENT | RF_BEAM;
		VectorCopy (start, l->ent.origin);
		VectorCopy (end, l->ent.oldorigin);
		l->ent.alpha = n ? 0.25f : 0.55f;
		l->ent.skinnum = n ? 0x73 : 0x70;	/* palette blues: wide soft glow, narrow bright core */
		l->ent.model = NULL;
		l->ent.frame = n ? 9 : 3;
		l->endtime = cl.time + (n ? 450 : 600);
		n++;
	}
	if (cl_hdfx_rail_hit)
		CL_HDFXExplosion (end, cl_hdfx_rail_hit, 350, 6, 200, 0.4f, 0.55f, 1.0f);
}

void CL_ParseLaser (int colors)
{
	vec3_t	start;
	vec3_t	end;
	laser_t	*l;
	int		i;

	MSG_ReadPos (&net_message, start);
	MSG_ReadPos (&net_message, end);

	for (i=0, l=cl_lasers ; i< MAX_LASERS ; i++, l++)
	{
		if (l->endtime < cl.time)
		{
			l->ent.flags = RF_TRANSLUCENT | RF_BEAM;
			VectorCopy (start, l->ent.origin);
			VectorCopy (end, l->ent.oldorigin);
			l->ent.alpha = 0.30;
			l->ent.skinnum = (colors >> ((rand() % 4)*8)) & 0xff;
			l->ent.model = NULL;
			l->ent.frame = 4;
			l->endtime = cl.time + 100;
			return;
		}
	}
}

//=============
//ROGUE
void CL_ParseSteam (void)
{
	vec3_t	pos, dir;
	int		id, i;
	int		r;
	int		cnt;
	int		color;
	int		magnitude;
	cl_sustain_t	*s, *free_sustain;

	id = MSG_ReadShort (&net_message);		// an id of -1 is an instant effect
	if (id != -1) // sustains
	{
//			Com_Printf ("Sustain effect id %d\n", id);
		free_sustain = NULL;
		for (i=0, s=cl_sustains; i<MAX_SUSTAINS; i++, s++)
		{
			if (s->id == 0)
			{
				free_sustain = s;
				break;
			}
		}
		if (free_sustain)
		{
			s->id = id;
			s->count = MSG_ReadByte (&net_message);
			MSG_ReadPos (&net_message, s->org);
			MSG_ReadDir (&net_message, s->dir);
			r = MSG_ReadByte (&net_message);
			s->color = r & 0xff;
			s->magnitude = MSG_ReadShort (&net_message);
			s->endtime = cl.time + MSG_ReadLong (&net_message);
			s->think = CL_ParticleSteamEffect2;
			s->thinkinterval = 100;
			s->nextthink = cl.time;
		}
		else
		{
//				Com_Printf ("No free sustains!\n");
			// FIXME - read the stuff anyway
			cnt = MSG_ReadByte (&net_message);
			MSG_ReadPos (&net_message, pos);
			MSG_ReadDir (&net_message, dir);
			r = MSG_ReadByte (&net_message);
			magnitude = MSG_ReadShort (&net_message);
			magnitude = MSG_ReadLong (&net_message); // really interval
		}
	}
	else // instant
	{
		cnt = MSG_ReadByte (&net_message);
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		r = MSG_ReadByte (&net_message);
		magnitude = MSG_ReadShort (&net_message);
		color = r & 0xff;
		CL_ParticleSteamEffect (pos, dir, color, cnt, magnitude);
//		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
	}
}

void CL_ParseWidow (void)
{
	vec3_t	pos;
	int		id, i;
	cl_sustain_t	*s, *free_sustain;

	id = MSG_ReadShort (&net_message);

	free_sustain = NULL;
	for (i=0, s=cl_sustains; i<MAX_SUSTAINS; i++, s++)
	{
		if (s->id == 0)
		{
			free_sustain = s;
			break;
		}
	}
	if (free_sustain)
	{
		s->id = id;
		MSG_ReadPos (&net_message, s->org);
		s->endtime = cl.time + 2100;
		s->think = CL_Widowbeamout;
		s->thinkinterval = 1;
		s->nextthink = cl.time;
	}
	else // no free sustains
	{
		// FIXME - read the stuff anyway
		MSG_ReadPos (&net_message, pos);
	}
}

void CL_ParseNuke (void)
{
	vec3_t	pos;
	int		i;
	cl_sustain_t	*s, *free_sustain;

	free_sustain = NULL;
	for (i=0, s=cl_sustains; i<MAX_SUSTAINS; i++, s++)
	{
		if (s->id == 0)
		{
			free_sustain = s;
			break;
		}
	}
	if (free_sustain)
	{
		s->id = 21000;
		MSG_ReadPos (&net_message, s->org);
		s->endtime = cl.time + 1000;
		s->think = CL_Nukeblast;
		s->thinkinterval = 1;
		s->nextthink = cl.time;
	}
	else // no free sustains
	{
		// FIXME - read the stuff anyway
		MSG_ReadPos (&net_message, pos);
	}
}

//ROGUE
//=============


/*
=================
CL_ParseTEnt
=================
*/
static byte splash_color[] = {0x00, 0xe0, 0xb0, 0x50, 0xd0, 0xe0, 0xe8};

void CL_ParseTEnt (void)
{
	int		type;
	vec3_t	pos, pos2, dir;
	explosion_t	*ex;
	int		cnt;
	int		color;
	int		r;
	int		ent;
	int		magnitude;
	qboolean	sprite_hit;	/* BDGN 3.51ai */

	type = MSG_ReadByte (&net_message);
	VectorClear (pos);	/* BDGN 3.55y: CL_Gore_TEnt reads them after every type */
	VectorClear (pos2);
	VectorClear (dir);

	switch (type)
	{
	case TE_BLOOD:			// bullet hitting flesh
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (CL_Gore_BloodOn ())
			break;	/* BDGN 3.55af: with the 3D blood no red cloud at the hit, only the drops and splats (CL_Gore_TEnt) */
		if (cl_bloodfx->value != 2 && CL_SpriteHit (pos, dir, 0))	// BDGN 3.51ai (test)
			break;
		if (cl_bloodfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_blood);
		else if (cl_bloodfx->value < 2)
			CL_ParticleEffect (pos, dir, 0xe8, (cl_bloodfx->value == 1) ? 15 : 60);
		break;

	case TE_GUNSHOT:			// bullet hitting wall
	case TE_SPARKS:
	case TE_BULLET_SPARKS:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		sprite_hit = type != TE_SPARKS && cl_impactfx->value != 2 && CL_SpriteHit (pos, dir, 1);	// BDGN 3.51ai (test)
		if (sprite_hit) ;
		else if (type != TE_SPARKS && cl_impactfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_grey);
		else if (type == TE_SPARKS || cl_impactfx->value < 2)
		{
			if (type == TE_GUNSHOT)
				CL_ParticleEffect (pos, dir, 0, cl_impactfx->value == 1 ? 10 : 40);
			else
				CL_ParticleEffect (pos, dir, 0xe0, (type != TE_SPARKS && cl_impactfx->value == 1) ? 2 : 6);
		}

		if (type != TE_SPARKS)
		{
			if (!cl_impactfx->value && !sprite_hit)
				CL_SmokeAndFlash(pos);
			
			// impact sound (BDGN 3.4: skipped with cl_nosmoke)
			cnt = rand()&15;
			if (cl_nosmoke->value)
				;
			else if (cnt == 1)
				S_StartSound (pos, 0, 0, cl_sfx_ric1, 1, ATTN_NORM, 0);
			else if (cnt == 2)
				S_StartSound (pos, 0, 0, cl_sfx_ric2, 1, ATTN_NORM, 0);
			else if (cnt == 3)
				S_StartSound (pos, 0, 0, cl_sfx_ric3, 1, ATTN_NORM, 0);
		}

		break;
		
	case TE_SCREEN_SPARKS:
	case TE_SHIELD_SPARKS:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (type == TE_SCREEN_SPARKS)
			CL_ParticleEffect (pos, dir, 0xd0, 40);
		else
			CL_ParticleEffect (pos, dir, 0xb0, 40);
		//FIXME : replace or remove this sound
		S_StartSound (pos, 0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;
		
	case TE_SHOTGUN:			// bullet hitting wall
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (cl_impactfx->value != 2 && CL_SpriteHit (pos, dir, 2))	// BDGN 3.51ai (test)
			break;
		if (cl_impactfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_grey);
		else if (cl_impactfx->value < 2)
			CL_ParticleEffect (pos, dir, 0, cl_impactfx->value == 1 ? 5 : 20);
		if (!cl_impactfx->value)
			CL_SmokeAndFlash(pos);
		break;

	case TE_SPLASH:			// bullet hitting water
		cnt = MSG_ReadByte (&net_message);
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		r = MSG_ReadByte (&net_message);
		if (r > 6)
			color = 0x00;
		else
			color = splash_color[r];
		if (r == SPLASH_BLOOD && cl_bloodfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_blood);
		else if (r != SPLASH_BLOOD || cl_bloodfx->value < 2)
			CL_ParticleEffect (pos, dir, color, (r == SPLASH_BLOOD && cl_bloodfx->value == 1) ? (cnt + 3)/4 : cnt);

		if ((r == SPLASH_BLUE_WATER || r == SPLASH_BROWN_WATER) && CL_WaterInteraction())
			CL_AddWaterRipple(pos);

		if (r == SPLASH_SPARKS)
		{
			r = rand() & 3;
			if (r == 0)
				S_StartSound (pos, 0, 0, cl_sfx_spark5, 1, ATTN_STATIC, 0);
			else if (r == 1)
				S_StartSound (pos, 0, 0, cl_sfx_spark6, 1, ATTN_STATIC, 0);
			else
				S_StartSound (pos, 0, 0, cl_sfx_spark7, 1, ATTN_STATIC, 0);
		}
		break;

	case TE_LASER_SPARKS:
		cnt = MSG_ReadByte (&net_message);
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		color = MSG_ReadByte (&net_message);
		CL_ParticleEffect2 (pos, dir, color, cnt);
		break;

	// RAFAEL
	case TE_BLUEHYPERBLASTER:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadPos (&net_message, dir);
		if (CL_SimpleBlasterHit (pos, dir, false))
			break;
		CL_BlasterParticles (pos, dir);
		CL_FX3D_HitSmoke(pos,dir,180,true);
		break;

	case TE_BLASTER:			// blaster hitting wall
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (CL_SimpleBlasterHit (pos, dir, true))
			break;
		CL_FX3D_HitSmoke(pos,dir,300,false);
		CL_BlasterParticles (pos, dir);
		if (CL_HDFX () && cl_hdfx_blaster_hit)
		{	/* Keep the flash on the same plane/point as the impact mark. */
			ex = CL_SpriteExplosion (pos, cl_hdfx_blaster_hit, 300, 6);
			if (ex) {
				vec3_t normal;
				VectorCopy(dir,normal);
				if (VectorNormalize(normal)<.001f) VectorSet(normal,0,0,1);
				ex->pull=0;
				ex->ent.lightstyle=FX_SPRITE_IMPACT;
				ex->ent.angles[YAW]=(float)(atan2(normal[1],normal[0])*180/M_PI);
				ex->ent.angles[PITCH]=(float)(atan2(-normal[2],sqrt(normal[0]*normal[0]+normal[1]*normal[1]))*180/M_PI);
				VectorMA(pos,.5f,normal,ex->ent.origin);
				VectorCopy(ex->ent.origin,ex->ent.oldorigin);
				ex->light=150; VectorSet(ex->lightcolor,1,.8f,.3f);
			}
			S_StartSound (pos, 0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
			break;
		}

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->ent.angles[0] = acos(dir[2])/M_PI*180;
	// PMM - fixed to correct for pitch of 0
		if (dir[0])
			ex->ent.angles[1] = atan2(dir[1], dir[0])/M_PI*180;
		else if (dir[1] > 0)
			ex->ent.angles[1] = 90;
		else if (dir[1] < 0)
			ex->ent.angles[1] = 270;
		else
			ex->ent.angles[1] = 0;

		ex->type = ex_misc;
		ex->ent.flags = RF_FULLBRIGHT|RF_TRANSLUCENT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 150;
		ex->lightcolor[0] = 1;
		ex->lightcolor[1] = 1;
		ex->ent.model = cl_mod_explode;
		ex->frames = 4;
		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;
		
	case TE_RAILTRAIL:			// railgun effect
		MSG_ReadPos (&net_message, pos);
		MSG_ReadPos (&net_message, pos2);
		if (!CL_Gore_RailOn ())	/* BDGN 3.55y: the 3D railgun replaces both */
		{
			CL_RailTrail (pos, pos2);
			if (CL_HDFX ())
				CL_HDFXRail (pos, pos2);	/* BDGN 3.55s */
		}
		S_StartSound (pos2, 0, 0, cl_sfx_railg, 1, ATTN_NORM, 0);
		break;

	case TE_EXPLOSION2:
	case TE_GRENADE_EXPLOSION:
	case TE_GRENADE_EXPLOSION_WATER:
		MSG_ReadPos (&net_message, pos);
		if (cl_static_rockets->value || (CL_2D_SPRITES() && (cl_fx_grenade_explo || cl_fx_rocket_explo)))
		{
			CL_RocketStyleExplosion (pos, true);
			S_StartSound (pos, 0, 0, type == TE_GRENADE_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_grenexp, 1, ATTN_NORM, 0);
			break;
		}
		if (type != TE_EXPLOSION2 && CL_FX3D_GrenadeExplosion (pos))
		{	/* BDGN 3.55ab: the grenade's own 3D explosion */
			CL_ExplosionParticles (pos);
			S_StartSound (pos, 0, 0, type == TE_GRENADE_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_grenexp, 1, ATTN_NORM, 0);
			break;
		}
		if (type == TE_EXPLOSION2 && CL_FX3D_RocketExplosion (pos))
		{	/* BDGN 3.55aa: TE_EXPLOSION2 is not a grenade but an exploding object (barrels, misc_explobox and other
			   BecomeExplosion2 in the game): the 3D explosion like the rockets, not the grenade sprite */
			CL_ExplosionParticles (pos);
			S_StartSound (pos, 0, 0, cl_sfx_grenexp, 1, ATTN_NORM, 0);
			break;
		}
		if (CL_HDFX ())
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, (cl_hdfx_grenade && type != TE_EXPLOSION2) ? cl_hdfx_grenade : cl_hdfx_explo, 900, 16, 350, 1, 0.55f, 0.25f);	/* 3.55aa: barrels get the big fireball */
			CL_ExplosionParticles (pos);
			CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t: HD smoke (grey set) */
			S_StartSound (pos, 0, 0, type == TE_GRENADE_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_grenexp, 1, ATTN_NORM, 0);
			break;
		}

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_poly;
		ex->ent.flags = RF_FULLBRIGHT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 350;
		ex->lightcolor[0] = 1.0;
		ex->lightcolor[1] = 0.5;
		ex->lightcolor[2] = 0.5;
		ex->ent.model = cl_mod_explo4;
		ex->frames = 19;
		ex->baseframe = 30;
		ex->ent.angles[1] = rand() % 360;
		CL_ExplosionParticles (pos);
		CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t */
		if (type == TE_GRENADE_EXPLOSION_WATER)
			S_StartSound (pos, 0, 0, cl_sfx_watrexp, 1, ATTN_NORM, 0);
		else
			S_StartSound (pos, 0, 0, cl_sfx_grenexp, 1, ATTN_NORM, 0);
		break;

	// RAFAEL
	case TE_PLASMA_EXPLOSION:
		MSG_ReadPos (&net_message, pos);
		if (cl_static_rockets->value || (CL_2D_SPRITES() && cl_fx_rocket_explo)) {
			CL_RocketStyleExplosion(pos,false);
			S_StartSound(pos,0,0,cl_sfx_rockexp,1,ATTN_NORM,0);
			break;
		}
		if (CL_HDFX ())
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, cl_hdfx_explo, 900, 16, 350, 1, 0.55f, 0.25f);
			CL_ExplosionParticles (pos);
			CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t */
			S_StartSound (pos, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
			break;
		}

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_poly;
		ex->ent.flags = RF_FULLBRIGHT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 350;
		ex->lightcolor[0] = 1.0; 
		ex->lightcolor[1] = 0.5;
		ex->lightcolor[2] = 0.5;
		ex->ent.angles[1] = rand() % 360;
		ex->ent.model = cl_mod_explo4;
		if (frand() < 0.5)
			ex->baseframe = 15;
		ex->frames = 15;
		CL_ExplosionParticles (pos);
		S_StartSound (pos, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
		break;
	
	case TE_EXPLOSION1:
	case TE_EXPLOSION1_BIG:						// PMM
	case TE_ROCKET_EXPLOSION:
	case TE_ROCKET_EXPLOSION_WATER:
	case TE_EXPLOSION1_NP:						// PMM
		MSG_ReadPos (&net_message, pos);
		if (cl_static_rockets->value || (CL_2D_SPRITES() && cl_fx_rocket_explo))
		{
			CL_RocketStyleExplosion (pos, false);
			S_StartSound (pos, 0, 0, type == TE_ROCKET_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_rockexp, 1, ATTN_NORM, 0);
			break;
		}
		if (type != TE_EXPLOSION1_BIG && CL_FX3D_RocketExplosion (pos))
		{	/* BDGN 3.55t: 3D rocket explosion (fire smoke set) */
			if (type != TE_EXPLOSION1_NP)
				CL_ExplosionParticles (pos);
			S_StartSound (pos, 0, 0, type == TE_ROCKET_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_rockexp, 1, ATTN_NORM, 0);
			break;
		}
		if (CL_HDFX ())
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, cl_hdfx_explo, 900, 16, 350, 1, 0.55f, 0.25f);
			CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t */
			if ((type != TE_EXPLOSION1_BIG) && (type != TE_EXPLOSION1_NP))
				CL_ExplosionParticles (pos);
			S_StartSound (pos, 0, 0, type == TE_ROCKET_EXPLOSION_WATER ? cl_sfx_watrexp : cl_sfx_rockexp, 1, ATTN_NORM, 0);
			break;
		}

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_poly;
		ex->ent.flags = RF_FULLBRIGHT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 350;
		ex->lightcolor[0] = 1.0;
		ex->lightcolor[1] = 0.5;
		ex->lightcolor[2] = 0.5;
		ex->ent.angles[1] = rand() % 360;
		if (type != TE_EXPLOSION1_BIG)				// PMM
			ex->ent.model = cl_mod_explo4;			// PMM
		else
			ex->ent.model = cl_mod_explo4_big;
		if (frand() < 0.5)
			ex->baseframe = 15;
		ex->frames = 15;
		if ((type != TE_EXPLOSION1_BIG) && (type != TE_EXPLOSION1_NP))		// PMM
			CL_ExplosionParticles (pos);									// PMM
		CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t */
		if (type == TE_ROCKET_EXPLOSION_WATER)
			S_StartSound (pos, 0, 0, cl_sfx_watrexp, 1, ATTN_NORM, 0);
		else
			S_StartSound (pos, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
		break;

	case TE_BFG_EXPLOSION:
		MSG_ReadPos (&net_message, pos);
		if (!cl_static_bfg->value && !CL_2D_SPRITES() && CL_FX3D_BFGExplosion (pos, false))
			break;	/* BDGN 3.55t: 3D BFG hit */
		if (!cl_static_bfg->value && CL_HDFX () && cl_hdfx_bfg_explo)
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, cl_hdfx_bfg_explo, 600, 12, 350, 0, 1, 0.2f);
			CL_FX3D_BFGMist(pos,600,true);
			break;
		}
		if (cl_static_bfg->value || (CL_2D_SPRITES() && cl_fx_bfg_hit))
		{
			if (CL_SPR_BFG() && cl_fx_bfg_hit)
				CL_SpriteExplosion (pos, cl_fx_bfg_hit, 320, 4);
			else
				CL_SimpleExplosion (pos, cl_mod_bfg_oval, 500);
			break;
		}
		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_poly;
		ex->ent.flags = RF_FULLBRIGHT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 350;
		ex->lightcolor[0] = 0.0;
		ex->lightcolor[1] = 1.0;
		ex->lightcolor[2] = 0.0;
		ex->ent.model = cl_mod_bfg_explo;
		ex->ent.flags |= RF_TRANSLUCENT;
		ex->ent.alpha = 0.30;
		ex->frames = 4;
		CL_FX3D_BFGMist(pos,300,true);
		break;

	case TE_BFG_BIGEXPLOSION:
		MSG_ReadPos (&net_message, pos);
		if (!cl_static_bfg->value && !CL_2D_SPRITES() && CL_FX3D_BFGExplosion (pos, true))
		{	/* BDGN 3.55t: 3D BFG explosion with the bright green mist */
			CL_BFGExplosionParticles (pos);
			break;
		}
		if (!cl_static_bfg->value && CL_HDFX () && cl_hdfx_bfg_explo)
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, cl_hdfx_bfg_explo, 800, 12, 500, 0, 1, 0.2f);
			CL_BFGExplosionParticles (pos);
			CL_FX3D_BFGMist (pos, 800, false);	/* BDGN 3.55t: mist with the 0.8 s HD explosion */
			break;
		}
		if (CL_SPR_BFG() && cl_fx_bfg_explo)
			CL_SpriteExplosion (pos, cl_fx_bfg_explo, 240, 3);
		else if (cl_static_bfg->value)
			CL_SimpleExplosion (pos, cl_mod_bfg_core_oval, 500);
		else
		{
			CL_BFGExplosionParticles (pos);
			CL_FX3D_BFGMist (pos, 500, false);	/* BDGN 3.55t: the original explosion lasts 0.5 s */
		}
		break;

	case TE_BFG_LASER:
		CL_ParseLaser (0xd0d1d2d3);
		break;

	case TE_BUBBLETRAIL:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadPos (&net_message, pos2);
		CL_BubbleTrail (pos, pos2);
		break;

	case TE_PARASITE_ATTACK:
	case TE_MEDIC_CABLE_ATTACK:
		ent = CL_ParseBeam (cl_mod_parasite_segment);
		break;

	case TE_BOSSTPORT:			// boss teleporting to station
		MSG_ReadPos (&net_message, pos);
		CL_BigTeleportParticles (pos);
		S_StartSound (pos, 0, 0, S_RegisterSound ("misc/bigtele.wav"), 1, ATTN_NONE, 0);
		break;

	case TE_GRAPPLE_CABLE:
		ent = CL_ParseBeam2 (cl_mod_grapple_cable);
		break;

	// RAFAEL
	case TE_WELDING_SPARKS:
		cnt = MSG_ReadByte (&net_message);
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		color = MSG_ReadByte (&net_message);
		CL_ParticleEffect2 (pos, dir, color, cnt);

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_flash;
		// note to self
		// we need a better no draw flag
		ex->ent.flags = RF_BEAM;
		ex->start = cl.frame.servertime - 0.1;
		ex->light = 100 + (rand()%75);
		ex->lightcolor[0] = 1.0;
		ex->lightcolor[1] = 1.0;
		ex->lightcolor[2] = 0.3;
		ex->ent.model = cl_mod_flash;
		ex->frames = 2;
		break;

	case TE_GREENBLOOD:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (cl_bloodfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_blood);
		else if (cl_bloodfx->value < 2)
			CL_ParticleEffect2 (pos, dir, 0xdf, (cl_bloodfx->value == 1) ? 7 : 30);
		break;

	// RAFAEL
	case TE_TUNNEL_SPARKS:
		cnt = MSG_ReadByte (&net_message);
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		color = MSG_ReadByte (&net_message);
		CL_ParticleEffect3 (pos, dir, color, cnt);
		break;

//=============
//PGM
		// PMM -following code integrated for flechette (different color)
	case TE_BLASTER2:			// green blaster hitting wall
	case TE_FLECHETTE:			// flechette
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (CL_SimpleBlasterHit (pos, dir, true))
			break;
		
		// PMM
		if (type == TE_BLASTER2)
		{
			CL_BlasterParticles2 (pos, dir, 0xd0);
			CL_FX3D_HitSmoke(pos,dir,300,true);
		}
		else
			CL_BlasterParticles2 (pos, dir, 0x6f); // 75

		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->ent.angles[0] = acos(dir[2])/M_PI*180;
	// PMM - fixed to correct for pitch of 0
		if (dir[0])
			ex->ent.angles[1] = atan2(dir[1], dir[0])/M_PI*180;
		else if (dir[1] > 0)
			ex->ent.angles[1] = 90;
		else if (dir[1] < 0)
			ex->ent.angles[1] = 270;
		else
			ex->ent.angles[1] = 0;

		ex->type = ex_misc;
		ex->ent.flags = RF_FULLBRIGHT|RF_TRANSLUCENT;

		// PMM
		if (type == TE_BLASTER2)
			ex->ent.skinnum = 1;
		else // flechette
			ex->ent.skinnum = 2;

		ex->start = cl.frame.servertime - 100;
		ex->light = 150;
		// PMM
		if (type == TE_BLASTER2)
			ex->lightcolor[1] = 1;
		else // flechette
		{
			ex->lightcolor[0] = 0.19;
			ex->lightcolor[1] = 0.41;
			ex->lightcolor[2] = 0.75;
		}
		ex->ent.model = cl_mod_explode;
		ex->frames = 4;
		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;


	case TE_LIGHTNING:
		ent = CL_ParseLightning (cl_mod_lightning);
		S_StartSound (NULL, ent, CHAN_WEAPON, cl_sfx_lightning, 1, ATTN_NORM, 0);
		break;

	case TE_DEBUGTRAIL:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadPos (&net_message, pos2);
		CL_DebugTrail (pos, pos2);
		break;

	case TE_PLAIN_EXPLOSION:
		MSG_ReadPos (&net_message, pos);
		if (cl_static_rockets->value || (CL_2D_SPRITES() && cl_fx_rocket_explo)) {
			CL_RocketStyleExplosion(pos,false);
			S_StartSound(pos,0,0,cl_sfx_rockexp,1,ATTN_NORM,0);
			break;
		}
		if (CL_HDFX ())
		{	/* BDGN 3.55s */
			CL_HDFXExplosion (pos, cl_hdfx_explo, 900, 16, 350, 1, 0.55f, 0.25f);
			CL_ExplosionParticles (pos);
			CL_FX3D_ExplosionSmoke (pos, false);	/* BDGN 3.55t */
			S_StartSound (pos, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
			break;
		}


		ex = CL_AllocExplosion ();
		ex->light_effects = CL_ExplosionLightEffects(type);
		VectorCopy (pos, ex->ent.origin);
		ex->type = ex_poly;
		ex->ent.flags = RF_FULLBRIGHT;
		ex->start = cl.frame.servertime - 100;
		ex->light = 350;
		ex->lightcolor[0] = 1.0;
		ex->lightcolor[1] = 0.5;
		ex->lightcolor[2] = 0.5;
		ex->ent.angles[1] = rand() % 360;
		ex->ent.model = cl_mod_explo4;
		if (frand() < 0.5)
			ex->baseframe = 15;
		ex->frames = 15;
		if (type == TE_ROCKET_EXPLOSION_WATER)
			S_StartSound (pos, 0, 0, cl_sfx_watrexp, 1, ATTN_NORM, 0);
		else
			S_StartSound (pos, 0, 0, cl_sfx_rockexp, 1, ATTN_NORM, 0);
		break;

	case TE_FLASHLIGHT:
		MSG_ReadPos(&net_message, pos);
		ent = MSG_ReadShort(&net_message);
		CL_Flashlight(ent, pos);
		break;

	case TE_FORCEWALL:
		MSG_ReadPos(&net_message, pos);
		MSG_ReadPos(&net_message, pos2);
		color = MSG_ReadByte (&net_message);
		CL_ForceWall(pos, pos2, color);
		break;

	case TE_HEATBEAM:
		ent = CL_ParsePlayerBeam (cl_mod_heatbeam);
		break;

	case TE_MONSTER_HEATBEAM:
		ent = CL_ParsePlayerBeam (cl_mod_monster_heatbeam);
		break;

	case TE_HEATBEAM_SPARKS:
//		cnt = MSG_ReadByte (&net_message);
		cnt = 50;
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
//		r = MSG_ReadByte (&net_message);
//		magnitude = MSG_ReadShort (&net_message);
		r = 8;
		magnitude = 60;
		color = r & 0xff;
		CL_ParticleSteamEffect (pos, dir, color, cnt, magnitude);
		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;
	
	case TE_HEATBEAM_STEAM:
//		cnt = MSG_ReadByte (&net_message);
		cnt = 20;
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
//		r = MSG_ReadByte (&net_message);
//		magnitude = MSG_ReadShort (&net_message);
//		color = r & 0xff;
		color = 0xe0;
		magnitude = 60;
		CL_ParticleSteamEffect (pos, dir, color, cnt, magnitude);
		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;

	case TE_STEAM:
		CL_ParseSteam();
		break;

	case TE_BUBBLETRAIL2:
//		cnt = MSG_ReadByte (&net_message);
		cnt = 8;
		MSG_ReadPos (&net_message, pos);
		MSG_ReadPos (&net_message, pos2);
		CL_BubbleTrail2 (pos, pos2, cnt);
		S_StartSound (pos,  0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;

	case TE_MOREBLOOD:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
		if (CL_Gore_BloodOn ())
			break;	/* BDGN 3.55af */
		if (cl_bloodfx->value != 2 && CL_SpriteHit (pos, dir, 0))	// BDGN 3.51ai (test)
			break;
		if (cl_bloodfx->value == 3) CL_SimpleImpact(pos,dir,cl_mod_hit_blood);
		else if (cl_bloodfx->value < 2)
			CL_ParticleEffect (pos, dir, 0xe8, (cl_bloodfx->value == 1) ? 62 : 250);
		break;

	case TE_CHAINFIST_SMOKE:
		dir[0]=0; dir[1]=0; dir[2]=1;
		MSG_ReadPos(&net_message, pos);
		CL_ParticleSmokeEffect (pos, dir, 0, 20, 20);
		break;

	case TE_ELECTRIC_SPARKS:
		MSG_ReadPos (&net_message, pos);
		MSG_ReadDir (&net_message, dir);
//		CL_ParticleEffect (pos, dir, 109, 40);
		CL_ParticleEffect (pos, dir, 0x75, 40);
		//FIXME : replace or remove this sound
		S_StartSound (pos, 0, 0, cl_sfx_lashit, 1, ATTN_NORM, 0);
		break;

	case TE_TRACKER_EXPLOSION:
		MSG_ReadPos (&net_message, pos);
		CL_ColorFlash (pos, 0, 150, -1, -1, -1);
		CL_ColorExplosionParticles (pos, 0, 1);
//		CL_Tracker_Explode (pos);
		S_StartSound (pos, 0, 0, cl_sfx_disrexp, 1, ATTN_NORM, 0);
		break;

	case TE_TELEPORT_EFFECT:
	case TE_DBALL_GOAL:
		MSG_ReadPos (&net_message, pos);
		CL_TeleportParticles (pos);
		break;

	case TE_WIDOWBEAMOUT:
		CL_ParseWidow ();
		break;

	case TE_NUKEBLAST:
		CL_ParseNuke ();
		break;

	case TE_WIDOWSPLASH:
		MSG_ReadPos (&net_message, pos);
		CL_WidowSplash (pos);
		break;
//PGM
//==============

	default:
		Com_Error (ERR_DROP, "CL_ParseTEnt: bad type");
	}
	CL_Gore_TEnt (type, pos, pos2, dir);	/* BDGN 3.55y: marks, 3D railgun, what made the gibs */
}

/*
=================
CL_AddBeams
=================
*/
void CL_AddBeams (void)
{
	int			i,j;
	beam_t		*b;
	vec3_t		dist, org;
	float		d;
	entity_t	ent;
	float		yaw, pitch;
	float		forward;
	float		len, steps;
	float		model_length;
	
// update beams
	for (i=0, b=cl_beams ; i< MAX_BEAMS ; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
			continue;

		// if coming from the player, update the start position
		if (b->entity == cl.playernum+1)	// entity 0 is the world
		{
			VectorCopy (cl.refdef.vieworg, b->start);
			b->start[2] -= 22;	// adjust for view height
		}
		VectorAdd (b->start, b->offset, org);

	// calculate pitch and yaw
		VectorSubtract (b->end, org, dist);

		if (dist[1] == 0 && dist[0] == 0)
		{
			yaw = 0;
			if (dist[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
	// PMM - fixed to correct for pitch of 0
			if (dist[0])
				yaw = (atan2(dist[1], dist[0]) * 180 / M_PI);
			else if (dist[1] > 0)
				yaw = 90;
			else
				yaw = 270;
			if (yaw < 0)
				yaw += 360;
	
			forward = sqrt (dist[0]*dist[0] + dist[1]*dist[1]);
			pitch = (atan2(dist[2], forward) * -180.0 / M_PI);
			if (pitch < 0)
				pitch += 360.0;
		}

	// add new entities for the beams
		d = VectorNormalizeF(dist);

		memset (&ent, 0, sizeof(ent));
		if (b->model == cl_mod_lightning)
		{
			model_length = 35.0;
			d-= 20.0;  // correction so it doesn't end in middle of tesla
		}
		else
		{
			model_length = 30.0;
		}
		steps = ceil(d/model_length);
		len = (d-model_length)/(steps-1);

		// PMM - special case for lightning model .. if the real length is shorter than the model,
		// flip it around & draw it from the end to the start.  This prevents the model from going
		// through the tesla mine (instead it goes through the target)
		if ((b->model == cl_mod_lightning) && (d <= model_length))
		{
//			Com_Printf ("special case\n");
			VectorCopy (b->end, ent.origin);
			// offset to push beam outside of tesla model (negative because dist is from end to start
			// for this beam)
//			for (j=0 ; j<3 ; j++)
//				ent.origin[j] -= dist[j]*10.0;
			ent.model = b->model;
			ent.flags = RF_FULLBRIGHT;
			ent.angles[0] = pitch;
			ent.angles[1] = yaw;
			ent.angles[2] = rand()%360;
			V_AddEntity (&ent);			
			return;
		}
		while (d > 0)
		{
			VectorCopy (org, ent.origin);
			ent.model = b->model;
			if (b->model == cl_mod_lightning)
			{
				ent.flags = RF_FULLBRIGHT;
				ent.angles[0] = -pitch;
				ent.angles[1] = yaw + 180.0;
				ent.angles[2] = rand()%360;
			}
			else
			{
				ent.angles[0] = pitch;
				ent.angles[1] = yaw;
				ent.angles[2] = rand()%360;
			}
			
//			Com_Printf("B: %d -> %d\n", b->entity, b->dest_entity);
			V_AddEntity (&ent);

			for (j=0 ; j<3 ; j++)
				org[j] += dist[j]*len;
			d -= model_length;
		}
	}
}


/*
//				Com_Printf ("Endpoint:  %f %f %f\n", b->end[0], b->end[1], b->end[2]);
//				Com_Printf ("Pred View Angles:  %f %f %f\n", cl.predicted_angles[0], cl.predicted_angles[1], cl.predicted_angles[2]);
//				Com_Printf ("Act View Angles: %f %f %f\n", cl.refdef.viewangles[0], cl.refdef.viewangles[1], cl.refdef.viewangles[2]);
//				VectorCopy (cl.predicted_origin, b->start);
//				b->start[2] += 22;	// adjust for view height
//				if (fabs(cl.refdef.vieworg[2] - b->start[2]) >= 10) {
//					b->start[2] = cl.refdef.vieworg[2];
//				}

//				Com_Printf ("Time:  %d %d %f\n", cl.time, cls.realtime, cls.frametime);
*/

extern cvar_t *hand;

/*
=================
ROGUE - draw player locked beams
CL_AddPlayerBeams
=================
*/
void CL_AddPlayerBeams (void)
{
	int			i,j;
	beam_t		*b;
	vec3_t		dist, org;
	float		d;
	entity_t	ent;
	float		yaw, pitch;
	float		forward;
	float		len, steps;
	int			framenum;
	float		model_length;
	
	float		hand_multiplier;
	frame_t		*oldframe;
	player_state_t	*ps, *ops;

//PMM
	if (hand)
	{
		if (hand->value == 2)
			hand_multiplier = 0;
		else if (hand->value == 1)
			hand_multiplier = -1;
		else
			hand_multiplier = 1;
	}
	else 
	{
		hand_multiplier = 1;
	}
//PMM

// update beams
	for (i=0, b=cl_playerbeams ; i< MAX_BEAMS ; i++, b++)
	{
		vec3_t		f,r,u;
		if (!b->model || b->endtime < cl.time)
			continue;

		if(cl_mod_heatbeam && (b->model == cl_mod_heatbeam))
		{

			// if coming from the player, update the start position
			if (b->entity == cl.playernum+1)	// entity 0 is the world
			{	
				// set up gun position
				// code straight out of CL_AddViewWeapon
				ps = &cl.frame.playerstate;
				j = (cl.frame.serverframe - 1) & UPDATE_MASK;
				oldframe = &cl.frames[j];
				if (oldframe->serverframe != cl.frame.serverframe-1 || !oldframe->valid)
					oldframe = &cl.frame;		// previous frame was dropped or involid
				ops = &oldframe->playerstate;
				for (j=0 ; j<3 ; j++)
				{
					b->start[j] = cl.refdef.vieworg[j] + ops->gunoffset[j]
						+ cl.lerpfrac * (ps->gunoffset[j] - ops->gunoffset[j]);
				}
				VectorMA (b->start, (hand_multiplier * b->offset[0]), cl.v_right, org);
				VectorMA (     org, b->offset[1], cl.v_forward, org);
				VectorMA (     org, b->offset[2], cl.v_up, org);
				if ((hand) && (hand->value == 2)) {
					VectorMA (org, -1, cl.v_up, org);
				}
				// FIXME - take these out when final
				VectorCopy (cl.v_right, r);
				VectorCopy (cl.v_forward, f);
				VectorCopy (cl.v_up, u);

			}
			else
				VectorCopy (b->start, org);
		}
		else
		{
			// if coming from the player, update the start position
			if (b->entity == cl.playernum+1)	// entity 0 is the world
			{
				VectorCopy (cl.refdef.vieworg, b->start);
				b->start[2] -= 22;	// adjust for view height
			}
			VectorAdd (b->start, b->offset, org);
		}

	// calculate pitch and yaw
		VectorSubtract (b->end, org, dist);

//PMM
		if(cl_mod_heatbeam && (b->model == cl_mod_heatbeam) && (b->entity == cl.playernum+1))
		{
			vec_t len;

			len = VectorLengthF (dist);
			VectorScale (f, len, dist);
			VectorMA (dist, (hand_multiplier * b->offset[0]), r, dist);
			VectorMA (dist, b->offset[1], f, dist);
			VectorMA (dist, b->offset[2], u, dist);
			if ((hand) && (hand->value == 2)) {
				VectorMA (org, -1, cl.v_up, org);
			}
		}
//PMM

		if (dist[1] == 0 && dist[0] == 0)
		{
			yaw = 0;
			if (dist[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
	// PMM - fixed to correct for pitch of 0
			if (dist[0])
				yaw = (atan2(dist[1], dist[0]) * 180 / M_PI);
			else if (dist[1] > 0)
				yaw = 90;
			else
				yaw = 270;
			if (yaw < 0)
				yaw += 360;
	
			forward = sqrt (dist[0]*dist[0] + dist[1]*dist[1]);
			pitch = (atan2(dist[2], forward) * -180.0 / M_PI);
			if (pitch < 0)
				pitch += 360.0;
		}
		
		if (cl_mod_heatbeam && (b->model == cl_mod_heatbeam))
		{
			if (b->entity != cl.playernum+1)
			{
				framenum = 2;
//				Com_Printf ("Third person\n");
				ent.angles[0] = -pitch;
				ent.angles[1] = yaw + 180.0;
				ent.angles[2] = 0;
//				Com_Printf ("%f %f - %f %f %f\n", -pitch, yaw+180.0, b->offset[0], b->offset[1], b->offset[2]);
				AngleVectors(ent.angles, f, r, u);
					
				// if it's a non-origin offset, it's a player, so use the hardcoded player offset
				if (!VectorCompare (b->offset, vec3_origin))
				{
					VectorMA (org, -(b->offset[0])+1, r, org);
					VectorMA (org, -(b->offset[1]), f, org);
					VectorMA (org, -(b->offset[2])-10, u, org);
				}
				else
				{
					// if it's a monster, do the particle effect
					CL_MonsterPlasma_Shell(b->start);
				}
			}
			else
			{
				framenum = 1;
			}
		}

		// if it's the heatbeam, draw the particle effect
		if ((cl_mod_heatbeam && (b->model == cl_mod_heatbeam) && (b->entity == cl.playernum+1)))
		{
			CL_Heatbeam (org, dist);
		}

	// add new entities for the beams
		d = VectorNormalizeF(dist);

		memset (&ent, 0, sizeof(ent));
		if (b->model == cl_mod_heatbeam)
		{
			model_length = 32.0;
		}
		else if (b->model == cl_mod_lightning)
		{
			model_length = 35.0;
			d-= 20.0;  // correction so it doesn't end in middle of tesla
		}
		else
		{
			model_length = 30.0;
		}
		steps = ceil(d/model_length);
		len = (d-model_length)/(steps-1);

		// PMM - special case for lightning model .. if the real length is shorter than the model,
		// flip it around & draw it from the end to the start.  This prevents the model from going
		// through the tesla mine (instead it goes through the target)
		if ((b->model == cl_mod_lightning) && (d <= model_length))
		{
//			Com_Printf ("special case\n");
			VectorCopy (b->end, ent.origin);
			// offset to push beam outside of tesla model (negative because dist is from end to start
			// for this beam)
//			for (j=0 ; j<3 ; j++)
//				ent.origin[j] -= dist[j]*10.0;
			ent.model = b->model;
			ent.flags = RF_FULLBRIGHT;
			ent.angles[0] = pitch;
			ent.angles[1] = yaw;
			ent.angles[2] = rand()%360;
			V_AddEntity (&ent);			
			return;
		}
		while (d > 0)
		{
			VectorCopy (org, ent.origin);
			ent.model = b->model;
			if(cl_mod_heatbeam && (b->model == cl_mod_heatbeam))
			{
//				ent.flags = RF_FULLBRIGHT|RF_TRANSLUCENT;
//				ent.alpha = 0.3;
				ent.flags = RF_FULLBRIGHT;
				ent.angles[0] = -pitch;
				ent.angles[1] = yaw + 180.0;
				ent.angles[2] = (cl.time) % 360;
//				ent.angles[2] = rand()%360;
				ent.frame = framenum;
			}
			else if (b->model == cl_mod_lightning)
			{
				ent.flags = RF_FULLBRIGHT;
				ent.angles[0] = -pitch;
				ent.angles[1] = yaw + 180.0;
				ent.angles[2] = rand()%360;
			}
			else
			{
				ent.angles[0] = pitch;
				ent.angles[1] = yaw;
				ent.angles[2] = rand()%360;
			}
			
//			Com_Printf("B: %d -> %d\n", b->entity, b->dest_entity);
			V_AddEntity (&ent);

			for (j=0 ; j<3 ; j++)
				org[j] += dist[j]*len;
			d -= model_length;
		}
	}
}

/*
=================
CL_AddExplosions
=================
*/
void CL_AddExplosions (void)
{
	entity_t	*ent;
	int			i;
	explosion_t	*ex;
	float		frac;
	int			f;

	memset (&ent, 0, sizeof(ent));

	for (i=0, ex=cl_explosions ; i< MAX_EXPLOSIONS ; i++, ex++)
	{
		if (ex->type == ex_free)
			continue;
		if (ex->type == ex_simple)
		{
			if (cl.time - ex->start >= ex->duration)
				ex->type = ex_free;
			else
			{
				if (ex->pull > 0)
				{	// BDGN 3.51ac: towards the viewer, never more than half the distance; 3.55s: also single-frame sprites
					vec3_t to;
					float dist, p;
					VectorSubtract (cl.refdef.vieworg, ex->base, to);
					dist = VectorNormalize (to);
					p = ex->pull < dist * 0.5f ? ex->pull : dist * 0.5f;
					VectorMA (ex->base, p, to, ex->ent.origin);
					VectorCopy (ex->ent.origin, ex->ent.oldorigin);
				}
				if (ex->light && CL_WeaponLightEnabled (EF_ROCKET))	/* BDGN 3.55s: HD effects carry their own light */
				{
					float lt = (cl.time - ex->start) / (float)ex->duration;
					V_AddLight (ex->base, ex->light * (1.0f - lt), ex->lightcolor[0], ex->lightcolor[1], ex->lightcolor[2]);
				}
				if (ex->frames > 1)
				{	// BDGN 3.51v: animated sprite explosion
					float t = (cl.time - ex->start) / (float)ex->duration;
					int fr = (int)(t * ex->frames);
					if (CL_LOWEND_SPRITES()) fr &= ~1;
					ex->ent.frame = ex->ent.oldframe = fr >= ex->frames ? ex->frames - 1 : fr;
					ex->ent.alpha = t < 0.5f ? 0.90f : 0.90f - (t - 0.5f) * 1.2f;
				}
				if (!CL_ObjectTooFar (ex->ent.origin))	/* BDGN 3.57c: object draw distance (smoke, hit sprites) */
					V_AddEntity (&ex->ent);
			}
			continue;
		}
		frac = (cl.time - ex->start)/100.0;
		f = floor(frac);

		ent = &ex->ent;

		switch (ex->type)
		{
		case ex_mflash:
			if (f >= ex->frames-1)
				ex->type = ex_free;
			break;
		case ex_misc:
			if (f >= ex->frames-1)
			{
				ex->type = ex_free;
				break;
			}
			ent->alpha = 1.0 - frac/(ex->frames-1);
			break;
		case ex_flash:
			if (f >= 1)
			{
				ex->type = ex_free;
				break;
			}
			ent->alpha = 1.0;
			break;
		case ex_poly:
			if (f >= ex->frames-1)
			{
				ex->type = ex_free;
				break;
			}

			ent->alpha = (16.0 - (float)f)/16.0;

			if (f < 10)
			{
				ent->skinnum = (f>>1);
				if (ent->skinnum < 0)
					ent->skinnum = 0;
			}
			else
			{
				ent->flags |= RF_TRANSLUCENT;
				if (f < 13)
					ent->skinnum = 5;
				else
					ent->skinnum = 6;
			}
			break;
		case ex_poly2:
			if (f >= ex->frames-1)
			{
				ex->type = ex_free;
				break;
			}

			ent->alpha = (5.0 - (float)f)/5.0;
			ent->skinnum = 0;
			ent->flags |= RF_TRANSLUCENT;
			break;
		}

		if (ex->type == ex_free)
			continue;
		if (ex->type == ex_misc || ex->type == ex_flash || ex->type == ex_poly || ex->type == ex_poly2)
		{	/* BDGN 3.55s: impact flashes and 3D explosions sit on the wall point and were cut by the wall; drawn
			   towards the viewer (blaster / hit flash 10, explosions 24 units, never more than half the distance) */
			vec3_t to;
			float dist, p, pull = (ex->type == ex_misc || ex->type == ex_flash) ? 10.0f : 24.0f;
			if (!ex->based)
			{
				VectorCopy (ent->origin, ex->base);
				ex->based = 1;
			}
			VectorSubtract (cl.refdef.vieworg, ex->base, to);
			dist = VectorNormalize (to);
			p = pull < dist * 0.5f ? pull : dist * 0.5f;
			VectorMA (ex->base, p, to, ent->origin);
		}
		if (ex->light && CL_WeaponLightEnabled(ex->light_effects))
		{
			V_AddLight ((ex->pull>0 || ex->based)?ex->base:ent->origin, ex->light*ent->alpha,
				ex->lightcolor[0], ex->lightcolor[1], ex->lightcolor[2]);
		}

		VectorCopy (ent->origin, ent->oldorigin);

		if (f < 0)
			f = 0;
		ent->frame = ex->baseframe + f + 1;
		ent->oldframe = ex->baseframe + f;
		ent->backlerp = 1.0 - cl.lerpfrac;

		if (ex->type == ex_misc && CL_ObjectTooFar (ent->origin))
			continue;	/* BDGN 3.57c: object draw distance: small impact / smoke sprites (3D explosions stay) */
		V_AddEntity (ent);
	}
}


/*
=================
CL_AddLasers
=================
*/
void CL_AddLasers (void)
{
	laser_t		*l;
	railbeam_t	*rail;
	int			i;

	for (i=0, l=cl_lasers ; i< MAX_LASERS ; i++, l++)
	{
		if (l->endtime >= cl.time)
			V_AddEntity (&l->ent);
	}
	for (i=0, rail=cl_railbeams; i<MAX_LASERS; i++, rail++)
	{
		if (rail->endtime > cl.time)
		{
			rail->ent.alpha = rail->alpha * (rail->endtime - cl.time) / rail->duration;
			V_AddEntity (&rail->ent);
		}
	}
}

/* PMM - CL_Sustains */
void CL_ProcessSustain ()
{
	cl_sustain_t	*s;
	int				i;

	for (i=0, s=cl_sustains; i< MAX_SUSTAINS; i++, s++)
	{
		if (s->id)
			if ((s->endtime >= cl.time) && (cl.time >= s->nextthink))
			{
//				Com_Printf ("think %d %d %d\n", cl.time, s->nextthink, s->thinkinterval);
				s->think (s);
			}
			else if (s->endtime < cl.time)
				s->id = 0;
	}
}

/*
=================
CL_AddTEnts
=================
*/
void CL_AddTEnts (void)
{
	CL_AddBeams ();
	// PMM - draw plasma beams
	CL_AddPlayerBeams ();
	CL_AddExplosions ();
	CL_AddLasers ();
	CL_AddWaterRipples ();
	CL_FX3D_AddEntities ();	/* BDGN 3.55t: 3D explosions, smoke, muzzle flashes, heat distortion */
	CL_Gore_AddEntities ();	/* BDGN 3.55y: marks, 3D railgun, HD gibs and blood */
	// PMM - set up sustain
	CL_ProcessSustain();
}
