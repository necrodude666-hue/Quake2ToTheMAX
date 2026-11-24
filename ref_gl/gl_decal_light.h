/* gl_decal_light.h -- BDGN 3.56b: how bright a blood mark is drawn (gl_decal.c, mode 2 = blood painted in its colour).

The mark is painted over a wall or floor whose brightness is texture x lightmap. To match it, the blood is drawn at
the wall's light: the world light at the mark (R_LightPointWorld: the lightmap sample below the point with gl_modulate
and the light styles, no dynamic lights), with a small minimum so a mark in a dark corner is still a dark red.
3.55ae-3.56a: 0.44 + 0.7 x R_LightPoint (that function applies gl_modulate a second time, id's model lighting, and
added the dynamic lights measured from the last drawn entity; the value was kept for the mark's whole life), so blood
stayed bright in dark rooms. No engine code (test/decal_light_test.c). VS2005 C. */
#ifndef GL_DECAL_LIGHT_H
#define GL_DECAL_LIGHT_H

#define DECAL_BLOOD_MIN	0.12f	/* a dark red even in a black corner */

/* colour factor of the blood: follows the wall's light, never above 1, never below the small minimum */
static float R_DecalBloodLight (float light)
{
	if (light < DECAL_BLOOD_MIN)
		return DECAL_BLOOD_MIN;
	if (light > 1.0f)
		return 1.0f;
	return light;
}

/* The light of one surface at texture coordinates (s, t) (DotProduct with the texinfo vecs + offset, truncated):
   id's RecursiveLightPoint for that surface - the lightmap texel, every light style of the surface, gl_modulate once.
   samples: 3 bytes per texel, one block per style ((extents>>4)+1 texels each way); styles end at 255; stylergb[k] =
   the colour of light style styles[k]. Outside the surface the nearest edge texel is used and 0 is returned (1 inside).
   3.56b, Codex review: blood marks read the light of the surface they lie on, every frame (follows flickering lights
   and gl_modulate); the downward trace of R_LightPoint gave a mark on a wall the light of the floor below it. */
static int R_LightmapTexel (const unsigned char *samples, const short *texturemins, const short *extents,
	const unsigned char *styles, const float *const *stylergb, float modulate, int s, int t, float out[3])
{
	int smax = (extents[0] >> 4) + 1, tmax = (extents[1] >> 4) + 1, ds, dt, inside = 1, maps;
	const unsigned char *lm;

	out[0] = out[1] = out[2] = 0;
	ds = s - texturemins[0];
	dt = t - texturemins[1];
	if (ds < 0 || dt < 0 || ds > extents[0] || dt > extents[1])
		inside = 0;
	if (!samples)
		return inside;
	ds >>= 4;
	dt >>= 4;
	if (ds < 0) ds = 0;
	if (dt < 0) dt = 0;
	if (ds > smax - 1) ds = smax - 1;
	if (dt > tmax - 1) dt = tmax - 1;
	lm = samples + 3 * (dt * smax + ds);
	for (maps = 0 ; maps < 4 && styles[maps] != 255 ; maps++)
	{
		const float *rgb = stylergb[maps];
		out[0] += lm[0] * modulate * rgb[0] * (1.0f / 255);
		out[1] += lm[1] * modulate * rgb[1] * (1.0f / 255);
		out[2] += lm[2] * modulate * rgb[2] * (1.0f / 255);
		lm += 3 * smax * tmax;
	}
	return inside;
}

#endif
