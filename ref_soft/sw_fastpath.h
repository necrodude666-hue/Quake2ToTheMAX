/* sw_fastpath.h -- BDGN 3.56c: the low-risk software renderer fixes of REPORT_software_v3.56b.md, as code without the
engine (test/sw_fastpath_test.c). VS2005 C89. Needs SURF_WARP/SURF_FLOWING, VID_CBITS and qcommon/software_smp.h.

- SW_AlphaTurbFlags: R_DrawAlphaSurfaces passed "flags & SURF_WARP|SURF_FLOWING", which C reads as
  "(flags & SURF_WARP) | SURF_FLOWING" - never 0 - so every glass and grate surface took the turbulent spanlets
  (blank turbulence table, two more table reads and a wrap per pixel). Rogue meant water and flowing surfaces only.
- SW_LightmapFinish: R_BuildLightMap's bound/invert loop. 3.52p multiplied every sample by gl_modulate as a float and
  converted back (a float->int call per sample in the VS2005 C_ONLY build); at gl_modulate 1 the integer is used, as in
  id's code. Other values keep the float multiply, so every value stays exactly as in 3.56b.
- SW_TileFill: Draw_TileClear without two divisions and two remainders per pixel (same pixels).
- SW_AsmPaths: which hot paths run as C instead of id's assembly with the current settings, and why (console:
  sw_paths). Several of these settings are saved in config.cfg and change nothing visible.
*/
#ifndef SW_FASTPATH_H
#define SW_FASTPATH_H

#include <string.h>
#include "../qcommon/software_smp.h"

static int SW_AlphaTurbFlags (int flags)
{
	return flags & (SURF_WARP | SURF_FLOWING);
}

static void SW_LightmapFinish (unsigned *blocklights, int size, float modulate)
{
	int		i, t;

	for (i = 0 ; i < size ; i++)
	{
		if (modulate == 1.0f)
			t = (int)blocklights[i];
		else
			t = (int)(blocklights[i] * modulate);
		if (t < 0)
			t = 0;
		t = (255*256 - t) >> (8 - VID_CBITS);
		if (t < (1 << 6))
			t = (1 << 6);
		blocklights[i] = t;
	}
}

/* fill [left,right) x [top,bottom) (all >= 0) with the picture tiled at "scale" screen pixels per picture pixel */
static void SW_TileFill (unsigned char *buffer, int rowbytes, int left, int top, int right, int bottom,
	const unsigned char *pixels, int width, int height, int scale)
{
	int		u, v, col0, sub0, col, sub;
	const unsigned char *row;
	unsigned char *d;

	if (scale < 1 || width <= 0 || height <= 0 || left >= right)
		return;
	col0 = (left / scale) % width;
	sub0 = left % scale;
	for (v = top ; v < bottom ; v++)
	{
		row = pixels + ((v / scale) % height) * width;
		d = buffer + v * rowbytes;
		col = col0;
		sub = sub0;
		for (u = left ; u < right ; u++)
		{
			d[u] = row[col];
			if (++sub == scale)
			{
				sub = 0;
				if (++col == width)
					col = 0;
			}
		}
	}
}

/* paths that can leave id's assembly */
#define SWPATH_WORLD_C		0x01	/* wall/floor pixels and depth spans (r_draw16) */
#define SWPATH_SURF_C		0x02	/* lit surface blocks for the surface cache (r_surf8) */
#define SWPATH_EDGE_C		0x04	/* active edge list (r_edgea) */
#define SWPATH_SPANGEN_C	0x08	/* span generator (r_edgea) */
#define SWPATH_MODEL_C		0x10	/* plain opaque model pixels (r_polysa) */
#define SWPATH_WATER_C		0x20	/* water, slime, lava (r_scana) */
#define SWPATH_CLIP_C		0x40	/* world edge clipping (r_drawa) */
#define SWPATH_ALL_ASM		0x7f
#define SWPATH_TINT			0x80	/* extra coloured-light pass on every surface build */
#define SWPATH_SCALED		0x100	/* the frame is stretched by Windows / DirectDraw (sw_render_scale) */

typedef struct
{
	int		asmspans;		/* sw_asmspans */
	int		prefetch;		/* sw_prefetch */
	int		prefetch_cpu;	/* 1 = 3DNow! PREFETCH, 2 = SSE, 0 = none (SW_PrefetchCpu) */
	int		texfilter;		/* sw_texfilter */
	int		coloredlight;	/* sw_coloredlight */
	int		fullbright;		/* SW_Fullbright (): no tint pass and no model remap */
	int		smp;			/* sw_smp */
	int		smp_failed;		/* the worker threads could not be started: single-threaded */
	int		processors;		/* processors this process may use */
	int		scaled;			/* the presenter stretches the frame (render size != output size) */
	int		draworder;		/* sw_draworder */
} swpaths_in_t;

static void SW_PathsAppend (char *text, int size, const char *s)
{
	int n = (int)strlen (text);
	while (*s && n < size - 1)
		text[n++] = *s++;
	text[n] = 0;
}

/* returns the SWPATH_ bits; text (size > 0) gets one line per path that is not id's assembly */
static int SW_AsmPaths (const swpaths_in_t *in, char *text, int size)
{
	int		p = 0, smp_builds, colour = in->coloredlight && !in->fullbright;

	if (size > 0)
		text[0] = 0;
	smp_builds = in->smp > SW_SMP_OFF && in->smp < SW_SMP_COUNT && in->smp != SW_SMP_AUDIO
		&& in->smp != SW_SMP_FULL_AUDIO && !in->smp_failed && (in->smp == SW_SMP_FORCE || in->processors >= 2);
	if (!in->asmspans)
	{
		p |= SWPATH_ALL_ASM;
		SW_PathsAppend (text, size, "everything below in C: sw_asmspans 0\n");
	}
	else
	{
		if (in->texfilter || (in->prefetch && in->prefetch_cpu))
		{
			p |= SWPATH_WORLD_C;
			SW_PathsAppend (text, size, in->texfilter ? "walls/floors in C: sw_texfilter (filtering)\n"
				: "walls/floors in C: sw_prefetch 1 (test switch, set it to 0)\n");
		}
		if (smp_builds)
		{
			p |= SWPATH_SURF_C;
			SW_PathsAppend (text, size, "surface cache builds in C: sw_smp (threads)\n");
		}
		if (in->draworder)
		{
			p |= SWPATH_SPANGEN_C;
			SW_PathsAppend (text, size, "span generator in C: sw_draworder 1\n");
		}
		if (in->texfilter || colour)
		{
			p |= SWPATH_MODEL_C;
			SW_PathsAppend (text, size, in->texfilter ? "models in C: sw_texfilter\n"
				: "models standing in coloured (not white) light in C: sw_coloredlight 1\n");
		}
	}
	if (colour)
	{
		p |= SWPATH_TINT;
		SW_PathsAppend (text, size, "extra colour pass on every surface build: sw_coloredlight 1\n");
	}
	if (in->scaled)
	{
		p |= SWPATH_SCALED;
		SW_PathsAppend (text, size, "every frame stretched to the screen: render size differs from the screen (sw_render_scale)\n");
	}
	if (!p)
		SW_PathsAppend (text, size, "all id assembly (walls, surfaces, edges, models, water, clipping)\n");
	return p;
}

#endif
