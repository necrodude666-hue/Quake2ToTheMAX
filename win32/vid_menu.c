#include "../qcommon/software_smp.h"
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
#include "../client/client.h"
#include "../client/render_scale.h"
#include "../client/gl_fidelity.h"
#include "../client/qmenu.h"
#include "../client/tweak_settings.h"
#include "../qcommon/q3e_3dnow.h"	/* BDGN 3.40: CPU detection for the greyed rows */

#define REF_SOFT		0
#define REF_OPENGL		1
#define REF_3DFX		2
#define REF_POWERVR		3
// SMP support
#define REF_MT_OPENGL	4
//#define REF_MT_3DFX		5
// end
#define REF_SWAM        7
#define REF_VERITE		98	// (not in the list; 3.51ak: moved off 8)
#define REF_S3VIRGE		8	// BDGN 3.51ak: ref_gl + S3VirgeOpenGL.dll (Techland S3 Virge driver)
#define REF_3DNOW_S3VIRGE	9	// BDGN 3.51ak: ref_glam.dll + S3VirgeOpenGL.dll
#define S3V_DRIVER		"S3VirgeOpenGL"
/* BDGN 3.51ast: Rendition Verite. REF_RENDITION_V1K = Rendition's native Quake II renderer ref_v1k.dll (vid_ref v1k,
   RRedline, V1000 and V2x00, 16-bit desktop, v1k_mode 0-3 = 320x240..640x480, 4 = 800x600 on V2x00 only; beta 5 from
   refv1kb5, shipped in Quake2/ since 3.51ast). REF_RENDITION_GL = ref_gl on the Rendition V2x00 OpenGL ICD (gl_driver opengl32, remembered
   with vid_rendition 1). Both get the Rendition presets. */
#define REF_RENDITION_V1K	10
#define REF_RENDITION_GL	11
#define V1K_MAX_MODE		4
/* BDGN 3.52i: 3dfx Voodoo. REF_3DFX = 3dfx's Quake miniGL (gl_driver 3dfxgl, 3dfxgl.dll); REF_3DFX_GLIDE = 3dfx's
   standalone OpenGL on Glide for Voodoo Graphics / Voodoo2 (gl_driver 3dfxvgl, 3dfxvgl.dll, the "Voodoo GL" ICD that
   Quake III loads with r_glDriver 3dfxvgl). Both get the 3dfx presets (14-16, listed only with them; the 3dfx preset is
   applied once when one of them is chosen). Voodoo1-3 render 16-bit only. */
#define REF_3DFX_GLIDE		12
#define FX_GLIDE_DRIVER		"3dfxvgl"
/* BDGN 3.52k: more 3dfx drivers (all shipped in Quake2/) */
#define REF_WICKED		13	// ref_gl + Wicked3DFXgl.dll (Metabyte WickedGL 4.99, Voodoo2/3/5: boards with 2 texture units)
#define REF_3DNOW_3DFX		14	// ref_glam.dll + 3dfxglam.dll (3dfx's Quake II OpenGL that AMD paired with ref_glam; Voodoo1/2/Rush)
#define REF_MT_3DFX		15	// 3.52k: ref_gl + mtgl32.dll running 3dfxgl.dll (mt_gl.ini), 2+ processors (test)
#define REF_MT_3DNOW_3DFX	16	// 3.52k: ref_glam + mtgl32.dll running 3dfxglam.dll, 2+ processors and 3DNow! (test)
#define REF_MT_3DFX_GLIDE	17	// ref_gl + mtgl32.dll running 3dfxvgl.dll
#define REF_MT_WICKED		18
#define REF_MT_RENDITION	19
#define REF_COUNT		20	// entries in refs[]
#define WICKED_DRIVER		"Wicked3DFXgl"
#define FXGLAM_DRIVER		"3dfxglam"
int VID_DriverDllFound (const char *name);	/* vid_dll.c */
int VID_HardwareFlags (void);			/* vid_dll.c, BDGN 3.52k */
qboolean VID_SetMtGlTarget (const char *dll);	/* vid_dll.c, BDGN 3.52k: mt_gl.ini */
void VID_GetMtGlTarget (char *dll, int size);
#define HW_V1		1
#define HW_V2		2
#define HW_BANSHEE	4
#define HW_V3		8
#define HW_V45		16
#define HW_RUSH		32
#define HW_PVR		64
#define HW_S3V		128
#define HW_V1000	256
#define HW_V2X00	512
#define HW_RIVA		1024	/* BDGN 3.52m: cards on the system OpenGL driver */
#define HW_MGA		2048
#define HW_RAGE		4096
#define HW_I740		8192
#define HW_SAVAGE	16384
#define HW_LOWEND	32768
#define HW_GF23		65536	/* BDGN 3.58d: GeForce2 / GeForce3 (vid_dll.c) */
#define HW_FX1		( HW_V1 | HW_RUSH | HW_BANSHEE )	/* 3dfx boards with one texture unit */
#define HW_FX2		( HW_V2 | HW_V3 | HW_V45 )		/* 3dfx boards with two texture units */
/* BDGN 3.51ak: S3 Virge boards have 4 MB (some 2 MB) for front, back and Z buffer and textures; the driver only runs
   15/16-bit modes. Modes up to 640x480 are offered (640x480x16 with double buffer and Z = 1.8 MB, leaving ~2 MB for
   textures on a 4 MB board; 800x600 would leave ~1 MB, below what Quake II's textures need). */
#define S3V_MAX_PIXELS	(640 * 480)
/* BDGN 3.51al: config preset names; the S3 Virge preset is listed only with the S3 Virge renderers */
static const char *preset_names[] = { "keep current", "Fast", "Faster", "Ultra Fast", "default config", "MAX Fidelity", 0 };
static const char *preset_names_s3v[] = { "keep current", "Fast", "Faster", "Ultra Fast", "default config", "MAX Fidelity", "S3 Virge (fast)", "S3 Virge ULTRA (worse graphics)", 0 };
/* BDGN 3.51ast: list entries 6/7 are presets 8/9 (Rendition files) with the Rendition renderers */
static const char *preset_names_rend[] = { "keep current", "Fast", "Faster", "Ultra Fast", "default config", "MAX Fidelity", "Rendition (fast)", "Rendition ULTRA (worse graphics)", 0 };
#define REF_3DNOW		5	// ref_glam.dll (AMD 3DNow! OpenGL renderer)
#define REF_MT_3DNOW	6	// ref_glam.dll together with mtgl32.dll (SMP OpenGL driver)

// fov cvar
extern cvar_t *fov;
extern cvar_t *dfov;
extern cvar_t *zfov;
// disable textures
static cvar_t *gl_hack_notextures;
static cvar_t *gl_hack_noimages;	// Doogie: do not load wall/sky images at all (all surfaces use the notexture image)
extern cvar_t *vid_ref;
extern cvar_t *vid_fullscreen;
extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;

static cvar_t *gl_mode;
static cvar_t *gl_driver;
static cvar_t *gl_picmip;
static cvar_t *gl_ext_palettedtexture;
static cvar_t *gl_3dnow;
static cvar_t *gl_drawdist;
// BDGN 3.4 performance toggles (from Breakfast Pie)
static cvar_t *gl_fastworld;
static cvar_t *gl_fastdraw;
static cvar_t *gl_sse;
static cvar_t *gl_mmx;
static cvar_t *gl_shadows;
static cvar_t *gl_dynamic;
static cvar_t *gl_fastparticles;
static cvar_t *g_gibmode;
static cvar_t *g_noflies;
static cvar_t *gl_finish;

static cvar_t *sw_mode;
static cvar_t *sw_stipplealpha;

extern void M_ForceMenuOff( void );

/*
====================================================================

MENU INTERACTION

====================================================================
*/
#define SOFTWARE_MENU 0
#define OPENGL_MENU   1

static menuframework_s  s_software_menu;
static menuframework_s	s_opengl_menu;
static menuframework_s *s_current_menu;
/* BDGN 3.55i: Video sub-pages (index OPENGL_MENU / SOFTWARE_MENU like the main pages) */
static menuframework_s s_pic_menu[2], s_perf_menu[2], *s_sub_current;
static int s_sub_scroll;
static const char *hd_set_names[8];
static int hd_set_values[8];
static int				s_current_menu_index;

static menulist_s		s_mode_list[2];
//testing add new mode list
static menulist_s		s_mode_list_new[2];
//
static menulist_s		s_ref_list[2];
// fov
static menuslider_s		s_fov_slider;
//
static menuslider_s		s_tq_slider;
static menulist_s		s_hdtexture_box;
static menulist_s		s_hd4k_size_box;	/* BDGN 3.55w: largest level of the 4K set (gl_hd_maxsize) */
static const char *hd4k_size_names[]={"4096 (4K)","2048","1024",NULL};
static const int hd4k_size_values[]={4096,2048,1024};
static menulist_s		s_gl_filter_box, s_gl_anisotropy_box, s_gl_msaa_box;
static int gl_msaa_choices[4], gl_msaa_at_open;
static const char *gl_msaa_labels[5];
static const char *gl_filter_labels[]={
	"nearest (no mipmaps)", "linear (no mipmaps)", "nearest mipmap",
	"bilinear", "nearest + mip blend", "trilinear", NULL
};
static const char *gl_filter_modes[]={
	"GL_NEAREST", "GL_LINEAR", "GL_NEAREST_MIPMAP_NEAREST",
	"GL_LINEAR_MIPMAP_NEAREST", "GL_NEAREST_MIPMAP_LINEAR", "GL_LINEAR_MIPMAP_LINEAR"
};
static const int gl_anisotropy_levels[]={1,2,4,8,16};
static const char *gl_anisotropy_labels[]={"off","2x","4x","8x","16x"};
static const char *gl_anisotropy_choices[6];
static menuslider_s		s_screensize_slider[2];
static menuslider_s		s_brightness_slider[2];
static menulist_s  		s_fs_box[2];
static menulist_s  		s_stipple_box;
static menuslider_s		s_render_scale_box[2];
static menulist_s		s_upscale_filter, s_gl_post_boxes[5];
static const char *gl_post_cvars[]={"gl_fxaa","gl_bloom","gl_ssao","gl_tonemap","gl_colour_grade"};
#define GL_POST_MENU_COUNT (sizeof(gl_post_cvars)/sizeof(gl_post_cvars[0]))
static const char *sw_texfilter_names[]={"off (sharp pixels)","dithered (Unreal style)",NULL};
static const char *sw_quality_filter_names[]={"off (sharp pixels)","dithered (Unreal style)","bilinear","trilinear",NULL};
static menulist_s s_sw_threads_box;
static menulist_s		s_texfilter_box;	/* BDGN 3.52n: software texture filtering (dithered, Unreal style) */
static menulist_s		s_swcolor_box;		/* BDGN 3.52p: software coloured lighting (like OpenGL) */
static menuslider_s		s_swdrawdist_slider;	/* BDGN 3.52r: software draw distance + fog */
static menulist_s		s_swprefetch_box;	/* BDGN 3.52r: prefetch test toggle */
static menulist_s  		s_paletted_texture_box;
// disable textures
static menulist_s		s_disable_textures_box;
static menulist_s		s_noimages_box;
static menulist_s		s_flat_list;			// BDGN 3.47: "single colour textures" for ref_gl = r_drawflat
static int				flat_at_open;
static const int		flat_values[4] = { 0, 2, 3, 1 };	// list index -> r_drawflat (off, material, grayscale, random)
static menulist_s		s_lowtex_box;
static menulist_s		s_s3v_dither_box, s_s3v_square_box, s_s3v_alpha_box, s_s3v_math_box;	/* BDGN 3.51ak */
static menulist_s		s_fx_tb_box, s_fx_hz_box, s_fx_dither_box, s_fx_fast_box, s_fx_filter_box, s_fx_cpu_box;	/* BDGN 3.52l: 3dfx driver options */
static const int fx_hz_values[] = { 0, 60, 72, 75, 85, 100, 120 };
static const int		s3v_alpha_values[] = { 40, 80, 128, 192 };
static menulist_s		s_v1k_aa_box, s_v1k_warp_box, s_v1k_flip_box, s_v1k_lookup_box, s_v1k_rate_box;	/* BDGN 3.51ast: ref_v1k */
static const int		v1k_aa_values[] = { 0, 1, 3, 7 };	/* off, world, world + models, world + models + particles */
static const int		v1k_rate_values[] = { 60, 72, 75, 85 };
static menulist_s		s_smp_box;			/* BDGN 3.51p: sw_smp (ref_soft only) */
static int s_video_scroll[2];
static menulist_s s_audio_worker_box;
static int s_audio_worker_at_open;
static qboolean s_has_audio_workers;
static menulist_s		s_3dnow_box;
static menuslider_s		s_drawdist_slider;
static menulist_s		s_lightfx_box;
static menulist_s		s_preset_box;
static int				lightfx_touched;
static menulist_s		s_fastworld_box;
static menulist_s		s_sse_box, s_cpu_path_box;
static const char *cpu_path_names[4];
static menulist_s		s_mmx_box;
static menulist_s		s_vsync_box;		/* BDGN 3.40 */
static menulist_s		s_cpuauto_box;		/* BDGN 3.40: cpu_auto */
static menuaction_s		s_cancel_action[2];

extern void M_Menu_Speed_f( void );

static menuaction_s s_tweak_graphics_action[2], s_tweak_effects_action[2];
extern void M_Menu_TweaksGraphics_f (void);
extern void M_Menu_TweaksEffects_f (void);
extern void M_Menu_Benchmarks_f (void);
static menuaction_s s_benchmarks_action[2];
static void BenchmarksCallback (void *unused) { M_Menu_Benchmarks_f(); }
/* BDGN 3.55i: page links of the Video page */
static menuaction_s s_pic_action[2], s_light_action[2], s_fx_action[2], s_enemy_action[2], s_perf_action[2], s_paths_action[2], s_testing_action[2];
extern void M_PushMenu ( void (*draw) (void), const char *(*key) (int k) );
extern void M_PopMenu (void);
extern void M_Menu_TweaksLighting_f (void);
extern void M_Menu_TweaksEnemies_f (void);
extern void M_Menu_TweaksPaths_f (void);
extern void M_Menu_TweaksTesting_f (void);
extern void M_Menu_TweaksSimpler_f (void);	/* BDGN 3.57d */
extern void M_Menu_TweaksOldSchool_f (void);
/* BDGN 3.57d: the Video page in two groups, each with a heading: pages that make the game look better (and slower), and
   pages that make it faster (and simpler to look at) */
static menuseparator_s s_hdr_better[2], s_hdr_faster[2];
static menuaction_s s_simpler_action[2], s_old_action[2];
static const char *VID_Note (void *item);
static void VID_NoteLayout (menuframework_s *m);
static void ApplyChanges( void *unused );	/* 3.60f: used by the sub-pages */
static void VID_SubDraw (void)
{
	int w,h,top;
	re.DrawGetPicSize(&w,&h,"m_banner_video");
	if (h<0) h=0;
	top=viddef.height/12;
	re.DrawPic(viddef.width/2-w/2,top,"m_banner_video");
	Menu_AdjustCursor(s_sub_current,1);
	Menu_DrawScrolled(s_sub_current,top+h+8,viddef.height-20,&s_sub_scroll);
}
static const char *VID_SubKey (int key)
{
	menuframework_s *m=s_sub_current;
	switch (key)
	{
	case K_ESCAPE:
		M_PopMenu();	/* back to the Video page; leaving Video applies, as before */
		return "misc/menu3.wav";
	case K_KP_UPARROW: case K_MWHEELUP: case K_UPARROW:
		m->cursor--; Menu_AdjustCursor(m,-1); break;
	case K_KP_DOWNARROW: case K_MWHEELDOWN: case K_DOWNARROW:
		m->cursor++; Menu_AdjustCursor(m,1); break;
	case K_KP_LEFTARROW: case K_LEFTARROW:
		Menu_SlideItem(m,-1); break;
	case K_KP_RIGHTARROW: case K_RIGHTARROW:
		Menu_SlideItem(m,1); break;
	case K_KP_ENTER: case K_ENTER:
		/* BDGN 3.60f (owner 2026-11-22: choosing a texture pack did nothing): Enter on a row of a sub-page applies,
		   as on the Video page itself; before, it only went back and the change waited until Video was left */
		if (!Menu_SelectItem(m)) { M_PopMenu(); ApplyChanges(NULL); }
		break;
	}
	return "misc/menu1.wav";
}
/* BDGN 3.60f: every sub-page (picture & textures, performance; OpenGL and Software) ends with an "apply" row */
static menuaction_s s_sub_apply[4];
static void VID_SubApply (void *unused)
{
	M_PopMenu();
	ApplyChanges(NULL);
}
static void VID_SubAddApply (menuframework_s *m)
{
	int k=m==&s_pic_menu[0]?0:m==&s_pic_menu[1]?1:m==&s_perf_menu[0]?2:3;
	int i, y=0;
	menuaction_s *a=&s_sub_apply[k];
	for (i=0;i<m->nitems;i++) {
		menucommon_s *it=(menucommon_s *)m->items[i];
		if (it==&a->generic) return;	/* already there (the pages are rebuilt with nitems 0) */
		if (it->y>y) y=it->y;
	}
	if (m->nitems>=MAXMENUITEMS) return;
	memset(a,0,sizeof(*a));
	a->generic.type=MTYPE_ACTION;
	a->generic.name="apply";
	a->generic.y=y+(viddef.height<400?16:20);
	a->generic.callback=VID_SubApply;
	a->generic.statusbar="apply the changes (Enter on any row does the same)";
	Menu_AddItem(m,a);
}
static void VID_OpenSub (menuframework_s *m)
{
	if (!m->nitems) return;
	VID_SubAddApply(m);
	s_sub_current=m; s_sub_scroll=0;
	VID_NoteLayout(m);	/* BDGN 3.57d */
	if (m->cursor>=m->nitems || m->cursor<0) m->cursor=0;
	Menu_AdjustCursor(m,1);
	M_PushMenu(VID_SubDraw,VID_SubKey);
}
static void PicturePageCallback (void *unused) { VID_OpenSub(&s_pic_menu[s_current_menu_index]); }
static void PerformancePageCallback (void *unused) { VID_OpenSub(&s_perf_menu[s_current_menu_index]); }
static void LightingPageCallback (void *unused) { M_Menu_TweaksLighting_f(); }
static void EnemiesPageCallback (void *unused) { M_Menu_TweaksEnemies_f(); }
static void PathsPageCallback (void *unused) { M_Menu_TweaksPaths_f(); }
static void TestingPageCallback (void *unused) { M_Menu_TweaksTesting_f(); }	/* BDGN 3.55ai */
static void SimplerPageCallback (void *unused) { M_Menu_TweaksSimpler_f(); }	/* BDGN 3.57d */
static void OldSchoolPageCallback (void *unused) { M_Menu_TweaksOldSchool_f(); }
static void GraphicsTweaksCallback (void *unused) { M_Menu_TweaksGraphics_f(); }
static void EffectsTweaksCallback (void *unused) { M_Menu_TweaksEffects_f(); }

/* choosing 3DNow!/SSE/MMX by hand switches cpu_auto to "manual" */
static void CpuRowCallback( void *unused )
{
	s_cpuauto_box.curvalue = 1;
}
static menuaction_s		s_defaults_action[2];

/*
** BDGN 3.37: aspect ratio filter for the OpenGL "video mode" list.  vid_aspect: 0 = all,
** 1 = 4:3, 2 = 5:4, 3 = 16:10, 4 = 16:9, 5 = 21:9.  The list only shows the modes of the
** chosen ratio; s_res_map[] converts the list index back to the real gl_mode number.
*/
#define NUM_GL_MODES	28
static const int res_wh[NUM_GL_MODES][2] =		// same order as vid_modes[] in vid_dll.c / resolutions_new[]
{
	{ 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 }, { 800, 600 }, { 960, 720 }, { 1024, 768 },
	{ 1152, 864 }, { 1280, 960 }, { 1280, 1024 }, { 1600, 1200 }, { 2048, 1536 },
	{ 1280, 720 }, { 1280, 800 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 }, { 1680, 1050 },
	{ 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 2560, 1600 }, { 3200, 1800 }, { 3840, 2160 },
	{ 3840, 2400 }, { 2560, 1080 }, { 3440, 1440 }, { 3840, 1600 }
};
static const char		**s_res_all;				// all mode names (resolutions_new in VID_MenuInit)
static const char		*s_res_names[NUM_GL_MODES + 1];	// the names that are shown now
static int				s_res_map[NUM_GL_MODES];		// list index -> gl_mode
static int				s_res_count;
static cvar_t			*vid_aspect;
static menulist_s		s_aspect_list;
static menulist_s s_sw_aspect_list;
static const char *s_sw_res_names[NUM_GL_MODES+1];
static int s_sw_res_map[NUM_GL_MODES], s_sw_res_count;
static cvar_t *sw_aspect;
static menuaction_s s_sw_apply_action, s_gl_apply_action;	/* 3.60f: OpenGL has an "apply" row too */

/* aspect class of a mode: 1 = 4:3, 2 = 5:4, 3 = 16:10, 4 = 16:9, 5 = 21:9 */
static int ResAspectClass( int mode )
{
 int w,h;
 if(mode<0 || mode>=NUM_GL_MODES) return 0;
 w=res_wh[mode][0]; h=res_wh[mode][1];
 if(w*3==h*4) return 1;
 if(w*4==h*5) return 2;
 if(w*10==h*16) return 3;
 /* 1366x768 rounds the ideal 16:9 width by two-thirds of one pixel. */
 if(abs(w*9-h*16)<=9) return 4;
 /* The installed ultrawide modes use the conventional 21:9 monitor family. */
 if((w==2560 && h==1080) || (w==3440 && h==1440) || (w==3840 && h==1600)) return 5;
 return 0;
}

/* fill the video mode list with the modes of the chosen aspect ratio; selects 'mode' if it is in the list */
/* Shared classification/list generation for both renderers. */
static int FillModeList(int aspect,int limit,const char **names,int *map)
{
 int i,n=0;
 for(i=0;i<limit;i++) {
  if(!aspect || ResAspectClass(i)==aspect) { names[n]=s_res_all[i];map[n]=i;n++; }
 }
 names[n]=NULL;
 return n;
}

static int VID_S3VirgeSelected (void)
{
 return s_ref_list[OPENGL_MENU].curvalue==REF_S3VIRGE || s_ref_list[OPENGL_MENU].curvalue==REF_3DNOW_S3VIRGE;
}
static int VID_V1KSelected (void)
{
 return s_ref_list[OPENGL_MENU].curvalue==REF_RENDITION_V1K;
}
static int VID_3dfxSelected (void)	/* BDGN 3.52i; 3.52k: + WickedGL, 3DNow! 3Dfx */
{
 int r=s_ref_list[OPENGL_MENU].curvalue;
 return r==REF_3DFX || r==REF_3DFX_GLIDE || r==REF_WICKED || r==REF_3DNOW_3DFX ||
  r==REF_MT_3DFX || r==REF_MT_3DNOW_3DFX || r==REF_MT_3DFX_GLIDE || r==REF_MT_WICKED;
}
/* BDGN 3.60: case-insensitive strstr */
static int Q_stristr_bdgn (const char *s, const char *sub)
{
 int n=(int)strlen(sub);
 for (;s && *s;s++) if (!Q_strncasecmp((char *)s,(char *)sub,n)) return 1;
 return 0;
}
static int VID_PowerVRSelected (void)	/* BDGN 3.52k */
{
 return s_ref_list[OPENGL_MENU].curvalue==REF_POWERVR;
}
/* BDGN 3.52k: the drivers offered for this computer: processors (SMP), 3DNow! and the graphics cards found by
   VID_HardwareFlags. The driver that is in use stays in the list even when it doesn't match (s_ref_inuse). */
static int s_ref_inuse = -1, s_ref_prev = -1;
static int CPUHas3DNow( void );
static qboolean VID_RefExcludedOnVoodoo1(int ref, int hardware)
{
 return (hardware&(HW_FX1|HW_FX2))==HW_V1 &&
        (ref==REF_3DFX_GLIDE || ref==REF_MT_3DFX_GLIDE || ref==REF_WICKED || ref==REF_MT_WICKED);
}

static int VID_RefVisible (int ref)
{
 int hw=VID_HardwareFlags(),amd=CPUHas3DNow(),nsys=1,nproc=1,smp;
 if((ref==REF_MT_RENDITION && !Cvar_VariableValue("bdgn_rendition_smp_test")) ||
    VID_RefExcludedOnVoodoo1(ref,hw)) return 0;
 if(ref==s_ref_inuse) return 1;
 Sys_ProcessorCounts(&nsys,&nproc);
 smp=nproc>=2 || Cvar_VariableValue("bdgn_hw_all");
 if(Cvar_VariableValue("bdgn_hw_all")) amd=1;
 switch(ref) {
 case REF_SOFT: case REF_OPENGL: return 1;
 case REF_SWAM: case REF_3DNOW: return amd;
 case REF_MT_OPENGL: return smp;
 case REF_MT_3DNOW: return smp && amd;
 case REF_3DFX: return (hw&(HW_FX1|HW_FX2))!=0;			/* miniGL 1.47: every Voodoo */
 case REF_3DFX_GLIDE: return (hw&(HW_V2|HW_BANSHEE|HW_V3|HW_V45))!=0;	/* 3dfx ICD on Glide3 */
 case REF_WICKED: return (hw&HW_FX2)!=0;				/* needs two texture units */
 case REF_3DNOW_3DFX: return amd && (hw&(HW_V1|HW_V2|HW_RUSH));	/* 3dfxglam: Voodoo, Voodoo2, Rush */
 case REF_MT_3DFX: return smp && (hw&(HW_FX1|HW_FX2)) &&
  VID_DriverDllFound("mtgl32") && VID_DriverDllFound("3dfxgl");
 case REF_MT_WICKED: return smp && (hw&HW_FX2) &&
  VID_DriverDllFound("mtgl32") && VID_DriverDllFound(WICKED_DRIVER);
 case REF_MT_3DFX_GLIDE: return smp && (hw&(HW_V2|HW_BANSHEE|HW_V3|HW_V45)) &&
  VID_DriverDllFound("mtgl32") && VID_DriverDllFound(FX_GLIDE_DRIVER);
 case REF_MT_3DNOW_3DFX: return smp && amd && (hw&(HW_V1|HW_V2|HW_RUSH)) &&
  VID_DriverDllFound("mtgl32") && VID_DriverDllFound("ref_glam") && VID_DriverDllFound(FXGLAM_DRIVER);
 case REF_POWERVR: return (hw&HW_PVR)!=0;
 case REF_S3VIRGE: return (hw&HW_S3V)!=0;
 case REF_3DNOW_S3VIRGE: return amd && (hw&HW_S3V);
 case REF_RENDITION_V1K: return (hw&(HW_V1000|HW_V2X00))!=0;
 case REF_RENDITION_GL: return (hw&HW_V2X00)!=0;
 case REF_MT_RENDITION: return smp && (hw&HW_V2X00) &&
  Cvar_VariableValue("bdgn_rendition_smp_test") && VID_DriverDllFound("mtgl32");
 }
 return 0;
}
/* left/right in the driver list jumps over the drivers that are not offered */
static void VID_SkipHiddenRef (void)
{
 int cur=s_ref_list[s_current_menu_index].curvalue,dir;
 if(s_ref_prev<0) s_ref_prev=cur;
 dir=cur>=s_ref_prev?1:-1;
 while(cur>=0 && cur<REF_COUNT && !VID_RefVisible(cur)) cur+=dir;
 if(cur<0 || cur>=REF_COUNT) cur=s_ref_prev;
 s_ref_list[s_current_menu_index].curvalue=cur;
 s_ref_prev=cur;
}
static int VID_RenditionSelected (void)
{
 return VID_V1KSelected() || s_ref_list[OPENGL_MENU].curvalue==REF_RENDITION_GL ||
  s_ref_list[OPENGL_MENU].curvalue==REF_MT_RENDITION;
}
static void BuildModeList(int aspect,int mode)
{
 int i,n,found=0;
 if(VID_V1KSelected()) {	/* BDGN 3.51ast: ref_v1k has v1k_mode 0-4 = the first five (4:3) modes */
  s_res_count=FillModeList(0,V1K_MAX_MODE+1,s_res_names,s_res_map);
  s_mode_list_new[OPENGL_MENU].itemnames=s_res_names;
  s_mode_list_new[OPENGL_MENU].curvalue=mode>=0 && mode<=V1K_MAX_MODE?mode:2;	/* default 512x384 */
  return;
 }
 s_res_count=FillModeList(aspect,NUM_GL_MODES,s_res_names,s_res_map);
 if(VID_S3VirgeSelected()) {	/* BDGN 3.51ak: 4 MB card, modes up to 640x480 only */
  for(i=0,n=0;i<s_res_count;i++) {
   int m=s_res_map[i];
   if(res_wh[m][0]*res_wh[m][1]<=S3V_MAX_PIXELS) {s_res_names[n]=s_res_names[i];s_res_map[n++]=m;}
  }
  s_res_count=n;s_res_names[n]=NULL;
  if(!s_res_count) {	/* an aspect filter with no small modes: fall back to every allowed mode */
   s_res_count=FillModeList(0,NUM_GL_MODES,s_res_names,s_res_map);
   for(i=0,n=0;i<s_res_count;i++) if(res_wh[s_res_map[i]][0]*res_wh[s_res_map[i]][1]<=S3V_MAX_PIXELS) {s_res_names[n]=s_res_names[i];s_res_map[n++]=s_res_map[i];}
   s_res_count=n;s_res_names[n]=NULL;
  }
 }
 s_mode_list_new[OPENGL_MENU].itemnames=s_res_names;
 s_mode_list_new[OPENGL_MENU].curvalue=0;
 for(i=0;i<s_res_count;i++) if(s_res_map[i]==mode) {s_mode_list_new[OPENGL_MENU].curvalue=i;found=1;}
 if(!found && VID_S3VirgeSelected())	/* default on S3 Virge: 512x384 */
  for(i=0;i<s_res_count;i++) if(s_res_map[i]==2) s_mode_list_new[OPENGL_MENU].curvalue=i;
}

static void BuildSoftwareModeList(int aspect,int mode)
{
 int i,n,swam=s_ref_list[SOFTWARE_MENU].curvalue==REF_SWAM;
 int extended=Cvar_VariableValue("sw_bdgn_features")>=3518;
 int limit=extended?NUM_GL_MODES:12;
 if(swam && aspect>4) aspect=0;
 if(aspect<0 || aspect>5 || (limit==12 && aspect>2)) aspect=0;
 s_sw_aspect_list.curvalue=aspect;
 s_sw_res_count=FillModeList(aspect,limit,s_sw_res_names,s_sw_res_map);
 if(swam) {
  for(i=0,n=0;i<s_sw_res_count;i++) {
   int m=s_sw_res_map[i];
   if(res_wh[m][0]<=1600 && res_wh[m][1]<=1200) {
    s_sw_res_names[n]=s_sw_res_names[i];s_sw_res_map[n++]=m;
   }
  }
  s_sw_res_count=n;s_sw_res_names[n]=NULL;
 }
 s_mode_list[SOFTWARE_MENU].itemnames=s_sw_res_names;
 s_mode_list[SOFTWARE_MENU].curvalue=0;
 for(i=0;i<s_sw_res_count;i++) if(s_sw_res_map[i]==mode) s_mode_list[SOFTWARE_MENU].curvalue=i;
}

static void SoftwareAspectCallback(void *unused)
{
 int index=s_mode_list[SOFTWARE_MENU].curvalue;
 int mode=index>=0 && index<s_sw_res_count?s_sw_res_map[index]:3;
 Cvar_SetValue("sw_aspect",s_sw_aspect_list.curvalue);
 BuildSoftwareModeList(s_sw_aspect_list.curvalue,mode);
}

static void AspectCallback( void *unused )
{
	int mode = s_res_map[s_mode_list_new[OPENGL_MENU].curvalue];

	Cvar_SetValue( "vid_aspect", s_aspect_list.curvalue );
	BuildModeList( s_aspect_list.curvalue, mode );
}

/* BDGN 3.51bat: 3DNow!, SSE and MMX status rows.  Nothing to choose: the renderer switches them on by itself
   (3DNow! when the CPU has it; otherwise SSE and MMX when the CPU has them and their self-test passes) and publishes
   r_simd_3dnow / r_simd_sse / r_simd_mmx: 1 on, 0 not supported, 2 self-test failed, 3 not used (3DNow! is used). */
/* BDGN 3.51bdt: the list in the menu is built from these: entries and the preset number each one applies */
#define VID_AUTO_PRESET 90	/* BDGN 3.55o: "Auto" in the preset list */
static const char *s_preset_list[80];
static int s_preset_ids[80] = { 0, 1, 2, 3, 4, 5 };
/* BDGN 3.57h: every preset this computer offers (s_all_*), each in a category; the menu shows one category at a time
   (s_preset_list / s_preset_ids) */
enum { PCAT_HIDDEN=-1, PCAT_GENERAL, PCAT_LOW, PCAT_MEDIUM, PCAT_HIGH, PCAT_ULTRA, PCAT_HIGHFI, PCAT_SOFTWARE, PCAT_3DFX,
	PCAT_FAMILY, PCAT_LOWRAM, PCAT_COUNT };
static const char *pcat_names[PCAT_COUNT]={"general","Low","Medium","High","Ultra","High Fidelity","Software","3dfx",
	"card family","low RAM"};
#define PRESET_SW_BASE 200	/* Software category from the OpenGL menu: 200 + preset, renderer switched to Software */
#define PRESET_FX_BASE 300	/* 3dfx category: 300 + preset, renderer switched to 3dfx's miniGL */
static const char *s_all_list[120];
static int s_all_ids[120], s_all_cat[120], s_all_count, s_catalog_count;
static const char *s_cat_names[PCAT_COUNT+1];
static int s_cat_map[PCAT_COUNT];
static menulist_s s_preset_cat_box;
/* BDGN 3.52k: hardware presets: default / fast / faster / ultra for every card family, listed only with its drivers.
   6-9 and 14-16 keep their old numbers. */
#define PF_S3	1
#define PF_REND	2
#define PF_FX1	4
#define PF_FX2	8
#define PF_PVR	16
#define PF_RIVA	32	/* BDGN 3.52m: presets for cards on the system OpenGL driver (default / SMP / 3DNow! OpenGL) */
#define PF_MGA	64
#define PF_RAGE	128
#define PF_I740	256
#define PF_SAVAGE	512
#define PF_LOWEND	1024
#define PF_K6GF		2048	/* BDGN 3.58d: AMD K6-2/K6-III (also +) with a GeForce2/3 on the system OpenGL driver */
typedef struct { int id, family; const char *label, *file, *name; } hwpreset_t;
static const hwpreset_t hw_presets[] = {
	{ 17, PF_S3,   "S3 Virge default",                 "s3virgedefault.cfg",   "s3virgedefault" },
	{ 6,  PF_S3,   "S3 Virge fast",                    "s3virgeconfig.cfg",    "s3virge" },
	{ 18, PF_S3,   "S3 Virge faster",                  "s3virgefaster.cfg",    "s3virgefaster" },
	{ 7,  PF_S3,   "S3 Virge Ultra Fast",  "s3virgeultra.cfg",     "s3virgeultra" },
	{ 54,  PF_S3,   "S3 Virge PSYCHO Fast",  "s3virgepsycho.cfg",     "s3virgepsycho" },
	{ 19, PF_REND, "Rendition default",                "renditiondefault.cfg", "renditiondefault" },
	{ 8,  PF_REND, "Rendition fast",                   "renditionconfig.cfg",  "rendition" },
	{ 20, PF_REND, "Rendition faster",                 "renditionfaster.cfg",  "renditionfaster" },
	{ 9,  PF_REND, "Rendition Ultra Fast", "renditionultra.cfg",   "renditionultra" },
	{ 55,  PF_REND, "Rendition PSYCHO Fast", "renditionpsycho.cfg",   "renditionpsycho" },
	{ 22, PF_FX1,  "Voodoo1/Rush/Banshee default",     "voodoo1config.cfg",    "voodoo1" },
	{ 23, PF_FX1,  "Voodoo1/Rush/Banshee fast",        "voodoo1fast.cfg",      "voodoo1fast" },
	{ 24, PF_FX1,  "Voodoo1/Rush/Banshee faster",      "voodoo1faster.cfg",    "voodoo1faster" },
	{ 25, PF_FX1,  "Voodoo1/Rush/Banshee Ultra Fast",       "voodoo1ultra.cfg",     "voodoo1ultra" },
	{ 56, PF_FX1,  "Voodoo1/Rush/Banshee PSYCHO Fast",       "voodoo1psycho.cfg",     "voodoo1psycho" },
	{ 14, PF_FX2,  "Voodoo2/3/4/5 default",            "3dfxconfig.cfg",       "3dfx" },
	{ 15, PF_FX2,  "Voodoo2/3/4/5 fast",               "3dfxfast.cfg",         "3dfxfast" },
	{ 21, PF_FX2,  "Voodoo2/3/4/5 faster",             "3dfxfaster.cfg",       "3dfxfaster" },
	{ 16, PF_FX2,  "Voodoo2/3/4/5 Ultra Fast",              "3dfxultra.cfg",        "3dfxultra" },
	{ 57, PF_FX2,  "Voodoo2/3/4/5 PSYCHO Fast",              "3dfxpsycho.cfg",        "3dfxpsycho" },
	{ 26, PF_PVR,  "PowerVR default",                  "powervrconfig.cfg",    "powervr" },
	{ 27, PF_PVR,  "PowerVR fast",                     "powervrfast.cfg",      "powervrfast" },
	{ 28, PF_PVR,  "PowerVR faster",                   "powervrfaster.cfg",    "powervrfaster" },
	{ 29, PF_PVR,  "PowerVR Ultra Fast",   "powervrultra.cfg",     "powervrultra" },
	{ 58, PF_PVR,  "PowerVR PSYCHO Fast",   "powervrpsycho.cfg",     "powervrpsycho" },
	{ 30, PF_RIVA,   "Riva 128/TNT default", "rivaconfig.cfg",        "riva" },
	{ 31, PF_RIVA,   "Riva 128/TNT fast", "rivafast.cfg",          "rivafast" },
	{ 32, PF_RIVA,   "Riva 128/TNT faster", "rivafaster.cfg",        "rivafaster" },
	{ 33, PF_RIVA,   "Riva 128/TNT Ultra Fast", "rivaultra.cfg",         "rivaultra" },
	{ 59, PF_RIVA,   "Riva 128/TNT PSYCHO Fast", "rivapsycho.cfg",         "rivapsycho" },
	{ 34, PF_MGA,    "Matrox G200/G400 default", "matroxconfig.cfg",      "matrox" },
	{ 35, PF_MGA,    "Matrox G200/G400 fast", "matroxfast.cfg",        "matroxfast" },
	{ 36, PF_MGA,    "Matrox G200/G400 faster", "matroxfaster.cfg",      "matroxfaster" },
	{ 37, PF_MGA,    "Matrox G200/G400 Ultra Fast", "matroxultra.cfg",       "matroxultra" },
	{ 60, PF_MGA,    "Matrox G200/G400 PSYCHO Fast", "matroxpsycho.cfg",       "matroxpsycho" },
	{ 38, PF_RAGE,   "ATI Rage default", "rageconfig.cfg",        "rage" },
	{ 39, PF_RAGE,   "ATI Rage fast", "ragefast.cfg",          "ragefast" },
	{ 40, PF_RAGE,   "ATI Rage faster", "ragefaster.cfg",        "ragefaster" },
	{ 41, PF_RAGE,   "ATI Rage Ultra Fast", "rageultra.cfg",         "rageultra" },
	{ 61, PF_RAGE,   "ATI Rage PSYCHO Fast", "ragepsycho.cfg",         "ragepsycho" },
	{ 42, PF_I740,   "Intel i740 default", "i740config.cfg",        "i740" },
	{ 43, PF_I740,   "Intel i740 fast", "i740fast.cfg",          "i740fast" },
	{ 44, PF_I740,   "Intel i740 faster", "i740faster.cfg",        "i740faster" },
	{ 45, PF_I740,   "Intel i740 Ultra Fast", "i740ultra.cfg",         "i740ultra" },
	{ 62, PF_I740,   "Intel i740 PSYCHO Fast", "i740psycho.cfg",         "i740psycho" },
	{ 46, PF_SAVAGE, "S3 Savage default", "savageconfig.cfg",      "savage" },
	{ 47, PF_SAVAGE, "S3 Savage fast", "savagefast.cfg",        "savagefast" },
	{ 48, PF_SAVAGE, "S3 Savage faster", "savagefaster.cfg",      "savagefaster" },
	{ 49, PF_SAVAGE, "S3 Savage Ultra Fast", "savageultra.cfg",       "savageultra" },
	{ 63, PF_SAVAGE, "S3 Savage PSYCHO Fast", "savagepsycho.cfg",       "savagepsycho" },
	{ 50, PF_LOWEND, "budget 3D card default", "budget3dconfig.cfg",    "budget3d" },
	{ 51, PF_LOWEND, "budget 3D card fast", "budget3dfast.cfg",      "budget3dfast" },
	{ 52, PF_LOWEND, "budget 3D card faster", "budget3dfaster.cfg",    "budget3dfaster" },
	{ 53, PF_LOWEND, "budget 3D card Ultra Fast", "budget3dultra.cfg",     "budget3dultra" },
	{ 64, PF_LOWEND, "budget 3D card PSYCHO Fast", "budget3dpsycho.cfg",     "budget3dpsycho" },
	/* BDGN 3.58d: K6 + GeForce2/3 (owner, 2026-11-20). The general presets (same cfg files, no new PAK) with what the
	   owner's K6-3+ 616 MHz + GeForce2 GTS measured: 3.58j multitexture ON in fast, faster, Ultra Fast and PSYCHO
	   (K6-3 Special, 3.57x: Faster 146.81 -> 154.31 fps with it, +5.1 % MEASURED; owner's go, 2026-11-20), OFF in
	   default (owner's exception; otherwise the original 3.21 one), dynamic light off in faster,
	   Ultra Fast and PSYCHO (+8 % MEASURED, K6 OpenGL). VID_ApplyPresetSettings maps 84-88 to 4, 1, 2, 3 and 65. */
	{ 84, PF_K6GF,  "K6 + GeForce2/3 default",    "defaultconfig.cfg", "k6gfdefault" },
	{ 85, PF_K6GF,  "K6 + GeForce2/3 fast",       "fastconfig.cfg",    "k6gffast" },
	{ 86, PF_K6GF,  "K6 + GeForce2/3 faster",     "fasterconfig.cfg",  "k6gffaster" },
	{ 87, PF_K6GF,  "K6 + GeForce2/3 Ultra Fast", "ultraconfig.cfg",   "k6gfultra" },
	{ 88, PF_K6GF,  "K6 + GeForce2/3 PSYCHO Fast","psychoconfig.cfg",  "k6gfpsycho" },
};
#define HW_PRESET_COUNT ( (int)( sizeof( hw_presets ) / sizeof( hw_presets[0] ) ) )
static const hwpreset_t *VID_HwPreset (int id)
{
	int i;
	for ( i = 0; i < HW_PRESET_COUNT; i++ )
		if ( hw_presets[i].id == id )
			return &hw_presets[i];
	return NULL;
}
/* the 3dfx family for the presets: one texture unit (Voodoo1, Rush, Banshee) or two (Voodoo2/3/4/5); both when
   nothing or both kinds were found */
static int VID_FxFamilies (void)
{
	int hw = VID_HardwareFlags(), f = 0;
	if ( hw & HW_FX1 ) f |= PF_FX1;
	if ( hw & HW_FX2 ) f |= PF_FX2;
	return f ? f : PF_FX1 | PF_FX2;
}
qboolean VID_FidelityPresetAvailable(int id)
{
	const gl_fidelity_preset_t *tier=GL_FidelityPreset(id);
	const char *driver=Cvar_VariableString("gl_driver");
	int needed,post,msaa,smp=0;
 char target[MAX_QPATH];
 if (!Q_stricmp(driver,"mtgl32") || !Q_stricmp(driver,"mtgl32.dll")) {
  VID_GetMtGlTarget(target,sizeof(target));
  smp=!target[0] || !Q_stricmp(target,"opengl32") || !Q_stricmp(target,"opengl32.dll");
 }
	if (!tier || Q_stricmp(Cvar_VariableString("vid_ref"),"gl") ||
	    Cvar_VariableValue("vid_rendition") ||
	    (!smp && Q_stricmp(driver,"opengl32") && Q_stricmp(driver,"opengl32.dll")))
		return false;
 /* Native OpenGL through MTGL keeps the same quality profiles. The wrapper
    owns its GL context: individual shader/FBO controls retain their REAL caps.
    Never transfer native function pointers/capabilities to its worker. */
 if (smp) {
  if (Cvar_VariableValue("bdgn_legacy_graphics") || !Cvar_VariableValue("bdgn_modern_lighting_cpu") ||
      atoi(Cvar_VariableString("bdgn_gl_version"))<2) return false;
  return tier->hdset[0]!=5 || FS_HDTexSetAvailable(5);
 }
	if (tier->hdset[0]==5 && (!FS_HDTexSetAvailable(5) || !Cvar_VariableValue("bdgn_gl_dds_capable")))
		return false;	/* BDGN 3.55w: Ultra High Fidelity - 4K needs the 4K set and S3TC */
	needed=GL_VisualEffects(&tier->visual);
	post=(int)Cvar_VariableValue("bdgn_gl_post_available");
	msaa=(int)Cvar_VariableValue("bdgn_gl_msaa_modes");
	if (tier->pixel && (!Cvar_VariableValue("bdgn_gl_pixel_lighting_capable") || Cvar_VariableValue("bdgn_legacy_graphics")))
		return false;	/* BDGN 3.55i: High Fidelity needs the driver's shader lighting (SSE2, OpenGL 2, GLSL) */
	return (post&needed)==needed &&
	       (!tier->visual.msaa || (msaa&tier->visual.msaa)) &&
	       (tier->visual.scale==100 || Cvar_VariableValue("gl_render_scale_fbo_capable")) &&
	       Cvar_VariableValue("bdgn_gl_anisotropy_max")>=tier->anisotropy;
}

static void VID_BuildPresetCatalog( int s3v, int rend, int fx, int software )
{	/* BDGN 3.52k: fx = PF_FX1 / PF_FX2 families; PowerVR with the PowerVR driver */
	static const char *base[] = { "keep current", "Fast", "Faster", "Ultra Fast", "default config", "MAX Fidelity" };
	static const char *soft[] = { "keep current", "Software Fast", "Software Faster",
		"Software Ultra Fast", "default config", "Software MAX Fidelity" };
	static const char *lowram[] = { "low RAM default", "low RAM fast", "low RAM faster", "low RAM Ultra Fast", "low RAM PSYCHO Fast" };
	int n = 0, i;
	for ( i = 0 ; i < 6 ; i++ ) {
		s_all_list[n] = software ? soft[i] : base[i]; s_all_ids[n++] = i;
		if (i == 0 && !Cvar_VariableValue("bdgn_lowram")) {	/* BDGN 3.55o: low RAM machines keep their own start-up presets */
			s_all_list[n] = "Auto (best look, 125+ fps)";
			s_all_ids[n++] = VID_AUTO_PRESET;
		}
		if (i == 3) {
			s_all_list[n] = software ? "Software PSYCHO Fast" : "PSYCHO Fast";
			s_all_ids[n++] = 65;
		}
	}
	if (!software) {
		int fam = ( s3v ? PF_S3 : 0 ) | ( rend ? PF_REND : 0 ) | ( fx & ( PF_FX1 | PF_FX2 ) ) | ( VID_PowerVRSelected() && !s3v && !rend && !fx ? PF_PVR : 0 );
		int r = s_ref_list[OPENGL_MENU].curvalue;
		if ((r==REF_OPENGL || r==REF_MT_OPENGL) && (s_ref_inuse==REF_OPENGL || s_ref_inuse==REF_MT_OPENGL))
			for (i=0;i<(int)GL_VISUAL_PRESET_COUNT;i++)	/* BDGN 3.55i: Ultra and High Fidelity tiers the driver supports */
				if (VID_FidelityPresetAvailable(GL_VisualPresetId(i))) {
					s_all_list[n]=GL_FidelityPreset(GL_VisualPresetId(i))->visual.label;
					s_all_ids[n++]=GL_VisualPresetId(i);
				}
		if ( !s3v && !rend && !fx && ( r == REF_OPENGL || r == REF_MT_OPENGL || r == REF_3DNOW || r == REF_MT_3DNOW ) )
		{	/* BDGN 3.52m: the system OpenGL driver: presets for the cards found */
			int hw = VID_HardwareFlags ();
			if ( hw & HW_RIVA ) fam |= PF_RIVA;
			if ( hw & HW_MGA ) fam |= PF_MGA;
			if ( hw & HW_RAGE ) fam |= PF_RAGE;
			if ( hw & HW_I740 ) fam |= PF_I740;
			if ( hw & HW_SAVAGE ) fam |= PF_SAVAGE;
			if ( hw & HW_LOWEND ) fam |= PF_LOWEND;
			if ( ( hw & HW_GF23 ) && Cvar_VariableValue( "bdgn_cpu_k6" ) ) fam |= PF_K6GF;	/* 3.58d */
		}
		for ( i = 0; i < HW_PRESET_COUNT; i++ )
			if ( hw_presets[i].family & fam ) { s_all_list[n] = hw_presets[i].label; s_all_ids[n++] = hw_presets[i].id; }
	}
	if ( Cvar_VariableValue( "bdgn_lowram" ) )
		for ( i = 0 ; i < 5 ; i++ ) { s_all_list[n] = lowram[i]; s_all_ids[n++] = i == 4 ? 66 : 10 + i; }
	if (Cvar_VariableValue("bdgn_extra_lowram") && software) {	/* below 32 MB: the most extreme presets */
		s_all_list[n]="lowerThen32mb";
		s_all_ids[n++]=71;
		s_all_list[n]="lowerThen32mb EXTREME";	/* BDGN 3.55i */
		s_all_ids[n++]=72;
	}
	s_all_list[n] = 0;
	s_all_count = s_catalog_count = n;
	/* BDGN 3.57h: every entry gets its category */
	for ( i = 0 ; i < n ; i++ ) {
		int id = s_all_ids[i];
		if ( id == 0 || id == VID_AUTO_PRESET || id == 4 ) s_all_cat[i] = PCAT_GENERAL;
		else if ( id == 10 || id == 11 || id == 12 || id == 13 || id == 66 || id == 71 || id == 72 ) s_all_cat[i] = PCAT_LOWRAM;
		else if ( software ) s_all_cat[i] = PCAT_SOFTWARE;
		else if ( ( id >= 1 && id <= 5 ) || id == 65 || GL_FidelityPreset( id ) ) s_all_cat[i] = PCAT_HIDDEN;	/* shown through the tiers */
		else {
			int k, f = 0;
			for ( k = 0 ; k < HW_PRESET_COUNT ; k++ ) if ( hw_presets[k].id == id ) f = hw_presets[k].family;
			s_all_cat[i] = ( f & ( PF_FX1 | PF_FX2 ) ) ? PCAT_3DFX : PCAT_FAMILY;
		}
	}
}

/* BDGN 3.57h: the presets in categories that follow the hardware tiers of the automatic choice (client/cl_main.c
   CL_AutoPresetChoose: default, MAX Fidelity, Ultra Fidelity 1-3, High Fidelity 1-3, 4K). Every tier has the same
   three steps: MAX Fidelity (the best look of the tier), Fast and Faster. The Software category switches the renderer
   to Software as well, the 3dfx category to 3dfx's miniGL. A category is listed only when this computer and driver
   offer its presets; a step only when its preset is offered. The presets themselves are the ones of before. */
static int VID_CatOffered (int id)
{
	int i;
	for ( i = 0 ; i < s_all_count ; i++ ) if ( s_all_ids[i] == id ) return 1;
	return 0;
}
static int s_view_count;
static void VID_CatAdd (int cat, int id, const char *label)
{
	if ( s_all_count >= 118 ) return;
	s_all_list[s_all_count] = label;
	s_all_ids[s_all_count] = id;
	s_all_cat[s_all_count] = cat;
	s_all_count++;
	s_all_list[s_all_count] = 0;
}
static void VID_BuildPresetView (void)
{
	int i, n = 0, cats = 0, c, want;
	/* categories with entries */
	for ( c = 0 ; c < PCAT_COUNT ; c++ ) {
		for ( i = 0 ; i < s_all_count ; i++ ) if ( s_all_cat[i] == c ) break;
		if ( i < s_all_count ) { s_cat_names[cats] = pcat_names[c]; s_cat_map[cats++] = c; }
	}
	s_cat_names[cats] = 0;
	if ( s_preset_cat_box.curvalue >= cats || s_preset_cat_box.curvalue < 0 ) s_preset_cat_box.curvalue = 0;
	s_preset_cat_box.itemnames = s_cat_names;
	want = cats ? s_cat_map[s_preset_cat_box.curvalue] : PCAT_GENERAL;
	for ( i = 0 ; i < s_all_count && n < 79 ; i++ )
		if ( s_all_cat[i] == want ) { s_preset_list[n] = s_all_list[i]; s_preset_ids[n++] = s_all_ids[i]; }
	if ( !n ) { s_preset_list[n] = "keep current"; s_preset_ids[n++] = 0; }
	s_preset_list[n] = 0;
	s_view_count = n;
	s_preset_box.itemnames = s_preset_list;
	if ( s_preset_box.curvalue >= n )
		s_preset_box.curvalue = 0;
}
static void PresetCatCallback (void *unused)
{
	s_preset_box.curvalue = 0;
	VID_BuildPresetView ();
}

static void VID_BuildPresetList( int s3v, int rend, int fx, int software )
{
	VID_BuildPresetCatalog( s3v, rend, fx, software );
	if ( !software ) {
		int hf_max = VID_CatOffered(83) ? 83 : 82, hw = VID_HardwareFlags(), k;
		/* Low: the generic speed presets */
		if ( VID_CatOffered(1) ) VID_CatAdd( PCAT_LOW, 1, "MAX Fidelity" );
		if ( VID_CatOffered(2) ) VID_CatAdd( PCAT_LOW, 2, "Fast" );
		if ( VID_CatOffered(3) ) VID_CatAdd( PCAT_LOW, 3, "Faster" );
		if ( VID_CatOffered(65) ) VID_CatAdd( PCAT_LOW, 65, "PSYCHO Fast" );
		/* Medium: the MAX Fidelity step of the automatic choice */
		if ( VID_CatOffered(5) ) VID_CatAdd( PCAT_MEDIUM, 5, "MAX Fidelity" );
		if ( VID_CatOffered(5) && VID_CatOffered(1) ) VID_CatAdd( PCAT_MEDIUM, 1, "Fast" );
		if ( VID_CatOffered(5) && VID_CatOffered(2) ) VID_CatAdd( PCAT_MEDIUM, 2, "Faster" );
		/* High: Ultra Fidelity 1-2 */
		if ( VID_CatOffered(68) ) VID_CatAdd( PCAT_HIGH, 68, "MAX Fidelity" );
		if ( VID_CatOffered(67) ) VID_CatAdd( PCAT_HIGH, 67, "Fast" );
		if ( ( VID_CatOffered(67) || VID_CatOffered(68) ) && VID_CatOffered(5) ) VID_CatAdd( PCAT_HIGH, 5, "Faster" );
		/* Ultra: Ultra Fidelity 3 and Adaptive */
		if ( VID_CatOffered(69) ) VID_CatAdd( PCAT_ULTRA, 69, "MAX Fidelity" );
		if ( VID_CatOffered(70) ) VID_CatAdd( PCAT_ULTRA, 70, "Fast" );
		if ( ( VID_CatOffered(69) || VID_CatOffered(70) ) && VID_CatOffered(68) ) VID_CatAdd( PCAT_ULTRA, 68, "Faster" );
		/* High Fidelity: 1-3 and 4K */
		if ( VID_CatOffered(hf_max) ) VID_CatAdd( PCAT_HIGHFI, hf_max, hf_max == 83 ? "MAX Fidelity (4K)" : "MAX Fidelity" );
		if ( hf_max == 83 && VID_CatOffered(82) ) VID_CatAdd( PCAT_HIGHFI, 82, "MAX Fidelity (HD)" );
		if ( VID_CatOffered(81) ) VID_CatAdd( PCAT_HIGHFI, 81, "Fast" );
		if ( VID_CatOffered(80) ) VID_CatAdd( PCAT_HIGHFI, 80, "Faster" );
		/* Software: the software presets; choosing one switches the renderer to Software */
		VID_CatAdd( PCAT_SOFTWARE, PRESET_SW_BASE + 5, "MAX Fidelity" );
		VID_CatAdd( PCAT_SOFTWARE, PRESET_SW_BASE + 1, "Fast" );
		VID_CatAdd( PCAT_SOFTWARE, PRESET_SW_BASE + 2, "Faster" );
		VID_CatAdd( PCAT_SOFTWARE, PRESET_SW_BASE + 3, "Ultra Fast" );
		VID_CatAdd( PCAT_SOFTWARE, PRESET_SW_BASE + 65, "PSYCHO Fast" );
		/* 3dfx: a 3dfx card was found but another driver is chosen: its presets switch to 3dfx's miniGL */
		if ( ( hw & ( HW_FX1 | HW_FX2 ) ) && !fx )
			for ( k = 0 ; k < HW_PRESET_COUNT ; k++ )
				if ( hw_presets[k].family & VID_FxFamilies() & ( PF_FX1 | PF_FX2 ) )
					VID_CatAdd( PCAT_3DFX, PRESET_FX_BASE + hw_presets[k].id, hw_presets[k].label );
	}
	VID_BuildPresetView ();
	s_preset_box.generic.statusbar=!software && s_ref_list[OPENGL_MENU].curvalue==REF_MT_WICKED?
		"PSYCHO Fast uses Ultra Fast settings.":NULL;
}
/* BDGN 3.52c: low fidelity textures = lowtex-small.pak (half-size textures) only; pak6-lowtextures.pak is gone.  The pack
   needs BDGN's OpenGL renderer (other renderers would misalign the halved WALs), so the row is greyed for them and
   keeps fs_lowtextures as it is. */
static void VID_TextureChoice(void *item)
{
 if (item==&s_hdtexture_box && s_hdtexture_box.curvalue)
  s_lowtex_box.curvalue=0;
 if (item==&s_lowtex_box && s_lowtex_box.curvalue)
  s_hdtexture_box.curvalue=0;
}

static void VID_LowTexRow( int small_ok, int ultra_ok )
{
	static const char *lowtex2[] = { "off", "half-size", 0 };
	static const char *lowtex3[] = { "off", "half-size", "extra low (software)", 0 };	/* BDGN 3.56r: the whole game, was base1 walls only */
	static const char *lowtex_na[] = { "off (BDGN renderers only)", 0 };
	ultra_ok = ultra_ok && FS_UltraTexPackAvailable();
	small_ok = small_ok && FS_SmallTexPackAvailable();
	if ( small_ok || ultra_ok )
	{
		s_lowtex_box.itemnames = ultra_ok ? lowtex3 : lowtex2;
		s_lowtex_box.generic.flags = 0;
		s_lowtex_box.curvalue = Cvar_VariableValue("fs_lowtextures") == 2 && ultra_ok ? 2 :
			Cvar_VariableValue("fs_lowtextures") != 0;
		s_lowtex_box.generic.statusbar = "Extra low: quarter-size walls, small skins - for Pentium / under 64 MB";
	}
	else
	{
		s_lowtex_box.itemnames = lowtex_na;
		s_lowtex_box.generic.flags = QMF_GRAYED;
		s_lowtex_box.curvalue = 0;
		s_lowtex_box.generic.statusbar = NULL;
	}
}
static const char *simd_on[] = { "ON (automatic)", "off (test)", 0 };
static const char *simd_mmx_on[] = { "ON (texture loading)", "off (test)", 0 };
static const char *simd_na[] = { "not supported by the CPU", 0 };
static const char *simd_fail[] = { "off (self-test failed)", 0 };
static const char *simd_3d[] = { "off (3DNow! is used)", 0 };
static const char *simd_unknown[] = { "-", 0 };
/* BDGN 3.51bbt: each row is "ON (automatic)" with a second choice "off (test)" for comparing (bdgn_no3dnow /
   bdgn_nosse / bdgn_nommx, not saved: every start of the game is automatic again).  Rows of something the CPU
   lacks, or that the renderer does not use, are greyed status lines. */
static void VID_SimdRow( menulist_s *row, const char *name, const char *status, const char *off, const char **onnames, int cpu, int is_gl )
{
	int st = -1;
	row->generic.type = MTYPE_SPINCONTROL;
	row->generic.name = name;
	row->generic.callback = NULL;
	row->generic.statusbar = "found and switched on automatically; off (test) only for comparing";
	row->curvalue = 0;
	if ( status && is_gl && Cvar_VariableString( (char *)status )[0] )
		st = atoi( Cvar_VariableString( (char *)status ) );
	if ( !cpu )
		row->itemnames = simd_na;
	else if ( !status )
		st = 1;		/* 3DNow!: the client switches it on whenever the CPU has it */
	else if ( st < 0 )
		row->itemnames = simd_unknown;
	else if ( st == 2 )
		row->itemnames = simd_fail;
	else if ( st == 3 )
		row->itemnames = simd_3d;
	else if ( st != 1 )
		row->itemnames = simd_na;
	if ( cpu && st == 1 )
	{
		row->generic.flags = 0;
		row->itemnames = onnames;
		row->curvalue = Cvar_VariableValue( (char *)off ) ? 1 : 0;
	}
	else
		row->generic.flags = QMF_GRAYED;
}
static void VID_SimdRows( void )
{
	int is_gl = !strcmp( Cvar_VariableString( "vid_ref" ), "gl" );
	VID_SimdRow( &s_3dnow_box, is_gl && Cvar_VariableValue("r_simd_3dnow_ext_ready") ? "3DNow!+ (AMD)" : "3DNow! (AMD)", NULL, "bdgn_no3dnow", simd_on, Q3E_CPUHas3DNow(), is_gl );
	VID_SimdRow( &s_sse_box, "SSE", "r_simd_sse", "bdgn_nosse", simd_on, Q3E_CPUHasSSE(), is_gl );
	VID_SimdRow( &s_mmx_box, "MMX", "r_simd_mmx", "bdgn_nommx", simd_mmx_on, Q3E_CPUHasMMX(), is_gl );
	{
		int path=(int)Cvar_Get("bdgn_cpu_path","0",CVAR_ARCHIVE)->value;
		int sse=is_gl && Cvar_VariableValue("r_simd_sse_ready");
		int sse2=sse && Cvar_VariableValue("r_simd_sse2_ready");
		cpu_path_names[0]="Auto";
		cpu_path_names[1]=sse?"SSE":NULL;
		cpu_path_names[2]=sse2?"SSE2":NULL;
		cpu_path_names[3]=NULL;
		s_cpu_path_box.generic.type=MTYPE_SPINCONTROL;
		s_cpu_path_box.generic.name="CPU path";
		s_cpu_path_box.generic.statusbar="Auto prefers 3DNow! on supported AMD CPUs; SSE choices override it";
		s_cpu_path_box.itemnames=cpu_path_names;
		s_cpu_path_box.curvalue=path>0 && path<3 && cpu_path_names[path]?path:0;
	}
}

static void BuildOpenGLMenu( void );
static void BuildSoftwareMenu(void);

/*
** ref_glam.dll is AMD's 3DNow! renderer and crashes on CPUs without 3DNow! (Intel, K6 without the
** "-2", modern AMD), so the menu only loads it when the CPU reports 3DNow! (CPUID 80000001h EDX bit 31)
*/
/* v3.51M development: share the engine's cached feature result. */
static int CPUHas3DNow( void )
{
	return Q3E_CPUHas3DNow ();
}

static void LightFxCallback( void *unused )
{
	lightfx_touched = 1;
}

static void DriverCallback( void *unused )
{
	int previous=s_ref_prev;
	VID_SkipHiddenRef ();	/* BDGN 3.52k: only the drivers for this computer */
	if (s_ref_list[s_current_menu_index].curvalue!=previous)
		s_preset_box.curvalue=0;
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;

	if ( (s_ref_list[s_current_menu_index].curvalue == REF_SOFT ||
         s_ref_list[s_current_menu_index].curvalue == REF_SWAM) )
	{
		s_current_menu = &s_software_menu;
		s_current_menu_index = 0;
        {
         static const char *swam_aspects[]={"all","4:3","5:4","16:10","16:9",NULL};
         static const char *soft_aspects[]={"all","4:3","5:4","16:10","16:9","21:9",NULL};
         static const char *old_aspects[]={"all","4:3","5:4",NULL};
         s_sw_aspect_list.itemnames=s_ref_list[0].curvalue==REF_SWAM?swam_aspects:
          (Cvar_VariableValue("sw_bdgn_features")>=3518?soft_aspects:old_aspects);
         BuildSoftwareModeList(s_sw_aspect_list.curvalue,(int)sw_mode->value);
        }
        BuildSoftwareMenu();
	}
	else
	{
		s_current_menu = &s_opengl_menu;
		s_current_menu_index = 1;
		{	/* BDGN 3.51ak: the S3 Virge allows fewer modes; rebuild the list for the chosen driver */
			int idx = s_mode_list_new[OPENGL_MENU].curvalue;
			int mode = idx >= 0 && idx < s_res_count ? s_res_map[idx] : (int)gl_mode->value;
			BuildModeList( s_aspect_list.curvalue, mode );
		}
		BuildOpenGLMenu();
	}

}

/*
** (Re)build the OpenGL menu.  With the 3DNow! OpenGL renderer (AMD's ref_glam.dll) the rows that
** it does not support ("disable textures", "3DNow! optimizations") are hidden and the rest moves up.
*/
/* BDGN 3.55i: one Video layout for every renderer. The Video page keeps the display settings and the preset; the rest
   is grouped in sub-pages: picture & textures and performance (this file, applied with the Video page), lighting &
   shadows, effects, enemies & weapons and draw paths (tweak pages in client/menu.c). Rows only appear when the
   loaded renderer and driver support them. */
static int VID_HDSetChoices (void)
{
	static const char *labels[]={"off","standard","enhanced","enhanced (less grain)","AI (Stable Diffusion)","AI 4K (Stable Diffusion)"};
	static const int sets[]={0,2,1,3,4,5};	/* 3.55k: 4 = pak8-hdsd-*.pak; 3.55w: 5 = pak8-hd4k-*.pak (DDS) */	/* gl_hdtextures values, files.c hd_set_prefix */
	int i,n=0,cur=(int)Cvar_VariableValue("gl_hdtextures"),choice=0;
	FS_RescanQualityPacks();	/* BDGN 3.55n: a set copied into baseq2/textures/ while the game runs */
	for (i=0;i<6;i++) {
		if (i && !FS_HDTexSetAvailable(sets[i])) continue;
		if (sets[i]==5 && !Cvar_VariableValue("bdgn_gl_dds_capable")) continue;	/* 3.55w: compressed textures (S3TC) */
		hd_set_names[n]=labels[i]; hd_set_values[n]=sets[i];
		if (sets[i]==cur) choice=n;
		n++;
	}
	hd_set_names[n]=NULL;
	s_hdtexture_box.itemnames=hd_set_names;
	if (s_hdtexture_box.curvalue<0 || s_hdtexture_box.curvalue>=n) s_hdtexture_box.curvalue=choice;
	return n;
}

static void BuildOpenGLMenu( void )
{
	int glam = ( s_ref_list[OPENGL_MENU].curvalue == REF_3DNOW || s_ref_list[OPENGL_MENU].curvalue == REF_MT_3DNOW || s_ref_list[OPENGL_MENU].curvalue == REF_3DNOW_S3VIRGE || s_ref_list[OPENGL_MENU].curvalue == REF_3DNOW_3DFX || s_ref_list[OPENGL_MENU].curvalue == REF_MT_3DNOW_3DFX );
	int s3v = VID_S3VirgeSelected ();
	int v1k = VID_V1KSelected ();
	int rend = VID_RenditionSelected ();
	int gl_inuse = s_ref_list[OPENGL_MENU].curvalue==s_ref_inuse && !Q_stricmp(vid_ref->string,"gl");
	int y = 0, py = 0, fy = 0, step=viddef.height<400?8:10;
	menuframework_s *pic=&s_pic_menu[OPENGL_MENU], *perf=&s_perf_menu[OPENGL_MENU];

#define GLROW( item, gap )	do { ( item ).generic.y = y; y += step + (viddef.height<400?0:(gap)); Menu_AddItem( &s_opengl_menu, ( void * ) &( item ) ); } while ( 0 )
#define PICROW( item, gap )	do { ( item ).generic.y = py; py += step + (viddef.height<400?0:(gap)); Menu_AddItem( pic, ( void * ) &( item ) ); } while ( 0 )
#define PERFROW( item, gap )	do { ( item ).generic.y = fy; fy += step + (viddef.height<400?0:(gap)); Menu_AddItem( perf, ( void * ) &( item ) ); } while ( 0 )

	s_opengl_menu.nitems = 0;
	pic->nitems = 0;
	perf->nitems = 0;
	s_cpu_path_box.generic.parent=NULL;
	s_render_scale_box[OPENGL_MENU].generic.parent=NULL;
	s_upscale_filter.generic.parent=NULL;
	s_gl_filter_box.generic.parent=NULL;
	s_hdtexture_box.generic.parent=NULL;
	s_hd4k_size_box.generic.parent=NULL;
	s_gl_anisotropy_box.generic.parent=NULL;
	s_gl_msaa_box.generic.parent=NULL;
	s_gl_post_boxes[0].generic.parent=NULL;
	s_gl_post_boxes[1].generic.parent=NULL;
	s_gl_post_boxes[2].generic.parent=NULL;
	s_gl_post_boxes[3].generic.parent=NULL;
	s_gl_post_boxes[4].generic.parent=NULL;
	s_tq_slider.generic.flags=0;

	/* Video: display */
	GLROW( s_ref_list[OPENGL_MENU], 0 );
	if ( !v1k )
		GLROW( s_aspect_list, 0 );
	GLROW( s_mode_list_new[OPENGL_MENU], 0 );
	GLROW( s_screensize_slider[OPENGL_MENU], 0 );
	GLROW( s_brightness_slider[OPENGL_MENU], 0 );
	GLROW( s_fs_box[OPENGL_MENU], 10 );
	GLROW( s_fov_slider, 0 );
	/* BDGN 3.58o: the "vertical sync" row is gone; vsync is always off (owner, 2026-11-20) */
	y += viddef.height<400?0:10;

	/* Picture & textures */
	if ( v1k )
	{	/* BDGN 3.51ast: Rendition's own renderer reads only these */
		VID_LowTexRow( 0, 0 );
		PICROW( s_lowtex_box, 0 );
	}
	else
	{
		PICROW( s_tq_slider, 0 );
		if (gl_inuse) {
			if (!Cvar_VariableValue("bdgn_legacy_graphics")) PICROW(s_gl_filter_box,0);
			if (gl_anisotropy_choices[1]) PICROW(s_gl_anisotropy_box,0);
			if (Cvar_VariableValue("bdgn_gl_hdtextures_capable") && FS_HDTexPackAvailable() && VID_HDSetChoices()>1)
				PICROW(s_hdtexture_box,0);
			if (Cvar_VariableValue("bdgn_gl_dds_capable") && FS_HDTexSetAvailable(5))
				PICROW(s_hd4k_size_box,0);	/* BDGN 3.55w: only with the 4K set installed and a driver with S3TC */
		}
		if ( !s3v )	/* the S3 Virge driver has no paletted textures */
			PICROW( s_paletted_texture_box, 0 );
		VID_LowTexRow( !glam || (!strcmp(vid_ref->string,"glam") && Cvar_VariableValue("glam_bdgn_features") >= 3522), !glam );	/* BDGN 3.52c: the low fidelity pack only with BDGN's OpenGL renderer; 3.52z: or AMD's ref_glam patched to 3.52z (loaded) */
		PICROW( s_lowtex_box, 0 );
		if (s_lowtex_box.curvalue) s_hdtexture_box.curvalue=0;
		if (!glam && gl_inuse) {
			int post=(int)Cvar_VariableValue("bdgn_gl_post_available");
			if (gl_msaa_labels[1]) PICROW(s_gl_msaa_box,0);
			if (post&POST_FXAA) PICROW(s_gl_post_boxes[0],0);
			if (Cvar_VariableValue("gl_render_scale_fbo_capable")) {
				PICROW(s_render_scale_box[OPENGL_MENU],0);
				PICROW(s_upscale_filter,0);
			}
		}
		if ( glam && Cvar_VariableValue("glam_bdgn_features") < 3516 )
			PICROW( s_disable_textures_box, 0 );	// ref_glam.dll: the patched single colour switch (gl_hack_notextures)
	}

	/* Performance */
	if ( v1k )
	{
		PERFROW( s_v1k_aa_box, 0 );
		PERFROW( s_v1k_warp_box, 0 );
		PERFROW( s_v1k_flip_box, 0 );
		PERFROW( s_v1k_lookup_box, 0 );
		PERFROW( s_v1k_rate_box, 0 );
	}
	else
	{
		PERFROW( s_drawdist_slider, 10 );
		if ( !glam )
		{	/* BDGN 3.51bat: 3DNow!, SSE and MMX are automatic; these rows show what is on */
			VID_SimdRows ();
			if (s_ref_list[OPENGL_MENU].curvalue==s_ref_inuse && cpu_path_names[1])
				PERFROW(s_cpu_path_box,0);
			PERFROW( s_3dnow_box, 0 );
			PERFROW( s_sse_box, 0 );
			PERFROW( s_mmx_box, 10 );
		}
		if ( s3v )
		{	/* BDGN 3.51ak: the S3 Virge driver's own options (OpenGL32.INI) */
			PERFROW( s_s3v_dither_box, 0 );
			PERFROW( s_s3v_square_box, 0 );
			PERFROW( s_s3v_alpha_box, 0 );
			PERFROW( s_s3v_math_box, 0 );
		}
		if ( VID_3dfxSelected () )
		{	/* BDGN 3.52l: switches of the 3dfx drivers */
			int hw = VID_HardwareFlags ();
			PERFROW( s_fx_tb_box, 0 );
			PERFROW( s_fx_hz_box, 0 );
			PERFROW( s_fx_dither_box, 0 );
			if ( hw & HW_V2 )
			{
				PERFROW( s_fx_fast_box, 0 );
				PERFROW( s_fx_filter_box, 0 );
			}
			if ( s_ref_list[OPENGL_MENU].curvalue == REF_3DFX || s_ref_list[OPENGL_MENU].curvalue == REF_MT_3DFX )
				PERFROW( s_fx_cpu_box, 0 );	/* miniGL 1.47 */
		}
	}
	if (s_has_audio_workers) PERFROW(s_audio_worker_box,0);
	/* BDGN 3.57h: draw paths & detail is now on the old-school modes page */

	/* Video: preset and pages */
	if ( v1k )
		VID_BuildPresetList( 0, 1, 0, 0 );
	else
		VID_BuildPresetList( s3v, rend, VID_3dfxSelected () ? VID_FxFamilies () : 0, 0 );	/* BDGN 3.52i: 3dfx presets only with 3dfx; 3.51al: S3 Virge; 3.51ast: Rendition; 3.51bdt: low RAM */
	GLROW( s_preset_cat_box, 0 );	/* BDGN 3.57h */
	GLROW( s_preset_box, 10 );
	/* BDGN 3.57d: two groups with headings */
	GLROW( s_hdr_better[OPENGL_MENU], 2 );
	GLROW( s_pic_action[OPENGL_MENU], 0 );
	GLROW( s_light_action[OPENGL_MENU], 0 );
	GLROW( s_fx_action[OPENGL_MENU], 8 );
	GLROW( s_hdr_faster[OPENGL_MENU], 2 );
	GLROW( s_perf_action[OPENGL_MENU], 0 );
	GLROW( s_simpler_action[OPENGL_MENU], 0 );
	GLROW( s_enemy_action[OPENGL_MENU], 0 );
	GLROW( s_old_action[OPENGL_MENU], 0 );
	GLROW( s_paths_action[OPENGL_MENU], 8 );	/* BDGN 3.57v: on the Video page itself again (owner, 2026-11-20) */
	GLROW( s_testing_action[OPENGL_MENU], 10 );	/* BDGN 3.55ai: new switches to try */
	GLROW( s_benchmarks_action[OPENGL_MENU], 0 );
	GLROW( s_gl_apply_action, 0 );
	GLROW( s_defaults_action[OPENGL_MENU], 0 );
	GLROW( s_cancel_action[OPENGL_MENU], 0 );

#undef GLROW
#undef PICROW
#undef PERFROW

	s_opengl_menu.y=viddef.height/2-68;
	if(s_opengl_menu.y+y>viddef.height-8) s_opengl_menu.y=viddef.height-8-y;
	if ( s_opengl_menu.cursor >= s_opengl_menu.nitems )
		s_opengl_menu.cursor = 0;
	if ( pic->cursor >= pic->nitems ) pic->cursor = 0;
	if ( perf->cursor >= perf->nitems ) perf->cursor = 0;
}


/* BDGN 3.57d: notes to the right of the picture and performance rows (what the choice does to the picture) */
static const char *VID_Note (void *item)
{
 int i;
 for (i=0;i<2;i++) if (item==(void *)&s_render_scale_box[i]) return "lower = blurrier";
 if (item==(void *)&s_tq_slider) return "texture sharpness";
 if (item==(void *)&s_gl_filter_box || item==(void *)&s_texfilter_box) return "smooth or pixelated";
 if (item==(void *)&s_gl_anisotropy_box) return "sharper floors far off";
 if (item==(void *)&s_hdtexture_box) return "high-res wall textures";
 if (item==(void *)&s_hd4k_size_box) return "size of 4K textures";
 if (item==(void *)&s_paletted_texture_box) return "fewer texture colours";
 if (item==(void *)&s_lowtex_box) return "blurrier textures";
 if (item==(void *)&s_gl_msaa_box) return "smooth edges";
 if (item==(void *)&s_gl_post_boxes[0]) return "smooth edges (soft)";
 if (item==(void *)&s_upscale_filter) return "smooth or sharp pixels";
 if (item==(void *)&s_swcolor_box) return "coloured light";
 if (item==(void *)&s_stipple_box) return "glass and water look";
 if (item==(void *)&s_swdrawdist_slider || item==(void *)&s_drawdist_slider) return "fog, shorter view";
 if (item==(void *)&s_disable_textures_box) return "walls in one colour";
 if (item==(void *)&s_s3v_dither_box) return "dithered textures";
 if (item==(void *)&s_fx_dither_box) return "smoother mip levels";
 if (item==(void *)&s_fx_filter_box) return "video output filter";
 if (item==(void *)&s_v1k_aa_box) return "smooth edges";
 if (item==(void *)&s_v1k_warp_box) return "underwater wobble";
 return NULL;
}
static void VID_NoteLayout (menuframework_s *m)
{
 int k, n, maxval=0, maxnote=0;
 m->note=NULL; m->notecolumn=0; m->notechars=0;
 for (k=0;k<m->nitems;k++) {
  menucommon_s *c=(menucommon_s *)m->items[k];
  const char *note=VID_Note(c);
  if (c->type==MTYPE_SLIDER) { if (maxval<12) maxval=12; }
  else if (c->type==MTYPE_SPINCONTROL) {
   const char **names=((menulist_s *)c)->itemnames;
   for (n=0;names && names[n];n++) if ((int)strlen(names[n])>maxval) maxval=strlen(names[n]);
  }
  if (note && (int)strlen(note)>maxnote) maxnote=strlen(note);
 }
 if (!maxnote) return;
 if (maxval>26) maxval=26;	/* a very long value is cut by the note, not the other way round */
 m->note=VID_Note;
 m->notecolumn=16+maxval*8+16;
 m->notechars=(viddef.width-8-(m->x+m->notecolumn))/8;
}

static void BuildSoftwareMenu(void)
{
 int y=0,py=0,fy=0,step=viddef.height<400?8:10, modern;
 menuframework_s *pic=&s_pic_menu[SOFTWARE_MENU], *perf=&s_perf_menu[SOFTWARE_MENU];
 modern=s_ref_list[SOFTWARE_MENU].curvalue==REF_SOFT && Cvar_VariableValue("sw_bdgn_features")>=3525;
#define SWROW(item,gap) do { (item).generic.y=y; y+=step+(viddef.height<=280?0:(gap)); Menu_AddItem(&s_software_menu,&(item)); } while(0)
#define SPICROW(item,gap) do { (item).generic.y=py; py+=step+(viddef.height<=280?0:(gap)); Menu_AddItem(pic,&(item)); } while(0)
#define SPERFROW(item,gap) do { (item).generic.y=fy; fy+=step+(viddef.height<=280?0:(gap)); Menu_AddItem(perf,&(item)); } while(0)
 s_software_menu.nitems=0;
 pic->nitems=0;
 perf->nitems=0;
 s_render_scale_box[SOFTWARE_MENU].generic.parent=NULL;
 s_tq_slider.generic.flags=Cvar_VariableValue("sw_bdgn_features")>=3518?0:QMF_GRAYED;
 /* Video: display */
 SWROW(s_ref_list[SOFTWARE_MENU],0);
 SWROW(s_sw_aspect_list,0);
 SWROW(s_mode_list[SOFTWARE_MENU],0);
 SWROW(s_screensize_slider[SOFTWARE_MENU],0);
 SWROW(s_brightness_slider[SOFTWARE_MENU],0);
 SWROW(s_fs_box[SOFTWARE_MENU],step);
 SWROW(s_fov_slider,step);
 /* Picture & textures */
 s_render_scale_box[SOFTWARE_MENU].generic.flags=modern?0:QMF_GRAYED;
 if(modern) SPICROW(s_render_scale_box[SOFTWARE_MENU],0);
 if(!(s_tq_slider.generic.flags&QMF_GRAYED)) SPICROW(s_tq_slider,0);
 /* BDGN 3.52n: dithered filtering needs BDGN's ref_soft 3.52n (sw_bdgn_features 3521); AMD's ref_swam has none */
 s_texfilter_box.generic.flags=(s_ref_list[SOFTWARE_MENU].curvalue!=REF_SWAM && Cvar_VariableValue("sw_bdgn_features")>=3521)?0:QMF_GRAYED;
 s_texfilter_box.itemnames=Cvar_VariableValue("sw_smp_quality")?sw_quality_filter_names:sw_texfilter_names;
 if(s_texfilter_box.itemnames==sw_texfilter_names && s_texfilter_box.curvalue>1)
  s_texfilter_box.curvalue=1;
 if(!(s_texfilter_box.generic.flags&QMF_GRAYED)) SPICROW(s_texfilter_box,0);
 s_swcolor_box.generic.flags=(s_ref_list[SOFTWARE_MENU].curvalue!=REF_SWAM && Cvar_VariableValue("sw_bdgn_features")>=3522)?0:QMF_GRAYED;
 if(!(s_swcolor_box.generic.flags&QMF_GRAYED)) SPICROW(s_swcolor_box,0);
 SPICROW(s_stipple_box,0);
 VID_LowTexRow(s_ref_list[SOFTWARE_MENU].curvalue==REF_SOFT && Cvar_VariableValue("sw_bdgn_features")>=3524,
               s_ref_list[SOFTWARE_MENU].curvalue==REF_SOFT && Cvar_VariableValue("sw_bdgn_features")>=3524);	/* 3.52y: BDGN software too */
 SPICROW(s_lowtex_box,0);
 /* Performance */
 s_swdrawdist_slider.generic.flags=(s_ref_list[SOFTWARE_MENU].curvalue!=REF_SWAM && Cvar_VariableValue("sw_bdgn_features")>=3523)?0:QMF_GRAYED;
 if(!(s_swdrawdist_slider.generic.flags&QMF_GRAYED)) SPERFROW(s_swdrawdist_slider,10);
 s_swprefetch_box.generic.flags=s_swdrawdist_slider.generic.flags;
 if(!(s_swprefetch_box.generic.flags&QMF_GRAYED)) SPERFROW(s_swprefetch_box,0);
 s_smp_box.generic.flags=(!s_has_audio_workers || s_ref_list[SOFTWARE_MENU].curvalue==REF_SWAM)?QMF_GRAYED:0;
 if (s_has_audio_workers) {
  if (s_ref_list[SOFTWARE_MENU].curvalue==REF_SWAM) SPERFROW(s_audio_worker_box,0);
  else SPERFROW(s_smp_box,0);
 }
 if(!(s_smp_box.generic.flags&QMF_GRAYED) && Cvar_VariableValue("sw_smp_quality"))
  SPERFROW(s_sw_threads_box,0);
 /* BDGN 3.57h: draw paths & detail is now on the old-school modes page */
 /* Video: preset and pages */
 VID_BuildPresetList(0,0,0,1);
 SWROW(s_preset_cat_box,0);	/* BDGN 3.57h */
 SWROW(s_preset_box,step);
 /* BDGN 3.57d: two groups with headings */
 SWROW(s_hdr_better[SOFTWARE_MENU],2);
 SWROW(s_pic_action[SOFTWARE_MENU],0);
 SWROW(s_light_action[SOFTWARE_MENU],0);
 SWROW(s_fx_action[SOFTWARE_MENU],8);
 SWROW(s_hdr_faster[SOFTWARE_MENU],2);
 SWROW(s_perf_action[SOFTWARE_MENU],0);
 SWROW(s_simpler_action[SOFTWARE_MENU],0);
 SWROW(s_enemy_action[SOFTWARE_MENU],0);
 SWROW(s_old_action[SOFTWARE_MENU],0);
 SWROW(s_paths_action[SOFTWARE_MENU],8);	/* BDGN 3.57v: on the Video page itself again */
 SWROW(s_testing_action[SOFTWARE_MENU],step);
 SWROW(s_benchmarks_action[SOFTWARE_MENU],0);
 SWROW(s_sw_apply_action,0);
 SWROW(s_defaults_action[SOFTWARE_MENU],0);
 SWROW(s_cancel_action[SOFTWARE_MENU],0);
#undef SWROW
#undef SPICROW
#undef SPERFROW
 s_software_menu.y=viddef.height/2-68;
 if(s_software_menu.y+y>viddef.height-8) s_software_menu.y=viddef.height-8-y;
 if(s_software_menu.cursor>=s_software_menu.nitems) s_software_menu.cursor=0;
 if(pic->cursor>=pic->nitems) pic->cursor=0;
 if(perf->cursor>=perf->nitems) perf->cursor=0;
}

static void ScreenSizeCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "viewsize", slider->curvalue * 10 );
}

static qboolean VID_SceneBrightness(int menu)
{
	return menu==OPENGL_MENU && s_ref_list[OPENGL_MENU].curvalue==REF_OPENGL &&
		((int)Cvar_VariableValue("bdgn_gl_post_available")&POST_BRIGHTNESS)!=0;
}

static void BrightnessCallback( void *s )
{
	menuslider_s *slider = (menuslider_s *)s;
	float gamma = (18.0f-slider->curvalue)*0.1f;
	if (VID_SceneBrightness(s_current_menu_index))
		Cvar_SetValue("gl_scene_gamma",gamma);
	else if (s_current_menu_index==SOFTWARE_MENU && !stricmp(vid_ref->string,"soft"))
		Cvar_SetValue("vid_gamma",gamma);
}

/*
** "fullbright world" and "disable textures" exclude each other (fullbright only gives its FPS with
** textures on), so switching one on switches the other off
*/
static void FastWorldCallback( void *unused )
{
	if ( s_fastworld_box.curvalue )
	{
		s_disable_textures_box.curvalue = 0;	// single colour textures off
		s_noimages_box.curvalue = 0;			// no texture images off
	}
}

static void DisableTexturesCallback( void *unused )
{
	if ( s_disable_textures_box.curvalue )
		s_fastworld_box.curvalue = 0;
}

static void FlatModeCallback (void *unused)
{
	if (s_flat_list.curvalue)
	{
		s_fastworld_box.curvalue = 0;	// normalized when Video applies
	}
}

extern void CL_BenchCfgDefaults( void );
extern void CL_CheckFullbright( void );
static const char *preset_extra_defaults[][2] = {
		{ "gl_ext_multitexture", "0" },		/* 3.52i: off by default */
		{ "gl_shadows", "0" }, { "gl_drawdist", "0" }, { "r_brightskins", "0" }, { "r_drawflat", "0" },
		{ "gl_picmip", "0" }, { "gl_hack_noimages", "0" }, { "gl_hack_notextures", "0" },
		{ "fov", "90" }, { "viewsize", "100" }, { "cl_maxfps", "100" },
		{ "sw_texfilter", "0" }, { "sw_coloredlight", "0" }, { "sw_drawdist", "0" }, { "sw_prefetch", "0" },
		{ "sw_smp", "0" }, { "sw_stipplealpha", "0" },
		{ "fx_cpupath", "1" }, { "fx_fastmem", "0" }, { "fx_lod_dither", "0" }, { "fx_refresh", "0" },
		{ "fx_triplebuffer", "0" }, { "fx_videofilter", "1" },
		{ "s3v_alpha", "80" }, { "s3v_dither", "0" }, { "s3v_squaredown", "0" }, { "s3v_fastmath", "0" },
		{ "bdgn_no3dnow", "0" }, { "bdgn_nommx", "0" }, { "bdgn_nosse", "0" },	/* "off (test)" toggles */
		{ "r_profile", "0" }, { "timedemo", "0" },
		{ "cl_simpleenemies", "0" }, { "cl_simpleenemy_anim", "0" },
		{ "cl_sprite_hits", "0" }, { "cl_sprite_trails", "0" },
		{ "gl_weapon_texcache", "32" }, { "r_duke_cache", "1024" },
		{ "gl_ext_samples", "0" }, { "gl_hack_nocinematics", "0" },
		{ "v1k_mode", "2" }, { "v1k_antialias", "0" }, { "v1k_nopageflip", "0" },
		{ "v1k_refreshrate", "60" }, { "v1k_surfacelookup", "1" }, { "v1k_waterwarp", "1" },
		{ 0, 0 }
	};

static void ApplyConfigPreset(int preset);

static void ResetDefaults(void *unused)
{
	ApplyConfigPreset(4);
	VID_MenuInit();
}

#define DEFAULT_CONFIG_FILE	"defaultconfig.cfg"
#define FAST_CONFIG_FILE	"fastconfig.cfg"
#define ULTRA_CONFIG_FILE	"ultraconfig.cfg"	/* BDGN 3.37: everything for fps, worse graphics */
#define FASTER_CONFIG_FILE	"fasterconfig.cfg"
#define ULTRAHIGH_CONFIG_FILE	"MAXfidelity.cfg"	/* BDGN 3.38: ultra high fidelity, best looking, fps does not matter */
#define S3VIRGE_ULTRA_FILE	"s3virgeultra.cfg"	/* BDGN 3.51an: S3 Virge ultra speed; only listed with the S3 Virge renderers */
#define S3VIRGE_CONFIG_FILE	"s3virgeconfig.cfg"	/* BDGN 3.51al: S3 Virge, 4 MB: speed first; only listed with the S3 Virge renderers */
#define RENDITION_CONFIG_FILE	"renditionconfig.cfg"	/* BDGN 3.51ast: Rendition Verite (ref_v1k or V2x00 OpenGL); only listed with the Rendition renderers */
#define RENDITION_ULTRA_FILE	"renditionultra.cfg"	/* BDGN 3.51ast: Rendition Verite, ultra speed */
/* BDGN 3.51bdt: low RAM presets (less than 64 MB, bdgn_lowram 1), listed only on such machines */
#define LOWRAM_DEFAULT_FILE	"lowramdefault.cfg"
#define LOWRAM_FAST_FILE	"lowramfast.cfg"
#define LOWRAM_FASTER_FILE	"lowramfaster.cfg"
#define LOWRAM_ULTRA_FILE	"lowramultra.cfg"

static int PresetCvarWanted( const char *name )
{
	static const char *prefixes[] = { "gl_", "vid_", "sw_", "fs_low", "v1k_", "s3v_", "fx_", 0 };
	static const char *names[] = { "viewsize", "fov", "dfov", "zfov", "cl_fastfx", "cl_weaponlights", "cl_nosmoke", "cl_hack_nocinematics",
								   "cl_vwep", "g_noflies", "g_gibmode", "s_mixinterval", "s_mmx", "s_maxchannels", "cl_maxpackets",
								   "cl_fovaspect", "r_drawflat", "cl_nosmoke", "cpu_auto", "fastsqrt", "fastsqrt2", "cm_amd_trace", "cl_amd_particles", "s_amd_mix", "cl_fovaspect", "r_fullbright", "r_lerpmodels", "gl_hack_noimages", "gl_hack_notextures", "cpu_auto", "cl_entities", "r_drawentities", "r_drawworld", "r_norefresh", "cl_testentities", "cl_testparticles", "cl_testlights", "cl_testblend", "crosshair", "viewsize", "fs_lowtextures", "fs_lowsounds", "s_nosound", "s_midieffects", "s_pcspeaker", "s_profile", "s_khz", "s_loadas8bit", "s_primary", "s_initsound", "s_musicmode", "s_musicdefault", "s_musicsource", "cd_nocd", "timedemo", "paused",
								   "cl_run", "cl_maxfps", "cl_gun", "cl_particles", "cl_blend", "scr_scale", "cl_footsteps",
								   "cl_simpleitems", "cl_simpleenemies", "cl_simpleenemy_anim",
								   "cl_weaponsprite_anim", "cl_sprite_hits", "cl_sprite_trails", "r_duke_cache",
								   "s_mixahead", "s_volume", "triangles", "intensity", "cl_predict", "cl_simpleblaster", 0 };
	int i;

	for(i=0;i<bdgn_tweak_count;i++)
		if(!Q_stricmp((char *)name,(char *)bdgn_tweaks[i].cvar) ||
			(bdgn_tweaks[i].aux && !Q_stricmp((char *)name,(char *)bdgn_tweaks[i].aux))) return 1;
	for ( i = 0 ; prefixes[i] ; i++ )
		if ( Q_strncasecmp( ( char * ) name, ( char * ) prefixes[i], ( int ) strlen( prefixes[i] ) ) == 0 )
			return 1;
	for ( i = 0 ; names[i] ; i++ )
		if ( Q_stricmp( ( char * ) name, ( char * ) names[i] ) == 0 )
			return 1;
	return 0;
}

static const char *preset_stock_defaults[][2] = {
 {"gl_materials","0"}, {"gl_soft_particles","0"}, {"gl_smoke_lighting","0"}, {"gl_ssr","0"}, {"gl_ssgi","0"}, {"gl_volume_fog","0"}, {"gl_caustics","0"},

		{ "cl_maxfps", "100" }, { "cl_particles", "1" }, { "cl_gun", "1" }, { "cl_footsteps", "1" },
		{ "cl_predict", "1" }, { "cl_blend", "1" }, { "gl_polyblend", "1" }, { "gl_flashblend", "0" },
		{ "gl_hdtextures", "0" }, { "gl_relief", "0" }, { "gl_sun", "0" }, { "gl_godrays", "0" },	/* 3.55al: on in MAX only (below) */
		 { "gl_pixel_lighting", "0" }, { "gl_pixel_bump", "1" }, { "gl_anisotropy", "1" }, { "gl_fxaa", "0" }, { "gl_bloom", "0" }, { "gl_ssao", "0" }, { "gl_tonemap", "0" }, { "gl_colour_grade", "0" },
		{ "gl_ztrick", "0" }, { "gl_cull", "1" }, { "gl_round_down", "1" }, { "gl_skymip", "0" },
		{ "gl_playermip", "0" }, { "gl_picmip", "0" }, { "intensity", "2" }, { "gl_lightmap", "0" },
		{ "gl_flatmodels", "0" }, { "gl_lowdetail", "0" }, { "gl_maxtexsize", "0" }, { "s_maxchannels", "32" },
		{ "cl_maxpackets", "0" }, { "cl_fovaspect", "1" }, { "gl_fastdraw", "0" }, { "gl_fastparticles", "0" },
		{ "gl_fastworld", "0" }, { "gl_drawdist", "0" }, { "r_drawflat", "0" },
		{ "gl_fastsky", "0" }, { "gl_fastwater", "0" }, { "gl_shadows", "0" },
		{ "cl_fovaspect", "0" },
		{ "r_fullbright", "0" },
		{ "r_lerpmodels", "1" },
		{ "gl_hack_noimages", "0" },
		{ "gl_hack_notextures", "0" },
		{ "cpu_auto", "1" },
		{ "cl_entities", "1" },
		{ "r_drawentities", "1" },
		{ "r_drawworld", "1" },
		{ "r_norefresh", "0" },
		{ "cl_testentities", "0" },
		{ "cl_testparticles", "0" },
		{ "cl_testlights", "0" },
		{ "cl_testblend", "0" },
		{ "crosshair", "1" },
		{ "viewsize", "100" },
		{ "fs_lowtextures", "0" },
		{ "fs_lowsounds", "0" },
		{ "s_nosound", "0" },
		{ "s_midieffects", "0" },
		{ "s_pcspeaker", "0" },	/* BDGN 3.58g: no preset turns on the PC speaker mode */
		{ "s_profile", "1" },
		{ "s_khz", "11" },
		{ "s_loadas8bit", "1" },
		{ "s_primary", "0" },
		{ "s_initsound", "1" },
		{ "s_musicmode", "0" },
		{ "s_musicdefault", "1" },
		{ "s_musicsource", "1" },
		{ "cd_nocd", "0" },
		{ "timedemo", "0" },
		{ "paused", "0" },
		{ 0, 0 }
	};

typedef struct { const char *file, *name; } preset_file_t;
static const preset_file_t standard_presets[] = {
	{NULL,NULL}, {FAST_CONFIG_FILE,"fast"}, {FASTER_CONFIG_FILE,"faster"},
	{ULTRA_CONFIG_FILE,"ultra"}, {DEFAULT_CONFIG_FILE,"default"}, {ULTRAHIGH_CONFIG_FILE,"maxfidelity"}
};
static const char *software_presets[] = {
	NULL, "softwarefast.cfg", "softwarefaster.cfg", "softwareultra.cfg", NULL, "softwaremaxfidelity.cfg"
};
static const preset_file_t lowram_presets[] = {
	{LOWRAM_DEFAULT_FILE,"lowram"}, {LOWRAM_FAST_FILE,"lowramfast"},
	{LOWRAM_FASTER_FILE,"lowramfaster"}, {LOWRAM_ULTRA_FILE,"lowramultra"}
};

#include "../qcommon/minigl_compat.h"
void VID_GetMtGlTarget (char *dll, int size);

static qboolean PresetUsesSmpWicked(void)
{
	char target[MAX_QPATH];
	if (Q_stricmp(vid_ref->string,"gl") ||
	    !BDGN_GLDriverIs(Cvar_VariableString("gl_driver"),"mtgl32"))
		return false;
	VID_GetMtGlTarget(target,sizeof(target));
	return BDGN_GLDriverIs(target,WICKED_DRIVER);
}

static qboolean PresetUses3DWeapons (void)
{
 const char *driver;
 char target[MAX_QPATH]="";
 if(Q_stricmp(vid_ref->string,"gl")) return false;
 driver=Cvar_VariableString("gl_driver");
 if(BDGN_GLDriverIs(driver,"mtgl32"))
  VID_GetMtGlTarget(target,sizeof(target));
 return BDGN_GLUses3DWeapons(driver,target);
}

static const char *legacy_fidelity_defaults[][2]={
 {"gl_bitdepth","16"}, {"gl_round_down","1"},
 {"gl_texturesolidmode","default"}, {"gl_texturealphamode","default"},
 {"gl_texturemode","GL_LINEAR_MIPMAP_NEAREST"},
 {"gl_ext_samples","0"}, {"gl_ext_multitexture","0"},
 {"gl_ext_palettedtexture","0"}, {"gl_render_scale","100"},
 {"gl_render_scale_legacy","0"}, {"gl_render_scale_pixelcopy","0"},
 {NULL,NULL}
};

void VID_ApplyLegacyFidelity(void)
{
 const char *ref=Cvar_VariableString("vid_ref");
 const char *driver=Cvar_VariableString("gl_driver");
 char target[MAX_QPATH];
 int i, mode, hardware;
 qboolean s3, rendition, powervr, voodoo;

 if(Q_stricmp(Cvar_VariableString("cfg_preset"),"maxfidelity")) return;
 if(!Q_stricmp(ref,"v1k")) {
  mode=(int)Cvar_VariableValue("v1k_mode");
  hardware=VID_HardwareFlags();
  if(mode<0 || mode>V1K_MAX_MODE ||
     (mode==V1K_MAX_MODE && (hardware&HW_V1000) && !(hardware&HW_V2X00)))
   Cvar_SetValue("v1k_mode",2);
  Cvar_SetValue("v1k_antialias",0);
  return;
 }
 if(Q_stricmp(ref,"gl") && Q_stricmp(ref,"glam")) return;

 if(BDGN_GLDriverIs(driver,"mtgl32")) {
  VID_GetMtGlTarget(target,sizeof(target));
  driver=target;
 }
 s3=BDGN_GLDriverIs(driver,S3V_DRIVER);
 rendition=BDGN_GLDriverIs(driver,"opengl32") && Cvar_VariableValue("vid_rendition")!=0;
 powervr=BDGN_GLDriverIs(driver,"pvrgl");
 voodoo=BDGN_GLDriverIs(driver,"3dfxgl") || BDGN_GLDriverIs(driver,FX_GLIDE_DRIVER) ||
        BDGN_GLDriverIs(driver,WICKED_DRIVER) || BDGN_GLDriverIs(driver,FXGLAM_DRIVER);
 if(!s3 && !rendition && !powervr && !voodoo) return;

 for(i=0;legacy_fidelity_defaults[i][0];i++)
  Cvar_Set(legacy_fidelity_defaults[i][0],legacy_fidelity_defaults[i][1]);
 if(s3 || rendition || powervr) {
  Cvar_SetValue("gl_ext_pointparameters",0);
  Cvar_SetValue("gl_ext_compiled_vertex_array",0);
  Cvar_SetValue("gl_playermip",1);
  Cvar_SetValue("gl_maxtexsize",rendition?256:128);
  Cvar_SetValue("fs_lowtextures",1);
 }
 if(s3) {
  mode=(int)Cvar_VariableValue("gl_mode");
  if(mode<0 || mode>=NUM_GL_MODES || res_wh[mode][0]*res_wh[mode][1]>S3V_MAX_PIXELS)
   Cvar_SetValue("gl_mode",2);
 }
}

static int PresetIsSoftware(void)
{
	return !strcmp(vid_ref->string,"soft") || !strcmp(vid_ref->string,"swam");
}

static int PresetLookup(int id, preset_file_t *result)
{
	const hwpreset_t *hardware;
	if (id>=1 && id<=5) {
		*result=standard_presets[id];
		return 1;
	}
	if (id==71 || id==72) {
		result->file=id==72?"lowrampsycho.cfg":LOWRAM_DEFAULT_FILE;
		result->name=id==72?"lowerThen32mbextreme":"lowerThen32mb";
		return 1;
	}
	if (id == 65 || id == 66) {
		result->file = id == 65 ? "psychoconfig.cfg" : "lowrampsycho.cfg";
		result->name = id == 65 ? "psycho" : "lowrampsycho";
		return 1;
	}
	if (id>=10 && id<=13) {
		*result=lowram_presets[id-10];
		return 1;
	}
	hardware=VID_HwPreset(id);
	if (!hardware) return 0;
	result->file=hardware->file;
	result->name=hardware->name;
	return 1;
}

/* Keep the renderer and the display mode that the user selected. A card
   preset can still set an explicit colour depth for its OpenGL driver. */
static int PresetControls(const char *name, int base)
{
	static const char *keep[]={"vid_ref","gl_driver","gl_mode","sw_mode","vid_aspect",
		"sw_aspect","sw_render_scale","gl_render_scale","gl_upscale_filter","vid_fullscreen","vid_gamma","vid_xpos","vid_ypos","vid_rendition",NULL};
	int i;
	for (i=0;keep[i];i++)
		if (!Q_stricmp(name,keep[i])) return 0;
	if ((base || PresetIsSoftware()) && !Q_stricmp(name,"gl_bitdepth")) return 0;
	return PresetCvarWanted(name);
}

static int preset_archive;
static void SetPresetValue(const char *name,const char *value)
{
	if (preset_archive && strncmp(name,"bdgn_no",7) && strcmp(name,"r_profile") &&
		strcmp(name,"timedemo") && strcmp(name,"paused"))
		Cvar_Get((char *)name,(char *)value,CVAR_ARCHIVE);
	Cvar_Set((char *)name,(char *)value);
}
static void ApplyDefaultPresetOne(const char *name,const char *value)
{
	if (PresetControls(name,1)) SetPresetValue(name,value);
}
static void ApplySelectedPresetOne(const char *name,const char *value)
{
	if (PresetControls(name,0)) SetPresetValue(name,value);
}

static int LoadPreset(const char *file,char **buf)
{
	int len;
	*buf=NULL;
	len=FS_LoadFile(FS_PresetPath(file),(void **)buf);
	if (len>0 && *buf) return len;
	if (*buf) FS_FreeFile(*buf);
	*buf=NULL;
	return 0;
}

static void ApplyPresetBase(void)
{
	char *buf;
	int i,len;
	CL_BenchCfgDefaults();
	BDGN_TweakDefaults();
	for(i=0;preset_stock_defaults[i][0];i++)
		SetPresetValue(preset_stock_defaults[i][0],preset_stock_defaults[i][1]);
	for(i=0;preset_extra_defaults[i][0];i++)
		SetPresetValue(preset_extra_defaults[i][0],preset_extra_defaults[i][1]);
	len=LoadPreset(DEFAULT_CONFIG_FILE,&buf);
	if (len) {
		CL_ParseSetLines(buf,len,ApplyDefaultPresetOne);
		FS_FreeFile(buf);
	}
}

/* The only non-assignment used by these presets is the shared Ultra bundle.
   Apply it in file order so later card-specific settings still take effect. */
static void ApplySelectedPreset(const char *buf,int len)
{
	static const char fastest[]="bdgn_fastest";
	const char *p=buf,*end=buf+len,*next,*line;
	while(p<end) {
		next=(const char *)memchr(p,'\n',end-p);
		if (!next) next=end;
		line=p;
		while(line<next && (*line==' ' || *line=='\t')) line++;
		if (next-line >= (int)sizeof(fastest)-1 && !strncmp(line,fastest,sizeof(fastest)-1) &&
			(next-line==(int)sizeof(fastest)-1 || line[sizeof(fastest)-1]=='\r' ||
			 line[sizeof(fastest)-1]==' ' || line[sizeof(fastest)-1]=='\t'))
			CL_ApplyRetained(1);
		else
			CL_ParseSetLines(p,(int)(next-p),ApplySelectedPresetOne);
		p=next<end?next+1:end;
	}
}

static int LoadSelectedPreset(int id,const preset_file_t *preset,char **buf)
{
	int len=0;
	if ((id==65 || id==66 || id==57) && PresetUsesSmpWicked())
		return LoadPreset(id==57?"3dfxultra.cfg":id==66?lowram_presets[3].file:ULTRA_CONFIG_FILE,buf);
	if (PresetIsSoftware()) {
		if (id == 65) len=LoadPreset("softwarepsycho.cfg",buf);
		else if (id>=1 && id<=5 && software_presets[id])
			len=LoadPreset(software_presets[id],buf);
	}
	return len ? len : LoadPreset(preset->file,buf);
}

/* Keep the legacy texture restrictions when applying the common Default. */
static void ApplyDefaultDriverLimits(void)
{
	const char *driver;
	char target[MAX_QPATH];
	if (Q_stricmp(vid_ref->string,"gl") && Q_stricmp(vid_ref->string,"glam")) return;
	driver=Cvar_VariableString("gl_driver");
	if (BDGN_GLDriverIs(driver,"mtgl32")) {
		VID_GetMtGlTarget(target,sizeof(target));
		driver=target;
	}
	if (BDGN_GLDriverIs(driver,"pvrgl")) {
		SetPresetValue("gl_maxtexsize","256");
		SetPresetValue("gl_ext_pointparameters","0");
		SetPresetValue("gl_ext_compiled_vertex_array","0");
	} else if (BDGN_GLDriverIs(driver,"3dfxgl") && VID_FxFamilies()==PF_FX1) {
		SetPresetValue("fs_lowtextures","1");
	}
}

/* BDGN 3.55i: render scale that draws the software view at about want x (want*3/4) in the current sw_mode
   (320 -> 320x240 inside 640x480: 50 %); 100 when the mode is already that small */
qboolean VID_GetModeInfo (int *width, int *height, int mode);
void VID_DesktopSize (int *w, int *h);	/* vid_dll.c */
static int VID_LowEndScale (int want)
{
	int w=0,h=0,percent;
	if (!VID_GetModeInfo(&w,&h,(int)Cvar_VariableValue("sw_mode")) || w<=0) return 100;
	percent=want*100/w;
	percent-=percent%RENDER_SCALE_STEP;
	if (percent<RENDER_SCALE_MIN) percent=RENDER_SCALE_MIN;
	if (percent>100) percent=100;
	return percent;
}

/* BDGN 3.55i: the new low-end options in the low RAM presets.
   Below 64 MB (bdgn_lowram): low RAM Ultra Fast draws about 400x300, low RAM PSYCHO Fast about 320x240, with the
   ultra texture pack and shorter draw distance. Below 32 MB (bdgn_extra_lowram): lowerThen32mb and
   lowerThen32mb EXTREME (gray lightmaps, compact walls, short draw distance, 4 kHz, 4 sound channels). */
static void VID_ApplyLowEndSteps (int id)
{
	int software=PresetIsSoftware();
	if ((id==13 || id==66) && Cvar_VariableValue("bdgn_lowram")) {
		if (software) {
			SetPresetValue("sw_render_scale",va("%i",VID_LowEndScale(id==66?320:400)));
			/* 3.60f: no draw distance (VID_DropUnmeasured set 0 anyway): weapons, items and enemies always seen */
		}
		if (FS_UltraTexPackAvailable() && (id==66 || software)) SetPresetValue("fs_lowtextures","2");
		/* BDGN 3.56s: below 64 MB PSYCHO stays at 8 kHz (its cfg); 4 kHz only for the machines below 32 MB */
	}
	if ((id==71 || id==72) && software) {
		SetPresetValue("sw_render_scale",va("%i",VID_LowEndScale(320)));
		SetPresetValue("sw_texfilter","0");
		SetPresetValue("sw_coloredlight","0");
	}
	if (id==71) {
		SetPresetValue("s_khz",FS_LowSound4PackAvailable()?"4":"8");	/* BDGN 3.56s: below 32 MB: 4 kHz (8 kHz without the 4 kHz pack) */
		SetPresetValue("s_loadas8bit","1");
	}
	if ((Cvar_VariableValue("bdgn_lowram") || Cvar_VariableValue("bdgn_legacy_graphics")) && Cvar_VariableValue("s_khz")>22) {
		SetPresetValue("s_khz","22");	/* 44 kHz HD sound (MAX Fidelity) is too big for low RAM / legacy machines */
		SetPresetValue("s_loadas8bit","0");
	}
	if (software && (id==66 || id==71 || id==72)) {
		/* BDGN 3.57: old-school modes on the slowest machines (TEORETISK ANALYS):
		   half vertical resolution halves the span and z fill (the largest part of a software frame) for one stretched
		   copy; Doom light / Blake Stone shading take dynamic lights (and with Blake Stone the light styles) out of the
		   surface cache, so firing and flickering lights no longer rebuild it; the Quake 1 fog only changes the colour
		   of the draw distance fog these presets already have (same cost). */
		SetPresetValue("sw_halfrows","1");
		SetPresetValue(id==72?"sw_blakelight":"sw_doomlight","1");
		if (id!=66) SetPresetValue("sw_q1fog","1");
		/* BDGN 3.57a (TEORETISK ANALYS): light flash instead of dynamic lights in all three; below 32 MB also the
		   locked view (level floors drawn with one step per span, exact) and far enemies as sprites (lowerThen32mb
		   draws 3D enemies near, sprites beyond 512); EXTREME adds Doom's low detail (half the columns too: a quarter
		   of the pixels). lowrampsycho/EXTREME already draw every enemy as a sprite. */
		SetPresetValue("cl_lightflash","1");
		if (id!=66) SetPresetValue("cl_lockview","1");
		/* 3.57o: far enemies as sprites below 32 MB now through cl_modellod (further down) */
		if (id==72) SetPresetValue("sw_halfcols","1");	/* 3.57c: potato (2) further down */
		/* BDGN 3.57b (TEORETISK ANALYS): the two older switches that were already there, flat model light (one light
		   level per model, no shading per vertex) and no animation blending for far models (beyond 600), plus solid
		   Quake 1 particles where particles are on */
		SetPresetValue("gl_flatmodels","1");
		SetPresetValue("gl_lowdetail","600");
		SetPresetValue("sw_q1particles","1");
		/* BDGN 3.57c, FastDoom ideas (TEORETISK ANALYS, not measured):
		   - a smaller view (viewsize 90, EXTREME 80): 81% / 64% of the 3D pixels, the HUD around it costs nothing per frame;
		   - simple sky: one colour instead of the sky box spans;
		   - translucent surfaces: stipple (half the pixels, no blend table) in lowram PSYCHO, solid below 32 MB (glass
		     and water drawn as plain spans, no read of the pixel behind);
		   - object draw distance: pickups, gibs, debris, brass and smoke beyond 1024 (EXTREME 768) are not drawn;
		   - below 32 MB the far walls flat (flat floors 2), EXTREME every wall flat (3: no surface cache at all for the
		     world) and potato detail (a quarter of the columns, with half rows an eighth of the pixels). */
		SetPresetValue("viewsize",id==72?"80":"90");
		SetPresetValue("gl_fastsky","1");
		SetPresetValue("sw_stipplealpha",id==66?"1":"2");
		/* 3.60f: no object draw distance in any preset (owner: items and weapons must always be seen) */
		if (id==71) SetPresetValue("sw_flatfloors","2");
		if (id==72) {
			SetPresetValue("sw_flatfloors","3");
			SetPresetValue("sw_halfcols","2");
		}
	}
	/* BDGN 3.57d: the old-school modes in the OpenGL presets (TEORETISK ANALYS, not measured). On a 3D card of the time
	   the frame is limited by fill rate (pixels drawn, blended, fogged) and by uploading light maps:
	   - light flash: no dynamic lights, so no light map is rebuilt and sent to the card when someone fires;
	   - solid translucent surfaces and Quake 1 particles: no blend (a read of the frame buffer) for glass, water and
	     particles, and no texture for the particles;
	   - object draw distance and far enemies as sprites: fewer models;
	   - smaller view (lowram PSYCHO only): 81 % of the pixels.
	   Doom light and Blake Stone shading only change the look in OpenGL (the light map pass costs the same), so no preset
	   has them; Quake 1 fog is no preset either (fog is slow on some cards, S3 Virge among them). The cvars are only read
	   by BDGN's ref_gl; ref_glam ignores the gl_ ones. */
	if (!software) {
		int psycho = id==65 || (id>=54 && id<=64);	/* PSYCHO Fast and the PSYCHO preset of every card family */
		if (psycho || id==66) {
			SetPresetValue("cl_lightflash","1");
			SetPresetValue("gl_solidalpha","1");
			SetPresetValue("gl_q1particles","1");
		}
		if (id==66)
			SetPresetValue("viewsize","90");
	}
	/* BDGN 3.57l: simpler 3D models (bdgn_simplemodels.pak; TEORETISK ANALYS, not measured). Fewer triangles mean less
	   vertex transform and lighting (CPU work in both renderers on these machines) and fewer, smaller triangles to set up
	   in the software renderer; the smaller skins fit the CPU cache better. Software: Ultra Fast "simple", PSYCHO and
	   lowram PSYCHO "simpler", below 32 MB and EXTREME "simplest". OpenGL: the PSYCHO presets "simple" (the card fills
	   the pixels anyway, only the vertex work gets smaller). */
	if (software && id==3) {
		/* BDGN 3.58i, MEASURED on the owner's K6-3+ (Software 320x240, 3.57x, Faster 79.22 fps) and the i7 (Software
		   1080p): simplest models 84.51 (+6.7 %) beat simple 81.94 (+3.4 %); classic 2D enemies cost 7.8 % (73.07) and
		   the 2D sprite weapon 2.6 % (77.17), on the i7 4.8 % and 2.3 %. Ultra Fast keeps the fastest: simplest models,
		   enemies as in Faster (the static low-res 2D enemies, cl_simple2denemies 2, of its cfg; not the animated
		   classic 2D sprites), 3D weapon. The 2D sprite weapon and the classic 2D
		   enemies stay in the old-school presets (owner's decision, 2026-11-20). The sum is TEORETISK ANALYS until
		   the benchmark's "K6S / PRESET Ultra Fast" has measured it. */
		SetPresetValue("cl_simplemodels","3");
		SetPresetValue("cl_classic2d","0");
		SetPresetValue("cl_weaponsprites","0");
		SetPresetValue("cl_weapon_simple","0");
	}
	if (software && (id==65 || id==66)) SetPresetValue("cl_simplemodels","2");
	if (software && (id==71 || id==72)) SetPresetValue("cl_simplemodels","3");
	/* BDGN 3.58i, MEASURED (K6-3+ / GeForce2 GTS, OpenGL 640x480, on top of Faster 146.81 fps): simple models 144.69
	   (-1.4 %), simpler 157.72 (+7.4 %), simplest 168.77 (+15.0 %). PSYCHO Fast uses the simplest (was simple); the
	   PSYCHO presets of the card families keep simple until they are measured. */
	if (!software && id==65) SetPresetValue("cl_simplemodels","3");
	else if (!software && id>=54 && id<=64) SetPresetValue("cl_simplemodels","1");
	/* BDGN 3.57o: model LOD by distance (cl_modellod, TEORETISK ANALYS): one choice for simplest models far away, then
	   2D sprites, then sprites that change pose seldom (client/cl_ents.c CL_ModelLodSync sets the three rows). The levels
	   keep the far 2D distances these presets had (PSYCHO 768, lowram PSYCHO / below 32 MB 512):
	   light   - Software Ultra Fast (simplest beyond 1024);
	   medium  - Software lowram PSYCHO (simplest beyond 768, sprites beyond 1024);
	   strong  - PSYCHO, Software and OpenGL, every card family (simplest beyond 512, sprites beyond 768);
	   max     - OpenGL lowram PSYCHO, below 32 MB and EXTREME (sprites beyond 512). */
	/* BDGN 3.57o (TEORETISK ANALYS): simpler maps for the renderer (no wall/ceiling detail, fewer surfaces and edges)
	   in Software Ultra Fast and every PSYCHO / lowram / below 32 MB preset, both renderers */
	if ((software && id==3) || id==65 || id==66 || id==71 || id==72 || (!software && id>=54 && id<=64))
		SetPresetValue("cl_simpleworld","1");
	if (software && id==3) SetPresetValue("cl_modellod","1");
	if (software && id==66) SetPresetValue("cl_modellod","2");
	if (id==65 || (!software && id>=54 && id<=64)) SetPresetValue("cl_modellod","3");
	if ((!software && id==66) || id==71 || id==72) SetPresetValue("cl_modellod","4");
	if (software && id==65) {	/* PSYCHO: light flash and far enemies as sprites (beyond 768, cl_modellod 3) */
		SetPresetValue("cl_lightflash","1");
		SetPresetValue("sw_stipplealpha","1");	/* 3.57c: stipple instead of blending (TEORETISK ANALYS) */
	}
	if (id==72) {
		SetPresetValue("sw_test_gray_lightmaps","1");
		SetPresetValue("sw_test_compact_walls","1");
		SetPresetValue("gl_dynamic","0");
		SetPresetValue("cl_bloodfx","2");
		SetPresetValue("cl_impactfx","2");
		SetPresetValue("g_gibmode","2");
		SetPresetValue("cl_staticmap","1");
		SetPresetValue("cl_noitemrotate","1");
		SetPresetValue("cl_nosmoke","1");
		SetPresetValue("cl_notrails","1");
		SetPresetValue("s_khz",FS_LowSound4PackAvailable()?"4":"8");
		SetPresetValue("s_loadas8bit","1");
		SetPresetValue("s_maxchannels","4");
		SetPresetValue("s_mono","1");	/* BDGN 3.57m (TEORETISK ANALYS): half the samples to the sound card */
	}
}

/* BDGN 3.58u: the four speed tiers (owner, 2026-11-20). 1 Fast, 2 Faster, 3 Ultra Fast, 4 PSYCHO Fast; 0 for every other
   preset (default, MAX / High Fidelity, and the extreme lowram / below 32 MB / EXTREME presets 10-13, 66, 71, 72,
   which keep their own lower resolution, shorter view and 4 kHz). The card families by the name of their preset. */
static int VID_PresetTier (int id)
{
	const hwpreset_t *hp;
	if (id==1) return 1;
	if (id==2) return 2;
	if (id==3) return 3;
	if (id==65) return 4;
	hp=VID_HwPreset(id);
	if (!hp || hp->family==PF_K6GF) return 0;
	if (strstr(hp->label,"PSYCHO")) return 4;
	if (strstr(hp->label,"Ultra")) return 3;
	if (strstr(hp->label,"faster")) return 2;
	if (strstr(hp->label,"fast")) return 1;
	return 0;
}

/* BDGN 3.58u: a 3dfx card with two texture units (Voodoo2/3/4/5) on a 3dfx driver */
static qboolean VID_Preset3dfxTwoTmu (void)
{
	char target[MAX_QPATH];
	const char *driver=Cvar_VariableString("gl_driver");
	if (Q_stricmp(vid_ref->string,"gl") && Q_stricmp(vid_ref->string,"glam")) return false;
	if (BDGN_GLDriverIs(driver,"mtgl32")) {
		VID_GetMtGlTarget(target,sizeof(target));
		driver=target;
	}
	if (!(BDGN_GLDriverIs(driver,"3dfxgl") || BDGN_GLDriverIs(driver,FX_GLIDE_DRIVER) ||
		  BDGN_GLDriverIs(driver,WICKED_DRIVER) || BDGN_GLDriverIs(driver,FXGLAM_DRIVER)))
		return false;
	return (VID_FxFamilies() & PF_FX2) != 0;
}

/* BDGN 3.58u: what each tier contains (owner, 2026-11-20). Percentages: MEASURED on the owner's K6-3+ 616 MHz, one
   setting at a time against the baseline, OpenGL 640x480 (GeForce2 GTS) / Software 320x240, v3.58j; they do not add up,
   the presets themselves are measured by the K6-3 Special benchmark (K6S / PRESET ...).
   Fast: small visible reductions that give noticeable speed. No muzzle-flash light on the walls (+7.2 / +8.0 %), a light
     flash instead of dynamic lights (+6.6 / +7.4 %), reduced blood and bullet hits (+3.3 / +2.7 %), no smoke puffs
     (+1.3 / +1.4 %), fewer gibs; 3D weapon and enemies, the small texture pack (3.58v), 11 kHz, every sound.
   Faster: Fast plus classic 2D sprite mode (+33 / +24 %) with its explosions and the 2D weapon (+3.6 % OpenGL), no
     blood or gibs (+4.3 / +3.2, +1.8 / +2.1 %), dynamic lights off (+6.6 % OpenGL), low textures in Software (+3.7 %),
     11 kHz.
   Ultra Fast: faster and simpler. Classic 2D low-end (+48 / +37 %), the simple one-colour explosions and hits
     (+2.3 / +2.5 %; shown over the classic sprites since 3.58u, client/cl_tent.c), fullbright (all objects) in OpenGL
     (+6.9 %) / Blake Stone shading in Software (+7.8 %), no particles, smoke, trails, steam, banners, corpses; the
     simplest models, the lowest textures in Software, 8 kHz.
   PSYCHO Fast: everything, but not a lower resolution or a shorter view: Ultra Fast plus single-colour walls
     (+23 / +35 %), flat floors and walls in Software (+29 %), far enemies as 2D beyond 384 (+12 / +12 %), 8 kHz. No half
     rows or columns, no render scale, no draw distance: those stay in the extreme presets.
   Sound rate: the tier sets the K6-class rate; the 3.58r block below raises it on newer and fast computers. */
static void VID_ApplyTierDesign (int id, int k6gf)
{
	int tier=VID_PresetTier(id), soft=PresetIsSoftware();
	if (id==10 || id==11 || id==12 || id==13 || id==66 || id==71 || id==72) {
		/* 3.58v: the low RAM and below 32 MB presets step the same way (owner 2026-11-21): low RAM default and fast
		   mip 0, faster 2, Ultra Fast, PSYCHO, below 32 MB and EXTREME 3 */
		int lt=id==10||id==11?1:id==12?2:3;
		SetPresetValue("gl_picmip",lt==1?"0":lt==2?"2":"3");
		SetPresetValue("gl_playermip",lt==1?"0":lt==2?"2":"3");
		/* the low texture packs as in the four steps: small in fast and faster, ultra from Ultra Fast on (low RAM
		   default keeps its cfg) */
		if (id!=10) SetPresetValue("fs_lowtextures",lt==3 && FS_UltraTexPackAvailable()?"2":"1");
		return;
	}
	if (!tier) return;
	/* all four: the full picture size and view (only the extreme presets lower them) */
	SetPresetValue("sw_halfrows","0");
	SetPresetValue("sw_halfcols","0");
	SetPresetValue("sw_render_scale","100");
	SetPresetValue("sw_drawdist","0");
	SetPresetValue("gl_drawdist","0");
	SetPresetValue("cl_objdist","0");
	SetPresetValue("viewsize","100");
	/* no model distance chain: it would set cl_far2d itself (client/cl_ents.c CL_ModelLodSync); PSYCHO sets cl_far2d */
	SetPresetValue("cl_modellod","0");
	SetPresetValue("cl_simplemodels_far","0");
	if (tier<3) SetPresetValue("cl_simpleworld","0");
	if (tier<3) SetPresetValue("cl_simplemodels","0");	/* the full 3D enemies until Ultra Fast */
	/* Fast and up: the settings that cannot be seen or heard (the primary sound buffer, the fastest mixer profile) */
	SetPresetValue("s_primary","1");
	SetPresetValue("s_profile","4");
	SetPresetValue("cl_weaponlights","0");
	SetPresetValue("cl_lightflash","1");
	SetPresetValue("cl_nosmoke","1");
	if (tier==1) {
		SetPresetValue("cl_bloodfx","1");
		SetPresetValue("cl_impactfx","1");
		SetPresetValue("g_gibmode","1");
		SetPresetValue("cl_classic2d","0");
		SetPresetValue("cl_simple2denemies","0");
		SetPresetValue("cl_weaponsprites","0");	/* the 3D weapon */
		SetPresetValue("cl_weapon_simple","0");
		SetPresetValue("cl_static_rockets","0");
		SetPresetValue("cl_static_bfg","0");
		SetPresetValue("cl_simpleblaster","0");
		SetPresetValue("cl_footsteps","1");
		SetPresetValue("cl_vwep","1");
		SetPresetValue("cl_blend","1");
		SetPresetValue("gl_polyblend","1");
		SetPresetValue("s_basic_sounds","0");
		SetPresetValue("s_maxchannels","32");
		SetPresetValue("fs_lowtextures","1");	/* 3.58v: the small texture pack (half size) in Fast and Faster */
		SetPresetValue("cl_particles","1");
		SetPresetValue("s_khz","11");
	} else {
		SetPresetValue("cl_bloodfx","2");
		SetPresetValue("cl_impactfx","2");
		SetPresetValue("g_gibmode","2");
		SetPresetValue("cl_simple2denemies","0");
		SetPresetValue("cl_weaponsprites","1");	/* the 2D weapon of the classic look */
		SetPresetValue("cl_weapon_simple","0");
		SetPresetValue("gl_dynamic","0");
		SetPresetValue("fs_lowtextures","1");	/* 3.58v: both renderers (owner 2026-11-21; MEASURED -0.7 % on K6 + GeForce2
			OpenGL and no gain on the Pentium 133 in Software, both within noise: the owner's choice for the look) */
		SetPresetValue("s_khz","11");
	}
	if (tier==2) {
		SetPresetValue("cl_classic2d","1");
		SetPresetValue("cl_static_rockets","2");	/* the classic 2D explosions */
		SetPresetValue("cl_static_bfg","2");
		SetPresetValue("cl_simpleblaster","2");
	}
	if (tier>=3) {
		SetPresetValue("cl_classic2d","3");	/* low-end */
		SetPresetValue("cl_static_rockets","1");	/* simple one-colour explosions and hits */
		SetPresetValue("cl_static_bfg","1");
		SetPresetValue("cl_simpleblaster","1");
		if (soft) {
			SetPresetValue("sw_blakelight","1");
		} else
			SetPresetValue("gl_fastworld","1");	/* fullbright (all objects) */
		SetPresetValue("fs_lowtextures",FS_UltraTexPackAvailable()?"2":"1");	/* 3.58v: the ultra pack, both renderers */
		SetPresetValue("cl_particles","0");
		SetPresetValue("cl_notrails","1");
		SetPresetValue("cl_nosteam","1");
		SetPresetValue("g_noflies","1");
		SetPresetValue("cl_nobanners","1");
		SetPresetValue("cl_nodecorpses","1");
		SetPresetValue("cl_simple_rail","1");
		SetPresetValue("cl_simplemodels","3");
		SetPresetValue("s_khz","8");
		/* 3.59g (owner 2026-11-21): MEASURED on the K6-3+ in Software (v3.59b), Ultra Fast with the 2D enemy
		   animation 0 instead of 2: +4.2 % at 320x240, +1.6 % at 640x480 */
		SetPresetValue("cl_simpleenemy_anim","0");
		/* 3.59g: OpenGL draws the 2D enemies as the simplest 3D models. MEASURED on K6-3+ / GeForce2 (v3.58j, when
		   a bug did this): Faster + simplest models 170.0 against 146.7 (+16 %), PSYCHO 277.2; v3.59b with real
		   sprites: PSYCHO 216.7, 920 sprite uploads during the demo, 1 % low 119 -> 68 fps. Software keeps the
		   sprites (MEASURED faster there). */
		SetPresetValue("r_duke_simple3d",soft?"0":"1");
	} else
		SetPresetValue("r_duke_simple3d","0");
	/* 3.58v (owner 2026-11-21, from Pa1983's own cfg, 139 fps on Voodoo2 SLI with v3.27): texture mip steps Fast 0,
	   Faster 2, Ultra Fast and PSYCHO 3, the same for player skins (Software MEASURED +4.1 / +7.3 % for mip 1 / 2 on the
	   K6-3+; OpenGL GeForce2 -0.6 / -1.1 %, Voodoo2 not measured). 3.58w: model smoothing stays on (r_lerpmodels 0
	   MEASURED no gain, VID_DropUnmeasured). */
	SetPresetValue("gl_picmip",tier==1?"0":tier==2?"2":"3");
	SetPresetValue("gl_playermip",tier==1?"0":tier==2?"2":"3");
	if (tier==4) {
		SetPresetValue("r_drawflat","2");	/* single-colour walls */
		if (soft) SetPresetValue("sw_flatfloors","3");
		SetPresetValue("cl_far2d","384");
		if (!soft)	/* 3.59g (owner 2026-11-21: PSYCHO "ALL in", also gains under 1 %; full view in every preset but the
			   ultra low ones, so no draw distance): far 2D off MEASURED +1.1 % on PSYCHO (K6-3+ / GeForce2, v3.59b).
			   Software keeps its far 2D. */
			SetPresetValue("cl_far2d","0");
	} else {
		SetPresetValue("r_drawflat","0");
		SetPresetValue("sw_flatfloors","0");
		SetPresetValue("cl_far2d","0");
	}
	/* multitexture: a 3dfx card with two texture units has it in every tier (3.58u: MEASURED on Pa1983's Voodoo2 SLI, the
	   general presets turned it off and ran at 33-36 fps against 87 in default); K6 + GeForce on in Fast to Ultra Fast,
	   off in PSYCHO (MEASURED -4.2 % with single-colour walls); otherwise as the cfg says */
	if (!soft && VID_Preset3dfxTwoTmu())
		SetPresetValue("gl_ext_multitexture","1");
	if (k6gf)
		SetPresetValue("gl_ext_multitexture",tier>=1 && tier<=3?"1":"0");
}

/* BDGN 3.58w (owner 2026-11-21: "om nej så skall de bort" - out of the presets, kept in the menus): settings that were
   MEASURED with no gain or a loss on the owner's K6-3+ 616 MHz (v3.58j, one at a time against the baseline, Software
   320x240 / OpenGL 640x480 GeForce2 GTS): Quake 1 fog -5.9 %, Software draw distance medium/short -5.6 / -5.7 %,
   locked view -1.8 %, Build floors -1.7 %, stipple/solid translucency 0, grey light maps 0, compact walls 0, object
   draw distance 768-1536 0, fewer sound channels 0, mono 0, r_lerpmodels 0 -0.4 / -0.7 %, gl_maxtexsize 128 0, the
   low texture packs in OpenGL -0.8 / -0.9 % (Software +3.7 / +4.6 %: kept there). EXTREME gets the one draw distance
   that measured faster (sw_drawdist 6, +3.3 %). Doom / Wolfenstein 3D mode keep their locked view (cl_retromode). */
static void VID_DropUnmeasured (int id)
{
	int soft=PresetIsSoftware();
	SetPresetValue("sw_q1fog","0");
	SetPresetValue("gl_q1fog","0");
	SetPresetValue("sw_drawdist","0");	/* 3.60f (owner: weapons, items and enemies always seen): not even in EXTREME, where it MEASURED +3.3 % (fog hides everything beyond ~590 units) */
	SetPresetValue("cl_lockview","0");
	SetPresetValue("sw_buildfloors","0");
	SetPresetValue("sw_stipplealpha","0");
	SetPresetValue("gl_solidalpha","0");
	SetPresetValue("sw_test_gray_lightmaps","0");
	SetPresetValue("sw_test_compact_walls","0");
	SetPresetValue("cl_objdist","0");
	SetPresetValue("s_maxchannels","32");
	SetPresetValue("s_mono","0");
	SetPresetValue("r_lerpmodels","1");
	SetPresetValue("gl_maxtexsize","0");
	/* 3.59g: the low texture packs stay as the tiers set them (owner's rule: presets that have the low fidelity pack
	   keep it); 3.58w had taken them out of the OpenGL presets here */
	(void)soft;
}

/* 3.60i: owner's EXTREME configuration (2026-11-23), after the generic
   low-RAM setup. Shared by the toggle, preset selection and preset benchmark.
   Copy only graphics/audio choices, not the config file or machine/user state.
   Display modes and the existing half-desktop window handling stay separate.
   Gameplay objects must remain visible at every distance. */
static void VID_ApplyExtremeConfig (void)
{
	static const char *common[][2] = {
		{ "viewsize", "60" }, { "cl_classic2d", "1" }, { "cl_lowend_sprites", "1" },
		{ "cl_sprite_rate", "2" }, { "cl_simpleenemy_anim", "2" }, { "cl_weaponsprite_anim", "2" },
		{ "cl_duke_directions", "2" }, { "cl_simpleenemy_lod", "2" }, { "cl_simple2denemies", "0" },
		{ "cl_simplemodels", "0" }, { "cl_simpleworld", "1" }, { "cl_modellod", "4" },
		{ "cl_simplemodels_far", "512" }, { "cl_weaponsprites", "1" }, { "cl_weapon_simple", "1" },
		{ "cl_staticenemies", "0" }, { "r_brightskins", "1" }, { "r_spritetint", "1" },
		{ "cl_retroweapons", "1" }, { "cl_simpleitems", "1" }, { "cl_simpleblaster", "0" },
		{ "cl_static_rockets", "0" }, { "cl_static_bfg", "0" }, { "cl_staticlightstyles", "0" },
		{ "cl_staticmap", "1" }, { "cl_noitemrotate", "1" }, { "cl_nosteam", "1" },
		{ "g_noflies", "1" }, { "cl_nobanners", "1" }, { "cl_nodecorpses", "1" },
		{ "cl_nospawnpads", "0" }, { "cl_nosmoke", "1" }, { "cl_notrails", "1" },
		{ "cl_sprite_hits", "1" }, { "cl_sprite_trails", "1" }, { "cl_fastfx", "1" },
		{ "cl_bloodfx", "2" }, { "cl_impactfx", "2" }, { "g_gibmode", "2" },
		{ "cl_weaponlights", "0" }, { "cl_particles", "0" }, { "cl_blend", "0" },
		{ "cl_footsteps", "0" }, { "cl_fxlevel", "0" }, { "cl_hdfx", "0" },
		{ "cl_decals", "0" }, { "cl_3dexplo", "0" }, { "cl_3dbfg", "0" },
		{ "cl_3dblaster", "0" }, { "cl_3drail", "0" }, { "cl_hdgibs", "0" },
		{ "cl_hdsmoke", "0" }, { "cl_hdmuzzle", "0" }, { "cl_heatdistort", "0" },
		{ "cl_grenadesmoke", "0" }, { "cl_explosmoke", "0" }, { "cl_shells", "0" },
		{ "cl_cells", "0" }, { "cl_fx3d_tex", "0" }, { "cl_lightflash", "1" },
		{ "cl_lockview", "0" }, { "cl_vwep", "0" }, { "cl_hack_nocinematics", "0" },
		{ "r_fullbright", "0" }, { "r_lerpmodels", "1" }, { "r_drawflat", "2" },
		{ "r_norefresh", "0" }, { "r_drawworld", "1" }, { "r_drawentities", "1" },
		{ "cl_entities", "1" }, { "cl_gun", "1" }, { "fastsqrt", "1" },
		{ "fastsqrt2", "1" }, { "s_pcspeaker", "1" }, { "s_musicmode", "2" },
		{ "cd_nocd", "1" }, { "s_loadas8bit", "1" }, { "s_basic_sounds", "1" },
		{ "s_profile", "0" }, { "s_maxchannels", "32" }, { "s_mono", "0" },
		{ "s_primary", "0" }, { "s_mixinterval", "15" }, { "s_mixahead", "0.2" },
		{ "s_nosound", "0" }, { "s_initsound", "1" },
		{ NULL, NULL }
	};
	static const char *opengl[][2] = {
		{ "gl_dynamic", "0" }, { "gl_weapon_texcache", "12" }, { "gl_fastparticles", "0" },
		{ "gl_vertex_arrays", "1" }, { "gl_ext_compiled_vertex_array", "1" }, { "gl_ext_pointparameters", "1" },
		{ "gl_ext_palettedtexture", "1" }, { "gl_anisotropy", "1" }, { "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST" },
		{ "gl_texturesolidmode", "default" }, { "gl_texturealphamode", "default" }, { "gl_solidalpha", "0" },
		{ "gl_q1particles", "0" }, { "gl_q1fog", "0" }, { "gl_blakelight", "1" },
		{ "gl_doomlight", "1" }, { "gl_picmip", "3" }, { "gl_playermip", "3" },
		{ "gl_skymip", "4" }, { "gl_round_down", "1" }, { "gl_dlight_maxdist", "512" },
		{ "gl_dlight_cull", "1" }, { "gl_cull", "1" }, { "gl_finish", "0" },
		{ "gl_ztrick", "1" }, { "gl_batchworld", "0" }, { "gl_fastdraw", "3" },
		{ "gl_modelbatch_auto", "0" }, { "gl_particlebatch_auto", "0" }, { "gl_flashblend", "0" },
		{ "gl_fastwater", "0" }, { "gl_fastsky", "0" }, { "gl_fastworld", "0" },
		{ "gl_flatfloors", "0" }, { "gl_flatmodels", "0" }, { "gl_lowdetail", "0" },
		{ "gl_hack_noimages", "0" }, { "gl_hack_notextures", "0" }, { "gl_polyblend", "0" },
		{ "gl_shadows", "0" }, { "gl_sun", "0" }, { "gl_lightmap", "0" },
		{ "gl_water_reflect", "0" }, { "gl_godrays", "0" }, { "gl_relief", "0" },
		{ "gl_relief_depth", "1.5" }, { "gl_pixel_lighting", "0" }, { "gl_pixel_bump", "1" },
		{ "gl_pixel_models", "1" }, { "gl_water_interaction", "0" }, { "gl_water_surface", "0" },
		{ "gl_colour_grade", "0" }, { "gl_tonemap", "0" }, { "gl_ssao", "0" },
		{ "gl_bloom", "0" }, { "gl_fxaa", "0" }, { "gl_ext_samples", "0" },
		{ "gl_hdtextures", "0" }, { "gl_upscale_filter", "0" }, { "gl_render_scale_copyformat", "0" },
		{ "gl_render_scale_pixelcopy", "0" }, { "gl_render_scale_legacy", "0" },
		{ NULL, NULL }
	};
    int i;
    for (i=0;common[i][0];i++)
        SetPresetValue(common[i][0],common[i][1]);
    SetPresetValue("cl_retromode","0");
    SetPresetValue("cl_yshear",Cvar_VariableValue("r_yshear_support")>=1?"1":"0");
    SetPresetValue("fs_lowtextures",FS_UltraTexPackAvailable()?"2":"1");
    SetPresetValue("s_khz",FS_LowSound4PackAvailable()?"4":"8");
    SetPresetValue("cl_objdist","0");
    SetPresetValue("gl_drawdist","0");
    SetPresetValue("sw_drawdist","0");
    if (!PresetIsSoftware()) {
        for (i=0;opengl[i][0];i++)
            SetPresetValue(opengl[i][0],opengl[i][1]);
        SetPresetValue("r_duke_simple3d","1");
        /* Preserve the two-TMU 3dfx rule and the driver's existing fallbacks. */
        if (VID_Preset3dfxTwoTmu()) SetPresetValue("gl_ext_multitexture","1");
        if (Cvar_VariableValue("gl_render_scale_fbo_capable"))
            SetPresetValue("gl_scene_gamma","0.7");
    }
    /* Quake 1 fog creates its own far plane even when draw distance is zero. */
    SetPresetValue("gl_q1fog","0");
    SetPresetValue("sw_q1fog","0");
}

/* BDGN 3.59y (owner 2026-11-22): "fastest preset" toggle (menu ANIMATIONS, under Wolfenstein 3D mode,
   cl_fastestpreset). On: lowerThen32mb EXTREME (id 72), on every computer, also above 32 MB. Its
   older profile measured 17.3 fps against 7.5 in default on Pentium 133 (v3.59e);
   the owner's updated 3.60i graphics/audio profile below still needs measurement. The preset that was on
   is remembered (cl_fastest_prev) and comes back when the toggle is turned off (default = id's 3.21 when unknown). */
qboolean VID_ApplyPresetSettings(int id,qboolean archive);
static int VID_PresetIdByName (const char *name)
{
	preset_file_t p;
	int id;
	if (!name || !name[0]) return 0;
	for (id=1;id<100;id++)
		if (PresetLookup(id,&p) && p.name && !Q_stricmp((char *)p.name,(char *)name)) return id;
	return 0;
}
/* BDGN 3.60g (owner 2026-11-22, as asked in 3.59x): the toggle also plays in a window of half the screen (half the
   desktop width and height: a quarter of the pixels; TEORETISK ANALYS from the MEASURED P133 Software numbers, where the
   pixels are most of the frame) with render scale 75 % (Software: sw_render_scale; OpenGL: gl_render_scale where the
   driver can render to a texture). The window size, full screen and render scale from before are kept in
   cl_fastest_prev_video and come back when the toggle is turned off. */
static void VID_FastestHalfWindow (void)
{
	int dw=0,dh=0,m,w,h,best=-1,bestpix=0;
	int soft=!Q_stricmp(vid_ref->string,"soft") || !Q_stricmp(vid_ref->string,"swam");
	VID_DesktopSize(&dw,&dh);
	if (dw<=0 || dh<=0) { dw=640; dh=480; }
	for (m=0;VID_GetModeInfo(&w,&h,m);m++)
		if (w<=dw/2 && h<=dh/2 && w*h>bestpix) { best=m; bestpix=w*h; }
	if (best<0) best=0;	/* 320x240 */
	SetPresetValue("vid_fullscreen","0");
	SetPresetValue(soft?"sw_mode":"gl_mode",va("%i",best));
	if (soft) SetPresetValue("sw_render_scale","75");
	else if (Cvar_VariableValue("gl_render_scale_fbo_capable")) SetPresetValue("gl_render_scale","75");
}
void VID_FastestPresetToggle (int on)
{
	int id;
	Cvar_Get("cl_fastestpreset","0",CVAR_ARCHIVE);
	if (on) {
		if (!Cvar_VariableValue("cl_fastestpreset")) {
			Cvar_FullSet("cl_fastest_prev",Cvar_VariableString("cfg_preset"),CVAR_ARCHIVE);
			Cvar_FullSet("cl_fastest_prev_video",va("%i %i %i %i %i",(int)Cvar_VariableValue("vid_fullscreen"),
				(int)Cvar_VariableValue("gl_mode"),(int)Cvar_VariableValue("sw_mode"),
				(int)Cvar_VariableValue("sw_render_scale"),(int)Cvar_VariableValue("gl_render_scale")),CVAR_ARCHIVE);
		}
        if (!VID_ApplyPresetSettings(72,true)) return;
        VID_FastestHalfWindow();
	} else {
		int fs,glm,swm,sws,gls;
		id=VID_PresetIdByName(Cvar_Get("cl_fastest_prev","",CVAR_ARCHIVE)->string);
		if (id<=0 || id==72) id=4;	/* default: id's 3.21 */
		VID_ApplyPresetSettings(id,true);
		if (sscanf(Cvar_Get("cl_fastest_prev_video","",CVAR_ARCHIVE)->string,"%i %i %i %i %i",&fs,&glm,&swm,&sws,&gls)==5) {
			SetPresetValue("vid_fullscreen",va("%i",fs));
			SetPresetValue("gl_mode",va("%i",glm));
			SetPresetValue("sw_mode",va("%i",swm));
			SetPresetValue("sw_render_scale",va("%i",sws>0?sws:100));
			SetPresetValue("gl_render_scale",va("%i",gls>0?gls:100));
		}
	}
	Cvar_SetValue("cl_fastestpreset",on?1:0);
}
qboolean VID_ApplyPresetSettings(int id,qboolean archive)
{
	preset_file_t preset;
	const gl_fidelity_preset_t *tier=GL_FidelityPreset(id);
	char *buf=NULL;
	int len=0, fidelity;
	int tier_id=id;	/* BDGN 3.56v: id becomes 5 (MAX's cfg) for the tiers below; the 4K preset is told apart by tier_id */
	int k6gf=id>=84 && id<=88 ? id : 0;	/* BDGN 3.58d: K6 + GeForce2/3 = the general preset, then its own values below */

	if (archive && id!=72) { Cvar_Get("cl_fastestpreset","0",CVAR_ARCHIVE); Cvar_SetValue("cl_fastestpreset",0); }	/* 3.59y: another preset chosen: the toggle shows off */
	if (k6gf) id=k6gf==84?4:k6gf==85?1:k6gf==86?2:k6gf==87?3:65;

	if (tier) {
		if (!VID_FidelityPresetAvailable(id)) return false;
  if (BDGN_GLDriverIs(Cvar_VariableString("gl_driver"),"mtgl32"))
   Com_Printf("SMP OpenGL: quality profile selected; unsupported wrapper effects use the original rendering path.\n");
		id=5;
	}

	if (!PresetLookup(id,&preset)) return false;
	if (id!=4) {
		len=LoadSelectedPreset(id,&preset,&buf);
		if (!len) {
			Com_Printf("config preset: missing %s\n",preset.file);
			return false;
		}
	}
	preset_archive=archive;
	/* BDGN 3.58w: a preset ends Doom / Wolfenstein 3D mode first (its own settings follow and win); no preset turns a
	   mode on: they are the player's choice (benchmark cases RETRO / ...) */
	if (Cvar_VariableValue("cl_retromode")) BDGN_RetroModeSet(0);
	SetPresetValue("cl_retromode","0");
	SetPresetValue("cl_yshear","0");	/* 3.60i: the Duke Nukem 3D view is off in every preset (default = vanilla 3.21) */
	SetPresetValue("cl_retro_prevtex","-1");	/* 3.59b: the preset sets the texture pack itself */
	ApplyPresetBase();
	if (buf) {
		ApplySelectedPreset(buf,len);
		FS_FreeFile(buf);
	}
	if(PresetUses3DWeapons()) {
		SetPresetValue("cl_weaponsprites","0");
		SetPresetValue("cl_weapon_simple","0");
	}
	fidelity=id==5;
	if (fidelity) {
		Cvar_SetValue("sw_render_scale",100);
		Cvar_SetValue("gl_render_scale",100);
	}
	SetPresetValue("sw_texfilter",fidelity?"1":"0");
	/* BDGN 3.55ai: flat floors and far walls (Wolfenstein style) only in the software PSYCHO preset (it changes the look);
	   off everywhere else until it is measured on a real Pentium */
	SetPresetValue("sw_flatfloors",id==65?"2":"0");
	/* BDGN 3.57: old-school modes, off by default; the low end presets turn on the ones expected to give speed
	   (TEORETISK ANALYS until the benchmark has measured them on a Pentium; VID_ApplyLowEndSteps sets them) */
	SetPresetValue("sw_doomlight","0");
	SetPresetValue("sw_blakelight","0");
	SetPresetValue("sw_buildfloors","0");
	SetPresetValue("sw_q1fog","0");
	SetPresetValue("sw_halfrows","0");
	SetPresetValue("sw_wolfpalette","0");
	SetPresetValue("sw_halfcols","0");	/* BDGN 3.57a */
	SetPresetValue("cl_lockview","0");
	SetPresetValue("cl_far2d","0");
	SetPresetValue("cl_lightflash","0");
	SetPresetValue("sw_q1particles","0");	/* BDGN 3.57b */
	SetPresetValue("cl_objdist","0");	/* BDGN 3.57c: FastDoom ideas, off by default (VID_ApplyLowEndSteps) */
	SetPresetValue("cl_simplemodels","0");	/* BDGN 3.57l: simpler 3D models, off by default (VID_ApplyLowEndSteps) */
	SetPresetValue("cl_simplemodels_far","0");	/* BDGN 3.57m: simplest models far away, off by default */
	SetPresetValue("cl_lowspec3d","0");	/* BDGN 3.57s: the bundle switch; the presets set its three settings one by one */
	Cvar_Get("cl_lowspec3d","0",CVAR_ARCHIVE)->modified=false;	/* so it does not turn off what this preset sets below */
	SetPresetValue("cl_modellod","0");	/* BDGN 3.57o: model LOD by distance, off by default (VID_ApplyLowEndSteps) */
	SetPresetValue("cl_simpleworld","0");	/* BDGN 3.57o: simpler maps, off by default (VID_ApplyLowEndSteps) */
	SetPresetValue("s_mono","0");	/* BDGN 3.57m: stereo, as id's 3.21; mono only in EXTREME (below) */
	SetPresetValue("viewsize","100");
	SetPresetValue("sw_stipplealpha","0");
	SetPresetValue("gl_doomlight","0");	/* BDGN 3.57d: the OpenGL old-school modes */
	SetPresetValue("gl_blakelight","0");
	SetPresetValue("gl_q1fog","0");
	SetPresetValue("gl_q1particles","0");
	SetPresetValue("gl_solidalpha","0");
	if (id==65 && PresetIsSoftware())
		SetPresetValue("sw_halfrows","1");	/* PSYCHO: half the rows to fill (the Wolfenstein floors are there already) */
	SetPresetValue("sw_asmspans","1");	/* BDGN 3.55ac: every preset draws the software spans with id's Pentium code (measured 2.3-2.9x on spans) */
	SetPresetValue("sw_prefetch","0");
	SetPresetValue("sw_coloredlight",fidelity?"1":"0");
	SetPresetValue("cpu_auto","1");
	SetPresetValue("gl_shadows","0");
	SetPresetValue("gl_water_surface","0");
	SetPresetValue("gl_water_interaction","0");
	SetPresetValue("gl_water_reflect","0");	/* BDGN 3.55o: only High Fidelity turns it on */
	/* BDGN 3.55u: three tiers of weapon effects. HD weapon effects (sprites, light) are the middle tier: on in
	   default config and Fast as well as MAX, Ultra and High Fidelity, on BDGN's OpenGL renderer with any OpenGL
	   driver, not on legacy machines. Faster, Ultra Fast, PSYCHO Fast, the card family and low RAM presets keep the
	   original effects. The 3D effects below are the top tier (modern driver only); where both are on, the 3D
	   effects replace rockets, BFG and blaster bolts and HD covers grenades, railgun and hits. */
	{
		/* BDGN 3.55ab: the presets pick an effect level and CL_FXLevel_Value (cl_gore.c, the same table as the menu
		   row "effect level") says what each row is at that level on this computer:
		     HD 3D (2): MAX Fidelity, Ultra, High Fidelity 1-3 and 4K
		     HD (1):    Fast
		     original:  default config (3.21, rule of 3.55ad), Faster, Ultra Fast, PSYCHO Fast, the card family presets, low RAM */
		static const char *lv[]={"cl_hdfx","cl_decals","cl_3dexplo","cl_3dbfg","cl_3dblaster","cl_3drail","cl_hdgibs","cl_hdsmoke","cl_hdmuzzle",NULL};
		int fxlevel=Cvar_VariableValue("bdgn_lowram")?0:((tier || id==5)?2:(id==1?1:0)), q;	/* 3.55ad: default config (4) = original */
		if (tier_id==83 && fxlevel==2 && CL_FXLevel_Allowed(4)) fxlevel=4;	/* BDGN 3.56r: Ultra High Fidelity - 4K: HD 3D 4K */
		for (q=0;lv[q];q++)
			SetPresetValue(lv[q],va("%i",CL_FXLevel_Value(fxlevel,lv[q])));
		SetPresetValue("cl_fxlevel",va("%i",fxlevel));
	}
	{	/* BDGN 3.55t: modern 3D weapon effects in MAX, Ultra and High Fidelity when the driver allows them; heat
		   distortion (a copy of the view every frame) in High Fidelity only */
		SetPresetValue("cl_heatdistort",!Cvar_VariableValue("bdgn_lowram") && ((tier && tier->pixel) || id==5) && CL_FX3D_CvarAvailable("cl_heatdistort")==1?"1":"0");
		/* BDGN 3.55v: the AI (Stable Diffusion) textures on the 3D effects in High Fidelity 1-3; MAX and Ultra keep
		   the standard ones (the middle way) */
		SetPresetValue("cl_fx3d_tex",tier && tier->pixel && CL_FX3D_CvarAvailable("cl_fx3d_tex")==1 &&
			(Cvar_VariableValue("bdgn_hdfx3d_sd_available") || FS_LoadFile("models/hdfx3d_sd/puffs.tga",NULL)>0)?"1":"0");
		if (tier_id==83 && CL_FXLevel_Value(4,"cl_fx3d_tex")>=2 && CL_FXLevel_Allowed(4))	/* BDGN 3.56r: 4K effect textures */
			SetPresetValue("cl_fx3d_tex",va("%i",CL_FXLevel_Value(4,"cl_fx3d_tex")));
	}
	/* BDGN 3.56s: shell casings are a modern effect of the middle presets and up: Fast (OpenGL), MAX Fidelity, the
	   Ultra and High Fidelity tiers. Off in default config (3.21), Faster, Ultra Fast, PSYCHO, the card family and
	   low RAM presets, and in the software renderer's Fast. */
	/* BDGN 3.58n: Fast only with an SSE2 processor (Pentium 4 / Athlon 64 and later). MEASURED: shell casings cost 53 %
	   on the K6-3+ / GeForce2 (OpenGL), and Fast was 35 % slower than default on Pa1983's VIA C3 800 / Voodoo2 (Socket
	   370, so not "legacy": casings, cells and HD effects on); the casings' part of it there is TEORETISK ANALYS. */
	SetPresetValue("cl_shells",CL_Shells_Available() && !Cvar_VariableValue("bdgn_lowram") &&
		(id==5 || (id==1 && !PresetIsSoftware() && !Cvar_VariableValue("bdgn_legacy_graphics") &&
		Cvar_VariableValue("bdgn_modern_lighting_cpu")))?"1":"0");	/* 3.57o: not in Fast on Socket 7 (MEASURED) */
	/* BDGN 3.56z: energy cells as the casings; lava bursts (a 3D effect) in MAX and the fidelity tiers, 2x in the best */
	SetPresetValue("cl_cells",CL_Cells_Available() && !Cvar_VariableValue("bdgn_lowram") &&
		(id==5 || (id==1 && !PresetIsSoftware() && !Cvar_VariableValue("bdgn_legacy_graphics") &&
		Cvar_VariableValue("bdgn_modern_lighting_cpu")))?"1":"0");	/* 3.57o: as cl_shells; 3.58n: SSE2 */
	SetPresetValue("cl_lavabursts",(id==5 && !PresetIsSoftware() && !Cvar_VariableValue("bdgn_lowram") &&
		(Cvar_VariableValue("bdgn_lavabursts_available") || FS_LoadFile("models/hdgore/lava_burst1.md2",NULL)>0))?(tier_id==83?"2":"1"):"0");
	/* BDGN 3.57h: modern grenade smoke and the smoke puff after the fire smoke: on in MAX Fidelity and the Ultra / High
	   Fidelity tiers (id 5) when the 3D effects can be drawn; off everywhere else (default config = 3.21) */
	{
		int modern3d = id==5 && !PresetIsSoftware() && !Cvar_VariableValue("bdgn_lowram");
		SetPresetValue("cl_grenadesmoke",modern3d && CL_FX3D_CvarAvailable("cl_grenadesmoke")==1?"1":"0");
		SetPresetValue("cl_explosmoke",modern3d && CL_FX3D_CvarAvailable("cl_explosmoke")==1?"1":"0");
	}
	SetPresetValue("fs_vanilla","0");	/* 3.55al: 1 only in the default config */
	SetPresetValue("gl_swapinterval","0");
	SetPresetValue("timedemo","0");
	SetPresetValue("cl_async","1");	/* BDGN 3.58q: fixed 100 Hz physics in every preset but default (vanilla below); the
		frame rate cap (r_maxfps) is the player's own and no preset changes it */
	SetPresetValue("cl_maxfps","100");	/* BDGN 3.58p: 100 fps = 10 ms per move, the highest and longest jumps (MEASURED with
		id's pmove.c on a flat floor: 42.25 / 192.0 units against 41.25 / 187.6 at 125); over the 125 of the cfg files */
	SetPresetValue("cl_run","1");
	if (id==4) {
		/* BDGN 3.55ad, a standing rule: "default config" is id's Quake II 3.21 - nothing else. The base above already
		   has every BDGN feature off; these are the values where BDGN's own base differs from 3.21's defaults
		   (client/cl_main.c, ref_gl/gl_rmain.c, client/snd_dma.c of id's 3.21 source). Kept from this computer: the
		   renderer and driver, the screen mode and gamma, and the mouse / key settings (not preset settings). */
		static const char *vanilla[][2]={
			{"gl_ext_multitexture","1"}, {"cl_maxfps","90"}, {"cl_async","0"},	/* 3.58o: no gl_swapinterval 1: vsync is always off (owner) */
			{"cl_run","0"}, {"crosshair","0"},
			{"s_musicsource","0"}, {"s_musicmode","0"},	/* CD audio */
			{"cl_skybox","0"}, {"gl_sky_lighting","1"},
   {"gl_hdtextures","0"}, {"gl_anisotropy","1"}, {"fs_lowtextures","0"}, {"fs_lowsounds","0"},
			{"sw_asmspans","1"},	/* 3.21's software renderer was the x86 code */
			{"fs_vanilla","1"},	/* 3.55al: only id's paks (pak0-pak2) are searched: no texture, sound or effect paks */
			{"s_khz","11"}, {"cl_fxlevel","0"}, {"gl_shadows","0"},
			{NULL,NULL}};
		int q;
		for (q=0;vanilla[q][0];q++)
			SetPresetValue(vanilla[q][0],vanilla[q][1]);
	}
	if (id==4 || id==10) ApplyDefaultDriverLimits();
	SetPresetValue("s_gl_audio_worker","0");
	if (id==71 || id==72) {
		SetPresetValue("fs_lowtextures",FS_UltraTexPackAvailable()?"2":"1");
		SetPresetValue("cl_classic2d","1");
		SetPresetValue("cl_lowend_sprites","1");
		SetPresetValue("cl_simple2denemies","0");
		SetPresetValue("cl_simpleenemies","3");
		SetPresetValue("cl_simpleitems","1");
		SetPresetValue("cl_static_rockets","0");
		SetPresetValue("cl_static_bfg","0");
		SetPresetValue("cl_simpleblaster","0");
		SetPresetValue("cl_staticenemies","0");
		SetPresetValue("cl_sprite_rate","2");
		SetPresetValue("cl_simpleenemy_anim","2");
		SetPresetValue("cl_duke_directions","2");
		SetPresetValue("cl_simpleenemy_lod","2");
		SetPresetValue("cl_weaponsprites","1");
		SetPresetValue("cl_weapon_simple","1");
		SetPresetValue("s_musicmode","2");
		SetPresetValue("cd_nocd","1");
		SetPresetValue("s_khz","11");
		SetPresetValue("s_loadas8bit","1");
		SetPresetValue("s_maxchannels","8");
		SetPresetValue("s_basic_sounds","1");
	}
	{	/* BDGN 3.56s: sound rate by weight. default config 11 kHz (3.21); Fast and the card family default presets
		   11 kHz (their cfg); Faster, Ultra Fast and PSYCHO 8 kHz (cfg); the slightly heavier Ultra Fidelity 1-3 and
		   Adaptive 22 kHz; the heaviest (MAX Fidelity, High Fidelity 1-3, Ultra High Fidelity 4K) 44 kHz when the HD
		   sound pack is installed, else 22 kHz. 4 kHz only below 32 MB (VID_ApplyLowEndSteps). */
		const char *khz=NULL;
		if (tier) khz=tier->pixel?"44":"22";
		else if (id==5) khz="44";
		if (khz && khz[0]=='4' && !FS_HDSoundPackAvailable()) khz="22";
		if (khz) {
			SetPresetValue("s_khz",khz);
			SetPresetValue("s_loadas8bit","0");
		}
	}
	VID_ApplyLowEndSteps(id);	/* BDGN 3.55i */
	VID_ApplyTierDesign(id,k6gf);	/* BDGN 3.58u: the four speed tiers (owner, 2026-11-20) */
	if (VID_PresetTier(id) || id==10 || id==11 || id==12 || id==13 || id==66 || id==71 || id==72)
		VID_DropUnmeasured(id);	/* BDGN 3.58w */
	/* BDGN 3.58w (owner 2026-11-21): PC speaker sound in the most extreme preset, lowerThen32mb EXTREME. No WAV is mixed
	   or loaded (client/snd_dma.c), so it should cost less than 4 kHz sound (TEORETISK ANALYS: on the Pentium 133 the
	   sound off MEASURED +22 %). The output is chosen by win32/snd_pcspk.c: the real speaker on Windows 95/98/ME and
	   NT/2000/XP, the sound card (headphones) on Vista and later. Every other preset turns it off. */
	/* 3.59g: PSYCHO too (owner: all in). MEASURED K6-3+ / GeForce2 v3.59b: PC speaker 189.3 against 4 kHz 187.5 (+1 %) */
	/* 3.60: not on 3dfx (Glide / MiniGL / WickedGL, also through mtgl32): MEASURED v3.59z dual Pentium II 300, Voodoo2
	   SLI, 3dfxgl: PSYCHO 161.3 with the PC speaker, 165.8 with the sound card (+2.8 %); Chromatic's Voodoo3 (one CPU)
	   174.8 both ways. The sound card is never slower there, so PSYCHO on 3dfx keeps it. */
	{
		const char *drv = Cvar_VariableString("gl_driver");
		int fx = !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") &&
			(Q_stristr_bdgn(drv,"3dfx") || Q_stristr_bdgn(drv,"wicked") || Q_stristr_bdgn(drv,"glide") ||
			 (Q_stristr_bdgn(drv,"mtgl") && (Q_stristr_bdgn(Cvar_VariableString("bdgn_mtgl_target"),"3dfx") || VID_3dfxSelected())));
		SetPresetValue("s_pcspeaker",id==72 || (VID_PresetTier(id)==4 && !fx)?"1":"0");
	}
	/* BDGN 3.58r, owner's rule (2026-11-20): the sound rate follows the computer. K6 class and the like (Socket 7,
	   32 MB or less, low RAM: bdgn_legacy_graphics / bdgn_lowram) keep the rates above: 11 kHz, 8 kHz in the speed
	   presets (MEASURED faster on the K6), at most 22 kHz in the quality presets. Newer computers (not legacy, no SSE2:
	   Pentium III, Athlon, VIA C3 ...) one step up: 8 -> 11, 11 -> 22, quality presets 22. Really fast computers (SSE2,
	   not legacy: Pentium 4 / Athlon 64 and later) 44 kHz in every preset, MAX Fidelity included, whether or not the
	   HD sound pack is installed (the mixing rate; the HD pack gives 44 kHz sounds as well). default config stays
	   id's 3.21 (11 kHz); the old-school modes (71, 72) keep their 4 / 8 kHz. */
	if (id!=4 && id!=71 && id!=72 && !Cvar_VariableValue("bdgn_lowram") && !Cvar_VariableValue("bdgn_legacy_graphics")) {
		int cur=(int)Cvar_VariableValue("s_khz");
		const char *k;
		if (Cvar_VariableValue("bdgn_modern_lighting_cpu"))
			k="44";
		else
			k=cur<=8?"11":"22";
		SetPresetValue("s_khz",k);
		if (k[0]!='1')
			SetPresetValue("s_loadas8bit","0");	/* 16-bit samples at 22 and 44 kHz */
	}
	SetPresetValue("cfg_preset",tier?tier->name:preset.name);
	/* BDGN 3.55ah: 3D surface structure in the High Fidelity presets with per-pixel lighting (the modern tiers) */
	SetPresetValue("gl_relief",((tier && tier->pixel) || id==5) && Cvar_VariableValue("bdgn_gl_relief_capable") &&
		!Cvar_VariableValue("bdgn_legacy_graphics")?"1":"0");
	SetPresetValue("gl_relief_depth","1.5");
	if (tier && tier->pixel>=4 && !Cvar_VariableValue("bdgn_legacy_graphics") && !Cvar_VariableValue("bdgn_lowram")) {
		/* BDGN 3.56a: the best High Fidelity preset follows the owner's own config.cfg of 2026-11-15: deeper surface
		   structure, the sun and lamps as light sources, god rays, long-lived blood and marks. The rest of that file
		   (8x MSAA, 16x anisotropy, FXAA, bloom 2, tone mapping, colour grade, per-pixel light 4, full-size water
		   reflection, 4K textures, all 3D effects) was this preset already. */
		SetPresetValue("gl_relief_depth","3");
		if (Cvar_VariableValue("bdgn_gl_soft_shadow_capable"))
			SetPresetValue("gl_sun","1");
		if (Cvar_VariableValue("bdgn_gl_godrays_capable"))
			SetPresetValue("gl_godrays","1");
		SetPresetValue("cl_blood_amount","1");
		SetPresetValue("cl_blood_time","180");
		SetPresetValue("cl_blood_max","384");
		SetPresetValue("cl_decals_time","45");
		SetPresetValue("cl_decals_max","128");
	}
	if (id==5 && !Cvar_VariableValue("bdgn_legacy_graphics") && !Cvar_VariableValue("bdgn_lowram")) {
		/* BDGN 3.55al: MAX gets the new picture settings, each only when the driver can do it: 3D surface structure
		   (above), shadows from the lights (volumes with a stencil buffer), sun and lamps as light sources, god rays */
		if (Cvar_VariableValue("bdgn_gl_soft_shadow_capable")) {
			SetPresetValue("gl_shadows",Cvar_VariableValue("bdgn_gl_volume_shadow_capable")?"4":"3");
			SetPresetValue("gl_sun","1");
		}
		if (Cvar_VariableValue("bdgn_gl_godrays_capable"))
			SetPresetValue("gl_godrays","1");
	}
	if (tier) {
		const gl_visual_settings_t *visual=&tier->visual;
		SetPresetValue("gl_texturemode","GL_LINEAR_MIPMAP_LINEAR");
		SetPresetValue("gl_anisotropy",va("%i",tier->anisotropy));
		SetPresetValue("gl_ext_samples",va("%i",visual->msaa));
		SetPresetValue("gl_fxaa",va("%i",visual->fxaa));
		SetPresetValue("gl_bloom",va("%i",visual->bloom));
		SetPresetValue("gl_ssao",va("%i",visual->ssao));
		SetPresetValue("gl_tonemap",va("%i",visual->tonemap));
		SetPresetValue("gl_colour_grade",va("%i",visual->grade));
		SetPresetValue("gl_render_scale",va("%i",visual->scale));
		SetPresetValue("gl_upscale_filter",va("%i",visual->filter));
		SetPresetValue("gl_render_scale_legacy","0");
		SetPresetValue("gl_render_scale_pixelcopy","0");
		SetPresetValue("gl_render_scale_copyformat","0");
		if (tier_id==83) {
			/* Keep native output and full scene detail even after a low-quality configuration. */
			SetPresetValue("gl_picmip","0");
			SetPresetValue("gl_playermip","0");
			SetPresetValue("gl_ext_palettedtexture","0");
			SetPresetValue("gl_fastwater","0");
			SetPresetValue("gl_fastsky","0");
			SetPresetValue("gl_dynamic","1");
			SetPresetValue("gl_dlight_maxdist","0");
			SetPresetValue("cl_notrails","0");
			SetPresetValue("cl_nosmoke","0");
			SetPresetValue("cl_nobanners","0");
			SetPresetValue("cl_nodecorpses","0");
			SetPresetValue("cl_nospawnpads","0");
		}
		if (tier->pixel) {	/* BDGN 3.55i: High Fidelity steps */
			int k;
			SetPresetValue("gl_pixel_lighting",va("%i",tier->pixel));
			SetPresetValue("gl_pixel_bump","1");
			SetPresetValue("gl_pixel_models","1");
			if (Cvar_VariableValue("bdgn_gl_soft_shadow_capable"))
				SetPresetValue("gl_shadows",Cvar_VariableValue("bdgn_gl_volume_shadow_capable")?"4":"3");	/* BDGN 3.55n: High Fidelity: shadows from the lights (only these presets turn shadows on); 3.55ai: shadow volumes when the window has a stencil buffer */
			if (Cvar_VariableValue("bdgn_gl_water_surface_capable") && tier->pixel>=2)
			{
				SetPresetValue("gl_water_surface","1");
				SetPresetValue("gl_water_interaction",tier->pixel>=4?"1":"0");
			}
			if (Cvar_VariableValue("bdgn_gl_water_reflect_capable"))
				SetPresetValue("gl_water_reflect",tier->pixel>=4?"2":"1");	/* BDGN 3.55o: HF1/HF2 half size, HF3 full size */
			if (Cvar_VariableValue("bdgn_gl_hdtextures_capable"))
				for (k=0;k<4;k++)
					if (tier->hdset[k] && FS_HDTexSetAvailable(tier->hdset[k]) &&
				    (tier->hdset[k]!=5 || Cvar_VariableValue("bdgn_gl_dds_capable"))) {
						SetPresetValue("gl_hdtextures",va("%i",tier->hdset[k]));
						if (tier->hdset[k]==5) SetPresetValue("gl_hd_maxsize","4096");	/* BDGN 3.55w: full 4K */
						break;
					}
			if (tier->sound44 && FS_HDSoundPackAvailable()) {
				SetPresetValue("s_khz","44");
				SetPresetValue("s_loadas8bit","0");
			}
		}
	}
	if (tier_id==83 && !Cvar_VariableValue("bdgn_legacy_graphics") && !Cvar_VariableValue("bdgn_lowram")) {
  SetPresetValue("gl_materials",Cvar_VariableValue("bdgn_gl_materials_capable")?"1":"0");
  SetPresetValue("gl_soft_particles",Cvar_VariableValue("bdgn_gl_soft_particles_capable")?"1":"0");
  SetPresetValue("gl_smoke_lighting",Cvar_VariableValue("bdgn_gl_soft_particles_capable")?"1":"0");
  SetPresetValue("gl_ssr",Cvar_VariableValue("bdgn_gl_environment_capable")?"2":"0");
  SetPresetValue("gl_ssgi",Cvar_VariableValue("bdgn_gl_environment_capable")?"2":"0");
  SetPresetValue("gl_volume_fog",Cvar_VariableValue("bdgn_gl_environment_capable")?"2":"0");
  SetPresetValue("gl_caustics",Cvar_VariableValue("bdgn_gl_environment_capable")?"1":"0");
 }
	if (tier_id==83 && !Cvar_VariableValue("bdgn_legacy_graphics") && !Cvar_VariableValue("bdgn_lowram")) {
		/* BDGN 3.56v: the best preset, Ultra High Fidelity - 4K, is the owner's own config.cfg of 2026-11-18 ("the
		   best looking of them all") with 44 kHz sound, at the desktop resolution. The picture steps that need the
		   driver stay behind their capability checks above (shadows, sun, god rays, water, relief, MSAA, 4K). */
		int k, w=0, h=0;
		SetPresetValue("gl_relief_depth","4");
		SetPresetValue("gl_scene_gamma","0.7");
		SetPresetValue("intensity","2");
		SetPresetValue("gl_modulate","1");
		SetPresetValue("gl_anisotropy","16");
		SetPresetValue("gl_texturemode","GL_LINEAR_MIPMAP_LINEAR");
		/* BDGN 3.56w: HD sprite effects on, at 4K when that pack is there (the owner's config now has them too): the hits,
		   sparks and railgun effects that the 3D effects do not replace */
		SetPresetValue("cl_hdfx",va("%i",CL_FXLevel_Value(4,"cl_hdfx")));	/* HD 4K, HD, or off where the driver cannot */
		/* BDGN 3.56w: "as good looking as possible": the AI (Stable Diffusion) 4K effect textures when they are installed,
		   the owner's favourite set; else standard 4K */
		if (CL_FX4K_Allowed("cl_fx3d_tex",3))
			SetPresetValue("cl_fx3d_tex","3");
		else if (CL_FX4K_Allowed("cl_fx3d_tex",2))
			SetPresetValue("cl_fx3d_tex","2");
		if (CL_FX4K_Allowed("cl_decals",2))
			SetPresetValue("cl_decals","2");
		SetPresetValue("cl_shells",CL_Shells_Available()?"1":"0");
		SetPresetValue("cl_cells",CL_Cells_Available()?"1":"0");
		SetPresetValue("cl_explosion_debris","24");
		SetPresetValue("cl_weaponlights","1");
		SetPresetValue("s_maxchannels","32");
		SetPresetValue("s_mixahead","0.2");
		if (FS_HDSoundPackAvailable()) {
			SetPresetValue("s_khz","44");
			SetPresetValue("s_loadas8bit","0");
		}
		/* the desktop resolution, full screen: the mode of exactly that size, with its aspect ratio in the list */
		VID_DesktopSize(&w,&h);
		for (k=0;preset_archive && !Q_stricmp(vid_ref->string,"gl") && k<NUM_GL_MODES;k++)	/* not while the benchmark or Auto tries presets */
			if (res_wh[k][0]==w && res_wh[k][1]==h) {
				int a=w*3==h*4?1:(w*4==h*5?2:(w*10==h*16?3:(w*21>h*40?5:4)));
				SetPresetValue("gl_mode",va("%i",k));
				SetPresetValue("vid_fullscreen","1");
				SetPresetValue("vid_aspect",va("%i",a));
				break;
			}
	}
	if ((id==65 || id==66 || id==57) && PresetUsesSmpWicked())
		Com_Printf("SMP WickedGL: PSYCHO Fast uses Ultra Fast settings.\n");
	if (k6gf) {	/* BDGN 3.58d: K6 + GeForce2/3 */
		SetPresetValue("gl_ext_multitexture",k6gf>=84 && k6gf<=87?"1":"0");	/* 3.58j: on in fast to Ultra Fast (MEASURED +5.1 %);
			3.58u: off in PSYCHO (MEASURED -4.2 %: single-colour walls have no texture to combine); 3.59g (owner
			2026-11-21): also K6 + GeForce default (MEASURED +1.0 % on default, v3.58j; the same picture). The general
			default stays id's 3.21. */
		if (k6gf>=86) SetPresetValue("gl_dynamic","0");
		/* cfg_preset keeps the general name (fast, ultra ...): the restart logic of client/cl_main.c reads it */
	}
	if (id==72) VID_ApplyExtremeConfig();	/* owner config; also when archive=false in benchmarks */
	VID_ApplyLegacyFidelity();
	BDGN_SyncEnemyLook();
	CL_CheckFullbright();
	return true;
}

static void ApplyConfigPreset(int id)
{
	if (id==VID_AUTO_PRESET) {	/* BDGN 3.55o: "Auto": the client tries the best looking presets and keeps the first with 125+ fps */
		Cbuf_AddText("bdgn_autopreset\n");
		return;
	}
	if (VID_ApplyPresetSettings(id,true)) {
		CL_ModelLodSync();	/* BDGN 3.57t: cl_modellod sets its rows before the renderer restarts */
		Cbuf_AddText("cpu_apply\nvid_restart\nwait\nsnd_restart\n");
	}
}

qboolean VID_ExecPreset(const char *file)
{
	preset_file_t preset;
	int id;
	if (!Q_strncasecmp((char *)file,"configs/",8)) file+=8;
	for(id=1;id<=66;id++) {
		if (PresetLookup(id,&preset) && !Q_stricmp(file,preset.file)) {
			ApplyConfigPreset(id);
			return true;
		}
	}
	if (!Q_stricmp(file,"softwarepsycho.cfg")) {
		if (PresetIsSoftware()) ApplyConfigPreset(65);
		else Com_Printf("Select a software renderer before applying this preset.\n");
		return true;
	}
	for(id=1;id<=5;id++) {
		if (software_presets[id] && !Q_stricmp(file,software_presets[id])) {
			if (PresetIsSoftware()) ApplyConfigPreset(id);
			else Com_Printf("Select a software renderer before applying this preset.\n");
			return true;
		}
	}
	return false;
}

static void (*preset_save_cvar)(const char *);
static void SavePresetCvar(const char *name,const char *value)
{
	if (PresetControls(name,0)) {
		Cvar_Get((char *)name,(char *)value,0);
		preset_save_cvar(name);
	}
}

void VID_SaveBenchmarkPresetCvars(void (*save)(const char *name))
{
	preset_file_t preset;
	char *buf;
	int id,i,len;
	preset_save_cvar=save;
	save("sw_render_scale");
	save("gl_render_scale");
 for(i=0;legacy_fidelity_defaults[i][0];i++)
  save(legacy_fidelity_defaults[i][0]);
 save("v1k_mode");
 save("v1k_antialias");
 save("gl_mode");
	for(i=0;preset_stock_defaults[i][0];i++)
		SavePresetCvar(preset_stock_defaults[i][0],preset_stock_defaults[i][1]);
	for(i=0;preset_extra_defaults[i][0];i++)
		SavePresetCvar(preset_extra_defaults[i][0],preset_extra_defaults[i][1]);
	for(id=1;id<=65;id++) {
		if (id>5 && id!=65) continue;
		if (!PresetLookup(id,&preset)) continue;
		len=LoadSelectedPreset(id,&preset,&buf);
		if (len) {
			CL_ParseSetLines(buf,len,SavePresetCvar);
			FS_FreeFile(buf);
		}
	}
	preset_save_cvar=NULL;
}

/* BDGN 3.51bdt: the low RAM check at start applies the low RAM default preset once */
qboolean CL_S3VirgeChosen (void);
/*
================
BDGN 3.55o: automatic preset

VID_AutoPresetChain lists the presets of the loaded renderer and driver from the best looking to the fastest:
High Fidelity 3, 2, 1, Ultra Fidelity SSAO, Bloom, MSAA, Adaptive, MAX Fidelity, then default, fast, faster, Ultra Fast,
PSYCHO Fast (the card family's own presets when there are any, else the general ones). Only presets the driver
offers are in the menu list, so the chain never holds a feature the driver lacks. The client (cl_main.c
CL_AutoPresetFrame) applies them in this order and keeps the first that holds 125 fps.
================
*/
static int VID_AutoRank (int id,const char *label)
{
	switch (id) {
	case 83: return 0;	/* BDGN 3.55w: Ultra High Fidelity - 4K */
	case 82: return 1;	case 81: return 2;	case 80: return 3;
	case 69: return 4;	case 68: return 5;	case 67: return 6;	case 70: return 7;
	case 5: return 8;
	case 0: case VID_AUTO_PRESET: case 10: case 11: case 12: case 13: case 66: case 71: case 72: return -1;
	}
	if (!label) return -1;
	if (strstr(label,"PSYCHO")) return 60;
	if (strstr(label,"Ultra Fast")) return 50;
	if (strstr(label,"aster")) return 40;
	if (strstr(label,"ast")) return 30;
	if (strstr(label,"default")) return 20;
	return -1;
}

int VID_AutoPresetChain (int *out,int max)
{
	int n=0,rank,i,best;
	for (rank=0;rank<=60;rank++) {
		best=-1;
		for (i=0;i<s_catalog_count;i++)	/* 3.57h: the presets with their own labels, not the category entries */
			if (VID_AutoRank(s_all_ids[i],s_all_list[i])==rank) {
				int generic=s_all_ids[i]<=5 || s_all_ids[i]==65;
				if (best<0 || (!generic && (s_all_ids[best]<=5 || s_all_ids[best]==65))) best=i;
			}
		if (best>=0 && n<max) out[n++]=s_all_ids[best];
	}
	return n;
}

const char *VID_PresetLabel (int id)
{
	int i;
	for (i=0;i<s_all_count;i++)	/* 3.57h: first entry = the preset's own name */
		if (s_all_ids[i]==id) return s_all_list[i];
	return "?";
}

/* BDGN 3.55q: for the hardware-based automatic preset */
qboolean VID_PresetOffered (int id)
{
	int i;
	for (i=0;i<s_all_count;i++)	/* 3.57h */
		if (s_all_ids[i]==id) return true;
	return false;
}

int VID_AutoDefaultPreset (void)	/* the card family's default preset, else "default config" */
{
	int chain[32],n=VID_AutoPresetChain(chain,32),i;
	for (i=0;i<n;i++)
		if (VID_AutoRank(chain[i],VID_PresetLabel(chain[i]))==20) return chain[i];
	return 4;
}

void VID_ApplyAutoPreset (int id)
{
	ApplyConfigPreset(id);
}

void VID_ApplyLowRamPreset (void)
{
	if ( !gl_driver )
		VID_MenuInit();		/* normally done when the renderer starts */
	if (Cvar_VariableValue("bdgn_extra_lowram") && CL_S3VirgeChosen())
		ApplyConfigPreset(7);	/* BDGN 3.55j: below 32 MB with the S3 Virge renderer: S3 Virge Ultra Fast, renderer kept */
	else
		ApplyConfigPreset(Cvar_VariableValue("bdgn_extra_lowram")?71:10);
}

void VID_DefaultConfig_f (void)
{
 ApplyConfigPreset(4);
}


/* BDGN 3.52t: the vid_ref / gl_driver / gl_bitdepth / mt_gl.ini of one driver list entry (ApplyChanges and bdgn_bench_all) */
const char *VID_MtGlTargetForRef (int ref)
{
	switch (ref)
	{
	case REF_MT_OPENGL:
	case REF_MT_3DNOW:
	case REF_MT_RENDITION: return "opengl32.dll";
	case REF_MT_3DFX: return "3dfxgl.dll";
	case REF_MT_3DFX_GLIDE: return FX_GLIDE_DRIVER ".dll";
	case REF_MT_WICKED: return WICKED_DRIVER ".dll";
	case REF_MT_3DNOW_3DFX: return CPUHas3DNow() ? "3dfxglam.dll" : "3dfxgl.dll";
	}
	return "";
}

static void VID_SetRefCvars( int ref )
{
	switch ( ref )
	{
	case REF_SWAM:
        if(CPUHas3DNow()) Cvar_Set("vid_ref","swam");
        else {
         Com_Printf("3dnow software needs a CPU with 3DNow!, using software instead\n");
         Cvar_Set("vid_ref","soft");
        }
        break;
    case REF_SOFT:
		Cvar_Set( "vid_ref", "soft" );
		break;
	case REF_OPENGL:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "opengl32" );
		break;
	case REF_3DFX:	/* 3dfx miniGL 1.47 (3dfxgl.dll, shipped in Quake2/) */
		if ( !VID_DriverDllFound( "3dfxgl" ) )
			Com_Printf( "3dfx miniGL: 3dfxgl.dll not found (put it in the Quake 2 folder)\n" );
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "3dfxgl" );
		Cvar_SetValue( "gl_bitdepth", 16 );	/* BDGN 3.52i: Voodoo1-3 render 16-bit only */
		break;
	case REF_3DFX_GLIDE:	/* BDGN 3.52i: 3dfx OpenGL on Glide (3dfxvgl.dll); without it the miniGL is used */
		Cvar_Set( "vid_ref", "gl" );
		if ( VID_DriverDllFound( FX_GLIDE_DRIVER ) )
			Cvar_Set( "gl_driver", FX_GLIDE_DRIVER );
		else
		{
			Com_Printf( "3dfx Glide GL: 3dfxvgl.dll not found (Quake 2 folder or Windows system folder), using the 3dfx miniGL\n" );
			Cvar_Set( "gl_driver", "3dfxgl" );
		}
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
	case REF_POWERVR:
		if ( !VID_DriverDllFound( "pvrgl" ) )
			Com_Printf( "PowerVR OpenGL: pvrgl.dll not found (put the PowerVR Quake II driver in the Quake 2 folder)\n" );
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "pvrgl" );
		Cvar_SetValue( "gl_bitdepth", 16 );	/* BDGN 3.52k: PCX1/PCX2 render 16-bit */
		break;
	case REF_WICKED:	/* BDGN 3.52k: Metabyte WickedGL (Voodoo2/3/5) */
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", WICKED_DRIVER );
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
	case REF_3DNOW_3DFX:	/* BDGN 3.52k: AMD's ref_glam.dll on 3dfx's Quake II OpenGL (3dfxglam.dll) */
		if ( CPUHas3DNow() )
			Cvar_Set( "vid_ref", "glam" );
		else
		{
			Com_Printf( "3DNow! OpenGL needs a CPU with 3DNow!, using the 3dfx miniGL instead\n" );
			Cvar_Set( "vid_ref", "gl" );
		}
		Cvar_Set( "gl_driver", CPUHas3DNow() ? FXGLAM_DRIVER : "3dfxgl" );
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
	// SMP support
	case REF_MT_OPENGL:
	case REF_MT_RENDITION:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "mtgl32" );
		break;
	case REF_MT_3DFX:
	case REF_MT_3DFX_GLIDE:
	case REF_MT_WICKED:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "mtgl32" );
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
	case REF_MT_3DNOW_3DFX:	/* BDGN 3.52k: ref_glam + SMP wrapper on 3dfxglam.dll */
		Cvar_Set( "vid_ref", CPUHas3DNow() ? "glam" : "gl" );
		Cvar_Set( "gl_driver", "mtgl32" );
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
/*
	case REF_MT_3DFX:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "mt3dfx" );
		break;
	// end
*/
	case REF_VERITE:
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "veritegl" );
	break;
	case REF_RENDITION_V1K:	/* BDGN 3.51ast: Rendition's ref_v1k.dll (falls back to software when it is missing) */
		Cvar_Set( "vid_ref", "v1k" );
		break;
	case REF_RENDITION_GL:	/* BDGN 3.51ast: ref_gl on the Rendition V2x00 OpenGL ICD (the system OpenGL driver) */
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", "opengl32" );
		break;
	case REF_S3VIRGE:	/* BDGN 3.51ak */
		Cvar_Set( "vid_ref", "gl" );
		Cvar_Set( "gl_driver", S3V_DRIVER );
		Cvar_SetValue( "gl_bitdepth", 16 );	/* the driver runs 15/16-bit modes only */
		break;
	case REF_3DNOW_S3VIRGE:	/* BDGN 3.51ak: AMD's 3DNow! renderer on the S3 Virge driver */
		if ( CPUHas3DNow() )
			Cvar_Set( "vid_ref", "glam" );
		else
		{
			Com_Printf( "3DNow! OpenGL needs a CPU with 3DNow!, using S3 Virge OpenGL instead\n" );
			Cvar_Set( "vid_ref", "gl" );
		}
		Cvar_Set( "gl_driver", S3V_DRIVER );
		Cvar_SetValue( "gl_bitdepth", 16 );
		break;
	case REF_3DNOW:
		if ( CPUHas3DNow() )
		{
			Cvar_Set( "vid_ref", "glam" );
		}
		else
		{
			Com_Printf( "3DNow! OpenGL needs a CPU with 3DNow!, using default OpenGL instead\n" );
			Cvar_Set( "vid_ref", "gl" );
		}
		Cvar_Set( "gl_driver", "opengl32" );
	break;
	case REF_MT_3DNOW:
		// ref_glam.dll loads whatever gl_driver names, so this is 3DNow! renderer + SMP driver
		if ( CPUHas3DNow() )
		{
			Cvar_Set( "vid_ref", "glam" );
		}
		else
		{
			Com_Printf( "3DNow! OpenGL needs a CPU with 3DNow!, using SMP OpenGL instead\n" );
			Cvar_Set( "vid_ref", "gl" );
		}
		Cvar_Set( "gl_driver", "mtgl32" );
	break;
	}
}

/* BDGN 3.60c: the driver (REF_*) and menu (software / OpenGL) of the cvars vid_ref, gl_driver and mt_gl.ini; was inline
   in the video menu's init, now also used by the benchmark "Quick: my presets" (current renderer only) */
static int VID_RefFromCvars (int *menu_index)
{
	int ref = REF_SOFT, idx = SOFTWARE_MENU;
	if ( !vid_ref ) vid_ref = Cvar_Get( "vid_ref", "soft", CVAR_ARCHIVE );
	if ( !gl_driver ) gl_driver = Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
	if ( strcmp( vid_ref->string, "soft" ) == 0 || strcmp(vid_ref->string,"swam")==0 )
	{
		idx = SOFTWARE_MENU;
		ref = !strcmp(vid_ref->string,"swam")?REF_SWAM:REF_SOFT;
	}
	else if ( strcmp( vid_ref->string, "glam" ) == 0 )
	{
		idx = OPENGL_MENU;
		if ( strcmp( gl_driver->string, "mtgl32" ) == 0 )
		{	/* BDGN 3.52k: mt_gl.ini says which driver the SMP wrapper runs */
			char mt[64];
			VID_GetMtGlTarget( mt, sizeof( mt ) );
			ref = !Q_stricmp( mt, "3dfxglam.dll" ) ? REF_MT_3DNOW_3DFX : REF_MT_3DNOW;
		}
		else if ( stricmp( gl_driver->string, FXGLAM_DRIVER ) == 0 )
			ref = REF_3DNOW_3DFX;	/* BDGN 3.52k */
		else if ( stricmp( gl_driver->string, S3V_DRIVER ) == 0 )
			ref = REF_3DNOW_S3VIRGE;
		else
			ref = REF_3DNOW;
	}
	else if ( strcmp( vid_ref->string, "v1k" ) == 0 )
	{	/* BDGN 3.51ast */
		idx = OPENGL_MENU;
		ref = REF_RENDITION_V1K;
	}
	else if ( strcmp( vid_ref->string, "gl" ) == 0 && strcmp( gl_driver->string, "opengl32" ) == 0 && Cvar_Get( "vid_rendition", "0", CVAR_ARCHIVE )->value )
	{	/* BDGN 3.51ast */
		idx = OPENGL_MENU;
		ref = REF_RENDITION_GL;
	}
	else if ( strcmp( vid_ref->string, "gl" ) == 0 )
	{
		idx = OPENGL_MENU;
		if ( strcmp( gl_driver->string, "3dfxgl" ) == 0 )
			ref = REF_3DFX;
		else if ( stricmp( gl_driver->string, FX_GLIDE_DRIVER ) == 0 )
			ref = REF_3DFX_GLIDE;	/* BDGN 3.52i */
		else if ( stricmp( gl_driver->string, WICKED_DRIVER ) == 0 )
			ref = REF_WICKED;	/* BDGN 3.52k */
		else if ( stricmp( gl_driver->string, S3V_DRIVER ) == 0 )
			ref = REF_S3VIRGE;
		else if ( strcmp( gl_driver->string, "pvrgl" ) == 0 )
			ref = REF_POWERVR;
		else if ( strcmp( gl_driver->string, "opengl32" ) == 0 )
			ref = REF_OPENGL;
		// SMP support
		/*
		else if ( strcmp( gl_driver->string, "mt3dfx" ) == 0 )
			ref = REF_MT_3DFX;
		*/
		else if ( strcmp( gl_driver->string, "mtgl32" ) == 0 )
		{	/* BDGN 3.52k */
			char mt[64];
			VID_GetMtGlTarget( mt, sizeof( mt ) );
			if ( !Q_stricmp( mt, "3dfxgl.dll" ) )
				ref = REF_MT_3DFX;
			else if ( !Q_stricmp( mt, FX_GLIDE_DRIVER ".dll" ) )
				ref = REF_MT_3DFX_GLIDE;
			else if (BDGN_GLDriverIs(mt,WICKED_DRIVER))
				ref = REF_MT_WICKED;
			else if (BDGN_GLDriverIs(mt,"opengl32") && Cvar_VariableValue("vid_rendition"))
				ref = REF_MT_RENDITION;
			else
				ref = REF_MT_OPENGL;
		}
		//
		else
//			ref = REF_VERITE;
			ref = REF_OPENGL;
	}

	if ( menu_index ) *menu_index = idx;
	return ref;
}
/* BDGN 3.52t, 3.60c: the renderers the benchmark runs; since 3.60c only "Quick: my presets": the driver in use */
int VID_BenchRefList( int *out, int max, char *skipped, int skipsize, int mode, const char *driver )
{
	if ( skipped && skipsize ) skipped[0] = 0;
	if ( max < 1 ) return 0;
	out[0] = VID_RefFromCvars( NULL );
	return 1;
}
const char *VID_BenchRefName( int ref )
{
	static const char *names[REF_COUNT] = { "software", "default OpenGL", "3Dfx miniGL", "PowerVR OpenGL", "SMP OpenGL",
		"3DNow! OpenGL", "3DNow! SMP OpenGL", "3DNow! software", "S3 Virge OpenGL", "3DNow! S3 Virge", "Rendition V1000",
		"Rendition OpenGL", "3Dfx Glide GL", "WickedGL Voodoo2", "3DNow! 3Dfx",
		"SMP 3Dfx miniGL", "3DNow! SMP 3Dfx", "SMP 3Dfx Glide GL", "SMP WickedGL", "SMP Rendition OpenGL" };
	if ( ref == -1 ) return "software SMP";
	return ref >= 0 && ref < REF_COUNT ? names[ref] : "?";
}
qboolean VID_BenchSelectRef( int ref )
{
	const char *target=VID_MtGlTargetForRef(ref);
	if(target[0] && !VID_SetMtGlTarget(target)) return false;
	Cvar_Set( "vid_rendition", (ref == REF_RENDITION_GL || ref==REF_MT_RENDITION) ? "1" : "0" );
	VID_SetRefCvars( ref < 0 ? REF_SOFT : ref );
	return true;
}

#define VID_GL_SHOWN(item) ((item).generic.parent==&s_opengl_menu || (item).generic.parent==&s_pic_menu[OPENGL_MENU] || (item).generic.parent==&s_perf_menu[OPENGL_MENU])
static void ApplyChanges( void *unused )
{
	float gamma;
	int noimages_changed;
	int lowtex_changed;
	int was_s3v = stricmp( gl_driver->string, S3V_DRIVER ) == 0;	/* BDGN 3.51al */
	int was_rend = !strcmp( vid_ref->string, "v1k" ) || ( !strcmp( vid_ref->string, "gl" ) && Cvar_VariableValue( "vid_rendition" ) );	/* BDGN 3.51ast */
	int v1k_changed = 0;
	int preset;
	int selected_ref=s_ref_list[s_current_menu_index].curvalue;
	float previous_gl_scale=Cvar_VariableValue("gl_render_scale");
	const char *target;

	{	/* BDGN 3.57h (3.57j: after the declarations, C89): a preset of the Software or the 3dfx category switches the renderer first */
		int pick=s_preset_ids[s_preset_box.curvalue<0 || s_preset_box.curvalue>=80 ? 0 : s_preset_box.curvalue];
		if (pick>=PRESET_FX_BASE)
			s_ref_list[s_current_menu_index].curvalue=selected_ref=REF_3DFX;
		else if (pick>=PRESET_SW_BASE)
			s_ref_list[s_current_menu_index].curvalue=selected_ref=REF_SOFT;
	}
	target=VID_MtGlTargetForRef(selected_ref);

	if(target[0] && !VID_SetMtGlTarget(target))
	{
		s_opengl_menu.statusbar="Could not update mt_gl.ini. See console.";
		return;
	}

	/*
	** make values consistent
	*/
	s_fs_box[!s_current_menu_index].curvalue = s_fs_box[s_current_menu_index].curvalue;
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( s_brightness_slider[s_current_menu_index].curvalue/10.0 - 0.5 ) ) + 0.5;

	Cvar_SetValue(VID_SceneBrightness(s_current_menu_index)?"gl_scene_gamma":"vid_gamma",gamma);
	Cvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );
	if ( s_current_menu_index == SOFTWARE_MENU && !( s_render_scale_box[SOFTWARE_MENU].generic.flags & QMF_GRAYED ) )
		Cvar_SetValue( "sw_render_scale", RenderScalePercent((int)s_render_scale_box[SOFTWARE_MENU].curvalue) );
	if ( s_current_menu_index == OPENGL_MENU && VID_GL_SHOWN(s_render_scale_box[OPENGL_MENU]) ) {
		Cvar_SetValue( "gl_render_scale", RenderScalePercent((int)s_render_scale_box[OPENGL_MENU].curvalue) );
		Cvar_SetValue( "gl_upscale_filter", s_upscale_filter.curvalue );
	}
	if (s_current_menu_index==OPENGL_MENU) {
		int i;
		for (i=0;i<(int)GL_POST_MENU_COUNT;i++)
			if (VID_GL_SHOWN(s_gl_post_boxes[i]))
				Cvar_SetValue((char *)gl_post_cvars[i],s_gl_post_boxes[i].curvalue);
	}
	if ( s_current_menu_index == SOFTWARE_MENU && !( s_texfilter_box.generic.flags & QMF_GRAYED ) )
		Cvar_SetValue( "sw_texfilter", s_texfilter_box.curvalue );	/* BDGN 3.52n: takes effect at once */
	if ( s_current_menu_index == SOFTWARE_MENU && !( s_swcolor_box.generic.flags & QMF_GRAYED ) )
		Cvar_SetValue( "sw_coloredlight", s_swcolor_box.curvalue );	/* BDGN 3.52p: the surface cache is rebuilt by itself */
	if ( s_current_menu_index == SOFTWARE_MENU && !( s_swdrawdist_slider.generic.flags & QMF_GRAYED ) )
	{	/* BDGN 3.52r: slider right = unlimited = sw_drawdist 0, like the OpenGL draw distance */
		Cvar_SetValue( "sw_drawdist", 10 - s_swdrawdist_slider.curvalue );
		Cvar_SetValue( "sw_prefetch", s_swprefetch_box.curvalue );
	}
	// fov
	Cvar_SetValue( "fov", s_fov_slider.curvalue );
	Cvar_SetValue( "dfov", s_fov_slider.curvalue );
	Cvar_SetValue( "zfov", s_fov_slider.curvalue / 2.4 );
	// end
	Cvar_SetValue( "gl_picmip", 3 - s_tq_slider.curvalue );
	if ((!(s_lowtex_box.generic.flags & QMF_GRAYED) && s_lowtex_box.curvalue) ||
	    (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_hdtexture_box))) {
		int hd=!(s_lowtex_box.generic.flags & QMF_GRAYED) && s_lowtex_box.curvalue ? 0 :
			(s_hdtexture_box.curvalue>0 && s_hdtexture_box.curvalue<7 && hd_set_names[s_hdtexture_box.curvalue] ? hd_set_values[s_hdtexture_box.curvalue] : 0);	/* BDGN 3.55i: texture set */
		if (hd!=(int)Cvar_VariableValue("gl_hdtextures")) {
			Cvar_SetValue("gl_hdtextures",hd);
			vid_ref->modified=true;
		}
	}
	if (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_hd4k_size_box) &&
	    hd4k_size_values[s_hd4k_size_box.curvalue]!=(int)Cvar_VariableValue("gl_hd_maxsize")) {	/* BDGN 3.55w */
		Cvar_SetValue("gl_hd_maxsize",hd4k_size_values[s_hd4k_size_box.curvalue]);
		vid_ref->modified=true;	/* the textures are loaded again at the new size */
	}
	if (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_gl_filter_box))
		Cvar_Set("gl_texturemode",(char *)gl_filter_modes[s_gl_filter_box.curvalue]);
	if (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_gl_anisotropy_box))
		Cvar_SetValue("gl_anisotropy",gl_anisotropy_levels[s_gl_anisotropy_box.curvalue]);
	if (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_gl_msaa_box) &&
	    s_gl_msaa_box.curvalue!=gl_msaa_at_open) {
		int samples=gl_msaa_choices[s_gl_msaa_box.curvalue];
		if (samples!=Cvar_VariableValue("gl_ext_samples")) {
			Cvar_SetValue("gl_ext_samples",samples);
			vid_ref->modified=true;
		}
	}
	Cvar_SetValue( "vid_fullscreen", s_fs_box[s_current_menu_index].curvalue );
	Cvar_SetValue( "gl_ext_palettedtexture", s_paletted_texture_box.curvalue );
	// disable textures
	noimages_changed = ( (int)gl_hack_noimages->value != 0 );	// BDGN 3.47: "no texture images" has no row any more; a saved 1 is cleared
	Cvar_SetValue( "gl_hack_noimages", 0 );
	lowtex_changed = !( s_lowtex_box.generic.flags & QMF_GRAYED ) && ( (int)Cvar_VariableValue( "fs_lowtextures" ) != s_lowtex_box.curvalue );
	if ( VID_S3VirgeSelected () )
	{	/* BDGN 3.51ak: S3 Virge driver options; vid_dll.c writes OpenGL32.INI when the renderer (re)starts */
		int s3v_changed = (int)Cvar_VariableValue( "s3v_dither" ) != s_s3v_dither_box.curvalue ||
			(int)Cvar_VariableValue( "s3v_squaredown" ) != s_s3v_square_box.curvalue ||
			(int)Cvar_VariableValue( "s3v_alpha" ) != s3v_alpha_values[s_s3v_alpha_box.curvalue] ||
			(int)Cvar_VariableValue( "s3v_fastmath" ) != s_s3v_math_box.curvalue;
		Cvar_SetValue( "s3v_dither", s_s3v_dither_box.curvalue );
		Cvar_SetValue( "s3v_squaredown", s_s3v_square_box.curvalue );
		Cvar_SetValue( "s3v_alpha", s3v_alpha_values[s_s3v_alpha_box.curvalue] );
		Cvar_SetValue( "s3v_fastmath", s_s3v_math_box.curvalue );
		if ( s3v_changed )
			lowtex_changed = 1;	/* restart the renderer so the driver reads the new options */
	}
	if ( VID_3dfxSelected () )
	{	/* BDGN 3.52l: 3dfx driver options; a change restarts the renderer so the driver reads them */
		int fx_changed = (int)Cvar_VariableValue( "fx_triplebuffer" ) != s_fx_tb_box.curvalue ||
			(int)Cvar_VariableValue( "fx_refresh" ) != fx_hz_values[s_fx_hz_box.curvalue] ||
			(int)Cvar_VariableValue( "fx_lod_dither" ) != s_fx_dither_box.curvalue ||
			(int)Cvar_VariableValue( "fx_fastmem" ) != s_fx_fast_box.curvalue ||
			(int)Cvar_VariableValue( "fx_videofilter" ) != s_fx_filter_box.curvalue ||
			(int)Cvar_VariableValue( "fx_cpupath" ) != s_fx_cpu_box.curvalue;
		Cvar_SetValue( "fx_triplebuffer", s_fx_tb_box.curvalue );
		Cvar_SetValue( "fx_refresh", fx_hz_values[s_fx_hz_box.curvalue] );
		Cvar_SetValue( "fx_lod_dither", s_fx_dither_box.curvalue );
		Cvar_SetValue( "fx_fastmem", s_fx_fast_box.curvalue );
		Cvar_SetValue( "fx_videofilter", s_fx_filter_box.curvalue );
		Cvar_SetValue( "fx_cpupath", s_fx_cpu_box.curvalue );
		if ( fx_changed )
			lowtex_changed = 1;	/* restart the renderer */
	}
	if ( !( s_lowtex_box.generic.flags & QMF_GRAYED ) )	/* 3.52c: greyed for renderers that can't use the pack */
		Cvar_SetValue( "fs_lowtextures", s_lowtex_box.curvalue );
	if ( s_ref_list[s_current_menu_index].curvalue == REF_3DNOW || s_ref_list[s_current_menu_index].curvalue == REF_MT_3DNOW || s_ref_list[s_current_menu_index].curvalue == REF_3DNOW_S3VIRGE || s_ref_list[s_current_menu_index].curvalue == REF_3DNOW_3DFX || s_ref_list[s_current_menu_index].curvalue == REF_MT_3DNOW_3DFX )
		Cvar_SetValue( "gl_hack_notextures", s_disable_textures_box.curvalue );
	else
	{	/* BDGN 3.47: single colour textures = r_drawflat; the old all-one-colour switch stays off with ref_gl */
		Cvar_SetValue( "gl_hack_notextures", 0 );
		if ( s_flat_list.curvalue != flat_at_open )
			Cvar_SetValue( "r_drawflat", flat_values[s_flat_list.curvalue] );
	}
	// BDGN 3.40: CPU optimizations.  auto = everything the CPU supports; manual = the three rows (a row of an
	// instruction set the CPU lacks is greyed out and always off).  AMD's ref_glam.dll has no such rows.
	if ( s_ref_list[s_current_menu_index].curvalue != REF_3DNOW && s_ref_list[s_current_menu_index].curvalue != REF_MT_3DNOW && s_ref_list[s_current_menu_index].curvalue != REF_3DNOW_S3VIRGE && s_ref_list[s_current_menu_index].curvalue != REF_3DNOW_3DFX && s_ref_list[s_current_menu_index].curvalue != REF_MT_3DNOW_3DFX )
	{
		/* BDGN 3.51bbt: 3DNow!/SSE/MMX are automatic; the rows only switch them off for a test (not saved) */
		if ( !( s_3dnow_box.generic.flags & QMF_GRAYED ) )
			Cvar_FullSet( "bdgn_no3dnow", s_3dnow_box.curvalue ? "1" : "0", 0 );
		if ( !( s_sse_box.generic.flags & QMF_GRAYED ) )
			Cvar_FullSet( "bdgn_nosse", s_sse_box.curvalue ? "1" : "0", 0 );
		if ( !( s_mmx_box.generic.flags & QMF_GRAYED ) )
			Cvar_FullSet( "bdgn_nommx", s_mmx_box.curvalue ? "1" : "0", 0 );
		if (s_current_menu_index==OPENGL_MENU && VID_GL_SHOWN(s_cpu_path_box) &&
		    s_cpu_path_box.curvalue!=Cvar_VariableValue("bdgn_cpu_path")) {
			Cvar_SetValue("bdgn_cpu_path",s_cpu_path_box.curvalue);
			if (s_cpu_path_box.curvalue) Cvar_SetValue("bdgn_nosse",0);
			vid_ref->modified=true;
		}
		CL_CPUApply();
	}
	Cvar_SetValue( "gl_drawdist", 10 - s_drawdist_slider.curvalue );	// slider right = maximum = cvar 0
	Cvar_SetValue( "gl_swapinterval", 0 );	/* BDGN 3.58o: vsync is always off */
	//
	if (s_current_menu_index == SOFTWARE_MENU)
	{
		Cvar_SetValue( "sw_mode", s_sw_res_map[s_mode_list[SOFTWARE_MENU].curvalue] );
		Cvar_SetValue("sw_aspect",s_sw_aspect_list.curvalue);
		if ( s_ref_list[SOFTWARE_MENU].curvalue != REF_SWAM ) {
			Cvar_SetValue( "sw_smp", s_smp_box.curvalue );
			if(Cvar_VariableValue("sw_smp_quality"))
				Cvar_SetValue("sw_smp_threads",s_sw_threads_box.curvalue?s_sw_threads_box.curvalue+1:0);
		}
	}
	else {
		Cvar_SetValue( "gl_mode", s_res_map[s_mode_list_new[OPENGL_MENU].curvalue] );
	}

	if ( s_current_menu_index == OPENGL_MENU && VID_V1KSelected () )
	{	/* BDGN 3.51ast: Rendition ref_v1k options; the renderer is restarted when one of them changes */
		int mode = (int)gl_mode->value;
		if ( mode < 0 || mode > V1K_MAX_MODE )
			mode = 2;
		v1k_changed = (int)Cvar_VariableValue( "v1k_mode" ) != mode ||
			(int)Cvar_VariableValue( "v1k_antialias" ) != v1k_aa_values[s_v1k_aa_box.curvalue] ||
			(int)Cvar_VariableValue( "v1k_waterwarp" ) != s_v1k_warp_box.curvalue ||
			(int)Cvar_VariableValue( "v1k_nopageflip" ) != s_v1k_flip_box.curvalue ||
			(int)Cvar_VariableValue( "v1k_surfacelookup" ) != !s_v1k_lookup_box.curvalue ||
			(int)Cvar_VariableValue( "v1k_refreshrate" ) != v1k_rate_values[s_v1k_rate_box.curvalue];
		Cvar_FullSet( "v1k_mode", va( "%i", mode ), CVAR_ARCHIVE );
		Cvar_FullSet( "v1k_antialias", va( "%i", v1k_aa_values[s_v1k_aa_box.curvalue] ), CVAR_ARCHIVE );
		Cvar_FullSet( "v1k_waterwarp", va( "%i", s_v1k_warp_box.curvalue ), CVAR_ARCHIVE );
		Cvar_FullSet( "v1k_nopageflip", va( "%i", s_v1k_flip_box.curvalue ), CVAR_ARCHIVE );
		Cvar_FullSet( "v1k_surfacelookup", va( "%i", !s_v1k_lookup_box.curvalue ), CVAR_ARCHIVE );
		Cvar_FullSet( "v1k_refreshrate", va( "%i", v1k_rate_values[s_v1k_rate_box.curvalue] ), CVAR_ARCHIVE );
	}
	Cvar_FullSet( "vid_rendition", (s_ref_list[s_current_menu_index].curvalue == REF_RENDITION_GL ||
		s_ref_list[s_current_menu_index].curvalue==REF_MT_RENDITION) ? "1" : "0", CVAR_ARCHIVE );
	//

	VID_SetRefCvars( s_ref_list[s_current_menu_index].curvalue );	/* BDGN 3.52t: shared with the all-renderers benchmark */

	/*
	** update appropriate stuff if we're running OpenGL and gamma
	** has been modified
	*/
	if ( stricmp( vid_ref->string, "gl" ) == 0 || stricmp( vid_ref->string, "glam" ) == 0 )
	{
		if ( vid_gamma->modified )
		{
			vid_ref->modified = true;
			if ( stricmp( gl_driver->string, "3dfxgl" ) == 0 || stricmp( gl_driver->string, FX_GLIDE_DRIVER ) == 0
				|| stricmp( gl_driver->string, WICKED_DRIVER ) == 0 || stricmp( gl_driver->string, FXGLAM_DRIVER ) == 0 )
			{
				char envbuffer[1024];
				float g;

				vid_ref->modified = true;

				g = 2.00 * ( 0.8 - ( vid_gamma->value - 0.5 ) ) + 1.0F;
				Com_sprintf( envbuffer, sizeof(envbuffer), "SSTV2_GAMMA=%f", g );
				putenv( envbuffer );
				Com_sprintf( envbuffer, sizeof(envbuffer), "SST_GAMMA=%f", g );
				putenv( envbuffer );

				vid_gamma->modified = false;
			}
		}

		if ( gl_driver->modified )
			vid_ref->modified = true;

		// the images are only (re)loaded when the renderer restarts
		if ( noimages_changed || lowtex_changed )
			vid_ref->modified = true;
	}

	if ( !strcmp( vid_ref->string, "v1k" ) && ( v1k_changed || lowtex_changed ) )
		vid_ref->modified = true;	/* BDGN 3.51ast: ref_v1k reads its options and the textures when it starts */

	preset = s_preset_ids[s_preset_box.curvalue < 0 || s_preset_box.curvalue >= (int)(sizeof(s_preset_ids)/sizeof(s_preset_ids[0])) ? 0 : s_preset_box.curvalue];	/* BDGN 3.51bdt: list entry -> preset */
	if ( preset >= PRESET_FX_BASE )
		preset -= PRESET_FX_BASE;	/* 3.57h: the renderer was switched above */
	else if ( preset >= PRESET_SW_BASE )
		preset -= PRESET_SW_BASE;
	if ( preset )
		ApplyConfigPreset( preset );
	else if ( !was_s3v && s_current_menu_index == OPENGL_MENU && VID_S3VirgeSelected () )
		ApplyConfigPreset( Cvar_VariableValue("bdgn_extra_lowram") ? 7 : 6 );	/* BDGN 3.51al: switching to S3 Virge applies the S3 Virge preset once; 3.55j: Ultra Fast below 32 MB */
	else if ( !was_rend && s_current_menu_index == OPENGL_MENU && VID_RenditionSelected () )
		ApplyConfigPreset( 8 );	/* BDGN 3.51ast: switching to Rendition applies the Rendition preset once */
	else if (selected_ref!=s_ref_inuse &&
	         !VID_S3VirgeSelected() && !VID_RenditionSelected())
		ApplyConfigPreset(Cvar_VariableValue("bdgn_extra_lowram") && selected_ref==REF_SOFT?71:Cvar_VariableValue("bdgn_lowram")?10:4);

	if (s_has_audio_workers && selected_ref!=REF_SOFT &&
	    (!preset && (selected_ref==s_ref_inuse || s_audio_worker_box.curvalue!=s_audio_worker_at_open)))
		Cvar_SetValue("s_gl_audio_worker",s_audio_worker_box.curvalue);

	if ((!strcmp(vid_ref->string,"soft") || !strcmp(vid_ref->string,"swam")) && lowtex_changed) vid_ref->modified=true;
	{
		int mode=s_current_menu_index==SOFTWARE_MENU ? s_sw_res_map[s_mode_list[SOFTWARE_MENU].curvalue] :
			s_res_map[s_mode_list_new[OPENGL_MENU].curvalue];
		int aspect=ResAspectClass(mode);
		if (aspect==3 || aspect==4) Cvar_SetValue("cl_fovaspect",1);
	}
	/* Recreate the GL context on scale changes, as on a successful fresh start. */
	if (!Q_stricmp(vid_ref->string,"gl") &&
	    previous_gl_scale!=Cvar_VariableValue("gl_render_scale"))
		vid_ref->modified=true;
	VID_RequestConfigSave ();
	CL_CheckFullbright ();
	M_ForceMenuOff();
}

static void CancelChanges( void *unused )
{
	extern void M_PopMenu( void );

	M_PopMenu();
}

/*
** VID_MenuInit
*/
static void VID_RenderScaleLabel(void *self)
{
	menuslider_s *slider=self;
	Menu_DrawString(slider->generic.parent->x+slider->generic.x+120,
		slider->generic.parent->y+slider->generic.y,
		va("%d%%",RenderScalePercent((int)slider->curvalue)));
}

void VID_MenuInit( void )
{

		//new resolutions for opengl only.
	static const char *resolutions_new[] = 
	{
		"[320 240  ]",
		"[400 300  ]",
		"[512 384  ]",
		"[640 480  ]",
		"[800 600  ]",
		"[960 720  ]",
		"[1024 768 ]",
		"[1152 864 ]",
		"[1280 960 ]",
		"[1280 1024]",	// I NEED THIS!
		"[1600 1200]",
		"[2048 1536]",
		// Add new video modes for those with modern machines and monitors
		//testing
		//16:9 and 16:10
		"[1280 720]",
		"[1280 800]",
		"[1366 768]",
		"[1440 900]",
		"[1600 900]",
		"[1680 1050]",
		"[1920 1080]",
		"[1920 1200]",
		"[2560 1440]",
		"[2560 1600]",
		"[3200 1800]",
		"[3840 2160]",
		"[3840 2400]",
		//21:9
		"[2560 1080]",
		"[3440 1440]",
		"[3840 1600]",
		//testing ends
		0
	};
	static const char *refs[] =
	{
		"[software       ]",
		"[default OpenGL ]",
		"[3Dfx miniGL    ]",
		"[PowerVR OpenGL ]",
		"[SMP OpenGL     ]",
		"[3DNow! OpenGL  ]",
		"[3DNow! SMP GL  ]",
        "3dnow software",
		"[S3 Virge OpenGL]",	/* BDGN 3.51ak: REF_S3VIRGE */
		"[3DNow! S3 Virge]",	/* BDGN 3.51ak: REF_3DNOW_S3VIRGE */
		"[Rendition V1000]",	/* BDGN 3.51ast: REF_RENDITION_V1K (ref_v1k.dll) */
		"[Rendition OpenGL]",	/* BDGN 3.51ast: REF_RENDITION_GL (V2x00 OpenGL ICD) */
		"[3Dfx Glide GL  ]",	/* BDGN 3.52i: REF_3DFX_GLIDE (3dfxvgl.dll) */
		"[WickedGL]",	/* BDGN 3.52k: REF_WICKED (Wicked3DFXgl.dll) */
		"[3DNow! 3Dfx    ]",	/* BDGN 3.52k: REF_3DNOW_3DFX (ref_glam + 3dfxglam.dll) */
		"[SMP 3Dfx miniGL]",
		"[3DNow! SMP 3Dfx]",	/* BDGN 3.52k: REF_MT_3DNOW_3DFX (ref_glam + mtgl32 -> 3dfxglam.dll) */
		"[SMP 3Dfx Glide GL]",
		"[SMP WickedGL]",
		"[SMP Rendition OpenGL]",
		0
	};
	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};
	static const char *stipple_names[] = { "no", "yes", "solid", 0 };	/* BDGN 3.57c: 2 = translucent surfaces drawn opaque */
	/* preset_names / preset_names_s3v: file scope (BDGN 3.51al) */
	static const char *onoff_names[] =
	{
		"off",
		"on",
		0
	};
	int i;

	CL_CheckFullbright ();

	if ( !gl_driver )
		gl_driver = Cvar_Get( "gl_driver", "opengl32", 0 );
	if ( !gl_picmip )
		gl_picmip = Cvar_Get( "gl_picmip", "0", 0 );
	if ( !gl_mode )
		gl_mode = Cvar_Get( "gl_mode", "3", 0 );
	if ( !sw_mode )
		sw_mode = Cvar_Get( "sw_mode", "0", 0 );
	if ( !gl_ext_palettedtexture )
		gl_ext_palettedtexture = Cvar_Get( "gl_ext_palettedtexture", "0", CVAR_ARCHIVE );
	if ( !gl_finish )
		gl_finish = Cvar_Get( "gl_finish", "0", CVAR_ARCHIVE );

	if ( !sw_stipplealpha )
		sw_stipplealpha = Cvar_Get( "sw_stipplealpha", "0", CVAR_ARCHIVE );

	//fov
	if ( !fov )
		fov = Cvar_Get( "fov", "0", CVAR_ARCHIVE );
		dfov = Cvar_Get( "dfov", "0", CVAR_ARCHIVE );
		zfov = Cvar_Get( "zfov", "0", CVAR_ARCHIVE );
	// testing
	if ( !gl_hack_notextures )
	gl_hack_notextures = Cvar_Get( "gl_hack_notextures", "0", CVAR_ARCHIVE  );
	if ( !gl_hack_noimages )
		gl_hack_noimages = Cvar_Get( "gl_hack_noimages", "0", CVAR_ARCHIVE );
	if ( !gl_3dnow )
		gl_3dnow = Cvar_Get( "gl_3dnow", "1", CVAR_ARCHIVE );
	if ( !gl_drawdist )
		gl_drawdist = Cvar_Get( "gl_drawdist", "0", CVAR_ARCHIVE );
	if ( !cl_fastfx )
		cl_fastfx = Cvar_Get( "cl_fastfx", "1", CVAR_ARCHIVE | CVAR_NOSET );
	if ( !cl_nosmoke )
		cl_nosmoke = Cvar_Get( "cl_nosmoke", "0", CVAR_ARCHIVE );
	if ( !gl_fastworld )
		gl_fastworld = Cvar_Get( "gl_fastworld", "0", CVAR_ARCHIVE );
	if ( !gl_sse )
		gl_sse = Cvar_Get( "gl_sse", "0", CVAR_ARCHIVE );
	if ( !gl_mmx )
		gl_mmx = Cvar_Get( "gl_mmx", "0", CVAR_ARCHIVE );
	if ( !gl_shadows )
		gl_shadows = Cvar_Get( "gl_shadows", "0", CVAR_ARCHIVE );
	if ( !gl_dynamic )
		gl_dynamic = Cvar_Get( "gl_dynamic", "1", CVAR_ARCHIVE );
	if ( !gl_fastdraw )
		gl_fastdraw = Cvar_Get( "gl_fastdraw", "0", CVAR_ARCHIVE );
	if ( !gl_fastparticles )
		gl_fastparticles = Cvar_Get( "gl_fastparticles", "0", CVAR_ARCHIVE );
	if ( !g_gibmode )
		g_gibmode = Cvar_Get( "g_gibmode", "0", CVAR_ARCHIVE );
	if ( !g_noflies )
		g_noflies = Cvar_Get( "g_noflies", "0", CVAR_ARCHIVE );
	//

	s_mode_list[SOFTWARE_MENU].curvalue = sw_mode->value;
	// new list for opengl modes for wide  and ultra wide resolutions
	//s_mode_list[OPENGL_MENU].curvalue = gl_mode->value;
	s_res_all = resolutions_new;
	if ( !vid_aspect )
		vid_aspect = Cvar_Get( "vid_aspect", "0", CVAR_ARCHIVE );
	{
		int cur = (int)gl_mode->value;
		int asp = (int)vid_aspect->value;

		if ( cur < 0 || cur >= NUM_GL_MODES )
			cur = 3;
		if ( asp < 0 || asp > 5 )
			asp = 0;		// invalid filter only; never silently broaden a selected ratio
		s_aspect_list.curvalue = asp;
		BuildModeList( asp, cur );
	}
	//

	if ( !scr_viewsize )
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	s_screensize_slider[SOFTWARE_MENU].curvalue = scr_viewsize->value/10;
	s_screensize_slider[OPENGL_MENU].curvalue = scr_viewsize->value/10;

	{	/* BDGN 3.60c: VID_RefFromCvars */
		int idx;
		int ref = VID_RefFromCvars( &idx );
		s_current_menu_index = idx;
		s_ref_list[s_current_menu_index].curvalue = ref;
		if ( idx == SOFTWARE_MENU )
			s_ref_list[0].curvalue = s_ref_list[1].curvalue = ref;
	}
	if (VID_RefExcludedOnVoodoo1(s_ref_list[s_current_menu_index].curvalue,VID_HardwareFlags()))
		s_ref_list[s_current_menu_index].curvalue=REF_3DFX;
	if(s_ref_list[s_current_menu_index].curvalue==REF_MT_RENDITION &&
	   !Cvar_VariableValue("bdgn_rendition_smp_test"))
		s_ref_list[s_current_menu_index].curvalue=REF_RENDITION_GL;
	s_ref_inuse = s_ref_prev = s_ref_list[s_current_menu_index].curvalue;	/* BDGN 3.52k: the driver in use always stays in the list */
	s_ref_list[!s_current_menu_index].curvalue = s_ref_list[s_current_menu_index].curvalue;
	if ( VID_S3VirgeSelected () || VID_V1KSelected () )	/* BDGN 3.51ak: the driver is known now; limit the modes */
		BuildModeList( s_aspect_list.curvalue, VID_V1KSelected () ? (int)Cvar_Get( "v1k_mode", "2", CVAR_ARCHIVE )->value : (int)gl_mode->value );

	s_software_menu.x = viddef.width * 0.50;
	s_software_menu.nitems = 0;
	s_opengl_menu.x = viddef.width * 0.50;
	s_opengl_menu.nitems = 0;

	for ( i = 0; i < 2; i++ )
	{
		s_ref_list[i].generic.type = MTYPE_SPINCONTROL;
		s_ref_list[i].generic.name = "driver";
		s_ref_list[i].generic.x = 0;
		s_ref_list[i].generic.y = 0;
		s_ref_list[i].generic.callback = DriverCallback;
		s_ref_list[i].itemnames = refs;

		s_mode_list[i].generic.type = MTYPE_SPINCONTROL;
		s_mode_list[i].generic.name = "video mode";
		s_mode_list[i].generic.x = 0;
		s_mode_list[i].generic.y = 10;


		//second mode selector for opengl only
		s_mode_list_new[i].generic.type = MTYPE_SPINCONTROL;
		s_mode_list_new[i].generic.name = "video mode";
		s_mode_list_new[i].generic.x = 0;
		s_mode_list_new[i].generic.y = 20;		// below the "aspect ratio" row
		/* Keep the filtered labels paired with s_res_map and its selected index. */
		s_mode_list_new[i].itemnames = (i == OPENGL_MENU) ? s_res_names : resolutions_new;
		//

		s_screensize_slider[i].generic.type	= MTYPE_SLIDER;
		s_screensize_slider[i].generic.x		= 0;
		s_screensize_slider[i].generic.y		= 20;
		s_screensize_slider[i].generic.name	= "screen size";
		s_screensize_slider[i].minvalue = 3;
		s_screensize_slider[i].maxvalue = 12;
		s_screensize_slider[i].generic.callback = ScreenSizeCallback;

		s_brightness_slider[i].generic.type	= MTYPE_SLIDER;
		s_brightness_slider[i].generic.x	= 0;
		s_brightness_slider[i].generic.y	= 30;
		s_brightness_slider[i].generic.name	= "brightness";
		s_brightness_slider[i].generic.callback = BrightnessCallback;
		s_brightness_slider[i].minvalue = 5;
		s_brightness_slider[i].maxvalue = 13;
		s_brightness_slider[i].curvalue = 18.0f - 10.0f *
			(VID_SceneBrightness(i)?Cvar_VariableValue("gl_scene_gamma"):vid_gamma->value);
		if (s_brightness_slider[i].curvalue<5) s_brightness_slider[i].curvalue=5;
		if (s_brightness_slider[i].curvalue>13) s_brightness_slider[i].curvalue=13;

		s_fs_box[i].generic.type = MTYPE_SPINCONTROL;
		s_fs_box[i].generic.x	= 0;
		s_fs_box[i].generic.y	= 40;
		s_fs_box[i].generic.name	= "fullscreen";
		s_fs_box[i].itemnames = yesno_names;
		s_fs_box[i].curvalue = vid_fullscreen->value;

		s_defaults_action[i].generic.type = MTYPE_ACTION;
		s_defaults_action[i].generic.name = "reset to defaults";
		s_defaults_action[i].generic.x    = 0;
//		s_defaults_action[i].generic.y    = 90;
		s_defaults_action[i].generic.y    = 160;
		s_defaults_action[i].generic.callback = ResetDefaults;

		s_cancel_action[i].generic.type = MTYPE_ACTION;
		s_cancel_action[i].generic.name = "cancel";
		s_cancel_action[i].generic.x    = 0;
//		s_cancel_action[i].generic.y    = 100;
		s_cancel_action[i].generic.y    = 170;
		s_cancel_action[i].generic.callback = CancelChanges;
	}

	// fov
	{
		static const char *aspect_names[] = { "all", "4:3", "5:4", "16:10", "16:9", "21:9", 0 };

		s_aspect_list.generic.type = MTYPE_SPINCONTROL;
		s_aspect_list.generic.name = "aspect ratio";
		s_aspect_list.generic.x = 0;
		s_aspect_list.generic.y = 10;
		s_aspect_list.generic.callback = AspectCallback;
		s_aspect_list.itemnames = aspect_names;
        s_sw_aspect_list.generic.type=MTYPE_SPINCONTROL;
        s_sw_aspect_list.generic.name="aspect ratio";
        s_sw_aspect_list.generic.callback=SoftwareAspectCallback;
        s_sw_aspect_list.itemnames=aspect_names;
        if(!sw_aspect) sw_aspect=Cvar_Get("sw_aspect",vid_aspect->string,CVAR_ARCHIVE);
        if(Cvar_VariableValue("sw_bdgn_features")<3518) {
            static const char *old_aspects[]={"all","4:3","5:4",NULL};
            s_sw_aspect_list.itemnames=old_aspects;
        }
        if(s_ref_list[SOFTWARE_MENU].curvalue==REF_SWAM) {
            static const char *swam_aspects[]={"all","4:3","5:4","16:10","16:9",NULL};
            s_sw_aspect_list.itemnames=swam_aspects;
        }
        BuildSoftwareModeList((int)sw_aspect->value,(int)sw_mode->value);
	}
	// the OpenGL menu has two more rows at the top (aspect ratio, mode), so its rows move down by 10 (the gap at y=50 absorbs it)
	s_screensize_slider[OPENGL_MENU].generic.y = 30;
	s_brightness_slider[OPENGL_MENU].generic.y = 40;
	s_fs_box[OPENGL_MENU].generic.y = 50;

	s_fov_slider.generic.type = MTYPE_SLIDER;
	s_fov_slider.generic.x	= 0;
	s_fov_slider.generic.y	= 60;
	s_fov_slider.generic.name	= "fov";
	s_fov_slider.minvalue = 90;
	s_fov_slider.maxvalue = 125;
	s_fov_slider.curvalue = fov->value;
	//

	s_stipple_box.generic.type = MTYPE_SPINCONTROL;
	s_stipple_box.generic.x	= 0;
	s_stipple_box.generic.y	= 70;
	s_stipple_box.generic.name	= "stipple alpha";	/* no / yes (every other pixel) / solid */
	s_stipple_box.curvalue = sw_stipplealpha->value;
	s_stipple_box.itemnames = stipple_names;
	{	/* BDGN 3.52n */
		static const char *upscale_basic[]={"sharp pixels","smooth",NULL};
		static const char *upscale_shader[]={"sharp pixels","smooth","edge-aware",NULL};
		static const char *upscale_adaptive[]={"sharp pixels","smooth","edge-aware","adaptive + sharpen",NULL};
		static const char *post_quality[]={"off","low","high",NULL};
		static const char *fxaa_choices[]={"off","on",NULL};
		static const char *tone_choices[]={"off","filmic",NULL};
		static const char *grade_choices[]={"off","warm","cool",NULL};
		static const char *effect_names[]={"FXAA","bloom","ambient occlusion","tone mapping","colour grading"};
		const char **choices[]={fxaa_choices,post_quality,post_quality,tone_choices,grade_choices};
		static const char *effect_help[]={"Smooths geometry edges before the HUD",
			"Adds glow around bright parts of the scene","SSAO adds contact shading using scene depth",
			"Filmic contrast curve for the scene","Warm or cool colour treatment for the scene"};
		int maxfilter,post=(int)Cvar_VariableValue("bdgn_gl_post_available");
		float filter = Cvar_Get("sw_texfilter", "0", CVAR_ARCHIVE)->value;
		int filter2 = (int)Cvar_Get("gl_upscale_filter", "0", CVAR_ARCHIVE)->value;
		for(i=0;i<2;i++) {
			menuslider_s *slider=&s_render_scale_box[i];
			slider->generic.type=MTYPE_SLIDER;
			slider->generic.name="render scale";
			slider->generic.statusbar="50-150%: internal rendering resolution (100% is native)";
			slider->generic.ownerdraw=VID_RenderScaleLabel;
			slider->minvalue=0;
			slider->maxvalue=RENDER_SCALE_STEPS;
			slider->curvalue=RenderScaleStep(Cvar_Get(i==SOFTWARE_MENU?"sw_render_scale":"gl_render_scale","100",CVAR_ARCHIVE)->value);
		}
		s_upscale_filter.generic.type=MTYPE_SPINCONTROL;
		s_upscale_filter.generic.name="upscale filter";
		s_upscale_filter.generic.statusbar="Adaptive + sharpen reconstructs edges below 100% render scale";
		maxfilter=(post&POST_ADAPTIVE)?3:Cvar_VariableValue("gl_upscale_shader_capable")?2:1;
		s_upscale_filter.itemnames=maxfilter==3?upscale_adaptive:maxfilter==2?upscale_shader:upscale_basic;
		s_upscale_filter.curvalue=filter2>=0 && filter2<=maxfilter?filter2:0;
		for (i=0;i<(int)GL_POST_MENU_COUNT;i++) {
			menulist_s *effect=&s_gl_post_boxes[i];
			float value=Cvar_Get((char *)gl_post_cvars[i],"0",CVAR_ARCHIVE)->value;
			effect->generic.type=MTYPE_SPINCONTROL;
			effect->generic.name=effect_names[i];
			effect->generic.statusbar=effect_help[i];
			effect->itemnames=choices[i];
			effect->curvalue=value>=2 && choices[i][2]?2:value>=1?1:0;
		}
		s_texfilter_box.generic.type = MTYPE_SPINCONTROL;
		s_texfilter_box.generic.name = "texture filtering";
		s_texfilter_box.generic.statusbar = "Bilinear and trilinear improve sampling and can cost FPS";
		s_texfilter_box.itemnames = sw_texfilter_names;
		s_texfilter_box.curvalue = filter>=0 && filter<=3?(int)filter:0;
	}
	{	/* BDGN 3.52p */
		static const char *swcolor_names[] = { "off (grey light)", "on (like OpenGL)", 0 };
		s_swcolor_box.generic.type = MTYPE_SPINCONTROL;
		s_swcolor_box.generic.name = "coloured lighting";
		s_swcolor_box.generic.statusbar = "the map's coloured light on the walls, as in OpenGL; surfaces build a little slower";
		s_swcolor_box.itemnames = swcolor_names;
		s_swcolor_box.curvalue = Cvar_Get( "sw_coloredlight", "0", CVAR_ARCHIVE )->value ? 1 : 0;
	}
	{	/* BDGN 3.52r */
		static const char *swprefetch_names[] = { "off", "on (test)", 0 };
		s_swdrawdist_slider.generic.type = MTYPE_SLIDER;
		s_swdrawdist_slider.generic.name = "draw distance (fog)";
		s_swdrawdist_slider.generic.statusbar = "left = shorter: fog, and nothing is drawn beyond it (more fps); right = unlimited";
		s_swdrawdist_slider.minvalue = 0;
		s_swdrawdist_slider.maxvalue = 10;
		s_swdrawdist_slider.curvalue = 10 - Cvar_Get( "sw_drawdist", "0", CVAR_ARCHIVE )->value;
		s_swprefetch_box.generic.type = MTYPE_SPINCONTROL;
		s_swprefetch_box.generic.name = "texture prefetch";
		s_swprefetch_box.generic.statusbar = "3DNow! / SSE prefetch in the drawing loops (test: compare the fps)";
		s_swprefetch_box.itemnames = swprefetch_names;
		s_swprefetch_box.curvalue = Cvar_Get( "sw_prefetch", "0", 0 )->value ? 1 : 0;	/* BDGN 3.56c: test switch, not saved */
	}

	s_tq_slider.generic.type	= MTYPE_SLIDER;
	s_tq_slider.generic.x		= 0;
	s_tq_slider.generic.y		= 70;
	s_tq_slider.generic.name	= "texture quality";
	s_tq_slider.minvalue = 0;
	s_tq_slider.maxvalue = 3;
	s_tq_slider.curvalue = 3-gl_picmip->value;
	s_hdtexture_box.generic.type=MTYPE_SPINCONTROL;
	s_hdtexture_box.generic.name="HD textures";
	s_hdtexture_box.generic.statusbar="Standard or enhanced HD texture set (installed packs only)";
	s_hdtexture_box.generic.callback=VID_TextureChoice;
	s_hdtexture_box.itemnames=yesno_names;
	s_hdtexture_box.curvalue=-1;	/* BDGN 3.55i: VID_HDSetChoices picks the row of gl_hdtextures */
	s_hd4k_size_box.generic.type=MTYPE_SPINCONTROL;
	s_hd4k_size_box.generic.name="4K texture size";
	s_hd4k_size_box.generic.statusbar="Largest picture of the AI 4K set (less video memory with 2048 / 1024)";
	s_hd4k_size_box.itemnames=hd4k_size_names;
	{
		int v=(int)Cvar_VariableValue("gl_hd_maxsize");
		s_hd4k_size_box.curvalue=v>0 && v<=1024 ? 2 : (v>0 && v<=2048 ? 1 : 0);
	}

	{
		int i;
		float max=Cvar_VariableValue("bdgn_gl_anisotropy_max");
		float requested=Cvar_VariableValue("gl_anisotropy");
		const char *filter=Cvar_VariableString("gl_texturemode");

		s_gl_filter_box.generic.type=MTYPE_SPINCONTROL;
		s_gl_filter_box.generic.name="texture filtering";
		s_gl_filter_box.generic.statusbar="Trilinear smooths transitions between mip levels";
		s_gl_filter_box.itemnames=gl_filter_labels;
		s_gl_filter_box.curvalue=3;
		for (i=0;i<(int)(sizeof(gl_filter_modes)/sizeof(gl_filter_modes[0]));i++)
			if (!Q_stricmp(filter,gl_filter_modes[i])) s_gl_filter_box.curvalue=i;

		s_gl_anisotropy_box.generic.type=MTYPE_SPINCONTROL;
		s_gl_anisotropy_box.generic.name="anisotropic filtering";
		s_gl_anisotropy_box.generic.statusbar="Sharper textures viewed at an angle";
		s_gl_anisotropy_box.itemnames=gl_anisotropy_choices;
		s_gl_anisotropy_box.curvalue=0;
		gl_anisotropy_choices[0]=gl_anisotropy_labels[0];
		for (i=1;i<(int)(sizeof(gl_anisotropy_levels)/sizeof(gl_anisotropy_levels[0])) &&
		     gl_anisotropy_levels[i]<=max;i++) {
			gl_anisotropy_choices[i]=gl_anisotropy_labels[i];
			if (gl_anisotropy_levels[i]<=requested) s_gl_anisotropy_box.curvalue=i;
		}
		gl_anisotropy_choices[i]=NULL;
	}
	{
		static const int samples[]={2,4,8};
		static const char *names[]={"2x MSAA","4x MSAA","8x MSAA"};
		int i,count=1;
		int supported=(int)Cvar_VariableValue("bdgn_gl_msaa_modes");
		int active=(int)Cvar_VariableValue("bdgn_gl_msaa_samples");
		s_gl_msaa_box.generic.type=MTYPE_SPINCONTROL;
		s_gl_msaa_box.generic.name="anti-aliasing";
		s_gl_msaa_box.generic.statusbar="MSAA applies at native scale with post-processing off";
		s_gl_msaa_box.itemnames=gl_msaa_labels;
		s_gl_msaa_box.curvalue=0;
		gl_msaa_choices[0]=0;
		gl_msaa_labels[0]="off";
		for (i=0;i<3;i++) {
			if (!(supported&samples[i])) continue;
			gl_msaa_choices[count]=samples[i];
			gl_msaa_labels[count]=names[i];
			if (active==samples[i]) s_gl_msaa_box.curvalue=count;
			count++;
		}
		gl_msaa_labels[count]=NULL;
		gl_msaa_at_open=s_gl_msaa_box.curvalue;
	}
	s_paletted_texture_box.generic.type = MTYPE_SPINCONTROL;
	s_paletted_texture_box.generic.x	= 0;
	s_paletted_texture_box.generic.y	= 80;
	s_paletted_texture_box.generic.name	= "8-bit textures";
	s_paletted_texture_box.itemnames = yesno_names;
	s_paletted_texture_box.curvalue = gl_ext_palettedtexture->value;

	// disable textures
	s_disable_textures_box.generic.type = MTYPE_SPINCONTROL;
	s_disable_textures_box.generic.x	= 0;
	s_disable_textures_box.generic.y	= 100;
	s_disable_textures_box.generic.name	= "single colour textures";
	s_disable_textures_box.itemnames = yesno_names;
	s_disable_textures_box.curvalue = gl_hack_notextures->value;

	// BDGN 3.47: ref_gl: "single colour textures" = flat coloured polygons (r_drawflat): every polygon in the dominant
	// colour of its texture (flatcolors.txt).  The list index is not the cvar value: 0 off, 2 material, 3 grayscale, 1 random.
	{
		static const char *flat_names[] = { "off", "material colours", "grayscale", "random colours", 0 };
		int rv = (int)Cvar_VariableValue( "r_drawflat" );

		s_flat_list.generic.type = MTYPE_SPINCONTROL;
		s_flat_list.generic.x	= 0;
		s_flat_list.generic.y	= 100;
		s_flat_list.generic.name	= "single colour textures";
		s_flat_list.itemnames = flat_names;
		s_flat_list.generic.callback = FlatModeCallback;
		flat_at_open = ( rv == 2 ) ? 1 : ( rv == 3 ) ? 2 : ( rv == 1 ) ? 3 : 0;
		s_flat_list.curvalue = flat_at_open;
	}

	// pak6-lowtextures.pak on/off (works with both OpenGL renderers; restarts the renderer when changed)
	s_lowtex_box.generic.type = MTYPE_SPINCONTROL;
	s_lowtex_box.generic.x	= 0;
	s_lowtex_box.generic.y	= 90;
	s_lowtex_box.generic.name	= "low fidelity textures";
	s_lowtex_box.itemnames = onoff_names;
	s_lowtex_box.generic.callback=VID_TextureChoice;
	s_lowtex_box.curvalue = Cvar_VariableValue( "fs_lowtextures" ) ? 1 : 0;
	/* BDGN 3.51ak: S3 Virge driver options (shown only with the S3 Virge renderers) */
	{
		static const char *s3v_square_names[] = { "enlarge (default)", "shrink (saves memory)", 0 };
		static const char *s3v_alpha_names[] = { "40", "80 (default)", "128", "192", 0 };
		int a = (int)Cvar_Get( "s3v_alpha", "80", CVAR_ARCHIVE )->value, k;
		s_s3v_dither_box.generic.type = MTYPE_SPINCONTROL;
		s_s3v_dither_box.generic.name = "S3 dither textures";
		s_s3v_dither_box.generic.statusbar = "smoother 16-bit colours, a little slower";
		s_s3v_dither_box.itemnames = onoff_names;
		s_s3v_dither_box.curvalue = Cvar_Get( "s3v_dither", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_s3v_square_box.generic.type = MTYPE_SPINCONTROL;
		s_s3v_square_box.generic.name = "S3 non-square textures";
		s_s3v_square_box.generic.statusbar = "the Virge needs square textures: enlarge or shrink the rest";
		s_s3v_square_box.itemnames = s3v_square_names;
		s_s3v_square_box.curvalue = Cvar_Get( "s3v_squaredown", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_s3v_alpha_box.generic.type = MTYPE_SPINCONTROL;
		s_s3v_alpha_box.generic.name = "S3 alpha cut-off";
		s_s3v_alpha_box.generic.statusbar = "where see-through texture pixels start (grates, sprites)";
		s_s3v_alpha_box.itemnames = s3v_alpha_names;
		s_s3v_alpha_box.curvalue = 1;
		for ( k = 0; k < 4; k++ )
			if ( s3v_alpha_values[k] == a )
				s_s3v_alpha_box.curvalue = k;
		s_s3v_math_box.generic.type = MTYPE_SPINCONTROL;
		s_s3v_math_box.generic.name = "S3 fast FPU math";
		s_s3v_math_box.generic.statusbar = "single precision maths in the driver: faster, a little less exact";
		s_s3v_math_box.itemnames = onoff_names;
		s_s3v_math_box.curvalue = Cvar_Get( "s3v_fastmath", "0", CVAR_ARCHIVE )->value ? 1 : 0;
	}
	/* BDGN 3.52l: 3dfx driver options (shown only with the 3dfx drivers) */
	{
		static const char *fx_hz_names[] = { "driver default", "60 Hz", "72 Hz", "75 Hz", "85 Hz", "100 Hz", "120 Hz", 0 };
		static const char *fx_cpu_names[] = { "off", "auto (3DNow! / MMX)", 0 };
		int hz = (int)Cvar_Get( "fx_refresh", "0", CVAR_ARCHIVE )->value, k;
		s_fx_tb_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_tb_box.generic.name = "3dfx triple buffer";
		s_fx_tb_box.generic.statusbar = "three colour buffers: smoother with vsync, uses more card memory";
		s_fx_tb_box.itemnames = onoff_names;
		s_fx_tb_box.curvalue = Cvar_Get( "fx_triplebuffer", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_fx_hz_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_hz_box.generic.name = "3dfx refresh rate";
		s_fx_hz_box.generic.statusbar = "monitor refresh of the Voodoo output";
		s_fx_hz_box.itemnames = fx_hz_names;
		s_fx_hz_box.curvalue = 0;
		for ( k = 0; k < 7; k++ )
			if ( fx_hz_values[k] == hz )
				s_fx_hz_box.curvalue = k;
		s_fx_dither_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_dither_box.generic.name = "3dfx mipmap dither";
		s_fx_dither_box.generic.statusbar = "softer steps between texture sizes, slower";
		s_fx_dither_box.itemnames = onoff_names;
		s_fx_dither_box.curvalue = Cvar_Get( "fx_lod_dither", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_fx_fast_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_fast_box.generic.name = "Voodoo2 fast memory (test)";
		s_fx_fast_box.generic.statusbar = "faster memory and PCI reads; turn off if the picture breaks up";
		s_fx_fast_box.itemnames = onoff_names;
		s_fx_fast_box.curvalue = Cvar_Get( "fx_fastmem", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_fx_filter_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_filter_box.generic.name = "Voodoo2 video filter";
		s_fx_filter_box.generic.statusbar = "smooths the 16-bit dither pattern on the monitor";
		s_fx_filter_box.itemnames = onoff_names;
		s_fx_filter_box.curvalue = Cvar_Get( "fx_videofilter", "1", CVAR_ARCHIVE )->value ? 1 : 0;
		s_fx_cpu_box.generic.type = MTYPE_SPINCONTROL;
		s_fx_cpu_box.generic.name = "miniGL CPU code";
		s_fx_cpu_box.generic.statusbar = "the miniGL's 3DNow! or MMX path for this CPU";
		s_fx_cpu_box.itemnames = fx_cpu_names;
		s_fx_cpu_box.curvalue = Cvar_Get( "fx_cpupath", "1", CVAR_ARCHIVE )->value ? 1 : 0;
	}
	/* BDGN 3.51ast: Rendition ref_v1k options (shown only with [Rendition V1000]; see ref_v1k's readme) */
	{
		static const char *v1k_aa_names[] = { "off", "world", "world + models", "all (slow)", 0 };
		static const char *v1k_flip_names[] = { "page flip", "copy (faster, may tear)", 0 };
		static const char *v1k_lookup_names[] = { "Verite chip", "CPU", 0 };
		static const char *v1k_rate_names[] = { "60 Hz", "72 Hz", "75 Hz", "85 Hz", 0 };
		int aa = (int)Cvar_Get( "v1k_antialias", "0", CVAR_ARCHIVE )->value;
		int rate = (int)Cvar_Get( "v1k_refreshrate", "60", CVAR_ARCHIVE )->value, k;
		s_v1k_aa_box.generic.type = MTYPE_SPINCONTROL;
		s_v1k_aa_box.generic.name = "Verite antialiasing";
		s_v1k_aa_box.generic.statusbar = "smooth polygon edges (the V1000 slows down a lot)";
		s_v1k_aa_box.itemnames = v1k_aa_names;
		s_v1k_aa_box.curvalue = 0;
		for ( k = 0; k < 4; k++ )
			if ( v1k_aa_values[k] == aa )
				s_v1k_aa_box.curvalue = k;
		s_v1k_warp_box.generic.type = MTYPE_SPINCONTROL;
		s_v1k_warp_box.generic.name = "Verite underwater warp";
		s_v1k_warp_box.itemnames = onoff_names;
		s_v1k_warp_box.curvalue = Cvar_Get( "v1k_waterwarp", "1", CVAR_ARCHIVE )->value ? 1 : 0;
		s_v1k_flip_box.generic.type = MTYPE_SPINCONTROL;
		s_v1k_flip_box.generic.name = "Verite buffer swap";
		s_v1k_flip_box.itemnames = v1k_flip_names;
		s_v1k_flip_box.curvalue = Cvar_Get( "v1k_nopageflip", "0", CVAR_ARCHIVE )->value ? 1 : 0;
		s_v1k_lookup_box.generic.type = MTYPE_SPINCONTROL;
		s_v1k_lookup_box.generic.name = "Verite texture convert";
		s_v1k_lookup_box.generic.statusbar = "who converts textures to 16-bit: try both";
		s_v1k_lookup_box.itemnames = v1k_lookup_names;
		s_v1k_lookup_box.curvalue = Cvar_Get( "v1k_surfacelookup", "1", CVAR_ARCHIVE )->value ? 0 : 1;
		s_v1k_rate_box.generic.type = MTYPE_SPINCONTROL;
		s_v1k_rate_box.generic.name = "Verite refresh rate";
		s_v1k_rate_box.itemnames = v1k_rate_names;
		s_v1k_rate_box.curvalue = 0;
		for ( k = 0; k < 4; k++ )
			if ( v1k_rate_values[k] == rate )
				s_v1k_rate_box.curvalue = k;
	}
	{
		static const char *worker_names[]={AUDIO_WORKER_NAMES,NULL};
		int system,process;
		Sys_ProcessorCounts(&system,&process);
		s_has_audio_workers=process>=2;
		s_audio_worker_box.generic.type=MTYPE_SPINCONTROL;
		s_audio_worker_box.generic.name="multiprocessor";
		s_audio_worker_box.generic.statusbar="Audio workers. SMP drivers also run rendering through MTGL32.";
		s_audio_worker_box.itemnames=worker_names;
		s_audio_worker_at_open=(int)Cvar_VariableValue("s_gl_audio_worker");
		if (s_audio_worker_at_open<0 || s_audio_worker_at_open>=AUDIO_WORKER_COUNT) s_audio_worker_at_open=0;
		s_audio_worker_box.curvalue=s_audio_worker_at_open;
	}
	{	/* BDGN 3.51p: software renderer on two processors (r_smp.c); off by default */
		static const char *smp_names[] = { SW_SMP_NAMES, NULL };
		static const char *legacy_names[] = { "Off", "Auto", "Forced Auto", NULL };
		int modes = Cvar_VariableValue("sw_smp_modes") == SW_SMP_COUNT ? SW_SMP_COUNT : 3;
		int smp = (int)Cvar_VariableValue( "sw_smp" );
		static const char *thread_names[]={"Auto","2","3","4",NULL};
		int threads=(int)Cvar_Get("sw_smp_threads","0",CVAR_ARCHIVE)->value;
		s_smp_box.generic.type = MTYPE_SPINCONTROL;
		s_smp_box.generic.x = 0;
		s_smp_box.generic.name = "multiprocessor";
		s_smp_box.generic.statusbar = "Auto uses two CPUs; new modes tested in SMP Test";
		s_smp_box.itemnames = modes == SW_SMP_COUNT ? smp_names : legacy_names;
		s_smp_box.curvalue = smp < 0 ? 0 : ( smp >= modes ? 0 : smp );
		s_sw_threads_box.generic.type=MTYPE_SPINCONTROL;
		s_sw_threads_box.generic.name="SMP render threads";
		s_sw_threads_box.generic.statusbar="Limits adaptive cache and large bilinear/trilinear wall batches";
		s_sw_threads_box.itemnames=thread_names;
		s_sw_threads_box.curvalue=threads>=2 && threads<=4?threads-1:0;
	}

	// end

	// BDGN 3.40: CPU optimizations.  cpu_auto (default) switches on everything the CPU supports.  An instruction
	// set the CPU (or Windows, for SSE) lacks is greyed out: it can't be changed and always shows "not supported".
	{
		static const char *auto_names[] = { "auto (all supported)", "manual", 0 };
		static const char *na_names[] = { "not supported", "not supported", 0 };
		int has3d = Q3E_CPUHas3DNow();
		int hassse = Q3E_CPUHasSSE();
		int hasmmx = Q3E_CPUHasMMX();

		s_cpuauto_box.generic.type = MTYPE_SPINCONTROL;
		s_cpuauto_box.generic.x		= 0;
		s_cpuauto_box.generic.name	= "CPU optimizations";
		s_cpuauto_box.itemnames = auto_names;
		s_cpuauto_box.curvalue = Cvar_VariableValue( "cpu_auto" ) ? 0 : 1;

		(void)has3d; (void)hassse; (void)hasmmx; (void)na_names;
		VID_SimdRows ();	/* BDGN 3.51bat: status rows, filled again each time the menu is built */
	}

	// vertical sync: off = more fps (the picture may tear); the driver can override it
	s_vsync_box.generic.type = MTYPE_SPINCONTROL;
	s_vsync_box.generic.x	= 0;
	s_vsync_box.generic.name	= "vertical sync";
	s_vsync_box.itemnames = onoff_names;
	s_vsync_box.curvalue = Cvar_VariableValue( "gl_swapinterval" ) ? 1 : 0;




	// draw distance: right = maximum (default, no fog), left = minimum (about 5 m, with fog)
	// the slider runs backwards compared to the cvar (gl_drawdist 0 = unlimited, 10 = shortest)
	s_drawdist_slider.generic.type	= MTYPE_SLIDER;
	s_drawdist_slider.generic.x		= 0;
	s_drawdist_slider.generic.y		= 110;
	s_drawdist_slider.generic.name	= "draw distance";
	s_drawdist_slider.minvalue = 0;
	s_drawdist_slider.maxvalue = 10;
	s_drawdist_slider.curvalue = 10 - gl_drawdist->value;

	// BDGN 3.4 performance toggles (y positions are set by BuildOpenGLMenu)

	// config preset: applied with OK (see ApplyConfigPreset)
	s_preset_box.generic.type = MTYPE_SPINCONTROL;
	s_preset_box.generic.x		= 0;
	s_preset_box.generic.name	= "config preset";
	s_preset_box.itemnames = preset_names;
	s_preset_box.curvalue = 0;
	/* BDGN 3.57h: the preset category (VID_BuildPresetView) */
	s_preset_cat_box.generic.type = MTYPE_SPINCONTROL;
	s_preset_cat_box.generic.x = 0;
	s_preset_cat_box.generic.name = "preset category";
	s_preset_cat_box.generic.callback = PresetCatCallback;
	s_preset_cat_box.generic.statusbar = "Low ... High Fidelity follow the hardware tiers; Software and 3dfx also switch the renderer";
	s_preset_cat_box.itemnames = pcat_names;
	s_preset_cat_box.curvalue = 0;


	// light/smoke effects: smoke and flash puffs + dynamic lights + flies + gibs, all in one row.
	// Shows "on" only when all four are normal; mixed settings made on the console are kept unless the row is changed.
	lightfx_touched = 0;
	s_lightfx_box.generic.type = MTYPE_SPINCONTROL;
	s_lightfx_box.generic.x		= 0;
	s_lightfx_box.generic.name	= "light/smoke/gibs/flies effects";
	s_lightfx_box.itemnames = onoff_names;
	s_lightfx_box.generic.callback = LightFxCallback;
	s_lightfx_box.curvalue = ( !cl_nosmoke->value && gl_dynamic->value && !g_noflies->value && !g_gibmode->value ) ? 1 : 0;

	s_fastworld_box.generic.type = MTYPE_SPINCONTROL;
	s_fastworld_box.generic.x	= 0;
	s_fastworld_box.generic.name	= "fullbright world (fast)";
	s_fastworld_box.itemnames = onoff_names;
	s_fastworld_box.generic.callback = FastWorldCallback;

	s_fastworld_box.curvalue = gl_fastworld->value ? 1 : 0;

 for (i=0;i<2;i++) {
  static const char *page_names[5]={"picture & textures...","lighting & shadows...","effects & water...","2D enemies & sprites...","performance..."};	/* 3.57d */
  menuaction_s *pages[5];
  int k;
  pages[0]=&s_pic_action[i]; pages[1]=&s_light_action[i]; pages[2]=&s_fx_action[i]; pages[3]=&s_enemy_action[i]; pages[4]=&s_perf_action[i];
  for (k=0;k<5;k++) {	/* BDGN 3.55i */
   pages[k]->generic.type=MTYPE_ACTION;
   pages[k]->generic.name=page_names[k];
  }
  s_pic_action[i].generic.callback=PicturePageCallback;
  s_light_action[i].generic.callback=LightingPageCallback;
  s_fx_action[i].generic.callback=EffectsTweaksCallback;
  s_enemy_action[i].generic.callback=EnemiesPageCallback;
  s_perf_action[i].generic.callback=PerformancePageCallback;
  s_paths_action[i].generic.type=MTYPE_ACTION;
  s_paths_action[i].generic.name="draw paths & detail...";
  s_paths_action[i].generic.callback=PathsPageCallback;
  s_testing_action[i].generic.type=MTYPE_ACTION;
  s_testing_action[i].generic.name="advanced...";	/* 3.60c: was "testing..." */
  s_testing_action[i].generic.callback=TestingPageCallback;
  /* BDGN 3.57d */
  s_simpler_action[i].generic.type=MTYPE_ACTION;
  s_simpler_action[i].generic.name="simpler graphics...";
  s_simpler_action[i].generic.callback=SimplerPageCallback;
  s_old_action[i].generic.type=MTYPE_ACTION;
  s_old_action[i].generic.name="old-school modes...";
  s_old_action[i].generic.callback=OldSchoolPageCallback;
  s_hdr_better[i].generic.type=s_hdr_faster[i].generic.type=MTYPE_SEPARATOR;
  s_hdr_better[i].generic.name=viddef.width<640?"BETTER LOOKING (slower)":"BETTER LOOKING - nicer, but slower";
  s_hdr_faster[i].generic.name=viddef.width<640?"FASTER (simpler look)":"FASTER - more fps, simpler look";
  s_hdr_better[i].generic.x=(int)strlen(s_hdr_better[i].generic.name)*4;	/* centred on the column */
  s_hdr_faster[i].generic.x=(int)strlen(s_hdr_faster[i].generic.name)*4;
  s_pic_menu[i].x=s_perf_menu[i].x=(int)(viddef.width*0.50)-8;
  s_pic_menu[i].y=s_perf_menu[i].y=viddef.height/2-58;
  s_tweak_graphics_action[i].generic.type=MTYPE_ACTION;
  s_tweak_graphics_action[i].generic.name=viddef.width < 640 ? "rendering..." : "rendering & performance...";
  s_tweak_graphics_action[i].generic.y=100;
  s_tweak_graphics_action[i].generic.callback=GraphicsTweaksCallback;
  s_tweak_effects_action[i].generic.type=MTYPE_ACTION;
  s_tweak_effects_action[i].generic.name="effects & animations...";
  s_tweak_effects_action[i].generic.y=110;
  s_tweak_effects_action[i].generic.callback=EffectsTweaksCallback;
  s_benchmarks_action[i].generic.type=MTYPE_ACTION;
  s_benchmarks_action[i].generic.name="benchmarks...";
  s_benchmarks_action[i].generic.callback=BenchmarksCallback;
 }
 s_sw_apply_action.generic.type=MTYPE_ACTION;
 s_sw_apply_action.generic.name="apply";
 s_sw_apply_action.generic.callback=ApplyChanges;
 s_gl_apply_action=s_sw_apply_action;
 BuildSoftwareMenu();
	BuildOpenGLMenu();

	s_video_scroll[0]=s_video_scroll[1]=0;
	s_opengl_menu.x -= 8;
	s_software_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int w, h, top;

	s_current_menu=s_current_menu_index==SOFTWARE_MENU ? &s_software_menu : &s_opengl_menu;
	if (s_current_menu_index==SOFTWARE_MENU) {
		if (s_fov_slider.generic.parent!=&s_software_menu) BuildSoftwareMenu();
	} else if (s_fov_slider.generic.parent!=&s_opengl_menu) BuildOpenGLMenu();

	re.DrawGetPicSize(&w,&h,"m_banner_video");
	if (h<0) h=0;
	top=viddef.height/12;
	re.DrawPic(viddef.width/2-w/2,top,"m_banner_video");
	Menu_DrawScrolled(s_current_menu,top+h+8,viddef.height-20,&s_video_scroll[s_current_menu_index]);
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	menuframework_s *m = s_current_menu;
	static const char *sound = "misc/menu1.wav";

	switch ( key )
	{
	case K_ESCAPE:
		ApplyChanges( 0 );
		return NULL;
	case K_KP_UPARROW:
	case K_MWHEELUP:
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_KP_DOWNARROW:
	case K_MWHEELDOWN:
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_KP_ENTER:
	case K_ENTER:
		if ( !Menu_SelectItem( m ) )
			ApplyChanges( NULL );
		break;
	}

	return sound;
}


