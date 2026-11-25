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
// cl_main.c  -- client main loop

#include "client.h"

/* BDGN 3.59f: bounded copy/append, always terminated */
static void Q_strncpyz_bdgn (char *dst, const char *src, int size)
{
	int n = (int)strlen (src);
	if (size <= 0) return;
	if (n > size - 1) n = size - 1;
	memcpy (dst, src, n);
	dst[n] = 0;
}
static void Q_strncatz_bdgn (char *dst, const char *src, int size)
{
	int l = (int)strlen (dst);
	if (l < size - 1)
		Q_strncpyz_bdgn (dst + l, src, size - l);
}

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif
static void CL_FrameTimes_f (void);	/* BDGN 3.52 (WESTBECK): bdgn_frametimes (above CL_Frame) */
#include "../win32/snd_wavmusic.h"
#include "../qcommon/q3e_3dnow.h"	// BDGN 3.38: Q3E_CPUHas3DNow

cvar_t	*freelook;

cvar_t	*adr0;
cvar_t	*adr1;
cvar_t	*adr2;
cvar_t	*adr3;
cvar_t	*adr4;
cvar_t	*adr5;
cvar_t	*adr6;
cvar_t	*adr7;
cvar_t	*adr8;

cvar_t	*cl_stereo_separation;
cvar_t	*cl_stereo;

cvar_t	*rcon_client_password;
cvar_t	*rcon_address;

cvar_t	*cl_noskins;
cvar_t	*cl_autoskins;
cvar_t	*cl_footsteps;
cvar_t	*cl_timeout;
cvar_t	*cl_predict;
//cvar_t	*cl_minfps;
cvar_t	*cl_maxfps;
cvar_t	*cl_async;	/* BDGN 3.58q: fixed 100 Hz physics, the picture paced by r_maxfps */
cvar_t	*r_maxfps;	/* BDGN 3.58q: frame rate cap with cl_async (0 = none) */
cvar_t	*cl_sleep;	/* BDGN 3.51ar: 0 busy wait (id), 1 Sleep(0), 2 Sleep(1) while waiting for the next frame (cl_framepace.h) */
cvar_t	*cl_gun;

cvar_t	*cl_add_particles;
cvar_t	*cl_add_lights;
cvar_t	*cl_add_entities;
cvar_t	*cl_add_blend;

cvar_t	*cl_shownet;
cvar_t	*cl_showmiss;
cvar_t	*cl_showclamp;

cvar_t	*cl_paused;
cvar_t	*cl_timedemo;

cvar_t	*lookspring;
cvar_t	*lookstrafe;
cvar_t	*sensitivity;
// add default and zoom sensitivity
cvar_t	*dsensitivity;
cvar_t	*zsensitivity;
// end

cvar_t	*m_pitch;
cvar_t	*m_yaw;
cvar_t	*m_forward;
cvar_t	*m_side;

cvar_t	*cl_lightlevel;

//
// userinfo
//
cvar_t	*info_password;
cvar_t	*info_spectator;
cvar_t	*name;
cvar_t	*skin;
cvar_t	*rate;
cvar_t	*fov;
// add fov for zoom and add default fov
cvar_t	*dfov;
cvar_t	*zfov;
// end
cvar_t	*msg;
cvar_t	*hand;
cvar_t	*gender;
cvar_t	*gender_auto;

cvar_t	*cl_vwep;

// BDGN 3.4 (from Breakfast Pie): performance toggles
cvar_t	*cl_amd_particles;	// 1 = 3DNow! CL_AddParticles (AMD patch, engine E2)
int		cl_3dnow_fx;
int		cl_sweep_frames;	/* BDGN 3.46: set by CL_PrepRefresh */
cvar_t	*cpu_auto;			// BDGN 3.40: 1 = every CPU optimization the CPU supports is on

/*
===============
CL_CPUApply / CL_3DNowEnforce

BDGN 3.51aut: 3DNow! is automatic and has no switch.  On a CPU with 3DNow! everything 3DNow! that gives frame rate
is always on: the 3DNow! kernels (gl_3dnow), all of AMD's kernels (gl_amd_*, cl_amd_particles, cm_amd_trace,
s_amd_mix) and the batched model/particle drawing that uses them (gl_modelbatch_auto, gl_particlebatch_auto, i.e.
gl_fastdraw "auto" = 3).  Choices that are slower on a 3DNow! machine are not allowed there: MMX (gl_mmx, s_mmx;
slower on the K6-III+ in bdgn_bench), the weaker model batching modes gl_fastdraw 1/2, and the 3DNow! rsqrt
(fastsqrt 2, also slower on the K6-III+; it becomes the plain fast sqrt 1).  CL_3DNowEnforce runs every frame, so a
config file, preset or console value cannot switch any of it off.  The hidden, unsaved bdgn_no3dnow 1 turns it all
off instead (bdgn_bench's "3DNow! off" reference only).  Without 3DNow! nothing 3DNow! can be on.  SSE stays off
(project policy); MMX follows the video menu's MMX row on CPUs without 3DNow!.
3.51bat: without 3DNow! SSE and MMX are automatic in ref_gl (CPU support + self-test, r_simd_sse / r_simd_mmx);
the MMX sound mixing (s_mmx) follows r_simd_mmx.
===============
*/
static const char *cl_3dnow_on[] = {
	"gl_3dnow", "gl_amd_sky", "gl_amd_angles", "gl_amd_cull", "cl_amd_particles", "cm_amd_trace",
	"gl_amd_marklights", "gl_amd_lightpoint", "gl_amd_particles", "s_amd_mix",
	"gl_modelbatch_auto", "gl_particlebatch_auto", "cpu_auto", NULL
};
#define CL_3DNOW_FLAGS	13
static const char *cl_3dnow_off[] = { "gl_mmx", "s_mmx", "gl_sse", NULL };
#define CL_3DNOW_OFFS	3

void CL_3DNowEnforce (void)
{
	static cvar_t *on[CL_3DNOW_FLAGS], *off[CL_3DNOW_OFFS], *fastdraw, *fastsqrt, *no3d, *path;
	static int sse_available;
	static int has3d = -1;
	int i, want;
	if (has3d < 0)
	{
		has3d = Q3E_CPUHas3DNow () ? 1 : 0;
		for (i = 0; cl_3dnow_on[i]; i++)
			on[i] = Cvar_Get ((char *)cl_3dnow_on[i], "1", CVAR_ARCHIVE);
		for (i = 0; cl_3dnow_off[i]; i++)
			off[i] = Cvar_Get ((char *)cl_3dnow_off[i], "0", CVAR_ARCHIVE);
		fastdraw = Cvar_Get ("gl_fastdraw", "0", CVAR_ARCHIVE);
		fastsqrt = Cvar_Get ("fastsqrt", "0", CVAR_ARCHIVE);
		no3d = Cvar_Get ("bdgn_no3dnow", "0", 0);
		path=Cvar_Get("bdgn_cpu_path","0",CVAR_ARCHIVE);
		sse_available=has3d && Q3E_CPUHasSSE();
	}
	if (!has3d)
	{	/* BDGN 3.51bbt: no MMX sound mixing at all (the per-frame MMX code was slower on the Pentium II) */
		static cvar_t *smmx;
		if (!smmx)
			smmx = Cvar_Get ("s_mmx", "0", CVAR_ARCHIVE);
		if (smmx->value)
			Cvar_SetValue ("s_mmx", 0);
		return;
	}
	want=!no3d->value;
	if (want && sse_available && !Q_stricmp(Cvar_VariableString("vid_ref"),"gl") &&
	    ((path->value==1 && Cvar_VariableValue("r_simd_sse_ready")) ||
	     (path->value==2 && Cvar_VariableValue("r_simd_sse2_ready"))) &&
	    !Cvar_VariableValue("bdgn_nosse"))
		want=0;
	for (i = 0; i < CL_3DNOW_FLAGS; i++)
		if (on[i]->value != want && !( i == CL_3DNOW_FLAGS - 1 && on[i]->value ))	/* cpu_auto: only switched on */
			Cvar_SetValue ((char *)cl_3dnow_on[i], (float)want);
	if (!want)
		return;
	for (i = 0; i < CL_3DNOW_OFFS; i++)
		if (off[i]->value)
			Cvar_SetValue ((char *)cl_3dnow_off[i], 0);
	if (fastdraw->value == 1 || fastdraw->value == 2)
		Cvar_SetValue ("gl_fastdraw", 0);		/* auto: 3 (strips/fans + byte colours with 3DNow!) */
	if (fastsqrt->value == 2)
		Cvar_SetValue ("fastsqrt", 1);
}

/* BDGN 3.58w (owner 2026-11-21): the CPU profile, chosen automatically from the processor (qcommon/common.c
   Com_CpuProfileClass: bdgn_cpu_profile names it). cpu_profile 1 (default) applies it, 0 leaves the generic values
   (Video -> VIEW & MATH "CPU profile", benchmark case "cpu_profile = 0"). What each profile does:
   - every profile: the CPU kernels the processor has (3DNow!, SSE, MMX) as before, chosen by feature, not by name;
   - Pentium, Pentium MMX, K5, WinChip, Cyrix (Socket 7 CPUs without 3DNow!): the fastest mixer profile and the primary
     sound buffer (s_profile 4 +0.8 %, s_primary 1 +0.6 %, MEASURED on the K6-3+; the Pentium 133 MEASURED sound off
     +22 %, so the mixer matters most there);
   - K5, VIA C3 without SSE (Samuel / Ezra: the FPU runs at half the clock), WinChip, Cyrix (weak FPUs): the integer
     fast square root (fastsqrt 1) instead of the FPU (TEORETISK ANALYS);
   - VIA C3 (both): the legacy class of the presets (K6-class sound rates, no HD effects; qcommon/common.c), the same
     sound settings as the Socket 7 CPUs; the C3 with SSE (Nehemiah) also gets the SSE kernels of 3.58w (ref_gl
     gl_simd.c, by feature);
   - K6 family: the measured K6 choices stay (no software prefetch, MEASURED -19.1 %; 3DNow! automatic);
   - Pentium Pro / II / III, Athlon, modern: no changes beyond the kernels.
   Turning the profile off puts the values back to the generic ones (s_profile 1, s_primary 0, fastsqrt 0). Under
   default config nothing is changed: it stays vanilla 3.21. */
static void CL_CPUProfileApply (void)
{
	static cvar_t *prof;
	int cls=(int)Cvar_VariableValue("bdgn_cpu_class");
	qboolean socket7=cls==CPUP_P5 || cls==CPUP_PMMX || cls==CPUP_K5 || cls==CPUP_WINCHIP || cls==CPUP_CYRIX ||
		cls==CPUP_C3 || cls==CPUP_C3SSE;
	qboolean weakfpu=cls==CPUP_K5 || cls==CPUP_C3 || cls==CPUP_WINCHIP || cls==CPUP_CYRIX;
	if (!prof) { prof=Cvar_Get("cpu_profile","1",CVAR_ARCHIVE); prof->modified=true; }
	if (!prof->modified) return;
	prof->modified=false;
	if (!Q_stricmp(Cvar_VariableString("cfg_preset"),"default") || !Q_stricmp(Cvar_VariableString("cfg_preset"),"k6gfdefault"))
		return;	/* default config stays vanilla 3.21 (owner's rule) */
	if (prof->value) {
		if (socket7) { Cvar_SetValue("s_profile",4); Cvar_SetValue("s_primary",1); }
		if (weakfpu && !Cvar_VariableValue("fastsqrt")) Cvar_SetValue("fastsqrt",1);
		Com_Printf("CPU profile: %s\n",Cvar_VariableString("bdgn_cpu_profile"));
	} else {
		if (socket7) { Cvar_SetValue("s_profile",1); Cvar_SetValue("s_primary",0); }
		if (weakfpu) Cvar_SetValue("fastsqrt",0);
		Com_Printf("CPU profile: off (generic)\n");
	}
}

void CL_CPUApply (void)
{
	int has3d = Q3E_CPUHas3DNow ();
	int hasmmx = Q3E_CPUHasMMX ();
	int i, onmmx;

	if (has3d)
	{
		CL_3DNowEnforce ();
		return;
	}
	onmmx = 0;	/* BDGN 3.51bbt: MMX only in the renderer's texture loader (automatic there); no MMX sound mixing */
	(void)hasmmx;
	for (i = 0; cl_3dnow_on[i]; i++)
		if (strcmp (cl_3dnow_on[i], "gl_modelbatch_auto") && strcmp (cl_3dnow_on[i], "gl_particlebatch_auto") && strcmp (cl_3dnow_on[i], "cpu_auto"))
			Cvar_SetValue ((char *)cl_3dnow_on[i], 0);	/* no 3DNow!: none of its kernels */
	Cvar_SetValue ("cpu_auto", 1);
	Cvar_SetValue ("gl_sse", 0);	/* BDGN 3.50f: the SSE renderer path can tint/darken textures */
	Cvar_SetValue ("gl_mmx", onmmx);
	Cvar_SetValue ("s_mmx", onmmx);
	if (Cvar_VariableValue ("fastsqrt") == 2)
		Cvar_SetValue ("fastsqrt", 1);
}

/*
===============
bdgn_bench   (BDGN 3.42)

Runs the shared tweak catalogue three times per case, writes TSV settings/results/summary,
then restores the saved settings. The penultimate block has all controls on except drawflat;
the final block has drawflat on. bdgn_bench_stop restores settings and stops early.
===============
*/
/*
===============
bdgn_status   (BDGN 3.46)

Prints what is really switched on: build type, CPU, the CPU switches and the speed cvars, and what the hidden
3DNow! behaviour (batched models, batched particles, fast effects) adds on top of the cvars.  Send the output when the
fps is not what bdgn_bench says it should be.
===============
*/
static void CL_Status_f (void)
{
	int		d3 = (int)Cvar_VariableValue ("gl_3dnow");
	int		fd = (int)Cvar_VariableValue ("gl_fastdraw");

#ifdef NDEBUG
	Com_Printf ("build            : Release (NDEBUG)\n");
#else
	Com_Printf ("build            : DEBUG - slow, build Release\n");
#endif
	Com_Printf ("version          : %s\n", BDGN_VERSION_STRING);
	Com_Printf ("CPU              : 3DNow! %s, SSE %s, MMX %s\n", Q3E_CPUHas3DNow () ? "yes" : "no",
		Q3E_CPUHasSSE () ? "yes" : "no", Q3E_CPUHasMMX () ? "yes" : "no");
	{	/* BDGN 3.51p: what the CPU and Windows report (TO DO 5 Nov: vendor, family/model, SSE2, HTT, processors) */
		q3e_cpuid_raw_t	raw;
		q3e_cpuinfo_t	info;
		char			line[256];
		int				nsys, nproc;

		Q3E_ReadCPUID (&raw);
		Q3E_DecodeCPU (&raw, &info);
		Q3E_DescribeCPU (&info, line, sizeof(line));
		Com_Printf ("cpu id           : %s\n", line);
		Com_Printf ("os support       : SSE %s, SSE2 %s, AVX %s, AVX2 %s\n",
			Q3E_CPUHasSSE()?"yes":"no",Q3E_CPUHasSSE2()?"yes":"no",
			Q3E_UsableAVXLevel()>=1?"yes":"no",Q3E_UsableAVXLevel()>=2?"yes":"no");
		Sys_ProcessorCounts (&nsys, &nproc);
		Com_Printf ("processors       : %i in Windows, %i for the game%s\n", nsys, nproc,
			info.htt ? " (CPU package has several logical processors: Hyper-Threading or cores; Windows counts each)" : "");
		Sys_OSDescription (line, sizeof(line));
		Com_Printf ("os               : %s\n", line);
		if (Cvar_VariableString ("bdgn_gl_renderer")[0])
			Com_Printf ("opengl driver    : %s / %s / %s\n", Cvar_VariableString ("bdgn_gl_vendor"),
				Cvar_VariableString ("bdgn_gl_renderer"), Cvar_VariableString ("bdgn_gl_version"));
	}
	Com_Printf ("cpu_auto         : %g\n", Cvar_VariableValue ("cpu_auto"));
	Com_Printf ("gl_3dnow/sse/mmx : %g / %g / %g   (s_mmx %g)\n", Cvar_VariableValue ("gl_3dnow"),
		Cvar_VariableValue ("gl_sse"), Cvar_VariableValue ("gl_mmx"), Cvar_VariableValue ("s_mmx"));
	Com_Printf ("vid_ref          : %s\n", Cvar_VariableString ("vid_ref"));
	Com_Printf ("gl_fastdraw      : %i   effective %i%s\n", fd, fd ? fd : (Cvar_VariableValue ("gl_modelbatch_auto") && d3 && Q3E_CPUHas3DNow () ? 3 : 0),
		(!fd && Cvar_VariableValue ("gl_modelbatch_auto") && d3 && Q3E_CPUHas3DNow ()) ? " (hidden 3DNow! batching)" : "");
	Com_Printf ("gl_fastparticles : %g   effective %i\n", Cvar_VariableValue ("gl_fastparticles"),
		(Cvar_VariableValue ("gl_fastparticles") || (Cvar_VariableValue ("gl_particlebatch_auto") && d3 && Q3E_CPUHas3DNow ())) ? 1 : 0);
	Com_Printf ("fast effects     : on (permanent)\n");
	Com_Printf ("3.50 AMD parts   : marklights %g, lightpoint %g, particles %g, s_amd_mix %g\n",
		Cvar_VariableValue ("gl_amd_marklights"), Cvar_VariableValue ("gl_amd_lightpoint"),
		Cvar_VariableValue ("gl_amd_particles"), Cvar_VariableValue ("s_amd_mix"));
	Com_Printf ("s_mixinterval    : %g\n", Cvar_VariableValue ("s_mixinterval"));
	Com_Printf ("gl_finish        : %g\n", Cvar_VariableValue ("gl_finish"));
	Com_Printf ("gl_batchworld    : %g\n", Cvar_VariableValue ("gl_batchworld"));
	Com_Printf ("gl_flatmodels    : %g   gl_fastsky %g   gl_fastwater %g\n", Cvar_VariableValue ("gl_flatmodels"),
		Cvar_VariableValue ("gl_fastsky"), Cvar_VariableValue ("gl_fastwater"));
	Com_Printf ("r_drawflat       : %g\n", Cvar_VariableValue ("r_drawflat"));
	Com_Printf ("maxclients       : %g\n", Cvar_VariableValue ("maxclients"));
}

#include "tweak_settings.h"
#include "render_scale.h"
#include "gl_fidelity.h"
#include "../qcommon/bdgn_prof.h"	/* BDGN 3.52t: the profiler areas (the counting is in the renderers) */
#define BENCH_REPEATS 3	/* 3.60c: 3 runs per case */
#define BENCH_MAX_CASES 64	/* 3.60c: the presets and audio cases (4096 before: 2.5 MB of tables) */
#define BENCH_MAX_VARS 384
#define BENCH_AUDIO (-12)
enum { BENCH_MUSIC_OFF, BENCH_MUSIC_WAV, BENCH_MUSIC_MP3, BENCH_MUSIC_CD, BENCH_MUSIC_MAPPER, BENCH_MUSIC_DEVICE };
static int bench_music_frames, bench_music_active_frames;
static int bench_stage_log;	/* BDGN 3.59g: DRIVER_STAGE lines on 3dfx drivers (the Voodoo2 crashed in glide2x.dll, owner 2026-11-21) */
static qboolean bench_first_frame;
static const int bench_preset_order[]={4,1,2,3,6,5};
static char bench_worker_ref[MAX_QPATH], bench_worker_driver[MAX_QPATH], bench_worker_target[MAX_QPATH];
#ifdef _WIN32
static qboolean bench_minigl_ini_saved;
static char bench_minigl_dll[MAX_OSPATH], bench_minigl_sse[MAX_OSPATH], bench_minigl_gdi[MAX_OSPATH];
#endif
#ifdef _WIN32
qboolean NET_BenchUdpStart (qboolean worker);
void NET_BenchUdpFrame (void);
void NET_BenchUdpStop (unsigned *attempted, unsigned *sent, unsigned *received, unsigned *worker_received, unsigned *errors);
#endif
#include "../qcommon/software_smp.h"
typedef struct { int setting, choice; char label[128]; } benchcase_t;
static benchcase_t bench_cases[BENCH_MAX_CASES];
static int bench_case_count, bench_next, bench_cur=-1, bench_repeat, bench_phase;
static qboolean bench_count_only;
static int bench_modern;
static int bench_launch_ms, bench_file_serial, bench_nvars;
static double bench_fps[BENCH_MAX_CASES][BENCH_REPEATS];
static int bench_done[BENCH_MAX_CASES][BENCH_REPEATS];
static char bench_names[BENCH_MAX_VARS][MAX_QPATH];
static char *bench_saved[BENCH_MAX_VARS];
static FILE *bench_log;
static char bench_logname[MAX_OSPATH], bench_demo[MAX_QPATH];
/* BDGN 3.52t: extended benchmark: 4 runs per case and the renderer's frame time split into areas (r_profile) */
static int bench_repeats = 3, bench_smp_presets;
static qboolean cl_sound_restart_pending;
/* BDGN 3.52t, 3.60c: the presets benchmark (bdgn_bench_presets) on the renderer in use, with an overview TSV */
int VID_BenchRefList (int *out, int max, char *skipped, int skipsize, int mode, const char *driver);	/* vid_menu.c */
const char *VID_BenchRefName (int ref);
qboolean VID_BenchSelectRef (int ref);
qboolean VID_SetMtGlTarget(const char *dll);
qboolean VID_ApplyPresetSettings(int id,qboolean archive);
/* 3.58d: the K6 + GeForce2/3 presets are offered (win32/vid_menu.c: GeForce2/3 found, K6 CPU, OpenGL) */
qboolean VID_FidelityPresetAvailable(int id);
void VID_SaveBenchmarkPresetCvars(void (*save)(const char *name));
const char *VID_MtGlTargetForRef (int ref);
const char *VID_MtGlGdiValue (const char *dll);
void VID_GetMtGlOptions (char *dll, char *sse, char *gdi, int size);
qboolean VID_WriteMtGlOptions (const char *dll, const char *sse, const char *gdi);
#define BENCH_ALL_MAX 24
typedef struct { double base,fastest,best; char best_label[128],status[128],logname[MAX_OSPATH],renderer[192]; int done; } benchall_t;
static benchall_t bench_all_res[BENCH_ALL_MAX];
static void BenchAllLine (const char *type, const char *a, const char *b);	/* BDGN 3.53 */
static int bench_all, bench_all_cancel, bench_all_ref[BENCH_ALL_MAX], bench_all_count, bench_all_next;
static int bench_all_state, bench_all_ms, bench_all_started, bench_force_smp;
static FILE *bench_all_log;
static char bench_all_logname[MAX_OSPATH], bench_all_demo[MAX_QPATH], bench_all_expect[MAX_QPATH];
static char bench_all_saved[6][MAX_QPATH];
static char bench_all_mtgl[260], bench_all_mtgl_sse[260], bench_all_mtgl_gdi[260];
static char bench_all_driver[MAX_QPATH], bench_all_target[MAX_QPATH];
static int bench_all_mtgl_changed;
static const char *bench_all_cvars[]={"vid_ref","gl_driver","gl_bitdepth","sw_smp","vid_rendition","gl_swapinterval"};

qboolean CL_BenchmarkRunning (void)
{
 return bench_phase || bench_all;
}

static int BenchAllRendererMatches (void)
{
 if(Q_stricmp(Cvar_VariableString("vid_ref"),bench_all_expect)) return 0;
 if(Q_stricmp(bench_all_expect,"gl") && Q_stricmp(bench_all_expect,"glam")) return 1;
 if(Q_stricmp(Cvar_VariableString("gl_driver"),bench_all_driver)) return 0;
 return !bench_all_target[0] ||
  (!Q_stricmp(Cvar_VariableString("bdgn_mtgl_target"),bench_all_target) &&
   !strcmp(Cvar_VariableString("bdgn_mtgl_gdi"),VID_MtGlGdiValue(bench_all_target)));
}
#define BENCH_ALL_CVARS (sizeof(bench_all_cvars)/sizeof(bench_all_cvars[0]))
#define PROF_SLOTS (PROF_COUNT+1)	/* + the part of the frame outside the renderer (client, game, sound, 2D HUD) */
static float bench_prof[BENCH_MAX_CASES][BENCH_REPEATS][PROF_SLOTS];
static float bench_prof_frame[BENCH_MAX_CASES][BENCH_REPEATS];
/* BDGN 3.52v: frame time statistics of every run (all benchmark modes) and the frame counters (with the profiler) */
typedef struct { float avg_ms,p99_ms,worst_ms,best_ms,min_fps,max_fps,low1_fps; int frames; } frametime_t;
static frametime_t bench_ft[BENCH_MAX_CASES][BENCH_REPEATS];
static float bench_cnt[BENCH_MAX_CASES][BENCH_REPEATS][PROFN_COUNT];
static char bench_cnt_ok[BENCH_MAX_CASES][BENCH_REPEATS];
static const char *bench_cnt_names[PROFN_COUNT] = { "surfaces", "models", "sprites", "particles", "dlights", "lightmap uploads", "entities" };
static const char *bench_prof_names[PROF_SLOTS] = { "other 3D", "world", "dynamic light", "surface cache", "models", "sprites",
	"weapon", "particles", "water/glass", "fog/warp/blend", "swap", "outside renderer" };

/* BDGN 3.52t: CPU cycle counter (RDTSC: Pentium, K6 and later) for the profiler report */
#if defined(_MSC_VER) && defined(_M_IX86)
#include <intrin.h>
static double CL_Tick (void) { return (double)(__int64)__rdtsc(); }
#elif defined(__GNUC__) && defined(__i386__)
static double CL_Tick (void) { return (double)(long long)__builtin_ia32_rdtsc(); }	/* gcc K6 test build */
#else
static double CL_Tick (void) { return 0; }
#endif
static double cl_prof_tick0;
static int cl_prof_ms0;
/* zero the renderer's totals at its next frame and start the client's clock */
/* BDGN 3.52v: frame times (cycles between frames) during a timedemo or while profiling */
#define CL_FT_MAX 65536
static unsigned int cl_ft[CL_FT_MAX], cl_ft_sorted[CL_FT_MAX];
static int cl_ft_n, cl_ft_skip;
static double cl_ft_t0, cl_ft_last;
static cvar_t *cl_ft_prof;
static frametime_t cl_ft_result; static int cl_ft_result_ok;
void CL_FrameTimeMark (int first)
{
 double t,d;
 if(!first) {
  if(!cl_ft_prof) cl_ft_prof=Cvar_Get("r_profile","0",0);
  if(!cl_timedemo->value && !cl_ft_prof->value) return;
 }
 t=CL_Tick();
 if(first) { cl_ft_n=0; cl_ft_t0=cl_ft_last=t; cl_ft_skip=1; return; }
 if(cl_ft_skip) { cl_ft_skip=0; cl_ft_last=t; return; }	/* the frame that started the count */
 d=t-cl_ft_last; cl_ft_last=t;
 if(cl_ft_n<CL_FT_MAX) cl_ft[cl_ft_n++]=d>4.0e9?4000000000u:(unsigned int)d;
}
static int CL_FtCompare (const void *a, const void *b)
{
 unsigned int x=*(const unsigned int *)a,y=*(const unsigned int *)b;
 return x<y?-1:x>y?1:0;
}
/* elapsed_ms: wall time since the first mark; the cycle counter is calibrated against it */
static int CL_FrameTimeStats (int elapsed_ms, frametime_t *o)
{
 double cpms,sum=0,slow=0; int i,n=cl_ft_n,k;
 memset(o,0,sizeof(*o));
 if(n<10 || elapsed_ms<100) return 0;
 cpms=(CL_Tick()-cl_ft_t0)/elapsed_ms;
 if(cpms<=0) return 0;
 memcpy(cl_ft_sorted,cl_ft,n*sizeof(cl_ft[0]));
 qsort(cl_ft_sorted,n,sizeof(cl_ft_sorted[0]),CL_FtCompare);
 for(i=0;i<n;i++) sum+=cl_ft_sorted[i];
 k=n/100; if(k<1) k=1;
 for(i=n-k;i<n;i++) slow+=cl_ft_sorted[i];
 o->frames=n;
 o->avg_ms=(float)(sum/n/cpms);
 o->best_ms=(float)(cl_ft_sorted[0]/cpms);
 o->worst_ms=(float)(cl_ft_sorted[n-1]/cpms);
 o->p99_ms=(float)(cl_ft_sorted[(int)(0.99*(n-1))]/cpms);
 o->min_fps=o->worst_ms>0?1000/o->worst_ms:0;
 o->max_fps=o->best_ms>0?1000/o->best_ms:0;
 o->low1_fps=slow>0?(float)(1000*k*cpms/slow):0;	/* "1% low": mean fps of the slowest 1% of the frames */
 return 1;
}
static void CL_FrameTimeFormat (char *out, int size, const frametime_t *f)
{
 Com_sprintf(out,size,"frame time avg %.3f ms | 99%% %.3f ms | worst %.3f ms | best %.3f ms | fps min %.1f | max %.1f | 1%% low %.1f | %i frames",
  f->avg_ms,f->p99_ms,f->worst_ms,f->best_ms,f->min_fps,f->max_fps,f->low1_fps,f->frames);
}
/* BDGN 3.52v: what the renderer drew per frame since the reset (r_profile_count); 0 without */
static int CL_ProfileCounts (float cnt[PROFN_COUNT])
{
 double v[PROFN_COUNT]; unsigned frames=0; int i;
 if(sscanf(Cvar_VariableString("r_profile_count"),"%u %lf %lf %lf %lf %lf %lf %lf",&frames,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6])!=1+PROFN_COUNT || !frames)
  return 0;
 for(i=0;i<PROFN_COUNT;i++) cnt[i]=(float)(v[i]/frames);
 return 1;
}
static void CL_CountsFormat (char *out, int size, const float cnt[PROFN_COUNT])
{
 int i,n; out[0]=0; n=0;
 for(i=0;i<PROFN_COUNT && n<size-40;i++) {
  Com_sprintf(out+n,size-n,"%s%s %.1f",i?" | ":"per frame: ",bench_cnt_names[i],cnt[i]); n+=(int)strlen(out+n);
 }
}
void CL_ProfileReset (void)
{
 Cvar_Set("r_profile_reset","1");
 cl_prof_tick0=CL_Tick(); cl_prof_ms0=Sys_Milliseconds();
 CL_FrameTimeMark(1);
}
/* BDGN 3.52v: timedemo end (CL_Disconnect): frame time statistics of the demo, printed and kept for the benchmark */
void CL_TimedemoStats (int elapsed_ms)
{
 char line[256];
 cl_ft_result_ok=CL_FrameTimeStats(elapsed_ms,&cl_ft_result);
 if(cl_ft_result_ok) { CL_FrameTimeFormat(line,sizeof(line),&cl_ft_result); Com_Printf("%s\n",line); }
}
/* ms per frame for every area since CL_ProfileReset; 0 when there is nothing to report */
static int CL_ProfileRead (float ms[PROF_SLOTS], float *frame_ms, float *mhz)
{
 double v[PROF_COUNT],cpms,sum=0,elapsed;
 unsigned frames=0;
 int i;
 const char *d=Cvar_VariableString("r_profile_data");
 elapsed=Sys_Milliseconds()-cl_prof_ms0;
 if(sscanf(d,"%u %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",&frames,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10])!=1+PROF_COUNT || !frames || elapsed<100)
  return 0;
 cpms=(CL_Tick()-cl_prof_tick0)/elapsed;
 if(cpms<=0) return 0;
 *frame_ms=(float)(elapsed/frames);
 *mhz=(float)(cpms/1000);
 for(i=0;i<PROF_COUNT;i++){ms[i]=(float)(v[i]/frames/cpms);sum+=ms[i];}
 ms[PROF_COUNT]=(float)(*frame_ms-sum); if(ms[PROF_COUNT]<0)ms[PROF_COUNT]=0;
 return 1;
}
static void CL_ProfileFormat (char *out, int size, const float ms[PROF_SLOTS], float frame_ms)
{
 int i,n;
 Com_sprintf(out,size,"frame %.3f ms",frame_ms); n=(int)strlen(out);
 for(i=0;i<PROF_SLOTS && n<size-48;i++)
  if(ms[i]>0.0005f){ Com_sprintf(out+n,size-n," | %s %.3f ms %.1f%%",bench_prof_names[i],ms[i],frame_ms>0?100*ms[i]/frame_ms:0); n+=(int)strlen(out+n); }
}
/* console: bdgn_profile [on|off|reset] - shows where the frame time went since the last reset */
static void CL_Profile_f (void)
{
 float ms[PROF_SLOTS],frame_ms,mhz;
 char line[1024];
 const char *a=Cmd_Argc()>1?Cmd_Argv(1):"";
 Cvar_Get("r_profile","0",0);
 if(!Q_stricmp(a,"on")){Cvar_Set("r_profile","1");CL_ProfileReset();Com_Printf("bdgn_profile: on (counting from now)\n");return;}
 if(!Q_stricmp(a,"off")){Cvar_Set("r_profile","0");Com_Printf("bdgn_profile: off\n");return;}
 if(!Q_stricmp(a,"reset")){CL_ProfileReset();Com_Printf("bdgn_profile: reset\n");return;}
 if(!Cvar_VariableValue("r_profile")){Com_Printf("usage: bdgn_profile on | off | reset; then bdgn_profile shows the split\n");return;}
 if(!CL_ProfileRead(ms,&frame_ms,&mhz)){Com_Printf("bdgn_profile: nothing measured yet (the renderer must be BDGN ref_gl or ref_soft 3.52t)\n");return;}
 CL_ProfileFormat(line,sizeof(line),ms,frame_ms);
 Com_Printf("%s | CPU %.0f MHz\n",line,mhz);
 { frametime_t ft; float cnt[PROFN_COUNT];	/* BDGN 3.52v */
  if(CL_FrameTimeStats(Sys_Milliseconds()-cl_prof_ms0,&ft)) { CL_FrameTimeFormat(line,sizeof(line),&ft); Com_Printf("%s\n",line); }
  if(CL_ProfileCounts(cnt)) { CL_CountsFormat(line,sizeof(line),cnt); Com_Printf("%s\n",line); }
 }
}
static void BenchField (const char *s)
{
 if (!s) return;
 while (*s) { fputc((*s=='\t'||*s=='\r'||*s=='\n')?' ':*s,bench_log); s++; }
}
static void BenchNote (const char *type, const char *note)
{
 if (!bench_log) return;
 BenchField(type); fprintf(bench_log,"\t-1\t0\t\t\t\t\t\t\t"); BenchField(note); fputc('\n',bench_log);
}
static void BenchSnapshot (const char *type, int id, int repeat)
{
 int i;
 for(i=0;i<bench_nvars;i++) {
  BenchField(type); fprintf(bench_log,"\t%i\t%i\t",id,repeat); BenchField(bench_names[i]); fputc('\t',bench_log);
  BenchField(Cvar_VariableString((char *)bench_names[i])); fprintf(bench_log,"\t\t\t\t\t\n");
 }
}
static void BenchLogPendingCase (int id, int repeat)
{
 int i;
 const char *value;
 BenchNote("RUN_START",va("%s | repeat %i/%i | vid_ref %s | gl_driver %s | MTGL target %s | preset %s",
  bench_cases[id].label,repeat+1,bench_repeats,Cvar_VariableString("vid_ref"),
  Cvar_VariableString("gl_driver"),bench_all?bench_all_target:Cvar_VariableString("bdgn_mtgl_target"),
  Cvar_VariableString("cfg_preset")));
 for(i=0;i<bench_nvars;i++) {
  value=Cvar_VariableString(bench_names[i]);
  if(!strcmp(value,bench_saved[i])) continue;
  fprintf(bench_log,"PENDING_CHANGE\t%i\t%i\t",id,repeat+1);
  BenchField(bench_names[i]); fputc('\t',bench_log);
  BenchField(value); fprintf(bench_log,"\t\t\t\t\twas ");
  BenchField(bench_saved[i]); fputc('\n',bench_log);
 }
 fflush(bench_log);
#ifdef _WIN32
 
#endif
}

static void BenchSave (const char *name)
{
 int i;
 for(i=0;i<bench_nvars;i++) if(!strcmp(name,bench_names[i])) return;
 if(bench_nvars>=BENCH_MAX_VARS) Com_Error(ERR_DROP,"benchmark variable limit");
 Com_sprintf(bench_names[bench_nvars],sizeof(bench_names[0]),"%s",name);
 bench_saved[bench_nvars]=CopyString(Cvar_VariableString((char *)name));
 bench_nvars++;
}
static void BenchRestore (void)
{
 int i;
 for(i=0;i<bench_nvars;i++) Cvar_Set((char *)bench_names[i],bench_saved[i]);
}
#include "bench_config.h"
/* BDGN 3.52t: video menu "reset to defaults": every benchmark row back to its reference (stock) value.
   Resolution / fullscreen / gamma (@saved), sound and music rows are left alone. */
void CL_BenchCfgDefaults (void)
{
 int i;
 for(i=0;i<(int)BENCH_CFG_COUNT;i++){
  const char *n=bench_cfg[i].name,*r=bench_cfg[i].reference;
  if(!n||!r||n[0]=='@'||r[0]=='@') continue;
  if(!strcmp(bench_cfg[i].category,"SOUND")||!strcmp(bench_cfg[i].category,"MUSIC")) continue;
  Cvar_Set((char *)n,(char *)r);
 }
}

/* BDGN 3.52t: extended benchmark summary. Means of runs 2..4 (run 1 has cold caches). PROFILE_MEAN: where the frame
   time of each case goes. HOTSPOT: areas that take 2% or more of the BASELINE frame. SIGNIFICANT: cases whose fps
   differ 2% or more from BASELINE, with the areas that changed most. */
static double BenchMeanFps (int c)
{
 int j,n=0; double sum=0;
 for(j=bench_repeats>1?1:0;j<bench_repeats;j++) if(bench_done[c][j]) {sum+=bench_fps[c][j];n++;}
 return n?sum/n:0;
}
/* BDGN 3.52v: per case the mean frame time statistics (runs 2..n) and, with the profiler, the mean frame counters */
static void BenchFrameTimeSummary (void)
{
 int c,j,i,n,nc; frametime_t m; float cnt[PROFN_COUNT]; char line[512],label[64];
 for(c=0;c<bench_case_count;c++) {
  memset(&m,0,sizeof(m)); n=0;
  for(i=0;i<PROFN_COUNT;i++)cnt[i]=0; nc=0;
  for(j=bench_repeats>1?1:0;j<bench_repeats;j++) if(bench_done[c][j]) {
   frametime_t *f=&bench_ft[c][j];
   if(f->frames>0) { m.avg_ms+=f->avg_ms;m.p99_ms+=f->p99_ms;m.worst_ms+=f->worst_ms;m.best_ms+=f->best_ms;
    m.min_fps+=f->min_fps;m.max_fps+=f->max_fps;m.low1_fps+=f->low1_fps;m.frames+=f->frames;n++; }
   if(bench_cnt_ok[c][j]) { for(i=0;i<PROFN_COUNT;i++)cnt[i]+=bench_cnt[c][j][i]; nc++; }
  }
  if(n) {
   m.avg_ms/=n;m.p99_ms/=n;m.worst_ms/=n;m.best_ms/=n;m.min_fps/=n;m.max_fps/=n;m.low1_fps/=n;m.frames/=n;
   CL_FrameTimeFormat(line,sizeof(line),&m);
   Com_sprintf(label,sizeof(label),"FRAMETIME_MEAN %i",c); BenchNote(label,va("%s | %s",bench_cases[c].label,line));
  }
  if(nc) {
   for(i=0;i<PROFN_COUNT;i++)cnt[i]/=nc;
   CL_CountsFormat(line,sizeof(line),cnt);
   Com_sprintf(label,sizeof(label),"COUNTS_MEAN %i",c); BenchNote(label,va("%s | %s",bench_cases[c].label,line));
  }
 }
}

static qboolean BenchCaseComplete(int id)
{
 int i;
 for(i=0;i<bench_repeats;i++)
  if(!bench_done[id][i]) return false;
 return true;
}

static void BenchFpsLosses(void)
{
 double baseline,fps,percent;
 int i;
 

 if(!bench_case_count || !BenchCaseComplete(0)) return;
 baseline=BenchMeanFps(0);
 if(baseline<=0) return;
 for(i=1;i<bench_case_count;i++) {
  if(!BenchCaseComplete(i)) continue;
  fps=BenchMeanFps(i);
  percent=100*(fps-baseline)/baseline;
  if(!strcmp(bench_cases[i].label,"BASELINE CHECK")) {
   if(fabs(percent)>=2)
    BenchNote("BASELINE_DRIFT",va("%+.1f%% vs initial baseline; interpret similar deltas cautiously",percent));
  } else if(percent<=-2) {
   BenchNote("FPS_LOSS",va("%s: %.2f fps (%+.1f%% vs %.2f)",bench_cases[i].label,fps,percent,baseline));
  }
 }
}

#ifdef _WIN32
typedef BOOL (WINAPI *bench_get_affinity_t)(HANDLE, PDWORD_PTR, PDWORD_PTR);
typedef BOOL (WINAPI *bench_set_affinity_t)(HANDLE, DWORD_PTR);


#endif

/* BDGN 3.57m: goals of the benchmark ("BDGN Retro Benchmark"). The log gets the goal of this computer (CPU class and
   renderer) and, per case, whether it reaches it, so the work aims at the project's targets. The numbers are the
   owner's to set: these are first proposals (TEORETISK); bench_goal <fps> replaces them for this computer. */
extern void VID_CpuName (char *out, int size);
static void CL_HwClean (const char *in, char *out, int size);
/* BDGN 3.57o: the CPU's name for the goals, the auto preset and the scores. Windows 95/98/ME have no
   ProcessorNameString in the registry (MEASURED: the owner's K6-3+ on Windows 98 got the "modern" goal), so the
   name then comes from CPUID family/model: AMD K5/K6/K6-2/K6-III/K6-2+ and K6-III+, Athlon; Intel Pentium,
   Pentium MMX, Pentium Pro, Pentium II, Celeron, Pentium III; Cyrix. Newer CPUs run Windows that has the name. */
static void CL_CpuNameAny (char *out, int size)
{
 q3e_cpuid_raw_t raw; q3e_cpuinfo_t c; const char *n=NULL;
 VID_CpuName(out,size);
 if(out[0]) return;
 Q3E_ReadCPUID(&raw); Q3E_DecodeCPU(&raw,&c);
 if(!strcmp(c.vendor,"AuthenticAMD")) {
  if(c.family==5) n=c.model<4?"AMD K5":c.model<8?"AMD-K6":c.model==8?"AMD-K6-2":c.model==9?"AMD-K6-III":"AMD-K6-III+ / K6-2+";
  else if(c.family==6) n="AMD Athlon";
 } else if(!strcmp(c.vendor,"GenuineIntel")) {
  if(c.family==5) n=(c.mmx||c.model==4||c.model==8)?"Intel Pentium MMX":"Intel Pentium (P5)";
  else if(c.family==6) n=c.model==1?"Intel Pentium Pro":(c.model==3||c.model==5)?"Intel Pentium II":c.model==6?"Intel Celeron (P6)":
                         (c.model==7||c.model==8||c.model==10||c.model==11)?"Intel Pentium III":NULL;
 } else if(!strcmp(c.vendor,"CyrixInstead")) n="Cyrix 6x86";
 if(n) { strncpy(out,n,size-1); out[size-1]=0; }
}
static const struct { const char *pattern, *gpu, *cls; int soft, gl; } bench_goals[]={
 /* 3.57o: the owner's goals (2026-11-19): Pentium 133 Software 30, Pentium 233 MMX Software 40, K6-2 500 + Voodoo2 90
    (3dfx scales far better than TNT in OpenGL), Pentium II 300 + TNT 80, Pentium III 450 + TNT2 125. The OpenGL goals
    with a card name are taken only when the card (bdgn_gl_renderer of ref_gl, or the adapter name) matches; the
    others are first proposals. 3.57n: K6-III / K6-3+ OpenGL 125, MEASURED there: 113 fps in default config, about 274
    at most (K6-3+ 616 MHz, GeForce2 GTS). */
 {"Pentium 4",NULL,"modern",125,125}, {"Pentium M",NULL,"modern",125,125}, {"Pentium D",NULL,"modern",125,125},
 {"Pentium Dual",NULL,"modern",125,125}, {"Pentium G",NULL,"modern",125,125}, {"Pentium Silver",NULL,"modern",125,125},
 {"Pentium E",NULL,"modern",125,125},
 {"K6-III",NULL,"K6-III / K6-3+",45,125}, {"K6-3",NULL,"K6-III / K6-3+",45,125},
 {"K6-2","Voodoo","K6-2 + 3dfx Voodoo",40,90}, {"K6 3D","Voodoo","K6-2 + 3dfx Voodoo",40,90},
 {"K6-2",NULL,"K6-2",40,60}, {"K6 3D",NULL,"K6-2",40,60}, {"K6",NULL,"K6",30,40},
 {"Pentium III",NULL,"Pentium III",60,125},
 {"Pentium II","TNT","Pentium II + TNT",40,80}, {"Pentium II",NULL,"Pentium II",40,80},
 {"Celeron",NULL,"Pentium II / Celeron",40,80},
 {"MMX",NULL,"Pentium MMX",40,30}, {"Pentium",NULL,"Pentium",30,25},
 {NULL,NULL,"modern",125,125}
};
static int BenchHas (const char *hay, const char *needle)
{
 int n=strlen(needle);
 for(;*hay;hay++) if(!Q_strncasecmp((char *)hay,(char *)needle,n)) return 1;
 return 0;
}
static int BenchGoal (const char **cls)
{
 char raw[160],cpu[160],gpu[256];
 extern void VID_AdapterName (char *out, int size);
 int i,soft=!Q_stricmp(Cvar_VariableString("vid_ref"),"soft");
 CL_CpuNameAny(raw,sizeof(raw)); CL_HwClean(raw,cpu,sizeof(cpu));
 VID_AdapterName(raw,sizeof(raw));
 Com_sprintf(gpu,sizeof(gpu),"%s | %s",Cvar_VariableString("bdgn_gl_renderer"),raw);
 for(i=0;bench_goals[i].pattern;i++) {
  if(!BenchHas(cpu,bench_goals[i].pattern)) continue;
  if(bench_goals[i].gpu && (soft || !BenchHas(gpu,bench_goals[i].gpu))) continue;
  break;
 }
 *cls=bench_goals[i].cls;
 if(Cvar_VariableValue("bench_goal")>0) { *cls="bench_goal"; return (int)Cvar_VariableValue("bench_goal"); }
 return soft?bench_goals[i].soft:bench_goals[i].gl;
}

/* BDGN 3.59g (owner 2026-11-21: "glide2x.dll kraschar hela tiden" on the Voodoo2): a driver crash used to end the
   benchmark with every case after it unmeasured. Before each run the case is written to bdgn_bench_resume.txt; a
   finished or stopped benchmark removes the file. When the same benchmark starts again and the file is there, the
   case that was running is logged as CRASH, skipped, and the run goes on with the next case (the cases before it
   are in the earlier tsv file). */
static void BenchResumePath (char *path, int size)
{
 Com_sprintf(path,size,"%s/bdgn_bench_resume.txt",FS_Gamedir());
}
static void BenchResumeSignature (char *sig, int size)
{
 Com_sprintf(sig,size,"%i %i %i %.40s",1,bench_case_count,bench_repeats,bench_case_count>0?bench_cases[0].label:"");
}
static void BenchResumeWrite (int id, int repeat)
{
 char path[MAX_OSPATH],sig[96];
 FILE *f;
 BenchResumePath(path,sizeof(path));
 BenchResumeSignature(sig,sizeof(sig));
 f=fopen(path,"w");
 if(!f) return;
 fprintf(f,"%s\n%i %i\n",sig,id,repeat);
 fclose(f);
}
static void BenchResumeClear (void)
{
 char path[MAX_OSPATH];
 BenchResumePath(path,sizeof(path));
 remove(path);
}
static void BenchResumeCheck (void)
{
 char path[MAX_OSPATH],sig[96],line[128];
 int id=-1,repeat=0,n;
 FILE *f;
 BenchResumePath(path,sizeof(path));
 f=fopen(path,"r");
 if(!f) return;
 BenchResumeSignature(sig,sizeof(sig));
 if(fgets(line,sizeof(line),f)) {
  n=(int)strlen(line);
  while(n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
  if(!strcmp(line,sig) && fgets(line,sizeof(line),f) && sscanf(line,"%i %i",&id,&repeat)==2 && id>=0 && id<bench_case_count) {
   BenchNote("CRASH",va("%s / repeat %i: the game stopped here in the last run (driver or game crash); the case is skipped",bench_cases[id].label,repeat+1));
   BenchNote("RESUMED",va("from case %i of %i; the cases before it are in the earlier tsv file",id+2,bench_case_count));
   bench_next=(id+1)*bench_repeats;
  }
 }
 fclose(f);
 remove(path);
}

static void CL_BenchFinish (const char *reason)
{
 int i,j,n,restart;
 double sum,min,max;
 if(!bench_phase) return;
 BenchResumeClear();	/* BDGN 3.59g */
 bench_phase=0; bench_cur=-1; bench_modern=0;
 Cvar_SetValue("bdgn_bench_active",0); Cvar_SetValue("bdgn_bench_eof",0);
 BenchReloadCapture();
 BenchRestore(); CL_CheckFullbright();
 {	/* BDGN 3.58r: every saved setting as it was before the benchmark (presets read during it changed more than the
	   benchmark's own list); the renderer is put back just below */
  static const char *skip[]={"vid_ref","gl_driver",NULL};
  int n=Cvar_SnapshotRestore(skip);
  if(n && bench_log) BenchNote("RESTORED_EXTRA",va("%i settings outside the benchmark list put back",n));
 }
 restart=BenchReloadChanges();

#ifdef _WIN32
 if(bench_minigl_ini_saved) {
  bench_minigl_ini_saved=false;
  if(!VID_WriteMtGlOptions(bench_minigl_dll,bench_minigl_sse,bench_minigl_gdi)) {
   BenchNote("RESTORE_ERROR","could not restore mt_gl.ini");
   if(!Q_stricmp(Cvar_VariableString("gl_driver"),"mtgl32") ||
      !Q_stricmp(Cvar_VariableString("gl_driver"),"mtgl32.dll"))
    Cvar_Set("gl_driver","opengl32");
  }
  restart|=1;
 }

#endif
 if(bench_log) {
  for(i=0;i<bench_case_count;i++) {
   sum=0;min=1e30;max=0;n=0;
   for(j=0;j<bench_repeats;j++) if(bench_done[i][j]) {
    double f=bench_fps[i][j]; sum+=f;if(f<min)min=f;if(f>max)max=f;n++;
   }
   if(n) {
    fprintf(bench_log,"SUMMARY\t%i\t%i\t",i,n);BenchField(bench_cases[i].label);
    fprintf(bench_log,"\t\t\t%.6f\t%.6f\t%.6f\t%s\n",n==bench_repeats?BenchMeanFps(i):sum/n,min,max,n==bench_repeats?"complete":"partial");
   }
  }
  {	/* BDGN 3.57m: goal of this computer and which cases reach it */
   const char *cls; int goal=BenchGoal(&cls), reached=0, done=0; double best=0; int bi=-1;
   for(i=0;i<bench_case_count;i++) {
    double f;
    if(!BenchCaseComplete(i)) continue;
    f=BenchMeanFps(i); done++;
    if(f>=goal) reached++;
    if(f>best) { best=f; bi=i; }
    fprintf(bench_log,"GOAL_CASE\t%i\t",i); BenchField(bench_cases[i].label);
    fprintf(bench_log,"\t%.2f\t%i\t%s\n",f,goal,f>=goal?"REACHED":"below");
   }
   fprintf(bench_log,"GOAL\t%s\t%s\t%i fps\t%i of %i cases reach it\tbest %.2f fps\t",cls,Cvar_VariableString("vid_ref"),goal,reached,done,best);
   BenchField(bi>=0?bench_cases[bi].label:"-"); fprintf(bench_log,"\n");
   Com_Printf("bdgn_bench: goal %i fps (%s, %s): %i of %i cases reach it, best %.1f fps\n",goal,cls,Cvar_VariableString("vid_ref"),reached,done,best);
  }
  BenchFpsLosses();
  BenchFrameTimeSummary();	/* BDGN 3.52v */

  BenchSnapshot("RESTORED",-1,0); BenchNote("END",reason); fclose(bench_log);bench_log=NULL;
 }
 if(bench_all && bench_all_started && bench_all_next<bench_all_count) {	/* BDGN 3.52t: this renderer's line in the overview */
  benchall_t *r=&bench_all_res[bench_all_next];
  r->base=bench_case_count>0?BenchMeanFps(0):0; r->fastest=0; r->best=0; r->best_label[0]=0;
  for(i=0;i<bench_case_count;i++) {
   double f;
   if(!BenchCaseComplete(i)) continue;
   f=BenchMeanFps(i);
   if((!strncmp(bench_cases[i].label,"ALL RETAINED",12) || bench_cases[i].setting==-9) && f>r->fastest)
    r->fastest=f;
   if(f>r->best){r->best=f;Com_sprintf(r->best_label,sizeof(r->best_label),"%s",bench_cases[i].label);}
  }
  Com_sprintf(r->status,sizeof(r->status),"%s",reason); Com_sprintf(r->logname,sizeof(r->logname),"%s",bench_logname); r->done=!strcmp(reason,"complete");
  BenchAllLine("DONE",VID_BenchRefName(bench_all_ref[bench_all_next]),va("BASELINE %.2f fps | fastest preset or bundle %.2f fps | best case %.2f fps (%s) | %s | log %s",
   r->base,r->fastest,r->best,r->best_label,r->status,r->logname));	/* BDGN 3.53: written at once (a crash later kept nothing) */
  if(!strcmp(reason,"cancelled")) bench_all_cancel=1;
 }
 for(i=0;i<bench_nvars;i++) { Z_Free(bench_saved[i]);bench_saved[i]=NULL; }
 bench_nvars=0;
 BenchQueueReload(restart);
 M_RefreshMusic();
 Com_Printf("bdgn_bench: %s; settings restored. Log: %s\n",reason,bench_logname);
}
/* CL_Drop is also used by SV_InitGame during normal demo startup.
 * Only Com_Error may classify a benchmark interruption as an error. */
void CL_BenchError (int code, const char *message)
{
 char info[32];
 if (!bench_phase) return;
 if (code == ERR_DISCONNECT && bench_phase == 3 && Cvar_VariableValue("bdgn_bench_eof")) return;
 Com_sprintf(info,sizeof(info),"%i",code); BenchNote("ERROR_CODE",info);
 BenchNote("ERROR_DETAIL",message);
 CL_BenchFinish(code == ERR_DISCONNECT ? "unexpected disconnect" : "error/drop");
}
static void BenchAddCase (int setting,int choice,const char *label)
{
 benchcase_t *c;
 if(bench_case_count>=BENCH_MAX_CASES) Com_Error(ERR_DROP,"benchmark case limit");
 if(bench_count_only) { bench_case_count++; return; }
 c=&bench_cases[bench_case_count++];c->setting=setting;c->choice=choice;
 Com_sprintf(c->label,sizeof(c->label),"%s",label);
}
static int bench_category_count;
#include "bench_modern.h"


static void BenchAddAudioCases (void)
{
 BenchAddCase(BENCH_AUDIO,11,"AUDIO / 11 kHz");
 BenchAddCase(BENCH_AUDIO,8,"AUDIO / 8 kHz");
 BenchAddCase(BENCH_AUDIO,4,"AUDIO / 4 kHz");
 BenchAddCase(BENCH_AUDIO,22,"AUDIO / 22 kHz");
 BenchAddCase(BENCH_AUDIO,0,"AUDIO / sound and music off");
}

static int BenchCurrentSmp (void)
{
 const char *ref=Cvar_VariableString("vid_ref");
 if (!Q_stricmp((char *)ref,"soft"))
  return bench_force_smp || Cvar_VariableValue("sw_smp") > 0;
 return (!Q_stricmp((char *)ref,"gl") || !Q_stricmp((char *)ref,"glam")) &&
        !Q_stricmp(Cvar_VariableString("gl_driver"),"mtgl32");
}

static void BenchBuildCases (void)
{
 /* BDGN 3.60c: the presets (and on a non-SMP renderer the audio cases); the settings sweep is gone */
 int i;
 static const char *preset_labels[]={NULL,"PRESET / Fast","PRESET / Faster","PRESET / Ultra Fast",
  "PRESET / Default","PRESET / MAX Fidelity","PRESET / PSYCHO Fast"};
 bench_case_count=0;bench_category_count=0;
 if(bench_modern) { ModernBenchBuild(); return; }
 if(Cvar_VariableValue("bdgn_extra_lowram") && !Q_stricmp(Cvar_VariableString("vid_ref"),"soft")) {
  BenchAddCase(-9,71,"PRESET / lowerThen32mb");
 }
 for(i=0;i<(int)(sizeof(bench_preset_order)/sizeof(bench_preset_order[0]));i++)
  BenchAddCase(-9,bench_preset_order[i] == 6 ? 65 : bench_preset_order[i],preset_labels[bench_preset_order[i]]);
 BenchAddCase(-9,72,"PRESET / EXTREME (owner config)"); /* 3.60i: same profile as the toggle, every renderer */
 if(!bench_smp_presets) BenchAddAudioCases();
}

/* Count the active renderer's actual plan without writing cases or starting it. */
static int BenchPreviewCases (int smp)
{
 int result, old_count=bench_case_count, old_categories=bench_category_count, old_smp=bench_smp_presets;

 bench_smp_presets=smp;
 bench_count_only=true;
 BenchBuildCases();
 result=bench_case_count;
 bench_count_only=false;
 bench_case_count=old_count; bench_category_count=old_categories;
 bench_smp_presets=old_smp;
 return result;
}

int CL_BenchmarkMenuRuns (int type, const char *driver, qboolean *estimated)
{
 /* BDGN 3.60c: the release menu has one benchmark, "Quick: my presets" (the presets of the renderer in use) */
 int i, count, cases, preset_cases, quick_cases, refs[BENCH_ALL_MAX];

 *estimated=false;
 if(CL_BenchmarkRunning()) return -1;
 if(type!=BM_ALL_QUICK) return 0;
 count=VID_BenchRefList(refs,BENCH_ALL_MAX,NULL,0,(driver && !strcmp(driver,"presets"))?3:0,driver);
 if(!count) return 0;
 preset_cases=BenchPreviewCases(1);
 quick_cases=BenchPreviewCases(0);
 cases=0;
 for(i=0;i<count;i++) cases+=(refs[i]==-1 || VID_MtGlTargetForRef(refs[i])[0])?preset_cases:quick_cases;
 return cases*3;
}

/* BDGN 3.52h: every retained ON choice (tweak rows and benchmark cfg rows), one category or all (NULL) */
static void CL_ApplyRetainedSet (const char *category)
{
 int i,choice;
 for(i=0;i<bdgn_tweak_count;i++) {
  if((bdgn_tweaks[i].flags&TK_MENUONLY) || (category && strcmp(category,bdgn_tweaks[i].category))) continue;
  if(bdgn_tweaks[i].flags&(TK_3DNOW|TK_SSE|TK_MMX)) continue;	/* automatic (CL_3DNowEnforce, Q3D_AutoSIMD) */
  choice=bdgn_tweaks[i].on;
  while(choice>=0 && !BenchAllowed(i,choice,false)) choice--;
  if(choice>=0) BDGN_TweakSet(i,choice);
 }
 for(i=0;i<BENCH_CFG_COUNT;i++) {
  if(!BenchCfgAllowed(i) || (bench_cfg[i].flags&(BC_CURRENT|BC_SOFT))) continue;
  if(category && strcmp(category,bench_cfg[i].category)) continue;
  BenchCfgSet(i,bench_cfg[i].on);
 }
 Cvar_SetValue("cl_gun",1);	/* never hide the view weapon */
}
/* BDGN 3.52h: the fastest benchmark result ("ALL RETAINED ON / DRAWFLAT ON / SOUND ON / LOW TEX") as one function,
   used by bdgn_bench (final two cases) and by every ultra preset (command bdgn_fastest in the cfg). A new fps option
   that gets an ON choice in the tweak table or bench_config.h is therefore in the ultra presets automatically.
   drawflat 1 = single colours (r_drawflat 2, the fastest case); 0 = textures with fullbright world.
   The low fidelity texture pack (fs_lowtextures 1) is always on here (BDGN OpenGL; ignored by other renderers). */
void CL_ApplyRetained (int drawflat)
{
 CL_ApplyRetainedSet(NULL);
 M_SetSoundMode(1);Cvar_SetValue("s_musicmode",0);Cvar_SetValue("cd_nocd",0);Cvar_SetValue("s_nosound",0);
 Cvar_SetValue("fs_lowtextures",1);
 Cvar_SetValue("r_drawflat",drawflat?2:0);
 Cvar_SetValue("gl_fastworld",drawflat?0:1);
 Cvar_SetValue("cl_gun",1);
 CL_CheckFullbright();
}
static void CL_Fastest_f (void)
{
 CL_ApplyRetained(1);
 Com_Printf("BDGN: fastest settings applied (ultra preset; bdgn_bench \"ALL RETAINED ON / DRAWFLAT ON\")\n");
}

static qboolean BenchApplyCaseBody (int id)
{
 /* BDGN 3.60c: two kinds of case are left: a preset (-9) and a sound quality (BENCH_AUDIO) */
 const benchcase_t *c=&bench_cases[id];
 int i,software_smp;
 BenchRestore();
 bench_shadow_case=false;
 if(c->setting==BENCH_MODERN) { ModernBenchApply(c->choice); return true; }
 software_smp=(int)Cvar_VariableValue("sw_smp");
 Cvar_SetValue("s_gl_audio_worker",0);
 Cvar_SetValue("bdgn_no3dnow",0);
 Cvar_SetValue("bdgn_nosse",0);
 Cvar_SetValue("bdgn_nommx",0);
 if(c->setting==-9) {
  if(!VID_ApplyPresetSettings(c->choice,false)) return false;
  if(bench_smp_presets && !Q_stricmp(Cvar_VariableString("vid_ref"),"soft"))
   Cvar_SetValue("sw_smp",bench_force_smp ? 1 : software_smp);
  Cvar_SetValue("r_profile",0);
  Cvar_SetValue("timedemo",1); Cvar_SetValue("paused",0); Cvar_Set("nextserver","");
  BenchGuards();
  return true;
 }
 Cvar_Set("r_profile","0");
 Cvar_Set("cfg_preset","default"); /* stable original-effect reference on every CPU */
 for(i=0;i<bdgn_tweak_count;i++) if(!(bdgn_tweaks[i].flags&TK_MENUONLY) && bdgn_tweaks[i].baseline>=0) BDGN_TweakSet(i,bdgn_tweaks[i].baseline);
 for(i=0;i<BENCH_CFG_COUNT;i++) if(BenchCfgAllowed(i)) BenchCfgSet(i,bench_cfg[i].reference);
 Cvar_SetValue("s_nosound",0);Cvar_SetValue("s_midieffects",0);Cvar_SetValue("s_pcspeaker",0);
 Cvar_SetValue("s_initsound",1);Cvar_SetValue("s_musicmode",0);
 Cvar_SetValue("s_musicdefault",1);Cvar_SetValue("s_musicsource",1);Cvar_SetValue("cd_nocd",0);
 Cvar_SetValue("cl_weaponlights",1);
 Cvar_SetValue("cpu_auto",0); Cvar_SetValue("r_fullbright",0);
 Cvar_SetValue("sw_smp",0);
 Cvar_SetValue("gl_hack_notextures",0); Cvar_SetValue("gl_swapinterval",0);
 Cvar_SetValue("paused",0); Cvar_SetValue("timedemo",1); Cvar_Set("nextserver","");
 Cvar_SetValue("cl_weaponsprites",0);Cvar_SetValue("cl_weapon_simple",0);Cvar_SetValue("cl_weaponsprite_anim",2);	/* BDGN 3.51q: 3D weapon reference */
 if(c->setting==BENCH_AUDIO) {
  if(c->choice) M_SetSoundQuality(c->choice==4?4:c->choice==8?0:c->choice==11?1:2);
  else {
   Cvar_SetValue("s_initsound",0);
   Cvar_SetValue("s_nosound",1);
   Cvar_SetValue("s_musicmode",2);
   Cvar_SetValue("cd_nocd",1);
  }
 }
 if(bench_force_smp) Cvar_SetValue("sw_smp",1);
 BenchGuards();CL_CheckFullbright();
 return true;
}

/* Internal command executes AFTER any renderer restart, outside the timed demo. */
static void CL_BenchRun_f (void)
{
 char command[MAX_QPATH+32],info[128];
 int real_width,real_height,rate;
 const benchcase_t *c;
 if(bench_phase!=2) return;
 c=&bench_cases[bench_cur];
 if((c->setting==BENCH_AUDIO) && cl_sound_restart_pending) {
  bench_phase=4;bench_launch_ms=Sys_Milliseconds();
  return;
 }
 Cvar_SetValue("timedemo",1); /* Every run, including repeats after renderer restarts. */
 BenchGuards();
 if((bench_all && !BenchAllRendererMatches())) {
  CL_BenchFinish("renderer changed during benchmark");
  return;
 }
 
 
 VID_GetRealSize(&real_width,&real_height);
 Com_sprintf(info,sizeof(info),"%i x %i",real_width,real_height);BenchNote("RUN_RESOLUTION",info);
 if(!Q_stricmp(Cvar_VariableString("vid_ref"),"soft") && Cvar_VariableValue("sw_bdgn_features")>=3525) {
  Com_sprintf(info,sizeof(info),"%i x %i",(int)Cvar_VariableValue("sw_output_width"),
   (int)Cvar_VariableValue("sw_output_height"));BenchNote("RUN_OUTPUT_RESOLUTION",info);
 }
 
 BenchNote("RUN_GL_COLOR_BITS",Cvar_VariableString("bdgn_gl_colorbits"));
 BenchNote("RUN_GL_MSAA_PIXEL_FORMAT",Cvar_VariableString("bdgn_gl_msaa_samples"));
 
 BenchNote("RUN_GL_SWAP_INTERVAL",Cvar_VariableString("bdgn_gl_swapinterval"));
 {	/* BDGN 3.58n: what the 3dfx drivers were given as they loaded (they read vsync from the environment) */
  const char *e=getenv("FX_GLIDE_SWAPINTERVAL");
  if(e) BenchNote("RUN_GLIDE_SWAPINTERVAL",va("%s (FX_GLIDE / SST / SSTV2 / SSTH3_SWAPINTERVAL at the renderer load)%s",e,
   VID_GlideVsyncOn()?" - vsync ON":""));
 }
 S_BenchmarkInfo(info,sizeof(info));BenchNote("RUN_AUDIO",info);
#ifdef _WIN32
 
#endif
 
 
 
 if(c->setting==BENCH_AUDIO) {
  rate=c->choice==4?4000:c->choice==8?8000:c->choice==11?11025:c->choice==22?22050:0;
  if(S_BenchmarkRate()!=rate) {
   BenchNote("SKIP",va("%s: expected %i Hz, active %i Hz",c->label,rate,S_BenchmarkRate()));
   fflush(bench_log);
   bench_next=(bench_cur+1)*bench_repeats;
   bench_phase=1;
   return;
  }
 }
 BenchSnapshot("SETTING",bench_cur,bench_repeat+1);fflush(bench_log);
 if(ferror(bench_log)) { CL_BenchFinish("log write error");return; }
#ifdef _WIN32
 

#endif
 if(bench_stage_log) {
  BenchNote("DRIVER_STAGE",va("%s / repeat %i: starting demo",c->label,bench_repeat+1));	/* also bench_stage_log */
  fflush(bench_log);
  bench_first_frame=false;
 }
 bench_music_frames=bench_music_active_frames=0;
 bench_phase=3;bench_launch_ms=Sys_Milliseconds();
 /* BDGN 3.51att: DUKE_STATS / WEAPON_STATS count this run only (they used to add up over all cases) */
 if(Cmd_Exists("duke_stats"))Cbuf_AddText("duke_stats reset\n");
 if(Cmd_Exists("weapon_stats"))Cbuf_AddText("weapon_stats zero\n");
 if(Cmd_Exists("dlight_stats"))Cbuf_AddText("dlight_stats zero\n");	/* BDGN 3.52 (WESTBECK) measurement build */
 Com_sprintf(command,sizeof(command),"timedemo 1\ndemomap %s\n",bench_demo);Cbuf_AddText(command);
}
static void CL_BenchLaunch (int id,int repeat)
{
 int reload;
 int preset_switch = repeat==0 && (bench_cases[id].setting==-9);
#ifdef _WIN32
 
 
#endif
 BenchReloadCapture();
 bench_cur=id;bench_repeat=repeat; Cvar_SetValue("bdgn_bench_eof",0);
 if(!BenchApplyCaseBody(id)) { CL_BenchFinish("preset or MTGL32 configuration unavailable"); return; }
 bench_phase=2;reload=BenchReloadChanges();
 BenchClearUnchangedModified();	/* 3.59v: no renderer restart for a value that ended where it was */
 if(preset_switch) reload|=3;
 
 Com_Printf("bdgn_bench %i/%i, repeat %i/%i: %s\n",id+1,bench_case_count,repeat+1,bench_repeats,bench_cases[id].label);
 
 if(bench_stage_log) {
  BenchNote("DRIVER_STAGE",va("%s / repeat %i: preparing, reload flags=%i",bench_cases[id].label,repeat+1,reload));
  fflush(bench_log);
 }
 BenchResumeWrite(id,repeat);	/* BDGN 3.59g */
 BenchLogPendingCase(id,repeat);
 if(ferror(bench_log)) { CL_BenchFinish("log write error"); return; }
 BenchQueueReload(reload);Cbuf_AddText("bdgn_bench_run\n");
}
/* Refuse a native or simpler-filter fallback as a result for a requested mode. */

static qboolean BenchScaleResultMatches(const benchcase_t *c)
{
 int width,height,outwidth,outheight,percent;
 
 {return true;}
 VID_GetRealSize(&width,&height);
 {
  percent=c->choice;
  outwidth=(int)Cvar_VariableValue("sw_output_width");
  outheight=(int)Cvar_VariableValue("sw_output_height");
  if(Q_stricmp(Cvar_VariableString("vid_ref"),"soft") || Cvar_VariableValue("sw_render_scale")!=percent) return false;
 }
 return outwidth>0 && outheight>0 && width==RenderScaleWidth(outwidth,percent) &&
  height==RenderScaleHeight(outheight,percent);
}

void CL_BenchResult (double fps)
{
 if(bench_phase!=3 || bench_cur<0 || fps<=0 || cl.timedemo_frames<=0 || !Cvar_VariableValue("bdgn_bench_eof")) return;

 if(!BenchScaleResultMatches(&bench_cases[bench_cur])) {
  BenchNote("SKIP",va("case %i %s: requested scale/filter did not remain active",
    bench_cur,bench_cases[bench_cur].label));
  Com_Printf("bdgn_bench: %s fell back; skipping remaining repetitions.\n",bench_cases[bench_cur].label);
  bench_next=(bench_cur+1)*bench_repeats;
  bench_cur=-1;bench_phase=1;
  return;
 }
#ifdef _WIN32

 

#endif
 bench_fps[bench_cur][bench_repeat]=fps;bench_done[bench_cur][bench_repeat]=1;
 if(bench_cases[bench_cur].setting==BENCH_AUDIO) {
  char music[96];
  Com_sprintf(music,sizeof(music),"case %i/%i: WAV/MP3 open=%i, MIDI open=%i, CD device open=%i",
   bench_cur,bench_repeat+1,!WavMusic_IsIdle(),!MIDI_IsIdle(),!CDAudio_IsIdle());
  BenchNote("RUN_MUSIC_OUTPUT",music);
 }
#ifdef _WIN32

#endif

 fprintf(bench_log,"RESULT\t%i\t%i\t",bench_cur,bench_repeat+1);BenchField(bench_cases[bench_cur].label);
 fprintf(bench_log,"\t\t%.6f\t\t\t\t\n",fps);
 if(!Q_stricmp(Cvar_VariableString("vid_ref"),"gl")) {
  char scale[128];
  Com_sprintf(scale,sizeof(scale),"%ix%i, path %i, filter %i",
   (int)Cvar_VariableValue("gl_render_width"),(int)Cvar_VariableValue("gl_render_height"),
   (int)Cvar_VariableValue("gl_render_scale_path"),(int)Cvar_VariableValue("gl_render_scale_filter_active"));
  BenchNote("GL_RENDER_SCALE",scale);
 }
 {	/* BDGN 3.51q: sprite cache / fallback counters and map load time of this run (counted from this map load) */
  char note[64];
  if(Cmd_Exists("duke_stats")){Cmd_ExecuteString("duke_stats quiet");Com_sprintf(note,sizeof(note),"DUKE_STATS %i/%i",bench_cur,bench_repeat+1);BenchNote(note,Cvar_VariableString("r_duke_stats"));}
  if(Cmd_Exists("weapon_stats")){Cmd_ExecuteString("weapon_stats quiet");Com_sprintf(note,sizeof(note),"WEAPON_STATS %i/%i",bench_cur,bench_repeat+1);BenchNote(note,Cvar_VariableString("r_weapon_stats"));}
  if(Cmd_Exists("dlight_stats")){Cmd_ExecuteString("dlight_stats quiet");Com_sprintf(note,sizeof(note),"DLIGHT_STATS %i/%i",bench_cur,bench_repeat+1);BenchNote(note,Cvar_VariableString("r_dlight_stats"));}	/* BDGN 3.52 (WESTBECK) */
  Com_sprintf(note,sizeof(note),"LOAD_MS %i/%i",bench_cur,bench_repeat+1);BenchNote(note,Cvar_VariableString("bdgn_load_ms"));
  Com_sprintf(note,sizeof(note),"LOAD_PHASES %i/%i",bench_cur,bench_repeat+1);BenchNote(note,Cvar_VariableString("bdgn_load_phases"));	/* BDGN 3.59f */
 }
 {	/* BDGN 3.52v: frame time statistics of this run (CL_TimedemoStats) */
  char line[256],note[64];
  Com_sprintf(note,sizeof(note),"FRAMETIME %i/%i",bench_cur,bench_repeat+1);
  if(cl_ft_result_ok) { bench_ft[bench_cur][bench_repeat]=cl_ft_result; CL_FrameTimeFormat(line,sizeof(line),&cl_ft_result); BenchNote(note,line); }
  else { memset(&bench_ft[bench_cur][bench_repeat],0,sizeof(frametime_t)); BenchNote(note,"no frame times (CPU without RDTSC or too few frames)"); }
 }

 fflush(bench_log);
 Com_Printf("bdgn_bench: %.3f fps\n",fps);
 if(ferror(bench_log)) { CL_BenchFinish("log write error");return; }
 bench_cur=-1;bench_phase=1;
}
static void BenchAllStep (void);
static const char *BenchAllCommand (void)
{
 return "bdgn_bench_presets";
}
static void CL_BenchFrame (void)
{
 int id,repeat;
 if(bench_all && !bench_phase) { BenchAllStep(); return; }	/* BDGN 3.52t */
 if(!bench_phase || cls.state!=ca_disconnected) return;
 if(bench_phase==4) {
  if(!cl_sound_restart_pending) { bench_phase=2; CL_BenchRun_f(); }
  else if(Sys_Milliseconds()-bench_launch_ms>15000) CL_BenchFinish("sound restart timed out");
  return;
 }
 if(bench_phase==3) {
  if(Sys_Milliseconds()-bench_launch_ms>(15000)) CL_BenchFinish("demo did not start / timed out");
  return;
 }
 if(bench_phase!=1) return;
 if(bench_next>=bench_case_count*bench_repeats) { CL_BenchFinish("complete");return; }
 id=bench_next/bench_repeats;repeat=bench_next%bench_repeats;bench_next++;
 CL_BenchLaunch(id,repeat);
}
static qboolean BenchDemoValid(const char *demo)
{
 const char *p;
 size_t len=strlen(demo);
 if(len<5 || len>=MAX_QPATH || strstr(demo,"..") || Q_stricmp((char *)demo+len-4,".dm2")) return false;
 for(p=demo;*p;p++)
  if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||
       *p=='_'||*p=='-'||*p=='.'||*p=='/')) return false;
 return true;
}
static void CL_Bench_f (void)
{
 static const char *extra[]={"timedemo","paused","nextserver","gl_swapinterval","r_fullbright","gl_hack_notextures","cfg_preset",
 "s_nosound","s_midieffects","s_pcspeaker","s_initsound","s_musicmode","s_musicdefault","s_musicsource","s_mididevice","s_musicvolume","cd_nocd",
 "r_drawentities","cl_entities","r_drawworld","r_norefresh","crosshair","viewsize",
 "cl_testentities","cl_testparticles","cl_testlights","cl_testblend","cl_weaponlights",
 "bdgn_no3dnow","bdgn_no3dnow_ext","bdgn_nosse","bdgn_nommx","bdgn_cpu_path","gl_render_scale","sw_render_scale","gl_upscale_filter",
 "gl_render_scale_legacy","gl_render_scale_pixelcopy","gl_render_scale_copyformat",
 "sw_mode","sw_smp_threads","sw_filter_threads","vid_ref","gl_driver","vid_rendition","s_gl_audio_worker","gl_ztrick",NULL};
 static const char *context[]={"vid_ref","gl_driver","gl_mode","vid_fullscreen","gl_picmip","cl_maxfps","cl_gun","cl_particles","s_khz","s_mono","s_loadas8bit","s_maxchannels","cl_simpleenemy_anim","cl_simpleenemy_lod","r_duke_res_avail","cl_weaponsprites","cl_weapon_simple",NULL};
 const char *demo=Cmd_Argc()==2?Cmd_Argv(1):"demo1.dm2";
 FILE *exists;int i,real_width,real_height;char info[128];
 if(bench_phase) {Com_Printf("benchmark already running; bdgn_bench_stop cancels\n");return;}

 

#ifdef _WIN32
 bench_minigl_ini_saved=false;

#endif

 bench_modern=!strcmp(Cmd_Argv(0),"bdgn_bench_modern");
 if(bench_modern && (!Cvar_VariableValue("bdgn_gl_environment_capable") || Q_stricmp(Cvar_VariableString("vid_ref"),"gl"))) {
  Com_Printf("Modern graphics benchmark requires a compatible native OpenGL driver.\n"); bench_modern=0; return;
 }
 bench_smp_presets=BenchCurrentSmp();

 bench_repeats=3;
 if(Cmd_Argc()>2 || !BenchDemoValid(demo)) {Com_Printf("usage: bdgn_bench [demo1.dm2]\n");return;}
 if(!bench_modern && (!FS_LowPackAvailable(true) || !FS_LowPackAvailable(false))) {
  Com_Printf("bdgn_bench requires lowtex-small.pak and pak7-lowsounds.pak in the loaded game search path. Restart the game after installing them.\n");return;
 }
 Com_sprintf(bench_demo,sizeof(bench_demo),"%s",demo);
 do {
  Com_sprintf(bench_logname,sizeof(bench_logname),"%s/bdgn_bench_%u_%i.tsv",FS_Gamedir(),(unsigned)Sys_Milliseconds(),++bench_file_serial);
  exists=fopen(bench_logname,"r");if(exists)fclose(exists);
 } while(exists);
 bench_log=fopen(bench_logname,"w");
 if(!bench_log) {Com_Printf("could not create benchmark log: %s\n",bench_logname);return;}
 if(bench_all) bench_all_started=1;
 Cvar_SnapshotTake(20000);	/* BDGN 3.58r: every saved setting, put back by CL_BenchFinish (or the copy bdgn_bench_sw_all
	took just before it switched the renderer) */
 Cvar_Get("bdgn_bench_active","0",0); Cvar_Get("bdgn_bench_eof","0",0);
 Cvar_Get("s_gl_audio_worker","0",CVAR_ARCHIVE);
 CL_CheckFullbright();bench_nvars=0;
 for(i=0;i<bdgn_tweak_count;i++) {
  Cvar_Get((char *)bdgn_tweaks[i].cvar,(char *)bdgn_tweaks[i].default_value,CVAR_ARCHIVE);
  BenchSave(bdgn_tweaks[i].cvar);
  if(bdgn_tweaks[i].aux) {Cvar_Get((char *)bdgn_tweaks[i].aux,bdgn_tweaks[i].kind==3?"0":"1",CVAR_ARCHIVE);BenchSave(bdgn_tweaks[i].aux);}
 }
 BenchSave("cl_lowend_sprites");
 VID_SaveBenchmarkPresetCvars(BenchSave);
 for(i=0;extra[i];i++) BenchSave(extra[i]);
 for(i=0;i<(int)BENCH_CFG_COUNT;i++) if(bench_cfg[i].name[0]!='@') BenchSave(bench_cfg[i].name);
 Cvar_Get("r_profile","0",0); BenchSave("r_profile");
 Cvar_Set("r_profile","0");	/* BDGN 3.52t: the renderer counts only in the extended benchmark */
 memset(bench_prof,0,sizeof(bench_prof)); memset(bench_prof_frame,0,sizeof(bench_prof_frame));
 memset(bench_ft,0,sizeof(bench_ft)); memset(bench_cnt,0,sizeof(bench_cnt)); memset(bench_cnt_ok,0,sizeof(bench_cnt_ok));	/* 3.52v */
 fprintf(bench_log,"type\tcase\trepeat\tvariable_or_case\tvalue\tfps\tmean\tmin\tmax\tnote\n");
 BenchNote("VERSION",BDGN_VERSION_STRING);
 BenchNote("PERMANENT_FAST_EFFECTS","on; independent of CPU and preset");
 BenchNote("BENCH_PROFILE",bench_modern?"modern graphics, skies, isolated cases and combinations":bench_smp_presets?"SMP presets only":"presets and audio");
 BenchNote("BENCH_TYPE","standard: 3 runs per case");
 BenchNote("NOTE",bench_modern?"Current output mode retained. Caustics need an underwater demo; smoke needs visible puffs; reflections need visible reflective materials. Cases with no relevant content can measure only overhead.":"The presets on the active renderer, with audio cases when applicable.");BenchNote("DEMO",bench_demo);
 VID_GetRealSize(&real_width,&real_height);
 Com_sprintf(info,sizeof(info),"%i x %i",real_width,real_height);BenchNote("RESOLUTION",info);
 BenchNote("MACHINE_LABEL",Cvar_VariableString("bdgn_bench_label"));
 if(bench_all && bench_all_next<bench_all_count)	/* BDGN 3.53: the overview shows which renderer was running */
  BenchAllLine("START",VID_BenchRefName(bench_all_ref[bench_all_next]),bench_logname);
 if(bench_all && bench_all_next<bench_all_count)	/* BDGN 3.52t */
  BenchNote("ALL_RENDERERS",va("renderer %i/%i: %s (%s); overview %s",bench_all_next+1,bench_all_count,VID_BenchRefName(bench_all_ref[bench_all_next]),"quick: my presets",bench_all_logname));
#ifdef NDEBUG
 BenchNote("BUILD","Release");
#else
 BenchNote("BUILD","DEBUG - results are not release performance");
#endif
 BenchNote("CPU_3DNOW",Q3E_CPUHas3DNow()?"yes":"no");BenchNote("CPU_SSE",Q3E_CPUHasSSE()?"yes":"no");BenchNote("CPU_MMX",Q3E_CPUHasMMX()?"yes":"no");
 {	/* BDGN 3.51p: CPU identity, SSE2 and processors for comparing machines (TO DO 5 Nov) */
  q3e_cpuid_raw_t raw;q3e_cpuinfo_t cpu;char line[256];int nsys,nproc;
  Q3E_ReadCPUID(&raw);Q3E_DecodeCPU(&raw,&cpu);Q3E_DescribeCPU(&cpu,line,sizeof(line));BenchNote("CPU_ID",line);
  BenchNote("CPU_PROFILE",va("%s (cpu_profile %s)",Cvar_VariableString("bdgn_cpu_profile"),Cvar_VariableString("cpu_profile")));	/* 3.58w */
  BenchNote("CPU_SSE2",Q3E_CPUHasSSE2()?"yes":"no");
  Sys_ProcessorCounts(&nsys,&nproc);Com_sprintf(line,sizeof(line),"%i in Windows, %i for the game",nsys,nproc);BenchNote("PROCESSORS",line);
  Sys_OSDescription(line,sizeof(line));BenchNote("OS",line);	/* BDGN 3.51p: OS and OpenGL driver (TO DO 5 Nov) */
  BenchNote("GL_VENDOR",Cvar_VariableString("bdgn_gl_vendor")[0]?Cvar_VariableString("bdgn_gl_vendor"):"n/a (not ref_gl)");
  BenchNote("GL_RENDERER",Cvar_VariableString("bdgn_gl_renderer")[0]?Cvar_VariableString("bdgn_gl_renderer"):"n/a (not ref_gl)");
  BenchNote("GL_VERSION",Cvar_VariableString("bdgn_gl_version")[0]?Cvar_VariableString("bdgn_gl_version"):"n/a (not ref_gl)");
  if(!Q_stricmp(Cvar_VariableString("gl_driver"),"mtgl32") &&
     (!Q_stricmp(Cvar_VariableString("vid_ref"),"gl") || !Q_stricmp(Cvar_VariableString("vid_ref"),"glam"))) {
   const char *target=Cvar_VariableString("bdgn_mtgl_target");
   const char *sse=Cvar_VariableString("bdgn_mtgl_sse");
   const char *gdi=Cvar_VariableString("bdgn_mtgl_gdi");
   BenchNote("MTGL_TARGET",target[0]?target:"default");
   BenchNote("MTGL_USE_SSE",sse[0]?sse:"default");
   BenchNote("MTGL_DO_GDI_CALLS",gdi[0]?gdi:"default");
  }
 }
 BenchNote("WARMUP","Complete-case means exclude run 1. Min and max include every run.");
 for(i=0;context[i];i++) {
  fprintf(bench_log,"CONTEXT\t-1\t0\t");BenchField(context[i]);fputc('\t',bench_log);BenchField(Cvar_VariableString((char *)context[i]));fprintf(bench_log,"\t\t\t\t\t\n");
 }
 BenchNote("NOTE","VSync forced off for measurements; saved settings restored. Driver safety fallbacks remain enabled.");
 BenchNote("NOTE","3DNow!, SSE and MMX are automatic (3.52): every case runs with them as the game uses them. REFERENCE / 3DNow! off and REFERENCE / SSE off show what each gives; MMX is used only when textures load (no fps effect). Unsupported CPU/renderer choices are SKIP records.");
 BenchNote("SIMD",va("3DNow! %s, SSE %s, MMX %s (r_simd_*: 1 on, 0 not supported, 2 self-test failed, 3 not used because of 3DNow!)",Cvar_VariableString("r_simd_3dnow"),Cvar_VariableString("r_simd_sse"),Cvar_VariableString("r_simd_mmx")));
 BenchNote("RAM",va("%s MB, low RAM %s",Cvar_VariableString("bdgn_ram_mb"),Cvar_VariableValue("bdgn_lowram")?"yes (low RAM presets listed)":"no"));
 BenchNote("VRAM",va("%s KB, %s KB free (saved by bdgn_vramcheck, primary display; 0 = not read); vid_lowvram %s",Cvar_VariableString("bdgn_vram_kb"),Cvar_VariableString("bdgn_vram_free_kb"),Cvar_VariableString("vid_lowvram")));	/* 3.59s */
 BenchNote("CACHES",va("gl_weapon_texcache %s, r_duke_cache %s",Cvar_VariableString("gl_weapon_texcache"),Cvar_VariableString("r_duke_cache")));
 BenchNote("CLASSIC",va("renderer sprite support %s, 32x32 library %s; 3.52h: automatic 2 directions beyond 1024 and solid sprites batched per atlas page (ref_gl; DUKE_STATS: batched/batches); 2D weapon always 6.25/s. Removed: weapon occlusion, 64x64 library",
  Cvar_VariableString("r_duke_anim_support"),((int)Cvar_VariableValue("r_duke_res_avail")&1)?"installed":"not installed"));
 BenchNote("NOTE","Historical K6 exclusions apply only to unchanged individual settings; merged effects and new sprite paths are tested afresh. Unsupported hardware choices are logged as SKIP.");
 
 BenchNote("NOTE","The diagnostic sweep also tests categories after BASELINE CHECK. Final two use low textures and sound ON, first fullbright then drawflat.");
 BenchNote("NOTE","Audio cases share one reference. Music uses the demo soundtrack if installed. Audio OFF disables music and sound output. Display/compatibility changes are individual tests only. HUD, crosshair, enemies and world remain visible. Config strings/fractions are tested as actual values.");
 BenchNote("NOTE","Shadows forced OFF in every run, including baseline/categories/finals. This differs from historical benchmarks with shadows ON in the control.");
 BenchNote("LOW_TEXTURE_PACK",FS_LowPackAvailable(true)?"loaded":"missing");BenchNote("LOW_SOUND_PACK",FS_LowPackAvailable(false)?"loaded":"missing");
 BenchNote("NOTE","Playback may not exercise steam, flies or other effects. Server-side gib/fly spawning is not measured by a recorded demo.");
 BenchNote("NOTE","PRESET cases use the same settings as the video menu, with the renderer and display mode held fixed. Other cases use the controlled reference. Saved settings are restored.");
 BenchSnapshot("SAVED",-1,0);BenchBuildCases();
 Com_sprintf(info,sizeof(info),"%i cases x %i repeats = %i runs",bench_case_count,bench_repeats,bench_case_count*bench_repeats);BenchNote("PLAN",info);
 memset(bench_done,0,sizeof(bench_done));bench_next=0;bench_cur=-1;bench_phase=1;
 {	/* BDGN 3.59g: stage lines on 3dfx drivers, and resume after a crash */
  const char *drv=Cvar_VariableString("gl_driver");
  bench_stage_log=Q_stricmp(Cvar_VariableString("vid_ref"),"soft") && (strstr(drv,"3dfx") || strstr(drv,"glide") || strstr(drv,"Glide") ||
   strstr(drv,"3DFX") || strstr(drv,"Wicked") || strstr(drv,"wicked") ||
   !Q_stricmp(drv,"mtgl32"));	/* 3.59n: also the SMP wrapper (SMP WickedGL stopped in its first Ultra Fast run, dual P2, v3.59h) */
  BenchResumeCheck();
 }
 Cvar_SetValue("bdgn_bench_active",1); Cvar_SetValue("bdgn_bench_eof",0);
 fflush(bench_log);
 if(ferror(bench_log)) {CL_BenchFinish("log write error");return;}
 Com_Printf("bdgn_bench%s: %i cases x %i repeats = %i demos. bdgn_bench_stop cancels. Log: %s\n","",bench_case_count,bench_repeats,bench_case_count*bench_repeats,bench_logname);
 if(cls.state!=ca_disconnected) {
  if(Com_ServerState()) SV_Shutdown("Starting benchmark\n",false);
  CL_Disconnect();
 }
}

static void BenchAllFinish (const char *reason);
static void CL_BenchStop_f (void)
{
 if(bench_all) { bench_all_cancel=1; if(!bench_phase) { BenchAllFinish("cancelled"); return; } }	/* BDGN 3.52t */
 if(!bench_phase) return;
 CL_BenchFinish("cancelled");
 if(cls.state!=ca_disconnected) Cbuf_AddText("disconnect\n");
}
/* ===== BDGN 3.52t: all-renderers benchmark ===== */
static void BenchAllLine (const char *type, const char *a, const char *b)
{
 const char *p;
 if(!bench_all_log) return;
 for(p=type;*p;p++)fputc(*p=='\t'?' ':*p,bench_all_log); fputc('\t',bench_all_log);
 for(p=a;*p;p++)fputc(*p=='\t'?' ':*p,bench_all_log); fputc('\t',bench_all_log);
 for(p=b;*p;p++)fputc((*p=='\t'||*p=='\n')?' ':*p,bench_all_log); fputc('\n',bench_all_log); fflush(bench_all_log);
}
static void BenchAllFinish (const char *reason)
{
 int i,j,order[BENCH_ALL_MAX],n=0,rank=0;
 if(!bench_all) return;
 bench_all=0; bench_force_smp=0;
 for(i=0;i<bench_all_count;i++) {
  benchall_t *r=&bench_all_res[i];
  BenchAllLine("RENDERER",VID_BenchRefName(bench_all_ref[i]),r->done?va("%s | BASELINE %.2f fps | fastest preset or bundle %.2f fps | best case %.2f fps (%s) | %s | log %s",
   r->renderer,r->base,r->fastest,r->best,r->best_label,r->status,r->logname):(r->status[0]?r->status:"not run"));
  if(r->done && r->fastest>0) order[n++]=i;
 }
 for(i=0;i<n;i++) for(j=i+1;j<n;j++) if(bench_all_res[order[j]].fastest>bench_all_res[order[i]].fastest){int t=order[i];order[i]=order[j];order[j]=t;}
 for(i=0;i<n;i++) {
  benchall_t *r=&bench_all_res[order[i]];
  char num[16],tail[64];
  Com_sprintf(num,sizeof(num),"%i",++rank);
  if(i) Com_sprintf(tail,sizeof(tail)," (%.1f%% vs the fastest)",100*(r->fastest-bench_all_res[order[0]].fastest)/bench_all_res[order[0]].fastest);
  else Com_sprintf(tail,sizeof(tail)," <- fastest on this computer");
  BenchAllLine("RANKING",num,va("%s: fastest %.2f fps, BASELINE %.2f fps%s",VID_BenchRefName(bench_all_ref[order[i]]),r->fastest,r->base,tail));
 }
 for(i=0;i<(int)BENCH_ALL_CVARS;i++) Cvar_Set((char *)bench_all_cvars[i],bench_all_saved[i]);
 if(bench_all_mtgl_changed && !VID_WriteMtGlOptions(bench_all_mtgl,bench_all_mtgl_sse,bench_all_mtgl_gdi)) {
  BenchAllLine("RESTORE_ERROR","MTGL","could not restore mt_gl.ini");
  if(!Q_stricmp(bench_all_saved[1],"mtgl32") || !Q_stricmp(bench_all_saved[1],"mtgl32.dll"))
   Cvar_Set("gl_driver","opengl32");
 }
 
 BenchAllLine("END",reason,"");
 if(bench_all_log){fclose(bench_all_log);bench_all_log=NULL;}
 Cbuf_AddText("vid_restart\n");
 Com_Printf("%s: %s. Overview: %s\n",BenchAllCommand(),reason,bench_all_logname);
 
}
static void BenchAllStep (void)
{
 benchall_t *r;
 if(cls.state!=ca_disconnected) return;
 if(bench_all_cancel) { BenchAllFinish("cancelled"); return; }
 if(bench_all_state==1) {	/* switch to the next renderer */
  int ref;
  if(bench_all_next>=bench_all_count) { BenchAllFinish("complete"); return; }
  ref=bench_all_ref[bench_all_next];
  Cvar_Set("gl_bitdepth",bench_all_saved[2]);
  Cvar_SetValue("gl_swapinterval",0); /* Glide reads it as the renderer loads. */
  if(!VID_BenchSelectRef(ref)) {
   BenchAllLine("SKIP",VID_BenchRefName(ref),"could not configure mt_gl.ini");
   bench_all_next++;
   return;
  }
  Com_sprintf(bench_all_driver,sizeof(bench_all_driver),"%s",Cvar_VariableString("gl_driver"));
  Com_sprintf(bench_all_target,sizeof(bench_all_target),"%s",VID_MtGlTargetForRef(ref));
  if(bench_all_target[0]) bench_all_mtgl_changed=1;
  bench_force_smp=ref==-1;
  Cvar_SetValue("sw_smp",ref==-1?1:0);
  Com_sprintf(bench_all_expect,sizeof(bench_all_expect),"%s",Cvar_VariableString("vid_ref"));
  Com_Printf("%s: renderer %i/%i: %s\n",BenchAllCommand(),
   bench_all_next+1,bench_all_count,VID_BenchRefName(ref));
  BenchAllLine("SWITCH",VID_BenchRefName(ref),va("%s / %s %s",bench_all_expect,bench_all_driver,bench_all_target));
  Cbuf_AddText("vid_restart\n");
  bench_all_ms=Sys_Milliseconds(); bench_all_started=0; bench_all_state=2;
  return;
 }
 r=&bench_all_res[bench_all_next];
 if(bench_all_state==2) {	/* the renderer restarts; then check that it is really the one asked for */
  if(Sys_Milliseconds()-bench_all_ms<3000) return;
  if(!BenchAllRendererMatches()) {
   Com_sprintf(r->status,sizeof(r->status),"SKIP: requested %s/%s %s, running %s/%s %s GDI=%s",
    bench_all_expect,bench_all_driver,bench_all_target,Cvar_VariableString("vid_ref"),
    Cvar_VariableString("gl_driver"),Cvar_VariableString("bdgn_mtgl_target"),Cvar_VariableString("bdgn_mtgl_gdi"));
   BenchAllLine("SKIP",VID_BenchRefName(bench_all_ref[bench_all_next]),r->status);
   bench_all_next++; bench_all_state=1; return;
  }
  Com_sprintf(r->renderer,sizeof(r->renderer),"vid_ref %s, gl_driver %s%s%s",Cvar_VariableString("vid_ref"),Cvar_VariableString("gl_driver"),
   Cvar_VariableString("bdgn_gl_renderer")[0]&&Q_stricmp(Cvar_VariableString("vid_ref"),"soft")&&Q_stricmp(Cvar_VariableString("vid_ref"),"swam")?", ":"",
   Q_stricmp(Cvar_VariableString("vid_ref"),"soft")&&Q_stricmp(Cvar_VariableString("vid_ref"),"swam")?Cvar_VariableString("bdgn_gl_renderer"):(bench_force_smp?", sw_smp 1":""));

  Cbuf_AddText(va("bdgn_bench %s\n",bench_all_demo));
  bench_all_ms=Sys_Milliseconds(); bench_all_state=3;
  return;
 }
 if(bench_all_state==3) {	/* the benchmark of this renderer has ended (CL_BenchFinish filled r) or never started */
  if(!bench_all_started) {
   if(Sys_Milliseconds()-bench_all_ms<5000) return;
   Com_sprintf(r->status,sizeof(r->status),"SKIP: benchmark did not start");
  }
  bench_all_next++; bench_all_state=1;
 }
}
static void CL_BenchAllStart (void)
{
 /* BDGN 3.60c: only "Quick: my presets" is left: the renderer and driver in use, every preset, 3 runs each */
 char skipped[1024],line[1024];int i,nsys=1,nproc=1;
 const char *demo=Cmd_Argc()>1?Cmd_Argv(1):"demo1.dm2";
 if(bench_phase || bench_all) { Com_Printf("benchmark already running; bdgn_bench_stop cancels\n"); return; }
 if(!BenchDemoValid(demo)) {Com_Printf("invalid benchmark demo name\n");return;}
 Com_sprintf(bench_all_demo,sizeof(bench_all_demo),"%s",demo);
 if(!FS_LowPackAvailable(true) || !FS_LowPackAvailable(false)) { Com_Printf("bdgn_bench_presets requires lowtex-small.pak and pak7-lowsounds.pak\n"); return; }
 bench_all_count=VID_BenchRefList(bench_all_ref,BENCH_ALL_MAX,skipped,sizeof(skipped),3,"");
 if(bench_all_count<=0) { Com_Printf("No benchmark renderer found. %s\n",skipped); return; }
 Com_sprintf(bench_all_logname,sizeof(bench_all_logname),"%s/%s_%u.tsv",FS_Gamedir(),"bdgn_bench_presets",(unsigned)Sys_Milliseconds());
 bench_all_log=fopen(bench_all_logname,"w");
 if(!bench_all_log) { Com_Printf("could not create %s\n",bench_all_logname); return; }
 memset(bench_all_res,0,sizeof(bench_all_res));
 for(i=0;i<(int)BENCH_ALL_CVARS;i++) Com_sprintf(bench_all_saved[i],sizeof(bench_all_saved[i]),"%s",Cvar_VariableString((char *)bench_all_cvars[i]));
 VID_GetMtGlOptions(bench_all_mtgl,bench_all_mtgl_sse,bench_all_mtgl_gdi,sizeof(bench_all_mtgl));
 bench_all_mtgl_changed=0;
 fprintf(bench_all_log,"type\titem\tnote\n");
 BenchAllLine("VERSION",BDGN_VERSION_STRING,"");
 BenchAllLine("MODE","quick: my presets","Default, Fast, Faster, Ultra Fast, PSYCHO Fast and MAX Fidelity, 3 runs each; audio cases on non-SMP");
 BenchAllLine("DEMO",bench_all_demo,"");
 {
  q3e_cpuid_raw_t raw;q3e_cpuinfo_t cpu;
  Q3E_ReadCPUID(&raw);Q3E_DecodeCPU(&raw,&cpu);Q3E_DescribeCPU(&cpu,line,sizeof(line));BenchAllLine("CPU",line,"");
 }
 Sys_ProcessorCounts(&nsys,&nproc); {char np[16];Com_sprintf(np,sizeof(np),"%i",nproc);Com_sprintf(line,sizeof(line),"%i in Windows",nsys);BenchAllLine("PROCESSORS",np,line);}
 BenchAllLine("HARDWARE",Cvar_VariableString("bdgn_hw"),"PCI scan bits (bdgn_hardware lists them)");
 line[0]=0; for(i=0;i<bench_all_count;i++){strcat(line,i?", ":"");strcat(line,VID_BenchRefName(bench_all_ref[i]));}
 BenchAllLine("PLAN",va("%i renderers",bench_all_count),line);
 if(skipped[0]) BenchAllLine("SKIP","excluded renderers",skipped);
 BenchAllLine("NOTE","","Each renderer starts with vid_restart; if it falls back to another renderer it is SKIP. Settings and your driver are restored at the end. bdgn_bench_stop cancels everything.");
 bench_all=1; bench_all_cancel=0; bench_all_next=0; bench_all_state=1; bench_all_started=0;
 Com_Printf("bdgn_bench_presets: %s\n",line);
 if(cls.state!=ca_disconnected) {
  if(Com_ServerState()) SV_Shutdown("Starting benchmark\n",false);
  CL_Disconnect();
 }
}

/* BDGN 3.60c: the one benchmark of the release menu: the presets (default to PSYCHO Fast and MAX Fidelity) on the
   renderer and driver in use, 3 runs each; the result shows which preset is fastest on this computer */
static void CL_BenchPresets_f (void)
{
 CL_BenchAllStart();
}

	// see CL_FASTFX() in client.h
cvar_t *cl_weaponlights;
cvar_t	*cl_fastfx;	// 1 = no blaster/hyperblaster/plasma/gib trails, no blaster dlights
cvar_t *cl_bloodfx;
cvar_t *cl_impactfx;
cvar_t *cl_static_rockets;
cvar_t *cl_static_bfg;
cvar_t *cl_notrails;
cvar_t *cl_sprite_trails;	/* BDGN 3.51ah (test) */
cvar_t *cl_sprite_hits;	/* BDGN 3.51ai (test) */
cvar_t *cl_nosteam;
cvar_t *cl_noitemrotate;
cvar_t *cl_nobanners;
cvar_t *cl_nodecorpses;
cvar_t *cl_nospawnpads;
cvar_t *cl_staticlightstyles;
cvar_t *cl_staticenemies, *cl_staticmap, *cl_simpleblaster;
cvar_t *cl_classic2d, *cl_simple_rail, *cl_lowend_sprites;
qboolean cl_fullbright_active;
static cvar_t *cl_fb_world, *cl_fb_all, *cl_fb_flat, *cl_fb_legacyflat;
cvar_t *cl_gibmode;
cvar_t	*cl_nosmoke;	// 1 = no smoke/flash puffs from muzzle flashes and bullet hits, no ricochet sounds

// doogies code
cvar_t  *cl_hack_nocinematics;
// end

// cvar so we can remember what combination of "s_loadas8bit" and "s_khz" we are using and load the proper menu settings
cvar_t	*s_profile;
cvar_t	*cfg_preset;		// "", "default", "fast", "ultra" or "ultrahigh": last config preset chosen in the video menu
// end

client_static_t	cls;
client_state_t	cl;

centity_t		cl_entities[MAX_EDICTS];

entity_state_t	cl_parse_entities[MAX_PARSE_ENTITIES];

extern	cvar_t *allow_download;
extern	cvar_t *allow_download_players;
extern	cvar_t *allow_download_models;
extern	cvar_t *allow_download_sounds;
extern	cvar_t *allow_download_maps;

//======================================================================

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
void CL_WriteDemoMessage (void)
{
	int		len, swlen;

	// the first eight bytes are just packet sequencing stuff
	len = net_message.cursize-8;
	swlen = LittleLong(len);
	fwrite (&swlen, 4, 1, cls.demofile);
	fwrite (net_message.data+8,	len, 1, cls.demofile);
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	int		len;

	if (!cls.demorecording)
	{
		Com_Printf ("Not recording a demo.\n");
		return;
	}

// finish up
	len = -1;
	fwrite (&len, 4, 1, cls.demofile);
	fclose (cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;
	Com_Printf ("Stopped demo.\n");
}

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
void CL_Record_f (void)
{
	char	name[MAX_OSPATH];
	char	buf_data[MAX_MSGLEN];
	sizebuf_t	buf;
	int		i;
	int		len;
	entity_state_t	*ent;
	entity_state_t	nullstate;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("record <demoname>\n");
		return;
	}

	if (cls.demorecording)
	{
		Com_Printf ("Already recording.\n");
		return;
	}

	if (cls.state != ca_active)
	{
		Com_Printf ("You must be in a level to record.\n");
		return;
	}

	//
	// open the demo file
	//
	Com_sprintf (name, sizeof(name), "%s/demos/%s.dm2", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf ("recording to %s.\n", name);
	FS_CreatePath (name);
	cls.demofile = fopen (name, "wb");
	if (!cls.demofile)
	{
		Com_Printf ("ERROR: couldn't open.\n");
		return;
	}
	cls.demorecording = true;

	// don't start saving messages until a non-delta compressed message is received
	cls.demowaiting = true;

	//
	// write out messages to hold the startup information
	//
	SZ_Init (&buf, buf_data, sizeof(buf_data));

	// send the serverdata
	MSG_WriteByte (&buf, svc_serverdata);
	MSG_WriteLong (&buf, PROTOCOL_VERSION);
	MSG_WriteLong (&buf, 0x10000 + cl.servercount);
	MSG_WriteByte (&buf, 1);	// demos are always attract loops
	MSG_WriteString (&buf, cl.gamedir);
	MSG_WriteShort (&buf, cl.playernum);

	MSG_WriteString (&buf, cl.configstrings[CS_NAME]);

	// configstrings
	for (i=0 ; i<MAX_CONFIGSTRINGS ; i++)
	{
		if (cl.configstrings[i][0])
		{
			if (buf.cursize + strlen (cl.configstrings[i]) + 32 > buf.maxsize)
			{	// write it out
				len = LittleLong (buf.cursize);
				fwrite (&len, 4, 1, cls.demofile);
				fwrite (buf.data, buf.cursize, 1, cls.demofile);
				buf.cursize = 0;
			}

			MSG_WriteByte (&buf, svc_configstring);
			MSG_WriteShort (&buf, i);
			MSG_WriteString (&buf, cl.configstrings[i]);
		}

	}

	// baselines
	memset (&nullstate, 0, sizeof(nullstate));
	for (i=0; i<MAX_EDICTS ; i++)
	{
		ent = &cl_entities[i].baseline;
		if (!ent->modelindex)
			continue;

		if (buf.cursize + 64 > buf.maxsize)
		{	// write it out
			len = LittleLong (buf.cursize);
			fwrite (&len, 4, 1, cls.demofile);
			fwrite (buf.data, buf.cursize, 1, cls.demofile);
			buf.cursize = 0;
		}

		MSG_WriteByte (&buf, svc_spawnbaseline);
		MSG_WriteDeltaEntity (&nullstate, &cl_entities[i].baseline, &buf, true, true);
	}

	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, "precache\n");

	// write it to the demo file

	len = LittleLong (buf.cursize);
	fwrite (&len, 4, 1, cls.demofile);
	fwrite (buf.data, buf.cursize, 1, cls.demofile);

	// the rest of the demo file will be individual frames
}

//======================================================================

/*
===================
Cmd_ForwardToServer

adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void Cmd_ForwardToServer (void)
{
	char	*cmd;

	cmd = Cmd_Argv(0);
	if (cls.state <= ca_connected || *cmd == '-' || *cmd == '+')
	{
		Com_Printf ("Unknown command \"%s\"\n", cmd);
		return;
	}

	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	SZ_Print (&cls.netchan.message, cmd);
	if (Cmd_Argc() > 1)
	{
		SZ_Print (&cls.netchan.message, " ");
		SZ_Print (&cls.netchan.message, Cmd_Args());
	}
}

void CL_Setenv_f( void )
{
	int argc = Cmd_Argc();

	if ( argc > 2 )
	{
		char buffer[1000];
		int i;

		/* BDGN 3.59f: bounded (the server can stuff setenv with long arguments) */
		Q_strncpyz_bdgn( buffer, Cmd_Argv(1), sizeof(buffer) );
		Q_strncatz_bdgn( buffer, "=", sizeof(buffer) );

		for ( i = 2; i < argc; i++ )
		{
			Q_strncatz_bdgn( buffer, Cmd_Argv( i ), sizeof(buffer) );
			Q_strncatz_bdgn( buffer, " ", sizeof(buffer) );
		}

		putenv( buffer );
	}
	else if ( argc == 2 )
	{
		char *env = getenv( Cmd_Argv(1) );

		if ( env )
		{
			Com_Printf( "%s=%s\n", Cmd_Argv(1), env );
		}
		else
		{
			Com_Printf( "%s undefined\n", Cmd_Argv(1), env );
		}
	}
}

/*
==================
CL_ForwardToServer_f
==================
*/
void CL_ForwardToServer_f (void)
{
	if (cls.state != ca_connected && cls.state != ca_active)
	{
		Com_Printf ("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}

	// don't forward the first argument
	if (Cmd_Argc() > 1)
	{
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		SZ_Print (&cls.netchan.message, Cmd_Args());
	}
}

/*
==================
CL_Pause_f
==================
*/
void CL_Pause_f (void)
{
	// never pause in multiplayer
	if (Cvar_VariableValue ("maxclients") > 1 || !Com_ServerState ())
	{
		Cvar_SetValue ("paused", 0);
		return;
	}

	Cvar_SetValue ("paused", !cl_paused->value);
}

/*
==================
CL_Quit_f
==================
*/
void CL_Quit_f (void)
{
	if (bench_phase) CL_BenchFinish("quit");
	CL_Disconnect ();
	Com_Quit ();
}

/*
================
CL_Drop

Disconnects the client after an error or a normal local server initialization.
================
*/
void CL_Drop (void)
{
	if (cls.state == ca_uninitialized)
		return;
	if (cls.state == ca_disconnected)
		return;

	CL_Disconnect ();

	// drop loading plaque unless this is the initial game start
	if (cls.disable_servercount != -1)
		SCR_EndLoadingPlaque ();	// get rid of loading plaque
}

/*
=======================
CL_SendConnectPacket

We have gotten a challenge from the server, so try and
connect.
======================
*/
void CL_SendConnectPacket (void)
{
	netadr_t	adr;
	int		port;

	if (!NET_StringToAdr (cls.servername, &adr))
	{
		Com_Printf ("Bad server address\n");
		cls.connect_time = 0;
		return;
	}
	if (adr.port == 0)
		adr.port = BigShort (PORT_SERVER);

	port = Cvar_VariableValue ("qport");
	userinfo_modified = false;

	Netchan_OutOfBandPrint (NS_CLIENT, adr, "connect %i %i %i \"%s\"\n",
		PROTOCOL_VERSION, port, cls.challenge, Cvar_Userinfo() );
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend (void)
{
	netadr_t	adr;

	// if the local server is running and we aren't
	// then connect
	if (cls.state == ca_disconnected && Com_ServerState() )
	{
		cls.state = ca_connecting;
		strncpy (cls.servername, "localhost", sizeof(cls.servername)-1);
		// we don't need a challenge on the localhost
		CL_SendConnectPacket ();
		return;
//		cls.connect_time = -99999;	// CL_CheckForResend() will fire immediately
	}

	// resend if we haven't gotten a reply yet
	if (cls.state != ca_connecting)
		return;

	if (cls.realtime - cls.connect_time < 3000)
		return;

	if (!NET_StringToAdr (cls.servername, &adr))
	{
		Com_Printf ("Bad server address\n");
		cls.state = ca_disconnected;
		return;
	}
	if (adr.port == 0)
		adr.port = BigShort (PORT_SERVER);

	cls.connect_time = cls.realtime;	// for retransmit requests

	Com_Printf ("Connecting to %s...\n", cls.servername);

	Netchan_OutOfBandPrint (NS_CLIENT, adr, "getchallenge\n");
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f (void)
{
	char	*server;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: connect <server>\n");
		return;
	}

	if (Com_ServerState ())
	{	// if running a local server, kill it and reissue
		SV_Shutdown (va("Server quit\n", msg), false);
	}
	else
	{
		CL_Disconnect ();
	}

	server = Cmd_Argv (1);

	NET_Config (true);		// allow remote

	CL_Disconnect ();

	cls.state = ca_connecting;
	strncpy (cls.servername, server, sizeof(cls.servername)-1);
	cls.connect_time = -99999;	// CL_CheckForResend() will fire immediately
}

/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
void CL_Rcon_f (void)
{
	char	message[1024];
	int		i;
	netadr_t	to;

	if (!rcon_client_password->string)
	{
		Com_Printf ("You must set 'rcon_password' before\n"
					"issuing an rcon command.\n");
		return;
	}

	message[0] = (char)255;
	message[1] = (char)255;
	message[2] = (char)255;
	message[3] = (char)255;
	message[4] = 0;

	NET_Config (true);		// allow remote

	/* BDGN 3.59f: bounded */
	Q_strncatz_bdgn (message, "rcon ", sizeof(message));

	Q_strncatz_bdgn (message, rcon_client_password->string, sizeof(message));
	Q_strncatz_bdgn (message, " ", sizeof(message));

	for (i=1 ; i<Cmd_Argc() ; i++)
	{
		Q_strncatz_bdgn (message, Cmd_Argv(i), sizeof(message));
		Q_strncatz_bdgn (message, " ", sizeof(message));
	}

	if (cls.state >= ca_connected)
		to = cls.netchan.remote_address;
	else
	{
		if (!strlen(rcon_address->string))
		{
			Com_Printf ("You must either be connected,\n"
						"or set the 'rcon_address' cvar\n"
						"to issue rcon commands\n");

			return;
		}
		NET_StringToAdr (rcon_address->string, &to);
		if (to.port == 0)
			to.port = BigShort (PORT_SERVER);
	}

	NET_SendPacket (NS_CLIENT, strlen(message)+1, message, to);
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	S_StopAllSounds ();
	CL_ClearEffects ();
	CL_ClearTEnts ();

// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));
	CL_ClearDecorationCache ();
	memset (&cl_entities, 0, sizeof(cl_entities));

	SZ_Clear (&cls.netchan.message);

}

/*
=====================
CL_Disconnect

Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Com_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
	byte	final[32];

	CL_LoadPhaseAbort ();	/* BDGN 3.59f: a load that ended in an error */
	CDAudio_Stop ();
	CDAudio_ReleaseDevice ();

	if (cls.state == ca_disconnected)
		return;

	if (cl_timedemo && cl_timedemo->value)
	{
		int	time;

		time = Sys_Milliseconds () - cl.timedemo_start;
		if (time > 0)
		{
			Com_Printf ("%i frames, %3.1f seconds: %3.1f fps\n", cl.timedemo_frames,
			time/1000.0, cl.timedemo_frames*1000.0 / time);
			CL_TimedemoStats (time);	/* BDGN 3.52v: min/max fps, 99% and worst frame time */
			CL_BenchResult (cl.timedemo_frames*1000.0 / time);		/* BDGN 3.42 */
		}
	}

	VectorClear (cl.refdef.blend);
	re.CinematicSetPalette(NULL);

	M_ForceMenuOff ();

	cls.connect_time = 0;

	SCR_StopCinematic ();

	if (cls.demorecording)
		CL_Stop_f ();

	// send a disconnect message to the server
	final[0] = clc_stringcmd;
	strcpy ((char *)final+1, "disconnect");
	Netchan_Transmit (&cls.netchan, strlen(final), final);
	Netchan_Transmit (&cls.netchan, strlen(final), final);
	Netchan_Transmit (&cls.netchan, strlen(final), final);

	CL_ClearState ();

	// stop download
	if (cls.download) {
		fclose(cls.download);
		cls.download = NULL;
	}

	cls.state = ca_disconnected;
}

void CL_Disconnect_f (void)
{
	if (bench_phase) CL_BenchStop_f();
	Com_Error (ERR_DROP, "Disconnected from server");
}

/*
====================
CL_Packet_f

packet <destination> <contents>

Contents allows \n escape character
====================
*/
void CL_Packet_f (void)
{
	char	send[2048];
	int		i, l;
	char	*in, *out;
	netadr_t	adr;

	if (Cmd_Argc() != 3)
	{
		Com_Printf ("packet <destination> <contents>\n");
		return;
	}

	NET_Config (true);		// allow remote

	if (!NET_StringToAdr (Cmd_Argv(1), &adr))
	{
		Com_Printf ("Bad address\n");
		return;
	}
	if (!adr.port)
		adr.port = BigShort (PORT_SERVER);

	in = Cmd_Argv(2);
	out = send+4;
	send[0] = send[1] = send[2] = send[3] = (char)0xff;

	l = strlen (in);
	for (i=0 ; i<l ; i++)
	{
		if (in[i] == '\\' && in[i+1] == 'n')
		{
			*out++ = '\n';
			i++;
		}
		else
			*out++ = in[i];
	}
	*out = 0;

	NET_SendPacket (NS_CLIENT, out-send, send, adr);
}

/*
=================
CL_Changing_f

Just sent as a hint to the client that they should
drop to full console
=================
*/
void CL_Changing_f (void)
{
	//ZOID
	//if we are downloading, we don't change!  This so we don't suddenly stop downloading a map
	if (cls.download)
		return;

	CDAudio_Stop ();
	CDAudio_ReleaseDevice ();
	SCR_BeginLoadingPlaque ();
	cls.state = ca_connected;	// not active anymore, but not disconnected
	Com_Printf ("\nChanging map...\n");
}

/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
void CL_Reconnect_f (void)
{
	//ZOID
	//if we are downloading, we don't change!  This so we don't suddenly stop downloading a map
	if (cls.download)
		return;

	CDAudio_Stop ();
	CDAudio_ReleaseDevice ();
	S_StopAllSounds ();
	if (cls.state == ca_connected) {
		Com_Printf ("reconnecting...\n");
		cls.state = ca_connected;
		MSG_WriteChar (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message, "new");
		return;
	}

	if (*cls.servername) {
		if (cls.state >= ca_connected) {
			CL_Disconnect();
			cls.connect_time = cls.realtime - 1500;
		} else
			cls.connect_time = -99999; // fire immediately

		cls.state = ca_connecting;
		Com_Printf ("reconnecting...\n");
	}
}

/*
=================
CL_ParseStatusMessage

Handle a reply from a ping
=================
*/
void CL_ParseStatusMessage (void)
{
	char	*s;

	s = MSG_ReadString(&net_message);

	Com_Printf ("%s\n", s);
	M_AddToServerList (net_from, s);
}

/*
=================
CL_PingServers_f
=================
*/
void CL_PingServers_f (void)
{
	int			i;
	netadr_t	adr;
	char		name[32];
	char		*adrstring;
	cvar_t		*noudp;
	cvar_t		*noipx;

	NET_Config (true);		// allow remote

	// send a broadcast packet
	Com_Printf ("pinging broadcast...\n");

	noudp = Cvar_Get ("noudp", "0", CVAR_NOSET);
	if (!noudp->value)
	{
		adr.type = NA_BROADCAST;
		adr.port = BigShort(PORT_SERVER);
		Netchan_OutOfBandPrint (NS_CLIENT, adr, va("info %i", PROTOCOL_VERSION));
	}

	noipx = Cvar_Get ("noipx", "0", CVAR_NOSET);
	if (!noipx->value)
	{
		adr.type = NA_BROADCAST_IPX;
		adr.port = BigShort(PORT_SERVER);
		Netchan_OutOfBandPrint (NS_CLIENT, adr, va("info %i", PROTOCOL_VERSION));
	}

	// send a packet to each address book entry
	for (i=0 ; i<16 ; i++)
	{
		Com_sprintf (name, sizeof(name), "adr%i", i);
		adrstring = Cvar_VariableString (name);
		if (!adrstring || !adrstring[0])
			continue;

		Com_Printf ("pinging %s...\n", adrstring);
		if (!NET_StringToAdr (adrstring, &adr))
		{
			Com_Printf ("Bad address: %s\n", adrstring);
			continue;
		}
		if (!adr.port)
			adr.port = BigShort(PORT_SERVER);
		Netchan_OutOfBandPrint (NS_CLIENT, adr, va("info %i", PROTOCOL_VERSION));
	}
}

/*
=================
CL_Skins_f

Load or download any custom player skins and models
=================
*/
void CL_Skins_f (void)
{
	int		i;

	for (i=0 ; i<MAX_CLIENTS ; i++)
	{
		if (!cl.configstrings[CS_PLAYERSKINS+i][0])
			continue;
		Com_Printf ("client %i: %s\n", i, cl.configstrings[CS_PLAYERSKINS+i]);
		SCR_UpdateScreen ();
		Sys_SendKeyEvents ();	// pump message loop
		CL_ParseClientinfo (i);
	}
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/

/*
=======================================================================
BDGN 3.59f: chat ignore list (as Q2PRO) and demo controls. Client only: nothing is sent to the server.
=======================================================================
*/
cvar_t	*cl_ignore, *cl_demospeed;
#define CL_IGNORE_MAX	32
static char	cl_ignore_nick[CL_IGNORE_MAX][64], cl_ignore_text[CL_IGNORE_MAX][64];
static int	cl_ignore_nicks, cl_ignore_texts;

static int CL_LowerEq (int a, int b)
{
	a &= 127; b &= 127;
	if (a >= 'A' && a <= 'Z') a += 32;
	if (b >= 'A' && b <= 'Z') b += 32;
	return a == b;
}
static int CL_PrefixI (const char *s, const char *p)
{
	for (; *p; s++, p++)
		if (!*s || !CL_LowerEq (*s, *p))
			return 0;
	return 1;
}
static int CL_ContainsI (const char *s, const char *p)
{
	for (; *s; s++)
		if (CL_PrefixI (s, p))
			return 1;
	return 0;
}
/* svc_print with PRINT_CHAT: "name: text" or "(name): text" */
qboolean CL_ChatIgnored (const char *msg)
{
	int i, n;
	if (!cl_ignore || !cl_ignore->value)
		return false;
	for (i = 0; i < cl_ignore_nicks; i++)
	{
		const char *m = msg;
		n = (int)strlen (cl_ignore_nick[i]);
		if (*m == '(')
			m++;
		if (CL_PrefixI (m, cl_ignore_nick[i]) && (m[n] == ':' || (m[n] == ')' && m[n + 1] == ':')))
			return true;
	}
	for (i = 0; i < cl_ignore_texts; i++)
		if (CL_ContainsI (msg, cl_ignore_text[i]))
			return true;
	return false;
}
static void CL_IgnoreList (char list[][64], int *count, const char *what, int add, int remove)
{
	char	arg[64];
	int		i;
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("%i %s ignored\n", *count, what);
		for (i = 0; i < *count; i++)
			Com_Printf ("  %s\n", list[i]);
		return;
	}
	Q_strncpyz_bdgn (arg, Cmd_Args (), sizeof(arg));
	if (arg[0] == '"')	/* "two words" */
	{
		int l;
		memmove (arg, arg + 1, strlen (arg));
		l = (int)strlen (arg);
		if (l && arg[l - 1] == '"') arg[l - 1] = 0;
	}
	for (i = 0; i < *count; i++)
		if (!Q_stricmp (list[i], arg) || (remove && !Q_stricmp (arg, "all")))
		{
			if (add)
			{
				Com_Printf ("%s is already ignored\n", arg);
				return;
			}
			memmove (list[i], list[i + 1], (*count - i - 1) * 64);
			(*count)--;
			i--;
		}
	if (add)
	{
		if (*count >= CL_IGNORE_MAX)
		{
			Com_Printf ("Ignore list is full (%i)\n", CL_IGNORE_MAX);
			return;
		}
		Q_strncpyz_bdgn (list[(*count)++], arg, 64);
		Com_Printf ("Ignoring %s %s\n", what, arg);
	}
}
static void CL_Ignore_f (void)		{ CL_IgnoreList (cl_ignore_nick, &cl_ignore_nicks, "players", Cmd_Argc () >= 2, 0); }
static void CL_Unignore_f (void)	{ CL_IgnoreList (cl_ignore_nick, &cl_ignore_nicks, "players", 0, 1); }
static void CL_IgnoreText_f (void)	{ CL_IgnoreList (cl_ignore_text, &cl_ignore_texts, "texts", Cmd_Argc () >= 2, 0); }
static void CL_UnignoreText_f (void)	{ CL_IgnoreList (cl_ignore_text, &cl_ignore_texts, "texts", 0, 1); }

/* Demo controls. Speed and seek scale the engine clock (timescale) only while a .dm2 plays, never in timedemo or the
   benchmark; the old timescale comes back when the demo ends. Backward seek restarts the demo and runs ahead to the
   time (Q2PRO keeps snapshots; this needs no memory, but restarting costs one map load). */
extern const char *SV_DemoName (void);
extern int SV_DemoPercent (void);
extern int sv_demopaused;
extern cvar_t *timescale;
static float	cl_demo_prevscale = -1;
static int		cl_demo_seek = -1, cl_demo_start = -1, cl_demo_count = -1, cl_demo_restarting;
static char		cl_demo_name[MAX_QPATH];

static int CL_DemoTime (void)
{
	return (cl_demo_start < 0) ? 0 : cl.frame.servertime - cl_demo_start;
}
void CL_DemoControlFrame (void)
{
	const char	*name = SV_DemoName ();
	float		want;

	if (!name || !cl.attractloop || cl_timedemo->value)
	{
		if (!cl_demo_restarting)
		{
			if (cl_demo_prevscale >= 0)
			{
				Cvar_SetValue ("timescale", cl_demo_prevscale);
				cl_demo_prevscale = -1;
			}
			cl_demo_seek = -1;
			cl_demo_start = -1;
		}
		return;
	}
	cl_demo_restarting = 0;
	Q_strncpyz_bdgn (cl_demo_name, name, sizeof(cl_demo_name));
	if (cls.state != ca_active)
		return;
	if (cl_demo_count != cl.servercount || cl_demo_start < 0)
	{
		cl_demo_count = cl.servercount;
		cl_demo_start = cl.frame.servertime;
	}
	if (cl_demo_seek >= 0 && CL_DemoTime () >= cl_demo_seek)
	{
		Com_Printf ("demoseek: at %i:%02i\n", CL_DemoTime () / 60000, (CL_DemoTime () / 1000) % 60);
		cl_demo_seek = -1;
	}
	want = (cl_demo_seek >= 0) ? 50.0f : (cl_demospeed->value > 0 ? cl_demospeed->value / 100.0f : 1.0f);
	if (want != timescale->value)
	{
		if (cl_demo_prevscale < 0)
		{
			if (want == 1.0f)
				return;
			cl_demo_prevscale = timescale->value;
		}
		Cvar_SetValue ("timescale", want);
	}
}
static void CL_DemoPause_f (void)
{
	if (!SV_DemoName ()) { Com_Printf ("No demo is playing\n"); return; }
	sv_demopaused = !sv_demopaused;
	Com_Printf (sv_demopaused ? "Demo paused\n" : "Demo playing\n");
}
static void CL_DemoInfo_f (void)
{
	if (!SV_DemoName ()) { Com_Printf ("No demo is playing\n"); return; }
	Com_Printf ("%s: %i:%02i, %i%% of the file, speed %i%%%s\n", SV_DemoName (), CL_DemoTime () / 60000,
		(CL_DemoTime () / 1000) % 60, SV_DemoPercent (), (int)cl_demospeed->value, sv_demopaused ? ", paused" : "");
}
/* demoseek <m:ss> (from the start) or demoseek +<s> / -<s> */
static void CL_DemoSeek_f (void)
{
	char	*a;
	int		t, now = CL_DemoTime ();
	if (!SV_DemoName () || cls.state != ca_active) { Com_Printf ("No demo is playing\n"); return; }
	if (Cmd_Argc () != 2) { Com_Printf ("Usage: demoseek <m:ss> | +<seconds> | -<seconds>\n"); return; }
	a = Cmd_Argv (1);
	if (*a == '+' || *a == '-')
		t = now + atoi (a) * 1000;
	else if (strchr (a, ':'))
		t = (atoi (a) * 60 + atoi (strchr (a, ':') + 1)) * 1000;
	else
		t = atoi (a) * 1000;
	if (t < 0) t = 0;
	sv_demopaused = 0;
	cl_demo_seek = t;
	if (t < now)
	{
		cl_demo_restarting = 1;
		cl_demo_start = -1;
		Cbuf_AddText (va ("demomap \"%s\"\n", cl_demo_name));
	}
}
void CL_InitChatDemo (void)
{
	cl_ignore = Cvar_Get ("cl_ignore", "1", CVAR_ARCHIVE);
	cl_demospeed = Cvar_Get ("cl_demospeed", "100", CVAR_ARCHIVE);
	Cmd_AddCommand ("ignore", CL_Ignore_f);
	Cmd_AddCommand ("unignore", CL_Unignore_f);
	Cmd_AddCommand ("ignoretext", CL_IgnoreText_f);
	Cmd_AddCommand ("unignoretext", CL_UnignoreText_f);
	Cmd_AddCommand ("demopause", CL_DemoPause_f);
	Cmd_AddCommand ("demoseek", CL_DemoSeek_f);
	Cmd_AddCommand ("demoinfo", CL_DemoInfo_f);
}

/* BDGN 3.59f (as R1Q2/Q2PRO): connection replies are only taken from the server we are connecting to.
   In 3.21 any host could send "challenge" or "client_connect" while connecting and take over the connection. */
static qboolean CL_FromConnectServer (void)
{
	netadr_t adr;
	if (!cls.servername[0] || !NET_StringToAdr (cls.servername, &adr))
		return false;
	if (adr.port == 0)
		adr.port = BigShort (PORT_SERVER);
	return NET_CompareAdr (net_from, adr);
}

void CL_ConnectionlessPacket (void)
{
	char	*s;
	char	*c;

	MSG_BeginReading (&net_message);
	MSG_ReadLong (&net_message);	// skip the -1

	s = MSG_ReadStringLine (&net_message);

	Cmd_TokenizeString (s, false);

	c = Cmd_Argv(0);

	Com_Printf ("%s: %s\n", NET_AdrToString (net_from), c);

	// server connection
	if (!strcmp(c, "client_connect"))
	{
		if (cls.state == ca_connected)
		{
			Com_Printf ("Dup connect received.  Ignored.\n");
			return;
		}
		if (cls.state != ca_connecting || !CL_FromConnectServer ())	/* BDGN 3.59f */
		{
			Com_Printf ("client_connect from wrong address.  Ignored.\n");
			return;
		}
		Netchan_Setup (NS_CLIENT, &cls.netchan, net_from, cls.quakePort);
		MSG_WriteChar (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message, "new");
		cls.state = ca_connected;
		return;
	}

	// server responding to a status broadcast
	if (!strcmp(c, "info"))
	{
		CL_ParseStatusMessage ();
		return;
	}

	// remote command from gui front end
	if (!strcmp(c, "cmd"))
	{
		if (!NET_IsLocalAddress(net_from))
		{
			Com_Printf ("Command packet from remote host.  Ignored.\n");
			return;
		}
		Sys_AppActivate ();
		s = MSG_ReadString (&net_message);
		Cbuf_AddText (s);
		Cbuf_AddText ("\n");
		return;
	}
	// print command from somewhere
	if (!strcmp(c, "print"))
	{
		s = MSG_ReadString (&net_message);
		Com_Printf ("%s", s);
		return;
	}

	// ping from somewhere
	if (!strcmp(c, "ping"))
	{
		Netchan_OutOfBandPrint (NS_CLIENT, net_from, "ack");
		return;
	}

	// challenge from the server we are connecting to
	if (!strcmp(c, "challenge"))
	{
		if (cls.state != ca_connecting || !CL_FromConnectServer ())	/* BDGN 3.59f */
		{
			Com_Printf ("challenge from wrong address.  Ignored.\n");
			return;
		}
		cls.challenge = atoi(Cmd_Argv(1));
		CL_SendConnectPacket ();
		return;
	}

	// echo request from server
	if (!strcmp(c, "echo"))
	{
		if (!CL_FromConnectServer ())	/* BDGN 3.59f: no echo reflection to third parties */
			return;
		Netchan_OutOfBandPrint (NS_CLIENT, net_from, "%s", Cmd_Argv(1) );
		return;
	}

	Com_Printf ("Unknown command.\n");
}

/*
=================
CL_DumpPackets

A vain attempt to help bad TCP stacks that cause problems
when they overflow
=================
*/
void CL_DumpPackets (void)
{
	while (NET_GetPacket (NS_CLIENT, &net_from, &net_message))
	{
		Com_Printf ("dumnping a packet\n");
	}
}

/*
=================
CL_ReadPackets
=================
*/
void CL_ReadPackets (void)
{
	while (NET_GetPacket (NS_CLIENT, &net_from, &net_message))
	{
//	Com_Printf ("packet\n");
		//
		// remote command packet
		//
		if (*(int *)net_message.data == -1)
		{
			CL_ConnectionlessPacket ();
			continue;
		}

		if (cls.state == ca_disconnected || cls.state == ca_connecting)
			continue;		// dump it if not connected

		if (net_message.cursize < 8)
		{
			Com_Printf ("%s: Runt packet\n",NET_AdrToString(net_from));
			continue;
		}

		//
		// packet from server
		//
		if (!NET_CompareAdr (net_from, cls.netchan.remote_address))
		{
			Com_DPrintf ("%s:sequenced packet without connection\n"
				,NET_AdrToString(net_from));
			continue;
		}
		if (!Netchan_Process(&cls.netchan, &net_message))
			continue;		// wasn't accepted for some reason
		CL_ParseServerMessage ();
	}

	//
	// check timeout
	//
	if (cls.state >= ca_connected
	 && cls.realtime - cls.netchan.last_received > cl_timeout->value*1000)
	{
		if (++cl.timeoutcount > 5)	// timeoutcount saves debugger
		{
			Com_Printf ("\nServer connection timed out.\n");
			CL_Disconnect ();
			return;
		}
	}
	else
		cl.timeoutcount = 0;

}

//=============================================================================

/*
==============
CL_FixUpGender_f
==============
*/
void CL_FixUpGender(void)
{
	char *p;
	char sk[80];

	if (gender_auto->value) {

		if (gender->modified) {
			// was set directly, don't override the user
			gender->modified = false;
			return;
		}

		strncpy(sk, skin->string, sizeof(sk) - 1);
		if ((p = strchr(sk, '/')) != NULL)
			*p = 0;
		if (Q_stricmp(sk, "male") == 0 || Q_stricmp(sk, "cyborg") == 0)
			Cvar_Set ("gender", "male");
		else if (Q_stricmp(sk, "female") == 0 || Q_stricmp(sk, "crackhor") == 0)
			Cvar_Set ("gender", "female");
		else
			Cvar_Set ("gender", "none");
		gender->modified = false;
	}
}

/*
==============
CL_Userinfo_f
==============
*/
void CL_Userinfo_f (void)
{
	Com_Printf ("User info settings:\n");
	Info_Print (Cvar_Userinfo());
}

/*
=================
CL_Snd_Restart_f

Restart the sound subsystem so it can pick up
new parameters and flush all sounds
=================
*/
void CL_Snd_Restart_f (void)
{
	cl_sound_restart_pending = false;
	S_Shutdown ();
	if (!MIDI_IsIdle () || !WavMusic_IsIdle () || !CDAudio_IsIdle ())
	{
		cl_sound_restart_pending = true;
		Com_Printf ("Sound restart deferred until music releases its output\n");
		return;
	}
	S_Init ();
	CL_RegisterSounds ();
	M_RefreshMusic ();
}

int precache_check; // for autodownload of precache items
int precache_spawncount;
int precache_tex;
int precache_model_skin;

byte *precache_model; // used for skin checking in alias models

#define PLAYER_MULT 5

// ENV_CNT is map load, ENV_CNT+1 is first env map
#define ENV_CNT (CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT)
#define TEXTURE_CNT (ENV_CNT+13)

static const char *env_suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

void CL_RequestNextDownload (void)
{
	unsigned	map_checksum;		// for detecting cheater maps
	char fn[MAX_OSPATH];
	dmdl_t *pheader;

	if (cls.state != ca_connected)
		return;

	if (!allow_download->value && precache_check < ENV_CNT)
		precache_check = ENV_CNT;

//ZOID
	if (precache_check == CS_MODELS) { // confirm map
		precache_check = CS_MODELS+2; // 0 isn't used
		if (allow_download_maps->value)
			if (!CL_CheckOrDownloadFile(cl.configstrings[CS_MODELS+1]))
				return; // started a download
	}
	if (precache_check >= CS_MODELS && precache_check < CS_MODELS+MAX_MODELS) {
		if (allow_download_models->value) {
			while (precache_check < CS_MODELS+MAX_MODELS &&
				cl.configstrings[precache_check][0]) {
				if (cl.configstrings[precache_check][0] == '*' ||
					cl.configstrings[precache_check][0] == '#') {
					precache_check++;
					continue;
				}
				if (precache_model_skin == 0) {
					if (!CL_CheckOrDownloadFile(cl.configstrings[precache_check])) {
						precache_model_skin = 1;
						return; // started a download
					}
					precache_model_skin = 1;
				}

				// checking for skins in the model
				if (!precache_model) {

					FS_LoadFile (cl.configstrings[precache_check], (void **)&precache_model);
					if (!precache_model) {
						precache_model_skin = 0;
						precache_check++;
						continue; // couldn't load it
					}
					if (LittleLong(*(unsigned *)precache_model) != IDALIASHEADER) {
						// not an alias model
						FS_FreeFile(precache_model);
						precache_model = 0;
						precache_model_skin = 0;
						precache_check++;
						continue;
					}
					pheader = (dmdl_t *)precache_model;
					if (LittleLong (pheader->version) != ALIAS_VERSION) {
						precache_check++;
						precache_model_skin = 0;
						continue; // couldn't load it
					}
				}

				pheader = (dmdl_t *)precache_model;

				while (precache_model_skin - 1 < LittleLong(pheader->num_skins)) {
					if (!CL_CheckOrDownloadFile((char *)precache_model +
						LittleLong(pheader->ofs_skins) +
						(precache_model_skin - 1)*MAX_SKINNAME)) {
						precache_model_skin++;
						return; // started a download
					}
					precache_model_skin++;
				}
				if (precache_model) {
					FS_FreeFile(precache_model);
					precache_model = 0;
				}
				precache_model_skin = 0;
				precache_check++;
			}
		}
		precache_check = CS_SOUNDS;
	}
	if (precache_check >= CS_SOUNDS && precache_check < CS_SOUNDS+MAX_SOUNDS) {
		if (allow_download_sounds->value) {
			if (precache_check == CS_SOUNDS)
				precache_check++; // zero is blank
			while (precache_check < CS_SOUNDS+MAX_SOUNDS &&
				cl.configstrings[precache_check][0]) {
				if (cl.configstrings[precache_check][0] == '*') {
					precache_check++;
					continue;
				}
				Com_sprintf(fn, sizeof(fn), "sound/%s", cl.configstrings[precache_check++]);
				if (!CL_CheckOrDownloadFile(fn))
					return; // started a download
			}
		}
		precache_check = CS_IMAGES;
	}
	if (precache_check >= CS_IMAGES && precache_check < CS_IMAGES+MAX_IMAGES) {
		if (precache_check == CS_IMAGES)
			precache_check++; // zero is blank
		while (precache_check < CS_IMAGES+MAX_IMAGES &&
			cl.configstrings[precache_check][0]) {
			Com_sprintf(fn, sizeof(fn), "pics/%s.pcx", cl.configstrings[precache_check++]);
			if (!CL_CheckOrDownloadFile(fn))
				return; // started a download
		}
		precache_check = CS_PLAYERSKINS;
	}
	// skins are special, since a player has three things to download:
	// model, weapon model and skin
	// so precache_check is now *3
	if (precache_check >= CS_PLAYERSKINS && precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT) {
		if (allow_download_players->value) {
			while (precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT) {
				int i, n;
				char model[MAX_QPATH], skin[MAX_QPATH], *p;

				i = (precache_check - CS_PLAYERSKINS)/PLAYER_MULT;
				n = (precache_check - CS_PLAYERSKINS)%PLAYER_MULT;

				if (!cl.configstrings[CS_PLAYERSKINS+i][0]) {
					precache_check = CS_PLAYERSKINS + (i + 1) * PLAYER_MULT;
					continue;
				}

				if ((p = strchr(cl.configstrings[CS_PLAYERSKINS+i], '\\')) != NULL)
					p++;
				else
					p = cl.configstrings[CS_PLAYERSKINS+i];
				Q_strncpyz_bdgn (model, p, sizeof(model));	/* BDGN 3.59f: a player skin string can run over several 64-byte slots */
				p = strchr(model, '/');
				if (!p)
					p = strchr(model, '\\');
				if (p) {
					*p++ = 0;
					Q_strncpyz_bdgn (skin, p, sizeof(skin));
				} else
					*skin = 0;

				switch (n) {
				case 0: // model
					Com_sprintf(fn, sizeof(fn), "players/%s/tris.md2", model);
					if (!CL_CheckOrDownloadFile(fn)) {
						precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 1;
						return; // started a download
					}
					n++;
					/*FALL THROUGH*/

				case 1: // weapon model
					Com_sprintf(fn, sizeof(fn), "players/%s/weapon.md2", model);
					if (!CL_CheckOrDownloadFile(fn)) {
						precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 2;
						return; // started a download
					}
					n++;
					/*FALL THROUGH*/

				case 2: // weapon skin
					Com_sprintf(fn, sizeof(fn), "players/%s/weapon.pcx", model);
					if (!CL_CheckOrDownloadFile(fn)) {
						precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 3;
						return; // started a download
					}
					n++;
					/*FALL THROUGH*/

				case 3: // skin
					Com_sprintf(fn, sizeof(fn), "players/%s/%s.pcx", model, skin);
					if (!CL_CheckOrDownloadFile(fn)) {
						precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 4;
						return; // started a download
					}
					n++;
					/*FALL THROUGH*/

				case 4: // skin_i
					Com_sprintf(fn, sizeof(fn), "players/%s/%s_i.pcx", model, skin);
					if (!CL_CheckOrDownloadFile(fn)) {
						precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 5;
						return; // started a download
					}
					// move on to next model
					precache_check = CS_PLAYERSKINS + (i + 1) * PLAYER_MULT;
				}
			}
		}
		// precache phase completed
		precache_check = ENV_CNT;
	}

	if (precache_check == ENV_CNT) {
		precache_check = ENV_CNT + 1;

		{	/* BDGN 3.59f: load phases (cl_view.c CL_PrepRefresh writes them) */
			int t0;
			CL_LoadPhaseBegin ();
			t0 = Sys_Milliseconds ();
			CM_LoadMap (cl.configstrings[CS_MODELS+1], true, &map_checksum);
			cl_load_cm_ms = Sys_Milliseconds () - t0;
		}

		if (map_checksum != atoi(cl.configstrings[CS_MAPCHECKSUM])) {
			Com_Error (ERR_DROP, "Local map version differs from server: %i != '%s'\n",
				map_checksum, cl.configstrings[CS_MAPCHECKSUM]);
			return;
		}
	}

	if (precache_check > ENV_CNT && precache_check < TEXTURE_CNT) {
		if (allow_download->value && allow_download_maps->value) {
			while (precache_check < TEXTURE_CNT) {
				int n = precache_check++ - ENV_CNT - 1;

				if (n & 1)
					Com_sprintf(fn, sizeof(fn), "env/%s%s.pcx",
						cl.configstrings[CS_SKY], env_suf[n/2]);
				else
					Com_sprintf(fn, sizeof(fn), "env/%s%s.tga",
						cl.configstrings[CS_SKY], env_suf[n/2]);
				if (!CL_CheckOrDownloadFile(fn))
					return; // started a download
			}
		}
		precache_check = TEXTURE_CNT;
	}

	if (precache_check == TEXTURE_CNT) {
		precache_check = TEXTURE_CNT+1;
		precache_tex = 0;
	}

	// confirm existance of textures, download any that don't exist
	if (precache_check == TEXTURE_CNT+1) {
		// from qcommon/cmodel.c
		extern int			numtexinfo;
		extern mapsurface_t	map_surfaces[];

		if (allow_download->value && allow_download_maps->value) {
			while (precache_tex < numtexinfo) {
				char fn[MAX_OSPATH];

				sprintf(fn, "textures/%s.wal", map_surfaces[precache_tex++].rname);
				if (!CL_CheckOrDownloadFile(fn))
					return; // started a download
			}
		}
		precache_check = TEXTURE_CNT+999;
	}

//ZOID
	{	int t0 = Sys_Milliseconds ();	/* BDGN 3.59f */
		CL_LoadPhaseBegin ();
		CL_RegisterSounds ();
		cl_load_snd_ms = Sys_Milliseconds () - t0;
	}
	CL_PrepRefresh ();

	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message, va("begin %i\n", precache_spawncount) );
}

/*
=================
CL_Precache_f

The server will send this command right
before allowing the client into the server
=================
*/
void CL_Precache_f (void)
{
	//Yet another hack to let old demos work
	//the old precache sequence
	if (Cmd_Argc() < 2) {
		unsigned	map_checksum;		// for detecting cheater maps

		{	int t0;	/* BDGN 3.59f */
			CL_LoadPhaseBegin ();
			t0 = Sys_Milliseconds ();
			CM_LoadMap (cl.configstrings[CS_MODELS+1], true, &map_checksum);
			cl_load_cm_ms = Sys_Milliseconds () - t0;
			t0 = Sys_Milliseconds ();
			CL_RegisterSounds ();
			cl_load_snd_ms = Sys_Milliseconds () - t0;
		}
		CL_PrepRefresh ();
		return;
	}

	precache_check = CS_MODELS;
	precache_spawncount = atoi(Cmd_Argv(1));
	precache_model = 0;
	precache_model_skin = 0;

	CL_RequestNextDownload();
}

/*
=================
CL_ParseSetLines

Walks a config file and calls fn (name, value) for every  set name "value"  line.
=================
*/
void CL_ParseSetLines (const char *buf, int len, void (*fn)(const char *name, const char *value))
{
	const char	*p = buf, *end = buf + len;
	char		name[64], value[128];
	int			n;

	while (p < end)
	{
		while (p < end && (*p == ' ' || *p == '\t'))
			p++;
		if (end - p > 4 && Q_strncasecmp ((char *)p, "set ", 4) == 0)
		{
			p += 4;
			while (p < end && *p == ' ')
				p++;
			n = 0;
			while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			{
				if (n < (int)sizeof(name) - 1)
					name[n++] = *p;
				p++;
			}
			name[n] = 0;
			while (p < end && (*p == ' ' || *p == '\t'))
				p++;
			n = 0;
			if (p < end && *p == '"')
			{
				p++;
				while (p < end && *p != '"' && *p != '\r' && *p != '\n')
				{
					if (n < (int)sizeof(value) - 1)
						value[n++] = *p;
					p++;
				}
			}
			else
			{
				while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
				{
					if (n < (int)sizeof(value) - 1)
						value[n++] = *p;
					p++;
				}
			}
			value[n] = 0;
			if (name[0])
				fn (name, value);
		}
		while (p < end && *p != '\n')		// rest of the line
			p++;
		if (p < end)
			p++;
	}
}

static void CL_ArchiveOne (const char *name, const char *value)
{
	// an existing cvar keeps its value, it only gets the archive flag
	Cvar_Get ((char *)name, (char *)value, CVAR_ARCHIVE);
}

/*
=================
CL_ArchivePresetFile

Makes every cvar set in the given preset file an archived cvar so its value is written to config.cfg.
Many of them (cl_maxfps, cl_particles, gl_polyblend, gl_ztrick ...) are not archived by Quake II itself
and were back at their defaults at the next start.
=================
*/
void CL_ArchivePresetFile (const char *file)
{
	char	*buf;
	int		len;

	len = FS_LoadFile (FS_PresetPath (file), (void **)&buf);	/* BDGN 3.52g: configs/ */
	if (len <= 0 || !buf)
		return;
	CL_ParseSetLines (buf, len, CL_ArchiveOne);
	FS_FreeFile (buf);
}

/*
=================
CL_InitLocal
=================
*/
/*
===============
CL_LowRamCheck

BDGN 3.51bdt: at every start.  Less than 64 MB of memory (Windows reports below 60 MB; a 64 MB machine reports about
62-63) sets bdgn_lowram 1: the video menu then lists the four low RAM presets, the low fidelity textures are on
(the smaller test pack lowtex-small.pak with BDGN's OpenGL renderer when it is installed, else pak6-lowtextures.pak)
and, the first time only (bdgn_lowram_done, saved), the "low RAM default" preset is applied.
+set bdgn_lowram_test 1 on the command line simulates a low RAM machine, -1 never treats it as one.
===============
*/
extern void VID_ApplyLowRamPreset (void);
extern int VID_AutoPresetChain (int *out,int max);
extern const char *VID_PresetLabel (int id);
extern void VID_ApplyAutoPreset (int id);

/*
================
BDGN 3.55q: automatic preset from the hardware (bdgn_autopreset)

On the first start (no preset chosen yet) and from Video -> preset -> "Auto", the preset is chosen from the
graphics card and the CPU, as their drivers name them (VID_AdapterName / VID_CpuName in vid_dll.c), with the
ratings in client/gpu_table.h (PassMark G3D Mark) and CL_CpuRating (PassMark single-thread class, estimated).
Each preset needs a graphics card and a CPU of its own class; the weaker of the two decides:

  step            G3D at 1920x1080   CPU   (the G3D need scales with the desktop pixels, 0.6x - 2.5x)
  High Fidelity 3       14000         2500
  High Fidelity 2        9000         2200
  High Fidelity 1        6000         1900
  Ultra Fidelity SSAO    3500         1600
  Ultra Fidelity Bloom   2200         1400
  Ultra Fidelity MSAA    1200         1200
  MAX Fidelity             40          400
  default (card family default when there is one): everything else

Cards and CPUs from before 2001 always get default. Unknown card or CPU: default; unknown card with a known CPU:
chosen by the CPU. Last check: when something was unknown and the newest display driver is dated 2026 or later,
the best preset is used. Only presets the driver offers are chosen (High Fidelity needs shader support), so a
weaker step is taken when needed. Below 64 MB the low RAM presets are used as before. Nothing is measured.
================
*/
#include "gpu_table.h"
extern void VID_AdapterName (char *out, int size);
extern void VID_CpuName (char *out, int size);
extern int VID_DriverYear (void);
extern void VID_DesktopSize (int *w, int *h);
extern qboolean VID_PresetOffered (int id);
extern int VID_AutoDefaultPreset (void);
#define AUTO_STEPS 9
static const char *auto_step_names[AUTO_STEPS]={"Ultra High Fidelity - 4K","High Fidelity 3","High Fidelity 2","High Fidelity 1",
	"Ultra Fidelity 3 - SSAO","Ultra Fidelity 2 - Bloom","Ultra Fidelity 1 - MSAA","MAX Fidelity","default"};
static const int auto_step_id[AUTO_STEPS-1]={83,82,81,80,69,68,67,5};	/* 3.55w: 83 only when the 4K set is installed (VID_PresetOffered) */
static const int auto_gpu_need[AUTO_STEPS]={20000,14000,9000,6000,3500,2200,1200,40,0};
static const int auto_cpu_need[AUTO_STEPS]={2800,2500,2200,1900,1600,1400,1200,400,0};
static int auto_state,auto_settle,auto_step;

/* name without (R) (TM) (tm), single spaces */
static void CL_HwClean (const char *in, char *out, int size)
{
	int n=0;
	while (*in && n<size-1) {
		if (*in=='(' && (!Q_strncasecmp((char *)in,"(R)",3) || !Q_strncasecmp((char *)in,"(TM)",4))) {
			in+=in[2]==')'?3:4;
			continue;
		}
		if (*in==' ' && (!n || out[n-1]==' ')) { in++; continue; }
		out[n++]=*in++;
	}
	while (n && out[n-1]==' ') n--;
	out[n]=0;
}

static const char *CL_HwFind (const char *hay, const char *needle)
{
	int n=strlen(needle);
	for (;*hay;hay++)
		if (!Q_strncasecmp((char *)hay,(char *)needle,n)) return hay;
	return NULL;
}

/* graphics card: G3D and year, or -1 when unknown */
static int CL_GpuRating (const char *name, int *year)
{
	int i,pass,best=-1,len=0;
	for (pass=0;pass<2 && best<0;pass++)	/* models first, the generic family patterns only when no model matches */
		for (i=0;i<(int)BDGN_GPU_COUNT;i++)
			if (bdgn_gpus[i].generic==pass && CL_HwFind(name,bdgn_gpus[i].pattern) && (int)strlen(bdgn_gpus[i].pattern)>len) {
				best=i; len=strlen(bdgn_gpus[i].pattern);
			}
	if (best<0) return -1;
	*year=bdgn_gpus[best].year;
	return bdgn_gpus[best].g3d;
}

/* CPU: estimated single-thread class (PassMark-like) and year, or -1 when unknown */
static int CL_CpuRating (const char *name, int *year)
{
	static const struct { const char *pattern; short year; int score; } cpus[]={
		{"Core Ultra",2024,4500},{"Threadripper",2019,3000},{"EPYC",2019,2800},
		{"Core2 Quad",2007,1000},{"Core2 Duo",2006,1200},{"Core2 Extreme",2007,1250},{"Core Duo",2006,700},
		{"Pentium 4",2002,600},{"Pentium D",2005,650},{"Pentium M",2003,600},{"Pentium Dual",2007,900},
		{"Pentium CPU G",2011,1600},{"Pentium Gold",2017,2200},{"Pentium III",1999,250},{"Pentium II",1997,150},
		{"Pentium MMX",1997,90},{"Celeron",2003,500},{"Atom",2008,250},
		{"Athlon 64 X2",2005,800},{"Athlon 64",2004,700},{"Athlon XP",2002,450},{"Athlon II",2009,1000},
		{"Athlon MP",2001,400},{"Phenom II",2009,1200},{"Phenom",2008,900},{"FX-",2012,1500},{"A10-",2013,1300},
		{"A8-",2013,1200},{"Xeon",2012,1700},{"Sempron",2004,500},{"Duron",2000,250},{"Athlon",1999,300},{"K6",1998,120},
		/* BDGN 3.57o: the names CL_CpuNameAny gives on Windows 9x (TEORETISK scores between the ones above) */
		{"K6-III",1999,170},{"K6-2",1998,140},{"Pentium Pro",1996,110},{"Celeron (P6)",1998,140},
		{"Pentium (P5)",1994,60},{"K5",1996,50},{"Cyrix",1997,60}
	};
	static const short intel_year[15]={0,2009,2011,2012,2013,2015,2015,2017,2017,2018,2020,2021,2021,2022,2023};
	static const short intel_score[15]={0,1200,1900,2000,2300,2300,2500,2700,2800,2900,3000,3400,3900,4300,4500};
	static const short ryzen_year[10]={0,2017,2018,2019,2020,2020,2021,2022,2024,2024};
	static const short ryzen_score[10]={0,2200,2350,2650,2700,3300,3200,4000,3900,4600};
	const char *p;
	int i,best=-1,len=0;
	if ((p=CL_HwFind(name,"Core i"))!=NULL && p[6]>='3' && p[6]<='9' && (p[7]=='-' || !Q_strncasecmp((char *)p+7," CPU ",5))) {	/* Intel Core i3/i5/i7/i9-model, first generation "Core i7 CPU 920" */
		const char *d=p+(p[7]=='-'?8:12);
		int digits=0,gen,score;
		while (d[digits]>='0' && d[digits]<='9') digits++;
		if (digits>=5) gen=(d[0]-'0')*10+(d[1]-'0');
		else if (digits==4) gen=d[0]-'0';
		else if (digits==3) gen=1;
		else return -1;
		if (gen<1 || gen>14) return -1;
		score=intel_score[gen];
		if (p[6]=='3') score=score*85/100;
		else if (p[6]=='5') score=score*95/100;
		else if (p[6]=='9') score=score*105/100;
		*year=intel_year[gen];
		return score;
	}
	if ((p=CL_HwFind(name,"Xeon"))!=NULL && !CL_HwFind(name,"Pentium")) {
		/* BDGN 3.57n: Xeons by generation and clock (all were one score before). Windows names them like
		   "Intel Xeon CPU E5-2670 0 @ 2.60GHz" (Sandy Bridge-EP, X79), "E5-2680 v2" (Ivy Bridge-EP, X79),
		   "E5-1650 v3" (Haswell-EP, X99), "E5-2699 v4" (Broadwell-EP, X99), "X5670" / "W3680" (Westmere/Nehalem,
		   X58), "E3-1230 V2", "Xeon W-2135" / "Xeon Gold 6130" (Skylake and later). Score = points per GHz of the
		   generation x clock; the dual-socket models (E5-2xxx/4xxx) get more turbo over their low base clock than the
		   single-socket ones. TEORETISK ANALYS: rough single-thread figures, PassMark-like, enough for the preset
		   steps (every one of these is far beyond what Quake II needs). "Pentium II/III Xeon" stay with their Pentium. */
		const char *g=CL_HwFind(name,"GHz"), *m;
		float ghz=0, boost=1.1f;
		int per=0, yr=0;
		if (g) {	/* the number before "GHz" */
			const char *s=g;
			while (s>name && ((s[-1]>='0' && s[-1]<='9') || s[-1]=='.')) s--;
			ghz=(float)atof(s);
		}
		if ((m=CL_HwFind(name,"E5-"))!=NULL || (m=CL_HwFind(name,"E7-"))!=NULL || (m=CL_HwFind(name,"E3-"))!=NULL) {
			if (CL_HwFind(m," v6") || CL_HwFind(m," v5")) { per=680; yr=2016; }
			else if (CL_HwFind(m," v4")) { per=620; yr=2016; }
			else if (CL_HwFind(m," v3")) { per=600; yr=2014; }
			else if (CL_HwFind(m," v2")) { per=560; yr=2013; }
			else { per=520; yr=2012; }
			if (m[3]=='2' || m[3]=='4' || (m[0]=='E' && m[1]=='7')) boost=1.2f;
		} else if (CL_HwFind(name,"Xeon W-") || CL_HwFind(name,"Bronze") || CL_HwFind(name,"Silver") ||
		           CL_HwFind(name,"Gold") || CL_HwFind(name,"Platinum")) {
			per=680; yr=2017;
			if (!CL_HwFind(name,"Xeon W-")) boost=1.25f;
		} else {	/* X58 era: a letter (W, X, E, L) and four digits, e.g. W3680, X5670 */
			const char *q=p+4;
			while (*q==' ' || !Q_strncasecmp((char *)q,"CPU",3)) q+=(*q==' ')?1:3;
			if ((*q=='W' || *q=='X' || *q=='E' || *q=='L') && q[1]>='3' && q[1]<='7' && q[2]>='0' && q[2]<='9' &&
			    q[3]>='0' && q[3]<='9' && q[4]>='0' && q[4]<='9') {
				per=420; yr=2009;
				if (q[1]=='5' || q[1]=='7') boost=1.15f;
			}
		}
		if (per) {
			if (ghz<1.0f || ghz>6.0f) ghz=2.5f;
			*year=yr;
			return (int)(per*ghz*boost);
		}
		*year=2002;	/* NetBurst Xeons ("Xeon CPU 3.06GHz"): Pentium 4 class */
		return 600;
	}
	if ((p=CL_HwFind(name,"Ryzen "))!=NULL) {	/* AMD Ryzen 3/5/7/9 ABCD, Ryzen AI */
		const char *d=p+6;
		if (!Q_strncasecmp((char *)d,"AI",2)) { *year=2024; return 4000; }
		while (*d && *d!=' ') d++;
		while (*d==' ') d++;
		if (*d>='1' && *d<='9') { *year=ryzen_year[*d-'0']; return ryzen_score[*d-'0']; }
		*year=2017;
		return 2200;
	}
	for (i=0;i<(int)(sizeof(cpus)/sizeof(cpus[0]));i++)
		if (CL_HwFind(name,cpus[i].pattern) && (int)strlen(cpus[i].pattern)>len) { best=i; len=strlen(cpus[i].pattern); }
	if (best<0) return -1;
	*year=cpus[best].year;
	return cpus[best].score;
}

static int CL_HwStep (int rating, int year, const int *need, float scale)
{
	int i;
	if (rating<0 || year<2001) return AUTO_STEPS-1;
	for (i=0;i<AUTO_STEPS-1;i++)
		if (rating>=need[i]*scale) return i;
	return AUTO_STEPS-1;
}

/* the step for this computer (0 = Ultra High Fidelity - 4K, 1 = High Fidelity 3 ... 8 = default) */
static int CL_AutoPresetChoose (qboolean report)
{
	char raw[160],gpu[160],cpu[160];
	int gyear=0,cyear=0,g,c,gstep,cstep,step,w,h,driver;
	float scale;
	VID_AdapterName(raw,sizeof(raw));
	if (!raw[0]) { strncpy(raw,Cvar_VariableString("bdgn_gl_renderer"),sizeof(raw)-1); raw[sizeof(raw)-1]=0; }
	CL_HwClean(raw,gpu,sizeof(gpu));
	CL_CpuNameAny(raw,sizeof(raw));	/* BDGN 3.57o: CPUID name on Windows 9x */
	CL_HwClean(raw,cpu,sizeof(cpu));
	VID_DesktopSize(&w,&h);
	scale=(float)w*h/(1920.0f*1080.0f);
	if (scale<0.6f) scale=0.6f;
	if (scale>2.5f) scale=2.5f;
	g=gpu[0]?CL_GpuRating(gpu,&gyear):-1;
	c=cpu[0]?CL_CpuRating(cpu,&cyear):-1;
	gstep=CL_HwStep(g,gyear,auto_gpu_need,scale);
	cstep=CL_HwStep(c,cyear,auto_cpu_need,1);
	if (g>=0 && c>=0) step=gstep>cstep?gstep:cstep;
	else if (g<0 && c>=0) step=cstep;
	else step=AUTO_STEPS-1;
	driver=VID_DriverYear();
	if ((g<0 || c<0) && driver>=2026) step=0;	/* unknown but a driver from 2026 or later: newest hardware */
	if (report) {
		Com_Printf("BDGN auto preset: graphics card \"%s\" -> %s\n",gpu[0]?gpu:"?",g>=0?va("%i (%i), %s",g,gyear,auto_step_names[gstep]):"unknown");
		Com_Printf("BDGN auto preset: CPU \"%s\" -> %s\n",cpu[0]?cpu:"?",c>=0?va("%i (%i), %s",c,cyear,auto_step_names[cstep]):"unknown");
		Com_Printf("BDGN auto preset: desktop %ix%i, driver year %i -> %s\n",w,h,driver,auto_step_names[step]);
	}
	return step;
}

static void CL_AutoPreset_f (void)
{
	if (Cvar_VariableValue("bdgn_lowram")) {
		Com_Printf("BDGN auto preset: below 64 MB the low RAM presets are used\n");
		return;
	}
	Cvar_Set("bdgn_autopreset","1");
	Cvar_Set("bdgn_autopreset_gltried","0");	/* 3.55r: "Auto" from the menu may change the driver again */
	auto_state=1;auto_settle=Sys_Milliseconds();
}
static void CL_AutoPresetDone (const char *why)
{
	Cvar_Set("bdgn_autopreset","0");
	auto_state=0;
	Com_Printf("BDGN auto preset: %s\n",why);
}
static void CL_AutoPresetFrame (void)
{
	int now=Sys_Milliseconds(),i,id;
	if (!auto_state || CL_BenchmarkRunning() || now-auto_settle<1500) return;
	if (auto_state==1) {
		auto_step=CL_AutoPresetChoose(true);
		/* 3.55r: the steps above MAX Fidelity need the system OpenGL driver (opengl32): shaders, framebuffers, MSAA,
		   HD textures. Software, AMD's glam and the MTGL / miniGL wrappers do not offer them. */
		if (auto_step<AUTO_STEPS-2 && Cvar_VariableValue("bdgn_modern_lighting_cpu") && !Cvar_VariableValue("bdgn_autopreset_gltried") &&
		    (Q_stricmp(Cvar_VariableString("vid_ref"),"gl") ||
		     (Q_stricmp(Cvar_VariableString("gl_driver"),"opengl32") && Q_stricmp(Cvar_VariableString("gl_driver"),"opengl32.dll")))) {
			Cvar_Set("bdgn_autopreset_gltried","1");
			Cvar_Set("gl_driver","opengl32");
			Cvar_Set("vid_ref","gl");
			Cvar_Set("vid_rendition","0");
			Com_Printf("BDGN auto preset: starting the system OpenGL driver (opengl32)\n");
			Cbuf_AddText("vid_restart\n");	/* vid_ref may already be "gl" (only the driver changes) */
		}
		auto_state=2;auto_settle=now;
		return;
	}
	/* 2: the renderer's preset list is built; take the chosen step or the next weaker one it offers */
	id=VID_AutoDefaultPreset();
	for (i=auto_step;i<AUTO_STEPS-1;i++)
		if (VID_PresetOffered(auto_step_id[i])) { id=auto_step_id[i]; break; }
	VID_ApplyAutoPreset(id);
	CL_AutoPresetDone(va("chose %s",VID_PresetLabel(id)));
}
static void CL_AutoPresetInit (void)
{
	cvar_t *a=Cvar_Get("bdgn_autopreset","",CVAR_ARCHIVE);
	Cvar_Get("bdgn_autopreset_gltried","0",CVAR_ARCHIVE);
	Cmd_AddCommand("bdgn_autopreset",CL_AutoPreset_f);
	if (!a->string[0])	/* never decided: first start when no preset has been chosen (an existing install keeps its choice) */
		Cvar_Set("bdgn_autopreset",Cvar_VariableString("cfg_preset")[0] || Cvar_VariableValue("bdgn_lowram") ? "0" : "1");
	if (Cvar_VariableValue("bdgn_autopreset") && !Cvar_VariableValue("bdgn_lowram")) {
		auto_state=1;auto_settle=Sys_Milliseconds();
		Com_Printf("BDGN auto preset: first start, choosing the preset from the graphics card and CPU\n");
	}
}
/* BDGN 3.55j: the S3 Virge OpenGL driver is selected (ref_gl or AMD's glam on S3VirgeOpenGL.dll) */
qboolean CL_S3VirgeChosen (void)
{
	const char *ref=Cvar_VariableString("vid_ref");
	return (!Q_stricmp((char *)ref,"gl") || !Q_stricmp((char *)ref,"glam")) &&
		!Q_strncasecmp(Cvar_VariableString("gl_driver"),"S3VirgeOpenGL",13);
}
/* BDGN 3.55j: below 32 MB the user's own choice is kept at start: a lowerThen32mb preset with Software, or
   an S3 Virge preset with the S3 Virge renderer */
static qboolean CL_LowRamPresetChosen (void)
{
	const char *preset=Cvar_VariableString("cfg_preset");
	if (CL_S3VirgeChosen())
		return !Q_strncasecmp((char *)preset,"s3virge",7);
	return !Q_strncasecmp((char *)preset,"lowerThen32mb",13);
}
/* BDGN 3.59u: bdgn_vramcheck - reads the primary display's video memory once (DirectDraw) and saves it (bdgn_vram_kb,
   config.cfg); vid_lowvram 2 (auto) uses it. Not with a 3dfx driver chosen: a Voodoo has its own texture memory that
   DirectDraw does not report, and DirectDraw next to Glide is the suspect of the Voodoo3 blue screen in v3.59t. */
static void CL_VramCheck_f (void)
{
#ifdef _WIN32
	const char *drv = Cvar_VariableString ("gl_driver");
	int vfree, vkb;
	if (cls.state == ca_active || cls.state == ca_connected)
	{
		Com_Printf ("bdgn_vramcheck: disconnect first (it must run before a map or demo)\n");
		return;
	}
	if (!Q_strncasecmp ((char *)drv, "3dfx", 4) || !Q_strncasecmp ((char *)drv, "wicked", 6) || !Q_strncasecmp ((char *)drv, "glide", 5))
	{
		Com_Printf ("bdgn_vramcheck: not with a 3dfx driver (gl_driver %s); a Voodoo's texture memory is not reported by DirectDraw\n", drv);
		return;
	}
	vkb = Sys_VideoRamKB (&vfree);
	Cvar_FullSet ("bdgn_vram_kb", va ("%i", vkb), CVAR_ARCHIVE);
	Cvar_FullSet ("bdgn_vram_free_kb", va ("%i", vfree), CVAR_ARCHIVE);
	Com_Printf ("Video memory: %i KB (%i KB free, primary display); saved%s\n", vkb, vfree,
		vkb > 0 && vkb <= 4608 ? "; 4 MB card: vid_lowvram 2 (auto) uses the smaller 2D caches" : "");
#else
	Com_Printf ("bdgn_vramcheck: Windows only\n");
#endif
}
static void CL_LowRamCheck (void)
{
	int		mb = Sys_TotalRamMB ();
	float	test = Cvar_Get ("bdgn_lowram_test", "0", 0)->value;
	int		low = Cvar_VariableValue("bdgn_extra_lowram") || test > 0 || ( test == 0 && mb > 0 && mb < 60 );
	cvar_t	*done;

	Cvar_FullSet ("bdgn_lowram", low ? "1" : "0", 0);
	Cvar_FullSet ("bdgn_ram_mb", va ("%i", mb), 0);
	Com_Printf ("Memory: %i MB%s\n", mb, low ? " -> low RAM: low fidelity textures, low RAM presets in the video menu" : "");
	/* BDGN 3.59u: the video memory is no longer read at every start. Chromatic's Voodoo3 (Windows 98 SE) got a blue screen
	   (fatal exception 0E in KERNEL32) with v3.59t, the first version that created a DirectDraw object at start before
	   the 3dfx driver; DirectDraw is the main suspect (TEORETISK ANALYS). bdgn_vramcheck reads it once and saves it. */
	Cvar_Get ("bdgn_vram_kb", "0", CVAR_ARCHIVE);
	Cvar_Get ("bdgn_vram_free_kb", "0", CVAR_ARCHIVE);
	Com_Printf ("Video memory: %s KB (saved by bdgn_vramcheck; 0 = not read)\n", Cvar_VariableString ("bdgn_vram_kb"));
#ifdef _WIN32
	if (Sys_StartupConfigRecovering ())
		return;
#endif
	if (!low)
		return;
	Cvar_SetValue("fs_lowtextures", Cvar_VariableValue("bdgn_extra_lowram") &&
		FS_UltraTexPackAvailable() ? 2 : 1);
	done = Cvar_Get ("bdgn_lowram_done", "0", CVAR_ARCHIVE);
	if (!done->value || (Cvar_VariableValue("bdgn_extra_lowram") && !CL_LowRamPresetChosen()))	/* 3.55i/j: keep a chosen below-32 MB or S3 Virge preset */
	{
		Cvar_SetValue ("bdgn_lowram_done", 1);
		VID_ApplyLowRamPreset ();
	}
}

/* BDGN 3.55y: bdgn_lowend - what this setup does more than id's 3.21 (for slow computers: Pentium 133 etc.) */
static void CL_LowEnd_f (void)
{
	static const char *list[][3] = {
		{ "vid_ref", "soft", "renderer (3.21 default soft)" },
		{ "sw_mode", "0", "software resolution, 0 = 320x240; every step costs more pixels" },
		{ "vid_fullscreen", "1", "windowed software output goes through GDI (slower)" },
		{ "sw_render_scale", "100", "render scaling" },
		{ "sw_texfilter", "0", "texture filter (per pixel)" },
		{ "sw_coloredlight", "0", "coloured light (surface cache)" },
		{ "sw_drawdist", "0", "draw distance fog (a pass over the screen)" },
		{ "sw_waterwarp", "1", "" },
		{ "sw_asmspans", "1", "id's Pentium span code (0 = slower C loops)" },
		{ "s_khz", "11", "sound mixing rate" },
		{ "s_maxchannels", "32", "" },
		{ "s_musicsource", "0", "music: 0 = CD (free), 1 = WAV files, 2 = MP3 (CPU decoding)" },
		{ "cl_simpleenemies", "0", "" },
		{ "r_lerpmodels", "1", "" },
		{ "cl_particles", "1", "" },
		{ "viewsize", "100", "" },
		{ NULL, NULL, NULL } };
	int i, n = 0;
	Com_Printf ("BDGN %s, CPU \"%s\", legacy computer: %s, RAM %s MB\n", BDGN_VERSION_STRING,
		Cvar_VariableString ("bdgn_hw_id"), Cvar_VariableValue ("bdgn_legacy_graphics") ? "yes" : "no",
		Cvar_VariableString ("bdgn_detected_ram_mb"));
	for (i = 0; list[i][0]; i++)
	{
		const char *v = Cvar_VariableString ((char *)list[i][0]);
		if (!Q_stricmp ((char *)v, (char *)list[i][1]))
			continue;
		Com_Printf ("  %-16s %-8s (3.21: %s) %s\n", list[i][0], v, list[i][1], list[i][2]);
		n++;
	}
	if (!n)
		Com_Printf ("  the costly settings are as in 3.21\n");
	Com_Printf ("Measure: timedemo 1 ; demo demo1.dm2  (same in 3.21), and host_speeds 1 for the split per frame\n");
}

void CL_InitLocal (void)
{
	cls.state = ca_disconnected;
	cls.realtime = Sys_Milliseconds ();

	CL_InitInput ();

	/* BDGN 3.58k: settings the menu and the presets set that were registered only on first use (or never): saved from
	   the start, so a change made before they are used is kept */
	Cvar_Get ("cl_simpleworld", "0", CVAR_ARCHIVE);
	Cvar_Get ("cl_modellod", "0", CVAR_ARCHIVE);
	Cvar_Get ("cl_simplemodels_far", "0", CVAR_ARCHIVE);
	Cvar_Get ("cl_lowspec3d", "0", CVAR_ARCHIVE);

	adr0 = Cvar_Get( "adr0", "", CVAR_ARCHIVE );
	adr1 = Cvar_Get( "adr1", "", CVAR_ARCHIVE );
	adr2 = Cvar_Get( "adr2", "", CVAR_ARCHIVE );
	adr3 = Cvar_Get( "adr3", "", CVAR_ARCHIVE );
	adr4 = Cvar_Get( "adr4", "", CVAR_ARCHIVE );
	adr5 = Cvar_Get( "adr5", "", CVAR_ARCHIVE );
	adr6 = Cvar_Get( "adr6", "", CVAR_ARCHIVE );
	adr7 = Cvar_Get( "adr7", "", CVAR_ARCHIVE );
	adr8 = Cvar_Get( "adr8", "", CVAR_ARCHIVE );

//
// register our variables
//
	cl_stereo_separation = Cvar_Get( "cl_stereo_separation", "0.4", CVAR_ARCHIVE );
	cl_stereo = Cvar_Get( "cl_stereo", "0", 0 );

	cl_add_blend = Cvar_Get ("cl_blend", "1", 0);
	cl_add_lights = Cvar_Get ("cl_lights", "1", 0);
	cl_add_particles = Cvar_Get ("cl_particles", "1", 0);
	cl_add_entities = Cvar_Get ("cl_entities", "1", 0);
	cl_gun = Cvar_Get ("cl_gun", "1", 0);
	cl_footsteps = Cvar_Get ("cl_footsteps", "1", 0);
	cl_noskins = Cvar_Get ("cl_noskins", "0", 0);
	cl_autoskins = Cvar_Get ("cl_autoskins", "0", 0);
	cl_predict = Cvar_Get ("cl_predict", "1", 0);
//	cl_minfps = Cvar_Get ("cl_minfps", "5", 0);
	cl_maxfps = Cvar_Get ("cl_maxfps", "100", CVAR_ARCHIVE);
	cl_async = Cvar_Get ("cl_async", "0", CVAR_ARCHIVE);	/* BDGN 3.58q: 0 = id's frame = move (vanilla) */
	r_maxfps = Cvar_Get ("r_maxfps", "0", CVAR_ARCHIVE);	/* BDGN 3.58p: 100 (10 ms per move: the highest and longest jumps,
		MEASURED with id's pmove.c, owner's choice 2026-11-20); saved like every setting (3.58k rule) */
	cl_sleep = Cvar_Get ("cl_sleep", "0", CVAR_ARCHIVE);
	cfg_preset = Cvar_Get ("cfg_preset", "", CVAR_ARCHIVE);

	cl_upspeed = Cvar_Get ("cl_upspeed", "200", 0);
	cl_forwardspeed = Cvar_Get ("cl_forwardspeed", "200", 0);
	cl_sidespeed = Cvar_Get ("cl_sidespeed", "200", 0);
	cl_yawspeed = Cvar_Get ("cl_yawspeed", "140", 0);
	cl_pitchspeed = Cvar_Get ("cl_pitchspeed", "150", 0);
	cl_anglespeedkey = Cvar_Get ("cl_anglespeedkey", "1.5", 0);

	cl_run = Cvar_Get ("cl_run", "0", CVAR_ARCHIVE);
// freelook on by default
//	freelook = Cvar_Get( "freelook", "0", CVAR_ARCHIVE );
	freelook = Cvar_Get( "freelook", "1", CVAR_ARCHIVE );
// end
	lookspring = Cvar_Get ("lookspring", "0", CVAR_ARCHIVE);
	lookstrafe = Cvar_Get ("lookstrafe", "0", CVAR_ARCHIVE);
	sensitivity = Cvar_Get ("sensitivity", "3", CVAR_ARCHIVE);
	// get default and zoom sensitivbity
	dsensitivity = Cvar_Get ("dsensitivity", "3", CVAR_ARCHIVE);
	zsensitivity = Cvar_Get ("zsensitivity", "1.25", CVAR_ARCHIVE);
	// end

	//m_filter
	m_filter = Cvar_Get ("m_filter", "0", CVAR_ARCHIVE);
	//

	//testing
	//s_khz
	//	s_khz = Cvar_Get ("s_khz", "11", CVAR_ARCHIVE);
	// testing

	m_pitch = Cvar_Get ("m_pitch", "0.022", CVAR_ARCHIVE);
	m_yaw = Cvar_Get ("m_yaw", "0.022", 0);
	m_forward = Cvar_Get ("m_forward", "1", 0);
	m_side = Cvar_Get ("m_side", "1", 0);

	cl_shownet = Cvar_Get ("cl_shownet", "0", 0);
	cl_showmiss = Cvar_Get ("cl_showmiss", "0", 0);
	cl_showclamp = Cvar_Get ("showclamp", "0", 0);
	cl_timeout = Cvar_Get ("cl_timeout", "120", 0);
	cl_paused = Cvar_Get ("paused", "0", 0);
	cl_timedemo = Cvar_Get ("timedemo", "0", 0);

	rcon_client_password = Cvar_Get ("rcon_password", "", 0);
	rcon_address = Cvar_Get ("rcon_address", "", 0);

	cl_lightlevel = Cvar_Get ("r_lightlevel", "0", 0);

	//
	// userinfo
	//
	info_password = Cvar_Get ("password", "", CVAR_USERINFO);
	info_spectator = Cvar_Get ("spectator", "0", CVAR_USERINFO);
	name = Cvar_Get ("name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE);
	skin = Cvar_Get ("skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE);
	rate = Cvar_Get ("rate", "25000", CVAR_USERINFO | CVAR_ARCHIVE);	// FIXME
	msg = Cvar_Get ("msg", "1", CVAR_USERINFO | CVAR_ARCHIVE);
	hand = Cvar_Get ("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	fov = Cvar_Get ("fov", "90", CVAR_USERINFO | CVAR_ARCHIVE);
	// zoom fov and default fov
	dfov = Cvar_Get ("dfov", "90", CVAR_ARCHIVE);
	zfov = Cvar_Get ("zfov", "37.5", CVAR_ARCHIVE);
	//
	gender = Cvar_Get ("gender", "male", CVAR_USERINFO | CVAR_ARCHIVE);
	gender_auto = Cvar_Get ("gender_auto", "1", CVAR_ARCHIVE);
	gender->modified = false; // clear this so we know when user sets it manually

	cl_vwep = Cvar_Get ("cl_vwep", "1", CVAR_ARCHIVE);

	// BDGN 3.4: performance toggles (default off = identical to 3.3)
	cl_fastfx = Cvar_Get ("cl_fastfx", "1", CVAR_ARCHIVE | CVAR_NOSET);
	Cvar_ForceSet("cl_fastfx","1");
	cl_weaponlights = Cvar_Get("cl_weaponlights",
	 (!Q_stricmp(Cvar_VariableString("cfg_preset"),"fast") || !Q_stricmp(Cvar_VariableString("cfg_preset"),"faster") || !Q_stricmp(Cvar_VariableString("cfg_preset"),"ultra")) ? "0" : "1", CVAR_ARCHIVE);
	cl_amd_particles = Cvar_Get ("cl_amd_particles", "1", CVAR_ARCHIVE);	// 3DNow! particles (follows the 3DNow! menu row)
	cl_bloodfx = Cvar_Get ("cl_bloodfx", "0", CVAR_ARCHIVE);
	cl_impactfx = Cvar_Get ("cl_impactfx", "0", CVAR_ARCHIVE);
	cl_static_rockets = Cvar_Get ("cl_static_rockets", "0", CVAR_ARCHIVE);
	cl_hdfx = Cvar_Get ("cl_hdfx", "0", CVAR_ARCHIVE);	/* BDGN 3.55s */
	CL_FX3D_Init ();	/* BDGN 3.55t: modern 3D weapon effects */
	CL_Gore_Init ();	/* BDGN 3.55y: marks, 3D railgun, HD gibs and blood */
	cl_static_bfg = Cvar_Get ("cl_static_bfg", "0", CVAR_ARCHIVE);
	cl_notrails = Cvar_Get ("cl_notrails", "0", CVAR_ARCHIVE);
	cl_sprite_trails = Cvar_Get ("cl_sprite_trails", "0", CVAR_ARCHIVE);	/* BDGN 3.51ah: test, sprite trails */
	cl_sprite_hits = Cvar_Get ("cl_sprite_hits", "0", CVAR_ARCHIVE);	/* BDGN 3.51ai: test, sprite blood / bullet hits */
	cl_nosteam = Cvar_Get ("cl_nosteam", "0", CVAR_ARCHIVE);
	cl_noitemrotate = Cvar_Get ("cl_noitemrotate", "0", CVAR_ARCHIVE);
	cl_nobanners = Cvar_Get ("cl_nobanners", "0", CVAR_ARCHIVE);
	cl_nodecorpses = Cvar_Get ("cl_nodecorpses", "0", CVAR_ARCHIVE);
	cl_nospawnpads = Cvar_Get ("cl_nospawnpads", "0", CVAR_ARCHIVE);
	cl_staticlightstyles = Cvar_Get ("cl_staticlightstyles", "0", CVAR_ARCHIVE);
	cl_staticenemies = Cvar_Get ("cl_staticenemies", "0", CVAR_ARCHIVE);
	cl_staticmap = Cvar_Get ("cl_staticmap", "0", CVAR_ARCHIVE);
	cl_simpleblaster = Cvar_Get ("cl_simpleblaster", "0", CVAR_ARCHIVE);
	cl_classic2d = Cvar_Get ("cl_classic2d", "0", CVAR_ARCHIVE);
	cl_lowend_sprites = Cvar_Get("cl_lowend_sprites","0",CVAR_ARCHIVE);
	Cvar_Get ("cl_simple2denemies", "0", CVAR_ARCHIVE);
	Cvar_Get ("cl_sprite_rate", "1", CVAR_ARCHIVE);	/* BDGN 3.51z: 2d sprites animation rate (menu.c BDGN_SpriteRate) */	/* BDGN 3.51y: menu state of static 2d enemies (menu.c BDGN_EnemyLook) */	/* BDGN 3.51x: menu state of classic 2d sprites mode (menu.c BDGN_ClassicSprites) */
	cl_simple_rail = Cvar_Get ("cl_simple_rail", "0", CVAR_ARCHIVE);
	cl_fb_world = Cvar_Get ("gl_fastworld", "0", CVAR_ARCHIVE);
	cl_fb_all = Cvar_Get ("r_fullbright", "0", 0);
	cl_fb_flat = Cvar_Get ("r_drawflat", "0", CVAR_ARCHIVE);
	cl_fb_legacyflat = Cvar_Get ("gl_hack_notextures", "0", CVAR_ARCHIVE);
	CL_CheckFullbright ();
	cl_gibmode = Cvar_Get ("g_gibmode", "0", CVAR_ARCHIVE);
	cl_nosmoke = Cvar_Get ("cl_nosmoke", "0", CVAR_ARCHIVE);
	cpu_auto = Cvar_Get ("cpu_auto", "1", CVAR_ARCHIVE);	// BDGN 3.40
	Cmd_AddCommand ("cpu_apply", CL_CPUApply);
	Cmd_AddCommand ("bdgn_lowend", CL_LowEnd_f);	/* BDGN 3.55y */
	Cmd_AddCommand ("bdgn_status", CL_Status_f);
	Cmd_AddCommand ("bdgn_spritecheck", CL_SpriteCheck_f);	/* BDGN 3.51q */
	Cmd_AddCommand ("bdgn_vramcheck", CL_VramCheck_f);	/* BDGN 3.59u */
	Cmd_AddCommand ("bdgn_bench", CL_Bench_f);
 Cmd_AddCommand("bdgn_bench_modern",CL_Bench_f);
	/* BDGN 3.60c (owner 2026-11-22): the release keeps one benchmark, bdgn_bench_presets (menu "Quick: my presets"); the
	   diagnostic and machine benchmarks (Pa1983, Chromatic, Lupusrex, K6-3 Special, CRASH HUNT, SMP, audio, 3dfx ...) are gone */
	Cmd_AddCommand ("bdgn_bench_presets", CL_BenchPresets_f);
	Cmd_AddCommand ("bdgn_profile", CL_Profile_f);	/* BDGN 3.52t */
	Cmd_AddCommand ("bdgn_fastest", CL_Fastest_f);	/* BDGN 3.52h: the ultra presets */
	Cmd_AddCommand ("bdgn_frametimes", CL_FrameTimes_f);	/* BDGN 3.52 (WESTBECK): measurement build, one line per frame */
	Cmd_AddCommand ("bdgn_bench_run", CL_BenchRun_f);
	Cmd_AddCommand ("bdgn_bench_stop", CL_BenchStop_f);

	// doogies code
	// Q2TTM: add cvar to stop cinematics from playing (cl_hack_nocinematics = 1 = no movies).
	cl_hack_nocinematics = Cvar_Get ("cl_hack_nocinematics", "0", CVAR_ARCHIVE);
	// end

	// cvar so we can remember what combination of "s_loadas8bit" and "s_khz" we are using and load the proper menu settings
	s_profile = Cvar_Get ("s_profile", "1", CVAR_ARCHIVE);
	// ends

	//
	// register our commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand ("pause", CL_Pause_f);
	Cmd_AddCommand ("pingservers", CL_PingServers_f);
	Cmd_AddCommand ("skins", CL_Skins_f);

	Cmd_AddCommand ("userinfo", CL_Userinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);

	Cmd_AddCommand ("changing", CL_Changing_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	CL_InitChatDemo ();	/* BDGN 3.59f */
	Cmd_AddCommand ("stop", CL_Stop_f);

	Cmd_AddCommand ("quit", CL_Quit_f);

	Cmd_AddCommand ("connect", CL_Connect_f);
	Cmd_AddCommand ("reconnect", CL_Reconnect_f);

	Cmd_AddCommand ("rcon", CL_Rcon_f);

// 	Cmd_AddCommand ("packet", CL_Packet_f); // this is dangerous to leave in

	Cmd_AddCommand ("setenv", CL_Setenv_f );

	Cmd_AddCommand ("precache", CL_Precache_f);

	Cmd_AddCommand ("download", CL_Download_f);

	//
	// forward to server commands
	//
	// the only thing this does is allow command completion
	// to work -- all unknown commands are automatically
	// forwarded to the server
	Cmd_AddCommand ("wave", NULL);
	Cmd_AddCommand ("inven", NULL);
	Cmd_AddCommand ("kill", NULL);
	Cmd_AddCommand ("use", NULL);
	Cmd_AddCommand ("drop", NULL);
	Cmd_AddCommand ("say", NULL);
	Cmd_AddCommand ("say_team", NULL);
	Cmd_AddCommand ("info", NULL);
	Cmd_AddCommand ("prog", NULL);
	Cmd_AddCommand ("give", NULL);
	Cmd_AddCommand ("god", NULL);
	Cmd_AddCommand ("notarget", NULL);
	Cmd_AddCommand ("noclip", NULL);
	Cmd_AddCommand ("invuse", NULL);
	Cmd_AddCommand ("invprev", NULL);
	Cmd_AddCommand ("invnext", NULL);
	Cmd_AddCommand ("invdrop", NULL);
	Cmd_AddCommand ("weapnext", NULL);
	Cmd_AddCommand ("weapprev", NULL);
}

/*
===============
CL_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
/* BDGN 3.58r (owner's rule: the game remembers every setting): many settings the menus change were registered without
   CVAR_ARCHIVE by id (cl_particles, gl_polyblend, gl_ztrick, cl_gun, r_lerpmodels ...) or by BDGN, so a change made
   in the menu was gone at the next start unless a preset had marked it. Every row of the settings pages and these
   controls of id's menus are saved. Not saved, on purpose: the test switches (sw_prefetch, sw_asmspans, the
   "SSE off" / "3DNow! off" switches bdgn_no*, cl_test_* and sw_test_*). */
static void CL_ArchiveUserSettings (void)
{
	static const char *more[] = { "cl_particles", "cl_gun", "cl_footsteps", "cl_blend", "cl_predict", "cl_vwep", "gl_polyblend",
		"gl_ztrick", "r_lerpmodels", "gl_playermip", "gl_picmip", "gl_skymip", "gl_flashblend", "gl_shadows", "cl_maxfps",
		"r_maxfps", "cl_async", "cl_maxpackets", "cl_sleep", "intensity", "gl_modulate", "cl_lightlevel", NULL };
	int i;
	for (i = 0; i < bdgn_tweak_count; i++)
	{
		const char *n = bdgn_tweaks[i].cvar;
		if (!n || !n[0] || !strncmp (n, "bdgn_no", 7) || !strncmp (n, "cl_test_", 8) || !strncmp (n, "sw_test_", 8) ||
			!strcmp (n, "sw_prefetch") || !strcmp (n, "sw_asmspans"))
			continue;
		if (Cvar_Exists ((char *)n))
			Cvar_Get ((char *)n, "0", CVAR_ARCHIVE);	/* an existing cvar keeps its value, it only gets the flag */
		if (bdgn_tweaks[i].aux && Cvar_Exists ((char *)bdgn_tweaks[i].aux))
			Cvar_Get ((char *)bdgn_tweaks[i].aux, "0", CVAR_ARCHIVE);
	}
	for (i = 0; more[i]; i++)
		if (Cvar_Exists ((char *)more[i]))
			Cvar_Get ((char *)more[i], "0", CVAR_ARCHIVE);
}

/* BDGN 3.59z: config.new -> config.cfg only when the contents differ; the same contents: config.new is removed */
static void CL_ConfigReplaceIfChanged (const char *newpath)
{
	char cfg[MAX_OSPATH];
	FILE *a, *b;
	int ca, cb, same = 1;
	if (!strstr (newpath, "config.new"))
		return;	/* cfg_writechanged 0: written straight to config.cfg */
	Com_sprintf (cfg, sizeof(cfg), "%s/config.cfg", FS_Gamedir());
	a = fopen (newpath, "rb");
	b = fopen (cfg, "rb");
	if (!a) { if (b) fclose (b); return; }
	if (!b) same = 0;
	else
		do { ca = fgetc (a); cb = fgetc (b); if (ca != cb) { same = 0; break; } } while (ca != EOF);
	fclose (a);
	if (b) fclose (b);
	if (same) { remove (newpath); return; }
	remove (cfg);	/* Windows 9x: rename does not replace an existing file */
	if (rename (newpath, cfg))
	{	/* copy instead, so config.cfg is never left missing */
		a = fopen (newpath, "rb");
		b = fopen (cfg, "wb");
		if (a && b) while ((ca = fgetc (a)) != EOF) fputc (ca, b);
		if (a) fclose (a);
		if (b) fclose (b);
		if (!a || !b) Com_Printf ("Couldn't replace config.cfg (the new settings are in config.new).\n");
		else remove (newpath);
	}
}
void CL_WriteConfiguration (void)
{
	FILE	*f;
	char	path[MAX_OSPATH];

#ifdef _WIN32
	if (!Sys_StartupConfigCanWrite () || !VID_ConfigCanWrite ())
		return;
#endif
	if (CL_BenchmarkRunning ()) { Com_Printf("configuration write skipped during benchmark\n"); return; }

	if (cls.state == ca_uninitialized)
		return;

	/* BDGN 3.59z (Q2PRO): written to config.new first; config.cfg is replaced only when something changed
	   (cfg_writechanged 1, menu CONSOLE), so quitting does not rewrite the file every time */
	{
		static cvar_t *wc;
		if (!wc) wc = Cvar_Get ("cfg_writechanged", "1", CVAR_ARCHIVE);
		Com_sprintf (path, sizeof(path), wc->value ? "%s/config.new" : "%s/config.cfg", FS_Gamedir());
	}
	f = fopen (path, "w");
	if (!f)
	{
		Com_Printf ("Couldn't write config.cfg.\n");
		return;
	}

	CL_ArchiveUserSettings ();	/* BDGN 3.58r */
	fprintf (f, "// generated by quake, do not modify\n");
	Key_WriteBindings (f);
	fclose (f);

#ifdef _WIN32
	VID_ConfigWriteBegin ();	/* BDGN 3.58r: the chosen renderer, also in a Software fallback session */
#endif
	Cvar_WriteVariables (path);
#ifdef _WIN32
	VID_ConfigWriteEnd ();
#endif
	CL_ConfigReplaceIfChanged (path);	/* BDGN 3.59z */
}

/*
==================
CL_FixCvarCheats

==================
*/

typedef struct
{
	char	*name;
	char	*value;
	cvar_t	*var;
} cheatvar_t;

cheatvar_t	cheatvars[] = {
	/* BDGN 3.46: only what changes the game itself or lets you see through walls is reset in multiplayer.
	   Looks (r_drawflat, r_fullbright, gl_lightmap, gl_saturatelighting, cl_testlights, sw_draworder) are free;
	   timedemo and paused are not touched either (the server decides about pause). */
	{"timescale", "1"},
	{"fixedtime", "0"},
	{"r_drawworld", "1"},
	{NULL, NULL}
};

int		numcheatvars;

void CL_FixCvarCheats (void)
{
	int			i;
	cheatvar_t	*var;

	if ( !strcmp(cl.configstrings[CS_MAXCLIENTS], "1")
		|| !cl.configstrings[CS_MAXCLIENTS][0] )
		return;		// single player can cheat

	// find all the cvars if we haven't done it yet
	if (!numcheatvars)
	{
		while (cheatvars[numcheatvars].name)
		{
			cheatvars[numcheatvars].var = Cvar_Get (cheatvars[numcheatvars].name,
					cheatvars[numcheatvars].value, 0);
			numcheatvars++;
		}
	}

	// make sure they are all set to the proper values
	for (i=0, var = cheatvars ; i<numcheatvars ; i++, var++)
	{
		if ( strcmp (var->var->string, var->value) )
		{
			Cvar_Set (var->name, var->value);
		}
	}
}

//============================================================================

/*
==================
CL_SendCommand

==================
*/
/* BDGN 3.58q: fixed 100 Hz physics in use: switched on, in a game (not a demo, a cinematic or a timedemo) */
qboolean CL_AsyncActive (void)
{
	return cl_async && cl_async->value && cls.state == ca_active && !cl.attractloop && !cl_timedemo->value &&
		!cl_paused->value;
}

void CL_SendCommand (void)
{
	// get new key events
	Sys_SendKeyEvents ();

	// allow mice or other external controllers to add commands
	IN_Commands ();

	// process console commands
	Cbuf_Execute ();

	// fix any cheating cvars
	CL_FixCvarCheats ();

	// send intentions now
	if (CL_AsyncActive ())
		CL_AsyncSendCmd ();	/* BDGN 3.58q: fixed 10 ms moves */
	else
	{
		if (cl_async_pending)
			CL_AsyncReset ();
		CL_SendCmd ();
	}

	// resend a connection request if necessary
	CL_CheckForResend ();
}

/*
==================
CL_Frame

==================
*/
/* Drawflat takes priority regardless of menu/config/console command order. */
void CL_CheckFullbright (void)
{
	cl_fullbright_active = false;
	if (!cl_fb_world || !cl_fb_all || !cl_fb_flat || !cl_fb_legacyflat)
		return;
	if (cl_fb_flat->value || cl_fb_legacyflat->value)
	{
		if (cl_fb_world->value) Cvar_SetValue ("gl_fastworld", 0);
		if (cl_fb_all->value) Cvar_SetValue ("r_fullbright", 0);
		return;
	}
	cl_fullbright_active = (cl_fb_world->value || cl_fb_all->value) ? true : false;
}

/*
BDGN 3.52 (WESTBECK) measurement build: bdgn_frametimes [seconds] (1-60, default 10).  Records every drawn frame: ms
since the previous drawn frame, ms drawing it (SCR_UpdateScreen) and whether a new server frame arrived; writes
bdgn_frametimes_<n>.txt in the game folder with a summary and one line per frame (client/cl_frametimes.h).
Works while playing (the server runs); the timedemo statistics (3.52v) are printed at the end of every timedemo.
*/
#include "cl_frametimes.h"
static ft_log_t	cl_ftl;
static int		cl_ftl_on, cl_ftl_end, cl_ftl_server, cl_ftl_skip;
static unsigned	cl_ftl_prev_us;	/* BDGN 3.57z: start of the previous drawn frame's drawing, microseconds */

static void CL_FrameTimesWrite (void)
{
	ft_summary_t s;
	char name[MAX_OSPATH];
	FILE *f;
	int i;

	FT_Summarize (&cl_ftl, &s);
	Com_Printf ("bdgn_frametimes: %d frames, avg %.1f ms (%.0f fps), min %d, max %d, %d over 1.5 x avg; drawing avg %.1f ms\n",
		s.n, s.avg, s.avg > 0 ? 1000.0 / s.avg : 0.0, s.min, s.max, s.over, s.render_avg);
	Com_Printf ("  frames with a new server frame: %d, avg %.1f ms; other frames avg %.1f ms\n", s.nserver, s.avg_server, s.avg_other);
	Com_Printf ("  microseconds: frame %.0f, drawing %.0f, of it glFinish %.0f and SwapBuffers %.0f\n", s.interval_us, s.render_us, s.finish_us, s.swap_us);
	Cvar_SetValue ("bdgn_ft_active", 0);	/* 3.57z */
	Com_sprintf (name, sizeof(name), "%s/bdgn_frametimes_%u.txt", FS_Gamedir (), (unsigned)Sys_Milliseconds ());
	f = fopen (name, "w");
	if (!f)
	{
		Com_Printf ("bdgn_frametimes: could not write %s\n", name);
		return;
	}
	fprintf (f, "BDGN %s bdgn_frametimes\nvid_ref %s  cl_maxfps %s  cl_sleep %s  cl_maxpackets %s  timedemo %s  map %s\n",
		BDGN_VERSION_STRING, Cvar_VariableString ("vid_ref"), Cvar_VariableString ("cl_maxfps"), Cvar_VariableString ("cl_sleep"),
		Cvar_VariableString ("cl_maxpackets"), Cvar_VariableString ("timedemo"), cl.configstrings[CS_MODELS+1]);
	fprintf (f, "frames %d (not stored: %d)\nframe time ms: avg %.2f min %d max %d, over 1.5 x avg %d\ndrawing ms: avg %.2f\n",
		s.n, cl_ftl.dropped, s.avg, s.min, s.max, s.over, s.render_avg);
	fprintf (f, "frames with a new server frame %d: avg %.2f ms; other frames: avg %.2f ms\n", s.nserver, s.avg_server, s.avg_other);
	fprintf (f, "microseconds (3.57z): frame %.0f, drawing %.0f, of the drawing glFinish %.0f (only with 'finish', -1 = not measured) and SwapBuffers %.0f; gl_mode %s, vid_fullscreen %s, gl_swapinterval %s\n",
		s.interval_us, s.render_us, s.finish_us, s.swap_us, Cvar_VariableString ("gl_mode"), Cvar_VariableString ("vid_fullscreen"), Cvar_VariableString ("gl_swapinterval"));
	fprintf (f, "histogram (ms: frames):");
	for (i = 0 ; i < FT_BUCKETS ; i++)
		if (s.hist[i])
			fprintf (f, " %d%s:%d", i, i == FT_BUCKETS - 1 ? "+" : "", s.hist[i]);
	fprintf (f, "\n\nframe\tms\tdraw_ms\tserver\tframe_us\tdraw_us\tfinish_us\tswap_us\n");
	for (i = 0 ; i < cl_ftl.n ; i++)
		fprintf (f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", i, cl_ftl.interval[i], cl_ftl.render[i], cl_ftl.server[i],
			cl_ftl.interval_us[i], cl_ftl.render_us[i], cl_ftl.finish_us[i], cl_ftl.swap_us[i]);
	fclose (f);
	Com_Printf ("  written to %s\n", name);
}

static void CL_FrameTimes_f (void)
{
	int seconds = Cmd_Argc () > 1 ? atoi (Cmd_Argv (1)) : 10;
	if (bench_phase || bench_all)
	{	/* writing the file would fall inside a timed run */
		Com_Printf ("bdgn_frametimes: not while bdgn_bench is running\n");
		return;
	}
	if (seconds < 1) seconds = 1;
	if (seconds > 60) seconds = 60;
	memset (&cl_ftl, 0, sizeof(cl_ftl));
	cl_ftl_server = cl.frame.serverframe;
	cl_ftl_end = Sys_Milliseconds () + seconds * 1000;
	cl_ftl_on = 1;
	cl_ftl_skip = 1;		/* the frame this command runs in began before it */
	cl_ftl_prev_us = 0;
	Cvar_Get ("bdgn_ft_active", "0", 0);
	Cvar_Get ("bdgn_ft_finish", "0", 0);
	Cvar_SetValue ("bdgn_ft_active", 1);	/* 3.57z: the OpenGL renderer times SwapBuffers (win32/glw_imp.c) */
	Cvar_SetValue ("bdgn_ft_finish", Cmd_Argc () > 2 && !Q_stricmp (Cmd_Argv (2), "finish"));	/* and a glFinish before it */
	Cvar_SetValue ("bdgn_ft_swap_us", -1); Cvar_SetValue ("bdgn_ft_finish_us", -1);
	Com_Printf ("bdgn_frametimes: recording %d s (play normally; the result is written when it ends)\n", seconds);
}

void CL_Frame (int msec)
{
#ifdef _WIN32
 qboolean lighting_done=false;
#endif
	int			ft_ms = 0, ft_r0 = 0, ft_draw = 0, ft_server = 0;	/* BDGN 3.52 (WESTBECK): bdgn_frametimes */
	unsigned	ft_u0 = 0, ft_udraw = 0;	/* BDGN 3.57z: the same in microseconds */
	static int	extratime;
	static int  lasttimecalled;
	extern cvar_t *cl_fastsqrt, *cl_fastsqrt2;
	static cvar_t *gl_3dnow_cv;

	if (dedicated->value)
		return;

	{	/* BDGN 3.52w: gl_flashblend is always off (big glowing balls around rockets and blaster shots); presets, old
		   configs and the console cannot switch it on (every renderer, also AMD's ref_glam) */
		static cvar_t *flashblend;
		if (!flashblend) flashblend = Cvar_Get ("gl_flashblend", "0", 0);
		if (flashblend->value) Cvar_Set ("gl_flashblend", "0");
	}
	CL_BenchFrame ();		/* BDGN 3.42 */
	CL_DemoControlFrame ();	/* BDGN 3.59f */

	if (cl_sweep_frames > 0)		/* BDGN 3.46: stray empty windows (task bar buttons on Windows 9x) after a map load */
	{
		cl_sweep_frames--;
		if ((cl_sweep_frames % 15) == 0)
			Sys_SweepStrayWindows ();
	}

	if (!gl_3dnow_cv)
		gl_3dnow_cv = Cvar_Get ("gl_3dnow", "1", CVAR_ARCHIVE);
	CL_3DNowEnforce ();	/* BDGN 3.51aut: 3DNow! is automatic, nothing can switch it off */
	{	/* BDGN 3.52f: static 2d enemies / classic 2d sprites mode set without the menu (a preset, the defaults, config.cfg)
		   left cl_simpleenemies as it was, e.g. flat 2D enemies with "static 2d enemies: off".  Not during bdgn_bench,
		   whose DUKE cases set cl_simpleenemies themselves. */
		static cvar_t *s2d;
		if (!s2d)
			s2d = Cvar_Get ("cl_simple2denemies", "0", CVAR_ARCHIVE);
		if ((s2d->modified || cl_classic2d->modified) && !bench_phase)
		{
			if (cl_classic2d->modified && cl_classic2d->value)
				Cvar_SetValue ("cl_simple_rail", 0);
			s2d->modified = cl_classic2d->modified = false;
			BDGN_SyncEnemyLook ();
		}
		/* BDGN 3.57e: classic 2d sprites mode on means 2D sprite enemies, whatever else changed cl_simpleenemies
		   (a preset, the far 2D enemies row, a config file): it is put back here */
		else if (!bench_phase && cl_classic2d->value && !s2d->value && cl_simpleenemies->value < 3)
			BDGN_SyncEnemyLook ();
	}
	CL_ModelLodSync ();	/* BDGN 3.57o: model LOD by distance (cl_modellod) sets its three rows when it changes */
	CL_CPUProfileApply ();	/* BDGN 3.58w: when cpu_profile changes (and once at start) */
	/* Default/Fast restore original visual effects; CPU kernels still follow detection. */
	cl_3dnow_fx = 0; /* Visual reductions follow cl_fastfx only; CPU detection is independent. */

	if (cl_fastsqrt && cl_fastsqrt2)		// BDGN 3.37 TEST: fast sqrt flags for pmove / prediction / client effects
		Q_SetFastSqrt ((int)cl_fastsqrt->value, (int)cl_fastsqrt2->value);

	extratime += msec;

	if (!cl_timedemo->value)
	{
		if (cls.state == ca_connected && extratime < 100)
			return;			// don't flood packets out while connecting
		if (CL_AsyncActive ())
		{	/* BDGN 3.58q: the picture is paced by r_maxfps (microseconds, so 280 is 280), not cl_maxfps; the moves
			   are 10 ms whatever the picture does (CL_AsyncSendCmd) */
			static unsigned	last_us;
			float			cap = r_maxfps->value;
			if (cap > 0)
			{
				unsigned	now = Sys_Microseconds (), iv;
				if (cap < 10) cap = 10;
				iv = (unsigned)(1000000.0f / cap);
				if (now - last_us < iv)
				{
					int sleep = CL_IdleSleep ((int)cl_sleep->value, (int)((iv - (now - last_us)) / 1000));
					if (sleep >= 0)
						Sys_Sleep (sleep);
					return;
				}
				/* keep the rhythm (280 = 3571 us every frame) unless more than a frame late */
				last_us = (now - last_us < 2 * iv) ? last_us + iv : now;
			}
		}
		else if (extratime < 1000/cl_maxfps->value)
		{	// framerate is too high
			int sleep = CL_IdleSleep ((int)cl_sleep->value, (int)(1000/cl_maxfps->value - extratime));	/* BDGN 3.51ar */
			if (sleep >= 0)
				Sys_Sleep (sleep);
			return;
		}
	}

	// let the mouse activate or deactivate
	IN_Frame ();

	// decide the simulation time
	cls.frametime = extratime/1000.0;
	cl.time += extratime;
	cls.realtime = curtime;

	ft_ms = extratime;	/* BDGN 3.52 (WESTBECK): bdgn_frametimes, time since the previous drawn frame */
	cl_async_frame_ms = extratime;	/* BDGN 3.58q: whole ms for the fixed physics moves */
	extratime = 0;
#if 0
	if (cls.frametime > (1.0 / cl_minfps->value))
		cls.frametime = (1.0 / cl_minfps->value);
#else
	if (cls.frametime > (1.0 / 5))
		cls.frametime = (1.0 / 5);
#endif

	// if in the debugger last frame, don't timeout
	if (msec > 5000)
		cls.netchan.last_received = Sys_Milliseconds ();

	// fetch results from server
	CL_ReadPackets ();
	if (cl_ftl_on)
	{	/* BDGN 3.52 (WESTBECK): bdgn_frametimes, did a new server frame arrive in this frame (0 = cleared at a map change) */
		ft_server = cl.frame.serverframe != cl_ftl_server && cl.frame.serverframe != 0;
		cl_ftl_server = cl.frame.serverframe;
	}

	// send a new command message to the server
	CL_SendCommand ();

	CL_CheckFullbright ();	// after console/config commands, before renderer setup

	// predict all unacknowledged movements
	CL_PredictMovement ();

	// allow rendering DLL change
	VID_CheckChanges ();
	if (!cl.refresh_prepped && cls.state == ca_active)
		CL_PrepRefresh ();
	else if (cl.refresh_prepped)
		CL_HDAssetsFrame ();	/* BDGN 3.59o: HD effects skipped at load, classic 2D turned off since */

	// update the screen
	if (host_speeds->value)
		time_before_ref = Sys_Milliseconds ();
	if (cl_ftl_on)
	{
		ft_r0 = Sys_Milliseconds ();
		ft_u0 = Sys_Microseconds ();
	}
	if(bench_stage_log && bench_phase==3 && !bench_first_frame &&
	   cls.state==ca_active && !cls.disable_screen && cl.timedemo_frames>0) {
		BenchNote("DRIVER_STAGE",va("case %i / repeat %i: entering first demo frame",bench_cur,bench_repeat+1));
		fflush(bench_log);
		SCR_UpdateScreen ();
		bench_first_frame=true;
		BenchNote("DRIVER_STAGE",va("case %i / repeat %i: first demo frame returned",bench_cur,bench_repeat+1));
		fflush(bench_log);
	} else SCR_UpdateScreen ();
	if (cl_ftl_on)
	{
		ft_draw = Sys_Milliseconds () - ft_r0;
		ft_udraw = Sys_Microseconds () - ft_u0;
	}
	if (host_speeds->value)
		time_after_ref = Sys_Milliseconds ();
	if (!cls.disable_screen)
		Com_StartupReport ();	// BDGN 3.50: once, at the first frame that is drawn
	CL_AutoPresetFrame ();	/* BDGN 3.55o */

#ifdef _WIN32

#endif

	// update audio
	S_Update (cl.refdef.vieworg, cl.v_forward, cl.v_right, cl.v_up);
#ifdef _WIN32
	if (S_FullAudioPending ()) {
		CL_RunDLights ();
		CL_RunLightStyles ();
		S_FullAudioFinish ();
		lighting_done=true;
	}
#endif

	CDAudio_Update();
	MIDI_Update ();		// BDGN 3.49h: MIDI note offs and music
	if (cl_sound_restart_pending && MIDI_IsIdle () && WavMusic_IsIdle () && CDAudio_IsIdle ()) CL_Snd_Restart_f ();
	WavMusic_Update ();

	// advance local effects for the next frame
#ifdef _WIN32
	if (!lighting_done)
#endif
	{
		CL_RunDLights ();
		CL_RunLightStyles ();
	}
	SCR_RunCinematic ();
	SCR_RunConsole ();

	cls.framecount++;
	if (cl_ftl_on)
	{	/* BDGN 3.52 (WESTBECK): bdgn_frametimes; only frames drawn in the game (not loading, not with the screen off) */
		if (bench_phase)
		{
			cl_ftl_on = 0;
			Cvar_SetValue ("bdgn_ft_active", 0);
			Com_Printf ("bdgn_frametimes: stopped, bdgn_bench started\n");
		}
		else
		{
			if (cl_ftl_skip)
				cl_ftl_skip = 0;
			else if (cls.state == ca_active && !cls.disable_screen)
			{
				int k = cl_ftl.n;
				FT_Add (&cl_ftl, ft_ms, ft_draw, ft_server);
				if (cl_ftl.n > k)
				{	/* 3.57z: microseconds; finish / swap come from the OpenGL renderer (-1 elsewhere) */
					qboolean gl = !Q_stricmp (Cvar_VariableString ("vid_ref"), "gl");
					cl_ftl.interval_us[k] = cl_ftl_prev_us ? (int)(ft_u0 - cl_ftl_prev_us) : -1;
					cl_ftl.render_us[k] = (int)ft_udraw;
					cl_ftl.finish_us[k] = gl ? (int)Cvar_VariableValue ("bdgn_ft_finish_us") : -1;
					cl_ftl.swap_us[k] = gl ? (int)Cvar_VariableValue ("bdgn_ft_swap_us") : -1;
				}
			}
			if (ft_u0)
				cl_ftl_prev_us = ft_u0;
			if (Sys_Milliseconds () - cl_ftl_end >= 0)
			{
				cl_ftl_on = 0;
				CL_FrameTimesWrite ();
			}
		}
	}

	if ( log_stats->value )
	{
		if ( cls.state == ca_active )
		{
			if ( !lasttimecalled )
			{
				lasttimecalled = Sys_Milliseconds();
				if ( log_stats_file )
					fprintf( log_stats_file, "0\n" );
			}
			else
			{
				int now = Sys_Milliseconds();

				if ( log_stats_file )
					fprintf( log_stats_file, "%d\n", now - lasttimecalled );
				lasttimecalled = now;
			}
		}
	}
}

//============================================================================

/*
====================
CL_Init
====================
*/
cvar_t *cl_simpleitems;
cvar_t *cl_simpleenemies;
cvar_t *cl_simpleenemy_anim;
cvar_t *cl_simpleenemy_lod;
cvar_t *cl_weaponsprites, *cl_weaponsprite_anim, *cl_weapon_simple;	/* BDGN 3.51q */

void CL_Init (void)
{
	if (dedicated->value)
		return;		// nothing running on the client

	// all archived variables will now be loaded

	cl_simpleitems = Cvar_Get ("cl_simpleitems", "0", CVAR_ARCHIVE);
	cl_simpleenemies = Cvar_Get ("cl_simpleenemies", "0", CVAR_ARCHIVE);
	cl_simpleenemy_anim = Cvar_Get ("cl_simpleenemy_anim", "0", CVAR_ARCHIVE);
	cl_simpleenemy_lod = Cvar_Get ("cl_simpleenemy_lod", "1", CVAR_ARCHIVE);	/* BDGN 3.51s: automatic (on) by default: half/quarter rate far away, near-still beyond 2048 */
	CL_MigrateSpriteRate ();	/* BDGN 3.51q: a saved 25/s setting (3) becomes 12.5/s (1) */
	cl_weaponsprites = Cvar_Get ("cl_weaponsprites", "0", CVAR_ARCHIVE);	/* BDGN 3.51q: 1 = 2D first-person weapon */
	cl_weaponsprite_anim = Cvar_Get ("cl_weaponsprite_anim", "2", CVAR_ARCHIVE);	/* BDGN 3.52h: not read any more; the 2D weapon always runs at 6.25 pictures/s */
	cl_weapon_simple = Cvar_Get ("cl_weapon_simple", "0", CVAR_ARCHIVE);	/* 1 = one static picture + shared flash */
	Con_Init ();
	if (Cvar_VariableValue("bdgn_extra_lowram") && !CL_LowRamPresetChosen()) {	/* 3.55j: a chosen below-32 MB or S3 Virge preset is kept */
		if (!CL_S3VirgeChosen()) {	/* 3.55j: the S3 Virge renderer is kept on below-32 MB machines */
			Cvar_Set("vid_ref","soft");
			Cvar_SetValue("sw_mode",0);
			Cvar_SetValue("sw_render_scale",100);
		}
		Cvar_SetValue("s_musicmode",2);
		Cvar_SetValue("cd_nocd",1);
		Cvar_SetValue("s_khz",11);
		Cvar_SetValue("s_loadas8bit",1);
		Cvar_SetValue("s_maxchannels",8);
		Cvar_SetValue("s_basic_sounds",1);
	}
#if defined __linux__ || defined __sgi
	S_Init ();
	VID_Init ();
#else
	VID_Init ();
	Com_StartupMark ("video (window, OpenGL, renderer)");
	S_Init ();	// sound must be initialized after window is created
	Com_StartupMark ("sound (DirectSound)");
#endif

	V_Init ();

	net_message.data = net_message_buffer;
	net_message.maxsize = sizeof(net_message_buffer);

	M_Init ();

	SCR_Init ();
	cls.disable_screen = true;	// don't draw yet
	Com_StartupMark ("menus, screen");

	CDAudio_Init ();
	Com_StartupMark ("CD audio and WAV");
	MIDI_Init ();
	Com_StartupMark ("MIDI setup");
	CL_InitLocal ();
	IN_Init ();
	Com_StartupMark ("client cvars, input");

	// the fast config preset keeps working over restarts: its cvars must be saved again at exit
	if (Q_stricmp (cfg_preset->string, "fast") == 0)
		CL_ArchivePresetFile ("fastconfig.cfg");
	else if (Q_stricmp (cfg_preset->string, "ultra") == 0)
		CL_ArchivePresetFile ("ultraconfig.cfg");	// BDGN 3.37
	else if (Q_stricmp (cfg_preset->string, "psycho") == 0)
		CL_ArchivePresetFile ("psychoconfig.cfg");
	else if (Q_stricmp (cfg_preset->string, "faster") == 0)
		CL_ArchivePresetFile ("fasterconfig.cfg");	// BDGN 3.49
	else if (Q_stricmp (cfg_preset->string, "maxfidelity") == 0)
		CL_ArchivePresetFile ("MAXfidelity.cfg");	// BDGN 3.49

#ifdef _WIN32
	if (!Sys_StartupConfigRecovering ())
#endif
		FS_ExecAutoexec ();
	Cbuf_Execute ();
	Com_StartupMark ("preset file, autoexec.cfg");

	CL_CPUApply ();		// BDGN 3.40: use what the CPU supports (after config.cfg and autoexec.cfg)
	Cvar_Get ("gl_swapinterval", "0", CVAR_ARCHIVE);
	Cvar_SetValue ("gl_swapinterval", 0);	/* BDGN 3.58o: vsync is always off (owner, 2026-11-20), whatever an old config says */
	if (Cvar_VariableValue ("bdgn_cfgver") < 346)
	{	// BDGN 3.44: 3.40-3.43 saved gl_fastdraw 3 with "performance tweaks"; without 3DNow! it is slower than plain drawing
		// (with 3DNow! it is switched on by itself).  gl_maxtexsize/low detail are no longer part of any mode.
		// Legacy configuration migration; text batching was removed.
		Cvar_SetValue ("gl_fastdraw", 0);
		Cvar_SetValue ("s_mixinterval", 15);
		Cvar_Get ("bdgn_cfgver", "0", CVAR_ARCHIVE);
		Cvar_SetValue ("bdgn_cfgver", 346);
	}
	if (Cvar_VariableValue ("bdgn_cfgver") < 349)
	{	// BDGN 3.49f: the game default is automatic (cpu_auto 1: 3DNow!/SSE on when the CPU has them).  3.49e presets saved cpu_auto 0.
		Cvar_SetValue ("cpu_auto", 1);
		CL_CPUApply ();
		Cvar_Get ("bdgn_cfgver", "0", CVAR_ARCHIVE);
		Cvar_SetValue ("bdgn_cfgver", 349);
	}
	if (Cvar_VariableValue ("bdgn_cfgver") < 352)
	{	// BDGN 3.52i: OpenGL multitexture off by default (K6-III+/GF2: 116.8 off, 114.5 on); stays on with the 3dfx drivers
		const char *drv = Cvar_VariableString ("gl_driver");
		if (Q_stricmp (drv, "3dfxgl") && Q_stricmp (drv, "3dfxvgl"))
			Cvar_SetValue ("gl_ext_multitexture", 0);
		Cvar_Get ("bdgn_cfgver", "0", CVAR_ARCHIVE);
		Cvar_SetValue ("bdgn_cfgver", 352);
	}
	if (Cvar_VariableValue ("bdgn_cfgver") < 358)
	{	/* BDGN 3.58r, owner's rule (standard controls, 2026-11-20): inverted mouse in every release until FINAL. Once, in
		   a config from an older version that has the mouse not inverted; changed back afterwards it stays as chosen. */
		if (Cvar_VariableValue ("m_pitch") > 0)
			Cvar_SetValue ("m_pitch", -Cvar_VariableValue ("m_pitch"));
		Cvar_Get ("bdgn_cfgver", "0", CVAR_ARCHIVE);
		Cvar_SetValue ("bdgn_cfgver", 358);
	}
	Com_StartupMark ("CPU options");
	CL_LowRamCheck ();		/* BDGN 3.51bdt */
	CL_AutoPresetInit ();	/* BDGN 3.55o */
}

/*
===============
CL_Shutdown

FIXME: this is a callback from Sys_Quit and Com_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void CL_Shutdown(void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

	if (bench_phase) CL_BenchFinish("shutdown");
	CL_WriteConfiguration ();

	CDAudio_Shutdown ();
	MIDI_FinalShutdown ();	// release a pending startup before process exit
	S_Shutdown();
	IN_Shutdown ();
	VID_Shutdown();
}

