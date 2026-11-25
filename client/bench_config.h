/* Benchmark-only catalogue. Values are effective assignments from shipped CFGs.
 * Optional JavaScript tests/tools are not required to build the VS2005 project. */
#define BC_GL 1
#define BC_VIDEO 2
#define BC_CURRENT 4
#define BC_SOUND 8
#define BC_MODE 16
#define BC_QUALITY 32
#define BC_MUSIC 64
#define BC_SOFT 128	/* BDGN 3.51p: ref_soft only, see bench_smp.h */
#define BC_SOFTF 256	/* BDGN 3.52t: BDGN ref_soft settings (vid_ref soft and sw_bdgn_features 3523) */
#include "bench_smp.h"
typedef struct {
 const char *name,*category,*reference,*on;
 int flags,count; const char *values[12];
} benchcfg_t;
static const benchcfg_t bench_cfg[]={
 {"gl_materials","LIGHTING","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"}},
 {"gl_soft_particles","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"}},
 {"gl_smoke_lighting","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"}},
 {"gl_ssr","PICTURE EFFECTS","0",NULL,BC_GL|BC_CURRENT,3,{"0","1","2"}},
 {"gl_ssgi","LIGHTING","0",NULL,BC_GL|BC_CURRENT,3,{"0","1","2"}},
 {"gl_volume_fog","LIGHTING","0",NULL,BC_GL|BC_CURRENT,3,{"0","1","2"}},
 {"gl_caustics","WATER","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"}},
 {"cl_skybox","SKY & ATMOSPHERE","0",NULL,BC_GL|BC_CURRENT,11,{"0","1","2","3","4","5","6","7","8","9","10"}},
 {"gl_sky_lighting","SKY & ATMOSPHERE","1",NULL,BC_GL|BC_CURRENT,2,{"1","0"}},
 { "gl_pixel_lighting","LIGHTING","0",NULL,BC_GL|BC_CURRENT,4,{"0","1","2","4"} },	/* BDGN 3.55h: lights per pixel */
 { "gl_pixel_bump","LIGHTING","1",NULL,BC_GL|BC_CURRENT,2,{"1","0"} },
 { "gl_shadows","LIGHTING","0",NULL,BC_GL|BC_CURRENT,4,{"0","1","2","3"} },
 { "gl_water_surface","LIGHTING","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "gl_water_reflect","LIGHTING","0",NULL,BC_GL|BC_CURRENT,3,{"0","1","2"} },
 { "cl_hdfx","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },	/* BDGN 3.55s: HD weapon effects */	/* BDGN 3.55o: water reflection off / half / full size */	/* BDGN 3.55m: shadow cost; modern drivers only */
 { "cl_3dexplo","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },	/* BDGN 3.55t: modern 3D weapon effects */
 { "cl_3dbfg","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_3dblaster","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_hdsmoke","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_hdmuzzle","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_heatdistort","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_fx3d_tex","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },	/* BDGN 3.55v: standard / AI textures on the 3D effects */
 { "cl_decals","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },	/* BDGN 3.55y: marks, 3D railgun, HD gibs and blood */
 { "cl_3drail","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "cl_hdgibs","WEAPON EFFECTS","0",NULL,BC_GL|BC_CURRENT,2,{"0","1"} },
 { "gl_vertex_arrays","DRAW PATHS","0","1",1,2, {"0","1"} },
 { "gl_ext_compiled_vertex_array","DRAW PATHS","1","1",3,2, {"1","0"} },
 { "gl_ext_pointparameters","PARTICLES","1","1",3,2, {"1","0"} },
 { "gl_ext_palettedtexture","TEXTURES","0","1",3,2, {"0","1"} },
 { "gl_texturemode","TEXTURES","GL_LINEAR_MIPMAP_NEAREST","GL_LINEAR_MIPMAP_NEAREST",1,6,
   {"GL_LINEAR_MIPMAP_NEAREST","GL_LINEAR_MIPMAP_LINEAR","GL_NEAREST_MIPMAP_NEAREST","GL_NEAREST_MIPMAP_LINEAR","GL_NEAREST","GL_LINEAR"} },
 { "gl_drawdist","LIGHTING","0",NULL,BC_GL|BC_CURRENT,5, {"0","1","3","6","10"} },
 { "gl_texturesolidmode","TEXTURES","default","default",3,2, {"default","GL_RGB8"} },
 { "gl_texturealphamode","TEXTURES","default","default",3,2, {"default","GL_RGBA8"} },
 { "gl_picmip","TEXTURES","0","2",3,4, {"0","1","2","3"} },	/* 3.58v: 3 (Ultra Fast, PSYCHO) */
 { "gl_skymip","TEXTURES","0","4",3,3, {"0","2","4"} },
 { "gl_playermip","TEXTURES","0","2",3,4, {"0","1","2","3"} },	/* 3.58v: 3 (Ultra Fast, PSYCHO) */
 { "gl_round_down","TEXTURES","1","1",3,2, {"1","0"} },
 { "gl_modulate","LIGHTING","1","1",3,2, {"1","4"} },
 { "intensity","LIGHTING","2","2",3,2, {"2","1.2"} },
 { "gl_saturatelighting","LIGHTING","0","0",1,2, {"0","1"} },
 /* 3.52w: gl_flashblend removed: always off (big glowing balls around rockets and blaster shots) */
 { "gl_polyblend","VIEW & MATH","1","0",1,2, {"1","0"} },
 { "gl_cull","DRAW PATHS","1","1",1,2, {"1","0"} },
 { "gl_ztrick","DRAW PATHS","0","1",1,2, {"0","1"} },
 { "r_lerpmodels","ANIMATIONS","1","0",1,2, {"1","0"} },
 { "cl_vwep","ANIMATIONS","1","0",0,2, {"1","0"} },
 { "cl_gun","VIEW & MATH","1","1",0,2, {"1","0"} },
 { "cpu_profile","VIEW & MATH","1","1",0,2, {"1","0"} },
 { "gl_sse_packed","DRAW PATHS","1",NULL,BC_GL|BC_CURRENT,2, {"1","0"} },	/* BDGN 3.58w: packed SSE light maps against the one-texel SSE code */	/* BDGN 3.58w: the CPU profile against the generic values */	/* BDGN 3.52h: combined/fastest cases keep the view weapon (presets never hide it); "0" is still an individual case */
 { "cl_particles","PARTICLES","1","0",0,2, {"1","0"} },
 { "cl_weaponlights","LIGHTING","1",NULL,BC_CURRENT,2, {"1","0"} },
 { "cl_blend","VIEW & MATH","1","0",0,2, {"1","0"} },
 { "cl_footsteps","SOUND","1","0",0,2, {"1","0"} },
 { "cl_predict","CPU",NULL,"1",4,3, {"@saved","0","1"} },
 { "gl_particle_att_a","PARTICLES","0.01","0.01",1,1, {"0.01"} },
 { "gl_particle_att_b","PARTICLES","0.0","0",1,1, {"0.0"} },
 { "gl_particle_att_c","PARTICLES","0.01","0.01",1,1, {"0.01"} },
 { "gl_particle_size","PARTICLES","40","40",1,1, {"40"} },
 { "gl_particle_min_size","PARTICLES","2","2",1,1, {"2"} },
 { "gl_particle_max_size","PARTICLES","40","40",1,1, {"40"} },
 { "gl_mode","DISPLAY",NULL,NULL,7,3, {"@saved","3","4"} },
 { "vid_fullscreen","DISPLAY",NULL,NULL,6,3, {"@saved","0","1"} },
 { "gl_bitdepth","DISPLAY",NULL,NULL,7,2, {"@saved","0"} },
 { "vid_gamma","DISPLAY",NULL,NULL,5,3, {"@saved","1","0.500000"} },
 { "fs_lowtextures","LOW QUALITY PACKS","0","1",3,3, {"0","1","2"} },
 { "sw_test_gray_lightmaps","TEST LOW END","0",NULL,BC_SOFTF|BC_CURRENT,2,{"0","1"} },
 { "sw_test_compact_walls","TEST LOW END","0",NULL,BC_SOFTF|BC_CURRENT,2,{"0","1"} },
 { "@sound_mode","SOUND","0","1",16,6, {"0","1","2","3","4","5"} },	/* 3.58g: 4 = PC speaker; 3.58l: 5 = through the sound card */
 { "s_profile","SOUND","1","0",40,5, {"1","0","2","3","4"} },
 { "s_primary","SOUND","0","0",8,2, {"0","1"} },
 { "s_loadas8bit","SOUND","1","1",8,2, {"1","0"} },
 { "s_khz","SOUND","11","8",8,4, {"11","8","22","44"} },
 { "s_maxchannels","SOUND","32","16",0,2, {"32","16"} },
 { "s_mixahead","SOUND","0.2","0.2",0,1, {"0.2"} },
 { "s_volume","SOUND","0.7","0.7",0,1, {"0.7"} },
 { "s_musicmode","MUSIC","0","0",64,3, {"0","1","2"} },
 { "sw_smp","CPU","0","0",128,2, {"0","1"} },
 /* BDGN 3.52t: the software renderer's settings of 3.52n-3.52r; ON stays "off" in the combined/fastest cases until measured */
 { "sw_texfilter","SOFTWARE","0","0",BC_SOFTF,2, {"0","1"} },
 { "sw_coloredlight","SOFTWARE","0","0",256,2, {"0","1"} },
 { "sw_drawdist","SOFTWARE","0","0",256,3, {"0","3","6"} },
 { "sw_prefetch","SOFTWARE","0","0",256,2, {"0","1"} }
};
#define BENCH_CFG_COUNT (sizeof(bench_cfg)/sizeof(bench_cfg[0]))
typedef struct {const char *name;int choice;} benchreject_t;
/* Below the 98.106239 FPS control in the user's K6-III+ log. Not a universal hardware claim. */
static const benchreject_t bench_rejected[]={
 {"gl_fastdraw",2},
 {"gl_fastparticles",0},
 {"gl_fastparticles",2},
 {"gl_batchworld",1},
 {"gl_finish",1},
 {"gl_flatmodels",1},
 {"cl_staticlightstyles",1},
 {"gl_fastsky",1},
 {"gl_fastwater",1},
 {"gl_lowdetail",1},
 {"gl_lowdetail",2},
 {"gl_lowdetail",3},
 {"gl_maxtexsize",2},
 {"gl_maxtexsize",3},
 {"cl_fovaspect",1},
 {"fastsqrt",2},
 {"cl_nospawnpads",1},
 {"gl_3dnow",0},
 {"gl_mmx",1},
 {"gl_amd_cull",0},
 {"cl_amd_particles",0},
 {"gl_amd_lightpoint",0},
 {"s_mixinterval",0}
};
static int BenchAllowed (int i,int choice,qboolean retest)
{
 int j;
 if(!strcmp(bdgn_tweaks[i].cvar,"gl_shadows") && bdgn_tweaks[i].values[choice]) return 0;
 if(!BDGN_TweakAllowed(i,choice)) return 0;
 if((!strncmp(bdgn_tweaks[i].cvar,"cl_simplemodels",15) || !strcmp(bdgn_tweaks[i].cvar,"cl_modellod") || !strcmp(bdgn_tweaks[i].cvar,"cl_lowspec3d") || !strcmp(bdgn_tweaks[i].cvar,"cl_simpleworld")) && !BDGN_TweakVisible(i)) return 0;	/* BDGN 3.57l: pak missing */
 if(retest) return 1;
 /* Historical single-cvar measurements do not describe the new grouped cases. */
 if(bdgn_tweaks[i].kind==3 || !strcmp(bdgn_tweaks[i].cvar,"cl_simpleenemies") || !strcmp(bdgn_tweaks[i].cvar,"cl_simpleitems")) return 1;
 for(j=0;j<sizeof(bench_rejected)/sizeof(bench_rejected[0]);j++)
  if(!strcmp(bdgn_tweaks[i].cvar,bench_rejected[j].name) && choice==bench_rejected[j].choice) return 0;
 return 1;
}
/* NULL = the row can be measured with the loaded renderer and this machine */
static const char *BenchCfgSkipReason (int i)
{
 if (!strcmp(bench_cfg[i].name,"fs_lowtextures")) {	/* 3.52d; 3.52y: BDGN software (3524) too */
  if (!FS_SmallTexPackAvailable()) return "lowtex-small.pak not installed";
  if (!Q_stricmp(Cvar_VariableString("vid_ref"),"gl")) return NULL;
  if (!Q_stricmp(Cvar_VariableString("vid_ref"),"glam") && Cvar_VariableValue("glam_bdgn_features")>=3522) return NULL;	/* 3.52z */
  if (!Q_stricmp(Cvar_VariableString("vid_ref"),"soft") && Cvar_VariableValue("sw_bdgn_features")>=3524) return NULL;
  return "needs BDGN OpenGL, BDGN software (3.52y) or ref_glam 3.52z";
 }
 if (bench_cfg[i].flags&BC_SOFTF) {	/* BDGN 3.52t */
  if (Q_stricmp(Cvar_VariableString("vid_ref"),"soft")) return "software renderer (vid_ref soft) only";
  if (Cvar_VariableValue("sw_bdgn_features")<3523) return "this ref_soft.dll is older than 3.52r";
  return NULL;
 }
 if (bench_cfg[i].flags&BC_SOFT) {
  int nsys,nproc;
  Sys_ProcessorCounts(&nsys,&nproc);
  return BenchSmpSkipReason(Cvar_VariableString("vid_ref"),(int)Cvar_VariableValue("sw_bdgn_features"),nproc);
 }
 return NULL;
}
static int BenchCfgAllowed (int i)
{
 if(!strcmp(bench_cfg[i].name,"gl_materials")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_materials_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_soft_particles")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_soft_particles_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_smoke_lighting")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_soft_particles_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_ssr")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_environment_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_ssgi")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_environment_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_volume_fog")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_environment_capable")!=0;
 if(!strcmp(bench_cfg[i].name,"gl_caustics")) return !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") && Cvar_VariableValue("bdgn_gl_environment_capable")!=0;
 if (!strcmp(bench_cfg[i].name,"cl_skybox") || !strcmp(bench_cfg[i].name,"gl_sky_lighting")) return CL_SkyboxAvailable();
 if (!strcmp(bench_cfg[i].name,"gl_pixel_lighting") || !strcmp(bench_cfg[i].name,"gl_pixel_bump"))
  return Cvar_VariableValue("bdgn_gl_pixel_lighting_capable")!=0;
 if (!strcmp(bench_cfg[i].name,"cl_hdfx"))
  return Cvar_VariableValue("bdgn_hdfx_available")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (CL_FX3D_CvarAvailable(bench_cfg[i].name)>=0) return CL_FX3D_CvarAvailable(bench_cfg[i].name);	/* BDGN 3.55t */
 if (!strcmp(bench_cfg[i].name,"gl_water_surface"))
  return Cvar_VariableValue("bdgn_gl_water_surface_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(bench_cfg[i].name,"gl_water_reflect"))
  return Cvar_VariableValue("bdgn_gl_water_reflect_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (!strcmp(bench_cfg[i].name,"gl_shadows"))
  return Cvar_VariableValue("bdgn_gl_soft_shadow_capable")!=0 && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
 if (bench_cfg[i].flags&(BC_SOFT|BC_SOFTF)) return BenchCfgSkipReason(i)==NULL;
 if (!strcmp(bench_cfg[i].name,"fs_lowtextures")) return BenchCfgSkipReason(i)==NULL;	/* 3.52d */
 if ((bench_cfg[i].flags&BC_GL) && (!Q_stricmp(Cvar_VariableString("vid_ref"),"soft") || !Q_stricmp(Cvar_VariableString("vid_ref"),"swam")))
  return BDGN_RendererSupports(bench_cfg[i].name);
 return !(bench_cfg[i].flags&BC_GL) || !Q_stricmp(Cvar_VariableString("vid_ref"),"gl");
}
static void BenchCfgSet (int i,const char *value)
{
 if(!value || !strcmp(value,"@saved")) return;
 if(bench_cfg[i].flags&BC_MODE) M_SetSoundMode(atoi(value));
 else if(bench_cfg[i].flags&BC_QUALITY) M_SetSoundQuality(atoi(value));
 else {
  Cvar_Set((char *)bench_cfg[i].name,(char *)value);
  if(bench_cfg[i].flags&BC_MUSIC) {
   Cvar_SetValue("s_musicdefault",1);Cvar_SetValue("s_musicsource",1);Cvar_SetValue("cd_nocd",0);
  }
 }
}
static char bench_reload_old[64][64];
static const char *bench_reload_names[]={
 "vid_ref","gl_driver","vid_rendition",
 "gl_maxtexsize","gl_ext_multitexture","s_nosound","s_midieffects","s_initsound","s_pcspeaker","gl_swapinterval",
 "sw_render_scale",
 "sw_test_gray_lightmaps",
 "sw_test_compact_walls",
 "sw_mode",
 "gl_ext_samples",
 "gl_hdtextures",
 "gl_ext_compiled_vertex_array",
 "gl_ext_pointparameters",
 "gl_ext_palettedtexture",
 "gl_texturesolidmode",
 "gl_texturealphamode",
 "gl_picmip",
 "gl_skymip",
 "gl_playermip",
 "gl_round_down",
 "gl_modulate",
 "intensity",
 "gl_mode",
 "vid_fullscreen",
 "gl_bitdepth",
 "fs_lowtextures",
 "s_profile",
 "s_primary",
 "s_loadas8bit",
 "s_khz",
 "s_mono",	/* 3.57m */
 "cl_simplemodels","cl_simplemodels_far","cl_simpleworld","cl_modellod","cl_lowspec3d",	/* 3.57t: renderer restart per case */
 NULL
};
static int BenchReloadFlag (const char *name)
{
 int i;
 if(!strcmp(name,"gl_swapinterval")) return VID_GlideVsyncOn()?1:0;	/* BDGN 3.58m: only the 3dfx drivers need a restart */
 /* 3.60: s_pcspeaker and s_primary need the sound restart too (they returned 1, a renderer restart, which left the
    PC speaker off after the "sound card" case: every later PSYCHO case in the v3.59z CRASH HUNT ran with the sound card) */
 if(!strcmp(name,"s_khz") || !strcmp(name,"s_mono") || !strcmp(name,"s_loadas8bit") || !strcmp(name,"s_nosound") || !strcmp(name,"s_midieffects") || !strcmp(name,"s_initsound")
  || !strcmp(name,"s_pcspeaker") || !strcmp(name,"s_primary")) return 2;
 for(i=0;i<BENCH_CFG_COUNT;i++) if(!strcmp(name,bench_cfg[i].name))
  return bench_cfg[i].flags&BC_SOUND ? 2 : 1;
 return 1;
}
static void BenchReloadCapture (void)
{
 int i;
 for(i=0;bench_reload_names[i];i++) Com_sprintf(bench_reload_old[i],64,"%s",Cvar_VariableString((char *)bench_reload_names[i]));
}
static int BenchReloadChanges (void)
{
 int i,flags=0;
 for(i=0;bench_reload_names[i];i++)
  if(strcmp(bench_reload_old[i],Cvar_VariableString((char *)bench_reload_names[i]))) flags|=BenchReloadFlag(bench_reload_names[i]);
 /* BDGN 3.58n (MEASURED, Pa1983's Voodoo2, 3.58m: every preset stopped at 59.97 fps = 60 Hz vsync): the loaded 3dfx
    driver still had vsync, because gl_swapinterval was already 0 when the case started, so no change was seen and
    the renderer was not restarted. Now: restart whenever the loaded 3dfx driver got vsync (once: during the benchmark
    it gets 0, win32/vid_dll.c). */
 if(VID_GlideVsyncOn()) flags|=1;
 return flags;
}
/* BDGN 3.59v: a case puts the preset on first and its own values after (preset gl_mode 3, case gl_mode 0): the value
   ends where it was, but the cvar stays "modified", and ref_gl restarts the whole renderer when gl_mode or
   vid_fullscreen is modified (R_BeginFrame, as id's 3.21). Every OpenGL repeat therefore loaded every texture again:
   183 enemy sprite pictures and 280-300 pictures in all per load, 180-260 ms on an i7 against 1 ms in Software (Lupusrex
   benchmark, v3.59t, MEASURED); on Chromatic's Voodoo3 the 2D sprite banks took 1.7-2.0 s at every repeat. A cvar whose
   value did not change loses its "modified" mark, so only real changes restart (BenchReloadChanges decides those). */
static void BenchClearUnchangedModified (void)
{
 cvar_t *v;
 int i;
 for(i=0;bench_reload_names[i];i++)
  if(!strcmp(bench_reload_old[i],Cvar_VariableString((char *)bench_reload_names[i]))) {
   v=Cvar_Get((char *)bench_reload_names[i],"",0);
   if(v) v->modified=false;
  }
}
static void BenchQueueReload (int flags)
{
 if(flags&1) Cbuf_AddText("vid_restart\nwait\nwait\n");
 if(flags&2) Cbuf_AddText("snd_restart\nwait\nwait\n");
}
static int bench_shadow_case;	/* BDGN 3.55m: the gl_shadows case keeps its value */
static void BenchGuards (void)
{
 float sight;int i;
 if (!bench_shadow_case) Cvar_SetValue("gl_shadows",0);
 Cvar_SetValue("gl_swapinterval",0);	/* BDGN 3.58m: vsync always off in every benchmark (owner), also after a preset */
 Cvar_SetValue("r_drawentities",1);Cvar_SetValue("cl_entities",1);
 Cvar_SetValue("r_drawworld",1);Cvar_SetValue("r_norefresh",0);
 Cvar_SetValue("cl_testentities",0);Cvar_SetValue("cl_testparticles",0);
 Cvar_SetValue("cl_testlights",0);Cvar_SetValue("cl_testblend",0);
 sight=1;
 for(i=0;i<bench_nvars;i++) if(!strcmp(bench_names[i],"crosshair") && atof(bench_saved[i])>0) sight=(float)atof(bench_saved[i]);
 Cvar_SetValue("crosshair",sight);
 /* Preserve the case's view area. 3.60i EXTREME uses 60%; the old 80% floor
    silently replaced it with 100%, so its benchmark did not match the profile. */
 if (Cvar_VariableValue("viewsize")<60) Cvar_SetValue("viewsize",100);
}
