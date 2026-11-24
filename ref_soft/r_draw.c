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

// draw.c

#include "r_local.h"
#include "sw_blit.h"
#include "sw_fastpath.h"	/* BDGN 3.56c */
#include "../client/vid_scale.h"

static int Draw_Scale(void)
{
 int s=sw_scale?(int)sw_scale->value:1;
 return s<1?1:(s>8?8:s);
}


static void Draw_Rect(int *x,int *y,int *w,int *h)
{
 int scale=Draw_Scale();
 int width=sw_present_width?(int)sw_present_width->value:0;
 int height=sw_present_height?(int)sw_present_height->value:0;
 if(width<=0 || height<=0) {
  width=vid.width; height=vid.height;
 }
 VID_RectToRender(x,y,w,h,scale,width/scale,height/scale,width,height,vid.width,vid.height);
}

image_t		*draw_chars;				// 8*8 graphic characters

//=============================================================================

/*
================
Draw_FindPic
================
*/
image_t *Draw_FindPic (char *name)
{
	image_t	*image;
	char	fullname[MAX_QPATH];

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		image = R_FindImage (fullname, it_pic);
	}
	else
		image = R_FindImage (name+1, it_pic);

	return image;
}



/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	draw_chars = Draw_FindPic ("conchars");
}



/*
================
Draw_Char

Draws one 8*8 graphics character
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Char (int x, int y, int num)
{
 int w=8,h=8;
 num &= 255;
 if(num==32 || num==160 || !draw_chars) return;
 Draw_Rect(&x,&y,&w,&h);
 SW_Blit8(vid.buffer,vid.width,vid.height,vid.rowbytes,
  draw_chars->pixels[0]+(num>>4)*8*draw_chars->width+(num&15)*8,
  8,8,draw_chars->width,x,y,w,h,1);
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		*w = *h = -1;
		return;
	}
	*w = gl->width;
	*h = gl->height;
}

/*
=============
Draw_StretchPicImplementation
=============
*/
void Draw_StretchPicImplementation (int x, int y, int w, int h, image_t	*pic)
{
 if(!pic) return;
 Draw_Rect(&x,&y,&w,&h);
 SW_Blit8(vid.buffer,vid.width,vid.height,vid.rowbytes,pic->pixels[0],
  pic->width,pic->height,pic->width,x,y,w,h,pic->transparent);
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *name)
{
	image_t	*pic;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	Draw_StretchPicImplementation (x, y, w, h, pic);
}

/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
 Draw_Rect(&x,&y,&w,&h);
 /* Cinematics use all 256 palette entries; 255 is not transparent here. */
 SW_Blit8(vid.buffer,vid.width,vid.height,vid.rowbytes,data,
  cols,rows,cols,x,y,w,h,0);
}

/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, char *name)
{
 image_t *pic=Draw_FindPic(name);
 if(!pic) { ri.Con_Printf(PRINT_ALL,"Can't find pic: %s\n",name); return; }
 Draw_StretchPicImplementation(x,y,pic->width,pic->height,pic);
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *name)
{
 int scale=Draw_Scale(),left,top,right,bottom;
 image_t *pic=Draw_FindPic(name);
 if(!pic || pic->width<=0 || pic->height<=0 || w<=0 || h<=0) return;
 Draw_Rect(&x,&y,&w,&h);
 left=x<0?0:x; top=y<0?0:y;
 right=x+w>vid.width?vid.width:x+w; bottom=y+h>vid.height?vid.height:y+h;
 /* BDGN 3.56c: was two divisions and two remainders per pixel */
 SW_TileFill(vid.buffer,vid.rowbytes,left,top,right,bottom,pic->pixels[0],pic->width,pic->height,scale);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
 byte color=(byte)c;
 Draw_Rect(&x,&y,&w,&h);
 SW_Blit8(vid.buffer,vid.width,vid.height,vid.rowbytes,&color,1,1,1,
  x,y,w,h,0);
}
//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	int			x,y;
	byte		*pbuf;
	int	t;

	for (y=0 ; y<vid.height ; y++)
	{
		pbuf = (byte *)(vid.buffer + vid.rowbytes*y);
		t = (y & 1) << 1;

		for (x=0 ; x<vid.width ; x++)
		{
			if ((x & 3) != t)
				pbuf[x] = 0;
		}
	}
}
