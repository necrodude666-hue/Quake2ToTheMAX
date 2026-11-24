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
// cl_ents.c -- entity parsing and management

#include "client.h"

/* BDGN 3.52r: sprite lighting (test toggle cl_spritelight, saved): 2D enemies, classic sprites and 2D items get the
   world light at their origin instead of full brightness (RF_LITSPRITE; GL: colour of R_LightPoint, software: the
   colormap row of the light, coloured with sw_coloredlight). The brightskins look (RF_SIMPLEBRIGHT) stays as it is. */
static void CL_SpriteLight (entity_t *ent)
{
	static cvar_t *cv;
	if (!cv)
		cv = Cvar_Get ("cl_spritelight", "0", CVAR_ARCHIVE);
	if (cv->value && !(ent->flags & (RF_SIMPLEBRIGHT|RF_TRANSLUCENT)))
		ent->flags |= RF_LITSPRITE;
}

#include "simple_items.h"	/* BDGN 3.51t: 2D items (one flat picture per pickup); the 4/8-direction views are gone */
#include "simple_enemies.h"
#include "duke_lod.h"	/* BDGN 3.51p: cl_simpleenemy_lod */
#include "../qcommon/duke_modes.h"	/* BDGN 3.51q: one definition of the sprite modes and rates */

static struct model_s *simple_item_models[MAX_MODELS];


static int simple_item_last_mode=-1,simple_enemy_last_signature=-1;
/* BDGN 3.51q: renderer capability cvars looked up once (3.51p: a name search per enemy and frame) */
static cvar_t *cl_duke_support,*cl_duke_bright;
static void CL_SpriteCvars(void)
{
 if(cl_duke_support)return;
 cl_duke_support=Cvar_Get("r_duke_anim_support","0",0);cl_duke_bright=Cvar_Get("r_simpleenemy_bright","0",0);
}
/* BDGN 3.57a: far enemies as sprites (cl_far2d, Duke 3D / Doom style): with 2D enemies off, monsters farther than
   cl_far2d units are drawn as the pre-rendered sprites of the 2D enemy mode (4 directions) and the near ones as 3D
   models. The sprite mode in use: cl_simpleenemies, or 3 when only cl_far2d asks for sprites. */
static cvar_t *cl_far2d;

/* BDGN 3.57c / 3.60i hotfix: object draw distance (cl_objdist, FastDoom style): gibs, debris, brass, smoke and small impact
   sprites farther than cl_objdist units from the view are not drawn (0 = off). Monsters, players, doors, lifts,
   projectiles and big explosions are always drawn. */
static cvar_t *cl_objdist;
qboolean CL_ObjectTooFar (const vec3_t org)
{
 vec3_t d;
 if (!cl_objdist) cl_objdist=Cvar_Get("cl_objdist","0",CVAR_ARCHIVE);
 if (cl_objdist->value <= 0) return false;
 VectorSubtract (org, cl.refdef.vieworg, d);
 return DotProduct (d, d) > cl_objdist->value * cl_objdist->value;
}
/* BDGN 3.57m: simpler 3D models by distance (cl_simplemodels_far): beyond that many units from the view a model
   is drawn with its "simplest" version of bdgn_simplemodels.pak (models/simple3/...), the near ones as chosen with
   cl_simplemodels. Only the picture changes: the client keeps the model index, so nothing changes for the server. */
static cvar_t *cl_simplemodels_far;
/* BDGN 3.57o: one choice for the whole distance chain (cl_modellod): near the normal model, then the simplest model,
   then a 2D sprite, then a sprite that changes pose seldom. It sets the three single rows when it changes (a menu, a
   preset, the benchmark or the console); 0 leaves them as they are set one by one.
     1 light : simplest beyond 1024, sprites off,         far animation auto
     2 medium: simplest beyond 768,  sprites beyond 1024, far animation auto
     3 strong: simplest beyond 512,  sprites beyond 768,  far animation 3.125 fps
     4 max   : simplest beyond 512,  sprites beyond 512,  far animation 3.125 fps
   Sprites need the 2D enemy pictures (pak4); without them that step is left out by the sprite code itself. */
static cvar_t *cl_modellod;
/* BDGN 3.57s: one switch for the whole low-spec 3D bundle (owner's wish): cl_lowspec3d 1 = simpler models,
   simpler maps and model LOD strong (cl_simplemodels 2, cl_simpleworld 1, cl_modellod 3); 0 turns the three off.
   It acts when it changes (menu, preset, benchmark, console); the menu row restarts the renderer. */
static cvar_t *cl_lowspec3d;
static void CL_LowSpec3DSync (void)
{
 if (!cl_lowspec3d) { cl_lowspec3d=Cvar_Get("cl_lowspec3d","0",CVAR_ARCHIVE); cl_lowspec3d->modified=false; }
 if (!cl_lowspec3d->modified) return;
 cl_lowspec3d->modified=false;
 if (!cl_lowspec3d->value && !(Cvar_VariableValue("cl_simplemodels")==2 && Cvar_VariableValue("cl_simpleworld")==1 &&
     Cvar_VariableValue("cl_modellod")==3))
  return;	/* off, but the three are not the bundle's (a preset or the benchmark set them): leave them */
 Cvar_Set("cl_simplemodels",cl_lowspec3d->value?"2":"0");
 Cvar_Set("cl_simpleworld",cl_lowspec3d->value?"1":"0");
 Cvar_Set("cl_modellod",cl_lowspec3d->value?"3":"0");
 if (!cl_lowspec3d->value) {	/* cl_modellod 0 leaves its rows as they are: turn them off as well */
  Cvar_Set("cl_simplemodels_far","0");
  Cvar_Set("cl_far2d","0");
 }
}
void CL_ModelLodSync (void)
{
 static const char *far3d[5]={NULL,"1024","768","512","512"};
 static const char *far2d[5]={NULL,"0","1024","768","512"};
 static const char *anim[5]={NULL,"1","1","2","2"};
 int v;
 CL_LowSpec3DSync ();	/* 3.57s: the bundle first, it sets cl_modellod */
 if (!cl_modellod) cl_modellod=Cvar_Get("cl_modellod","0",CVAR_ARCHIVE);
 if (!cl_modellod->modified) return;
 cl_modellod->modified=false;
 v=(int)cl_modellod->value;
 if (v<1 || v>4) return;
 Cvar_Set("cl_simplemodels_far",(char *)far3d[v]);
 Cvar_Set("cl_far2d",(char *)far2d[v]);
 Cvar_Set("cl_simpleenemy_lod",(char *)anim[v]);
}
void CL_RegisterFarModel (int i)
{
 const char *name = cl.configstrings[CS_MODELS+i];
 char simple[MAX_QPATH];
 size_t n;
 cl.model_far[i] = NULL;
 CL_ModelLodSync ();	/* 3.57o: a changed cl_modellod sets the distance first */
 if (!cl_simplemodels_far) cl_simplemodels_far=Cvar_Get("cl_simplemodels_far","0",CVAR_ARCHIVE);
 if (cl_simplemodels_far->value <= 0 || Cvar_VariableValue("fs_vanilla") || Cvar_VariableValue("cl_simplemodels") >= 3)
  return;
 n = strlen (name);
 if (n < 12 || n >= MAX_QPATH - 9 || Q_strncasecmp ((char *)name, "models/", 7) || Q_stricmp ((char *)name + n - 4, ".md2") ||
     !Q_strncasecmp ((char *)name + 7, "simple", 6) || !Q_strncasecmp ((char *)name + 7, "weapons/v_", 10))
  return;
 Com_sprintf (simple, sizeof(simple), "models/simple3/%s", name + 7);
 if (FS_LoadFile (simple, NULL) > 0)
  cl.model_far[i] = re.RegisterModel (simple);
}
static qboolean CL_FarModel (const vec3_t org)
{
 vec3_t d;
 if (!cl_simplemodels_far || cl_simplemodels_far->value <= 0) return false;
 VectorSubtract (org, cl.refdef.vieworg, d);
 return DotProduct (d, d) > cl_simplemodels_far->value * cl_simplemodels_far->value;
}
static float CL_SpriteModeValue(void)
{
 if(!cl_far2d) cl_far2d=Cvar_Get("cl_far2d","0",CVAR_ARCHIVE);
 if(cl_simpleenemies->value) return cl_simpleenemies->value;
 return cl_far2d->value>0?3.0f:0.0f;
}
/* BDGN 3.51q: the same test for registration, pose clock and signature (3.51p: 1-2 here, 1-3 in the pose code) */
static int CL_DukeSpriteActive(void)
{
 CL_SpriteCvars();
 return Duke_ModeDirectional(CL_SpriteModeValue()) && Duke_RateValid(cl_simpleenemy_anim->value) &&
  !cl_staticenemies->value && cl_duke_support->value>=2;
}
/* BDGN 3.51bct: simple 2d enemies 2 = low-res, 4 = low-res brightskins */
static int CL_SimpleLowRes (void)
{
 int v=(int)Cvar_VariableValue("cl_simple2denemies");
 return v==2 || v==4;
}
static int CL_SimpleSpriteSignature(void)
{ CL_SpriteCvars(); return (int)CL_SpriteModeValue() + (CL_DukeSpriteActive()?16:0) + (cl_duke_bright->value?32:0) + (CL_SimpleLowRes()?64:0); }

/* BDGN 3.51t: 2D items. One flat picture per stock pickup, rendered from the 3D model (weapons from the side, muzzle
   to the left; everything else from the front), with the model's size and position (sprites/items in
   bdgn_sprites.pak, _extra/items/build_items.py). cl_simpleitems 0 = 3D models, 1 = 2D items; the old 4- and
   8-direction values (2/3) become 1. */
void CL_RegisterSimpleItems (void)
{
 int i, j, mode;
 if (cl_simpleitems->value >= 2) {
  Com_Printf ("BDGN: cl_simpleitems %g (directional items) no longer exists; set to 1 (2D items)\n", cl_simpleitems->value);
  Cvar_SetValue ("cl_simpleitems", 1);
 }
 /* BDGN 3.51aj: 2D items only with classic 2d sprites mode; otherwise the stock 3D pickups (the default) */
 if (cl_simpleitems->value && !Cvar_VariableValue ("cl_classic2d")) Cvar_SetValue ("cl_simpleitems", 0);
 mode=(int)cl_simpleitems->value;
 simple_item_last_mode=mode;
 memset (simple_item_models, 0, sizeof(simple_item_models));
 if(mode!=1)return;
 for (i = 1; i < MAX_MODELS && cl.configstrings[CS_MODELS+i][0]; i++)
  for (j = 0; j < sizeof(simple_items)/sizeof(simple_items[0]); j++)
   if (!Q_stricmp (cl.configstrings[CS_MODELS+i], simple_items[j].model))
   {
    char texture[MAX_QPATH];
    Com_sprintf (texture, sizeof(texture), "%s", simple_items[j].sprite);
    strcpy (texture + strlen(texture) - 4, ".pcx");
    if (FS_LoadFile (texture, NULL) > 0)
     simple_item_models[i] = re.RegisterModel (simple_items[j].sprite);
    break;
   }
}


/* Client-side impostors: original entities/collision/network data stay intact. */
#define SIMPLE_ENEMY_SKINS 8
static struct model_s *simple_enemy_pose_models[sizeof(simple_enemies)/sizeof(simple_enemies[0])];
static struct model_s *simple_enemy_models[MAX_MODELS][SIMPLE_ENEMY_SKINS];
static struct image_s *simple_enemy_skins[MAX_MODELS][SIMPLE_ENEMY_SKINS];
static byte simple_enemy_known[MAX_MODELS][SIMPLE_ENEMY_SKINS];

static struct model_s *CL_LoadSimpleEnemySprite (const char *name)
{
 char modelname[MAX_QPATH];struct model_s *sprite;int j,mode=(int)CL_SpriteModeValue();
 if(mode<1 || mode>4 || CL_DukeSpriteActive())return NULL;	/* BDGN 3.51s: 8 directions (5/6) removed */
 CL_SpriteCvars();
 if(!(mode & 1) && !cl_duke_bright->value)mode--;
 Com_sprintf(modelname,sizeof(modelname),"%s",name);
 Com_sprintf(modelname+strlen(modelname)-4,sizeof(modelname)-strlen(modelname)+4,"_p%i.sp2",mode);
 /* BDGN 3.51ast: simple 2d enemies "low-res" (_q1/_q2.sp2, pak4); 3.51bct: in the look of classic 2d sprites mode
    (its 32x32 tiles, drawn GL_NEAREST); the 256x256 set is used when the low-res set is missing */
 if(mode<=2 && CL_SimpleLowRes()) {
  char lowname[MAX_QPATH];
  Com_sprintf(lowname,sizeof(lowname),"%s",modelname);
  lowname[strlen(lowname)-6]='q';	/* ..._p1.sp2 -> ..._q1.sp2 */
  if(FS_LoadFile(lowname,NULL)>0)strcpy(modelname,lowname);
 }
 /* The PAK is validated as one atomic asset set; no per-texture existence probes. */
 if(FS_LoadFile(modelname,NULL)<=0)return NULL;
 sprite=re.RegisterModel(modelname);
 for(j=0;j<sizeof(simple_enemies)/sizeof(simple_enemies[0]);j++)if(!strcmp(name,simple_enemies[j].sprite))simple_enemy_pose_models[j]=sprite;
 return sprite;
}


typedef struct {
 struct model_s *model;
 int valid,bucket,period,frame,oldframe,tier,lastframe;	/* tier: distance class of cl_simpleenemy_lod; lastframe: server frame last seen (duke_lod.h) */
 float backlerp,pitch,roll;
} duke_actor_pose_t;
static duke_actor_pose_t duke_actor_poses[MAX_EDICTS];
static qboolean CL_DukeEnemyPose(entity_t *ent,const entity_state_t *state)
{
 duke_actor_pose_t *pose;
 int mode=(int)CL_SpriteModeValue(),period,bucket,i,tier=0;
 float depth;
 static float lodfov=-1,lodscale;	/* fov scale computed once per fov, not per enemy */
 if(!CL_DukeSpriteActive() || state->number<1 || state->number>=MAX_EDICTS)return false;
 period=Duke_RatePeriod(cl_simpleenemy_anim->value);pose=&duke_actor_poses[state->number];	/* 80 or 160 ms (3.51q: no 25/s) */
 /* BDGN 3.51p: a reused entity number or an entity back in view starts a fresh slot (no inherited pose or tier) */
 if(pose->valid && !CL_DukePoseContinuous(pose->lastframe,cl.frame.serverframe))pose->valid=0;
 pose->lastframe=cl.frame.serverframe;
 if(cl_simpleenemy_lod->value || CL_LOWEND_SPRITES()) {	/* BDGN 3.51p: far enemies hold each pose longer (display only) */
  depth=(ent->origin[0]-cl.refdef.vieworg[0])*cl.v_forward[0]+(ent->origin[1]-cl.refdef.vieworg[1])*cl.v_forward[1]+(ent->origin[2]-cl.refdef.vieworg[2])*cl.v_forward[2];
  /* fov_y is set later in V_RenderView: this is the previous frame's (0 on the first frame = full rate) */
  if(cl.refdef.fov_y!=lodfov){lodfov=cl.refdef.fov_y;lodscale=CL_DukeLodScale(lodfov);}
  tier=CL_DukeLodTier(CL_DukeLodDistanceScaled(depth,lodscale),pose->valid?pose->tier:0);
  if(cl_simpleenemy_lod->value==2 || CL_LOWEND_SPRITES()) {
   float distance=CL_DukeLodDistanceScaled(depth,lodscale);
   tier=distance>DUKE_LOD_STILL*(pose->valid && pose->tier==DUKE_LOD_TIERS?.92f:1.08f)?DUKE_LOD_TIERS:0;
   if(tier) period=320;
  } else
   period=CL_DukePosePeriod(period,1,tier);
  /* BDGN 3.51s: beyond 2048 a near-still picture (one pose per 2.56 s, so an enemy that dies there still falls) */
 }
 pose->tier=tier;bucket=cl.time/period;
 /* Position remains smooth; only the displayed pose is held on the sprite clock. */
 if(state->renderfx & RF_FRAMELERP)for(i=0;i<3;i++)ent->origin[i]=ent->oldorigin[i]*ent->backlerp+ent->origin[i]*(1.0f-ent->backlerp);
 VectorCopy(ent->origin,ent->oldorigin);
 if(!pose->valid || pose->model!=ent->model || pose->bucket!=bucket || pose->period!=period) {
  pose->valid=1;pose->model=ent->model;pose->bucket=bucket;pose->period=period;
  pose->frame=ent->frame;pose->oldframe=ent->oldframe;pose->backlerp=ent->backlerp;pose->pitch=ent->angles[0];pose->roll=ent->angles[2];
 }
 ent->frame=pose->frame;ent->oldframe=pose->oldframe;ent->backlerp=pose->backlerp;ent->angles[0]=pose->pitch;ent->angles[2]=pose->roll;
 ent->flags=(ent->flags & (RF_TRANSLUCENT|RF_IR_VISIBLE))|RF_DUKEANIM|RF_FULLBRIGHT;
 if(mode==4 && cl_duke_bright->value)ent->flags|=RF_SIMPLEBRIGHT;
 CL_SpriteLight(ent);
 return true;
}

void CL_RegisterSimpleEnemyModel (int index)
{
 int j, sk;
 if (index <= 0 || index >= MAX_MODELS) return;
 memset (simple_enemy_models[index], 0, sizeof(simple_enemy_models[index]));
 memset (simple_enemy_skins[index], 0, sizeof(simple_enemy_skins[index]));
 memset (simple_enemy_known[index],0,sizeof(simple_enemy_known[index]));
 if(!CL_SpriteModeValue())return;
 for (j = 0; j < sizeof(simple_enemies)/sizeof(simple_enemies[0]); j++)
 {
  sk = simple_enemies[j].skinnum;
  if (sk < 0 || sk >= SIMPLE_ENEMY_SKINS ||
   Q_stricmp (cl.configstrings[CS_MODELS+index], (char *)simple_enemies[j].model)) continue;
  simple_enemy_known[index][sk]=1;
  if(CL_DukeSpriteActive())continue;
  simple_enemy_models[index][sk] = CL_LoadSimpleEnemySprite (simple_enemies[j].sprite);
  if (simple_enemy_models[index][sk])
   simple_enemy_skins[index][sk] = re.RegisterSkin ((char *)simple_enemies[j].bright_skin);
 }
}

void CL_RegisterSimpleEnemies (void)
{
 int i;
 simple_enemy_last_signature=CL_SimpleSpriteSignature();
 memset (simple_enemy_known,0,sizeof(simple_enemy_known));
 memset (duke_actor_poses, 0, sizeof(duke_actor_poses));
 memset (simple_enemy_pose_models, 0, sizeof(simple_enemy_pose_models));
 memset (simple_enemy_models, 0, sizeof(simple_enemy_models));
 memset (simple_enemy_skins, 0, sizeof(simple_enemy_skins));
 for (i = 1; i < MAX_MODELS && cl.configstrings[CS_MODELS+i][0]; i++)
  CL_RegisterSimpleEnemyModel (i);
}

struct model_s *CL_RegisterSimpleEnemySkin (const char *model, const char *skin, int *known)
{
 int j;
 *known=0;
 for (j = 0; j < sizeof(simple_enemies)/sizeof(simple_enemies[0]); j++)
  if (simple_enemies[j].skinnum < 0 &&
   !Q_stricmp ((char *)model, (char *)simple_enemies[j].model) &&
   !Q_stricmp ((char *)skin, (char *)simple_enemies[j].skin))
  { *known=1; return CL_LoadSimpleEnemySprite (simple_enemies[j].sprite); }
 return NULL; /* An unknown custom model keeps its original appearance. */
}

static int CL_SimpleEnemyPose (struct model_s *sprite, int frame)
{
 int j, k;
 for (j = 0; j < sizeof(simple_enemies)/sizeof(simple_enemies[0]); j++)
  if (simple_enemy_pose_models[j] == sprite)
  {
   for (k = 0; k < sizeof(simple_enemy_deaths)/sizeof(simple_enemy_deaths[0]); k++)
    if (simple_enemy_deaths[k].entry == j && frame >= simple_enemy_deaths[k].first && frame <= simple_enemy_deaths[k].last) return 2;
   return 0;
  }
 return 0;
}

/* Relative camera bearing, quantized to cardinal or eight-way views.
 * Asset view 0 looks along the model's +X (front), view 2 from +Y. */
static int CL_SimpleEnemyDirection (const entity_t *ent, int mode)
{
 float dx, dy, bearing;
 int count, sector;
 if (mode < 3 || mode > 6) return 0;
 count = mode < 5 ? 4 : 8;
 dx = cl.refdef.vieworg[0] - ent->origin[0];
 dy = cl.refdef.vieworg[1] - ent->origin[1];
 if (dx == 0 && dy == 0) bearing = 0;
 else bearing = anglemod ((float)(atan2(dy, dx) * (180.0 / M_PI)) - ent->angles[1]);
 sector = (int)(bearing * count / 360.0f + 0.5001f) % count;
 return sector * (8 / count) * 4;
}

static qboolean CL_SimpleEnemy (entity_t *ent, const entity_state_t *state, int effects)
{
 struct model_s *sprite;
 struct image_s *skin;
 clientinfo_t *ci;
 int i, sk;
 qboolean known;
 if (!CL_SpriteModeValue() || state->modelindex <= 0 || state->modelindex >= MAX_MODELS ||
  (effects & EF_ROTATE) ||
  (state->renderfx & (RF_BEAM|RF_WEAPONMODEL|RF_USE_DISGUISE))) return false;
 if (!cl_simpleenemies->value)
 {	/* BDGN 3.57a: only cl_far2d: the near ones stay 3D */
  vec3_t d;
  VectorSubtract (ent->origin, cl.refdef.vieworg, d);
  if (DotProduct (d, d) < cl_far2d->value * cl_far2d->value) return false;
 }
 if (state->modelindex == 255)
 {
  ci = &cl.clientinfo[state->skinnum & 0xff];
  if (!ci->model || !ci->skin) ci = &cl.baseclientinfo;
  if (ci->model != ent->model || ci->skin != ent->skin) return false;
  known=ci->simple_enemy_known;
  sprite = ci->simple_enemy;
  skin = ci->skin;
 }
 else
 {
  sk = ent->skinnum;
  if (sk < 0 || sk >= SIMPLE_ENEMY_SKINS) return false;
  known=simple_enemy_known[state->modelindex][sk];
  sprite = simple_enemy_models[state->modelindex][sk];
  skin = simple_enemy_skins[state->modelindex][sk];
 }
 if (known && CL_DukeEnemyPose(ent,state)) return true;
 if (!sprite) return false;
 /* Alias frame lerp also interpolates origins for RF_FRAMELERP actors. */
 if (state->renderfx & RF_FRAMELERP)
  for (i = 0; i < 3; i++)
   ent->origin[i] = ent->oldorigin[i]*ent->backlerp + ent->origin[i]*(1.0f-ent->backlerp);
 ent->model = sprite;
 ent->skin = skin; /* Original skin identity supplies exactly the existing bright colour. */
 ent->skinnum = 0;
 ent->frame = ent->oldframe = CL_SimpleEnemyDirection (ent, (int)CL_SpriteModeValue()) + CL_SimpleEnemyPose (sprite, state->frame) + (((CL_SpriteModeValue() == 2 || CL_SpriteModeValue() == 4 || CL_SpriteModeValue() == 6) && skin &&
  cl_duke_bright->value) ? 1 : 0);
 ent->backlerp = 0;
 ent->flags = (ent->flags & (RF_TRANSLUCENT|RF_IR_VISIBLE)) | RF_FULLBRIGHT;
 if (ent->frame & 1) ent->flags |= RF_SIMPLEBRIGHT;
 CL_SpriteLight (ent);
 VectorClear (ent->angles);
 VectorCopy (ent->origin, ent->oldorigin);
 return true;
}

/*
BDGN 3.51q: bdgn_spritecheck -- why enemies are (or are not) drawn as pre-rendered sprites, without changing anything:
exe and renderer versions, the settings and what they result in, which PAK supplies the libraries and which models of
the current map have a bank. A missing PAK is only found after a full restart (vid_restart keeps the file system).
*/
#include "../qcommon/duke_banks.h"
void CL_SpriteCheck_f (void)
{
 int i,j,known=0,missing=0,res,sup;const char *where,*path;
 CL_SpriteCvars();
 sup=(int)cl_duke_support->value;res=32;	/* BDGN 3.52h: 32x32 library only */
 Com_Printf("exe: BDGN %s; renderer vid_ref %s, sprite support %i (%s)\n",BDGN_VERSION_STRING,Cvar_VariableString("vid_ref"),sup,
  sup>=3?"32x32 library":(sup==2?"3.51p: 64x64 only (not supported)":"none: enemies are drawn as 3D models"));
 Com_Printf("settings: cl_simpleenemies %g, cl_simpleenemy_anim %g, cl_staticenemies %g, cl_simpleenemy_lod %g\n",
  cl_simpleenemies->value,cl_simpleenemy_anim->value,cl_staticenemies->value,cl_simpleenemy_lod->value);
 if(!cl_simpleenemies->value)Com_Printf("result: simple enemies off (3D models)\n");
 else if(!Duke_ModeDirectional(cl_simpleenemies->value))Com_Printf("result: 2D models (static, no directions)\n");
 else if(cl_staticenemies->value)Com_Printf("result: frozen enemies (cl_staticenemies), static sprites\n");
 else if(!Duke_RateValid(cl_simpleenemy_anim->value))Com_Printf("result: static directional sprites (bdgn_sprites.pak)\n");
 else if(sup<2)Com_Printf("result: this renderer has no animated sprites; static sprites are used\n");
 else Com_Printf("result: animated sprites, %s poses/s, %ix%i library wanted\n",Duke_RatePeriod(cl_simpleenemy_anim->value)==80?"12.5":"6.25",res,res);
 for(i=0;i<3;i++) {
  path=i==0?duke_bank_names[0].bank32:(i==1?duke_bank_names[0].bank:"sprites/enemies/e000.sp2");
  where=FS_WhereIs(path);
  Com_Printf("%-26s %s\n",i==0?"32x32 library:":(i==1?"64x64 library:":"static sprites:"),where?where:"NOT FOUND (install the PAK, then restart the game)");
 }
 if(!cl.refresh_prepped){Com_Printf("no map loaded: load a map for the per-model check\n");return;}
 for(i=1;i<MAX_MODELS && cl.configstrings[CS_MODELS+i][0];i++)
  for(j=0;j<(int)(sizeof(duke_bank_names)/sizeof(duke_bank_names[0]));j++)
   if(!Q_stricmp(cl.configstrings[CS_MODELS+i],(char *)duke_bank_names[j].model)) {
    path=duke_bank_names[j].bank32;
    if(FS_WhereIs(path))known++;else{missing++;if(missing<=8)Com_Printf("  missing bank: %s (%s)\n",path,duke_bank_names[j].model);}
   }
 Com_Printf("map models with an animation bank: %i found, %i missing in the %ix%i library; duke_stats shows what was drawn\n",known,missing,res,res);
}

/* BDGN 3.51q: 25 poses per second was removed; a saved cl_simpleenemy_anim 3 becomes 1 (12.5/s), once */
void CL_MigrateSpriteRate(void)
{
 int to=Duke_ModeMigrate(cl_simpleenemies->value);
 if(to>=0){	/* BDGN 3.51s: the 8-direction modes were removed */
  Com_Printf("BDGN: cl_simpleenemies %g (8 directions) no longer exists; set to %i (4 directions)\n",cl_simpleenemies->value,to);
  Cvar_SetValue("cl_simpleenemies",(float)to);
 }
 to=Duke_RateMigrate(cl_simpleenemy_anim->value);
 if(to<0)return;
 Cvar_SetValue("cl_simpleenemy_anim",(float)to);
 Com_Printf("BDGN: cl_simpleenemy_anim 3 (25 poses/s) no longer exists; set to 1 (12.5 poses/s)\n");
}
static void CL_RefreshSimpleSprites(void)
{
 int i;
 CL_MigrateSpriteRate();
 if(!cl.refresh_prepped)return;
 if(simple_item_last_mode!=(int)cl_simpleitems->value)CL_RegisterSimpleItems();
 if(simple_enemy_last_signature!=CL_SimpleSpriteSignature()) {
  CL_RegisterSimpleEnemies();
  for(i=0;i<MAX_CLIENTS;i++)if(cl.configstrings[CS_PLAYERSKINS+i][0])CL_LoadClientinfo(&cl.clientinfo[i],cl.configstrings[CS_PLAYERSKINS+i]);
  CL_LoadClientinfo(&cl.baseclientinfo,"unnamed\\male/grunt");
 }
}

static qboolean CL_SimpleItem (entity_t *ent, const entity_state_t *state, int effects)
{
 struct model_s *sprite;
 if ((int)cl_simpleitems->value != 1 || state->modelindex <= 0 || state->modelindex >= MAX_MODELS
  || state->modelindex == 255 || (effects & (EF_GIB|EF_GREENGIB))) return false;
 sprite = simple_item_models[state->modelindex];
 if (!sprite) return false;
 /* A commander head can also be a gib/decor model; only its rotating pickup is eligible. */
 if (strncmp (cl.configstrings[CS_MODELS+state->modelindex], "models/monsters/", 16) == 0 && !(effects & EF_ROTATE)) return false;
 ent->model = sprite;
 ent->skin = NULL;
 ent->skinnum = 0;
 ent->frame = ent->oldframe = 0;
 ent->backlerp = 0;
 ent->flags = RF_FULLBRIGHT;
 CL_SpriteLight (ent);
 ent->alpha = 1;
 VectorClear (ent->angles);
	/* 3.51t: the sprite's own origin_x/origin_y put the picture where the model stood (no fixed lift) */
 VectorCopy (ent->origin, ent->oldorigin);
 return true;
}



extern	struct model_s	*cl_mod_powerscreen;

//PGM
int	vidref_val;
//PGM

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

#if 0

typedef struct
{
	int		modelindex;
	int		num; // entity number
	int		effects;
	vec3_t	origin;
	vec3_t	oldorigin;
	vec3_t	angles;
	qboolean present;
} projectile_t;

#define	MAX_PROJECTILES	64
projectile_t	cl_projectiles[MAX_PROJECTILES];

void CL_ClearProjectiles (void)
{
	int i;

	for (i = 0; i < MAX_PROJECTILES; i++) {
//		if (cl_projectiles[i].present)
//			Com_DPrintf("PROJ: %d CLEARED\n", cl_projectiles[i].num);
		cl_projectiles[i].present = false;
	}
}

/*
=====================
CL_ParseProjectiles

Flechettes are passed as efficient temporary entities
=====================
*/
void CL_ParseProjectiles (void)
{
	int		i, c, j;
	byte	bits[8];
	byte	b;
	projectile_t	pr;
	int lastempty = -1;
	qboolean old = false;

	c = MSG_ReadByte (&net_message);
	for (i=0 ; i<c ; i++)
	{
		bits[0] = MSG_ReadByte (&net_message);
		bits[1] = MSG_ReadByte (&net_message);
		bits[2] = MSG_ReadByte (&net_message);
		bits[3] = MSG_ReadByte (&net_message);
		bits[4] = MSG_ReadByte (&net_message);
		pr.origin[0] = ( ( bits[0] + ((bits[1]&15)<<8) ) <<1) - 4096;
		pr.origin[1] = ( ( (bits[1]>>4) + (bits[2]<<4) ) <<1) - 4096;
		pr.origin[2] = ( ( bits[3] + ((bits[4]&15)<<8) ) <<1) - 4096;
		VectorCopy(pr.origin, pr.oldorigin);

		if (bits[4] & 64)
			pr.effects = EF_BLASTER;
		else
			pr.effects = 0;

		if (bits[4] & 128) {
			old = true;
			bits[0] = MSG_ReadByte (&net_message);
			bits[1] = MSG_ReadByte (&net_message);
			bits[2] = MSG_ReadByte (&net_message);
			bits[3] = MSG_ReadByte (&net_message);
			bits[4] = MSG_ReadByte (&net_message);
			pr.oldorigin[0] = ( ( bits[0] + ((bits[1]&15)<<8) ) <<1) - 4096;
			pr.oldorigin[1] = ( ( (bits[1]>>4) + (bits[2]<<4) ) <<1) - 4096;
			pr.oldorigin[2] = ( ( bits[3] + ((bits[4]&15)<<8) ) <<1) - 4096;
		}

		bits[0] = MSG_ReadByte (&net_message);
		bits[1] = MSG_ReadByte (&net_message);
		bits[2] = MSG_ReadByte (&net_message);

		pr.angles[0] = 360*bits[0]/256;
		pr.angles[1] = 360*bits[1]/256;
		pr.modelindex = bits[2];

		b = MSG_ReadByte (&net_message);
		pr.num = (b & 0x7f);
		if (b & 128) // extra entity number byte
			pr.num |= (MSG_ReadByte (&net_message) << 7);

		pr.present = true;

		// find if this projectile already exists from previous frame 
		for (j = 0; j < MAX_PROJECTILES; j++) {
			if (cl_projectiles[j].modelindex) {
				if (cl_projectiles[j].num == pr.num) {
					// already present, set up oldorigin for interpolation
					if (!old)
						VectorCopy(cl_projectiles[j].origin, pr.oldorigin);
					cl_projectiles[j] = pr;
					break;
				}
			} else
				lastempty = j;
		}

		// not present previous frame, add it
		if (j == MAX_PROJECTILES) {
			if (lastempty != -1) {
				cl_projectiles[lastempty] = pr;
			}
		}
	}
}

/*
=============
CL_LinkProjectiles

=============
*/
void CL_AddProjectiles (void)
{
	int		i, j;
	projectile_t	*pr;
	entity_t		ent;

	memset (&ent, 0, sizeof(ent));

	for (i=0, pr=cl_projectiles ; i < MAX_PROJECTILES ; i++, pr++)
	{
		// grab an entity to fill in
		if (pr->modelindex < 1)
			continue;
		if (!pr->present) {
			pr->modelindex = 0;
			continue; // not present this frame (it was in the previous frame)
		}

		ent.model = cl.model_draw[pr->modelindex];

		// interpolate origin
		for (j=0 ; j<3 ; j++)
		{
			ent.origin[j] = ent.oldorigin[j] = pr->oldorigin[j] + cl.lerpfrac * 
				(pr->origin[j] - pr->oldorigin[j]);

		}

		if (pr->effects & EF_BLASTER)
			CL_BlasterTrail (pr->oldorigin, ent.origin);
		CL_AddEffectLight (pr->origin, 200, 1, 1, 0, CL_GENERIC_WEAPON_LIGHT);

		VectorCopy (pr->angles, ent.angles);
		V_AddEntity (&ent);
	}
}
#endif

/*
=================
CL_ParseEntityBits

Returns the entity number and the header bits
=================
*/
int	bitcounts[32];	/// just for protocol profiling
int CL_ParseEntityBits (unsigned *bits)
{
	unsigned	b, total;
	int			i;
	int			number;

	total = MSG_ReadByte (&net_message);
	if (total & U_MOREBITS1)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<8;
	}
	if (total & U_MOREBITS2)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<16;
	}
	if (total & U_MOREBITS3)
	{
		b = MSG_ReadByte (&net_message);
		total |= b<<24;
	}

	// count the bits for net profiling
	for (i=0 ; i<32 ; i++)
		if (total&(1<<i))
			bitcounts[i]++;

	if (total & U_NUMBER16)
		number = MSG_ReadShort (&net_message);
	else
		number = MSG_ReadByte (&net_message);

	*bits = total;

	return number;
}

/*
==================
CL_ParseDelta

Can go from either a baseline or a previous packet_entity
==================
*/
void CL_ParseDelta (entity_state_t *from, entity_state_t *to, int number, int bits)
{
	// set everything to the state we are delta'ing from
	*to = *from;

	VectorCopy (from->origin, to->old_origin);
	to->number = number;

	if (bits & U_MODEL)
		to->modelindex = MSG_ReadByte (&net_message);
	if (bits & U_MODEL2)
		to->modelindex2 = MSG_ReadByte (&net_message);
	if (bits & U_MODEL3)
		to->modelindex3 = MSG_ReadByte (&net_message);
	if (bits & U_MODEL4)
		to->modelindex4 = MSG_ReadByte (&net_message);
		
	if (bits & U_FRAME8)
		to->frame = MSG_ReadByte (&net_message);
	if (bits & U_FRAME16)
		to->frame = MSG_ReadShort (&net_message);

	if ((bits & U_SKIN8) && (bits & U_SKIN16))		//used for laser colors
		to->skinnum = MSG_ReadLong(&net_message);
	else if (bits & U_SKIN8)
		to->skinnum = MSG_ReadByte(&net_message);
	else if (bits & U_SKIN16)
		to->skinnum = MSG_ReadShort(&net_message);

	if ( (bits & (U_EFFECTS8|U_EFFECTS16)) == (U_EFFECTS8|U_EFFECTS16) )
		to->effects = MSG_ReadLong(&net_message);
	else if (bits & U_EFFECTS8)
		to->effects = MSG_ReadByte(&net_message);
	else if (bits & U_EFFECTS16)
		to->effects = MSG_ReadShort(&net_message);

	if ( (bits & (U_RENDERFX8|U_RENDERFX16)) == (U_RENDERFX8|U_RENDERFX16) )
		to->renderfx = MSG_ReadLong(&net_message);
	else if (bits & U_RENDERFX8)
		to->renderfx = MSG_ReadByte(&net_message);
	else if (bits & U_RENDERFX16)
		to->renderfx = MSG_ReadShort(&net_message);

	if (bits & U_ORIGIN1)
		to->origin[0] = MSG_ReadCoord (&net_message);
	if (bits & U_ORIGIN2)
		to->origin[1] = MSG_ReadCoord (&net_message);
	if (bits & U_ORIGIN3)
		to->origin[2] = MSG_ReadCoord (&net_message);
		
	if (bits & U_ANGLE1)
		to->angles[0] = MSG_ReadAngle(&net_message);
	if (bits & U_ANGLE2)
		to->angles[1] = MSG_ReadAngle(&net_message);
	if (bits & U_ANGLE3)
		to->angles[2] = MSG_ReadAngle(&net_message);

	if (bits & U_OLDORIGIN)
		MSG_ReadPos (&net_message, to->old_origin);

	if (bits & U_SOUND)
		to->sound = MSG_ReadByte (&net_message);

	if (bits & U_EVENT)
		to->event = MSG_ReadByte (&net_message);
	else
		to->event = 0;

	if (bits & U_SOLID)
		to->solid = MSG_ReadShort (&net_message);
}

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
void CL_DeltaEntity (frame_t *frame, int newnum, entity_state_t *old, int bits)
{
	centity_t	*ent;
	entity_state_t	*state;

	ent = &cl_entities[newnum];

	state = &cl_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES-1)];
	cl.parse_entities++;
	frame->num_entities++;

	CL_ParseDelta (old, state, newnum, bits);

	// some data changes will force no lerping
	if (state->modelindex != ent->current.modelindex
		|| state->modelindex2 != ent->current.modelindex2
		|| state->modelindex3 != ent->current.modelindex3
		|| state->modelindex4 != ent->current.modelindex4
		|| fabs(state->origin[0] - ent->current.origin[0]) > 512
		|| fabs(state->origin[1] - ent->current.origin[1]) > 512
		|| fabs(state->origin[2] - ent->current.origin[2]) > 512
		|| state->event == EV_PLAYER_TELEPORT
		|| state->event == EV_OTHER_TELEPORT
		)
	{
		CL_Gore_ClearActorMarks(newnum);
		ent->serverframe = -99;
	}

	if (ent->serverframe != cl.frame.serverframe - 1)
	{	// wasn't in last update, so initialize some things
		if (!state->solid || state->event == EV_PLAYER_TELEPORT ||
		    state->modelindex != ent->current.modelindex)
			CL_Gore_ClearActorMarks(newnum);
		ent->rocket_trail_step = 0;
		ent->trailcount = 1024;		// for diminishing rocket / grenade trails
		// duplicate the current state so lerping doesn't hurt anything
		ent->prev = *state;
		if (state->event == EV_OTHER_TELEPORT)
		{
			VectorCopy (state->origin, ent->prev.origin);
			VectorCopy (state->origin, ent->lerp_origin);
		}
		else
		{
			VectorCopy (state->old_origin, ent->prev.origin);
			VectorCopy (state->old_origin, ent->lerp_origin);
		}
	}
	else
	{	// shuffle the last state to previous
		ent->prev = ent->current;
	}

	ent->serverframe = cl.frame.serverframe;
	ent->current = *state;
}

/*
==================
CL_ParsePacketEntities

An svc_packetentities has just been parsed, deal with the
rest of the data stream.
==================
*/
void CL_ParsePacketEntities (frame_t *oldframe, frame_t *newframe)
{
	int			newnum;
	int			bits;
	entity_state_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parse_entities = cl.parse_entities;
	newframe->num_entities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	if (!oldframe)
		oldnum = 99999;
	else
	{
		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while (1)
	{
		newnum = CL_ParseEntityBits (&bits);
		if (newnum >= MAX_EDICTS)
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: bad number:%i", newnum);

		if (net_message.readcount > net_message.cursize)
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: end of message");

		if (!newnum)
			break;

		while (oldnum < newnum)
		{	// one or more entities from the old packet are unchanged
			if (cl_shownet->value == 3)
				Com_Printf ("   unchanged: %i\n", oldnum);
			CL_DeltaEntity (newframe, oldnum, oldstate, 0);
			
			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}

		if (bits & U_REMOVE)
		{	// the entity present in oldframe is not in the current frame
			if (cl_shownet->value == 3)
				Com_Printf ("   remove: %i\n", newnum);
			if (oldnum != newnum)
				Com_Printf ("U_REMOVE: oldnum != newnum\n");

			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum == newnum)
		{	// delta from previous state
			if (cl_shownet->value == 3)
				Com_Printf ("   delta: %i\n", newnum);
			CL_DeltaEntity (newframe, newnum, oldstate, bits);

			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum > newnum)
		{	// delta from baseline
			if (cl_shownet->value == 3)
				Com_Printf ("   baseline: %i\n", newnum);
			CL_DeltaEntity (newframe, newnum, &cl_entities[newnum].baseline, bits);
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while (oldnum != 99999)
	{	// one or more entities from the old packet are unchanged
		if (cl_shownet->value == 3)
			Com_Printf ("   unchanged: %i\n", oldnum);
		CL_DeltaEntity (newframe, oldnum, oldstate, 0);
		
		oldindex++;

		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &cl_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}



/*
===================
CL_ParsePlayerstate
===================
*/
void CL_ParsePlayerstate (frame_t *oldframe, frame_t *newframe)
{
	int			flags;
	player_state_t	*state;
	int			i;
	int			statbits;

	state = &newframe->playerstate;

	// clear to old value before delta parsing
	if (oldframe)
		*state = oldframe->playerstate;
	else
		memset (state, 0, sizeof(*state));

	flags = MSG_ReadShort (&net_message);

	//
	// parse the pmove_state_t
	//
	if (flags & PS_M_TYPE)
		state->pmove.pm_type = MSG_ReadByte (&net_message);

	if (flags & PS_M_ORIGIN)
	{
		state->pmove.origin[0] = MSG_ReadShort (&net_message);
		state->pmove.origin[1] = MSG_ReadShort (&net_message);
		state->pmove.origin[2] = MSG_ReadShort (&net_message);
	}

	if (flags & PS_M_VELOCITY)
	{
		state->pmove.velocity[0] = MSG_ReadShort (&net_message);
		state->pmove.velocity[1] = MSG_ReadShort (&net_message);
		state->pmove.velocity[2] = MSG_ReadShort (&net_message);
	}

	if (flags & PS_M_TIME)
		state->pmove.pm_time = MSG_ReadByte (&net_message);

	if (flags & PS_M_FLAGS)
		state->pmove.pm_flags = MSG_ReadByte (&net_message);

	if (flags & PS_M_GRAVITY)
		state->pmove.gravity = MSG_ReadShort (&net_message);

	if (flags & PS_M_DELTA_ANGLES)
	{
		state->pmove.delta_angles[0] = MSG_ReadShort (&net_message);
		state->pmove.delta_angles[1] = MSG_ReadShort (&net_message);
		state->pmove.delta_angles[2] = MSG_ReadShort (&net_message);
	}

	if (cl.attractloop)
		state->pmove.pm_type = PM_FREEZE;		// demo playback

	//
	// parse the rest of the player_state_t
	//
	if (flags & PS_VIEWOFFSET)
	{
		state->viewoffset[0] = MSG_ReadChar (&net_message) * 0.25;
		state->viewoffset[1] = MSG_ReadChar (&net_message) * 0.25;
		state->viewoffset[2] = MSG_ReadChar (&net_message) * 0.25;
	}

	if (flags & PS_VIEWANGLES)
	{
		state->viewangles[0] = MSG_ReadAngle16 (&net_message);
		state->viewangles[1] = MSG_ReadAngle16 (&net_message);
		state->viewangles[2] = MSG_ReadAngle16 (&net_message);
	}

	if (flags & PS_KICKANGLES)
	{
		state->kick_angles[0] = MSG_ReadChar (&net_message) * 0.25;
		state->kick_angles[1] = MSG_ReadChar (&net_message) * 0.25;
		state->kick_angles[2] = MSG_ReadChar (&net_message) * 0.25;
	}

	if (flags & PS_WEAPONINDEX)
	{
		state->gunindex = MSG_ReadByte (&net_message);
	}

	if (flags & PS_WEAPONFRAME)
	{
		state->gunframe = MSG_ReadByte (&net_message);
		state->gunoffset[0] = MSG_ReadChar (&net_message)*0.25;
		state->gunoffset[1] = MSG_ReadChar (&net_message)*0.25;
		state->gunoffset[2] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[0] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[1] = MSG_ReadChar (&net_message)*0.25;
		state->gunangles[2] = MSG_ReadChar (&net_message)*0.25;
	}

	if (flags & PS_BLEND)
	{
		state->blend[0] = MSG_ReadByte (&net_message)/255.0;
		state->blend[1] = MSG_ReadByte (&net_message)/255.0;
		state->blend[2] = MSG_ReadByte (&net_message)/255.0;
		state->blend[3] = MSG_ReadByte (&net_message)/255.0;
	}

	if (flags & PS_FOV)
		state->fov = MSG_ReadByte (&net_message);

	if (flags & PS_RDFLAGS)
		state->rdflags = MSG_ReadByte (&net_message);

	// parse stats
	statbits = MSG_ReadLong (&net_message);
	for (i=0 ; i<MAX_STATS ; i++)
		if (statbits & (1<<i) )
			state->stats[i] = MSG_ReadShort(&net_message);
}


/*
==================
CL_FireEntityEvents

==================
*/
void CL_FireEntityEvents (frame_t *frame)
{
	entity_state_t		*s1;
	int					pnum, num;

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		num = (frame->parse_entities + pnum)&(MAX_PARSE_ENTITIES-1);
		s1 = &cl_parse_entities[num];
		if (s1->event)
			CL_EntityEvent (s1);

		// EF_TELEPORTER acts like an event, but is not cleared each frame
		if (s1->effects & EF_TELEPORTER)
			CL_TeleporterParticles (s1);
	}
}


/*
================
CL_ParseFrame
================
*/
void CL_ParseFrame (void)
{
	int			cmd;
	int			len;
	frame_t		*old;

	memset (&cl.frame, 0, sizeof(cl.frame));

#if 0
	CL_ClearProjectiles(); // clear projectiles for new frame
#endif

	cl.frame.serverframe = MSG_ReadLong (&net_message);
	cl.frame.deltaframe = MSG_ReadLong (&net_message);
	cl.frame.servertime = cl.frame.serverframe*100;

	// BIG HACK to let old demos continue to work
	if (cls.serverProtocol != 26)
		cl.surpressCount = MSG_ReadByte (&net_message);

	if (cl_shownet->value == 3)
		Com_Printf ("   frame:%i  delta:%i\n", cl.frame.serverframe,
		cl.frame.deltaframe);

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if (cl.frame.deltaframe <= 0)
	{
		cl.frame.valid = true;		// uncompressed frame
		old = NULL;
		cls.demowaiting = false;	// we can start recording now
	}
	else
	{
		old = &cl.frames[cl.frame.deltaframe & UPDATE_MASK];
		if (!old->valid)
		{	// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		}
		if (old->serverframe != cl.frame.deltaframe)
		{	// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		}
		else if (cl.parse_entities - old->parse_entities > MAX_PARSE_ENTITIES-128)
		{
			Com_Printf ("Delta parse_entities too old.\n");
		}
		else
			cl.frame.valid = true;	// valid delta parse
	}

	// clamp time 
	if (cl.time > cl.frame.servertime)
	{
		cl.time = cl.frame.servertime;
	}
	else if (cl.time < cl.frame.servertime - 100)
	{
		cl.time = cl.frame.servertime - 100;
	}

	// read areabits
	len = MSG_ReadByte (&net_message);
	if (len < 0 || len > (int)sizeof(cl.frame.areabits))	/* BDGN 3.59f: id copied up to 255 bytes into a 32-byte array */
		Com_Error (ERR_DROP, "CL_ParseFrame: bad areabits length %i", len);
	MSG_ReadData (&net_message, &cl.frame.areabits, len);

	// read playerinfo
	cmd = MSG_ReadByte (&net_message);
	SHOWNET(svc_strings[cmd]);
	if (cmd != svc_playerinfo)
		Com_Error (ERR_DROP, "CL_ParseFrame: not playerinfo");
	CL_ParsePlayerstate (old, &cl.frame);

	// read packet entities
	cmd = MSG_ReadByte (&net_message);
	SHOWNET(svc_strings[cmd]);
	if (cmd != svc_packetentities)
		Com_Error (ERR_DROP, "CL_ParseFrame: not packetentities");
	CL_ParsePacketEntities (old, &cl.frame);

#if 0
	if (cmd == svc_packetentities2)
		CL_ParseProjectiles();
#endif

	// save the frame off in the backup array for later delta comparisons
	cl.frames[cl.frame.serverframe & UPDATE_MASK] = cl.frame;

	if (cl.frame.valid)
	{
		// getting a valid frame message ends the connection process
		if (cls.state != ca_active)
		{
			cls.state = ca_active;
			cl.force_refdef = true;
			cl.predicted_origin[0] = cl.frame.playerstate.pmove.origin[0]*0.125;
			cl.predicted_origin[1] = cl.frame.playerstate.pmove.origin[1]*0.125;
			cl.predicted_origin[2] = cl.frame.playerstate.pmove.origin[2]*0.125;
			VectorCopy (cl.frame.playerstate.viewangles, cl.predicted_angles);
			if (cls.disable_servercount != cl.servercount
				&& cl.refresh_prepped)
				SCR_EndLoadingPlaque ();	// get rid of loading plaque
		}
		cl.sound_prepped = true;	// can start mixing ambient sounds
	
		// fire entity events
		CL_FireEntityEvents (&cl.frame);
		CL_CheckPredictionError ();
	}
}

/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARMS

==========================================================================
*/

struct model_s *S_RegisterSexedModel (entity_state_t *ent, char *base)
{
	int				n;
	char			*p;
	struct model_s	*mdl;
	char			model[MAX_QPATH];
	char			buffer[MAX_QPATH];

	// determine what model the client is using
	model[0] = 0;
	n = CS_PLAYERSKINS + ent->number - 1;
	if (cl.configstrings[n][0])
	{
		p = strchr(cl.configstrings[n], '\\');
		if (p)
		{
			p += 1;
			strcpy(model, p);
			p = strchr(model, '/');
			if (p)
				*p = 0;
		}
	}
	// if we can't figure it out, they're male
	if (!model[0])
		strcpy(model, "male");

	Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", model, base+1);
	mdl = re.RegisterModel(buffer);
	if (!mdl) {
		// not found, try default weapon model
		Com_sprintf (buffer, sizeof(buffer), "players/%s/weapon.md2", model);
		mdl = re.RegisterModel(buffer);
		if (!mdl) {
			// no, revert to the male model
			Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", "male", base+1);
			mdl = re.RegisterModel(buffer);
			if (!mdl) {
				// last try, default male weapon.md2
				Com_sprintf (buffer, sizeof(buffer), "players/male/weapon.md2");
				mdl = re.RegisterModel(buffer);
			}
		} 
	}

	return mdl;
}

// PMM - used in shell code 
extern int Developer_searchpath (int who);
// pmm
/*
===============
CL_AddPacketEntities

===============
*/
/* Classify each model once, not each entity on every frame. */
static byte cl_decoration_types[MAX_MODELS];

/* Map metadata identifies rotating fans without guessing from moving doors. */
static byte cl_rotating_models[MAX_MODELS];
static qboolean cl_map_models_ready;
static int cl_frozen_rot_model[MAX_EDICTS];
static vec3_t cl_frozen_rot_angles[MAX_EDICTS];
static int cl_last_staticmap;
#define CL_MODEL_ACTOR 32
#define CL_MODEL_MAPANIM 64

static void CL_BuildMapAnimationCache (void)
{
	char *data, *token;
	char key[64], classname[MAX_QPATH], model[MAX_QPATH];
	int i;

	memset (cl_rotating_models, 0, sizeof(cl_rotating_models));
	data = CM_EntityString ();
	while (data && *data)
	{
		token = COM_Parse (&data);
		if (strcmp(token,"{")) break;
		classname[0] = model[0] = 0;
		while (data)
		{
			token = COM_Parse (&data);
			if (!data || !strcmp(token,"}")) break;
			Com_sprintf(key,sizeof(key),"%s",token);
			token = COM_Parse (&data);
			if (!data) break;
			if (!strcmp(key,"classname")) Com_sprintf(classname,sizeof(classname),"%s",token);
			else if (!strcmp(key,"model")) Com_sprintf(model,sizeof(model),"%s",token);
		}
		if (!strcmp(classname,"func_rotating") && model[0] == '*')
			for (i=1; i<MAX_MODELS; i++)
				if (!strcmp(model,cl.configstrings[CS_MODELS+i])) cl_rotating_models[i] = 1;
	}
	cl_map_models_ready = true;
}

void CL_ClearDecorationCache (void)
{
	memset (cl_decoration_types, 0, sizeof(cl_decoration_types));
	memset (cl_frozen_rot_model, 0, sizeof(cl_frozen_rot_model));
	cl_map_models_ready = false;
	cl_last_staticmap = 0;
}

static int CL_DecorationType (int modelindex)
{
	char *name;
	int type;

	if (modelindex <= 0 || modelindex >= MAX_MODELS)
		return 0;
	if (!cl_decoration_types[modelindex])
	{
		name = cl.configstrings[CS_MODELS+modelindex];
		type = 8;	// known, not a decoration
		if (!Q_stricmp (name, "models/objects/banner/tris.md2")) type |= 1;
		if (!Q_stricmp (name, "models/deadbods/dude/tris.md2")) type |= 2;
		if (!Q_stricmp (name, "models/objects/dmspot/tris.md2")) type |= 4;
		if (!Q_stricmp (name, "sprites/s_bfg3.sp2")) type |= 16;
		if (!Q_strncasecmp(name,"models/monsters/",16)) type |= CL_MODEL_ACTOR;
		if (!Q_stricmp(name,"models/objects/banner/tris.md2") ||
			!Q_stricmp(name,"models/objects/black/tris.md2") ||
			!Q_stricmp(name,"models/objects/satellite/tris.md2") ||
			!Q_strncasecmp(name,"models/objects/minelite/",24) ||
			!Q_strncasecmp(name,"models/mapobjects/",18) ||
			!Q_strncasecmp(name,"models/items/",13) ||
			!Q_strncasecmp(name,"models/weapons/g_",17)) type |= CL_MODEL_MAPANIM;
		cl_decoration_types[modelindex] = (byte)type;
	}
	return cl_decoration_types[modelindex] & (23 | CL_MODEL_ACTOR | CL_MODEL_MAPANIM);
}

static void CL_StaticEntityAnimation (entity_t *ent, entity_state_t *state, unsigned int effects)
{
	int type, model, number;
	qboolean freeze;

	if (!cl_staticenemies->value && !cl_staticmap->value) return;
	model = state->modelindex;
	number = state->number;
	type = CL_DecorationType (model);
	freeze = cl_staticenemies->value && ((type & CL_MODEL_ACTOR) || model == 255);
	if (cl_staticmap->value && ((type & CL_MODEL_MAPANIM) ||
		(model > 0 && model < MAX_MODELS && cl.configstrings[CS_MODELS+model][0] == '*' &&
		(effects & (EF_ANIM01|EF_ANIM23|EF_ANIM_ALL|EF_ANIM_ALLFAST))))) freeze = true;
	if (cl_staticmap->value && (type & CL_MODEL_MAPANIM)) ent->flags &= ~RF_GLOW;
	if (freeze)
	{
		ent->frame = ent->oldframe = 0;
		ent->backlerp = 0;
	}
	if (cl_staticmap->value && model > 0 && model < MAX_MODELS && cl_rotating_models[model] &&
		number > 0 && number < MAX_EDICTS)
	{
		if (cl_frozen_rot_model[number] != model)
		{
			cl_frozen_rot_model[number] = model;
			VectorCopy(ent->angles,cl_frozen_rot_angles[number]);
		}
		VectorCopy(cl_frozen_rot_angles[number],ent->angles);
	}
}

void CL_AddPacketEntities (frame_t *frame)
{
	entity_t			ent;
	entity_state_t		*s1;
	float				autorotate;
	int					i;
	int					pnum;
	centity_t			*cent;
	int					autoanim;
	clientinfo_t		*ci;
	unsigned int		effects, renderfx;
	int				hide_decorations;
	qboolean		simple_projectile;
	qboolean		simple_enemy;
	qboolean		animated_enemy;
	int animated_flags;
	float animated_alpha;

	CL_RefreshSimpleSprites();
	if (cl_last_staticmap != (cl_staticmap->value != 0))
	{
		memset(cl_frozen_rot_model,0,sizeof(cl_frozen_rot_model));
		cl_last_staticmap = cl_staticmap->value != 0;
	}
	if (cl_staticmap->value && !cl_map_models_ready) CL_BuildMapAnimationCache ();

	hide_decorations = (cl_nobanners->value ? 1 : 0) |
		(cl_nodecorpses->value ? 2 : 0) | (cl_nospawnpads->value ? 4 : 0) |
		(cl_static_bfg->value == 1 ? 16 : 0);	/* BDGN 3.51v: sprites mode (2) shows the BFG explosion as a sprite */

	// bonus items rotate at a fixed rate
	autorotate = (cl_noitemrotate->value || cl_staticmap->value) ? 0 : anglemod(cl.time/10);

	// brush models can auto animate their frames
	autoanim = 2*cl.time/1000;

	memset (&ent, 0, sizeof(ent));

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		s1 = &cl_parse_entities[(frame->parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)];

		cent = &cl_entities[s1->number];

		effects = s1->effects;
		renderfx = s1->renderfx;

		/* Cosmetic models only: never remove server entities or collision boxes. */
		if (hide_decorations && (CL_DecorationType (s1->modelindex) & hide_decorations))
		{
			VectorCopy (s1->origin, cent->lerp_origin);
			continue;
		}

		/* Hide heads and existing gibs too, including on remote servers. */
		if (cl_gibmode->value >= 2 && (effects & (EF_GIB|EF_GREENGIB)))
		{
			VectorCopy (s1->origin, cent->lerp_origin);
			continue;
		}

		/* 3.60i hotfix: only cosmetic gibs/debris may be distance-culled.
		   Pickups (EF_ROTATE), enemies and other gameplay entities always remain. */
		if (s1->modelindex && s1->modelindex != 255 && !s1->solid &&
		    ((effects & (EF_GIB|EF_GREENGIB)) ||
		     !strncmp (cl.configstrings[CS_MODELS+s1->modelindex], "models/objects/debris", 21) ||
		     !strncmp (cl.configstrings[CS_MODELS+s1->modelindex], "models/objects/gibs", 19)) &&
		    CL_ObjectTooFar (s1->origin))
		{
			VectorCopy (s1->origin, cent->lerp_origin);
			continue;
		}

			// set frame
		if (effects & EF_ANIM01)
			ent.frame = autoanim & 1;
		else if (effects & EF_ANIM23)
			ent.frame = 2 + (autoanim & 1);
		else if (effects & EF_ANIM_ALL)
			ent.frame = autoanim;
		else if (effects & EF_ANIM_ALLFAST)
			ent.frame = cl.time / 100;
		else
			ent.frame = s1->frame;

		// quad and pent can do different things on client
		if (effects & EF_PENT)
		{
			effects &= ~EF_PENT;
			effects |= EF_COLOR_SHELL;
			renderfx |= RF_SHELL_RED;
		}

		if (effects & EF_QUAD)
		{
			effects &= ~EF_QUAD;
			effects |= EF_COLOR_SHELL;
			renderfx |= RF_SHELL_BLUE;
		}
//======
// PMM
		if (effects & EF_DOUBLE)
		{
			effects &= ~EF_DOUBLE;
			effects |= EF_COLOR_SHELL;
			renderfx |= RF_SHELL_DOUBLE;
		}

		if (effects & EF_HALF_DAMAGE)
		{
			effects &= ~EF_HALF_DAMAGE;
			effects |= EF_COLOR_SHELL;
			renderfx |= RF_SHELL_HALF_DAM;
		}
// pmm
//======
		ent.oldframe = cent->prev.frame;
		ent.backlerp = 1.0 - cl.lerpfrac;

		if (renderfx & (RF_FRAMELERP|RF_BEAM))
		{	// step origin discretely, because the frames
			// do the animation properly
			VectorCopy (cent->current.origin, ent.origin);
			VectorCopy (cent->current.old_origin, ent.oldorigin);
		}
		else
		{	// interpolate origin
			for (i=0 ; i<3 ; i++)
			{
				ent.origin[i] = ent.oldorigin[i] = cent->prev.origin[i] + cl.lerpfrac * 
					(cent->current.origin[i] - cent->prev.origin[i]);
			}
		}

		// create a new entity
	
		// tweak the color of beams
		if ( renderfx & RF_BEAM )
		{	// the four beam colors are encoded in 32 bits of skinnum (hack)
			ent.alpha = 0.30;
			ent.skinnum = (s1->skinnum >> ((rand() % 4)*8)) & 0xff;
			ent.model = NULL;
		}
		else
		{
			// set skin
			if (s1->modelindex == 255)
			{	// use custom player skin
				ent.skinnum = 0;
				ci = &cl.clientinfo[s1->skinnum & 0xff];
				ent.skin = ci->skin;
				ent.model = ci->model;
				if (!ent.skin || !ent.model)
				{
					ent.skin = cl.baseclientinfo.skin;
					ent.model = cl.baseclientinfo.model;
				}

//============
//PGM
				if (renderfx & RF_USE_DISGUISE)
				{
					if(!strncmp((char *)ent.skin, "players/male", 12))
					{
						ent.skin = re.RegisterSkin ("players/male/disguise.pcx");
						ent.model = re.RegisterModel ("players/male/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/female", 14))
					{
						ent.skin = re.RegisterSkin ("players/female/disguise.pcx");
						ent.model = re.RegisterModel ("players/female/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/cyborg", 14))
					{
						ent.skin = re.RegisterSkin ("players/cyborg/disguise.pcx");
						ent.model = re.RegisterModel ("players/cyborg/tris.md2");
					}
				}
//PGM
//============
			}
			else
			{
				ent.skinnum = s1->skinnum;
				ent.skin = NULL;
				ent.model = cl.model_draw[s1->modelindex];
			}
		}

		// only used for black hole model right now, FIXME: do better
		if (renderfx == RF_TRANSLUCENT)
			ent.alpha = 0.70;

		// render effects (fullbright, translucent, etc)
		if ((effects & EF_COLOR_SHELL))
			ent.flags = 0;	// renderfx go on color shell entity
		else
			ent.flags = renderfx;

		// calculate angles
		if (effects & EF_ROTATE)
		{	// some bonus items auto-rotate
			ent.angles[0] = 0;
			ent.angles[1] = autorotate;
			ent.angles[2] = 0;
		}
		// RAFAEL
		else if (effects & EF_SPINNINGLIGHTS)
		{
			ent.angles[0] = 0;
			ent.angles[1] = anglemod(cl_staticmap->value ? 0 : cl.time/2) + s1->angles[1];
			ent.angles[2] = 180;
			{
				vec3_t forward;
				vec3_t start;

				AngleVectors (ent.angles, forward, NULL, NULL);
				VectorMA (ent.origin, 64, forward, start);
				V_AddLight (start, 100, 1, 0, 0);
			}
		}
		else
		{	// interpolate angles
			float	a1, a2;

			for (i=0 ; i<3 ; i++)
			{
				a1 = cent->current.angles[i];
				a2 = cent->prev.angles[i];
				ent.angles[i] = LerpAngle (a2, a1, cl.lerpfrac);
			}
		}

		if (s1->modelindex == 255)
			CL_WaterEntry(s1->number, frame->serverframe, cent->prev.origin, s1->origin);

		if (s1->number == cl.playernum+1)
		{
			ent.flags |= RF_VIEWERMODEL;	// only draw from mirrors
			// FIXME: still pass to refresh

			if (effects & EF_FLAG1)
				V_AddLight (ent.origin, 225, 1.0, 0.1, 0.1);
			else if (effects & EF_FLAG2)
				V_AddLight (ent.origin, 225, 0.1, 0.1, 1.0);
			else if (effects & EF_TAGTRAIL)						//PGM
				V_AddLight (ent.origin, 225, 1.0, 1.0, 0.0);	//PGM
			else if (effects & EF_TRACKERTRAIL)					//PGM
				V_AddLight (ent.origin, 225, -1.0, -1.0, -1.0);	//PGM

			continue;
		}

		// if set to invisible, skip
		if (!s1->modelindex)
			continue;

		if (effects & EF_BFG)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.alpha = 0.30;
		}

		// RAFAEL
		if (effects & EF_PLASMA)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.alpha = 0.6;
		}

		if (effects & EF_SPHERETRANS)
		{
			ent.flags |= RF_TRANSLUCENT;
			// PMM - *sigh*  yet more EF overloading
			if (effects & EF_TRACKERTRAIL)
				ent.alpha = 0.6;
			else
				ent.alpha = 0.3;
		}
//pmm

		if (CL_SimpleItem (&ent, s1, effects))
		{
			V_AddEntity (&ent);
			VectorCopy (ent.origin, cent->lerp_origin);
			continue; /* a single camera-facing sprite, with no shell or animated attachments */
		}

		simple_enemy = CL_SimpleEnemy (&ent, s1, effects);
		animated_enemy = (ent.flags & RF_DUKEANIM) != 0;
		animated_flags = ent.flags; animated_alpha = ent.alpha;
		if (!simple_enemy && (cl_staticenemies->value || cl_staticmap->value))
			CL_StaticEntityAnimation (&ent, s1, effects);
		if (CL_FX3D_HideEntity (&ent))
		{	/* BDGN 3.55t: the 3D BFG explosion replaces the game's explosion sprite */
			VectorCopy (ent.origin, cent->lerp_origin);
			continue;
		}
		if (effects & (EF_GIB|EF_GREENGIB))
			CL_Gore_GibEntity (&ent, s1->number);	/* BDGN 3.55y: HD gib model, extra pieces, blood where it lands */
		simple_projectile = CL_SimpleProjectile (&ent, effects);
		if (!simple_projectile && CL_Gore_BFGBall (&ent, effects, s1->number, cent->lerp_origin))
		{	/* BDGN 3.55z: the flying BFG ball in 3D (with "3D BFG explosion") */
			VectorCopy (ent.origin, cent->lerp_origin);
			continue;
		}
		if (!simple_projectile && CL_FX3D_Bolt (&ent, effects, s1->number, cent->lerp_origin))
		{	/* BDGN 3.55t: comet bolt with its own tail, embers and light */
			VectorCopy (ent.origin, cent->lerp_origin);
			continue;
		}

		if (s1->modelindex > 0 && s1->modelindex < MAX_MODELS && cl.model_far[s1->modelindex] &&
		    ent.model == cl.model_draw[s1->modelindex] && CL_FarModel (ent.origin))
			ent.model = cl.model_far[s1->modelindex];	/* BDGN 3.57m: far: the simplest model */

		// add to refresh list
		V_AddEntity (&ent);
		CL_Gore_ActorMarks(&ent,s1);


		// color shells generate a seperate entity for the main model
		if (!simple_enemy && !simple_projectile && (effects & EF_COLOR_SHELL))
		{
			// PMM - at this point, all of the shells have been handled
			// if we're in the rogue pack, set up the custom mixing, otherwise just
			// keep going
//			if(Developer_searchpath(2) == 2)
//			{
				// all of the solo colors are fine.  we need to catch any of the combinations that look bad
				// (double & half) and turn them into the appropriate color, and make double/quad something special
				if (renderfx & RF_SHELL_HALF_DAM)
				{
					if(Developer_searchpath(2) == 2)
					{
						// ditch the half damage shell if any of red, blue, or double are on
						if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_DOUBLE))
							renderfx &= ~RF_SHELL_HALF_DAM;
					}
				}

				if (renderfx & RF_SHELL_DOUBLE)
				{
					if(Developer_searchpath(2) == 2)
					{
						// lose the yellow shell if we have a red, blue, or green shell
						if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_GREEN))
							renderfx &= ~RF_SHELL_DOUBLE;
						// if we have a red shell, turn it to purple by adding blue
						if (renderfx & RF_SHELL_RED)
							renderfx |= RF_SHELL_BLUE;
						// if we have a blue shell (and not a red shell), turn it to cyan by adding green
						else if (renderfx & RF_SHELL_BLUE)
							// go to green if it's on already, otherwise do cyan (flash green)
							if (renderfx & RF_SHELL_GREEN)
								renderfx &= ~RF_SHELL_BLUE;
							else
								renderfx |= RF_SHELL_GREEN;
					}
				}
//			}
			// pmm
			ent.flags = renderfx | RF_TRANSLUCENT;
			ent.alpha = 0.30;
			V_AddEntity (&ent);
		}

		ent.skin = NULL;		// never use a custom skin on others
		ent.skinnum = 0;
		ent.flags = 0;
		ent.alpha = 0;

		// duplicate for linked models
		if ((!simple_enemy || animated_enemy) && !simple_projectile && s1->modelindex2)
		{
			if (s1->modelindex2 == 255)
			{	// custom weapon
				ci = &cl.clientinfo[s1->skinnum & 0xff];
				i = (s1->skinnum >> 8); // 0 is default weapon model
				if (!cl_vwep->value || i > MAX_CLIENTWEAPONMODELS - 1)
					i = 0;
				ent.model = ci->weaponmodel[i];
				if (!ent.model) {
					if (i != 0)
						ent.model = ci->weaponmodel[0];
					if (!ent.model)
						ent.model = cl.baseclientinfo.weaponmodel[0];
				}
			}
			else
				ent.model = cl.model_draw[s1->modelindex2];

			// PMM - check for the defender sphere shell .. make it translucent
			// replaces the previous version which used the high bit on modelindex2 to determine transparency
			if (!Q_strcasecmp (cl.configstrings[CS_MODELS+(s1->modelindex2)], "models/items/shell/tris.md2"))
			{
				ent.alpha = 0.32;
				ent.flags = RF_TRANSLUCENT;
			}
			// pmm

			if (animated_enemy) { ent.flags = (animated_flags & (RF_TRANSLUCENT|RF_SIMPLEBRIGHT|RF_LITSPRITE)) | RF_DUKEANIM | RF_DUKEWEAPON; ent.alpha = animated_alpha; }
			V_AddEntity (&ent);

			//PGM - make sure these get reset.
			ent.flags = 0;
			ent.alpha = 0;
			//PGM
		}
		if (!simple_projectile && s1->modelindex3)
		{
			ent.model = cl.model_draw[s1->modelindex3];
			V_AddEntity (&ent);
		}
		if (!simple_projectile && s1->modelindex4)
		{
			ent.model = cl.model_draw[s1->modelindex4];
			V_AddEntity (&ent);
		}

		if ( effects & EF_POWERSCREEN )
		{
			ent.model = cl_mod_powerscreen;
			ent.oldframe = 0;
			ent.frame = 0;
			ent.flags |= (RF_TRANSLUCENT | RF_SHELL_GREEN);
			ent.alpha = 0.30;
			V_AddEntity (&ent);
		}

		// add automatic particle trails
		if ( (effects&~EF_ROTATE) )
		{
			CL_HDFXProjectile (ent.origin, effects);	/* BDGN 3.55s: HD glow on rockets, blaster, hyperblaster, BFG */
			if (effects & EF_ROCKET)
			{
				CL_RocketTrail (cent->lerp_origin, ent.origin, cent);
				CL_AddEffectLight (ent.origin, 200, 1, 1, 0, effects);
			}
			// PGM - Do not reorder EF_BLASTER and EF_HYPERBLASTER. 
			// EF_BLASTER | EF_TRACKER is a special case for EF_BLASTER2... Cheese!
			else if (effects & EF_BLASTER)
			{
//				CL_BlasterTrail (cent->lerp_origin, ent.origin);
//PGM
				/* v3.51o: blaster trails remain; the light filter stays active. */
				if (effects & EF_TRACKER)	// lame... problematic?
				{
					CL_BlasterTrail2 (cent->lerp_origin, ent.origin);
					CL_AddEffectLight (ent.origin, 200, 0, 1, 0, effects);		
				}
				else
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
					CL_AddEffectLight (ent.origin, 200, 1, 1, 0, effects);
				}
//PGM
			}
			else if (effects & EF_HYPERBLASTER)
			{
				if (effects & EF_TRACKER)						// PGM	overloaded for blaster2.
					CL_AddEffectLight (ent.origin, 200, 0, 1, 0, effects);		// PGM
				else											// PGM
					CL_AddEffectLight (ent.origin, 200, 1, 1, 0, effects);
			}
			else if (effects & EF_GIB)
			{
				if (!CL_FASTFX())
					CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);
			}
			else if (effects & EF_GRENADE)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);
			}
			else if (effects & EF_FLIES)
			{
				CL_FlyEffect (cent, ent.origin);
			}
			else if (effects & EF_BFG)
			{
				static int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};

				if (effects & EF_ANIM_ALLFAST)
				{
					if (!simple_projectile) CL_BfgParticles (&ent);
					i = 200;
				}
				else
				{
					i = bfg_lightramp[s1->frame];
				}
				CL_AddEffectLight (ent.origin, i, 0, 1, 0, effects);
			}
			// RAFAEL
			else if (effects & EF_TRAP)
			{
				ent.origin[2] += 32;
				CL_TrapParticles (&ent);
				i = (rand()%100) + 100;
				CL_AddEffectLight (ent.origin, i, 1, 0.8, 0.1, effects);
			}
			else if (effects & EF_FLAG1)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, 242);
				CL_AddEffectLight (ent.origin, 225, 1, 0.1, 0.1, effects);
			}
			else if (effects & EF_FLAG2)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, 115);
				CL_AddEffectLight (ent.origin, 225, 0.1, 0.1, 1, effects);
			}
//======
//ROGUE
			else if (effects & EF_TAGTRAIL)
			{
				CL_TagTrail (cent->lerp_origin, ent.origin, 220);
				CL_AddEffectLight (ent.origin, 225, 1.0, 1.0, 0.0, effects);
			}
			else if (effects & EF_TRACKERTRAIL)
			{
				if (effects & EF_TRACKER)
				{
					float intensity;

					intensity = 50 + (500 * (sin(cl.time/500.0) + 1.0));
					// FIXME - check out this effect in rendition
					if(vidref_val == VIDREF_GL)
						CL_AddEffectLight (ent.origin, intensity, -1.0, -1.0, -1.0, effects);
					else
						CL_AddEffectLight (ent.origin, -1.0 * intensity, 1.0, 1.0, 1.0, effects);
					}
				else
				{
					CL_Tracker_Shell (cent->lerp_origin);
					CL_AddEffectLight (ent.origin, 155, -1.0, -1.0, -1.0, effects);
				}
			}
			else if (effects & EF_TRACKER)
			{
				CL_TrackerTrail (cent->lerp_origin, ent.origin, 0);
				// FIXME - check out this effect in rendition
				if(vidref_val == VIDREF_GL)
					CL_AddEffectLight (ent.origin, 200, -1, -1, -1, effects);
				else
					CL_AddEffectLight (ent.origin, -200, 1, 1, 1, effects);
			}
//ROGUE
//======
			// RAFAEL
			else if (effects & EF_GREENGIB)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);				
			}
			// RAFAEL
			else if (effects & EF_IONRIPPER)
			{
				CL_IonripperTrail (cent->lerp_origin, ent.origin);
				CL_AddEffectLight (ent.origin, 100, 1, 0.5, 0.5, effects);
			}
			// RAFAEL
			else if (effects & EF_BLUEHYPERBLASTER)
			{
				CL_AddEffectLight (ent.origin, 200, 0, 0, 1, effects);
			}
			// RAFAEL
			else if (effects & EF_PLASMA)
			{
				if ((effects & EF_ANIM_ALLFAST) && !CL_FASTFX())
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
				}
				CL_AddEffectLight (ent.origin, 130, 1, 0.5, 0.5, effects);
			}
		}

		VectorCopy (ent.origin, cent->lerp_origin);
	}
}



/*
==============
CL_AddViewWeapon
==============
*/
void CL_AddViewWeapon (player_state_t *ps, player_state_t *ops)
{
	entity_t	gun;		// view model
	int			i;

	// allow the gun to be completely removed
	if (!cl_gun->value)
		return;

	// don't draw gun if in wide angle view
	//if (ps->fov > 90)
	if (ps->fov > 106)		// lets draw the gun at higher fov
		return;

	// dont draw the gun at lower fov so it wont show up when using Zoom
	if (ps->fov < 75)		// hide the gun if zoom is used
		return;

	memset (&gun, 0, sizeof(gun));

	if (gun_model)
		gun.model = gun_model;	// development tool
	else
		gun.model = cl.model_draw[ps->gunindex];
	if (!gun.model)
		return;

	// set up gun position
	for (i=0 ; i<3 ; i++)
	{
		gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i]
			+ cl.lerpfrac * (ps->gunoffset[i] - ops->gunoffset[i]);
		gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle (ops->gunangles[i],
			ps->gunangles[i], cl.lerpfrac);
	}
	if (Cvar_VariableValue ("cl_yshear") && Cvar_VariableValue ("r_yshear_support"))
		gun.angles[PITCH] -= cl.refdef.viewangles[PITCH];	/* BDGN 3.60i: Duke Nukem 3D view: the weapon stays level */

	if (gun_frame)
	{
		gun.frame = gun_frame;	// development tool
		gun.oldframe = gun_frame;	// development tool
	}
	else
	{
		gun.frame = ps->gunframe;
		if (gun.frame == 0)
			gun.oldframe = 0;	// just changed weapons, don't lerp from old
		else
			gun.oldframe = ops->gunframe;
	}

	gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
	if (ps->gunindex>0 && ps->gunindex<MAX_MODELS &&
	    !Q_stricmp(cl.configstrings[CS_MODELS+ps->gunindex],"models/weapons/v_chain/tris.md2"))
		gun.lightstyle=CL_ChaingunHeatLevel();
	else if (ps->gunindex>0 && ps->gunindex<MAX_MODELS &&
	    !Q_stricmp(cl.configstrings[CS_MODELS+ps->gunindex],"models/weapons/v_machn/tris.md2"))
		gun.lightstyle=CL_MachinegunHeatLevel();	/* BDGN 3.56z */
	gun.backlerp = 1.0 - cl.lerpfrac;
	VectorCopy (gun.origin, gun.oldorigin);	// don't lerp at all
	CL_FX3D_ViewShotgunSmoke(&gun,ps->gunindex);

	/* BDGN 3.51q: 2D first-person weapon. Only what is drawn changes: the same model, frame numbers, origin and angles
	   as the 3D weapon (bob and kick included); the renderer draws the pre-rendered picture of the frame instead of
	   the model, or the model when no picture bank exists. Frames change on a 12.5 or 6.25 per second clock (the
	   server frame is held between ticks); Simple Weapon Mode shows one static picture. BDGN 3.51r: the shared
	   2D muzzle flash is removed. */
	if ((cl_classic2d->value || cl_weaponsprites->value) && Cvar_VariableValue ("r_weaponsprite_support") >= 1 && !gun_frame)
	{
		static struct model_s *held_model;
		static int held_bucket = -1, held_frame;
		int period = CL_LOWEND_SPRITES() ? 80 : 160, bucket;
		bucket = cl.time / period;
		if (gun.model != held_model || bucket != held_bucket || gun.frame == 0 || bucket < held_bucket)
		{
			held_model = gun.model; held_bucket = bucket; held_frame = gun.frame;
		}
		gun.frame = gun.oldframe = held_frame;
		gun.backlerp = 0;
		gun.flags |= RF_WEAPONSPRITE;
		if (cl_weapon_simple->value && !CL_LOWEND_SPRITES())
			gun.flags |= RF_WEAPONSIMPLE;
		V_AddEntity (&gun);
		return;
	}
	V_AddEntity (&gun);
}


/*
===============
CL_CalcViewValues

Sets cl.refdef view values
===============
*/
void CL_CalcViewValues (void)
{
	int			i;
	float		lerp, backlerp;
	centity_t	*ent;
	frame_t		*oldframe;
	player_state_t	*ps, *ops;

	// find the previous frame to interpolate from
	ps = &cl.frame.playerstate;
	i = (cl.frame.serverframe - 1) & UPDATE_MASK;
	oldframe = &cl.frames[i];
	if (oldframe->serverframe != cl.frame.serverframe-1 || !oldframe->valid)
		oldframe = &cl.frame;		// previous frame was dropped or involid
	ops = &oldframe->playerstate;

	// see if the player entity was teleported this frame
	if ( abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256*8
		|| abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256*8
		|| abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256*8)
		ops = ps;		// don't interpolate

	ent = &cl_entities[cl.playernum+1];
	lerp = cl.lerpfrac;

	// calculate the origin
	if ((cl_predict->value) && !(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
	{	// use predicted values
		unsigned	delta;

		backlerp = 1.0 - lerp;
		for (i=0 ; i<3 ; i++)
		{
			cl.refdef.vieworg[i] = cl.predicted_origin[i] + ops->viewoffset[i] 
				+ cl.lerpfrac * (ps->viewoffset[i] - ops->viewoffset[i])
				- backlerp * cl.prediction_error[i];
		}

		// smooth out stair climbing
		delta = cls.realtime - cl.predicted_step_time;
		if (delta < 100)
			cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01;
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			cl.refdef.vieworg[i] = ops->pmove.origin[i]*0.125 + ops->viewoffset[i] 
				+ lerp * (ps->pmove.origin[i]*0.125 + ps->viewoffset[i] 
				- (ops->pmove.origin[i]*0.125 + ops->viewoffset[i]) );
	}

	// if not running a demo or on a locked frame, add the local angle movement
	if ( cl.frame.playerstate.pmove.pm_type < PM_DEAD )
	{	// use predicted values
		for (i=0 ; i<3 ; i++)
			cl.refdef.viewangles[i] = cl.predicted_angles[i];
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			cl.refdef.viewangles[i] = LerpAngle (ops->viewangles[i], ps->viewangles[i], lerp);
	}

	for (i=0 ; i<3 ; i++)
		cl.refdef.viewangles[i] += LerpAngle (ops->kick_angles[i], ps->kick_angles[i], lerp);

	AngleVectors (cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

	// interpolate field of view
	cl.refdef.fov_x = ops->fov + lerp * (ps->fov - ops->fov);

	// don't interpolate blend color
	for (i=0 ; i<4 ; i++)
		cl.refdef.blend[i] = ps->blend[i];

	// add the weapon
	CL_AddViewWeapon (ps, ops);
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CL_AddEntities (void)
{
	if (cls.state != ca_active)
		return;

	if (cl.time > cl.frame.servertime)
	{
		if (cl_showclamp->value)
			Com_Printf ("high clamp %i\n", cl.time - cl.frame.servertime);
		cl.time = cl.frame.servertime;
		cl.lerpfrac = 1.0;
	}
	else if (cl.time < cl.frame.servertime - 100)
	{
		if (cl_showclamp->value)
			Com_Printf ("low clamp %i\n", cl.frame.servertime-100 - cl.time);
		cl.time = cl.frame.servertime - 100;
		cl.lerpfrac = 0;
	}
	else
		cl.lerpfrac = 1.0 - (cl.frame.servertime - cl.time) * 0.01;

	if (cl_timedemo->value)
		cl.lerpfrac = 1.0;

//	CL_AddPacketEntities (&cl.frame);
//	CL_AddTEnts ();
//	CL_AddParticles ();
//	CL_AddDLights ();
//	CL_AddLightStyles ();

	CL_CalcViewValues ();
	// PMM - moved this here so the heat beam has the right values for the vieworg, and can lock the beam to the gun
	CL_AddPacketEntities (&cl.frame);
#if 0
	CL_AddProjectiles ();
#endif
	CL_AddTEnts ();
	CL_AddSpriteTrails ();	/* BDGN 3.51ah (test) */
	CL_AddParticles ();
	CL_AddDLights ();
	CL_AddLightStyles ();
}



/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin (int ent, vec3_t org)
{
	centity_t	*old;

	if (ent < 0 || ent >= MAX_EDICTS)
		Com_Error (ERR_DROP, "CL_GetEntitySoundOrigin: bad ent");
	old = &cl_entities[ent];
	VectorCopy (old->lerp_origin, org);

	// FIXME: bmodel issues...
}
