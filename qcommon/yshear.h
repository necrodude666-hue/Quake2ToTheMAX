/*
yshear.h -- BDGN 3.60i (owner 2026-11-23): the Duke Nukem 3D view (cl_yshear 1, "Duke 3D view" and Duke Nukem 3D mode,
menu ANIMATIONS under Wolfenstein 3D mode). Included by ref_gl and ref_soft.

The Build engine (Duke Nukem 3D, Shadow Warrior, Blood) never tilted its camera. Looking up or down moved the horizon on
the screen instead ("y-shearing"): walls stay vertical, floors and ceilings keep one depth per screen row (that is why
Build could draw them with straight steps, see ref_soft sw_buildfloors), and the picture stretches as you look further
up or down. Here the view is drawn with the pitch at 0 and the projection centre moved by tan(pitch): the ray through the
middle of the screen still has the real pitch, so the crosshair stays where the shots go. The client keeps the pitch
inside the screen (client/cl_input.c), as Build did; the view weapon is drawn without the shift (it stays put, as the 2D
weapons of Duke Nukem 3D).

R_YShearTake: 0 when the view is off (or for a view without a world, the player setup model); otherwise the tangent of
the pitch (positive = looking down, as Quake II's pitch), clamped to 95 % of the half screen, with the pitch in
angles[] set to 0. TEORETISK ANALYS: not run in the game.
*/
#ifndef BDGN_YSHEAR_H
#define BDGN_YSHEAR_H

static cvar_t *r_yshear_cv;

static float R_YShearTake (float *angles, float fov_y, int rdflags)
{
	float p, t, lim;
	if (!r_yshear_cv)
		r_yshear_cv = ri.Cvar_Get ("cl_yshear", "0", CVAR_ARCHIVE);
	if (!r_yshear_cv->value || (rdflags & RDF_NOWORLDMODEL) || fov_y <= 0 || fov_y >= 179)
		return 0;
	p = angles[0];
	if (p > 180)
		p -= 360;
	if (p < -180)
		p += 360;
	t = (float)tan (p * M_PI / 180.0);
	lim = 0.95f * (float)tan (fov_y * M_PI / 360.0);
	if (t > lim)
		t = lim;
	if (t < -lim)
		t = -lim;
	angles[0] = 0;
	return t;
}

#endif
