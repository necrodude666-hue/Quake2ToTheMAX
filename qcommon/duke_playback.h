/* Pre-generated animation playback. No model rasterization in the renderer.
 *
 * BDGN 3.51q:
 * - BDGN 3.52h: one library, 32x32 (sprites/anim32/, bdgn_anim32.pak). The 64x64 library (bdgn_anim.pak, 3.51p)
 *   and cl_simpleenemy_res are gone. r_duke_res_avail (1 = installed, 0 = not) is set when the renderer starts,
 *   so the menu and bdgn_bench see it before any animated sprite has been drawn (3.52g: only after the first one).
 * - the cvars are looked up once (3.51p: two Cvar_Get name searches per drawn enemy and frame)
 * - model+skin -> bank through a small cache (3.51p: up to 139 string compares per enemy and frame for a model
 *   without a bank, i.e. every frame for every fallback); identical banks are shared (duke_banks.h bank32)
 * - R_DukeCull: conservative sphere test before any bank, decode or upload work. The sphere holds every pose of
 *   the two frames used, so a sprite never disappears at the screen edge (3.51p used the 3D model's box, which is
 *   smaller than the camera-facing sprite of a lying body or a long weapon)
 * - diagnostics: missing library/bank/chunk and fallback to the 3D model are counted by reason and reported once
 *   per bank and map (not per frame); duke_stats shows the active library, banks, cache, evictions, memory and
 *   fallbacks, "duke_stats reset" starts a new count, r_duke_stats holds the same numbers for bdgn_bench.
 * Counters start at renderer start and at every map load (R_DukeBeginRegistration); a vid_restart is a renderer
 * start. New PAK files are only found after a full game restart (vid_restart keeps the file system). */
#include "duke_anim.h"
#include "duke_modes.h"
#include "duke_banks.h"
#define DUKE_BANKS (sizeof(duke_bank_names)/sizeof(duke_bank_names[0]))
typedef struct { byte *data;int size,epoch,attempt,frames,tile,warned;int *slots[2];byte failed[512]; } duke_bank_t;
typedef struct {
 int bank,record,mask;unsigned used;image_t image;duke_record_t pose;
#ifndef BDGN_DUKE_GL
 byte pixels[DUKE_TILE*DUKE_TILE];
#endif
} duke_tile_t;
static duke_bank_t duke_banks[DUKE_BANKS];
/* BDGN 3.51att: ref_gl keeps up to 1024 decoded tiles (was 512: 32x32 enemies were evicted and uploaded again);
   r_duke_cache sets how many are used (64-1024, default 1024), never more than 32 atlas pages (512 at 64x64).
   ref_soft keeps 512 (its tiles hold pixels in RAM). */
#ifdef BDGN_DUKE_GL
#define DUKE_TILE_SLOTS 1024
#else
#define DUKE_TILE_SLOTS DUKE_SLOTS
#endif
static duke_tile_t duke_tiles[DUKE_TILE_SLOTS];
/* BDGN 3.59r: 1 once a tile has been filled. ref_soft's tiles hold their pixels (2.1 MB); clearing them at every
   renderer start touched every page, so the whole table took real RAM even when no 2D sprite was ever drawn (low RAM
   computers). They are cleared only when used. */
static int duke_tiles_dirty;
static int Duke_SlotLimit(int t);
static unsigned duke_clock,duke_hits,duke_misses,duke_uploads,duke_chunk_reads,duke_expands;
static unsigned duke_evictions,duke_draws,duke_culled,duke_chunk_evictions;
static unsigned duke_fb_nobank,duke_fb_missing,duke_fb_invalid,duke_fb_chunk;	/* drawn as the original 3D model, by reason */
static int duke_res_active,duke_res_avail=-1,duke_res_note,duke_warnings,duke_lookup_seq=1;
static const char *duke_since="renderer start";
static cvar_t *duke_cv_mode,*duke_cv_anim,*duke_cv_stats,*duke_cv_directions,*duke_cv_classic,*duke_cv_lowend;
static int duke_extra_lowram;
/* BDGN 3.58w (owner 2026-11-21): r_spritetint 1 = the 2D enemies in one blue tone (Wolfenstein 3D mode sets it). Each
   palette colour is mapped to the palette colour nearest to a blue of the same brightness (light stays light, dark
   stays dark), once per palette, at the moment a sprite picture is unpacked into the cache: no cost per frame. Only
   colours below 224 are used, so no picture gets a fullbright colour in the software renderer. */
static cvar_t *duke_cv_tint;
static byte duke_tint[256];
static int duke_tint_ready, duke_tint_last=-1;
static void Duke_TintTable(void)
{
 int i,j,best;float l,tr,tg,tb,d,bd;
 for(i=0;i<256;i++) {
  const byte *p=(const byte *)&d_8to24table[i];
  l=0.30f*p[0]+0.59f*p[1]+0.11f*p[2];
  tr=l*0.40f;tg=l*0.50f;tb=l*1.30f+12.0f;if(tb>255)tb=255;
  best=0;bd=1e30f;
  for(j=0;j<224;j++) {
   const byte *q=(const byte *)&d_8to24table[j];
   d=(q[0]-tr)*(q[0]-tr)*0.30f+(q[1]-tg)*(q[1]-tg)*0.59f+(q[2]-tb)*(q[2]-tb)*0.60f;
   if(d<bd){bd=d;best=j;}
  }
  duke_tint[i]=(byte)best;
 }
 duke_tint[255]=255;
 duke_tint_ready=1;
}
/* BDGN 3.52h: always (no setting), beyond this view depth (fov 90 on 4:3; the 3-poses-per-second tier of
   client/duke_lod.h) only the front and back pictures are used: fewer different tiles, fewer uploads */
#define DUKE_TWODIR_DEPTH 1024.0f
#define DUKE_CHUNKS 64
#define DUKE_CHUNK_BYTES (8*1024*1024)
typedef struct {int bank,group,size;byte *data;unsigned used;} duke_chunk_t;
typedef struct {int bank,frame;byte data[DUKE_FRAME_BYTES];unsigned used;} duke_frame_t;
static duke_chunk_t duke_chunks[DUKE_CHUNKS];
static duke_frame_t duke_frames[32];
static int duke_chunk_bytes;
static int duke_bank_bytes,duke_serial;
#define DUKE_LOOKUP 64
typedef struct {const model_t *model;const image_t *skin;int bank,seq,warned;} duke_lookup_t;
static duke_lookup_t duke_lookup[DUKE_LOOKUP];
static short duke_canon[2][DUKE_BANKS];	/* [0] 32x32, [1] 64x64: first entry with the same bank file */
#ifdef BDGN_DUKE_GL
extern void GL_DukeTile(int slot,byte *pixels,image_t *image,int tile);
extern void GL_DukeDelete(void);
extern int GL_DukeAtlasBytes(void);
extern void R_DrawSpriteModel(entity_t *e);
static byte duke_decode[DUKE_TILE*DUKE_TILE];
/* BDGN 3.52h: solid classic sprites are queued and drawn together after the solid entities (R_DukeFlush, from
   R_DrawEntitiesOnList), sorted by atlas page: texture environment, alpha test and colour reset once per frame and
   one glBindTexture + glBegin per page, instead of all of that for every sprite (R_DrawSpriteModel). Translucent,
   depth-hacked and weapon entities are still drawn at once. A tile is never overwritten while it is queued: before
   a tile used in this batch is evicted, the batch is drawn. */
#define DUKE_QUEUE 256
typedef struct {int texnum;float s0,t0,s1,t1;vec3_t origin;short w,h,ox,oy;byte rgb[3];} duke_queued_t;
static duke_queued_t duke_queue[DUKE_QUEUE];
static int duke_queued;
static unsigned duke_queue_clock,duke_batches,duke_batched;
void R_DukeFlush(void)
{
 int i,j,n=duke_queued,bound=-1;duke_queued_t tmp;const duke_queued_t *q;vec3_t p;
 if(!n)return;
 duke_queued=0;duke_batches++;duke_batched+=n;
 for(i=1;i<n;i++){	/* insertion sort by page; usually already grouped (gl_sortentities) */
  if(duke_queue[i-1].texnum<=duke_queue[i].texnum)continue;
  tmp=duke_queue[i];for(j=i;j>0 && duke_queue[j-1].texnum>tmp.texnum;j--)duke_queue[j]=duke_queue[j-1];duke_queue[j]=tmp;
 }
 GL_TexEnv(GL_MODULATE);qglEnable(GL_ALPHA_TEST);
 for(i=0;i<n;i++) {
  q=&duke_queue[i];
  if(q->texnum!=bound){if(bound>=0)qglEnd();GL_Bind(q->texnum);bound=q->texnum;qglBegin(GL_QUADS);}
  qglColor4f(q->rgb[0]*(1.0f/255),q->rgb[1]*(1.0f/255),q->rgb[2]*(1.0f/255),1);
  qglTexCoord2f(q->s0,q->t1);VectorMA(q->origin,-q->oy,vup,p);VectorMA(p,-q->ox,vright,p);qglVertex3fv(p);
  qglTexCoord2f(q->s0,q->t0);VectorMA(q->origin,q->h-q->oy,vup,p);VectorMA(p,-q->ox,vright,p);qglVertex3fv(p);
  qglTexCoord2f(q->s1,q->t0);VectorMA(q->origin,q->h-q->oy,vup,p);VectorMA(p,q->w-q->ox,vright,p);qglVertex3fv(p);
  qglTexCoord2f(q->s1,q->t1);VectorMA(q->origin,-q->oy,vup,p);VectorMA(p,q->w-q->ox,vright,p);qglVertex3fv(p);
 }
 qglEnd();
 qglDisable(GL_ALPHA_TEST);GL_TexEnv(GL_REPLACE);qglColor4f(1,1,1,1);
}
#endif
#define DUKE_MAX_WARNINGS 8
static void Duke_Warn(const char *fmt,const char *a,const char *b)
{
 if(duke_warnings>=DUKE_MAX_WARNINGS)return;
 duke_warnings++;
 ri.Con_Printf(PRINT_ALL,(char *)fmt,a,b);
 if(duke_warnings==DUKE_MAX_WARNINGS)ri.Con_Printf(PRINT_ALL,"BDGN sprites: further warnings for this map are only counted (duke_stats)\n");
}
/* BDGN 3.58y (owner 2026-11-21: the WWII uniform only in Wolfenstein 3D mode, not in classic 2d); 3.59b: every enemy
   on two legs (ranks by size: bigger enemy, higher rank), the soldier banks
   drawn from the WWII skins (sprites/anim32ww2/, bdgn_enemyskins.pak, _extra/enemy_skins/build_ww2_sprites.sh) when
   cl_enemyskins 1 and Wolfenstein 3D mode (cl_retromode 2/3) and the banks are installed */
static int duke_ww2, duke_ww2_avail=-1;
static signed char duke_ww2_has[512];	/* 3.59b: per bank, -1 not looked up, 0 no WWII bank, 1 there */
static char duke_ww2_path[MAX_QPATH];
static const char *Duke_BankPath(int i,int tile)
{
 const char *p=tile==64?duke_bank_names[i].bank:duke_bank_names[i].bank32;
 if(duke_ww2 && tile==32 && i<512 && !strncmp(p,"sprites/anim32/",15)) {	/* 3.59b: every enemy that has one */
  Com_sprintf(duke_ww2_path,sizeof(duke_ww2_path),"sprites/anim32ww2/%s",p+15);
  if(duke_ww2_has[i]<0) duke_ww2_has[i]=(signed char)(ri.FS_LoadFile(duke_ww2_path,NULL)>0);
  if(duke_ww2_has[i]) return duke_ww2_path;
 }
 return p;
}
static void Duke_Cvars(void)
{
 int i,j,r;
 duke_cv_mode=ri.Cvar_Get("cl_simpleenemies","0",CVAR_ARCHIVE);
 duke_cv_anim=ri.Cvar_Get("cl_simpleenemy_anim","0",CVAR_ARCHIVE);
 duke_cv_stats=ri.Cvar_Get("r_duke_stats","",0);
 duke_cv_directions=ri.Cvar_Get("cl_duke_directions","4",CVAR_ARCHIVE);
 duke_cv_classic=ri.Cvar_Get("cl_classic2d","0",CVAR_ARCHIVE);
 duke_cv_lowend=ri.Cvar_Get("cl_lowend_sprites","0",CVAR_ARCHIVE);
 duke_cv_tint=ri.Cvar_Get("r_spritetint","0",CVAR_ARCHIVE);	/* BDGN 3.58w */
 duke_extra_lowram=ri.Cvar_Get("bdgn_extra_lowram","0",0)->value!=0;
 for(r=0;r<2;r++)for(i=0;i<(int)DUKE_BANKS;i++) {
  duke_canon[r][i]=(short)i;
  for(j=0;j<i;j++)if(!strcmp(Duke_BankPath(j,r?64:32),Duke_BankPath(i,r?64:32))){duke_canon[r][i]=(short)j;break;}
 }
}
/* 1 = the 32x32 library is installed (FS_LoadFile with no buffer only returns the length) */
static int Duke_Available(void)
{
 int avail=0;
 if(ri.FS_LoadFile((char *)duke_bank_names[0].bank32,NULL)>0)avail|=1;
 ri.Cvar_SetValue("r_duke_res_avail",(float)avail);
 return avail;
}
static void Duke_FreeBank(int i)
{
 duke_bank_t *b=&duke_banks[i];int j;
#ifdef BDGN_DUKE_GL
 if(duke_queued)R_DukeFlush();	/* BDGN 3.52h: queued sprites may use this bank's tiles */
#endif
 if(duke_tiles_dirty)	/* 3.59r: an unused table is not read (its pages stay without RAM) */
  for(j=0;j<DUKE_TILE_SLOTS;j++)if(duke_tiles[j].used && duke_tiles[j].bank==i)duke_tiles[j].used=0;
 if(b->data){ri.FS_FreeFile(b->data);duke_bank_bytes-=b->size;}
 if(b->slots[0])free(b->slots[0]);if(b->slots[1])free(b->slots[1]);
 for(j=0;j<DUKE_CHUNKS;j++)if(duke_chunks[j].used && duke_chunks[j].bank==i){ri.FS_FreeFile(duke_chunks[j].data);duke_chunk_bytes-=duke_chunks[j].size;memset(&duke_chunks[j],0,sizeof(duke_chunks[j]));}
 for(j=0;j<32;j++)if(duke_frames[j].used && duke_frames[j].bank==i)duke_frames[j].used=0;
 memset(b,0,sizeof(*b));
}
static void Duke_ResetCounters(const char *since)
{
 duke_hits=duke_misses=duke_uploads=duke_chunk_reads=duke_expands=0;
 duke_evictions=duke_draws=duke_culled=duke_chunk_evictions=0;
 duke_fb_nobank=duke_fb_missing=duke_fb_invalid=duke_fb_chunk=0;
#ifdef BDGN_DUKE_GL
 duke_batches=duke_batched=0;
#endif
 duke_since=since;
}
/* BDGN 3.52h: always 32x32; one console line when bdgn_anim32.pak is missing */
static int Duke_ChooseRes(void)
{
 if(duke_res_avail<0)duke_res_avail=Duke_Available();
 if(!(duke_res_avail&1) && duke_res_note!=-1){duke_res_note=-1;ri.Con_Printf(PRINT_ALL,"BDGN sprites: animation library bdgn_anim32.pak not found; original 3D models are drawn. Install %s and restart the game (vid_restart does not reload PAK files).\n",duke_extra_lowram?"bdgn_anim32_fb.pak":"bdgn_anim32.pak");}
 return 32;
}
static void Duke_StatsString(char *out,int size)
{
 int i,tiles=0,banks=0;for(i=0;i<DUKE_TILE_SLOTS;i++)if(duke_tiles[i].used)tiles++;for(i=0;i<(int)DUKE_BANKS;i++)if(duke_banks[i].data)banks++;
 Com_sprintf(out,size,"res %i draws %u hits %u misses %u uploads %u evictions %u chunkreads %u chunkevictions %u expands %u culled %u banks %i tiles %i bankKB %i chunkKB %i texKB %i fb_nobank %u fb_missing %u fb_invalid %u fb_chunk %u",
  duke_res_active,duke_draws,duke_hits,duke_misses,duke_uploads,duke_evictions,duke_chunk_reads,duke_chunk_evictions,duke_expands,duke_culled,banks,tiles,duke_bank_bytes/1024,duke_chunk_bytes/1024,
#ifdef BDGN_DUKE_GL
  GL_DukeAtlasBytes()/1024,
#else
  tiles*duke_res_active*duke_res_active/1024,
#endif
  duke_fb_nobank,duke_fb_missing,duke_fb_invalid,duke_fb_chunk);
#ifdef BDGN_DUKE_GL
 {int n=(int)strlen(out);if(n<size)Com_sprintf(out+n,size-n," batched %u batches %u",duke_batched,duke_batches);}	/* BDGN 3.52h */
#endif
}
void R_DukeStats(void)
{
 int i,tiles=0,banks=0,failed=0,mode;char line[512];
 if(!duke_cv_mode)Duke_Cvars();
 if(ri.Cmd_Argc()>1 && !strcmp(ri.Cmd_Argv(1),"reset")){Duke_ResetCounters("duke_stats reset");ri.Con_Printf(PRINT_ALL,"BDGN sprites: counters reset\n");return;}
 for(i=0;i<DUKE_TILE_SLOTS;i++)if(duke_tiles[i].used)tiles++;
 for(i=0;i<(int)DUKE_BANKS;i++){if(duke_banks[i].data)banks++;else if(duke_banks[i].attempt)failed++;}
 Duke_StatsString(line,sizeof(line));ri.Cvar_Set("r_duke_stats",line);
 if(ri.Cmd_Argc()>1 && !strcmp(ri.Cmd_Argv(1),"quiet"))return;
 mode=(int)duke_cv_mode->value;
 if(duke_res_avail<0)duke_res_avail=Duke_Available();
 ri.Con_Printf(PRINT_ALL,"BDGN sprites (counted since %s):\n",duke_since);
 ri.Con_Printf(PRINT_ALL,"  mode: cl_simpleenemies %i, cl_simpleenemy_anim %g -> %s\n",mode,duke_cv_anim->value,
  !Duke_ModeDirectional(duke_cv_mode->value)?"animation off (needs cl_simpleenemies 3-4)":(!Duke_RateValid(duke_cv_anim->value)?"static sprites (no animation)":(Duke_RatePeriod(duke_cv_anim->value)==80?"animated 12.5 poses/s":"animated 6.25 poses/s")));
 ri.Con_Printf(PRINT_ALL,"  library: 32x32 bdgn_anim32.pak %s\n",(duke_res_avail&1)?"installed":"not installed");
 ri.Con_Printf(PRINT_ALL,"  sprites drawn %u, culled before work %u; tile hits %u, misses %u, uploads %u, evictions %u\n",duke_draws,duke_culled,duke_hits,duke_misses,duke_uploads,duke_evictions);
#ifdef BDGN_DUKE_GL
 ri.Con_Printf(PRINT_ALL,"  batched: %u sprites in %u batches (one state setup per batch)\n",duke_batched,duke_batches);
#endif
 ri.Con_Printf(PRINT_ALL,"  banks loaded %i (failed %i), chunk reads %u (evicted %u), decodes %u, tiles %i/%i\n",banks,failed,duke_chunk_reads,duke_chunk_evictions,duke_expands,tiles,Duke_SlotLimit(duke_res_active));
 ri.Con_Printf(PRINT_ALL,"  memory: banks %i KB, compressed cache %i KB, decoded strips %i KB, textures %s KB\n",duke_bank_bytes/1024,duke_chunk_bytes/1024,(int)sizeof(duke_frames)/1024,strstr(line,"texKB ")+6);
 ri.Con_Printf(PRINT_ALL,"  drawn as 3D model: no bank for model/skin %u, bank file missing %u, invalid/RAM limit %u, chunk missing %u\n",duke_fb_nobank,duke_fb_missing,duke_fb_invalid,duke_fb_chunk);
 if(!duke_draws && !duke_fb_nobank && !duke_fb_missing && !duke_fb_invalid && !duke_fb_chunk)
  ri.Con_Printf(PRINT_ALL,"  nothing drawn with animated sprites yet: check the mode line, that an enemy has been in view since the count started, and bdgn_spritecheck\n");
}
void R_DukeReset(void)
{
 int i;if(!duke_cv_mode)Duke_Cvars();
 for(i=0;i<(int)DUKE_BANKS;i++)Duke_FreeBank(i);
#ifdef BDGN_DUKE_GL
 GL_DukeDelete();
#endif
 if(duke_tiles_dirty){memset(duke_tiles,0,sizeof(duke_tiles));duke_tiles_dirty=0;}memset(duke_lookup,0,sizeof(duke_lookup));duke_lookup_seq++;
 duke_clock=0;duke_serial=0;duke_res_active=0;duke_warnings=0;
 duke_tint_ready=0;	/* BDGN 3.58w: built again from the palette of this renderer start */
 memset(duke_ww2_has,-1,sizeof(duke_ww2_has));	/* BDGN 3.59b: looked up again */
#ifdef BDGN_DUKE_GL
 duke_queued=0;
#endif
 duke_res_avail=Duke_Available();	/* BDGN 3.52h: at renderer start, sets r_duke_res_avail for the menu and bdgn_bench */
 Duke_ResetCounters("renderer start");
}
static int Duke_LoadBank(int i)
{
 duke_bank_t *b=&duke_banks[i];byte *data=NULL;int size,n,tile;
 if(b->data){b->epoch=registration_sequence;return 1;}
 if(b->attempt==registration_sequence)return 0;b->attempt=registration_sequence;
 size=ri.FS_LoadFile((char *)Duke_BankPath(i,duke_res_active),(void **)&data);
 if(size<0 || !data) {
  duke_fb_missing++;
  if(!b->warned){b->warned=1;Duke_Warn("BDGN sprites: %s missing for %s; original model drawn\n",Duke_BankPath(i,duke_res_active),duke_bank_names[i].model);}
  return 0;
 }
 tile=Duke_ValidBank(data,size);
 if(tile!=duke_res_active || size+((int *)data)[5]*2*(int)sizeof(int)>(duke_extra_lowram?1024*1024:DUKE_BANK_LIMIT)-duke_bank_bytes) {
  duke_fb_invalid++;
  if(!b->warned){b->warned=1;Duke_Warn("BDGN sprites: invalid bank or 24 MB RAM limit: %s%s; original model retained\n",Duke_BankPath(i,duke_res_active),"");}
  ri.FS_FreeFile(data);return 0;
 }
 n=((int *)data)[5];b->slots[0]=(int *)calloc(n,sizeof(int));b->slots[1]=(int *)calloc(n,sizeof(int));
 if(!b->slots[0] || !b->slots[1]){if(b->slots[0])free(b->slots[0]);if(b->slots[1])free(b->slots[1]);b->slots[0]=b->slots[1]=NULL;ri.FS_FreeFile(data);return 0;}
 b->data=data;b->size=size+n*2*(int)sizeof(int);b->frames=((int *)data)[2];b->tile=tile;b->epoch=registration_sequence;duke_bank_bytes+=b->size;return 1;
}
static byte *Duke_Frame(int b,int frame,int direction)
{
 int budget=duke_extra_lowram?512*1024:DUKE_CHUNK_BYTES;
 int i,slot=0,cs=-1,group=frame/8,size,key=frame*8+direction,fb=DUKE_FRAME_BYTES_T(duke_banks[b].tile);char name[MAX_QPATH];byte *data=NULL;
 const unsigned *record=(const unsigned *)(duke_banks[b].data+32)+key*3;
 const char *bankname=Duke_BankPath(b,duke_res_active);
 if(duke_banks[b].failed[group])return NULL;
 for(i=0;i<32;i++){if(duke_frames[i].used && duke_frames[i].bank==b && duke_frames[i].frame==key){duke_frames[i].used=++duke_clock;return duke_frames[i].data;}if(!duke_frames[i].used || duke_frames[i].used<duke_frames[slot].used)slot=i;}
 for(i=0;i<DUKE_CHUNKS;i++)if(duke_chunks[i].used && duke_chunks[i].bank==b && duke_chunks[i].group==group){cs=i;break;}
 if(cs<0) {
  Com_sprintf(name,sizeof(name),"%.*s_%03i.dsb",(int)strlen(bankname)-4,bankname,group);
  size=ri.FS_LoadFile(name,(void **)&data);
  if(!data || size<1){if(data)ri.FS_FreeFile(data);duke_banks[b].failed[group]=1;if(!duke_banks[b].warned){duke_banks[b].warned=1;Duke_Warn("BDGN sprites: %s missing or empty%s; original model drawn for those frames\n",name,"");}return NULL;}
  if(size>budget){ri.FS_FreeFile(data);return NULL;}
  while(duke_chunk_bytes+size>budget) {
   cs=-1;for(i=0;i<DUKE_CHUNKS;i++)if(duke_chunks[i].used && (cs<0 || duke_chunks[i].used<duke_chunks[cs].used))cs=i;
   if(cs<0){ri.FS_FreeFile(data);return NULL;}
   ri.FS_FreeFile(duke_chunks[cs].data);duke_chunk_bytes-=duke_chunks[cs].size;memset(&duke_chunks[cs],0,sizeof(duke_chunks[cs]));duke_chunk_evictions++;
  }
  cs=0;for(i=0;i<DUKE_CHUNKS;i++)if(!duke_chunks[i].used){cs=i;break;}else if(duke_chunks[i].used<duke_chunks[cs].used)cs=i;
  if(duke_chunks[cs].used){ri.FS_FreeFile(duke_chunks[cs].data);duke_chunk_bytes-=duke_chunks[cs].size;duke_chunk_evictions++;}
  duke_chunks[cs].data=data;duke_chunks[cs].size=size;duke_chunks[cs].bank=b;duke_chunks[cs].group=group;duke_chunk_bytes+=size;duke_chunk_reads++;
 }
 duke_chunks[cs].used=++duke_clock;
 if(record[0]>(unsigned)duke_chunks[cs].size || record[1]>(unsigned)duke_chunks[cs].size-record[0])return NULL;
 duke_frames[slot].used=0;
 if(!Duke_Expand(duke_chunks[cs].data+record[0],record[1],duke_frames[slot].data,fb))return NULL;
 duke_expands++;duke_frames[slot].bank=b;duke_frames[slot].frame=key;duke_frames[slot].used=++duke_clock;return duke_frames[slot].data;
}
void R_DukeBeginRegistration(void)
{
 int i;if(!duke_cv_mode)Duke_Cvars();
 for(i=0;i<(int)DUKE_BANKS;i++)Duke_FreeBank(i);
 memset(duke_lookup,0,sizeof(duke_lookup));duke_lookup_seq++;duke_warnings=0;
 Duke_ResetCounters("map load");
}
/* canonical bank for model+skin, -1 = none; cached per (model, skin) until the next map or flush */
static int Duke_FindBank(const model_t *model,const image_t *skin)
{
 unsigned h=((unsigned)((size_t)model>>3)*31u+(unsigned)((size_t)skin>>3))&(DUKE_LOOKUP-1);
 duke_lookup_t *l=&duke_lookup[h];int i,b=-1,r=duke_res_active==64;
 if(l->model==model && l->skin==skin && l->seq==duke_lookup_seq)return l->bank;
 {	/* BDGN 3.59b: a simpler model (cl_simplemodels, models/simple<n>/...) keeps the model's name but its skin has the
	   simple path; it was not found, so with simpler models on (Ultra Fast, PSYCHO, Doom / Wolfenstein 3D mode before
	   3.59b) the 2D enemies were drawn as simple 3D models. The skin is compared without the simple<n>/ part now. */
  const char *sn=skin->name;char plain[MAX_QPATH];
  if(!strncmp(sn,"models/simple",13) && sn[13]>='1' && sn[13]<='3' && sn[14]=='/') {
   Com_sprintf(plain,sizeof(plain),"models/%s",sn+15);sn=plain;
  }
  for(i=0;i<(int)DUKE_BANKS;i++)if(!strcmp(model->name,duke_bank_names[i].model) && !strcmp(sn,duke_bank_names[i].skin)){b=duke_canon[r][i];break;}
 }
 l->model=model;l->skin=skin;l->seq=duke_lookup_seq;l->bank=b;l->warned=0;
 return b;
}
static int Duke_Active(void)
{
 if(!duke_cv_mode)Duke_Cvars();
#ifdef BDGN_DUKE_GL
 if(!qglTexSubImage2D)return 0;
#endif
 if(!Duke_ModeDirectional(duke_cv_mode->value) || !Duke_RateValid(duke_cv_anim->value))return 0;
 if(!duke_res_active)duke_res_active=Duke_ChooseRes();
 return (duke_res_avail&1)!=0;
}
void R_DukeRegisterModel(model_t *model)
{
 int i,b;
 if(duke_extra_lowram || !model || model->type!=mod_alias || !Duke_Active())return;
 for(i=0;i<(int)DUKE_BANKS;i++)if(!strcmp(model->name,duke_bank_names[i].model)) {
  b=duke_canon[duke_res_active==64][i];
  if(Duke_LoadBank(b) && model->skins[0] && !strcmp(model->skins[0]->name,duke_bank_names[i].skin))Duke_Frame(b,0,0);
 }
}
static image_t *Duke_ModelSkin(model_t *model,entity_t *ent)
{
 dmdl_t *h=(dmdl_t *)model->extradata;int sk=ent->skinnum;
 if(ent->skin)return ent->skin;if(sk<0 || sk>=h->num_skins || sk>=MAX_MD2SKINS)sk=0;return model->skins[sk];
}
/* Conservative visibility for the camera-facing sprite: a sphere around the origin that holds both MD2 frames'
 * boxes (any pose between them, seen from any side) plus the 2-unit picture margin. 1 = outside the view. */
static int R_DukeCull(entity_t *e)
{
 const dmdl_t *h;const daliasframe_t *f;int k,c,frame;float r2=0,x,y,z,d,radius;
 if(!currentmodel || currentmodel->type!=mod_alias || !currentmodel->extradata)return 0;
 h=(const dmdl_t *)currentmodel->extradata;
 for(k=0;k<2;k++) {
  frame=k?e->oldframe:e->frame;if(frame<0 || frame>=h->num_frames)frame=0;
  f=(const daliasframe_t *)((const byte *)h+h->ofs_frames+frame*h->framesize);
  for(c=0;c<8;c++) {
   x=f->translate[0]+((c&1)?f->scale[0]*255:0);y=f->translate[1]+((c&2)?f->scale[1]*255:0);z=f->translate[2]+((c&4)?f->scale[2]*255:0);
   d=x*x+y*y+z*z;if(d>r2)r2=d;
  }
 }
 radius=(float)sqrt(r2)+4;
#ifdef BDGN_DUKE_GL
 for(k=0;k<r_numfrustumplanes;k++)if(DotProduct(e->origin,frustum[k].normal)-frustum[k].dist<-radius){duke_culled++;return 1;}
#else
 for(k=0;k<4;k++) {
  float len=(float)sqrt(DotProduct(view_clipplanes[k].normal,view_clipplanes[k].normal));
  if(len>0 && DotProduct(e->origin,view_clipplanes[k].normal)-view_clipplanes[k].dist<-radius*len){duke_culled++;return 1;}
 }
#endif
 return 0;
}
static int Duke_SlotLimit(int t)
{
#ifdef BDGN_DUKE_GL
 int n=(int)ri.Cvar_Get("r_duke_cache","1024",CVAR_ARCHIVE)->value,pages=t>0?(256/t)*(256/t)*32:DUKE_TILE_SLOTS;
 if(n<64)n=64;if(n>DUKE_TILE_SLOTS)n=DUKE_TILE_SLOTS;if(n>pages)n=pages;
 /* BDGN 3.59s: on a video card with 4 MB at most (vid_lowvram) the atlas keeps 4 pages = 1 MB at 32-bit, instead of up
    to 16 pages = 4 MB (1024 tiles of 32x32), which do not fit next to the frame buffer (TEORETISK ANALYS) */
 if(R_LowVram() && t>0 && n>(256/t)*(256/t)*4)n=(256/t)*(256/t)*4;
 return n;
#else
 (void)t;return duke_extra_lowram?64:DUKE_SLOTS;
#endif
}
static int R_DrawDuke(entity_t *e)
{
 model_t fake,*savedmodel;dsprite_t sprite;entity_t draw,*savedentity;
 image_t *skin;duke_bank_t *bank;duke_tile_t *tile;const duke_record_t *record;
 float dx,dy,bearing;int i,b,index,slot=-1,mask,t;byte *frame_data,*pixels;duke_lookup_t *miss;
 if(!(e->flags&RF_DUKEANIM) || currentmodel->type!=mod_alias || !Duke_Active())return 0;
 skin=Duke_ModelSkin(currentmodel,e);if(!skin)return 0;
 {	/* BDGN 3.59g: r_duke_simple3d 1 (OpenGL Ultra Fast / PSYCHO): with simpler models on, the 2D enemies are drawn as
	   those simple 3D models, as before 3.59b. MEASURED K6-3+ / GeForce2: +16 % (v3.58j, Faster + simplest models) */
  static cvar_t *simple3d;
  if(!simple3d)simple3d=ri.Cvar_Get("r_duke_simple3d","0",CVAR_ARCHIVE);
  if(simple3d->value && !strncmp(skin->name,"models/simple",13))return 0;
 }
 b=Duke_FindBank(currentmodel,skin);
 if(b<0) {
  duke_fb_nobank++;
  miss=&duke_lookup[((unsigned)((size_t)currentmodel>>3)*31u+(unsigned)((size_t)skin>>3))&(DUKE_LOOKUP-1)];
  if(!miss->warned){miss->warned=1;Duke_Warn("BDGN sprites: no animation bank for %s / %s; original model drawn\n",currentmodel->name,skin->name);}
  return 0;
 }
 {	/* BDGN 3.58w: a changed tint empties the picture cache once (the pictures are unpacked with the tint) */
  int tint=duke_cv_tint && duke_cv_tint->value?1:0, ww2;
  static cvar_t *cv_skins,*cv_retro;
  if(!cv_skins){cv_skins=ri.Cvar_Get("cl_enemyskins","0",CVAR_ARCHIVE);cv_retro=ri.Cvar_Get("cl_retromode","0",CVAR_ARCHIVE);}
  if(duke_ww2_avail<0)duke_ww2_avail=ri.FS_LoadFile("sprites/anim32ww2/a041.dsa",NULL)>0;
  ww2=duke_ww2_avail && cv_skins->value==1 && (cv_retro->value==2 || cv_retro->value==3);	/* 3.60i: 4 = Duke Nukem 3D mode */
  if(ww2!=duke_ww2) {	/* BDGN 3.58y: other banks: the loaded ones and their pictures go */
#ifdef BDGN_DUKE_GL
   if(duke_queued)R_DukeFlush();
#endif
   R_DukeReset();
   duke_ww2=ww2;
  }
  if(duke_tint_last>=0 && tint!=duke_tint_last) {
#ifdef BDGN_DUKE_GL
   if(duke_queued)R_DukeFlush();
#endif
   R_DukeReset();
  }
  duke_tint_last=tint;
  if(tint && !duke_tint_ready)Duke_TintTable();
 }
 if(!Duke_LoadBank(b))return 0;
 bank=&duke_banks[b];t=bank->tile;
 dx=r_newrefdef.vieworg[0]-e->origin[0];dy=r_newrefdef.vieworg[1]-e->origin[1];
 bearing=(dx==0 && dy==0)?0:anglemod((float)(atan2(dy,dx)*(180.0/3.14159265358979323846))-e->angles[1]);
 i=(duke_extra_lowram || duke_cv_directions->value==2 || (duke_cv_classic->value && duke_cv_lowend->value))?2:4;
 {	/* BDGN 3.52h: far away only front and back (automatic) */
  static float lodfov=-1,lodscale;float depth;
  if(r_newrefdef.fov_y!=lodfov){lodfov=r_newrefdef.fov_y;lodscale=(lodfov>0 && lodfov<180)?(float)tan(lodfov*(M_PI/360.0))/0.75f:0;}
  depth=-(dx*vpn[0]+dy*vpn[1]+(r_newrefdef.vieworg[2]-e->origin[2])*vpn[2]);
  if(depth*lodscale>DUKE_TWODIR_DEPTH)i=2;
 }
 index=((int)(bearing*i/360.0f+0.5001f)%i)*(8/i);
 index=Duke_PoseIndex(e->frame,e->oldframe,e->backlerp,bank->frames,index);mask=(e->flags&RF_SIMPLEBRIGHT)!=0;
 if(bank->slots[mask][index]){slot=bank->slots[mask][index]-1;duke_hits++;}
 if(slot<0) {
 frame_data=Duke_Frame(b,index/40,index%8);if(!frame_data){duke_fb_chunk++;return 0;}
 record=((const duke_record_t *)frame_data)+((index%40)/8);if(record->width<1 || record->height<1){duke_fb_chunk++;return 0;}
 pixels=frame_data+40+((index%40)/8)*t*t;
  duke_misses++;slot=0;{int limit=Duke_SlotLimit(t);for(i=0;i<limit;i++)if(!duke_tiles[i].used){slot=i;break;}else if(duke_tiles[i].used<duke_tiles[slot].used)slot=i;}
  tile=&duke_tiles[slot];duke_tiles_dirty=1;
#ifdef BDGN_DUKE_GL
  if(duke_queued && tile->used>=duke_queue_clock)R_DukeFlush();	/* BDGN 3.52h: queued sprites still read this tile */
#endif
  if(tile->used){duke_banks[tile->bank].slots[tile->mask][tile->record]=0;duke_evictions++;}tile->used=0;
#ifdef BDGN_DUKE_GL
  if(mask)for(i=0;i<t*t;i++)duke_decode[i]=pixels[i]==255?255:215;
  else if(duke_tint_last==1)for(i=0;i<t*t;i++)duke_decode[i]=duke_tint[pixels[i]];	/* BDGN 3.58w */
  else memcpy(duke_decode,pixels,t*t);
  memset(&tile->image,0,sizeof(tile->image));GL_DukeTile(slot,duke_decode,&tile->image,t);duke_uploads++;
#else
  if(mask)for(i=0;i<t*t;i++)tile->pixels[i]=pixels[i]==255?255:215;
  else if(duke_tint_last==1)for(i=0;i<t*t;i++)tile->pixels[i]=duke_tint[pixels[i]];	/* BDGN 3.58w */
  else memcpy(tile->pixels,pixels,t*t);
  memset(&tile->image,0,sizeof(tile->image));tile->image.pixels[0]=tile->pixels;tile->image.transparent=true;
#endif
  tile->image.width=tile->image.height=t;tile->image.registration_sequence=++duke_serial;
  tile->pose=*record;tile->bank=b;tile->record=index;tile->mask=mask;bank->slots[mask][index]=slot+1;
 }
 tile=&duke_tiles[slot];tile->used=++duke_clock;record=&tile->pose;duke_draws++;
 memset(&fake,0,sizeof(fake));memset(&sprite,0,sizeof(sprite));sprite.ident=IDSPRITEHEADER;sprite.version=SPRITE_VERSION;sprite.numframes=1;
 sprite.frames[0].width=record->width;sprite.frames[0].height=record->height;sprite.frames[0].origin_x=record->ox;sprite.frames[0].origin_y=record->oy;
 fake.type=mod_sprite;fake.extradata=&sprite;fake.skins[0]=&tile->image;
 draw=*e;draw.model=&fake;draw.skin=skin;draw.frame=draw.oldframe=0;draw.flags &= ~(RF_DUKEANIM|RF_DUKEWEAPON);
 savedmodel=currentmodel;savedentity=currententity;currentmodel=&fake;currententity=&draw;
#ifdef BDGN_DUKE_GL
 if(!(e->flags&(RF_TRANSLUCENT|RF_DEPTHHACK|RF_WEAPONMODEL))) {	/* BDGN 3.52h: queued, drawn by R_DukeFlush */
  duke_queued_t *q;
  if(duke_queued>=DUKE_QUEUE)R_DukeFlush();
  if(!duke_queued)duke_queue_clock=tile->used;
  q=&duke_queue[duke_queued++];
  q->texnum=tile->image.texnum;q->s0=tile->image.sl;q->t0=tile->image.tl;q->s1=tile->image.sh;q->t1=tile->image.th;
  VectorCopy(e->origin,q->origin);q->w=record->width;q->h=record->height;q->ox=record->ox;q->oy=record->oy;
  if((e->flags&RF_SIMPLEBRIGHT) && skin)GL_BrightSkinColor(skin,q->rgb);
  else if(e->flags&RF_LITSPRITE)R_SpriteLightColor(e->origin,q->rgb);	/* BDGN 3.52r: sprite lighting */
  else q->rgb[0]=q->rgb[1]=q->rgb[2]=255;
  currentmodel=savedmodel;currententity=savedentity;return 1;
 }
 R_DrawSpriteModel(&draw);
#else
 R_DrawSprite();
#endif
 currentmodel=savedmodel;currententity=savedentity;return 1;
}
