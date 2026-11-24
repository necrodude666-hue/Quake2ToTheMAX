/* cl_frametimes.h -- BDGN 3.52 measurement build: bdgn_frametimes <seconds>. One record per drawn frame: time since
   the previous drawn frame (ms, what cl_maxfps paces), time spent drawing (SCR_UpdateScreen, ms) and whether a new
   server frame (10 per second) arrived in that frame. The summary shows whether long frames come with server frames,
   which separates uneven pacing from slow frames (TO DO item 3, jerky animations). No engine code
   (test/perfstats_test.c). VS2005 C. */
#ifndef CL_FRAMETIMES_H
#define CL_FRAMETIMES_H

#include <string.h>

#define FT_MAX		8192	/* about 65 s at 125 fps */
#define FT_BUCKETS	34		/* 0..32 ms, then 33 ms and more */

typedef struct
{
	int				n, dropped;
	short			interval[FT_MAX], render[FT_MAX];
	unsigned char	server[FT_MAX];
	int				interval_us[FT_MAX], render_us[FT_MAX], finish_us[FT_MAX], swap_us[FT_MAX];	/* BDGN 3.57z */
} ft_log_t;

typedef struct
{
	int		n, min, max, over, nserver;
	double	avg, render_avg, avg_server, avg_other;
	int		hist[FT_BUCKETS];
	double	interval_us, render_us, finish_us, swap_us;	/* BDGN 3.57z: averages in microseconds */
} ft_summary_t;

static void FT_Add (ft_log_t *l, int interval, int render, int server)
{
	if (l->n >= FT_MAX)
	{
		l->dropped++;
		return;
	}
	l->interval_us[l->n] = l->render_us[l->n] = l->finish_us[l->n] = l->swap_us[l->n] = -1;
	l->interval[l->n] = (short)(interval > 32767 ? 32767 : interval);
	l->render[l->n] = (short)(render > 32767 ? 32767 : render);
	l->server[l->n] = (unsigned char)(server != 0);
	l->n++;
}

static void FT_Summarize (const ft_log_t *l, ft_summary_t *s)
{
	int i, b;
	double total = 0, rtotal = 0, ts = 0, to = 0;

	memset (s, 0, sizeof(*s));
	s->n = l->n;
	if (!l->n)
		return;
	s->min = s->max = l->interval[0];
	for (i = 0 ; i < l->n ; i++)
	{
		int v = l->interval[i];
		total += v;
		rtotal += l->render[i];
		if (v < s->min) s->min = v;
		if (v > s->max) s->max = v;
		b = v < 0 ? 0 : (v >= FT_BUCKETS - 1 ? FT_BUCKETS - 1 : v);
		s->hist[b]++;
		s->interval_us += l->interval_us[i];
		s->render_us += l->render_us[i];
		s->finish_us += l->finish_us[i];
		s->swap_us += l->swap_us[i];
		if (l->server[i]) { ts += v; s->nserver++; }
		else to += v;
	}
	s->avg = total / l->n;
	s->interval_us /= l->n; s->render_us /= l->n; s->finish_us /= l->n; s->swap_us /= l->n;
	s->render_avg = rtotal / l->n;
	s->avg_server = s->nserver ? ts / s->nserver : 0;
	s->avg_other = l->n - s->nserver ? to / (l->n - s->nserver) : 0;
	for (i = 0 ; i < l->n ; i++)
		if (l->interval[i] > 1.5 * s->avg)
			s->over++;
}

#endif
