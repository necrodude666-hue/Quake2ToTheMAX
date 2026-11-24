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

#include "r_local.h"
int R_PageInWanted (void);	/* BDGN 3.60b: r_model.c */


#define	MAX_RIMAGES	4096
image_t		r_images[MAX_RIMAGES];
int			numr_images;


/*
===============
R_ImageList_f
===============
*/
void	R_ImageList_f (void)
{
	int		i;
	image_t	*image;
	int		texels;

	ri.Con_Printf (PRINT_ALL, "------------------\n");
	texels = 0;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (image->registration_sequence <= 0)
			continue;
		texels += image->width*image->height;
		switch (image->type)
		{
		case it_skin:
			ri.Con_Printf (PRINT_ALL, "M");
			break;
		case it_sprite:
			ri.Con_Printf (PRINT_ALL, "S");
			break;
		case it_wall:
			ri.Con_Printf (PRINT_ALL, "W");
			break;
		case it_pic:
			ri.Con_Printf (PRINT_ALL, "P");
			break;
		default:
			ri.Con_Printf (PRINT_ALL, " ");
			break;
		}

		ri.Con_Printf (PRINT_ALL,  " %3i %3i : %s\n",
			image->width, image->height, image->name);
	}
	ri.Con_Printf (PRINT_ALL, "Total texel count: %i\n", texels);
}


/*
=================================================================

PCX LOADING

=================================================================
*/

/*
==============
LoadPCX
==============
*/
void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height)
{
	byte	*raw;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;

	*pic = NULL;

	//
	// load the file
	//
	len = ri.FS_LoadFile (filename, (void **)&raw);
	if (!raw)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Bad pcx file %s\n", filename);
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;

    pcx->xmin = LittleShort(pcx->xmin);
    pcx->ymin = LittleShort(pcx->ymin);
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);

	raw = &pcx->data;

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| pcx->xmax >= 640
		|| pcx->ymax >= 480)
	{
		ri.Con_Printf (PRINT_ALL, "Bad pcx file %s\n", filename);
		return;
	}

	out = malloc ( (pcx->ymax+1) * (pcx->xmax+1) );

	*pic = out;

	pix = out;

	if (palette)
	{
		*palette = malloc(768);
		memcpy (*palette, (byte *)pcx + len - 768, 768);
	}

	if (width)
		*width = pcx->xmax+1;
	if (height)
		*height = pcx->ymax+1;

	/* BDGN 3.57u: each row is bytes_per_line bytes (PCX rows of an odd width carry one padding byte) and no write may
	   leave the picture. id's loop ignored bytes_per_line, so a skin of an odd width (77 wide, 78 bytes a row: the
	   simpler models of bdgn_simplemodels.pak) wrote one byte more per row past the end of the malloc'd picture and
	   corrupted the heap: the crash of 3.57l-3.57t with cl_simplemodels/cl_lowspec3d (bdgn_crash.txt: free() from
	   GL_FindImage, LoadPCX, fclose in FS_LoadFile). Same pixels as before for every file id's loop read correctly. */
	{
		int w = pcx->xmax + 1, bpl = pcx->bytes_per_line;
		byte *rawend = (byte *)pcx + (len > 768 + 128 ? len - 768 : len);
		if (bpl < w)
			bpl = w;
		for (y=0 ; y<=pcx->ymax ; y++, pix += w)
		{
			for (x=0 ; x<bpl ; )
			{
				if (raw >= rawend)
				{
					dataByte = 0;
					runLength = bpl - x;
				}
				else
				{
					dataByte = *raw++;
					if((dataByte & 0xC0) == 0xC0)
					{
						runLength = dataByte & 0x3F;
						dataByte = raw < rawend ? *raw++ : 0;
					}
					else
						runLength = 1;
				}
				while(runLength-- > 0 && x < bpl)
				{
					if (x < w)
						pix[x] = dataByte;
					x++;
				}
			}
		}
	}

	if ( raw - (byte *)pcx > len)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "PCX file %s was malformed", filename);
		free (*pic);
		*pic = NULL;
	}

	ri.FS_FreeFile (pcx);
}

/*
=========================================================

TARGA LOADING

=========================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;


/*
=============
LoadTGA
=============
*/
void LoadTGA (char *name, byte **pic, int *width, int *height)
{
	int		columns, rows, numPixels;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	int		length;
	TargaHeader		targa_header;
	byte			*targa_rgba;

	*pic = NULL;

	//
	// load the file
	//
	length = ri.FS_LoadFile (name, (void **)&buffer);
	if (!buffer)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Bad tga file %s\n", name);
		return;
	}

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;
	
	targa_header.colormap_index = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.colormap_length = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.y_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.width = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.height = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type!=2 
		&& targa_header.image_type!=10) 
		ri.Sys_Error (ERR_DROP, "LoadTGA: Only type 2 and 10 targa RGB images supported\n");

	if (targa_header.colormap_type !=0 
		|| (targa_header.pixel_size!=32 && targa_header.pixel_size!=24))
		ri.Sys_Error (ERR_DROP, "LoadTGA: Only 32 or 24 bit images supported (no colormaps)\n");

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = malloc (numPixels*4);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;  // skip TARGA image comment
	
	if (targa_header.image_type==2) {  // Uncompressed, RGB images
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; column++) {
				unsigned char red,green,blue,alphabyte;
				switch (targa_header.pixel_size) {
					case 24:
							
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
					case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
				}
			}
		}
	}
	else if (targa_header.image_type==10) {   // Runlength encoded RGB images
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; ) {
				packetHeader= *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) {        // run-length packet
					switch (targa_header.pixel_size) {
						case 24:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = 255;
								break;
						case 32:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = *buf_p++;
								break;
					}
	
					for(j=0;j<packetSize;j++) {
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if (column==columns) { // run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else {                            // non run-length packet
					for(j=0;j<packetSize;j++) {
						switch (targa_header.pixel_size) {
							case 24:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = 255;
									break;
							case 32:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									alphabyte = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = alphabyte;
									break;
						}
						column++;
						if (column==columns) { // pixel packet run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}						
					}
				}
			}
			breakOut:;
		}
	}

	ri.FS_FreeFile (buffer);
}


//=======================================================

image_t *R_FindFreeImage (void)
{
	image_t		*image;
	int			i;

	// find a free image_t
	for (i=0, image=r_images ; i<numr_images ; i++,image++)
	{
		if (!image->registration_sequence)
			break;
	}
	if (i == numr_images)
	{
		if (numr_images == MAX_RIMAGES)
			ri.Sys_Error (ERR_DROP, "MAX_RIMAGES");
		numr_images++;
	}
	image = &r_images[i];
	memset(image, 0, sizeof(*image)); /* discard colours/mips from a freed image */

	return image;
}

/*
================
GL_LoadPic

================
*/
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type)
{
	image_t		*image;
	int			i, c, b;

	image = R_FindFreeImage ();
	if (strlen(name) >= sizeof(image->name))
		ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);
	strcpy (image->name, name);
	image->registration_sequence = registration_sequence;

	image->width = width;
	image->height = height;
	image->type = type;

	c = width*height;
	image->pixels[0] = malloc (c);
	image->transparent = false;
	for (i=0 ; i<c ; i++)
	{
		b = pic[i];
		if (b == 255)
			image->transparent = true;
		image->pixels[0][i] = b;
	}

	return image;
}

/*
================
R_LoadWal
================
*/
image_t *R_LoadWal (char *name)
{
	miptex_t	*mt;
	int			ofs, length, width, height, pixels, mip, mip_offsets[4], minmip, marker;
	image_t		*image;
	int			size;

	length=ri.FS_LoadFile(name,(void **)&mt);
	if (!mt)
	{
		ri.Con_Printf (PRINT_ALL, "R_LoadWal: can't load %s\n", name);
		return r_notexture_mip;
	}

	if (length<(int)sizeof(*mt)) goto invalid;
	width=LittleLong(mt->width);
	height=LittleLong(mt->height);
	if (width<=0 || height<=0 || (width&7) || (height&7) ||
	    height>((0x7fffffff/340)*64)/width) goto invalid;
	pixels=width*height;
	for (mip=0;mip<4;mip++) {
		ofs=LittleLong(mt->offsets[mip]);
		if (ofs<(int)sizeof(*mt) || ofs>length || (pixels>>(2*mip))>length-ofs)
			goto invalid;
		mip_offsets[mip]=ofs;
	}
	size=pixels+pixels/4+pixels/16+pixels/64;

	image = R_FindFreeImage ();
	strcpy (image->name, name);
	image->width = width;
	image->height = height;
	image->type = it_wall;
	image->registration_sequence = registration_sequence;

	marker = LittleLong(mt->value);
	minmip = marker == 0x32484442 ? 2 : marker == 0x31484442 ? 1 : 0;
	if (minmip)
	{
		int full, x, y;
		byte *src, *dest;

		if (pixels > (0x7fffffff / 2) >> (2 * minmip)) goto alloc_failed;
		image->width = width << minmip;
		image->height = height << minmip;
		full = image->width * image->height;
		if (minmip==2 && ri.Cvar_Get("sw_test_compact_walls","0",CVAR_ARCHIVE)->value)
		{
			image->pixels[0]=malloc(pixels+pixels/4);
			if (!image->pixels[0]) goto alloc_failed;
			image->pixels[2]=image->pixels[0];
			image->pixels[3]=image->pixels[2]+pixels;
			memcpy(image->pixels[2],(byte *)mt+mip_offsets[0],pixels);
			memcpy(image->pixels[3],(byte *)mt+mip_offsets[1],pixels/4);
			image->bdgn_minmip=2;
			image->bdgn_compact=true;
			ri.FS_FreeFile(mt);
			return image;
		}
		size = full + full/4 + full/16 + full/64;
		image->pixels[0] = malloc(size);
		if (!image->pixels[0]) goto alloc_failed;
		for (mip=1; mip<4; mip++)
			image->pixels[mip] = image->pixels[mip-1] + (full >> (2*(mip-1)));
		for (mip=0; mip<minmip; mip++)
		{
			int shift = minmip-mip;
			src = (byte *)mt + mip_offsets[0];
			dest = image->pixels[mip];
			for (y=0; y<(height<<shift); y++)
				for (x=0; x<(width<<shift); x++)
					*dest++ = src[(y>>shift)*width+(x>>shift)];
		}
		for (; mip<4; mip++)
			memcpy(image->pixels[mip], (byte *)mt + mip_offsets[mip-minmip], full >> (2*mip));
		image->bdgn_minmip = minmip;
		ri.FS_FreeFile(mt);
		return image;
	}

	image->pixels[0] = malloc (size);
	if (!image->pixels[0]) goto alloc_failed;
	image->pixels[1] = image->pixels[0] + image->width*image->height;
	image->pixels[2] = image->pixels[1] + image->width*image->height/4;
	image->pixels[3] = image->pixels[2] + image->width*image->height/16;

	ofs = LittleLong (mt->offsets[0]);
	for (mip=0;mip<4;mip++)
		memcpy(image->pixels[mip],(byte *)mt+mip_offsets[mip],pixels>>(2*mip));

	ri.FS_FreeFile ((void *)mt);

	return image;

alloc_failed:
	memset(image,0,sizeof(*image));
	ri.Con_Printf(PRINT_ALL,"Out of memory loading WAL texture: %s\n",name);
	ri.FS_FreeFile(mt);
	return r_notexture_mip;

invalid:
	ri.Con_Printf(PRINT_ALL,"Invalid WAL texture: %s\n",name);
	ri.FS_FreeFile(mt);
	return r_notexture_mip;
}


/* Special surfaces sample the full-size bitmap directly. Expand only when
 * such a surface actually needs this texture. */
void R_ExpandWallMips(image_t *image)
{
	int full, coarse, mip, x, y;
	byte *data, *dest;
	const byte *src;

	if (!image->bdgn_compact) return;
	full=image->width*image->height;
	coarse=full/16;
	data=malloc(full+full/4+coarse+coarse/4);
	if (!data) {
		ri.Sys_Error(ERR_DROP,"Out of memory expanding wall %s",image->name);
		return;
	}
	src=image->pixels[2];
	dest=data;
	for (mip=0;mip<2;mip++) {
		int shift=2-mip;
		image->pixels[mip]=dest;
		for (y=0;y<(image->height>>mip);y++)
			for (x=0;x<(image->width>>mip);x++)
				*dest++=src[(y>>shift)*(image->width>>2)+(x>>shift)];
	}
	memcpy(dest,src,coarse+coarse/4);
	free(image->pixels[2]);
	image->pixels[2]=dest;
	image->pixels[3]=dest+coarse;
	image->bdgn_compact=false;
}

/*
===============
R_FindImage

Finds or loads the given image
===============
*/
image_t	*R_FindImage (char *name, imagetype_t type)
{
	image_t	*image;
	int		i, len;
	byte	*pic, *palette;
	int		width, height;

	if (!name)
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: NULL name");
	len = strlen(name);
	if (len<5)
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: bad name: %s", name);

	// look for it
	for (i=0, image=r_images ; i<numr_images ; i++,image++)
	{
		if (!strcmp(name, image->name))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	}

	//
	// load the pic from disk
	//
	pic = NULL;
	palette = NULL;
	if (!strcmp(name+len-4, ".pcx"))
	{
		LoadPCX (name, &pic, &palette, &width, &height);
		if (!pic)
			return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: can't load %s", name);
		image = GL_LoadPic (name, pic, width, height, type);
	}
	else if (!strcmp(name+len-4, ".wal"))
	{
		image = R_LoadWal (name);
	}
	else if (!strcmp(name+len-4, ".tga"))
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: can't load %s in software renderer", name);
	else
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: bad extension on: %s", name);

	if (pic)
		free(pic);
	if (palette)
		free(palette);

	return image;
}



/*
===============
R_RegisterSkin
===============
*/
struct image_s *R_RegisterSkin (char *name)
{
	return R_FindImage (name, it_skin);
}


/*
================
R_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void R_FreeUnusedImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (image->registration_sequence == registration_sequence)
		{
			if (R_PageInWanted ())	/* BDGN 3.60b: r_model.c */
				Com_PageInMemory(image->pixels[0], (image->width*image->height) >> (image->bdgn_compact?4:0));
			continue;		// used this sequence
		}
		if (!image->registration_sequence)
			continue;		// free texture
		if (image->type == it_pic)
			continue;		// don't free pics
		// free it
		free (image->pixels[0]);	// the other mip levels just follow
		memset (image, 0, sizeof(*image));
	}
}



/*
===============
R_InitImages
===============
*/
void	R_InitImages (void)
{
	registration_sequence = 1;
}

/*
===============
R_ShutdownImages
===============
*/
void	R_ShutdownImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (!image->registration_sequence)
			continue;		// free texture
		// free it
		free (image->pixels[0]);	// the other mip levels just follow
		memset (image, 0, sizeof(*image));
	}
}

