/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_sprite.c
#include "r_local.h"

extern polydesc_t r_polydesc;

void R_BuildPolygonFromSurface(msurface_t *fa);
void R_PolygonCalculateGradients (void);

extern void R_PolyChooseSpanletRoutine( float alpha, qboolean isturbulent );

extern vec5_t r_clip_verts[2][MAXWORKINGVERTS+2];

extern void	R_ClipAndDrawPoly( float alpha, qboolean isturbulent, qboolean textured );

/*
** R_DrawSprite
**
** Draw currententity / currentmodel as a single texture
** mapped polygon
*/

/* Reuse the existing software bright-skin colour for the original skin.
 * One bounded scratch cache: no image allocations or ownership changes. */
static byte *R_SimpleEnemyBrightPixels (image_t *mask, image_t *skin)
{
 static byte pixels[256*256];
 static image_t *lastmask;
 static const byte *lastsource;
 static int lastsequence = -1, lastcolor = -1, lastcount = -1;
 int i, count, color;
 count = mask->width*mask->height;
 if (!skin || count <= 0 || count > sizeof(pixels)) return mask->pixels[0];
 color = SW_BrightColor (skin);
 if (lastmask != mask || lastsource != mask->pixels[0] ||
  lastsequence != mask->registration_sequence || lastcolor != color || lastcount != count)
 {
  for (i = 0; i < count; i++) pixels[i] = mask->pixels[0][i] == 255 ? 255 : color;
  lastmask = mask; lastsource = mask->pixels[0];
  lastsequence = mask->registration_sequence; lastcolor = color; lastcount = count;
 }
 return pixels;
}

/* BDGN 3.52r: sprite lighting (cl_spritelight): a 256-entry palette map for the light at the sprite's origin: the
   colormap row a model in the same light gets (as R_AliasSetupLighting), then the coloured-light remap when
   sw_coloredlight is on. Used by R_DrawSpanletOpaque. */
const byte *r_sprite_lightmap;
const byte *R_SpriteLightMap (void)
{
	static byte map[256];
	vec3_t	light;
	float	m;
	int		j, row, i;
	const byte *remap = NULL;
	if ( SW_Fullbright () )
		return NULL;
	R_LightPoint( currententity->origin, light );
	m = light[0] > light[1] ? light[0] : light[1];
	if ( light[2] > m ) m = light[2];
	j = (int)( m * 255 * SW_LightScale () );
	if ( j < 40 ) j = 40;
	if ( j > 255 ) j = 255;
	row = ( ( 255 - j ) << VID_CBITS ) & 0xFF00;
	if ( SW_TintTable () && m > 0 )
		remap = SW_ModelRemap( (int)( light[0]*255/m ), (int)( light[1]*255/m ), (int)( light[2]*255/m ) );
	for ( i = 0; i < 256; i++ )
	{
		byte c = vid.colormap[row + i];
		map[i] = remap ? remap[c] : c;
	}
	map[255] = 255;
	return map;
}

void R_DrawSprite (void)
{
	vec5_t		*pverts;
	vec3_t		left, up, right, down;
	dsprite_t	*s_psprite;
	dsprframe_t	*s_psprframe;
	image_t *image;
	float sx, sy;


	s_psprite = (dsprite_t *)currentmodel->extradata;
#if 0
	if (currententity->frame >= s_psprite->numframes
		|| currententity->frame < 0)
	{
		ri.Con_Printf (PRINT_ALL, "No such sprite frame %i\n", 
			currententity->frame);
		currententity->frame = 0;
	}
#endif
	if (!s_psprite || s_psprite->numframes <= 0) return;
    currententity->frame %= s_psprite->numframes;
    if(currententity->frame < 0) currententity->frame += s_psprite->numframes;

	s_psprframe = &s_psprite->frames[currententity->frame];

	image=currentmodel->skins[currententity->frame];
    if(!image || !image->pixels[0] || image->width <= 0 || image->height <= 0 ||
       s_psprframe->width <= 0 || s_psprframe->height <= 0) return;
    sx=(float)image->width/s_psprframe->width; sy=(float)image->height/s_psprframe->height;
    r_polydesc.pixels = ((currententity->flags & RF_SIMPLEBRIGHT) && currententity->skin) ?
        R_SimpleEnemyBrightPixels (image, currententity->skin) : image->pixels[0];
	r_polydesc.pixel_width  = image->width;
	r_polydesc.pixel_height = image->height;
	r_polydesc.dist         = 0;

	// generate the sprite's axes, completely parallel to the viewplane.
	VectorCopy (vup, r_polydesc.vup);
	VectorCopy (vright, r_polydesc.vright);
	VectorCopy (vpn, r_polydesc.vpn);

// build the sprite poster in worldspace
	VectorScale (r_polydesc.vright, 
		s_psprframe->width - s_psprframe->origin_x, right);
	VectorScale (r_polydesc.vup, 
		s_psprframe->height - s_psprframe->origin_y, up);
	VectorScale (r_polydesc.vright,
		-s_psprframe->origin_x, left);
	VectorScale (r_polydesc.vup,
		-s_psprframe->origin_y, down);

	// invert UP vector for sprites
	VectorInverse( r_polydesc.vup );

	pverts = r_clip_verts[0];

	pverts[0][0] = r_entorigin[0] + up[0] + left[0];
	pverts[0][1] = r_entorigin[1] + up[1] + left[1];
	pverts[0][2] = r_entorigin[2] + up[2] + left[2];
	pverts[0][3] = 0;
	pverts[0][4] = 0;

	pverts[1][0] = r_entorigin[0] + up[0] + right[0];
	pverts[1][1] = r_entorigin[1] + up[1] + right[1];
	pverts[1][2] = r_entorigin[2] + up[2] + right[2];
	pverts[1][3] = image->width;
	pverts[1][4] = 0;

	pverts[2][0] = r_entorigin[0] + down[0] + right[0];
	pverts[2][1] = r_entorigin[1] + down[1] + right[1];
	pverts[2][2] = r_entorigin[2] + down[2] + right[2];
	pverts[2][3] = image->width;
	pverts[2][4] = image->height;

	pverts[3][0] = r_entorigin[0] + down[0] + left[0];
	pverts[3][1] = r_entorigin[1] + down[1] + left[1];
	pverts[3][2] = r_entorigin[2] + down[2] + left[2];
	pverts[3][3] = 0;
	pverts[3][4] = image->height;

	r_polydesc.nump = 4;
	VectorScale(r_polydesc.vright,sx,r_polydesc.vright);
    VectorScale(r_polydesc.vup,sy,r_polydesc.vup);
    r_polydesc.s_offset = s_psprframe->origin_x*sx;
    r_polydesc.t_offset = (s_psprframe->height-s_psprframe->origin_y)*sy;
	VectorCopy( modelorg, r_polydesc.viewer_position );

	r_polydesc.stipple_parity = 1;
	r_sprite_lightmap = ( ( currententity->flags & RF_LITSPRITE ) && !( currententity->flags & RF_SIMPLEBRIGHT ) ) ? R_SpriteLightMap () : NULL;	/* BDGN 3.52r */
	if ( currententity->flags & RF_TRANSLUCENT )
		R_ClipAndDrawPoly ( currententity->alpha, false, true );
	else
		R_ClipAndDrawPoly ( 1.0F, false, true );
	r_sprite_lightmap = NULL;
	r_polydesc.stipple_parity = 0;
}

