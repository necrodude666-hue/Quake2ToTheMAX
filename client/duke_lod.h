/*
duke_lod.h -- BDGN 3.51p: distance-based pose rate for the pre-rendered enemies (cl_simpleenemy_lod).

Only the displayed pose clock of CL_DukeEnemyPose changes; position, AI, damage, network and game timing do not.
Near enemies keep the chosen rate (cl_simpleenemy_anim 12.5/6.25 per second; 25 was removed in 3.51q); beyond 512 units they get half,
beyond 1024 a quarter, at most 640 ms per pose. BDGN 3.51s: beyond 2048 a near-still picture: one pose per 2.56 s
(not frozen, so an enemy that dies far away still falls); and the option is on by default (cl_simpleenemy_lod 1 =
automatic). Distance is the depth along the view direction scaled by the field
of view (fov 90 on 4:3 = depth), so zooming in keeps the full rate. An 8 percent band around each boundary keeps an
enemy that stands on it from switching every frame. Fewer pose changes = fewer cache misses, decodes and uploads.
No engine code, so it is tested on its own (test/anim_lod_test.c).
*/
#ifndef DUKE_LOD_H
#define DUKE_LOD_H

#include <math.h>

#define DUKE_LOD_NEAR		512.0f
#define DUKE_LOD_FAR		1024.0f
#define DUKE_LOD_STILL		2048.0f	/* BDGN 3.51s: beyond this a near-still picture */
#define DUKE_LOD_STILLPERIOD	2560
#define DUKE_LOD_TIERS		3
#define DUKE_LOD_BAND		0.08f
#define DUKE_LOD_MAXPERIOD	640

/* fov scale, once per fov (not per enemy): tan(fov_y/2) / 0.75, 0 for no usable fov */
static float CL_DukeLodScale (float fov_y)
{
	if (!(fov_y > 0 && fov_y < 180))
		return 0;
	return (float)tan (fov_y * (3.14159265358979323846 / 360.0)) / 0.75f;
}

/* behind the camera or no usable fov (scale 0): 0 = near (full rate) */
static float CL_DukeLodDistanceScaled (float depth, float scale)
{
	if (!(depth > 0) || !(scale > 0))
		return 0;
	return depth * scale;
}

static float CL_DukeLodDistance (float depth, float fov_y)
{
	return CL_DukeLodDistanceScaled (depth, CL_DukeLodScale (fov_y));
}

/* A pose slot (indexed by entity number) belongs to the same entity only while that entity is in every server frame.
   A gap of 5 or more server frames means a new entity on a reused number (the game waits at least 0.5 s = 5 frames
   before reusing one) or one coming back into view; a smaller frame number means a new map. Then the slot starts
   fresh (no held pose, tier from near). Gaps up to 3 are normal below 10 fps (several frames arrive per drawn frame). */
#define DUKE_POSE_MAXGAP	3
static int CL_DukePoseContinuous (int lastframe, int serverframe)
{
	return serverframe >= lastframe && serverframe - lastframe <= DUKE_POSE_MAXGAP;
}

static int CL_DukeLodTier (float distance, int previous)
{
	static const float edge[DUKE_LOD_TIERS] = { DUKE_LOD_NEAR, DUKE_LOD_FAR, DUKE_LOD_STILL };
	int tier = previous < 0 ? 0 : (previous > DUKE_LOD_TIERS ? DUKE_LOD_TIERS : previous);

	while (tier < DUKE_LOD_TIERS && distance > edge[tier] * (1.0f + DUKE_LOD_BAND))
		tier++;
	while (tier > 0 && distance < edge[tier - 1] * (1.0f - DUKE_LOD_BAND))
		tier--;
	return tier;
}

static int CL_DukePosePeriod (int base, int lod, int tier)
{
	int period;

	if (!lod || tier <= 0)
		return base;
	if (tier >= DUKE_LOD_TIERS)
		return base > DUKE_LOD_STILLPERIOD ? base : DUKE_LOD_STILLPERIOD;	/* BDGN 3.51s: near-still picture */
	period = base << tier;
	return period > DUKE_LOD_MAXPERIOD && base <= DUKE_LOD_MAXPERIOD ? DUKE_LOD_MAXPERIOD : period;
}

#endif
