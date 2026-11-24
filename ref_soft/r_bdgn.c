/* BDGN software equivalents of the shared renderer settings. */
#include "r_local.h"
#include "../qcommon/software_smp.h"
#include "sw_fastpath.h"	/* BDGN 3.56c: sw_paths */
#include "../client/simple_enemies.h"
#include "../ref_gl/weapon_colors.h"

cvar_t *sw_drawflat_mode, *sw_fastworld, *sw_fastsky, *sw_fastwater;
cvar_t *sw_flatfloors;	/* BDGN 3.55ai */
cvar_t *sw_flatmodels, *sw_lowdetail, *sw_maxtexsize, *sw_brightskins;
cvar_t *sw_staticmap, *sw_dynamic, *sw_particles, *sw_scale;
cvar_t *sw_picmip;
static cvar_t *sw_polyblend;
static cvar_t *sw_modulate;
static cvar_t *sw_coloredlight;	/* BDGN 3.52p */
static cvar_t *sw_drawdist;	/* BDGN 3.52r: software draw distance + fog, 0 = off, 1..10 like gl_drawdist */
/* BDGN 3.57: old-school modes (Video menu, Software): Doom light, Build floors, Quake 1 fog, Blake Stone shading, half
   vertical resolution, Wolfenstein palette */
static cvar_t *sw_doomlight, *sw_blakelight, *sw_q1fog, *sw_wolfpalette;
cvar_t *sw_buildfloors, *sw_halfrows, *sw_halfcols, *sw_lockview;	/* the last two: 3.57a (sw_interlace removed 3.58b) */
cvar_t *sw_q1particles;	/* 3.57b */
float sw_drawdistance;		/* world units, 0 = unlimited */
static byte sw_tint_table[32768], sw_fullbright[256];
static int sw_tint_ready;
static cvar_t *sw_fastsqrt, *sw_fastsqrt2;

byte sw_filter_palette[32768];
int sw_texture_filter_mode;
static qboolean sw_filter_ready;

static void SW_BuildTextureFilter(void)
{
 int i, color, r, g, b, dr, dg, db, dist, best, bestdist;
 const byte *p;
 for(color=0;color<32768;color++) {
  r=(color&31)*8+4;
  g=((color>>5)&31)*8+4;
  b=(color>>10)*8+4;
  best=0;
  bestdist=0x7fffffff;
  for(i=0;i<255;i++) {
   p=(const byte *)&d_8to24table[i];
   dr=p[0]-r; dg=p[1]-g; db=p[2]-b;
   dist=3*dr*dr+4*dg*dg+2*db*db;
   if(dist<bestdist) { best=i; bestdist=dist; }
  }
  sw_filter_palette[color]=(byte)best;
 }
 sw_filter_ready=true;
}

/* BDGN 3.56c: which hot paths run as C instead of id's assembly, and why (sw_fastpath.h) */
int SW_PrefetchCpuKind(void);
int R_SMP_Processors(void);
int R_SMP_Failed(void);
qboolean SWimp_Scaled(void);
static int SW_PathsText(char *text,int size)
{
 swpaths_in_t in;
 memset(&in,0,sizeof(in));
 in.asmspans=ri.Cvar_Get("sw_asmspans","1",0)->value!=0;
 in.prefetch=ri.Cvar_Get("sw_prefetch","0",0)->value!=0;
 in.prefetch_cpu=in.prefetch?SW_PrefetchCpuKind():0;
 in.texfilter=sw_texfilter && sw_texfilter->value!=0;
 in.coloredlight=sw_coloredlight && sw_coloredlight->value!=0;
 in.fullbright=SW_Fullbright();
 in.smp=sw_smp?(int)sw_smp->value:0;
 in.processors=in.smp?R_SMP_Processors():1;
 in.smp_failed=in.smp?R_SMP_Failed():0;
 in.scaled=SWimp_Scaled();
 in.draworder=ri.Cvar_Get("sw_draworder","0",0)->value!=0;
 return SW_AsmPaths(&in,text,size);
}
static void SW_Paths_f(void)
{
 char text[512];
 SW_PathsText(text,sizeof(text));
 ri.Con_Printf(PRINT_ALL,"%s",text);
}
/* at every map load: say so when a setting keeps hot paths away from id's assembly */
void SW_PathsAnnounce(void)
{
 char text[512];
 if(SW_PathsText(text,sizeof(text)))
  ri.Con_Printf(PRINT_ALL,"BDGN software renderer (sw_paths):\n%s",text);
}

void SW_BDGN_Init(void)
{
 sw_drawflat_mode=ri.Cvar_Get("r_drawflat","0",CVAR_ARCHIVE);
 sw_fastworld=ri.Cvar_Get("gl_fastworld","0",CVAR_ARCHIVE);
 sw_fastsky=ri.Cvar_Get("gl_fastsky","0",CVAR_ARCHIVE);
 sw_flatfloors=ri.Cvar_Get("sw_flatfloors","0",CVAR_ARCHIVE);	/* BDGN 3.55ai: 0 off, 1 floors and ceilings, 2 and far walls; 3.57c: 3 every wall too */
 sw_fastwater=ri.Cvar_Get("gl_fastwater","0",CVAR_ARCHIVE);
 sw_flatmodels=ri.Cvar_Get("gl_flatmodels","0",CVAR_ARCHIVE);
 sw_lowdetail=ri.Cvar_Get("gl_lowdetail","0",CVAR_ARCHIVE);
 sw_maxtexsize=ri.Cvar_Get("gl_maxtexsize","0",CVAR_ARCHIVE);
 sw_brightskins=ri.Cvar_Get("r_brightskins","0",CVAR_ARCHIVE);
 R_DukeReset();
 ri.Cmd_AddCommand("duke_stats",R_DukeStats);
 ri.Cvar_Get("r_duke_anim_support","0",0);
 ri.Cvar_SetValue("r_duke_anim_support",3);
 ri.Cvar_Get("r_yshear_support","0",0);
 ri.Cvar_SetValue("r_yshear_support",1);	/* BDGN 3.60i: the Duke Nukem 3D view (qcommon/yshear.h) */	/* BDGN 3.51q: 3 = reads r_duke_res_avail (3.52h: 32x32 only); 2 = 3.51p */
 ri.Cvar_Get("r_simpleenemy_bright","0",0);
 ri.Cvar_SetValue("r_simpleenemy_bright",1);
 sw_staticmap=ri.Cvar_Get("cl_staticmap","0",CVAR_ARCHIVE);
 sw_dynamic=ri.Cvar_Get("gl_dynamic","1",CVAR_ARCHIVE);
 sw_particles=ri.Cvar_Get("cl_particles","1",CVAR_ARCHIVE);
 sw_fastsqrt=ri.Cvar_Get("fastsqrt","0",CVAR_ARCHIVE);
 sw_fastsqrt2=ri.Cvar_Get("fastsqrt2","0",CVAR_ARCHIVE);
 sw_scale=ri.Cvar_Get("scr_scale_active","1",0);
 sw_picmip=ri.Cvar_Get("gl_picmip","0",CVAR_ARCHIVE);
 sw_polyblend=ri.Cvar_Get("gl_polyblend","1",CVAR_ARCHIVE);
 sw_modulate=ri.Cvar_Get("sw_lightscale","1",CVAR_ARCHIVE);
 sw_coloredlight=ri.Cvar_Get("sw_coloredlight","0",CVAR_ARCHIVE);	/* BDGN 3.52p: coloured lighting like OpenGL */
 sw_drawdist=ri.Cvar_Get("sw_drawdist","0",CVAR_ARCHIVE);	/* BDGN 3.52r: draw distance + fog */
 sw_doomlight=ri.Cvar_Get("sw_doomlight","0",CVAR_ARCHIVE);	/* BDGN 3.57 */
 sw_blakelight=ri.Cvar_Get("sw_blakelight","0",CVAR_ARCHIVE);
 sw_buildfloors=ri.Cvar_Get("sw_buildfloors","0",CVAR_ARCHIVE);
 sw_q1fog=ri.Cvar_Get("sw_q1fog","0",CVAR_ARCHIVE);
 sw_halfrows=ri.Cvar_Get("sw_halfrows","0",CVAR_ARCHIVE);
 sw_wolfpalette=ri.Cvar_Get("sw_wolfpalette","0",CVAR_ARCHIVE);
 sw_halfcols=ri.Cvar_Get("sw_halfcols","0",CVAR_ARCHIVE);	/* BDGN 3.57a: Doom's low detail */
 sw_q1particles=ri.Cvar_Get("sw_q1particles","0",CVAR_ARCHIVE);	/* BDGN 3.57b */
 sw_lockview=ri.Cvar_Get("cl_lockview","0",CVAR_ARCHIVE);	/* BDGN 3.57a: the client locks the view level; the floors know it */
 ri.Cvar_SetValue("sw_oldschool_capable",1);	/* the menu shows the six rows only with this ref_soft */
 ri.Cvar_Get("sw_bdgn_features","0",0);
 ri.Cvar_SetValue("sw_bdgn_features",3527);
 ri.Cvar_SetValue("sw_lowskins_capable",1);	/* BDGN 3.56r: smaller model skins from the low packs (r_alias.c) */
 ri.Cvar_SetValue("sw_smp_modes",SW_SMP_COUNT);
 ri.Cvar_SetValue("sw_smp_quality",1);
 ri.Cmd_AddCommand("sw_paths",SW_Paths_f);	/* BDGN 3.56c */
 ri.Cmd_AddCommand("sw_quality_stats",R_SmpQualityStats);	/* 3520 = BDGN 3.51p: sw_smp; 3521 = 3.52n: sw_texfilter; 3522 = 3.52p: sw_coloredlight; 3523 = 3.52r: sw_drawdist, sprite lighting, prefetch; 3524 = 3.52y: low fidelity texture pack; 3525 = bilinear filtering and windowed upscale; 3526 = fullscreen upscale */
}

/* BDGN 3.57: 1 = Doom light, 2 = Blake Stone shading (wins when both are on) */
int SW_OldLightMode(void)
{
 if(sw_blakelight && sw_blakelight->value) return 2;
 if(sw_doomlight && sw_doomlight->value) return 1;
 return 0;
}

/* After the light maps are added (blocklights in lightmap x style units, 128 = normal): one level for the surface.
   Doom: the surface's average in 8 steps, as Doom's sector light levels (light styles still flicker it).
   Blake Stone: no light maps at all: a fixed level by the way the surface faces: floors 80 %, ceilings 60 %,
   walls facing north or south 100 %, east or west 75 % (sloped walls in between). No dynamic lights in either. */
void SW_OldLight(msurface_t *surf, unsigned *blocklights, int size)
{
 unsigned v;
 int i;
 if(size<=0) return;
 if(SW_OldLightMode()==2) {
  float nx=0,ny=0,nz=0,f;
  if(surf->plane) {
   nx=surf->plane->normal[0]; ny=surf->plane->normal[1]; nz=surf->plane->normal[2];
   if(surf->flags&SURF_PLANEBACK) { nx=-nx; ny=-ny; nz=-nz; }
  }
  if(nz>0.7f) f=0.8f;
  else if(nz<-0.7f) f=0.6f;
  else {
   float ax=nx<0?-nx:nx, ay=ny<0?-ny:ny, t=(ax+ay)>0?ay/(ax+ay):1;
   f=0.75f+0.25f*t;
  }
  v=(unsigned)(150*128*f);
 } else {
  double sum=0;
  for(i=0;i<size;i++) sum+=blocklights[i];
  v=(unsigned)(sum/size);
  v=((v+2048)>>12)<<12;	/* 8 steps over the normal range */
 }
 for(i=0;i<size;i++) blocklights[i]=v;
}

int SW_Fullbright(void)
{
 return r_fullbright->value || (sw_fastworld->value && !sw_drawflat_mode->value);
}

float SW_LightScale(void)
{
 float scale=sw_modulate->value;
 return scale<0?0:(scale>4?4:scale);
}

/* BDGN 3.52p: the 15-bit RGB -> palette table for coloured lighting (nearest of the 0-254 colours that are not
   fullbright), and which palette colours are fullbright (the same in every row of the colormap). Built once, the
   first frame sw_coloredlight is on (before the sw_smp worker starts building surfaces). */
static void SW_TintBuild(void)
{
 int i,r,g,b;
 for(i=0;i<256;i++)
  sw_fullbright[i]=vid.colormap && vid.colormap[i]==i && vid.colormap[63*256+i]==i;
 for(r=0;r<32;r++)for(g=0;g<32;g++)for(b=0;b<32;b++) {
  int best=0,bestdist=0x7fffffff,dist,dr,dg,db;
  const byte *p;
  for(i=0;i<255;i++) {
   if(sw_fullbright[i])continue;
   p=(const byte *)&d_8to24table[i];
   dr=p[0]-(r*8+4);dg=p[1]-(g*8+4);db=p[2]-(b*8+4);
   dist=dr*dr*3+dg*dg*4+db*db*2;
   if(dist<bestdist){bestdist=dist;best=i;}
  }
  sw_tint_table[(r<<10)|(g<<5)|b]=(byte)best;
 }
 sw_tint_ready=1;
}
const byte *SW_TintTable(void)
{
 return sw_coloredlight && sw_coloredlight->value && sw_tint_ready ? sw_tint_table : NULL;
}
/* BDGN 3.52r: software fog for the draw distance, after the 3D view is drawn and before the screen warp: every pixel
   is blended toward the fog colour (the OpenGL draw distance fog colour, 0.30/0.30/0.32) by its depth from the
   z-buffer, linear from 35% to 100% of sw_drawdistance (as OpenGL), in 16 steps through palette tables. The sky and
   anything not drawn (beyond the draw distance) get the fog colour. */
static byte sw_fogmap[17][256];
static byte sw_foglevel[32768];
static float sw_fog_lastdist=-1;
static int sw_fogmap_ready;
void SW_FogScreen(void)
{
 int x,y,i,l;
 byte *pd;
 short *pz;
 static int fog_black=-1;
 int black=sw_q1fog && sw_q1fog->value;
 if(sw_drawdistance<=0 || !sw_tint_ready) return;
 if(black!=fog_black) { sw_fogmap_ready=0; fog_black=black; }	/* 3.57: rebuilt for the other fog colour */
 if(!sw_fogmap_ready && black && vid.colormap) {
  /* BDGN 3.57 Quake 1 fog: the scene fades to black through the colormap's darker rows */
  for(l=0;l<=16;l++)for(i=0;i<256;i++) {
   int row=l*4; if(row>63) row=63;
   sw_fogmap[l][i]=(l && i!=255)?((byte *)vid.colormap)[(row<<8)+i]:(byte)i;
  }
  sw_fogmap[16][255]=0;
  sw_fogmap_ready=1;
 }
 if(!sw_fogmap_ready) {
  const int fr=77,fg=77,fb=82;
  for(l=0;l<=16;l++)for(i=0;i<256;i++) {
   const byte *p=(const byte *)&d_8to24table[i];
   int r=(p[0]*(16-l)+fr*l)>>4,g=(p[1]*(16-l)+fg*l)>>4,b=(p[2]*(16-l)+fb*l)>>4;
   sw_fogmap[l][i]=(l && i!=255)?sw_tint_table[((r>>3)<<10)|((g>>3)<<5)|(b>>3)]:(byte)i;
  }
  sw_fogmap_ready=1;
 }
 if(sw_drawdistance!=sw_fog_lastdist) {
  float start=sw_drawdistance*0.35f,end=sw_drawdistance;
  sw_foglevel[0]=16;
  for(i=1;i<32768;i++) {
   float z=(float)0x8000/i,f=(z-start)/(end-start);
   sw_foglevel[i]=(byte)(f<=0?0:f>=1?16:(int)(f*16));
  }
  sw_fog_lastdist=sw_drawdistance;
 }
 for(y=r_refdef.vrect.y;y<r_refdef.vrect.y+r_refdef.vrect.height;y++) {
  pd=(byte *)d_viewbuffer+y*r_screenwidth+r_refdef.vrect.x;
  pz=d_pzbuffer+y*d_zwidth+r_refdef.vrect.x;
  for(x=0;x<r_refdef.vrect.width;x++) {
   int zb=pz[x];
   l=zb<=0?16:sw_foglevel[zb];
   if(l)pd[x]=sw_fogmap[l][pd[x]];
  }
 }
}
/* BDGN 3.52r: an entity beyond the draw distance (not the view weapon) */
int SW_EntityBeyondDrawDistance(entity_t *e)
{
 vec3_t d;
 float r=sw_drawdistance+64;
 if(sw_drawdistance<=0 || (e->flags&(RF_WEAPONMODEL|RF_DEPTHHACK|RF_BEAM))) return 0;
 VectorSubtract(e->origin,r_origin,d);
 return DotProduct(d,d)>r*r;
}
const byte *SW_FullbrightTable(void)
{
 return sw_fullbright;
}
/* BDGN 3.52q: palette remap for one model in coloured light: colour * tint (0-255 per channel), through the 15-bit
   table; NULL when the light is (nearly) white. Rebuilt only when the tint changes. */
const byte *sw_model_remap;
const byte *SW_ModelRemap(int tr,int tg,int tb)
{
 static byte remap[256];
 static int lr=-1,lg=-1,lb=-1;
 int i,r,g,b;
 const byte *p;
 if(tr>=248 && tg>=248 && tb>=248) return NULL;
 tr&=~3;tg&=~3;tb&=~3;	/* fewer rebuilds for nearly the same colour */
 if(tr==lr && tg==lg && tb==lb) return remap;
 lr=tr;lg=tg;lb=tb;
 for(i=0;i<256;i++) {
  if(i==255 || sw_fullbright[i]) { remap[i]=(byte)i; continue; }
  p=(const byte *)&d_8to24table[i];
  r=p[0]*tr>>8;g=p[1]*tg>>8;b=p[2]*tb>>8;
  remap[i]=sw_tint_table[((r>>3)<<10)|((g>>3)<<5)|(b>>3)];
 }
 return remap;
}

void SW_BDGN_Frame(void)
{
 static int oldfull=-1, olddynamic=-1, oldcolor=-1;
 static float oldscale=-1;
 float scale=SW_LightScale();
 int full=SW_Fullbright(), dynamic=sw_dynamic->value!=0, color=sw_coloredlight->value!=0;
 sw_texture_filter_mode=sw_texfilter->value==3 ? 3 : sw_texfilter->value==2 ? 2 : (sw_texfilter->value!=0);
 if(sw_texture_filter_mode>=2 && !sw_filter_ready) SW_BuildTextureFilter();
 if(color && !sw_tint_ready) SW_TintBuild();
 {	/* BDGN 3.52r: draw distance (same steps as gl_drawdist: 1 = about 3100, 10 = 160 world units) */
  float step=sw_drawdist->value;
  if(step<0.5f && sw_q1fog->value) step=4;	/* BDGN 3.57: Quake 1 fog alone: about 1100 units */
  sw_drawdistance=(step<0.5f || (r_newrefdef.rdflags&RDF_NOWORLDMODEL))?0:4096.0f*(float)pow(160.0/4096.0,(step>10?10:step)/10.0);
  if(sw_drawdistance>0 && !sw_tint_ready) SW_TintBuild();
 }
 /* Lighting lives in surface caches. Invalidate when either mode changes,
    including a return to normal lighting without a vid_restart. */
 {	/* BDGN 3.57: the surface caches hold the light: rebuilt when an old-school light mode changes */
  static int oldlight=-1;
  int ol=SW_OldLightMode();
  if(ol!=oldlight && oldlight!=-1) D_FlushCaches();
  oldlight=ol;
 }
 if(sw_wolfpalette->modified) {	/* BDGN 3.57: the displayed palette changes, nothing else */
  sw_wolfpalette->modified=false;
  R_GammaCorrectAndSetPalette((const unsigned char *)d_8to24table);
 }
 if(full!=oldfull || dynamic!=olddynamic || scale!=oldscale || color!=oldcolor) D_FlushCaches();
 oldscale=scale; oldcolor=color;
 oldfull=full; olddynamic=dynamic;
 if(sw_staticmap->value) r_newrefdef.time=0;
 if(!sw_polyblend->value) r_newrefdef.blend[3]=0;
 Q_SetFastSqrt((int)sw_fastsqrt->value,(int)sw_fastsqrt2->value);
}

int SW_NearestColor(int r,int g,int b)
{
 int i,best=0,dist,bestdist=0x7fffffff,dr,dg,db;
 const byte *p;
 for(i=0;i<255;i++) {
  p=(const byte *)&d_8to24table[i];
  dr=p[0]-r; dg=p[1]-g; db=p[2]-b;
  dist=dr*dr+dg*dg+db*db;
  if(dist<bestdist) { best=i; bestdist=dist; }
 }
 return best;
}

static void SW_ImageColors(image_t *image)
{
 int bins[512],sums[512][3],i,j,k,n,step,best,count,gray;
 unsigned hash=2166136261U;
 const byte *p;
 byte rgb[3];
 if(image->bdgn_colors_valid) return;
 memset(bins,0,sizeof(bins)); memset(sums,0,sizeof(sums));
 n=(image->width*image->height) >> (image->bdgn_compact?4:0); step=n/4096; if(step<1) step=1;
 for(i=0;i<n;i+=step) {
  j=image->pixels[0][i]; if(j==255) continue;
  p=(const byte *)&d_8to24table[j];
  k=((p[0]>>5)<<6)|((p[1]>>5)<<3)|(p[2]>>5);
  bins[k]++; for(j=0;j<3;j++) sums[k][j]+=p[j];
 }
 best=0; for(i=1;i<512;i++) if(bins[i]>bins[best]) best=i;
 count=bins[best];
 for(j=0;j<3;j++) rgb[j]=count?sums[best][j]/count:128;
 for(i=0;image->name[i];i++) hash=(hash^(byte)image->name[i])*16777619U;
 image->bdgn_colors[1]=SW_NearestColor(64+(hash&127),64+((hash>>8)&127),64+((hash>>16)&127));
 image->bdgn_colors[2]=SW_NearestColor(rgb[0],rgb[1],rgb[2]);
 gray=(rgb[0]*77+rgb[1]*150+rgb[2]*29)>>8;
 image->bdgn_colors[3]=SW_NearestColor(gray,gray,gray);
 memcpy(image->bdgn_rgb,rgb,3);
 image->bdgn_bright=-1;
 image->bdgn_colors_valid=true;
}

int SW_ImageColor(image_t *image,int mode)
{
 if(!image || !image->pixels[0]) return SW_NearestColor(128,128,128);
 SW_ImageColors(image);
 if(mode<1 || mode>3) mode=2;
 return image->bdgn_colors[mode];
}

int SW_BrightColor(image_t *image)
{
 static const byte colors[6][3]={{32,255,48},{255,128,0},{32,112,255},
  {224,48,255},{255,240,16},{255,32,24}};
 extern image_t r_images[];
 extern int numr_images;
 byte used[256];
 const byte *p,*target;
 int i,j,max,min,best=-1,dist,bestdist=0x7fffffff,dr,dg,db;
 if(!image) return SW_NearestColor(32,255,48);
 /* Damaged skins share the healthy variant's cached bright colour. */
 for(i=0;i<sizeof(simple_enemies)/sizeof(simple_enemies[0]);i++)
  if(!strcmp(image->name,simple_enemies[i].skin) && strcmp(simple_enemies[i].skin,simple_enemies[i].bright_skin)) {
   image_t *healthy = R_FindImage((char *)simple_enemies[i].bright_skin,it_skin);
   if(healthy) {return SW_BrightColor(healthy);}
   break;
  }

 SW_ImageColors(image);
 if(image->bdgn_bright>=0) return image->bdgn_bright;
 /* Dominant skin hue selects the target. Avoid colours assigned to live skins
    until the finite bright part of the 8-bit palette is exhausted. */
 best=0;
 for(i=1;i<3;i++) if(image->bdgn_rgb[i]>image->bdgn_rgb[best]) best=i;
 j=best==1?0:(best==2?2:5);
 if(abs(image->bdgn_rgb[0]-image->bdgn_rgb[1])<40 && best!=2) j=4;
 else if(best==0 && image->bdgn_rgb[1]>image->bdgn_rgb[2]+32) j=1;
 else if(best==2 && image->bdgn_rgb[0]>image->bdgn_rgb[1]+32) j=3;
 target=colors[j]; memset(used,0,sizeof(used));
 for(i=0;i<numr_images;i++)
  if(r_images[i].registration_sequence && r_images[i].bdgn_colors_valid && r_images[i].bdgn_bright>=0)
   used[r_images[i].bdgn_bright]=1;
 best=-1;
 for(i=0;i<255;i++) {
  if(used[i]) continue;
  p=(const byte *)&d_8to24table[i]; max=min=p[0];
  for(j=1;j<3;j++) { if(p[j]>max) max=p[j]; if(p[j]<min) min=p[j]; }
  if(max<112 || max-min<64) continue;
  dr=p[0]-target[0]; dg=p[1]-target[1]; db=p[2]-target[2]; dist=dr*dr+dg*dg+db*db;
  if(dist<bestdist) { best=i; bestdist=dist; }
 }
 if(best<0) best=SW_NearestColor(target[0],target[1],target[2]);
 image->bdgn_bright=best;
 return best;
}

/* BDGN 3.57m: the part of a model path after "models/" or "models/simple1..3/" (simpler models drawn far away keep the
   same rules as their normal model); NULL if it is not under models/ */
static const char *R_ModelRest (const char *n)
{
 if (strncmp (n, "models/", 7)) return NULL;
 n += 7;
 if (!strncmp (n, "simple", 6) && n[6] >= '1' && n[6] <= '3' && n[7] == '/') n += 8;
 return n;
}
int SW_BrightModel(const char *name,int flags)
{
 int len;
 const char *rest=R_ModelRest(name);
 if(flags & (RF_WEAPONMODEL|RF_SHELL_HALF_DAM|RF_SHELL_GREEN|RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_DOUBLE)) return 0;
 if(rest && !strncmp(rest,"monsters/",9)) return 1;
 len=strlen(name);
 return !strncmp(name,"players/",8) && len>=9 && !strcmp(name+len-9,"/tris.md2");
}

int SW_AliasColor(image_t *skin)
{
 byte rgb[3];
 int mode=(int)sw_drawflat_mode->value;
 if(sw_brightskins->value && SW_BrightModel(currentmodel->name,currententity->flags)) return SW_BrightColor(skin);
 if(mode<2 || mode>3) return -1;
 if(mode==2 && (currententity->flags & RF_WEAPONMODEL) && R_FlatWeaponColor(currentmodel->name,rgb))
  return SW_NearestColor(rgb[0],rgb[1],rgb[2]);
 return SW_ImageColor(skin,mode);
}

/* BDGN 3.57 Wolfenstein palette: the colours sent to the screen in 5 levels per channel (125 colours), as the coarse
   VGA palettes of the early 90s. Only the palette changes: nothing per pixel, no cost. */
int SW_WolfPalette(void)
{
 return sw_wolfpalette && sw_wolfpalette->value;
}

/*
BDGN 3.57c: the reduced 3D view. Half vertical resolution (sw_halfrows), Doom's low detail (sw_halfcols 1, half the
columns) and FastDoom's potato detail (sw_halfcols 2, a quarter of the columns) draw only the 3D view smaller, in the
top left corner of its own rectangle, and widen it in place afterwards (each pixel copied 2 or 4 times across, each row
twice down). The HUD, menus and console stay at full resolution. R_ViewChanged gives the pixels their shape, so the
picture keeps its proportions. Before 3.57c the whole buffer was made smaller, HUD included.
*/
int sw_reduce_x=1, sw_reduce_y=1;
static int sw_full_w, sw_full_h;

void SW_ReducedViewBegin(void)
{
 int cx=1, cy=1;
 if(!(r_newrefdef.rdflags&RDF_NOWORLDMODEL)) {
  if(sw_halfcols && sw_halfcols->value) cx=sw_halfcols->value>=2?4:2;
  if(sw_halfrows && sw_halfrows->value) cy=2;
 }
 sw_reduce_x=cx; sw_reduce_y=cy;	/* R_SetupFrame calls R_ViewChanged every frame, which reads these */
 sw_full_w=r_newrefdef.width; sw_full_h=r_newrefdef.height;
 if(cx>1) r_newrefdef.width=(r_newrefdef.width+cx-1)/cx;
 if(cy>1) r_newrefdef.height=(r_newrefdef.height+cy-1)/cy;
}

void SW_ReducedViewEnd(void)
{
 int x, y, cx=sw_reduce_x, cy=sw_reduce_y, sh=cx==4?2:(cx==2?1:0);
 byte *base;
 if(cx==1 && cy==1) return;
 base=vid.buffer+r_newrefdef.y*vid.rowbytes+r_newrefdef.x;
 /* bottom row first and right to left: every pixel is read before it is written over */
 for(y=sw_full_h-1;y>=0;y--) {
  byte *dst=base+y*vid.rowbytes, *src=base+(y/cy)*vid.rowbytes;
  if(cx==1) { if(dst!=src) memcpy(dst,src,sw_full_w); continue; }
  for(x=sw_full_w-1;x>=0;x--) dst[x]=src[x>>sh];
 }
 r_newrefdef.width=sw_full_w; r_newrefdef.height=sw_full_h;
}

