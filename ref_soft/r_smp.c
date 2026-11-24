/*
r_smp.c -- BDGN 3.51p: optional software SMP for the software renderer (sw_smp, default 0 = off).

sw_smp 0  off: the renderer works exactly as before (nothing in this file runs).
sw_smp 1  on when Windows gives the game more than one processor (dual CPU, Hyper-Threading, multi-core;
          Windows 95/98/ME always report one, so nothing changes there).
sw_smp 2  always on, also with one processor: for checking the pictures on any PC (no speed gain there).

What runs in parallel: building the surface cache (lit wall textures). D_DrawSurfaces calls R_SMP_Prebuild before
it draws: every solid surface of the frame whose cache is missing or out of date (texture animation, light styles,
dynamic lights - the same test as D_CacheSurface) gets its cache block now, allocated in order on the main thread,
and is then built by two threads at once: the worker takes every second surface, the main thread the others. Each
thread has its own copy of the building state (smpctx_t); the original builders keep their globals and are not
used by the threads. Afterwards the normal draw loop finds the blocks ready (cache->smpstamp) and only draws spans.
If an allocation in the batch evicts a block allocated earlier in the same batch (surface cache too small), the
batch is cancelled before any thread starts and those blocks are marked out of date: the normal path rebuilds them.
The builders are copies of R_BuildLightMap/R_AddDynamicLights/R_DrawSurface/R_DrawSurfaceBlock8_mip*; the four
block drawers are one function with the block size as a parameter (same integer arithmetic). test/smp_surface_test.c
checks that the bytes are identical to the original path. The worker runs with the main thread's x87 precision and
rounding, so float steps round the same way. Not done in parallel: edges, spans, models, particles.
*/
#include "r_local.h"
#include "../qcommon/software_smp.h"
#include "sw_fastpath.h"	/* BDGN 3.56c */
#include "../qcommon/bdgn_prof.h"	/* BDGN 3.52v: surface build counter */
#ifdef _WIN32
#include <windows.h>
#include <float.h>
#include <process.h>
#endif

cvar_t		*sw_smp;
unsigned	r_smp_active;		/* stamp of the batch D_CacheSurface may use; 0 = none */
static unsigned	r_smp_stamp;

surfcache_t *D_SCAlloc (int width, int size);	/* r_surf.c */

#define SMP_MAXJOBS		4096

typedef struct
{
	msurface_t	*surf;
	surfcache_t	*cache;
	image_t		*image;
	int			mip, lane, cost;
	int			lightadj[MAXLIGHTMAPS];
} smpjob_t;

typedef struct
{
	unsigned		blocklights[1024];
	/* frame constants, read once on the main thread */
	int				fullbright, dynamic, lightdata, framecount, oldlight;	/* oldlight: BDGN 3.57 */
	float			modulate;
	/* the surface being built (r_drawsurf of the original) */
	msurface_t		*surf;
	image_t			*image;
	int				lightadj[MAXLIGHTMAPS];
	int				surfmip, surfwidth, surfheight, rowbytes;
	byte			*surfdat;
	/* R_DrawSurface state (globals of r_surf.c in the original) */
	int				lightleft, lightright, lightleftstep, lightrightstep;
	int				sourcetstep, surfrowbytes, stepback, lightwidth, numvblocks;
	unsigned		*lightptr;
	byte			*prowdestbase, *pbasesource, *sourcemax;
} smpctx_t;

static smpjob_t	smp_jobs[SMP_MAXJOBS];
static int		smp_njobs;
static int smp_weighted;
#define SMP_MAX_THREADS 4
static smpctx_t smp_ctx[SMP_MAX_THREADS];
static int smp_threads = 2;
static void (*smp_draw_task)(void *, int, int);
static void *smp_draw_data;
static unsigned quality_cache_batches, quality_filter_batches;

void R_SmpQualityStats (void)
{
 char result[100];
 if(ri.Cmd_Argc()>1 && !strcmp(ri.Cmd_Argv(1),"reset")) {
  quality_cache_batches=quality_filter_batches=0;
  return;
 }
 Com_sprintf(result,sizeof(result),"cache batches=%u, filter batches=%u",
             quality_cache_batches,quality_filter_batches);
 ri.Cvar_Set("sw_quality_stats_result",result);
}

static void SMP_RunJobs (smpctx_t *ctx, int first, int step);

/* ---------------------------------------------------------------- builders (copies of the original arithmetic) */

/* R_TextureAnimation with the entity frame passed in (the original reads currententity) */
static image_t *SMP_TextureAnimation (mtexinfo_t *tex, int frame)
{
	int c;

	if (sw_staticmap->value || !tex->next || tex->numframes <= 0)
		return tex->image;
	c = frame % tex->numframes;
	if (c < 0)
		c = 0;
	while (c)
	{
		tex = tex->next;
		c--;
	}
	return tex->image;
}

static void SMP_AddDynamicLights (smpctx_t *ctx)
{
	msurface_t	*surf = ctx->surf;
	int			lnum, sd, td, s, t, i, smax, tmax, negativeLight;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	unsigned	*blocklights = ctx->blocklights;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;
		dl = &r_newrefdef.dlights[lnum];
		rad = dl->intensity;
		negativeLight = 0;
		if (rad < 0)
		{
			negativeLight = 1;
			rad = -rad;
		}
		dist = DotProduct (dl->origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs(dist);
		minlight = 32;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;
		for (i=0 ; i<3 ; i++)
			impact[i] = dl->origin[i] - surf->plane->normal[i]*dist;
		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];
		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		for (t = 0 ; t<tmax ; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (!negativeLight)
				{
					if (dist < minlight)
						blocklights[t*smax + s] += (rad - dist)*256;
				}
				else
				{
					if (dist < minlight)
						blocklights[t*smax + s] -= (rad - dist)*256;
					if (blocklights[t*smax + s] < minlight)
						blocklights[t*smax + s] = minlight;
				}
			}
		}
	}
}

static void SMP_BuildLightMap (smpctx_t *ctx)
{
	int			smax, tmax, i, size, maps;
	byte		*lightmap;
	unsigned	scale;
	msurface_t	*surf = ctx->surf;
	unsigned	*blocklights = ctx->blocklights;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;

	if (ctx->fullbright || !ctx->lightdata)
	{
		for (i=0 ; i<size ; i++)
			blocklights[i] = 0;
		return;
	}
	for (i=0 ; i<size ; i++)
		blocklights[i] = 0;
	if (ctx->oldlight == 2)
	{	/* BDGN 3.57 Blake Stone shading: the light maps are not read at all */
		SW_OldLight (surf, blocklights, size);
		SW_LightmapFinish (blocklights, size, ctx->modulate);
		return;
	}

	lightmap = surf->samples;
	if (lightmap)
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
		{
			scale = ctx->lightadj[maps];
			for (i=0 ; i<size ; i++)
				blocklights[i] += lightmap[i] * scale;
			lightmap += size;
		}

	if (ctx->oldlight)
		SW_OldLight (surf, blocklights, size);	/* BDGN 3.57 */
	else if (ctx->dynamic && surf->dlightframe == ctx->framecount)
		SMP_AddDynamicLights (ctx);

	SW_LightmapFinish (blocklights, size, ctx->modulate);	/* BDGN 3.56c: as R_BuildLightMap */
}

/* R_DrawSurfaceBlock8_mip0..3 in one: shift = 4 - mip, block = 1 << shift */
static void SMP_DrawBlock (smpctx_t *ctx, int shift)
{
	int				v, i, b, lightstep, lighttemp, light, block = 1 << shift;
	unsigned char	pix, *psource, *prowdest;

	psource = ctx->pbasesource;
	prowdest = ctx->prowdestbase;
	for (v=0 ; v<ctx->numvblocks ; v++)
	{
		ctx->lightleft = ctx->lightptr[0];
		ctx->lightright = ctx->lightptr[1];
		ctx->lightptr += ctx->lightwidth;
		ctx->lightleftstep = (ctx->lightptr[0] - ctx->lightleft) >> shift;
		ctx->lightrightstep = (ctx->lightptr[1] - ctx->lightright) >> shift;
		for (i=0 ; i<block ; i++)
		{
			lighttemp = ctx->lightleft - ctx->lightright;
			lightstep = lighttemp >> shift;
			light = ctx->lightright;
			for (b=block-1; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)[(light & 0xFF00) + pix];
				light += lightstep;
			}
			psource += ctx->sourcetstep;
			ctx->lightright += ctx->lightrightstep;
			ctx->lightleft += ctx->lightleftstep;
			prowdest += ctx->surfrowbytes;
		}
		if (psource >= ctx->sourcemax)
			psource -= ctx->stepback;
	}
}

static void SMP_DrawSurface (smpctx_t *ctx)
{
	unsigned char	*basetptr, *pcolumndest, *source;
	int				smax, tmax, twidth, u, soffset, basetoffset, texwidth, horzblockstep, blocksize, blockdivshift, numhblocks;
	image_t			*mt = ctx->image;

	ctx->surfrowbytes = ctx->rowbytes;
	source = mt->pixels[ctx->surfmip];
	texwidth = mt->width >> ctx->surfmip;
	blocksize = 16 >> ctx->surfmip;
	blockdivshift = 4 - ctx->surfmip;
	ctx->lightwidth = (ctx->surf->extents[0]>>4)+1;
	numhblocks = ctx->surfwidth >> blockdivshift;
	ctx->numvblocks = ctx->surfheight >> blockdivshift;
	horzblockstep = blocksize;
	smax = mt->width >> ctx->surfmip;
	twidth = texwidth;
	tmax = mt->height >> ctx->surfmip;
	ctx->sourcetstep = texwidth;
	ctx->stepback = tmax * twidth;
	ctx->sourcemax = source + (tmax * smax);
	soffset = ctx->surf->texturemins[0];
	basetoffset = ctx->surf->texturemins[1];
	soffset = ((soffset >> ctx->surfmip) + (smax << 16)) % smax;
	basetptr = &source[((((basetoffset >> ctx->surfmip) + (tmax << 16)) % tmax) * twidth)];
	pcolumndest = ctx->surfdat;
	for (u=0 ; u<numhblocks; u++)
	{
		ctx->lightptr = ctx->blocklights + u;
		ctx->prowdestbase = pcolumndest;
		ctx->pbasesource = basetptr + soffset;
		SMP_DrawBlock (ctx, blockdivshift);
		soffset = soffset + blocksize;
		if (soffset >= smax)
			soffset = 0;
		pcolumndest += horzblockstep;
	}
}

static void SMP_FrameConstants (smpctx_t *ctx)
{
	ctx->fullbright = SW_Fullbright ();
	ctx->dynamic = sw_dynamic->value != 0;
	ctx->oldlight = SW_OldLightMode ();
	ctx->lightdata = r_worldmodel && r_worldmodel->lightdata;
	ctx->framecount = r_framecount;
	ctx->modulate = SW_LightScale ();
}

static void SMP_Build (smpctx_t *ctx, msurface_t *surf, int mip, image_t *image, const int *lightadj, byte *dest)
{
	int k;

	ctx->surf = surf;
	ctx->image = image;
	for (k = 0 ; k < MAXLIGHTMAPS ; k++)
		ctx->lightadj[k] = lightadj[k];
	ctx->surfmip = mip;
	ctx->surfwidth = surf->extents[0] >> mip;
	ctx->rowbytes = ctx->surfwidth;
	ctx->surfheight = surf->extents[1] >> mip;
	ctx->surfdat = dest;
	SMP_BuildLightMap (ctx);
	SMP_DrawSurface (ctx);
	if (SW_TintTable () && !ctx->fullbright)	/* BDGN 3.52p: coloured lighting, also on the worker thread */
		R_TintSurface (surf, mip, dest, ctx->rowbytes, ctx->surfwidth, ctx->surfheight, ctx->lightadj);
}

/* one surface with the current frame constants (used by test/smp_surface_test.c) */
void R_SMP_BuildOne (msurface_t *surf, int mip, image_t *image, const int *lightadj, byte *dest)
{
	SMP_FrameConstants (&smp_ctx[0]);
	SMP_Build (&smp_ctx[0], surf, mip, image, lightadj, dest);
}

/* jobs first, first + step, first + 2*step ... */
static void SMP_RunJobs (smpctx_t *ctx, int first, int step)
{
	int i;
	smpjob_t *j;

	for (i = smp_weighted ? 0 : first; i < smp_njobs; i += smp_weighted ? 1 : step)
	{
		j = &smp_jobs[i];
		if (smp_weighted && j->lane != first) continue;
		SMP_Build (ctx, j->surf, j->mip, j->image, j->lightadj, (byte *)j->cache->data);
	}
}

/* ---------------------------------------------------------------- worker thread (Windows) */

#ifdef _WIN32
typedef struct { HANDLE thread, go, done; int lane; } smpworker_t;
static smpworker_t smp_workers[SMP_MAX_THREADS-1];
static volatile LONG	smp_quit;
static unsigned			smp_fpcw;			/* main thread's x87 precision/rounding, copied into the worker */
static int				smp_failed;			/* thread creation failed once: stay single-threaded */

static unsigned __stdcall SMP_Worker (void *unused)
{
	smpworker_t *worker=(smpworker_t *)unused;
	for (;;)
	{
		WaitForSingleObject (worker->go, INFINITE);
		if (smp_quit)
			break;
		_controlfp (smp_fpcw, _MCW_PC | _MCW_RC);
		if (smp_draw_task)
			smp_draw_task(smp_draw_data,worker->lane,smp_threads);
		else
			SMP_RunJobs (&smp_ctx[worker->lane],worker->lane,smp_threads);
		SetEvent (worker->done);
	}
	return 0;
}

static int SMP_Processors (void)
{
	static int	count;
	SYSTEM_INFO	info;
	HMODULE		kernel;
	BOOL		(WINAPI *getmask) (HANDLE, PDWORD_PTR, PDWORD_PTR);
	DWORD_PTR	procmask, sysmask;
	int			n;

	if (count)
		return count;
	memset (&info, 0, sizeof(info));
	GetSystemInfo (&info);
	count = info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
	kernel = GetModuleHandle ("kernel32.dll");
	getmask = kernel ? (BOOL (WINAPI *) (HANDLE, PDWORD_PTR, PDWORD_PTR))GetProcAddress (kernel, "GetProcessAffinityMask") : NULL;
	if (getmask && getmask (GetCurrentProcess (), &procmask, &sysmask))
	{
		for (n = 0 ; procmask ; procmask >>= 1)
			n += (int)(procmask & 1);
		if (n > 0 && n < count)
			count = n;
	}
	return count;
}

static int SMP_StartThreads (int threads)
{
 int i;
 unsigned id;
 if (smp_failed) return 0;
 for(i=0;i<threads-1;i++) {
  smpworker_t *w=&smp_workers[i];
  if(w->thread) continue;
  w->lane=i+1;
  w->go=CreateEvent(NULL,FALSE,FALSE,NULL);
  w->done=CreateEvent(NULL,FALSE,FALSE,NULL);
  if(w->go && w->done)
   w->thread=(HANDLE)_beginthreadex(NULL,0,SMP_Worker,w,0,&id);
  if(!w->thread) {
   if(w->go) CloseHandle(w->go);
   if(w->done) CloseHandle(w->done);
   w->go=w->done=NULL;
   smp_failed=1;
   ri.Con_Printf(PRINT_ALL,"Software SMP: worker creation failed; using serial drawing.\n");
   return 0;
  }
 }
 return 1;
}

/* Cache entries and draw inputs stay stable until every worker returns. */
static void SMP_RunBatch (void)
{
 int i;
 smp_fpcw=_controlfp(0,0);
 for(i=0;i<smp_threads-1;i++) SetEvent(smp_workers[i].go);
 if(smp_draw_task) smp_draw_task(smp_draw_data,0,smp_threads);
 else SMP_RunJobs(&smp_ctx[0],0,smp_threads);
 for(i=0;i<smp_threads-1;i++) WaitForSingleObject(smp_workers[i].done,INFINITE);
}

qboolean R_SMP_DrawFiltered (void (*draw)(void *,int,int), void *data, int threads)
{
 int previous=smp_threads;
 if(threads<2 || threads>SMP_MAX_THREADS || threads>SMP_Processors() || !SMP_StartThreads(threads))
  return false;
 smp_threads=threads;
 smp_draw_task=draw;
 smp_draw_data=data;
 SMP_RunBatch();
 smp_draw_task=NULL;
 smp_draw_data=NULL;
 smp_threads=previous;
 quality_filter_batches++;
 return true;
}

void R_SMP_Shutdown (void)
{
 int i;
 smp_quit=1;
 for(i=0;i<SMP_MAX_THREADS-1;i++)
  if(smp_workers[i].thread) SetEvent(smp_workers[i].go);
 for(i=0;i<SMP_MAX_THREADS-1;i++) {
  smpworker_t *w=&smp_workers[i];
  if(!w->thread) continue;
  WaitForSingleObject(w->thread,INFINITE);
  CloseHandle(w->thread);
  CloseHandle(w->go);
  CloseHandle(w->done);
  memset(w,0,sizeof(*w));
 }
 smp_quit=0;
 smp_failed=0;
}
int R_SMP_Failed (void) { return smp_failed; }	/* BDGN 3.56c: for sw_paths */
#else
/* no threads on other hosts (tests): the worker's share runs inline, with the same split */
static int SMP_Processors (void) { return 1; }
static int SMP_StartThreads (int threads) { return 1; }
qboolean R_SMP_DrawFiltered (void (*draw)(void *,int,int), void *data, int threads) { return false; }
static void SMP_RunBatch (void)
{
	SMP_RunJobs (&smp_ctx[0], 0, 2);
	SMP_RunJobs (&smp_ctx[1], 1, 2);
}
void R_SMP_Shutdown (void) {}
int R_SMP_Failed (void) { return 0; }
#endif

static int SMP_RenderThreads (int requested)
{
 int available=SMP_Processors();
 if(available>SMP_MAX_THREADS) available=SMP_MAX_THREADS;
 if(requested>=2 && requested<available) return requested;
 return available;
}

int R_SMP_FilterThreads (int requested)
{
 int mode=sw_smp?(int)sw_smp->value:SW_SMP_OFF;
 if(mode<=SW_SMP_OFF || mode>=SW_SMP_COUNT ||
    mode==SW_SMP_AUDIO || mode==SW_SMP_FULL_AUDIO) return 1;
 if(!requested) requested=(int)ri.Cvar_Get("sw_smp_threads","0",CVAR_ARCHIVE)->value;
 return SMP_RenderThreads(requested);
}

/* ---------------------------------------------------------------- the pre-pass */

/* the cache of (surf, mip) is out of date: the same test as D_CacheSurface */
static int SMP_NeedsBuild (msurface_t *surf, int mip, image_t *image, const int *lightadj)
{
	surfcache_t *cache = surf->cachespots[mip];

	return !(cache && !cache->dlight && (surf->dlightframe != r_framecount || SW_OldLightMode ()) && cache->image == image
		&& cache->lightadj[0] == lightadj[0] && cache->lightadj[1] == lightadj[1]
		&& cache->lightadj[2] == lightadj[2] && cache->lightadj[3] == lightadj[3]);
}

void R_SMP_Prebuild (void)
{
	surf_t		*s;
	msurface_t	*pface;
	surfcache_t	*cache;
	smpjob_t	*j;
	entity_t	*ent;
	int			i, k, mip, mode, width, height, ok;
	int cost[SMP_MAX_THREADS] = {0, 0, 0, 0};
	int lane, requested;
	unsigned	stamp;
	int			lightadj[MAXLIGHTMAPS];
	image_t		*image;

	r_smp_active = 0;
	mode = sw_smp ? (int)sw_smp->value : 0;
	if (mode <= SW_SMP_OFF || mode >= SW_SMP_COUNT || (mode == SW_SMP_AUDIO || mode == SW_SMP_FULL_AUDIO) ||
	    (mode != SW_SMP_FORCE && SMP_Processors () < 2))
		return;
	requested=mode==SW_SMP_ADAPTIVE || mode==SW_SMP_ADAPTIVE_AUDIO
		?(int)ri.Cvar_Get("sw_smp_threads","0",CVAR_ARCHIVE)->value:2;
	smp_threads=mode==SW_SMP_FORCE?2:SMP_RenderThreads(requested);
	if (!SMP_StartThreads (smp_threads))
		return;

	if (++r_smp_stamp == 0)
		r_smp_stamp = 1;
	stamp = r_smp_stamp;
	smp_njobs = 0;

	/* collect and allocate, in draw order (the same surfaces D_DrawSurfaces gives to D_SolidSurf) */
	for (s = &surfaces[1] ; s < surface_p && smp_njobs < SMP_MAXJOBS ; s++)
	{
		if (!s->spans || !s->msurf)
			continue;
		if ((sw_drawflat_mode->value && !(s->flags & SURF_DRAWBACKGROUND)) || (sw_fastsky->value && (s->flags & SURF_DRAWSKYBOX)))
			continue;
		if (s->flags & (SURF_DRAWSKYBOX|SURF_DRAWBACKGROUND|SURF_DRAWTURB))
			continue;
		pface = s->msurf;
		mip = D_SolidSurfMip (s);
		if (sw_flatfloors->value && pface->plane && (pface->plane->normal[2] > 0.9f || pface->plane->normal[2] < -0.9f ||
		    sw_flatfloors->value >= 3 || (sw_flatfloors->value >= 2 && mip >= 2)))
			continue;	/* BDGN 3.55ai: drawn flat, no cache block needed (same test as D_DrawSurfaces) */
		ent = s->insubmodel ? s->entity : &r_worldentity;
		image = SMP_TextureAnimation (pface->texinfo, ent ? ent->frame : 0);
		for (k = 0 ; k < MAXLIGHTMAPS ; k++)
			lightadj[k] = SW_OldLightMode () == 2 ? 128 : r_newrefdef.lightstyles[pface->styles[k]].white*128;	/* 3.57: Blake Stone ignores light styles */
		cache = pface->cachespots[mip];
		if (cache && cache->smpstamp == stamp)
			continue;		/* already in this batch */
		if (!SMP_NeedsBuild (pface, mip, image, lightadj))
			continue;
		width = pface->extents[0] >> mip;
		height = pface->extents[1] >> mip;
		if (!cache)
		{
			cache = D_SCAlloc (width, width * height);
			pface->cachespots[mip] = cache;
			cache->owner = &pface->cachespots[mip];
			cache->mipscale = 1.0 / (1<<mip);
		}
		cache->dlight = pface->dlightframe == r_framecount && !SW_OldLightMode ();	/* 3.57 */
		cache->image = image;
		for (k = 0 ; k < MAXLIGHTMAPS ; k++)
			cache->lightadj[k] = lightadj[k];
		cache->smpstamp = stamp;
		j = &smp_jobs[smp_njobs++];
		j->surf = pface;
		j->cache = cache;
		j->image = image;
		j->mip = mip;
		j->cost = width * height;
		for (k = 0 ; k < MAXLIGHTMAPS ; k++)
			j->lightadj[k] = lightadj[k];
	}

	/* an allocation later in the batch may have evicted an earlier block: then no thread may write to it */
	ok = smp_njobs >= 2;
	for (i = 0 ; i < smp_njobs && ok ; i++)
		if (smp_jobs[i].surf->cachespots[smp_jobs[i].mip] != smp_jobs[i].cache)
			ok = 0;
	if (!ok)
	{
		for (i = 0 ; i < smp_njobs ; i++)
		{
			j = &smp_jobs[i];
			if (j->surf->cachespots[j->mip] == j->cache)
			{
				j->cache->image = NULL;		/* out of date: the normal path rebuilds it */
				j->cache->smpstamp = 0;
			}
		}
		smp_njobs = 0;
		return;
	}

	smp_weighted = mode >= SW_SMP_BALANCED;
	if (smp_weighted)
		for (i = 0; i < smp_njobs; i++) {
			j = &smp_jobs[i];
			j->lane = mode == SW_SMP_CACHE_WORKER ? 1 : 0;
			if(mode != SW_SMP_CACHE_WORKER)
				for(lane=1;lane<smp_threads;lane++)
					if(cost[lane]<cost[j->lane]) j->lane=lane;
			cost[j->lane] += j->cost;
		}

	SMP_FrameConstants (&smp_ctx[0]);
	for(lane=1;lane<smp_threads;lane++) {
		smp_ctx[lane].fullbright=smp_ctx[0].fullbright;
		smp_ctx[lane].dynamic=smp_ctx[0].dynamic;
		smp_ctx[lane].lightdata=smp_ctx[0].lightdata;
		smp_ctx[lane].framecount=smp_ctx[0].framecount;
		smp_ctx[lane].modulate=smp_ctx[0].modulate;
	}
	/* Small batches cost more to wake than to build inline. */
	if ((mode == SW_SMP_ADAPTIVE || mode == SW_SMP_ADAPTIVE_AUDIO) &&
	    (smp_njobs < 4 || cost[0] + cost[1] + cost[2] + cost[3] < 8192*smp_threads)) {
		smp_weighted = 0;
		SMP_RunJobs (&smp_ctx[0], 0, 1);
	} else {
		SMP_RunBatch ();
		quality_cache_batches++;
	}
	c_surf += smp_njobs;
	BDGN_PROF_N( PROFN_LIGHTMAPS, smp_njobs );	/* BDGN 3.52v */
	r_smp_active = stamp;
}

/* BDGN 3.56c: for sw_paths (r_bdgn.c) */
int R_SMP_Processors (void)
{
	return SMP_Processors ();
}
