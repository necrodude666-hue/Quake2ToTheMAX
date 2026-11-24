/*
duke_modes.h -- BDGN 3.51q: one definition of the pre-rendered enemy modes for client, both renderers, menu,
benchmark and tests (3.51p had three slightly different checks: CL_DukeSpriteActive accepted animation 1-2,
CL_DukeEnemyPose 1-3, the renderers any non-zero value).

cl_simpleenemies 3..4        directional sprite modes (4 directions, 4 = bright). BDGN 3.51s: the 8-direction modes
                             (5/6) were removed; a saved 5 becomes 3 and a saved 6 becomes 4, once (Duke_ModeMigrate)
cl_simpleenemy_anim 0        static sprites (bdgn_sprites.pak)
                    1        animated, 12.5 poses per second (80 ms)
                    2        animated, 6.25 poses per second (160 ms)
                    3        3.51p: 25 poses per second. Removed in 3.51q: a saved 3 becomes 1 (12.5) once,
                             with a console note (DUKE_RATE_REMOVED_25 -> DUKE_RATE_12)
cl_simpleenemy_res           removed in 3.52h: the 32x32 library (bdgn_anim32.pak, sprites/anim32/) is the only one;
                             bdgn_anim.pak (64x64, 3.51p) is no longer loaded.
Only header code; no engine dependencies (test/duke_modes_test.c).
*/
#ifndef DUKE_MODES_H
#define DUKE_MODES_H

#define DUKE_RATE_12			1
#define DUKE_RATE_6				2
#define DUKE_RATE_REMOVED_25	3

static int Duke_ModeDirectional (float mode)
{
	int m = (int)mode;
	return m >= 3 && m <= 4;
}

/* BDGN 3.51s: value a saved cl_simpleenemies must be changed to (8 directions removed), or -1 to keep it */
static int Duke_ModeMigrate (float mode)
{
	int m = (int)mode;
	return m == 5 ? 3 : (m == 6 ? 4 : -1);
}

static int Duke_RateValid (float rate)
{
	int r = (int)rate;
	return r == DUKE_RATE_12 || r == DUKE_RATE_6;
}

/* displayed pose period in ms, 0 = not animated */
static int Duke_RatePeriod (float rate)
{
	int r = (int)rate;
	return r == DUKE_RATE_12 ? 80 : (r == DUKE_RATE_6 ? 160 : 0);
}

/* value a saved setting must be changed to, or -1 to keep it */
static int Duke_RateMigrate (float rate)
{
	return (int)rate == DUKE_RATE_REMOVED_25 ? DUKE_RATE_12 : -1;
}

/* BDGN 3.52h: the 64x64 library (bdgn_anim.pak) is no longer used; every tile is 32x32 (bdgn_anim32.pak) */
static int Duke_ResTile (float res)
{
	(void)res;
	return 32;
}

#endif
