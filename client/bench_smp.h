/* BDGN 3.51p: when bdgn_bench measures software SMP (sw_smp 1). Only the loaded ref_soft.dll of 3.51p or newer has
   sw_smp (sw_bdgn_features >= 3520), and sw_smp 1 does nothing unless Windows gives the game two or more processors.
   Returns NULL when the case is measured, otherwise the reason it is skipped. VS2005 C. */
#ifndef BDGN_BENCH_SMP_H
#define BDGN_BENCH_SMP_H
#define BENCH_SMP_FEATURES 3520
static const char *BenchSmpSkipReason (const char *vid_ref, int sw_features, int processors)
{
	if (!vid_ref || strcmp (vid_ref, "soft"))
		return "software renderer (vid_ref soft) only";
	if (sw_features < BENCH_SMP_FEATURES)
		return "this ref_soft.dll has no sw_smp";
	if (processors < 2)
		return "one processor for the game: sw_smp 1 would change nothing";
	return NULL;
}
#endif
