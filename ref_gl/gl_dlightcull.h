/*
gl_dlightcull.h -- BDGN 3.51p: which dynamic lights must mark world surfaces (R_PushDlights).

gl_dlight_cull 1 (default): a light whose reach lies completely outside one view frustum plane (or beyond the
draw-distance plane) cannot change a visible lightmap texel, so it is not pushed through the BSP and no lightmap is
rebuilt for it. Same picture. Reach: R_AddDynamicLights measures the in-plane distance in texture units, so on a
surface whose texture mapping stretches by sigma (smallest in-plane singular value of its two texture vectors) a
light of intensity I reaches at most I * max(1, 1/sigma) world units (the plane distance is subtracted linearly from
I). The margin (16-unit lightmap texels, filtered: ~23 texture units) scales the same way.
Per surface (R_DlightSurfReach, once per map in gl_light.c): reach = max(1, 1/sigma), unbounded for a degenerate
mapping. Per light (R_DlightNeed, every frame): the smallest reach a surface needs for the light to touch the view.
A light is not pushed through the BSP when no surface on the map has that much reach (R_DlightMarksView with the map
maximum), and at draw time a surface whose own reach is smaller drops the light (R_DlightFilterSurf): if no light
remains, its lightmap is not rebuilt. Exact in both steps. Model lighting (R_LightPoint) reads the lights directly and is not
affected. gl_dlight_cull 0 = every light, as before.
gl_dlight_maxdist N (default 0 = off): lights farther than N units from the viewer do not light walls and floors.
This changes the picture (optional, for slow CPUs).
No engine code, so it is tested on its own (test/dlightcull_test.c).
*/
#ifndef GL_DLIGHTCULL_H
#define GL_DLIGHTCULL_H

#include <math.h>

#define DLIGHT_VIEW_MARGIN	32.0f
#define DLIGHT_MAX_PLANES	5
#define DLIGHT_MIN_SIGMA	0.05f		/* stretch below this (20x): that surface keeps every light */
#define DLIGHT_REACH_ANY	1e30f		/* reach of a degenerate surface: unbounded */

/* smallest stretch of the texture mapping (vectors v0, v1) inside the plane with normal n; 0 if degenerate */
static float R_DlightTexSigma (const float *v0, const float *v1, const float *normal)
{
	float	n[3], e1[3], e2[3], len, a, b, c, d, sum, det, disc;
	int		k, axis = 0;

	len = (float)sqrt (normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
	if (!(len > 0))
		return 0;
	for (k = 0 ; k < 3 ; k++)
		n[k] = normal[k] / len;
	for (k = 1 ; k < 3 ; k++)		/* axis least aligned with the normal */
		if (fabs (n[k]) < fabs (n[axis]))
			axis = k;
	e1[0] = axis == 0 ? 0 : (axis == 1 ? n[2] : -n[1]);	/* e1 = n x unit(axis) */
	e1[1] = axis == 0 ? -n[2] : (axis == 1 ? 0 : n[0]);
	e1[2] = axis == 0 ? n[1] : (axis == 1 ? -n[0] : 0);
	len = (float)sqrt (e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
	for (k = 0 ; k < 3 ; k++)
		e1[k] /= len;
	e2[0] = n[1] * e1[2] - n[2] * e1[1];
	e2[1] = n[2] * e1[0] - n[0] * e1[2];
	e2[2] = n[0] * e1[1] - n[1] * e1[0];
	a = v0[0] * e1[0] + v0[1] * e1[1] + v0[2] * e1[2];
	b = v0[0] * e2[0] + v0[1] * e2[1] + v0[2] * e2[2];
	c = v1[0] * e1[0] + v1[1] * e1[1] + v1[2] * e1[2];
	d = v1[0] * e2[0] + v1[1] * e2[1] + v1[2] * e2[2];
	sum = a * a + b * b + c * c + d * d;
	det = a * d - b * c;
	disc = sum * sum - 4 * det * det;
	if (disc < 0)
		disc = 0;
	sum = (sum - (float)sqrt (disc)) * 0.5f;
	return sum > 0 ? (float)sqrt (sum) : 0;
}

/* reach factor of one surface: max(1, 1/sigma); DLIGHT_REACH_ANY for a degenerate mapping */
static float R_DlightSurfReach (float sigma)
{
	if (!(sigma >= DLIGHT_MIN_SIGMA))
		return DLIGHT_REACH_ANY;
	return sigma >= 1 ? 1.0f : 1.0f / sigma;
}

/* smallest surface reach this light needs to change a visible texel: 0 when its centre is inside every plane */
static float R_DlightNeed (const float *origin, float radius, const float (*normals)[3], const float *dists, int planes)
{
	int		i;
	float	d, k, need = 0;

	if (planes > DLIGHT_MAX_PLANES)
		planes = DLIGHT_MAX_PLANES;
	for (i = 0 ; i < planes ; i++)
	{
		d = origin[0] * normals[i][0] + origin[1] * normals[i][1] + origin[2] * normals[i][2] - dists[i];
		if (d < 0)
		{
			k = -d / (radius + DLIGHT_VIEW_MARGIN);
			if (k > need)
				need = k;
		}
	}
	return need;
}

/* a surface keeps a light when its own reach is at least what the light needs */
static int R_DlightSurfKeeps (float need, float surfreach)
{
	return need <= surfreach;
}

static int R_DlightMarksView (const float *origin, float radius, float reach, const float (*normals)[3], const float *dists, int planes,
							  const float *vieworg, int cull, float maxdist)
{
	float	dx, dy, dz;

	if (maxdist > 0)
	{
		dx = origin[0] - vieworg[0];
		dy = origin[1] - vieworg[1];
		dz = origin[2] - vieworg[2];
		if (dx * dx + dy * dy + dz * dz > maxdist * maxdist)
			return 0;
	}
	if (!cull || !(reach > 0) || reach >= DLIGHT_REACH_ANY)
		return 1;		/* culling off, or some surface on the map has unbounded reach */
	return R_DlightSurfKeeps (R_DlightNeed (origin, radius, normals, dists, planes), reach);
}

#endif
