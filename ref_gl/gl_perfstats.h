/* gl_perfstats.h -- BDGN 3.52 measurement build: where the dynamic light time goes (dlight_stats, bdgn_bench
   DLIGHT_STATS). Counts per drawn frame: lightmaps rebuilt for light styles (animated/flickering lights) and for
   dynamic lights, their uploads (glTexSubImage2D, and whole-block uploads of the path without multitexture) and the
   texels sent, plus CPU cycles (RDTSC) spent building and uploading and in the whole frame. Cycles become
   milliseconds with the clock measured over the same period (Perf_CyclesPerMs). Only counters: nothing is drawn or
   uploaded differently. No engine code (test/perfstats_test.c). VS2005 C. */
#ifndef GL_PERFSTATS_H
#define GL_PERFSTATS_H

#include <stdio.h>

#ifdef _MSC_VER
typedef unsigned __int64	perf_cycles_t;
#else
typedef unsigned long long	perf_cycles_t;
#endif

typedef struct
{
	unsigned		frames;						/* frames begun since zero (counted without a cycle counter too) */
	unsigned		intervals;					/* frame-to-frame periods measured in c_frames */
	unsigned		style_builds, dyn_builds;	/* R_BuildLightMap for light styles / for dynamic lights */
	unsigned		uploads, block_uploads;		/* glTexSubImage2D of one surface / of a whole dynamic block */
	double			texels;
	perf_cycles_t	c_build, c_upload, c_frames;	/* cycles building, uploading, and in whole frames */
	perf_cycles_t	cyc0;						/* cycle counter and clock when counting started */
	unsigned		ms0;
} dls_t;

/* cycles per millisecond between two readings; 0 = unknown (no cycle counter, or under 100 ms measured) */
static double Perf_CyclesPerMs (perf_cycles_t cyc0, perf_cycles_t cyc1, unsigned ms0, unsigned ms1)
{
	if (!cyc0 || cyc1 <= cyc0 || ms1 - ms0 < 100)
		return 0;
	return (double)(cyc1 - cyc0) / (double)(ms1 - ms0);
}

static double DLS_Ms (perf_cycles_t c, unsigned frames, double cpm)
{
	if (cpm <= 0 || !frames)
		return -1;
	return (double)c / cpm / frames;
}

/* one line, values per frame (times -1 when the clock is unknown); out must hold 512 bytes */
static void DLS_Format (const dls_t *s, double cpm, char *out, int size)
{
	double f = s->frames ? (double)s->frames : 1.0;
	/* cycles of one average frame; the shares compare work per frame with it (-1 = no cycle counter) */
	double fr = s->intervals && s->c_frames ? (double)s->c_frames / s->intervals : 0;

	if (size < 512)
	{
		if (size > 0)
			out[0] = 0;
		return;
	}
	sprintf (out, "frames %u style_builds %.1f dyn_builds %.1f uploads %.1f block_uploads %.2f texels %.0f "
		"build_ms %.3f upload_ms %.3f frame_ms %.2f build_pct %.1f upload_pct %.1f mhz %.0f",
		s->frames, s->style_builds / f, s->dyn_builds / f, s->uploads / f, s->block_uploads / f, s->texels / f,
		DLS_Ms (s->c_build, s->frames, cpm), DLS_Ms (s->c_upload, s->frames, cpm), DLS_Ms (s->c_frames, s->intervals, cpm),
		fr > 0 ? 100.0 * (double)s->c_build / f / fr : -1.0, fr > 0 ? 100.0 * (double)s->c_upload / f / fr : -1.0,
		cpm > 0 ? cpm / 1000.0 : 0.0);
}

#endif
