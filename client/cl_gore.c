/*
cl_gore.c -- BDGN 3.55y: world marks (decals), the 3D railgun and HD gibs / blood (pak8-hdgore.pak, models/hdgore/)

Three toggles on the effects page under HITS AND EXPLOSIONS, each shown only when the driver and the pack allow it:

  cl_decals   marks on the world like Quake III: bullet and shotgun holes, scorch after rocket, grenade, plasma and
              BFG explosions, blaster and railgun burns (with a short glow). Needs the BDGN OpenGL renderer on a
              driver with polygon offset (bdgn_gl_decal_capable: every OpenGL driver, not legacy computers).
              cl_decals_max (16-128, default 128) and cl_decals_time (seconds, default 45) limit them.
  cl_3drail   HD railgun: a white-hot core, a wide blue glow and two energy spirals that wind around the shot,
              the air along the shot shimmers (heat distortion, GLSL drivers), a blue flash where it hits, lights.
              Replaces the original / HD rail trail. Modern drivers only (bdgn_gl_modernfx_capable).
  cl_hdgibs   HD gibs and blood: the game's gibs are drawn with detailed models (bones, ribcage, spine, vertebrae,
              skull, arms, legs, flesh chunks and strips), and more pieces fly with client physics. What they look
              like and how they fly follows what made them: explosions (rocket, grenade, BFG, plasma) tear the body
              into many charred pieces thrown outwards with a cloud of blood, the railgun cuts cleanly along the
              shot, bullets and shotgun tear out flesh with sprays of blood, blasters burn.
              3.55z blood: no mist. Blood is flying drops (as Brutal Doom / ZDoom blood actors) that come out on the
              far side of a hit body, fly on in the projectile's direction, fall and leave a splat, smear or spatter
              where they land; splats are also traced onto the wall behind (as ZDoom's TraceBleed). Explosions throw
              the blood of each hurt body away from the explosion. Gibs bleed as they fly and leave pools that spread
              under them. Blood has its own pool (cl_blood_max 384, cl_blood_time 180 s, cl_blood_amount 0.25-3)
              so it never pushes out the marks. Modern drivers only.

Everything here is visual and client side: the server's gibs keep their own physics (the server decides where they
lie), so playing on any server is unchanged. The renderer flags RF_DECAL, RF_FXBEAM, RF_PUFFS, RF_DISTORT are sent
to the BDGN ref_gl only (vid_ref gl).
*/
#include "client.h"

cvar_t *cl_decals, *cl_3drail, *cl_hdgibs;
/* BDGN 3.56r: colour schemes of the 3D railgun, menu row "3D railgun colours" (cl_3drail_colors). Scheme 0 is the
   rail of 3.56h-q (models/hdgore/rail.md2: red core, blue / orange / green spirals); schemes 1-9 are
   models/hdgore/rail_s<N>.md2 with the same five skins (0 core, 1 glow, 2-4 spirals), so the drawing is the same.
   The light along the shot and at the hit takes the colour of the core (scheme 0 keeps its blue light). A missing model gives scheme 0. */
static cvar_t *cl_3drail_colors;
#define RAIL_SCHEMES 10
static struct model_s *rail_scheme_model[RAIL_SCHEMES];
static const float rail_scheme_light[RAIL_SCHEMES][3] = {
	{ 0.35f, 0.55f, 1.00f },	/* 0 red / blue orange green: the light of 3.56q, unchanged */
	{ 1.00f, 0.20f, 0.10f },	/* 1 red / yellow */
	{ 0.20f, 1.00f, 0.25f },	/* 2 green / red */
	{ 0.25f, 0.45f, 1.00f },	/* 3 blue / yellow */
	{ 1.00f, 0.90f, 0.20f },	/* 4 yellow / purple */
	{ 0.70f, 0.25f, 1.00f },	/* 5 purple / green */
	{ 0.20f, 0.95f, 1.00f },	/* 6 cyan / magenta */
	{ 0.85f, 0.90f, 1.00f },	/* 7 white / blue */
	{ 1.00f, 0.55f, 0.10f },	/* 8 orange / cyan */
	{ 0.95f, 0.95f, 0.95f }		/* 9 white / rainbow */
};
static int Gore_RailScheme (void)
{
	int n = cl_3drail_colors ? (int)cl_3drail_colors->value : 0;
	if (n < 1 || n >= RAIL_SCHEMES || !rail_scheme_model[n])
		return 0;
	return n;
}
static cvar_t *cl_explosion_debris;
static cvar_t *cl_decals_max, *cl_decals_time, *cl_blood_max, *cl_blood_time, *cl_blood_amount, *gore_bloodfx;

enum { G_DECALS, G_RAIL, G_RAILFLASH, G_RAILSMOKE, G_BFGBALL, G_BFGTAIL,
	G_BONE1, G_BONE2, G_RIBCAGE, G_SPINE, G_VERTEBRA, G_SKULL, G_ARM, G_LEG, G_FLESH1, G_FLESH2, G_FLESH3, G_STRIP1, G_STRIP2,
	G_GUT1, G_GUT2, G_HEART, G_LIVER, G_LUNG, G_STOMACH, G_BRAIN, G_HAND, G_FOOT, G_JAW, G_PELVIS, G_SHARD1, G_SHARD2, G_RIBS, G_SKINFLAP,	/* 3.55ah */
	G_COUNT };
static const char *gore_names[G_COUNT] = { "decals", "rail", "rail_flash", "rail_smoke", "bfg_ball", "bfg_ball_tail",
	"gib_bone1", "gib_bone2", "gib_ribcage", "gib_spine", "gib_vertebra", "gib_skull", "gib_arm", "gib_leg",
	"gib_flesh1", "gib_flesh2", "gib_flesh3", "gib_strip1", "gib_strip2",
	"gib_gut1", "gib_gut2", "gib_heart", "gib_liver", "gib_lung", "gib_stomach", "gib_brain", "gib_hand", "gib_foot", "gib_jaw",
	"gib_pelvis", "gib_shard1", "gib_shard2", "gib_ribs", "gib_skinflap" };
static const int gore_frames[G_COUNT] = { 1, 1, 10, 24, 8, 16, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static struct model_s *gore_model[G_COUNT];
static int gore_loaded;
#define MAX_ACTOR_MARKS 4
static struct image_s *actor_mark_skin;
static struct model_s *gravel_model;
typedef struct { vec3_t local; int packed, tile; } actor_mark_t;
static actor_mark_t actor_marks[MAX_EDICTS][MAX_ACTOR_MARKS];
static unsigned char actor_mark_slot[MAX_EDICTS];
#define G_GRAVEL G_COUNT

/* the game's gib models */
enum { OG_MEAT, OG_BONE, OG_BONE2, OG_CHEST, OG_SKULL, OG_HEAD2, OG_ARM, OG_LEG, OG_COUNT };
static const char *og_names[OG_COUNT] = { "sm_meat", "bone", "bone2", "chest", "skull", "head2", "arm", "leg" };
static struct model_s *og_model[OG_COUNT];

/* decal atlas tiles (gore_gen.py) */
#define T_HOLE		0	/* 0..3 */
#define T_SCORCH	4	/* 4, 5 */
#define T_BLASTER	6
#define T_RAIL		7
#define T_BLOOD		8	/* 8..11 */
#define T_DROPS		12
#define T_BFG		13
#define T_GLOW_BLASTER	14
#define T_GLOW_RAIL	15
/* 3.55z: second atlas (decals_blood.tga), skinnum 16 + tile */
#define B_SPLAT		16	/* 16..19 big splats with streaks */
#define B_STREAK	20	/* 20..23 smears along +s (turned along the travel) */
#define B_POOL		24	/* 24..27 pools */
#define B_SPATTER	28	/* 28..31 fine spatter */
#define IS_BLOOD(t)	(((t) >= T_BLOOD && (t) <= T_DROPS) || (t) >= 16)

/* ----------------------------------------------------------------------------------------- availability */

/* 3.55ab: the renderer / driver answers are read once per frame (these tests run for every hit, gib, drop and
   mark: before, each one looked up three cvars by name and compared the renderer's name) */
static int gore_caps_frame = -1, gore_cap_gl, gore_cap_decal, gore_cap_modern, gore_cap_distort;
static void Gore_Caps (void)
{
	if (gore_caps_frame == cls.framecount)
		return;
	gore_caps_frame = cls.framecount;
	gore_cap_gl = !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
	gore_cap_decal = Cvar_VariableValue ("bdgn_gl_decal_capable") != 0;
	gore_cap_modern = Cvar_VariableValue ("bdgn_gl_modernfx_capable") != 0;
	gore_cap_distort = Cvar_VariableValue ("bdgn_gl_distort_capable") != 0;
}
static qboolean Gore_GL (void)
{
	Gore_Caps ();
	return gore_loaded && gore_cap_gl && !cl_classic2d->value && !CL_LOWEND_SPRITES ();
}
static qboolean Gore_DecalsOn (void)
{
	return cl_decals && cl_decals->value && Gore_GL () && gore_cap_decal;
}
static qboolean Gore_Modern (void)
{
	return Gore_GL () && gore_cap_modern;
}
qboolean CL_Gore_RailOn (void)
{
	return cl_3drail && cl_3drail->value && Gore_Modern ();
}
static qboolean Gore_GibsOn (void)
{
	return cl_hdgibs && cl_hdgibs->value && Gore_Modern ();
}
qboolean CL_Gore_BloodOn (void)
{	/* 3.55af: the 3D blood replaces the game's red particle cloud at a hit */
	return Gore_GibsOn () && gore_bloodfx->value != 2;
}
static qboolean Gore_BloodDecals (void)
{	/* blood splats belong to the HD blood; they still need the decal support of the driver */
	return Gore_GibsOn () && gore_cap_decal && gore_bloodfx->value != 2;
}

/* menu, benchmark and presets: -1 = not one of these cvars, else 1 when it can be chosen on this machine */
int CL_Gore_CvarAvailable (const char *name)
{
	qboolean installed = gore_loaded || FS_LoadFile ("models/hdgore/gib_skull.md2", NULL) > 0;
	qboolean gl = installed && !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
	if (!strcmp (name, "cl_decals"))
		return gl && Cvar_VariableValue ("bdgn_gl_decal_capable") != 0;
	if (!strcmp (name, "cl_3drail") || !strcmp (name, "cl_hdgibs"))
		return gl && Cvar_VariableValue ("bdgn_gl_modernfx_capable") != 0;
	return -1;
}

/* ----------------------------------------------------------------------------------------- effect level */

/* BDGN 3.55ab: one place that says what the two HD levels are (menu row "effect level" and every preset use it).
     0 original   id's effects
     1 HD         the light middle way, any OpenGL driver of BDGN's ref_gl, not legacy computers:
                  HD sprite effects (pak8-hdfx.pak) and bullet holes / marks
     2 HD 3D      level 1 plus the 3D animations, modern drivers only: 3D rocket / grenade / barrel explosions,
                  3D BFG ball and explosion, comet blaster bolts, 3D railgun, 3D gibs and blood, 3D smoke, 3D muzzle
                  flashes. Where a 3D effect exists it replaces the HD sprite; the sprites cover the rest.
   Heat distortion and the AI textures are extras on top of level 2 (kept when on, never forced on). Each effect is
   only turned on when this computer can show it, so level 2 on an old driver gives level 1. */
static const char *fxlevel_cvars[] = { "cl_hdfx", "cl_decals",	/* level 1 */
	"cl_3dexplo", "cl_3dbfg", "cl_3dblaster", "cl_3drail", "cl_hdgibs", "cl_hdsmoke", "cl_hdmuzzle", NULL };	/* level 2 */
#define FXLEVEL_FIRST3D 2

static int FXLevel_Available (const char *name)
{
	if (!strcmp (name, "cl_hdfx"))
		return Cvar_VariableValue ("bdgn_hdfx_available") != 0 && !Cvar_VariableValue ("bdgn_legacy_graphics") &&
			!Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
	return CL_FX3D_CvarAvailable (name) == 1;
}

/* BDGN 3.56r: two more levels, kept after the old numbers so saved configs keep their meaning:
     3 HD 4K      level 1 with the HD 4K packs: HD sprite effects and bullet holes / marks at 4x (pak8-hdfx-4k.pak,
                  pak8-hdgore-4k.pak, DDS, needs S3TC)
     4 HD 3D 4K   level 2 with the 4K packs, the 3D effect textures included (AI 4K when the AI textures are
                  installed, else standard 4K)
   A 4K row falls back to HD where its pack is missing. 5 = custom (read only). */
static qboolean FX4K_Pack (const char *pak)
{
	return FS_EffectPackInstalled (pak) && Cvar_VariableValue ("bdgn_gl_dds_capable") != 0 &&
		!Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
}

/* the row values a 4K choice may use on this computer (menu, presets) */
int CL_FX4K_Allowed (const char *name, int value)
{
	if (value < 2)
		return 1;
	if (!strcmp (name, "cl_hdfx")) return value == 2 && FX4K_Pack ("pak8-hdfx-4k.pak");
	if (!strcmp (name, "cl_decals")) return value == 2 && FX4K_Pack ("pak8-hdgore-4k.pak");
	if (!strcmp (name, "cl_fx3d_tex"))
	{
		if (value == 2) return FX4K_Pack ("pak8-hdfx3d-4k.pak");
		if (value == 3) return FX4K_Pack ("pak8-hdfx3d-sd-4k.pak") && Cvar_VariableValue ("bdgn_hdfx3d_sd_available") != 0;
	}
	return 0;
}

/* what cvar "name" is at this level on this computer (0, 1, 2 ...), or -1 when the level does not decide it */
int CL_FXLevel_Value (int level, const char *name)
{
	int i, base = level == 3 ? 1 : (level == 4 ? 2 : level);
	qboolean k4 = level == 3 || level == 4;
	for (i = 0; fxlevel_cvars[i]; i++)
		if (!strcmp (name, fxlevel_cvars[i]))
		{
			if (!(base >= (i < FXLEVEL_FIRST3D ? 1 : 2) && FXLevel_Available (name)))
				return 0;
			if (k4 && i < FXLEVEL_FIRST3D && CL_FX4K_Allowed (name, 2))
				return 2;
			return 1;
		}
	if (!strcmp (name, "cl_heatdistort"))
		return base >= 2 ? -1 : 0;
	if (!strcmp (name, "cl_fx3d_tex"))
	{
		int now = (int)Cvar_VariableValue ("cl_fx3d_tex");
		if (base < 2)
			return 0;
		if (level == 4)
		{
			if (CL_FX4K_Allowed (name, 3) && (now == 1 || now == 3 || !CL_FX4K_Allowed (name, 2)))
				return 3;
			if (CL_FX4K_Allowed (name, 2))
				return 2;
			return -1;
		}
		return now >= 2 ? now - 2 : -1;	/* HD 3D: the 4K textures off, the AI choice kept */
	}
	return -1;
}

/* the highest of the old levels (0..2) this computer can show */
int CL_FXLevel_Max (void)
{
	int i, max = 0;
	for (i = 0; fxlevel_cvars[i]; i++)
		if (FXLevel_Available (fxlevel_cvars[i]))
			max = i < FXLEVEL_FIRST3D ? (max > 1 ? max : 1) : 2;
	return max;
}

/* level 0..4 can be chosen on this computer */
int CL_FXLevel_Allowed (int level)
{
	int max = CL_FXLevel_Max ();
	qboolean any4k = CL_FX4K_Allowed ("cl_hdfx", 2) || CL_FX4K_Allowed ("cl_decals", 2);
	switch (level)
	{
	case 0: return 1;
	case 1: return max >= 1;
	case 2: return max >= 2;
	case 3: return max >= 1 && any4k;
	case 4: return max >= 2 && (any4k || CL_FX4K_Allowed ("cl_fx3d_tex", 2) || CL_FX4K_Allowed ("cl_fx3d_tex", 3));
	}
	return 0;
}

void CL_FXLevel_Apply (int level)
{
	static const char *extra[] = { "cl_heatdistort", "cl_fx3d_tex", NULL };
	int i, v;
	for (i = 0; fxlevel_cvars[i]; i++)
		Cvar_SetValue ((char *)fxlevel_cvars[i], (float)CL_FXLevel_Value (level, fxlevel_cvars[i]));
	for (i = 0; extra[i]; i++)
		if ((v = CL_FXLevel_Value (level, extra[i])) >= 0)
			Cvar_SetValue ((char *)extra[i], (float)v);
}

/* 0..4 when the settings are exactly a level, 5 = custom (rows changed one by one) */
int CL_FXLevel_Current (void)
{
	static const int order[] = { 4, 2, 3, 1, 0 };
	int k, i, level, v;
	for (k = 0; k < 5; k++)
	{
		level = order[k];
		if (!CL_FXLevel_Allowed (level))
			continue;
		for (i = 0; fxlevel_cvars[i]; i++)
			if ((int)Cvar_VariableValue ((char *)fxlevel_cvars[i]) != CL_FXLevel_Value (level, fxlevel_cvars[i]))
				break;
		if (fxlevel_cvars[i])
			continue;
		v = CL_FXLevel_Value (level, "cl_fx3d_tex");
		if (level == 4 && v >= 2 && (int)Cvar_VariableValue ("cl_fx3d_tex") < 2)
			continue;	/* 4K effect textures are part of HD 3D 4K */
		if (level == 2 && (int)Cvar_VariableValue ("cl_fx3d_tex") >= 2)
			continue;
		return level;
	}
	return 5;
}

/* ----------------------------------------------------------------------------------------- pools */

typedef struct
{
	int		id, start, life, fade, grow;
	vec3_t	origin, angles;
	int		size, tile, glow;
	float	alpha;
} decal_t;
#define MAX_MARKS 128	/* 3.55z: decals[0..127] marks, decals[128..511] blood (the blood never pushes out marks) */
#define MAX_DECALS 512
static decal_t decals[MAX_DECALS];
static int decal_nextid = 1;

typedef struct
{
	int		start;
	vec3_t	start_pos, end_pos, normal;
	qboolean hit;
} rail3d_t;
#define MAX_RAILS 8
static rail3d_t rails[MAX_RAILS];

typedef struct
{
	int		model;		/* G_*, 0 = free */
	int		skin, start, life, landed, bleed, lastsplat;
	vec3_t	origin, velocity, angles, avelocity;
	float	radius;
} debris_t;
#define MAX_DEBRIS 128	/* 3.55ah: was 48 */
static debris_t debris[MAX_DEBRIS];

/* 3.55z: flying blood drops (as in Brutal Doom / ZDoom's blood: drops fly, fall and leave a splat where they land) */
typedef struct
{
	vec3_t	org, vel;
	int		start;
	float	size;	/* 0.5 .. 2 */
} drop_t;
#define MAX_DROPS 768
static drop_t drops[MAX_DROPS];
static int drop_next;

/* the flying BFG ball */
static int bfg_birth[MAX_EDICTS], bfg_seen[MAX_EDICTS];

typedef struct { int model, start, frames; float fps; vec3_t origin, angles; } goreanim_t;
#define MAX_GOREANIM 24
static goreanim_t goreanim[MAX_GOREANIM];

/* what made the damage: recent hits (temp entities) */
enum { C_NONE, C_EXPLODE, C_BFG, C_RAIL, C_BULLET, C_BLASTER };
typedef struct { int cause, time; vec3_t pos, pos2; } goreevent_t;
#define MAX_EVENTS 32
static goreevent_t events[MAX_EVENTS];
static int event_next;

/* server gibs: first sight (extra pieces once per body), cause, landing splat */
typedef struct { int lastseen, cause, falling, splatted; vec3_t lastorg; } gibstate_t;
static gibstate_t gibstate[MAX_EDICTS];
typedef struct { int time; vec3_t pos; } burst_t;
static burst_t bursts[8];

int CL_Gore_DebrisCount(void)
{
	float count=cl_explosion_debris->value;
	if (!(count>=0)) return 0;
	if (count>EXPLOSION_DEBRIS_MAX) return EXPLOSION_DEBRIS_MAX;
	return (int)count;
}

void CL_Gore_Init (void)
{
	cl_explosion_debris=Cvar_Get("cl_explosion_debris",EXPLOSION_DEBRIS_DEFAULT,CVAR_ARCHIVE);
	cl_decals = Cvar_Get ("cl_decals", "0", CVAR_ARCHIVE);
	cl_3drail = Cvar_Get ("cl_3drail", "0", CVAR_ARCHIVE);
	cl_3drail_colors = Cvar_Get ("cl_3drail_colors", "0", CVAR_ARCHIVE);	/* BDGN 3.56r: 0..9 */
	cl_hdgibs = Cvar_Get ("cl_hdgibs", "0", CVAR_ARCHIVE);
	gore_bloodfx = Cvar_Get ("cl_bloodfx", "0", 0);
	cl_decals_max = Cvar_Get ("cl_decals_max", "128", CVAR_ARCHIVE);	/* 3.55z: marks only (16..128) */
	cl_decals_time = Cvar_Get ("cl_decals_time", "45", CVAR_ARCHIVE);
	cl_blood_max = Cvar_Get ("cl_blood_max", "384", CVAR_ARCHIVE);	/* 3.55z: blood splats (32..384) */
	cl_blood_time = Cvar_Get ("cl_blood_time", "180", CVAR_ARCHIVE);	/* seconds the blood stays */
	cl_blood_amount = Cvar_Get ("cl_blood_amount", "1", CVAR_ARCHIVE);	/* drops: 0.25 .. 3 */
}

void CL_Gore_Clear (void)
{
	memset(actor_marks,0,sizeof(actor_marks));
	memset(actor_mark_slot,0,sizeof(actor_mark_slot));
	memset (decals, 0, sizeof(decals));
	memset (rails, 0, sizeof(rails));
	memset (debris, 0, sizeof(debris));
	memset (goreanim, 0, sizeof(goreanim));
	memset (events, 0, sizeof(events));
	memset (gibstate, 0, sizeof(gibstate));
	memset (bursts, 0, sizeof(bursts));
	memset (drops, 0, sizeof(drops));
}

/* BDGN 3.59o: the HD gore models are loaded only where they can be shown (GL renderer, classic 2D off); 2.2 s of a
   map load on a dual Pentium II 300 with 3dfx MiniGL went here although classic 2D left them unused (v3.59n,
   MEASURED). The original gibs are always loaded. cl_tent.c loads the HD set when classic 2D is turned off. */
void CL_Gore_RegisterModels (void)
{
	char name[MAX_QPATH];
	int i;
	CL_Gore_Clear ();
	gore_loaded = 0;
	actor_mark_skin=NULL;
	gravel_model=NULL;
	memset (gore_model, 0, sizeof(gore_model));
	memset (rail_scheme_model, 0, sizeof(rail_scheme_model));
	for (i = 0; i < OG_COUNT; i++)
	{
		Com_sprintf (name, sizeof(name), "models/objects/gibs/%s/tris.md2", og_names[i]);
		og_model[i] = re.RegisterModel (name);
	}
	if (CL_HDAssetsUnused ())
	{
		for (i = 0; i < G_COUNT; i++)
		{
			Com_sprintf (name, sizeof(name), "models/hdgore/%s.md2", gore_names[i]);
			if (FS_LoadFile (name, NULL) <= 0)
				break;
		}
		Cvar_FullSet ("bdgn_hdgore_available", i == G_COUNT ? "1" : "0", 0);
		return;
	}
	CL_Gore_RegisterHD ();
}

void CL_Gore_RegisterHD (void)
{
	char name[MAX_QPATH];
	int i;
	gore_loaded = 0;
	for (i = 0; i < G_COUNT; i++)
	{
		Com_sprintf (name, sizeof(name), "models/hdgore/%s.md2", gore_names[i]);
		gore_model[i] = FS_LoadFile (name, NULL) > 0 ? re.RegisterModel (name) : NULL;	/* not installed: no messages */
		if (!gore_model[i])
			break;
	}
	gore_loaded = i == G_COUNT;
	actor_mark_skin=NULL;
	gravel_model=NULL;
	memset (rail_scheme_model, 0, sizeof(rail_scheme_model));
	if (gore_loaded)
	{
		for (i = 1; i < RAIL_SCHEMES; i++)	/* BDGN 3.56r */
		{
			Com_sprintf (name, sizeof(name), "models/hdgore/rail_s%d.md2", i);
			if (FS_LoadFile (name, NULL) > 0)
				rail_scheme_model[i] = re.RegisterModel (name);
		}
		if (FS_LoadFile("models/hdgore/actor_marks_lit.tga",NULL)>0)
			actor_mark_skin=re.RegisterSkin("models/hdgore/actor_marks_lit.tga");
		if (FS_LoadFile("models/hdgore/gravel.md2",NULL)>0)
			gravel_model=re.RegisterModel("models/hdgore/gravel.md2");
	}
	Cvar_FullSet ("bdgn_hdgore_available", gore_loaded ? "1" : "0", 0);
}

/* ----------------------------------------------------------------------------------------- helpers */

static void Gore_AnglesFromDir (vec3_t dir, vec3_t angles)
{	/* as the game's vectoangles, random roll */
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

static trace_t Gore_Trace (vec3_t a, vec3_t b)
{
	return CM_BoxTrace (a, b, vec3_origin, vec3_origin, 0, MASK_SOLID);
}

/* a mark at pos on the surface with this normal; travel (may be NULL) turns streaks along the drop's way */
static void Gore_DecalEx (vec3_t pos, vec3_t normal, float size, int tile, int glow, int life, float alpha, vec3_t travel, int grow)
{
	int i, first, last, oldest, max, now = cl.time;
	decal_t *d;
	if (IS_BLOOD (tile))
	{
		max = (int)cl_blood_max->value;
		if (max < 32) max = 32;
		if (max > MAX_DECALS - MAX_MARKS) max = MAX_DECALS - MAX_MARKS;
		first = MAX_MARKS;
	}
	else
	{
		max = (int)cl_decals_max->value;
		if (max < 16) max = 16;
		if (max > MAX_MARKS) max = MAX_MARKS;
		first = 0;
	}
	last = first + max;
	oldest = first;
	for (i = first; i < last; i++)
	{
		if (!decals[i].id || now - decals[i].start >= decals[i].life)
		{
			oldest = i;
			break;
		}
		if (decals[i].start < decals[oldest].start)
			oldest = i;
	}
	d = &decals[oldest];
	memset (d, 0, sizeof(*d));
	/* 3.55ab: id = generation * 1024 + pool slot + 1. The renderer caches the cut mark in slot (id & 1023), so two
	   living marks never share a cache slot (with a running number they could, and were then cut again every frame) */
	decal_nextid = (decal_nextid + 1) & 0xfffff;
	d->id = (decal_nextid << 10) | (oldest + 1);
	d->start = now;
	d->life = life;
	d->fade = glow ? life : (life > 4000 ? 3000 : life / 2);
	if (IS_BLOOD (tile))
	{	/* 3.55ae: blood is painted red (renderer mode 2), only from the blood atlas */
		glow = 2;
		if (tile < 16) tile = tile == T_DROPS ? B_SPATTER + (rand () & 3) : B_SPLAT + (rand () & 3);
	}
	d->grow = grow;
	VectorMA (pos, 0.5f, normal, d->origin);
	Gore_AnglesFromDir (normal, d->angles);
	if (travel)
	{	/* the atlas streaks run along +s = the decal's right vector: pick the roll that turns it along the travel */
		float best = -2, r;
		vec3_t f, rt, up;
		for (r = 0; r < 360; r += 10)
		{
			float dot;
			d->angles[ROLL] = r;
			AngleVectors (d->angles, f, rt, up);
			dot = DotProduct (rt, travel);
			if (dot > best) { best = dot; i = (int)r; }
		}
		d->angles[ROLL] = (float)i;
	}
	d->size = (int)(size + 0.5f);
	if (d->size < 1) d->size = 1;
	d->tile = tile;
	d->glow = glow;
	d->alpha = alpha;
}
static int Gore_BloodLife (void);
/* 3.55ah: blood on a wall runs down: a smear under the splat, thick end down, that grows for a few seconds */
static void Gore_WallDrip (vec3_t pos, vec3_t normal, float size)
{
	vec3_t up = { 0, 0, 1 }, p;
	float len;
	if (normal[2] > 0.5f || normal[2] < -0.5f || (rand () & 1))
		return;	/* floors and ceilings: no run; half of the wall splats */
	len = size * (0.7f + 0.9f * frand ());
	VectorCopy (pos, p);
	p[2] -= len * 0.55f;
	Gore_DecalEx (p, normal, len, B_STREAK + (rand () & 3), 0, Gore_BloodLife (), 0.85f, up, 2500 + (rand () % 2500));
}

static void Gore_Decal (vec3_t pos, vec3_t normal, float size, int tile, int glow, int life, float alpha)
{
	Gore_DecalEx (pos, normal, size, tile, glow, life, alpha, NULL, 0);
}
static int Gore_BloodLife (void)
{
	int s = (int)cl_blood_time->value;
	if (s < 10) s = 10;
	if (s > 1800) s = 1800;
	return s * 1000;
}
static int Gore_DecalLife (void)
{
	int s = (int)cl_decals_time->value;
	if (s < 5) s = 5;
	if (s > 600) s = 600;
	return s * 1000;
}

/* the nearest wall or floor within reach: hit point and normal */
static qboolean Gore_NearestSurface (vec3_t pos, float reach, vec3_t hitpos, vec3_t normal)
{
	static float dirs[6][3] = { {0,0,-1}, {0,0,1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
	trace_t tr;
	vec3_t end;
	float best = 2.0f;
	int i;
	for (i = 0; i < 6; i++)
	{
		VectorMA (pos, reach, dirs[i], end);
		tr = Gore_Trace (pos, end);
		if (tr.fraction < best && !tr.allsolid && !tr.startsolid && !(tr.surface && (tr.surface->flags & SURF_SKY)))
		{
			best = tr.fraction;
			VectorCopy (tr.endpos, hitpos);
			VectorCopy (tr.plane.normal, normal);
		}
	}
	return best < 1.0f;
}

/* blood thrown from pos along dir: a splat where it hits a wall / floor within reach */
static void Gore_BloodSplat (vec3_t pos, vec3_t dir, float reach, float size)
{
	trace_t tr;
	vec3_t end;
	VectorMA (pos, reach, dir, end);
	end[2] -= reach * 0.25f;	/* blood falls */
	tr = Gore_Trace (pos, end);
	if (tr.fraction >= 1.0f || tr.allsolid || tr.startsolid || (tr.surface && (tr.surface->flags & SURF_SKY)))
		return;
	Gore_DecalEx (tr.endpos, tr.plane.normal, size * (0.8f + 0.5f * frand ()), B_SPLAT + (rand () & 3), 0, Gore_BloodLife (), 0.85f + 0.15f * frand (), NULL, 0);
	Gore_WallDrip (tr.endpos, tr.plane.normal, size);
}

static void Gore_Anim (int model, vec3_t org, vec3_t dir, float fps)
{
	int i, oldest = 0;
	for (i = 0; i < MAX_GOREANIM; i++)
	{
		if (!goreanim[i].model || cl.time - goreanim[i].start > (int)(goreanim[i].frames * 1000 / goreanim[i].fps))
		{
			oldest = i;
			break;
		}
		if (goreanim[i].start < goreanim[oldest].start)
			oldest = i;
	}
	goreanim[oldest].model = model;
	goreanim[oldest].start = cl.time;
	goreanim[oldest].frames = gore_frames[model];
	goreanim[oldest].fps = fps;
	VectorCopy (org, goreanim[oldest].origin);
	Gore_AnglesFromDir (dir, goreanim[oldest].angles);
}

static void Gore_Event (int cause, vec3_t pos, vec3_t pos2)
{
	goreevent_t *e = &events[event_next++ & (MAX_EVENTS - 1)];
	e->cause = cause;
	e->time = cl.time;
	VectorCopy (pos, e->pos);
	if (pos2) VectorCopy (pos2, e->pos2);
	else VectorCopy (pos, e->pos2);
}

static float Distance (vec3_t a, vec3_t b)
{
	vec3_t d;
	VectorSubtract (a, b, d);
	return VectorLength (d);
}

/* distance from p to the segment a-b */
static float Gore_SegDist (vec3_t p, vec3_t a, vec3_t b)
{
	vec3_t ab, ap, c;
	float t, l = 0;
	VectorSubtract (b, a, ab);
	VectorSubtract (p, a, ap);
	l = DotProduct (ab, ab);
	t = l > 0.01f ? DotProduct (ap, ab) / l : 0;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	VectorMA (a, t, ab, c);
	VectorSubtract (p, c, c);
	return VectorLength (c);
}

/* what made a gib at pos (the newest fitting hit of the last 0.6 s) */
static int Gore_CauseAt (vec3_t pos, vec3_t from)
{
	int i, best = C_NONE, besttime = -100000;
	for (i = 0; i < MAX_EVENTS; i++)
	{
		goreevent_t *e = &events[i];
		float reach, d;
		if (!e->cause || cl.time - e->time > 600 || e->time < besttime)
			continue;
		reach = e->cause == C_EXPLODE ? 220 : e->cause == C_BFG ? 400 : e->cause == C_RAIL ? 48 : 96;
		d = e->cause == C_RAIL ? Gore_SegDist (pos, e->pos, e->pos2) : Distance (pos, e->pos);
		if (d > reach)
			continue;
		best = e->cause;
		besttime = e->time;
		if (from)
			VectorCopy (e->pos, from);
	}
	return best;
}

/* ----------------------------------------------------------------------------------------- blood drops */

/* n drops from pos, thrown along dir (normalised) inside a cone, speed range, extra upward speed */
static void Gore_Drops (vec3_t pos, vec3_t dir, int n, float speed0, float speed1, float cone, float up, float size)
{
	int i;
	float amount = cl_blood_amount->value;
	if (amount < 0.25f) amount = 0.25f;
	if (amount > 3) amount = 3;
	n = (int)(n * amount + 0.5f);
	for (i = 0; i < n; i++)
	{
		drop_t *d = &drops[drop_next++ % MAX_DROPS];
		vec3_t r;
		float sp = speed0 + (speed1 - speed0) * frand ();
		VectorSet (r, crand (), crand (), crand ());
		VectorMA (dir, cone, r, d->vel);
		VectorNormalize (d->vel);
		VectorScale (d->vel, sp, d->vel);
		d->vel[2] += up * (0.4f + 0.8f * frand ());
		VectorMA (pos, 2, r, d->org);
		VectorMA (d->org, 0.03f, d->vel, d->org);	/* 3.56: already on its way in the first picture */
		d->start = cl.time;
		d->size = size * (0.5f + frand ());
	}
}

static void Gore_RunDrops (void)
{
	static int last;
	int i, splats = 0;
	float dt = (cl.time - last) * 0.001f;	/* 3.55ab: the game clock (was the frame time: moved while paused) */
	qboolean decals_ok = Gore_BloodDecals ();
	last = cl.time;
	if (dt > 0.05f) dt = 0.05f;
	if (dt < 0) dt = 0;
	for (i = 0; i < MAX_DROPS; i++)
	{
		drop_t *d = &drops[i];
		vec3_t end, tail;
		trace_t tr;
		if (!d->start)
			continue;
		if (cl.time - d->start > 3000 || cl.time < d->start)
		{
			d->start = 0;
			continue;
		}
		if (dt > 0)
		{
			d->vel[2] -= 800 * dt;
			VectorMA (d->org, dt, d->vel, end);
			tr = Gore_Trace (d->org, end);
			if (tr.startsolid || tr.allsolid)
			{
				d->start = 0;
				continue;
			}
			if (tr.fraction < 1.0f)
			{	/* lands: a splat, a smear when it slides along a wall or comes in flat and fast */
				if (decals_ok && !(tr.surface && (tr.surface->flags & (SURF_SKY | SURF_WARP))) && splats < 24)
				{
					float speed = VectorLength (d->vel), into = -DotProduct (d->vel, tr.plane.normal) / (speed + 0.01f);
					vec3_t trav;
					splats++;
					VectorMA (d->vel, -DotProduct (d->vel, tr.plane.normal), tr.plane.normal, trav);
					VectorNormalize (trav);
					if (into < 0.75f && speed > 160)
						Gore_DecalEx (tr.endpos, tr.plane.normal, (5 + 6 * d->size) * (speed > 320 ? 1.4f : 1.0f), B_STREAK + (rand () & 3), 0, Gore_BloodLife (), 0.9f, trav, 0);
					else if (d->size > 1.2f)
					{
							Gore_DecalEx (tr.endpos, tr.plane.normal, 4 + 5 * d->size, B_SPLAT + (rand () & 3), 0, Gore_BloodLife (), 0.9f, NULL, 0);
						Gore_WallDrip (tr.endpos, tr.plane.normal, 4 + 5 * d->size);
					}
					else
						Gore_DecalEx (tr.endpos, tr.plane.normal, 4 + 4 * d->size, B_SPATTER + (rand () & 3), 0, Gore_BloodLife (), 0.85f, NULL, 0);
				}
				d->start = 0;
				continue;
			}
			VectorCopy (end, d->org);
		}
		/* a drop is a short dark red streak: head and two along its way back */
		V_AddParticle (d->org, 0xe8 + (i & 3), 1.0f);
		VectorMA (d->org, -0.012f, d->vel, tail);
		V_AddParticle (tail, 0xe9 + (i & 3), 0.85f);
		if (d->size > 1.0f)
		{
			VectorMA (d->org, -0.024f, d->vel, tail);
			V_AddParticle (tail, 0xea + (i & 3), 0.6f);
		}
	}
}

/* explosions hurt with the explosion's position as the hit point and no direction: find the body that was hit
   (the nearest solid entity near it that was not yet used this frame) */
static qboolean Gore_VictimNear (vec3_t pos, vec3_t victim)
{
	static int used_frame, used[16], nused;
	int i, best = -1;
	float bestd = 260;
	if (used_frame != cl.frame.serverframe)
	{
		used_frame = cl.frame.serverframe;
		nused = 0;
	}
	for (i = 0; i < cl.frame.num_entities; i++)
	{
		entity_state_t *st = &cl_parse_entities[(cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1)];
		vec3_t d;
		float dist;
		int k, skip = 0;
		if (!st->solid || st->solid == 31 || !st->modelindex || st->number == cl.playernum + 1)	/* 31 = brush models; not ourselves */
			continue;
		for (k = 0; k < nused; k++)
			if (used[k] == st->number) skip = 1;
		if (skip)
			continue;
		VectorSubtract (st->origin, pos, d);
		dist = VectorLength (d);
		if (dist < bestd)
		{
			bestd = dist;
			best = i;
		}
	}
	if (best < 0)
		return false;
	{
		entity_state_t *st = &cl_parse_entities[(cl.frame.parse_entities + best) & (MAX_PARSE_ENTITIES - 1)];
		if (nused < 16) used[nused++] = st->number;
		VectorCopy (st->origin, victim);
		victim[2] += 8;
	}
	return true;
}

/* blood thrown out behind a hit body along the projectile's way: drops that fly and land, splats on the wall
   behind (traced, ZDoom-style with some spread), a burst of red particles at the exit (no mist) */
static void Gore_ExitBlood (vec3_t pos, vec3_t way, int drops_n, float speed, float splat)
{
	int k;
	vec3_t d, out, behind;
	trace_t tr;
	VectorMA (pos, 12, way, behind);	/* the hit point is on the near side: the blood comes out behind it (3.56: 12 units, was 20 -
										   it stayed hidden behind the body for the first moment) */
	tr = Gore_Trace (pos, behind);
	if (tr.startsolid) VectorCopy (pos, out);
	else VectorCopy (tr.endpos, out);
	/* 3.56: faster out of the body (the blood seemed to come a moment after the hit): 0.7-1.6 x speed instead of
	   0.45-1.0, the heavy drops 0.35-0.8 instead of 0.2-0.5 */
	Gore_Drops (out, way, drops_n, speed * 0.7f, speed * 1.6f, 0.32f, 70, 1.0f);
	Gore_Drops (out, way, drops_n / 3 + 1, speed * 0.35f, speed * 0.8f, 0.45f, 120, 1.6f);	/* heavier, fall closer */
	/* 3.56a: nothing comes back towards the shooter (the burst out of the entry wound of 3.56 is gone, the heavy drops
	   spread less: 0.45 instead of 0.7) - the bullet takes the blood with it, out of the far side */
	if (Gore_BloodDecals ())
		for (k = 0; k < 2; k++)
		{
			VectorSet (d, way[0] + crand () * 0.25f, way[1] + crand () * 0.25f, way[2] + crand () * 0.2f);
			VectorNormalize (d);
			Gore_BloodSplat (out, d, 150, splat);
		}
	/* 3.55af: no particle burst here (it read as a red cloud) */
}

/* ----------------------------------------------------------------------------------------- gibs */

static int Gore_HDModelFor (struct model_s *m, int number)
{
	int h = (number * 2654435761u) >> 7;
	if (!m) return -1;
	if (m == og_model[OG_MEAT])
	{	/* 3.55ah: the game's meat chunks become flesh, guts and organs */
		static const int v[12] = { G_FLESH1, G_FLESH2, G_FLESH3, G_STRIP1, G_GUT1, G_GUT2, G_LIVER, G_STOMACH, G_HEART, G_LUNG, G_SKINFLAP, G_STRIP2 };
		return v[h % 12];
	}
	if (m == og_model[OG_BONE]) { static const int v[4] = { G_BONE1, G_BONE2, G_RIBS, G_PELVIS }; return v[h & 3]; }
	if (m == og_model[OG_BONE2]) return (h & 1) ? G_SPINE : G_VERTEBRA;
	if (m == og_model[OG_CHEST]) return G_RIBCAGE;
	if (m == og_model[OG_SKULL] || m == og_model[OG_HEAD2]) return G_SKULL;
	if (m == og_model[OG_ARM]) return G_ARM;
	if (m == og_model[OG_LEG]) return G_LEG;
	return -1;
}

static debris_t *Gore_Debris (int model, vec3_t org, vec3_t vel, int charred, int bleed)
{
	int i, oldest = 0;
	debris_t *d;
	for (i = 0; i < MAX_DEBRIS; i++)
	{
		if (!debris[i].model)
		{
			oldest = i;
			break;
		}
		if (debris[i].start < debris[oldest].start)
			oldest = i;
	}
	d = &debris[oldest];
	memset (d, 0, sizeof(*d));
	d->model = model;
	d->skin = charred;
	d->start = cl.time;
	d->life = 9000 + (rand () % 4000);
	d->bleed = bleed;
	VectorCopy (org, d->origin);
	VectorCopy (vel, d->velocity);
	VectorSet (d->angles, frand () * 360, frand () * 360, frand () * 360);
	VectorSet (d->avelocity, crand () * 600, crand () * 600, crand () * 300);
	d->radius = (model == G_RIBCAGE || model == G_LEG || model == G_PELVIS || model == G_GUT2 || model == G_LIVER) ? 4 : (model == G_SHARD1 || model == G_SHARD2 ? 1 : 2);
	return d;
}

static void Gore_Gravel(vec3_t pos)
{
	vec3_t origin, vel;
	int i, count;
	if (!gravel_model || !Gore_Modern() || !cl_3dexplo || !cl_3dexplo->value ||
	    cl_gibmode->value>=2)
		return;
	count=CL_Gore_DebrisCount();
	if (cl_gibmode->value) count=(count+1)/2;
	VectorCopy(pos,origin);
	origin[2]+=5;
	for(i=0;i<count;i++)
	{
		debris_t *d;
		VectorSet(vel,crand()*185,crand()*185,75+frand()*140);
		d=Gore_Debris(G_GRAVEL,origin,vel,0,0);
		d->life=5500;
	}
}

/* extra pieces for one body, by cause; dir = away from what hit it */
static void Gore_BodyBurst (vec3_t pos, int cause, vec3_t from)
{
	/* 3.55ah: whole insides. An explosion empties the body: guts, organs, hands and feet, jaw, pelvis, ribs, splinters */
	static const int explode[] = { G_BONE1, G_BONE2, G_VERTEBRA, G_SPINE, G_FLESH1, G_FLESH2, G_FLESH3, G_STRIP1, G_STRIP2,
		G_GUT1, G_GUT2, G_GUT1, G_HEART, G_LIVER, G_LUNG, G_LUNG, G_STOMACH, G_BRAIN, G_HAND, G_HAND, G_FOOT, G_FOOT, G_JAW, G_PELVIS,
		G_RIBS, G_RIBS, G_SHARD1, G_SHARD2, G_SHARD1, G_SHARD2, G_SKINFLAP, G_SKINFLAP };
	static const int rail[] = { G_STRIP1, G_STRIP2, G_VERTEBRA, G_FLESH1, G_GUT1, G_SHARD1, G_LIVER, G_RIBS };
	static const int bullet[] = { G_FLESH1, G_FLESH3, G_STRIP2, G_BONE2, G_FLESH2, G_SHARD1, G_SHARD2, G_SKINFLAP, G_GUT1, G_GUT2, G_STOMACH };
	static const int blaster[] = { G_FLESH2, G_FLESH3, G_STRIP1, G_SKINFLAP, G_SHARD2 };
	static const int generic[] = { G_FLESH1, G_STRIP2, G_FLESH3, G_GUT1, G_SHARD1 };
	const int *list;
	int n, i, charred_pct;
	float speed, up;
	vec3_t dir, vel, p;
	VectorSubtract (pos, from, dir);
	if (VectorNormalize (dir) < 1)
		VectorSet (dir, 0, 0, 1);
	switch (cause)
	{
	case C_EXPLODE: case C_BFG: list = explode; n = sizeof(explode) / sizeof(int); speed = 380; up = 220; charred_pct = cause == C_BFG ? 30 : 60; break;
	case C_RAIL: list = rail; n = sizeof(rail) / sizeof(int); speed = 200; up = 120; charred_pct = 0; break;
	case C_BULLET: list = bullet; n = sizeof(bullet) / sizeof(int); speed = 220; up = 180; charred_pct = 0; break;
	case C_BLASTER: list = blaster; n = sizeof(blaster) / sizeof(int); speed = 180; up = 160; charred_pct = 80; break;
	default: list = generic; n = sizeof(generic) / sizeof(int); speed = 160; up = 200; charred_pct = 0; break;
	}
	for (i = 0; i < n; i++)
	{
		float s = speed * (0.4f + 0.7f * frand ());
		vec3_t r;
		VectorSet (r, crand (), crand (), crand ());
		if (cause == C_RAIL)	/* clean cut: pieces leave along the shot */
			VectorMA (dir, 0.35f, r, vel);
		else if (cause == C_EXPLODE || cause == C_BFG)
			VectorMA (dir, 0.9f, r, vel);
		else
			VectorMA (dir, 0.6f, r, vel);
		VectorNormalize (vel);
		VectorScale (vel, s, vel);
		vel[2] += up * (0.5f + frand ());
		VectorMA (pos, 4, r, p);
		Gore_Debris (list[i], p, vel, (rand () % 100) < charred_pct, cause != C_BLASTER);
	}
	/* blood: thrown away from what hit the body, as drops that land, and splats on the walls around */
	if (cause == C_EXPLODE || cause == C_BFG)
	{
		vec3_t updir = { 0, 0, 1 };
		Gore_Drops (pos, dir, 70, 150, 520, 0.9f, 160, 1.3f);
		Gore_Drops (pos, updir, 30, 100, 300, 1.2f, 80, 1.6f);
	}
	else if (cause != C_BLASTER)
		Gore_Drops (pos, dir, 40, 120, 380, 0.5f, 120, 1.3f);
	if (Gore_BloodDecals () && cause != C_BLASTER)
	{
		int k, count = (cause == C_EXPLODE || cause == C_BFG) ? 10 : 5;
		for (k = 0; k < count; k++)
		{
			vec3_t d;
			VectorSet (d, crand (), crand (), crand () * 0.6f - 0.4f);
			if (cause != C_EXPLODE && cause != C_BFG)
				VectorMA (d, 1.5f, dir, d);
			VectorNormalize (d);
			Gore_BloodSplat (pos, d, cause == C_EXPLODE ? 180.0f : 120.0f, cause == C_EXPLODE ? 22.0f : 15.0f);
		}
	}
}

/* from cl_ents.c, for every packet entity before it is added: HD gib model, first-sight pieces, landing splat.
   Returns true when the entity was changed. */
qboolean CL_Gore_GibEntity (entity_t *ent, int number)
{
	gibstate_t *g;
	int hd;
	if (!Gore_GibsOn () || number < 0 || number >= MAX_EDICTS)
		return false;
	hd = Gore_HDModelFor (ent->model, number);
	if (hd < 0)
		return false;
	g = &gibstate[number];
	if (cl.time - g->lastseen > 1000 || g->lastseen > cl.time)
	{	/* a new gib */
		vec3_t from;
		int i, close_by = 0;
		memset (g, 0, sizeof(*g));
		VectorCopy (ent->origin, from);
		from[2] -= 16;
		g->cause = Gore_CauseAt (ent->origin, from);
		for (i = 0; i < 8; i++)	/* one burst of extra pieces per body */
			if (cl.time - bursts[i].time < 300 && Distance (bursts[i].pos, ent->origin) < 96)
				close_by = 1;
		if (!close_by)
		{
			int slot = 0;
			for (i = 1; i < 8; i++)
				if (bursts[i].time < bursts[slot].time) slot = i;
			bursts[slot].time = cl.time;
			VectorCopy (ent->origin, bursts[slot].pos);
			Gore_BodyBurst (ent->origin, g->cause, from);
		}
		VectorCopy (ent->origin, g->lastorg);
	}
	/* landing: was falling, now lies still */
	if (ent->origin[2] < g->lastorg[2] - 0.5f)
		g->falling = 1;
	else if (g->falling && fabs (ent->origin[2] - g->lastorg[2]) < 0.05f && !g->splatted)
	{
		g->splatted = 1;
		if (Gore_BloodDecals () && g->cause != C_BLASTER)
		{
			vec3_t down = { 0, 0, -1 }, end;
			trace_t tr;
			VectorMA (ent->origin, 32, down, end);
			tr = Gore_Trace (ent->origin, end);
			if (tr.fraction < 1.0f && !tr.startsolid)	/* a pool that spreads out under it over 4 s */
				Gore_DecalEx (tr.endpos, tr.plane.normal, (hd == G_RIBCAGE || hd == G_LEG || hd == G_SKULL ? 22.0f : 13.0f) * (0.8f + 0.4f * frand ()),
					B_POOL + (rand () & 3), 0, Gore_BloodLife (), 0.9f, NULL, 4000);
		}
	}
	VectorCopy (ent->origin, g->lastorg);
	g->lastseen = cl.time;
	ent->model = gore_model[hd];
	ent->frame = ent->oldframe = 0;
	ent->skin = NULL;
	ent->skinnum = (g->cause == C_EXPLODE || g->cause == C_BLASTER || (g->cause == C_BFG && (number & 1))) ? 1 : 0;
	return true;
}

static void Gore_RunDebris (void)
{
	static int last;
	int i;
	float dt = (cl.time - last) * 0.001f;
	last = cl.time;
	if (dt > 0.1f) dt = 0.1f;
	if (dt < 0) dt = 0;
	for (i = 0; i < MAX_DEBRIS; i++)
	{
		debris_t *d = &debris[i];
		entity_t e;
		int age;
		if (!d->model)
			continue;
		age = cl.time - d->start;
		if (age >= d->life || age < 0)
		{
			d->model = 0;
			continue;
		}
		if (!d->landed && dt > 0)
		{
			vec3_t end;
			trace_t tr;
			d->velocity[2] -= 800 * dt;
			VectorMA (d->origin, dt, d->velocity, end);
			tr = Gore_Trace (d->origin, end);
			if (tr.startsolid || tr.allsolid)
				d->model = 0;
			else if (tr.fraction < 1.0f)
			{
				float speed = VectorLength (d->velocity), into = DotProduct (d->velocity, tr.plane.normal);
				VectorCopy (tr.endpos, d->origin);
				VectorMA (d->origin, 0.5f, tr.plane.normal, d->origin);
				VectorMA (d->velocity, -1.6f * into, tr.plane.normal, d->velocity);	/* bounce, wet and heavy */
				VectorScale (d->velocity, 0.45f, d->velocity);
				VectorScale (d->avelocity, 0.6f, d->avelocity);
				if (d->bleed && speed > 140 && cl.time - d->lastsplat > 150 && Gore_BloodDecals ())
				{
					vec3_t trav;
					d->lastsplat = cl.time;
					VectorCopy (d->velocity, trav);
					VectorNormalize (trav);
					Gore_DecalEx (tr.endpos, tr.plane.normal, (d->radius * 3 + 5) * (0.8f + 0.4f * frand ()), (speed > 300 ? B_STREAK + (rand () & 3) : B_SPLAT + (rand () & 3)), 0, Gore_BloodLife (), 0.9f, trav, 0);
				}
				if (tr.plane.normal[2] > 0.7f && VectorLength (d->velocity) < 40)
				{
					d->landed = 1;
					if (d->model==G_GRAVEL) d->life=age+2800;
					if (d->bleed && Gore_BloodDecals ())
						Gore_DecalEx (tr.endpos, tr.plane.normal, (d->radius * 3 + 6) * (0.8f + 0.4f * frand ()), B_POOL + (rand () & 3), 0, Gore_BloodLife (), 0.9f, NULL, 3000);
					d->angles[PITCH] = (float)((rand () & 1) * 180);
					d->angles[ROLL] = 0;
				}
			}
			else
				VectorCopy (end, d->origin);
			if (d->model && !d->landed)
				VectorMA (d->angles, dt, d->avelocity, d->angles);
			if (d->model && d->bleed && !d->landed && (rand () & 3) == 0)
			{	/* it bleeds as it flies: drops that fall and splat */
				vec3_t v;
				VectorScale (d->velocity, 0.002f, v);
				Gore_Drops (d->origin, v, 1, 10, 60, 1.0f, 0, 0.8f);
			}
		}
		if (!d->model)
			continue;
		memset (&e, 0, sizeof(e));
		e.model = d->model==G_GRAVEL ? gravel_model : gore_model[d->model];
		if (!e.model) continue;
		e.skinnum = d->skin;
		VectorCopy (d->origin, e.origin);
		VectorCopy (d->origin, e.oldorigin);
		VectorCopy (d->angles, e.angles);
		if (d->life - age < 1000)
		{	/* sink into the floor and fade */
			e.flags |= RF_TRANSLUCENT;
			e.alpha = (d->life - age) / 1000.0f;
			e.origin[2] -= (1000 - (d->life - age)) * 0.004f;
		}
		V_AddEntity (&e);
	}
}

/* ----------------------------------------------------------------------------------------- 3D railgun */

void CL_Gore_Rail (vec3_t start, vec3_t end)
{
	int i, oldest = 0;
	rail3d_t *r;
	vec3_t dir, beyond, trace_start;
	trace_t tr;
	for (i = 1; i < MAX_RAILS; i++)
		if (rails[i].start < rails[oldest].start)
			oldest = i;
	r = &rails[oldest];
	memset (r, 0, sizeof(*r));
	r->start = cl.time;
	VectorCopy (start, r->start_pos);
	VectorCopy (end, r->end_pos);
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);
	VectorMA (end, 8, dir, beyond);
	/* 3.60i hotfix: end belongs to the caller (TE_RAILTRAIL pos2). Never move it:
	   the following scorch trace must still end at the server's impact point. */
	VectorMA (end, -8, dir, trace_start);
	tr = Gore_Trace (trace_start, beyond);
	if (tr.fraction < 1.0f && !(tr.surface && (tr.surface->flags & SURF_SKY)))
	{
		r->hit = true;
		VectorCopy (tr.plane.normal, r->normal);
	}
	else
		VectorNegate (dir, r->normal);
	Gore_Anim (G_RAILFLASH, r->end_pos, r->normal, 20);
	CL_ParticleEffect (r->end_pos, r->normal, 0x74, 24);
}

/* Smoke uses 64-unit pieces and starts one millisecond after the core. */
enum { RAIL_CORE_MS=700, RAIL_BEAM_MS=1200, RAIL_SMOKE_START=RAIL_CORE_MS+1, RAIL_SMOKE_GROW=1000, RAIL_SMOKE_FADE=2000, RAIL_SMOKE_MS=RAIL_SMOKE_GROW+RAIL_SMOKE_FADE };

static qboolean Gore_RailSmoke (rail3d_t *r, int t)
{
	entity_t e;
	vec3_t dir, ang;
	float len, fr, x;
	int i0;
	t -= RAIL_SMOKE_START;
	if (t < 0 || t >= RAIL_SMOKE_MS)
		return false;
	VectorSubtract (r->end_pos, r->start_pos, dir);
	len = VectorNormalize (dir);
	Gore_AnglesFromDir (dir, ang);
	ang[ROLL] = (float)((r->start / 7) % 360);
	memset (&e, 0, sizeof(e));
	e.model = gore_model[G_RAILSMOKE];
	e.flags = RF_PUFFS | RF_TRANSLUCENT;
	e.alpha = t<RAIL_SMOKE_GROW ? 1 : 1-(t-RAIL_SMOKE_GROW)/(float)RAIL_SMOKE_FADE;
	fr = t<RAIL_SMOKE_GROW ? 23*t/(float)RAIL_SMOKE_GROW : 23;
	i0 = (int)fr;
	e.oldframe = i0;
	e.frame = i0 + 1 > 23 ? 23 : i0 + 1;
	e.backlerp = 1.0f - (fr - i0);
	VectorCopy (ang, e.angles);
	for (x = 0; x < len && x < 4096; x += 64)	/* starts a little from the muzzle */
	{
		e.flags = RF_PUFFS | RF_TRANSLUCENT;
		if (len-x < 64)
		{
			e.flags |= RF_RAILSMOKE_SHORT;
			e.lightstyle = (int)(len-x);
		}
		VectorMA (r->start_pos, x, dir, e.origin);
		VectorCopy (e.origin, e.oldorigin);
		V_AddEntity (&e);
	}
	return true;
}

static void Gore_AddRails (void)
{
	int i;
	qboolean distort = gore_cap_distort && cl_heatdistort && cl_heatdistort->value;
	int scheme = Gore_RailScheme ();
	struct model_s *railmodel = scheme ? rail_scheme_model[scheme] : gore_model[G_RAIL];
	const float *lc = rail_scheme_light[scheme];
	for (i = 0; i < MAX_RAILS; i++)
	{
		rail3d_t *r = &rails[i];
		entity_t e;
		int t = cl.time - r->start, k;
		float x;
		if (!r->start)
			continue;
		if (t >= RAIL_SMOKE_START + RAIL_SMOKE_MS || t < 0)
		{
			r->start = 0;
			continue;
		}
		if (t >= RAIL_BEAM_MS)
		{	/* only the smoke is left */
			Gore_RailSmoke (r, t);
			continue;
		}
		x = t / (float)RAIL_BEAM_MS;
		memset (&e, 0, sizeof(e));
		e.model = railmodel;
		e.flags = RF_FXBEAM | RF_TRANSLUCENT;
		VectorCopy (r->start_pos, e.origin);
		VectorCopy (r->end_pos, e.oldorigin);
		if (t < RAIL_CORE_MS)
		{	/* saturated core */
			e.skinnum = 0;
			e.frame = 9;	/* 3.56a: wider (was 5), stays bright longer */
			e.alpha = 1.0f - (t / (float)RAIL_CORE_MS) * (t / (float)RAIL_CORE_MS);
			V_AddEntity (&e);
		}
		/* wide glow */
		e.skinnum = 1;
		e.frame = (int)(14 + 16 * x);
		e.alpha = 0.42f * (1 - x) * (1 - x);	/* 3.56a: was 0.75 */
		V_AddEntity (&e);
		/* three ribbons 120 degrees apart (scheme 0: blue, orange and green) */
		for (k = 0; k < 3; k++)
		{
			e.skinnum = 2 + k;
			e.oldframe = 1;
			e.frame = 4;
			e.lightstyle = (int)(3 + 9 * x) + (k & 1) * 3;
			e.angles[2] = (float)(k * 120 + t * 0.5f);
			e.alpha = 0.90f * (1.0f - x * x);
			V_AddEntity (&e);
		}
		e.oldframe = 0;
		e.lightstyle = 0;
		e.angles[2] = 0;
		Gore_RailSmoke (r, t);
		if (distort && t < 500)
		{	/* the air along the shot shimmers */
			memset (&e, 0, sizeof(e));
			e.flags = RF_DISTORT | RF_TRANSLUCENT;
			e.frame = 2;
			e.skinnum = 30 + t / 40;
			e.alpha = 1.0f - t / 500.0f;
			VectorCopy (r->start_pos, e.origin);
			VectorCopy (r->end_pos, e.oldorigin);
			V_AddEntity (&e);
		}
		if (t < 450 && CL_WeaponLightEnabled (EF_ROCKET))
		{
			float in = 1 - t / 450.0f;
			vec3_t p;
			V_AddLight (r->end_pos, 260 * in, lc[0], lc[1], lc[2]);	/* 3.56r: the core's colour (was blue) */
			if (t < 200)
				for (k = 1; k < 4; k++)
				{
					VectorSubtract (r->end_pos, r->start_pos, p);
					VectorMA (r->start_pos, k / 4.0f, p, p);
					V_AddLight (p, 160 * (1 - t / 200.0f), lc[0], lc[1], lc[2]);
				}
		}
	}
}

/* ----------------------------------------------------------------------------------------- 3D BFG ball */

/* 3.55z: the flying BFG ball (EF_BFG with EF_ANIM_ALLFAST) with "3D BFG explosion" on: a crackling green plasma
   ball with lightning arcs, a glowing tail of green fire behind it, an aura, sparks, heat shimmer and green light.
   The game's sprite is replaced; its swirling particles stay. True when handled. */
qboolean CL_Gore_BFGBall (entity_t *ent, int effects, int number, vec3_t oldorg)
{
	entity_t e;
	vec3_t dir;
	int age;
	if (!(effects & EF_BFG) || !(effects & EF_ANIM_ALLFAST) || number <= 0 || number >= MAX_EDICTS)
		return false;
	if (!cl_3dbfg || !cl_3dbfg->value || !Gore_Modern () || !Cvar_VariableValue ("bdgn_hdfx3d_available"))
		return false;
	if (cl.time - bfg_seen[number] > 250 || bfg_birth[number] > cl.time)
		bfg_birth[number] = cl.time;
	bfg_seen[number] = cl.time;
	age = cl.time - bfg_birth[number];
	VectorSubtract (ent->origin, oldorg, dir);
	if (VectorNormalize (dir) < 0.01f)
		VectorSet (dir, 1, 0, 0);

	memset (&e, 0, sizeof(e));
	VectorCopy (ent->origin, e.origin);
	VectorCopy (ent->origin, e.oldorigin);
	Gore_AnglesFromDir (dir, e.angles);
	e.angles[ROLL] = (float)((cl.time / 4 + number * 37) % 360);
	e.model = gore_model[G_BFGBALL];
	e.flags = RF_FXMESH | RF_FXADD | RF_TRANSLUCENT;
	e.alpha = 1;
	e.frame = e.oldframe = ((cl.time / 40) + number) & 7;	/* new arcs 25 times a second */
	V_AddEntity (&e);

	e.model = gore_model[G_BFGTAIL];
	e.flags = RF_PUFFS | RF_FULLBRIGHT | RF_TRANSLUCENT;
	e.angles[ROLL] = 0;
	if (age < 150)
	{
		float f = age / 150.0f * 7.0f;
		e.oldframe = (int)f;
		e.frame = e.oldframe + 1 > 7 ? 7 : e.oldframe + 1;
		e.backlerp = 1.0f - (f - (int)f);
	}
	else
	{
		float f = (cl.time % 400) / 50.0f;
		e.oldframe = 8 + (int)f;
		e.frame = e.oldframe == 15 ? 8 : e.oldframe + 1;
		e.backlerp = 1.0f - (f - (int)f);
	}
	V_AddEntity (&e);

	if (gore_cap_distort && cl_heatdistort && cl_heatdistort->value)
	{
		memset (&e, 0, sizeof(e));
		VectorCopy (ent->origin, e.origin);
		VectorCopy (ent->origin, e.oldorigin);
		e.flags = RF_DISTORT | RF_TRANSLUCENT;
		e.skinnum = 42;
		e.alpha = 0.6f;
		V_AddEntity (&e);
	}
	CL_BfgParticles (ent);
	V_AddLight (ent->origin, 320 + (rand () & 31), 0.1f, 1.0f, 0.15f);
	return true;
}

/* ----------------------------------------------------------------------------------------- temp entities */

/* from CL_ParseTEnt, after every temp entity (pos / pos2 / dir as read there, zero when not sent) */
void CL_Gore_ClearActorMarks(int number)
{
	if (actor_mark_slot[number])
		memset(actor_marks[number],0,sizeof(actor_marks[number]));
	actor_mark_slot[number]=0;
}

void CL_Gore_ActorMarks(entity_t *ent,const entity_state_t *state)
{
	int i, number=state->number;
	if (!actor_mark_slot[number]) return;
	if (!state->solid || state->solid==31) { CL_Gore_ClearActorMarks(number); return; }
	if (!actor_mark_skin || !Gore_BloodDecals() || !ent->model ||
	    (ent->flags&(RF_DUKEANIM|RF_TRANSLUCENT))) return;
	for(i=0;i<MAX_ACTOR_MARKS;i++)
	{
		actor_mark_t *mark=&actor_marks[number][i];
		entity_t draw;
		if (!mark->packed) continue;
		draw=*ent;
		draw.flags=RF_TRANSLUCENT|RF_ACTOR_MARK;
		draw.skin=actor_mark_skin;
		draw.lightstyle=mark->packed;
		draw.skinnum=mark->tile;
		VectorCopy(mark->local,draw.oldorigin);
		V_AddEntity(&draw);
	}
}

static void Gore_RecordActorMark(vec3_t pos,vec3_t normal)
{
	const entity_state_t *target=NULL;
	actor_mark_t *mark;
	vec3_t axes[3], delta, local;
	float best=16;
	int i, bits, number;
	if (!actor_mark_skin || !Gore_BloodDecals()) return;
	for(i=0;i<cl.frame.num_entities;i++)
	{
		const entity_state_t *s=&cl_parse_entities[(cl.frame.parse_entities+i)&(MAX_PARSE_ENTITIES-1)];
		float x, ymin, ymax, dist;
		if (!s->solid || s->solid==31 || !s->modelindex || s->number==cl.playernum+1)
			continue;
		if (s->modelindex!=255 && strncmp(cl.configstrings[CS_MODELS+s->modelindex],"models/monsters/",16))
			continue;
		VectorSubtract(pos,s->origin,delta);
		x=8*(s->solid&31);
		ymin=-8*((s->solid>>5)&31);
		ymax=8*((s->solid>>10)&63)-32;
		if (fabs(delta[0])>x+3 || fabs(delta[1])>x+3 || delta[2]<ymin-3 || delta[2]>ymax+3)
			continue;
		dist=(float)fabs(x-(fabs(delta[0])>fabs(delta[1])?fabs(delta[0]):fabs(delta[1])));
		if (dist<best) { best=dist; target=s; }
	}
	if (!target) return;
	number=target->number;
	i=actor_mark_slot[number]?actor_mark_slot[number]-1:0;
	actor_mark_slot[number]=1+((i+1)%MAX_ACTOR_MARKS);
	mark=&actor_marks[number][i];
	AngleVectors(target->angles,axes[0],axes[1],axes[2]);
	VectorSubtract(pos,target->origin,delta);
	VectorSet(mark->local,DotProduct(delta,axes[0]),-DotProduct(delta,axes[1]),DotProduct(delta,axes[2]));
	VectorSet(local,DotProduct(normal,axes[0]),-DotProduct(normal,axes[1]),DotProduct(normal,axes[2]));
	bits=0;
	for(i=0;i<3;i++) bits|=((int)(local[i]*120)+128)<<(i*8);
	mark->packed=bits;
	mark->tile=rand()%3;
}

void CL_Gore_TEnt (int type, vec3_t pos, vec3_t pos2, vec3_t dir)
{
	qboolean marks = Gore_DecalsOn (), gibs = Gore_GibsOn ();
	vec3_t hit, normal;
	int life;
	if (!marks && !gibs && !CL_Gore_RailOn () && !(gravel_model && cl_3dexplo && cl_3dexplo->value))
		return;
	life = Gore_DecalLife ();
	switch (type)
	{
	case TE_GUNSHOT:
	case TE_BULLET_SPARKS:
		Gore_Event (C_BULLET, pos, NULL);
		if (marks) Gore_Decal (pos, dir, 2.6f + frand (), T_HOLE + (rand () & 3), 0, life, 1);
		break;
	case TE_SHOTGUN:
		Gore_Event (C_BULLET, pos, NULL);
		if (marks) Gore_Decal (pos, dir, 2.0f + frand () * 0.8f, T_HOLE + (rand () & 3), 0, life, 1);
		break;
	case TE_BLOOD:
		if (VectorLength (dir) < 0.1f || (dir[1] == 0 && dir[0] < -0.52f && dir[0] > -0.53f && dir[2] > 0.85f))
		{	/* 3.55ab: a zero normal arrives as direction 0 of the table (-0.5257, 0, 0.8507), never a real hit face */	/* 3.55z: explosion damage (the game sends the explosion's position, no direction): blood flies out
			   of the hit body away from the explosion */
			vec3_t victim, way;
			Gore_Event (C_EXPLODE, pos, NULL);
			if (gibs && gore_bloodfx->value != 2 && Gore_VictimNear (pos, victim))
			{
				VectorSubtract (victim, pos, way);
				if (VectorNormalize (way) < 1)
					VectorSet (way, crand (), crand (), 1);
				way[2] += 0.25f;
				VectorNormalize (way);
				Gore_ExitBlood (victim, way, 26, 420, 20);
			}
			break;
		}
		Gore_RecordActorMark(pos,dir);
		Gore_Event (C_BULLET, pos, NULL);
		if (gibs && gore_bloodfx->value != 2)
		{	/* 3.55z: the blood leaves the body on the far side and flies on in the projectile's direction.
			   dir is the face of the body that was hit (towards the shooter); a shot of our own comes from the view,
			   which gives the real direction */
			vec3_t way, fromview;
			VectorNegate (dir, way);
			VectorSubtract (pos, cl.refdef.vieworg, fromview);
			if (VectorNormalize (fromview) > 1 && DotProduct (fromview, way) > 0.3f)
				VectorCopy (fromview, way);
			Gore_ExitBlood (pos, way, 12, 360, 14);
		}
		break;
	case TE_BLASTER:
	case TE_BLASTER2:
	case TE_FLECHETTE:
		Gore_Event (C_BLASTER, pos, NULL);
		if (marks)
		{
			Gore_Decal (pos, dir, 6, T_BLASTER, 0, life, 0.9f);
			Gore_Decal (pos, dir, 8, T_GLOW_BLASTER, 1, 1200, 1);
		}
		break;
	case TE_RAILTRAIL:
		Gore_Event (C_RAIL, pos, pos2);
		if (CL_Gore_RailOn ())
			CL_Gore_Rail (pos, pos2);
		if (marks)
		{
			trace_t tr;
			vec3_t d, a, b;
			VectorSubtract (pos2, pos, d);
			VectorNormalize (d);
			VectorMA (pos2, -8, d, a);
			VectorMA (pos2, 8, d, b);
			tr = Gore_Trace (a, b);
			if (tr.fraction < 1.0f && !(tr.surface && (tr.surface->flags & SURF_SKY)))
			{
				Gore_Decal (tr.endpos, tr.plane.normal, 12, T_RAIL, 0, life, 1);
				/* The dark burn stays visible; no additive glow over its centre. */
				CL_FX3D_RailImpactSmoke (tr.endpos, tr.plane.normal);
			}
		}
		break;
	case TE_EXPLOSION1: case TE_EXPLOSION1_BIG: case TE_ROCKET_EXPLOSION: case TE_ROCKET_EXPLOSION_WATER: case TE_EXPLOSION1_NP:
	case TE_EXPLOSION2: case TE_GRENADE_EXPLOSION: case TE_GRENADE_EXPLOSION_WATER: case TE_PLASMA_EXPLOSION: case TE_PLAIN_EXPLOSION:
		Gore_Event (C_EXPLODE, pos, NULL);
		Gore_Gravel(pos);
		if (marks && Gore_NearestSurface (pos, 48, hit, normal))
		{
			float size = (type == TE_GRENADE_EXPLOSION || type == TE_GRENADE_EXPLOSION_WATER) ? 34 : (type == TE_EXPLOSION2 ? 56 : 44);	/* 3.55aa: barrels leave a big scorch */
			if (type == TE_EXPLOSION1_BIG) size = 60;
			Gore_Decal (hit, normal, size * (0.9f + 0.2f * frand ()), T_SCORCH + (rand () & 1), 0, life, 0.95f);
		}
		break;
	case TE_BFG_EXPLOSION:
	case TE_BFG_BIGEXPLOSION:
		Gore_Event (C_BFG, pos, NULL);
		if (marks && Gore_NearestSurface (pos, 64, hit, normal))
			Gore_Decal (hit, normal, type == TE_BFG_BIGEXPLOSION ? 80.0f : 30.0f, T_BFG, 0, life, 0.9f);
		break;
	default:
		break;
	}
}

/* ----------------------------------------------------------------------------------------- per frame */

void CL_Gore_AddEntities (void)
{
	int i, now = cl.time;
	entity_t e;
	qboolean marks = Gore_DecalsOn (), blood = Gore_BloodDecals ();
	if (marks || blood)
	{
		for (i = 0; i < MAX_DECALS; i++)
		{
			decal_t *d = &decals[i];
			int t;
			qboolean is_blood;
			if (!d->id)
				continue;
			is_blood = IS_BLOOD (d->tile);
			if (is_blood ? !blood : !marks)
				continue;
			t = now - d->start;
			if (t >= d->life || t < 0)
			{
				d->id = 0;
				continue;
			}
			memset (&e, 0, sizeof(e));
			e.model = gore_model[G_DECALS];
			e.flags = RF_DECAL | RF_TRANSLUCENT;
			VectorCopy (d->origin, e.origin);
			VectorCopy (d->origin, e.oldorigin);
			VectorCopy (d->angles, e.angles);
			e.frame = d->size;
			if (d->grow > 0 && t < d->grow)	/* pools spread out (whole units, so the renderer re-cuts it a few times) */
			{
				float g = t / (float)d->grow;
				e.frame = (int)(d->size * (0.3f + 0.7f * (1 - (1 - g) * (1 - g))) + 0.5f);
				if (e.frame < 1) e.frame = 1;
			}
			e.oldframe = d->id;
			e.skinnum = d->tile;
			e.lightstyle = d->glow;
			if (is_blood)
			{	/* 3.55ah: the shade of the blood (the renderer multiplies its colour with backlerp): more blood is
				   darker, and it dries - fresh blood is brighter, after a minute it is a fifth darker */
				float shade = 1.14f - (float)e.frame * 0.0145f, dry = t / 60000.0f;
				if (shade < 0.72f) shade = 0.72f;
				if (dry > 1) dry = 1;
				e.backlerp = shade * (1.0f - 0.2f * dry) * 0.84f;	/* 3.55al: a little darker overall */
			}
			e.alpha = d->alpha;
			if (d->fade > 0 && t > d->life - d->fade)
				e.alpha *= (d->life - t) / (float)d->fade;
			V_AddEntity (&e);
		}
	}
	if (CL_Gore_RailOn ())
		Gore_AddRails ();
	if (!Gore_Modern ())
		return;
	Gore_RunDebris ();
	Gore_RunDrops ();
	for (i = 0; i < MAX_GOREANIM; i++)
	{
		goreanim_t *a = &goreanim[i];
		float fr;
		int i0;
		if (!a->model)
			continue;
		fr = (now - a->start) * a->fps / 1000.0f;
		if (fr >= a->frames - 1 || fr < 0)
		{
			a->model = 0;
			continue;
		}
		memset (&e, 0, sizeof(e));
		e.model = gore_model[a->model];
		e.flags = RF_PUFFS | RF_TRANSLUCENT;
		e.alpha = 1;
		i0 = (int)fr;
		e.oldframe = i0;
		e.frame = i0 + 1;
		e.backlerp = 1.0f - (fr - i0);
		VectorCopy (a->origin, e.origin);
		VectorCopy (a->origin, e.oldorigin);
		VectorCopy (a->angles, e.angles);
		V_AddEntity (&e);
	}
}
